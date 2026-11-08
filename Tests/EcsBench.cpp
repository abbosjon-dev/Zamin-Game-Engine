// ZaminBench --ecs [--entities N] [--frames N] [--json <file>]: what the scene's entity-component system costs,
// headless (task 431). N entities (10 000 by default) are created, given components, iterated, put in a
// hierarchy, looked up, copied and destroyed through the Scene API the editor, the loader and the scripts use,
// next to the bare EnTT registry underneath (no names, ids, scene order or hierarchy) for comparison; then
// runtime frames of a scene that size: idle, with N / 10 scripts, and spawning / destroying 500 entities a
// frame. Every number is the median of 3 runs (a fresh scene each), in ms, with the cost per item in ns.
// docs/Benchmarks.md has the baseline.
#include <Zamin/Zamin.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <format>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <vector>

using namespace Zamin;

namespace {

    using Clock = std::chrono::steady_clock;

    double Ms(Clock::time_point start) { return std::chrono::duration<double, std::milli>(Clock::now() - start).count(); }

    // A script that moves its entity every frame (the per-frame cost of a script).
    class EcsBenchMover : public ScriptableEntity
    {
    public:
        float Speed = 1.0f;
        void OnUpdate(Timestep ts) override { Transform().Translation.x += Speed * (float)ts; }
    };

    struct EcsRow
    {
        std::string Key, Label;
        double Ms = 0.0;
        size_t Items = 0;
        bool PerFrame = false;
    };

    struct EcsReport
    {
        std::vector<EcsRow> Rows;

        void Section(const char* title) { std::printf("\n%s\n", title); }

        void Add(std::string key, std::string label, double ms, size_t items, bool perFrame = false)
        {
            const double ns = items > 0 ? ms * 1e6 / (double)items : 0.0;
            std::printf("  %-58s %10.3f %-7s %9.1f ns / ta  (%zu)\n", label.c_str(), ms, perFrame ? "ms/kadr" : "ms", ns, items);
            Rows.push_back({ std::move(key), std::move(label), ms, items, perFrame });
        }

        std::string Json(const std::string& build, int entities, int frames) const
        {
            std::string out = std::format("{{\n  \"Build\": \"{}\",\n  \"Entities\": {},\n  \"Frames\": {},\n  \"Results\": {{\n", build, entities, frames);
            for (size_t i = 0; i < Rows.size(); i++)
            {
                const EcsRow& r = Rows[i];
                out += std::format("    \"{}\": {{ \"Ms\": {:.4f}, \"Items\": {}, \"NsPerItem\": {:.2f}{} }}{}\n", r.Key, r.Ms, r.Items,
                                   r.Items > 0 ? r.Ms * 1e6 / (double)r.Items : 0.0, r.PerFrame ? ", \"PerFrame\": true" : "",
                                   i + 1 < Rows.size() ? "," : "");
            }
            return out + "  }\n}\n";
        }
    };

    // The median of `runs` timings: `setup` (not timed) makes a fresh state, `work` is timed on it, `teardown`
    // (not timed) runs after it.
    template<typename State>
    double Median(int runs, const std::function<State()>& setup, const std::function<void(State&)>& work,
                  const std::function<void(State&)>& teardown = {})
    {
        std::vector<double> times;
        for (int i = 0; i < runs; i++)
        {
            State state = setup();
            const auto start = Clock::now();
            work(state);
            times.push_back(Ms(start));
            if (teardown)
                teardown(state);
        }
        std::sort(times.begin(), times.end());
        return times[times.size() / 2];
    }

    struct RegistryState
    {
        std::unique_ptr<entt::registry> Registry = std::make_unique<entt::registry>();
        std::vector<entt::entity> Entities;
    };

    struct SceneState
    {
        Ref<Zamin::Scene> World;
        std::vector<Entity> Entities; // in creation order
    };

