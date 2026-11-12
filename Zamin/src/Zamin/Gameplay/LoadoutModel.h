#pragma once
#include "RulesModel.h"
#include <array>
#include <set>
#include <vector>
namespace Zamin::Gameplay {
    // Attack, armor, spell power, maximum energy, energy regenerated per game second.
    using CombatStats = std::array<double, 5>;
    inline constexpr CombatStats DefaultCombatStats{ 0, 0, 0, 100, 5 };
    enum class EquipSlot
    {
        Weapon,
        Offhand,
        Body,
        Trinket
    };
    enum class AbilityKind
    {
        Damage,
        Heal,
        Shield
    };
    struct ItemSpec
    {
        std::string Key;
        EquipSlot Slot = EquipSlot::Weapon;
        CombatStats Modifiers{};
    };
    struct AbilitySpec
    {
        std::string Key, Ingredient;
        AbilityKind Kind = AbilityKind::Damage;
        double Amount = 20, Cost = 10, Cooldown = 1, Range = 5;
        int Count = 1;
        bool Unlocked = true;
    };
    struct AbilityProgress
    {
        double Remaining = 0;
        uint32_t Uses = 0;
        bool Unlocked = true;
    };
    struct LoadoutState
    {
        std::map<EquipSlot, std::string> Equipped;
        std::map<std::string, AbilityProgress> Abilities;
        double Energy = 100;
    };
    bool ValidateCombatStats(const CombatStats&, bool modifiers, std::string&);
    bool ValidateItemSpec(const ItemSpec&, std::string&);
    bool ValidateAbilitySpec(const AbilitySpec&, std::string&);
    CombatStats EffectiveStats(const CombatStats&, const std::map<std::string, ItemSpec>&, const LoadoutState&);
    bool ValidateLoadout(const CombatStats&, const std::map<std::string, ItemSpec>&, const std::map<std::string, AbilitySpec>&, const LoadoutState&, std::string&);
    bool EquipItem(const CombatStats&, const std::map<std::string, ItemSpec>&, Bag&, LoadoutState&, const std::string&, std::string&);
    bool UnequipItem(const CombatStats&, const std::map<std::string, ItemSpec>&, Bag&, LoadoutState&, EquipSlot, std::string&);
    void TickLoadoutState(const CombatStats&, const std::map<std::string, ItemSpec>&, LoadoutState&, double delta);
}
