#include "TestAssertions.h"
#include <Zamin/AI/NavQueryQueue.h>
#include <array>
#include <chrono>
#include <thread>
using namespace Zamin;
namespace {
    std::shared_ptr<const NavAtlas> QueryAtlas(int size = 32)
    {
        NavGrid grid(size, size);
        auto atlas = std::make_shared<NavAtlas>();
        std::string error;
        const std::array<NavAtlas::Source, 1> source{ { { 1, &grid } } };
        CHECK(atlas->Build(source, {}, error, 1048576));
        return atlas;
    }
    template<class F>
    bool WaitQuery(F condition)
    {
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        do {
            if (condition()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (std::chrono::steady_clock::now() < end);
        return condition();
    }
}
TEST(NavQueryQueueStartAndPublicationBudgetsAreSeparate)
{
    NavQueryQueue::Limits limits;
    limits.Workers = 1;
    NavQueryQueue queue(limits);
    auto atlas = QueryAtlas();
    std::string error;
    CHECK(queue.Submit(1, atlas, { .5f, .5f }, { 20.5f, 20.5f }, {}, error));
    CHECK(queue.Submit(2, atlas, { .5f, .5f }, { 21.5f, 21.5f }, {}, error));
    CHECK(queue.GetStats().Queued == 2 && queue.GetStats().Started == 0);
    queue.Pump(1, 0);
    CHECK(WaitQuery([&] { return queue.GetStats().Completed == 1; }));
    CHECK(queue.GetStats().Started == 1 && queue.GetStats().Queued == 1);
    NavQueryQueue::Result result;
    CHECK(!queue.Poll(1, result));
    queue.Pump(0, 1);
    CHECK(queue.Poll(1, result));
    CHECK(result.Key == 1 && result.Route.Status == NavPathStatus::Complete);
    CHECK(queue.GetStats().Started == 1 && queue.GetStats().SnapshotBytes == atlas->GetMemoryBytes());
    queue.Pump(1, 0);
    CHECK(WaitQuery([&] { return queue.GetStats().Completed == 1; }));
    queue.Pump(0, 1);
    CHECK(queue.Poll(2, result));
    CHECK(queue.GetStats().Outstanding == 0 && queue.GetStats().SnapshotBytes == 0 && queue.GetStats().ResultBytes == 0);
}
TEST(NavQueryQueueLatestRequestReplacesQueuedWorkWithinCapacity)
{
    NavQueryQueue::Limits limits;
    limits.Capacity = 1;
    NavQueryQueue queue(limits);
    auto atlas = QueryAtlas();
    std::string error;
    const auto first = queue.Submit(8, atlas, { .5f, .5f }, { 8.5f, 8.5f }, {}, error);
    CHECK(first && !queue.Submit(9, atlas, { .5f, .5f }, { 9.5f, 9.5f }, {}, error));
    const auto second = queue.Submit(8, atlas, { .5f, .5f }, { 25.5f, 25.5f }, {}, error);
    CHECK(second > first && queue.GetStats().Outstanding == 1 && queue.GetStats().Cancelled == 1);
    queue.Pump(1, 0);
    CHECK(WaitQuery([&] { return queue.GetStats().Completed == 1; }));
    queue.Pump(0, 1);
    NavQueryQueue::Result result;
    CHECK(queue.Poll(8, result));
    CHECK(result.Serial == second && result.To == glm::vec2(25.5f) && result.Route.Points.back().Position == glm::vec2(25.5f));
}
TEST(NavQueryQueueSharesSnapshotAccountingAndReleasesCancelledPayloads)
{
    NavQueryQueue queue({});
    auto atlas = QueryAtlas();
    std::string error;
    for (uint64_t key = 1; key <= 8; ++key) CHECK(queue.Submit(key, atlas, { .5f, .5f }, { 20.5f, 20.5f }, {}, error));
    CHECK(queue.GetStats().SnapshotBytes == atlas->GetMemoryBytes());
    queue.Cancel(1);
    CHECK(!queue.Has(1) && queue.GetStats().Outstanding == 7);
    queue.CancelAll();
    CHECK(queue.GetStats().Outstanding == 0 && queue.GetStats().SnapshotBytes == 0);
    CHECK(queue.GetStats().Cancelled == 8 && queue.Keys().empty());
    queue.Pump(10, 10);
    CHECK(queue.GetStats().Started == 0);
}
TEST(NavQueryQueueSnapshotBudgetRejectsWithoutDestroyingPreviousRequest)
{
    auto atlas = QueryAtlas();
    auto other = QueryAtlas();
    NavQueryQueue::Limits limits;
    limits.SnapshotBytes = atlas->GetMemoryBytes();
    limits.Capacity = 2;
    NavQueryQueue queue(limits);
    std::string error;
    const auto serial = queue.Submit(1, atlas, { .5f, .5f }, { 20.5f, 20.5f }, {}, error);
    CHECK(serial);
    CHECK(!queue.Submit(2, other, { .5f, .5f }, { 20.5f, 20.5f }, {}, error));
    CHECK(queue.Has(1) && !queue.Has(2) && queue.GetStats().Rejected == 1);
    // A queued replacement may exchange its one retained snapshot atomically.
    CHECK(queue.Submit(1, other, { .5f, .5f }, { 21.5f, 21.5f }, {}, error) > serial);
    CHECK(queue.GetStats().SnapshotBytes == other->GetMemoryBytes() && queue.GetStats().Outstanding == 1);
    queue.CancelAll();
    CHECK(queue.GetStats().SnapshotBytes == 0);
}
TEST(NavQueryQueueResultByteBudgetFailsExplicitlyAndDoesNotLeak)
{
    NavQueryQueue::Limits limits;
    limits.ResultBytes = sizeof(NavRoutePoint);
    NavQueryQueue queue(limits);
    auto atlas = QueryAtlas();
    std::string error;
    CHECK(queue.Submit(1, atlas, { .5f, .5f }, { 30.5f, 30.5f }, {}, error));
    queue.Pump(1, 0);
    CHECK(WaitQuery([&] { return queue.GetStats().Completed == 1; }));
    CHECK(queue.GetStats().ResultBytes == 0 && queue.GetStats().Discarded == 1);
    queue.Pump(0, 1);
    NavQueryQueue::Result result;
    CHECK(queue.Poll(1, result));
    CHECK(result.Route.Status == NavPathStatus::Failed && result.Route.Points.empty() && !result.Error.empty());
    CHECK(result.Route.Revision == atlas->GetRevision() && queue.GetStats().SnapshotBytes == 0);
}
TEST(NavQueryQueuePointAndExpansionLimitsAreEnforced)
{
    auto atlas = QueryAtlas();
    std::string error;
    NavQueryQueue::Limits limits;
    limits.MaxExpanded = 1;
    NavQueryQueue queue(limits);
    NavPathOptions options;
    options.MaxExpanded = 1000000;
    CHECK(queue.Submit(1, atlas, { .5f, .5f }, { 30.5f, 30.5f }, options, error));
    queue.Pump(1, 0);
    CHECK(WaitQuery([&] { return queue.GetStats().Completed == 1; }));
    queue.Pump(0, 1);
    NavQueryQueue::Result result;
    CHECK(queue.Poll(1, result));
    CHECK(result.Route.Status == NavPathStatus::Partial && result.Route.Expanded == 1 && result.Options.MaxExpanded == 1000000);
    limits.MaxExpanded = 65536;
    limits.MaxPoints = 1;
    NavQueryQueue pointQueue(limits);
    CHECK(pointQueue.Submit(2, atlas, { .5f, .5f }, { 30.5f, 30.5f }, {}, error));
    pointQueue.Pump(1, 0);
    CHECK(WaitQuery([&] { return pointQueue.GetStats().Completed == 1; }));
    pointQueue.Pump(0, 1);
    CHECK(pointQueue.Poll(2, result));
    CHECK(result.Route.Status == NavPathStatus::Failed && !result.Error.empty());
    CHECK(pointQueue.GetStats().ResultBytes == 0);
}
TEST(NavQueryQueueCancelsRunningSearchAndRetainsCapacityUntilAcknowledged)
{
    auto atlas = QueryAtlas(1024);
    NavQueryQueue::Limits limits;
    limits.Workers = 1;
    limits.Capacity = 1;
    limits.MaxExpanded = 1048576;
    NavQueryQueue queue(limits);
    std::string error;
    NavPathOptions options;
    options.MaxExpanded = 1048576;
    options.HeuristicWeight = 0;
    CHECK(queue.Submit(1, atlas, { .5f, .5f }, { 1023.5f, 1023.5f }, options, error));
    queue.Pump(1, 0);
    CHECK(WaitQuery([&] { return queue.GetStats().Running == 1; }));
    queue.Cancel(1);
    CHECK(!queue.Has(1));
    CHECK(WaitQuery([&] { return queue.GetStats().Outstanding == 0; }));
    CHECK(queue.GetStats().Cancelled == 1 && queue.GetStats().SnapshotBytes == 0 && queue.GetStats().ResultBytes == 0);
    NavQueryQueue::Result result;
    CHECK(!queue.Poll(1, result));
    CHECK(queue.Submit(2, atlas, { .5f, .5f }, { .5f, .5f }, {}, error));
}
TEST(NavQueryQueueShutdownJoinsAnActiveCancelledSearch)
{
    auto atlas = QueryAtlas(1024);
    std::string error;
    const auto start = std::chrono::steady_clock::now();
    {
        NavQueryQueue::Limits limits;
        limits.Workers = 1;
        limits.MaxExpanded = 1048576;
        NavQueryQueue queue(limits);
        NavPathOptions options;
        options.HeuristicWeight = 0;
        options.MaxExpanded = 1048576;
        CHECK(queue.Submit(1, atlas, { .5f, .5f }, { 1023.5f, 1023.5f }, options, error));
        queue.Pump(1, 0);
        CHECK(WaitQuery([&] { return queue.GetStats().Running == 1; }));
    }
    CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(5));
}
TEST(NavQueryQueueRejectsInvalidInputAndPrecancelledAtlasSearch)
{
    NavQueryQueue queue({});
    auto atlas = QueryAtlas();
    std::string error;
    CHECK(!queue.Submit(0, atlas, {}, {}, {}, error));
    CHECK(!queue.Submit(1, nullptr, {}, {}, {}, error));
    CHECK(!queue.Submit(1, atlas, { NAN, 0 }, {}, {}, error));
    std::stop_source stop;
    stop.request_stop();
    const auto result = atlas->FindRouteCancellable({ .5f, .5f }, { 20.5f, 20.5f }, {}, stop.get_token());
    CHECK(result.Status == NavPathStatus::Failed && result.Points.empty() && result.Expanded == 0);
    CHECK(atlas->SameWalkableCell({ .1f, .1f }, { .9f, .9f }) && !atlas->SameWalkableCell({ .1f, .1f }, { 1.1f, .1f }));
    CHECK(!atlas->SameWalkableCell({ -1, 0 }, { -1, 0 }));
}
TEST(NavQueryQueueMixedReplacementCyclesStayBounded)
{
    NavQueryQueue::Limits limits;
    limits.Workers = 2;
    limits.Capacity = 8;
    NavQueryQueue queue(limits);
    auto atlas = QueryAtlas(64);
    std::string error;
    for (int cycle = 0; cycle < 200; ++cycle)
    {
        queue.Submit(uint64_t(cycle % 12 + 1), atlas, { .5f, .5f }, { 60.5f, 60.5f }, {}, error);
        if (cycle % 3 == 0) queue.Cancel(uint64_t(cycle % 12 + 1));
        queue.Pump(2, 2);
        for (auto key : queue.Keys())
        {
            NavQueryQueue::Result result;
            queue.Poll(key, result);
        }
        const auto stats = queue.GetStats();
        CHECK(stats.Outstanding <= 8 && stats.SnapshotBytes <= limits.SnapshotBytes && stats.ResultBytes <= limits.ResultBytes);
    }
    queue.CancelAll();
    CHECK(WaitQuery([&] { return queue.GetStats().Outstanding == 0; }));
    CHECK(queue.GetStats().SnapshotBytes == 0 && queue.GetStats().ResultBytes == 0);
}
