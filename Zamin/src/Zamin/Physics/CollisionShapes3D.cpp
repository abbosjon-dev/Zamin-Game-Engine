#include "Zamin/Physics/CollisionShapes3D.h"
#include "Zamin/Asset/AssetImporter.h"
#include "Zamin/Asset/AssetMetadata.h"
#include "Zamin/Project/Project.h"

#include <Jolt/Jolt.h>
#include <Jolt/Core/Memory.h>
#include <Jolt/Geometry/ConvexHullBuilder.h>

#include <cgltf.h>

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <tuple>
#include <unordered_map>

namespace Zamin {

    namespace fs = std::filesystem;

    namespace {

        // The file and its .zmeta as they were when the geometry was read: changed - read again.
        struct SourceStamp
        {
            fs::file_time_type File{}, Meta{};
            bool operator==(const SourceStamp&) const = default;
        };

        struct MeshEntry
        {
            Ref<const CollisionMesh3D> Mesh;
            std::string Error;
            SourceStamp Stamp;
        };

        struct HullEntry
        {
            Ref<const ConvexHull3D> Hull;
            std::string Error;
            SourceStamp Stamp;
        };

        struct EdgeEntry
        {
            std::weak_ptr<const CollisionMesh3D> Mesh;
            Ref<const CollisionShapes3D::EdgeList> Edges;
        };

        std::mutex s_Mutex;
        std::unordered_map<std::string, MeshEntry> s_Meshes;
        std::unordered_map<std::string, HullEntry> s_Hulls;
        std::unordered_map<const CollisionMesh3D*, EdgeEntry> s_Edges;
        std::once_flag s_AllocatorOnce;

        fs::path ResolveModel(const std::string& modelPath)
        {
            return Project::GetAssetFileSystemPath(modelPath).lexically_normal();
        }

        std::string SourceKey(const std::string& modelPath, PrimitiveMesh primitive)
        {
            if (modelPath.empty())
                return std::format("primitive:{}", (int)primitive);
            return ResolveModel(modelPath).generic_string();
        }

        SourceStamp ReadStamp(const std::string& modelPath)
        {
            SourceStamp stamp;
            if (modelPath.empty())
                return stamp; // primitives never change
            const fs::path file = ResolveModel(modelPath);
            std::error_code ec;
            stamp.File = fs::last_write_time(file, ec);
            stamp.Meta = fs::last_write_time(AssetMetaFile::GetMetaPath(file), ec);
            return stamp;
        }

        Ref<const CollisionMesh3D> ReadMesh(const std::string& modelPath, PrimitiveMesh primitive, std::string& error)
        {
            auto mesh = CreateRef<CollisionMesh3D>();
            if (modelPath.empty())
            {
                if (primitive == PrimitiveMesh::Model)
                {
                    error = "model fayli ko'rsatilmagan";
                    return nullptr;
                }
                std::vector<MeshVertex> vertices;
                MeshLibrary::GeneratePrimitive(primitive, vertices, mesh->Indices);
                mesh->Positions.reserve(vertices.size());
                for (const MeshVertex& v : vertices)
                    mesh->Positions.push_back(v.Position);
            }
            else if (!CollisionShapes3D::ReadModelGeometry(ResolveModel(modelPath), *mesh, &error))
                return nullptr;
            if (mesh->Indices.empty() || mesh->Positions.empty())
            {
                error = "meshda uchburchak yo'q";
                return nullptr;
            }
            return mesh;
        }
    }

    // ------------------------------------------------------------------ hull
    std::vector<std::pair<uint32_t, uint32_t>> ConvexHull3D::GetEdges() const
    {
        std::set<std::pair<uint32_t, uint32_t>> edges;
        for (const auto& face : Faces)
            for (size_t i = 0; i < face.size(); i++)
            {
                uint32_t a = face[i], b = face[(i + 1) % face.size()];
                edges.insert({ std::min(a, b), std::max(a, b) });
            }
        return { edges.begin(), edges.end() };
    }

    Ref<const ConvexHull3D> CollisionShapes3D::BuildConvexHull(const std::vector<glm::vec3>& points, int maxVertices, std::string* error)
    {
        auto fail = [&](std::string reason) -> Ref<const ConvexHull3D> {
            if (error)
                *error = std::move(reason);
            return nullptr;
        };
        maxVertices = std::clamp(maxVertices, MinHullVertices, MaxHullVertices);
        // The hull builder allocates through Jolt (which may not be running yet: edit mode).
        std::call_once(s_AllocatorOnce, [] { JPH::RegisterDefaultAllocator(); });

        // Duplicates (a vertex per face corner in most meshes) are left out first.
        std::vector<glm::vec3> unique;
        unique.reserve(points.size());
        {
            std::set<std::tuple<float, float, float>> seen;
            for (const glm::vec3& p : points)
                if (std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) && seen.insert({ p.x, p.y, p.z }).second)
                    unique.push_back(p);
        }
        if (unique.size() < 4)
            return fail("konveks qobiq uchun kamida 4 ta har xil nuqta kerak");

