// Chiroqbon (Projects/LanternKeeper, Tools/samples/gen_lantern_keeper.py): the compiler-free 2D vertical slice, played
// headless from the title menu through both levels to the victory screen by its demo pilot, with the real scene
// switches (LoadScene requests), the real transfer document between the levels and the real gameplay saves.
#include "TestAssertions.h"
#include <Zamin/Core/Log.h>
#include <Zamin/Core/GameState.h>
#include <Zamin/Core/InputActions.h>
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
        std::string Banner;             // the last banner text shown (entity 6091)
        glm::vec3 HeroAt{ 0 };          // the hero at the stop
        glm::vec3 StartAt{ 0 };         // the hero at the level start line (Continue)
        double PilotStep = -1;          // the demo pilot's route step at the stop
        float Health = 0;
        std::string RawHint;            // the first text that showed a control hint as written ("[Interact] ...")
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
        o.HeroAt = hero.GetTransform().Translation;
        if (auto* script = hero.TryGetComponent<NativeScriptComponent>())
            if (auto* host = dynamic_cast<VisualGraphHost*>(script->Instance); host && host->VM.Variables().contains("Step"))
                o.PilotStep = host->VM.Variables().at("Step").get<double>();
        if (auto h = rules.Health(hero))
        {
            o.Deaths = h->Deaths;
            o.Health = float(h->Current);
        }
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
    // returns true for a new log line, `frame` returns true after a frame (after its log lines) or `limit` frames pass.
    // `between` sees every scene switch (the scene asked for the next one; the files are what it wrote until then).
    Outcome Play(const std::string& start, int limit, const std::function<bool(const std::string&, Outcome&)>& stop = {},
                 const std::function<bool(Scene&, Outcome&)>& frame = {}, const std::function<void(const std::string&)>& between = {})
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
                    if (stop && stop(text, o))
                    {
                        done = true;
                        Observe(*scene, o);
                    }
                }
                seen = first + messages.size();
                if (auto banner = scene->GetEntityByUUID(UUID(6091)))
                    if (auto* text = banner.TryGetComponent<TextComponent>(); text && !text->Text.empty()) o.Banner = text->Text;
                // what the next frame draws: no text may show an input action's name instead of its key
                if (o.RawHint.empty())
                {
                    auto texts = scene->GetRegistry().view<TextComponent>();
                    for (auto e : texts)
                    {
                        auto& text = texts.get<TextComponent>(e);
                        text.UpdateLocalized();
                        if (auto token = FindUnexpandedInputToken(Project::GetInputActionMap(), text.GetDisplayText()); !token.empty())
                        {
                            o.RawHint = std::format("{} f{}: '{}'", Entity{ e, scene.get() }.GetName(), o.Frames, text.GetDisplayText());
                            break;
                        }
                    }
                }
                if (!done && frame && frame(*scene, o))
                {
                    done = true;
                    Observe(*scene, o);
                }
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
            if (!next.empty() && between) between(next);
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
    // the oracle is test data next to the project (Tests/), never an asset an export ships
    CHECK(!std::filesystem::exists(root / "Assets/Data/oracle.json"));
    auto oracle = J::parse(FileSystem::ReadTextFile(root / "Tests/oracle.json"));
    CHECK(oracle["XP"] == 850 && oracle["FinalLevel"] == 4 && oracle["Coins"].get<int>() > 10);
}
// Plan 0: New game from the menu -> Boglar (fights, chest, equip, shrine, chief, gate, lamp) -> level card -> transfer
// -> Yertola (carry, autosave, side rooms, shrine, boss phases, altar) -> victory.
SLOW_TEST(LanternKeeperOracleHeadless)
{
    const auto prior = Project::GetActive();
    const auto work = Prepare(0);
    const auto oracle = J::parse(FileSystem::ReadTextFile(work / "LanternKeeper/Tests/oracle.json"));
    auto o = Play("Scenes/Menu.zscene", 30 * 60 * 6, [](const std::string& line, Outcome&) { return line.find("SLICE_VICTORY") != std::string::npos; });
    std::printf("  frames %d scenes", o.Frames);
    for (auto& s : o.Scenes) std::printf(" %s", s.c_str());
    std::printf("\n  xp %u level %u coins %u lamps %u herbs %u claimed %u deaths %u sword %d charm %d armor %d nova %d mend %d boss %d projectiles %zu\n", o.XP, o.Level,
                o.Coins, o.Lamps, o.Herbs, o.Claimed, o.Deaths, o.Sword, o.Charm, o.Armor, o.Nova, o.Mend, o.BossDead, o.Projectiles);
    if (!o.Victory || !Clean(o.Log)) std::printf("%s\n", o.Log.c_str());
    CHECK(o.Victory && Clean(o.Log));
    // every prompt and banner of the whole run showed keys, never "[Interact]" / "[Nova]" as written
    if (!o.RawHint.empty()) std::printf("  raw control hint: %s\n", o.RawHint.c_str());
    CHECK(o.RawHint.empty());
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
SLOW_TEST(LanternKeeperLosePlanHeadless)
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
SLOW_TEST(LanternKeeperDeterminism)
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
    // ZAMIN_SLICE_TRACE_OUT=<file>: the trace for Tests/slice_workflow_test.py's D phase (the Player's level 1 trace must equal it)
    if (const char* out = std::getenv("ZAMIN_SLICE_TRACE_OUT"))
    {
        std::string text;
        for (const auto& line : traces[0]) text += line.substr(line.find("SLICE_TRACE")) + "\n";
        CHECK(FileSystem::WriteTextFile(out, text));
    }
    Project::SetActive(prior);
}
namespace {
    // A fresh process on the files in `from`: the HOME is replaced by them and GameState / SaveData are read again.
    void FreshProcess(const std::filesystem::path& home, const std::filesystem::path& from)
    {
        std::filesystem::remove_all(home);
        std::filesystem::copy(from, home, std::filesystem::copy_options::recursive);
        SaveData::Reload();
        GameState::Clear();
        GameState::Load();
    }
    void Keep(const std::filesystem::path& home, const std::filesystem::path& to)
    {
        std::filesystem::remove_all(to);
        std::filesystem::create_directories(to.parent_path());
        std::filesystem::copy(home, to, std::filesystem::copy_options::recursive);
    }
    // A torn write (the document ends mid-way) and a changed byte (the checksum no longer matches).
    void Tear(const std::filesystem::path& file) { CHECK(FileSystem::WriteTextFile(file, FileSystem::ReadTextFile(file).substr(0, 96))); }
    void Tamper(const std::filesystem::path& file)
    {
        auto text = FileSystem::ReadTextFile(file);
        const auto at = text.find("\"Health\":");
        CHECK(at != std::string::npos);
        text.insert(at + 9, "1");
        CHECK(FileSystem::WriteTextFile(file, text));
    }
    // The hero of a save document: XP, coins.
    std::pair<uint32_t, uint32_t> SavedHero(const std::filesystem::path& file)
    {
        G::GameSave save;
        std::string error;
        CHECK(G::DecodeGameSave(FileSystem::ReadTextFile(file), save, error));
        for (const auto& e : save.Entities)
            if (e.Key == "Hero") return { e.QuestBook.XP, uint32_t(e.Inventory.Count("Coin")) };
        return { 99999u, 99999u };
    }
    // Only the markers a run on broken files is allowed to show (an unreadable save says so, never crashes).
    bool CleanIO(const std::string& log)
    {
        for (auto bad : { "SLICE_CONFIG_FAILED", "SLICE_ORACLE_FAILED", "Visual graph '", "Instruction budget", "nomli harakat topilmadi", "SLICE_IO_FAILED autosave" })
            if (log.find(bad) != std::string::npos) return false;
        return true;
    }
    // The demo menu (Continue when it is offered, else New game) on a fresh process over `from`, until `after` frames
    // after the level's start line.
    Outcome Continue(const std::filesystem::path& home, const std::filesystem::path& from, int after = 12)
    {
        FreshProcess(home, from);
        int started = -1;
        return Play("Scenes/Menu.zscene", 30 * 40, {}, [&](Scene& scene, Outcome& o) {
            if (started < 0 && o.Log.find("SLICE_LEVEL_START") != std::string::npos)
            {
                started = o.Frames;
                if (auto hero = scene.GetEntityByUUID(UUID(Hero))) o.StartAt = hero.GetTransform().Translation;
            }
            return started >= 0 && o.Frames >= started + after;
        });
    }
}
// The crash/kill matrix of the run flow (docs/VerticalSlices.md, "Save and resume"): one normal run keeps the files a
// process killed at each step would leave behind - the level 1 exit's transfer written but not GameState, GameState
// written but not the level 2 autosave, the autosave written but not GameState, both written, the level 2 shrine - and
// a fresh process (the menu's Continue) on each must resume a consistent run with no reward granted twice; a damaged
// primary save falls back to its backup (recovered, said on screen), a damaged primary and backup to the level's entry
// (the transfer in level 2, a fresh level 1) with a visible message, never a crash or a half-applied state.
SLOW_TEST(LanternKeeperResumeMatrixHeadless)
{
    const auto prior = Project::GetActive();
    const auto work = Prepare(3);
    const auto home = work / "home", cut = work / "cuts";
    std::filesystem::path complete = cut / "l1-complete", switched = cut / "l2-switch", l2 = cut / "l2-start", shrine = cut / "l2-shrine";
    // the write run: menu -> Boglar (shrine) -> level card -> transfer -> Yertola (carry, autosave) -> shrine, pilot stops
    auto keep = [&](const std::string& line, Outcome&) {
        if (line.find("SLICE_LEVEL_COMPLETE lvl=1") != std::string::npos) Keep(home, complete);
        if (line.find("SLICE_LEVEL_START lvl=2") != std::string::npos) Keep(home, l2);
        if (line.find("SLICE_SAVED lvl=2 at=shrine") == std::string::npos) return false;
        Keep(home, shrine);
        return true;
    };
    auto switching = [&](const std::string& next) {
        if (next.find("Yertola") != std::string::npos) Keep(home, switched);
    };
    auto a = Play("Scenes/Menu.zscene", 30 * 60 * 5, keep, {}, switching);
    if (!Clean(a.Log)) std::printf("%s\n", a.Log.c_str());
    CHECK(Clean(a.Log) && a.Scenes.size() == 3 && std::filesystem::exists(shrine / "gameplay-saves/Run2.json"));
    CHECK(std::filesystem::exists(complete / "gameplay-saves/Run1.json") && !std::filesystem::exists(complete / "gameplay-transfers/Carry.json"));
    if (!std::filesystem::exists(shrine / "gameplay-saves/Run2.json"))
    {
        std::filesystem::remove_all(work);
        Project::SetActive(prior);
        return;
    }
    G::GameTransfer carry;
    std::string error;
    CHECK(G::DecodeGameTransfer(FileSystem::ReadTextFile(switched / "gameplay-transfers/Carry.json"), carry, error) && carry.QuestBook.XP == 200);
    const auto [shrineXP, shrineCoins] = SavedHero(complete / "gameplay-saves/Run1.json");
    const auto [l2XP, l2Coins] = SavedHero(l2 / "gameplay-saves/Run2.json");
    std::printf("  write run %d frames; level 1 shrine save xp %u coins %u; carry xp %u; level 2 autosave xp %u coins %u\n", a.Frames, shrineXP, shrineCoins,
                carry.QuestBook.XP, l2XP, l2Coins);
    CHECK(l2XP == 200 && shrineXP < 200);
    auto has = [](const Outcome& o, const char* marker) {
        if (o.Log.find(marker) != std::string::npos) return true;
        std::printf("    missing '%s' in:\n%s\n", marker, o.Log.c_str());
        return false;
    };

    // 1. killed after the transfer write, before GameState: Continue resumes level 1 from its shrine save; the exit is
    //    replayed and writes the same transfer again (no reward twice)
    {
        const auto files = cut / "after-transfer";
        Keep(complete, files);
        std::filesystem::copy(switched / "gameplay-transfers", files / "gameplay-transfers", std::filesystem::copy_options::recursive);
        FreshProcess(home, files);
        auto o = Play("Scenes/Menu.zscene", 30 * 60 * 3, [](const std::string& line, Outcome&) { return line.find("SLICE_RESUMED lvl=2") != std::string::npos; });
        std::printf("  after-transfer: resumed level 1 from its save (xp %u), exit again, level 2 xp %u coins %u\n", shrineXP, o.XP, o.Coins);
        CHECK(Clean(o.Log) && has(o, "Menyu: Asosiy davom=1") && has(o, "SLICE_RESUMED lvl=1 source=save recovered=0") && has(o, "SLICE_LEVEL_START lvl=1 entry=continue"));
        CHECK(has(o, "SLICE_RESUMED lvl=2 source=carry") && o.Log.find(" xp=200 ") != std::string::npos && o.XP == 200 && o.Coins == uint32_t(carry.Inventory.Count("Coin")));
        G::GameTransfer again;
        CHECK(G::DecodeGameTransfer(FileSystem::ReadTextFile(home / "gameplay-transfers/Carry.json"), again, error) && again.QuestBook.XP == 200 &&
              again.Inventory.Items == carry.Inventory.Items);
    }
    // 2. killed after GameState (level 2 asked for), before the level 2 autosave: Continue enters level 2 through the transfer
    // 3. killed after the level 2 autosave, before its GameState: the same (the autosave is written again)
    {
        const auto files = cut / "after-autosave";
        Keep(l2, files);
        std::filesystem::copy_file(switched / "prefs.json", files / "prefs.json", std::filesystem::copy_options::overwrite_existing);
        for (auto [name, from] : { std::pair{ "after-gamestate", switched }, std::pair{ "after-autosave", files } })
        {
            auto o = Continue(home, from);
            std::printf("  %s: entered level 2 through the transfer, xp %u coins %u hp %.0f\n", name, o.XP, o.Coins, o.Health);
            CHECK(Clean(o.Log) && has(o, "Menyu: Asosiy davom=1") && has(o, "SLICE_RESUMED lvl=2 source=carry recovered=0") && has(o, "SLICE_SAVED lvl=2 at=auto"));
            CHECK(has(o, "SLICE_LEVEL_START lvl=2 entry=continue") && o.XP == 200 && o.Coins == l2Coins && o.Scenes.back() == "Yertola");
            CHECK(GameState::GetFloat("LK.Run.Save", 0) == 2 && SavedHero(home / "gameplay-saves/Run2.json").first == 200);
        }
    }
    // 4. killed after the autosave and its GameState: Continue loads the level 2 save; the demo pilot starts level 2's
    //    route from its beginning (the entry save resets the route step - before, it kept level 1's shrine step and the
    //    pilot got stuck in a relocated export's resumed attract run)
    Outcome entry = Continue(home, l2);
    std::printf("  level 2 entry save: xp %u coins %u, pilot step %.0f\n", entry.XP, entry.Coins, entry.PilotStep);
    CHECK(Clean(entry.Log) && has(entry, "SLICE_RESUMED lvl=2 source=save recovered=0") && entry.XP == 200 && entry.Coins == l2Coins);
    CHECK(entry.PilotStep >= 0 && entry.PilotStep < 3);
    // 5. the shrine save damaged (a changed byte): its backup - the autosave before it - with the "recovered" banner
    {
        const auto files = cut / "tampered";
        Keep(shrine, files);
        Tamper(files / "gameplay-saves/Run2.json");
        auto o = Continue(home, files);
        std::printf("  tampered shrine save: backup restored at (%.2f, %.2f) xp %u coins %u lamps %u (the level 2 autosave: (%.2f, %.2f) coins %u lamps %u), banner '%s'\n",
                    o.StartAt.x, o.StartAt.y, o.XP, o.Coins, o.Lamps, entry.StartAt.x, entry.StartAt.y, entry.Coins, entry.Lamps, o.Banner.c_str());
        CHECK(Clean(o.Log) && has(o, "SLICE_RESUMED lvl=2 source=save recovered=1") && o.Banner == "Zaxira nusxadan tiklandi");
        CHECK(o.XP == 200 && o.Coins == entry.Coins && o.Lamps == entry.Lamps && glm::length(o.StartAt - entry.StartAt) < 0.001f);
    }
    // 6. the shrine save and its backup unreadable: the level starts again from the transfer, said on screen
    {
        const auto files = cut / "unreadable";
        Keep(shrine, files);
        Tear(files / "gameplay-saves/Run2.json");
        Tear(files / "gameplay-saves/Run2.json.bak");
        auto o = Continue(home, files);
        std::printf("  unreadable save and backup: carry again, xp %u, banner '%s'\n", o.XP, o.Banner.c_str());
        CHECK(CleanIO(o.Log) && has(o, "SLICE_IO_FAILED load") && has(o, "SLICE_RESUMED lvl=2 source=carry") && has(o, "SLICE_SAVED lvl=2 at=auto"));
        CHECK(o.Banner == "Saqlash o'qilmadi - daraja boshidan" && o.XP == 200 && o.Coins == l2Coins && SavedHero(home / "gameplay-saves/Run2.json").first == 200);
    }
    // 7. level 1's save and backup unreadable: a fresh level 1, said on screen
    {
        const auto files = cut / "unreadable-l1";
        Keep(complete, files);
        Tear(files / "gameplay-saves/Run1.json");
        if (std::filesystem::exists(files / "gameplay-saves/Run1.json.bak")) Tear(files / "gameplay-saves/Run1.json.bak");
        auto o = Continue(home, files);
        CHECK(CleanIO(o.Log) && has(o, "SLICE_IO_FAILED load") && has(o, "SLICE_SAVED lvl=1 at=auto") && has(o, "SLICE_LEVEL_START lvl=1 entry=continue"));
        CHECK(o.Banner == "Saqlash o'qilmadi - daraja boshidan" && o.XP == 0 && o.Coins == 0 && o.Scenes.back() == "Boglar");
    }
    // 8. level 2's save, its backup and the transfer (and its backup) all unreadable: a fresh level 2, said on screen
    {
        const auto files = cut / "unreadable-all";
        Keep(shrine, files);
        for (auto name : { "gameplay-saves/Run2.json", "gameplay-saves/Run2.json.bak", "gameplay-transfers/Carry.json", "gameplay-transfers/Carry.json.bak" })
            if (std::filesystem::exists(files / name)) Tear(files / name);
        auto o = Continue(home, files);
        CHECK(CleanIO(o.Log) && has(o, "SLICE_IO_FAILED load") && has(o, "SLICE_IO_FAILED transfer") && has(o, "SLICE_SAVED lvl=2 at=auto"));
        CHECK(o.Banner == "Saqlash o'qilmadi - daraja boshidan" && o.XP == 0 && o.Scenes.back() == "Yertola" && SavedHero(home / "gameplay-saves/Run2.json").first == 0);
    }
    std::filesystem::remove_all(work);
    Project::SetActive(prior);
}
namespace {
    // The saved health of the entity `key` in a save document (-1: not there).
    double SavedHealth(const std::filesystem::path& file, const std::string& key = "Hero")
    {
        G::GameSave save;
        std::string error;
        if (!G::DecodeGameSave(FileSystem::ReadTextFile(file), save, error)) return -1;
        for (const auto& e : save.Entities)
            if (e.Key == key) return e.Health;
        return -1;
    }
}
// A shrine rests the keeper and then saves: the save holds the healed keeper (a Continue or a defeat's retry starts
// at full health, as the live run goes on). The level exit's card pauses the world (no hit can land while the keeper
// waits) and its transfer is retried until it is written: a failed write (the transfer folder blocked) never leaves
// the card up for good.
SLOW_TEST(LanternKeeperShrineSaveAndLevelExitCardHoldUp)
{
    const auto prior = Project::GetActive();
    const auto work = Prepare(0);
    const auto home = work / "home", cut = work / "cuts/shrine";
    std::filesystem::create_directories(home);
    CHECK(FileSystem::WriteTextFile(home / "gameplay-transfers", "a file where the transfer folder goes"));
    bool hurt = false, unblocked = false, switched = false, healthChanged = false;
    int complete = -1, failed = -1, cardFrames = 0, pausedFrames = 0;
    double shrineSaved = -1, shrineLive = -1, hurtTo = -1, cardHealth = -1;
    auto o = Play(
        "Scenes/Boglar.zscene", 30 * 60 * 3,
        [&](const std::string& line, Outcome& o) {
            if (line.find("SLICE_SAVED lvl=1 at=shrine") != std::string::npos && shrineSaved < 0)
            {
                shrineSaved = SavedHealth(home / "gameplay-saves/Run1.json");
                Keep(home, cut);
            }
            if (line.find("SLICE_LEVEL_COMPLETE lvl=1") != std::string::npos) complete = o.Frames;
            if (line.find("SLICE_IO_FAILED transfer") != std::string::npos && failed < 0) failed = o.Frames;
            return line.find("SLICE_RESUMED lvl=2 source=carry") != std::string::npos;
        },
        [&](Scene& scene, Outcome&) {
            auto hero = scene.GetEntityByUUID(UUID(Hero));
            if (!hero || switched) return false;
            auto& rules = G::RuleWorld::For(scene);
            // wounded on the way into the shrine (as a fight before it would)
            if (auto shrine = scene.GetEntityByUUID(UUID(5601)); !hurt && shrine &&
                glm::distance(glm::vec2(hero.GetTransform().Translation), glm::vec2(shrine.GetTransform().Translation)) < 2.5f)
            {
                rules.Damage(hero, {}, 45, false, true, true);
                hurtTo = rules.Health(hero)->Current;
                hurt = true;
            }
            if (shrineSaved >= 0 && shrineLive < 0) shrineLive = rules.Health(hero)->Current;
            if (complete >= 0)
            {
                ++cardFrames;
                pausedFrames += scene.IsGamePaused() ? 1 : 0;
                const double h = rules.Health(hero)->Current;
                if (cardHealth < 0) cardHealth = h;
                healthChanged |= h != cardHealth;
            }
            if (failed >= 0 && !unblocked)
            {
                std::filesystem::remove(home / "gameplay-transfers"); // the disk is writable again
                unblocked = true;
            }
            return false;
        },
        [&](const std::string&) { switched = true; });
    std::printf("  hurt to %.0f, shrine save %.0f (live %.0f); card %d frames (%d paused), transfer failed at %d after the exit at %d, scenes %zu\n", hurtTo,
                shrineSaved, shrineLive, cardFrames, pausedFrames, failed, complete, o.Scenes.size());
    CHECK(hurt && hurtTo < 100 && shrineSaved == 100 && shrineLive == 100);
    CHECK(complete > 0 && failed > complete && unblocked && o.Scenes.size() == 2 && o.Scenes.back() == "Yertola");
    CHECK(cardFrames > 60 && pausedFrames == cardFrames && !healthChanged);
    CHECK(o.Log.find("SLICE_IO_FAILED transfer") != std::string::npos && o.Log.find("SLICE_RESUMED lvl=2 source=carry") != std::string::npos);
    // a fresh process on the shrine save: Continue restores the rested keeper
    if (std::filesystem::exists(cut / "gameplay-saves/Run1.json"))
    {
        auto c = Continue(home, cut);
        std::printf("  Continue from the shrine save: health %.0f\n", c.Health);
        CHECK(Clean(c.Log) && c.Log.find("SLICE_RESUMED lvl=1 source=save recovered=0") != std::string::npos && c.Health == 100);
    }
    std::filesystem::remove_all(work);
    Project::SetActive(prior);
}
// A save in the middle of the Soya Bek fight (phase 2, one spirit killed and gone): Continue resumes phase 2 calmly -
// the killed spirit stays down, no second rage banner, shake or phase-2 trace (before, the phase flag was not saved
// and the phase-2 branch ran again, waking every spirit, the dead ones too).
SLOW_TEST(LanternKeeperBossFightResumesInPhaseTwo)
{
    const auto prior = Project::GetActive();
    const auto work = Prepare(0);
    const auto home = work / "home", cut = work / "cuts/boss";
    constexpr uint64_t Wisp = 5070;
    int phase2 = -1, killed = -1;
    bool quit = false;
    auto a = Play(
        "Scenes/Menu.zscene", 30 * 60 * 6,
        [&](const std::string& line, Outcome& o) {
            if (line.find("SLICE_TRACE ev=boss a=phase2") != std::string::npos && phase2 < 0) phase2 = o.Frames;
            if (line.find("SLICE_SAVED lvl=2 at=quit") == std::string::npos) return false;
            Keep(home, cut);
            return true;
        },
        [&](Scene& scene, Outcome& o) {
            auto wisp = scene.GetEntityByUUID(UUID(Wisp));
            if (phase2 < 0 || !wisp || quit) return false;
            if (killed < 0 && scene.IsActiveInHierarchy(wisp) && o.Frames > phase2 + 10)
            {
                G::RuleWorld::For(scene).Damage(wisp, scene.GetEntityByUUID(UUID(Hero)), 999, true, true, true);
                scene.SetActive(wisp, false); // the corpse removed, as the enemy graph does 1.4 s later
                killed = o.Frames;
            }
            else if (killed >= 0)
            {
                ScriptMessage message;
                message.Name = "SaveQuit"; // the pause menu's Saqlab, bosh menyuga
                message.Payload = std::string();
                scene.SendMessage(scene.GetEntityByUUID(UUID(1100)), message);
                quit = true;
            }
            return false;
        });
    const double bossSaved = SavedHealth(cut / "gameplay-saves/Run2.json", "L2-Boss"), wispSaved = SavedHealth(cut / "gameplay-saves/Run2.json", "L2-Wisp-1");
    std::printf("  phase 2 at frame %d, spirit killed at %d, quit save: boss %.0f, spirit %.0f\n", phase2, killed, bossSaved, wispSaved);
    CHECK(Clean(a.Log) && phase2 > 0 && killed > phase2 && bossSaved > 0 && bossSaved < 240 * 0.55 && wispSaved == 0);
    // a fresh process: Continue enters the saved fight
    FreshProcess(home, cut);
    int started = -1;
    bool wispBack = false;
    std::string banners;
    auto b = Play("Scenes/Menu.zscene", 30 * 40, {}, [&](Scene& scene, Outcome& o) {
        if (started < 0 && o.Log.find("SLICE_LEVEL_START lvl=2") != std::string::npos) started = o.Frames;
        if (auto wisp = scene.GetEntityByUUID(UUID(Wisp)); wisp && started >= 0) wispBack |= scene.IsActiveInHierarchy(wisp);
        if (auto banner = scene.GetEntityByUUID(UUID(6091)))
            if (auto* text = banner.TryGetComponent<TextComponent>(); text && !text->Text.empty() && banners.find(text->Text) == std::string::npos) banners += text->Text + "|";
        return started >= 0 && o.Frames >= started + 90;
    });
    const auto resumed = b.Log.find("SLICE_TRACE ev=boss a=phase2-resumed");
    std::printf("  resumed: banners '%s', killed spirit back %d, calm phase 2 %d\n", banners.c_str(), wispBack, resumed != std::string::npos);
    CHECK(Clean(b.Log) && b.Log.find("SLICE_RESUMED lvl=2 source=save") != std::string::npos && resumed != std::string::npos);
    CHECK(b.Log.find("SLICE_TRACE ev=boss a=phase2\n") == std::string::npos && banners.find("Soya Bek g'azablandi!") == std::string::npos && !wispBack);
    std::filesystem::remove_all(work);
    Project::SetActive(prior);
}
