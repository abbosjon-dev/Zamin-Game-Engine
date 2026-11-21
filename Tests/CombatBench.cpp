// ZaminBench --combat [--projectiles N] [--actors N] [--walls N] [--effects N] [--frames N] [--2d | --3d | --effects-only]
// [--json <file>]: what the gameplay combat world (Zamin/Gameplay/CombatWorld) costs per frame, headless (P5).
//   Combat2D / Combat3D: N projectiles (128, the pool) kept alive by re-firing (at most 32 a frame, the spawn
//   budget) from half of the actors (team 1) at the other half (team 2) across lanes, a wall blocking every other
//   lane: analytic actor sweeps plus Box2D rays (2D, 3 per projectile) or Jolt sphere casts (3D) every 60 Hz step.
//   CombatEffects: status-effect books at the scene cap (64 actors x 16 instances = 1024) ticking damage.
// Each frame times Gameplay::TickCombat alone (the frame budgets are reset first, as Scene::UpdateRuntime does), and
// counts its heap allocations (AllocCounter). Reported: mean / median / p95 / max ms per frame, allocations per
// frame and the world's per-step work. docs/Benchmarks.md has the baseline (Tests/Baselines/combat-release.json).
#include <Zamin/Zamin.h>
#include <Zamin/Gameplay/CombatWorld.h>
#include <Zamin/Gameplay/GameplayRules.h>

#include "AllocCounter.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>
#include <string>
#include <vector>

using namespace Zamin;

namespace {
    namespace G = Zamin::Gameplay;
    using Clock = std::chrono::steady_clock;
    constexpr double Step = 1.0 / 60.0;

    struct Options
    {
        int Projectiles = 128, Actors = 64, Walls = 32, Effects = 16, Frames = 600;
        bool TwoD = true, ThreeD = true, EffectBooks = true;
    };
    struct Result
    {
        std::string Key, Label;
        double Mean = 0, Median = 0, P95 = 0, Max = 0, Allocations = 0;
        double LivePerFrame = 0, SweepsPerStep = 0, ActorTestsPerStep = 0, TicksPerStep = 0, HitsPerFrame = 0;
        uint64_t Hits = 0;
    };

    void Summarize(Result& r, std::vector<double> ms, uint64_t allocations)
    {
        std::sort(ms.begin(), ms.end());
        double sum = 0;
        for (double v : ms) sum += v;
        r.Mean = sum / double(ms.size());
        r.Median = ms[ms.size() / 2];
        r.P95 = ms[std::min(ms.size() - 1, size_t(double(ms.size()) * 0.95))];
        r.Max = ms.back();
        r.Allocations = double(allocations) / double(ms.size());
    }

    void Health(Scene& scene, Entity e, uint16_t team)
    {
        std::string error;
        if (!G::RuleWorld::For(scene).Configure(e, { 1e6, 0, 0, .5, team }, true, error)) std::printf("health: %s\n", error.c_str());
    }

    Entity Box(Scene& scene, const std::string& name, glm::vec3 position, glm::vec3 half, bool twoD)
    {
        Entity e = scene.CreateEntity(name);
        e.GetTransform().Translation = position;
        if (twoD)
        {
            e.AddComponent<Rigidbody2DComponent>().Type = Rigidbody2DComponent::BodyType::Static;
            e.AddComponent<BoxCollider2DComponent>().Size = { half.x, half.y };
        }
        else
        {
            e.AddComponent<Rigidbody3DComponent>().Type = Rigidbody3DComponent::BodyType::Static;
            e.AddComponent<BoxCollider3DComponent>().HalfExtents = half;
        }
        return e;
    }

    // One frame as Scene::UpdateRuntime orders it (frame budgets, then the combat step on the game clock), with
    // only TickCombat timed and counted.
    template<typename Before>
    void Frame(Scene& scene, std::vector<double>& ms, uint64_t& allocations, bool measure, Before before)
    {
        G::BeginRulesFrame(scene);
        G::BeginCombatFrame(scene);
        before();
        const auto start = Clock::now();
        uint64_t count = 0;
        {
            AllocCounter::Scope scope;
            G::TickCombat(scene, Step);
            count = scope.Count();
        }
        const double elapsed = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        G::TickRules(scene, float(Step));
        if (!measure) return;
        ms.push_back(elapsed);
        allocations += count;
    }

