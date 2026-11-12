#include "LoadoutModel.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace Zamin::Gameplay {
    bool ValidateCombatStats(const CombatStats& s, bool modifiers, std::string& error)
    {
        error.clear();
        for (auto x : s)
            if (!std::isfinite(x) || x > 10000 || x < (modifiers ? -10000 : 0))
            {
                error = "Stats require finite values within bounds";
                return false;
            }
        if (!modifiers && s[3] < 1)
        {
            error = "Maximum energy must be at least one";
            return false;
        }
        return true;
    }
    bool ValidateItemSpec(const ItemSpec& s, std::string& error)
    {
        if (!ValidateCombatStats(s.Modifiers, true, error)) return false;
        if (!ValidItem(s.Key) || int(s.Slot) < 0 || int(s.Slot) > 3)
        {
            error = "Invalid item key/slot";
            return false;
        }
        return true;
    }
    bool ValidateAbilitySpec(const AbilitySpec& s, std::string& error)
    {
        error.clear();
        auto range = [](double x, double a, double b) { return std::isfinite(x) && x >= a && x <= b; };
        if (!ValidItem(s.Key) || int(s.Kind) < 0 || int(s.Kind) > 2 || !range(s.Amount, .001, 100000) || !range(s.Cost, 0, 100000) || !range(s.Cooldown, 0, 300) || !range(s.Range, .001, 100) || (!s.Ingredient.empty() && !ValidItem(s.Ingredient)) || s.Count < 1 || s.Count > 999999)
        {
            error = "Invalid ability definition";
            return false;
        }
        return true;
    }
    CombatStats EffectiveStats(const CombatStats& base, const std::map<std::string, ItemSpec>& items, const LoadoutState& state)
    {
        auto out = base;
        for (auto& [slot, key] : state.Equipped)
        {
            (void)slot;
            auto it = items.find(key);
            if (it != items.end())
                for (size_t n = 0; n < 5; ++n) out[n] += it->second.Modifiers[n];
        }
        for (size_t n = 0; n < 5; ++n) out[n] = std::clamp(out[n], n == 3 ? 1. : 0., 100000.);
        return out;
    }
    bool ValidateLoadout(const CombatStats& base, const std::map<std::string, ItemSpec>& items, const std::map<std::string, AbilitySpec>& abilities, const LoadoutState& state, std::string& error)
    {
        if (!ValidateCombatStats(base, false, error)) return false;
        if (items.size() > 32 || abilities.size() > 16 || state.Equipped.size() > 4 || state.Abilities.size() != abilities.size())
        {
            error = "Loadout definition/state limits";
            return false;
        }
        for (auto& [key, s] : items)
            if (key != s.Key || !ValidateItemSpec(s, error)) return false;
        for (auto& [slot, key] : state.Equipped)
        {
            auto i = items.find(key);
            if (i == items.end() || i->second.Slot != slot)
            {
                error = "Equipped item/slot mismatch";
                return false;
            }
        }
        for (auto& [key, s] : abilities)
        {
            if (key != s.Key || !ValidateAbilitySpec(s, error)) return false;
            auto p = state.Abilities.find(key);
            if (p == state.Abilities.end() || !std::isfinite(p->second.Remaining) || p->second.Remaining < 0 || p->second.Remaining > s.Cooldown)
            {
                error = "Invalid ability cooldown/state";
                return false;
            }
        }
        if (!std::isfinite(state.Energy) || state.Energy < 0 || state.Energy > EffectiveStats(base, items, state)[3])
        {
            error = "Invalid saved energy";
            return false;
        }
        return true;
    }
    bool EquipItem(const CombatStats& base, const std::map<std::string, ItemSpec>& items, Bag& bag, LoadoutState& state, const std::string& key, std::string& error)
    {
        error.clear();
        auto i = items.find(key);
        if (i == items.end())
        {
            error = "Item not defined";
            return false;
        }
        auto p = state.Equipped.find(i->second.Slot);
        std::string old = p == state.Equipped.end() ? "" : p->second;
        if (old == key)
        {
            error = "Item already equipped";
            return false;
        }
        auto nextBag = bag;
        auto next = state;
        if (!nextBag.Exchange(key, 1, true, old, 1, error)) return false;
        next.Equipped[i->second.Slot] = key;
        next.Energy = std::min(next.Energy, EffectiveStats(base, items, next)[3]);
        bag = std::move(nextBag);
        state = std::move(next);
        return true;
    }
    bool UnequipItem(const CombatStats& base, const std::map<std::string, ItemSpec>& items, Bag& bag, LoadoutState& state, EquipSlot slot, std::string& error)
    {
        error.clear();
        auto i = state.Equipped.find(slot);
        if (i == state.Equipped.end())
        {
            error = "Equipment slot empty";
            return false;
        }
        auto nextBag = bag;
        auto next = state;
        if (!nextBag.Exchange("", 0, false, i->second, 1, error)) return false;
        next.Equipped.erase(slot);
        next.Energy = std::min(next.Energy, EffectiveStats(base, items, next)[3]);
        bag = std::move(nextBag);
        state = std::move(next);
        return true;
    }
    void TickLoadoutState(const CombatStats& base, const std::map<std::string, ItemSpec>& items, LoadoutState& state, double delta)
    {
        if (!std::isfinite(delta) || delta < 0 || delta > 1e6) throw std::runtime_error("Invalid loadout tick delta");
        auto stats = EffectiveStats(base, items, state);
        state.Energy = std::min(stats[3], state.Energy + stats[4] * delta);
        for (auto& [key, p] : state.Abilities)
        {
            (void)key;
            p.Remaining = std::max(0., p.Remaining - delta);
        }
    }
}
