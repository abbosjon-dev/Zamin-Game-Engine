#include "Zamin/AI/NavCarving.h"

#include "Zamin/AI/NavGridBuilder.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace Zamin {

    namespace {

        float Cross(const glm::vec2& o, const glm::vec2& a, const glm::vec2& b) { return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x); }

        float SegmentDistance(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b)
        {
            const glm::vec2 ab = b - a;
            const float length2 = glm::dot(ab, ab);
            const float t = length2 > 1e-12f ? std::clamp(glm::dot(p - a, ab) / length2, 0.0f, 1.0f) : 0.0f;
            return glm::length(p - (a + ab * t));
        }

        bool Finite(const glm::vec2& v) { return std::isfinite(v.x) && std::isfinite(v.y); }

    }

    std::vector<glm::vec2> NavCarving::ConvexHull(std::vector<glm::vec2> points)
    {
        // Andrew's monotone chain.
        points.erase(std::remove_if(points.begin(), points.end(), [](const glm::vec2& p) { return !Finite(p); }), points.end());
        std::sort(points.begin(), points.end(), [](const glm::vec2& a, const glm::vec2& b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
        points.erase(std::unique(points.begin(), points.end(), [](const glm::vec2& a, const glm::vec2& b) { return glm::length(a - b) < 1e-6f; }),
                     points.end());
        if (points.size() < 3)
            return points;
        std::vector<glm::vec2> hull(points.size() * 2);
        size_t k = 0;
        for (size_t i = 0; i < points.size(); i++)
        {
            while (k >= 2 && Cross(hull[k - 2], hull[k - 1], points[i]) <= 1e-9f)
                k--;
            hull[k++] = points[i];
        }
        for (size_t i = points.size() - 1, t = k + 1; i > 0; i--)
        {
            while (k >= t && Cross(hull[k - 2], hull[k - 1], points[i - 1]) <= 1e-9f)
                k--;
            hull[k++] = points[i - 1];
        }
        hull.resize(k - 1);
        return hull;
    }

    float NavCarving::Distance(const NavCarveShape& shape, const glm::vec2& point)
    {
        if (shape.IsCircle())
        {
            if (shape.Outline.size() == 2) // a flat box seen edge on: a segment
                return SegmentDistance(point, shape.Outline[0], shape.Outline[1]);
            return std::max(0.0f, glm::length(point - shape.Center) - std::max(0.0f, shape.Radius));
        }
        if (NavGridBuilder::PointInPolygon(shape.Outline, point))
            return 0.0f;
        float best = std::numeric_limits<float>::max();
        for (size_t i = 0; i < shape.Outline.size(); i++)
            best = std::min(best, SegmentDistance(point, shape.Outline[i], shape.Outline[(i + 1) % shape.Outline.size()]));
        return best;
    }

    void NavCarving::GetBounds(const NavCarveShape& shape, glm::vec2& min, glm::vec2& max)
    {
        if (shape.IsCircle() && shape.Outline.size() != 2)
        {
            const float r = std::max(0.0f, shape.Radius);
            min = shape.Center - r;
            max = shape.Center + r;
            return;
        }
        min = glm::vec2(std::numeric_limits<float>::max());
        max = glm::vec2(std::numeric_limits<float>::lowest());
        for (const glm::vec2& p : shape.Outline)
        {
            min = glm::min(min, p);
            max = glm::max(max, p);
        }
    }

    void NavCarving::CollectCells(const NavGrid& grid, const NavCarveShape& shape, float clearance, float stepHeight, float agentHeight,
                                  std::vector<uint32_t>& out)
    {
        out.clear();
        if (grid.IsEmpty())
            return;
        const bool segment = shape.Outline.size() == 2;
        if (shape.IsCircle() && !segment && (!Finite(shape.Center) || !std::isfinite(shape.Radius) || !(shape.Radius > 0.0f)))
            return;
        clearance = std::isfinite(clearance) ? std::max(0.0f, clearance) : 0.0f;
        glm::vec2 lo, hi;
        GetBounds(shape, lo, hi);
        if (!Finite(lo) || !Finite(hi) || lo.x > hi.x || lo.y > hi.y)
            return;
        lo -= clearance;
        hi += clearance;
        glm::ivec2 c0 = grid.WorldToCell(lo), c1 = grid.WorldToCell(hi);
        if (c1.x < 0 || c1.y < 0 || c0.x >= grid.GetWidth() || c0.y >= grid.GetHeight())
            return;
        c0 = grid.ClampCell(c0);
        c1 = grid.ClampCell(c1);

        const bool heights = grid.HasHeights();
        stepHeight = std::isfinite(stepHeight) ? std::max(0.0f, stepHeight) : 0.0f;
        const float inset = grid.GetCellSize() * 1e-3f;
        for (int y = c0.y; y <= c1.y; y++)
            for (int x = c0.x; x <= c1.x; x++)
            {
                if (heights)
                {
                    const float floor = grid.GetFloorHeight(x, y);
                    if (!std::isfinite(floor))
                        continue; // nothing to stand on: never walkable
                    if (!(shape.Top > floor + stepHeight) || (agentHeight > 0.0f && !(shape.Bottom < floor + agentHeight)))
                        continue;
                }
                const glm::vec2 min = grid.GridToWorld(glm::vec2((float)x, (float)y)) + inset;
                const glm::vec2 max = grid.GridToWorld(glm::vec2((float)(x + 1), (float)(y + 1))) - inset;
                const glm::vec2 center = (min + max) * 0.5f;
                bool carve = clearance > 0.0f && Distance(shape, center) < clearance;
                if (!carve)
                {
                    if (segment)
                        carve = NavGridBuilder::SegmentIntersectsRect(shape.Outline[0], shape.Outline[1], min, max);
                    else if (shape.IsCircle())
                    {
                        const glm::vec2 nearest = glm::clamp(shape.Center, min, max);
                        carve = glm::dot(nearest - shape.Center, nearest - shape.Center) < shape.Radius * shape.Radius;
                    }
                    else
                    {
                        // The centre inside, or an edge through the cell (a small outline inside it too).
                        carve = NavGridBuilder::PointInPolygon(shape.Outline, center);
                        for (size_t i = 0; i < shape.Outline.size() && !carve; i++)
                            carve = NavGridBuilder::SegmentIntersectsRect(shape.Outline[i], shape.Outline[(i + 1) % shape.Outline.size()], min, max);
                    }
                }
                if (carve)
                    out.push_back((uint32_t)grid.Index(x, y));
            }
    }

    void NavCarving::Diff(const std::vector<uint32_t>& before, const std::vector<uint32_t>& now, std::vector<uint32_t>& added,
                          std::vector<uint32_t>& removed)
    {
        added.clear();
        removed.clear();
        std::set_difference(now.begin(), now.end(), before.begin(), before.end(), std::back_inserter(added));
        std::set_difference(before.begin(), before.end(), now.begin(), now.end(), std::back_inserter(removed));
    }

}
