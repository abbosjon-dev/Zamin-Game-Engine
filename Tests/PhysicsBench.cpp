// ZaminBench --physics [--2d | --3d] [--bodies N] [--actors N] [--frames N] [--json <file>]: the physics stress gates of
// the vertical slices (P5), headless. Each scenario is run twice from a fresh scene; the runs must end in the very same
// state (a hash of every body's and character's pose bits), and invariants are checked every frame:
//   Physics2D: a 60 x 40 m tilemap arena (border and pillar tiles, top-down: no gravity) with N dynamic circles / boxes
//     (2048) bouncing around, 64 actors driven by the gameplay motor (Gameplay::StepMotor, top-down) along waypoint
//     loops and 128 CombatWorld projectiles (Box2D rays) kept in flight between the actors' two teams. Invariants: no
//     body leaves the arena (tunnelling), no NaN, no body faster than the cap (40 m/s).
//   Physics3D: a walled pit with two ramps into which N rigid bodies (512: boxes, spheres, capsules) are dropped, a
//     65 x 65 terrain heightfield, a 0.25 m stair ziggurat and a 35 degree ridge; 64 Character Controller 3D walk loops
//     over the stairs, the ridge and the terrain while 128 sphere casts a frame sweep the pit. Invariants: a character's
//     feet never sink more than 0.05 m under the ground below it, no character under y -10, every character covers at
//     least 80 % of the distance it was told to walk, every body stays inside the pit, no NaN.
// Timed per frame: the whole Scene::UpdateRuntime (physics steps, characters, the combat world, the frame's gameplay)
// and its "Fizika" profiler scope; the sphere casts on their own. docs/Benchmarks.md has the Release baseline
// (Tests/Baselines/physics-release.json, Tests/bench.sh --only physics).
#include <Zamin/Zamin.h>
#include <Zamin/Gameplay/CombatWorld.h>
#include <Zamin/Gameplay/GameplayMotor.h>
#include <Zamin/Gameplay/GameplayRules.h>
#include <Zamin/Physics/PhysicsWorld2D.h>
#include <Zamin/Physics/PhysicsWorld3D.h>
#include <Zamin/Scene/Terrain.h>