    // `count` entities named "Obyekt i" on a grid; `components`: a sprite on every one, a 2D body and a box on
    // every fifth; `hierarchy`: every tenth is the parent of the nine after it.
    SceneState MakeScene(int count, bool components, bool hierarchy)
    {
        SceneState s{ CreateRef<Scene>(), {} };
        s.Entities.reserve((size_t)count);
        Entity group;
        for (int i = 0; i < count; i++)
        {
            Entity e = s.World->CreateEntity(std::format("Obyekt {}", i));
            e.GetTransform().Translation = { (float)(i % 100), (float)(i / 100), 0.0f };
            if (components)
            {
                e.AddComponent<SpriteRendererComponent>().Color = { 1.0f, 0.5f, 0.25f, 1.0f };
                if (i % 5 == 0)
                {
                    e.AddComponent<Rigidbody2DComponent>().Type = Rigidbody2DComponent::BodyType::Dynamic;
                    e.AddComponent<BoxCollider2DComponent>();
                }
            }
            if (hierarchy)
            {
                if (i % 10 == 0)
                    group = e;
                else
                    s.World->SetParent(e, group, false);
            }
            s.Entities.push_back(e);
        }
        return s;
    }

}

ZM_REGISTER_SCRIPT(EcsBenchMover, ZM_FIELD(EcsBenchMover, Speed))

