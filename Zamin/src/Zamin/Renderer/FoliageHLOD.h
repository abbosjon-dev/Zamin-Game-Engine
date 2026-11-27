#pragma once
#include "Zamin/Renderer/Mesh.h"
#include "Zamin/Math/Geometry.h"
#include <glm/glm.hpp>
#include <cstdint>
#include <span>
#include <vector>

namespace Zamin {
    // One HLOD cell: the instances of a square of the world X/Z grid, collapsed into a few clump pyramids.
    struct FoliageHLODCell
    {
        glm::ivec2 Coord{ 0 };
        AABB Bounds = AABB::Empty(); // world bounds of the cell's instances (the proxy is inside them)
        uint32_t Instances = 0;
        uint64_t Hash = 0;           // the cell's source: its instance transforms in order, the prototype, the cell size
        uint32_t FirstIndex = 0, IndexCount = 0;
        uint32_t FirstVertex = 0, VertexCount = 0;
    };
    // The baked proxies of one packed foliage batch: CPU data only (FoliageBatch makes the GPU mesh).
    struct FoliageHLODBake
    {
        float CellSize = 0;               // effective: doubled from the requested size until at most MaxCells remain
        glm::vec2 Origin{ 0 };            // world X/Z of cell (0, 0)
        glm::ivec2 Dimensions{ 0 };
        std::vector<FoliageHLODCell> Cells;  // non-empty cells ordered by (z, x)
        std::vector<uint32_t> InstanceCell;  // per instance: index into Cells
        std::vector<MeshVertex> Vertices;    // world space; TexCoord unused (0)
        std::vector<uint32_t> Indices;
        uint64_t Hash = 0;                // the whole bake: cell table and geometry
        uint32_t RebakedCells = 0, ReusedCells = 0;
        bool IsValid() const { return !Cells.empty(); }
    };
    namespace FoliageHLOD {
        constexpr uint32_t MaxCells = 4096;
        constexpr uint32_t ClumpsPerSide = 4; // a cell holds up to 4 x 4 clumps
        constexpr uint32_t FadeLevels = 16;   // the dither crossfade's steps (16: the proxy alone)
        // Deterministic: the same transforms, prototype and cell size always give the same bytes and hash. With
        // `previous` on the same grid, cells whose content hash is unchanged keep their geometry (ReusedCells); only
        // cells that a paint/regenerate/projection edit touched are rebuilt (RebakedCells).
        FoliageHLODBake Bake(std::span<const glm::mat4> transforms, const AABB& prototype, float cellSize,
                             const FoliageHLODBake* previous = nullptr);
        // The crossfade step of a cell `distance` away: 0 below `start` (instances only), FadeLevels at or past
        // start + width (the proxy only), between them the dithered transition. A width of 0 switches at once.
        uint32_t FadeLevel(float distance, float start, float width);
    }
}
