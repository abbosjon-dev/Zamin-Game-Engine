#include "TestAssertions.h"
#include <Zamin/AI/NavAtlas.h>
#include <Zamin/AI/NavMosaic.h>
#include <array>
#include <algorithm>
#include <thread>
using namespace Zamin;
namespace {
    struct AtlasFixture
    {
        NavGrid A{ 8, 8 }, B{ 8, 8, 1, { 8, 0 } };
        NavAtlas Atlas;
        std::string Error;
        bool Build(std::vector<NavTraversalLink> links = {}, size_t budget = 128)
        {
            const std::array<NavAtlas::Source, 2> sources{ { { 10, &A }, { 20, &B } } };
            return Atlas.Build(sources, links, Error, budget);
        }
    };
}
TEST(NavAtlasSparseGapsUseOnlyLoadedCells)
{
    AtlasFixture f;
    f.B.Reset(8, 8, 1, { 100000, 0 });
    CHECK(f.Build());
    CHECK(f.Atlas.GetCellCount() == 128 && f.Atlas.GetMemoryBytes() < 16384);
    NavGrid dense;
    const std::array<const NavGrid*, 2> grids{ &f.A, &f.B };
    CHECK(!BuildNavMosaic(grids, dense, f.Error, 128));
    CHECK(f.Atlas.FindRoute({ .5f, .5f }, { 100000.5f, .5f }).Status == NavPathStatus::Partial);
}
TEST(NavAtlasAdjacentTilesMatchDensePathCost)
{
    AtlasFixture f;
    f.A.FillRect(3, 0, 3, 5, false);
    f.B.SetCost(3, 6, 15);
    CHECK(f.Build());
    NavGrid dense;
    const std::array<const NavGrid*, 2> grids{ &f.A, &f.B };
    CHECK(BuildNavMosaic(grids, dense, f.Error));
    NavPathfinder finder;
    NavPathResult expected;
    for (bool diagonal : { false, true })
    {
        NavPathOptions options;
        options.Diagonals = diagonal;
        finder.FindPath(dense, glm::vec2(.5f), { 15.5f, .5f }, expected, options);
        auto route = f.Atlas.FindRoute(glm::vec2(.5f), { 15.5f, .5f }, options);
        CHECK(route.Status == NavPathStatus::Complete);
        CHECK_NEAR(route.Cost, expected.Cost, .001);
        CHECK(route.Points.front().Tile == 10 && route.Points.back().Tile == 20);
    }
}
TEST(NavAtlasOneWayLinksAndInactiveEndpoints)
{
    AtlasFixture f;
    f.B.Reset(8, 8, 1, { 10000, 0 });
    CHECK(f.Build({ { 7, { 2.5f, 2.5f }, { 10002.5f, 2.5f }, 3, false, true } }));
    auto route = f.Atlas.FindRoute({ 2.5f, 2.5f }, { 10002.5f, 2.5f });
    CHECK(route.Status == NavPathStatus::Complete && route.Points.size() == 2 && route.Points.back().IncomingLink == 7);
    CHECK_NEAR(route.Cost, 3, .00001);
    CHECK(f.Atlas.FindRoute({ 10002.5f, 2.5f }, { 2.5f, 2.5f }).Status == NavPathStatus::Partial);
    f.B.SetWalkable(2, 2, false);
    CHECK(f.Build({ { 7, { 2.5f, 2.5f }, { 10002.5f, 2.5f }, 3, true, true } }));
    CHECK(f.Atlas.GetActiveLinks() == 0);
    CHECK(f.Build({ { 7, { 2.5f, 2.5f }, { 10003.5f, 2.5f }, 3, true, false } }));
    CHECK(f.Atlas.GetActiveLinks() == 0);
}
TEST(NavAtlasCheapPortalAwayFromGoalRemainsOptimal)
{
    AtlasFixture f;
    CHECK(f.Build({ { 1, { .5f, .5f }, { 15.5f, .5f }, .25f, true, true } }));
    auto route = f.Atlas.FindRoute({ 2.5f, .5f }, { 13.5f, .5f });
    CHECK(route.Status == NavPathStatus::Complete);
    CHECK_NEAR(route.Cost, 4.25, .001);
    CHECK(std::count_if(route.Points.begin(), route.Points.end(), [](auto p) { return p.IncomingLink != 0; }) == 1);
}
TEST(NavAtlasBuildIsTransactionalAndCopiesSources)
{
    AtlasFixture f;
    CHECK(f.Build());
    const auto revision = f.Atlas.GetRevision();
    f.A.Fill(false);
    CHECK(f.Atlas.FindRoute({ .5f, .5f }, { 15.5f, .5f }).Status == NavPathStatus::Complete);
    f.B.Reset(8, 8, 1, { 0, 0 });
    CHECK(!f.Build());
    CHECK(f.Atlas.GetRevision() == revision && f.Atlas.GetCellCount() == 128);
    f.B.Reset(8, 8, 1, { 8, 0 });
    CHECK(!f.Build({}, 127));
    CHECK(f.Atlas.GetRevision() == revision);
    CHECK(f.Atlas.Build({}, {}, f.Error));
    CHECK(f.Atlas.GetRevision() != revision && f.Atlas.GetCellCount() == 0);
    CHECK(f.Atlas.FindRoute({}, {}).Status == NavPathStatus::Failed);
}
TEST(NavAtlasRejectsInvalidTilesAndLinks)
{
    AtlasFixture f;
    f.B.Reset(8, 8, 1, { 8.5f, 0 });
    CHECK(!f.Build());
    f.B.Reset(8, 8, .5f, { 8, 0 });
    CHECK(!f.Build());
    f.B.Reset(8, 8, 1, { 8, 0 });
    f.B.EnableHeights();
    CHECK(!f.Build());
    f.B.Reset(8, 8, 1, { 8, 0 });
    CHECK(!f.Build({ { 0, {}, {}, 1 } }));
    CHECK(!f.Build({ { 1, {}, {}, 1 }, { 1, {}, {}, 1 } }));
    CHECK(!f.Build({ { 1, { NAN, 0 }, {}, 1 } }));
    CHECK(!f.Build({ { 1, {}, {}, 0 } }));
    CHECK(!f.Build({ { 1, {}, {}, INFINITY } }));
    const std::array<NavAtlas::Source, 2> duplicate{ { { 1, &f.A }, { 1, &f.B } } };
    CHECK(!f.Atlas.Build(duplicate, {}, f.Error));
}
TEST(NavAtlasClimbCostsAndCornerRulesCrossTiles)
{
    AtlasFixture f;
    f.A.EnableHeights();
    f.B.EnableHeights();
    f.A.SetMaxClimb(1);
    f.B.SetMaxClimb(1);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) f.B.SetFloorHeight(x, y, 2);
    CHECK(f.Build());
    CHECK(f.Atlas.FindRoute({ 6.5f, 3.5f }, { 9.5f, 3.5f }).Status == NavPathStatus::Partial);
    f.B.SetFloorHeight(0, 3, 1);
    CHECK(f.Build());
    auto route = f.Atlas.FindRoute({ 6.5f, 3.5f }, { 9.5f, 3.5f });
    CHECK(route.Status == NavPathStatus::Complete && route.Points.back().Height == 2);
    CHECK(route.Cost > 3);
    f.A.Fill(false);
    f.B.Fill(false);
    f.A.SetWalkable(7, 3, true);
    f.B.SetWalkable(0, 4, true);
    f.B.SetFloorHeight(0, 4, 0);
    CHECK(f.Build());
    CHECK(f.Atlas.FindRoute({ 7.5f, 3.5f }, { 8.5f, 4.5f }).Status == NavPathStatus::Partial);
}
TEST(NavAtlasSnapHeightBoundsAndExpansionLimit)
{
    AtlasFixture f;
    CHECK(f.Build());
    NavPathOptions options;
    options.MaxExpanded = 1;
    auto route = f.Atlas.FindRoute({ .5f, .5f }, { 15.5f, 7.5f }, options);
    CHECK(route.Status == NavPathStatus::Partial && route.Expanded == 1 && route.Points.size() == 1);
    options.MaxExpanded = 0;
    options.SnapRadius = 0;
    CHECK(f.Atlas.FindRoute({ -100, 0 }, { 1, 1 }, options).Status == NavPathStatus::Failed);
    CHECK(f.Atlas.FindRoute({ NAN, 0 }, { 1, 1 }).Status == NavPathStatus::Failed);
    CHECK(f.Atlas.FindRoute({ 1, 1 }, { INFINITY, 1 }).Status == NavPathStatus::Failed);
    f.A.EnableHeights();
    f.B.EnableHeights();
    CHECK(f.Build());
    options.StartHeight = 10;
    CHECK(f.Atlas.FindRoute({ .5f, .5f }, { 15.5f, 7.5f }, options).Status == NavPathStatus::Failed);
}
TEST(NavAtlasCarvesAndRetainedSnapshots)
{
    AtlasFixture f;
    CHECK(f.Build());
    const auto old = f.Atlas;
    std::vector<uint32_t> wall;
    for (int y = 0; y < 8; ++y) wall.push_back(uint32_t(f.B.Index(0, y)));
    f.B.Carve(wall);
    CHECK(f.Build());
    CHECK(f.Atlas.FindRoute({ .5f, .5f }, { 15.5f, .5f }).Status == NavPathStatus::Partial);
    CHECK(old.FindRoute({ .5f, .5f }, { 15.5f, .5f }).Status == NavPathStatus::Complete);
    f.B.Uncarve(wall);
    CHECK(f.Build());
    CHECK(f.Atlas.FindRoute({ .5f, .5f }, { 15.5f, .5f }).Status == NavPathStatus::Complete);
}
TEST(NavAtlasDeterministicParallelQueries)
{
    AtlasFixture f;
    CHECK(f.Build());
    std::array<NavRoute, 8> paths;
    std::vector<std::jthread> threads;
    for (size_t i = 0; i < paths.size(); ++i) threads.emplace_back([&, i] { paths[i] = f.Atlas.FindRoute({ .5f, .5f }, { 15.5f, 7.5f }); });
    threads.clear();
    for (const auto& p : paths)
    {
        CHECK(p.Status == NavPathStatus::Complete && p.Cost == paths[0].Cost && p.Expanded == paths[0].Expanded);
        CHECK(p.Points.size() == paths[0].Points.size());
        for (size_t i = 0; i < p.Points.size(); ++i) CHECK(p.Points[i].Position == paths[0].Points[i].Position);
    }
}
TEST(NavRouteFollowerRequiresExplicitTraversalAndRejectsStale)
{
    AtlasFixture f;
    CHECK(f.Build({ { 5, { 2.5f, .5f }, { 12.5f, .5f }, 1, true, true } }));
    NavRouteFollower follower;
    CHECK(follower.SetRoute(f.Atlas.FindRoute({ .5f, .5f }, { 15.5f, .5f })));
    CHECK(follower.Advance(f.Atlas.GetRevision(), 100) == NavRouteFollower::State::AwaitingTraversal);
    CHECK(follower.GetPosition().Position.x == 2.5f && follower.GetPendingLink() == 5);
    CHECK(!follower.CompleteTraversal(f.Atlas.GetRevision(), 6));
    CHECK(follower.CompleteTraversal(f.Atlas.GetRevision(), 5));
    CHECK(follower.GetPosition().Position.x == 12.5f);
    CHECK(follower.Advance(f.Atlas.GetRevision(), 100) == NavRouteFollower::State::Arrived);
    CHECK(follower.Advance(f.Atlas.GetRevision() + 1, 100) == NavRouteFollower::State::Stale);
    CHECK(follower.GetPosition().Position.x == 15.5f);
}
TEST(NavRouteFollowerPartialDistanceAndInvalidInput)
{
    AtlasFixture f;
    CHECK(f.Build());
    NavRouteFollower follower;
    CHECK(follower.SetRoute(f.Atlas.FindRoute({ .5f, .5f }, { 4.5f, .5f })));
    const auto revision = f.Atlas.GetRevision();
    follower.Advance(revision, .25f);
    CHECK_NEAR(follower.GetPosition().Position.x, .75f, .0001);
    follower.Advance(revision, -1);
    follower.Advance(revision, NAN);
    CHECK_NEAR(follower.GetPosition().Position.x, .75f, .0001);
    auto partial = f.Atlas.FindRoute({ .5f, .5f }, { 1000, 0 });
    CHECK(follower.SetRoute(partial));
    CHECK(follower.Advance(revision, 100) == NavRouteFollower::State::PartialEnd);
    CHECK(!follower.SetRoute({}) && follower.GetState() == NavRouteFollower::State::Idle);
    partial.Points[0].Position.x = NAN;
    CHECK(!follower.SetRoute(partial));
}

