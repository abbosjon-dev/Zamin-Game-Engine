#include "TestAssertions.h"
#include <Zamin/Gameplay/RulesModel.h>
#include <limits>
using namespace Zamin::Gameplay;
namespace {
    HealthState MakeHealth(double shield = 25)
    {
        HealthState h;
        std::string error;
        CHECK(ConfigureHealth(h, { 100, shield, .2, .5, 1 }, error));
        return h;
    }
    DamageResult Hit(HealthState& h, double amount, double now = 0, bool pierce = false, bool grace = false)
    {
        DamageResult r;
        std::string error;
        CHECK(DamageHealth(h, amount, now, pierce, grace, r, error));
        return r;
    }
}
TEST(RulesHealthShieldAbsorbsThenOverflowsIntoHealth)
{
    auto h = MakeHealth();
    auto r = Hit(h, 40);
    CHECK_NEAR(h.Shield, 0, 1e-9);
    CHECK_NEAR(h.Current, 85, 1e-9);
    CHECK(r.Shield == 25 && r.Health == 15 && r.Applied == 40 && !r.Killed);
}
TEST(RulesHealthPiercingLeavesShieldIntact)
{
    auto h = MakeHealth();
    auto r = Hit(h, 50, 0, true);
    CHECK(h.Shield == 25 && h.Current == 50 && r.Health == 50 && r.Shield == 0);
}
TEST(RulesHealthGraceExpiresAtExactBoundary)
{
    auto h = MakeHealth();
    Hit(h, 10);
    auto r = Hit(h, 10, .1);
    CHECK(r.Applied == 0 && r.Reason == "Invulnerable");
    CHECK(Hit(h, 10, .2).Applied == 10);
}
TEST(RulesHealthZeroHitDoesNotStartInvulnerability)
{
    auto h = MakeHealth();
    CHECK(Hit(h, 0).Applied == 0);
    CHECK(h.ImmuneUntil == 0);
    CHECK(Hit(h, 1).Applied == 1);
}
TEST(RulesHealthOverkillReportsOnlyActualLossAndDeathOnce)
{
    auto h = MakeHealth();
    auto r = Hit(h, 1000000);
    CHECK(r.Applied == 125 && r.Killed && h.Deaths == 1 && h.Dead);
    CHECK(Hit(h, 100, 1).Applied == 0);
    CHECK(h.Deaths == 1);
}
TEST(RulesHealthCannotHealDeadActor)
{
    auto h = MakeHealth();
    Hit(h, 125);
    CHECK(HealHealth(h, 100, false) == 0);
    CHECK(HealHealth(h, 100, true) == 0);
    CHECK(h.Dead && h.Current == 0 && h.Shield == 0);
}
TEST(RulesHealthHealingAndShieldRestorationAreCapped)
{
    auto h = MakeHealth();
    Hit(h, 75);
    CHECK(HealHealth(h, 1000, false) == 50);
    CHECK(h.Current == 100);
    CHECK(HealHealth(h, 1000, true) == 25);
    CHECK(h.Shield == 25);
}
TEST(RulesHealthInvalidInputsAreTransactional)
{
    auto h = MakeHealth();
    DamageResult r;
    r.Reason = "sentinel";
    std::string error;
    for (double v : { -1., std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN(), 1000001. })
    {
        CHECK(!DamageHealth(h, v, 0, false, false, r, error));
        CHECK(h.Current == 100 && h.Shield == 25 && r.Reason == "sentinel");
    }
    CHECK(!DamageHealth(h, 1, -1, false, false, r, error));
    CHECK(HealHealth(h, -1, false) == 0);
}
TEST(RulesHealthInvalidConfigurationPreservesState)
{
    auto h = MakeHealth();
    std::string error;
    CHECK(!ConfigureHealth(h, { 0, 0, 0, 0, 0 }, error));
    CHECK(h.Current == 100 && h.Shield == 25);
    CHECK(!ConfigureHealth(h, { 100, -1, 0, 0, 0 }, error));
    CHECK(!ConfigureHealth(h, { 100, 0, 61, 0, 0 }, error));
}
TEST(RulesHealthRespawnDelayAndGracePreserveDeathHistory)
{
    auto h = MakeHealth();
    Hit(h, 200);
    std::string error;
    CHECK(!ReviveHealth(h, .49, 1, error));
    CHECK(h.Dead);
    CHECK(ReviveHealth(h, .5, 1, error));
    CHECK(!h.Dead && h.Current == 100 && h.Shield == 25 && h.Deaths == 1);
    CHECK(Hit(h, 50, 1).Applied == 0);
    CHECK(Hit(h, 50, 1.5).Applied == 50);
}
TEST(RulesHealthInvalidOrAliveReviveDoesNotMutate)
{
    auto h = MakeHealth();
    std::string error;
    CHECK(!ReviveHealth(h, 0, 1, error));
    Hit(h, 200);
    CHECK(!ReviveHealth(h, 1, -1, error));
    CHECK(!ReviveHealth(h, std::numeric_limits<double>::infinity(), 1, error));
    CHECK(h.Dead && h.Current == 0);
}
TEST(RulesHealthExplicitGraceBypassStillRespectsShield)
{
    auto h = MakeHealth();
    Hit(h, 10);
    CHECK(Hit(h, 20, .01, false, true).Applied == 20);
    CHECK(h.Shield == 0 && h.Current == 95);
}
TEST(RulesBagRejectsUnsafeAndOversizedItemKeys)
{
    for (const auto& key : { "", "a/b", "../key", "a b", "a\\b" }) CHECK(!ValidItem(key));
    CHECK(!ValidItem(std::string(65, 'x')));
    CHECK(ValidItem("Quest.Key_01-red"));
}
TEST(RulesBagAddsAndConsumesWithoutZeroSlots)
{
    Bag b;
    std::string error;
    CHECK(b.Exchange("", 1, false, "Coin", 3, error));
    CHECK(b.Count("Coin") == 3);
    CHECK(b.Exchange("Coin", 3, true, "", 1, error));
    CHECK(b.Count("Coin") == 0 && b.Items.empty());
}
TEST(RulesBagMissingKeyCannotAwardReward)
{
    Bag b;
    std::string error;
    CHECK(!b.Exchange("Key", 1, true, "Gem", 1, error));
    CHECK(b.Items.empty());
}
TEST(RulesBagRewardOverflowDoesNotConsumeKey)
{
    Bag b;
    std::string error;
    CHECK(b.Configure(2, 2, false, error));
    CHECK(b.Exchange("", 1, false, "Key", 1, error));
    CHECK(b.Exchange("", 1, false, "Gem", 2, error));
    CHECK(!b.Exchange("Key", 1, true, "Gem", 1, error));
    CHECK(b.Count("Key") == 1 && b.Count("Gem") == 2);
}
TEST(RulesBagCanConsumeLastKeyToFreeRewardSlot)
{
    Bag b;
    std::string error;
    CHECK(b.Configure(1, 1, false, error));
    CHECK(b.Exchange("", 1, false, "Key", 1, error));
    CHECK(b.Exchange("Key", 1, true, "Gem", 1, error));
    CHECK(b.Items.size() == 1 && b.Count("Gem") == 1);
}
TEST(RulesBagNonConsumingRequirementAndSameItemExchange)
{
    Bag b;
    std::string error;
    CHECK(b.Exchange("", 1, false, "Coin", 5, error));
    CHECK(b.Exchange("Coin", 2, false, "Gem", 1, error));
    CHECK(b.Count("Coin") == 5);
    CHECK(b.Exchange("Coin", 3, true, "Coin", 1, error));
    CHECK(b.Count("Coin") == 3);
}
TEST(RulesBagShrinkingLimitsRejectsWithoutLoss)
{
    Bag b;
    std::string error;
    b.Items = { { "A", 5 }, { "B", 1 } };
    CHECK(!b.Configure(1, 10, false, error));
    CHECK(!b.Configure(2, 4, false, error));
    CHECK(b.Items.size() == 2 && b.Slots == 32 && b.Stack == 999);
    CHECK(b.Configure(1, 1, true, error));
    CHECK(b.Items.empty());
}
TEST(RulesBagBoundaryCountsAndInvalidConfigurationAreTransactional)
{
    Bag b;
    std::string error;
    CHECK(!b.Configure(65, 1, false, error));
    CHECK(!b.Configure(1, 0, false, error));
    CHECK(!b.Exchange("", 1, false, "Coin", 0, error));
    CHECK(b.Items.empty());
    CHECK(b.Configure(64, 999999, false, error));
    CHECK(b.Exchange("", 1, false, "Coin", 999999, error));
    CHECK(!b.Exchange("", 1, false, "Coin", 999999, error));
    CHECK(b.Count("Coin") == 999999);
}
