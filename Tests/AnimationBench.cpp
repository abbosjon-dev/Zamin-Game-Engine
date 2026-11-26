// ZaminBench --animation [--humanoids N] [--sprites N] [--model <glb>] [--controller <scene>:<entity UUID>] [--frames N]
// [--jobs N] [--humanoids-only | --sprites-only] [--json <file>]: the animation stress gates of the vertical slices (P5),
// headless (models read without GPU objects, AsyncLoader::DeferGPU). The older synthetic chain bench stays
// `ZaminBench --animators N`; `--animators N --model <glb> [--controller ...]` and `--animators N --sprite-clips` run
// the scenarios below with N animators.
//   AnimationHumanoids (A1): N (256) copies of the 3D slice's hero - its skinned glTF (Projects/BeaconPass Hero.glb,
//     21 joints) with its real controller (the Pass scene's hero Animator: Locomotion 1D blend Idle/Walk/Run, Air,
//     Land, Dead, the UpperBody override and Hit additive layers, Footstep/Hit/Release events). Speed sweeps through
//     every threshold, an attack swing every 1.7 s (UpperBody, restarted) and a hurt flinch every 3.1 s (Hit), half of
//     them off screen (CullPose + MarkVisibleAnimators). Timed with the job system off and on (--jobs, 3).
//     Invariants: blend weights sum to 1 (1e-5), no NaN / inf in any joint matrix, the pose is bit-identical with and
//     without workers, the culled half is not posed, and the clips' events fire exactly once per crossing at 20, 30,
//     60 and 144 Hz (the same counts at every rate, the three Hit events of three swings).
//   AnimationSprites (A2): N (1024) model-free Animators driving sprite clips (DriveSpriteClips): a 2D blend tree of
//     Idle and four walk directions whose direction turns all the time. Invariants: the frame shown is
//     StartFrame + floor(phase x count) exactly, the phase advances by dt / Duration across every direction switch,
//     the clips' frame events fire once per crossing at every rate.
// Both: a steady frame on one thread allocates nothing (fewer than 0.5 operator new calls a frame on average).
// docs/Benchmarks.md has the Release baseline (Tests/Baselines/animation-release.json, Tests/bench.sh --only animation).
#include <Zamin/Zamin.h>
#include <Zamin/Asset/AsyncLoader.h>
#include <Zamin/Renderer/Model.h>
#include <Zamin/Scene/SceneSerializer.h>
#include <Zamin/Scene/SpriteClip.h>
#include <Zamin/Scripting/EventBus.h>
#include <Zamin/Utils/FileSystem.h>

#include "AllocCounter.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <map>
#include <string>
#include <vector>

using namespace Zamin;

namespace {
    using Clock = std::chrono::steady_clock;

    struct Options
    {
        int Humanoids = 256, Sprites = 1024, Frames = 600, Jobs = 3;
        bool RunHumanoids = true, RunSprites = true;
        std::string ModelPath = std::string(ZAMIN_SOURCE_DIR) + "/Projects/BeaconPass/Assets/Models/Hero.glb";
        std::string Controller = std::string(ZAMIN_SOURCE_DIR) + "/Projects/BeaconPass/Assets/Scenes/Pass.zscene:1000";
    };

    struct Hash
    {
        uint64_t Value = 1469598103934665603ull;
        void Add(const void* data, size_t size)
        {
            const auto* bytes = (const uint8_t*)data;
            for (size_t i = 0; i < size; ++i)
            {
                Value ^= bytes[i];
                Value *= 1099511628211ull;
            }
        }
    };

    struct Timing
    {
        FrameTimeStats Frame, Update;
        double Allocations = 0;
        uint32_t Posed = 0, Culled = 0;
    };

    struct Result
    {
        std::string Key, Label;
        Timing Single, Parallel;
        int Jobs = 0;
        uint64_t PoseHashSingle = 0, PoseHashParallel = 0;
        int Joints = 0;
        // invariants
        double MaxWeightError = 0;
        int NonFinite = 0, CullLeaks = 0, FrameMismatches = 0, PhaseBreaks = 0, Switches = 0;
        std::map<int, uint64_t> EventsAtRate; // Hz -> events in all
        int RateMismatches = 0, HitMisses = 0;
        uint64_t Events = 0;
        std::string Error;
        bool Passed() const
        {
            // A steady frame allocates nothing on one thread (the job system's dispatch allocates per ParallelFor).
            const bool steady = !AllocCounter::Available() || Single.Allocations < 0.5;
            return Error.empty() && PoseHashSingle == PoseHashParallel && MaxWeightError <= 1e-5 && !NonFinite && !CullLeaks && !FrameMismatches &&
                   !PhaseBreaks && !RateMismatches && !HitMisses && steady;
        }
    };

