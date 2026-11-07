#pragma once

#include "Zamin/AI/NavGrid.h"

#include <glm/glm.hpp>

#include <cstddef>
#include <vector>

namespace Zamin {

    struct TilemapComponent;
    class TilesetProperties;

    // Rasterising a 2D level into a NavGrid (world XY = the grid's plane): each Block* function
    // blocks the cells a shape overlaps (touching a cell's border does not count) and returns how
    // many cells it newly blocked. NavGrid2DComponent's bake uses them for tilemaps and static 2D
    // colliders; they need no scene (tests and tools call them directly).
    struct NavGridBuilder
    {
        // The cells under a tilemap's colliding tiles (TilemapCollision::GetCell: Solid, OneWay too
        // with oneWayBlocks; nothing when the tilemap's Collision is off). `world`: the tilemap's
        // world transform (tile (x, y) covers [x, x + 1) x [y, y + 1) times TileSize in its own space).
        // Each cell is sampled often enough that every tile overlapping it is seen.
        static size_t BlockTilemap(NavGrid& grid, const TilemapComponent& tilemap, const glm::mat4& world, const TilesetProperties* properties,
                                   bool oneWayBlocks);
        // A closed outline (convex or concave, in world units).
        static size_t BlockPolygon(NavGrid& grid, const std::vector<glm::vec2>& outline);
        static size_t BlockCircle(NavGrid& grid, const glm::vec2& center, float radius);
        // A wall without thickness (edge colliders): every cell the segment crosses.
        static size_t BlockSegment(NavGrid& grid, const glm::vec2& a, const glm::vec2& b);

        // The world rectangle a tilemap covers (all of its Width x Height cells).
        static void GetTilemapBounds(const TilemapComponent& tilemap, const glm::mat4& world, glm::vec2& min, glm::vec2& max);

        // ---- shape tests (world units)
        static bool PointInPolygon(const std::vector<glm::vec2>& outline, const glm::vec2& point); // even-odd rule
        // Whether the segment passes through the rectangle [min, max].
        static bool SegmentIntersectsRect(const glm::vec2& a, const glm::vec2& b, const glm::vec2& min, const glm::vec2& max);
    };

}
