#include "TestAssertions.h"
#include <Zamin/Gameplay/EffectModel.h>
#include <cmath>
#include <map>
#include <vector>
using namespace Zamin::Gameplay;
namespace {
    EffectSpec Spec(const std::string& key, StackRule rule = StackRule::Refresh, int stacks = 1)
    {
        EffectSpec s;
        s.Key = key;
        s.Rule = rule;
        s.MaxStacks = stacks;
        return s;
    }
    EffectSpec Dot(const std::string& key, double duration, double period, double amount, StackRule rule = StackRule::Refresh, int stacks = 1)
    {
        auto s = Spec(key, rule, stacks);
        s.Duration = duration;
        s.Period = period;
        s.Tick = TickKind::Damage;
        s.TickAmount = amount;
        return s;
    }
    EffectApplication From(uint64_t source, double magnitude = -1)
    {
        EffectApplication a;
        a.Source = source;
        a.Team = 2;
        a.Magnitude = magnitude;
        return a;
    }
    struct Stepper
    {
        std::map<std::string, EffectSpec> Specs;
        EffectBook Book;
        std::vector<EffectTick> Ticks;
        std::vector<ExpiredEffect> Expired;
        void Add(const EffectSpec& s) { Specs[s.Key] = s; }
        ApplyResult Apply(const std::string& key, EffectApplication a = From(7)) { return ApplyEffect(Book, Specs.at(key), a); }
        void Step(double h, int steps = 1)
        {
            for (int i = 0; i < steps; ++i) TickEffects(Book, Specs, h, Ticks, Expired);
        }
    };
}
TEST(EffectModelSpecValidationBounds)
{
    std::string e;
    CHECK(ValidateEffectSpec(Spec("Burn"), e) && e.empty());
    CHECK(ValidateEffectSpec(Dot("Poison", 3, 1, 2, StackRule::Independent, 3), e));
    auto bad = [&](auto change) {
        auto copy = Dot("Poison", 3, 1, 2);
        change(copy);
        return !ValidateEffectSpec(copy, e) && !e.empty();
    };
    CHECK(bad([](EffectSpec& x) { x.Key = "no spaces"; }));
    CHECK(bad([](EffectSpec& x) { x.Duration = .04; }));
    CHECK(bad([](EffectSpec& x) { x.Duration = 601; }));
    CHECK(bad([](EffectSpec& x) { x.Period = .04; }));
    CHECK(bad([](EffectSpec& x) { x.Period = 4; }));          // longer than the duration
    CHECK(bad([](EffectSpec& x) { x.Period = NAN; }));
    CHECK(bad([](EffectSpec& x) { x.TickAmount = 100001; }));
    CHECK(bad([](EffectSpec& x) { x.Tick = TickKind::None; })); // a period without a tick kind
    CHECK(bad([](EffectSpec& x) { x.Period = 0; }));            // a tick kind without a period
    CHECK(bad([](EffectSpec& x) { x.Add[2] = 10001; }));
    CHECK(bad([](EffectSpec& x) { x.Add[0] = INFINITY; }));
    CHECK(bad([](EffectSpec& x) { x.Speed = 4.5; }));
    CHECK(bad([](EffectSpec& x) { x.Speed = -.1; }));
    CHECK(bad([](EffectSpec& x) { x.MaxStacks = 17; }));
    CHECK(bad([](EffectSpec& x) { x.MaxStacks = 0; }));
    CHECK(bad([](EffectSpec& x) { x.Rule = StackRule(4); }));
    CHECK(bad([](EffectSpec& x) { x.Tick = TickKind(4); }));
    CHECK(bad([](EffectSpec& x) { x.Hostile = false; }));       // damage ticks must be hostile
    CHECK(bad([](EffectSpec& x) {
        x.Rule = StackRule::HighestWins;
        x.MaxStacks = 2;
    }));
    auto regen = Dot("Regen", 3, .5, 4);
    regen.Tick = TickKind::Heal;
    regen.Hostile = false;
    CHECK(ValidateEffectSpec(regen, e));
    regen.Hostile = true; // heal ticks must be beneficial
    CHECK(!ValidateEffectSpec(regen, e));
    auto stun = Spec("Shock", StackRule::HighestWins);
    stun.Stun = true;
    CHECK(ValidateEffectSpec(stun, e));
    stun.Hostile = false;
    CHECK(!ValidateEffectSpec(stun, e));
    auto none = Spec("Mark");
    none.TickAmount = 1; // magnitude without ticks
    CHECK(!ValidateEffectSpec(none, e));
    CHECK(EffectDurationCap(Spec("Frost", StackRule::Extend, 3)) == 9 && EffectDurationCap(Spec("Burn", StackRule::Refresh, 3)) == 3);
}
TEST(EffectModelRefreshStacksResetTimerAndKeepTickPhase)
{
    Stepper s;
    s.Add(Dot("Burn", 2, .5, 3, StackRule::Refresh, 3));
    auto first = s.Apply("Burn");
    CHECK(first.Outcome == ApplyOutcome::Added && first.Stacks == 1 && first.Remaining == 2);
    s.Step(.05, 8); // 0.4 s: the first tick is 0.1 s away
    CHECK(s.Ticks.empty());
    auto second = s.Apply("Burn", From(8, 5));
    CHECK(second.Outcome == ApplyOutcome::Stacked && second.Stacks == 2 && second.Remaining == 2);
    const auto& i = s.Book.Instances[0];
    CHECK_NEAR(i.UntilTick, .1, 1e-9); // the tick phase is kept: a refresh cannot postpone a tick
    CHECK(i.Magnitude == 5 && i.Source == 8 && s.Book.Instances.size() == 1);
    s.Step(.05, 2);
    CHECK(s.Ticks.size() == 1 && s.Ticks[0].Amount == 10 && s.Ticks[0].Source == 8); // 5 per stack x 2 stacks
    CHECK(s.Apply("Burn", From(7, 1)).Outcome == ApplyOutcome::Stacked);
    auto capped = s.Apply("Burn", From(7, 1));
    CHECK(capped.Outcome == ApplyOutcome::Refreshed && capped.Stacks == 3);
    CHECK(s.Book.Instances[0].Magnitude == 5); // max(old, new)
    // Without further refreshes: 2 s left at 0.5 s period = 4 more ticks, then expiry.
    s.Ticks.clear();
    s.Step(.05, 40);
    CHECK(s.Ticks.size() == 4 && s.Expired.size() == 1 && s.Book.Instances.empty());
    CHECK(s.Expired[0].Spec->Key == "Burn" && s.Expired[0].Source == 7);
    for (const auto& t : s.Ticks) CHECK(t.Amount == 15 && t.Kind == TickKind::Damage);
}
TEST(EffectModelExtendAddsDurationUpToCap)
{
    Stepper s;
    auto frost = Spec("Frost", StackRule::Extend, 2);
    frost.Speed = .5;
    s.Add(frost);
    CHECK(s.Apply("Frost").Remaining == 3);
    s.Step(.5);
    auto extended = s.Apply("Frost");
    CHECK(extended.Outcome == ApplyOutcome::Extended && extended.Stacks == 1 && extended.Remaining == 5.5);
    CHECK(s.Apply("Frost").Remaining == 6); // capped at Duration x MaxStacks
    CHECK(s.Book.Instances.size() == 1 && s.Book.Instances[0].Stacks == 1);
    s.Step(.5, 11);
    CHECK(s.Book.Instances.size() == 1);
    s.Step(.5);
    CHECK(s.Book.Instances.empty() && s.Expired.size() == 1);
}
TEST(EffectModelIndependentEvictsLeastRemainingThenOldest)
{
    Stepper s;
    s.Add(Dot("Poison", 3, 1, 2, StackRule::Independent, 3));
    CHECK(s.Apply("Poison", From(1)).Stacks == 1);
    s.Step(.5);
    CHECK(s.Apply("Poison", From(2)).Stacks == 2);
    s.Step(.5);
    auto third = s.Apply("Poison", From(3));
    CHECK(third.Outcome == ApplyOutcome::Added && third.Stacks == 3 && s.Book.Instances.size() == 3);
    // Full: the instance with the least time left (the first, 2 s) is replaced; the new one is newest.
    auto fourth = s.Apply("Poison", From(4));
    CHECK(fourth.Outcome == ApplyOutcome::Replaced && fourth.Stacks == 3 && s.Book.Instances.size() == 3);
    CHECK(s.Book.Instances[0].Source == 2 && s.Book.Instances[1].Source == 3 && s.Book.Instances[2].Source == 4);
    CHECK(s.Book.Instances[0].Sequence < s.Book.Instances[1].Sequence && s.Book.Instances[1].Sequence < s.Book.Instances[2].Sequence);
    // Equal remaining: the older application (lower Sequence) goes first.
    Stepper t;
    t.Add(Dot("Poison", 3, 1, 2, StackRule::Independent, 2));
    t.Apply("Poison", From(1));
    t.Apply("Poison", From(2));
    t.Apply("Poison", From(3));
    CHECK(t.Book.Instances[0].Source == 2 && t.Book.Instances[1].Source == 3);
    // Each instance keeps its own timer and tick phase: 2 instances x 3 ticks of 2.
    t.Step(1.0 / 60, 180);
    CHECK(t.Ticks.size() == 6 && t.Book.Instances.empty() && t.Expired.size() == 2);
}
TEST(EffectModelHighestWinsStrongerReplacesEqualExtendsWeakerRejected)
{
    Stepper s;
    auto shred = Spec("Shred", StackRule::HighestWins);
    shred.Add[1] = -20;
    s.Add(shred);
    CHECK(s.Apply("Shred", From(1, 5)).Outcome == ApplyOutcome::Added);
    s.Step(1);
    auto equal = s.Apply("Shred", From(2, 5));
    CHECK(equal.Outcome == ApplyOutcome::Refreshed && equal.Remaining == 3 && s.Book.Instances[0].Source == 2);
    s.Step(1);
    auto weaker = s.Apply("Shred", From(3, 4));
    CHECK(weaker.Outcome == ApplyOutcome::Rejected && weaker.Reason == "Stronger effect active");
    CHECK(s.Book.Instances[0].Remaining == 2 && s.Book.Instances[0].Source == 2); // unchanged
    const auto sequence = s.Book.Instances[0].Sequence;
    auto stronger = s.Apply("Shred", From(4, 9));
    CHECK(stronger.Outcome == ApplyOutcome::Replaced && stronger.Remaining == 3);
    CHECK(s.Book.Instances.size() == 1 && s.Book.Instances[0].Magnitude == 9 && s.Book.Instances[0].Sequence > sequence);
    // An equal re-application never shortens a longer remaining time.
    s.Book.Instances[0].Remaining = 2.5;
    CHECK(s.Apply("Shred", From(4, 9)).Remaining == 3);
}
TEST(EffectModelBookCapacityRejectsWithoutEvictingOtherKeys)
{
    Stepper s;
    for (int i = 0; i < 16; ++i) s.Add(Spec("E" + std::to_string(i)));
    s.Add(Spec("Late"));
    for (int i = 0; i < 16; ++i) CHECK(s.Apply("E" + std::to_string(i)).Outcome == ApplyOutcome::Added);
    const auto before = s.Book.Instances;
    auto late = s.Apply("Late");
    CHECK(late.Outcome == ApplyOutcome::Rejected && late.Reason == "Effect limit 16");
    CHECK(s.Book.Instances == before && s.Book.NextSequence == 17); // transactional
    CHECK(s.Apply("E3").Outcome == ApplyOutcome::Refreshed);         // existing keys still refresh
    auto bad = From(1, 100001);
    CHECK(s.Apply("E4", bad).Reason == "Effect magnitude 0..100000");
    bad.Magnitude = NAN;
    CHECK(s.Apply("E4", bad).Outcome == ApplyOutcome::Rejected);
    // A full Independent key replaces only its own instances.
    Stepper t;
    t.Add(Dot("Poison", 3, 1, 2, StackRule::Independent, 16));
    for (int i = 0; i < 16; ++i) t.Apply("Poison");
    auto more = t.Apply("Poison");
    CHECK(more.Outcome == ApplyOutcome::Replaced && t.Book.Instances.size() == 16);
}
TEST(EffectModelExactTickCountsAcrossStepRates)
{
    struct Case
    {
        double Duration, Period;
        size_t Ticks;
    };
    const Case cases[] = { { 3, 1, 3 }, { 2, .5, 4 }, { 1, .3, 3 }, { 2.2, 1, 2 }, { .05, .05, 1 }, { 600, 60, 10 } };
    for (int rate : { 20, 30, 50, 60, 120, 144, 240 })
        for (const auto& c : cases)
        {
            Stepper s;
            s.Add(Dot("Dot", c.Duration, c.Period, 1));
            s.Apply("Dot");
            const double h = 1.0 / double(rate);
            int steps = 0;
            while (!s.Book.Instances.empty() && steps < 200000)
            {
                s.Step(h);
                ++steps;
            }
            CHECK(s.Ticks.size() == c.Ticks);
            // Expiry rounds up to whole steps.
            CHECK(steps == int(std::ceil(c.Duration * rate - 1e-6)));
        }
}
TEST(EffectModelTickCapCarriesAndTicksBeforeExpiry)
{
    Stepper s;
    s.Add(Dot("Fast", 10, .1, 1));
    s.Apply("Fast");
    s.Step(1); // 10 ticks due, at most 4 per step: the rest are carried
    CHECK(s.Ticks.size() == size_t(MaxTicksPerStep) && s.Book.Instances[0].UntilTick < 0);
    s.Step(.001);
    CHECK(s.Ticks.size() == 8);
    // A tick due on the expiry step is still delivered, before the instance is removed.
    Stepper t;
    t.Add(Dot("Edge", 1, 1, 2));
    t.Apply("Edge");
    t.Step(.5, 2);
    CHECK(t.Ticks.size() == 1 && t.Expired.size() == 1 && t.Book.Instances.empty());
    // An instance whose definition is gone expires without a spec.
    Stepper u;
    u.Add(Spec("Gone"));
    u.Apply("Gone");
    u.Specs.clear();
    u.Step(.01);
    CHECK(u.Book.Instances.empty() && u.Expired.size() == 1 && u.Expired[0].Spec == nullptr);
}
TEST(EffectModelComposeModifiersSlowHasteStunAndStacks)
{
    Stepper s;
    auto burn = Dot("Burn", 2, .5, 3, StackRule::Refresh, 3);
    burn.Add = { -5, 0, 0, 0, 0 };
    auto frost = Spec("Frost", StackRule::Extend, 2);
    frost.Speed = .5;
    auto chill = Spec("Chill");
    chill.Speed = .8;
    auto haste = Spec("Haste");
    haste.Hostile = false;
    haste.Speed = 1.5;
    haste.Add = { 10, 0, 0, 25, 0 };
    auto shock = Spec("Shock", StackRule::HighestWins);
    shock.Stun = true;
    for (const auto& e : { burn, frost, chill, haste, shock }) s.Add(e);
    CHECK(ComposeModifiers(s.Book, s.Specs) == EffectModifiers{});
    s.Apply("Burn");
    s.Apply("Burn");
    s.Apply("Frost");
    s.Apply("Chill");
    s.Apply("Haste");
    auto m = ComposeModifiers(s.Book, s.Specs);
    CHECK(m.Instances == 4 && !m.Stunned);
    CHECK(m.Add[0] == 0 && m.Add[3] == 25); // 2 burn stacks x -5 + haste +10
    CHECK(m.Speed == .75);                    // strongest slow (0.5) x strongest haste (1.5)
    s.Apply("Shock");
    CHECK(ComposeModifiers(s.Book, s.Specs).Stunned);
    CHECK(RemoveEffects(s.Book, "Shock") == 1 && !ComposeModifiers(s.Book, s.Specs).Stunned);
    auto quad = Spec("Quad");
    quad.Hostile = false;
    quad.Speed = 4;
    s.Add(quad);
    s.Apply("Quad");
    CHECK(ComposeModifiers(s.Book, s.Specs).Speed == 2); // 0.5 x 4
    auto root = Spec("Root");
    root.Speed = 0;
    s.Add(root);
    s.Apply("Root");
    CHECK(ComposeModifiers(s.Book, s.Specs).Speed == 0);
}
TEST(EffectModelRemoveAndValidateBook)
{
    Stepper s;
    s.Add(Dot("Poison", 3, 1, 2, StackRule::Independent, 3));
    s.Add(Dot("Burn", 2, .5, 3, StackRule::Refresh, 3));
    auto frost = Spec("Frost", StackRule::Extend, 2);
    s.Add(frost);
    s.Apply("Poison");
    s.Apply("Poison");
    s.Apply("Burn");
    s.Apply("Burn");
    s.Apply("Frost");
    s.Apply("Frost");
    s.Step(1.0 / 60, 10);
    std::string e;
    CHECK(ValidateEffectBook(s.Book, s.Specs, e));
    auto broken = [&](auto change) {
        auto copy = s.Book;
        change(copy);
        return !ValidateEffectBook(copy, s.Specs, e) && !e.empty();
    };
    CHECK(broken([](EffectBook& b) { b.Instances[0].Key = "Unknown"; }));
    CHECK(broken([](EffectBook& b) { std::swap(b.Instances[0], b.Instances[1]); }));
    CHECK(broken([](EffectBook& b) { b.NextSequence = b.Instances.back().Sequence; }));
    CHECK(broken([](EffectBook& b) { b.Instances[2].Stacks = 4; }));           // Burn: MaxStacks 3
    CHECK(broken([](EffectBook& b) { b.Instances[0].Stacks = 2; }));           // Poison: not Refresh
    CHECK(broken([](EffectBook& b) { b.Instances[0].Remaining = 0; }));
    CHECK(broken([](EffectBook& b) { b.Instances[3].Remaining = 6.5; }));      // Frost cap 6
    CHECK(broken([](EffectBook& b) { b.Instances[0].UntilTick = 1.5; }));
    CHECK(broken([](EffectBook& b) { b.Instances[3].UntilTick = .1; }));       // no ticks: phase 0
    CHECK(broken([](EffectBook& b) { b.Instances[1].Magnitude = -1; }));
    CHECK(broken([](EffectBook& b) {
        auto extra = b.Instances[2];
        extra.Sequence = b.NextSequence++;
        b.Instances.push_back(extra); // a second Burn instance
    }));
    CHECK(broken([](EffectBook& b) {
        while (b.Instances.size() <= EffectBook::Capacity)
        {
            auto extra = b.Instances[0];
            extra.Sequence = b.NextSequence++;
            b.Instances.push_back(extra);
        }
    }));
    CHECK(RemoveEffects(s.Book, "Poison") == 2 && s.Book.Instances.size() == 2);
    CHECK(RemoveEffects(s.Book, "Poison") == 0);
    CHECK(RemoveEffects(s.Book, "") == 2 && s.Book.Instances.empty());
    CHECK(ValidateEffectBook(s.Book, s.Specs, e));
}
TEST(AreaModelValidationFalloffAndOrdering)
{
    std::string e;
    AreaSpec a;
    a.Key = "Blast";
    CHECK(ValidateAreaSpec(a, e));
    auto bad = [&](auto change) {
        auto copy = a;
        change(copy);
        return !ValidateAreaSpec(copy, e) && !e.empty();
    };
    CHECK(bad([](AreaSpec& x) { x.Key = ""; }));
    CHECK(bad([](AreaSpec& x) { x.Effect = "bad key"; }));
    CHECK(bad([](AreaSpec& x) { x.Kind = PayloadKind(3); }));
    CHECK(bad([](AreaSpec& x) { x.Shape = Falloff(2); }));
    CHECK(bad([](AreaSpec& x) { x.Radius = .04; }));
    CHECK(bad([](AreaSpec& x) { x.Radius = 51; }));
    CHECK(bad([](AreaSpec& x) { x.Amount = -1; }));
    CHECK(bad([](AreaSpec& x) { x.Amount = NAN; }));
    CHECK(bad([](AreaSpec& x) { x.EdgeScale = 1.5; }));
    CHECK(bad([](AreaSpec& x) { x.MaxTargets = 0; }));
    CHECK(bad([](AreaSpec& x) { x.MaxTargets = 33; }));
    CHECK(bad([](AreaSpec& x) { x.TargetOffset = { 0, 101, 0 }; }));
    CHECK(AreaScale(a, 0) == 1 && AreaScale(a, 3) == 1);
    a.Shape = Falloff::Linear;
    a.Radius = 4;
    a.EdgeScale = .5;
    CHECK(AreaScale(a, 0) == 1 && AreaScale(a, 2) == .75 && AreaScale(a, 4) == .5 && AreaScale(a, 9) == .5 && AreaScale(a, -1) == 1);
    a.EdgeScale = 0;
    CHECK(AreaScale(a, 4) == 0);
    std::vector<std::pair<double, uint64_t>> c{ { 2, 9 }, { 1, 5 }, { 2, 3 }, { 0, 7 }, { 3, 1 } };
    OrderAreaTargets(c, 3);
    CHECK(c.size() == 3 && c[0].second == 7 && c[1].second == 5 && c[2].second == 3); // ties by UUID
    OrderAreaTargets(c, 8);
    CHECK(c.size() == 3);
    OrderAreaTargets(c, -1);
    CHECK(c.empty());
}