    Result Projectiles(bool twoD, const Options& o)
    {
        Result r;
        r.Key = twoD ? "Combat2D" : "Combat3D";
        const int lanes = std::max(1, o.Actors / 2);
        r.Label = std::format("{} snaryad, {} aktor, {} devor ({})", o.Projectiles, lanes * 2, o.Walls, twoD ? "Box2D: 3 nur" : "Jolt: sphere cast");
        auto scene = CreateRef<Scene>();
        std::vector<Entity> shooters, targets;
        for (int i = 0; i < lanes; ++i)
        {
            const float y = float(i - lanes / 2) * 1.5f;
            const glm::vec3 a = twoD ? glm::vec3(-15, y, 0) : glm::vec3(-15, 0, y), b = twoD ? glm::vec3(15, y, 0) : glm::vec3(15, 0, y);
            shooters.push_back(Box(*scene, std::format("Shooter{}", i), a, { .3f, .3f, .3f }, twoD));
            targets.push_back(Box(*scene, std::format("Target{}", i), b, { .3f, .3f, .3f }, twoD));
        }
        // Walls block every other lane (the rest reach their targets); extra walls stand in a second row.
        for (int k = 0; k < o.Walls; ++k)
        {
            const int lane = (2 * k) % lanes;
            const float y = float(lane - lanes / 2) * 1.5f, x = float(k / std::max(1, (lanes + 1) / 2)) * 3.0f;
            Box(*scene, std::format("Wall{}", k), twoD ? glm::vec3(x, y, 0) : glm::vec3(x, 0, y), { .2f, .5f, .5f }, twoD);
        }
        scene->OnRuntimeStart();
        for (auto e : shooters) Health(*scene, e, 1);
        for (auto e : targets) Health(*scene, e, 2);
        auto& world = G::CombatWorld::For(*scene);
        G::ProjectileSpec spec;
        spec.Key = "Bench";
        spec.Speed = 20;
        spec.Lifetime = 2;
        spec.Damage = 1;
        spec.TwoD = twoD;
        std::string error;
        if (!world.DefineProjectile(spec, error)) std::printf("define: %s\n", error.c_str());
        std::vector<double> ms;
        ms.reserve(size_t(o.Frames));
        uint64_t allocations = 0, sweeps = 0, tests = 0, steps = 0, live = 0;
        size_t next = 0;
        const int warmup = 120;
        const uint64_t hitsBefore = 0;
        uint64_t hitsAtStart = hitsBefore;
        for (int frame = 0; frame < warmup + o.Frames; ++frame)
        {
            const bool measure = frame >= warmup;
            if (frame == warmup) hitsAtStart = world.Stats().Hits;
            Frame(*scene, ms, allocations, measure, [&] {
                // Refill to the target count: at most the per-frame spawn budget, round-robin over the shooters.
                for (int fired = 0; fired < int(G::CombatWorld::FireBudget) && int(world.LiveCount()) < o.Projectiles; ++fired)
                {
                    G::FireRequest shot;
                    shot.Key = "Bench";
                    shot.Owner = shooters[next++ % shooters.size()];
                    shot.Direction = { 1, 0, 0 };
                    if (!world.Fire(shot).Success) break;
                }
            });
            if (!measure) continue;
            const auto stats = world.Stats();
            sweeps += stats.Sweeps;
            tests += stats.ActorTests;
            steps += stats.Steps;
            live += stats.Live;
        }
        Summarize(r, ms, allocations);
        r.Hits = world.Stats().Hits - hitsAtStart;
        r.LivePerFrame = double(live) / double(o.Frames);
        r.SweepsPerStep = steps ? double(sweeps) / double(steps) : 0;
        r.ActorTestsPerStep = steps ? double(tests) / double(steps) : 0;
        r.HitsPerFrame = double(r.Hits) / double(o.Frames);
        scene->OnRuntimeStop();
        return r;
    }

