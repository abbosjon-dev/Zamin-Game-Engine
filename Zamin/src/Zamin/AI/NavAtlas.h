#pragma once
#include "Zamin/AI/NavPathfinder.h"
#include <memory>
#include <span>
#include <string>
#include <stop_token>

namespace Zamin {
    // A link is an explicit gameplay transition, never an ordinary walk through empty space.
    // Endpoints must be on loaded walkable cells. Missing/blocked endpoints leave it inactive.
    struct NavTraversalLink
    {
        uint64_t ID = 0;
        glm::vec2 From{ 0 }, To{ 0 };
        float Cost = 1;
        bool Bidirectional = true, Enabled = true;
    };
    struct NavRoutePoint
    {
        uint64_t Tile = 0;
        glm::vec2 Position{ 0 };
        float Height = 0;
        uint64_t IncomingLink = 0;
    };
    struct NavRoute
    {
        uint64_t Revision = 0;
        NavPathStatus Status = NavPathStatus::None;
        double Cost = 0;
        size_t Expanded = 0;
        std::vector<NavRoutePoint> Points;
    };
    // Immutable after publication. Retain shared_ptr<const NavAtlas> while running parallel queries.
    // Memory depends on loaded cells, not the rectangle enclosing them. All tiles share a lattice,
    // resolution and climb rule. Build is transactional, including an empty tile set.
    class NavAtlas
    {
    public:
        struct Source
        {
            uint64_t ID;
            const NavGrid* Grid;
        };
        static bool ValidateLinks(std::span<const NavTraversalLink> links, std::string& error);
        bool Build(std::span<const Source> sources, std::span<const NavTraversalLink> links, std::string& error,
                   size_t maxCells = 262144);
        // Query-local scratch is proportional to loaded cells. SnapRadius is clamped to 0..64.
        NavRoute FindRoute(glm::vec2 from, glm::vec2 to, const NavPathOptions& options = {}) const;
        NavRoute FindRouteCancellable(glm::vec2 from, glm::vec2 to, const NavPathOptions& options, std::stop_token stop) const;
        bool SameWalkableCell(glm::vec2 first, glm::vec2 second) const;
        uint64_t GetRevision() const { return m_Revision; }
        size_t GetCellCount() const { return m_Cells; }
        size_t GetTileCount() const { return m_Tiles.size(); }
        size_t GetActiveLinks() const { return m_ActiveLinks; }
        size_t GetMemoryBytes() const;
    private:
        struct Tile
        {
            uint64_t ID;
            NavGrid Grid;
            int64_t X, Y;
            uint32_t Offset;
        };
        struct Edge
        {
            uint32_t From, To;
            double Cost;
            uint64_t Link;
        };
        std::vector<Tile> m_Tiles;
        std::vector<Edge> m_Edges;
        size_t m_Cells = 0, m_ActiveLinks = 0;
        uint64_t m_Revision = 0;
        uint32_t Locate(int64_t x, int64_t y) const;
        uint32_t Snap(glm::vec2 p, int radius, float height) const;
        const Tile& Owner(uint32_t node) const;
        NavRoutePoint Point(uint32_t node) const;
        bool CanStep(uint32_t from, uint32_t to) const;
    };

    // Kinematic route cursor. It stops at a link until gameplay explicitly completes that link;
    // completion returns the exit position. Physics/collision and animations belong to the caller.
    // A changed atlas revision stops movement, including any pending link, until SetRoute again.
    class NavRouteFollower
    {
    public:
        enum class State
        {
            Idle,
            Walking,
            AwaitingTraversal,
            Arrived,
            PartialEnd,
            Stale
        };
        bool SetRoute(NavRoute route);
        State Advance(uint64_t revision, float distance);
        bool CompleteTraversal(uint64_t revision, uint64_t link);
        void Clear();
        State GetState() const { return m_State; }
        const NavRoutePoint& GetPosition() const { return m_Position; }
        uint64_t GetPendingLink() const;
    private:
        NavRoute m_Route;
        NavRoutePoint m_Position;
        size_t m_Next = 0;
        State m_State = State::Idle;
    };
}
