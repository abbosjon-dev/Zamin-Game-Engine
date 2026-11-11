#include "TestAssertions.h"
#include <Zamin/AI/NavCrowd.h>
#include <Zamin/AI/NavAtlas.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <set>
using namespace Zamin;
namespace {
    NavCrowd::Agent Walker(uint64_t id, glm::vec2 p, glm::vec2 v)
    {
        NavCrowd::Agent a;
        a.ID = id;
        a.Position = p;
        a.Velocity = a.Preferred = v;
        a.MaxSpeed = 2;
        return a;
    }
    bool Run(std::vector<NavCrowd::Agent>& agents, std::span<const glm::vec2> goals, int steps, float& separation)
    {
        std::vector<NavCrowd::Output> output;
        NavCrowd::Stats stats;
        separation = INFINITY;
        for (int tick = 0; tick < steps; ++tick)
        {
            for (size_t i = 0; i < agents.size(); ++i)
            {
                auto delta = goals[i] - agents[i].Position;
                const float d = glm::length(delta);
                agents[i].Preferred = d > .04f ? delta * (std::min(2.f, d * 3) / d) : glm::vec2(0);
            }
            if (!NavCrowd::Solve(agents, 1.f / 60, output, stats)) return false;
            for (size_t i = 0; i < agents.size(); ++i)
            {
                agents[i].Velocity = output[i].Velocity;
                agents[i].Position += output[i].Velocity / 60.f;
            }
            for (size_t i = 0; i < agents.size(); ++i)
                for (size_t j = i + 1; j < agents.size(); ++j)
                    separation = std::min(separation, glm::length(agents[i].Position - agents[j].Position));
        }
        return true;
    }
}
TEST(NavCrowdIsolatedAgentKeepsPreferredAndSpeedCap)
{
    auto a = Walker(1, { 0, 0 }, { 100, 0 });
    std::vector<NavCrowd::Output> out;
    NavCrowd::Stats stats;
    CHECK(NavCrowd::Solve(std::span(&a, 1), .02f, out, stats));
    CHECK_NEAR(glm::length(out[0].Velocity), 2, .001);
    CHECK(out[0].Neighbors == 0 && !out[0].Blocked);
}
TEST(NavCrowdHeadOnPairPassesWithoutOverlap)
{
    std::vector<NavCrowd::Agent> agents{ Walker(1, { -4, 0 }, { 2, 0 }), Walker(2, { 4, 0 }, { -2, 0 }) };
    const std::array<glm::vec2, 2> goals{ glm::vec2(4, 0), glm::vec2(-4, 0) };
    float separation;
    CHECK(Run(agents, goals, 600, separation));
    CHECK(separation > .58f);
    CHECK(glm::length(agents[0].Position - goals[0]) < .1f && glm::length(agents[1].Position - goals[1]) < .1f);
}
TEST(NavCrowdFourWayCrossingReachesGoals)
{
    std::vector<NavCrowd::Agent> agents{ Walker(1, { -4, 0 }, { 2, 0 }), Walker(2, { 4, 0 }, { -2, 0 }), Walker(3, { 0, -4 }, { 0, 2 }), Walker(4, { 0, 4 }, { 0, -2 }) };
    const std::array<glm::vec2, 4> goals{ glm::vec2(4, 0), glm::vec2(-4, 0), glm::vec2(0, 4), glm::vec2(0, -4) };
    float separation;
    CHECK(Run(agents, goals, 900, separation));
    CHECK(separation > .54f);
    for (size_t i = 0; i < agents.size(); ++i) CHECK(glm::length(agents[i].Position - goals[i]) < .12f);
}
TEST(NavCrowdPermutationDoesNotChangeVelocities)
{
    std::vector<NavCrowd::Agent> agents{ Walker(11, { -1, 0 }, { 2, 0 }), Walker(5, { 1, 0 }, { -2, 0 }), Walker(3, { 0, 1 }, { 0, -2 }) };
    std::vector<NavCrowd::Output> a, b;
    NavCrowd::Stats stats;
    CHECK(NavCrowd::Solve(agents, .02f, a, stats));
    std::reverse(agents.begin(), agents.end());
    CHECK(NavCrowd::Solve(agents, .02f, b, stats));
    for (size_t i = 0; i < a.size(); ++i) CHECK(glm::length(a[i].Velocity - b[a.size() - 1 - i].Velocity) < 1e-6f);
}
TEST(NavCrowdStationaryAndDifferentFloors)
{
    std::array<NavCrowd::Agent, 2> a{ Walker(1, { 0, 0 }, { 2, 0 }), Walker(2, { 1, 0 }, { 0, 0 }) };
    a[1].Mobile = false;
    std::vector<NavCrowd::Output> out;
    NavCrowd::Stats stats;
    CHECK(NavCrowd::Solve(a, .02f, out, stats));
    CHECK(out[1].Velocity == glm::vec2(0));
    CHECK(out[0].Neighbors == 1);
    a[1].Floor = 5;
    CHECK(NavCrowd::Solve(a, .02f, out, stats));
    CHECK(out[0].Neighbors == 0);
    CHECK_NEAR(out[0].Velocity.x, 2, .001);
}
TEST(NavCrowdOverlappingPairSeparatesDeterministically)
{
    std::array<NavCrowd::Agent, 2> a{ Walker(1, { -.1f, 0 }, { -2, 0 }), Walker(2, { .1f, 0 }, { 2, 0 }) };
    std::vector<NavCrowd::Output> out;
    NavCrowd::Stats stats;
    CHECK(NavCrowd::Solve(a, .02f, out, stats));
    CHECK(out[0].Velocity.x < 0 && out[1].Velocity.x > 0);
}
TEST(NavCrowdNeighborAndCandidateOverflowStopVisibly)
{
    std::vector<NavCrowd::Agent> agents;
    for (uint64_t i = 1; i <= 200; ++i) agents.push_back(Walker(i, { float(i) * .001f, 0 }, { 1, 0 }));
    std::vector<NavCrowd::Output> out;
    NavCrowd::Stats stats;
    CHECK(NavCrowd::Solve(agents, .02f, out, stats));
    CHECK(stats.Limited == agents.size());
    CHECK(stats.CandidateChecks <= agents.size() * NavCrowd::MaxCandidates);
    for (auto result : out) CHECK(result.Limited && result.Blocked && result.Velocity == glm::vec2(0));
}
TEST(NavCrowdInvalidInputAndCapacityFailClosed)
{
    std::vector<NavCrowd::Agent> a{ Walker(1, { 0, 0 }, { 1, 0 }), Walker(1, { 2, 0 }, { -1, 0 }) };
    std::vector<NavCrowd::Output> out;
    NavCrowd::Stats stats;
    CHECK(!NavCrowd::Solve(a, .02f, out, stats) && out.empty());
    a[1].ID = 2;
    a[1].Position.x = NAN;
    CHECK(!NavCrowd::Solve(a, .02f, out, stats));
    a[1].Position.x = 2;
    CHECK(!NavCrowd::Solve(a, NAN, out, stats));
    a.resize(NavCrowd::MaxAgents + 1);
    CHECK(!NavCrowd::Solve(a, .02f, out, stats));
}
TEST(NavCrowdSparseBroadphaseSkipsDistantActors)
{
    std::vector<NavCrowd::Agent> a;
    for (uint64_t i = 1; i <= 1000; ++i) a.push_back(Walker(i, { float(i) * 64, 0 }, { 1, 0 }));
    std::vector<NavCrowd::Output> out;
    NavCrowd::Stats stats;
    CHECK(NavCrowd::Solve(a, .02f, out, stats));
    CHECK(stats.CandidateChecks == 0 && stats.NeighborPairs == 0);
}
TEST(NavAtlasSegmentRejectsHolesWallsCornersAndBudgets)
{
    NavGrid a(8, 8), b(8, 8, 1, { 16, 0 });
    NavAtlas atlas;
    std::string error;
    std::array<NavAtlas::Source, 2> sources{ { { 1, &a }, { 2, &b } } };
    CHECK(atlas.Build(sources, {}, error));
    CHECK(atlas.CanWalkSegment({ .5f, .5f }, { 7.5f, 7.5f }));
    CHECK(!atlas.CanWalkSegment({ .5f, .5f }, { 17.5f, .5f }));
    NavSegmentOptions limited;
    limited.MaxCells = 4;
    CHECK(!atlas.CanWalkSegment({ .5f, .5f }, { 7.5f, 7.5f }, limited));
    a.SetWalkable(3, 2, false);
    CHECK(atlas.Build(sources, {}, error));
    CHECK(!atlas.CanWalkSegment({ 2.5f, 2.5f }, { 3.5f, 3.5f }));
    CHECK(!atlas.CanWalkSegment({ NAN, 0 }, { 1, 1 }));
}
TEST(NavAtlasSegmentClearanceAndWeightedCostAreConservative)
{
    NavGrid grid(8, 8);
    grid.SetWalkable(3, 2, false);
    grid.SetCost(5, 4, 4);
    NavAtlas atlas;
    std::string error;
    const NavAtlas::Source source{ 1, &grid };
    CHECK(atlas.Build(std::span(&source, 1), {}, error));
    CHECK(atlas.CanWalkSegment({ 2.5f, 3.25f }, { 4.5f, 3.25f }));
    NavSegmentOptions options;
    options.Radius = .3f;
    CHECK(!atlas.CanWalkSegment({ 2.5f, 3.25f }, { 4.5f, 3.25f }, options));
    CHECK(atlas.CanWalkSegment({ 4.5f, 4.5f }, { 6.5f, 4.5f }));
    options.Radius = 0;
    options.UnitCostOnly = true;
    CHECK(!atlas.CanWalkSegment({ 4.5f, 4.5f }, { 6.5f, 4.5f }, options));
}
TEST(NavAtlasSmoothingReducesCornersAndPreservesPortalHandshake)
{
    NavGrid a(8, 8), b(8, 8, 1, { 100, 0 });
    NavAtlas atlas;
    std::string error;
    std::array<NavAtlas::Source, 2> sources{ { { 1, &a }, { 2, &b } } };
    std::array<NavTraversalLink, 1> links{ { { 9, { 6.5f, 3.5f }, { 101.5f, 3.5f }, 1, true, true } } };
    CHECK(atlas.Build(sources, links, error));
    auto raw = atlas.FindRoute({ .5f, 3.5f }, { 106.5f, 3.5f });
    auto smooth = atlas.SmoothRoute(raw);
    CHECK(smooth.Points.size() < raw.Points.size());
    CHECK(smooth.Status == raw.Status && smooth.Cost == raw.Cost);
    auto point = std::find_if(smooth.Points.begin(), smooth.Points.end(), [](const auto& p) { return p.IncomingLink == 9; });
    CHECK(point != smooth.Points.end() && point != smooth.Points.begin());
    if (point != smooth.Points.end() && point != smooth.Points.begin()) CHECK((point - 1)->Position == links[0].From && point->Position == links[0].To);
    for (size_t i = 1; i < smooth.Points.size(); ++i)
        if (!smooth.Points[i].IncomingLink) CHECK(atlas.CanWalkSegment(smooth.Points[i - 1].Position, smooth.Points[i].Position));
}
TEST(NavAtlasWorkerSmoothingOptionAndCancellation)
{
    NavGrid grid(32, 32);
    NavAtlas atlas;
    std::string error;
    NavAtlas::Source source{ 1, &grid };
    CHECK(atlas.Build(std::span(&source, 1), {}, error));
    NavPathOptions options;
    auto raw = atlas.FindRoute({ .5f, .5f }, { 30.5f, 30.5f }, options);
    options.SmoothAtlas = true;
    auto smooth = atlas.FindRoute({ .5f, .5f }, { 30.5f, 30.5f }, options);
    CHECK(smooth.Points.size() < raw.Points.size());
    std::stop_source stop;
    stop.request_stop();
    CHECK(atlas.SmoothRoute(raw, stop.get_token()).Status == NavPathStatus::Failed);
}
TEST(NavAtlasSegmentChecksFloorClimbAndStartHeight)
{
    NavGrid grid(8, 8);
    grid.EnableHeights();
    grid.SetMaxClimb(.3f);
    grid.SetFloorHeight(4, 4, 2);
    NavAtlas atlas;
    std::string error;
    NavAtlas::Source source{ 1, &grid };
    CHECK(atlas.Build(std::span(&source, 1), {}, error));
    CHECK(!atlas.CanWalkSegment({ 3.5f, 4.5f }, { 5.5f, 4.5f }));
    NavSegmentOptions options;
    options.StartHeight = 5;
    CHECK(!atlas.CanWalkSegment({ 1.5f, 1.5f }, { 2.5f, 1.5f }, options));
    grid.SetMaxClimb(0);
    CHECK(atlas.Build(std::span(&source, 1), {}, error));
    CHECK(atlas.CanWalkSegment({ 3.5f, 4.5f }, { 5.5f, 4.5f }));
}
TEST(NavAtlasSmoothingKeepsWeightedDetourAndStaleRoutes)
{
    NavGrid grid(12, 12);
    for (int y = 1; y < 10; ++y) grid.SetCost(6, y, 100);
    NavAtlas atlas;
    std::string error;
    NavAtlas::Source source{ 1, &grid };
    CHECK(atlas.Build(std::span(&source, 1), {}, error));
    auto route = atlas.FindRoute({ 1.5f, 5.5f }, { 10.5f, 5.5f });
    auto smooth = atlas.SmoothRoute(route);
    NavSegmentOptions options;
    options.UnitCostOnly = true;
    for (size_t i = 1; i < smooth.Points.size(); ++i)
    {
        bool original = false;
        for (size_t j = 1; j < route.Points.size(); ++j)
            original |= route.Points[j - 1].Position == smooth.Points[i - 1].Position && route.Points[j].Position == smooth.Points[i].Position;
        CHECK(atlas.CanWalkSegment(smooth.Points[i - 1].Position, smooth.Points[i].Position));
        if (!original) CHECK(atlas.CanWalkSegment(smooth.Points[i - 1].Position, smooth.Points[i].Position, options));
    }
    CHECK(atlas.Build(std::span(&source, 1), {}, error));
    auto stale = atlas.SmoothRoute(route);
    CHECK(stale.Points.size() == route.Points.size() && stale.Revision == route.Revision);
}
TEST(NavCrowdSettingsClampAndNonfiniteReject)
{
    auto a = Walker(1, { 0, 0 }, { 1, 0 });
    a.Radius = -1;
    a.TimeHorizon = 100;
    a.Priority = 0;
    a.NeighborDistance = 100;
    std::vector<NavCrowd::Output> out;
    NavCrowd::Stats stats;
    CHECK(NavCrowd::Solve(std::span(&a, 1), .02f, out, stats));
    CHECK(std::isfinite(out[0].Velocity.x));
    a.Priority = INFINITY;
    CHECK(!NavCrowd::Solve(std::span(&a, 1), .02f, out, stats));
}
TEST(NavCrowdMovingUnresponsiveObstacleIsPredicted)
{
    std::array<NavCrowd::Agent, 2> a{ Walker(1, { 0, 0 }, { 2, 0 }), Walker(2, { 1.2f, 0 }, { -2, 0 }) };
    a[1].Mobile = false;
    std::vector<NavCrowd::Output> out;
    NavCrowd::Stats stats;
    CHECK(NavCrowd::Solve(a, .02f, out, stats));
    CHECK(out[0].Velocity != a[0].Preferred && out[1].Velocity == glm::vec2(0));
}
