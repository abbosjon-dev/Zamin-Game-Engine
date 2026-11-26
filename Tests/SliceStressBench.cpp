// ZaminBench --slice-stress [--2d | --3d] [--frames N] [--json <file>]: the vertical slices' in-game stress gates (P5),
// headless: a slice level played with its real graphs (hero, level director, enemies), its real gameplay worlds and
// physics, crowded far past what the level ships with - and what that costs the game per frame (Scene::UpdateRuntime:
// scripts, AI, physics, animation, combat, rules, UI bindings; no drawing).
//   SliceStress2D (Lantern Keeper, Orchards): enemies cloned up to the scene's 32 AI brains around the hero, 200 crates
//     (Box2D), 64 projectiles kept in flight at the hero, 256 extra sprite Animators (DriveSpriteClips, directions
//     turning) and 48 Light 2D.
//   SliceStress3D (Beacon Pass, Pass): 24 animated guards (skinned humanoids with the real locomotion controller,
//     AI crowd), the hero, 64 projectiles in flight, 200 rigid crates (Jolt) dropped round the camp.
// The hero is kept alive (healed every frame) so the fight goes on; the pilot is off. Invariants: no graph error and no
// "Visual graph" / instruction budget message, every brain alive or dead consistently with its health, the projectile
// count reached. Private project copies (no source file is written); projectile visual prefabs are left out headless
// (they need a GL context), as in the slices' oracle tests. docs/Benchmarks.md has the Release baseline
// (Tests/Baselines/slicestress-release.json, Tests/bench.sh --only slicestress).
#include <Zamin/Zamin.h>
#include <Zamin/Asset/AsyncLoader.h>
#include <Zamin/Core/GameState.h>
#include <Zamin/Core/SaveData.h>
#include <Zamin/Gameplay/CombatWorld.h>
#include <Zamin/Gameplay/GameplayAI.h>
#include <Zamin/Gameplay/GameplayRules.h>
#include <Zamin/Project/Project.h>
#include <Zamin/Renderer/Model.h>
#include <Zamin/Scene/SceneSerializer.h>
#include <Zamin/Scripting/Visual/VisualGraphHost.h>
#include <Zamin/Utils/FileSystem.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <set>
#include <string>
#include <vector>

using namespace Zamin;

namespace {
    namespace G = Zamin::Gameplay;
    using J = nlohmann::json;
    using Clock = std::chrono::steady_clock;
    constexpr uint64_t HeroID = 1000;

    struct Result
    {
        std::string Key, Label, Error;
        FrameTimeStats Frame;
        int Brains = 0, BrainsAlive = 0, Bodies = 0, Animators = 0, Lights = 0, Enemies = 0;
        double LiveProjectiles = 0;
        size_t MaxProjectiles = 0;
        int GraphErrors = 0, BadMessages = 0, Inconsistent = 0;
        bool Passed() const { return Error.empty() && !GraphErrors && !BadMessages && !Inconsistent && MaxProjectiles >= 48; }
    };

    std::filesystem::path PrepareCopy(const std::string& game, std::filesystem::path& work)
    {
        work = std::filesystem::temp_directory_path() / std::format("zamin-stress-{}-{}", game, uint64_t(UUID()));
        std::filesystem::create_directories(work);
        std::filesystem::copy(std::filesystem::path(ZAMIN_SOURCE_DIR) / "Projects" / game, work / game, std::filesystem::copy_options::recursive);
        const auto project = work / game;
        for (const auto& entry : std::filesystem::directory_iterator(project / "Assets/Scripts"))
        {
            if (entry.path().extension() != ".zgraph")
                continue;
            J graph = J::parse(FileSystem::ReadTextFile(entry.path()), nullptr, false);
            if (graph.is_discarded() || !graph.contains("Nodes"))
                continue;
            for (auto& node : graph["Nodes"])
                if (node.value("Kind", "") == "Gameplay.ProjectileDefine") node["Inputs"]["Prefab"] = "";
            FileSystem::WriteTextFile(entry.path(), graph.dump() + "\n");
        }
        SaveData::SetFilePath(work / "home" / "prefs.json");
        GameState::Clear();
        return project / (game + ".zproject");
    }

