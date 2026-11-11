#include "Zamin/AI/NavCrowd.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <tuple>
namespace Zamin {
    namespace {
        bool Finite(glm::vec2 p) { return std::isfinite(p.x) && std::isfinite(p.y); }
        glm::vec2 Limit(glm::vec2 p, float speed)
        {
            const float length = glm::length(p);
            return length > speed && length > 0 ? p * (speed / length) : p;
        }
        struct Cell
        {
            int X, Y;
            size_t Index;
            uint64_t ID;
        };
        bool Earlier(const Cell& a, const Cell& b) { return std::tie(a.X, a.Y, a.ID) < std::tie(b.X, b.Y, b.ID); }
        // First time two discs meet; infinity means no predicted collision in the horizon.
        float Contact(glm::vec2 delta, glm::vec2 relative, float radius)
        {
            const float c = glm::dot(delta, delta) - radius * radius;
            if (c <= 0) return 0;
            const float a = glm::dot(relative, relative), b = glm::dot(delta, relative);
            if (a < 1e-8f || b <= 0) return INFINITY;
            const float d = b * b - a * c;
            return d < 0 ? INFINITY : std::max(0.f, (b - std::sqrt(d)) / a);
        }
    }
    bool NavCrowd::Solve(std::span<const Agent> agents, float dt, std::vector<Output>& output, Stats& stats)
    {
        stats = {};
        output.clear();
        if (agents.size() > MaxAgents || !std::isfinite(dt) || dt <= 0 || dt > .1f) return false;
        std::vector<Agent> safe(agents.begin(), agents.end());
        std::vector<Cell> cells;
        std::vector<uint64_t> ids;
        cells.reserve(safe.size());
        for (size_t i = 0; i < safe.size(); ++i)
        {
            auto& a = safe[i];
            if (!a.ID || !Finite(a.Position) || !Finite(a.Velocity) || !Finite(a.Preferred) ||
                std::abs(a.Position.x) > 1e8f || std::abs(a.Position.y) > 1e8f ||
                !std::isfinite(a.Radius) || !std::isfinite(a.MaxSpeed) || !std::isfinite(a.NeighborDistance) ||
                !std::isfinite(a.TimeHorizon) || !std::isfinite(a.Priority) || !std::isfinite(a.Floor) || !std::isfinite(a.Height)) return false;
            a.Radius = std::clamp(a.Radius, .01f, 10.f);
            a.MaxSpeed = std::clamp(a.MaxSpeed, 0.f, 1000.f);
            a.NeighborDistance = std::clamp(a.NeighborDistance, .1f, 32.f);
            a.TimeHorizon = std::clamp(a.TimeHorizon, .1f, 5.f);
            a.Priority = std::clamp(a.Priority, .1f, 10.f);
            a.Height = std::clamp(a.Height, .01f, 10.f);
            a.Velocity = Limit(a.Velocity, a.MaxSpeed);
            a.Preferred = a.Mobile ? Limit(a.Preferred, a.MaxSpeed) : glm::vec2(0);
            cells.push_back({ int(std::floor(a.Position.x / 4)), int(std::floor(a.Position.y / 4)), i, a.ID });
            ids.push_back(a.ID);
        }
        std::sort(ids.begin(), ids.end());
        if (std::adjacent_find(ids.begin(), ids.end()) != ids.end()) return false;
        std::sort(cells.begin(), cells.end(), Earlier);
        output.resize(safe.size());
        stats.Agents = uint32_t(safe.size());
        constexpr std::array<float, 11> angles{ 0, -.35f, .35f, -.7f, .7f, -1.05f, 1.05f, -1.57f, 1.57f, -2.4f, 2.4f };
        for (size_t i = 0; i < safe.size(); ++i)
        {
            const auto& a = safe[i];
            auto& result = output[i];
            if (!a.Mobile) continue;
            std::vector<std::pair<float, size_t>> nearby;
            nearby.reserve(MaxCandidates);
            uint32_t examined = 0;
            const int cx = int(std::floor(a.Position.x / 4)), cy = int(std::floor(a.Position.y / 4));
            const int range = int(std::ceil(a.NeighborDistance / 4));
            for (int x = cx - range; x <= cx + range && !result.Limited; ++x)
                for (int y = cy - range; y <= cy + range && !result.Limited; ++y)
                {
                    auto it = std::lower_bound(cells.begin(), cells.end(), Cell{ x, y, 0, 0 }, Earlier);
                    for (; it != cells.end() && it->X == x && it->Y == y; ++it)
                    {
                        if (it->Index == i) continue;
                        if (examined == MaxCandidates)
                        {
                            result.Limited = true;
                            break;
                        }
                        ++examined;
                        ++stats.CandidateChecks;
                        const auto& b = safe[it->Index];
                        if (a.Floor >= b.Floor + b.Height || b.Floor >= a.Floor + a.Height) continue;
                        const float d2 = glm::dot(b.Position - a.Position, b.Position - a.Position);
                        if (d2 <= a.NeighborDistance * a.NeighborDistance) nearby.emplace_back(d2, it->Index);
                    }
                }
            std::sort(nearby.begin(), nearby.end(), [&](auto left, auto right) {
                return left.first != right.first ? left.first < right.first : safe[left.second].ID < safe[right.second].ID;
            });
            if (nearby.size() > MaxNeighbors)
            {
                nearby.resize(MaxNeighbors);
                result.Limited = true;
            }
            result.Neighbors = uint32_t(nearby.size());
            stats.NeighborPairs += result.Neighbors;
            if (result.Limited)
            {
                result.Blocked = true;
                ++stats.Limited;
                ++stats.Blocked;
                continue; // bounded broadphase overflow is visible and stops rather than ignoring actors
            }
            const float speed = glm::length(a.Preferred);
            const auto forward = speed > 1e-6f ? a.Preferred / speed : glm::vec2(1, 0);
            glm::vec2 best(0);
            float bestScore = INFINITY;
            auto score = [&](glm::vec2 velocity, float bias) {
                float penalty = glm::length(velocity - a.Preferred) + .1f * glm::length(velocity - a.Velocity) + bias;
                for (const auto& [d2, index] : nearby)
                {
                    const auto& b = safe[index];
                    // Symmetric prediction from preferred velocities makes head-on agents choose
                    // the same passing convention without depending on entity update order.
                    const glm::vec2 other = b.Mobile ? b.Preferred : b.Velocity;
                    const auto delta = b.Position - a.Position;
                    const float radius = a.Radius + b.Radius + .04f;
                    const float hit = Contact(delta, velocity - other, radius);
                    if (hit < a.TimeHorizon)
                        penalty += (b.Mobile ? b.Priority / a.Priority : 2.f) * (20.f + 20.f * (a.TimeHorizon - hit) / a.TimeHorizon);
                    if (d2 < radius * radius)
                    {
                        // Allow an overlapped pair to separate; never reward deeper penetration.
                        const float away = glm::dot(velocity - other, delta);
                        penalty += away > 0 ? 100.f + away : away * 10.f;
                    }
                    if (d2 >= radius * radius && Contact(delta, velocity - b.Velocity, radius) <= dt) return; // reject immediate collisions
                }
                if (penalty < bestScore)
                {
                    bestScore = penalty;
                    best = velocity;
                }
            };
            for (float angle : angles)
            {
                const auto direction = glm::vec2(forward.x * std::cos(angle) - forward.y * std::sin(angle), forward.x * std::sin(angle) + forward.y * std::cos(angle));
                score(direction * speed, angle > 0 ? .035f : .0f);
            }
            score(a.Preferred * .5f, .01f);
            score(glm::vec2(0), .02f);
            result.Velocity = best;
            result.Blocked = speed > .01f && glm::length(best) < .01f;
            if (result.Blocked) ++stats.Blocked;
        }
        return true;
    }
}
