#include "Zamin/AI/NavPathfinder.h"

#include <algorithm>
#include <cmath>

namespace Zamin {

    const char* NavPathStatusName(NavPathStatus status)
    {
        switch (status)
        {
            case NavPathStatus::Complete: return "Complete";
            case NavPathStatus::Partial: return "Partial";
            case NavPathStatus::Failed: return "Failed";
            default: return "None";
        }
    }

    static constexpr float Sqrt2 = 1.41421356237f;

    float NavPathfinder::EstimateCost(const NavGrid& grid, const glm::ivec2& a, const glm::ivec2& b, bool diagonals)
    {
        const float dx = (float)std::abs(a.x - b.x), dy = (float)std::abs(a.y - b.y);
        const float cells = diagonals ? std::max(dx, dy) + (Sqrt2 - 1.0f) * std::min(dx, dy) : dx + dy;
        return cells * grid.GetCellSize();
    }

    // ------------------------------------------------------------------ heap
    bool NavPathfinder::Less(const HeapEntry& a, const HeapEntry& b) const
    {
        if (a.F != b.F)
            return a.F < b.F;
        // Equal estimates: the one nearer to the goal first (goes straight on instead of fanning
        // out), then by cell index - the same order every time.
        if (a.H != b.H)
            return a.H < b.H;
        return a.Node < b.Node;
    }

    void NavPathfinder::SiftUp(size_t i)
    {
        HeapEntry entry = m_Heap[i];
        while (i > 0)
        {
            const size_t parent = (i - 1) / 2;
            if (!Less(entry, m_Heap[parent]))
                break;
            m_Heap[i] = m_Heap[parent];
            m_HeapIndex[m_Heap[i].Node] = (int32_t)i;
            i = parent;
        }
        m_Heap[i] = entry;
        m_HeapIndex[entry.Node] = (int32_t)i;
    }

    void NavPathfinder::SiftDown(size_t i)
    {
        const size_t count = m_Heap.size();
        HeapEntry entry = m_Heap[i];
        for (;;)
        {
            size_t child = i * 2 + 1;
            if (child >= count)
                break;
            if (child + 1 < count && Less(m_Heap[child + 1], m_Heap[child]))
                child++;
            if (!Less(m_Heap[child], entry))
                break;
            m_Heap[i] = m_Heap[child];
            m_HeapIndex[m_Heap[i].Node] = (int32_t)i;
            i = child;
        }
        m_Heap[i] = entry;
        m_HeapIndex[entry.Node] = (int32_t)i;
    }

    void NavPathfinder::Push(uint32_t node, float f, float h)
    {
        m_Heap.push_back({ f, h, node });
        SiftUp(m_Heap.size() - 1);
    }

    void NavPathfinder::Decrease(uint32_t node, float f, float h)
    {
        const size_t i = (size_t)m_HeapIndex[node];
        m_Heap[i].F = f;
        m_Heap[i].H = h;
        SiftUp(i);
    }

    uint32_t NavPathfinder::Pop()
    {
        const uint32_t node = m_Heap.front().Node;
        m_Heap.front() = m_Heap.back();
        m_Heap.pop_back();
        if (!m_Heap.empty())
            SiftDown(0);
        m_HeapIndex[node] = -1; // closed
        return node;
    }

    void NavPathfinder::Prepare(size_t cells)
    {
        if (m_Stamp.size() < cells)
        {
            m_G.resize(cells);
            m_Parent.resize(cells);
            m_HeapIndex.resize(cells);
            m_Stamp.resize(cells, 0);
        }
        if (++m_Generation == 0)
        {
            // Wrapped around: no stamp may look current.
            std::fill(m_Stamp.begin(), m_Stamp.end(), 0u);
            m_Generation = 1;
        }
        m_Heap.clear();
    }

    size_t NavPathfinder::GetMemoryBytes() const
    {
        return m_G.capacity() * sizeof(float) + m_Parent.capacity() * sizeof(uint32_t) + m_Stamp.capacity() * sizeof(uint32_t) +
               m_HeapIndex.capacity() * sizeof(int32_t) + m_Heap.capacity() * sizeof(HeapEntry);
    }

    // ------------------------------------------------------------------ search
    NavPathStatus NavPathfinder::FindPath(const NavGrid& grid, const glm::vec2& start, const glm::vec2& goal, NavPathResult& out,
                                          const NavPathOptions& options)
    {
        return FindPath(grid, grid.WorldToCell(start), grid.WorldToCell(goal), out, options);
    }

