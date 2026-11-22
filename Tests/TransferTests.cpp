// Transfer documents (SaveModel GameTransfer + SaveWorld *Transfer): one bound, living actor's progression carried
// from one scene to the next (a level exit), strict codec, signature-checked transactional restore.
#include <Zamin/Gameplay/GameplayRules.h>
#include "TestAssertions.h"
#include <Zamin/Gameplay/SaveWorld.h>
#include <Zamin/Gameplay/QuestWorld.h>
#include <Zamin/Gameplay/LoadoutWorld.h>
#include <Zamin/Gameplay/CombatWorld.h>
#include <Zamin/Gameplay/GameplayRuntime.h>
#include <Zamin/Scene/Entity.h>
#include <Zamin/Core/SaveData.h>
#include <Zamin/Utils/FileSystem.h>
#include <nlohmann/json.hpp>
#include <functional>
using namespace Zamin;
namespace {
    namespace G = Zamin::Gameplay;
    using J = nlohmann::json;
    const char* Schema = "Chiroqbon-1";
    std::filesystem::path Fixture() { return std::filesystem::path(ZAMIN_SOURCE_DIR) / "Tests/Fixtures/GameSaves/transfer-v1.json"; }
    bool Throws(const std::function<void()>& f)
    {
        try
        {
            f();
        }
        catch (const std::exception&)
        {
            return true;
        }
        return false;
    }
    std::string Hash(const std::string& bytes)
    {
        uint64_t h = 14695981039346656037ull;
        for (unsigned char c : bytes)
        {
            h ^= c;
            h *= 1099511628211ull;
        }
        return std::to_string(h);
    }
    // One level scene: the same hero configuration (health, bag, loadout, quest book) under a different UUID.
    struct Level
    {
        Scene World;
        Entity Hero;
        explicit Level(const char* name, uint64_t id, int bagSlots = 8)
        {
            World.SetName(name);
            Hero = World.CreateEntityWithUUID(UUID(id), "Hero");
            World.OnRuntimeStart();
            std::string error;
            auto& rules = G::RuleWorld::For(World);
            CHECK(rules.Configure(Hero, { 100, 20, .5, 1.2, 1 }, false, error));
            CHECK(rules.ConfigureBag(Hero, bagSlots, 99, false, error));
            auto& loadout = G::LoadoutWorld::For(World);
            CHECK(loadout.Configure(Hero, { 0, 0, 0, 60, 4 }, error));
            CHECK(loadout.DefineItem(Hero, { "Sword", G::EquipSlot::Weapon, { 8, 0, 0, 0, 0 } }, error));
            CHECK(loadout.DefineItem(Hero, { "Charm", G::EquipSlot::Trinket, { 0, 0, 3, 20, 0 } }, error));
            G::AbilitySpec mend;
            mend.Key = "Mend";
            mend.Kind = G::AbilityKind::Heal;
            mend.Amount = 10;
            mend.Cost = 10;
            mend.Cooldown = 4;
            CHECK(loadout.DefineAbility(Hero, mend, error));
            G::AbilitySpec nova = mend;
            nova.Key = "Nova";
            nova.Unlocked = false;
            CHECK(loadout.DefineAbility(Hero, nova, error));
            auto& quests = G::QuestWorld::For(World);
            G::QuestSpec gate;
            gate.Key = "Gate";
            gate.Title = "Bog' darvozasi";
            gate.Objectives = { { "Chief", "Boshliq", "", G::QuestKind::Manual, 1 } };
            gate.Rewards = { { "Coin", 5 } };
            gate.XP = 100;
            CHECK(quests.Configure(Hero, gate, error));
            G::QuestSpec lamps;
            lamps.Key = "Lamps";
            lamps.Title = "Uch chiroq";
            lamps.Objectives = { { "Lamp", "Chiroqlar", "Lamp", G::QuestKind::Collect, 3 } };
            lamps.XP = 200;
            CHECK(quests.Configure(Hero, lamps, error));
            G::QuestSpec boss;
            boss.Key = "Boss";
            boss.Objectives = { { "Boss", "Soya Bek", "", G::QuestKind::Manual, 1 } };
            boss.Requires = { "Gate" };
            boss.XP = 250;
            CHECK(quests.Configure(Hero, boss, error));
            G::EffectSpec burn;
            burn.Key = "Burn";
            burn.Duration = 3;
            burn.Period = .5;
            burn.TickAmount = 1;
            burn.Tick = G::TickKind::Damage;
            CHECK(G::CombatWorld::For(World).DefineEffect(burn, error));
            CHECK(G::SaveWorld::For(World).Bind(Hero, "Hero", error));
        }
        G::RuleWorld& Rules() { return G::RuleWorld::For(World); }
        G::SaveWorld& Saves() { return G::SaveWorld::For(World); }
        G::LoadoutSnapshot Loadout() { return *G::LoadoutWorld::For(World).State(Hero); }
        std::string Mode(const char* quest) { return std::string(G::QuestModeName(G::QuestWorld::For(World).State(Hero, quest)->Progress.Mode)); }
        void Frame() { G::BeginSaveFrame(World); }
    };
    // Level 1 progress: Gate claimed (100 XP, 5 coins), 1/3 lamps (collecting notices the quest), sword + charm equipped, Nova unlocked, Mend used.
    void Play(Level& l)
    {
        std::string error;
        auto& rules = l.Rules();
        CHECK(rules.ChangeItem(l.Hero, "Coin", 7, false, error));
        CHECK(rules.ChangeItem(l.Hero, "Sword", 1, false, error));
        CHECK(rules.ChangeItem(l.Hero, "Charm", 1, false, error));
        auto& loadout = G::LoadoutWorld::For(l.World);
        CHECK(loadout.Equip(l.Hero, "Sword", error));
        CHECK(loadout.Equip(l.Hero, "Charm", error));
        CHECK(loadout.Unlock(l.Hero, "Nova", true, error));
        auto& quests = G::QuestWorld::For(l.World);
        CHECK(quests.Start(l.Hero, "Gate", error) && quests.Start(l.Hero, "Lamps", error));
        CHECK(quests.Advance(l.Hero, "Gate", "Chief", 1, "Chief", error));
        CHECK(quests.Claim(l.Hero, "Gate", error));
        CHECK(rules.ChangeItem(l.Hero, "Lamp", 1, false, error));
        rules.Damage(l.Hero, {}, 45, true, false, true); // shield 20 absorbs first: 100 -> 75
        CHECK(loadout.Cast(l.Hero, l.Hero, "Mend", {}).Success); // 75 -> 88 (10 + Power 3), costs 10 energy, cooldown 4 s
        CHECK(G::CombatWorld::For(l.World).ApplyEffect(l.Hero, "Burn").Outcome != G::ApplyOutcome::Rejected);
    }
}
TEST(GameTransferCodecRoundtripsExactlyAndRejectsCorruption)
{
    G::GameTransfer t;
    t.Scene = "Boglar";
    t.Schema = Schema;
    t.Key = "Hero";
    t.Signature = "{\"Parent\":\"\"}";
    t.Health = 85;
    t.Shield = 0;
    t.Deaths = 2;
    std::string error;
    CHECK(t.Inventory.Configure(8, 99, true, error));
    t.Inventory.Items = { { "Coin", 12 }, { "Lamp", 1 } };
    t.QuestBook.XP = 100;
    t.QuestBook.Quests["Gate"] = { G::QuestMode::Claimed, { 1 }, { "Chief" } };
    t.QuestBook.Quests["Lamps"] = { G::QuestMode::Active, { 1 }, {} };
    G::LoadoutState s;
    s.Energy = 70;
    s.Equipped = { { G::EquipSlot::Weapon, "Sword" } };
    s.Abilities = { { "Mend", { 0, 1, true } }, { "Nova", { 0, 0, true } } };
    t.Loadout = s;
    const auto bytes = G::EncodeGameTransfer(t);
    G::GameTransfer back;
    CHECK(G::DecodeGameTransfer(bytes, back, error));
    CHECK(error.empty() && G::EncodeGameTransfer(back) == bytes);
    CHECK(back.Key == "Hero" && back.Health == 85 && back.Deaths == 2 && back.Inventory.Count("Coin") == 12 && back.QuestBook.XP == 100 && back.Loadout->Equipped.at(G::EquipSlot::Weapon) == "Sword");
    // The committed golden document (written by the first implementation) decodes and re-encodes byte-identically.
    const auto golden = FileSystem::ReadTextFile(Fixture());
    G::GameTransfer fixture;
    CHECK(G::DecodeGameTransfer(golden, fixture, error));
    CHECK(G::EncodeGameTransfer(fixture) + "\n" == golden || G::EncodeGameTransfer(fixture) == golden);
    // Document-level corruption.
    const auto doc = J::parse(bytes);
    auto rejects = [&](const J& value) {
        G::GameTransfer out = back;
        const bool ok = G::DecodeGameTransfer(value.dump(), out, error);
        CHECK(!ok && !error.empty() && G::EncodeGameTransfer(out) == bytes); // failure never changes the caller
        return !ok;
    };
    auto withPayload = [&](auto edit) {
        auto d = doc;
        edit(d["Payload"]);
        d["Checksum"] = Hash(d["Payload"].dump());
        return d;
    };
    size_t rejected = 0;
    for (auto edit : std::vector<std::function<void(J&)>>{
             [](J& d) { d["Format"] = "Zamin.GameSave"; }, [](J& d) { d["Version"] = 0; }, [](J& d) { d["Version"] = 2; },
             [](J& d) { d["Version"] = 1.5; }, [](J& d) { d["Checksum"] = "1"; }, [](J& d) { d["Extra"] = 1; },
             [](J& d) { d.erase("Checksum"); }, [](J& d) { d["Payload"]["Health"] = 84; } })
    {
        auto d = doc;
        edit(d);
        rejected += rejects(d);
    }
    for (auto edit : std::vector<std::function<void(J&)>>{
             [](J& p) { p["Health"] = 0; }, [](J& p) { p["Health"] = -1; }, [](J& p) { p["Health"] = 2e6; }, [](J& p) { p["Shield"] = -1; },
             [](J& p) { p["Deaths"] = 1.5; }, [](J& p) { p["Key"] = "bad key"; }, [](J& p) { p["Key"] = ""; }, [](J& p) { p["Schema"] = "a/b"; },
             [](J& p) { p["Scene"] = ""; }, [](J& p) { p["Signature"] = ""; }, [](J& p) { p["Slots"] = 0; }, [](J& p) { p["Slots"] = 65; },
             [](J& p) { p["Items"]["Coin"] = 0; }, [](J& p) { p["Items"]["Coin"] = 100; }, [](J& p) { p["Items"]["bad key"] = 1; },
             [](J& p) { p["Slots"] = 1; }, [](J& p) { p.erase("Quests"); }, [](J& p) { p["Unknown"] = true; },
             [](J& p) { p["Quests"]["XP"] = -1; }, [](J& p) { p["Quests"]["Entries"]["Lamps"]["Mode"] = 9; },
             [](J& p) { p["Quests"]["Entries"]["Gate"]["Receipts"] = J::array({ "Chief", "Chief" }); },
             [](J& p) { p["Quests"]["Entries"] = J::object(); },
             [](J& p) { p["Loadout"]["Energy"] = -1; }, [](J& p) { p["Loadout"]["Equipped"] = J::array({ "Sword" }); },
             [](J& p) { p["Loadout"]["Equipped"] = J::array({ "Sword", "Sword", "", "" }); },
             [](J& p) { p["Loadout"]["Abilities"]["Mend"]["Remaining"] = 301; }, [](J& p) { p["Loadout"]["Abilities"]["Mend"].erase("Uses"); },
             [](J& p) { p["Loadout"] = 1; }, [](J& p) { p["Items"] = J::array(); }, [](J& p) { p["Key"] = std::string(65, 'k'); },
             [](J& p) { p["Signature"] = std::string(32769, 's'); } })
        rejected += rejects(withPayload(edit));
    CHECK(rejected == 39);
    // Duplicate keys, nesting and size limits.
    auto text = bytes;
    text.insert(text.find("\"Format\""), "\"Format\":\"x\",");
    G::GameTransfer out;
    CHECK(!G::DecodeGameTransfer(text, out, error) && error == "Duplicate JSON transfer key");
    std::string deep = "{\"Format\":";
    for (int i = 0; i < 20; ++i) deep += "[";
    CHECK(!G::DecodeGameTransfer(deep, out, error) && error == "Gameplay transfer nesting limit");
    CHECK(!G::DecodeGameTransfer(std::string(256 * 1024 + 1, ' '), out, error) && error == "Gameplay transfer exceeds 256 KiB");
    auto huge = t;
    huge.Signature = std::string(32768, 's');
    CHECK(!G::EncodeGameTransfer(huge).empty());
    huge.Version = 2;
    CHECK(Throws([&] { G::EncodeGameTransfer(huge); }));
    auto dead = t;
    dead.Health = 0;
    CHECK(Throws([&] { G::EncodeGameTransfer(dead); }));
}
// Level 1 -> level 2: bag, equipment, unlocks, ability uses, energy, quest book and XP arrive on a hero with a
// different UUID; effects, cooldowns and grace end; nothing is granted again.
TEST(SaveTransferCarriesProgressionAcrossScenesAndNeverReplaysRewards)
{
    const auto root = std::filesystem::temp_directory_path() / ("zamin-transfer-" + std::to_string(uint64_t(UUID())));
    std::filesystem::create_directories(root);
    SaveData::SetFilePath(root / "prefs.json");
    G::GameTransfer written;
    double energy = 0;
    {
        Level one("Boglar", 100);
        Play(one);
        CHECK(one.Loadout().State.Abilities.at("Mend").Remaining > 3.9);
        energy = one.Loadout().State.Energy;
        CHECK(energy > 0 && energy < 80);
        auto r = one.Saves().WriteTransfer("Carry", "Hero", Schema);
        CHECK(r.Success && r.Entities == 1 && r.Reason.empty());
        std::string error;
        CHECK(G::DecodeGameTransfer(FileSystem::ReadTextFile(root / "gameplay-transfers/Carry.json"), written, error));
        CHECK(written.Scene == "Boglar" && written.Key == "Hero" && written.Health == 88 && written.Shield == 0);
        CHECK(written.Loadout->Abilities.at("Mend").Remaining == 0 && written.Loadout->Energy == energy);
        // The golden fixture is this exact document (regenerate deliberately: ZAMIN_WRITE_TRANSFER_FIXTURE=1).
        if (std::getenv("ZAMIN_WRITE_TRANSFER_FIXTURE")) CHECK(FileSystem::WriteTextFile(Fixture(), G::EncodeGameTransfer(written) + "\n"));
        one.World.OnRuntimeStop();
    }
    Level two("Yertola", 900);
    std::string error;
    CHECK(G::CombatWorld::For(two.World).ApplyEffect(two.Hero, "Burn").Outcome != G::ApplyOutcome::Rejected);
    two.Frame();
    auto r = two.Saves().LoadTransfer("Carry", "Hero", Schema);
    CHECK(r.Success && !r.Recovered && r.Reason.empty());
    auto health = *two.Rules().Health(two.Hero);
    CHECK(health.Current == 88 && health.Shield == 0 && !health.Dead && health.Deaths == 0);
    auto bag = two.Rules().Inventory(two.Hero);
    CHECK(bag.Count("Coin") == 12 && bag.Count("Lamp") == 1 && bag.Count("Sword") == 0 && bag.Count("Charm") == 0);
    auto l = two.Loadout();
    CHECK(l.State.Equipped.at(G::EquipSlot::Weapon) == "Sword" && l.State.Equipped.at(G::EquipSlot::Trinket) == "Charm");
    CHECK(l.Stats[0] == 8 && l.Stats[3] == 80 && l.State.Energy == energy);
    CHECK(l.State.Abilities.at("Nova").Unlocked && l.State.Abilities.at("Mend").Uses == 1 && l.State.Abilities.at("Mend").Remaining == 0);
    CHECK(G::QuestWorld::For(two.World).XP(two.Hero) == 100 && two.Mode("Gate") == "Claimed" && two.Mode("Lamps") == "Active" && two.Mode("Boss") == "Locked");
    CHECK(G::QuestWorld::For(two.World).State(two.Hero, "Lamps")->Progress.Counts.at(0) == 1);
    CHECK(G::CombatWorld::For(two.World).Effect(two.Hero, "Burn").Instances == 0);
    // No reward or progress notice is replayed: the claimed quest stays claimed and grants nothing again.
    CHECK(!G::QuestWorld::For(two.World).Poll(two.Hero) && !G::LoadoutWorld::For(two.World).Poll(two.Hero));
    CHECK(!G::QuestWorld::For(two.World).Claim(two.Hero, "Gate", error) && two.Rules().Inventory(two.Hero).Count("Coin") == 12);
    // A transfer replaces (never adds to) the destination state: loading the same document again changes nothing.
    two.Frame();
    CHECK(two.Saves().LoadTransfer("Carry", "Hero", Schema).Success);
    CHECK(two.Rules().Inventory(two.Hero).Count("Coin") == 12 && G::QuestWorld::For(two.World).XP(two.Hero) == 100);
    // Cooldowns, grace and attack timers start from zero: Mend casts at once after the transfer.
    two.Rules().Damage(two.Hero, {}, 30, true, false, false);
    CHECK(two.Rules().Health(two.Hero)->Current == 58);
    CHECK(G::LoadoutWorld::For(two.World).Cast(two.Hero, two.Hero, "Mend", {}).Success);
    // The progressed hero can now start the boss quest (its prerequisite was carried).
    CHECK(G::QuestWorld::For(two.World).Start(two.Hero, "Boss", error));
    // Inspect reports the slot; the Blueprint nodes write/load/inspect with typed outputs.
    two.Frame();
    CHECK(two.Saves().InspectTransfer("Carry").Success);
    G::Runtime runtime;
    auto call = [&](const char* kind, std::map<std::string, Visual::Value> args) { return runtime.Dispatch(two.Hero, two.Hero, kind, args); };
    two.Frame();
    auto wrote = call("Gameplay.TransferWrite", { { "Key", "Hero" }, { "Slot", "Graph" }, { "Schema", Schema } });
    CHECK(wrote.at("Success") == true && wrote.at("Recovered") == false && wrote.at("Reason") == "");
    auto inspected = call("Gameplay.TransferInspect", { { "Slot", "Graph" } });
    CHECK(inspected.at("Success") == true);
    two.Frame();
    auto loaded = call("Gameplay.TransferLoad", { { "Key", "Hero" }, { "Slot", "Graph" }, { "Schema", "Other" } });
    CHECK(loaded.at("Success") == false && loaded.at("Reason") == "Gameplay transfer schema mismatch");
    two.World.OnRuntimeStop();
    std::filesystem::remove_all(root);
}
TEST(SaveTransferRejectsChangedContentDeadActorsUnboundKeysAndForgedStateTransactionally)
{
    const auto root = std::filesystem::temp_directory_path() / ("zamin-transfer-" + std::to_string(uint64_t(UUID())));
    std::filesystem::create_directories(root);
    SaveData::SetFilePath(root / "prefs.json");
    Level one("Boglar", 100);
    Play(one);
    auto t = one.Saves().CaptureTransfer("Hero", Schema);
    // Capture requires a bound, active, living actor.
    CHECK(Throws([&] { one.Saves().CaptureTransfer("Ghost", Schema); }));
    one.Rules().Damage(one.Hero, {}, 500, true, true, true);
    CHECK(one.Rules().Dead(one.Hero));
    CHECK(Throws([&] { one.Saves().CaptureTransfer("Hero", Schema); }));
    one.Frame();
    auto dead = one.Saves().WriteTransfer("Carry", "Hero", Schema);
    CHECK(!dead.Success && dead.Reason == "Transfer requires a living actor" && !std::filesystem::exists(root / "gameplay-transfers/Carry.json"));
    // A destination whose hero is configured differently (a bigger bag) rejects the document.
    Level wide("Yertola", 900, 12);
    auto r = wide.Saves().RestoreTransfer(t, "Hero", Schema);
    CHECK(!r.Success && r.Reason == "Transfer content changed");
    Level two("Yertola", 901);
    auto before = two.Rules().Inventory(two.Hero);
    for (auto [key, schema, reason] : { std::tuple{ "Ghost", Schema, "Gameplay transfer key mismatch" }, std::tuple{ "Hero", "V2", "Gameplay transfer schema mismatch" } })
    {
        auto bad = two.Saves().RestoreTransfer(t, key, schema);
        CHECK(!bad.Success && bad.Reason == reason);
    }
    // Forged late fields fail before the commit: nothing changes (bag, health, quests, loadout).
    auto forged = t;
    forged.Loadout->Equipped[G::EquipSlot::Body] = "Armor"; // an item this hero does not define
    CHECK(two.Saves().RestoreTransfer(forged, "Hero", Schema).Reason == "Invalid saved loadout");
    forged = t;
    forged.QuestBook.XP = 300;
    CHECK(two.Saves().RestoreTransfer(forged, "Hero", Schema).Reason == "Saved XP disagrees with claimed quests");
    forged = t;
    forged.QuestBook.Quests["Boss"].Mode = G::QuestMode::Active;
    forged.QuestBook.Quests["Gate"] = { G::QuestMode::Active, { 0 }, {} };
    forged.QuestBook.XP = 0;
    CHECK(two.Saves().RestoreTransfer(forged, "Hero", Schema).Reason == "Saved prerequisite not claimed");
    forged = t;
    forged.Health = 150;
    CHECK(two.Saves().RestoreTransfer(forged, "Hero", Schema).Reason == "Saved health exceeds configuration");
    CHECK(two.Rules().Inventory(two.Hero).Items == before.Items && two.Rules().Health(two.Hero)->Current == 100);
    CHECK(G::QuestWorld::For(two.World).XP(two.Hero) == 0 && two.Loadout().State.Equipped.empty() && two.Loadout().State.Energy == 60);
    // An unbound destination key and a dead destination hero.
    CHECK(G::SaveWorld::For(two.World).Unbind("Hero"));
    CHECK(two.Saves().RestoreTransfer(t, "Hero", Schema).Reason == "Transfer actor is not bound here");
    std::string error;
    CHECK(two.Saves().Bind(two.Hero, "Hero", error));
    two.Rules().Damage(two.Hero, {}, 500, true, true, true);
    CHECK(two.Saves().RestoreTransfer(t, "Hero", Schema).Reason == "Transfer target is dead");
    std::filesystem::remove_all(root);
}
TEST(SaveTransferBackupRecoveryAndSceneIOBudget)
{
    const auto root = std::filesystem::temp_directory_path() / ("zamin-transfer-" + std::to_string(uint64_t(UUID())));
    std::filesystem::create_directories(root);
    SaveData::SetFilePath(root / "prefs.json");
    Level one("Boglar", 100);
    Play(one);
    // Two writes per frame (shared with game saves), the third waits for the next frame.
    CHECK(one.Saves().WriteTransfer("Carry", "Hero", Schema).Success);
    std::string error;
    CHECK(one.Rules().ChangeItem(one.Hero, "Coin", 3, false, error));
    CHECK(one.Saves().WriteTransfer("Carry", "Hero", Schema).Success);
    auto busy = one.Saves().WriteTransfer("Carry", "Hero", Schema);
    CHECK(!busy.Success && busy.Reason == "Gameplay transfer I/O budget or busy");
    CHECK(!one.Saves().Save("Run1", Schema).Success);
    const auto file = root / "gameplay-transfers/Carry.json";
    CHECK(std::filesystem::exists(file.string() + ".bak"));
    // A corrupt primary falls back to the previous good document (12 coins) and reports the recovery.
    CHECK(FileSystem::WriteTextFile(file, "{broken"));
    Level two("Yertola", 900);
    auto inspected = two.Saves().InspectTransfer("Carry");
    CHECK(inspected.Success && inspected.Recovered);
    auto r = two.Saves().LoadTransfer("Carry", "Hero", Schema);
    CHECK(r.Success && r.Recovered && two.Rules().Inventory(two.Hero).Count("Coin") == 12);
    // Both invalid: a clear failure, nothing applied; a missing slot and unsafe slot names fail too.
    two.Frame();
    CHECK(FileSystem::WriteTextFile(file.string() + ".bak", "[]"));
    auto none = two.Saves().LoadTransfer("Carry", "Hero", Schema);
    CHECK(!none.Success && none.Reason == "Gameplay transfer and backup unavailable/invalid");
    CHECK(!two.Saves().LoadTransfer("../Carry", "Hero", Schema).Success);
    two.Frame();
    CHECK(!two.Saves().InspectTransfer("Missing").Success);
    CHECK(two.Rules().Inventory(two.Hero).Count("Coin") == 12);
    std::filesystem::remove_all(root);
}