#include "AllocCounter.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
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
    constexpr float Step = 1.0f / 60.0f;

    struct Options
    {
        int Bodies2D = 2048, Bodies3D = 512, Actors = 64, Frames = 600;
        bool TwoD = true, ThreeD = true;
    };

    // A fixed-seed generator: the scenes are the same in every run and on every machine.
    struct Lcg
    {
        uint64_t State;
        explicit Lcg(uint64_t seed) : State(seed) {}
        uint32_t Next()
        {
            State = State * 6364136223846793005ull + 1442695040888963407ull;
            return uint32_t(State >> 33);
        }
        float Uniform(float a, float b) { return a + (b - a) * float(Next() & 0xffffff) / float(0xffffff); }
    };

    struct Hash
    {
        uint64_t Value = 1469598103934665603ull;
        void Add(float v)
        {
            uint32_t bits;
            std::memcpy(&bits, &v, sizeof bits);
            for (int i = 0; i < 4; ++i)
            {
                Value ^= (bits >> (i * 8)) & 0xff;
                Value *= 1099511628211ull;
            }
        }
        void Add(const glm::vec3& v) { Add(v.x), Add(v.y), Add(v.z); }
    };

    struct Result
    {
        std::string Key, Label;
        FrameTimeStats Frame, Physics, Casts;
        double Allocations = 0;
        uint64_t HashA = 0, HashB = 0;
        // invariants
        int Escaped = 0, NonFinite = 0, TooFast = 0, Sunk = 0, Fell = 0, Slow = 0;
        float MaxSpeed = 0, MinClearance = 1e9f, MinProgress = 1e9f, MeanProgress = 0;
        uint64_t Hits = 0, Fired = 0, CastHits = 0;
        int Steps = 0, Sleeping = 0;
        bool Passed() const { return HashA == HashB && !Escaped && !NonFinite && !TooFast && !Sunk && !Fell && !Slow; }
    };

    double ProfilerScope(const char* name)
    {
        const Profiler::Entry* e = Profiler::Find(name);
        return e ? double(e->Milliseconds) : 0.0;
    }

    // ------------------------------------------------------------------ 2D: tilemap arena, bodies, motors, projectiles
    struct Run2D
    {
        uint64_t Hash = 0;
        std::vector<double> Frame, Physics;
        uint64_t Allocations = 0;
        int Escaped = 0, NonFinite = 0, TooFast = 0;
        float MaxSpeed = 0;
        uint64_t Hits = 0, Fired = 0;
    };

    constexpr int ArenaW = 60, ArenaH = 40;
    constexpr float SpeedCap2D = 40.0f;

    bool SolidTile(int x, int y)
    {
        if (x == 0 || y == 0 || x == ArenaW - 1 || y == ArenaH - 1) return true;
        // 2 x 2 pillars on an 8-tile lattice (a field of cover, like the slices' ruins and trees)
        return x % 8 >= 6 && y % 8 >= 6 && x < ArenaW - 4 && y < ArenaH - 4;
    }

    Run2D Simulate2D(const Options& o, bool measure)
    {
        Run2D run;
        auto scene = CreateRef<Scene>();
        auto& settings = scene->GetSettings();
        settings.UseProjectGravity = false;
        settings.Gravity = { 0.0f, 0.0f }; // top-down
        settings.Physics2D.AllowSleep = true;
        const glm::vec2 origin{ -ArenaW * 0.5f, -ArenaH * 0.5f };
        {
            Entity map = scene->CreateEntity("Arena");
            map.GetTransform().Translation = { origin.x, origin.y, 0.0f };
            auto& t = map.AddComponent<TilemapComponent>();
            t.Width = ArenaW;
            t.Height = ArenaH;
            t.EnsureSize();
            for (int y = 0; y < ArenaH; ++y)
                for (int x = 0; x < ArenaW; ++x)
                    if (SolidTile(x, y)) t.Set(x, y, 1);
            t.Friction = 0.2f;
        }
        Lcg rng(0x2D5EED);
        // The free cells, every one a candidate spot (bodies first, then actors on the cells left).
        std::vector<glm::ivec2> cells;
        for (int y = 1; y < ArenaH - 1; ++y)
            for (int x = 1; x < ArenaW - 1; ++x)
                if (!SolidTile(x, y) && !SolidTile(x - 1, y) && !SolidTile(x + 1, y) && !SolidTile(x, y - 1) && !SolidTile(x, y + 1)) cells.push_back({ x, y });
        // Two bodies a cell at most (half cell offsets), in a shuffled but fixed order.
        std::vector<glm::vec2> spots;
        for (const auto& c : cells)
            for (int k = 0; k < 2; ++k)
                spots.push_back(origin + glm::vec2(c) + glm::vec2(k ? 0.28f : 0.72f, k ? 0.3f : 0.7f));
        for (size_t i = spots.size(); i > 1; --i) std::swap(spots[i - 1], spots[rng.Next() % i]);
        const int actors = std::min(o.Actors, 64);
        const int bodies = std::min<int>(o.Bodies2D, int(spots.size()) - actors);
        std::vector<Entity> dynamic, actor;
        for (int i = 0; i < bodies; ++i)
        {
            Entity e = scene->CreateEntity(std::format("Body{}", i));
            e.GetTransform().Translation = { spots[size_t(i)], 0.0f };
            auto& rb = e.AddComponent<Rigidbody2DComponent>();
            rb.GravityScale = 0.0f;
            rb.LinearDamping = 0.02f;
            if (i % 2)
            {
                auto& c = e.AddComponent<CircleCollider2DComponent>();
                c.Radius = rng.Uniform(0.12f, 0.2f);
                c.Restitution = 0.6f;
                c.Friction = 0.2f;
            }
            else
            {
                auto& b = e.AddComponent<BoxCollider2DComponent>();
                b.Size = glm::vec2(rng.Uniform(0.1f, 0.17f));
                b.Restitution = 0.5f;
                b.Friction = 0.3f;
            }
            dynamic.push_back(e);
        }
        for (int i = 0; i < actors; ++i)
        {
            Entity e = scene->CreateEntity(std::format("Actor{}", i));
            e.GetTransform().Translation = { spots[size_t(bodies + i)], 0.0f };
            auto& rb = e.AddComponent<Rigidbody2DComponent>();
            rb.GravityScale = 0.0f;
            rb.FixedRotation = true;
            rb.Sleeping = Rigidbody2DComponent::SleepMode::NeverSleep;
            e.AddComponent<CircleCollider2DComponent>().Radius = 0.3f;
            actor.push_back(e);
        }
        scene->OnRuntimeStart();
        std::string error;
        for (int i = 0; i < actors; ++i)
            if (!G::RuleWorld::For(*scene).Configure(actor[size_t(i)], { 1e6, 0, 0, .2, uint16_t(1 + i % 2) }, true, error))
                std::printf("health: %s\n", error.c_str());
        auto& combat = G::CombatWorld::For(*scene);
        G::ProjectileSpec spec;
        spec.Key = "Shot";
        spec.Speed = 18;
        spec.Lifetime = 1.5;
        spec.Damage = 1;
        spec.TwoD = true;
        if (!combat.DefineProjectile(spec, error)) std::printf("define: %s\n", error.c_str());

        // Each actor walks a loop of four waypoints (a rectangle round its start, clipped to the arena).
        std::vector<std::array<glm::vec2, 4>> loops;
        std::vector<int> next(size_t(actors), 0);
        std::vector<G::MotorState> motors((size_t)actors);
        for (int i = 0; i < actors; ++i)
        {
            const glm::vec2 p = spots[size_t(bodies + i)];
            const float w = 4.0f + float(i % 5), h = 3.0f + float(i % 3);
            auto clip = [&](glm::vec2 v) { return glm::clamp(v, origin + 1.6f, origin + glm::vec2(ArenaW, ArenaH) - 1.6f); };
            loops.push_back({ clip(p), clip(p + glm::vec2(w, 0)), clip(p + glm::vec2(w, h)), clip(p + glm::vec2(0, h)) });
        }
        G::MotorSettings motor;
        motor.Speed = 4.2f;
        motor.Acceleration = 28;
        motor.Deceleration = 32;
        const glm::vec2 low = origin + 1.0f - 0.02f, high = origin + glm::vec2(ArenaW, ArenaH) - 1.0f + 0.02f;
        size_t shooter = 0;
        const int warmup = measure ? 60 : 0;
        Profiler::SetEnabled(true);
        for (int frame = 0; frame < warmup + o.Frames; ++frame)
        {
            // The motors: the stick points at the next waypoint, the motor gives the body its velocity.
            for (int i = 0; i < actors; ++i)
            {
                b2Body* body = (b2Body*)actor[size_t(i)].GetComponent<Rigidbody2DComponent>().RuntimeBody;
                const glm::vec2 p = PhysicsWorld2D::GetPosition(body);
                glm::vec2 goal = loops[size_t(i)][size_t(next[size_t(i)])];
                if (glm::distance(p, goal) < 0.6f)
                {
                    next[size_t(i)] = (next[size_t(i)] + 1) % 4;
                    goal = loops[size_t(i)][size_t(next[size_t(i)])];
                }
                G::MotorInput in;
                const glm::vec2 d = goal - p;
                in.Move = glm::length(d) > 1e-4f ? d / glm::length(d) : glm::vec2(0);
                in.HorizontalVelocity = PhysicsWorld2D::GetLinearVelocity(body);
                in.Delta = Step;
                in.Grounded = true;
                G::MotorOutput out;
                if (G::StepMotor(motor, motors[size_t(i)], in, out, error)) PhysicsWorld2D::SetLinearVelocity(body, out.Horizontal);
            }
            // Keep the projectiles in flight: each frame up to the spawn budget, actor i at actor i + 1 (other team).
            for (int fired = 0; fired < int(G::CombatWorld::FireBudget) && combat.LiveCount() < 128; ++fired)
            {
                G::FireRequest shot;
                shot.Key = "Shot";
                shot.Owner = actor[shooter % actor.size()];
                shot.Target = actor[(shooter + 1) % actor.size()];
                ++shooter;
                if (!combat.Fire(shot).Success) break;
                ++run.Fired;
            }
            Profiler::BeginFrame();
            const auto start = Clock::now();
            uint64_t count = 0;
            {
                AllocCounter::Scope scope;
                scene->UpdateRuntime(Step);
                count = scope.Count();
            }
            const double ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
            Profiler::BeginFrame();
            if (frame >= warmup)
            {
                run.Frame.push_back(ms);
                run.Physics.push_back(ProfilerScope("Fizika"));
                run.Allocations += count;
            }
            // Invariants: every dynamic body inside the arena, finite, under the speed cap.
            auto check = [&](Entity e) {
                const b2Body* body = (const b2Body*)e.GetComponent<Rigidbody2DComponent>().RuntimeBody;
                const glm::vec2 p = PhysicsWorld2D::GetPosition(body), v = PhysicsWorld2D::GetLinearVelocity(body);
                if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(v.x) || !std::isfinite(v.y))
                {
                    ++run.NonFinite;
                    return;
                }
                if (p.x < low.x || p.y < low.y || p.x > high.x || p.y > high.y) ++run.Escaped;
                const float speed = glm::length(v);
                run.MaxSpeed = std::max(run.MaxSpeed, speed);
                if (speed > SpeedCap2D) ++run.TooFast;
            };
            for (Entity e : dynamic) check(e);
            for (Entity e : actor) check(e);
        }
        run.Hits = combat.Stats().Hits;
        Hash hash;
        for (Entity e : dynamic) hash.Add(glm::vec3(e.GetTransform().Translation.x, e.GetTransform().Translation.y, e.GetTransform().Rotation.z));
        for (Entity e : actor) hash.Add(e.GetTransform().Translation);
        hash.Add(float(combat.LiveCount()));
        run.Hash = hash.Value;
        scene->OnRuntimeStop();
        return run;
    }

    Result Physics2D(const Options& o)
    {
        Result r;
        r.Key = "Physics2D";
        const Run2D a = Simulate2D(o, true), b = Simulate2D(o, true);
        r.Label = std::format("{}x{} m tilemap maydon, {} dinamik jism, {} motor aktor, 128 snaryad (Box2D)", ArenaW, ArenaH,
                              std::min(o.Bodies2D, 4000), std::min(o.Actors, 64));
        r.Frame = FrameTimeStats::Compute(a.Frame);
        r.Physics = FrameTimeStats::Compute(a.Physics);
        r.Allocations = double(a.Allocations) / double(std::max<size_t>(1, a.Frame.size()));
        r.HashA = a.Hash;
        r.HashB = b.Hash;
        r.Escaped = a.Escaped + b.Escaped;
        r.NonFinite = a.NonFinite + b.NonFinite;
        r.TooFast = a.TooFast + b.TooFast;
        r.MaxSpeed = std::max(a.MaxSpeed, b.MaxSpeed);
        r.Hits = a.Hits;
        r.Fired = a.Fired;
        r.Steps = o.Frames;
        return r;
    }

    // ------------------------------------------------------------------ 3D: pit, terrain, stairs, ridge, characters
    constexpr float PitHalf = 12.0f, PitWall = 4.0f;
    // The stair ziggurat: Levels square steps, StepRise high, the lowest HalfBase wide, centred at Stairs.
    constexpr int Levels = 6;
    constexpr float StepRise = 0.25f, HalfBase = 6.0f;
    const glm::vec3 Stairs{ -36.0f, 0.0f, 0.0f };
    // The ridge: a 35 degree ramp up, a flat top, a 35 degree ramp down (along X), centred at Ridge.
    const glm::vec3 Ridge{ 0.0f, 0.0f, 36.0f };
    constexpr float RidgeHeight = 2.0f, RidgeTop = 1.5f, RidgeLength = 18.0f;
    const float RidgeSlope = glm::radians(35.0f);
    // The terrain: 65 x 65 samples over 64 x 64 m (rolling hills, at most ~25 degrees), its corner at TerrainAt.
    const glm::vec3 TerrainAt{ 20.0f, 0.0f, -32.0f };
    constexpr float TerrainSize = 64.0f;

    float TerrainHeightAt(float x, float z) // local x, z in [0, 64]
    {
        return 1.6f + 1.2f * std::sin(x * 0.21f) * std::cos(z * 0.17f) + 0.6f * std::sin((x + z) * 0.11f);
    }

    // The ground under (x, z) in the stair / ridge / floor areas (the terrain is read from the component).
    float AnalyticGround(float x, float z)
    {
        float y = 0.0f;
        const float cheb = std::max(std::abs(x - Stairs.x), std::abs(z - Stairs.z));
        for (int k = 0; k < Levels; ++k)
            if (cheb <= HalfBase - float(k)) y = std::max(y, StepRise * float(k + 1));
        if (std::abs(z - Ridge.z) <= RidgeLength * 0.5f)
        {
            const float run = RidgeHeight / std::tan(RidgeSlope), dx = std::abs(x - Ridge.x);
            if (dx <= RidgeTop) y = std::max(y, RidgeHeight);
            else if (dx <= RidgeTop + run) y = std::max(y, RidgeHeight - (dx - RidgeTop) * std::tan(RidgeSlope));
        }
        return y;
    }

    struct Run3D
    {
        uint64_t Hash = 0;
        std::vector<double> Frame, Physics, Casts;
        uint64_t Allocations = 0, CastHits = 0;
        int Escaped = 0, NonFinite = 0, Sunk = 0, Fell = 0, Slow = 0, Sleeping = 0;
        float MinClearance = 1e9f, MinProgress = 1e9f, MeanProgress = 0;
    };

    Entity StaticBox(Scene& scene, const std::string& name, glm::vec3 center, glm::vec3 half, float rotationZ = 0.0f)
    {
        Entity e = scene.CreateEntity(name);
        e.GetTransform().Translation = center;
        e.GetTransform().Rotation = { 0.0f, 0.0f, rotationZ };
        e.AddComponent<Rigidbody3DComponent>().Type = Rigidbody3DComponent::BodyType::Static;
        e.AddComponent<BoxCollider3DComponent>().HalfExtents = half;
        return e;
    }

    Run3D Simulate3D(const Options& o, bool measure)
    {
        Run3D run;
        auto scene = CreateRef<Scene>();
        auto& settings = scene->GetSettings();
        settings.UseProjectGravity3D = false;
        settings.Gravity3D = { 0.0f, -9.81f, 0.0f };
        // The pit: a floor, four walls and two ramps down into it.
        StaticBox(*scene, "PitFloor", { 0, -0.5f, 0 }, { PitHalf + 1, 0.5f, PitHalf + 1 });
        StaticBox(*scene, "PitWallN", { 0, PitWall * 0.5f, PitHalf + 0.5f }, { PitHalf + 1, PitWall * 0.5f, 0.5f });
        StaticBox(*scene, "PitWallS", { 0, PitWall * 0.5f, -PitHalf - 0.5f }, { PitHalf + 1, PitWall * 0.5f, 0.5f });
        StaticBox(*scene, "PitWallE", { PitHalf + 0.5f, PitWall * 0.5f, 0 }, { 0.5f, PitWall * 0.5f, PitHalf + 1 });
        StaticBox(*scene, "PitWallW", { -PitHalf - 0.5f, PitWall * 0.5f, 0 }, { 0.5f, PitWall * 0.5f, PitHalf + 1 });
        StaticBox(*scene, "PitRampA", { -6, 1.0f, -4 }, { 4, 0.2f, 3 }, glm::radians(-20.0f));
        StaticBox(*scene, "PitRampB", { 6, 1.0f, 4 }, { 4, 0.2f, 3 }, glm::radians(20.0f));
        // The stair course: a floor and the ziggurat on it.
        StaticBox(*scene, "StairFloor", Stairs + glm::vec3(0, -0.5f, 0), { 12, 0.5f, 12 });
        for (int k = 0; k < Levels; ++k)
        {
            const float half = HalfBase - float(k), top = StepRise * float(k + 1);
            StaticBox(*scene, std::format("Step{}", k), Stairs + glm::vec3(0, top - StepRise * 0.5f, 0), { half, StepRise * 0.5f, half });
        }
        // The ridge course: a floor, a flat top and two 35 degree ramps (boxes turned about Z, their top faces meeting
        // the top's edges and the floor).
        StaticBox(*scene, "RidgeFloor", Ridge + glm::vec3(0, -0.5f, 0), { 14, 0.5f, 11 });
        StaticBox(*scene, "RidgeTop", Ridge + glm::vec3(0, RidgeHeight - 0.5f, 0), { RidgeTop, 0.5f, RidgeLength * 0.5f });
        {
            const float run = RidgeHeight / std::tan(RidgeSlope), slant = std::sqrt(run * run + RidgeHeight * RidgeHeight);
            const float thick = 0.4f;
            for (int side : { -1, 1 })
            {
                // the ramp's top face from (top edge, RidgeHeight) to (top edge + run, 0); the box hangs under it
                const glm::vec2 mid{ float(side) * (RidgeTop + run * 0.5f), RidgeHeight * 0.5f };
                const glm::vec2 normal{ float(side) * std::sin(RidgeSlope), std::cos(RidgeSlope) };
                const glm::vec2 c = mid - normal * (thick * 0.5f);
                StaticBox(*scene, side < 0 ? "RidgeUp" : "RidgeDown", Ridge + glm::vec3(c.x, c.y, 0), { slant * 0.5f + 0.05f, thick * 0.5f, RidgeLength * 0.5f },
                          float(-side) * RidgeSlope);
            }
        }
        // The terrain.
        Entity terrainEntity = scene->CreateEntity("Terrain");
        {
            terrainEntity.GetTransform().Translation = TerrainAt;
            auto& t = terrainEntity.AddComponent<TerrainComponent>();
            t.Reset(65, glm::vec2(TerrainSize));
            for (uint32_t z = 0; z < t.Resolution; ++z)
                for (uint32_t x = 0; x < t.Resolution; ++x)
                    t.Heights[size_t(z) * t.Resolution + x] = TerrainHeightAt(float(x) * TerrainSize / 64.0f, float(z) * TerrainSize / 64.0f);
            t.GeometryChanged();
            terrainEntity.AddComponent<Rigidbody3DComponent>().Type = Rigidbody3DComponent::BodyType::Static;
            terrainEntity.AddComponent<MeshCollider3DComponent>();
        }
        // The rigid bodies, dropped into the pit from a jittered lattice.
        Lcg rng(0x3D5EED);
        std::vector<Entity> bodies;
        const int count = std::min(o.Bodies3D, 2048);
        const int side = 8;
        for (int i = 0; i < count; ++i)
        {
            const int layer = i / (side * side), cell = i % (side * side);
            Entity e = scene->CreateEntity(std::format("Body{}", i));
            e.GetTransform().Translation = { -8.75f + 2.5f * float(cell % side) + rng.Uniform(-0.3f, 0.3f), 3.0f + 1.2f * float(layer),
                                             -8.75f + 2.5f * float(cell / side) + rng.Uniform(-0.3f, 0.3f) };
            e.GetTransform().Rotation = { rng.Uniform(0, 3), rng.Uniform(0, 3), rng.Uniform(0, 3) };
            auto& rb = e.AddComponent<Rigidbody3DComponent>();
            rb.Mass = rng.Uniform(1, 8);
            switch (i % 3)
            {
                case 0: e.AddComponent<BoxCollider3DComponent>().HalfExtents = glm::vec3(rng.Uniform(0.2f, 0.4f)); break;
                case 1: e.AddComponent<SphereCollider3DComponent>().Radius = rng.Uniform(0.2f, 0.4f); break;
                default:
                {
                    auto& c = e.AddComponent<CapsuleCollider3DComponent>();
                    c.Radius = 0.2f;
                    c.HalfHeight = 0.3f;
                }
            }
            bodies.push_back(e);
        }
        // The characters: a third on the stairs (circles round the ziggurat's centre crossing its 0.25 m steps), a third
        // on the ridge (rectangles over both 35 degree ramps), a third on the terrain (circles over the hills).
        struct Walker
        {
            Entity E;
            int Course = 0; // 0 stairs, 1 ridge, 2 terrain
            glm::vec3 Center{};
            float Radius = 0, Phase = 0, Lane = 0;
            int Corner = 0;
            double Commanded = 0, Covered = 0;
        };
        std::vector<Walker> walkers;
        const int actors = std::min(o.Actors, 64);
        constexpr float WalkSpeed = 3.0f;
        for (int i = 0; i < actors; ++i)
        {
            Walker w;
            w.Course = i % 3;
            const int n = i / 3;
            w.Phase = float(n) * 0.9f;
            glm::vec3 p{};
            if (w.Course == 0)
            {
                w.Center = Stairs;
                w.Radius = 2.0f + 0.9f * float(n % 4); // circles 0.9 m apart: neighbours never touch
                p = Stairs + glm::vec3(std::cos(w.Phase) * w.Radius, 0, std::sin(w.Phase) * w.Radius);
            }
            else if (w.Course == 1)
            {
                // one lane each, 0.8 m apart (wider than a character), there and back over the ridge
                w.Lane = -8.0f + 0.8f * float(n % 21);
                w.Corner = n % 2;
                p = Ridge + glm::vec3(w.Corner ? 10.0f : -10.0f, 0, w.Lane);
            }
            else
            {
                w.Center = TerrainAt + glm::vec3(TerrainSize * 0.5f, 0, TerrainSize * 0.5f);
                w.Radius = 6.0f + 2.2f * float(n % 10);
                p = w.Center + glm::vec3(std::cos(w.Phase) * w.Radius, 0, std::sin(w.Phase) * w.Radius);
            }
            const float ground = w.Course == 2 ? TerrainHeightAt(p.x - TerrainAt.x, p.z - TerrainAt.z) : AnalyticGround(p.x, p.z);
            w.E = scene->CreateEntity(std::format("Walker{}", i));
            w.E.GetTransform().Translation = p + glm::vec3(0, ground + 0.3f, 0);
            auto& cc = w.E.AddComponent<CharacterController3DComponent>();
            cc.PushBodies = false;
            walkers.push_back(w);
        }
        scene->OnRuntimeStart();
        auto& terrain = terrainEntity.GetComponent<TerrainComponent>();
        auto ground = [&](const Walker& w, glm::vec3 p) {
            if (w.Course == 2) return TerrainTools::SampleHeight(terrain, p.x - TerrainAt.x, p.z - TerrainAt.z);
            // the lowest ground within 5 cm (a step's edge under the feet's centre)
            float y = 1e9f;
            for (glm::vec2 d : { glm::vec2(0), glm::vec2(.05f, 0), glm::vec2(-.05f, 0), glm::vec2(0, .05f), glm::vec2(0, -.05f) })
                y = std::min(y, AnalyticGround(p.x + d.x, p.z + d.y));
            return y;
        };
        const int warmup = 60; // the characters land and the bodies start falling
        Profiler::SetEnabled(true);
        for (int frame = 0; frame < warmup + o.Frames; ++frame)
        {
            for (auto& w : walkers)
            {
                auto& cc = w.E.GetComponent<CharacterController3DComponent>();
                const glm::vec3 p = w.E.GetTransform().Translation;
                glm::vec3 dir{};
                if (w.Course == 1)
                {
                    // x -10 -> 10 on its lane (up 35 degrees, over the top, down 35 degrees) and back
                    auto end = [&](int corner) { return glm::vec2(Ridge.x + (corner ? 10.0f : -10.0f), Ridge.z + w.Lane); };
                    glm::vec2 goal = end(1 - w.Corner);
                    if (glm::distance(glm::vec2(p.x, p.z), goal) < 0.3f)
                    {
                        w.Corner = 1 - w.Corner;
                        goal = end(1 - w.Corner);
                    }
                    const glm::vec2 d = goal - glm::vec2(p.x, p.z);
                    if (glm::length(d) > 1e-4f) dir = glm::vec3(d.x, 0, d.y) / glm::length(d);
                }
                else
                {
                    // a circle: the tangent, pulled back onto the radius
                    const glm::vec2 r = glm::vec2(p.x - w.Center.x, p.z - w.Center.z);
                    const float len = std::max(glm::length(r), 1e-4f);
                    const glm::vec2 tangent = glm::vec2(-r.y, r.x) / len, pull = -(r / len) * std::clamp((len - w.Radius) * 0.8f, -0.6f, 0.6f);
                    const glm::vec2 d = tangent + pull;
                    dir = glm::normalize(glm::vec3(d.x, 0, d.y));
                }
                cc.Move(dir * WalkSpeed);
                if (frame >= warmup) w.Commanded += WalkSpeed * Step;
            }
            std::vector<glm::vec3> before;
            before.reserve(walkers.size());
            for (auto& w : walkers) before.push_back(w.E.GetTransform().Translation);
            Profiler::BeginFrame();
            const auto start = Clock::now();
            uint64_t allocs = 0;
            {
                AllocCounter::Scope scope;
                scene->UpdateRuntime(Step);
                allocs = scope.Count();
            }
            const double ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
            Profiler::BeginFrame();
            // 128 sphere casts across the pit (down onto the pile and sideways over it).
            const auto castStart = Clock::now();
            for (int k = 0; k < 128; ++k)
            {
                const float a = float(k) * 0.049f + float(frame) * 0.013f;
                const glm::vec3 from{ std::cos(a) * 10.0f, 8.0f, std::sin(a * 1.3f) * 10.0f };
                const glm::vec3 dir = k % 2 ? glm::vec3(0, -1, 0) : glm::normalize(glm::vec3(-from.x, -2.0f, -from.z));
                if (scene->SphereCast3D(from, 0.25f, dir, 20.0f)) ++run.CastHits;
            }
            const double castMs = std::chrono::duration<double, std::milli>(Clock::now() - castStart).count();
            if (frame >= warmup)
            {
                run.Frame.push_back(ms);
                run.Physics.push_back(ProfilerScope("Fizika"));
                run.Casts.push_back(castMs);
                run.Allocations += allocs;
            }
            // Invariants of the characters.
            for (size_t i = 0; i < walkers.size(); ++i)
            {
                auto& w = walkers[i];
                const glm::vec3 p = w.E.GetTransform().Translation;
                if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
                {
                    ++run.NonFinite;
                    continue;
                }
                if (p.y < -10.0f) ++run.Fell;
                if (frame < warmup) continue;
                const float clearance = p.y - ground(w, p);
                run.MinClearance = std::min(run.MinClearance, clearance);
                if (clearance < -0.05f) ++run.Sunk;
                const glm::vec3 moved = p - before[i];
                w.Covered += glm::length(glm::vec2(moved.x, moved.z));
            }
            // The bodies stay in the pit.
            for (Entity e : bodies)
            {
                const glm::vec3 p = e.GetTransform().Translation;
                if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) ++run.NonFinite;
                else if (std::abs(p.x) > PitHalf + 0.05f || std::abs(p.z) > PitHalf + 0.05f || p.y < -0.5f) ++run.Escaped;
            }
        }
        double sum = 0;
        for (auto& w : walkers)
        {
            const float progress = w.Commanded > 0 ? float(w.Covered / w.Commanded) : 1.0f;
            run.MinProgress = std::min(run.MinProgress, progress);
            sum += progress;
            if (progress < 0.8f) ++run.Slow;
        }
        run.MeanProgress = walkers.empty() ? 1.0f : float(sum / double(walkers.size()));
        Hash hash;
        for (Entity e : bodies)
        {
            hash.Add(e.GetTransform().Translation);
            hash.Add(e.GetTransform().Rotation);
        }
        for (auto& w : walkers) hash.Add(w.E.GetTransform().Translation);
        run.Hash = hash.Value;
        if (auto* world = scene->GetPhysicsWorld3D())
            for (Entity e : bodies)
                if (const auto& rb = e.GetComponent<Rigidbody3DComponent>(); rb.RuntimeBodyID != 0xffffffff &&
                                                                             glm::length(world->GetLinearVelocity(rb.RuntimeBodyID)) < 1e-6f)
                    ++run.Sleeping;
        (void)measure;
        scene->OnRuntimeStop();
        return run;
    }

    Result Physics3D(const Options& o)
    {
        Result r;
        r.Key = "Physics3D";
        const Run3D a = Simulate3D(o, true), b = Simulate3D(o, true);
        r.Label = std::format("chuqurlik + 65x65 relyef + 0.25 m zinapoya + 35° qiyalik, {} jism, {} Character Controller 3D, 128 sphere cast / kadr (Jolt)",
                              std::min(o.Bodies3D, 2048), std::min(o.Actors, 64));
        r.Frame = FrameTimeStats::Compute(a.Frame);
        r.Physics = FrameTimeStats::Compute(a.Physics);
        r.Casts = FrameTimeStats::Compute(a.Casts);
        r.Allocations = double(a.Allocations) / double(std::max<size_t>(1, a.Frame.size()));
        r.HashA = a.Hash;
        r.HashB = b.Hash;
        r.Escaped = a.Escaped + b.Escaped;
        r.NonFinite = a.NonFinite + b.NonFinite;
        r.Sunk = a.Sunk + b.Sunk;
        r.Fell = a.Fell + b.Fell;
        r.Slow = a.Slow + b.Slow;
        r.MinClearance = std::min(a.MinClearance, b.MinClearance);
        r.MinProgress = std::min(a.MinProgress, b.MinProgress);
        r.MeanProgress = a.MeanProgress;
        r.CastHits = a.CastHits;
        r.Sleeping = a.Sleeping;
        r.Steps = o.Frames;
        return r;
    }
}

