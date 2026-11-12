#include "TestAssertions.h"
#include <Zamin/Gameplay/LoadoutWorld.h>
#include <Zamin/Gameplay/SaveWorld.h>
#include <Zamin/Gameplay/QuestWorld.h>
#include <Zamin/Gameplay/GameplayRuntime.h>
#include <Zamin/Scene/Entity.h>
#include <cmath>
using namespace Zamin;
namespace {
    struct LFixture
    {
        Scene World;
        Entity Hero, Enemy;
        LFixture()
        {
            World.SetName("LoadoutTest");
            Hero = World.CreateEntityWithUUID(UUID(100), "Hero");
            Enemy = World.CreateEntityWithUUID(UUID(200), "Enemy");
            Enemy.GetTransform().Translation = { 2, 0, 0 };
            World.OnRuntimeStart();
            std::string e;
            CHECK(Rules().Configure(Hero, { 100, 50, 0, 0, 1 }, false, e));
            CHECK(Rules().Configure(Enemy, { 200, 0, 0, 0, 2 }, false, e));
            CHECK(Rules().ConfigureBag(Hero, 4, 99, false, e));
            CHECK(Kit().Configure(Hero, Gameplay::DefaultCombatStats, e));
            CHECK(Saves().Bind(Hero, "Player", e));
            CHECK(Saves().Bind(Enemy, "Enemy", e));
        }
        ~LFixture() { World.OnRuntimeStop(); }
        Gameplay::RuleWorld& Rules() { return Gameplay::RuleWorld::For(World); }
        Gameplay::LoadoutWorld& Kit() { return Gameplay::LoadoutWorld::For(World); }
        Gameplay::SaveWorld& Saves() { return Gameplay::SaveWorld::For(World); }
        void Item(std::string key = "Sword", Gameplay::EquipSlot slot = Gameplay::EquipSlot::Weapon, Gameplay::CombatStats bonus = { 20, 0, 10, 0, 0 })
        {
            std::string e;
            CHECK(Kit().DefineItem(Hero, { key, slot, bonus }, e));
            CHECK(Rules().ChangeItem(Hero, key, 1, false, e));
        }
        void Ability(std::string key = "Bolt", Gameplay::AbilityKind kind = Gameplay::AbilityKind::Damage, double cost = 10, double cooldown = 1, std::string ingredient = "")
        {
            std::string e;
            Gameplay::AbilitySpec a;
            a.Key = key;
            a.Kind = kind;
            a.Amount = 30;
            a.Cost = cost;
            a.Cooldown = cooldown;
            a.Ingredient = ingredient;
            CHECK(Kit().DefineAbility(Hero, a, e));
        }
        Gameplay::AbilityResult Cast(std::string key = "Bolt")
        {
            Gameplay::RuleQuery q;
            q.TwoD = true;
            q.Origin = {};
            q.LineOfSight = false;
            return Kit().Cast(Hero, Enemy, key, q);
        }
    };
    Visual::Value LCall(Gameplay::Runtime& r, Entity h, const std::string& kind, std::map<std::string, Visual::Value> args = {})
    {
        std::map<std::string, Visual::Value> a;
        for (auto& p : Visual::Catalog().at(kind).Inputs)
            if (p.Kind != Visual::Type::Exec) a[p.Name] = p.Default;
        for (auto& [k, v] : args) a[k] = v;
        return r.Dispatch(h, h, kind, a);
    }
}
TEST(LoadoutWorldEquipmentAppliesToRealCombatAndArmor)
{
    LFixture f;
    f.Item();
    std::string e;
    CHECK(f.Kit().Equip(f.Hero, "Sword", e));
    CHECK(f.Rules().Inventory(f.Hero).Count("Sword") == 0);
    auto hit = f.Rules().Damage(f.Enemy, f.Hero, 10, false, false, false);
    CHECK(hit.Applied == 30);
    CHECK(f.Kit().Configure(f.Enemy, { 0, 100, 0, 100, 0 }, e));
    hit = f.Rules().Damage(f.Enemy, f.Hero, 10, false, false, false);
    CHECK(hit.Applied == 15);
    CHECK(f.Kit().Unequip(f.Hero, Gameplay::EquipSlot::Weapon, e));
    CHECK(f.Rules().Inventory(f.Hero).Count("Sword") == 1);
}
TEST(LoadoutWorldAbilityChargesOnceAndCooldownDoesNotUseAttackClock)
{
    LFixture f;
    f.Ability();
    auto hit = f.Cast();
    CHECK(hit.Success && hit.Applied == 30);
    CHECK(f.Kit().State(f.Hero)->State.Energy == 90);
    CHECK(!f.Cast().Success);
    CHECK(f.Kit().State(f.Hero)->State.Abilities.at("Bolt").Uses == 1);
    f.Kit().Tick(.5);
    CHECK(!f.Cast().Success);
    f.Kit().Tick(.5);
    CHECK(f.Cast().Success);
    CHECK(f.Kit().State(f.Hero)->State.Abilities.at("Bolt").Uses == 2);
}
TEST(LoadoutWorldSpellPowerAndAttackCompose)
{
    LFixture f;
    f.Item();
    std::string e;
    CHECK(f.Kit().Equip(f.Hero, "Sword", e));
    f.Ability();
    auto hit = f.Cast();
    CHECK(hit.Success && hit.Applied == 60);
    CHECK(f.Rules().Health(f.Enemy)->Current == 140);
}
TEST(LoadoutWorldBlockedOutOfRangeAndFriendlyCastPreserveResources)
{
    LFixture f;
    f.Ability();
    f.Enemy.GetTransform().Translation = { 10, 0, 0 };
    CHECK(!f.Cast().Success);
    CHECK(f.Kit().State(f.Hero)->State.Energy == 100);
    f.Enemy.GetTransform().Translation = { 2, 0, 0 };
    std::string e;
    CHECK(f.Rules().Configure(f.Enemy, { 200, 0, 0, 0, 1 }, true, e));
    CHECK(!f.Cast().Success);
    CHECK(f.Kit().State(f.Hero)->State.Abilities.at("Bolt").Uses == 0);
}
TEST(LoadoutWorldMissingIngredientAndLockedAbilityAreTransactional)
{
    LFixture f;
    f.Ability("Bolt", Gameplay::AbilityKind::Damage, 10, 1, "Rune");
    CHECK(!f.Cast().Success);
    std::string e;
    CHECK(f.Rules().ChangeItem(f.Hero, "Rune", 1, false, e));
    CHECK(f.Kit().Unlock(f.Hero, "Bolt", false, e));
    CHECK(!f.Cast().Success);
    CHECK(f.Rules().Inventory(f.Hero).Count("Rune") == 1);
    CHECK(f.Kit().Unlock(f.Hero, "Bolt", true, e));
    CHECK(f.Cast().Success);
    CHECK(f.Rules().Inventory(f.Hero).Count("Rune") == 0);
}
TEST(LoadoutWorldInsufficientEnergyAndRefillClamp)
{
    LFixture f;
    f.Ability("Bolt", Gameplay::AbilityKind::Damage, 80, 0);
    CHECK(f.Cast().Success);
    CHECK(!f.Cast().Success);
    CHECK(f.Kit().Refill(f.Hero, 200) == 80);
    CHECK(f.Kit().State(f.Hero)->State.Energy == 100);
}
TEST(LoadoutWorldHealShieldNoEffectDoesNotConsumePotion)
{
    LFixture f;
    f.Ability("Heal", Gameplay::AbilityKind::Heal, 5, 1, "Potion");
    f.Ability("Shield", Gameplay::AbilityKind::Shield, 5, 1);
    std::string e;
    CHECK(f.Rules().ChangeItem(f.Hero, "Potion", 1, false, e));
    Gameplay::RuleQuery q;
    CHECK(!f.Kit().Cast(f.Hero, {}, "Heal", q).Success);
    CHECK(f.Rules().Inventory(f.Hero).Count("Potion") == 1);
    f.Rules().Damage(f.Hero, {}, 70, true, false, true);
    auto heal = f.Kit().Cast(f.Hero, {}, "Heal", q);
    CHECK(heal.Success && heal.Applied == 20);
    CHECK(f.Rules().Inventory(f.Hero).Count("Potion") == 0);
    auto shield = f.Kit().Cast(f.Hero, f.Hero, "Shield", q);
    CHECK(shield.Success && shield.Applied == 30);
    CHECK(!f.Kit().Cast(f.Hero, f.Enemy, "Shield", q).Success);
}
TEST(LoadoutWorldInvulnerabilityDoesNotChargeCast)
{
    LFixture f;
    f.Ability();
    std::string e;
    CHECK(f.Rules().Configure(f.Enemy, { 200, 0, 5, 0, 2 }, true, e));
    f.Rules().Damage(f.Enemy, {}, 1, true, false, false);
    CHECK(!f.Cast().Success);
    CHECK(f.Kit().State(f.Hero)->State.Energy == 100 && f.Kit().State(f.Hero)->State.Abilities.at("Bolt").Remaining == 0);
}
TEST(LoadoutWorldDeadInactiveForeignAndGenerationIsolation)
{
    LFixture f;
    f.Ability();
    f.World.SetActive(f.Hero, false);
    CHECK(!f.Cast().Success);
    f.Kit().Tick(10);
    CHECK(f.Kit().State(f.Hero)->State.Energy == 100);
    f.World.SetActive(f.Hero, true);
    f.Rules().Damage(f.Hero, {}, 1000, true, true, true);
    CHECK(!f.Cast().Success);
    std::string e;
    CHECK(!f.Kit().Equip(f.Hero, "Sword", e));
    Scene other;
    auto alien = other.CreateEntity();
    CHECK(!f.Kit().Configure(alien, Gameplay::DefaultCombatStats, e));
    f.World.DestroyEntity(f.Hero);
    auto replacement = f.World.CreateEntityWithUUID(UUID(100));
    CHECK(!f.Kit().State(replacement));
    f.Kit().BeginFrame();
    CHECK(!f.Kit().State(replacement));
}
TEST(LoadoutWorldPauseAndInactiveFreezeEnergyAndCooldown)
{
    LFixture f;
    f.Ability();
    CHECK(f.Cast().Success);
    f.World.SetActive(f.Hero, false);
    f.Kit().Tick(1);
    CHECK(f.Kit().State(f.Hero)->State.Energy == 90 && f.Kit().State(f.Hero)->State.Abilities.at("Bolt").Remaining == 1);
    f.World.SetActive(f.Hero, true);
    f.World.SetPaused(true);
    f.World.UpdateRuntime(.5f);
    CHECK(f.Kit().State(f.Hero)->State.Energy == 90);
    f.World.SetPaused(false);
    f.Kit().Tick(1);
    CHECK(f.Kit().State(f.Hero)->State.Energy == 95 && f.Kit().State(f.Hero)->State.Abilities.at("Bolt").Remaining == 0);
}
TEST(LoadoutWorldVersion3SaveRestoresEquipmentResourcesAndCooldown)
{
    LFixture f;
    f.Item();
    f.Ability();
    std::string e;
    CHECK(f.Kit().Equip(f.Hero, "Sword", e));
    CHECK(f.Cast().Success);
    f.Kit().Tick(.25);
    auto save = f.Saves().Capture("LoadoutV1");
    CHECK(save.Version == 3);
    CHECK(f.Kit().Unequip(f.Hero, Gameplay::EquipSlot::Weapon, e));
    f.Kit().Tick(10);
    CHECK(f.Saves().Restore(save, "LoadoutV1").Success);
    auto s = f.Kit().State(f.Hero);
    CHECK(s->State.Equipped.at(Gameplay::EquipSlot::Weapon) == "Sword");
    CHECK(s->State.Energy == 91.25 && s->State.Abilities.at("Bolt").Remaining == .75);
    CHECK(f.Rules().Inventory(f.Hero).Count("Sword") == 0);
    CHECK(!f.Cast().Success);
}
TEST(LoadoutWorldMalformedSaveRejectsWholeTransaction)
{
    LFixture f;
    f.Item();
    f.Ability();
    std::string e;
    CHECK(f.Kit().Equip(f.Hero, "Sword", e));
    auto original = f.Saves().Capture("LoadoutV1");
    for (int variant = 0; variant < 4; ++variant)
    {
        auto save = original;
        auto& hero = save.Entities.at(1);
        CHECK(hero.Key == "Player");
        hero.Health = 1;
        hero.Position = { 50, 0, 0 };
        if (variant == 0) hero.Loadout->Energy = 1000;
        if (variant == 1) hero.Loadout->Abilities.at("Bolt").Remaining = 2;
        if (variant == 2) hero.Loadout->Equipped[Gameplay::EquipSlot::Body] = "Sword";
        if (variant == 3) hero.Loadout->Abilities.erase("Bolt");
        CHECK(!f.Saves().Restore(save, "LoadoutV1").Success);
        CHECK(f.Rules().Health(f.Hero)->Current == 100);
        CHECK(f.Hero.GetTransform().Translation.x == 0);
        CHECK(f.Kit().State(f.Hero)->State.Equipped.size() == 1);
    }
}
TEST(LoadoutWorldLegacyV1AndV2MigrationInitializesAuthoredState)
{
    for (int version : { 1, 2 })
    {
        LFixture f;
        f.Item();
        f.Ability();
        auto save = f.Saves().Capture("LoadoutV1");
        save.Version = version;
        for (auto& s : save.Entities)
        {
            s.Loadout.reset();
            auto signature = Visual::Value::parse(s.Signature);
            signature.erase("Loadout");
            s.Signature = signature.dump();
        }
        std::string e;
        CHECK(f.Kit().Equip(f.Hero, "Sword", e));
        CHECK(f.Cast().Success);
        CHECK(f.Saves().Restore(save, "LoadoutV1").Success);
        auto s = f.Kit().State(f.Hero);
        CHECK(s->State.Equipped.empty() && s->State.Energy == 100 && s->State.Abilities.at("Bolt").Uses == 0);
        CHECK(f.Rules().Inventory(f.Hero).Count("Sword") == 1);
    }
}
TEST(LoadoutWorldChangedDefinitionRejectedBeforeMutation)
{
    LFixture f;
    f.Ability();
    auto save = f.Saves().Capture("LoadoutV1");
    f.Item();
    CHECK(!f.Saves().Restore(save, "LoadoutV1").Success);
    CHECK(f.Rules().Health(f.Hero)->Current == 100);
}
TEST(LoadoutWorldFreshSceneStableKeyRestore)
{
    Gameplay::GameSave save;
    {
        LFixture f;
        f.Item();
        f.Ability();
        std::string e;
        CHECK(f.Kit().Equip(f.Hero, "Sword", e));
        CHECK(f.Cast().Success);
        save = f.Saves().Capture("LoadoutV1");
    }
    {
        LFixture f;
        f.Item();
        f.Ability();
        CHECK(f.Saves().Restore(save, "LoadoutV1").Success);
        CHECK(f.Kit().State(f.Hero)->State.Equipped.size() == 1);
        CHECK(f.Kit().State(f.Hero)->State.Abilities.at("Bolt").Uses == 1);
        CHECK(f.Rules().Health(f.Enemy)->Current == 140);
    }
}
TEST(LoadoutWorldDefinitionsAndEventsAreBounded)
{
    LFixture f;
    std::string e;
    CHECK(!f.Kit().Configure(f.Hero, Gameplay::DefaultCombatStats, e));
    for (int n = 0; n < 32; ++n) CHECK(f.Kit().DefineItem(f.Hero, { "Item" + std::to_string(n) }, e));
    CHECK(!f.Kit().DefineItem(f.Hero, { "Overflow" }, e));
    for (int n = 0; n < 16; ++n)
    {
        Gameplay::AbilitySpec a;
        a.Key = "Skill" + std::to_string(n);
        CHECK(f.Kit().DefineAbility(f.Hero, a, e));
    }
    Gameplay::AbilitySpec a;
    a.Key = "Overflow";
    CHECK(!f.Kit().DefineAbility(f.Hero, a, e));
    for (int n = 0; n < 40; ++n) CHECK(f.Kit().Unlock(f.Hero, "Skill0", n % 2, e));
    CHECK(f.Kit().Dropped(f.Hero) == 8);
    int count = 0;
    while (f.Kit().Poll(f.Hero)) ++count;
    CHECK(count == 32);
}
TEST(LoadoutWorldQuestKillIntegrationAndEquipmentDoesNotForgeCollect)
{
    LFixture f;
    f.Item();
    std::string e;
    auto& quests = Gameplay::QuestWorld::For(f.World);
    Gameplay::QuestSpec spec;
    spec.Key = "Kill";
    spec.Objectives = { { "Kill", "Enemy defeated", "", Gameplay::QuestKind::Kill, 1 } };
    CHECK(quests.Configure(f.Hero, spec, e));
    CHECK(quests.Start(f.Hero, "Kill", e));
    spec.Key = "Collect";
    spec.Objectives = { { "Get", "Get sword", "Sword", Gameplay::QuestKind::Collect, 1 } };
    CHECK(quests.Configure(f.Hero, spec, e));
    CHECK(quests.Start(f.Hero, "Collect", e));
    CHECK(f.Kit().Equip(f.Hero, "Sword", e));
    CHECK(f.Kit().Unequip(f.Hero, Gameplay::EquipSlot::Weapon, e));
    CHECK(quests.State(f.Hero, "Collect")->Progress.Mode == Gameplay::QuestMode::Active);
    f.Ability("Bolt", Gameplay::AbilityKind::Damage, 0, 0);
    for (int n = 0; n < 7; ++n) f.Cast();
    CHECK(quests.State(f.Hero, "Kill")->Progress.Mode == Gameplay::QuestMode::Ready);
}
TEST(LoadoutWorldBlueprintCatalogDispatchAndTypedValidation)
{
    LFixture f;
    Gameplay::Runtime r;
    auto item = LCall(r, f.Hero, "Gameplay.ItemDefine", { { "Item", "Sword" } });
    CHECK(item.at("Success") == true);
    std::string e;
    CHECK(f.Rules().ChangeItem(f.Hero, "Sword", 1, false, e));
    CHECK(LCall(r, f.Hero, "Gameplay.EquipmentEquip", { { "Item", "Sword" } }).at("Success") == true);
    CHECK(LCall(r, f.Hero, "Gameplay.LoadoutStats").at("Attack") == 10);
    CHECK(LCall(r, f.Hero, "Gameplay.AbilityDefine").at("Success") == true);
    CHECK(LCall(r, f.Hero, "Gameplay.AbilityCast", { { "Victim", uint64_t(f.Enemy.GetUUID()) }, { "LineOfSight", false }, { "TwoD", true } }).at("Success") == true);
    CHECK(LCall(r, f.Hero, "Gameplay.AbilityState").at("Uses") == 1);
    bool threw = false;
    try
    {
        LCall(r, f.Hero, "Gameplay.ItemDefine", { { "Slot", .5 } });
    }
    catch (...)
    {
        threw = true;
    }
    CHECK(threw);
    threw = false;
    try
    {
        LCall(r, f.Hero, "Gameplay.LoadoutConfigure", { { "Stats", Visual::Value::array({ 1, 2 }) } });
    }
    catch (...)
    {
        threw = true;
    }
    CHECK(threw);
}
TEST(LoadoutWorldRuntimeStopStartClearsOwnedRecords)
{
    LFixture f;
    f.Item();
    f.World.OnRuntimeStop();
    f.World.OnRuntimeStart();
    CHECK(!f.Kit().State(f.Hero));
}
TEST(LoadoutWorldWorkshopGraphsValidateAndRoundtrip)
{
    auto assets = std::filesystem::path(ZAMIN_SOURCE_DIR) / "Projects/LoadoutWorkshop/Assets/Scripts";
    for (auto file : { "Loadout2D.zgraph", "Loadout3D.zgraph" })
    {
        auto g = Visual::Graph::Load(assets / file);
        CHECK(g.Validate().empty());
        CHECK(Visual::Graph::Parse(g.Serialize()).Validate().empty());
    }
}
