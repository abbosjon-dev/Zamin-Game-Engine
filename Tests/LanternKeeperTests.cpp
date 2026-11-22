// Chiroqbon (Projects/LanternKeeper, Tools/samples/gen_lantern_keeper.py): the compiler-free 2D vertical slice, played
// headless from the title menu through both levels to the victory screen by its demo pilot, with the real scene
// switches (LoadScene requests), the real transfer document between the levels and the real gameplay saves.
#include "TestAssertions.h"
#include <Zamin/Core/Log.h>
#include <Zamin/Core/GameState.h>
#include <Zamin/Core/SaveData.h>
#include <Zamin/Gameplay/CombatWorld.h>
#include <Zamin/Gameplay/GameplayRules.h>
#include <Zamin/Gameplay/LoadoutWorld.h>
#include <Zamin/Gameplay/QuestWorld.h>
#include <Zamin/Gameplay/SaveModel.h>
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
#include <functional>
using namespace Zamin;
namespace {
    namespace G = Zamin::Gameplay;
    using J = nlohmann::json;
    std::filesystem::path SliceRoot() { return std::filesystem::path(ZAMIN_SOURCE_DIR) / "Projects/LanternKeeper"; }
    constexpr uint64_t Hero = 1000;
    struct Outcome
    {
        std::string Log;                 // SLICE_* / Menyu markers and every warning or error
        std::vector<std::string> Scenes; // the scenes played, in order
        bool Victory = false, Defeat = false;
        int Frames = 0;
        // the hero at the victory marker
        uint32_t XP = 0, Level = 0, Coins = 0, Lamps = 0, Herbs = 0, Claimed = 0, Deaths = 0;
        bool Sword = false, Armor = false, Charm = false, Nova = false, Mend = false, BossDead = false;
        size_t Projectiles = 99;
        std::vector<std::string> Trace; // SLICE_TRACE lines (determinism)
    };
    // A private copy of the project: the pilot on, demo menu, and (headless, no GL context) projectile definitions
    // without their visual prefabs (presentation only, outside the combat signature).
    std::filesystem::path Prepare(int plan, bool demoMenu = true)
    {
        const auto work = std::filesystem::temp_directory_path() / ("zamin-lantern-" + std::to_string(uint64_t(UUID())));
        std::filesystem::create_directories(work);
        std::filesystem::copy(SliceRoot(), work / "LanternKeeper", std::filesystem::copy_options::recursive);
        const auto project = work / "LanternKeeper";
        auto graph = project / "Assets/Scripts/Hero.zgraph";
        auto json = J::parse(FileSystem::ReadTextFile(graph));
        for (auto& node : json["Nodes"])
            if (node["Kind"] == "Gameplay.ProjectileDefine") node["Inputs"]["Prefab"] = "";
        CHECK(FileSystem::WriteTextFile(graph, json.dump() + "\n"));
        for (auto name : { "Boglar", "Yertola" })
        {
            auto path = project / "Assets/Scenes" / (std::string(name) + ".zscene");
            auto scene = J::parse(FileSystem::ReadTextFile(path));
            for (auto& e : scene["Entities"])
                if (e["Entity"] == Hero)
                {
                    auto overrides = J::parse(e["NativeScript"]["Fields"]["Overrides"]["Value"].get<std::string>());
                    overrides["Pilot"] = true;
                    overrides["Plan"] = plan;
                    e["NativeScript"]["Fields"]["Overrides"]["Value"] = overrides.dump();
                }
            CHECK(FileSystem::WriteTextFile(path, scene.dump() + "\n"));
        }
        if (demoMenu)
        {
            auto path = project / "Assets/Scenes/Menu.zscene";
            auto scene = J::parse(FileSystem::ReadTextFile(path));
            for (auto& e : scene["Entities"])
                if (e["Entity"] == 1100) e["NativeScript"]["Fields"]["Overrides"] = { { "Type", "String" }, { "Value", J{ { "Demo", true } }.dump() } };
            CHECK(FileSystem::WriteTextFile(path, scene.dump() + "\n"));
        }
        SaveData::SetFilePath(work / "home" / "prefs.json");
        GameState::Clear();
        CHECK(Project::Load(project / "LanternKeeper.zproject") != nullptr);
        return work;
    }
    void Observe(Scene& scene, Outcome& o)
    {
        auto hero = scene.GetEntityByUUID(UUID(Hero));
        if (!hero) return;
        auto& rules = G::RuleWorld::For(scene);
        auto bag = rules.Inventory(hero);
        o.Coins = uint32_t(bag.Count("Coin"));
        o.Lamps = uint32_t(bag.Count("Lamp"));
        o.Herbs = uint32_t(bag.Count("Herb"));
        if (auto h = rules.Health(hero)) o.Deaths = h->Deaths;
        auto& quests = G::QuestWorld::For(scene);
        o.XP = quests.XP(hero);
        o.Level = quests.Progression(hero).Level;
        o.Claimed = 0;
        for (auto key : { "Gate", "Herbs", "Lamps", "Boss", "Dawn" })
            if (auto s = quests.State(hero, key); s && s->Progress.Mode == G::QuestMode::Claimed) ++o.Claimed;
        if (auto l = G::LoadoutWorld::For(scene).State(hero))
        {
            auto equipped = [&](const char* item) {
                for (auto& [slot, key] : l->State.Equipped)
                    if (key == item) return true;
                return false;
            };
            o.Sword = equipped("Qilich");
            o.Charm = equipped("Tumor");
            o.Armor = equipped("Zirh");
            o.Nova = l->State.Abilities.at("Nova").Unlocked;
            o.Mend = l->State.Abilities.at("Mend").Unlocked;
        }
        if (auto boss = scene.GetEntityByUUID(UUID(5060)))
            if (auto h = rules.Health(boss)) o.BossDead = h->Dead;
        if (auto* combat = scene.GetRegistry().ctx().find<G::CombatWorld>()) o.Projectiles = combat->LiveCount();
    }
    // Plays from `start` (a scene path inside Assets) following LoadScene requests, at a fixed 1/30 s, until `stop`
    // returns true for a new log line or `limit` frames pass.
    Outcome Play(const std::string& start, int limit, const std::function<bool(const std::string&, Outcome&)>& stop = {})
    {
        Outcome o;
        const auto level = Log::GetMinLevel();
        const bool terminal = Log::GetTerminalOutput();
        Log::SetMinLevel(LogLevel::Info);
        Log::SetTerminalOutput(std::getenv("ZAMIN_SLICE_LOG") != nullptr);
        uint64_t seen = Log::GetTotalWritten();
        const int trace = std::getenv("ZAMIN_SLICE_TRACE") ? std::atoi(std::getenv("ZAMIN_SLICE_TRACE")) : 0;
        std::string path = start;
        bool done = false;
        while (!done && o.Frames < limit)
        {
            auto scene = CreateRef<Scene>();
            const auto file = Project::GetAssetDirectory() / path;
            CHECK(SceneSerializer(scene).Deserialize(file));
            o.Scenes.push_back(scene->GetName());
            scene->OnRuntimeStart();
            std::string next;
            while (o.Frames < limit && !done)
            {
                scene->UpdateRuntime(1.0f / 30);
                ++o.Frames;
                uint64_t first = 0;
                auto messages = Log::GetMessages(&first);
                for (size_t i = 0; i < messages.size(); ++i)
                {
                    if (first + i < seen) continue;
                    const auto& text = messages[i].Text;
                    const bool marker = text.find("SLICE_") != std::string::npos || text.find("Menyu:") != std::string::npos;
                    if (marker || messages[i].Level >= LogLevel::Warn) o.Log += text + "\n";
                    if (text.find("SLICE_TRACE") != std::string::npos) o.Trace.push_back(scene->GetName() + ": " + text.substr(text.find("SLICE_TRACE")));
                    if (text.find("SLICE_VICTORY") != std::string::npos)
                    {
                        o.Victory = true;
                        Observe(*scene, o);
                    }
                    if (text.find("SLICE_DEFEAT") != std::string::npos) o.Defeat = true;
                    if (stop && stop(text, o)) done = true;
                }
                seen = first + messages.size();
                if (trace && o.Frames % trace == 0)
                    if (auto hero = scene->GetEntityByUUID(UUID(Hero)))
                    {
                        auto p = hero.GetTransform().Translation;
                        auto* script = hero.TryGetComponent<NativeScriptComponent>();
                        auto* host = script ? dynamic_cast<VisualGraphHost*>(script->Instance) : nullptr;
                        auto h = G::RuleWorld::For(*scene).Health(hero);
                        std::printf("  %s f%d hero (%.2f, %.2f) hp %.1f step %s err '%s'\n", scene->GetName().c_str(), o.Frames, p.x, p.y, h ? h->Current : -1.0,
                                    host ? host->VM.Variables().at("Step").dump().c_str() : "-", host ? host->VM.Error().c_str() : "");
                    }
                if (!scene->GetPendingSceneLoad().empty())
                {
                    next = scene->GetPendingSceneLoad().generic_string();
                    break;
                }
            }
            if (!done && o.Victory) Observe(*scene, o);
            scene->OnRuntimeStop();
            if (next.empty()) break;
            path = next;
        }
        Log::SetMinLevel(level);
        Log::SetTerminalOutput(terminal);
        return o;
    }
    // stuck: whether a stuck pilot is an error (the lose plan walks to the locked gate without the chief's key).
    bool Clean(const std::string& log, bool stuck = true)
    {
        for (auto bad : { "SLICE_CONFIG_FAILED", "SLICE_IO_FAILED", "SLICE_BOT_STUCK", "SLICE_ORACLE_FAILED", "Visual graph '", "Instruction budget", "nomli harakat topilmadi" })
            if ((stuck || std::string(bad) != "SLICE_BOT_STUCK") && log.find(bad) != std::string::npos) return false;
        return true;
    }
}
TEST(LanternKeeperGraphsValidateAndScenesLoad)
{
    const auto root = SliceRoot();
    size_t nodes = 0;
    for (auto name : { "Hero", "Game", "Enemy", "Menu" })
    {
        auto g = Visual::Graph::Load(root / "Assets/Scripts" / (std::string(name) + ".zgraph"));
        auto diagnostics = g.Validate();
        for (auto& d : diagnostics) std::printf("    %s node %u: %s\n", name, d.Node, d.Message.c_str());
        CHECK(diagnostics.empty());
        CHECK(Visual::Graph::Parse(g.Serialize()).Validate().empty());
        CHECK(g.Nodes.size() <= 2500);
        nodes += g.Nodes.size();
    }
    CHECK(nodes > 1000);
    for (auto name : { "Menu", "Boglar", "Yertola" })
    {
        auto scene = CreateRef<Scene>();
        CHECK(SceneSerializer(scene).Deserialize(root / "Assets/Scenes" / (std::string(name) + ".zscene")));
        CHECK(scene->GetEntityByUUID(UUID(1100)) && scene->GetEntityByUUID(UUID(6000)));
        if (std::string(name) != "Menu") CHECK(scene->GetEntityByUUID(UUID(Hero)) && scene->GetEntityByUUID(UUID(2000)) && scene->GetEntityByUUID(UUID(2001)));
    }
    auto oracle = J::parse(FileSystem::ReadTextFile(root / "Assets/Data/oracle.json"));
    CHECK(oracle["XP"] == 850 && oracle["FinalLevel"] == 4 && oracle["Coins"].get<int>() > 10);
}
// Plan 0: New game from the menu -> Boglar (fights, chest, equip, shrine, chief, gate, lamp) -> level card -> transfer
// -> Yertola (carry, autosave, side rooms, shrine, boss phases, altar) -> victory.
TEST(LanternKeeperOracleHeadless)
{
    const auto prior = Project::GetActive();
    const auto work = Prepare(0);
    const auto oracle = J::parse(FileSystem::ReadTextFile(work / "LanternKeeper/Assets/Data/oracle.json"));
    auto o = Play("Scenes/Menu.zscene", 30 * 60 * 6, [](const std::string& line, Outcome&) { return line.find("SLICE_VICTORY") != std::string::npos; });
    std::printf("  frames %d scenes", o.Frames);
    for (auto& s : o.Scenes) std::printf(" %s", s.c_str());
    std::printf("\n  xp %u level %u coins %u lamps %u herbs %u claimed %u deaths %u sword %d charm %d armor %d nova %d mend %d boss %d projectiles %zu\n", o.XP, o.Level,
                o.Coins, o.Lamps, o.Herbs, o.Claimed, o.Deaths, o.Sword, o.Charm, o.Armor, o.Nova, o.Mend, o.BossDead, o.Projectiles);
    if (!o.Victory || !Clean(o.Log)) std::printf("%s\n", o.Log.c_str());
    CHECK(o.Victory && Clean(o.Log));
    CHECK(o.Scenes.size() >= 3 && o.Scenes[0] == "Menu" && o.Scenes[1] == "Boglar" && o.Scenes[2] == "Yertola");
    CHECK(o.XP == 850 && o.Level == 4 && o.Claimed == 5);
    CHECK(o.Coins == oracle["Coins"].get<uint32_t>() && o.Lamps == 0 && o.Herbs == 3);
    CHECK(o.Sword && o.Charm && o.Armor && o.Nova && o.Mend && o.BossDead && o.Projectiles == 0);
    for (auto marker : { "Menyu: Asosiy", "Menyu: Yangi o'yin", "SLICE_LEVEL_START lvl=1 entry=new", "SLICE_SAVED lvl=1 at=auto", "SLICE_SAVED lvl=1 at=shrine",
                         "SLICE_LEVEL_COMPLETE lvl=1", "SLICE_RESUMED lvl=2 source=carry", "SLICE_LEVEL_START lvl=2 entry=carry", "SLICE_SAVED lvl=2 at=shrine",
                         "SLICE_TRACE ev=boss a=engaged", "SLICE_TRACE ev=boss a=phase2", "SLICE_TRACE ev=boss a=defeated", "SLICE_VICTORY game=Chiroqbon" })
    {
        if (o.Log.find(marker) == std::string::npos) std::printf("    missing: %s\n", marker);
        CHECK(o.Log.find(marker) != std::string::npos);
    }
    // The transfer document of the level exit: v1, the hero's key, and a valid quest book (Gate + Herbs claimed).
    G::GameTransfer transfer;
    std::string error;
    CHECK(G::DecodeGameTransfer(FileSystem::ReadTextFile(work / "home/gameplay-transfers/Carry.json"), transfer, error));
    CHECK(transfer.Key == "Hero" && transfer.Scene == "Boglar" && transfer.QuestBook.XP == 200);
    // The level 2 save is v4 (combat present) and the run is finished: the menu offers no Continue.
    G::GameSave save;
    CHECK(G::DecodeGameSave(FileSystem::ReadTextFile(work / "home/gameplay-saves/Run2.json"), save, error) && save.Version == 4 && save.Scene == "Yertola");
    CHECK(GameState::GetFloat("LK.Run.Finished", 0) == 1 && GameState::GetFloat("LK.Best.Time", 0) > 0);
    std::filesystem::remove_all(work);
    Project::SetActive(prior);
}
// Plan 1 (lose): the pilot walks into the bandits without fighting - three deaths end the run on the defeat screen;
// the demo picks "Oxirgi saqlashdan", the level reloads from its save (no reward is granted again) and the pilot,
// now fighting, completes level 1 with exactly the normal run's progress.
TEST(LanternKeeperLosePlanHeadless)
{
    const auto prior = Project::GetActive();
    const auto work = Prepare(1);
    auto o = Play("Scenes/Menu.zscene", 30 * 60 * 5, [](const std::string& line, Outcome&) { return line.find("SLICE_LEVEL_COMPLETE") != std::string::npos; });
    std::printf("  frames %d scenes %zu\n", o.Frames, o.Scenes.size());
    if (!Clean(o.Log, false) || o.Log.find("SLICE_LEVEL_COMPLETE") == std::string::npos) std::printf("%s\n", o.Log.c_str());
    CHECK(Clean(o.Log, false) && o.Defeat);
    // after the reload the pilot fights: nothing is stuck any more
    CHECK(o.Log.find("SLICE_BOT_STUCK", o.Log.find("SLICE_RESUMED")) == std::string::npos);
    CHECK(o.Scenes.size() == 3 && o.Scenes[1] == "Boglar" && o.Scenes[2] == "Boglar");
    const auto defeat = o.Log.find("SLICE_DEFEAT lvl=1 deaths=3"), resumed = o.Log.find("SLICE_RESUMED lvl=1 source=save"),
               complete = o.Log.find("SLICE_LEVEL_COMPLETE lvl=1");
    CHECK(defeat != std::string::npos && resumed != std::string::npos && complete != std::string::npos && defeat < resumed && resumed < complete);
    CHECK(o.Log.find("Menyu: Oxirgi saqlashdan") != std::string::npos);
    // No replay: the same 200 XP and 13 coins as a run without the defeat.
    CHECK(o.Log.find(" xp=200 ", complete) != std::string::npos && o.Log.find(" coins=13 ", complete) != std::string::npos);
    CHECK(GameState::GetFloat("LK.Run.Lost", 0) == 1);
    std::filesystem::remove_all(work);
    Project::SetActive(prior);
}
// Fixed frame time: two fresh runs of level 1 produce the same ordered gameplay trace at the same frames.
TEST(LanternKeeperDeterminism)
{
    const auto prior = Project::GetActive();
    std::vector<std::string> traces[2];
    int frames[2]{};
    for (int run = 0; run < 2; ++run)
    {
        const auto work = Prepare(0, false);
        auto o = Play("Scenes/Boglar.zscene", 30 * 60 * 3, [](const std::string& line, Outcome&) { return line.find("SLICE_LEVEL_COMPLETE") != std::string::npos; });
        CHECK(Clean(o.Log) && o.Log.find("SLICE_LEVEL_COMPLETE lvl=1") != std::string::npos);
        traces[run] = o.Trace;
        frames[run] = o.Frames;
        std::filesystem::remove_all(work);
    }
    std::printf("  frames %d / %d, trace lines %zu / %zu\n", frames[0], frames[1], traces[0].size(), traces[1].size());
    CHECK(frames[0] == frames[1] && traces[0] == traces[1] && traces[0].size() > 20);
    Project::SetActive(prior);
}
