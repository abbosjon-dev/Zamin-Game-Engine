#include "TestAssertions.h"
#include <Zamin/Gameplay/QuestWorld.h>
#include <Zamin/Gameplay/GameplayRules.h>
#include <Zamin/Gameplay/SaveWorld.h>
#include <Zamin/Gameplay/GameplayRuntime.h>
#include <Zamin/Gameplay/GameplayUI.h>
#include <Zamin/Scene/Entity.h>
#include <Zamin/Core/SaveData.h>
#include <Zamin/Utils/FileSystem.h>
using namespace Zamin;
namespace {
    struct QFixture
    {
        Scene World;
        Entity Hero, Enemy, Object, Label, Bar;
        std::filesystem::path Root;
        QFixture(uint64_t offset = 0)
        {
            Root = std::filesystem::temp_directory_path() / ("zamin-quest-" + std::to_string(uint64_t(UUID())));
            std::filesystem::create_directories(Root);
            SaveData::SetFilePath(Root / "prefs.json");
            World.SetName("QuestTest");
            Hero = World.CreateEntityWithUUID(UUID(100 + offset), "Hero");
            Enemy = World.CreateEntityWithUUID(UUID(200 + offset), "Enemy");
            Enemy.AddTag("Enemy");
            Object = World.CreateEntityWithUUID(UUID(300 + offset), "Beacon");
            Object.AddTag("Beacon");
            Object.GetTransform().Translation = { 1, 0, 0 };
            Label = World.CreateEntity();
            Label.AddComponent<UITransformComponent>();
            Label.AddComponent<TextComponent>().Text = "Authored";
            Bar = World.CreateEntity();
            Bar.AddComponent<UITransformComponent>();
            Bar.AddComponent<UIProgressBarComponent>().Value = .2f;
            World.OnRuntimeStart();
            std::string e;
            CHECK(Rules().Configure(Hero, { 100, 0, 0, 0, 1 }, false, e));
            CHECK(Rules().Configure(Enemy, { 100, 0, 0, 0, 2 }, false, e));
            CHECK(Rules().ConfigureBag(Hero, 8, 99, false, e));
            CHECK(Saves().Bind(Hero, "Player", e));
        }
        ~QFixture()
        {
            World.OnRuntimeStop();
            SaveData::SetFilePath({});
            std::filesystem::remove_all(Root);
        }
        Gameplay::QuestWorld& Quests() { return Gameplay::QuestWorld::For(World); }
        Gameplay::RuleWorld& Rules() { return Gameplay::RuleWorld::For(World); }
        Gameplay::SaveWorld& Saves() { return Gameplay::SaveWorld::For(World); }
        Gameplay::QuestSpec Spec(std::string key = "Survey")
        {
            Gameplay::QuestSpec s;
            s.Key = key;
            s.Title = "Survey";
            s.Objectives = { { "Visit", "Visit checkpoint", "", Gameplay::QuestKind::Manual, 1 } };
            s.Rewards = { { "Coin", 5 } };
            s.XP = 150;
            return s;
        }
        void Configure(Gameplay::QuestSpec s)
        {
            std::string e;
            CHECK(Quests().Configure(Hero, s, e));
        }
        void Ready(std::string key = "Survey")
        {
            std::string e;
            Configure(Spec(key));
            CHECK(Quests().Start(Hero, key, e));
            CHECK(Quests().Advance(Hero, key, "Visit", 1, "Zone", e));
        }
    };
    Visual::Value Call(Gameplay::Runtime& r, Entity host, const std::string& kind, std::map<std::string, Visual::Value> overrides = {}, Entity target = {})
    {
        std::map<std::string, Visual::Value> a;
        for (auto& p : Visual::Catalog().at(kind).Inputs)
            if (p.Kind != Visual::Type::Exec) a[p.Name] = p.Default;
        for (auto& [k, v] : overrides) a[k] = v;
        return r.Dispatch(host, target ? target : host, kind, a);
    }
}
TEST(QuestWorldClaimCommitsBagXPAndModeExactlyOnce)
{
    QFixture f;
    f.Ready();
    std::string e;
    CHECK(f.Quests().Claim(f.Hero, "Survey", e));
    CHECK(f.Rules().Inventory(f.Hero).Count("Coin") == 5);
    CHECK(f.Quests().XP(f.Hero) == 150 && f.Quests().Progression(f.Hero).Level == 2);
    CHECK(!f.Quests().Claim(f.Hero, "Survey", e));
    CHECK(f.Rules().Inventory(f.Hero).Count("Coin") == 5 && f.Quests().XP(f.Hero) == 150);
}
TEST(QuestWorldFullBagCannotPartiallyGrantMultipleRewardsOrXP)
{
    QFixture f;
    auto s = f.Spec();
    s.Rewards = { { "Apple", 1 }, { "Coin", 5 } };
    f.Configure(s);
    std::string e;
    CHECK(f.Rules().ConfigureBag(f.Hero, 1, 10, true, e));
    CHECK(f.Rules().ChangeItem(f.Hero, "Key", 1, false, e));
    CHECK(f.Quests().Start(f.Hero, "Survey", e));
    CHECK(f.Quests().Advance(f.Hero, "Survey", "Visit", 1, "Zone", e));
    CHECK(!f.Quests().Claim(f.Hero, "Survey", e));
    CHECK(f.Quests().State(f.Hero, "Survey")->Progress.Mode == Gameplay::QuestMode::Ready);
    CHECK(f.Quests().XP(f.Hero) == 0 && f.Rules().Inventory(f.Hero).Count("Apple") == 0);
    CHECK(f.Rules().Inventory(f.Hero).Count("Key") == 1);
}
TEST(QuestWorldPrerequisitesRequireClaimAndRejectForwardCycles)
{
    QFixture f;
    auto second = f.Spec("Second");
    second.Requires = { "Survey" };
    std::string e;
    CHECK(!f.Quests().Configure(f.Hero, second, e));
    f.Ready();
    f.Configure(second);
    CHECK(!f.Quests().Start(f.Hero, "Second", e));
    CHECK(f.Quests().Claim(f.Hero, "Survey", e));
    CHECK(f.Quests().Start(f.Hero, "Second", e));
    CHECK(!f.Quests().Configure(f.Hero, f.Spec(), e));
}
TEST(QuestWorldAutomaticCollectCountsCommittedGainsNotRemovals)
{
    QFixture f;
    auto s = f.Spec();
    s.Objectives = { { "Runes", "Runes", "Rune", Gameplay::QuestKind::Collect, 3 } };
    f.Configure(s);
    std::string e;
    CHECK(f.Rules().ChangeItem(f.Hero, "Rune", 1, false, e));
    CHECK(f.Quests().Start(f.Hero, "Survey", e));
    CHECK(f.Rules().ChangeItem(f.Hero, "Rune", 2, false, e));
    CHECK(f.Rules().ChangeItem(f.Hero, "Rune", 1, true, e));
    CHECK(f.Quests().State(f.Hero, "Survey")->Progress.Counts[0] == 2);
    CHECK(f.Rules().ChangeItem(f.Hero, "Rune", 1, false, e));
    CHECK(f.Quests().State(f.Hero, "Survey")->Progress.Mode == Gameplay::QuestMode::Ready);
}
TEST(QuestWorldAutomaticKillCreditsOnlyLethalMatchingSource)
{
    QFixture f;
    auto s = f.Spec();
    s.Objectives = { { "Kill", "Defeat enemy", "Enemy", Gameplay::QuestKind::Kill, 1 } };
    f.Configure(s);
    std::string e;
    CHECK(f.Quests().Start(f.Hero, "Survey", e));
    f.Rules().Damage(f.Enemy, f.Hero, 20, false, false, true);
    CHECK(f.Quests().State(f.Hero, "Survey")->Progress.Counts[0] == 0);
    f.Rules().Damage(f.Enemy, f.Hero, 1000, false, false, true);
    CHECK(f.Quests().State(f.Hero, "Survey")->Progress.Counts[0] == 1);
    CHECK(f.Quests().State(f.Hero, "Survey")->Progress.Mode == Gameplay::QuestMode::Ready);
}
TEST(QuestWorldInteractionNoticesDoNotDoubleCountCollectedEvents)
{
    QFixture f;
    auto s = f.Spec();
    s.Objectives = { { "Use", "Activate", "Beacon", Gameplay::QuestKind::Interact, 1 }, { "Rune", "Rune", "Rune", Gameplay::QuestKind::Collect, 2 } };
    f.Configure(s);
    std::string e;
    CHECK(f.Quests().Start(f.Hero, "Survey", e));
    Gameplay::InteractionSettings use;
    use.RewardItem = "Rune";
    use.RewardCount = 1;
    CHECK(f.Rules().ConfigureInteraction(f.Object, use, false, e));
    Gameplay::RuleQuery q;
    q.TwoD = true;
    q.LineOfSight = false;
    CHECK(f.Rules().Interact(f.Hero, f.Object, q).Success);
    auto state = f.Quests().State(f.Hero, "Survey");
    CHECK(state->Progress.Counts[0] == 1 && state->Progress.Counts[1] == 1);
    CHECK(!f.Rules().Interact(f.Hero, f.Object, q).Success);
    CHECK(f.Quests().State(f.Hero, "Survey")->Progress.Counts[1] == 1);
}
TEST(QuestWorldRewardGrantsCannotRecursivelyAdvanceCollection)
{
    QFixture f;
    f.Ready();
    auto other = f.Spec("CollectCoins");
    other.Objectives = { { "Coins", "Coins", "Coin", Gameplay::QuestKind::Collect, 5 } };
    f.Configure(other);
    std::string e;
    CHECK(f.Quests().Start(f.Hero, "CollectCoins", e));
    CHECK(f.Quests().Claim(f.Hero, "Survey", e));
    CHECK(f.Quests().State(f.Hero, "CollectCoins")->Progress.Counts[0] == 0);
}
TEST(QuestWorldDeathInactiveAndForeignActorsCannotMutateProgress)
{
    QFixture f;
    f.Configure(f.Spec());
    std::string e;
    f.World.SetActive(f.Hero, false);
    CHECK(!f.Quests().Start(f.Hero, "Survey", e));
    f.World.SetActive(f.Hero, true);
    f.Rules().Damage(f.Hero, {}, 1000, true, false, true);
    CHECK(!f.Quests().Start(f.Hero, "Survey", e));
    Scene foreign;
    auto actor = foreign.CreateEntity();
    CHECK(!f.Quests().Configure(actor, f.Spec(), e));
}
TEST(QuestWorldUUIDReplacementCannotInheritBook)
{
    QFixture f;
    f.Ready();
    auto id = f.Hero.GetUUID();
    f.World.DestroyEntity(f.Hero);
    auto replacement = f.World.CreateEntityWithUUID(id, "NewHero");
    CHECK(!f.Quests().State(replacement, "Survey"));
    f.Quests().BeginFrame();
    CHECK(f.Quests().XP(replacement) == 0);
}
TEST(QuestWorldHUDUpdatesFromModelAndReleasesOriginalWidgets)
{
    QFixture f;
    f.Configure(f.Spec());
    std::string e;
    CHECK(f.Quests().BindHUD(f.Hero, f.Hero, f.Label, "Survey", "Visit", e));
    CHECK(f.Quests().BindHUD(f.Hero, f.Hero, f.Bar, "Survey", "", e));
    CHECK(f.Quests().Start(f.Hero, "Survey", e));
    f.Quests().Refresh();
    CHECK(f.Label.GetComponent<TextComponent>().Text == "Visit checkpoint  0 / 1");
    CHECK(f.Quests().Advance(f.Hero, "Survey", "Visit", 1, "Zone", e));
    f.Quests().Refresh();
    CHECK_NEAR(f.Bar.GetComponent<UIProgressBarComponent>().Value, 1, 1e-6);
    CHECK(f.Quests().UnbindHUD(f.Label));
    CHECK(f.Label.GetComponent<TextComponent>().Text == "Authored");
}
TEST(QuestWorldHUDCannotCompeteWithGameplayUIBindings)
{
    QFixture f;
    f.Configure(f.Spec());
    std::string e;
    CHECK(Gameplay::UIWorld::For(f.World).BindHUD(f.Hero, f.Hero, f.Label, {}, e));
    CHECK(!f.Quests().BindHUD(f.Hero, f.Hero, f.Label, "Survey", "", e));
    CHECK(f.Quests().BindHUD(f.Hero, f.Hero, f.Bar, "Survey", "", e));
    CHECK(!Gameplay::UIWorld::For(f.World).BindHUD(f.Hero, f.Hero, f.Bar, {}, e));
}
TEST(QuestWorldEventCapacityAndDefinitionLimitsAreBounded)
{
    QFixture f;
    std::string e;
    for (int i = 0; i < 16; ++i)
    {
        auto key = "Q" + std::to_string(i);
        f.Configure(f.Spec(key));
        CHECK(f.Quests().Start(f.Hero, key, e));
        CHECK(f.Quests().Advance(f.Hero, key, "Visit", 1, "Zone", e));
    }
    CHECK(!f.Quests().Configure(f.Hero, f.Spec("Overflow"), e));
    CHECK(f.Quests().Dropped(f.Hero) == 16);
    int events = 0;
    while (f.Quests().Poll(f.Hero)) ++events;
    CHECK(events == 32);
}
TEST(QuestWorldSaveV2RestoresClaimsAndRewardBagTogether)
{
    QFixture f;
    f.Ready();
    std::string e;
    CHECK(f.Quests().Claim(f.Hero, "Survey", e));
    auto save = f.Saves().Capture("V1");
    CHECK(save.Version == 2);
    CHECK(f.Rules().ChangeItem(f.Hero, "Coin", 5, true, e));
    CHECK(f.Saves().Restore(save, "V1").Success);
    CHECK(f.Rules().Inventory(f.Hero).Count("Coin") == 5 && f.Quests().XP(f.Hero) == 150);
    CHECK(!f.Quests().Claim(f.Hero, "Survey", e));
    CHECK(!f.Quests().Poll(f.Hero));
}
TEST(QuestWorldSaveReceiptsSurviveNewUUIDRebinding)
{
    Gameplay::GameSave save;
    {
        QFixture first;
        auto s = first.Spec();
        s.Objectives[0].Goal = 2;
        first.Configure(s);
        std::string e;
        CHECK(first.Quests().Start(first.Hero, "Survey", e));
        CHECK(first.Quests().Advance(first.Hero, "Survey", "Visit", 1, "Zone1", e));
        save = first.Saves().Capture("V1");
    }
    QFixture second(1000);
    auto s = second.Spec();
    s.Objectives[0].Goal = 2;
    second.Configure(s);
    CHECK(second.Saves().Restore(save, "V1").Success);
    std::string e;
    CHECK(!second.Quests().Advance(second.Hero, "Survey", "Visit", 1, "Zone1", e));
    CHECK(second.Quests().Advance(second.Hero, "Survey", "Visit", 1, "Zone2", e));
}
TEST(QuestWorldV1SaveMigrationIntroducesLockedZeroXPQuests)
{
    QFixture f;
    auto legacy = f.Saves().Capture("V1");
    CHECK(legacy.Version == 1);
    f.Ready();
    std::string e;
    CHECK(f.Quests().Claim(f.Hero, "Survey", e));
    CHECK(f.Saves().Restore(legacy, "V1").Success);
    CHECK(f.Quests().State(f.Hero, "Survey")->Progress.Mode == Gameplay::QuestMode::Locked);
    CHECK(f.Quests().XP(f.Hero) == 0 && f.Rules().Inventory(f.Hero).Count("Coin") == 0);
}
TEST(QuestWorldForgedXPAndCountsCannotPartiallyRestore)
{
    QFixture f;
    f.Ready();
    auto save = f.Saves().Capture("V1");
    auto before = EncodeGameSave(save);
    save.Entities[0].Health = 50;
    save.Entities[0].QuestBook.XP = 150;
    CHECK(!f.Saves().Restore(save, "V1").Success);
    CHECK(EncodeGameSave(f.Saves().Capture("V1")) == before);
    save = f.Saves().Capture("V1");
    save.Entities[0].QuestBook.Quests.at("Survey").Counts[0] = 0;
    CHECK(!f.Saves().Restore(save, "V1").Success);
    CHECK(EncodeGameSave(f.Saves().Capture("V1")) == before);
}
TEST(QuestWorldChangedQuestDefinitionRejectsWholeSnapshot)
{
    QFixture f;
    f.Ready();
    auto save = f.Saves().Capture("V1");
    auto signature = Visual::Value::parse(save.Entities[0].Signature);
    signature["Quests"]["Survey"][0] = "Changed title";
    save.Entities[0].Signature = signature.dump();
    CHECK(!f.Saves().Restore(save, "V1").Success);
    CHECK(f.Quests().State(f.Hero, "Survey")->Progress.Mode == Gameplay::QuestMode::Ready);
}
TEST(QuestWorldPrimaryBackupKeepsQuestAndRewardConsistency)
{
    QFixture f;
    f.Ready();
    CHECK(f.Saves().Save("Q", "V1").Success);
    std::string e;
    CHECK(f.Quests().Claim(f.Hero, "Survey", e));
    CHECK(f.Saves().Save("Q", "V1").Success);
    CHECK(FileSystem::WriteTextFile(f.Saves().Path("Q"), "corrupt"));
    f.Saves().BeginFrame();
    auto r = f.Saves().Load("Q", "V1");
    CHECK(r.Success && r.Recovered);
    CHECK(f.Quests().XP(f.Hero) == 0 && f.Rules().Inventory(f.Hero).Count("Coin") == 0);
    CHECK(f.Quests().State(f.Hero, "Survey")->Progress.Mode == Gameplay::QuestMode::Ready);
}
TEST(QuestWorldBlueprintConfigureAdvanceClaimAndStateAreTyped)
{
    QFixture f;
    Gameplay::Runtime r;
    CHECK(Call(r, f.Hero, "Gameplay.QuestConfigure").at("Success") == true);
    CHECK(Call(r, f.Hero, "Gameplay.QuestStart").at("Success") == true);
    CHECK(Call(r, f.Hero, "Gameplay.QuestAdvance").at("Success") == true);
    CHECK(Call(r, f.Hero, "Gameplay.QuestClaim").at("Success") == true);
    CHECK(Call(r, f.Hero, "Gameplay.QuestState").at("Claimed") == true);
    CHECK(Call(r, f.Hero, "Gameplay.QuestProgression").at("XP") == 100);
    CHECK(Call(r, f.Hero, "Gameplay.QuestObjective").at("Complete") == true);
}
TEST(QuestWorldPlayRestartClearsBook)
{
    QFixture f;
    f.Ready();
    f.World.OnRuntimeStop();
    f.World.OnRuntimeStart();
    CHECK(!f.Quests().State(f.Hero, "Survey"));
    CHECK(f.Quests().XP(f.Hero) == 0);
}
TEST(QuestWorldCodecVersion2RoundtripAndV1RejectsQuestPayload)
{
    QFixture f;
    f.Ready();
    auto save = f.Saves().Capture("V1");
    auto bytes = EncodeGameSave(save);
    Gameplay::GameSave copy;
    std::string e;
    CHECK(DecodeGameSave(bytes, copy, e));
    CHECK(copy.Version == 2 && copy.Entities[0].QuestBook.Quests.at("Survey").Mode == Gameplay::QuestMode::Ready);
    CHECK(EncodeGameSave(copy) == bytes);
    save.Version = 1;
    bool threw = false;
    try
    {
        EncodeGameSave(save);
    }
    catch (const std::exception&)
    {
        threw = true;
    }
    CHECK(threw);
}
TEST(QuestWorldForgedPrerequisiteStateRejectsEntireRestore)
{
    QFixture f;
    f.Ready();
    auto second = f.Spec("Second");
    second.Requires = { "Survey" };
    f.Configure(second);
    auto save = f.Saves().Capture("V1");
    auto before = EncodeGameSave(save);
    save.Entities[0].QuestBook.Quests.at("Second").Mode = Gameplay::QuestMode::Active;
    save.Entities[0].Health = 50;
    CHECK(!f.Saves().Restore(save, "V1").Success);
    CHECK(EncodeGameSave(f.Saves().Capture("V1")) == before);
}
TEST(QuestWorldFailedQuestCannotClaimOrUnlockDependency)
{
    QFixture f;
    f.Ready();
    std::string e;
    CHECK(f.Quests().Fail(f.Hero, "Survey", e));
    CHECK(!f.Quests().Claim(f.Hero, "Survey", e));
    CHECK(f.Quests().XP(f.Hero) == 0 && f.Rules().Inventory(f.Hero).Count("Coin") == 0);
    auto second = f.Spec("Second");
    second.Requires = { "Survey" };
    f.Configure(second);
    CHECK(!f.Quests().Start(f.Hero, "Second", e));
}
TEST(QuestWorldActorCapacityAndExplicitReplacementAreBounded)
{
    QFixture f;
    f.Configure(f.Spec());
    std::string e;
    for (int i = 1; i < 32; ++i)
    {
        auto actor = f.World.CreateEntity();
        CHECK(f.Rules().Configure(actor, {}, false, e));
        CHECK(f.Quests().Configure(actor, f.Spec(), e));
    }
    auto overflow = f.World.CreateEntity();
    CHECK(f.Rules().Configure(overflow, {}, false, e));
    CHECK(!f.Quests().Configure(overflow, f.Spec(), e));
    auto id = f.Hero.GetUUID();
    f.World.DestroyEntity(f.Hero);
    auto fresh = f.World.CreateEntityWithUUID(id, "Fresh");
    CHECK(f.Rules().Configure(fresh, {}, false, e));
    CHECK(f.Quests().Configure(fresh, f.Spec(), e));
    CHECK(f.Quests().State(fresh, "Survey")->Progress.Mode == Gameplay::QuestMode::Locked);
}
TEST(QuestWorldHUDUsesFallbackForDestroyedSourceAndRejectsStaleWidgetReuse)
{
    QFixture f;
    f.Configure(f.Spec());
    std::string e;
    CHECK(f.Quests().BindHUD(f.Object, f.Hero, f.Label, "Survey", "", e));
    auto old = f.Label.GetUUID();
    f.World.DestroyEntity(f.Label);
    auto fresh = f.World.CreateEntityWithUUID(old, "Fresh label");
    fresh.AddComponent<UITransformComponent>();
    fresh.AddComponent<TextComponent>();
    CHECK(!f.Quests().BindHUD(f.Object, f.Hero, fresh, "Survey", "", e));
    f.Quests().Refresh();
    CHECK(f.Quests().BindHUD(f.Object, f.Hero, fresh, "Survey", "", e));
    f.World.DestroyEntity(f.Hero);
    f.Quests().BeginFrame();
    CHECK(fresh.GetComponent<TextComponent>().Text == "Quest unavailable");
}
namespace {
    void RehashQuestSave(Visual::Value& v)
    {
        uint64_t hash = 14695981039346656037ull;
        for (unsigned char c : v.at("Payload").dump())
        {
            hash ^= c;
            hash *= 1099511628211ull;
        }
        v["Checksum"] = std::to_string(hash);
    }
}
TEST(QuestWorldCodecRejectsDuplicateReceiptsAndUnknownQuestFields)
{
    QFixture f;
    f.Ready();
    auto v = Visual::Value::parse(EncodeGameSave(f.Saves().Capture("V1")));
    auto& entry = v["Payload"]["Entities"][0]["Quests"]["Entries"]["Survey"];
    entry["Receipts"] = { "Zone", "Zone" };
    RehashQuestSave(v);
    Gameplay::GameSave out;
    out.Schema = "Unchanged";
    std::string e;
    CHECK(!DecodeGameSave(v.dump(), out, e));
    CHECK(out.Schema == "Unchanged");
    entry["Receipts"] = { "Zone" };
    entry["Unexpected"] = true;
    RehashQuestSave(v);
    CHECK(!DecodeGameSave(v.dump(), out, e));
    CHECK(out.Schema == "Unchanged");
}
TEST(QuestWorldWorkshopGraphsValidateAndRoundtrip)
{
    auto root = std::filesystem::path(ZAMIN_SOURCE_DIR) / "Projects/QuestWorkshop/Assets/Scripts";
    int count = 0;
    for (auto& e : std::filesystem::directory_iterator(root))
        if (e.path().extension() == ".zgraph")
        {
            auto g = Visual::Graph::Load(e.path());
            CHECK(g.Validate().empty());
            CHECK(Visual::Graph::Parse(g.Serialize()).Validate().empty());
            ++count;
        }
    CHECK(count == 6);
}