        JPH::ConvexHullBuilder::Positions positions;
        positions.reserve(unique.size());
        for (const glm::vec3& p : unique)
            positions.push_back(JPH::Vec3(p.x, p.y, p.z));
        JPH::ConvexHullBuilder builder(positions);
        const char* reason = nullptr;
        // Points closer to the hull than this are left out (1 mm, or less for tiny meshes).
        glm::vec3 lo = unique[0], hi = unique[0];
        for (const glm::vec3& p : unique)
        {
            lo = glm::min(lo, p);
            hi = glm::max(hi, p);
        }
        const float tolerance = std::clamp(glm::length(hi - lo) * 1.0e-4f, 1.0e-5f, 1.0e-3f);
        const JPH::ConvexHullBuilder::EResult result = builder.Initialize(maxVertices, tolerance, reason);
        if (result != JPH::ConvexHullBuilder::EResult::Success && result != JPH::ConvexHullBuilder::EResult::MaxVerticesReached)
        {
            const bool flat = result == JPH::ConvexHullBuilder::EResult::Degenerate || result == JPH::ConvexHullBuilder::EResult::TooFewFaces;
            return fail(flat ? "nuqtalar hajm hosil qilmaydi (tekis yoki chiziq)" : std::format("qobiq qurilmadi ({})", reason ? reason : "?"));
        }

