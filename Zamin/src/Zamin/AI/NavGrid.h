#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

// Grids for path finding (A*: NavPathfinder.h, smoothing: NavPathSmoothing.h). A grid knows nothing
// about the scene: NavGrid2DComponent / NavGrid3DComponent bake one from the level (NavGridBuilder.h,
// Scene::BakeNavGrid) and NavAgentComponent walks along the paths found on it.
//
//   NavGrid grid(32, 32, 0.5f, { -8.0f, -8.0f });  // 16 x 16 world units from (-8, -8)
//   grid.FillRect(10, 0, 11, 20, false);            // a wall
//   NavPathfinder finder;
//   NavPathResult path;
//   finder.FindPath(grid, glm::vec2(-7, -7), glm::vec2(7, 7), path);
namespace Zamin {

    // Square cells over a plane: cell (x, y) covers Origin + [x, x + 1) x [y, y + 1) times CellSize of
    // the grid's plane - the world's XY for 2D grids, its XZ for 3D ones (grid y = world z). Each
    // cell is walkable or blocked and has a cost (>= 1: what a unit of walking through it costs - 1 is
    // open ground, 3 mud). 3D grids also have the height of each cell's floor and the most a step
    // between neighbouring cells may climb or drop (MaxClimb).
    class NavGrid
    {
    public:
        static constexpr int MaxSize = 4096;      // cells per side
        static constexpr float MaxCost = 1000.0f;

        NavGrid() = default;
        NavGrid(int width, int height, float cellSize = 1.0f, const glm::vec2& origin = glm::vec2(0.0f));

        // Width x height cells (1..MaxSize each), every one walkable with cost 1 (no heights). A cell
        // size that is not a positive number becomes 1.
        void Reset(int width, int height, float cellSize = 1.0f, const glm::vec2& origin = glm::vec2(0.0f));
        // No cells at all (a grid that was never baked).
        void Clear();

        int GetWidth() const { return m_Width; }
        int GetHeight() const { return m_Height; }
        size_t GetCellCount() const { return (size_t)m_Width * (size_t)m_Height; }
        bool IsEmpty() const { return m_Width <= 0 || m_Height <= 0; }
        float GetCellSize() const { return m_CellSize; }
        const glm::vec2& GetOrigin() const { return m_Origin; }
        glm::vec2 GetSize() const { return glm::vec2((float)m_Width, (float)m_Height) * m_CellSize; }

        bool Contains(int x, int y) const { return x >= 0 && y >= 0 && x < m_Width && y < m_Height; }
        bool Contains(const glm::ivec2& cell) const { return Contains(cell.x, cell.y); }
        size_t Index(int x, int y) const { return (size_t)y * (size_t)m_Width + (size_t)x; }
        glm::ivec2 CellOf(size_t index) const { return { (int)(index % (size_t)m_Width), (int)(index / (size_t)m_Width) }; }

        // ---- cells (outside the grid: blocked, cost 1, height 0; writes outside do nothing)
        bool IsWalkable(int x, int y) const { return Contains(x, y) && m_Walkable[Index(x, y)] != 0; }
        bool IsWalkable(const glm::ivec2& cell) const { return IsWalkable(cell.x, cell.y); }
        void SetWalkable(int x, int y, bool walkable);
        float GetCost(int x, int y) const { return Contains(x, y) ? m_Cost[Index(x, y)] : 1.0f; }
        // Clamped to 1..MaxCost (not a number: 1).
        void SetCost(int x, int y, float cost);
        // Every cell walkable / blocked; cells of [x0, x1] x [y0, y1] (inclusive, clipped to the grid).
        void Fill(bool walkable);
        void FillRect(int x0, int y0, int x1, int y1, bool walkable);
        size_t CountWalkable() const;

        // ---- heights (3D grids)
        // Every cell gets a floor height (0) from now on.
        void EnableHeights();
        bool HasHeights() const { return !m_Heights.empty(); }
        float GetFloorHeight(int x, int y) const { return HasHeights() && Contains(x, y) ? m_Heights[Index(x, y)] : 0.0f; }
        void SetFloorHeight(int x, int y, float height);
        // The floor under a world point of the grid's plane: blended between the centres of the
        // walkable cells around it (a smooth ramp), the cell's own height where it has no walkable
        // neighbours. 0 without heights.
        float SampleFloorHeight(const glm::vec2& position) const;
        // The most a step between neighbouring cells may climb or drop (world units); 0 - any.
        float GetMaxClimb() const { return m_MaxClimb; }
        void SetMaxClimb(float climb);
        // Whether an agent can step from cell a straight to its neighbour b: b walkable and (3D) the
        // height difference within MaxClimb.
        bool CanStep(int ax, int ay, int bx, int by) const;

        // ---- world (the grid's plane) <-> cells
        glm::ivec2 WorldToCell(const glm::vec2& position) const;      // may be outside the grid
        glm::vec2 CellToWorld(int x, int y) const { return m_Origin + (glm::vec2((float)x, (float)y) + 0.5f) * m_CellSize; } // centre
        glm::vec2 CellToWorld(const glm::ivec2& cell) const { return CellToWorld(cell.x, cell.y); }
        glm::vec2 WorldToGrid(const glm::vec2& position) const { return (position - m_Origin) / m_CellSize; } // cells, continuous
        glm::vec2 GridToWorld(const glm::vec2& grid) const { return m_Origin + grid * m_CellSize; }
        // The nearest cell inside the grid.
        glm::ivec2 ClampCell(const glm::ivec2& cell) const;

        // The walkable cell nearest to `cell` (by distance between centres; ties: lower y, then lower
        // x) at most maxRadius cells away along each axis: `cell` itself when it is walkable. False
        // when there is none.
        bool FindNearestWalkable(const glm::ivec2& cell, int maxRadius, glm::ivec2& out) const;

        // Agent clearance: blocks every walkable cell whose centre is nearer than `radius` (world
        // units) to a blocked cell's square - and, on 3D grids, to a ledge (the border between two
        // walkable neighbours more than MaxClimb apart: a crate's side, a drop). The grid's own edge
        // does not count.
        void Erode(float radius);

        // ---- line of sight (world positions on the grid's plane)
        // The cells a straight segment touches, in order from `from` to `to` (a supercover walk:
        // where it passes exactly through a corner, both cells beside the corner come before the
        // diagonal one). Cells outside the grid are included as they are met.
        void TraceLine(const glm::vec2& from, const glm::vec2& to, std::vector<glm::ivec2>& cells) const;
        // Whether an agent can walk straight from `from` to `to`: every cell the segment touches is
        // walkable (through a corner: both side cells too - no corner cutting) and, on 3D grids, each
        // step between them within MaxClimb.
        bool HasLineOfSight(const glm::vec2& from, const glm::vec2& to) const;

        // Bumped by every change (agents find their paths again when their grid changed).
        uint32_t GetRevision() const { return m_Revision; }
        size_t GetMemoryBytes() const;

        const std::vector<uint8_t>& GetWalkableData() const { return m_Walkable; }

    private:
        void Touch() { m_Revision++; }

        int m_Width = 0, m_Height = 0;
        float m_CellSize = 1.0f;
        glm::vec2 m_Origin{ 0.0f };
        std::vector<uint8_t> m_Walkable; // 1 walkable, 0 blocked
        std::vector<float> m_Cost;
        std::vector<float> m_Heights;    // empty: a flat (2D) grid
        float m_MaxClimb = 0.0f;
        uint32_t m_Revision = 1;
    };

}
