#include "Zamin/Scene/Foliage.h"
#include "Zamin/Scene/Terrain.h"
#include "Zamin/Project/Project.h"
#include "Zamin/Renderer/Model.h"
#include "Zamin/Utils/FileSystem.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <cmath>
#include <random>

namespace Zamin {
    struct FoliageComponent::Cache
    {
        Ref<FoliageBatch> Batch = CreateRef<FoliageBatch>();
        Ref<Model> Models[2];
        std::string Paths[2];
        std::vector<FoliageInstance> Snapshot;
        std::vector<glm::mat4> Transforms;
        glm::mat4 World{ 1 };
        bool Valid = false;
        AABB Prototype{ { -.5f, -.5f, -.5f }, { .5f, .5f, .5f } };
    };
    FoliageComponent::FoliageComponent(const FoliageComponent& o) { *this = o; }
    FoliageComponent& FoliageComponent::operator=(const FoliageComponent& o)
    {
        if (this != &o)
        {
            NearType = o.NearType;
            FarType = o.FarType;
            NearModel = o.NearModel;
            FarModel = o.FarModel;
            Tint = o.Tint;
            Settings = o.Settings;
            CastShadows = o.CastShadows;
            Instances = o.Instances;
            Runtime.reset();
        }
        return *this;
    }
    void FoliageComponent::Sanitize()
    {
        Settings.Sanitize();
        if (uint32_t(NearType) > 5) NearType = PrimitiveMesh::Cone;
        if (uint32_t(FarType) > 5) FarType = PrimitiveMesh::Cube;
        if (Instances.size() > FoliageBatch::MaxInstances) Instances.resize(FoliageBatch::MaxInstances);
        for (int i = 0; i < 4; ++i) Tint[i] = std::isfinite(Tint[i]) ? std::clamp(Tint[i], 0.f, 1.f) : 1;
        for (auto& instance : Instances)
        {
            for (int i = 0; i < 3; ++i)
            {
                instance.Position[i] = std::isfinite(instance.Position[i]) ? std::clamp(instance.Position[i], -100000.f, 100000.f) : 0;
                instance.Rotation[i] = std::isfinite(instance.Rotation[i]) ? std::remainder(instance.Rotation[i], 6.2831853f) : 0;
            }
            instance.Scale = std::isfinite(instance.Scale) ? std::clamp(instance.Scale, .01f, 1000.f) : 1;
        }
    }
    namespace FoliageTools {
        AABB LocalBounds(const FoliageComponent& c)
        {
            AABB result = AABB::Empty();
            AABB prototype = c.Runtime ? c.Runtime->Prototype : AABB{ { -.5f, -.5f, -.5f }, { .5f, .5f, .5f } };
            for (const auto& i : c.Instances) result.Encapsulate(TransformAABB(prototype, glm::translate(glm::mat4(1), i.Position) * glm::toMat4(glm::quat(i.Rotation)) * glm::scale(glm::mat4(1), glm::vec3(i.Scale))));
            return result;
        }
        uint32_t ProjectToTerrain(FoliageComponent& c, const TerrainComponent& terrain, const glm::mat4& rootToTerrain, float offset)
        {
            if (!std::isfinite(offset) || !std::isfinite(glm::determinant(rootToTerrain)) || std::abs(glm::determinant(rootToTerrain)) < 1e-8f) return 0;
            auto inverse = glm::inverse(rootToTerrain);
            uint32_t changed = 0;
            for (auto& i : c.Instances)
            {
                auto p = glm::vec3(rootToTerrain * glm::vec4(i.Position, 1));
                if (p.x < 0 || p.z < 0 || p.x > terrain.Size.x || p.z > terrain.Size.y) continue;
                float h = TerrainTools::SampleHeight(terrain, p.x, p.z) + offset;
                if (std::abs(p.y - h) > 1e-5f)
                {
                    p.y = h;
                    i.Position = glm::vec3(inverse * glm::vec4(p, 1));
                    ++changed;
                }
            }
            return changed;
        }
        Ref<FoliageBatch> Prepare(FoliageComponent& c, const glm::mat4& world, int id)
        {
            c.Sanitize();
            if (!c.Runtime) c.Runtime = CreateRef<FoliageComponent::Cache>();
            auto& cache = *c.Runtime;
            auto batch = cache.Batch;
            batch->Settings = c.Settings;
            batch->CastShadows = c.CastShadows;
            batch->Mirrored = glm::determinant(glm::mat3(world)) < 0;
            AABB bounds = AABB::Empty();
            for (int lod = 0; lod < 2; ++lod)
            {
                auto& parts = batch->Parts[lod];
                parts.clear();
                auto type = lod ? c.FarType : c.NearType;
                const auto& path = lod ? c.FarModel : c.NearModel;
                if (type == PrimitiveMesh::Model)
                {
                    if (cache.Paths[lod] != path)
                    {
                        cache.Paths[lod] = path;
                        cache.Models[lod].reset();
                    }
                    auto file = FileSystem::SafeJoin(Project::GetAssetDirectory(), path);
                    if (!cache.Models[lod] && !file.empty() && FileSystem::IsSubPathOnDisk(file, Project::GetAssetDirectory())) cache.Models[lod] = Model::Load(file);
                    if (cache.Models[lod])
                        for (const auto& sub : cache.Models[lod]->GetSubMeshes())
                        {
                            if (!sub.MeshRef || sub.MeshRef->IsSkinned() || sub.Morph || sub.Mat.BlendMode == MaterialBlendMode::Transparent || parts.size() >= 128) return nullptr;
                            auto mat = sub.Mat;
                            mat.Albedo *= c.Tint;
                            parts.push_back({ sub.MeshRef, mat });
                            bounds.Encapsulate(sub.MeshRef->GetBounds());
                        }
                }
                else
                {
                    auto mesh = MeshLibrary::GetPrimitive(type);
                    Material mat;
                    mat.Albedo = c.Tint;
                    mat.Roughness = .9f;
                    parts.push_back({ mesh, mat });
                    bounds.Encapsulate(mesh->GetBounds());
                }
            }
            if (batch->Parts[0].empty() || batch->Parts[1].empty()) return nullptr;
            if (!cache.Valid || cache.World != world || cache.Snapshot != c.Instances)
            {
                cache.Snapshot = c.Instances;
                cache.World = world;
                cache.Valid = true;
                cache.Transforms.clear();
                cache.Transforms.reserve(c.Instances.size());
                for (const auto& instance : c.Instances) cache.Transforms.push_back(world * glm::translate(glm::mat4(1), instance.Position) * glm::toMat4(glm::quat(instance.Rotation)) * glm::scale(glm::mat4(1), glm::vec3(instance.Scale)));
            }
            cache.Prototype = bounds;
            if (!batch->Upload(cache.Transforms, bounds, id)) return nullptr;
            return batch;
        }
        void GenerateGrid(FoliageComponent& c, uint32_t count, glm::vec2 size, uint32_t seed)
        {
            count = std::min(count, FoliageBatch::MaxInstances);
            size = glm::max(size, glm::vec2(1));
            c.Instances.clear();
            uint32_t side = uint32_t(std::ceil(std::sqrt(float(count))));
            if (!side) return;
            std::mt19937 rng(seed);
            std::uniform_real_distribution<float> unit(0, 1);
            for (uint32_t i = 0; i < count; ++i) c.Instances.push_back({ { (float(i % side) + .2f + .6f * unit(rng)) * size.x / float(side), 0, (float(i / side) + .2f + .6f * unit(rng)) * size.y / float(side) }, { 0, unit(rng) * 6.2831853f, 0 }, .8f + .4f * unit(rng) });
            c.Runtime.reset();
        }
    }
}
