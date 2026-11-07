#include "Zamin/Renderer/Model.h"
#include "Zamin/Renderer/Texture.h"
#include "Zamin/Asset/AssetImportLog.h"
#include "Zamin/Asset/AssetImporter.h"
#include "Zamin/Core/Log.h"
#include "Zamin/Math/Color.h"
#include "Zamin/Asset/AssetRegistry.h"
#include "Zamin/Project/AssetFiles.h"
#include "Zamin/Project/Project.h"
#include "Zamin/Renderer/MaterialAsset.h"
#include "Zamin/Utils/FileSystem.h"

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <unordered_map>
#include <unordered_set>

namespace Zamin {

    namespace fs = std::filesystem;

    // ------------------------------------------------------------------ import settings
    glm::mat4 ModelImportSettings::GetTransform() const
    {
        glm::mat4 transform = glm::scale(glm::mat4(1.0f), glm::vec3(Scale));
        if (UpAxis == ModelUpAxis::Z)
            transform = glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f)) * transform; // +Z -> +Y
        return transform;
    }

    void ApplyImportTransform(Skeleton& skeleton, const glm::mat4& transform)
    {
        // Skinning matrix of a joint: global * inverseBind. With vertices v' = T v the matrices
        // become T * global * inverseBind * T^-1, i.e. root' = T * root and inverseBind' = inverseBind * T^-1.
        glm::mat4 inverse = glm::inverse(transform);
        skeleton.RootTransform = transform * skeleton.RootTransform;
        for (Joint& joint : skeleton.Joints)
            joint.InverseBind = joint.InverseBind * inverse;
    }

    void ApplyClipSettings(std::vector<AnimationClip>& clips, const std::vector<ModelClipSettings>& settings, bool keepSources,
                           std::vector<std::string>* warnings)
    {
        if (settings.empty())
            return;
        const std::vector<AnimationClip> sources = clips;
        auto findSource = [&](const std::string& name) -> const AnimationClip* {
            for (const AnimationClip& c : sources)
                if (c.Name == name)
                    return &c;
            return nullptr;
        };
        auto warn = [&](std::string text) {
            if (warnings)
                warnings->push_back(std::move(text));
        };
        // The names the entries make (the first entry of a name wins).
        std::vector<std::string> names;
        std::vector<const ModelClipSettings*> entries;
        for (const ModelClipSettings& s : settings)
        {
            if (s.Source.empty())
                continue;
            if (!findSource(s.Source))
            {
                warn(std::format("'{}' klipi faylda yo'q - '{}' yaratilmadi", s.Source, s.GetName()));
                continue;
            }
            if (std::find(names.begin(), names.end(), s.GetName()) != names.end())
            {
                warn(std::format("'{}' nomli klip ikki marta - ikkinchisi tashlab ketildi", s.GetName()));
                continue;
            }
            // A sub-clip may not take the name of another file clip (it would hide it).
            if (s.GetName() != s.Source && findSource(s.GetName()))
            {
                warn(std::format("'{}' nomi fayldagi boshqa klipniki - '{}' dan kesilgan klip yaratilmadi", s.GetName(), s.Source));
                continue;
            }
            names.push_back(s.GetName());
            entries.push_back(&s);
        }
        std::vector<AnimationClip> out;
        out.reserve(sources.size() + entries.size());
        // The file's clips in their order: trimmed in place, kept, or dropped.
        for (const AnimationClip& c : sources)
        {
            const ModelClipSettings* same = nullptr;
            bool used = false;
            for (const ModelClipSettings* e : entries)
            {
                used |= e->Source == c.Name;
                if (e->Source == c.Name && e->GetName() == c.Name)
                    same = e;
            }
            if (same)
                out.push_back(TrimClip(c, same->Start, same->End, c.Name));
            else if (!used || keepSources)
                out.push_back(c);
        }
        // Then the sub-clips with names of their own, in the list's order.
        for (const ModelClipSettings* e : entries)
            if (e->GetName() != e->Source)
                out.push_back(TrimClip(*findSource(e->Source), e->Start, e->End, e->GetName()));
        clips = std::move(out);
    }

    // ------------------------------------------------------------------ loading
    class ModelLoader
    {
    public:
        // A new model (nullptr with the reason in `error` when the file cannot be read).
        static Ref<Model> LoadFile(const fs::path& path, const ModelImportSettings& settings, std::string& error);
        static Ref<Model> LoadGLTF(const fs::path& path, const ModelImportSettings& settings, std::string& error);
        static Ref<Model> LoadOBJ(const fs::path& path, const ModelImportSettings& settings, std::string& error);
        // Moves the content of `from` into `to` (the object everything holds stays the same).
        static void Replace(Model& to, Model&& from);
    };

    static std::unordered_map<std::string, Ref<Model>> s_ModelCache;

    static const char* CgltfResultText(cgltf_result result)
    {
        switch (result)
        {
            case cgltf_result_data_too_short:   return "fayl qisqa (buzilgan?)";
            case cgltf_result_unknown_format:   return "noma'lum format";
            case cgltf_result_invalid_json:     return "JSON xato";
            case cgltf_result_invalid_gltf:     return "glTF tuzilishi xato";
            case cgltf_result_invalid_options:  return "noto'g'ri sozlamalar";
            case cgltf_result_file_not_found:   return "fayl topilmadi";
            case cgltf_result_io_error:         return "o'qish xatosi";
            case cgltf_result_out_of_memory:    return "xotira yetmadi";
            case cgltf_result_legacy_gltf:      return "eski glTF 1.0 qo'llab-quvvatlanmaydi";
            default:                            return "noma'lum xato";
        }
    }

    static Ref<Texture2D> LoadGLTFTexture(const cgltf_texture_view& view, const std::filesystem::path& baseDir,
                                          std::unordered_map<const cgltf_image*, Ref<Texture2D>>& cache)
    {
        if (!view.texture || !view.texture->image) return nullptr;
        const cgltf_image* image = view.texture->image;
        if (auto it = cache.find(image); it != cache.end()) return it->second;

        Ref<Texture2D> tex;
        if (image->buffer_view)
        {
            const uint8_t* data = (const uint8_t*)image->buffer_view->buffer->data + image->buffer_view->offset;
            tex = Texture2D::CreateFromMemory(data, image->buffer_view->size, image->name ? image->name : "embedded");
        }
        else if (image->uri && std::string_view(image->uri).starts_with("data:"))
        {
            // base64 data URI
            const char* comma = std::strchr(image->uri, ',');
            if (comma)
            {
                std::string b64 = comma + 1;
                size_t size = 3 * b64.size() / 4;
                void* out = nullptr;
                cgltf_options opts{};
                size_t padding = (b64.size() >= 2 && b64[b64.size() - 1] == '=') + (b64.size() >= 2 && b64[b64.size() - 2] == '=');
                size -= padding;
                if (cgltf_load_buffer_base64(&opts, size, b64.c_str(), &out) == cgltf_result_success)
                {
                    tex = Texture2D::CreateFromMemory(out, size, "datauri");
                    std::free(out);
                }
            }
        }
        else if (image->uri)
        {
            std::string uri = image->uri;
            cgltf_decode_uri(uri.data());
            uri.resize(std::strlen(uri.c_str()));
            Ref<Texture2D> loaded = TextureLibrary::Load(baseDir / uri);
            tex = loaded;
            // glTF has top-left UV origin; files loaded through TextureLibrary are flipped, so
            // we flip V in the mesh instead (see LoadGLTF).
        }
        cache[image] = tex;
        return tex;
    }

    // The values and modes of a glTF material (its textures are loaded separately). glTF factors
    // are linear; our material colors are sRGB-authored.
    static void ReadGLTFMaterial(const cgltf_material& m, Material& out)
    {
        if (m.has_pbr_metallic_roughness)
        {
            const auto& pbr = m.pbr_metallic_roughness;
            out.Albedo = glm::vec4(Color::LinearToGamma(glm::vec3(pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2])),
                                   pbr.base_color_factor[3]);
            out.Metallic = pbr.metallic_factor;
            out.Roughness = pbr.roughness_factor;
        }
        out.Emission = { m.emissive_factor[0], m.emissive_factor[1], m.emissive_factor[2] };
        if (glm::length(out.Emission) > 0.0f)
            out.EmissionStrength = m.has_emissive_strength ? m.emissive_strength.emissive_strength : 1.0f;
        switch (m.alpha_mode)
        {
            case cgltf_alpha_mode_mask:
                out.BlendMode = MaterialBlendMode::Cutout;
                out.AlphaCutoff = std::clamp(m.alpha_cutoff, 0.0f, 1.0f);
                break;
            case cgltf_alpha_mode_blend:
                out.BlendMode = MaterialBlendMode::Transparent;
                out.PreserveSpecular = false; // glTF BLEND: the whole surface is composited "over" by alpha
                break;
            default:
                out.BlendMode = MaterialBlendMode::Opaque;
                break;
        }
        out.DoubleSided = m.double_sided;
        out.Shader = m.unlit ? MaterialShader::Unlit : MaterialShader::Lit; // KHR_materials_unlit
    }

    // ------------------------------------------------------------------ skins
    static bool NearlyEqual(const glm::mat4& a, const glm::mat4& b)
    {
        for (int c = 0; c < 4; c++)
            for (int r = 0; r < 4; r++)
                if (std::abs(a[c][r] - b[c][r]) > 1e-5f * std::max(1.0f, std::max(std::abs(a[c][r]), std::abs(b[c][r]))))
                    return false;
        return true;
    }

    void PrepareModelSkin(ModelSkin& skin, const Skeleton& skeleton, bool keepMode)
    {
        skin.InverseBind.resize(skin.Joints.size(), glm::mat4(1.0f));
        const size_t n = skeleton.Joints.size();
        if (!keepMode)
        {
            skin.Direct = true;
            for (size_t k = 0; k < skin.Joints.size() && skin.Direct; k++)
            {
                const int u = skin.Joints[k];
                skin.Direct = u >= 0 && (size_t)u < n && u < MaxJoints && NearlyEqual(skin.InverseBind[k], skeleton.Joints[(size_t)u].InverseBind);
            }
        }
        skin.Correction.clear();
        if (skin.Direct)
            return;
        skin.Correction.resize(skin.Joints.size(), glm::mat4(1.0f));
        for (size_t k = 0; k < skin.Joints.size(); k++)
        {
            const int u = skin.Joints[k];
            if (u >= 0 && (size_t)u < n)
                skin.Correction[k] = glm::inverse(skeleton.Joints[(size_t)u].InverseBind) * skin.InverseBind[k];
        }
    }

    const std::vector<glm::mat4>& GetSkinMatrices(const ModelSkin& skin, const std::vector<glm::mat4>& skeletonMatrices,
                                                  std::vector<glm::mat4>& scratch)
    {
        if (skin.Direct || skin.Correction.size() != skin.Joints.size())
            return skeletonMatrices;
        const size_t count = std::min(skin.Joints.size(), (size_t)MaxJoints);
        scratch.resize(count);
        for (size_t k = 0; k < count; k++)
        {
            const int u = skin.Joints[k];
            const glm::mat4& joint = u >= 0 && (size_t)u < skeletonMatrices.size() ? skeletonMatrices[(size_t)u] : glm::mat4(1.0f);
            scratch[k] = joint * skin.Correction[k];
        }
        return scratch;
    }

    void FitJointInfluences(glm::ivec4& joints, glm::vec4& weights)
    {
        float sum = 0.0f;
        for (int k = 0; k < 4; k++)
        {
            if (joints[k] < 0 || joints[k] >= MaxJoints || !std::isfinite(weights[k]))
            {
                joints[k] = 0;
                weights[k] = 0.0f;
            }
            sum += weights[k];
        }
        if (sum > 0.0f)
            weights /= sum;
        else
            weights = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f); // all on the first (joint 0 if it was past the palette)
    }

    const std::vector<glm::mat4>& Model::GetSubMeshJoints(size_t index, const std::vector<glm::mat4>& skeletonMatrices,
                                                          std::vector<glm::mat4>& scratch) const
    {
        if (index >= m_SubMeshes.size())
            return skeletonMatrices;
        const int skin = m_SubMeshes[index].Skin;
        if (skin < 0 || (size_t)skin >= m_Skins.size())
            return skeletonMatrices;
        return GetSkinMatrices(m_Skins[(size_t)skin], skeletonMatrices, scratch);
    }

    // The joints of every skin of a glTF file as one skeleton (each node once, parents before their
    // children, the skins' joints in their order) and each skin's own joint list. Root joints whose
    // (non-joint) parents are placed differently - two armatures - take those parents into the
    // skeleton as joints of their own (weighted by no vertex), so every root stays where the file
    // puts it; when they share the placement it is the skeleton's RootTransform, as with one skin.
    static void BuildGLTFSkeleton(const cgltf_data* data, Skeleton& sk, std::vector<ModelSkin>& skins,
                                  std::unordered_map<const cgltf_node*, int>& jointIndex)
    {
        sk = {};
        skins.clear();
        jointIndex.clear();
        std::unordered_set<const cgltf_node*> members;
        for (cgltf_size s = 0; s < data->skins_count; s++)
            for (cgltf_size i = 0; i < data->skins[s].joints_count; i++)
                if (data->skins[s].joints[i])
                    members.insert(data->skins[s].joints[i]);
        if (members.empty())
            return;
        auto memberParent = [&](const cgltf_node* n) -> const cgltf_node* {
            for (const cgltf_node* p = n->parent; p; p = p->parent)
                if (members.count(p))
                    return p;
            return nullptr;
        };
        auto parentWorld = [](const cgltf_node* n) {
            if (!n->parent)
                return glm::mat4(1.0f);
            float m[16];
            cgltf_node_transform_world(n->parent, m);
            return glm::make_mat4(m);
        };
        std::vector<const cgltf_node*> roots;
        for (cgltf_size s = 0; s < data->skins_count; s++)
            for (cgltf_size i = 0; i < data->skins[s].joints_count; i++)
            {
                const cgltf_node* n = data->skins[s].joints[i];
                if (n && !memberParent(n) && std::find(roots.begin(), roots.end(), n) == roots.end())
                    roots.push_back(n);
            }
        glm::mat4 rootWorld = roots.empty() ? glm::mat4(1.0f) : parentWorld(roots[0]);
        bool shared = true;
        for (const cgltf_node* r : roots)
            shared &= NearlyEqual(parentWorld(r), rootWorld);
        if (!shared)
        {
            for (const cgltf_node* r : roots)
                for (const cgltf_node* p = r->parent; p; p = p->parent)
                    members.insert(p);
            rootWorld = glm::mat4(1.0f);
        }

        // Parents first; the skins' joints in their order.
        std::vector<const cgltf_node*> ordered;
        std::unordered_set<const cgltf_node*> placed;
        std::function<void(const cgltf_node*)> add = [&](const cgltf_node* n) {
            if (placed.count(n))
                return;
            if (const cgltf_node* p = memberParent(n))
                add(p);
            placed.insert(n);
            ordered.push_back(n);
        };
        for (cgltf_size s = 0; s < data->skins_count; s++)
            for (cgltf_size i = 0; i < data->skins[s].joints_count; i++)
                if (data->skins[s].joints[i])
                    add(data->skins[s].joints[i]);
        for (size_t i = 0; i < ordered.size(); i++)
            jointIndex[ordered[i]] = (int)i;

        sk.Joints.resize(ordered.size());
        for (size_t i = 0; i < ordered.size(); i++)
        {
            const cgltf_node* n = ordered[i];
            Joint& j = sk.Joints[i];
            j.Name = n->name ? n->name : ("joint" + std::to_string(i));
            const cgltf_node* p = memberParent(n);
            j.Parent = p ? jointIndex[p] : -1;
            if (n->has_translation) j.RestTranslation = { n->translation[0], n->translation[1], n->translation[2] };
            if (n->has_rotation) j.RestRotation = glm::quat(n->rotation[3], n->rotation[0], n->rotation[1], n->rotation[2]);
            if (n->has_scale) j.RestScale = { n->scale[0], n->scale[1], n->scale[2] };
            if (n->has_matrix)
            {
                // A node placed by a matrix (armature nodes often are): its translation, rotation
                // and scale are what the joint rests at.
                const glm::mat4 m = glm::make_mat4(n->matrix);
                glm::vec3 scale(glm::length(glm::vec3(m[0])), glm::length(glm::vec3(m[1])), glm::length(glm::vec3(m[2])));
                glm::mat3 rotation(1.0f);
                for (int c = 0; c < 3; c++)
                    rotation[c] = scale[c] > 1e-8f ? glm::vec3(m[c]) / scale[c] : glm::vec3(0.0f);
                if (glm::determinant(rotation) < 0.0f) // mirrored: the flip goes into the scale
                {
                    scale.x = -scale.x;
                    rotation[0] = -rotation[0];
                }
                j.RestTranslation = glm::vec3(m[3]);
                j.RestRotation = glm::normalize(glm::quat_cast(rotation));
                j.RestScale = scale;
            }
        }
        sk.RootTransform = rootWorld;

        // Each skin: its joints; the skeleton's inverse binds are the first skin's that lists the joint.
        std::vector<bool> bound(ordered.size(), false);
        for (cgltf_size s = 0; s < data->skins_count; s++)
        {
            const cgltf_skin& skin = data->skins[s];
            ModelSkin out;
            out.Name = skin.name && *skin.name ? skin.name : std::format("Skin {}", s);
            for (cgltf_size i = 0; i < skin.joints_count; i++)
            {
                const int u = skin.joints[i] ? jointIndex[skin.joints[i]] : 0;
                float m[16];
                glm::mat4 inverseBind(1.0f);
                if (skin.inverse_bind_matrices && cgltf_accessor_read_float(skin.inverse_bind_matrices, i, m, 16))
                    inverseBind = glm::make_mat4(m);
                out.Joints.push_back(u);
                out.InverseBind.push_back(inverseBind);
                if (!bound[(size_t)u])
                {
                    sk.Joints[(size_t)u].InverseBind = inverseBind;
                    bound[(size_t)u] = true;
                }
            }
            skins.push_back(std::move(out));
        }
        for (ModelSkin& skin : skins)
            PrepareModelSkin(skin, sk);
    }

    // The import transform for the skeleton and the skins (see ApplyImportTransform); the skins'
    // palettes are made again from the moved inverse binds.
    static void FinishGLTFSkins(Skeleton& sk, std::vector<ModelSkin>& skins, const glm::mat4& importTransform)
    {
        if (sk.Empty())
            return;
        ApplyImportTransform(sk, importTransform);
        const glm::mat4 inverse = glm::inverse(importTransform);
        for (ModelSkin& skin : skins)
        {
            for (glm::mat4& m : skin.InverseBind)
                m = m * inverse;
            PrepareModelSkin(skin, sk, true);
        }
    }

    bool Model::ReadSkins(const fs::path& path, Skeleton& skeleton, std::vector<ModelSkin>& skins, std::string* error)
    {
        skeleton = {};
        skins.clear();
        cgltf_options options{};
        cgltf_data* data = nullptr;
        std::string file = path.string();
        if (cgltf_result result = cgltf_parse_file(&options, file.c_str(), &data); result != cgltf_result_success)
        {
            if (error) *error = CgltfResultText(result);
            return false;
        }
        if (cgltf_result result = cgltf_load_buffers(&options, data, file.c_str()); result != cgltf_result_success)
        {
            if (error) *error = std::format("buferlar yuklanmadi - {}", CgltfResultText(result));
            cgltf_free(data);
            return false;
        }
        std::unordered_map<const cgltf_node*, int> jointIndex;
        BuildGLTFSkeleton(data, skeleton, skins, jointIndex);
        FinishGLTFSkins(skeleton, skins, AssetImporter::GetModelSettings(path).GetTransform());
        cgltf_free(data);
        return true;
    }

    bool Model::ReadMaterials(const fs::path& path, std::vector<Material>& out, std::string* error)
    {
        out.clear();
        std::string ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        if (ext == ".obj")
        {
            std::error_code ec;
            if (!fs::exists(path, ec))
            {
                if (error) *error = "fayl topilmadi";
                return false;
            }
            out.emplace_back(); // .obj: one default material
            return true;
        }
        cgltf_options options{};
        cgltf_data* data = nullptr;
        std::string file = path.string();
        if (cgltf_result result = cgltf_parse_file(&options, file.c_str(), &data); result != cgltf_result_success)
        {
            if (error) *error = CgltfResultText(result);
            return false;
        }
        out.resize(data->materials_count);
        for (cgltf_size i = 0; i < data->materials_count; i++)
            ReadGLTFMaterial(data->materials[i], out[i]);
        cgltf_free(data);
        return true;
    }

    // ------------------------------------------------------------------ material export
    fs::path Model::GetDefaultMaterialExportDirectory(const fs::path& modelPath)
    {
        return modelPath.parent_path() / (modelPath.stem().string() + "_Materials");
    }

    // A name that is safe as a file name on every system (letters, digits, '_', '-', '.', ' ').
    static std::string SafeFileName(std::string_view name, std::string_view fallback)
    {
        std::string safe;
        for (unsigned char c : name)
            safe += (std::isalnum(c) || c == '_' || c == '-' || c == '.' || c == ' ' || c >= 0x80) ? (char)c : '_';
        while (!safe.empty() && (safe.back() == '.' || safe.back() == ' '))
            safe.pop_back();
        size_t start = safe.find_first_not_of(" .");
        safe = start == std::string::npos ? std::string() : safe.substr(start);
        return safe.empty() ? std::string(fallback) : safe;
    }

    // ".png" / ".jpg" from the image's bytes (or its MIME type); "" when it is neither.
    static std::string ImageExtension(const uint8_t* bytes, size_t size, const char* mimeType)
    {
        if (size >= 8 && bytes[0] == 0x89 && bytes[1] == 'P' && bytes[2] == 'N' && bytes[3] == 'G')
            return ".png";
        if (size >= 3 && bytes[0] == 0xFF && bytes[1] == 0xD8 && bytes[2] == 0xFF)
            return ".jpg";
        std::string_view mime = mimeType ? mimeType : "";
        if (mime == "image/png") return ".png";
        if (mime == "image/jpeg") return ".jpg";
        return {};
    }

    bool Model::ExportMaterials(const fs::path& path, const ModelMaterialExportOptions& options, ModelMaterialExport& result,
                                std::string* error)
    {
        result = {};
        auto fail = [&](std::string reason) {
            if (error) *error = std::move(reason);
            return false;
        };
        std::string ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        if (ext != ".gltf" && ext != ".glb")
            return fail("faqat glTF (.gltf, .glb) modellarining materiallari eksport qilinadi");

        cgltf_options cgltfOptions{};
        cgltf_data* data = nullptr;
        std::string file = path.string();
        if (cgltf_result r = cgltf_parse_file(&cgltfOptions, file.c_str(), &data); r != cgltf_result_success)
            return fail(CgltfResultText(r));
        if (data->materials_count == 0)
        {
            cgltf_free(data);
            return fail("modelda material yo'q");
        }
        // Buffers hold the images of a .glb; without them the values are still exported.
        bool buffers = cgltf_load_buffers(&cgltfOptions, data, file.c_str()) == cgltf_result_success;
        if (!buffers)
            result.Warnings.push_back("model buferlari o'qilmadi - fayl ichidagi rasmlar eksport qilinmaydi");

        const fs::path modelDir = path.parent_path();
        const fs::path outDir = (options.OutputDirectory.empty() ? GetDefaultMaterialExportDirectory(path) : options.OutputDirectory).lexically_normal();
        result.OutputDirectory = outDir;
        std::error_code ec;
        fs::create_directories(outDir, ec);
        if (!fs::is_directory(outDir, ec))
        {
            cgltf_free(data);
            return fail(std::format("papka yaratilmadi: {}", outDir.string()));
        }

        const bool project = Project::GetActive() != nullptr;
        AssetRegistry* registry = project ? Project::GetAssetRegistry() : nullptr;
        const fs::path assetDir = project ? Project::GetAssetDirectory() : fs::path();
        // Paths in .zmat files: relative to the asset folder (absolute without a project).
        auto materialPath = [&](const fs::path& p) {
            return project && AssetFiles::IsInside(p, assetDir) ? Project::GetAssetRelativePath(p) : p.lexically_normal().generic_string();
        };
        auto registered = [&](const fs::path& p) {
            if (registry && AssetFiles::IsInside(p, assetDir))
                registry->OnFileAdded(p); // a UUID at once: written next to the path
        };
        std::unordered_set<std::string> usedNames; // file names of this export (lower case)
        auto claimName = [&](const std::string& stem, const std::string& extension) {
            std::string name = stem + extension;
            auto lower = [](std::string text) {
                std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return (char)std::tolower(c); });
                return text;
            };
            for (int i = 1; !usedNames.insert(lower(name)).second; i++)
                name = std::format("{}_{}{}", stem, i, extension);
            return outDir / name;
        };
        // Writes bytes as `target`, or keeps a file already there (an earlier export).
        auto writeFile = [&](const fs::path& target, const void* bytes, size_t size) {
            if (fs::exists(target, ec) && !options.Overwrite)
            {
                result.Kept.push_back(target);
                return true;
            }
            if (!FileSystem::WriteBinaryFile(target, bytes, size))
                return false;
            result.Written.push_back(target);
            registered(target);
            return true;
        };

        std::unordered_map<const cgltf_image*, std::string> imagePaths; // what each image became ("" = nothing)
        auto exportImage = [&](const cgltf_texture_view& view, const std::string& materialName, const char* role) -> std::string {
            if (!view.texture || !view.texture->image)
                return {};
            const cgltf_image* image = view.texture->image;
            if (auto it = imagePaths.find(image); it != imagePaths.end())
                return it->second;
            std::string& out = imagePaths[image];
            const std::string label = image->name && *image->name ? image->name
                                    : image->uri && !std::string_view(image->uri).starts_with("data:") ? fs::path(image->uri).stem().string()
                                    : materialName + "_" + role;

            if (image->uri && !std::string_view(image->uri).starts_with("data:"))
            {
                // A file next to the model: used where it is, or copied when it is outside the assets.
                std::string uri = image->uri;
                cgltf_decode_uri(uri.data());
                uri.resize(std::strlen(uri.c_str()));
                fs::path source = (modelDir / uri).lexically_normal();
                if (!fs::is_regular_file(source, ec))
                {
                    result.Warnings.push_back(std::format("rasm topilmadi: {}", uri));
                    return out;
                }
                if (!project || AssetFiles::IsInside(source, assetDir))
                    return out = materialPath(source);
                fs::path target = claimName(SafeFileName(source.stem().string(), "texture"), source.extension().string());
                if (fs::exists(target, ec) && !options.Overwrite)
                    result.Kept.push_back(target);
                else if (fs::copy_file(source, target, fs::copy_options::overwrite_existing, ec))
                {
                    result.Written.push_back(target);
                    registered(target);
                }
                else
                {
                    result.Warnings.push_back(std::format("rasm nusxalanmadi: {}", uri));
                    return out;
                }
                return out = materialPath(target);
            }

            // Inside the file: a .glb buffer view or a base64 data URI.
            std::vector<uint8_t> bytes;
            if (image->buffer_view)
            {
                const cgltf_buffer_view* bv = image->buffer_view;
                if (!bv->buffer || !bv->buffer->data || bv->offset + bv->size > bv->buffer->size)
                {
                    result.Warnings.push_back(std::format("'{}' rasmi o'qilmadi (bufer yo'q)", label));
                    return out;
                }
                const uint8_t* begin = (const uint8_t*)bv->buffer->data + bv->offset;
                bytes.assign(begin, begin + bv->size);
            }
            else if (image->uri)
            {
                const char* comma = std::strchr(image->uri, ',');
                std::string_view header(image->uri, comma ? (size_t)(comma - image->uri) : 0);
                if (comma && header.ends_with(";base64"))
                {
                    std::string b64 = comma + 1;
                    size_t padding = (b64.size() >= 1 && b64.back() == '=') + (b64.size() >= 2 && b64[b64.size() - 2] == '=');
                    size_t size = 3 * b64.size() / 4 - padding;
                    void* decoded = nullptr;
                    cgltf_options base64Options{};
                    if (cgltf_load_buffer_base64(&base64Options, size, b64.c_str(), &decoded) == cgltf_result_success)
                    {
                        bytes.assign((const uint8_t*)decoded, (const uint8_t*)decoded + size);
                        std::free(decoded);
                    }
                }
                if (bytes.empty())
                {
                    result.Warnings.push_back(std::format("'{}' rasmi o'qilmadi (data URI)", label));
                    return out;
                }
            }
            std::string extension = ImageExtension(bytes.data(), bytes.size(), image->mime_type);
            if (extension.empty())
            {
                result.Warnings.push_back(std::format("'{}' rasmi PNG ham, JPEG ham emas - eksport qilinmadi", label));
                return out;
            }
            fs::path target = claimName(SafeFileName(label, "texture"), extension);
            if (!writeFile(target, bytes.data(), bytes.size()))
            {
                result.Warnings.push_back(std::format("rasm yozilmadi: {}", target.string()));
                return out;
            }
            return out = materialPath(target);
        };

        // Images first (their UUIDs go into the materials), then one .zmat per material.
        std::vector<MaterialData> materials(data->materials_count);
        std::vector<fs::path> targets(data->materials_count);
        std::string modelName = SafeFileName(path.stem().string(), "Model");
        for (cgltf_size i = 0; i < data->materials_count; i++)
        {
            const cgltf_material& m = data->materials[i];
            std::string name = SafeFileName(m.name ? m.name : "", std::format("{}_Material{}", modelName, i));
            targets[i] = claimName(name, ".zmat");
            Material values;
            ReadGLTFMaterial(m, values);
            MaterialData& d = materials[i];
            d = MaterialData::FromMaterial(values);
            std::string stem = targets[i].stem().string();
            if (m.has_pbr_metallic_roughness)
            {
                d.AlbedoMap = exportImage(m.pbr_metallic_roughness.base_color_texture, stem, "BaseColor");
                d.MetalRoughMap = exportImage(m.pbr_metallic_roughness.metallic_roughness_texture, stem, "MetalRough");
            }
            d.NormalMap = exportImage(m.normal_texture, stem, "Normal");
            d.EmissiveMap = exportImage(m.emissive_texture, stem, "Emissive");
            if (!d.NormalMap.empty() && m.normal_texture.scale > 0.0f)
                d.NormalStrength = m.normal_texture.scale;
        }
        cgltf_free(data);

        for (size_t i = 0; i < materials.size(); i++)
        {
            const fs::path& target = targets[i];
            if (fs::exists(target, ec) && !options.Overwrite)
                result.Kept.push_back(target);
            else if (MaterialSerializer::Serialize(materials[i], target))
            {
                result.Written.push_back(target);
                registered(target);
                if (MaterialLibrary::Find(target))
                    MaterialLibrary::ReloadFile(target); // an earlier export is in use: show the new one
            }
            else
                return fail(std::format("material yozilmadi: {}", target.string()));
            result.Materials.push_back(target);
        }
        ZM_CORE_INFO("Model materiallari eksport qilindi: {} -> {} ({} material, {} fayl yozildi)", path.filename().string(),
                     materialPath(outDir), result.Materials.size(), result.Written.size());
        return true;
    }

    Ref<Model> ModelLoader::LoadGLTF(const fs::path& path, const ModelImportSettings& settings, std::string& error)
    {
        cgltf_options options{};
        cgltf_data* data = nullptr;
        std::string file = path.string();
        if (cgltf_result result = cgltf_parse_file(&options, file.c_str(), &data); result != cgltf_result_success)
        {
            error = CgltfResultText(result);
            return nullptr;
        }

        auto model = CreateRef<Model>();
        model->m_Path = path;
        model->m_ImportSettings = settings;
        std::filesystem::path baseDir = path.parent_path();
        // External buffers: reloading the model follows changes of these files, too.
        for (cgltf_size i = 0; i < data->buffers_count; i++)
        {
            const char* uri = data->buffers[i].uri;
            if (!uri || std::string_view(uri).starts_with("data:"))
                continue;
            std::string decoded = uri;
            cgltf_decode_uri(decoded.data());
            decoded.resize(std::strlen(decoded.c_str()));
            model->m_Dependencies.push_back((baseDir / decoded).lexically_normal());
        }
        if (cgltf_result result = cgltf_load_buffers(&options, data, file.c_str()); result != cgltf_result_success)
        {
            error = std::format("buferlar yuklanmadi - {}", CgltfResultText(result));
            cgltf_free(data);
            return nullptr;
        }
        const glm::mat4 importTransform = settings.GetTransform();
        std::unordered_map<const cgltf_image*, Ref<Texture2D>> textureCache;
        bool first = true;

        // ---- skeleton: the joints of every skin, and each skin's own list of them
        std::unordered_map<const cgltf_node*, int> jointIndex;
        BuildGLTFSkeleton(data, model->m_Skeleton, model->m_Skins, jointIndex);
        for (const ModelSkin& s : model->m_Skins)
            if (!s.Direct && s.Joints.size() > (size_t)MaxJoints)
                ZM_CORE_WARN("{}: '{}' skinida {} suyak bor - GPU {} tasini oladi; ortiqcha suyaklarning og'irligi uchning "
                             "boshqa suyaklariga o'tadi (ular bo'lmasa birinchi suyakka bog'lanadi)",
                             path.filename().string(), s.Name, s.Joints.size(), MaxJoints);

        // Morph groups: one per mesh node with targets (its weights are the node's / mesh's).
        std::unordered_map<const cgltf_node*, int> nodeMorphGroup;
        auto morphGroupOf = [&](const cgltf_node* node) -> int {
            if (!node || !node->mesh || node->mesh->primitives_count == 0 || node->mesh->primitives[0].targets_count == 0)
                return -1;
            if (auto it = nodeMorphGroup.find(node); it != nodeMorphGroup.end())
                return it->second;
            const cgltf_mesh& mesh = *node->mesh;
            const cgltf_size count = mesh.primitives[0].targets_count;
            MorphGroup group;
            group.Name = node->name ? node->name : mesh.name ? mesh.name : std::format("Mesh{}", model->m_MorphGroups.size());
            for (cgltf_size t = 0; t < count; t++)
            {
                const char* targetName = t < mesh.target_names_count && mesh.target_names ? mesh.target_names[t] : nullptr;
                group.Targets.push_back(targetName && *targetName ? targetName : std::format("Target {}", t));
                float w = 0.0f;
                if (node->weights && t < node->weights_count) w = node->weights[t];
                else if (mesh.weights && t < mesh.weights_count) w = mesh.weights[t];
                group.DefaultWeights.push_back(w);
            }
            group.Offset = model->GetMorphWeightCount();
            model->m_MorphGroups.push_back(std::move(group));
            return nodeMorphGroup[node] = (int)model->m_MorphGroups.size() - 1;
        };

        auto addPrimitive = [&](const cgltf_primitive& prim, const glm::mat4& transformIn, const char* name, const cgltf_skin* primSkin,
                                int morphGroup) {
            if (prim.type != cgltf_primitive_type_triangles) return;
            const cgltf_accessor *pos = nullptr, *nrm = nullptr, *uv = nullptr, *joints = nullptr, *weights = nullptr;
            for (cgltf_size i = 0; i < prim.attributes_count; i++)
            {
                const auto& a = prim.attributes[i];
                if (a.type == cgltf_attribute_type_position) pos = a.data;
                else if (a.type == cgltf_attribute_type_normal) nrm = a.data;
                else if (a.type == cgltf_attribute_type_texcoord && a.index == 0) uv = a.data;
                else if (a.type == cgltf_attribute_type_joints && a.index == 0) joints = a.data;
                else if (a.type == cgltf_attribute_type_weights && a.index == 0) weights = a.data;
            }
            if (!pos) return;
            // Its node's skin (any of the file's): which of the skeleton's joints its vertices follow.
            const int skinIndex = primSkin ? (int)cgltf_skin_index(data, primSkin) : -1;
            const ModelSkin* modelSkin = skinIndex >= 0 && (size_t)skinIndex < model->m_Skins.size() ? &model->m_Skins[(size_t)skinIndex] : nullptr;
            bool skinned = modelSkin && !modelSkin->Joints.empty() && joints && weights;
            // glTF: the node transform of a skinned mesh is ignored (joints place it). The import
            // transform (scale, up axis) comes on top; the skeleton gets it too (see below).
            glm::mat4 transform = importTransform * (skinned ? glm::mat4(1.0f) : transformIn);

            glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(transform)));
            std::vector<MeshVertex> vertices(pos->count);
            for (cgltf_size i = 0; i < pos->count; i++)
            {
                float v[4] = { 0, 0, 0, 0 };
                cgltf_accessor_read_float(pos, i, v, 3);
                vertices[i].Position = glm::vec3(transform * glm::vec4(v[0], v[1], v[2], 1.0f));
                if (nrm)
                {
                    cgltf_accessor_read_float(nrm, i, v, 3);
                    vertices[i].Normal = glm::normalize(normalMatrix * glm::vec3(v[0], v[1], v[2]));
                }
                if (uv)
                {
                    cgltf_accessor_read_float(uv, i, v, 2);
                    vertices[i].TexCoord = { v[0], settings.FlipUVs ? v[1] : 1.0f - v[1] }; // textures are uploaded bottom-up
                }
            }

            // Morph targets: each target's position / normal offsets, through the same transform.
            Ref<MorphTargetData> morph;
            if (morphGroup >= 0 && prim.targets_count > 0)
            {
                morph = CreateRef<MorphTargetData>();
                morph->Group = morphGroup;
                const glm::mat3 linear(transform);
                bool anyNormals = false;
                for (cgltf_size t = 0; t < prim.targets_count; t++)
                {
                    std::vector<glm::vec3> dp(pos->count, glm::vec3(0.0f)), dn;
                    const cgltf_morph_target& target = prim.targets[t];
                    for (cgltf_size a = 0; a < target.attributes_count; a++)
                    {
                        const cgltf_attribute& attr = target.attributes[a];
                        if (!attr.data || attr.data->count < pos->count) continue;
                        if (attr.type == cgltf_attribute_type_position)
                            for (cgltf_size i = 0; i < pos->count; i++)
                            {
                                float d[3] = { 0, 0, 0 };
                                cgltf_accessor_read_float(attr.data, i, d, 3);
                                dp[i] = linear * glm::vec3(d[0], d[1], d[2]);
                            }
                        else if (attr.type == cgltf_attribute_type_normal && nrm)
                        {
                            dn.assign(pos->count, glm::vec3(0.0f));
                            for (cgltf_size i = 0; i < pos->count; i++)
                            {
                                float d[3] = { 0, 0, 0 };
                                cgltf_accessor_read_float(attr.data, i, d, 3);
                                dn[i] = normalMatrix * glm::vec3(d[0], d[1], d[2]);
                            }
                            anyNormals = true;
                        }
                    }
                    morph->PositionDeltas.push_back(std::move(dp));
                    morph->NormalDeltas.push_back(std::move(dn));
                }
                if (!anyNormals)
                    morph->NormalDeltas.clear();
                else
                    for (auto& dn : morph->NormalDeltas)
                        if (dn.empty())
                            dn.assign(pos->count, glm::vec3(0.0f));
            }

            std::vector<uint32_t> indices;
            if (prim.indices)
            {
                indices.resize(prim.indices->count);
                for (cgltf_size i = 0; i < prim.indices->count; i++)
                    indices[i] = (uint32_t)cgltf_accessor_read_index(prim.indices, i);
            }
            else
            {
                indices.resize(pos->count);
                for (cgltf_size i = 0; i < pos->count; i++) indices[i] = (uint32_t)i;
            }

            // A mirroring transform (negative scale) baked into the vertices turns the triangles
            // inside out: the winding is reversed so the front faces stay front faces.
            if (glm::determinant(glm::mat3(transform)) < 0.0f)
                for (size_t i = 0; i + 2 < indices.size(); i += 3)
                    std::swap(indices[i + 1], indices[i + 2]);

            if (!nrm) // flat normals
            {
                for (size_t i = 0; i + 2 < indices.size(); i += 3)
                {
                    auto& a = vertices[indices[i]]; auto& b = vertices[indices[i + 1]]; auto& c = vertices[indices[i + 2]];
                    glm::vec3 n = glm::normalize(glm::cross(b.Position - a.Position, c.Position - a.Position));
                    a.Normal = b.Normal = c.Normal = n;
                }
            }

            Model::SubMesh sub;
            sub.Name = name ? name : "mesh";
            // The shared mesh shows the group's default weights.
            const std::vector<float>* defaults = morph ? &model->m_MorphGroups[(size_t)morphGroup].DefaultWeights : nullptr;
            const bool morphed = defaults && std::any_of(defaults->begin(), defaults->end(), [](float w) { return w != 0.0f; });
            if (skinned)
            {
                std::vector<SkinnedVertex> sv(vertices.size());
                for (size_t i = 0; i < vertices.size(); i++)
                {
                    sv[i].Position = vertices[i].Position;
                    sv[i].Normal = vertices[i].Normal;
                    sv[i].TexCoord = vertices[i].TexCoord;
                    cgltf_uint j[4] = { 0, 0, 0, 0 };
                    cgltf_accessor_read_uint(joints, i, j, 4);
                    float w[4] = { 0, 0, 0, 0 };
                    cgltf_accessor_read_float(weights, i, w, 4);
                    for (int k = 0; k < 4; k++)
                    {
                        // A direct skin: skin-local joint index -> the skeleton's; else the skin's own
                        // (its palette, GetSkinMatrices).
                        const size_t local = j[k] < modelSkin->Joints.size() ? (size_t)j[k] : 0;
                        sv[i].Joints[k] = modelSkin->Direct ? modelSkin->Joints[local] : (int)local;
                        sv[i].Weights[k] = w[k];
                    }
                    // Joints past the GPU's palette (a skin with more than MaxJoints): their weight
                    // goes to the vertex's other joints.
                    FitJointInfluences(sv[i].Joints, sv[i].Weights);
                }
                if (morph)
                {
                    morph->Skinned = true;
                    morph->SkinnedVertices = sv;
                    morph->Indices = indices;
                    if (morphed)
                        ApplyMorphTargets(*morph, defaults->data(), defaults->size(), sv);
                }
                sub.MeshRef = CreateRef<Mesh>(sv, indices);
                sub.Skin = skinIndex;
            }
            else
            {
                if (morph)
                {
                    morph->Vertices = vertices;
                    morph->Indices = indices;
                    if (morphed)
                        ApplyMorphTargets(*morph, defaults->data(), defaults->size(), vertices);
                }
                sub.MeshRef = CreateRef<Mesh>(vertices, indices);
            }
            sub.Morph = morph;
            if (const cgltf_material* m = prim.material)
            {
                sub.MaterialIndex = (int)cgltf_material_index(data, m);
                ReadGLTFMaterial(*m, sub.Mat);
                if (m->has_pbr_metallic_roughness)
                {
                    sub.Mat.AlbedoMap = LoadGLTFTexture(m->pbr_metallic_roughness.base_color_texture, baseDir, textureCache);
                    sub.Mat.MetalRoughMap = LoadGLTFTexture(m->pbr_metallic_roughness.metallic_roughness_texture, baseDir, textureCache);
                }
                sub.Mat.NormalMap = LoadGLTFTexture(m->normal_texture, baseDir, textureCache);
                sub.Mat.EmissiveMap = LoadGLTFTexture(m->emissive_texture, baseDir, textureCache);
            }

            const AABB& b = sub.MeshRef->GetBounds();
            if (first) { model->m_Bounds = b; first = false; }
            else
            {
                model->m_Bounds.Min = glm::min(model->m_Bounds.Min, b.Min);
                model->m_Bounds.Max = glm::max(model->m_Bounds.Max, b.Max);
            }
            model->m_SubMeshes.push_back(std::move(sub));
        };

        std::function<void(const cgltf_node*, const glm::mat4&)> visit = [&](const cgltf_node* node, const glm::mat4& parent) {
            float m[16];
            cgltf_node_transform_local(node, m);
            glm::mat4 world = parent * glm::make_mat4(m);
            if (node->mesh)
                for (cgltf_size i = 0; i < node->mesh->primitives_count; i++)
                    addPrimitive(node->mesh->primitives[i], world, node->mesh->name ? node->mesh->name : node->name, node->skin,
                                 morphGroupOf(node));
            for (cgltf_size i = 0; i < node->children_count; i++)
                visit(node->children[i], world);
        };

        const cgltf_scene* scene = data->scene ? data->scene : (data->scenes_count ? &data->scenes[0] : nullptr);
        if (scene)
            for (cgltf_size i = 0; i < scene->nodes_count; i++)
                visit(scene->nodes[i], glm::mat4(1.0f));
        else
            for (cgltf_size i = 0; i < data->meshes_count; i++)
                for (cgltf_size p = 0; p < data->meshes[i].primitives_count; p++)
                    addPrimitive(data->meshes[i].primitives[p], glm::mat4(1.0f), data->meshes[i].name, nullptr, -1);

        // ---- animations: channels moving joints and channels of morph target weights
        for (cgltf_size a = 0; a < data->animations_count; a++)
        {
            const cgltf_animation& anim = data->animations[a];
            AnimationClip clip;
            clip.Name = anim.name ? anim.name : ("Anim" + std::to_string(a));
            for (cgltf_size c = 0; c < anim.channels_count; c++)
            {
                const cgltf_animation_channel& ch = anim.channels[c];
                if (!ch.sampler || !ch.sampler->input || !ch.sampler->output)
                    continue;
                const bool cubic = ch.sampler->interpolation == cgltf_interpolation_type_cubic_spline;
                const cgltf_accessor* in = ch.sampler->input;
                const cgltf_accessor* val = ch.sampler->output;
                if (ch.target_path == cgltf_animation_path_type_weights)
                {
                    auto it = nodeMorphGroup.find(ch.target_node);
                    if (it == nodeMorphGroup.end())
                        continue;
                    MorphWeightChannel out;
                    out.Group = it->second;
                    out.Count = model->m_MorphGroups[(size_t)it->second].GetCount();
                    out.Step = ch.sampler->interpolation == cgltf_interpolation_type_step;
                    for (cgltf_size k = 0; k < in->count; k++)
                    {
                        float t = 0.0f;
                        cgltf_accessor_read_float(in, k, &t, 1);
                        out.Times.push_back(t);
                        for (uint32_t w = 0; w < out.Count; w++)
                        {
                            // scalars: key k's weights; cubic: [in tangents, values, out tangents] per key
                            const cgltf_size index = cubic ? (k * 3 + 1) * out.Count + w : k * out.Count + w;
                            float v = 0.0f;
                            cgltf_accessor_read_float(val, index, &v, 1);
                            out.Values.push_back(v);
                        }
                        clip.Duration = std::max(clip.Duration, t);
                    }
                    clip.MorphChannels.push_back(std::move(out));
                    continue;
                }
                auto it = jointIndex.find(ch.target_node);
                if (it == jointIndex.end()) continue;
                AnimationChannel out;
                out.JointIndex = it->second;
                if (ch.target_path == cgltf_animation_path_type_translation) out.Target = AnimationChannel::Path::Translation;
                else if (ch.target_path == cgltf_animation_path_type_rotation) out.Target = AnimationChannel::Path::Rotation;
                else if (ch.target_path == cgltf_animation_path_type_scale) out.Target = AnimationChannel::Path::Scale;
                else continue;
                out.Step = ch.sampler->interpolation == cgltf_interpolation_type_step;
                int comps = out.Target == AnimationChannel::Path::Rotation ? 4 : 3;
                for (cgltf_size k = 0; k < in->count; k++)
                {
                    float t = 0.0f;
                    cgltf_accessor_read_float(in, k, &t, 1);
                    float v[4] = { 0, 0, 0, 1 };
                    cgltf_accessor_read_float(val, cubic ? k * 3 + 1 : k, v, comps); // cubic: take the value, ignore tangents
                    out.Times.push_back(t);
                    out.Values.emplace_back(v[0], v[1], v[2], v[3]);
                    clip.Duration = std::max(clip.Duration, t);
                }
                clip.Channels.push_back(std::move(out));
            }
            if (!clip.Channels.empty() || !clip.MorphChannels.empty())
                model->m_Animations.push_back(std::move(clip));
        }
        // Trimmed clips and sub-clips (the model's import settings).
        if (!settings.Clips.empty())
        {
            model->m_SourceAnimations = model->m_Animations;
            model->m_ClipsCut = true;
            std::vector<std::string> warnings;
            ApplyClipSettings(model->m_Animations, settings.Clips, settings.KeepSourceClips, &warnings);
            for (const std::string& w : warnings)
                ZM_CORE_WARN("{}: {}", path.filename().string(), w);
        }

        FinishGLTFSkins(model->m_Skeleton, model->m_Skins, importTransform);

        size_t triangles = 0;
        for (auto& s : model->m_SubMeshes) triangles += s.MeshRef->GetIndexCount() / 3;
        cgltf_free(data);
        if (model->m_SubMeshes.empty())
        {
            error = "faylda uchburchakli mesh yo'q";
            return nullptr;
        }
        std::string extra;
        if (model->m_Skins.size() > 1)
            extra += std::format(", {} skin", model->m_Skins.size());
        if (model->HasMorphTargets())
            extra += std::format(", {} morph target", model->GetMorphWeightCount());
        ZM_CORE_INFO("glTF yuklandi: {} ({} qism, {} uchburchak, {} suyak, {} animatsiya{})", path.filename().string(),
                     model->m_SubMeshes.size(), triangles, model->m_Skeleton.Joints.size(), model->m_Animations.size(), extra);
        return model;
    }

    Ref<Model> ModelLoader::LoadOBJ(const fs::path& path, const ModelImportSettings& settings, std::string& error)
    {
        std::vector<MeshVertex> vertices;
        std::vector<uint32_t> indices;
        if (!MeshLibrary::ReadOBJ(path, vertices, indices, &error))
            return nullptr;
        if (!(settings == ModelImportSettings{}))
        {
            glm::mat4 transform = settings.GetTransform();
            glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(transform)));
            for (MeshVertex& v : vertices)
            {
                v.Position = glm::vec3(transform * glm::vec4(v.Position, 1.0f));
                if (glm::dot(v.Normal, v.Normal) > 0.0f)
                    v.Normal = glm::normalize(normalMatrix * v.Normal);
                if (settings.FlipUVs)
                    v.TexCoord.y = 1.0f - v.TexCoord.y;
            }
            if (glm::determinant(glm::mat3(transform)) < 0.0f) // mirrored: keep the front faces in front
                for (size_t i = 0; i + 2 < indices.size(); i += 3)
                    std::swap(indices[i + 1], indices[i + 2]);
        }
        auto mesh = CreateRef<Mesh>(vertices, indices);
        auto model = CreateRef<Model>();
        model->m_Path = path;
        model->m_ImportSettings = settings;
        model->m_SubMeshes.push_back({ mesh, Material{}, path.stem().string(), 0 });
        model->m_Bounds = mesh->GetBounds();
        ZM_CORE_INFO("Model yuklandi: {} ({} uchburchak)", path.filename().string(), indices.size() / 3);
        return model;
    }

    Ref<Model> ModelLoader::LoadFile(const fs::path& path, const ModelImportSettings& settings, std::string& error)
    {
        std::error_code ec;
        if (!fs::is_regular_file(path, ec))
        {
            error = "fayl topilmadi";
            return nullptr;
        }
        std::string ext = path.extension().string();
        for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
        if (ext == ".gltf" || ext == ".glb")
            return LoadGLTF(path, settings, error);
        if (ext == ".obj")
            return LoadOBJ(path, settings, error);
        error = "format qo'llab-quvvatlanmaydi";
        return nullptr;
    }

    void ModelLoader::Replace(Model& to, Model&& from)
    {
        to.m_SubMeshes = std::move(from.m_SubMeshes);
        to.m_Bounds = from.m_Bounds;
        to.m_Skeleton = std::move(from.m_Skeleton);
        to.m_Skins = std::move(from.m_Skins);
        to.m_Animations = std::move(from.m_Animations);
        to.m_SourceAnimations = std::move(from.m_SourceAnimations);
        to.m_ClipsCut = from.m_ClipsCut;
        to.m_MorphGroups = std::move(from.m_MorphGroups);
        to.m_ImportSettings = from.m_ImportSettings;
        to.m_Dependencies = std::move(from.m_Dependencies);
        to.m_PoseCache.clear();
        to.m_Revision++;
    }

    // ------------------------------------------------------------------ morph targets
    template<typename V>
    static void ApplyMorph(const MorphTargetData& data, const std::vector<V>& base, const float* weights, size_t count, std::vector<V>& out)
    {
        out = base;
        const size_t targets = std::min(count, data.PositionDeltas.size());
        const bool normals = data.NormalDeltas.size() == data.PositionDeltas.size();
        bool anyNormal = false;
        for (size_t t = 0; t < targets; t++)
        {
            const float w = weights[t];
            if (w == 0.0f || !std::isfinite(w))
                continue;
            const auto& dp = data.PositionDeltas[t];
            for (size_t i = 0; i < out.size() && i < dp.size(); i++)
                out[i].Position += dp[i] * w;
            if (normals && !data.NormalDeltas[t].empty())
            {
                const auto& dn = data.NormalDeltas[t];
                for (size_t i = 0; i < out.size() && i < dn.size(); i++)
                    out[i].Normal += dn[i] * w;
                anyNormal = true;
            }
        }
        if (anyNormal)
            for (V& v : out)
            {
                const float len = glm::length(v.Normal);
                v.Normal = len > 1e-8f ? v.Normal / len : glm::vec3(0.0f, 1.0f, 0.0f);
            }
    }

    void ApplyMorphTargets(const MorphTargetData& data, const float* weights, size_t count, std::vector<MeshVertex>& out)
    {
        ApplyMorph(data, data.Vertices, weights, count, out);
    }

    void ApplyMorphTargets(const MorphTargetData& data, const float* weights, size_t count, std::vector<SkinnedVertex>& out)
    {
        ApplyMorph(data, data.SkinnedVertices, weights, count, out);
    }

    bool SampleClipMorphWeights(const Model& model, const AnimationClip* clip, float time, std::vector<float>& weights)
    {
        const uint32_t count = model.GetMorphWeightCount();
        if (weights.size() != count)
            weights.resize(count, 0.0f);
        if (!clip || clip->MorphChannels.empty())
            return false;
        const auto& groups = model.GetMorphGroups();
        bool any = false;
        for (const MorphWeightChannel& c : clip->MorphChannels)
        {
            if (c.Group < 0 || c.Group >= (int)groups.size())
                continue;
            const MorphGroup& g = groups[(size_t)c.Group];
            if (c.Count == 0 || g.Offset + std::min(c.Count, g.GetCount()) > count)
                continue;
            if (c.Count == g.GetCount())
                SampleMorphChannel(c, time, weights.data() + g.Offset);
            else
            {
                std::vector<float> values(c.Count);
                SampleMorphChannel(c, time, values.data());
                std::copy(values.begin(), values.begin() + std::min(c.Count, g.GetCount()), weights.begin() + g.Offset);
            }
            any = true;
        }
        return any;
    }

    int Model::FindMorphTarget(const std::string& name) const
    {
        const size_t slash = name.find('/');
        for (const MorphGroup& g : m_MorphGroups)
            for (uint32_t t = 0; t < g.GetCount(); t++)
            {
                const bool match = slash == std::string::npos ? g.Targets[t] == name
                                                              : name.compare(0, slash, g.Name) == 0 && slash == g.Name.size() &&
                                                                name.compare(slash + 1, std::string::npos, g.Targets[t]) == 0;
                if (match)
                    return (int)(g.Offset + t);
            }
        return -1;
    }

    std::string Model::GetMorphTargetName(uint32_t index) const
    {
        for (const MorphGroup& g : m_MorphGroups)
            if (index >= g.Offset && index < g.Offset + g.GetCount())
                return m_MorphGroups.size() == 1 ? g.Targets[index - g.Offset] : g.Name + "/" + g.Targets[index - g.Offset];
        return {};
    }

    std::vector<float> Model::GetDefaultMorphWeights() const
    {
        std::vector<float> weights(GetMorphWeightCount(), 0.0f);
        for (const MorphGroup& g : m_MorphGroups)
            for (uint32_t t = 0; t < g.GetCount() && t < g.DefaultWeights.size(); t++)
                weights[g.Offset + t] = g.DefaultWeights[t];
        return weights;
    }

    void Model::SetMorphGroups(std::vector<MorphGroup> groups)
    {
        uint32_t offset = 0;
        for (MorphGroup& g : groups)
        {
            g.Offset = offset;
            g.DefaultWeights.resize(g.Targets.size(), 0.0f);
            offset += g.GetCount();
        }
        m_MorphGroups = std::move(groups);
    }

    const BakedClip* Model::GetBakedClip(const AnimationClip& clip, float rate) const
    {
        if (m_Animations.empty() || &clip < m_Animations.data() || &clip >= m_Animations.data() + m_Animations.size())
            return nullptr;
        const size_t index = (size_t)(&clip - m_Animations.data());
        if (m_PoseCache.size() != m_Animations.size())
            m_PoseCache.resize(m_Animations.size());
        if (!std::isfinite(rate) || rate < 1.0f)
            rate = 1.0f;
        // One cache per rate: animators of this model asking for different rates each get their own.
        std::vector<PoseCacheEntry>& entries = m_PoseCache[index];
        PoseCacheEntry* entry = nullptr;
        for (PoseCacheEntry& e : entries)
            if (e.Baked && e.Baked->Rate == rate)
            {
                entry = &e;
                break;
            }
        if (!entry)
        {
            if (entries.size() < MaxPoseCacheRates)
                entry = &entries.emplace_back();
            else // full: the one asked for longest ago is made again for this rate
                entry = &*std::min_element(entries.begin(), entries.end(),
                                           [](const PoseCacheEntry& a, const PoseCacheEntry& b) { return a.LastUse < b.LastUse; });
        }
        entry->LastUse = ++m_PoseCacheUses;
        if (!entry->Baked)
            entry->Baked = CreateRef<BakedClip>();
        BakedClip& baked = *entry->Baked;
        if (baked.Rate != rate || baked.Joints != m_Skeleton.Joints.size() || baked.Frames == 0)
            BakeClip(m_Skeleton, clip, rate, baked);
        return &baked;
    }

    Ref<Model> Model::CreateSkeletal(Skeleton skeleton, std::vector<AnimationClip> animations)
    {
        auto model = CreateRef<Model>();
        model->m_Skeleton = std::move(skeleton);
        model->m_Animations = std::move(animations);
        return model;
    }

    Ref<Model> Model::Load(const std::filesystem::path& path)
    {
        std::string key = path.string();
        if (auto it = s_ModelCache.find(key); it != s_ModelCache.end())
            return it->second;

        std::string error;
        Ref<Model> model = ModelLoader::LoadFile(path, AssetImporter::GetModelSettings(path), error);
        if (!model)
            AssetImportLog::Report(path, error);
        else
            AssetImportLog::Clear(path);
        s_ModelCache[key] = model; // a failed file is not read again until it changes (ReloadFile)
        return model;
    }

    bool Model::Reimport(const ModelImportSettings& settings, std::string* error)
    {
        std::string reason;
        Ref<Model> fresh = ModelLoader::LoadFile(m_Path, settings, reason);
        if (!fresh)
        {
            if (error)
                *error = reason;
            else
                AssetImportLog::Report(m_Path, reason, true);
            return false;
        }
        ModelLoader::Replace(*this, std::move(*fresh));
        AssetImportLog::Clear(m_Path);
        return true;
    }

    bool Model::Reload(std::string* error)
    {
        return Reimport(AssetImporter::GetModelSettings(m_Path), error);
    }

    static std::string CanonicalModelPath(const fs::path& path)
    {
        std::error_code ec;
        fs::path canonical = fs::weakly_canonical(path, ec);
        return (ec ? path.lexically_normal() : canonical).string();
    }

    // Whether the cached model under `key` is made from `file` (canonical) or reads it.
    static bool UsesFile(const std::string& key, const Ref<Model>& model, const std::string& file)
    {
        if (CanonicalModelPath(key) == file)
            return true;
        if (model)
            for (const fs::path& dependency : model->GetDependencies())
                if (CanonicalModelPath(dependency) == file)
                    return true;
        return false;
    }

    size_t Model::ReloadFile(const std::filesystem::path& path, bool* failed)
    {
        std::string file = CanonicalModelPath(path);
        size_t reloaded = 0;
        std::string error, failedModel;
        for (auto it = s_ModelCache.begin(); it != s_ModelCache.end();)
        {
            if (!UsesFile(it->first, it->second, file))
            {
                ++it;
                continue;
            }
            if (!it->second)
            {
                it = s_ModelCache.erase(it); // failed before: loaded again by the next Load
                continue;
            }
            if (error.empty() && it->second->Reload(&error)) // a file that failed once is not read again
                reloaded++;
            else
            {
                if (failedModel.empty())
                    failedModel = it->first; // the model, also when one of its buffers changed
                if (failed)
                    *failed = true;
            }
            ++it;
        }
        if (!error.empty())
            AssetImportLog::Report(failedModel.empty() ? path : fs::path(failedModel), error, true);
        return reloaded;
    }

    bool Model::IsFileLoaded(const std::filesystem::path& path)
    {
        std::string file = CanonicalModelPath(path);
        for (const auto& [key, model] : s_ModelCache)
            if (model && UsesFile(key, model, file))
                return true;
        return false;
    }

    bool Model::ReleaseIfUnused(const std::filesystem::path& path)
    {
        auto it = s_ModelCache.find(path.string());
        if (it == s_ModelCache.end() || !it->second || it->second.use_count() > 1)
            return false; // a failed file stays known (not read again until it changes)
        s_ModelCache.erase(it);
        return true;
    }

    void Model::ClearCache() { s_ModelCache.clear(); }

}
