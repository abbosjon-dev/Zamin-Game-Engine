// ZaminBench: how long the particle systems take, headless (no window, no GPU). Each case fills
// its systems (about 50 000 particles by default), then times a number of frames of the scene's
// particle update - the same code the editor and the player run - and, for the "draw" case, the
// CPU part of drawing (order, colour, size). The profiler's scopes inside the update are listed
// for the last frame of each case.
//
//   ZaminBench [--frames N] [--scale X] [--case <part of a name>] [--profile] [--jobs N]
//
// --jobs N: the job system with N worker threads (0: all on the main thread - the default) - the
// systems that do not depend on each other are simulated in parallel.
//
// ZaminBench --animators N [--frames N] [--jobs N]: N skinned animators instead (their poses are
// sampled on the workers with --jobs).
//
// ZaminBench --queries [--entities N]: the scene's lookups instead (FindEntityByName, the tags,
// by component type) on N entities (10 000 by default) - the indices against a scan over every
// entity, and a frame that spawns, finds and destroys (the index is kept, not rebuilt).
//
// ZaminBench --serialize [--entities N] [--content 2d|3d|all] [--json <file>]: saving and loading large
// scenes instead (SerializeBench.cpp; 10 000 entities by default) in the sample Platformer2D project: a 2D
// and a 3D scene - the JSON text and the binary cache (.zsceneb), each load stage by stage (SceneLoadTiming) -,
// copy / paste of a 1 000-entity selection and what each component costs to save and load.
//
// ZaminBench --serialize --prefabs [--instances N] [--json <file>]: linked prefab instances instead
// (PrefabBench.cpp): N instances of a 20-entity prefab with a nested one (1000 by default) and of the
// sample GameMenu - saving / loading linked against unlinked, syncing, overrides, Apply, PrefabCache.
//
// ZaminBench --nav [--agents N] [--grid N] [--frames N] [--jobs N]: navigation instead (NavBench.cpp):
// the grids' bakes, N path searches at once on one thread and batched over the job system, and a
// scene of N Nav Agents (200 on a 256 x 256 grid by default).
//
// ZaminBench --ecs [--entities N] [--frames N] [--json <file>]: the entity-component system instead (EcsBench.cpp):
// creating, components, views, lookups, the hierarchy, Scene::Copy, destroying (10 000 entities by default) next
// to the bare EnTT registry, and runtime frames of a scene that size (idle, scripts, spawning / destroying).
//
// ZaminBench --render [--sprites N] [--meshes N] [--frames N] [--case ...] [--json <file>]: drawing instead
// (RenderBench.cpp, a GL context: xvfb-run without a display; Tests/render_bench.sh) - 10 000 sprites, 1 000
// meshes (instanced and not) and both in one scene, ms / frame on the CPU and with the GPU.
#include <Zamin/Zamin.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

using namespace Zamin;
using PS = ParticleSystemComponent;

int RunNavBench(int agents, int size, int frames, int jobs); // NavBench.cpp
int RunPrefabBench(int instances, const char* jsonFile);   // PrefabBench.cpp
int RunRenderBench(int argc, char** argv);                  // RenderBench.cpp
int RunEcsBench(int entities, int frames, const char* jsonFile); // EcsBench.cpp
int RunFontBench(const char* jsonFile);                         // FontBench.cpp
int RunSerializeBench(int entities, const char* jsonFile, const std::string& content); // SerializeBench.cpp

namespace {

    struct BenchCase
    {
        const char* Name;
        const char* Description;
        // Makes the systems (about `particles` in all) in the scene.
        std::function<void(Scene&, int particles)> Setup;
        // Moves the viewer (weather) before every frame; null: it stays.
        bool MovingViewer = false;
        // Also the CPU side of drawing every frame.
        bool Draw = false;
    };