int RunEcsBench(int count, int frames, const char* jsonFile)
{
    Log::SetMinLevel(LogLevel::Error);
#if defined(NDEBUG)
    const char* build = "Release";
#else
    const char* build = "Debug";
#endif
    const int runs = 3;
    const size_t n = (size_t)count;
    std::mt19937 rng(431);
    [[maybe_unused]] static volatile double s_Sink; // a volatile store: the work is not optimised away
    double sink = 0.0;
    std::printf("Zamin ECS benchmark: %d obyekt, %s, har raqam 3 marta o'lchanib mediana (ms, ns / ta)\n", count, build);
    EcsReport report;

    // ---- the bare registry: what the Scene API adds on top of it shows next to these
    report.Section("EnTT registry (taqqoslash uchun: nom, UUID xaritasi, sahna tartibi, ierarxiya yo'q)");
    auto filledRegistry = [count]() {
        RegistryState r;
        r.Entities.reserve((size_t)count);
        for (int i = 0; i < count; i++)
        {
            const entt::entity e = r.Registry->create();
            r.Registry->emplace<TransformComponent>(e);
            if (i % 2 == 0)
                r.Registry->emplace<SpriteRendererComponent>(e);
            r.Entities.push_back(e);
        }
        return r;
    };
    report.Add("EnTT.Create", "create + emplace<Transform> (+ yarmiga Sprite)",
               Median<RegistryState>(runs, []() { return RegistryState(); },
                                     [count](RegistryState& r) {
                                         for (int i = 0; i < count; i++)
                                         {
                                             const entt::entity e = r.Registry->create();
                                             r.Registry->emplace<TransformComponent>(e);
                                             if (i % 2 == 0)
                                                 r.Registry->emplace<SpriteRendererComponent>(e);
                                         }
                                     }),
               n);
    report.Add("EnTT.View1", "view<Transform>: har biriga yozish (x10)",
               Median<RegistryState>(runs, filledRegistry,
                                     [](RegistryState& r) {
                                         for (int k = 0; k < 10; k++)
                                             for (auto [e, t] : r.Registry->view<TransformComponent>().each())
                                                 t.Translation.x += 1.0f;
                                     }) / 10.0,
               n);
    report.Add("EnTT.View2", "view<Transform, Sprite> (yarmi mos, x10)",
               Median<RegistryState>(runs, filledRegistry,
                                     [&sink](RegistryState& r) {
                                         for (int k = 0; k < 10; k++)
                                             for (auto [e, t, sprite] : r.Registry->view<TransformComponent, SpriteRendererComponent>().each())
                                                 sink += (double)(t.Translation.x * sprite.Color.r);
                                     }) / 10.0,
               (n + 1) / 2);
    report.Add("EnTT.Destroy", "destroy (har biri alohida, eng eskisidan)",
               Median<RegistryState>(runs, filledRegistry,
                                     [](RegistryState& r) {
                                         for (entt::entity e : r.Entities)
                                             r.Registry->destroy(e);
                                     }),
               n);

    // ---- creating and components
    report.Section("Sahna: yaratish va komponentlar");
    auto empty = []() { return SceneState{ CreateRef<Scene>(), {} }; };
    report.Add("Scene.CreateEntity", "CreateEntity(nom): ID, Transform, Tag, Relationship, UUID xaritasi",
               Median<SceneState>(runs, empty,
                                  [count](SceneState& s) {
                                      for (int i = 0; i < count; i++)
                                          s.World->CreateEntity(std::format("Obyekt {}", i));
                                  }),
               n);
    std::vector<UUID> ids(n);
    for (UUID& id : ids)
        id = UUID();
    report.Add("Scene.CreateEntityWithUUID", "CreateEntityWithUUID (yuklovchi yo'li)",
               Median<SceneState>(runs, empty,
                                  [&ids](SceneState& s) {
                                      for (size_t i = 0; i < ids.size(); i++)
                                          s.World->CreateEntityWithUUID(ids[i], "Obyekt");
                                  }),
               n);
    auto flat = [count]() { return MakeScene(count, false, false); };
    report.Add("Scene.AddSprite", "AddComponent<SpriteRenderer> har biriga",
               Median<SceneState>(runs, flat,
                                  [](SceneState& s) {
                                      for (Entity e : s.Entities)
                                          e.AddComponent<SpriteRendererComponent>();
                                  }),
               n);
    report.Add("Scene.AddBody2D", "AddComponent<Rigidbody2D> + <BoxCollider2D> (beshdan biriga)",
               Median<SceneState>(runs, flat,
                                  [](SceneState& s) {
                                      for (size_t i = 0; i < s.Entities.size(); i += 5)
                                      {
                                          s.Entities[i].AddComponent<Rigidbody2DComponent>();
                                          s.Entities[i].AddComponent<BoxCollider2DComponent>();
                                      }
                                  }),
               (n + 4) / 5 * 2);
    auto full = [count]() { return MakeScene(count, true, false); };
    report.Add("Scene.RemoveSprite", "RemoveComponent<SpriteRenderer> har biridan",
               Median<SceneState>(runs, full,
                                  [](SceneState& s) {
                                      for (Entity e : s.Entities)
                                          e.RemoveComponent<SpriteRendererComponent>();
                                  }),
               n);

    // ---- iterating and looking up
    report.Section("Sahna: aylanib chiqish va qidirish");
    report.Add("Scene.View1", "GetAllEntitiesWith<Transform>: har biriga yozish (x10)",
               Median<SceneState>(runs, full,
                                  [](SceneState& s) {
                                      for (int k = 0; k < 10; k++)
                                          for (auto e : s.World->GetAllEntitiesWith<TransformComponent>())
                                              s.World->GetRegistry().get<TransformComponent>(e).Translation.x += 1.0f;
                                  }) / 10.0,
               n);
    report.Add("Scene.View2", "GetAllEntitiesWith<Transform, Sprite> (x10)",
               Median<SceneState>(runs, full,
                                  [&sink](SceneState& s) {
                                      for (int k = 0; k < 10; k++)
                                          for (auto [e, t, sprite] : s.World->GetAllEntitiesWith<TransformComponent, SpriteRendererComponent>().each())
                                              sink += (double)(t.Translation.x * sprite.Color.r);
                                  }) / 10.0,
               n);
    report.Add("Scene.View3Sparse", "GetAllEntitiesWith<Transform, Rigidbody2D, BoxCollider2D> (5 %, x10)",
               Median<SceneState>(runs, full,
                                  [&sink](SceneState& s) {
                                      for (int k = 0; k < 10; k++)
                                          for (auto [e, t, body, box] :
                                               s.World->GetAllEntitiesWith<TransformComponent, Rigidbody2DComponent, BoxCollider2DComponent>().each())
                                              sink += (double)(t.Translation.y + box.Size.x);
                                  }) / 10.0,
               (n + 4) / 5);
    report.Add("Scene.EntityGetComponent", "Entity::GetComponent<Transform> sahna tartibida (skriptlar kabi)",
               Median<SceneState>(runs, full,
                                  [&sink](SceneState& s) {
                                      for (Entity e : s.Entities)
                                          sink += (double)e.GetComponent<TransformComponent>().Translation.x;
                                  }),
               n);
    std::vector<size_t> shuffled(n);
    for (size_t i = 0; i < n; i++)
        shuffled[i] = i;
    std::shuffle(shuffled.begin(), shuffled.end(), rng);
    report.Add("Scene.GetComponentRandom", "Entity::GetComponent<Transform> tasodifiy tartibda",
               Median<SceneState>(runs, full,
                                  [&](SceneState& s) {
                                      for (size_t i : shuffled)
                                          sink += (double)s.Entities[i].GetComponent<TransformComponent>().Translation.y;
                                  }),
               n);
    report.Add("Scene.TryGetComponent", "Entity::TryGetComponent<Rigidbody2D> (20 % bor)",
               Median<SceneState>(runs, full,
                                  [&sink](SceneState& s) {
                                      for (Entity e : s.Entities)
                                          if (const auto* body = e.TryGetComponent<Rigidbody2DComponent>())
                                              sink += (double)body->GravityScale;
                                  }),
               n);
    struct LookupState
    {
        SceneState Scene;
        std::vector<UUID> Ids; // shuffled
    };
    report.Add("Scene.GetEntityByUUID", "GetEntityByUUID tasodifiy tartibda",
               Median<LookupState>(runs,
                                   [&]() {
                                       LookupState l{ MakeScene(count, true, false), {} };
                                       l.Ids.reserve(n);
                                       for (size_t i : shuffled)
                                           l.Ids.push_back(l.Scene.Entities[i].GetUUID());
                                       return l;
                                   },
                                   [&sink](LookupState& l) {
                                       for (UUID id : l.Ids)
                                           sink += (double)(uint32_t)l.Scene.World->GetEntityByUUID(id);
                                   }),
               n);
    report.Add("Scene.EntitiesInOrder", "GetEntitiesInOrder (sahna tartibidagi ro'yxat)",
               Median<SceneState>(runs, full, [&sink](SceneState& s) { sink += (double)s.World->GetEntitiesInOrder().size(); }), n);

    // ---- hierarchy
    report.Section("Sahna: ierarxiya (har 10-obyekt keyingi 9 tasining ota-onasi)");
    report.Add("Scene.SetParent", "SetParent (dunyo transformi saqlanadi)",
               Median<SceneState>(runs, full,
                                  [](SceneState& s) {
                                      Entity group;
                                      for (size_t i = 0; i < s.Entities.size(); i++)
                                      {
                                          if (i % 10 == 0)
                                              group = s.Entities[i];
                                          else
                                              s.World->SetParent(s.Entities[i], group, true);
                                      }
                                  }),
               n - (n + 9) / 10);
    auto grouped = [count]() { return MakeScene(count, true, true); };
    report.Add("Scene.WorldTransform", "GetWorldTransform har biriga (chuqurlik 2)",
               Median<SceneState>(runs, grouped,
                                  [&sink](SceneState& s) {
                                      for (Entity e : s.Entities)
                                          sink += (double)s.World->GetWorldTransform(e)[3].x;
                                  }),
               n);
    report.Add("Scene.HierarchyOrder", "GetEntitiesInHierarchyOrder",
               Median<SceneState>(runs, grouped, [&sink](SceneState& s) { sink += (double)s.World->GetEntitiesInHierarchyOrder().size(); }), n);
    report.Add("Scene.GetChildren", "GetChildren har bir guruhga",
               Median<SceneState>(runs, grouped,
                                  [&sink](SceneState& s) {
                                      for (size_t i = 0; i < s.Entities.size(); i += 10)
                                          sink += (double)s.World->GetChildren(s.Entities[i]).size();
                                  }),
               (n + 9) / 10);
    const size_t groupCopies = std::max<size_t>(1, n / 100);
    report.Add("Scene.Duplicate", std::format("DuplicateEntity: {} guruh (10 obyektdan)", groupCopies),
               Median<SceneState>(runs, grouped,
                                  [groupCopies](SceneState& s) {
                                      for (size_t g = 0; g < groupCopies; g++)
                                          s.World->DuplicateEntity(s.Entities[(g * 10) % s.Entities.size()]);
                                  }),
               groupCopies * 10);
    report.Add("Scene.Copy", "Scene::Copy (editor Play nusxasi)",
               Median<SceneState>(runs, grouped, [&sink](SceneState& s) { sink += (double)Scene::Copy(s.World)->GetEntitiesInOrder().size(); }), n);

    // ---- destroying
    report.Section("Sahna: o'chirish (sahna tartibi saqlanadi)");
    report.Add("Destroy.NewestFirst", "DestroyEntity: eng yangisidan boshlab",
               Median<SceneState>(runs, full,
                                  [](SceneState& s) {
                                      for (auto it = s.Entities.rbegin(); it != s.Entities.rend(); ++it)
                                          s.World->DestroyEntity(*it);
                                  }),
               n);
    report.Add("Destroy.Random", "DestroyEntity: tasodifiy tartibda",
               Median<SceneState>(runs, full,
                                  [&shuffled](SceneState& s) {
                                      for (size_t i : shuffled)
                                          s.World->DestroyEntity(s.Entities[i]);
                                  }),
               n);
    const size_t oldest = std::min<size_t>(n, 1000);
    report.Add("Destroy.OldestFirst", std::format("DestroyEntity: eng eskisidan boshlab ({} ta, sahnada {})", oldest, n),
               Median<SceneState>(runs, full,
                                  [oldest](SceneState& s) {
                                      for (size_t i = 0; i < oldest; i++)
                                          s.World->DestroyEntity(s.Entities[i]);
                                  }),
               oldest);
    report.Add("Destroy.Batch", "DestroyEntities(hammasi) - bitta to'plam",
               Median<SceneState>(runs, full, [](SceneState& s) { s.World->DestroyEntities(s.Entities); }), n);
    report.Add("Destroy.Groups", "DestroyEntity guruh ildiziga (bolalari bilan)",
               Median<SceneState>(runs, grouped,
                                  [](SceneState& s) {
                                      for (size_t i = 0; i < s.Entities.size(); i += 10)
                                          s.World->DestroyEntity(s.Entities[i]);
                                  }),
               n);
    // In a running scene (no bodies: the frame is the queue's flush): a script's Destroy waits for the frame's end.
    report.Add("Destroy.Deferred", "DestroyEntityDeferred eng eskisidan + kadr oxiridagi bo'shatish",
               Median<SceneState>(runs,
                                  [count]() {
                                      SceneState s = MakeScene(count, false, false);
                                      for (Entity e : s.Entities)
                                          e.AddComponent<SpriteRendererComponent>();
                                      s.World->OnRuntimeStart();
                                      return s;
                                  },
                                  [](SceneState& s) {
                                      for (Entity e : s.Entities)
                                          s.World->DestroyEntityDeferred(e);
                                      s.World->UpdateRuntime(Timestep(1.0f / 60.0f)); // FlushDestroyQueue at the frame's end
                                  },
                                  [](SceneState& s) { s.World->OnRuntimeStop(); }),
               n);

    // ---- runtime frames of a scene this size (no camera: nothing is drawn, the update only)
    report.Section(std::format("O'yin kadri (UpdateRuntime, {} kadr o'rtachasi; kamerasiz - faqat yangilash)", frames).c_str());
    const float dt = 1.0f / 60.0f;
    auto runFrames = [&](SceneState& s, const std::function<void(int)>& beforeFrame) {
        s.World->OnRuntimeStart();
        for (int f = 0; f < 10; f++) // warm up (the scripts' first frames, the caches)
        {
            if (beforeFrame)
                beforeFrame(-1 - f);
            s.World->UpdateRuntime(Timestep(dt));
        }
        const auto start = Clock::now();
        for (int f = 0; f < frames; f++)
        {
            if (beforeFrame)
                beforeFrame(f);
            s.World->UpdateRuntime(Timestep(dt));
        }
        const double perFrame = Ms(start) / (double)frames;
        s.World->OnRuntimeStop();
        return perFrame;
    };
    auto noBodies = [count]() {
        SceneState s = MakeScene(count, false, true);
        for (Entity e : s.Entities)
            e.AddComponent<SpriteRendererComponent>();
        return s;
    };
    {
        std::vector<double> times;
        for (int r = 0; r < runs; r++)
        {
            SceneState s = noBodies();
            times.push_back(runFrames(s, {}));
        }
        std::sort(times.begin(), times.end());
        report.Add("Frame.Idle", "bo'sh kadr: spritelar, ierarxiya (fizikasiz, skriptsiz)", times[1], n, true);
    }
    const size_t scripts = std::max<size_t>(1, n / 10);
    {
        std::vector<double> times;
        for (int r = 0; r < runs; r++)
        {
            SceneState s = noBodies();
            for (size_t i = 0; i < s.Entities.size(); i += 10)
            {
                auto& nsc = s.Entities[i].AddComponent<NativeScriptComponent>();
                nsc.ClassName = "EcsBenchMover";
            }
            times.push_back(runFrames(s, {}));
        }
        std::sort(times.begin(), times.end());
        report.Add("Frame.Scripts", std::format("{} skript OnUpdate (har biri Transform'ni o'zgartiradi)", scripts), times[1], scripts, true);
    }
    const int churn = 500;
    {
        std::vector<double> times, spawnTimes;
        for (int r = 0; r < runs; r++)
        {
            SceneState s = noBodies();
            Entity bullet = s.World->CreateEntity("O'q");
            bullet.AddComponent<SpriteRendererComponent>().Color = { 1.0f, 1.0f, 0.0f, 1.0f };
            std::vector<Entity> alive;
            double spawnMs = 0.0;
            // Every frame: `churn` new bullets, the oldest `churn` (after 4 frames of them) are destroyed - as a
            // script's Instantiate / Destroy do (Destroy waits for the frame's end).
            times.push_back(runFrames(s, [&](int frame) {
                const auto start = Clock::now();
                for (int i = 0; i < churn; i++)
                    alive.push_back(s.World->Instantiate(bullet, { (float)i * 0.1f, (float)(frame & 63), 0.0f }));
                if (frame >= 0)
                    spawnMs += Ms(start);
                if (alive.size() > (size_t)churn * 4)
                {
                    for (int i = 0; i < churn; i++)
                        s.World->DestroyEntityDeferred(alive[(size_t)i]);
                    alive.erase(alive.begin(), alive.begin() + churn);
                }
            }));
            spawnTimes.push_back(spawnMs / (double)frames);
        }
        std::sort(times.begin(), times.end());
        std::sort(spawnTimes.begin(), spawnTimes.end());
        report.Add("Frame.Churn", std::format("{} Instantiate + {} Destroy har kadr ({} obyektli sahnada)", churn, churn, n), times[1],
                   (size_t)churn * 2, true);
        report.Add("Frame.ChurnSpawn", std::format("  shundan {} Instantiate (o'yin davomida)", churn), spawnTimes[1], (size_t)churn, true);
        report.Add("Frame.ChurnRest", std::format("  qolgani: kadr yangilanishi va {} Destroy", churn), times[1] - spawnTimes[1], (size_t)churn,
                   true);
    }
    s_Sink = sink;

    if (jsonFile && *jsonFile)
    {
        FileSystem::WriteTextFile(jsonFile, report.Json(build, count, frames));
        std::printf("\nJSON: %s\n", jsonFile);
    }
    return 0;
}
