#pragma once

#include "Zamin/AI/NavGrid.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace Zamin {

    // How a path search ended: Complete - it reaches the goal; Partial - the goal cannot be reached
    // (walled off, blocked, outside the grid, or the search hit MaxExpanded) and the path leads to
    // the reachable cell nearest to it; Failed - no path at all (an empty grid, no walkable cell near
    // the start). None: no search yet.
    enum class NavPathStatus : uint8_t { None = 0, Complete, Partial, Failed };
    const char* NavPathStatusName(NavPathStatus status); // "None", "Complete", "Partial", "Failed"

    struct NavPathOptions
    {
        // 8 neighbours (diagonal steps cost sqrt 2; the octile distance guides the search), else 4
        // (Manhattan distance).
        bool Diagonals = true;
        // A diagonal step past one blocked side cell (it grazes the corner). Off: both side cells
        // must be walkable. Never between two blocked cells.
        bool CutCorners = false;
        // > 1: weighted A* - expands fewer cells, the path may be up to that much longer than the
        // shortest. 0: Dijkstra (no guidance).
        float HeuristicWeight = 1.0f;
        // The most cells taken from the open list (0: no limit); reached - a partial path to the
        // cell nearest to the goal found so far.
        int MaxExpanded = 0;
        // A blocked (or outside) start or goal moves to the nearest walkable cell at most this many
        // cells away (an agent standing in an eroded cell next to a wall, a click on a wall).
        int SnapRadius = 8;
    };

    struct NavPathResult
    {
        NavPathStatus Status = NavPathStatus::None;
        std::vector<glm::ivec2> Cells; // from the start to the goal (Partial: to the cell nearest to it)
        float Cost = 0.0f;             // world units walked, each step times the cost of the cell it enters
        int Expanded = 0;              // cells taken from the open list
        glm::ivec2 Start{ 0 };         // where the search began / aimed (after snapping)
        glm::ivec2 Goal{ 0 };

        bool Found() const { return Status == NavPathStatus::Complete || Status == NavPathStatus::Partial; }
        bool IsComplete() const { return Status == NavPathStatus::Complete; }
        void Clear()
        {
            Status = NavPathStatus::None;
            Cells.clear();
            Cost = 0.0f;
            Expanded = 0;
        }
    };

    // A* on a NavGrid with an indexed binary heap. Its buffers are kept and reused by the next search
    // (no allocation once they fit the grid), so keep one per thread: a pathfinder is not safe to
    // share between threads, grids are (searches only read them).
    class NavPathfinder
    {
    public:
        // The cheapest path of cells from `start` to `goal` (both cells of the grid). Ties between
        // equally cheap paths are broken the same way every time (deterministic).
        NavPathStatus FindPath(const NavGrid& grid, const glm::ivec2& start, const glm::ivec2& goal, NavPathResult& out,
                               const NavPathOptions& options = {});
        // The same between two positions on the grid's plane (world units).
        NavPathStatus FindPath(const NavGrid& grid, const glm::vec2& start, const glm::vec2& goal, NavPathResult& out,
                               const NavPathOptions& options = {});

        // The heuristic used: octile (diagonals) or Manhattan distance, in world units.
        static float EstimateCost(const NavGrid& grid, const glm::ivec2& a, const glm::ivec2& b, bool diagonals);

        size_t GetMemoryBytes() const;

    private:
        struct HeapEntry
        {
            float F;
            float H;
            uint32_t Node;
        };
        bool Less(const HeapEntry& a, const HeapEntry& b) const;
        void Prepare(size_t cells);
        void Push(uint32_t node, float f, float h);
        void Decrease(uint32_t node, float f, float h);
        uint32_t Pop();
        void SiftUp(size_t i);
        void SiftDown(size_t i);
        bool Visited(uint32_t node) const { return m_Stamp[node] == m_Generation; }

        std::vector<float> m_G;
        std::vector<uint32_t> m_Parent;
        std::vector<uint32_t> m_Stamp;     // == m_Generation: touched by the current search
        std::vector<int32_t> m_HeapIndex;  // >= 0: in the heap there; -1 open list left (closed)
        std::vector<HeapEntry> m_Heap;
        uint32_t m_Generation = 0;
    };

}