    // `systems` emitters on a grid, each filled to `particles / systems` (lifetime 1 s).
    void Grid(Scene& scene, int particles, int systems, const std::function<void(PS&, int)>& configure)
    {
        const int each = std::max(1, particles / systems);
        for (int i = 0; i < systems; i++)
        {
            Entity e = scene.CreateEntity(std::format("Fx {}", i));
            e.GetTransform().Translation = { (float)(i % 10) * 5.0f, 2.0f, (float)(i / 10) * 5.0f };
            auto& ps = e.AddComponent<PS>();
            ps.MaxParticles = each;
            ps.EmissionRate = (float)each * 1.05f; // full after a second, then steady
            ps.LifetimeMin = 0.9f;
            ps.LifetimeMax = 1.0f;
            ps.Shape = PS::EmitterShape::Sphere;
            ps.ShapeRadius = 0.5f;
            ps.Spread = 60.0f;
            ps.Gravity = { 0.0f, -2.0f, 0.0f };
            ps.Drag = 0.1f;
            ps.AngularVelocityMax = 1.0f;
            configure(ps, i);
        }
    }

    std::vector<BenchCase> MakeCases()
    {
        std::vector<BenchCase> cases;
        cases.push_back({ "Oddiy", "shakl, gravitatsiya, qarshilik, aylanish (modullarsiz)",
                          [](Scene& s, int n) { Grid(s, n, 50, [](PS&, int) {}); } });
        cases.push_back({ "Tezlik modullari", "tezlik umr bo'yi (chiziqli + orbita + radial) va tezlik chegarasi",
                          [](Scene& s, int n) {
                              Grid(s, n, 50, [](PS& ps, int) {
                                  ps.VelocityOverLifetime.Enabled = true;
                                  ps.VelocityOverLifetime.Linear = { 0.5f, 0.0f, 0.0f };
                                  ps.VelocityOverLifetime.Orbital = { 0.0f, 1.0f, 0.0f };
                                  ps.VelocityOverLifetime.Radial = 0.3f;
                                  ps.LimitVelocity.Enabled = true;
                                  ps.LimitVelocity.MaxSpeed = 2.0f;
                              });
                          } });
        cases.push_back({ "Bitta katta tizim", "bitta tizimda hamma particle'lar, shovqin bilan (tizim ichida bo'laklab parallel)",
                          [](Scene& s, int n) {
            Grid(s, n, 1, [](PS& ps, int) {
                ps.Noise.Enabled = true;
                ps.Noise.Octaves = 2;
            });
        } });
        cases.push_back({ "Shovqin", "shovqin turbulentligi (2 oktava)",
                          [](Scene& s, int n) {
                              Grid(s, n, 50, [](PS& ps, int) {
                                  ps.Noise.Enabled = true;
                                  ps.Noise.Octaves = 2;
                              });
                          } });
        cases.push_back({ "To'qnashuv", "yer tekisligi bilan to'qnashuv (sakrash)",
                          [](Scene& s, int n) {
                              Grid(s, n, 50, [](PS& ps, int) {
                                  ps.Collision.Enabled = true;
                                  ps.Gravity = { 0.0f, -9.8f, 0.0f };
                              });
                          } });
        cases.push_back({ "Iz (trail)", "hamma particle'larda iz (16 nuqta)",
                          [](Scene& s, int n) {
                              Grid(s, n, 50, [](PS& ps, int) {
                                  ps.Trails.Enabled = true;
                                  ps.Trails.MaxPoints = 16;
                                  ps.Trails.MinVertexDistance = 0.02f;
                              });
                          } });
        cases.push_back({ "Sub-emitter", "har o'lgan particle'dan 2 ta yangi (shablon tizim)",
                          [](Scene& s, int n) {
                              Grid(s, n * 2 / 3, 25, [](PS&, int) {});
                              Entity t = s.CreateEntity("Shablon");
                              auto& tp = t.AddComponent<PS>();
                              tp.EmissionEnabled = false;
                              tp.MaxParticles = n / 3;
                              tp.LifetimeMin = tp.LifetimeMax = 0.5f;
                              const uint64_t id = (uint64_t)t.GetUUID();
                              for (auto e : s.GetAllEntitiesWith<PS>())
                              {
                                  Entity entity(e, &s);
                                  if (entity == t)
                                      continue;
                                  auto& ps = entity.GetComponent<PS>();
                                  ps.SubEmitters.Enabled = true;
                                  PS::SubEmitter sub;
                                  sub.Emitter.ID = id;
                                  sub.Count = 2;
                                  ps.SubEmitters.Emitters = { sub };
                              }
                          } });
        cases.push_back({ "Ob-havo (yomg'ir)", "Yomg'ir preset'i: kamera yuradi, o'rash, sachrash",
                          [](Scene& s, int n) {
                              ParticlePreset rain = *ParticlePresets::GetBuiltin("Rain");
                              rain.System.MaxParticles = n;
                              rain.System.EmissionRate = (float)n / 0.9f;
                              ParticlePresets::Instantiate(s, rain);
                          },
                          true });
        cases.push_back({ "Chizishga tayyorlash", "oddiy + har kadrda saralash, rang va o'lcham (chizishning CPU qismi)",
                          [](Scene& s, int n) {
                              Grid(s, n, 50, [](PS& ps, int i) {
                                  ps.Additive = i % 2 == 0;
                                  ps.ColorOverLifetime.Enabled = true;
                                  ps.SizeOverLifetime.Enabled = true;
                              });
                          },
                          false, true });
        return cases;
    }