    std::string Overrides(const NativeScriptComponent& nsc)
    {
        const auto it = nsc.Fields.find("Overrides");
        return it == nsc.Fields.end() ? std::string("{}") : it->second.Get<std::string>();
    }

    // The enemies of the level's Enemy graph (melee kind first), the hero.
    std::vector<Entity> Enemies(Scene& scene, const char* kind)
    {
        std::vector<Entity> list;
        for (auto e : scene.GetAllEntitiesWith<NativeScriptComponent>())
        {
            Entity entity(e, &scene);
            const auto& nsc = entity.GetComponent<NativeScriptComponent>();
            const auto graph = nsc.Fields.find("Graph");
            if (graph == nsc.Fields.end() || graph->second.Get<std::string>().find("Enemy") == std::string::npos)
                continue;
            const J o = J::parse(Overrides(nsc), nullptr, false);
            if (!kind || (!o.is_discarded() && o.value("Kind", "") == kind))
                list.push_back(entity);
        }
        std::sort(list.begin(), list.end(), [](Entity a, Entity b) { return (uint64_t)a.GetUUID() < (uint64_t)b.GetUUID(); });
        return list;
    }

    // Copies of `source` round `centre` on a ring (radius `radius`), each with its own save key.
    void CloneEnemies(Scene& scene, Entity source, int count, glm::vec3 centre, float radius, bool twoD, std::vector<Entity>& out)
    {
        for (int i = 0; i < count; ++i)
        {
            Entity copy = scene.DuplicateEntity(source);
            auto& nsc = copy.GetComponent<NativeScriptComponent>();
            J o = J::parse(Overrides(nsc), nullptr, false);
            if (o.is_discarded()) o = J::object();
            o["Key"] = std::format("{}-S{:02}", o.value("Key", "Enemy"), i);
            nsc.Fields["Overrides"].Set(o.dump());
            const float a = 6.2831853f * float(i) / float(std::max(1, count)), r = radius + 1.5f * float(i % 3);
            glm::vec3 p = centre + (twoD ? glm::vec3(std::cos(a) * r, std::sin(a) * r, 0.0f) : glm::vec3(std::cos(a) * r, 0.6f, std::sin(a) * r));
            copy.GetTransform().Translation = p;
            out.push_back(copy);
        }
    }

    bool BadMessage(const std::string& text)
    {
        for (auto bad : { "Visual graph '", "Instruction budget", "SLICE_CONFIG_FAILED", "is not a known action" })
            if (text.find(bad) != std::string::npos)
                return true;
        return false;
    }