TEST(NavAtlasRandomizedDenseCostOracle)
{
    AtlasFixture f;
    uint32_t seed = 7331;
    auto random = [&] { seed = seed * 1664525u + 1013904223u; return seed; };
    for (int run = 0; run < 30; ++run)
    {
        for (auto* grid : { &f.A, &f.B })
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x)
                {
                    grid->SetWalkable(x, y, random() % 5 != 0);
                    grid->SetCost(x, y, float(1 + random() % 4));
                }
        f.A.SetWalkable(0, 0, true);
        f.B.SetWalkable(7, 7, true);
        CHECK(f.Build());
        NavGrid dense;
        const std::array<const NavGrid*, 2> grids{ &f.A, &f.B };
        CHECK(BuildNavMosaic(grids, dense, f.Error));
        NavPathfinder finder;
        NavPathResult expected;
        NavPathOptions options;
        options.Diagonals = run % 2;
        options.CutCorners = run % 3;
        finder.FindPath(dense, glm::vec2(.5f), { 15.5f, 7.5f }, expected, options);
        const auto route = f.Atlas.FindRoute(glm::vec2(.5f), { 15.5f, 7.5f }, options);
        CHECK(route.Status == expected.Status);
        if (route.Status == NavPathStatus::Complete) CHECK_NEAR(route.Cost, expected.Cost, .001);
    }
}
TEST(NavAtlasSourceOrderingAndBudgetedLargeWorld)
{
    NavGrid a(256, 256), b(256, 256, 1, { 10000, 10000 });
    std::array<NavAtlas::Source, 2> sources{ { { 99, &b }, { 5, &a } } };
    NavAtlas first, second;
    std::string error;
    CHECK(first.Build(sources, {}, error));
    std::reverse(sources.begin(), sources.end());
    CHECK(second.Build(sources, {}, error));
    NavPathOptions options;
    options.MaxExpanded = 32;
    options.SnapRadius = 0;
    const auto x = first.FindRoute({ .5f, .5f }, { 10255.5f, 10255.5f }, options);
    const auto y = second.FindRoute({ .5f, .5f }, { 10255.5f, 10255.5f }, options);
    CHECK(x.Status == NavPathStatus::Partial && x.Expanded == 32 && x.Cost == y.Cost);
    CHECK(first.GetCellCount() == 131072 && x.Points.size() == y.Points.size());
    for (size_t i = 0; i < x.Points.size(); ++i) CHECK(x.Points[i].Position == y.Points[i].Position);
}

