#pragma once

#include "Zamin/Scene/Terrain.h"
#include <span>

namespace Zamin {
    inline constexpr uint32_t TerrainPatchCells = 32;
    inline constexpr uint8_t TerrainMaxLOD = 5;
    // Shared edges are ordered -X, +X, -Z, +Z. Coordinates are global heightfield indices.
    struct TerrainPatch
    {
        uint32_t X = 0, Z = 0, CellsX = 0, CellsZ = 0;
        glm::vec3 BoundsMin{}, BoundsMax{};
        uint8_t LOD = 0;
        std::array<uint8_t, 4> Neighbors{};
    };
    struct TerrainRenderStats
    {
        uint32_t Patches = 0, RebuiltPatches = 0, Vertices = 0, Triangles = 0, FullResolutionTriangles = 0;
        std::array<uint32_t, TerrainMaxLOD + 1> LODCounts{};
    };
    namespace TerrainTools {
        // These three functions are CPU-only, also usable in tools/tests without a GL context.
        std::vector<TerrainPatch> BuildPatchLayout(const TerrainComponent& terrain);
        void SelectPatchLODs(std::vector<TerrainPatch>& patches, const TerrainComponent& terrain,
                             const glm::mat4& world, const glm::vec3& camera,
                             std::span<const uint8_t> previous = {}, bool fullDetail = false);
        // Fine boundaries follow the coarse neighbor's exact edge, avoiding cracks without skirts.
        void BuildPatchMesh(const TerrainComponent& terrain, const TerrainPatch& patch,
                            std::vector<MeshVertex>& vertices, std::vector<uint32_t>& indices);
        const std::vector<Ref<Mesh>>& GetRenderPatches(const TerrainComponent& terrain,
                                                       const glm::mat4& world, const glm::vec3& camera, bool fullDetail = false);
        TerrainRenderStats GetRenderStats(const TerrainComponent& terrain);
    }
}
