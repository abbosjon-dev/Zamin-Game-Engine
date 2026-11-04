#include "Zamin/Renderer/Model.h"
#include "Zamin/Renderer/Texture.h"
#include "Zamin/Core/Log.h"

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include <glm/gtc/type_ptr.hpp>
#include <cstring>
#include <functional>
#include <unordered_map>

namespace Zamin {

    class ModelLoader
    {
    public:
        static Ref<Model> LoadGLTF(const std::filesystem::path& path);
    };

    static std::unordered_map<std::string, Ref<Model>> s_ModelCache;

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

    Ref<Model> ModelLoader::LoadGLTF(const std::filesystem::path& path)
    {
        cgltf_options options{};
        cgltf_data* data = nullptr;
        std::string file = path.string();
        if (cgltf_parse_file(&options, file.c_str(), &data) != cgltf_result_success)
        {
            ZM_CORE_ERROR("glTF o'qilmadi: '{}'", file);
            return nullptr;
        }
        if (cgltf_load_buffers(&options, data, file.c_str()) != cgltf_result_success)
        {
            ZM_CORE_ERROR("glTF buferlari yuklanmadi: '{}'", file);
            cgltf_free(data);
            return nullptr;
        }

        auto model = CreateRef<Model>();
        model->m_Path = path;
        std::filesystem::path baseDir = path.parent_path();
        std::unordered_map<const cgltf_image*, Ref<Texture2D>> textureCache;
        bool first = true;

        auto addPrimitive = [&](const cgltf_primitive& prim, const glm::mat4& transform, const char* name) {
            if (prim.type != cgltf_primitive_type_triangles) return;
            const cgltf_accessor *pos = nullptr, *nrm = nullptr, *uv = nullptr;
            for (cgltf_size i = 0; i < prim.attributes_count; i++)
            {
                const auto& a = prim.attributes[i];
                if (a.type == cgltf_attribute_type_position) pos = a.data;
                else if (a.type == cgltf_attribute_type_normal) nrm = a.data;
                else if (a.type == cgltf_attribute_type_texcoord && a.index == 0) uv = a.data;
            }
            if (!pos) return;

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
                    vertices[i].TexCoord = { v[0], 1.0f - v[1] }; // textures are uploaded bottom-up
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
                    addPrimitive(node->mesh->primitives[i], world, node->mesh->name ? node->mesh->name : node->name);
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
                    addPrimitive(data->meshes[i].primitives[p], glm::mat4(1.0f), data->meshes[i].name);

        size_t triangles = 0;
        for (auto& s : model->m_SubMeshes) triangles += s.MeshRef->GetIndexCount() / 3;
        ZM_CORE_INFO("glTF yuklandi: {} ({} qism, {} uchburchak)", path.filename().string(), model->m_SubMeshes.size(), triangles);
        cgltf_free(data);
        return model->m_SubMeshes.empty() ? nullptr : model;
    }

    Ref<Model> Model::Load(const std::filesystem::path& path)
    {
        std::string key = path.string();
        if (auto it = s_ModelCache.find(key); it != s_ModelCache.end())
            return it->second;

        Ref<Model> model;
        std::string ext = path.extension().string();
        for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
        if (ext == ".gltf" || ext == ".glb")
            model = ModelLoader::LoadGLTF(path);
        else if (ext == ".obj")
        {
            if (Ref<Mesh> mesh = MeshLibrary::LoadOBJ(path))
            {
                model = CreateRef<Model>();
                model->m_Path = path;
                model->m_SubMeshes.push_back({ mesh, Material{}, path.stem().string() });
                model->m_Bounds = mesh->GetBounds();
            }
        }
        else
            ZM_CORE_ERROR("Model formati qo'llab-quvvatlanmaydi: '{}'", key);

        s_ModelCache[key] = model;
        return model;
    }

    void Model::ClearCache() { s_ModelCache.clear(); }

}
