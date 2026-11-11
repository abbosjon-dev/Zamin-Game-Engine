#include "Zamin/AI/NavAtlas.h"
#include <algorithm>
#include <cmath>
namespace Zamin {
    namespace {
        bool FiniteSegment(glm::vec2 p) { return std::isfinite(p.x) && std::isfinite(p.y); }
        bool Intersects(glm::dvec2 a, glm::dvec2 b, glm::dvec2 low, glm::dvec2 high)
        {
            const auto delta = b - a;
            double enter = 0, exit = 1;
            for (int axis = 0; axis < 2; ++axis)
            {
                if (std::abs(delta[axis]) < 1e-14)
                {
                    if (a[axis] < low[axis] || a[axis] > high[axis]) return false;
                }
                else
                {
                    double first = (low[axis] - a[axis]) / delta[axis], last = (high[axis] - a[axis]) / delta[axis];
                    if (first > last) std::swap(first, last);
                    enter = std::max(enter, first);
                    exit = std::min(exit, last);
                    if (enter > exit) return false;
                }
            }
            return true;
        }
    }
    bool NavAtlas::CanWalkSegment(glm::vec2 from, glm::vec2 to, const NavSegmentOptions& options) const
    {
        if (m_Tiles.empty() || !FiniteSegment(from) || !FiniteSegment(to) || !std::isfinite(options.Radius) ||
            options.Radius < 0 || options.Radius > 100 || !options.MaxCells || options.MaxCells > 65536) return false;
        const auto& first = m_Tiles.front().Grid;
        const double size = first.GetCellSize();
        const glm::dvec2 origin(first.GetOrigin()), a(from), b(to);
        const double radius = options.Radius + size * 1e-6; // reject corner and boundary grazing
        const auto low = glm::floor((glm::min(a, b) - radius - origin) / size);
        const auto high = glm::floor((glm::max(a, b) + radius - origin) / size);
        if (glm::any(glm::greaterThan(glm::abs(low), glm::dvec2(1e9))) || glm::any(glm::greaterThan(glm::abs(high), glm::dvec2(1e9)))) return false;
        const double width = high.x - low.x + 1, height = high.y - low.y + 1;
        if (width * height > options.MaxCells) return false;
        const auto start = Snap(from, 0, options.StartHeight);
        if (start == UINT32_MAX || Snap(to, 0, NAN) == UINT32_MAX) return false;
        float floorLow = INFINITY, floorHigh = -INFINITY;
        for (int64_t y = int64_t(low.y); y <= int64_t(high.y); ++y)
            for (int64_t x = int64_t(low.x); x <= int64_t(high.x); ++x)
            {
                const auto cellLow = origin + glm::dvec2(x, y) * size;
                if (!Intersects(a, b, cellLow - radius, cellLow + size + radius)) continue;
                const auto node = Locate(x, y);
                if (node == UINT32_MAX) return false;
                const auto& tile = Owner(node);
                const auto cell = tile.Grid.CellOf(node - tile.Offset);
                if (!tile.Grid.IsWalkable(cell) || (options.UnitCostOnly && tile.Grid.GetCost(cell.x, cell.y) != 1)) return false;
                const float floor = tile.Grid.GetFloorHeight(cell.x, cell.y);
                floorLow = std::min(floorLow, floor);
                floorHigh = std::max(floorHigh, floor);
                if (first.HasHeights() && first.GetMaxClimb() > 0 && floorHigh - floorLow > first.GetMaxClimb()) return false;
            }
        return true;
    }
    NavRoute NavAtlas::SmoothRoute(const NavRoute& route, std::stop_token stop) const
    {
        if (route.Revision != m_Revision || route.Points.size() < 3 || route.Status == NavPathStatus::Failed) return route;
        NavRoute out;
        out.Revision = route.Revision;
        out.Status = route.Status;
        out.Cost = route.Cost; // graph search cost, not shortened geometric length
        out.Expanded = route.Expanded;
        out.Points.reserve(route.Points.size());
        out.Points.push_back(route.Points.front());
        size_t cursor = 0;
        uint32_t probes = 0;
        while (cursor + 1 < route.Points.size())
        {
            if (stop.stop_requested())
            {
                out.Points.clear();
                out.Status = NavPathStatus::Failed;
                return out;
            }
            size_t end = std::min(cursor + 32, route.Points.size() - 1);
            for (size_t i = cursor + 1; i <= end; ++i)
                if (route.Points[i].IncomingLink)
                {
                    end = i - 1;
                    break;
                }
            size_t chosen = cursor + 1;
            for (size_t i = end; i > cursor + 1 && probes < 64; --i)
            {
                ++probes;
                NavSegmentOptions options;
                options.StartHeight = route.Points[cursor].Height;
                options.UnitCostOnly = true;
                if (CanWalkSegment(route.Points[cursor].Position, route.Points[i].Position, options))
                {
                    chosen = i;
                    break;
                }
            }
            out.Points.push_back(route.Points[chosen]);
            cursor = chosen;
        }
        return out;
    }
}
