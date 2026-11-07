#include "Zamin/AI/NavPathSmoothing.h"

#include "Zamin/Math/Spline.h"

#include <algorithm>
#include <cmath>

namespace Zamin {

    std::vector<glm::vec2> NavPathSmoothing::ToPoints(const NavGrid& grid, const std::vector<glm::ivec2>& cells, const glm::vec2* start,
                                                      const glm::vec2* goal)
    {
        std::vector<glm::vec2> points;
        points.reserve(cells.size() + 2);
        for (const glm::ivec2& cell : cells)
            points.push_back(grid.CellToWorld(cell));
        if (points.empty())
            return points;
        if (start)
        {
            // Inside the first cell: the exact position replaces its centre; elsewhere (a start moved
            // out of a blocked cell) it comes before it.
            if (grid.WorldToCell(*start) == cells.front())
                points.front() = *start;
            else
                points.insert(points.begin(), *start);
        }
        if (goal && grid.WorldToCell(*goal) == cells.back())
        {
            if (points.size() == 1 && start)
                points.push_back(*goal); // start and goal in one cell: a straight step
            else
                points.back() = *goal;
        }
        return points;
    }

    std::vector<glm::vec2> NavPathSmoothing::StringPull(const NavGrid& grid, const std::vector<glm::vec2>& points)
    {
        if (points.size() <= 2)
            return points;
        std::vector<glm::vec2> out;
        out.push_back(points.front());
        size_t anchor = 0;
        const size_t last = points.size() - 1;
        while (anchor < last)
        {
            size_t next = anchor + 1;
            while (next < last && grid.HasLineOfSight(points[anchor], points[next + 1]))
                next++;
            out.push_back(points[next]);
            anchor = next;
        }
        return out;
    }

    std::vector<glm::vec2> NavPathSmoothing::SplineSmooth(const NavGrid& grid, const std::vector<glm::vec2>& points, float spacing)
    {
        if (points.size() < 3)
            return points;
        if (!std::isfinite(spacing) || spacing <= 0.0f)
            spacing = 0.25f;
        spacing = std::max(spacing, grid.GetCellSize() * 0.05f);
        const size_t n = points.size();
        auto at = [&](long i) -> glm::vec3 {
            // Ends extended straight on (no doubled point: the curve leaves / reaches them along the path).
            if (i < 0)
                return glm::vec3(points[0] * 2.0f - points[1], 0.0f);
            if (i >= (long)n)
                return glm::vec3(points[n - 1] * 2.0f - points[n - 2], 0.0f);
            return glm::vec3(points[(size_t)i], 0.0f);
        };
        std::vector<glm::vec2> out;
        out.push_back(points.front());
        std::vector<glm::vec2> piece;
        for (size_t i = 0; i + 1 < n; i++)
        {
            const glm::vec3 p0 = at((long)i - 1), p1 = at((long)i), p2 = at((long)i + 1), p3 = at((long)i + 2);
            const float length = glm::length(glm::vec2(p2 - p1));
            const int samples = std::clamp((int)std::ceil(length / spacing), 2, 256);
            piece.clear();
            glm::vec2 previous(p1);
            bool clear = true;
            for (int s = 1; s <= samples; s++)
            {
                const float t = (float)s / (float)samples;
                const glm::vec2 q = s == samples ? glm::vec2(p2) : glm::vec2(SplineMath::CatmullRom(p0, p1, p2, p3, t));
                if (!std::isfinite(q.x) || !std::isfinite(q.y) || !grid.HasLineOfSight(previous, q))
                {
                    clear = false;
                    break;
                }
                piece.push_back(q);
                previous = q;
            }
            if (clear)
                out.insert(out.end(), piece.begin(), piece.end());
            else
                out.push_back(glm::vec2(p2));
        }
        return out;
    }

    float NavPathSmoothing::GetLength(const std::vector<glm::vec2>& points)
    {
        float length = 0.0f;
        for (size_t i = 1; i < points.size(); i++)
            length += glm::length(points[i] - points[i - 1]);
        return length;
    }

    float NavPathSmoothing::GetLength(const std::vector<glm::vec3>& points)
    {
        float length = 0.0f;
        for (size_t i = 1; i < points.size(); i++)
            length += glm::length(points[i] - points[i - 1]);
        return length;
    }

}