    NavPathStatus NavPathfinder::FindPath(const NavGrid& grid, const glm::ivec2& startCell, const glm::ivec2& goalCell, NavPathResult& out,
                                          const NavPathOptions& options)
    {
        out.Clear();
        if (grid.IsEmpty())
            return out.Status = NavPathStatus::Failed;

        const int snap = std::max(0, options.SnapRadius);
        glm::ivec2 start = grid.ClampCell(startCell);
        const bool onFloor = grid.HasHeights() && std::isfinite(options.StartHeight);
        if (onFloor ? !grid.FindNearestWalkable(start, snap, start, options.StartHeight, std::max(grid.GetMaxClimb(), 0.5f))
                    : !grid.FindNearestWalkable(start, snap, start))
            return out.Status = NavPathStatus::Failed;
        glm::ivec2 goal = grid.ClampCell(goalCell);
        bool goalMoved = goal != goalCell;
        if (glm::ivec2 walkable; grid.FindNearestWalkable(goal, snap, walkable))
        {
            goalMoved |= walkable != goal;
            goal = walkable;
        }
        else
            goalMoved = true; // aimed at, never reached: a partial path towards it
        out.Start = start;
        out.Goal = goal;

        const int width = grid.GetWidth();
        Prepare(grid.GetCellCount());
        const float weight = std::isfinite(options.HeuristicWeight) ? std::max(0.0f, options.HeuristicWeight) : 1.0f;
        const bool diagonals = options.Diagonals;
        const float cellSize = grid.GetCellSize();
        const bool heights = grid.HasHeights();

        const uint32_t startNode = (uint32_t)grid.Index(start.x, start.y);
        const uint32_t goalNode = (uint32_t)grid.Index(goal.x, goal.y);
        m_Stamp[startNode] = m_Generation;
        m_G[startNode] = 0.0f;
        m_Parent[startNode] = startNode;
        const float startH = EstimateCost(grid, start, goal, diagonals);
        Push(startNode, startH * weight, startH);

        uint32_t best = startNode;
        float bestH = startH, bestG = 0.0f;
        bool reached = false;
        static const int dirX[8] = { 1, -1, 0, 0, 1, -1, 1, -1 };
        static const int dirY[8] = { 0, 0, 1, -1, 1, 1, -1, -1 };
        const int directions = diagonals ? 8 : 4;

        while (!m_Heap.empty())
        {
            const uint32_t node = Pop();
            out.Expanded++;
            if (node == goalNode)
            {
                reached = true;
                best = node;
                break;
            }
            const int x = (int)(node % (uint32_t)width), y = (int)(node / (uint32_t)width);
            const float g = m_G[node];
            const float h = EstimateCost(grid, { x, y }, goal, diagonals);
            if (h < bestH || (h == bestH && g < bestG))
            {
                best = node;
                bestH = h;
                bestG = g;
            }
            if (options.MaxExpanded > 0 && out.Expanded >= options.MaxExpanded)
                break;

            for (int d = 0; d < directions; d++)
            {
                const int nx = x + dirX[d], ny = y + dirY[d];
                if (!grid.CanStep(x, y, nx, ny))
                    continue;
                const bool diagonal = d >= 4;
                if (diagonal)
                {
                    const bool sideA = grid.CanStep(x, y, nx, y), sideB = grid.CanStep(x, y, x, ny);
                    if (options.CutCorners ? !(sideA || sideB) : !(sideA && sideB))
                        continue;
                }
                float step = (diagonal ? Sqrt2 : 1.0f) * cellSize;
                if (heights)
                {
                    const float dh = grid.GetFloorHeight(nx, ny) - grid.GetFloorHeight(x, y);
                    step = std::sqrt(step * step + dh * dh);
                }
                const uint32_t next = (uint32_t)grid.Index(nx, ny);
                const float ng = g + step * grid.GetCost(nx, ny);
                if (!Visited(next))
                {
                    m_Stamp[next] = m_Generation;
                    m_G[next] = ng;
                    m_Parent[next] = node;
                    const float nh = EstimateCost(grid, { nx, ny }, goal, diagonals);
                    Push(next, ng + nh * weight, nh);
                }
                else if (m_HeapIndex[next] >= 0 && ng < m_G[next])
                {
                    m_G[next] = ng;
                    m_Parent[next] = node;
                    const float nh = m_Heap[(size_t)m_HeapIndex[next]].H;
                    Decrease(next, ng + nh * weight, nh);
                }
            }
        }

        // The path back from the goal (or the cell nearest to it) to the start.
        for (uint32_t node = best;; node = m_Parent[node])
        {
            out.Cells.push_back(grid.CellOf(node));
            if (node == startNode || out.Cells.size() > grid.GetCellCount())
                break;
        }
        std::reverse(out.Cells.begin(), out.Cells.end());
        out.Cost = m_G[best];
        out.Status = reached && !goalMoved ? NavPathStatus::Complete : NavPathStatus::Partial;
        return out.Status;
    }

}