    // Every animation event of the scene, per entity and name (the event bus hears every one).
    struct EventCounter
    {
        std::map<std::pair<uint64_t, std::string>, int> Counts;
        uint64_t Total = 0;
        void Attach(Scene& scene)
        {
            scene.GetEventBus().Subscribe("AnimationEvent", [this](const ScriptMessage& m) {
                if (const auto* info = m.TryGet<AnimationEventInfo>())
                {
                    ++Counts[{ info->Source ? (uint64_t)info->Source.GetUUID() : 0, info->Name }];
                    ++Total;
                }
            });
        }
    };

    // ------------------------------------------------------------------ the humanoid crowd
    bool LoadController(const std::string& spec, AnimatorComponent& out, std::string& error)
    {
        const size_t colon = spec.rfind(':');
        if (colon == std::string::npos || colon == 0)
        {
            error = "--controller <scene.zscene>:<entity UUID>";
            return false;
        }
        const std::string file = spec.substr(0, colon);
        const uint64_t id = std::strtoull(spec.c_str() + colon + 1, nullptr, 10);
        const std::string text = std::filesystem::exists(file) ? FileSystem::ReadTextFile(file) : std::string();
        if (text.empty())
        {
            error = "the scene could not be read: " + file;
            return false;
        }
        nlohmann::json scene = nlohmann::json::parse(text, nullptr, false);
        if (scene.is_discarded() || !scene.contains("Entities"))
        {
            error = "the scene is not JSON: " + file;
            return false;
        }
        for (const auto& e : scene["Entities"])
            if (e.value("Entity", uint64_t(0)) == id && e.contains("Animator"))
            {
                // A one-entity scene with only the controller: the serializer reads it as the scene would.
                nlohmann::json one = { { "Scene", "Controller" }, { "Version", scene.value("Version", 1) } };
                one["Entities"] = nlohmann::json::array({ { { "Entity", id }, { "Tag", "Controller" }, { "Animator", e["Animator"] } } });
                auto temp = CreateRef<Scene>();
                if (!SceneSerializer(temp).DeserializeFromString(one.dump()))
                {
                    error = "the Animator could not be read";
                    return false;
                }
                Entity source = temp->GetEntityByUUID(UUID(id));
                if (!source || !source.HasComponent<AnimatorComponent>())
                {
                    error = "the Animator could not be read";
                    return false;
                }
                out = source.GetComponent<AnimatorComponent>();
                return true;
            }
        error = std::format("entity {1} in {0} has no Animator", file, id);
        return false;
    }

    struct Crowd
    {
        Ref<Scene> World;
        glm::mat4 ViewProjection{ 1.0f };
        float Time = 0;
    };

