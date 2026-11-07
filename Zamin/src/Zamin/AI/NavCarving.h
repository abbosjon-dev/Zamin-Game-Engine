#pragma once

#include "Zamin/AI/NavGrid.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <limits>
#include <vector>

namespace Zamin {

    // The footprint of a dynamic obstacle (NavObstacleComponent) on a grid's plane, in world units:
    // a convex outline, or a circle when the outline has fewer than 3 points. On 3D grids Bottom / Top
    // are the world heights it spans (an agent steps over what ends below its step height and walks
    // under what starts above its head).
    struct NavCarveShape
    {
        std::vector<glm::vec2> Outline; // convex, either winding
        glm::vec2 Center{ 0.0f };       // circle
        float Radius = 0.0f;
        float Bottom = std::numeric_limits<float>::lowest();
        float Top = std::numeric_limits<float>::max();

        bool IsCircle() const { return Outline.size() < 3; }
    };

    // Rasterising obstacles for NavGrid::Carve (the scene does it for Nav Obstacles every time one
    // moved further than its MoveThreshold; only the cells that differ are carved / uncarved):
    //   NavCarveShape shape;
    //   shape.Outline = NavCarving::ConvexHull(corners);
    //   std::vector<uint32_t> cells;
    //   NavCarving::CollectCells(grid, shape, agentRadius, 0.0f, 0.0f, cells);
    //   grid.Carve(cells);
    struct NavCarving
    {
        // The cells (NavGrid::Index, ascending, each once) the shape blocks: the cells it overlaps
        // (touching a cell's border does not count) and those whose centre is nearer than `clearance`
        // (the agents' radius) to it. Grids with heights: only cells with a floor where the shape is
        // between floor + stepHeight and floor + agentHeight (agentHeight <= 0: any height above the
        // step).
        static void CollectCells(const NavGrid& grid, const NavCarveShape& shape, float clearance, float stepHeight, float agentHeight,
                                 std::vector<uint32_t>& out);

        // The convex hull (counter-clockwise; duplicate and collinear points dropped). Fewer than 3
        // distinct points: what is left of them.
        static std::vector<glm::vec2> ConvexHull(std::vector<glm::vec2> points);
        // How far a point is from the shape (0 inside it).
        static float Distance(const NavCarveShape& shape, const glm::vec2& point);
        static void GetBounds(const NavCarveShape& shape, glm::vec2& min, glm::vec2& max);

        // What changed between two sorted cell lists: `added` - in `now` only, `removed` - in `before` only.
        static void Diff(const std::vector<uint32_t>& before, const std::vector<uint32_t>& now, std::vector<uint32_t>& added,
                         std::vector<uint32_t>& removed);
    };

}
