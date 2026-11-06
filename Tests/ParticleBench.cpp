// ZaminBench: how long the particle systems take, headless (no window, no GPU). Each case fills
// its systems (about 50 000 particles by default), then times a number of frames of the scene's
// particle update - the same code the editor and the player run - and, for the "draw" case, the
// CPU part of drawing (order, colour, size). The profiler's scopes inside the update are listed
// for the last frame of each case.
//
//   ZaminBench [--frames N] [--scale X] [--case <part of a name>] [--profile]
//
// ZaminBench --queries [--entities N]: the scene's lookups instead (FindEntityByName, the tags,
// by component type) on N entities (10 000 by default) - the indices against a scan over every
// entity, and a frame that spawns, finds and destroys (the index is kept, not rebuilt).
#include <Zamin/Zamin.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>
#include <functional>
#include <string>
#include <vector>

using namespace Zamin;
using PS = ParticleSystemComponent;

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
            const auto& ps = Entity(e, &scene).GetComponent<PS>();
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
        static volatile size_t s_Sink;
        s_Sink = sink;
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
    int entities = 10000;
    for (int i = 1; i < argc; i++)
    {
        if (!std::strcmp(argv[i], "--frames") && i + 1 < argc)
            frames = std::max(1, std::atoi(argv[++i]));
        else if (!std::strcmp(argv[i], "--scale") && i + 1 < argc)
            scale = std::max(0.01f, (float)std::atof(argv[++i]));
        else if (!std::strcmp(argv[i], "--case") && i + 1 < argc)
            only = argv[++i];
        else if (!std::strcmp(argv[i], "--profile"))
            profile = true;
        else if (!std::strcmp(argv[i], "--queries"))
            queries = true;
        else if (!std::strcmp(argv[i], "--entities") && i + 1 < argc)
            entities = std::max(10, std::atoi(argv[++i]));
        else
        {
            std::printf("ZaminBench [--frames N] [--scale X] [--case <nom qismi>] [--profile]\n"
                        "ZaminBench --queries [--entities N]\n");
            return 1;
        }
    }
    if (queries)
        return RunQueryBench(entities);
    const int particles = (int)(50000.0f * scale);
#if defined(NDEBUG)
    const char* build = "Release";
#elif defined(ZAMIN_PARTICLES_OPTIMIZED_IN_DEBUG)
    const char* build = "Debug (particle simulyatsiyasi -O2 bilan: ZAMIN_OPTIMIZE_PARTICLES_IN_DEBUG)";
#else
    const char* build = "Debug (optimallashtirilmagan: Release'da o'nlab barobar tez)";
#endif
    std::printf("Zamin particle benchmark: ~%d particle, %d kadr, 1/60 s qadam, %s\n\n", particles, frames, build);
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
    static volatile double s_Sink;
    s_Sink = sink;
    return 0;
}
