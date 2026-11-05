#include "Zamin/Renderer/Model.h"
#include "Zamin/Renderer/Texture.h"
#include "Zamin/Asset/AssetImportLog.h"
#include "Zamin/Asset/AssetImporter.h"
#include "Zamin/Core/Log.h"
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
            out.Albedo = { std::pow(pbr.base_color_factor[0], 1.0f / 2.2f), std::pow(pbr.base_color_factor[1], 1.0f / 2.2f),
                           std::pow(pbr.base_color_factor[2], 1.0f / 2.2f), pbr.base_color_factor[3] };
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

        // ---- skeleton (first skin)
        std::unordered_map<const cgltf_node*, int> jointIndex;
        const cgltf_skin* skin = data->skins_count ? &data->skins[0] : nullptr;
        if (skin)
        {
            // Order joints so that parents precede children.
            std::vector<const cgltf_node*> ordered;
            std::function<void(const cgltf_node*)> addOrdered = [&](const cgltf_node* n) {
                for (const cgltf_node* p = n->parent; p; p = p->parent)
                    if (std::find(skin->joints, skin->joints + skin->joints_count, p) != skin->joints + skin->joints_count &&
                        std::find(ordered.begin(), ordered.end(), p) == ordered.end())
                    { addOrdered(p); break; }
                if (std::find(ordered.begin(), ordered.end(), n) == ordered.end()) ordered.push_back(n);
            };
            for (cgltf_size i = 0; i < skin->joints_count; i++) addOrdered(skin->joints[i]);
            for (size_t i = 0; i < ordered.size(); i++) jointIndex[ordered[i]] = (int)i;

            auto& sk = model->m_Skeleton;
            sk.Joints.resize(ordered.size());
            for (size_t i = 0; i < ordered.size(); i++)
            {
                const cgltf_node* n = ordered[i];
                Joint& j = sk.Joints[i];
                j.Name = n->name ? n->name : ("joint" + std::to_string(i));
                j.Parent = -1;
                for (const cgltf_node* p = n->parent; p; p = p->parent)
                    if (auto it = jointIndex.find(p); it != jointIndex.end()) { j.Parent = it->second; break; }
                if (n->has_translation) j.RestTranslation = { n->translation[0], n->translation[1], n->translation[2] };
                if (n->has_rotation) j.RestRotation = glm::quat(n->rotation[3], n->rotation[0], n->rotation[1], n->rotation[2]);
                if (n->has_scale) j.RestScale = { n->scale[0], n->scale[1], n->scale[2] };
            }
            // inverse bind matrices are indexed by skin joint order
            for (cgltf_size i = 0; i < skin->joints_count; i++)
            {
                float m[16];
                if (skin->inverse_bind_matrices && cgltf_accessor_read_float(skin->inverse_bind_matrices, i, m, 16))
                    sk.Joints[jointIndex[skin->joints[i]]].InverseBind = glm::make_mat4(m);
            }
            // world transform of the root joint's non-joint ancestors
            if (!ordered.empty() && ordered[0]->parent)
            {
                float m[16];
                cgltf_node_transform_world(ordered[0]->parent, m);
                sk.RootTransform = glm::make_mat4(m);
            }

            // ---- animations targeting joints
            for (cgltf_size a = 0; a < data->animations_count; a++)
            {
                const cgltf_animation& anim = data->animations[a];
                AnimationClip clip;
                clip.Name = anim.name ? anim.name : ("Anim" + std::to_string(a));
                for (cgltf_size c = 0; c < anim.channels_count; c++)
                {
                    const cgltf_animation_channel& ch = anim.channels[c];
                    auto it = jointIndex.find(ch.target_node);
                    if (it == jointIndex.end() || !ch.sampler) continue;
                    AnimationChannel out;
                    out.JointIndex = it->second;
                    if (ch.target_path == cgltf_animation_path_type_translation) out.Target = AnimationChannel::Path::Translation;
                    else if (ch.target_path == cgltf_animation_path_type_rotation) out.Target = AnimationChannel::Path::Rotation;
                    else if (ch.target_path == cgltf_animation_path_type_scale) out.Target = AnimationChannel::Path::Scale;
                    else continue;
                    out.Step = ch.sampler->interpolation == cgltf_interpolation_type_step;
                    bool cubic = ch.sampler->interpolation == cgltf_interpolation_type_cubic_spline;
                    const cgltf_accessor* in = ch.sampler->input;
                    const cgltf_accessor* val = ch.sampler->output;
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
                if (!clip.Channels.empty())
                    model->m_Animations.push_back(std::move(clip));
            }
        }

        auto addPrimitive = [&](const cgltf_primitive& prim, const glm::mat4& transformIn, const char* name, const cgltf_skin* primSkin) {
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
            bool skinned = primSkin == skin && skin && joints && weights;
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
                    float sum = w[0] + w[1] + w[2] + w[3];
                    for (int k = 0; k < 4; k++)
                    {
                        // skin-local joint index -> our ordered skeleton index
                        int idx = j[k] < skin->joints_count ? jointIndex[skin->joints[j[k]]] : 0;
                        sv[i].Joints[k] = std::min(idx, MaxJoints - 1);
                        sv[i].Weights[k] = sum > 0.0f ? w[k] / sum : (k == 0 ? 1.0f : 0.0f);
                    }
                }
                sub.MeshRef = CreateRef<Mesh>(sv, indices);
            }
            else
                sub.MeshRef = CreateRef<Mesh>(vertices, indices);
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
                    addPrimitive(node->mesh->primitives[i], world, node->mesh->name ? node->mesh->name : node->name, node->skin);
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
                    addPrimitive(data->meshes[i].primitives[p], glm::mat4(1.0f), data->meshes[i].name, nullptr);

        if (!model->m_Skeleton.Empty())
            ApplyImportTransform(model->m_Skeleton, importTransform);

        size_t triangles = 0;
        for (auto& s : model->m_SubMeshes) triangles += s.MeshRef->GetIndexCount() / 3;
        cgltf_free(data);
        if (model->m_SubMeshes.empty())
        {
            error = "faylda uchburchakli mesh yo'q";
            return nullptr;
        }
        ZM_CORE_INFO("glTF yuklandi: {} ({} qism, {} uchburchak, {} suyak, {} animatsiya)", path.filename().string(),
                     model->m_SubMeshes.size(), triangles, model->m_Skeleton.Joints.size(), model->m_Animations.size());
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
        to.m_Animations = std::move(from.m_Animations);
        to.m_ImportSettings = from.m_ImportSettings;
        to.m_Dependencies = std::move(from.m_Dependencies);
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
