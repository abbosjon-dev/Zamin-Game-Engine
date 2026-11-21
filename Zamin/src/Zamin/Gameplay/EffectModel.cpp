#include "EffectModel.h"
#include <algorithm>
#include <cmath>
namespace Zamin::Gameplay {
    namespace {
        constexpr double Epsilon = 1e-9;
        bool Range(double v, double low, double high) { return std::isfinite(v) && v >= low && v <= high; }
        bool Finite(const glm::dvec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
        bool Fail(std::string& error, const char* message)
        {
            error = message;
            return false;
        }
        const EffectSpec* Find(const std::map<std::string, EffectSpec>& specs, const std::string& key)
        {
            auto found = specs.find(key);
            return found == specs.end() ? nullptr : &found->second;
        }
    }
    bool ValidateAreaSpec(const AreaSpec& s, std::string& error)
    {
        error.clear();
        if (!ValidItem(s.Key)) return Fail(error, "Area key must be 1..64 letters, digits, _ . -");
        if (!s.Effect.empty() && !ValidItem(s.Effect)) return Fail(error, "Invalid area effect key");
        if (int(s.Kind) < 0 || int(s.Kind) > 2) return Fail(error, "Area kind must be Damage, Heal or Shield");
        if (int(s.Shape) < 0 || int(s.Shape) > 1) return Fail(error, "Area falloff must be None or Linear");
        if (!Range(s.Radius, .05, 50) || !Range(s.Amount, 0, 1e6) || !Range(s.EdgeScale, 0, 1))
            return Fail(error, "Area radius 0.05..50, amount 0..1000000, edge scale 0..1");
        if (s.MaxTargets < 1 || s.MaxTargets > 32) return Fail(error, "Area max targets 1..32");
        if (!Finite(s.TargetOffset) || glm::length(s.TargetOffset) > 100) return Fail(error, "Area target offset must be within 100");
        return true;
    }
    double AreaScale(const AreaSpec& s, double distance)
    {
        if (s.Shape == Falloff::None || !(s.Radius > 0)) return 1;
        const double t = std::clamp(std::isfinite(distance) ? distance / s.Radius : 1.0, 0.0, 1.0);
        return 1 - (1 - s.EdgeScale) * t;
    }
    void OrderAreaTargets(std::vector<std::pair<double, uint64_t>>& candidates, int limit)
    {
        // (distance, UUID) is a total order: UUIDs are unique per scene.
        std::sort(candidates.begin(), candidates.end());
        if (limit < 0) limit = 0;
        if (candidates.size() > size_t(limit)) candidates.resize(size_t(limit));
    }
    bool ValidateEffectSpec(const EffectSpec& s, std::string& error)
    {
        error.clear();
        if (!ValidItem(s.Key)) return Fail(error, "Effect key must be 1..64 letters, digits, _ . -");
        if (!Range(s.Duration, .05, 600)) return Fail(error, "Effect duration 0.05..600 s");
        if (s.Period != 0 && !Range(s.Period, .05, std::min(60.0, s.Duration))) return Fail(error, "Effect period 0 or 0.05..min(60, duration) s");
        if (!Range(s.TickAmount, 0, 1e5)) return Fail(error, "Effect tick amount 0..100000");
        if (int(s.Tick) < 0 || int(s.Tick) > 3) return Fail(error, "Effect tick must be None, Damage, Heal or Shield");
        if ((s.Tick == TickKind::None) != (s.Period == 0) || (s.Tick == TickKind::None && s.TickAmount != 0))
            return Fail(error, "A ticking effect needs a tick kind and a period; a non-ticking one neither");
        std::string stats;
        if (!ValidateCombatStats(s.Add, true, stats)) return Fail(error, "Effect stat modifiers must be finite -10000..10000");
        if (!Range(s.Speed, 0, 4)) return Fail(error, "Effect speed scale 0..4");
        if (int(s.Rule) < 0 || int(s.Rule) > 3) return Fail(error, "Effect stacking must be Refresh, Extend, Independent or HighestWins");
        if (s.MaxStacks < 1 || s.MaxStacks > 16 || (s.Rule == StackRule::HighestWins && s.MaxStacks != 1)) return Fail(error, "Effect max stacks 1..16 (HighestWins: 1)");
        const bool support = s.Tick == TickKind::Heal || s.Tick == TickKind::Shield;
        if (s.Hostile ? support : (s.Tick == TickKind::Damage || s.Stun)) return Fail(error, "Damage ticks and stun require a hostile effect; heal/shield ticks a beneficial one");
        return true;
    }
    double EffectDurationCap(const EffectSpec& s) { return s.Duration * (s.Rule == StackRule::Extend ? s.MaxStacks : 1); }
    const char* ApplyOutcomeName(ApplyOutcome outcome)
    {
        switch (outcome)
        {
            case ApplyOutcome::Added: return "Added";
            case ApplyOutcome::Refreshed: return "Refreshed";
            case ApplyOutcome::Stacked: return "Stacked";
            case ApplyOutcome::Extended: return "Extended";
            case ApplyOutcome::Replaced: return "Replaced";
            case ApplyOutcome::Rejected: break;
        }
        return "Rejected";
    }
    ApplyResult ApplyEffect(EffectBook& book, const EffectSpec& spec, const EffectApplication& app)
    {
        ApplyResult out;
        auto reject = [&](const char* reason) {
            out.Outcome = ApplyOutcome::Rejected;
            out.Reason = reason;
            return out;
        };
        const double magnitude = app.Magnitude < 0 ? spec.TickAmount : app.Magnitude;
        if (!Range(magnitude, 0, 1e5)) return reject("Effect magnitude 0..100000");
        size_t count = 0, first = book.Instances.size();
        for (size_t i = 0; i < book.Instances.size(); ++i)
            if (book.Instances[i].Key == spec.Key && count++ == 0) first = i;
        auto fresh = [&] {
            EffectInstance i;
            i.Key = spec.Key;
            i.Source = app.Source;
            i.SourceHandle = app.SourceHandle;
            i.SourceTeam = app.Team;
            i.FriendlyFire = app.FriendlyFire;
            i.Stacks = 1;
            i.Remaining = spec.Duration;
            i.UntilTick = spec.Period;
            i.Magnitude = magnitude;
            i.Sequence = book.NextSequence++;
            return i;
        };
        auto adopt = [&](EffectInstance& i) {
            // The latest applier owns a re-applied instance (kill credit, team snapshot).
            i.Source = app.Source;
            i.SourceHandle = app.SourceHandle;
            i.SourceTeam = app.Team;
            i.FriendlyFire = app.FriendlyFire;
        };
        // Replacing keeps the book sorted by Sequence: the replacement is the newest application.
        auto replace = [&](size_t index) {
            book.Instances.erase(book.Instances.begin() + std::ptrdiff_t(index));
            book.Instances.push_back(fresh());
            out.Outcome = ApplyOutcome::Replaced;
            out.Remaining = spec.Duration;
        };
        if (count == 0 || (spec.Rule == StackRule::Independent && count < size_t(spec.MaxStacks)))
        {
            if (book.Instances.size() >= EffectBook::Capacity) return reject("Effect limit 16");
            book.Instances.push_back(fresh());
            out.Outcome = ApplyOutcome::Added;
            out.Stacks = spec.Rule == StackRule::Independent ? uint32_t(count + 1) : 1;
            out.Remaining = spec.Duration;
            return out;
        }
        auto& i = book.Instances[first];
        switch (spec.Rule)
        {
            case StackRule::Refresh:
            {
                const bool stacked = i.Stacks < uint32_t(spec.MaxStacks);
                if (stacked) ++i.Stacks;
                i.Remaining = spec.Duration; // the tick phase is kept: frequent refreshes cannot postpone ticks
                i.Magnitude = std::max(i.Magnitude, magnitude);
                adopt(i);
                out.Outcome = stacked ? ApplyOutcome::Stacked : ApplyOutcome::Refreshed;
                out.Stacks = i.Stacks;
                out.Remaining = i.Remaining;
                return out;
            }
            case StackRule::Extend:
                i.Remaining = std::min(EffectDurationCap(spec), i.Remaining + spec.Duration);
                i.Stacks = 1;
                i.Magnitude = std::max(i.Magnitude, magnitude);
                adopt(i);
                out.Outcome = ApplyOutcome::Extended;
                out.Stacks = 1;
                out.Remaining = i.Remaining;
                return out;
            case StackRule::Independent:
            {
                // Full: the instance with the least time left is replaced (ties: the older application).
                size_t victim = first;
                for (size_t n = first; n < book.Instances.size(); ++n)
                {
                    const auto& c = book.Instances[n];
                    if (c.Key == spec.Key && c.Remaining < book.Instances[victim].Remaining) victim = n;
                }
                replace(victim);
                out.Stacks = uint32_t(count);
                return out;
            }
            case StackRule::HighestWins:
                if (magnitude > i.Magnitude)
                {
                    replace(first);
                    out.Stacks = 1;
                    return out;
                }
                if (magnitude < i.Magnitude) return reject("Stronger effect active");
                i.Remaining = std::max(i.Remaining, spec.Duration);
                adopt(i);
                out.Outcome = ApplyOutcome::Refreshed;
                out.Stacks = 1;
                out.Remaining = i.Remaining;
                return out;
        }
        return reject("Invalid effect stacking");
    }
    void TickEffects(EffectBook& book, const std::map<std::string, EffectSpec>& specs, double h, std::vector<EffectTick>& ticks, std::vector<ExpiredEffect>& expired)
    {
        for (size_t n = 0; n < book.Instances.size();)
        {
            auto& i = book.Instances[n];
            const EffectSpec* spec = Find(specs, i.Key);
            if (!spec)
            {
                expired.push_back({ nullptr, i.Source });
                book.Instances.erase(book.Instances.begin() + std::ptrdiff_t(n));
                continue;
            }
            i.Remaining -= h;
            if (spec->Period > 0)
            {
                i.UntilTick -= h;
                const double amount = std::min(1e6, i.Magnitude * (spec->Rule == StackRule::Refresh ? double(i.Stacks) : 1.0));
                // The epsilon absorbs the float accumulation of steps such as 1/60 s, so a tick due exactly on a
                // step boundary fires on that step.
                for (int k = 0; k < MaxTicksPerStep && i.UntilTick <= Epsilon; ++k)
                {
                    ticks.push_back({ spec, spec->Tick, amount, i.Source, i.Sequence, i.SourceHandle, i.SourceTeam, i.FriendlyFire });
                    i.UntilTick += spec->Period;
                }
            }
            if (i.Remaining <= Epsilon)
            {
                expired.push_back({ spec, i.Source });
                book.Instances.erase(book.Instances.begin() + std::ptrdiff_t(n));
                continue;
            }
            ++n;
        }
    }
    EffectModifiers ComposeModifiers(const EffectBook& book, const std::map<std::string, EffectSpec>& specs)
    {
        EffectModifiers m;
        double slow = 1, haste = 1;
        for (const auto& i : book.Instances)
        {
            const EffectSpec* spec = Find(specs, i.Key);
            if (!spec) continue;
            ++m.Instances;
            const double k = spec->Rule == StackRule::Refresh ? double(i.Stacks) : 1.0;
            for (size_t n = 0; n < m.Add.size(); ++n) m.Add[n] += spec->Add[n] * k;
            if (spec->Speed < 1) slow = std::min(slow, spec->Speed);
            else haste = std::max(haste, spec->Speed);
            m.Stunned = m.Stunned || spec->Stun;
        }
        m.Speed = std::clamp(slow * haste, 0.0, 4.0);
        return m;
    }
    size_t RemoveEffects(EffectBook& book, const std::string& key)
    {
        const size_t before = book.Instances.size();
        if (key.empty()) book.Instances.clear();
        else std::erase_if(book.Instances, [&](const EffectInstance& i) { return i.Key == key; });
        return before - book.Instances.size();
    }
    bool ValidateEffectBook(const EffectBook& book, const std::map<std::string, EffectSpec>& specs, std::string& error)
    {
        error.clear();
        if (book.Instances.size() > EffectBook::Capacity) return Fail(error, "Effect book exceeds 16 instances");
        if (book.NextSequence == 0) return Fail(error, "Effect sequence counter invalid");
        uint64_t last = 0;
        for (size_t n = 0; n < book.Instances.size(); ++n)
        {
            const auto& i = book.Instances[n];
            const EffectSpec* spec = Find(specs, i.Key);
            if (!spec) return Fail(error, "Effect not defined");
            if (i.Sequence <= last || i.Sequence >= book.NextSequence) return Fail(error, "Effect sequence order invalid");
            last = i.Sequence;
            const bool refresh = spec->Rule == StackRule::Refresh;
            if (i.Stacks < 1 || i.Stacks > (refresh ? uint32_t(spec->MaxStacks) : 1u)) return Fail(error, "Effect stacks out of range");
            if (!(std::isfinite(i.Remaining) && i.Remaining > 0 && i.Remaining <= EffectDurationCap(*spec) + Epsilon)) return Fail(error, "Effect remaining time out of range");
            if (spec->Period > 0 ? !(std::isfinite(i.UntilTick) && i.UntilTick > 0 && i.UntilTick <= spec->Period + Epsilon) : i.UntilTick != 0)
                return Fail(error, "Effect tick phase out of range");
            if (!Range(i.Magnitude, 0, 1e5)) return Fail(error, "Effect magnitude out of range");
            size_t same = 0;
            for (const auto& other : book.Instances)
                if (other.Key == i.Key) ++same;
            if (same > (spec->Rule == StackRule::Independent ? size_t(spec->MaxStacks) : 1u)) return Fail(error, "Too many instances of one effect");
        }
        return true;
    }
}