    Result Run(bool twoD, int frames)
    {
        Result r;
        r.Key = twoD ? "SliceStress2D" : "SliceStress3D";
        const std::string game = twoD ? "LanternKeeper" : "BeaconPass";
        std::filesystem::path work;
        const auto manifest = PrepareCopy(game, work);
        const auto prior = Project::GetActive();
        if (!Project::Load(manifest))
        {
            r.Error = "the project could not be loaded";
            return r;
        }
        auto scene = CreateRef<Scene>();
        const auto file = Project::GetAssetDirectory() / (twoD ? "Scenes/Orchards.zscene" : "Scenes/Pass.zscene");
        if (!SceneSerializer(scene).Deserialize(file))
        {
            r.Error = "the scene could not be loaded";
            return r;
        }
        Entity hero = scene->GetEntityByUUID(UUID(HeroID));
        const glm::vec3 at = hero ? hero.GetTransform().Translation : glm::vec3(0.0f);
        // ---- the crowd
        std::vector<Entity> crowd, crates, sprites;
        const auto melee = Enemies(*scene, twoD ? "bandit" : "guard");
        const int existing = (int)Enemies(*scene, nullptr).size();
        if (melee.empty())
        {
            r.Error = "no melee enemy was found";
            return r;
        }
        const int clones = twoD ? std::max(0, 32 - existing) : std::max(0, 24 - (int)melee.size());
        CloneEnemies(*scene, melee.front(), clones, at, twoD ? 5.0f : 7.0f, twoD, crowd);
        // ---- 200 crates in a grid beside the hero
        for (int i = 0; i < 200; ++i)
        {
            Entity c = scene->CreateEntity(std::format("Stress quti {}", i));
            const float x = float(i % 20) * (twoD ? 0.7f : 1.1f) - (twoD ? 7.0f : 11.0f), y = float(i / 20) * (twoD ? 0.7f : 1.1f);
            if (twoD)
            {
                c.GetTransform().Translation = at + glm::vec3(x, 3.0f + y, 0.0f);
                auto& rb = c.AddComponent<Rigidbody2DComponent>();
                rb.GravityScale = 0.0f;
                rb.LinearDamping = 2.0f;
                c.AddComponent<BoxCollider2DComponent>().Size = { 0.25f, 0.25f };
                c.AddComponent<SpriteRendererComponent>().Color = { 0.55f, 0.38f, 0.22f, 1.0f };
                if (i < 48)
                {
                    auto& light = c.AddComponent<Light2DComponent>();
                    light.Radius = 2.5f;
                    light.Intensity = 0.8f;
                    light.Flicker = 0.2f;
                    r.Lights++;
                }
            }
            else
            {
                c.GetTransform().Translation = at + glm::vec3(x, 4.0f + 0.9f * float(i / 20), 12.0f + float(i % 7) * 0.13f);
                c.AddComponent<Rigidbody3DComponent>().Mass = 4.0f;
                c.AddComponent<BoxCollider3DComponent>().HalfExtents = glm::vec3(0.35f);
                auto& mr = c.AddComponent<MeshRendererComponent>();
                mr.Type = PrimitiveMesh::Cube;
                c.GetTransform().Scale = glm::vec3(0.7f);
            }
            crates.push_back(c);
        }
        r.Bodies = (int)crates.size();
        // ---- 256 extra sprite Animators (the hero's own sprite, copied round the arena)
        if (twoD && hero)
        {
            Entity look;
            for (auto e : scene->GetAllEntitiesWith<AnimatorComponent>())
                if (Entity a(e, scene.get()); a.GetComponent<AnimatorComponent>().DriveSpriteClips && scene->GetParent(a) == hero)
                    look = a;
            if (look)
                for (int i = 0; i < 256; ++i)
                {
                    Entity copy = scene->DuplicateEntity(look);
                    scene->SetParent(copy, Entity(), false);
                    copy.GetTransform().Translation = at + glm::vec3(float(i % 16) * 0.9f - 7.0f, -3.0f - float(i / 16) * 0.9f, 0.0f);
                    sprites.push_back(copy);
                }
        }
        // ---- 3D: the humanoids' models read on the CPU (no GPU objects headless)
        std::set<std::filesystem::path> models;
        if (!twoD)
        {
            AsyncLoader::DeferGPU deferred;
            for (auto e : scene->GetAllEntitiesWith<AnimatorComponent, MeshRendererComponent>())
            {
                auto& mr = scene->GetRegistry().get<MeshRendererComponent>(e);
                if (mr.Type != PrimitiveMesh::Model || mr.ModelPath.empty()) continue;
                const auto path = Project::GetAssetFileSystemPath(mr.ModelPath);
                mr.ModelAsset = Model::Load(path);
                models.insert(path);
            }
        }
        scene->OnViewportResize(1280, 720);
        scene->OnRuntimeStart();
        auto& rules = G::RuleWorld::For(*scene);
        auto& combat = G::CombatWorld::For(*scene);
        G::ProjectileSpec spec;
        spec.Key = "StressShot";
        spec.Speed = 12;
        spec.Lifetime = 1.6;
        spec.Damage = 1;
        spec.TwoD = twoD;
        std::string error;
        combat.DefineProjectile(spec, error);
        const float dt = 1.0f / 60.0f;
        const int warmup = 90;
        std::vector<double> times;
        uint64_t seen = Log::GetTotalWritten();
        size_t shooter = 0;
        double live = 0;
        float time = 0;
        for (int f = 0; f < warmup + frames; ++f)
        {
            time += dt;
            // The hero holds out (the fight goes on); the projectiles at the hero refilled up to 64.
            if (hero)
            {
                if (auto h = rules.Health(hero); h && h->Dead)
                    rules.Revive(hero, 0.0, error);
                rules.Heal(hero, 1e4, false);
            }
            for (int fired = 0; fired < (int)G::CombatWorld::FireBudget && combat.LiveCount() < 64 && !crowd.empty(); ++fired)
            {
                G::FireRequest shot;
                shot.Key = "StressShot";
                shot.Owner = crowd[shooter++ % crowd.size()];
                shot.Target = hero;
                if (!shot.Owner.IsValid() || !combat.Fire(shot).Success) break;
            }
            for (size_t i = 0; i < sprites.size(); ++i)
                if (auto* a = sprites[i].TryGetComponent<AnimatorComponent>())
                {
                    const float angle = time * 1.3f + float(i) * 0.2f;
                    a->SetFloat("X", std::cos(angle));
                    a->SetFloat("Y", std::sin(angle));
                }
            const auto start = Clock::now();
            scene->UpdateRuntime(dt);
            const double ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
            if (f >= warmup)
            {
                times.push_back(ms);
                live += double(combat.LiveCount());
                r.MaxProjectiles = std::max(r.MaxProjectiles, combat.LiveCount());
            }
        }
        r.Frame = FrameTimeStats::Compute(times);
        r.LiveProjectiles = live / double(std::max(1, frames));
        // ---- invariants
        uint64_t first = 0;
        const auto messages = Log::GetMessages(&first);
        for (size_t i = 0; i < messages.size(); ++i)
            if (first + i >= seen && BadMessage(messages[i].Text))
            {
                if (r.BadMessages++ < 3) std::printf("    %s\n", messages[i].Text.c_str());
            }
        for (auto e : scene->GetAllEntitiesWith<NativeScriptComponent>())
            if (auto* host = dynamic_cast<VisualGraphHost*>(scene->GetRegistry().get<NativeScriptComponent>(e).Instance); host && !host->VM.Error().empty())
            {
                if (r.GraphErrors++ < 3) std::printf("    graf xatosi: %s\n", host->VM.Error().c_str());
            }
        auto& ai = G::AIWorld::For(*scene);
        for (Entity e : Enemies(*scene, nullptr))
        {
            r.Enemies++;
            const auto h = rules.Health(e);
            const auto brain = ai.State(e);
            if (brain) r.Brains++;
            if (brain && h && !h->Dead) r.BrainsAlive++;
            // alive or dead consistently: a dead actor's brain is not chasing / attacking
            if (brain && h && h->Dead && (brain->Brain.Mode == G::BrainMode::Chase || brain->Brain.Mode == G::BrainMode::Attack)) r.Inconsistent++;
        }
        r.Animators = (int)scene->GetAnimationStats().Animators;
        r.Label = twoD ? std::format("Orchards: {} enemies ({} AI brains, limit 32), 200 crates, up to 64 projectiles, {} extra sprite Animators, {} Light 2D", r.Enemies,
                                     r.Brains, sprites.size(), r.Lights)
                       : std::format("Pass: {} enemies ({} AI brains; 24 guards), the hero, up to 64 projectiles, 200 bodies (Jolt)", r.Enemies, r.Brains);
        scene->OnRuntimeStop();
        scene.reset();
        for (const auto& path : models) Model::ReleaseIfUnused(path);
        Project::SetActive(prior);
        std::error_code ec;
        std::filesystem::remove_all(work, ec);
        return r;
    }
}

