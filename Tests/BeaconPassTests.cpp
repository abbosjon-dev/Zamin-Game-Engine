// Dovon mash'ali (Projects/BeaconPass, Tools/samples/gen_beacon_pass.py): the compiler-free 3D vertical slice, played
// headless from the title menu to the victory screen by its demo pilot, with the real scene switch (LoadScene request),
// real gameplay saves and the real Animator: the humanoids' glTF models are read without GPU objects
// (AsyncLoader::DeferGPU), so the locomotion blend, the layers and the clips' Hit/Release/Footstep events run as in the
// Player.
#include "TestAssertions.h"
#include <Zamin/Asset/AsyncLoader.h>
#include <Zamin/Core/Log.h>
#include <Zamin/Core/GameState.h>
#include <Zamin/Core/Input.h>
#include <Zamin/Core/InputActions.h>
#include <Zamin/Core/SaveData.h>
#include <Zamin/Gameplay/CombatWorld.h>
#include <Zamin/Gameplay/GameplayAI.h>
#include <Zamin/Gameplay/GameplayRules.h>
#include <Zamin/Gameplay/GameplayUI.h>
#include <Zamin/Gameplay/LoadoutWorld.h>
#include <Zamin/Gameplay/QuestWorld.h>
#include <Zamin/Gameplay/SaveModel.h>
#include <Zamin/Gameplay/SaveWorld.h>
#include <Zamin/Project/Project.h>
#include <Zamin/Renderer/Model.h>
#include <Zamin/Scene/Entity.h>
#include <Zamin/Scene/SceneSerializer.h>
#include <Zamin/Scene/Terrain.h>
#include <Zamin/Scripting/Visual/VisualGraph.h>
#include <Zamin/Scripting/Visual/VisualGraphHost.h>
#include <Zamin/Utils/FileSystem.h>
#include <nlohmann/json.hpp>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <functional>
#include <map>
#include <optional>
#include <set>
using namespace Zamin;
namespace {
    namespace G = Zamin::Gameplay;
    using J = nlohmann::json;
    std::filesystem::path SliceRoot() { return std::filesystem::path(ZAMIN_SOURCE_DIR) / "Projects/BeaconPass"; }
    constexpr uint64_t Hero = 1000, CameraId = 1003, Warden = 5060;
    constexpr uint16_t ViewMask = (1 << 1) | (1 << 2); // World | Wall
    // What the hero's Animator, body and camera did over a run (BeaconPassLocomotionHeadless and the oracle).
    struct Motion
    {
        float MaxWalk = 0, MaxRun = 0, MaxUpper = 0;
        bool Air = false, Land = false, Dead = false, Hurt = false, AttackWhileUpper = false;
        int AirToLand = 0;
        // Footsteps against distance at a steady gait (one clip's weight >= 0.97, within 8 % of its threshold speed): the
        // distance covered between two consecutive footsteps that both fall inside one unbroken steady stretch
        double WalkDistance = 0, RunDistance = 0;
        uint32_t WalkSteps = 0, RunSteps = 0;
        // the damped Speed parameter against the measured speed at a steady pace (0.6 s without change)
        float SpeedError = 0;
        int SpeedSamples = 0;
        // character controller clearance over the terrain (feet - terrain), camera inside the level geometry
        float MinClearance = 1e9f;
        int TerrainSamples = 0, CameraInside = 0, CameraSamples = 0;
    };
    struct Outcome
    {
        std::string Log;
        std::vector<std::string> Scenes;
        bool Victory = false, Defeat = false;
        int Frames = 0;
        uint32_t XP = 0, Level = 0, Stones = 0, Claimed = 0, Deaths = 0;
        bool Sword = false, Shield = false, Slam = false, WardenDead = false;
        size_t Projectiles = 99;
        std::vector<std::string> Trace, Timed;
        glm::vec3 HeroAt{ 0 };
        Motion M;
        Ref<Scene> Live; // the scene at the stop when Play was asked to keep it running
        std::string RawHint; // the first text that showed a control hint as written ("[Interact] ...")
    };
    // A private copy of the project: the pilot on (plan), the demo menu, and (headless, no GL context) projectile
    // definitions without their visual prefabs (presentation only, outside the combat signature).
    std::filesystem::path Prepare(int plan, bool demoMenu = true)
    {
        const auto work = std::filesystem::temp_directory_path() / ("zamin-beacon-" + std::to_string(uint64_t(UUID())));
        std::filesystem::create_directories(work);
        std::filesystem::copy(SliceRoot(), work / "BeaconPass", std::filesystem::copy_options::recursive);
        const auto project = work / "BeaconPass";
        auto graph = project / "Assets/Scripts/Hero.zgraph";
        auto json = J::parse(FileSystem::ReadTextFile(graph));
        for (auto& node : json["Nodes"])
            if (node["Kind"] == "Gameplay.ProjectileDefine") node["Inputs"]["Prefab"] = "";
        CHECK(FileSystem::WriteTextFile(graph, json.dump() + "\n"));
        auto path = project / "Assets/Scenes/Dovon.zscene";
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
        if (demoMenu)
        {
            auto menu = project / "Assets/Scenes/Menu.zscene";
            auto doc = J::parse(FileSystem::ReadTextFile(menu));
            for (auto& e : doc["Entities"])
                if (e["Entity"] == 1100) e["NativeScript"]["Fields"]["Overrides"] = { { "Type", "String" }, { "Value", J{ { "Demo", true } }.dump() } };
            CHECK(FileSystem::WriteTextFile(menu, doc.dump() + "\n"));
        }
        SaveData::SetFilePath(work / "home" / "prefs.json");
        GameState::Clear();
        CHECK(Project::Load(project / "BeaconPass.zproject") != nullptr);
        return work;
    }
    // The animated humanoids' models, read on the CPU only (the meshes keep their data for a later upload that never
    // comes headless): the Animator needs the skeleton and the clips.
    std::set<std::filesystem::path> s_Loaded;
    void LoadModels(Scene& scene)
    {
        AsyncLoader::DeferGPU deferred;
        for (auto e : scene.GetRegistry().view<AnimatorComponent, MeshRendererComponent>())
        {
            auto& mr = scene.GetRegistry().get<MeshRendererComponent>(e);
            if (mr.Type != PrimitiveMesh::Model || mr.ModelPath.empty()) continue;
            const auto path = Project::GetAssetFileSystemPath(mr.ModelPath);
            mr.ModelAsset = Model::Load(path);
            s_Loaded.insert(path);
        }
    }
    // Drops the CPU-only models from the cache (their meshes cancel their queued uploads): nothing uploads them later.
    void ReleaseModels()
    {
        for (const auto& path : s_Loaded) Model::ReleaseIfUnused(path);
        s_Loaded.clear();
    }
    void Observe(Scene& scene, Outcome& o)
    {
        auto hero = scene.GetEntityByUUID(UUID(Hero));
        if (!hero) return;
        o.HeroAt = scene.GetWorldPosition(hero);
        auto& rules = G::RuleWorld::For(scene);
        o.Stones = uint32_t(rules.Inventory(hero).Count("FireStone"));
        if (auto h = rules.Health(hero)) o.Deaths = h->Deaths;
        auto& quests = G::QuestWorld::For(scene);
        o.XP = quests.XP(hero);
        o.Level = quests.Progression(hero).Level;
        o.Claimed = 0;
        for (auto key : { "Pass", "Stones", "Warden", "Beacon" })
            if (auto s = quests.State(hero, key); s && s->Progress.Mode == G::QuestMode::Claimed) ++o.Claimed;
        if (auto l = G::LoadoutWorld::For(scene).State(hero))
        {
            auto equipped = [&](const char* item) {
                for (auto& [slot, key] : l->State.Equipped)
                    if (key == item) return true;
                return false;
            };
            o.Sword = equipped("Shamshir");
            o.Shield = equipped("Qalqon");
            o.Slam = l->State.Abilities.at("Zilzila").Unlocked;
        }
        if (auto warden = scene.GetEntityByUUID(UUID(Warden)))
            if (auto h = rules.Health(warden)) o.WardenDead = h->Dead;
        if (auto* combat = scene.GetRegistry().ctx().find<G::CombatWorld>()) o.Projectiles = combat->LiveCount();
    }
    // Per frame in the level: the hero's Animator (blend weights, states, layers), its footsteps against the distance it
    // walked, the Speed parameter against its measured speed, its feet over the terrain, the camera against walls.
    struct Sampler
    {
        glm::vec3 Last{ 0 };
        bool Has = false;
        float Steady = 0, LastSpeed = 0;
        uint32_t LastSteps = 0;
        std::string LastState;
        // per gait: the distance since the last footstep, and whether that footstep began the current steady stretch
        double Since[2] = { 0, 0 };
        bool Open[2] = { false, false };
        void Step(Scene& scene, Motion& m, float dt)
        {
            auto hero = scene.GetEntityByUUID(UUID(Hero));
            if (!hero) return;
            auto& anim = hero.GetComponent<AnimatorComponent>();
            auto& steps = hero.GetComponent<FootstepsComponent>();
            const auto p = scene.GetWorldPosition(hero);
            const std::string state = anim.GetCurrentStateName();
            if (state == "Air") m.Air = true;
            if (LastState == "Air" && state == "Land") ++m.AirToLand, m.Land = true;
            if (state == "Dead") m.Dead = true;
            LastState = state;
            float walk = 0, run = 0;
            if (state == "Locomotion" && anim.BlendWeights.size() == 3) walk = anim.BlendWeights[1], run = anim.BlendWeights[2];
            m.MaxWalk = std::max(m.MaxWalk, walk);
            m.MaxRun = std::max(m.MaxRun, run);
            for (auto& l : anim.Layers)
            {
                if (l.Name == "UpperBody")
                {
                    m.MaxUpper = std::max(m.MaxUpper, l.EffectiveWeight);
                    if (l.Clip == "Attack" && l.EffectiveWeight > 0.99f) m.AttackWhileUpper = true;
                }
                if (l.Name == "Hit" && l.Clip == "Hurt") m.Hurt = true;
            }
            const uint32_t stepCount = steps.StepCount;
            if (Has && dt > 0)
            {
                const float d = glm::length(glm::vec2(p.x - Last.x, p.z - Last.z));
                const float speed = d / dt;
                const auto* grounded = anim.FindParameter("Grounded");
                const bool onGround = grounded && grounded->Value != 0;
                const bool moving = onGround && state == "Locomotion" && speed > 0.5f && d < 1.0f;
                // a steady gait near the clip's own speed (an Idle or Run share would change the cycle length; a
                // single slower frame on a slope does not end the stretch)
                const bool steady[2] = { moving && walk >= 0.97f && std::abs(speed - 1.8f) < 0.15f, moving && run >= 0.97f && std::abs(speed - 5.4f) < 0.43f };
                const uint32_t stepped = stepCount - LastSteps;
                for (int gait = 0; gait < 2; ++gait)
                {
                    if (!steady[gait])
                    {
                        Since[gait] = 0, Open[gait] = false;
                        continue;
                    }
                    Since[gait] += d;
                    if (stepped == 0) continue;
                    if (Open[gait] && stepped == 1)
                    {
                        (gait == 0 ? m.WalkDistance : m.RunDistance) += Since[gait];
                        ++(gait == 0 ? m.WalkSteps : m.RunSteps);
                    }
                    Since[gait] = 0, Open[gait] = true;
                }
                Steady = std::abs(speed - LastSpeed) < 0.02f ? Steady + dt : 0;
                if (Steady > 0.6f && onGround && state == "Locomotion")
                    if (const auto* param = anim.FindParameter("Speed"))
                    {
                        m.SpeedError = std::max(m.SpeedError, std::abs(param->Value - speed));
                        ++m.SpeedSamples;
                    }
                LastSpeed = speed;
            }
            LastSteps = stepCount;
            Last = p;
            Has = true;
            // feet over the terrain (the ledge, the beacon platform and its stairs stand above it)
            for (auto e : scene.GetRegistry().view<TerrainComponent>())
            {
                Entity t{ e, &scene };
                const auto origin = scene.GetWorldPosition(t);
                const float ground = TerrainTools::SampleHeight(t.GetComponent<TerrainComponent>(), p.x - origin.x, p.z - origin.z) + origin.y;
                m.MinClearance = std::min(m.MinClearance, p.y - ground);
                ++m.TerrainSamples;
            }
            if (auto camera = scene.GetEntityByUUID(UUID(CameraId)))
            {
                const auto c = scene.GetWorldPosition(camera);
                bool inside = false;
                for (auto hit : scene.OverlapSphere3D(c, 0.08f, ViewMask, false))
                    if (hit != hero) inside = true;
                m.CameraInside += inside ? 1 : 0;
                ++m.CameraSamples;
            }
        }
    };
    // Plays from `start` (a scene path inside Assets) following LoadScene requests, at a fixed 1/30 s, until `stop`
    // returns true for a new log line, `frame` returns true after a frame (the level scene, after its log lines) or
    // `limit` frames pass.
    Outcome Play(const std::string& start, int limit, const std::function<bool(const std::string&, Outcome&)>& stop = {}, bool sample = false,
                 bool keep = false, Ref<Scene> resume = nullptr, const std::function<bool(Scene&, Outcome&)>& frame = {})
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
        Sampler sampler;
        while (!done && o.Frames < limit)
        {
            auto scene = resume;
            if (scene)
                resume = nullptr; // continue a scene a previous Play kept running
            else
            {
                scene = CreateRef<Scene>();
                const auto file = Project::GetAssetDirectory() / path;
                CHECK(SceneSerializer(scene).Deserialize(file));
                LoadModels(*scene);
                scene->OnRuntimeStart();
            }
            o.Scenes.push_back(scene->GetName());
            std::string next;
            while (o.Frames < limit && !done)
            {
                scene->UpdateRuntime(1.0f / 30);
                ++o.Frames;
                if (sample && scene->GetName() == "Dovon") sampler.Step(*scene, o.M, 1.0f / 30);
                uint64_t first = 0;
                auto messages = Log::GetMessages(&first);
                for (size_t i = 0; i < messages.size(); ++i)
                {
                    if (first + i < seen) continue;
                    const auto& text = messages[i].Text;
                    const bool marker = text.find("SLICE_") != std::string::npos || text.find("Menyu:") != std::string::npos;
                    if (marker || messages[i].Level >= LogLevel::Warn) o.Log += text + "\n";
                    if (text.find("SLICE_TRACE") != std::string::npos) o.Trace.push_back(text.substr(text.find("SLICE_TRACE")));
                    if (text.find("SLICE_TRACE") != std::string::npos) o.Timed.push_back(std::format("f{} {}", o.Frames, text.substr(text.find("SLICE_TRACE"))));
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
                if (!done && frame && scene->GetName() == "Dovon" && frame(*scene, o))
                {
                    done = true;
                    Observe(*scene, o);
                }
                if (trace && o.Frames % trace == 0 && scene->GetName() == "Dovon")
                    if (auto hero = scene->GetEntityByUUID(UUID(Hero)))
                    {
                        auto p = hero.GetTransform().Translation;
                        auto* script = hero.TryGetComponent<NativeScriptComponent>();
                        auto* host = script ? dynamic_cast<VisualGraphHost*>(script->Instance) : nullptr;
                        auto h = G::RuleWorld::For(*scene).Health(hero);
                        std::string vars;
                        if (host)
                            for (auto name : { "Step", "MoveIn", "Bolt", "Attack", "Victim", "Busy", "Pending", "Stuck" })
                                if (host->VM.Variables().contains(name)) vars += std::string(" ") + name + "=" + host->VM.Variables().at(name).dump();
                        for (uint64_t id : { 5040ull, 5050ull, 5060ull })
                            if (auto enemy = scene->GetEntityByUUID(UUID(id)))
                            {
                                auto eh = G::RuleWorld::For(*scene).Health(enemy);
                                auto* escript = enemy.TryGetComponent<NativeScriptComponent>();
                                auto* ehost = escript ? dynamic_cast<VisualGraphHost*>(escript->Instance) : nullptr;
                                auto ep = enemy.GetTransform().Translation;
                                auto ai = G::AIWorld::For(*scene).State(enemy);
                                vars += std::format(" | e{} ({:.1f},{:.1f},{:.1f}) hp {:.0f} {} vis {} err '{}'", id, ep.x, ep.y, ep.z, eh ? eh->Current : -1.0,
                                                    ai ? G::BrainName(ai->Brain.Mode) : "-", ai ? ai->Visible : false, ehost ? ehost->VM.Error() : "");
                            }
                        auto& la = hero.GetComponent<AnimatorComponent>().Layers[0];
                        vars += std::format(" | upper {} t {:.2f} w {:.2f} | ts {:.2f}", la.Clip, la.Time, la.EffectiveWeight, scene->GetTimeScale());
                        std::printf("  %s f%d hero (%.2f, %.2f, %.2f) hp %.1f state %s%s err '%s'\n", scene->GetName().c_str(), o.Frames, p.x, p.y, p.z,
                                    h ? h->Current : -1.0, hero.GetComponent<AnimatorComponent>().GetCurrentStateName(), vars.c_str(),
                                    host ? host->VM.Error().c_str() : "");
                    }
                if (!scene->GetPendingSceneLoad().empty())
                {
                    next = scene->GetPendingSceneLoad().generic_string();
                    break;
                }
            }
            if (!done && o.Victory) Observe(*scene, o);
            if (done && keep)
            {
                o.Live = scene;
                break;
            }
            scene->OnRuntimeStop();
            if (next.empty()) break;
            path = next;
        }
        Log::SetMinLevel(level);
        Log::SetTerminalOutput(terminal);
        return o;
    }
    bool Clean(const std::string& log, bool stuck = true)
    {
        for (auto bad : { "SLICE_CONFIG_FAILED", "SLICE_IO_FAILED", "SLICE_BOT_STUCK", "SLICE_ORACLE_FAILED", "Visual graph '", "Instruction budget", "is not a known action" })
            if ((stuck || std::string(bad) != "SLICE_BOT_STUCK") && log.find(bad) != std::string::npos) return false;
        return true;
    }
    void Finish(const std::filesystem::path& work, const Ref<Project>& prior)
    {
        ReleaseModels();
        std::filesystem::remove_all(work);
        Project::SetActive(prior);
    }
}
TEST(BeaconPassGraphsValidateAndScenesLoad)
{
    const auto root = SliceRoot();
    size_t nodes = 0;
    for (auto name : { "Hero", "Camera3D", "Game", "Enemy", "Menu" })
    {
        auto g = Visual::Graph::Load(root / "Assets/Scripts" / (std::string(name) + ".zgraph"));
        auto diagnostics = g.Validate();
        for (auto& d : diagnostics) std::printf("    %s node %u: %s\n", name, d.Node, d.Message.c_str());
        CHECK(diagnostics.empty());
        CHECK(Visual::Graph::Parse(g.Serialize()).Validate().empty());
        CHECK(g.Nodes.size() <= 2500);
        nodes += g.Nodes.size();
    }
    CHECK(nodes > 1200);
    for (auto name : { "Menu", "Dovon" })
    {
        auto scene = CreateRef<Scene>();
        CHECK(SceneSerializer(scene).Deserialize(root / "Assets/Scenes" / (std::string(name) + ".zscene")));
        CHECK(scene->GetEntityByUUID(UUID(1100)) && scene->GetEntityByUUID(UUID(6000)) && scene->GetEntityByUUID(UUID(CameraId)) && scene->GetEntityByUUID(UUID(2000)));
        if (std::string(name) == "Dovon")
        {
            auto hero = scene->GetEntityByUUID(UUID(Hero));
            CHECK(hero && hero.HasComponent<CharacterController3DComponent>() && hero.HasComponent<AnimatorComponent>() && hero.HasComponent<FootstepsComponent>());
            auto& anim = hero.GetComponent<AnimatorComponent>();
            CHECK(anim.States.size() == 4 && anim.Layers.size() == 2 && anim.Events.size() == 6);
            CHECK(anim.States[0].Motions.size() == 3 && anim.States[0].Motions[1].Threshold == 1.8f && anim.States[0].Motions[2].Threshold == 5.4f);
            CHECK(scene->GetEntityByUUID(UUID(2001)) && scene->GetEntityByUUID(UUID(Warden)));
        }
    }
    // the humanoid's clips: Walk and Run cover their strides at the blend thresholds
    {
        AsyncLoader::DeferGPU deferred;
        const auto path = root / "Assets/Models/Hero.glb";
        auto model = Model::Load(path);
        CHECK(model && model->IsSkinned() && model->GetSkeleton().Joints.size() == 21);
        for (auto [clip, duration] : std::vector<std::pair<const char*, float>>{ { "Walk", 2.0f / 1.8f }, { "Run", 3.9f / 5.4f } })
        {
            const int i = model->FindAnimation(clip);
            CHECK(i >= 0);
            if (i >= 0) CHECK_NEAR(model->GetAnimations()[size_t(i)].Duration, duration, 0.04f);
        }
        for (auto clip : { "Idle", "Fall", "Land", "Attack", "Cast", "Hurt", "Death" }) CHECK(model->FindAnimation(clip) >= 0);
        model = nullptr;
        CHECK(Model::ReleaseIfUnused(path));
    }
    // the oracle is test data next to the project (Tests/), never an asset an export ships
    CHECK(!std::filesystem::exists(root / "Assets/Data/oracle.json"));
    auto oracle = J::parse(FileSystem::ReadTextFile(root / "Tests/oracle.json"));
    CHECK(oracle["XP"] == 800 && oracle["FinalLevel"] == 4 && oracle["Stones"] == 3);
}
// Plan 0: New game from the menu -> Dovon (walk to the camp chest, equip, the ledge jump, the gate guards and the gate,
// the courtyard, the shrine save, the stable, the plateau shrine, the Warden, the beacon stairs) -> victory.
SLOW_TEST(BeaconPassOracleHeadless)
{
    const auto prior = Project::GetActive();
    const auto work = Prepare(0);
    auto o = Play("Scenes/Menu.zscene", 30 * 60 * 8, [](const std::string& line, Outcome&) { return line.find("SLICE_VICTORY") != std::string::npos; }, true);
    std::printf("  frames %d scenes", o.Frames);
    for (auto& s : o.Scenes) std::printf(" %s", s.c_str());
    std::printf("\n  xp %u level %u stones %u claimed %u deaths %u sword %d shield %d slam %d warden %d projectiles %zu\n", o.XP, o.Level, o.Stones, o.Claimed,
                o.Deaths, o.Sword, o.Shield, o.Slam, o.WardenDead, o.Projectiles);
    std::printf("  walk %.2f run %.2f upper %.2f air %d land %d hurt %d | steps walk %u/%.1f m run %u/%.1f m | speed err %.3f (%d) | clearance %.3f (%d) camera inside %d/%d\n",
                o.M.MaxWalk, o.M.MaxRun, o.M.MaxUpper, o.M.Air, o.M.AirToLand, o.M.Hurt, o.M.WalkSteps, o.M.WalkDistance, o.M.RunSteps, o.M.RunDistance,
                o.M.SpeedError, o.M.SpeedSamples, o.M.MinClearance, o.M.TerrainSamples, o.M.CameraInside, o.M.CameraSamples);
    if (!o.Victory || !Clean(o.Log)) std::printf("%s\n", o.Log.c_str());
    CHECK(o.Victory && Clean(o.Log));
    // every prompt and banner of the whole run showed keys, never "[Interact]" / "[Slam]" as written
    if (!o.RawHint.empty()) std::printf("  raw control hint: %s\n", o.RawHint.c_str());
    CHECK(o.RawHint.empty());
    CHECK(o.Scenes.size() >= 2 && o.Scenes[0] == "Menu" && o.Scenes[1] == "Dovon");
    CHECK(o.XP == 800 && o.Level == 4 && o.Claimed == 4 && o.Stones == 0);
    CHECK(o.Sword && o.Shield && o.Slam && o.WardenDead && o.Projectiles == 0);
    for (auto marker : { "Menyu: Asosiy", "Menyu: Yangi o'yin", "SLICE_LEVEL_START lvl=1 entry=new", "SLICE_SAVED lvl=1 at=auto", "SLICE_SAVED lvl=1 at=shrine",
                         "SLICE_TRACE ev=claim a=Pass", "SLICE_TRACE ev=claim a=Stones", "SLICE_TRACE ev=boss a=engaged", "SLICE_TRACE ev=boss a=defeated",
                         "SLICE_TRACE ev=beacon a=lit", "SLICE_VICTORY game=Dovon" })
    {
        if (o.Log.find(marker) == std::string::npos) std::printf("    missing: %s\n", marker);
        CHECK(o.Log.find(marker) != std::string::npos);
    }
    G::GameSave save;
    std::string error;
    CHECK(G::DecodeGameSave(FileSystem::ReadTextFile(work / "home/gameplay-saves/Run1.json"), save, error) && save.Version == 4 && save.Scene == "Dovon");
    CHECK(GameState::GetFloat("BP.Run.Finished", 0) == 1 && GameState::GetFloat("BP.Best.Time", 0) > 0);
    Finish(work, prior);
}
namespace {
    void PrintMotion(const Motion& m)
    {
        std::printf("  walk %.2f run %.2f upper %.2f air %d land %d dead %d hurt %d | steps walk %u/%.1f m run %u/%.1f m | speed err %.3f (%d) | clearance %.3f "
                    "(%d) camera inside %d/%d\n",
                    m.MaxWalk, m.MaxRun, m.MaxUpper, m.Air, m.AirToLand, m.Dead, m.Hurt, m.WalkSteps, m.WalkDistance, m.RunSteps, m.RunDistance, m.SpeedError,
                    m.SpeedSamples, m.MinClearance, m.TerrainSamples, m.CameraInside, m.CameraSamples);
    }
    // What a save restores, read from the live scene: the hero, the enemies, the interactables, combat in flight.
    struct Snapshot
    {
        glm::vec3 Hero{ 0 };
        float Health = 0;
        uint32_t XP = 0, Stones = 0, Potions = 0;
        std::vector<std::pair<uint64_t, float>> Enemies; // alive ones: key -> health
        std::vector<uint64_t> Used;                      // inactive interactables
        size_t Projectiles = 0;
        std::string Quests, Loadout; // Loadout: energy and each ability's cooldown left / uses / unlocked
    };
    Snapshot Take(Scene& scene)
    {
        Snapshot s;
        auto hero = scene.GetEntityByUUID(UUID(Hero));
        auto& rules = G::RuleWorld::For(scene);
        s.Hero = scene.GetWorldPosition(hero);
        s.Health = rules.Health(hero)->Current;
        s.XP = G::QuestWorld::For(scene).XP(hero);
        s.Stones = uint32_t(rules.Inventory(hero).Count("FireStone"));
        s.Potions = uint32_t(rules.Inventory(hero).Count("Potion"));
        for (uint64_t id = 5000; id <= 5060; id += 10)
            if (auto e = scene.GetEntityByUUID(UUID(id)); e && e.IsActiveInHierarchy())
                if (auto h = rules.Health(e); h && !h->Dead) s.Enemies.push_back({ id, h->Current });
        for (uint64_t id = 5500; id < 5630; id += 4)
            if (auto e = scene.GetEntityByUUID(UUID(id)); e && !e.IsActiveInHierarchy()) s.Used.push_back(id);
        if (auto* combat = scene.GetRegistry().ctx().find<G::CombatWorld>()) s.Projectiles = combat->LiveCount();
        for (auto key : { "Pass", "Stones", "Warden", "Beacon" })
            if (auto q = G::QuestWorld::For(scene).State(hero, key)) s.Quests += std::string(key) + "=" + std::to_string(int(q->Progress.Mode)) + " ";
        if (auto l = G::LoadoutWorld::For(scene).State(hero))
        {
            s.Loadout = std::format("energy {:.1f}", l->State.Energy);
            for (const auto& [key, p] : l->State.Abilities) s.Loadout += std::format(" {} {:.2f}/{}/{}", key, p.Remaining, p.Uses, int(p.Unlocked));
        }
        return s;
    }
}
// The Animator during the first part of a run (camp -> ledge jump -> gate fight -> courtyard -> shrine): the blend
// reaches pure Walk and pure Run, the jump enters Air and lands through Land, strikes raise the UpperBody layer to 1,
// hits play the additive Hurt; footsteps follow the distance at the stride; the damped Speed parameter follows the
// measured speed; the character controller never sinks into the terrain; the orbit camera never ends inside a wall.
SLOW_TEST(BeaconPassLocomotionHeadless)
{
    const auto prior = Project::GetActive();
    const auto work = Prepare(0, false);
    auto o = Play("Scenes/Dovon.zscene", 30 * 60 * 3, [](const std::string& line, Outcome&) { return line.find("SLICE_SAVED lvl=1 at=shrine") != std::string::npos; }, true);
    std::printf("  frames %d\n", o.Frames);
    PrintMotion(o.M);
    if (!Clean(o.Log)) std::printf("%s\n", o.Log.c_str());
    CHECK(Clean(o.Log) && o.Log.find("SLICE_SAVED lvl=1 at=shrine") != std::string::npos);
    const auto& m = o.M;
    CHECK(m.MaxWalk >= 0.9f && m.MaxRun >= 0.9f && m.Air && m.Land && m.AirToLand >= 1 && m.MaxUpper >= 0.99f && m.AttackWhileUpper && m.Hurt);
    // two footsteps per gait cycle: the mean distance between footsteps is half the stride, within 10 %
    CHECK(m.WalkSteps >= 6 && m.RunSteps >= 8);
    const double walkStep = m.WalkDistance / std::max(1u, m.WalkSteps), runStep = m.RunDistance / std::max(1u, m.RunSteps);
    std::printf("  footstep spacing: walk %.3f m (half stride 1.000), run %.3f m (half stride 1.950)\n", walkStep, runStep);
    CHECK(std::abs(walkStep - 2.0 / 2) <= 0.1 * (2.0 / 2) && std::abs(runStep - 3.9 / 2) <= 0.1 * (3.9 / 2));
    CHECK(m.SpeedSamples > 100 && m.SpeedError <= 0.05f);
    CHECK(m.TerrainSamples > 500 && m.MinClearance >= -0.05f);
    CHECK(m.CameraSamples > 500 && m.CameraInside == 0);
    Finish(work, prior);
}
// Plan 1 (lose): the pilot does not fight or drink until a defeat - three deaths end the run on the defeat screen (the
// Dead state plays); the demo picks "Oxirgi saqlashdan", the level reloads from the autosave and the pilot, now
// fighting, wins the pass with exactly the normal reward (no replay).
SLOW_TEST(BeaconPassLosePlanHeadless)
{
    const auto prior = Project::GetActive();
    const auto work = Prepare(1);
    int claims = 0;
    auto o = Play("Scenes/Menu.zscene", 30 * 60 * 6, [&](const std::string& line, Outcome&) { return line.find("SLICE_TRACE ev=claim a=Pass") != std::string::npos && ++claims; },
                  true);
    std::printf("  frames %d scenes %zu xp %u level %u deaths %u\n", o.Frames, o.Scenes.size(), o.XP, o.Level, o.Deaths);
    PrintMotion(o.M);
    if (!Clean(o.Log, false) || !o.Defeat) std::printf("%s\n", o.Log.c_str());
    CHECK(Clean(o.Log, false) && o.Defeat && o.M.Dead);
    CHECK(o.Scenes.size() == 3 && o.Scenes[1] == "Dovon" && o.Scenes[2] == "Dovon");
    const auto defeat = o.Log.find("SLICE_DEFEAT lvl=1 deaths=3"), retry = o.Log.find("Menyu: Oxirgi saqlashdan"), resumed = o.Log.find("SLICE_RESUMED lvl=1 source=save"),
               pass = o.Log.find("SLICE_TRACE ev=claim a=Pass");
    CHECK(defeat != std::string::npos && retry > defeat && resumed > retry && pass > resumed);
    CHECK(o.Log.find("SLICE_BOT_STUCK", resumed) == std::string::npos);
    // the reloaded run: Pass's 100 XP once, the deaths of the lost run are not in the save
    CHECK(o.XP == 100 && o.Level == 2 && o.Deaths == 0);
    CHECK(GameState::GetFloat("BP.Run.Lost", 0) == 1);
    Finish(work, prior);
}
namespace {
    // A fresh process on the files in `from`: the HOME is replaced by them and GameState / SaveData read again.
    void FreshProcess(const std::filesystem::path& home, const std::filesystem::path& from)
    {
        std::filesystem::remove_all(home);
        std::filesystem::copy(from, home, std::filesystem::copy_options::recursive);
        SaveData::Reload();
        GameState::Clear();
        GameState::Load();
    }
    // The projectiles in flight as a save holds them (slot -> state).
    std::map<uint32_t, G::SavedProjectile> InFlight(Scene& scene)
    {
        std::map<uint32_t, G::SavedProjectile> out;
        auto save = G::SaveWorld::For(scene).Capture("Dovon-1");
        if (save.Combat)
            for (const auto& p : save.Combat->Projectiles) out[p.Slot] = p;
        return out;
    }
    // One projectile's flight after a save: its positions per frame until it is gone, and what it struck.
    struct Flight
    {
        std::vector<glm::dvec3> Path;
        int Gone = -1;            // the frame (after the save) it left the pool
        float TargetHealth = -1;  // its target's health then
    };
}
// The courtyard shrine save (v4) of a normal run is kept aside; the run goes on to the plateau shrine. A fresh process
// (the menu's Continue on the kept files) restores exactly the saved state (hero, enemies, used objects, quests, bag,
// loadout) and plays on to the plateau shrine with the same gameplay events in the same order and the same progress.
// (A restore stops every body and navigation agent - a save keeps no velocities or paths - so a guard that was walking
// starts again from a standstill: the fights take other frames, never another course.) Then, from the plateau save, a
// quick save in the middle of the Warden fight with projectiles in flight: the restored projectiles are exactly the
// saved ones and each flies the same path, ends on the same frame and leaves its target with the same health.
SLOW_TEST(BeaconPassSaveResumeHeadless)
{
    const auto prior = Project::GetActive();
    const auto work = Prepare(0);
    const auto home = work / "home", kept = work / "home-s2", plateau = work / "home-s3", quick = work / "home-quick";
    auto shrine = [](const std::string& line, Outcome&) { return line.find("SLICE_SAVED lvl=1 at=shrine") != std::string::npos; };
    auto entry = [](const std::string& line, Outcome&) { return line.find("SLICE_LEVEL_START lvl=1 entry=continue") != std::string::npos; };
    auto a = Play("Scenes/Menu.zscene", 30 * 60 * 3, shrine, false, true);
    CHECK(a.Live != nullptr && Clean(a.Log));
    if (!a.Live)
    {
        Finish(work, prior);
        return;
    }
    const Snapshot saved = Take(*a.Live);
    std::filesystem::copy(home, kept, std::filesystem::copy_options::recursive);
    G::GameSave save;
    std::string error;
    CHECK(G::DecodeGameSave(FileSystem::ReadTextFile(home / "gameplay-saves/Run1.json"), save, error) && save.Version == 4 && save.Scene == "Dovon");
    std::printf("  saved at frame %d: hero (%.2f, %.2f, %.2f) hp %.1f xp %u stones %u enemies %zu used %zu projectiles %zu %s| %s\n", a.Frames, saved.Hero.x, saved.Hero.y,
                saved.Hero.z, saved.Health, saved.XP, saved.Stones, saved.Enemies.size(), saved.Used.size(), saved.Projectiles, saved.Quests.c_str(), saved.Loadout.c_str());
    // the original scene goes on to the plateau shrine
    auto a2 = Play("", 30 * 60 * 2, shrine, false, true, a.Live);
    a.Live = nullptr;
    CHECK(a2.Live != nullptr);
    const Snapshot endA = a2.Live ? Take(*a2.Live) : Snapshot{};
    a2.Live = nullptr;
    // a fresh process on the kept files: the menu continues from the courtyard save
    FreshProcess(home, kept);
    auto b = Play("Scenes/Menu.zscene", 30 * 60 * 2, entry, false, true);
    CHECK(b.Live != nullptr && b.Log.find("SLICE_RESUMED lvl=1 source=save recovered=0") != std::string::npos);
    if (!b.Live)
    {
        Finish(work, prior);
        return;
    }
    const Snapshot restored = Take(*b.Live);
    std::printf("  restored: hero (%.2f, %.2f, %.2f) hp %.1f xp %u stones %u enemies %zu used %zu projectiles %zu %s| %s\n", restored.Hero.x, restored.Hero.y, restored.Hero.z,
                restored.Health, restored.XP, restored.Stones, restored.Enemies.size(), restored.Used.size(), restored.Projectiles, restored.Quests.c_str(), restored.Loadout.c_str());
    // the hero as the save document holds it, at rest (a shrine is a rest stop)
    const G::SavedEntity* stored = nullptr;
    for (const auto& e : save.Entities)
        if (e.Key == "Hero") stored = &e;
    CHECK(stored != nullptr);
    if (stored)
    {
        std::printf("  save document: hero (%.3f, %.3f, %.3f) hp %.1f\n", stored->Position.x, stored->Position.y, stored->Position.z, stored->Health);
        CHECK(glm::length(restored.Hero - stored->Position) < 0.001f && std::abs(restored.Health - float(stored->Health)) < 0.01f);
    }
    CHECK(glm::length(restored.Hero - saved.Hero) < 0.001f && std::abs(restored.Health - saved.Health) < 0.01f && restored.XP == saved.XP);
    CHECK(restored.Stones == saved.Stones && restored.Potions == saved.Potions && restored.Enemies == saved.Enemies && restored.Used == saved.Used);
    CHECK(restored.Projectiles == saved.Projectiles && restored.Quests == saved.Quests && restored.Loadout == saved.Loadout);
    auto b2 = Play("", 30 * 60 * 2, shrine, false, true, b.Live);
    b.Live = nullptr;
    CHECK(b2.Live != nullptr);
    const Snapshot endB = b2.Live ? Take(*b2.Live) : Snapshot{};
    b2.Live = nullptr;
    std::printf("  continuation: %zu / %zu events, frames %d / %d, xp %u / %u, stones %u / %u, hero (%.3f, %.3f) / (%.3f, %.3f) | %s/ %s\n", a2.Trace.size(),
                b2.Trace.size(), a2.Frames, b2.Frames, a2.XP, b2.XP, a2.Stones, b2.Stones, endA.Hero.x, endA.Hero.z, endB.Hero.x, endB.Hero.z, endA.Quests.c_str(),
                endB.Quests.c_str());
    if (a2.Trace != b2.Trace)
        for (size_t i = 0; i < std::max(a2.Timed.size(), b2.Timed.size()); ++i)
            std::printf("    %s | %s\n", i < a2.Timed.size() ? a2.Timed[i].c_str() : "-", i < b2.Timed.size() ? b2.Timed[i].c_str() : "-");
    // the same gameplay events in the same order, to the same rest at the plateau shrine with the same progress
    CHECK(Clean(a2.Log) && Clean(b2.Log) && a2.Trace == b2.Trace && a2.Trace.size() >= 4 && std::abs(a2.Frames - b2.Frames) < 90);
    CHECK(endA.XP == endB.XP && endA.Stones == endB.Stones && endA.Potions == endB.Potions && endA.Quests == endB.Quests && endA.Used == endB.Used);
    CHECK(endA.Enemies == endB.Enemies && endA.Loadout == endB.Loadout && std::abs(endA.Health - endB.Health) < 0.01f && glm::length(endA.Hero - endB.Hero) < 0.4f);

    // ---- the quick save with projectiles in flight: Continue from the plateau save, play into the Warden fight
    std::filesystem::copy(home, plateau, std::filesystem::copy_options::recursive);
    FreshProcess(home, plateau);
    auto c = Play("Scenes/Menu.zscene", 30 * 60, entry, false, true);
    CHECK(c.Live != nullptr);
    if (!c.Live)
    {
        Finish(work, prior);
        return;
    }
    // the first frame with a projectile at least 0.1 s into its flight
    auto flying = [](Scene& scene, Outcome&) {
        for (const auto& [slot, p] : InFlight(scene))
            if (p.Age >= 0.1) return true;
        return false;
    };
    auto c2 = Play("", 30 * 60, {}, false, true, c.Live, flying);
    c.Live = nullptr;
    CHECK(c2.Live != nullptr);
    if (!c2.Live)
    {
        Finish(work, prior);
        return;
    }
    // what the SaveWrite node does at a shrine, as a quick save now: the save, the pilot's route position, GameState
    Scene& live = *c2.Live;
    const auto before = InFlight(live);
    auto* heroHost = dynamic_cast<VisualGraphHost*>(live.GetEntityByUUID(UUID(Hero)).GetComponent<NativeScriptComponent>().Instance);
    CHECK(heroHost != nullptr && G::SaveWorld::For(live).Save("Run1", "Dovon-1").Success);
    if (heroHost) GameState::SetFloat("BP.Run.Step", heroHost->VM.Variables().at("Step").get<float>());
    GameState::Save();
    CHECK(SaveData::Save());
    std::filesystem::copy(home, quick, std::filesystem::copy_options::recursive);
    CHECK(G::DecodeGameSave(FileSystem::ReadTextFile(home / "gameplay-saves/Run1.json"), save, error) && save.Version == 4 && save.Combat &&
          save.Combat->Projectiles.size() == before.size() && !before.empty());
    // each projectile's flight from here: positions per frame, the frame it ends, its target's health then
    auto follow = [&](Ref<Scene> scene, int frames) {
        std::map<uint32_t, Flight> flights;
        for (const auto& [slot, p] : before) flights[slot] = {};
        int frame = 0;
        Play("", frames, {}, false, false, scene, [&](Scene& s, Outcome&) {
            ++frame;
            const auto now = InFlight(s);
            for (auto& [slot, f] : flights)
            {
                if (f.Gone >= 0) continue;
                auto it = now.find(slot);
                if (it != now.end() && it->second.Generation == before.at(slot).Generation) f.Path.push_back(it->second.Position);
                else
                {
                    f.Gone = frame;
                    // its target: a save key or "uuid:<n>" (an unbound authored entity)
                    const auto& target = before.at(slot).Target;
                    Entity t = target.starts_with("uuid:") ? s.GetEntityByUUID(UUID(std::stoull(target.substr(5)))) : G::SaveWorld::For(s).Find(target);
                    if (!target.empty() && t)
                        if (auto h = G::RuleWorld::For(s).Health(t)) f.TargetHealth = float(h->Current);
                }
            }
            return false;
        });
        return flights;
    };
    const auto original = follow(c2.Live, 60);
    c2.Live = nullptr;
    // a fresh process on the quick save
    FreshProcess(home, quick);
    auto d = Play("Scenes/Menu.zscene", 30 * 60, entry, false, true);
    CHECK(d.Live != nullptr && d.Log.find("SLICE_RESUMED lvl=1 source=save recovered=0") != std::string::npos);
    if (!d.Live)
    {
        Finish(work, prior);
        return;
    }
    const auto after = InFlight(*d.Live);
    CHECK(after.size() == before.size());
    for (const auto& [slot, p] : before)
    {
        auto it = after.find(slot);
        CHECK(it != after.end());
        if (it == after.end()) continue;
        const auto& q = it->second;
        std::printf("  projectile %s slot %u: (%.3f, %.3f, %.3f) v (%.2f, %.2f, %.2f) age %.3f -> restored (%.3f, %.3f, %.3f) age %.3f, target '%s'\n", p.Key.c_str(), slot,
                    p.Position.x, p.Position.y, p.Position.z, p.Velocity.x, p.Velocity.y, p.Velocity.z, p.Age, q.Position.x, q.Position.y, q.Position.z, q.Age, p.Target.c_str());
        CHECK(q.Key == p.Key && q.Generation == p.Generation && q.Owner == p.Owner && q.Target == p.Target && q.Team == p.Team);
        CHECK(glm::length(q.Position - p.Position) < 1e-5 && glm::length(q.Velocity - p.Velocity) < 1e-5 && std::abs(q.Age - p.Age) < 1e-6 && q.Damage == p.Damage);
    }
    const auto resumed = follow(d.Live, 60);
    d.Live = nullptr;
    for (const auto& [slot, f] : original)
    {
        const auto& g = resumed.at(slot);
        double worst = 0;
        for (size_t i = 0; i < std::min(f.Path.size(), g.Path.size()); ++i) worst = std::max(worst, glm::length(f.Path[i] - g.Path[i]));
        std::printf("  flight slot %u: %zu / %zu frames, ends at %d / %d, worst gap %.5f m, target health %.1f / %.1f\n", slot, f.Path.size(), g.Path.size(), f.Gone, g.Gone,
                    worst, f.TargetHealth, g.TargetHealth);
        CHECK(f.Gone > 0 && f.Gone == g.Gone && f.Path.size() == g.Path.size() && worst < 1e-3 && std::abs(f.TargetHealth - g.TargetHealth) < 0.01f);
    }
    Finish(work, prior);
}
// Fixed frame time: two fresh runs of the level produce the same gameplay trace at the same frames.
SLOW_TEST(BeaconPassDeterminism)
{
    const auto prior = Project::GetActive();
    std::vector<std::string> traces[2];
    int frames[2]{};
    for (int run = 0; run < 2; ++run)
    {
        const auto work = Prepare(0, false);
        auto o = Play("Scenes/Dovon.zscene", 30 * 60 * 4, [](const std::string& line, Outcome&) { return line.find("SLICE_VICTORY") != std::string::npos; });
        CHECK(Clean(o.Log) && o.Victory);
        traces[run] = o.Timed;
        frames[run] = o.Frames;
        Finish(work, prior);
    }
    std::printf("  frames %d / %d, trace lines %zu / %zu\n", frames[0], frames[1], traces[0].size(), traces[1].size());
    CHECK(frames[0] == frames[1] && traces[0] == traces[1] && traces[0].size() > 20);
    // ZAMIN_SLICE_TRACE_OUT=<file>: the trace for Tests/slice_workflow_test.py's D phase (the Player's trace must begin with it)
    if (const char* out = std::getenv("ZAMIN_SLICE_TRACE_OUT"))
    {
        std::string text;
        for (const auto& line : traces[0]) text += line.substr(line.find("SLICE_TRACE")) + "\n";
        CHECK(FileSystem::WriteTextFile(out, text));
    }
}
namespace {
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
    // The hero of a save document (null when it cannot be read).
    std::optional<G::SavedEntity> SavedHero(const std::filesystem::path& file)
    {
        G::GameSave save;
        std::string error;
        if (!G::DecodeGameSave(FileSystem::ReadTextFile(file), save, error)) return std::nullopt;
        for (const auto& e : save.Entities)
            if (e.Key == "Hero") return e;
        return std::nullopt;
    }
    bool CleanIO(const std::string& log)
    {
        for (auto bad : { "SLICE_CONFIG_FAILED", "SLICE_ORACLE_FAILED", "Visual graph '", "Instruction budget", "is not a known action", "SLICE_IO_FAILED autosave" })
            if (log.find(bad) != std::string::npos) return false;
        return true;
    }
    // The demo menu (Continue when it is offered) on a fresh process over `from`, until `after` frames after the level's
    // start line; the last banner shown in `banner`, the hero at the start line (the restored pose) in `startAt`.
    Outcome Continue(const std::filesystem::path& home, const std::filesystem::path& from, std::string& banner, glm::vec3& startAt, int after = 12)
    {
        FreshProcess(home, from);
        int started = -1;
        banner.clear();
        return Play("Scenes/Menu.zscene", 30 * 40, {}, false, false, nullptr, [&](Scene& scene, Outcome& o) {
            if (auto b = scene.GetEntityByUUID(UUID(6091)))
                if (auto* text = b.TryGetComponent<TextComponent>(); text && !text->Text.empty()) banner = text->Text;
            if (started < 0 && o.Log.find("SLICE_LEVEL_START") != std::string::npos)
            {
                started = o.Frames;
                if (auto hero = scene.GetEntityByUUID(UUID(Hero))) startAt = scene.GetWorldPosition(hero);
            }
            return started >= 0 && o.Frames >= started + after;
        });
    }
}
// The crash/kill matrix of the run (docs/VerticalSlices.md, "Save and resume"): one normal run to the courtyard shrine
// keeps the files a process killed at each step would leave - after New game (before the autosave), after the shrine
// save (before its GameState), after both - and a fresh process (the menu's Continue) on each resumes a consistent run;
// a damaged shrine save falls back to its backup (the autosave, recovered, said on screen), a damaged save and backup to
// a fresh start at the camp with a visible message, never a crash or a half-applied state.
SLOW_TEST(BeaconPassResumeMatrixHeadless)
{
    const auto prior = Project::GetActive();
    const auto work = Prepare(3);
    const auto home = work / "home", cut = work / "cuts";
    const auto newGame = cut / "new-game", autosave = cut / "autosave", shrine = cut / "shrine";
    auto a = Play("Scenes/Menu.zscene", 30 * 60 * 3, [&](const std::string& line, Outcome&) {
        if (line.find("Menyu: Yangi o'yin") != std::string::npos) Keep(home, newGame);
        if (line.find("SLICE_SAVED lvl=1 at=auto") != std::string::npos) Keep(home, autosave);
        if (line.find("SLICE_SAVED lvl=1 at=shrine") == std::string::npos) return false;
        Keep(home, shrine);
        return true;
    });
    if (!Clean(a.Log)) std::printf("%s\n", a.Log.c_str());
    const auto atShrine = SavedHero(shrine / "gameplay-saves/Run1.json"), atCamp = SavedHero(autosave / "gameplay-saves/Run1.json");
    CHECK(Clean(a.Log) && atShrine && atCamp && std::filesystem::exists(newGame / "prefs.json") && !std::filesystem::exists(newGame / "gameplay-saves/Run1.json"));
    if (!atShrine || !atCamp)
    {
        Finish(work, prior);
        return;
    }
    std::printf("  write run %d frames; autosave at (%.2f, %.2f, %.2f) xp %u; shrine save at (%.2f, %.2f, %.2f) xp %u stones %d\n", a.Frames, atCamp->Position.x,
                atCamp->Position.y, atCamp->Position.z, atCamp->QuestBook.XP, atShrine->Position.x, atShrine->Position.y, atShrine->Position.z, atShrine->QuestBook.XP,
                atShrine->Inventory.Count("FireStone"));
    CHECK(atShrine->QuestBook.XP == 100 && atCamp->QuestBook.XP == 0);
    auto has = [](const Outcome& o, const char* marker) {
        if (o.Log.find(marker) != std::string::npos) return true;
        std::printf("    missing '%s' in:\n%s\n", marker, o.Log.c_str());
        return false;
    };
    auto near = [](const glm::vec3& at, const glm::vec3& saved) { return glm::length(at - saved) < 0.001f; };
    std::string banner;
    glm::vec3 at{ 0 };
    // 1. after both writes, and 2. killed after the shrine save before its GameState: Continue loads the shrine save
    {
        const auto files = cut / "after-shrine-save";
        Keep(shrine, files);
        std::filesystem::copy_file(autosave / "prefs.json", files / "prefs.json", std::filesystem::copy_options::overwrite_existing);
        for (auto [name, from] : { std::pair{ "shrine", shrine }, std::pair{ "after-shrine-save", files } })
        {
            auto o = Continue(home, from, banner, at);
            std::printf("  %s: resumed at (%.3f, %.3f, %.3f) xp %u stones %u\n", name, at.x, at.y, at.z, o.XP, o.Stones);
            CHECK(Clean(o.Log) && has(o, "Menyu: Asosiy davom=1") && has(o, "SLICE_RESUMED lvl=1 source=save recovered=0") && has(o, "SLICE_LEVEL_START lvl=1 entry=continue"));
            CHECK(o.XP == 100 && o.Stones == uint32_t(atShrine->Inventory.Count("FireStone")) && near(at, atShrine->Position));
        }
    }
    // 3. killed after New game, before the autosave: Continue offers the run and starts it fresh (its autosave written now)
    {
        auto o = Continue(home, newGame, banner, at);
        CHECK(Clean(o.Log) && has(o, "Menyu: Asosiy davom=1") && has(o, "SLICE_SAVED lvl=1 at=auto") && has(o, "SLICE_LEVEL_START lvl=1 entry=continue"));
        CHECK(o.XP == 0 && o.Stones == 0 && near(at, atCamp->Position) && SavedHero(home / "gameplay-saves/Run1.json"));
    }
    // 4. the shrine save damaged (a changed byte): its backup - the autosave - with the "recovered" banner
    {
        const auto files = cut / "tampered";
        Keep(shrine, files);
        Tamper(files / "gameplay-saves/Run1.json");
        auto o = Continue(home, files, banner, at);
        std::printf("  tampered shrine save: backup restored at (%.3f, %.3f, %.3f) xp %u, banner '%s'\n", at.x, at.y, at.z, o.XP, banner.c_str());
        CHECK(Clean(o.Log) && has(o, "SLICE_RESUMED lvl=1 source=save recovered=1") && banner == "Restored from the backup save");
        CHECK(o.XP == 0 && o.Stones == 0 && near(at, atCamp->Position));
    }
    // 5. the save and its backup unreadable: a fresh start at the camp, said on screen; the autosave is valid again
    {
        const auto files = cut / "unreadable";
        Keep(shrine, files);
        Tear(files / "gameplay-saves/Run1.json");
        Tear(files / "gameplay-saves/Run1.json.bak");
        auto o = Continue(home, files, banner, at);
        std::printf("  unreadable save and backup: fresh start xp %u, banner '%s'\n", o.XP, banner.c_str());
        CHECK(CleanIO(o.Log) && has(o, "SLICE_IO_FAILED load") && has(o, "SLICE_SAVED lvl=1 at=auto") && has(o, "SLICE_LEVEL_START lvl=1 entry=continue"));
        const auto rewritten = SavedHero(home / "gameplay-saves/Run1.json");
        CHECK(banner == "The save could not be read - the level starts again" && o.XP == 0 && near(at, atCamp->Position) && rewritten && rewritten->QuestBook.XP == 0);
    }
    Finish(work, prior);
}
// The right stick turns the orbit camera at a rate (2.4 rad/s yaw at full tilt), the same at 30 and 60 frames a second
// (before, a fixed angle per frame: twice as fast at 60 Hz), and not at all while a blocking panel (the bag) fences the
// gameplay input - as the mouse look.
TEST(BeaconPassCameraStickTurnsAtARate)
{
    const auto prior = Project::GetActive();
    const auto work = Prepare(0, false);
    {
        auto path = work / "BeaconPass/Assets/Scenes/Dovon.zscene";
        auto doc = J::parse(FileSystem::ReadTextFile(path));
        for (auto& e : doc["Entities"])
            if (e["Entity"] == Hero)
            {
                auto overrides = J::parse(e["NativeScript"]["Fields"]["Overrides"]["Value"].get<std::string>());
                overrides["Pilot"] = false; // a player's hero: it stands still, the camera does not follow on its own
                e["NativeScript"]["Fields"]["Overrides"]["Value"] = overrides.dump();
            }
        CHECK(FileSystem::WriteTextFile(path, doc.dump() + "\n"));
    }
    const auto level = Log::GetMinLevel();
    Log::SetMinLevel(LogLevel::Error);
    // the camera's heading around the hero (the angle of camera - hero in the ground plane), after `hold` seconds of
    // a full right stick at `hz`, settled
    auto turn = [&](float hz, bool bag) {
        Input::ResetState();
        auto scene = CreateRef<Scene>();
        CHECK(SceneSerializer(scene).Deserialize(Project::GetAssetDirectory() / "Scenes/Dovon.zscene"));
        LoadModels(*scene);
        scene->OnRuntimeStart();
        const float dt = 1.0f / hz;
        auto frames = [&](float seconds) {
            for (int i = 0; i < int(std::lround(seconds * hz)); ++i)
            {
                Input::Update();
                scene->UpdateRuntime(dt);
            }
        };
        auto heading = [&] {
            const auto d = scene->GetEntityByUUID(UUID(CameraId)).GetTransform().Translation - scene->GetEntityByUUID(UUID(Hero)).GetTransform().Translation;
            return std::atan2(d.x, d.z);
        };
        frames(2.0f);
        std::string error;
        if (bag) CHECK(G::UIWorld::For(*scene).Open("Bag", error));
        const float before = heading();
        Input::SimulateGamepadConnected(0, true);
        Input::SimulateGamepadAxis(0, GamepadAxis::RightX, 1.0f);
        frames(0.5f);
        Input::SimulateGamepadAxis(0, GamepadAxis::RightX, 0.0f);
        frames(1.5f);
        const float after = heading();
        scene->OnRuntimeStop();
        Input::ResetState();
        return std::remainder(after - before, 6.283185307179586f);
    };
    const float at30 = turn(30, false), at60 = turn(60, false), fenced = turn(60, true);
    std::printf("  a half second of full right stick: %.3f rad at 30 Hz, %.3f rad at 60 Hz, %.3f rad behind the bag\n", at30, at60, fenced);
    CHECK(std::abs(std::abs(at30) - 1.2f) < 0.12f && std::abs(at30 - at60) < 0.06f && std::abs(fenced) < 0.01f);
    ReleaseModels();
    Log::SetMinLevel(level);
    std::filesystem::remove_all(work);
    Project::SetActive(prior);
}
