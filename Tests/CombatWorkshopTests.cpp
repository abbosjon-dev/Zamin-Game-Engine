// The compiler-free CombatWorkshop (Tools/samples/gen_combat_workshop.py): graphs validate, and the three QA phases
// (fresh run + v4 save, fresh-scene resume, no-replay) reach the generator's oracle in native state, headless.
#include "TestAssertions.h"
#include <Zamin/Core/Log.h>
#include <Zamin/Core/GameState.h>
#include <Zamin/Core/SaveData.h>
#include <Zamin/Gameplay/CombatWorld.h>
#include <Zamin/Gameplay/LoadoutWorld.h>
#include <Zamin/Gameplay/QuestWorld.h>
#include <Zamin/Gameplay/SaveModel.h>
#include <Zamin/Gameplay/SaveWorld.h>
#include <Zamin/Project/Project.h>
#include <Zamin/Scene/Entity.h>
#include <Zamin/Scene/SceneSerializer.h>
#include <Zamin/Scripting/Visual/VisualGraph.h>
#include <Zamin/Scripting/Visual/VisualGraphHost.h>
#include <Zamin/Utils/FileSystem.h>
#include <nlohmann/json.hpp>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
using namespace Zamin;
namespace {
    namespace G = Zamin::Gameplay;
    const std::filesystem::path WorkshopRoot() { return std::filesystem::path(ZAMIN_SOURCE_DIR) / "Projects/CombatWorkshop"; }
    struct Observed
    {
        double Sentry = 0, Player = 0, Shield = 0, Energy = 0, Attack = 0;
        double Dummy[3]{}, Hidden = 0;
        uint32_t SentryDeaths = 0, BurnStacks = 0, PoisonInstances = 0, FrostDummy1 = 0, FrostDummy3 = 0, PlayerEffects = 0;
        uint32_t BoltUses = 0, LanceUses = 0, NovaUses = 0, XP = 0, Coins = 0, Live = 0;
        std::string Log; // the graph's own Print lines and any engine warning/error of the run
    };
    Entity ByUUID(Scene& s, uint64_t id) { return s.GetEntityByUUID(UUID(id)); }
    Observed Observe(Scene& scene)
    {
        Observed o;
        auto& rules = G::RuleWorld::For(scene);
        auto& combat = G::CombatWorld::For(scene);
        auto player = ByUUID(scene, 800), sentry = ByUUID(scene, 850);
        auto health = [&](uint64_t id) { auto h = rules.Health(ByUUID(scene, id)); return h ? h->Current : -1.0; };
        o.Sentry = health(850);
        o.Player = health(800);
        if (auto h = rules.Health(player)) o.Shield = h->Shield;
        if (auto h = rules.Health(sentry)) o.SentryDeaths = h->Deaths;
        for (int i = 0; i < 3; ++i) o.Dummy[i] = health(861 + uint64_t(i));
        o.Hidden = health(871);
        if (auto state = G::LoadoutWorld::For(scene).State(player))
        {
            o.Energy = state->State.Energy;
            o.Attack = state->Stats[0];
            o.BoltUses = state->State.Abilities.at("Bolt").Uses;
            o.LanceUses = state->State.Abilities.at("Lance").Uses;
            o.NovaUses = state->State.Abilities.at("Nova").Uses;
        }
        o.BurnStacks = combat.Effect(sentry, "Burn").Stacks;
        o.PoisonInstances = combat.Effect(player, "Poison").Instances;
        o.FrostDummy1 = combat.Effect(ByUUID(scene, 861), "Frost").Instances;
        o.FrostDummy3 = combat.Effect(ByUUID(scene, 863), "Frost").Instances;
        o.PlayerEffects = combat.Modifiers(player).Instances;
        o.XP = G::QuestWorld::For(scene).XP(player);
        o.Coins = rules.Inventory(player).Count("Coin");
        o.Live = uint32_t(combat.LiveCount());
        return o;
    }
    void Print(const char* label, const Observed& o)
    {
        std::printf("  %s: sentry %.3f (deaths %u, burn %u) player %.3f/%.3f energy %.3f attack %.3f dummies %.3f/%.3f/%.3f hidden %.3f poison %u frost %u/%u effects %u uses %u/%u/%u xp %u coin %u live %u\n",
                    label, o.Sentry, o.SentryDeaths, o.BurnStacks, o.Player, o.Shield, o.Energy, o.Attack, o.Dummy[0], o.Dummy[1], o.Dummy[2], o.Hidden, o.PoisonInstances, o.FrostDummy1,
                    o.FrostDummy3, o.PlayerEffects, o.BoltUses, o.LanceUses, o.NovaUses, o.XP, o.Coins, o.Live);
    }
    // Runs one QA phase (Auto + Checks overrides, as the Editor/export workflow does) in a fresh scene for `frames`
    // frames at a fixed 1/30 s, observing native state after `checkFrame` frames.
    Observed RunPhase(const std::filesystem::path& project, const std::string& course, int phase, int checkFrame, int frames)
    {
        auto scenePath = project / "Assets/Scenes" / (course + ".zscene");
        auto data = nlohmann::json::parse(FileSystem::ReadTextFile(scenePath));
        for (auto& e : data["Entities"])
            if (e["Entity"] == 800) e["NativeScript"]["Fields"]["Overrides"] = { { "Type", "String" }, { "Value", nlohmann::json{ { "Auto", true }, { "Checks", true }, { "Phase", phase } }.dump() } };
        CHECK(FileSystem::WriteTextFile(scenePath, data.dump(2) + "\n"));
        GameState::Clear(); // a fresh process: only persisted state survives
        // Capture the run's log (the graph's Checks print COMBAT_* markers with the generator's oracle numbers).
        const auto level = Log::GetMinLevel();
        const bool terminal = Log::GetTerminalOutput();
        Log::SetMinLevel(LogLevel::Info);
        Log::SetTerminalOutput(false);
        const auto written = Log::GetTotalWritten();
        auto scene = CreateRef<Scene>();
        CHECK(SceneSerializer(scene).Deserialize(scenePath));
        scene->OnRuntimeStart();
        Observed seen;
        const bool trace = std::getenv("ZAMIN_COMBAT_TRACE") != nullptr;
        for (int frame = 1; frame <= frames; ++frame)
        {
            scene->UpdateRuntime(1.0f / 30);
            if (trace) Print(std::format("{} p{} f{}", course, phase, frame).c_str(), Observe(*scene));
            if (frame == checkFrame) seen = Observe(*scene);
        }
        scene->OnRuntimeStop();
        uint64_t first = 0;
        auto messages = Log::GetMessages(&first);
        for (size_t i = 0; i < messages.size(); ++i)
            if (first + i >= written && (messages[i].Level >= LogLevel::Warn || messages[i].Text.find("COMBAT_") != std::string::npos)) seen.Log += messages[i].Text + "\n";
        Log::SetMinLevel(level);
        Log::SetTerminalOutput(terminal);
        return seen;
    }
}
TEST(CombatWorkshopGraphsValidateAndRoundtrip)
{
    const auto root = WorkshopRoot();
    for (auto suffix : { "2D", "3D" })
    {
        auto g = Visual::Graph::Load(root / "Assets/Scripts" / (std::string("Combat") + suffix + ".zgraph"));
        CHECK(g.Validate().empty());
        CHECK(Visual::Graph::Parse(g.Serialize()).Validate().empty());
        for (auto key : { "Bolt", "Lance", "Venom" }) CHECK(std::filesystem::exists(root / "Assets/Prefabs" / (std::string(key) + suffix + ".zprefab")));
        auto scene = CreateRef<Scene>();
        CHECK(SceneSerializer(scene).Deserialize(root / "Assets/Scenes" / (std::string("Combat") + suffix + ".zscene")));
        for (uint64_t id : { 800, 850, 861, 862, 863, 870, 871, 1220, 1231 }) CHECK(scene->GetEntityByUUID(UUID(id)));
    }
}
// The generator's oracle (gen_combat_workshop.py): phase 0 checked at 2.45 s, phase 1 at 4.8 s, phase 2 at 2.0 s.
TEST(CombatWorkshopOracleHeadless)
{
    auto prior = Project::GetActive();
    for (std::string course : { "Combat2D", "Combat3D" })
    {
        const auto work = std::filesystem::temp_directory_path() / ("zamin-combat-workshop-" + std::to_string(uint64_t(UUID())));
        std::filesystem::create_directories(work);
        std::filesystem::copy(WorkshopRoot(), work / "CombatWorkshop", std::filesystem::copy_options::recursive);
        const auto project = work / "CombatWorkshop";
        // Headless there is no GL context, and instantiating a visual prefab resolves the scene's assets (font
        // atlases, meshes). Visuals are presentation only (excluded from the definition signature), so this copy
        // defines the projectiles without them; the Player/Editor workflows run the pooled visuals.
        for (auto suffix : { "2D", "3D" })
        {
            auto graph = project / "Assets/Scripts" / (std::string("Combat") + suffix + ".zgraph");
            auto json = nlohmann::json::parse(FileSystem::ReadTextFile(graph));
            for (auto& node : json["Nodes"])
                if (node["Kind"] == "Gameplay.ProjectileDefine") node["Inputs"]["Prefab"] = "";
            CHECK(FileSystem::WriteTextFile(graph, json.dump(2) + "\n"));
        }
        SaveData::SetFilePath(work / "home" / "prefs.json");
        CHECK(Project::Load(project / "CombatWorkshop.zproject") != nullptr);
        const auto savePath = work / "home/gameplay-saves/Combat.json";
        auto saved = [&]() {
            G::GameSave save;
            std::string error;
            CHECK(std::filesystem::exists(savePath));
            CHECK(G::DecodeGameSave(FileSystem::ReadTextFile(savePath), save, error));
            return save;
        };
        auto entity = [](const G::GameSave& save, const std::string& key) -> const G::SavedEntity* {
            for (auto& e : save.Entities)
                if (e.Key == key) return &e;
            return nullptr;
        };
        // Phase 0: Rally, Bolt, Lance (pierce), Nova (LoS), Bolt, F5 with a Bolt in flight; Sentry Venom -> Poison.
        auto p0 = RunPhase(project, course, 0, 75, 90);
        Print((course + " phase 0").c_str(), p0);
        CHECK(p0.Log == "[Graph Player] COMBAT_ACCEPTANCE sentry=45 burn=2 pierce=3 nova=2 hidden=30 poison=1 energy=25 save=4\n");
        if (p0.Log.find("COMBAT_ACCEPTANCE") == std::string::npos) std::printf("%s", p0.Log.c_str());
        CHECK(p0.Sentry == 45 && p0.BurnStacks == 2 && p0.SentryDeaths == 0);
        CHECK(p0.Player == 98 && p0.Shield == 24 && p0.PoisonInstances == 1);
        CHECK(p0.Dummy[0] == 13 && p0.Dummy[1] == 13 && p0.Dummy[2] == 35 && p0.Hidden == 30);
        CHECK(p0.FrostDummy1 == 1 && p0.FrostDummy3 == 0);
        CHECK(p0.Attack == 10 && p0.Energy == 25 && p0.BoltUses == 2 && p0.LanceUses == 1 && p0.NovaUses == 1);
        {
            auto save = saved();
            CHECK(save.Version == 4 && save.Entities.size() == 6);
            CHECK(save.Combat && save.Combat->Projectiles.size() == 1 && save.Combat->Projectiles[0].Key == "Bolt" && save.Combat->Projectiles[0].Owner == "Player");
            auto* hero = entity(save, "Player");
            auto* sentry = entity(save, "Sentry");
            CHECK(hero && sentry);
            if (hero && sentry)
            {
                bool poison = false, haste = false;
                for (auto& e : hero->Effects) poison |= e.Key == "Poison" && e.Remaining > 0 && e.Source == "Sentry", haste |= e.Key == "Haste";
                CHECK(poison && haste && hero->Health == 100 && hero->Shield == 24);
                CHECK(sentry->Effects.size() == 1 && sentry->Effects[0].Key == "Burn" && sentry->Health == 90);
                CHECK(hero->Loadout && hero->Loadout->Energy == 25 && hero->Loadout->Abilities.at("Bolt").Uses == 2);
            }
        }
        // Phase 1 (fresh scene): F9, the restored Bolt hits, Burn and Bolt 3 kill the Sentry, the reward is claimed.
        auto p1 = RunPhase(project, course, 1, 146, 150);
        Print((course + " phase 1").c_str(), p1);
        CHECK(p1.Log == "[Graph Player] COMBAT_RESUMED sentry=0 player=94 xp=150 coin=5 energy=15\n");
        if (p1.Log.find("COMBAT_RESUMED") == std::string::npos) std::printf("%s", p1.Log.c_str());
        CHECK(p1.Sentry == 0 && p1.SentryDeaths == 1 && p1.BurnStacks == 0);
        CHECK(p1.Player == 94 && p1.Shield == 24 && p1.PoisonInstances == 0);
        CHECK(p1.Attack == 0 && p1.Energy == 15 && p1.BoltUses == 3 && p1.XP == 150 && p1.Coins == 5);
        CHECK(p1.Dummy[0] == 13 && p1.Hidden == 30);
        {
            auto save = saved();
            auto* sentry = entity(save, "Sentry");
            CHECK(save.Version == 4 && save.Combat && save.Combat->Projectiles.empty() && sentry && sentry->Dead && sentry->Effects.empty());
        }
        // Phase 2 (fresh scene): F9, Bolt at the dead Sentry and Nova without energy change nothing.
        auto p2 = RunPhase(project, course, 2, 61, 62);
        Print((course + " phase 2").c_str(), p2);
        CHECK(p2.Log == "[Graph Player] COMBAT_NO_REPLAY bolts=3 xp=150 coin=5 effects=0\n");
        if (p2.Log.find("COMBAT_NO_REPLAY") == std::string::npos) std::printf("%s", p2.Log.c_str());
        CHECK(p2.Sentry == 0 && p2.SentryDeaths == 1 && p2.XP == 150 && p2.Coins == 5);
        CHECK(p2.Energy == 15 && p2.BoltUses == 3 && p2.NovaUses == 1 && p2.Player == 94 && p2.PlayerEffects == 0);
        CHECK(p2.Dummy[0] == 13 && p2.Dummy[2] == 35);
        Project::SetActive(prior);
        SaveData::SetFilePath({});
        GameState::Clear();
        std::error_code ec;
        std::filesystem::remove_all(work, ec);
    }
}