int RunSliceStressBench(int argc, char** argv)
{
    bool twoD = true, threeD = true;
    int frames = 300;
    const char* jsonFile = nullptr;
    for (int i = 1; i < argc; ++i)
    {
        if (!std::strcmp(argv[i], "--slice-stress")) continue;
        else if (!std::strcmp(argv[i], "--2d")) threeD = false;
        else if (!std::strcmp(argv[i], "--3d")) twoD = false;
        else if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) frames = std::clamp(std::atoi(argv[++i]), 1, 100000);
        else if (!std::strcmp(argv[i], "--json") && i + 1 < argc) jsonFile = argv[++i];
        else
        {
            std::printf("ZaminBench --slice-stress [--2d | --3d] [--frames N] [--json <file>]\n");
            return 1;
        }
    }
#if defined(NDEBUG)
    const char* build = "Release";
#else
    const char* build = "Debug";
#endif
    Log::SetMinLevel(LogLevel::Warn);
    Log::SetTerminalOutput(false);
    std::printf("Zamin slice stress benchmark: game code (Scene::UpdateRuntime, no drawing) every frame, 60 Hz, %d frames (+90 warm-up), %s\n\n", frames, build);
    std::vector<Result> results;
    if (twoD) results.push_back(Run(true, frames));
    if (threeD) results.push_back(Run(false, frames));
    bool passed = true;
    for (auto& r : results)
    {
        std::printf("%s: %s\n", r.Key.c_str(), r.Error.empty() ? r.Label.c_str() : r.Error.c_str());
        if (r.Error.empty())
        {
            std::printf("  game code: average %.3f ms, median %.3f, p95 %.3f, slowest %.3f ms / frame\n", r.Frame.Average, r.Frame.Median, r.Frame.P95, r.Frame.Max);
            std::printf("  projectiles in flight on average %.1f (at most %zu); AI brains %d (alive %d); Animators %d; graph errors %d, forbidden messages %d, "
                        "inconsistent brains %d; invariants: %s\n",
                        r.LiveProjectiles, r.MaxProjectiles, r.Brains, r.BrainsAlive, r.Animators, r.GraphErrors, r.BadMessages, r.Inconsistent,
                        r.Passed() ? "passed" : "BROKEN");
        }
        passed = passed && r.Passed();
    }
    if (jsonFile && *jsonFile)
    {
        std::string out = std::format("{{\n  \"Build\": \"{}\",\n  \"Frames\": {},\n  \"Results\": {{\n", build, frames);
        std::vector<std::string> rows, inv;
        for (auto& r : results)
        {
            if (!r.Error.empty()) continue;
            rows.push_back(std::format("    \"{}.Frame\": {{ \"Ms\": {:.4f}, \"PerFrame\": true }}", r.Key, r.Frame.Average));
            rows.push_back(std::format("    \"{}.FrameP95\": {{ \"Ms\": {:.4f}, \"PerFrame\": true }}", r.Key, r.Frame.P95));
            inv.push_back(std::format("    \"{}\": {{ \"Passed\": {}, \"Enemies\": {}, \"Brains\": {}, \"BrainsAlive\": {}, \"Bodies\": {}, \"Animators\": {}, \"Lights\": {}, "
                                      "\"LiveProjectiles\": {:.2f}, \"MaxProjectiles\": {}, \"GraphErrors\": {}, \"BadMessages\": {}, \"Inconsistent\": {}, "
                                      "\"MedianMs\": {:.4f}, \"MaxMs\": {:.4f} }}",
                                      r.Key, r.Passed() ? "true" : "false", r.Enemies, r.Brains, r.BrainsAlive, r.Bodies, r.Animators, r.Lights, r.LiveProjectiles,
                                      r.MaxProjectiles, r.GraphErrors, r.BadMessages, r.Inconsistent, r.Frame.Median, r.Frame.Max));
        }
        auto join = [](const std::vector<std::string>& v) {
            std::string s;
            for (size_t i = 0; i < v.size(); ++i) s += v[i] + (i + 1 < v.size() ? ",\n" : "\n");
            return s;
        };
        out += join(rows) + "  },\n  \"Invariants\": {\n" + join(inv) + "  }\n}\n";
        FileSystem::WriteTextFile(jsonFile, out);
        std::printf("\nJSON: %s\n", jsonFile);
    }
    if (!passed) std::printf("\nSLICE STRESS: an invariant is broken\n");
    return passed ? 0 : 3;
}