    Crowd MakeHumanoids(const Ref<Model>& model, const AnimatorComponent& controller, int count, bool cull)
    {
        Crowd c;
        c.World = CreateRef<Scene>();
        const int columns = 16;
        for (int i = 0; i < count; ++i)
        {
            Entity e = c.World->CreateEntityWithUUID(UUID(uint64_t(10000 + i)), std::format("Hero {}", i));
            auto& mr = e.AddComponent<MeshRendererComponent>();
            mr.Type = PrimitiveMesh::Model;
            mr.ModelAsset = model;
            auto& a = e.AddComponent<AnimatorComponent>(controller);
            a.Culling = cull ? AnimatorCulling::CullPose : AnimatorCulling::AlwaysAnimate;
            a.Speed = 0.9f + 0.005f * float(i % 40);
            e.GetTransform().Translation = { 2.0f * float(i % columns), 0.0f, -2.0f * float(i / columns) };
        }
        // The camera sees the left half of every row (x < 15): half of the crowd is off screen.
        c.ViewProjection = glm::ortho(-1.5f, 15.0f, -2.0f, 4.0f, 0.1f, 400.0f) *
                           glm::lookAt(glm::vec3(0.0f, 1.0f, 50.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        return c;
    }

    // One frame of gameplay input: the speed sweep and the timed layer swings.
    void DriveHumanoids(Crowd& c, float dt)
    {
        const float t0 = c.Time, t1 = c.Time + dt;
        int i = 0;
        for (auto e : c.World->GetAllEntitiesWith<AnimatorComponent>())
        {
            auto& a = Entity(e, c.World.get()).GetComponent<AnimatorComponent>();
            const float phase = float(i) * 0.37f;
            a.SetFloat("Speed", std::clamp(2.7f + 2.9f * std::sin(t1 * 0.7f + phase), 0.0f, 5.4f));
            auto crossed = [&](float period, float offset) { return std::floor((t1 + offset) / period) != std::floor((t0 + offset) / period); };
            const float offset = float(i) * 0.13f;
            if (crossed(1.7f, offset))
            {
                a.SetLayerWeight("UpperBody", 1.0f, 0.08f);
                a.PlayLayer("UpperBody", "Attack", false, 1.0f, 0.08f, true);
            }
            if (crossed(1.7f, offset - 0.8f)) a.SetLayerWeight("UpperBody", 0.0f, 0.2f);
            if (crossed(3.1f, offset)) a.PlayLayer("Hit", "Hurt", false, 1.0f, 0.05f, true);
            ++i;
        }
        c.Time = t1;
    }

    uint64_t PoseHash(Crowd& c)
    {
        Hash h;
        for (auto e : c.World->GetAllEntitiesWith<AnimatorComponent>())
        {
            Entity entity(e, c.World.get());
            const auto& a = entity.GetComponent<AnimatorComponent>();
            h.Add(a.JointMatrices.data(), a.JointMatrices.size() * sizeof(glm::mat4));
            h.Add(a.BlendWeights.data(), a.BlendWeights.size() * sizeof(float));
        }
        return h.Value;
    }

    void CheckPoses(Crowd& c, Result& r)
    {
        for (auto e : c.World->GetAllEntitiesWith<AnimatorComponent>())
        {
            Entity entity(e, c.World.get());
            const auto& a = entity.GetComponent<AnimatorComponent>();
            if (!a.BlendWeights.empty())
            {
                double sum = 0;
                for (float w : a.BlendWeights) sum += w;
                r.MaxWeightError = std::max(r.MaxWeightError, std::abs(sum - 1.0));
            }
            for (const glm::mat4& m : a.JointMatrices)
                for (int col = 0; col < 4; ++col)
                    for (int row = 0; row < 4; ++row)
                        if (!std::isfinite(m[col][row])) ++r.NonFinite;
        }
    }

    Timing TimeHumanoids(const Ref<Model>& model, const AnimatorComponent& controller, const Options& o, Result& r)
    {
        Timing t;
        Crowd c = MakeHumanoids(model, controller, o.Humanoids, true);
        c.World->OnRuntimeStart();
        const float dt = 1.0f / 60.0f;
        for (int i = 0; i < 60; ++i)
        {
            DriveHumanoids(c, dt);
            c.World->UpdateRuntime(dt);
            c.World->MarkVisibleAnimators(c.ViewProjection);
        }
        std::vector<double> frames, updates;
        uint64_t allocations = 0;
        const uint32_t visibleMax = uint32_t((o.Humanoids + 1) / 2) + 16; // the left half of each 16-wide row
        for (int i = 0; i < o.Frames; ++i)
        {
            DriveHumanoids(c, dt);
            const auto start = Clock::now();
            uint64_t count = 0;
            {
                AllocCounter::Scope scope;
                c.World->UpdateRuntime(dt);
                c.World->MarkVisibleAnimators(c.ViewProjection);
                count = scope.Count();
            }
            frames.push_back(std::chrono::duration<double, std::milli>(Clock::now() - start).count());
            const auto& stats = c.World->GetAnimationStats();
            updates.push_back(stats.Milliseconds);
            allocations += count;
            t.Posed = std::max(t.Posed, stats.Posed);
            t.Culled = std::max(t.Culled, stats.Culled);
            if (stats.Posed > visibleMax) ++r.CullLeaks;
            CheckPoses(c, r);
        }
        t.Frame = FrameTimeStats::Compute(frames);
        t.Update = FrameTimeStats::Compute(updates);
        t.Allocations = double(allocations) / double(std::max(1, o.Frames));
        c.World->OnRuntimeStop();
        return t;
    }

    uint64_t HumanoidPoseRun(const Ref<Model>& model, const AnimatorComponent& controller, int count, int frames)
    {
        Crowd c = MakeHumanoids(model, controller, count, false);
        c.World->OnRuntimeStart();
        for (int i = 0; i < frames; ++i)
        {
            DriveHumanoids(c, 1.0f / 60.0f);
            c.World->UpdateRuntime(1.0f / 60.0f);
        }
        const uint64_t h = PoseHash(c);
        c.World->OnRuntimeStop();
        return h;
    }

    // Fixed speeds (each a steady gait or blend) and three swings at fixed times: the events of 6 s at a rate.
    std::map<std::pair<uint64_t, std::string>, int> HumanoidEvents(const Ref<Model>& model, const AnimatorComponent& controller, int count, float hz)
    {
        Crowd c = MakeHumanoids(model, controller, count, false);
        EventCounter counter;
        counter.Attach(*c.World);
        c.World->OnRuntimeStart();
        const float speeds[] = { 0.0f, 0.9f, 1.8f, 2.7f, 3.6f, 4.5f, 5.4f, 5.4f };
        const int frames = int(std::lround(6.0 * hz));
        const float dt = 1.0f / hz;
        float time = 0;
        for (int f = 0; f < frames; ++f)
        {
            int i = 0;
            for (auto e : c.World->GetAllEntitiesWith<AnimatorComponent>())
            {
                auto& a = Entity(e, c.World.get()).GetComponent<AnimatorComponent>();
                a.SetFloat("Speed", speeds[i % 8]);
                for (float at : { 0.5f, 2.5f, 4.5f })
                    if (time <= at && time + dt > at)
                    {
                        a.SetLayerWeight("UpperBody", 1.0f, 0.0f);
                        a.PlayLayer("UpperBody", "Attack", false, 1.0f, 0.0f, true);
                    }
                ++i;
            }
            c.World->UpdateRuntime(dt);
            time += dt;
        }
        c.World->OnRuntimeStop();
        return counter.Counts;
    }

    Result Humanoids(const Options& o)
    {
        Result r;
        r.Key = "AnimationHumanoids";
        Ref<Model> model;
        {
            AsyncLoader::DeferGPU deferred;
            model = Model::Load(o.ModelPath);
        }
        AnimatorComponent controller;
        std::string error;
        if (!model || !model->IsSkinned())
        {
            r.Error = "the model could not be loaded or is not skinned: " + o.ModelPath;
            return r;
        }
        if (!LoadController(o.Controller, controller, error))
        {
            r.Error = error;
            return r;
        }
        r.Joints = (int)model->GetSkeleton().Joints.size();
        r.Label = std::format("{} humanoids ({} bones, {}), Locomotion 1D + UpperBody/Hit layers + events, half of them off screen (CullPose)",
                              o.Humanoids, r.Joints, std::filesystem::path(o.ModelPath).filename().string());
        // The pose with and without workers: the very same bits.
        const int poseFrames = std::min(o.Frames, 120);
        r.PoseHashSingle = HumanoidPoseRun(model, controller, o.Humanoids, poseFrames);
        r.Jobs = o.Jobs;
        r.Single = TimeHumanoids(model, controller, o, r);
        if (o.Jobs > 0)
        {
            JobSystem::Init(uint32_t(o.Jobs));
            r.PoseHashParallel = HumanoidPoseRun(model, controller, o.Humanoids, poseFrames);
            r.Parallel = TimeHumanoids(model, controller, o, r);
            JobSystem::Shutdown();
        }
        else
            r.PoseHashParallel = r.PoseHashSingle;
        // Events at four rates: the same counts (a step crossing the end of the run may fall either side: +-1),
        // and exactly the three Hit events of the three swings.
        const int eventActors = std::min(o.Humanoids, 32);
        const auto reference = HumanoidEvents(model, controller, eventActors, 60.0f);
        for (float hz : { 20.0f, 30.0f, 60.0f, 144.0f })
        {
            const auto counts = hz == 60.0f ? reference : HumanoidEvents(model, controller, eventActors, hz);
            uint64_t total = 0;
            for (const auto& [key, n] : counts) total += uint64_t(n);
            r.EventsAtRate[int(hz)] = total;
            std::map<uint64_t, int> hits;
            for (const auto& [key, n] : counts)
                if (key.second == "Hit") hits[key.first] += n;
            for (int i = 0; i < eventActors; ++i)
                if (hits[uint64_t(10000 + i)] != 3) ++r.HitMisses;
            for (const auto& [key, n] : reference)
            {
                const auto it = counts.find(key);
                if (std::abs((it == counts.end() ? 0 : it->second) - n) > 1) ++r.RateMismatches;
            }
            for (const auto& [key, n] : counts)
                if (!reference.count(key) && n > 1) ++r.RateMismatches;
        }
        r.Events = r.EventsAtRate[60];
        Model::ReleaseIfUnused(o.ModelPath);
        return r;
    }

    // ------------------------------------------------------------------ the sprite crowd
    struct SpriteClipInfo
    {
        std::string Name;
        int Start = 0, Count = 0;
    };

    std::vector<SpriteClipInfo> WriteSpriteClips(const std::filesystem::path& dir, std::vector<std::string>& paths)
    {
        std::vector<SpriteClipInfo> infos = { { "Idle", 0, 4 }, { "WalkRight", 4, 8 }, { "WalkLeft", 12, 8 }, { "WalkUp", 20, 8 }, { "WalkDown", 28, 8 } };
        std::filesystem::create_directories(dir);
        for (const auto& info : infos)
        {
            SpriteClip c;
            c.Columns = 8;
            c.Rows = 5;
            c.StartFrame = info.Start;
            c.FrameCount = info.Count;
            c.FPS = 10;
            c.Loop = true;
            if (info.Count == 8) c.Events = { { 2, "Step", "" }, { 6, "Step", "" } };
            const auto path = dir / (info.Name + ".zsanim");
            SpriteClipSerializer::Serialize(c, path);
            SpriteClipLibrary::ReloadFile(path);
            paths.push_back(path.string());
        }
        return infos;
    }

    Ref<Scene> MakeSprites(int count, const std::vector<std::string>& clips)
    {
        auto scene = CreateRef<Scene>();
        for (int i = 0; i < count; ++i)
        {
            Entity e = scene->CreateEntityWithUUID(UUID(uint64_t(20000 + i)), std::format("Sprite {}", i));
            e.GetTransform().Translation = { float(i % 32), float(i / 32), 0.0f };
            e.AddComponent<SpriteRendererComponent>();
            e.AddComponent<SpriteAnimationComponent>().Clips = clips;
            auto& a = e.AddComponent<AnimatorComponent>();
            a.Parameters = { { "X", AnimatorParameter::Type::Float, 1 }, { "Y", AnimatorParameter::Type::Float, 0 } };
            AnimatorState move{ "Move" };
            move.Type = AnimatorStateType::BlendTree2D;
            move.BlendParameter = "X";
            move.BlendParameterY = "Y";
            move.Motions = { { "Idle", 0, 1, { 0, 0 } }, { "WalkRight", 0, 1, { 1, 0 } }, { "WalkLeft", 0, 1, { -1, 0 } }, { "WalkUp", 0, 1, { 0, 1 } },
                             { "WalkDown", 0, 1, { 0, -1 } } };
            move.Duration = 0.8f;
            a.States = { move };
            a.DriveSpriteClips = true;
            a.Speed = 1.0f;
        }
        return scene;
    }

    // The direction turns all the time (a full turn every 4.8 s, each sprite its own phase); with stops, every 3 s a
    // 0.4 s stop (Idle).
    void DriveSprites(Scene& scene, float time, bool stops = true)
    {
        int i = 0;
        for (auto e : scene.GetAllEntitiesWith<AnimatorComponent>())
        {
            auto& a = Entity(e, &scene).GetComponent<AnimatorComponent>();
            const float angle = time * 1.31f + float(i) * 0.1f;
            const float stop = stops && std::fmod(time + float(i % 7) * 0.4f, 3.0f) < 0.4f ? 0.0f : 1.0f;
            a.SetFloat("X", std::cos(angle) * stop);
            a.SetFloat("Y", std::sin(angle) * stop);
            ++i;
        }
    }

    Result Sprites(const Options& o)
    {
        Result r;
        r.Key = "AnimationSprites";
        r.Label = std::format("{} sprite Animator (DriveSpriteClips, 2D blend tree: Idle + 4 directions, the direction keeps turning)", o.Sprites);
        const auto dir = std::filesystem::temp_directory_path() / std::format("zamin-sprite-bench-{}", uint64_t(UUID()));
        std::vector<std::string> paths;
        const auto infos = WriteSpriteClips(dir, paths);
        auto info = [&](const std::string& name) -> const SpriteClipInfo* {
            for (const auto& i : infos)
                if (i.Name == name) return &i;
            return nullptr;
        };
        const float dt = 1.0f / 60.0f, duration = 0.8f;
        auto timed = [&](bool parallel) {
            Timing t;
            auto scene = MakeSprites(o.Sprites, paths);
            scene->OnRuntimeStart();
            float time = 0;
            for (int i = 0; i < 30; ++i)
            {
                DriveSprites(*scene, time += dt);
                scene->UpdateRuntime(dt);
            }
            std::vector<double> frames, updates;
            std::vector<float> phases;
            std::vector<std::string> shown;
            uint64_t allocations = 0;
            for (int f = 0; f < o.Frames; ++f)
            {
                DriveSprites(*scene, time += dt);
                const auto start = Clock::now();
                uint64_t count = 0;
                {
                    AllocCounter::Scope scope;
                    scene->UpdateRuntime(dt);
                    count = scope.Count();
                }
                frames.push_back(std::chrono::duration<double, std::milli>(Clock::now() - start).count());
                updates.push_back(scene->GetAnimationStats().Milliseconds);
                allocations += count;
                if (parallel) continue;
                // The frame shown is the dominant clip's StartFrame + floor(phase x count), and the phase only grows by
                // dt / Duration (a direction switch keeps it).
                size_t k = 0;
                phases.resize(size_t(o.Sprites), -1.0f);
                shown.resize(size_t(o.Sprites));
                for (auto e : scene->GetAllEntitiesWith<AnimatorComponent>())
                {
                    Entity entity(e, scene.get());
                    const auto& a = entity.GetComponent<AnimatorComponent>();
                    const auto& sprite = entity.GetComponent<SpriteAnimationComponent>();
                    const std::string clip = sprite.GetCurrentClipName();
                    if (const SpriteClipInfo* c = info(clip))
                    {
                        const double phase = double(a.Time) - std::floor(double(a.Time));
                        const int expected = c->Start + int(std::floor(phase * c->Count)) % c->Count;
                        if (sprite.GetCurrentFrame() != expected) ++r.FrameMismatches;
                    }
                    else
                        ++r.FrameMismatches;
                    if (phases[k] >= 0 && std::abs(double(a.Time) - double(phases[k]) - double(dt / duration)) > 1e-4) ++r.PhaseBreaks;
                    if (!shown[k].empty() && shown[k] != clip) ++r.Switches;
                    phases[k] = a.Time;
                    shown[k] = clip;
                    ++k;
                }
            }
            t.Frame = FrameTimeStats::Compute(frames);
            t.Update = FrameTimeStats::Compute(updates);
            t.Allocations = double(allocations) / double(std::max(1, o.Frames));
            t.Posed = scene->GetAnimationStats().Animators;
            scene->OnRuntimeStop();
            return t;
        };
        r.Single = timed(false);
        r.Jobs = o.Jobs;
        if (o.Jobs > 0)
        {
            JobSystem::Init(uint32_t(o.Jobs));
            r.Parallel = timed(true);
            JobSystem::Shutdown();
        }
        // Frame events at four rates (6 s, 64 sprites, always walking - every walk clip steps on frames 2 and 6, so a
        // direction switch at a slightly different moment changes nothing): the same counts per sprite (+-1 at the end).
        auto events = [&](float hz) {
            auto scene = MakeSprites(std::min(o.Sprites, 64), paths);
            EventCounter counter;
            counter.Attach(*scene);
            scene->OnRuntimeStart();
            const int frames = int(std::lround(6.0 * hz));
            float time = 0;
            for (int f = 0; f < frames; ++f)
            {
                time += 1.0f / hz;
                DriveSprites(*scene, time, false);
                scene->UpdateRuntime(1.0f / hz);
            }
            scene->OnRuntimeStop();
            return counter.Counts;
        };
        const auto reference = events(60.0f);
        for (float hz : { 20.0f, 30.0f, 60.0f, 144.0f })
        {
            const auto counts = hz == 60.0f ? reference : events(hz);
            uint64_t total = 0;
            for (const auto& [key, n] : counts) total += uint64_t(n);
            r.EventsAtRate[int(hz)] = total;
            for (const auto& [key, n] : reference)
            {
                const auto it = counts.find(key);
                if (std::abs((it == counts.end() ? 0 : it->second) - n) > 1) ++r.RateMismatches;
            }
        }
        r.Events = r.EventsAtRate[60];
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        return r;
    }
}

int RunAnimationBench(int argc, char** argv)
{
    Options o;
    const char* jsonFile = nullptr;
    int animators = 0;
    bool spriteClips = false, model = false;
    for (int i = 1; i < argc; ++i)
    {
        auto number = [&](int& value, int low, int high) {
            if (i + 1 < argc) value = std::clamp(std::atoi(argv[++i]), low, high);
        };
        if (!std::strcmp(argv[i], "--animation")) continue;
        else if (!std::strcmp(argv[i], "--humanoids")) number(o.Humanoids, 1, 4096);
        else if (!std::strcmp(argv[i], "--sprites")) number(o.Sprites, 1, 16384);
        else if (!std::strcmp(argv[i], "--animators")) number(animators, 1, 16384);
        else if (!std::strcmp(argv[i], "--frames")) number(o.Frames, 1, 100000);
        else if (!std::strcmp(argv[i], "--jobs")) number(o.Jobs, 0, (int)JobSystem::MaxWorkers);
        else if (!std::strcmp(argv[i], "--humanoids-only")) o.RunSprites = false;
        else if (!std::strcmp(argv[i], "--sprites-only")) o.RunHumanoids = false;
        else if (!std::strcmp(argv[i], "--sprite-clips")) spriteClips = true;
        else if (!std::strcmp(argv[i], "--model") && i + 1 < argc) o.ModelPath = argv[++i], model = true;
        else if (!std::strcmp(argv[i], "--controller") && i + 1 < argc) o.Controller = argv[++i], model = true;
        else if (!std::strcmp(argv[i], "--json") && i + 1 < argc) jsonFile = argv[++i];
        else
        {
            std::printf("ZaminBench --animation [--humanoids N] [--sprites N] [--model <glb>] [--controller <scene>:<UUID>] [--frames N] [--jobs N]\n"
                        "                       [--humanoids-only | --sprites-only] [--json <file>]\n"
                        "ZaminBench --animators N --model <glb> [--controller <scene>:<UUID>] | --animators N --sprite-clips\n");
            return 1;
        }
    }
    // The design's spelling: --animators N with a model (humanoids) or with --sprite-clips (sprites).
    if (spriteClips)
    {
        o.RunHumanoids = false;
        if (animators) o.Sprites = animators;
    }
    else if (model || animators)
    {
        o.RunSprites = false;
        if (animators) o.Humanoids = animators;
    }
#if defined(NDEBUG)
    const char* build = "Release";
#else
    const char* build = "Debug";
#endif
    Log::SetMinLevel(LogLevel::Warn);
    std::printf("Zamin animation stress benchmark: 60 Hz, %d frames, job system with 0 and %d worker threads, %s, operator new counter: %s\n\n", o.Frames,
                o.Jobs, build, AllocCounter::Available() ? "yes" : "no");
    std::vector<Result> results;
    if (o.RunHumanoids) results.push_back(Humanoids(o));
    if (o.RunSprites) results.push_back(Sprites(o));
    bool passed = true;
    for (auto& r : results)
    {
        std::printf("%s: %s\n", r.Key.c_str(), r.Error.empty() ? r.Label.c_str() : r.Error.c_str());
        if (!r.Error.empty())
        {
            passed = false;
            continue;
        }
        auto line = [](const char* name, const Timing& t) {
            std::printf("  %s: frame average %.3f ms, median %.3f, p95 %.3f, slowest %.3f; Animator update average %.3f ms, p95 %.3f; allocations %.1f / frame",
                        name, t.Frame.Average, t.Frame.Median, t.Frame.P95, t.Frame.Max, t.Update.Average, t.Update.P95, t.Allocations);
        };
        line("bitta oqim", r.Single);
        if (r.Key == "AnimationHumanoids") std::printf("; posed %u, culled %u", r.Single.Posed, r.Single.Culled);
        std::printf("\n");
        if (r.Jobs > 0)
        {
            line(std::format("{} workers", r.Jobs).c_str(), r.Parallel);
            std::printf("\n");
        }
        if (r.Key == "AnimationHumanoids")
            std::printf("  blend weight error at most %.2e (limit 1e-5), NaN/inf matrices %d, culling leaks %d, pose %016llx %s %016llx (0 and %d workers)\n",
                        r.MaxWeightError, r.NonFinite, r.CullLeaks, (unsigned long long)r.PoseHashSingle, r.PoseHashSingle == r.PoseHashParallel ? "==" : "!=",
                        (unsigned long long)r.PoseHashParallel, r.Jobs);
        else
            std::printf("  frame = StartFrame + floor(phase x count) broken: %d, phase breaks: %d, clip switches: %d\n", r.FrameMismatches, r.PhaseBreaks,
                        r.Switches);
        std::printf("  events (6 s): 20 Hz %llu, 30 Hz %llu, 60 Hz %llu, 144 Hz %llu; mismatches %d%s\n", (unsigned long long)r.EventsAtRate[20],
                    (unsigned long long)r.EventsAtRate[30], (unsigned long long)r.EventsAtRate[60], (unsigned long long)r.EventsAtRate[144], r.RateMismatches,
                    r.Key == "AnimationHumanoids" ? std::format(", Hit (3 strikes = 3 events) misses {}", r.HitMisses).c_str() : "");
        std::printf("  invariants: %s\n", r.Passed() ? "passed" : "BROKEN");
        passed = passed && r.Passed();
    }
    if (jsonFile && *jsonFile)
    {
        std::string out = std::format("{{\n  \"Build\": \"{}\",\n  \"Frames\": {},\n  \"Humanoids\": {},\n  \"Sprites\": {},\n  \"Jobs\": {},\n  \"Results\": {{\n", build,
                                      o.Frames, o.Humanoids, o.Sprites, o.Jobs);
        std::vector<std::string> rows, allocs, inv;
        for (auto& r : results)
        {
            if (!r.Error.empty()) continue;
            rows.push_back(std::format("    \"{}.Frame\": {{ \"Ms\": {:.4f}, \"PerFrame\": true }}", r.Key, r.Single.Frame.Average));
            rows.push_back(std::format("    \"{}.FrameP95\": {{ \"Ms\": {:.4f}, \"PerFrame\": true }}", r.Key, r.Single.Frame.P95));
            if (r.Jobs > 0)
            {
                rows.push_back(std::format("    \"{}.FrameJobs\": {{ \"Ms\": {:.4f}, \"PerFrame\": true }}", r.Key, r.Parallel.Frame.Average));
                rows.push_back(std::format("    \"{}.FrameJobsP95\": {{ \"Ms\": {:.4f}, \"PerFrame\": true }}", r.Key, r.Parallel.Frame.P95));
            }
            allocs.push_back(std::format("    \"{}\": {:.2f}", r.Key, r.Single.Allocations));
            inv.push_back(std::format("    \"{}\": {{ \"Passed\": {}, \"MaxWeightError\": {:.3e}, \"NonFinite\": {}, \"CullLeaks\": {}, \"Posed\": {}, \"Culled\": {}, "
                                      "\"PoseIdentical\": {}, \"FrameMismatches\": {}, \"PhaseBreaks\": {}, \"Switches\": {}, \"Events20\": {}, \"Events30\": {}, "
                                      "\"Events60\": {}, \"Events144\": {}, \"RateMismatches\": {}, \"HitMisses\": {}, \"UpdateMs\": {:.4f}, \"UpdateJobsMs\": {:.4f} }}",
                                      r.Key, r.Passed() ? "true" : "false", r.MaxWeightError, r.NonFinite, r.CullLeaks, r.Single.Posed, r.Single.Culled,
                                      r.PoseHashSingle == r.PoseHashParallel ? "true" : "false", r.FrameMismatches, r.PhaseBreaks, r.Switches,
                                      r.EventsAtRate[20], r.EventsAtRate[30], r.EventsAtRate[60], r.EventsAtRate[144], r.RateMismatches, r.HitMisses,
                                      r.Single.Update.Average, r.Parallel.Update.Average));
        }
        auto join = [](const std::vector<std::string>& v) {
            std::string s;
            for (size_t i = 0; i < v.size(); ++i) s += v[i] + (i + 1 < v.size() ? ",\n" : "\n");
            return s;
        };
        out += join(rows) + "  },\n  \"AllocationsPerFrame\": {\n" + join(allocs) + "  },\n  \"Invariants\": {\n" + join(inv) + "  }\n}\n";
        FileSystem::WriteTextFile(jsonFile, out);
        std::printf("\nJSON: %s\n", jsonFile);
    }
    if (!passed) std::printf("\nANIMATION STRESS: an invariant is broken\n");
    return passed ? 0 : 3;
}