TEST(NavAtlasRejectsCollapsedFloatCellCentres)
{
    AtlasFixture f;
    f.A.Reset(8, 8, 1, { 16777210, 0 });
    f.B.Reset(8, 8, 1, { 16777218, 0 });
    CHECK(!f.Build());
    CHECK(f.Error.find("precision") != std::string::npos);
    f.A.Reset(8, 8, 1, { -16777218, 0 });
    f.B.Reset(8, 8, 1, { -16777210, 0 });
    CHECK(!f.Build());
    f.A.Reset(8, 8, 1, { 100000, 0 });
    f.B.Reset(8, 8, 1, { 100008, 0 });
    CHECK(f.Build());
}

TEST(NavAtlasAndFollowerLargeFiniteHeightDoesNotOverflow)
{
    NavGrid grid(2, 1);
    grid.EnableHeights();
    grid.SetFloorHeight(0, 0, 2e38f);
    grid.SetFloorHeight(1, 0, -2e38f);
    NavAtlas atlas;
    std::string error;
    const std::array<NavAtlas::Source, 1> sources{ { { 1, &grid } } };
    CHECK(atlas.Build(sources, {}, error));
    const auto route = atlas.FindRoute({ .5f, .5f }, { 1.5f, .5f });
    CHECK(route.Status == NavPathStatus::Complete && std::isfinite(route.Cost) && route.Cost > 3e38);
    NavRouteFollower follower;
    CHECK(follower.SetRoute(route));
    CHECK(follower.Advance(atlas.GetRevision(), 1e38f) == NavRouteFollower::State::Walking);
    CHECK_NEAR(double(follower.GetPosition().Height) / 1e38, 1, .001);
    CHECK_NEAR(follower.GetPosition().Position.x, .75f, .001);
}
