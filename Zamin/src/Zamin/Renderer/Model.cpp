#include "Zamin/Renderer/Model.h"
#include "Zamin/Renderer/Texture.h"
#include "Zamin/Asset/AssetImportLog.h"
#include "Zamin/Asset/AssetImporter.h"
#include "Zamin/Core/Log.h"

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cstring>
#include <functional>
#include <unordered_map>

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
                if (m->has_pbr_metallic_roughness)
                {
                    const auto& pbr = m->pbr_metallic_roughness;
                    // glTF factors are linear; our material colors are sRGB-authored.
                    sub.Mat.Albedo = { std::pow(pbr.base_color_factor[0], 1.0f / 2.2f), std::pow(pbr.base_color_factor[1], 1.0f / 2.2f),
                                       std::pow(pbr.base_color_factor[2], 1.0f / 2.2f), pbr.base_color_factor[3] };
                    sub.Mat.Metallic = pbr.metallic_factor;
                    sub.Mat.Roughness = pbr.roughness_factor;
                    sub.Mat.AlbedoMap = LoadGLTFTexture(pbr.base_color_texture, baseDir, textureCache);
                    sub.Mat.MetalRoughMap = LoadGLTFTexture(pbr.metallic_roughness_texture, baseDir, textureCache);
                }
                sub.Mat.NormalMap = LoadGLTFTexture(m->normal_texture, baseDir, textureCache);
                sub.Mat.EmissiveMap = LoadGLTFTexture(m->emissive_texture, baseDir, textureCache);
                sub.Mat.Emission = { m->emissive_factor[0], m->emissive_factor[1], m->emissive_factor[2] };
                if (glm::length(sub.Mat.Emission) > 0.0f)
                    sub.Mat.EmissionStrength = m->has_emissive_strength ? m->emissive_strength.emissive_strength : 1.0f;
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
        }
        auto mesh = CreateRef<Mesh>(vertices, indices);
        auto model = CreateRef<Model>();
        model->m_Path = path;
        model->m_ImportSettings = settings;
        model->m_SubMeshes.push_back({ mesh, Material{}, path.stem().string() });
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
