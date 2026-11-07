#pragma once

#include "Zamin/AI/NavGrid.h"

#include <glm/glm.hpp>

#include <vector>

namespace Zamin {

    // Turning a path of cells (NavPathfinder) into the points an agent walks through, on the grid's
    // plane (world units):
    //   points = NavPathSmoothing::ToPoints(grid, result.Cells, &start, &goal); // the zig-zag of cell centres
    //   points = NavPathSmoothing::StringPull(grid, points);                    // only the corners it must turn at
    //   points = NavPathSmoothing::SplineSmooth(grid, points, 0.25f);           // rounded corners (optional)
    struct NavPathSmoothing
    {
        // The centres of the cells; `start` / `goal` (when given) replace the first / last centre - the
        // agent's exact position and the exact destination (the goal only when it lies in the last
        // cell: a partial path ends at its last cell's centre).
        static std::vector<glm::vec2> ToPoints(const NavGrid& grid, const std::vector<glm::ivec2>& cells, const glm::vec2* start = nullptr,
                                               const glm::vec2* goal = nullptr);

        // String pulling: from each kept point, the farthest later point it can walk to straight
        // (NavGrid::HasLineOfSight) is the next one kept. The ends always stay; a straight run of
        // the original points becomes one segment. The result walks only walkable cells.
        static std::vector<glm::vec2> StringPull(const NavGrid& grid, const std::vector<glm::vec2>& points);

        // A centripetal Catmull-Rom curve through the points, sampled about every `spacing` world
        // units (at least 2 samples per segment). Where a curved piece would leave the walkable cells
        // (cut a corner) the straight segment is kept there instead, so the result walks only cells the
        // straight path did.
        static std::vector<glm::vec2> SplineSmooth(const NavGrid& grid, const std::vector<glm::vec2>& points, float spacing = 0.25f);

        // Lengths along a polyline.
        static float GetLength(const std::vector<glm::vec2>& points);
        static float GetLength(const std::vector<glm::vec3>& points);
    };

}
