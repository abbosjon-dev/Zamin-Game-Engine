#pragma once
#include <glm/glm.hpp>
#include <cstdint>
#include <span>
#include <vector>
namespace Zamin {
    // Deterministic, bounded predictive steering on a frozen owner-thread snapshot. No physics/ECS.
    // This chooses velocities; callers must still validate static navigation and apply physics.
    class NavCrowd
    {
    public:
        struct Agent
        {
            uint64_t ID = 0;
            glm::vec2 Position{ 0 }, Velocity{ 0 }, Preferred{ 0 };
            float Radius = .3f, MaxSpeed = 3.5f, NeighborDistance = 4, TimeHorizon = 1.5f, Priority = 1;
            float Floor = 0, Height = 1.8f;
            bool Mobile = true;
        };
        struct Output
        {
            glm::vec2 Velocity{ 0 };
            uint32_t Neighbors = 0;
            bool Limited = false, Blocked = false;
        };
        struct Stats
        {
            uint32_t Agents = 0, CandidateChecks = 0, NeighborPairs = 0, Limited = 0, Blocked = 0;
        };
        static constexpr uint32_t MaxAgents = 4096, MaxNeighbors = 16, MaxCandidates = 128;
        // Output matches input order. Invalid/duplicate IDs or capacity overflow fail closed.
        static bool Solve(std::span<const Agent> agents, float dt, std::vector<Output>& output, Stats& stats);
    };
}
