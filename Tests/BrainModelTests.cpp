#include "TestAssertions.h"
#include <Zamin/Gameplay/BrainModel.h>
#include <limits>
using namespace Zamin::Gameplay;
namespace {
    BrainDecision Step(BrainMemory& m, BrainInput in, BrainSettings s = {})
    {
        BrainDecision out;
        std::string error;
        CHECK(StepBrain(s, m, in, out, error));
        return out;
    }
    BrainInput Frame()
    {
        BrainInput in;
        in.Delta = .1f;
        in.HomeDistance = 2;
        in.TargetDistance = 5;
        in.TargetHomeDistance = 5;
        in.GoalDistance = 3;
        return in;
    }
}
TEST(BrainIdleAndPatrolSelectOnlyTheirOwnMovementIntent)
{
    BrainMemory m;
    auto in = Frame();
    CHECK(Step(m, in).Mode == BrainMode::Idle);
    in.Waypoints = 2;
    auto out = Step(m, in);
    CHECK(out.Mode == BrainMode::Patrol && out.Move && !out.Attack);
}
TEST(BrainVisibleHostileChasesThenAttacksWithHysteresis)
{
    BrainMemory m;
    auto in = Frame();
    in.Target = in.Visible = true;
    CHECK(Step(m, in).Mode == BrainMode::Chase);
    in.TargetDistance = 1.4f;
    CHECK(Step(m, in).Attack);
    in.TargetDistance = 1.7f;
    CHECK(Step(m, in).Mode == BrainMode::Attack);
    in.TargetDistance = 1.9f;
    CHECK(Step(m, in).Mode == BrainMode::Chase);
}
TEST(BrainLostSightInvestigatesAndNeverAttacksFromMemory)
{
    BrainMemory m;
    auto in = Frame();
    in.Target = in.Visible = true;
    in.TargetDistance = 1;
    Step(m, in);
    in.Visible = false;
    auto out = Step(m, in);
    CHECK(out.Mode == BrainMode::Investigate && out.Move && !out.Attack && out.Track);
}
TEST(BrainMemoryExpiresIntoReturnAndDoesNotReacquireBeforeHome)
{
    BrainMemory m;
    auto in = Frame();
    in.Target = in.Visible = true;
    Step(m, in);
    in.Visible = false;
    in.Delta = 3;
    CHECK(Step(m, in).Mode == BrainMode::Return);
    in.Visible = true;
    in.Delta = .1f;
    CHECK(Step(m, in).Mode == BrainMode::Return);
    in.HomeDistance = 0;
    CHECK(Step(m, in).Mode == BrainMode::Idle);
    CHECK(m.Returns == 1);
}
TEST(BrainDestroyedTargetSkipsMemoryAndReturns)
{
    BrainMemory m;
    m.Mode = BrainMode::Chase;
    m.Remember = 2;
    auto in = Frame();
    CHECK(Step(m, in).Mode == BrainMode::Return);
}
TEST(BrainHomeAndTargetLeashesForceReturn)
{
    BrainMemory m;
    auto in = Frame();
    in.Target = in.Visible = true;
    in.HomeDistance = 21;
    CHECK(Step(m, in).Mode == BrainMode::Return);
    m = {};
    in.HomeDistance = 0;
    in.TargetHomeDistance = 21;
    CHECK(Step(m, in).Mode == BrainMode::Return);
}
TEST(BrainReturnArrivalResumesPatrolWithoutLosingCounters)
{
    BrainMemory m;
    m.Mode = BrainMode::Return;
    m.Arrivals = 4;
    auto in = Frame();
    in.HomeDistance = 0;
    in.Waypoints = 2;
    CHECK(Step(m, in).Mode == BrainMode::Patrol);
    CHECK(m.Returns == 1 && m.Arrivals == 4);
}
TEST(BrainPatrolWaitThenPingPongDoesNotSkipEndpoints)
{
    BrainMemory m;
    auto in = Frame();
    in.Waypoints = 3;
    in.GoalDistance = 0;
    BrainSettings s;
    s.Wait = .2f;
    Step(m, in, s);
    CHECK(m.Waiting && m.Waypoint == 0 && m.Arrivals == 1);
    Step(m, in, s);
    CHECK(!m.Waiting && m.Waypoint == 1);
    Step(m, in, s);
    Step(m, in, s);
    CHECK(m.Waypoint == 2);
    Step(m, in, s);
    Step(m, in, s);
    CHECK(m.Waypoint == 1 && m.Direction == -1);
}
TEST(BrainLoopAndSingleWaypointHaveBoundedArrivalEdges)
{
    BrainMemory m;
    auto in = Frame();
    in.Waypoints = 2;
    in.GoalDistance = 0;
    BrainSettings s;
    s.Wait = 0;
    s.PingPong = false;
    Step(m, in, s);
    CHECK(m.Waypoint == 1);
    Step(m, in, s);
    CHECK(m.Waypoint == 0);
    m = {};
    in.Waypoints = 1;
    for (int i = 0; i < 100; ++i) Step(m, in, s);
    CHECK(m.Arrivals == 1 && m.Waiting);
}
TEST(BrainDeadStopsAndReviveReturnsWithoutOldMemory)
{
    BrainMemory m;
    m.Mode = BrainMode::Attack;
    m.Arrivals = 3;
    auto in = Frame();
    in.Alive = false;
    auto out = Step(m, in);
    CHECK(out.Mode == BrainMode::Dead && !out.Move && !out.Attack && m.Remember == 0);
    in.Alive = true;
    CHECK(Step(m, in).Mode == BrainMode::Return);
    CHECK(m.Arrivals == 3 && m.Remember == 0);
}
TEST(BrainDisabledSuppressesAllIntent)
{
    BrainMemory m;
    auto in = Frame();
    in.Target = in.Visible = true;
    in.Enabled = false;
    auto out = Step(m, in);
    CHECK(out.Mode == BrainMode::Disabled && !out.Move && !out.Attack && !out.Track);
}
TEST(BrainBlockedIsTerminalUntilExplicitReset)
{
    BrainMemory m;
    auto in = Frame();
    in.Blocked = true;
    CHECK(Step(m, in).Mode == BrainMode::Blocked);
    in.Blocked = false;
    in.Target = in.Visible = true;
    CHECK(Step(m, in).Mode == BrainMode::Blocked);
}
TEST(BrainInvalidSettingsAndInputsAreTransactional)
{
    BrainMemory m;
    m.Mode = BrainMode::Attack;
    m.Arrivals = 7;
    BrainDecision out;
    out.Mode = BrainMode::Dead;
    std::string error;
    auto in = Frame();
    BrainSettings s;
    s.Lose = 1;
    CHECK(!StepBrain(s, m, in, out, error));
    CHECK(m.Mode == BrainMode::Attack && m.Arrivals == 7 && out.Mode == BrainMode::Dead);
    s = {};
    in.Delta = std::numeric_limits<float>::quiet_NaN();
    CHECK(!StepBrain(s, m, in, out, error));
    in = Frame();
    in.Waypoints = 65;
    CHECK(!StepBrain(s, m, in, out, error));
    CHECK(m.Arrivals == 7);
}
TEST(BrainHitchExpiresMemoryWithoutUnboundedWaypointAdvances)
{
    BrainMemory m;
    m.Mode = BrainMode::Investigate;
    m.Remember = 2;
    auto in = Frame();
    in.Target = true;
    in.Delta = 60;
    CHECK(Step(m, in).Mode == BrainMode::Return);
    m = {};
    in.Target = false;
    in.Waypoints = 64;
    in.GoalDistance = 0;
    Step(m, in);
    CHECK(m.Arrivals == 1 && m.Waypoint == 1);
}
