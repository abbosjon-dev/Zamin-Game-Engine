#include "Zamin/AI/NavGridBuilder.h"

#include "Zamin/Scene/Components.h"
#include "Zamin/Scene/TilesetProperties.h"

#include <algorithm>
#include <cmath>

namespace Zamin {

    // A cell's rectangle shrunk a little: shapes that only touch its border do not block it.
    static void CellRect(const NavGrid& grid, int x, int y, glm::vec2& min, glm::vec2& max)
    {
        const float inset = grid.GetCellSize() * 1e-3f;
        min = grid.GridToWorld(glm::vec2((float)x, (float)y)) + inset;
        max = grid.GridToWorld(glm::vec2((float)(x + 1), (float)(y + 1))) - inset;
    }

    // The grid cells a world rectangle covers (clipped to the grid). False when it misses the grid.
    static bool CellRange(const NavGrid& grid, glm::vec2 min, glm::vec2 max, glm::ivec2& c0, glm::ivec2& c1)
    {
        if (grid.IsEmpty() || !std::isfinite(min.x) || !std::isfinite(min.y) || !std::isfinite(max.x) || !std::isfinite(max.y))
            return false;
        c0 = grid.WorldToCell(min);
        c1 = grid.WorldToCell(max);
        if (c1.x < 0 || c1.y < 0 || c0.x >= grid.GetWidth() || c0.y >= grid.GetHeight())
            return false;
        c0 = grid.ClampCell(c0);
        c1 = grid.ClampCell(c1);
        return true;
    }

    static size_t Block(NavGrid& grid, int x, int y)
    {
        if (!grid.IsWalkable(x, y))
            return 0;
        grid.SetWalkable(x, y, false);
        return 1;
    }

    bool NavGridBuilder::PointInPolygon(const std::vector<glm::vec2>& outline, const glm::vec2& point)
    {
        bool inside = false;
        const size_t n = outline.size();
        for (size_t i = 0, j = n - 1; i < n; j = i++)
        {
            const glm::vec2& a = outline[i];
            const glm::vec2& b = outline[j];
            if ((a.y > point.y) != (b.y > point.y) && point.x < (b.x - a.x) * (point.y - a.y) / (b.y - a.y) + a.x)
                inside = !inside;
        }
        return inside;
    }

    bool NavGridBuilder::SegmentIntersectsRect(const glm::vec2& a, const glm::vec2& b, const glm::vec2& min, const glm::vec2& max)
    {
        // Liang-Barsky clipping of the segment against the rectangle.
        float t0 = 0.0f, t1 = 1.0f;
        const glm::vec2 d = b - a;
        const float p[4] = { -d.x, d.x, -d.y, d.y };
        const float q[4] = { a.x - min.x, max.x - a.x, a.y - min.y, max.y - a.y };
        for (int i = 0; i < 4; i++)
        {
            if (p[i] == 0.0f)
            {
                if (q[i] < 0.0f)
                    return false; // parallel and outside
                continue;
            }
            const float t = q[i] / p[i];
            if (p[i] < 0.0f)
                t0 = std::max(t0, t);
            else
                t1 = std::min(t1, t);
            if (t0 > t1)
                return false;
        }
        return true;
    }

    size_t NavGridBuilder::BlockPolygon(NavGrid& grid, const std::vector<glm::vec2>& outline)
    {
        if (outline.size() < 3)
            return outline.size() == 2 ? BlockSegment(grid, outline[0], outline[1]) : 0;
        glm::vec2 lo = outline[0], hi = outline[0];
        for (const glm::vec2& p : outline)
        {
            lo = glm::min(lo, p);
            hi = glm::max(hi, p);
        }
        glm::ivec2 c0, c1;
        if (!CellRange(grid, lo, hi, c0, c1))
            return 0;
        size_t blocked = 0;
        for (int y = c0.y; y <= c1.y; y++)
            for (int x = c0.x; x <= c1.x; x++)
            {
                if (!grid.IsWalkable(x, y))
                    continue;
                glm::vec2 min, max;
                CellRect(grid, x, y, min, max);
                // Overlap: the cell's centre inside the outline (it covers the cell), a corner of the
                // outline inside the cell, or an edge crossing it.
                bool overlap = PointInPolygon(outline, (min + max) * 0.5f);
                for (size_t i = 0; i < outline.size() && !overlap; i++)
                {
                    const glm::vec2& a = outline[i];
                    const glm::vec2& b = outline[(i + 1) % outline.size()];
                    overlap = SegmentIntersectsRect(a, b, min, max);
                }
                if (overlap)
                    blocked += Block(grid, x, y);
            }
        return blocked;
    }