    // The CPU part of drawing a system: its order, every particle's colour and size.
    double DrawPrepare(Scene& scene)
    {
        static std::vector<uint32_t> order;
        double sum = 0.0;
        for (auto e : scene.GetAllEntitiesWith<PS>())
        {
            Entity psEntity = Entity(e, &scene);
            const auto& ps = psEntity.GetComponent<PS>();
            ParticleSimulation::GetDrawOrder(ps, nullptr, { 20.0f, 6.0f, -15.0f }, { 0.0f, -0.3f, 1.0f }, true, order);
            for (const Particle& p : ps.Particles)
                sum += ParticleSimulation::GetColor(ps, p).a + ParticleSimulation::GetSize(ps, p);
        }
        return sum;
    }

    size_t CountParticles(Scene& scene)
    {
        size_t n = 0;
        for (auto e : scene.GetAllEntitiesWith<PS>())
            n += Entity(e, &scene).GetComponent<PS>().Particles.size();
        return n;
    }

    // ---- scene lookups (--queries)
    double TimeMs(const std::function<void()>& work, int repeat)
    {
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < repeat; i++)
            work();
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / (double)repeat;
    }

    int RunQueryBench(int count)
    {
        auto scene = CreateRef<Scene>();
        std::vector<std::string> names;
        names.reserve((size_t)count);
        for (int i = 0; i < count; i++)
        {
            names.push_back(std::format("Obyekt {}", i));
            Entity e = scene->CreateEntity(names.back());
            if (i % 100 == 0)
                e.AddTag("Enemy");                // 1 % tagged
            if (i % 10 == 0)
                e.AddTag("Pickup");               // 10 %
            if (i % 50 == 0)
                e.AddComponent<CircleRendererComponent>();
        }
        size_t sink = 0;
        // What the lookups were before the indices: a scan over every entity.
        auto scanName = [&](std::string_view name) {
            for (Entity e : scene->GetEntitiesInOrder())
                if (e.GetName() == name)
                    return e;
            return Entity();
        };
        auto scanTag = [&](std::string_view tag) {
            std::vector<Entity> found;
            for (auto e : scene->GetAllEntitiesWith<TagsComponent>())
                if (Entity(e, scene.get()).GetComponent<TagsComponent>().Has(tag))
                    found.emplace_back(e, scene.get());
            return found;
        };
        const int lookups = 1000;
        scene->FindEntityByName(names[0]); // the indices are made once
        scene->CountEntitiesWithTag("Enemy");

        std::printf("Zamin sahna qidiruvi benchmark: %d obyekt\n\n", count);
        std::printf("%-44s %14s %14s %9s\n", "Amal", "indeks", "skan", "farq");
        auto row = [&](const char* what, double indexed, double scanned) {
            std::printf("%-44s %11.4f ms %11.4f ms %8.0fx\n", what, indexed, scanned, indexed > 0.0 ? scanned / indexed : 0.0);
        };
        int pick = 0;
        auto nextName = [&]() -> const std::string& { pick = (pick * 7919 + 13) % count; return names[(size_t)pick]; };
        row("FindEntityByName x1000 (bitta kadr)",
            TimeMs([&]() { for (int i = 0; i < lookups; i++) sink += (uint32_t)scene->FindEntityByName(nextName()); }, 5),
            TimeMs([&]() { for (int i = 0; i < 20; i++) sink += (uint32_t)scanName(nextName()); }, 1) * (lookups / 20));
        row("FindEntitiesWithTag(\"Enemy\") x1000 (1 %)",
            TimeMs([&]() { for (int i = 0; i < lookups; i++) sink += scene->FindEntitiesWithTag("Enemy").size(); }, 3),
            TimeMs([&]() { for (int i = 0; i < 20; i++) sink += scanTag("Enemy").size(); }, 1) * (lookups / 20));
        row("CountEntitiesWithTag(\"Pickup\") x1000 (10 %)",
            TimeMs([&]() { for (int i = 0; i < lookups; i++) sink += scene->CountEntitiesWithTag("Pickup"); }, 3),
            TimeMs([&]() { for (int i = 0; i < 20; i++) sink += scanTag("Pickup").size(); }, 1) * (lookups / 20));
        row("FindEntitiesWith<CircleRenderer> x1000 (2 %)",
            TimeMs([&]() { for (int i = 0; i < lookups; i++) sink += scene->FindEntitiesWith<CircleRendererComponent>().size(); }, 3),
            TimeMs([&]() {
                for (int i = 0; i < 20; i++)
                    for (Entity e : scene->GetEntitiesInOrder())
                        sink += e.HasComponent<CircleRendererComponent>() ? 1 : 0;
            }, 1) * (lookups / 20));

        // A game frame: 50 spawned, 100 lookups, 50 destroyed - the index follows, no rebuild.
        const uint32_t rebuilds = scene->GetNameCacheStats().Rebuilds;
        std::vector<Entity> spawned;
        const double frame = TimeMs([&]() {
            for (int i = 0; i < 50; i++)
                spawned.push_back(scene->CreateEntity("O'q"));
            for (int i = 0; i < 100; i++)
                sink += (uint32_t)scene->FindEntityByName(i % 2 ? std::string_view("O'q") : std::string_view(nextName()));
            for (Entity e : spawned)
                scene->DestroyEntity(e);
            spawned.clear();
        }, 50);
        std::printf("\n%-44s %11.4f ms   (indeks qayta qurilishi: %u)\n", "Kadr: 50 yaratish + 100 qidiruv + 50 o'chirish", frame,
                    scene->GetNameCacheStats().Rebuilds - rebuilds);
        // The once-a-frame check for direct writes (InvalidateNameCache: as at a new frame).
        const double check = TimeMs([&]() {
            scene->InvalidateNameCache();
            sink += (uint32_t)scene->FindEntityByName(names[1]);
        }, 20);
        std::printf("%-44s %11.4f ms\n", "Kadr boshidagi tekshiruv (to'g'ridan-to'g'ri yozuvlar)", check);
        [[maybe_unused]] static volatile size_t s_Sink; // a volatile store: the work is not optimised away
        s_Sink = sink;
        return 0;
    }

    // A skinned column along a joint chain (a ring of 4 vertices per joint, each ring on its joint): a
    // mesh made without GPU objects (AsyncLoader::DeferGPU) - its bounds and joints are what the
    // animators' visibility test reads.
    static Ref<Mesh> MakeSkinnedColumn(int joints)
    {
        std::vector<SkinnedVertex> vertices;
        std::vector<uint32_t> indices;
        for (int j = 0; j < joints; j++)
            for (int k = 0; k < 4; k++)
            {
                SkinnedVertex v{};
                const float angle = (float)k * 1.5707963f;
                v.Position = { 0.1f * std::cos(angle), 0.25f * (float)j, 0.1f * std::sin(angle) };
                v.Normal = { std::cos(angle), 0.0f, std::sin(angle) };
                v.Joints = glm::ivec4(j, 0, 0, 0);
                v.Weights = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
                vertices.push_back(v);
            }
        for (int j = 0; j + 1 < joints; j++)
            for (int k = 0; k < 4; k++)
            {
                const uint32_t a = (uint32_t)(j * 4 + k), b = (uint32_t)(j * 4 + (k + 1) % 4);
                indices.insert(indices.end(), { a, b, a + 4, b, b + 4, a + 4 });
            }
        AsyncLoader::DeferGPU deferred;
        return CreateRef<Mesh>(vertices, indices);
    }

    // ---- animators: `count` skinned animators (a 40-bone chain, a blend tree and a layer each),
    // the pose sampling per frame - on the workers with --jobs.
    int RunAnimatorBench(int count, int frames, bool profile)
    {
        Log::SetMinLevel(LogLevel::Error);
        const int joints = 40;
        Skeleton skeleton;
        skeleton.Joints.resize((size_t)joints);
        for (int i = 0; i < joints; i++)
        {
            skeleton.Joints[(size_t)i].Name = std::format("Bone{}", i);
            skeleton.Joints[(size_t)i].Parent = i - 1;
            skeleton.Joints[(size_t)i].RestTranslation = { 0.0f, i == 0 ? 0.0f : 0.2f, 0.0f };
        }
        std::vector<AnimationClip> clips;
        for (int c = 0; c < 3; c++)
        {
            AnimationClip clip;
            clip.Name = std::format("Clip{}", c);
            clip.Duration = 1.0f + 0.25f * (float)c;
            for (int j = 0; j < joints; j++)
            {
                AnimationChannel rot;
                rot.JointIndex = j;
                rot.Target = AnimationChannel::Path::Rotation;
                for (int k = 0; k <= 30; k++)
                {
                    const float t = clip.Duration * (float)k / 30.0f;
                    const glm::quat q = glm::angleAxis(0.3f * std::sin((float)k * 0.4f + (float)(j + c)), glm::vec3(1.0f, 0.0f, 0.0f));
                    rot.Times.push_back(t);
                    rot.Values.push_back(glm::vec4(q.x, q.y, q.z, q.w));
                }
                clip.Channels.push_back(rot);
            }
            clips.push_back(std::move(clip));
        }
        // A skinned mesh on the chain (no GPU objects): the visibility test moves its box by every joint.
        Model::SubMesh column;
        column.MeshRef = MakeSkinnedColumn(joints);
        Ref<Model> model = Model::CreateSkeletal(skeleton, clips, { column });
        auto scene = CreateRef<Scene>();
        for (int i = 0; i < count; i++)
        {
            Entity e = scene->CreateEntity(std::format("Hero {}", i));
            auto& mr = e.AddComponent<MeshRendererComponent>();
            mr.Type = PrimitiveMesh::Model;
            mr.ModelAsset = model;
            auto& a = e.AddComponent<AnimatorComponent>();
            a.Speed = 0.8f + 0.01f * (float)(i % 40);
            a.Parameters = { { "Speed", AnimatorParameter::Type::Float, 0.4f } };
            AnimatorState move{ "Move" };
            move.Type = AnimatorStateType::BlendTree1D;
            move.BlendParameter = "Speed";
            move.Motions = { { "Clip0", 0.0f, 1.0f }, { "Clip1", 1.0f, 1.0f } };
            a.States = { move };
            AnimatorLayer upper;
            upper.Clip = "Clip2";
            upper.Weight = 0.5f;
            upper.MaskBones = { "Bone20" };
            a.Layers = { upper };
            // (Spread along X: the culling pass below has a camera see half of them.)
            e.GetTransform().Translation = { (float)i * 2.0f, 0.0f, 0.0f };
        }
        // The camera: looking down -Z at the first half of the row.
        const float half = (float)count;
        const glm::mat4 viewProjection = glm::ortho(-1.0f, half, -5.0f, 15.0f, 0.1f, 100.0f) *
                                         glm::lookAt(glm::vec3(0.0f, 0.0f, 20.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        scene->OnRuntimeStart();
        for (int i = 0; i < 10; i++)
            scene->OnUpdateRuntime(1.0f / 60.0f);
        std::vector<double> times, culledTimes, visibility;
        for (int i = 0; i < frames; i++)
        {
            if (profile && i == frames - 1)
                Profiler::BeginFrame();
            const auto start = std::chrono::steady_clock::now();
            scene->OnUpdateRuntime(1.0f / 60.0f);
            times.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
        }
        const FrameTimeStats stats = FrameTimeStats::Compute(times);
        const uint32_t posed = scene->GetAnimationStats().Posed;
        std::string tree;
        if (profile)
        {
            Profiler::BeginFrame();
            tree = Profiler::FormatTree(Profiler::GetLastFrame(), 0.01f);
        }
        // Then with Animator culling (CullPose): what the camera sees is marked after every update
        // (RenderScene does it for each view) - the posed bounds of every animator - and only those are posed.
        for (auto e : scene->GetAllEntitiesWith<AnimatorComponent>())
            Entity(e, scene.get()).GetComponent<AnimatorComponent>().Culling = AnimatorCulling::CullPose;
        for (int i = 0; i < frames; i++)
        {
            const auto start = std::chrono::steady_clock::now();
            scene->OnUpdateRuntime(1.0f / 60.0f);
            culledTimes.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
            const auto seen = std::chrono::steady_clock::now();
            scene->MarkVisibleAnimators(viewProjection);
            visibility.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - seen).count());
        }
        const FrameTimeStats culledStats = FrameTimeStats::Compute(culledTimes);
        const FrameTimeStats seenStats = FrameTimeStats::Compute(visibility);
        std::printf("Animator benchmark: %d animator (%d suyak, blend tree + qatlam), %d kadr, %s\n", count, joints, frames,
                    JobSystem::IsParallel() ? std::format("vazifalar tizimi: {} ishchi oqim", JobSystem::GetWorkerCount()).c_str() : "bitta oqim");
        std::printf("  kadr: o'rtacha %.2f ms, mediana %.2f ms, eng kam %.2f ms (pozalar: %u)\n", stats.Average, stats.Median, stats.Min,
                    (unsigned)posed);
        std::printf("  culling bilan: kadr o'rtacha %.2f ms (pozalar: %u, kamera ko'rmagani: %u), ko'rinishni belgilash "
                    "(MarkVisibleAnimators) o'rtacha %.3f ms, eng kam %.3f ms\n",
                    culledStats.Average, (unsigned)scene->GetAnimationStats().Posed, (unsigned)scene->GetAnimationStats().Culled,
                    seenStats.Average, seenStats.Min);
        if (profile)
            std::printf("%s\n", tree.c_str());
        scene->OnRuntimeStop();
        JobSystem::Shutdown();
        return 0;
    }

}