int RunPhysicsBench(int argc, char** argv)
{
    Options o;
    const char* jsonFile = nullptr;
    for (int i = 1; i < argc; ++i)
    {
        auto number = [&](int& value, int low, int high) {
            if (i + 1 < argc) value = std::clamp(std::atoi(argv[++i]), low, high);
        };
        if (!std::strcmp(argv[i], "--physics")) continue;
        else if (!std::strcmp(argv[i], "--bodies"))
        {
            int n = 0;
            number(n, 1, 4000);
            o.Bodies2D = n;
            o.Bodies3D = std::min(n, 2048);
        }
        else if (!std::strcmp(argv[i], "--actors")) number(o.Actors, 1, 64);
        else if (!std::strcmp(argv[i], "--frames")) number(o.Frames, 1, 100000);
        else if (!std::strcmp(argv[i], "--2d")) o.ThreeD = false;
        else if (!std::strcmp(argv[i], "--3d")) o.TwoD = false;
        else if (!std::strcmp(argv[i], "--json") && i + 1 < argc) jsonFile = argv[++i];
        else
        {
            std::printf("ZaminBench --physics [--2d | --3d] [--bodies N] [--actors N] [--frames N] [--json <fayl>]\n");
            return 1;
        }
    }
#if defined(NDEBUG)
    const char* build = "Release";
#else
    const char* build = "Debug";
#endif
    Log::SetMinLevel(LogLevel::Warn);
    std::printf("Zamin fizika stress benchmarki: 60 Hz, %d kadr (+ isinish), har ssenariy ikki marta (determinizm), %s\n\n", o.Frames, build);
    std::vector<Result> results;
    if (o.TwoD) results.push_back(Physics2D(o));
    if (o.ThreeD) results.push_back(Physics3D(o));
    bool passed = true;
    for (auto& r : results)
    {
        std::printf("%s: %s\n", r.Key.c_str(), r.Label.c_str());
        std::printf("  kadr (UpdateRuntime): o'rtacha %.3f ms, mediana %.3f, p95 %.3f, eng sekin %.3f ms\n", r.Frame.Average, r.Frame.Median, r.Frame.P95,
                    r.Frame.Max);
        std::printf("  fizika qadami (\"Fizika\"): o'rtacha %.3f ms, p95 %.3f ms; ajratishlar: %.1f / kadr\n", r.Physics.Average, r.Physics.P95, r.Allocations);
        if (r.Key == "Physics2D")
            std::printf("  maydondan chiqqan: %d, NaN: %d, eng katta tezlik %.2f m/s (chegara %.0f), tez: %d; snaryad: %llu otilgan, %llu tekkan\n", r.Escaped,
                        r.NonFinite, r.MaxSpeed, SpeedCap2D, r.TooFast, (unsigned long long)r.Fired, (unsigned long long)r.Hits);
        else
            std::printf("  sphere cast (128): o'rtacha %.3f ms, p95 %.3f ms, tekkan %llu; oyoq-yer orasi eng kam %.3f m (chegara -0.05), botgan: %d, "
                        "y<-10: %d; yurilgan masofa eng kam %.1f %%, o'rtacha %.1f %% (chegara 80 %%), sekin: %d; chuqurlikdan chiqqan jism: %d, NaN: %d, "
                        "uxlagan: %d\n",
                        r.Casts.Average, r.Casts.P95, (unsigned long long)r.CastHits, r.MinClearance, r.Sunk, r.Fell, r.MinProgress * 100.0f,
                        r.MeanProgress * 100.0f, r.Slow, r.Escaped, r.NonFinite, r.Sleeping);
        std::printf("  determinizm: %016llx %s %016llx; invariantlar: %s\n", (unsigned long long)r.HashA, r.HashA == r.HashB ? "==" : "!=",
                    (unsigned long long)r.HashB, r.Passed() ? "o'tdi" : "BUZILDI");
        passed = passed && r.Passed();
    }
    if (jsonFile && *jsonFile)
    {
        std::string out = std::format("{{\n  \"Build\": \"{}\",\n  \"Frames\": {},\n  \"Bodies2D\": {},\n  \"Bodies3D\": {},\n  \"Actors\": {},\n  \"Results\": {{\n",
                                      build, o.Frames, o.Bodies2D, o.Bodies3D, o.Actors);
        std::vector<std::string> rows, allocs, inv;
        for (auto& r : results)
        {
            rows.push_back(std::format("    \"{}.Frame\": {{ \"Ms\": {:.4f}, \"PerFrame\": true }}", r.Key, r.Frame.Average));
            rows.push_back(std::format("    \"{}.FrameP95\": {{ \"Ms\": {:.4f}, \"PerFrame\": true }}", r.Key, r.Frame.P95));
            rows.push_back(std::format("    \"{}.Step\": {{ \"Ms\": {:.4f}, \"PerFrame\": true }}", r.Key, r.Physics.Average));
            if (r.Key == "Physics3D") rows.push_back(std::format("    \"{}.Casts\": {{ \"Ms\": {:.4f}, \"PerFrame\": true }}", r.Key, r.Casts.Average));
            allocs.push_back(std::format("    \"{}\": {:.2f}", r.Key, r.Allocations));
            inv.push_back(std::format("    \"{}\": {{ \"Passed\": {}, \"Deterministic\": {}, \"Escaped\": {}, \"NonFinite\": {}, \"TooFast\": {}, \"MaxSpeed\": {:.3f}, "
                                      "\"Sunk\": {}, \"Fell\": {}, \"MinClearance\": {:.4f}, \"MinProgress\": {:.4f}, \"MeanProgress\": {:.4f}, \"Hits\": {}, "
                                      "\"CastHits\": {}, \"Hash\": \"{:016x}\", \"MedianMs\": {:.4f}, \"MaxMs\": {:.4f} }}",
                                      r.Key, r.Passed() ? "true" : "false", r.HashA == r.HashB ? "true" : "false", r.Escaped, r.NonFinite, r.TooFast,
                                      r.MaxSpeed, r.Sunk, r.Fell, r.Key == "Physics3D" ? r.MinClearance : 0.0f, r.Key == "Physics3D" ? r.MinProgress : 1.0f,
                                      r.Key == "Physics3D" ? r.MeanProgress : 1.0f, r.Hits, r.CastHits, r.HashA, r.Frame.Median, r.Frame.Max));
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
    if (!passed) std::printf("\nFIZIKA STRESS: invariant buzildi\n");
    return passed ? 0 : 3;
}
