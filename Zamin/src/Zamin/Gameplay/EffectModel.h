#pragma once
#include "ProjectileModel.h"
#include "LoadoutModel.h"
#include <map>
#include <string>
#include <utility>
#include <vector>
namespace Zamin::Gameplay {
    // ---------------------------------------------------------------------------------------------- areas
    enum class Falloff
    {
        None,
        Linear
    };
    // Immutable, scene-shared instant area (explosion, nova, healing pulse). Targets are health actors whose aim
    // point (world position + TargetOffset) lies within Radius of the centre (XY distance in 2D).
    struct AreaSpec
    {
        std::string Key, Effect;                     // Effect: optional status applied to every affected target
        PayloadKind Kind = PayloadKind::Damage;
        double Radius = 3, Amount = 10, EdgeScale = .5; // 0.05..50 m, 0..1e6, 0..1 (Linear falloff at the edge)
        Falloff Shape = Falloff::None;
        int MaxTargets = 8;                          // 1..32
        glm::dvec3 TargetOffset{};                   // |.| <= 100
        uint16_t Mask = 0xFFFF;                      // line-of-sight blockers
        bool TwoD = false, LineOfSight = true, AffectOwner = false, FriendlyFire = false, PierceShield = false, IgnoreGrace = false;
        bool operator==(const AreaSpec&) const = default;
    };
    bool ValidateAreaSpec(const AreaSpec&, std::string& error);
    // None: 1. Linear: 1 at the centre down to EdgeScale at Radius (distance clamped to 0..Radius).
    double AreaScale(const AreaSpec&, double distance);
    // Candidates (distance, UUID): sorted by (distance, UUID) and truncated to `limit` (>= 0). Allocates nothing.
    void OrderAreaTargets(std::vector<std::pair<double, uint64_t>>&, int limit);
    // ---------------------------------------------------------------------------------------------- status effects
    enum class TickKind
    {
        None,
        Damage,
        Heal,
        Shield
    };
    enum class StackRule
    {
        Refresh,     // one instance; stacks add up to MaxStacks; timer resets, tick phase kept
        Extend,      // one instance; duration adds up to Duration * MaxStacks
        Independent, // up to MaxStacks instances with their own timers; full: the shortest one is replaced
        HighestWins  // one instance; stronger replaces, equal extends to Duration, weaker is rejected
    };
    // Immutable, scene-shared timed status effect.
    struct EffectSpec
    {
        std::string Key;
        double Duration = 3, Period = 0, TickAmount = 0; // 0.05..600 s; 0 or 0.05..min(60, Duration) s; 0..100000
        TickKind Tick = TickKind::None;
        CombatStats Add{};                               // additive loadout stat bonus, each -10000..10000
        double Speed = 1;                                // movement scale 0..4 (1 neutral, < 1 slow, > 1 haste)
        bool Stun = false, Hostile = true, PierceShield = false;
        StackRule Rule = StackRule::Refresh;
        int MaxStacks = 1;                               // 1..16 (HighestWins: 1)
        bool operator==(const EffectSpec&) const = default;
    };
    // Also enforces: Tick Damage or Stun => Hostile; Tick Heal/Shield => beneficial; Tick None <=> Period 0 and
    // TickAmount 0.
    bool ValidateEffectSpec(const EffectSpec&, std::string& error);
    struct EffectInstance
    {
        std::string Key;
        uint64_t Source = 0;         // applier UUID (0 = environment or gone)
        uint32_t SourceHandle = ~0u; // opaque to the model: the world's generational source check
        uint16_t SourceTeam = 0;     // applier team snapshot (team rule for damage ticks)
        bool FriendlyFire = false;   // applied by a friendly-fire payload: ticks ignore the team rule
        uint32_t Stacks = 1;         // Refresh: 1..MaxStacks; other rules: 1
        double Remaining = 0, UntilTick = 0, Magnitude = 0; // seconds; seconds to the next tick (0: no ticks); per tick
        uint64_t Sequence = 0;       // application order: stable ordering and eviction ties
        bool operator==(const EffectInstance&) const = default;
    };
    // One actor's active effects, sorted by Sequence.
    struct EffectBook
    {
        static constexpr size_t Capacity = 16;
        std::vector<EffectInstance> Instances;
        uint64_t NextSequence = 1;
    };
    struct EffectApplication
    {
        uint64_t Source = 0;
        uint32_t SourceHandle = ~0u;
        uint16_t Team = 0;
        bool FriendlyFire = false;
        double Magnitude = -1; // < 0: the spec's TickAmount
    };
    enum class ApplyOutcome
    {
        Added,
        Refreshed,
        Stacked,
        Extended,
        Replaced,
        Rejected
    };
    const char* ApplyOutcomeName(ApplyOutcome);
    struct ApplyResult
    {
        ApplyOutcome Outcome = ApplyOutcome::Rejected;
        uint32_t Stacks = 0;   // Refresh: the instance's stacks; Independent: instances of the key; otherwise 1
        double Remaining = 0;  // the applied instance's remaining time
        std::string Reason;
    };
    // Transactional: a rejected application leaves the book unchanged. Never evicts instances of other keys.
    ApplyResult ApplyEffect(EffectBook&, const EffectSpec&, const EffectApplication&);
    // Ticks and expiries reference the specs map passed to TickEffects (stable while definitions are immutable).
    struct EffectTick
    {
        const EffectSpec* Spec = nullptr;
        TickKind Kind = TickKind::None;
        double Amount = 0; // Magnitude (x Stacks for Refresh), at most 1000000
        uint64_t Source = 0, Sequence = 0;
        uint32_t SourceHandle = ~0u;
        uint16_t Team = 0;
        bool FriendlyFire = false;
    };
    struct ExpiredEffect
    {
        const EffectSpec* Spec = nullptr;
        uint64_t Source = 0;
    };
    inline constexpr int MaxTicksPerStep = 4;
    // One fixed step h: Remaining and UntilTick decrease by h; due ticks are emitted (at most 4 per instance per
    // step, the rest are carried); then instances with Remaining <= 1e-9 expire. A 3 s effect with a 1 s period
    // ticks exactly three times. Appends to the caller's vectors (reserve them to stay allocation-free). An
    // instance whose key has no spec is expired.
    void TickEffects(EffectBook&, const std::map<std::string, EffectSpec>&, double h, std::vector<EffectTick>& ticks, std::vector<ExpiredEffect>& expired);
    struct EffectModifiers
    {
        CombatStats Add{};
        double Speed = 1;
        bool Stunned = false;
        uint32_t Instances = 0;
        bool operator==(const EffectModifiers&) const = default;
    };
    // Add = sum of spec.Add (x Stacks for Refresh); Speed = (strongest slow) x (strongest haste), 0..4;
    // Stunned = any Stun instance.
    EffectModifiers ComposeModifiers(const EffectBook&, const std::map<std::string, EffectSpec>&);
    // Removes every instance of `key` ("" = all). Returns the number removed.
    size_t RemoveEffects(EffectBook&, const std::string& key);
    // Save/restore validation of a whole book against the current definitions.
    bool ValidateEffectBook(const EffectBook&, const std::map<std::string, EffectSpec>&, std::string& error);
    // The longest an instance of this spec can last (Extend: Duration x MaxStacks).
    double EffectDurationCap(const EffectSpec&);
}