    Result EffectBooks(const Options& o)
    {
        Result r;
        r.Key = "CombatEffects";
        const int effects = std::clamp(o.Effects, 1, 16);
        const int actors = std::max(1, std::min(o.Actors, int(G::CombatWorld::MaxEffects) / effects));
        r.Label = std::format("{} aktor x {} effekt = {} nusxa (sahna chegarasi 1024), snaryadsiz", actors, effects, actors * effects);
        auto scene = CreateRef<Scene>();
        std::vector<Entity> victims;
        for (int i = 0; i < actors; ++i)
        {
            Entity e = scene->CreateEntity(std::format("Victim{}", i));
            e.GetTransform().Translation = { float(i % 8), float(i / 8), 0 };
            victims.push_back(e);
        }
        scene->OnRuntimeStart();
        for (auto e : victims) Health(*scene, e, 2);
        auto& world = G::CombatWorld::For(*scene);
        std::string error;
        for (int k = 0; k < effects; ++k)
        {
            G::EffectSpec s;
            s.Key = std::format("E{}", k);
            s.Duration = 600;
            s.Period = .1 + .01 * k; // phases spread over the steps
            s.Tick = G::TickKind::Damage;
            s.TickAmount = .01;
            s.Speed = k % 2 ? .9 : 1;
            if (!world.DefineEffect(s, error)) std::printf("effect: %s\n", error.c_str());
        }
        std::vector<double> ms;
        ms.reserve(size_t(o.Frames));
        uint64_t allocations = 0, ticks = 0, steps = 0;
        size_t applied = 0;
        const size_t total = size_t(actors) * size_t(effects);
        const int warmup = 60;
        for (int frame = 0; frame < warmup + o.Frames; ++frame)
        {
            const bool measure = frame >= warmup;
            Frame(*scene, ms, allocations, measure, [&] {
                // The direct-application budget is 64 a frame: the books fill over the first frames.
                for (int n = 0; n < int(G::CombatWorld::EffectBudget) && applied < total; ++n, ++applied)
                    world.ApplyEffect(victims[applied / size_t(effects)], std::format("E{}", applied % size_t(effects)));
            });
            if (!measure) continue;
            const auto stats = world.Stats();
            ticks += stats.Ticks;
            steps += stats.Steps;
        }
        Summarize(r, ms, allocations);
        r.TicksPerStep = steps ? double(ticks) / double(steps) : 0;
        r.LivePerFrame = double(world.EffectCount());
        scene->OnRuntimeStop();
        return r;
    }
}

