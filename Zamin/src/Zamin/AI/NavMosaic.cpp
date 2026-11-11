#include "Zamin/AI/NavMosaic.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace Zamin {
    bool BuildNavMosaic(std::span<const NavGrid* const> tiles, NavGrid& output, std::string& error, size_t maxCells)
    {
        error.clear();
        auto fail = [&](const char* why) { error = why; return false; };
        if (tiles.empty() || tiles.size() > 512 || !tiles.front() || tiles.front()->IsEmpty()) return fail("Invalid navigation tile set");
        const auto& first = *tiles.front();
        const double size = first.GetCellSize();
        const auto origin = glm::dvec2(first.GetOrigin());
        if (!std::isfinite(size) || size <= 0 || !std::isfinite(origin.x) || !std::isfinite(origin.y)) return fail("Invalid navigation coordinates");
        struct Placement
        {
            const NavGrid* Grid;
            int64_t X, Y;
        };
        std::vector<Placement> placement;
        int64_t minX = 0, minY = 0, maxX = 0, maxY = 0;
        for (const auto* grid : tiles)
        {
            if (!grid || grid->IsEmpty() || std::abs(grid->GetCellSize() - size) > size * 1e-6 ||
                grid->HasHeights() != first.HasHeights() || grid->GetMaxClimb() != first.GetMaxClimb()) return fail("Incompatible navigation tiles");
            const auto delta = (glm::dvec2(grid->GetOrigin()) - origin) / size;
            if (!std::isfinite(delta.x) || !std::isfinite(delta.y) || std::abs(delta.x) > 1e8 || std::abs(delta.y) > 1e8 ||
                std::abs(delta.x - std::round(delta.x)) > .001 || std::abs(delta.y - std::round(delta.y)) > .001) return fail("Navigation tile is not aligned");
            const auto x = int64_t(std::llround(delta.x)), y = int64_t(std::llround(delta.y));
            placement.push_back({ grid, x, y });
            minX = std::min(minX, x);
            minY = std::min(minY, y);
            maxX = std::max(maxX, x + grid->GetWidth());
            maxY = std::max(maxY, y + grid->GetHeight());
        }
        const auto width = maxX - minX, height = maxY - minY;
        if (width <= 0 || height <= 0 || width > NavGrid::MaxSize || height > NavGrid::MaxSize || uint64_t(width) * uint64_t(height) > maxCells)
            return fail("Navigation mosaic cell budget exceeded");
        NavGrid next = output;
        next.Reset(int(width), int(height), float(size), glm::vec2(origin + glm::dvec2(minX, minY) * size));
        next.Fill(false);
        next.SetMaxClimb(first.GetMaxClimb());
        if (first.HasHeights()) next.EnableHeights();
        std::vector<uint8_t> occupied(next.GetCellCount());
        for (const auto& p : placement)
            for (int y = 0; y < p.Grid->GetHeight(); ++y)
                for (int x = 0; x < p.Grid->GetWidth(); ++x)
                {
                    const int xx = int(p.X - minX) + x, yy = int(p.Y - minY) + y;
                    auto& used = occupied[next.Index(xx, yy)];
                    if (used) return fail("Navigation tiles overlap");
                    used = 1;
                    next.SetWalkable(xx, yy, p.Grid->IsWalkable(x, y));
                    next.SetCost(xx, yy, p.Grid->GetCost(x, y));
                    if (first.HasHeights()) next.SetFloorHeight(xx, yy, p.Grid->GetFloorHeight(x, y));
                }
        output = std::move(next);
        return true;
    }
}
