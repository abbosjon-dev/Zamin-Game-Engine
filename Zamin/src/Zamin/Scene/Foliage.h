#pragma once
#include "Zamin/Renderer/FoliageBatch.h"
#include <string>

namespace Zamin {
    struct TerrainComponent;
    struct FoliageInstance
    {
        glm::vec3 Position{ 0 }, Rotation{ 0 };
        float Scale = 1;
        bool operator==(const FoliageInstance&) const = default;
    };
    struct FoliageComponent
    {
        PrimitiveMesh NearType = PrimitiveMesh::Cone, FarType = PrimitiveMesh::Cube;
        std::string NearModel, FarModel;
        glm::vec4 Tint{ .3f, .6f, .18f, 1 };
        FoliageSettings Settings;
        bool CastShadows = true;
        std::vector<FoliageInstance> Instances;
        struct Cache;
        mutable Ref<Cache> Runtime;
        FoliageComponent() = default;
        FoliageComponent(const FoliageComponent& other);
        FoliageComponent& operator=(const FoliageComponent& other);
        void Sanitize();
    };
    namespace FoliageTools {
        AABB LocalBounds(const FoliageComponent& foliage);
        uint32_t ProjectToTerrain(FoliageComponent& foliage, const TerrainComponent& terrain, const glm::mat4& rootToTerrain = glm::mat4(1), float offset = 0);
        Ref<FoliageBatch> Prepare(FoliageComponent& foliage, const glm::mat4& world, int entityID);
        void GenerateGrid(FoliageComponent& foliage, uint32_t count, glm::vec2 size, uint32_t seed = 42);
    }
}
