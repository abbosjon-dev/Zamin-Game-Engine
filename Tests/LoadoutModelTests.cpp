#include "TestAssertions.h"
#include <Zamin/Gameplay/LoadoutModel.h>
#include <cmath>
using namespace Zamin::Gameplay;
TEST(LoadoutModelValidatesStatsAndDefinitions)
{
    std::string e;
    CHECK(ValidateCombatStats(DefaultCombatStats, false, e));
    auto s = DefaultCombatStats;
    s[0] = NAN;
    CHECK(!ValidateCombatStats(s, false, e));
    s = DefaultCombatStats;
    s[3] = 0;
    CHECK(!ValidateCombatStats(s, false, e));
    s[3] = 100;
    s[1] = -1;
    CHECK(!ValidateCombatStats(s, false, e));
    CHECK(ValidateCombatStats(s, true, e));
    CHECK(!ValidateItemSpec({ "bad/key" }, e));
    CHECK(!ValidateItemSpec({ "Sword", EquipSlot(4) }, e));
    AbilitySpec a;
    a.Key = "Bolt";
    CHECK(ValidateAbilitySpec(a, e));
    a.Amount = INFINITY;
    CHECK(!ValidateAbilitySpec(a, e));
    a.Amount = 20;
    a.Cost = -1;
    CHECK(!ValidateAbilitySpec(a, e));
    a.Cost = 0;
    a.Cooldown = 301;
    CHECK(!ValidateAbilitySpec(a, e));
    a.Cooldown = 1;
    a.Count = 0;
    CHECK(!ValidateAbilitySpec(a, e));
}
TEST(LoadoutModelEquipmentMovesOwnershipWithoutDuplication)
{
    std::string e;
    Bag b;
    b.Items = { { "Sword", 1 }, { "Axe", 1 } };
    LoadoutState s;
    std::map<std::string, ItemSpec> items{ { "Sword", { "Sword", EquipSlot::Weapon, { 10, 0, 0, 0, 0 } } }, { "Axe", { "Axe", EquipSlot::Weapon, { 20, 0, 0, 0, 0 } } } };
    CHECK(EquipItem(DefaultCombatStats, items, b, s, "Sword", e));
    CHECK(b.Count("Sword") == 0);
    CHECK(EffectiveStats(DefaultCombatStats, items, s)[0] == 10);
    CHECK(!EquipItem(DefaultCombatStats, items, b, s, "Sword", e));
    CHECK(EquipItem(DefaultCombatStats, items, b, s, "Axe", e));
    CHECK(b.Count("Sword") == 1 && b.Count("Axe") == 0);
    CHECK(EffectiveStats(DefaultCombatStats, items, s)[0] == 20);
    CHECK(UnequipItem(DefaultCombatStats, items, b, s, EquipSlot::Weapon, e));
    CHECK(b.Count("Sword") == 1 && b.Count("Axe") == 1);
    CHECK(s.Equipped.empty());
}
TEST(LoadoutModelFullBagUnequipAndSwapRollback)
{
    std::string e;
    Bag b;
    b.Slots = 1;
    b.Items = { { "Sword", 1 } };
    LoadoutState s;
    std::map<std::string, ItemSpec> items{ { "Sword", { "Sword" } }, { "Axe", { "Axe" } } };
    CHECK(EquipItem(DefaultCombatStats, items, b, s, "Sword", e));
    b.Items = { { "Axe", 2 } };
    CHECK(!EquipItem(DefaultCombatStats, items, b, s, "Axe", e));
    CHECK(s.Equipped.at(EquipSlot::Weapon) == "Sword" && b.Count("Axe") == 2 && b.Count("Sword") == 0);
    CHECK(!UnequipItem(DefaultCombatStats, items, b, s, EquipSlot::Weapon, e));
    CHECK(s.Equipped.size() == 1);
    b.Items["Axe"] = 1;
    CHECK(EquipItem(DefaultCombatStats, items, b, s, "Axe", e));
    CHECK(b.Count("Sword") == 1);
}
TEST(LoadoutModelClampBonusesAndEnergyOnRemoval)
{
    std::string e;
    Bag b;
    b.Items = { { "Charm", 1 } };
    LoadoutState s;
    std::map<std::string, ItemSpec> items{ { "Charm", { "Charm", EquipSlot::Trinket, { -20, 50, 10, 100, -10 } } } };
    CHECK(EquipItem(DefaultCombatStats, items, b, s, "Charm", e));
    auto stats = EffectiveStats(DefaultCombatStats, items, s);
    CHECK(stats[0] == 0 && stats[1] == 50 && stats[3] == 200 && stats[4] == 0);
    s.Energy = 190;
    CHECK(UnequipItem(DefaultCombatStats, items, b, s, EquipSlot::Trinket, e));
    CHECK(s.Energy == 100);
}
TEST(LoadoutModelTickRegenerationCooldownAndInvalidDelta)
{
    LoadoutState s;
    s.Energy = 10;
    s.Abilities["Bolt"] = { 3, 4, true };
    TickLoadoutState(DefaultCombatStats, {}, s, 2);
    CHECK(s.Energy == 20 && s.Abilities["Bolt"].Remaining == 1);
    TickLoadoutState(DefaultCombatStats, {}, s, 100);
    CHECK(s.Energy == 100 && s.Abilities["Bolt"].Remaining == 0);
    bool threw = false;
    try
    {
        TickLoadoutState(DefaultCombatStats, {}, s, NAN);
    }
    catch (...)
    {
        threw = true;
    }
    CHECK(threw && s.Energy == 100);
}
TEST(LoadoutModelRestoreRejectsDefinitionAndProgressMismatch)
{
    std::string e;
    LoadoutState s;
    std::map<std::string, AbilitySpec> a{ { "Bolt", { "Bolt" } } };
    CHECK(!ValidateLoadout(DefaultCombatStats, {}, a, s, e));
    s.Abilities["Bolt"] = { 0, 0, true };
    CHECK(ValidateLoadout(DefaultCombatStats, {}, a, s, e));
    s.Abilities["Bolt"].Remaining = 2;
    CHECK(!ValidateLoadout(DefaultCombatStats, {}, a, s, e));
    s.Abilities["Bolt"].Remaining = 0;
    s.Energy = 101;
    CHECK(!ValidateLoadout(DefaultCombatStats, {}, a, s, e));
    s.Energy = 100;
    s.Equipped[EquipSlot::Weapon] = "Ghost";
    CHECK(!ValidateLoadout(DefaultCombatStats, {}, a, s, e));
}