    size_t NavGridBuilder::BlockCircle(NavGrid& grid, const glm::vec2& center, float radius)
    {
        if (!std::isfinite(radius) || !(radius > 0.0f))
            return 0;
        glm::ivec2 c0, c1;
        if (!CellRange(grid, center - radius, center + radius, c0, c1))
            return 0;
        size_t blocked = 0;
        for (int y = c0.y; y <= c1.y; y++)
            for (int x = c0.x; x <= c1.x; x++)
            {
                glm::vec2 min, max;
                CellRect(grid, x, y, min, max);
                const glm::vec2 nearest = glm::clamp(center, min, max);
                if (glm::dot(nearest - center, nearest - center) < radius * radius)
                    blocked += Block(grid, x, y);
            }
        return blocked;
    }

    size_t NavGridBuilder::BlockSegment(NavGrid& grid, const glm::vec2& a, const glm::vec2& b)
    {
        glm::ivec2 c0, c1;
        if (!CellRange(grid, glm::min(a, b), glm::max(a, b), c0, c1))
            return 0;
        size_t blocked = 0;
        for (int y = c0.y; y <= c1.y; y++)
            for (int x = c0.x; x <= c1.x; x++)
            {
                glm::vec2 min, max;
                CellRect(grid, x, y, min, max);
                if (SegmentIntersectsRect(a, b, min, max))
                    blocked += Block(grid, x, y);
            }
        return blocked;
    }

    void NavGridBuilder::GetTilemapBounds(const TilemapComponent& tilemap, const glm::mat4& world, glm::vec2& min, glm::vec2& max)
    {
        const float w = (float)std::max(1, tilemap.Width) * tilemap.TileSize, h = (float)std::max(1, tilemap.Height) * tilemap.TileSize;
        const glm::vec2 corners[4] = { { 0.0f, 0.0f }, { w, 0.0f }, { 0.0f, h }, { w, h } };
        min = glm::vec2(std::numeric_limits<float>::max());
        max = glm::vec2(std::numeric_limits<float>::lowest());
        for (const glm::vec2& c : corners)
        {
            const glm::vec2 p(world * glm::vec4(c, 0.0f, 1.0f));
            min = glm::min(min, p);
            max = glm::max(max, p);
        }
    }

    size_t NavGridBuilder::BlockTilemap(NavGrid& grid, const TilemapComponent& tilemap, const glm::mat4& world, const TilesetProperties* properties,
                                        bool oneWayBlocks)
    {
        if (grid.IsEmpty() || !tilemap.Collision || !(tilemap.TileSize > 0.0f) || tilemap.Width <= 0 || tilemap.Height <= 0)
            return 0;
        // The tilemap's plane: world.xy = axes * local.xy + origin.
        const glm::mat2 axes{ glm::vec2(world[0]), glm::vec2(world[1]) };
        const float det = glm::determinant(axes);
        if (!std::isfinite(det) || std::abs(det) < 1e-12f)
            return 0;
        const glm::mat2 toLocal = glm::inverse(axes);
        const glm::vec2 origin(world[3]);

        glm::vec2 lo, hi;
        GetTilemapBounds(tilemap, world, lo, hi);
        glm::ivec2 c0, c1;
        if (!CellRange(grid, lo, hi, c0, c1))
            return 0;
        // Samples per cell side: about two per tile (so a tile overlapping a cell is not missed); a
        // cell inside one tile (the grid aligned with the map) is read exactly.
        const float tileWorld = tilemap.TileSize * std::min(glm::length(axes[0]), glm::length(axes[1]));
        const int samples = std::clamp((int)std::ceil(2.0f * grid.GetCellSize() / std::max(tileWorld, 1e-6f)), 2, 16);
        size_t blocked = 0;
        for (int y = c0.y; y <= c1.y; y++)
            for (int x = c0.x; x <= c1.x; x++)
            {
                if (!grid.IsWalkable(x, y))
                    continue;
                bool solid = false;
                for (int j = 0; j < samples && !solid; j++)
                    for (int i = 0; i < samples && !solid; i++)
                    {
                        const glm::vec2 p = grid.GridToWorld(glm::vec2((float)x + ((float)i + 0.5f) / (float)samples,
                                                                       (float)y + ((float)j + 0.5f) / (float)samples));
                        const glm::vec2 local = toLocal * (p - origin) / tilemap.TileSize;
                        const int tx = (int)std::floor(local.x), ty = (int)std::floor(local.y);
                        if (!tilemap.Contains(tx, ty))
                            continue;
                        const TileCollision type = TilemapCollision::GetCell(tilemap, tx, ty, properties);
                        solid = type == TileCollision::Solid || (oneWayBlocks && type == TileCollision::OneWay);
                    }
                if (solid)
                    blocked += Block(grid, x, y);
            }
        return blocked;
    }

}
