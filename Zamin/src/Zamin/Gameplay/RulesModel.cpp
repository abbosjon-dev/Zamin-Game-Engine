#include "RulesModel.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace Zamin::Gameplay {
    namespace {
        bool Range(double v, double low, double high) { return std::isfinite(v) && v >= low && v <= high; }
    }
    bool ConfigureHealth(HealthState& state, const HealthSettings& settings, std::string& error)
    {
        error.clear();
        if (!Range(settings.Maximum, .001, 1e6) || !Range(settings.Shield, 0, 1e6) || !Range(settings.HitGrace, 0, 60) || !Range(settings.RespawnDelay, 0, 60))
        {
            error = "Health settings outside supported range";
            return false;
        }
        state = {};
        state.Settings = settings;
        state.Current = settings.Maximum;
        state.Shield = settings.Shield;
        return true;
    }
    bool DamageHealth(HealthState& state, double amount, double now, bool bypassShield, bool bypassGrace, DamageResult& result, std::string& error)
    {
        error.clear();
        if (!Range(amount, 0, 1e6) || !Range(now, 0, 1e15))
        {
            error = "Invalid damage amount/time";
            return false;
        }
        DamageResult next;
        if (state.Dead) next.Reason = "Dead";
        else if (!bypassGrace && now < state.ImmuneUntil) next.Reason = "Invulnerable";
        else if (amount == 0) next.Reason = "Zero damage";
        else
        {
            next.Shield = bypassShield ? 0 : std::min(amount, state.Shield);
            next.Health = std::min(amount - next.Shield, state.Current);
            next.Applied = next.Shield + next.Health;
            state.Shield -= next.Shield;
            state.Current -= next.Health;
            state.ImmuneUntil = now + state.Settings.HitGrace;
            if (state.Current <= 0)
            {
                state.Dead = next.Killed = true;
                state.DiedAt = now;
                if (state.Deaths != UINT32_MAX) ++state.Deaths;
            }
        }
        result = std::move(next);
        return true;
    }
    double HealHealth(HealthState& state, double amount, bool shield)
    {
        if (state.Dead || !Range(amount, 0, 1e6)) return 0;
        auto& value = shield ? state.Shield : state.Current;
        const auto applied = std::min(amount, (shield ? state.Settings.Shield : state.Settings.Maximum) - value);
        value += applied;
        return applied;
    }
    bool ReviveHealth(HealthState& state, double now, double grace, std::string& error)
    {
        error.clear();
        if (!Range(now, 0, 1e15) || !Range(grace, 0, 60))
        {
            error = "Invalid respawn time/grace";
            return false;
        }
        if (!state.Dead)
        {
            error = "Actor is alive";
            return false;
        }
        if (now < state.DiedAt + state.Settings.RespawnDelay)
        {
            error = "Respawn delay";
            return false;
        }
        state.Current = state.Settings.Maximum;
        state.Shield = state.Settings.Shield;
        state.Dead = false;
        state.ImmuneUntil = now + grace;
        return true;
    }
    bool ValidItem(const std::string& key)
    {
        return !key.empty() && key.size() <= 64 && key.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-") == std::string::npos;
    }
    int Bag::Count(const std::string& item) const
    {
        auto i = Items.find(item);
        return i == Items.end() ? 0 : i->second;
    }
    bool Bag::Configure(int slots, int stack, bool reset, std::string& error)
    {
        error.clear();
        if (slots < 1 || slots > 64 || stack < 1 || stack > 999999)
        {
            error = "Inventory limits must be slots 1..64, stack 1..999999";
            return false;
        }
        if (!reset && (Items.size() > size_t(slots) || std::any_of(Items.begin(), Items.end(), [&](const auto& i) { return i.second > stack; })))
        {
            error = "Existing items exceed new inventory limits";
            return false;
        }
        if (reset) Items.clear();
        Slots = slots;
        Stack = stack;
        return true;
    }
    bool Bag::Exchange(const std::string& required, int requiredCount, bool consume, const std::string& reward, int rewardCount, std::string& error)
    {
        error.clear();
        if ((!required.empty() && (!ValidItem(required) || requiredCount < 1 || requiredCount > 999999)) ||
            (!reward.empty() && (!ValidItem(reward) || rewardCount < 1 || rewardCount > 999999)))
        {
            error = "Invalid item/count";
            return false;
        }
        if (!required.empty() && Count(required) < requiredCount)
        {
            error = "Missing item: " + required;
            return false;
        }
        auto next = Items;
        if (consume && !required.empty())
        {
            auto& count = next.at(required);
            count -= requiredCount;
            if (count == 0) next.erase(required);
        }
        if (!reward.empty())
        {
            auto at = next.find(reward);
            int count = at == next.end() ? 0 : at->second;
            if (rewardCount > Stack - count)
            {
                error = "Inventory stack full";
                return false;
            }
            if (at == next.end() && next.size() >= size_t(Slots))
            {
                error = "Inventory slots full";
                return false;
            }
            next[reward] = count + rewardCount;
        }
        Items = std::move(next);
        return true;
    }
}