int main(int argc, char** argv)
{
    int frames = 120;
    float scale = 1.0f;
    std::string only;
    bool profile = false;
    bool queries = false;
    bool serialize = false;
    bool prefabs = false;
    int instances = 1000;
    const char* jsonFile = nullptr;
    int entities = 10000;
    int jobs = 0;
    int animators = 0;
    bool nav = false;
    bool ecs = false;
    bool fonts = false;
    std::string content = "all";
    int navAgents = 200, navGrid = 256;
    bool framesGiven = false;
    // Drawing (a window, a GL context) parses its own options.
    for (int i = 1; i < argc; i++)
        if (!std::strcmp(argv[i], "--render"))
            return RunRenderBench(argc, argv);
    for (int i = 1; i < argc; i++)
    {
        if (!std::strcmp(argv[i], "--frames") && i + 1 < argc)
        {
            frames = std::max(1, std::atoi(argv[++i]));
            framesGiven = true;
        }
        else if (!std::strcmp(argv[i], "--nav"))
            nav = true;
        else if (!std::strcmp(argv[i], "--ecs"))
            ecs = true;
        else if (!std::strcmp(argv[i], "--fonts"))
            fonts = true;
        else if (!std::strcmp(argv[i], "--agents") && i + 1 < argc)
            navAgents = std::max(1, std::atoi(argv[++i]));
        else if (!std::strcmp(argv[i], "--grid") && i + 1 < argc)
            navGrid = std::max(32, std::atoi(argv[++i]));
        else if (!std::strcmp(argv[i], "--scale") && i + 1 < argc)
            scale = std::max(0.01f, (float)std::atof(argv[++i]));
        else if (!std::strcmp(argv[i], "--case") && i + 1 < argc)
            only = argv[++i];
        else if (!std::strcmp(argv[i], "--profile"))
            profile = true;
        else if (!std::strcmp(argv[i], "--queries"))
            queries = true;
        else if (!std::strcmp(argv[i], "--serialize"))
            serialize = true;
        else if (!std::strcmp(argv[i], "--prefabs"))
            prefabs = true;
        else if (!std::strcmp(argv[i], "--instances") && i + 1 < argc)
            instances = std::max(2, std::atoi(argv[++i]));
        else if (!std::strcmp(argv[i], "--json") && i + 1 < argc)
            jsonFile = argv[++i];
        else if (!std::strcmp(argv[i], "--content") && i + 1 < argc && (!std::strcmp(argv[i + 1], "2d") || !std::strcmp(argv[i + 1], "3d") ||
                                                                      !std::strcmp(argv[i + 1], "all")))
            content = argv[++i];
        else if (!std::strcmp(argv[i], "--entities") && i + 1 < argc)
            entities = std::max(10, std::atoi(argv[++i]));
        else if (!std::strcmp(argv[i], "--animators") && i + 1 < argc)
            animators = std::max(1, std::atoi(argv[++i]));
        else if (!std::strcmp(argv[i], "--jobs") && i + 1 < argc)
            jobs = std::clamp(std::atoi(argv[++i]), 0, (int)JobSystem::MaxWorkers);
        else
        {
            std::printf("ZaminBench [--frames N] [--scale X] [--case <nom qismi>] [--profile] [--jobs N]\n"
                        "ZaminBench --animators N [--frames N] [--jobs N]\n"
                        "ZaminBench --queries [--entities N]\n"
                        "ZaminBench --serialize [--entities N] [--content 2d|3d|all] [--json <fayl>]\n"
                        "ZaminBench --serialize --prefabs [--instances N] [--json <fayl>]\n"
                        "ZaminBench --nav [--agents N] [--grid N] [--frames N] [--jobs N]\n"
                        "ZaminBench --ecs [--entities N] [--frames N] [--json <fayl>]\n"
                        "ZaminBench --fonts [--json <fayl>]\n"
                        "ZaminBench --render [--sprites N] [--meshes N] [--frames N] [--case <nom qismi>] [--json <fayl>]\n");
            return 1;
        }
    }
    if (ecs)
        return RunEcsBench(entities, framesGiven ? frames : 60, jsonFile);
    if (fonts)
        return RunFontBench(jsonFile);
    if (nav)
        return RunNavBench(navAgents, navGrid, framesGiven ? frames : 300, jobs);
    if (animators > 0)
    {
        if (jobs > 0)
            JobSystem::Init((uint32_t)jobs);
        return RunAnimatorBench(animators, frames, profile);
    }
    if (queries)
        return RunQueryBench(entities);
    if (serialize && prefabs)
        return RunPrefabBench(instances, jsonFile);
    if (serialize)
        return RunSerializeBench(entities, jsonFile, content);
    const int particles = (int)(50000.0f * scale);
#if defined(NDEBUG)
    const char* build = "Release";
#elif defined(ZAMIN_PARTICLES_OPTIMIZED_IN_DEBUG)
    const char* build = "Debug (particle simulyatsiyasi -O2 bilan: ZAMIN_OPTIMIZE_PARTICLES_IN_DEBUG)";
#else
    const char* build = "Debug (optimallashtirilmagan: Release'da o'nlab barobar tez)";
#endif
    if (jobs > 0)
        JobSystem::Init((uint32_t)jobs);
    std::printf("Zamin particle benchmark: ~%d particle, %d kadr, 1/60 s qadam, %s, %s\n\n", particles, frames, build,
                JobSystem::IsParallel() ? std::format("vazifalar tizimi: {} ishchi oqim", JobSystem::GetWorkerCount()).c_str()
                                        : "bitta oqim");
    std::printf("%-22s %9s %10s %10s %10s %12s\n", "Holat", "Particle", "o'rtacha", "eng kam", "eng ko'p", "ns/particle");
    Profiler::SetEnabled(true);
    double sink = 0.0;
    for (const BenchCase& bench : MakeCases())
    {
        if (!only.empty() && std::string(bench.Name).find(only) == std::string::npos)
            continue;
        auto scene = CreateRef<Scene>();
        bench.Setup(*scene, particles);
        glm::vec3 viewer(0.0f, 2.0f, 0.0f);
        scene->SetParticleViewer(viewer);
        const float dt = 1.0f / 60.0f;
        for (int i = 0; i < 90; i++) // filled and steady
        {
            if (bench.MovingViewer)
                scene->SetParticleViewer(viewer += glm::vec3(0.05f, 0.0f, 0.03f));
            scene->UpdateEditorAnimation(dt);
        }
        std::vector<double> times;
        size_t counted = 0;
        for (int i = 0; i < frames; i++)
        {
            if (bench.MovingViewer)
                scene->SetParticleViewer(viewer += glm::vec3(0.05f, 0.0f, 0.03f));
            Profiler::BeginFrame();
            const auto start = std::chrono::steady_clock::now();
            scene->UpdateEditorAnimation(dt);
            if (bench.Draw)
                sink += DrawPrepare(*scene);
            times.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
            counted += CountParticles(*scene);
        }
        double sum = 0.0;
        for (double t : times)
            sum += t;
        const double average = sum / (double)times.size();
        const double perParticle = counted > 0 ? sum * 1e6 / (double)counted : 0.0;
        std::printf("%-22s %9zu %8.2f ms %7.2f ms %7.2f ms %10.0f ns   %s\n", bench.Name, counted / (size_t)frames, average,
                    *std::min_element(times.begin(), times.end()), *std::max_element(times.begin(), times.end()), perParticle,
                    bench.Description);
        if (profile)
        {
            Profiler::BeginFrame(); // the last frame's scopes become readable
            std::printf("%s\n", Profiler::FormatTree(Profiler::GetLastFrame(), 0.01f).c_str());
        }
    }
    // A volatile store keeps the drawing work from being optimised away (nothing to print).
    [[maybe_unused]] static volatile double s_Sink;
    s_Sink = sink;
    JobSystem::Shutdown();
    return 0;
}
