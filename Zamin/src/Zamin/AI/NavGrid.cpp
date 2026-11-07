#include "Zamin/AI/NavGrid.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Zamin {

    NavGrid::NavGrid(int width, int height, float cellSize, const glm::vec2& origin)
    {
        Reset(width, height, cellSize, origin);
    }

    void NavGrid::Reset(int width, int height, float cellSize, const glm::vec2& origin)
    {
        m_Width = std::clamp(width, 1, MaxSize);
        m_Height = std::clamp(height, 1, MaxSize);
        m_CellSize = std::isfinite(cellSize) && cellSize > 0.0f ? cellSize : 1.0f;
        m_Origin = std::isfinite(origin.x) && std::isfinite(origin.y) ? origin : glm::vec2(0.0f);
        m_Walkable.assign(GetCellCount(), 1);
        m_Cost.assign(GetCellCount(), 1.0f);
        m_Heights.clear();
        m_MaxClimb = 0.0f;
        m_Baked.clear();
        m_Carves.clear();
        m_Serial++;
        m_CarveRevision++;
        Touch();
    }

    void NavGrid::Clear()
    {
        m_Width = m_Height = 0;
        m_CellSize = 1.0f;
        m_Origin = glm::vec2(0.0f);
        m_Walkable.clear();
        m_Cost.clear();
        m_Heights.clear();
        m_MaxClimb = 0.0f;
        m_Baked.clear();
        m_Carves.clear();
        m_Serial++;
        m_CarveRevision++;
        Touch();
    }

    void NavGrid::WriteBaked(size_t i, bool walkable)
    {
        if (m_Carves.empty())
            m_Walkable[i] = (uint8_t)walkable;
        else
        {
            m_Baked[i] = (uint8_t)walkable;
            m_Walkable[i] = (uint8_t)(walkable && m_Carves[i] == 0);
        }
    }

    void NavGrid::SetWalkable(int x, int y, bool walkable)
    {
        if (!Contains(x, y))
            return;
        const size_t i = Index(x, y);
        const uint8_t baked = m_Carves.empty() ? m_Walkable[i] : m_Baked[i];
        if (baked != (uint8_t)walkable)
        {
            WriteBaked(i, walkable);
            Touch();
        }
    }

    void NavGrid::SetCost(int x, int y, float cost)
    {
        if (!Contains(x, y))
            return;
        cost = std::isfinite(cost) ? std::clamp(cost, 1.0f, MaxCost) : 1.0f;
        float& cell = m_Cost[Index(x, y)];
        if (cell != cost)
        {
            cell = cost;
            Touch();
        }
    }

    void NavGrid::Fill(bool walkable)
    {
        for (size_t i = 0; i < m_Walkable.size(); i++)
            WriteBaked(i, walkable);
        Touch();
    }

    void NavGrid::FillRect(int x0, int y0, int x1, int y1, bool walkable)
    {
        if (IsEmpty())
            return;
        if (x0 > x1) std::swap(x0, x1);
        if (y0 > y1) std::swap(y0, y1);
        x0 = std::max(x0, 0); y0 = std::max(y0, 0);
        x1 = std::min(x1, m_Width - 1); y1 = std::min(y1, m_Height - 1);
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++)
                WriteBaked(Index(x, y), walkable);
        Touch();
    }

    size_t NavGrid::CountWalkable() const
    {
        return (size_t)std::count(m_Walkable.begin(), m_Walkable.end(), (uint8_t)1);
    }

    // ------------------------------------------------------------------ carving
    void NavGrid::Carve(const std::vector<uint32_t>& cells)
    {
        if (cells.empty() || IsEmpty())
            return;
        if (m_Carves.empty())
        {
            m_Carves.assign(GetCellCount(), 0);
            m_Baked = m_Walkable;
        }
        const size_t count = GetCellCount();
        for (uint32_t i : cells)
            if (i < count && m_Carves[i] < std::numeric_limits<uint16_t>::max())
            {
                m_Carves[i]++;
                m_Walkable[i] = 0;
            }
        m_CarveRevision++;
    }

    void NavGrid::Uncarve(const std::vector<uint32_t>& cells)
    {
        if (cells.empty() || m_Carves.empty())
            return;
        const size_t count = GetCellCount();
        for (uint32_t i : cells)
            if (i < count && m_Carves[i] > 0 && --m_Carves[i] == 0)
                m_Walkable[i] = m_Baked[i];
        m_CarveRevision++;
    }

    void NavGrid::ClearCarves()
    {
        if (m_Carves.empty())
            return;
        m_Walkable = m_Baked;
        m_Baked.clear();
        m_Carves.clear();
        m_CarveRevision++;
    }

    bool NavGrid::IsBakedWalkable(int x, int y) const
    {
        if (!Contains(x, y))
            return false;
        return (m_Carves.empty() ? m_Walkable[Index(x, y)] : m_Baked[Index(x, y)]) != 0;
    }

    size_t NavGrid::CountCarved() const
    {
        return m_Carves.empty() ? 0 : m_Carves.size() - (size_t)std::count(m_Carves.begin(), m_Carves.end(), (uint16_t)0);
    }

    // ------------------------------------------------------------------ heights
    void NavGrid::EnableHeights()
    {
        if (m_Heights.size() != GetCellCount())
        {
            m_Heights.assign(GetCellCount(), 0.0f);
            Touch();
        }
    }

    void NavGrid::SetFloorHeight(int x, int y, float height)
    {
        if (!Contains(x, y))
            return;
        EnableHeights();
        m_Heights[Index(x, y)] = height;
        Touch();
    }

    void NavGrid::SetMaxClimb(float climb)
    {
        m_MaxClimb = std::isfinite(climb) ? std::max(0.0f, climb) : 0.0f;
        Touch();
    }

    bool NavGrid::CanStep(int ax, int ay, int bx, int by) const
    {
        if (!IsWalkable(bx, by))
            return false;
        if (!HasHeights() || !(m_MaxClimb > 0.0f) || !Contains(ax, ay))
            return true;
        return std::abs(m_Heights[Index(bx, by)] - m_Heights[Index(ax, ay)]) <= m_MaxClimb;
    }

    float NavGrid::SampleFloorHeight(const glm::vec2& position) const
    {
        if (!HasHeights() || IsEmpty())
            return 0.0f;
        const glm::ivec2 own = ClampCell(WorldToCell(position));
        const float ownHeight = m_Heights[Index(own.x, own.y)];
        const glm::vec2 g = WorldToGrid(position) - 0.5f; // relative to the cell centres
        const int x0 = (int)std::floor(g.x), y0 = (int)std::floor(g.y);
        const float fx = g.x - (float)x0, fy = g.y - (float)y0;
        float sum = 0.0f, weight = 0.0f;
        for (int j = 0; j < 2; j++)
            for (int i = 0; i < 2; i++)
            {
                const int x = x0 + i, y = y0 + j;
                if (!IsWalkable(x, y))
                    continue;
                const float h = m_Heights[Index(x, y)];
                // Only the floor the point stands on (not the top of a crate next to it).
                if (!std::isfinite(h) || (std::isfinite(ownHeight) && m_MaxClimb > 0.0f && std::abs(h - ownHeight) > m_MaxClimb))
                    continue;
                const float w = (i ? fx : 1.0f - fx) * (j ? fy : 1.0f - fy);
                sum += h * w;
                weight += w;
            }
        if (weight > 1e-6f)
            return sum / weight;
        return std::isfinite(ownHeight) ? ownHeight : 0.0f;
    }

    // ------------------------------------------------------------------ cells
    glm::ivec2 NavGrid::WorldToCell(const glm::vec2& position) const
    {
        const glm::vec2 g = glm::floor(WorldToGrid(position));
        // Far away points (and non-numbers) stay safely outside.
        const float limit = (float)(MaxSize * 4);
        if (!std::isfinite(g.x) || !std::isfinite(g.y))
            return { -1, -1 };
        return { (int)std::clamp(g.x, -limit, limit), (int)std::clamp(g.y, -limit, limit) };
    }

    glm::ivec2 NavGrid::ClampCell(const glm::ivec2& cell) const
    {
        if (IsEmpty())
            return { 0, 0 };
        return { std::clamp(cell.x, 0, m_Width - 1), std::clamp(cell.y, 0, m_Height - 1) };
    }

    // The nearest cell (by distance between centres; ties: lower y, then lower x) at most maxRadius
    // cells away along each axis that `accept` takes.
    template<typename Accept>
    static bool FindNearest(const NavGrid& grid, const glm::ivec2& cell, int maxRadius, glm::ivec2& out, Accept&& accept)
    {
        if (grid.IsEmpty())
            return false;
        if (accept(cell.x, cell.y))
        {
            out = cell;
            return true;
        }
        maxRadius = std::clamp(maxRadius, 0, std::max(grid.GetWidth(), grid.GetHeight()) + std::abs(cell.x) + std::abs(cell.y));
        bool found = false;
        int bestDistance = std::numeric_limits<int>::max();
        glm::ivec2 best{ 0 };
        auto consider = [&](int x, int y) {
            if (!accept(x, y))
                return;
            const int dx = x - cell.x, dy = y - cell.y;
            const int d = dx * dx + dy * dy;
            if (d < bestDistance || (d == bestDistance && (y < best.y || (y == best.y && x < best.x))))
            {
                bestDistance = d;
                best = { x, y };
                found = true;
            }
        };
        for (int r = 1; r <= maxRadius; r++)
        {
            // A cell of a farther ring cannot be nearer than r (its distance along an axis).
            if (found && r * r > bestDistance)
                break;
            for (int x = cell.x - r; x <= cell.x + r; x++)
            {
                consider(x, cell.y - r);
                consider(x, cell.y + r);
            }
            for (int y = cell.y - r + 1; y <= cell.y + r - 1; y++)
            {
                consider(cell.x - r, y);
                consider(cell.x + r, y);
            }
        }
        if (found)
            out = best;
        return found;
    }

    bool NavGrid::FindNearestWalkable(const glm::ivec2& cell, int maxRadius, glm::ivec2& out) const
    {
        return FindNearest(*this, cell, maxRadius, out, [&](int x, int y) { return IsWalkable(x, y); });
    }

    bool NavGrid::FindNearestWalkable(const glm::ivec2& cell, int maxRadius, glm::ivec2& out, float height, float tolerance) const
    {
        if (!HasHeights() || !std::isfinite(height))
            return FindNearestWalkable(cell, maxRadius, out);
        tolerance = std::isfinite(tolerance) ? std::max(0.0f, tolerance) : 0.0f;
        return FindNearest(*this, cell, maxRadius, out,
                           [&](int x, int y) { return IsWalkable(x, y) && std::abs(m_Heights[Index(x, y)] - height) <= tolerance; });
    }

    void NavGrid::Erode(float radius)
    {
        if (IsEmpty() || !std::isfinite(radius) || !(radius > 0.0f))
            return;
        const float rc = radius / m_CellSize; // in cells
        const float rc2 = rc * rc;
        const int reach = (int)std::ceil(rc) + 1;
        // The baked state (carves are not walls to keep clear of: they come and go).
        const std::vector<uint8_t> source = m_Carves.empty() ? m_Walkable : m_Baked;

        // Ledges (3D): bit d of a walkable cell - its neighbour in direction d (+x, -x, +y, -y) is
        // walkable but more than MaxClimb higher or lower.
        static const int dirX[4] = { 1, -1, 0, 0 }, dirY[4] = { 0, 0, 1, -1 };
        std::vector<uint8_t> ledges;
        if (HasHeights() && m_MaxClimb > 0.0f)
        {
            ledges.assign(GetCellCount(), 0);
            for (int y = 0; y < m_Height; y++)
                for (int x = 0; x < m_Width; x++)
                {
                    const size_t i = Index(x, y);
                    if (!source[i])
                        continue;
                    for (int d = 0; d < 4; d++)
                    {
                        const int nx = x + dirX[d], ny = y + dirY[d];
                        if (Contains(nx, ny) && source[Index(nx, ny)] && std::abs(m_Heights[Index(nx, ny)] - m_Heights[i]) > m_MaxClimb)
                            ledges[i] |= (uint8_t)(1u << d);
                    }
                }
        }

        bool changed = false;
        for (int y = 0; y < m_Height; y++)
            for (int x = 0; x < m_Width; x++)
            {
                const size_t i = Index(x, y);
                if (!source[i])
                    continue;
                const float cx = (float)x + 0.5f, cy = (float)y + 0.5f;
                bool block = false;
                for (int yy = std::max(0, y - reach); yy <= std::min(m_Height - 1, y + reach) && !block; yy++)
                    for (int xx = std::max(0, x - reach); xx <= std::min(m_Width - 1, x + reach) && !block; xx++)
                    {
                        const size_t j = Index(xx, yy);
                        if (!source[j])
                        {
                            // The distance from the centre to the blocked cell's square.
                            const float dx = std::max({ (float)xx - cx, 0.0f, cx - (float)(xx + 1) });
                            const float dy = std::max({ (float)yy - cy, 0.0f, cy - (float)(yy + 1) });
                            block = dx * dx + dy * dy < rc2;
                        }
                        else if (!ledges.empty() && ledges[j])
                            for (int d = 0; d < 4 && !block; d++)
                            {
                                if (!(ledges[j] & (1u << d)))
                                    continue;
                                // The border segment between cell j and its neighbour in direction d.
                                float ax, ay, bx, by;
                                if (d < 2)
                                {
                                    ax = bx = (float)xx + (d == 0 ? 1.0f : 0.0f);
                                    ay = (float)yy;
                                    by = (float)yy + 1.0f;
                                }
                                else
                                {
                                    ay = by = (float)yy + (d == 2 ? 1.0f : 0.0f);
                                    ax = (float)xx;
                                    bx = (float)xx + 1.0f;
                                }
                                const float px = std::clamp(cx, ax, bx), py = std::clamp(cy, ay, by);
                                block = (px - cx) * (px - cx) + (py - cy) * (py - cy) < rc2;
                            }
                    }
                if (block)
                {
                    WriteBaked(i, false);
                    changed = true;
                }
            }
        if (changed)
            Touch();
    }

    // ------------------------------------------------------------------ line of sight
    // Walks the cells a segment touches (Amanatides & Woo, every cell: supercover). visit(cell, side)
    // returns false to stop; side = one of the two cells beside a corner the segment passes through.
    // Returns false when a visit stopped it.
    template<typename Visit>
    static bool WalkLine(const NavGrid& grid, const glm::vec2& from, const glm::vec2& to, Visit&& visit)
    {
        const glm::vec2 a = grid.WorldToGrid(from), b = grid.WorldToGrid(to);
        if (!std::isfinite(a.x) || !std::isfinite(a.y) || !std::isfinite(b.x) || !std::isfinite(b.y))
            return false;
        const float limit = (float)(NavGrid::MaxSize * 4);
        if (std::abs(a.x) > limit || std::abs(a.y) > limit || std::abs(b.x) > limit || std::abs(b.y) > limit)
            return false;
        glm::ivec2 cell((int)std::floor(a.x), (int)std::floor(a.y));
        const glm::ivec2 end((int)std::floor(b.x), (int)std::floor(b.y));
        const glm::vec2 d = b - a;
        const int stepX = d.x > 0.0f ? 1 : (d.x < 0.0f ? -1 : 0);
        const int stepY = d.y > 0.0f ? 1 : (d.y < 0.0f ? -1 : 0);
        constexpr float inf = std::numeric_limits<float>::infinity();
        const float tDeltaX = stepX ? std::abs(1.0f / d.x) : inf;
        const float tDeltaY = stepY ? std::abs(1.0f / d.y) : inf;
        float tMaxX = stepX > 0 ? ((float)cell.x + 1.0f - a.x) * tDeltaX : (stepX < 0 ? (a.x - (float)cell.x) * tDeltaX : inf);
        float tMaxY = stepY > 0 ? ((float)cell.y + 1.0f - a.y) * tDeltaY : (stepY < 0 ? (a.y - (float)cell.y) * tDeltaY : inf);
        if (!visit(cell, false))
            return false;
        const int maxSteps = std::abs(end.x - cell.x) + std::abs(end.y - cell.y) + 2;
        constexpr float cornerEpsilon = 1e-5f;
        for (int i = 0; i < maxSteps && cell != end; i++)
        {
            if (std::abs(tMaxX - tMaxY) <= cornerEpsilon)
            {
                if (tMaxX > 1.0f + cornerEpsilon)
                    break;
                if (!visit(glm::ivec2(cell.x + stepX, cell.y), true) || !visit(glm::ivec2(cell.x, cell.y + stepY), true))
                    return false;
                cell.x += stepX;
                cell.y += stepY;
                tMaxX += tDeltaX;
                tMaxY += tDeltaY;
            }
            else if (tMaxX < tMaxY)
            {
                if (tMaxX > 1.0f + cornerEpsilon)
                    break;
                cell.x += stepX;
                tMaxX += tDeltaX;
            }
            else
            {
                if (tMaxY > 1.0f + cornerEpsilon)
                    break;
                cell.y += stepY;
                tMaxY += tDeltaY;
            }
            if (!visit(cell, false))
                return false;
        }
        return true;
    }

    void NavGrid::TraceLine(const glm::vec2& from, const glm::vec2& to, std::vector<glm::ivec2>& cells) const
    {
        cells.clear();
        if (IsEmpty())
            return;
        WalkLine(*this, from, to, [&](const glm::ivec2& cell, bool) {
            cells.push_back(cell);
            return true;
        });
    }

    bool NavGrid::HasLineOfSight(const glm::vec2& from, const glm::vec2& to) const
    {
        if (IsEmpty())
            return false;
        const bool climb = HasHeights() && m_MaxClimb > 0.0f;
        glm::ivec2 previous(-1);
        bool first = true;
        return WalkLine(*this, from, to, [&](const glm::ivec2& cell, bool side) {
            if (!IsWalkable(cell))
                return false;
            if (climb && !first && std::abs(m_Heights[Index(cell.x, cell.y)] - m_Heights[Index(previous.x, previous.y)]) > m_MaxClimb)
                return false;
            if (!side)
            {
                previous = cell;
                first = false;
            }
            return true;
        });
    }

    size_t NavGrid::GetMemoryBytes() const
    {
        return m_Walkable.capacity() * sizeof(uint8_t) + m_Cost.capacity() * sizeof(float) + m_Heights.capacity() * sizeof(float) +
               m_Baked.capacity() * sizeof(uint8_t) + m_Carves.capacity() * sizeof(uint16_t);
    }

}