        auto hull = CreateRef<ConvexHull3D>();
        hull->SourceVertexCount = unique.size();
        hull->Simplified = result == JPH::ConvexHullBuilder::EResult::MaxVerticesReached;
        std::unordered_map<int, uint32_t> corner;
        for (const JPH::ConvexHullBuilder::Face* f : builder.GetFaces())
        {
            if (f->mRemoved || !f->mFirstEdge)
                continue;
            std::vector<uint32_t> face;
            const JPH::ConvexHullBuilder::Edge* e = f->mFirstEdge;
            do
            {
                auto [it, added] = corner.try_emplace(e->mStartIdx, (uint32_t)hull->Points.size());
                if (added)
                    hull->Points.push_back(unique[(size_t)e->mStartIdx]);
                face.push_back(it->second);
                e = e->mNextEdge;
            } while (e && e != f->mFirstEdge);
            if (face.size() >= 3)
                hull->Faces.push_back(std::move(face));
        }
        if (hull->Points.size() < 4 || hull->Faces.size() < 4)
            return fail("nuqtalar hajm hosil qilmaydi (tekis yoki chiziq)");
        hull->Edges = hull->GetEdges();
        hull->BoundsMin = hull->BoundsMax = hull->Points[0];
        for (const glm::vec3& p : hull->Points)
        {
            hull->BoundsMin = glm::min(hull->BoundsMin, p);
            hull->BoundsMax = glm::max(hull->BoundsMax, p);
        }
        return hull;
    }

    // ------------------------------------------------------------------ geometry
    bool CollisionShapes3D::ReadModelGeometry(const fs::path& path, CollisionMesh3D& out, std::string* error)
    {
        out = {};
        auto fail = [&](std::string reason) {
            if (error)
                *error = std::move(reason);
            return false;
        };
        std::error_code ec;
        if (!fs::is_regular_file(path, ec))
            return fail("fayl topilmadi");
        std::string ext = path.extension().string();
        for (char& c : ext)
            c = (char)std::tolower((unsigned char)c);
        const ModelImportSettings settings = AssetImporter::GetModelSettings(path);
        const glm::mat4 importTransform = settings.GetTransform();

        if (ext == ".obj")
        {
            std::vector<MeshVertex> vertices;
            std::string reason;
            if (!MeshLibrary::ReadOBJ(path, vertices, out.Indices, &reason))
                return fail(reason);
            out.Positions.reserve(vertices.size());
            for (const MeshVertex& v : vertices)
                out.Positions.push_back(glm::vec3(importTransform * glm::vec4(v.Position, 1.0f)));
            if (glm::determinant(glm::mat3(importTransform)) < 0.0f)
                for (size_t i = 0; i + 2 < out.Indices.size(); i += 3)
                    std::swap(out.Indices[i + 1], out.Indices[i + 2]);
            return !out.Indices.empty() || fail("faylda uchburchak yo'q");
        }
        if (ext != ".gltf" && ext != ".glb")
            return fail("format qo'llab-quvvatlanmaydi");

        cgltf_options options{};
        cgltf_data* data = nullptr;
        const std::string file = path.string();
        if (cgltf_parse_file(&options, file.c_str(), &data) != cgltf_result_success)
            return fail("glTF fayli o'qilmadi");
        if (cgltf_load_buffers(&options, data, file.c_str()) != cgltf_result_success)
        {
            cgltf_free(data);
            return fail("glTF buferlari yuklanmadi");
        }
        const cgltf_skin* skin = data->skins_count ? &data->skins[0] : nullptr;
        // As the renderer places it: node transforms baked in (skinned meshes: their bind pose).
        auto addPrimitive = [&](const cgltf_primitive& prim, const glm::mat4& world, const cgltf_skin* primSkin) {
            if (prim.type != cgltf_primitive_type_triangles)
                return;
            const cgltf_accessor *pos = nullptr, *joints = nullptr, *weights = nullptr;
            for (cgltf_size i = 0; i < prim.attributes_count; i++)
            {
                const auto& a = prim.attributes[i];
                if (a.type == cgltf_attribute_type_position) pos = a.data;
                else if (a.type == cgltf_attribute_type_joints && a.index == 0) joints = a.data;
                else if (a.type == cgltf_attribute_type_weights && a.index == 0) weights = a.data;
            }
            if (!pos)
                return;
            const bool skinned = skin && primSkin == skin && joints && weights;
            const glm::mat4 transform = importTransform * (skinned ? glm::mat4(1.0f) : world);
            const uint32_t base = (uint32_t)out.Positions.size();
            for (cgltf_size i = 0; i < pos->count; i++)
            {
                float v[4] = { 0, 0, 0, 0 };
                cgltf_accessor_read_float(pos, i, v, 3);
                out.Positions.push_back(glm::vec3(transform * glm::vec4(v[0], v[1], v[2], 1.0f)));
            }
            const size_t first = out.Indices.size();
            if (prim.indices)
                for (cgltf_size i = 0; i < prim.indices->count; i++)
                    out.Indices.push_back(base + (uint32_t)cgltf_accessor_read_index(prim.indices, i));
            else
                for (cgltf_size i = 0; i < pos->count; i++)
                    out.Indices.push_back(base + (uint32_t)i);
            out.Indices.resize(first + (out.Indices.size() - first) / 3 * 3);
            if (glm::determinant(glm::mat3(transform)) < 0.0f)
                for (size_t i = first; i + 2 < out.Indices.size(); i += 3)
                    std::swap(out.Indices[i + 1], out.Indices[i + 2]);
        };
        std::function<void(const cgltf_node*, const glm::mat4&)> visit = [&](const cgltf_node* node, const glm::mat4& parent) {
            float m[16];
            cgltf_node_transform_local(node, m);
            const glm::mat4 world = parent * glm::make_mat4(m);
            if (node->mesh)
                for (cgltf_size i = 0; i < node->mesh->primitives_count; i++)
                    addPrimitive(node->mesh->primitives[i], world, node->skin);
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
                    addPrimitive(data->meshes[i].primitives[p], glm::mat4(1.0f), nullptr);
        cgltf_free(data);
        return !out.Indices.empty() || fail("faylda uchburchakli mesh yo'q");
    }

    // ------------------------------------------------------------------ wireframe
    CollisionShapes3D::EdgeList CollisionShapes3D::BuildFeatureEdges(const CollisionMesh3D& mesh)
    {
        // Corners welded by position: a mesh has a vertex per face corner (seams of normals / UVs).
        std::map<std::tuple<float, float, float>, uint32_t> welded;
        std::vector<uint32_t> corner(mesh.Positions.size());
        std::vector<glm::vec3> points;
        for (size_t i = 0; i < mesh.Positions.size(); i++)
        {
            const glm::vec3& p = mesh.Positions[i];
            auto [it, added] = welded.try_emplace({ p.x, p.y, p.z }, (uint32_t)points.size());
            if (added)
                points.push_back(p);
            corner[i] = it->second;
        }
        // Each edge: the normals of the triangles that share it.
        struct Shared
        {
            glm::vec3 Normal{ 0.0f };
            int Count = 0;
            bool Crease = false;
        };
        std::map<std::pair<uint32_t, uint32_t>, Shared> edges;
        for (size_t t = 0; t + 2 < mesh.Indices.size(); t += 3)
        {
            uint32_t v[3];
            bool valid = true;
            for (int k = 0; k < 3; k++)
            {
                valid &= mesh.Indices[t + k] < corner.size();
                v[k] = valid ? corner[mesh.Indices[t + k]] : 0;
            }
            if (!valid || v[0] == v[1] || v[1] == v[2] || v[0] == v[2])
                continue;
            const glm::vec3 n = glm::cross(points[v[1]] - points[v[0]], points[v[2]] - points[v[0]]);
            const float length = glm::length(n);
            if (!(length > 1e-12f))
                continue;
            const glm::vec3 normal = n / length;
            for (int k = 0; k < 3; k++)
            {
                const uint32_t a = v[k], b = v[(k + 1) % 3];
                Shared& edge = edges[{ std::min(a, b), std::max(a, b) }];
                if (edge.Count == 0)
                    edge.Normal = normal;
                else if (glm::dot(edge.Normal, normal) < 0.9995f)
                    edge.Crease = true;
                edge.Count++;
            }
        }
        EdgeList out;
        for (const auto& [key, edge] : edges)
            if (edge.Count != 2 || edge.Crease)
                out.push_back({ points[key.first], points[key.second] });
        return out;
    }

    Ref<const CollisionShapes3D::EdgeList> CollisionShapes3D::GetFeatureEdges(const Ref<const CollisionMesh3D>& mesh)
    {
        if (!mesh)
            return nullptr;
        {
            std::lock_guard lock(s_Mutex);
            if (auto it = s_Edges.find(mesh.get()); it != s_Edges.end() && it->second.Mesh.lock() == mesh)
                return it->second.Edges;
        }
        auto edges = CreateRef<const EdgeList>(BuildFeatureEdges(*mesh));
        std::lock_guard lock(s_Mutex);
        std::erase_if(s_Edges, [](const auto& item) { return item.second.Mesh.expired(); });
        s_Edges[mesh.get()] = { mesh, edges };
        return edges;
    }

    // ------------------------------------------------------------------ cache
    Ref<const CollisionMesh3D> CollisionShapes3D::GetMesh(const std::string& modelPath, PrimitiveMesh primitive, std::string* error)
    {
        const std::string key = SourceKey(modelPath, primitive);
        const SourceStamp stamp = ReadStamp(modelPath);
        std::lock_guard lock(s_Mutex);
        auto it = s_Meshes.find(key);
        if (it == s_Meshes.end() || !(it->second.Stamp == stamp))
        {
            MeshEntry entry;
            entry.Stamp = stamp;
            entry.Mesh = ReadMesh(modelPath, primitive, entry.Error);
            it = s_Meshes.insert_or_assign(key, std::move(entry)).first;
        }
        if (error)
            *error = it->second.Error;
        return it->second.Mesh;
    }

    Ref<const ConvexHull3D> CollisionShapes3D::GetConvexHull(const std::string& modelPath, PrimitiveMesh primitive, int maxVertices,
                                                             std::string* error)
    {
        maxVertices = std::clamp(maxVertices, MinHullVertices, MaxHullVertices);
        const std::string key = std::format("{}#{}", SourceKey(modelPath, primitive), maxVertices);
        const SourceStamp stamp = ReadStamp(modelPath);
        {
            std::lock_guard lock(s_Mutex);
            if (auto it = s_Hulls.find(key); it != s_Hulls.end() && it->second.Stamp == stamp)
            {
                if (error)
                    *error = it->second.Error;
                return it->second.Hull;
            }
        }
        HullEntry entry;
        entry.Stamp = stamp;
        if (Ref<const CollisionMesh3D> mesh = GetMesh(modelPath, primitive, &entry.Error))
            entry.Hull = BuildConvexHull(mesh->Positions, maxVertices, &entry.Error);
        if (error)
            *error = entry.Error;
        Ref<const ConvexHull3D> hull = entry.Hull;
        std::lock_guard lock(s_Mutex);
        s_Hulls.insert_or_assign(key, std::move(entry));
        return hull;
    }

    size_t CollisionShapes3D::GetCacheSize()
    {
        std::lock_guard lock(s_Mutex);
        return s_Meshes.size() + s_Hulls.size();
    }

    void CollisionShapes3D::Clear()
    {
        std::lock_guard lock(s_Mutex);
        s_Meshes.clear();
        s_Hulls.clear();
        s_Edges.clear();
    }

}
