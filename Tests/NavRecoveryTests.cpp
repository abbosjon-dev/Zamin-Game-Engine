#include "TestAssertions.h"
#include <Zamin/AI/NavRecovery.h>
#include <cmath>
using namespace Zamin;
namespace {
    NavRecovery::Settings Config()
    {
        NavRecovery::Settings s;
        s.Enabled = true;
        s.StallSeconds = .2f;
        s.CooldownSeconds = .2f;
        s.MaxRetries = 2;
        return s;
    }
    NavRecovery::Input Frame()
    {
        NavRecovery::Input in;
        in.HasIntent = true;
        in.Delta = .1f;
        return in;
    }
}
TEST(NavRecoveryBoundsRetriesAndBecomesTerminal)
{
    auto config = Config();
    auto in = Frame();
    NavRecovery recovery;
    int retries = 0;
    for (int frame = 0; frame < 50; ++frame) retries += recovery.Observe(config, in) == NavRecovery::Action::Retry;
    CHECK(retries == 2 && recovery.IsExhausted() && recovery.GetRetries() == 2);
    CHECK(recovery.Observe(config, in) == NavRecovery::Action::Stop);
}
TEST(NavRecoveryProgressAccumulatesAndClearsAttempts)
{
    auto config = Config();
    auto in = Frame();
    NavRecovery recovery;
    for (int frame = 0; frame < 3; ++frame) recovery.Observe(config, in);
    CHECK(recovery.GetRetries() == 1);
    for (int i = 1; i <= 5; ++i)
    {
        in.Position.x = .011f * i;
        recovery.Observe(config, in);
    }
    CHECK(recovery.GetRetries() == 0 && !recovery.IsExhausted());
}
TEST(NavRecoverySmallJitterCannotResetWatchdog)
{
    auto config = Config();
    auto in = Frame();
    NavRecovery recovery;
    for (int i = 0; i < 50; ++i)
    {
        in.Position.x = i % 2 ? .001f : 0;
        recovery.Observe(config, in);
    }
    CHECK(recovery.IsExhausted());
}
TEST(NavRecoveryPendingQueriesDoNotConsumeRetryBudget)
{
    auto config = Config();
    auto in = Frame();
    in.WaitingPath = true;
    NavRecovery recovery;
    for (int i = 0; i < 100; ++i) CHECK(recovery.Observe(config, in) == NavRecovery::Action::None);
    CHECK(recovery.GetRetries() == 0 && recovery.GetPhase() == NavRecovery::Phase::WaitingPath);
}
TEST(NavRecoveryPortalAndZeroSpeedArePaused)
{
    auto config = Config();
    auto in = Frame();
    NavRecovery recovery;
    in.Traversing = true;
    for (int i = 0; i < 50; ++i) recovery.Observe(config, in);
    CHECK(recovery.GetPhase() == NavRecovery::Phase::Paused && recovery.GetRetries() == 0);
    in.Traversing = false;
    in.CanMove = false;
    for (int i = 0; i < 50; ++i) recovery.Observe(config, in);
    CHECK(recovery.GetRetries() == 0 && !recovery.IsExhausted());
}
TEST(NavRecoveryCooldownDoesNotCreatePerFrameRetryLoop)
{
    auto config = Config();
    config.CooldownSeconds = 1;
    auto in = Frame();
    NavRecovery recovery;
    for (int i = 0; i < 3; ++i) recovery.Observe(config, in);
    CHECK(recovery.GetRetries() == 1);
    for (int i = 0; i < 8; ++i) CHECK(recovery.Observe(config, in) == NavRecovery::Action::None);
    CHECK(recovery.GetRetries() == 1 && recovery.GetCooldown() > 0);
}
TEST(NavRecoveryResetAndArrivalKeepExplicitState)
{
    auto config = Config();
    auto in = Frame();
    NavRecovery recovery;
    for (int i = 0; i < 50; ++i) recovery.Observe(config, in);
    recovery.Reset();
    CHECK(!recovery.IsExhausted() && recovery.GetRetries() == 0);
    in.Arrived = true;
    recovery.Observe(config, in);
    CHECK(recovery.GetPhase() == NavRecovery::Phase::Arrived);
    in.HasIntent = false;
    recovery.Observe(config, in);
    CHECK(recovery.GetPhase() == NavRecovery::Phase::Idle);
}
TEST(NavRecoveryInvalidInputStopsAndDisabledIsHarmless)
{
    auto config = Config();
    auto in = Frame();
    NavRecovery recovery;
    in.Position.x = NAN;
    CHECK(recovery.Observe(config, in) == NavRecovery::Action::Stop && recovery.GetPhase() == NavRecovery::Phase::Invalid);
    config.Enabled = false;
    CHECK(recovery.Observe(config, in) == NavRecovery::Action::None && !recovery.IsExhausted());
    config = Config();
    config.MaxRetries = 0;
    in = Frame();
    CHECK(!config.Validate());
    CHECK(recovery.Observe(config, in) == NavRecovery::Action::Stop);
}
TEST(NavRecoveryDeltaIsClampedAndZeroDoesNotAdvanceTimers)
{
    auto config = Config();
    auto in = Frame();
    NavRecovery recovery;
    in.Delta = 0;
    for (int i = 0; i < 50; ++i) recovery.Observe(config, in);
    CHECK(recovery.GetRetries() == 0);
    in.Delta = 100;
    recovery.Observe(config, in);
    CHECK(recovery.GetRetries() == 0);
    recovery.Observe(config, in);
    CHECK(recovery.GetRetries() == 1);
}
TEST(NavRecoveryWaitingDoesNotResetExistingAttemptCount)
{
    auto config = Config();
    auto in = Frame();
    NavRecovery recovery;
    for (int i = 0; i < 3; ++i) recovery.Observe(config, in);
    CHECK(recovery.GetRetries() == 1);
    in.WaitingPath = true;
    for (int i = 0; i < 20; ++i) recovery.Observe(config, in);
    in.WaitingPath = false;
    recovery.Observe(config, in);
    CHECK(recovery.GetRetries() == 1);
    for (int i = 0; i < 30; ++i) recovery.Observe(config, in);
    CHECK(recovery.IsExhausted() && recovery.GetRetries() == 2);
}
