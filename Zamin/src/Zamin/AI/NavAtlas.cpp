#include "Zamin/AI/NavAtlas.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <queue>
#include <set>

namespace Zamin {
    namespace {
        constexpr uint32_t Missing = UINT32_MAX;
        std::atomic<uint64_t> s_Revision{ 0 };
        bool Finite(glm::vec2 p) { return std::isfinite(p.x) && std::isfinite(p.y); }
    }
    bool NavAtlas::ValidateLinks(std::span<const NavTraversalLink> links, std::string& error)
    {
        error.clear();
        std::set<uint64_t> ids;
        if (links.size() > 4096)
        {
            error = "Too many navigation links";
            return false;
        }
        for (const auto& l : links)
            if (!l.ID || !ids.insert(l.ID).second || !Finite(l.From) || !Finite(l.To) ||
                !std::isfinite(l.Cost) || l.Cost <= 0 || l.Cost > 1e9f)
            {
                error = "Invalid or duplicate navigation link";
                return false;
            }
        return true;
    }
    bool NavAtlas::Build(std::span<const Source> sources, std::span<const NavTraversalLink> links, std::string& error, size_t maxCells)
    {
        if (!ValidateLinks(links, error)) return false;
        auto fail = [&](const char* why) { error = why; return false; };
        if (sources.size() > 512 || !maxCells || maxCells > 1048576) return fail("Invalid navigation atlas budget");
        std::vector<Source> ordered(sources.begin(), sources.end());
        std::sort(ordered.begin(), ordered.end(), [](auto a, auto b) { return a.ID < b.ID; });
        NavAtlas next;
        glm::dvec2 origin(0);
        double size = 1;
        if (!ordered.empty())
        {
            if (!ordered[0].Grid || ordered[0].Grid->IsEmpty()) return fail("Empty navigation source");
            origin = ordered[0].Grid->GetOrigin();
            size = ordered[0].Grid->GetCellSize();
        }
        uint64_t previous = 0;
        for (auto source : ordered)
        {
            if (!source.ID || source.ID == previous || !source.Grid || source.Grid->IsEmpty()) return fail("Invalid navigation source");
            previous = source.ID;
            const auto& g = *source.Grid;
            const auto& first = *ordered[0].Grid;
            if (std::abs(g.GetCellSize() - size) > size * 1e-6 || g.HasHeights() != first.HasHeights() ||
                g.GetMaxClimb() != first.GetMaxClimb()) return fail("Incompatible navigation tiles");
            const glm::dvec2 delta = (glm::dvec2(g.GetOrigin()) - origin) / size;
            if (!std::isfinite(delta.x) || !std::isfinite(delta.y) || std::abs(delta.x) > 1e8 || std::abs(delta.y) > 1e8 ||
                std::abs(delta.x - std::round(delta.x)) > .001 || std::abs(delta.y - std::round(delta.y)) > .001)
                return fail("Navigation tile is not aligned");
            const int64_t x = int64_t(std::llround(delta.x)), y = int64_t(std::llround(delta.y));
            for (const auto& t : next.m_Tiles)
                if (x < t.X + t.Grid.GetWidth() && x + g.GetWidth() > t.X &&
                    y < t.Y + t.Grid.GetHeight() && y + g.GetHeight() > t.Y) return fail("Navigation tiles overlap");
            if (g.GetCellCount() > maxCells - next.m_Cells) return fail("Navigation atlas cell budget exceeded");
            // Reject coordinates where float world positions cannot represent adjacent cell centres.
            for (const auto cell : { glm::ivec2(0), glm::ivec2(g.GetWidth() - 1, g.GetHeight() - 1) })
            {
                const auto centre = g.CellToWorld(cell);
                const glm::dvec2 expected = glm::dvec2(g.GetOrigin()) + (glm::dvec2(cell) + .5) * double(g.GetCellSize());
                const auto rounding = glm::abs(glm::dvec2(centre) - expected);
                if (!Finite(centre) || rounding.x > size * .001 || rounding.y > size * .001 ||
                    double(centre.x + float(size)) - centre.x < size * .999 || double(centre.x + float(size)) - centre.x > size * 1.001 ||
                    double(centre.y + float(size)) - centre.y < size * .999 || double(centre.y + float(size)) - centre.y > size * 1.001)
                    return fail("Navigation coordinates exceed float precision");
            }
            next.m_Tiles.push_back({ source.ID, g, x, y, uint32_t(next.m_Cells) });
            next.m_Cells += g.GetCellCount();
        }
        auto sorted = std::vector<NavTraversalLink>(links.begin(), links.end());
        std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.ID < b.ID; });
        for (const auto& l : sorted)
        {
            if (!l.Enabled) continue;
            const auto a = next.Snap(l.From, 0, NAN), b = next.Snap(l.To, 0, NAN);
            if (a == Missing || b == Missing || a == b) continue;
            next.m_Edges.push_back({ a, b, l.Cost, l.ID });
            if (l.Bidirectional) next.m_Edges.push_back({ b, a, l.Cost, l.ID });
            ++next.m_ActiveLinks;
        }
        std::stable_sort(next.m_Edges.begin(), next.m_Edges.end(), [](auto a, auto b) { return a.From < b.From; });
        next.m_Revision = ++s_Revision;
        *this = std::move(next);
        return true;
    }
    const NavAtlas::Tile& NavAtlas::Owner(uint32_t node) const
    {
        auto it = std::upper_bound(m_Tiles.begin(), m_Tiles.end(), node, [](uint32_t n, const Tile& t) { return n < t.Offset; });
        return *--it;
    }
    uint32_t NavAtlas::Locate(int64_t x, int64_t y) const
    {
        for (const auto& t : m_Tiles)
            if (x >= t.X && y >= t.Y && x < t.X + t.Grid.GetWidth() && y < t.Y + t.Grid.GetHeight())
                return t.Offset + uint32_t(t.Grid.Index(int(x - t.X), int(y - t.Y)));
        return Missing;
    }
    uint32_t NavAtlas::Snap(glm::vec2 p, int radius, float height) const
    {
        if (!Finite(p)) return Missing;
        uint32_t best = Missing;
        double distance = std::numeric_limits<double>::infinity();
        radius = std::clamp(radius, 0, 64);
        for (const auto& t : m_Tiles)
        {
            const auto& g = t.Grid;
            const auto continuous = (glm::dvec2(p) - glm::dvec2(g.GetOrigin())) / double(g.GetCellSize());
            if (continuous.x < -radius || continuous.y < -radius || continuous.x >= g.GetWidth() + radius || continuous.y >= g.GetHeight() + radius) continue;
            glm::ivec2 cell(int(std::floor(continuous.x)), int(std::floor(continuous.y))), found;
            if (!g.FindNearestWalkable(cell, radius, found, height, std::max(.5f, g.GetMaxClimb()))) continue;
            const auto d = glm::dvec2(g.CellToWorld(found)) - glm::dvec2(p);
            const double d2 = glm::dot(d, d);
            if (d2 < distance)
            {
                distance = d2;
                best = t.Offset + uint32_t(g.Index(found.x, found.y));
            }
        }
        return best;
    }
    NavRoutePoint NavAtlas::Point(uint32_t node) const
    {
        const auto& t = Owner(node);
        const auto c = t.Grid.CellOf(node - t.Offset);
        return { t.ID, t.Grid.CellToWorld(c), t.Grid.GetFloorHeight(c.x, c.y), 0 };
    }
    bool NavAtlas::CanStep(uint32_t from, uint32_t to) const
    {
        if (to == Missing) return false;
        const auto& a = Owner(from);
        const auto& b = Owner(to);
        const auto ac = a.Grid.CellOf(from - a.Offset), bc = b.Grid.CellOf(to - b.Offset);
        return b.Grid.IsWalkable(bc) && (!a.Grid.HasHeights() || a.Grid.GetMaxClimb() <= 0 ||
                                         std::abs(a.Grid.GetFloorHeight(ac.x, ac.y) - b.Grid.GetFloorHeight(bc.x, bc.y)) <= a.Grid.GetMaxClimb());
    }
    NavRoute NavAtlas::FindRoute(glm::vec2 from, glm::vec2 to, const NavPathOptions& options) const
    {
        NavRoute out;
        out.Revision = m_Revision;
        out.Status = NavPathStatus::Failed;
        if (m_Tiles.empty() || !Finite(from) || !Finite(to)) return out;
        const auto start = Snap(from, options.SnapRadius, options.StartHeight), goal = Snap(to, options.SnapRadius, NAN);
        if (start == Missing) return out;
        const auto target = goal == Missing ? glm::dvec2(to) : glm::dvec2(Point(goal).Position);
        const bool moved = goal != Missing && Snap(to, 0, NAN) != goal;
        auto heuristic = [&](uint32_t n) { return glm::length(glm::dvec2(Point(n).Position) - target); };
        // Cheap links invalidate a distance heuristic. Dijkstra keeps weight=1 optimal even when a
        // portal initially leads away from the target. Geometric distance still selects partial ends.
        const double weight = m_Edges.empty() ? (std::isfinite(options.HeuristicWeight) ? std::clamp(double(options.HeuristicWeight), 0.0, 100.0) : 1) : 0;
        struct Entry
        {
            double F, H, G;
            uint32_t Node;
        };
        auto later = [](const Entry& a, const Entry& b) { if (a.F != b.F) return a.F > b.F; if (a.H != b.H) return a.H > b.H; return a.Node > b.Node; };
        std::priority_queue<Entry, std::vector<Entry>, decltype(later)> open(later);
        std::vector<double> costs(m_Cells, std::numeric_limits<double>::infinity());
        std::vector<uint32_t> parent(m_Cells, Missing);
        std::vector<uint64_t> incoming(m_Cells, 0);
        std::vector<uint8_t> closed(m_Cells, 0);
        costs[start] = 0;
        parent[start] = start;
        open.push({ heuristic(start) * weight, heuristic(start), 0, start });
        uint32_t best = start;
        double bestH = heuristic(start);
        const size_t limit = options.MaxExpanded > 0 ? std::min(size_t(options.MaxExpanded), m_Cells) : m_Cells;
        constexpr int dx[] = { 1, -1, 0, 0, 1, 1, -1, -1 }, dy[] = { 0, 0, 1, -1, 1, -1, 1, -1 };
        while (!open.empty())
        {
            const auto item = open.top();
            open.pop();
            const auto node = item.Node;
            if (closed[node] || item.G != costs[node]) continue;
            closed[node] = 1;
            ++out.Expanded;
            const double h = heuristic(node);
            if (h < bestH || (h == bestH && costs[node] < costs[best]))
            {
                best = node;
                bestH = h;
            }
            if (node == goal)
            {
                best = node;
                out.Status = moved ? NavPathStatus::Partial : NavPathStatus::Complete;
                break;
            }
            if (out.Expanded >= limit) break;
            auto relax = [&](uint32_t next, double step, uint64_t link) {
                if (closed[next]) return;
                const auto cost = costs[node] + step;
                if (cost >= costs[next]) return;
                costs[next] = cost;
                parent[next] = node;
                incoming[next] = link;
                const double nh = heuristic(next);
                open.push({ cost + weight * nh, nh, cost, next });
            };
            const auto& tile = Owner(node);
            const auto cell = tile.Grid.CellOf(node - tile.Offset);
            const int64_t x = tile.X + cell.x, y = tile.Y + cell.y;
            for (int d = 0; d < (options.Diagonals ? 8 : 4); ++d)
            {
                const auto local = cell + glm::ivec2(dx[d], dy[d]);
                const auto next = tile.Grid.Contains(local) ? tile.Offset + uint32_t(tile.Grid.Index(local.x, local.y)) : Locate(x + dx[d], y + dy[d]);
                if (!CanStep(node, next)) continue;
                if (d >= 4)
                {
                    const bool a = CanStep(node, Locate(x + dx[d], y)), b = CanStep(node, Locate(x, y + dy[d]));
                    if (options.CutCorners ? !(a || b) : !(a && b)) continue;
                }
                const auto& dest = Owner(next);
                const auto c = dest.Grid.CellOf(next - dest.Offset);
                const double planar = tile.Grid.GetCellSize() * (d >= 4 ? std::sqrt(2.0) : 1.0);
                const double dh = double(Point(next).Height) - double(Point(node).Height);
                relax(next, std::hypot(planar, dh) * dest.Grid.GetCost(c.x, c.y), 0);
            }
            auto edge = std::lower_bound(m_Edges.begin(), m_Edges.end(), node, [](const Edge& e, uint32_t n) { return e.From < n; });
            for (; edge != m_Edges.end() && edge->From == node; ++edge) relax(edge->To, edge->Cost, edge->Link);
        }
        if (out.Status == NavPathStatus::Failed) out.Status = NavPathStatus::Partial;
        out.Cost = costs[best];
        for (uint32_t n = best;; n = parent[n])
        {
            auto p = Point(n);
            p.IncomingLink = incoming[n];
            out.Points.push_back(p);
            if (n == start) break;
        }
        std::reverse(out.Points.begin(), out.Points.end());
        return out;
    }
    size_t NavAtlas::GetMemoryBytes() const
    {
        size_t bytes = m_Tiles.capacity() * sizeof(Tile) + m_Edges.capacity() * sizeof(Edge);
        for (const auto& t : m_Tiles) bytes += t.Grid.GetMemoryBytes();
        return bytes;
    }
    void NavRouteFollower::Clear()
    {
        m_Route = {};
        m_Position = {};
        m_Next = 0;
        m_State = State::Idle;
    }
    bool NavRouteFollower::SetRoute(NavRoute route)
    {
        Clear();
        if ((route.Status != NavPathStatus::Complete && route.Status != NavPathStatus::Partial) || !route.Revision || route.Points.empty()) return false;
        for (const auto& p : route.Points)
            if (!Finite(p.Position) || !std::isfinite(p.Height)) return false;
        if (route.Points.front().IncomingLink) return false;
        m_Route = std::move(route);
        m_Position = m_Route.Points.front();
        m_Next = 1;
        m_State = State::Walking;
        Advance(m_Route.Revision, 0);
        return true;
    }
    NavRouteFollower::State NavRouteFollower::Advance(uint64_t revision, float distance)
    {
        if (m_State == State::Idle) return m_State;
        if (revision != m_Route.Revision) return m_State = State::Stale;
        if (m_State != State::Walking || !std::isfinite(distance) || distance < 0) return m_State;
        while (m_Next < m_Route.Points.size())
        {
            const auto& next = m_Route.Points[m_Next];
            if (next.IncomingLink) return m_State = State::AwaitingTraversal;
            const double length = glm::length(glm::dvec3(glm::dvec2(next.Position) - glm::dvec2(m_Position.Position), double(next.Height) - double(m_Position.Height)));
            if (distance < length)
            {
                const float t = length > 0 ? float(double(distance) / length) : 0;
                m_Position.Position = glm::mix(m_Position.Position, next.Position, t);
                m_Position.Height = glm::mix(m_Position.Height, next.Height, t);
                return m_State;
            }
            distance -= float(length);
            m_Position = next;
            ++m_Next;
        }
        return m_State = m_Route.Status == NavPathStatus::Complete ? State::Arrived : State::PartialEnd;
    }
    uint64_t NavRouteFollower::GetPendingLink() const
    {
        return m_State == State::AwaitingTraversal && m_Next < m_Route.Points.size() ? m_Route.Points[m_Next].IncomingLink : 0;
    }
    bool NavRouteFollower::CompleteTraversal(uint64_t revision, uint64_t link)
    {
        if (revision != m_Route.Revision)
        {
            if (m_State != State::Idle) m_State = State::Stale;
            return false;
        }
        if (!link || link != GetPendingLink()) return false;
        m_Position = m_Route.Points[m_Next++];
        m_State = State::Walking;
        Advance(revision, 0);
        return true;
    }
}