int RunCombatBench(int argc, char** argv)
{
    Options o;
    const char* jsonFile = nullptr;
    for (int i = 1; i < argc; ++i)
    {
        auto number = [&](int& value, int low, int high) {
            if (i + 1 < argc) value = std::clamp(std::atoi(argv[++i]), low, high);
        };
        if (!std::strcmp(argv[i], "--combat")) continue;
        else if (!std::strcmp(argv[i], "--projectiles")) number(o.Projectiles, 1, 128);
        else if (!std::strcmp(argv[i], "--actors")) number(o.Actors, 2, 512);
        else if (!std::strcmp(argv[i], "--walls")) number(o.Walls, 0, 256);
        else if (!std::strcmp(argv[i], "--effects")) number(o.Effects, 1, 16);
        else if (!std::strcmp(argv[i], "--frames")) number(o.Frames, 1, 100000);
        else if (!std::strcmp(argv[i], "--2d")) o.ThreeD = o.EffectBooks = false;
        else if (!std::strcmp(argv[i], "--3d")) o.TwoD = o.EffectBooks = false;
        else if (!std::strcmp(argv[i], "--effects-only")) o.TwoD = o.ThreeD = false;
        else if (!std::strcmp(argv[i], "--json") && i + 1 < argc) jsonFile = argv[++i];
        else
        {
            std::printf("ZaminBench --combat [--projectiles N] [--actors N] [--walls N] [--effects N] [--frames N] [--2d | --3d | --effects-only] [--json <fayl>]\n");
            return 1;
        }
    }
#if defined(NDEBUG)
    const char* build = "Release";
#else
    const char* build = "Debug";
#endif
    std::printf("Zamin combat benchmark: TickCombat har kadr (60 Hz qadam), %d kadr, %s, operator new hisoblagich: %s\n\n", o.Frames, build,
                AllocCounter::Available() ? "bor" : "yo'q");
    std::vector<Result> results;
    if (o.TwoD) results.push_back(Projectiles(true, o));
    if (o.ThreeD) results.push_back(Projectiles(false, o));
    if (o.EffectBooks) results.push_back(EffectBooks(o));
    for (auto& r : results)
    {
        std::printf("%s: %s\n", r.Key.c_str(), r.Label.c_str());
        std::printf("  TickCombat: o'rtacha %.4f ms, mediana %.4f, p95 %.4f, eng sekin %.4f ms / kadr\n", r.Mean, r.Median, r.P95, r.Max);
        std::printf("  ajratishlar: %.2f / kadr", r.Allocations);
        if (r.Key == "CombatEffects")
            std::printf("; tiklar: %.1f / qadam; effekt nusxalari: %.0f\n", r.TicksPerStep, r.LivePerFrame);
        else
            std::printf("; uchayotgan: %.1f; sweep: %.1f / qadam; aktor testi: %.1f / qadam; zarba: %.2f / kadr\n", r.LivePerFrame, r.SweepsPerStep,
                        r.ActorTestsPerStep, r.HitsPerFrame);
    }
    if (jsonFile && *jsonFile)
    {
        std::string out = std::format("{{\n  \"Build\": \"{}\",\n  \"Frames\": {},\n  \"Projectiles\": {},\n  \"Actors\": {},\n  \"Walls\": {},\n  \"Effects\": {},\n  \"Results\": {{\n",
                                      build, o.Frames, o.Projectiles, o.Actors, o.Walls, o.Effects);
        std::vector<std::string> rows, allocs, work;
        for (auto& r : results)
        {
            rows.push_back(std::format("    \"{}.Tick\": {{ \"Ms\": {:.4f}, \"PerFrame\": true }}", r.Key, r.Mean));
            rows.push_back(std::format("    \"{}.TickP95\": {{ \"Ms\": {:.4f}, \"PerFrame\": true }}", r.Key, r.P95));
            allocs.push_back(std::format("    \"{}\": {:.2f}", r.Key, r.Allocations));
            if (r.Key == "CombatEffects") work.push_back(std::format("    \"{}\": {{ \"TicksPerStep\": {:.2f}, \"Instances\": {:.0f} }}", r.Key, r.TicksPerStep, r.LivePerFrame));
            else
                work.push_back(std::format("    \"{}\": {{ \"Live\": {:.2f}, \"SweepsPerStep\": {:.2f}, \"ActorTestsPerStep\": {:.2f}, \"HitsPerFrame\": {:.3f}, \"MedianMs\": {:.4f}, \"MaxMs\": {:.4f} }}",
                                           r.Key, r.LivePerFrame, r.SweepsPerStep, r.ActorTestsPerStep, r.HitsPerFrame, r.Median, r.Max));
        }
        auto join = [](const std::vector<std::string>& v) {
            std::string s;
            for (size_t i = 0; i < v.size(); ++i) s += v[i] + (i + 1 < v.size() ? ",\n" : "\n");
            return s;
        };
        out += join(rows) + "  },\n  \"AllocationsPerFrame\": {\n" + join(allocs) + "  },\n  \"Work\": {\n" + join(work) + "  }\n}\n";
        FileSystem::WriteTextFile(jsonFile, out);
        std::printf("\nJSON: %s\n", jsonFile);
    }
    return 0;
}
