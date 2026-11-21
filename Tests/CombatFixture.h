#pragma once
// Shared scene fixture for the CombatWorld/effect tests (one copy per test TU).
#include "TestAssertions.h"
#include <Zamin/Gameplay/CombatWorld.h>
#include <Zamin/Gameplay/LoadoutWorld.h>
#include <Zamin/Gameplay/SaveWorld.h>
#include <Zamin/Gameplay/GameplayRuntime.h>
#include <Zamin/Core/SaveData.h>
#include <Zamin/Project/Project.h>
#include <Zamin/Scene/Entity.h>
#include <glm/gtc/constants.hpp>
#include <cmath>
#include <filesystem>
#include <format>
#include <functional>
using namespace Zamin;
namespace {
    namespace G = Zamin::Gameplay;
    using Gameplay::CombatEvent;
    struct CFixture
    {
        Scene World;
        Entity Hero, Enemy;
        std::filesystem::path Root;
        explicit CFixture(const std::function<void(CFixture&)>& setup = {})
        {
            Root = std::filesystem::temp_directory_path() / ("zamin-combat-" + std::to_string(uint64_t(UUID())));
            std::filesystem::create_directories(Root);
            SaveData::SetFilePath(Root / "prefs.json");
            World.SetName("CombatTest");
            Hero = World.CreateEntityWithUUID(UUID(100), "Hero");
            Enemy = World.CreateEntityWithUUID(UUID(200), "Enemy");
            Enemy.GetTransform().Translation = { 5, 0, 0 };
            if (setup) setup(*this);
            World.OnRuntimeStart();
            Health(Hero, 1);
            Health(Enemy, 2);
        }
        ~CFixture()
        {
            World.OnRuntimeStop();
            SaveData::SetFilePath({});
            std::error_code ec;
            std::filesystem::remove_all(Root, ec);
        }
        Gameplay::RuleWorld& Rules() { return Gameplay::RuleWorld::For(World); }
        Gameplay::CombatWorld& Combat() { return Gameplay::CombatWorld::For(World); }
        void Health(Entity e, uint16_t team, double maximum = 100, double grace = 0)
        {
            std::string error;
            CHECK(Rules().Configure(e, { maximum, 0, grace, .1, team }, true, error));
        }
        Entity Actor(uint64_t id, glm::vec3 position, uint16_t team, double maximum = 100)
        {
            auto e = World.CreateEntityWithUUID(UUID(id), "Actor" + std::to_string(id));
            e.GetTransform().Translation = position;
            Health(e, team, maximum);
            return e;
        }
        static Gameplay::ProjectileSpec Bolt(bool twoD = true, std::string key = "Bolt")
        {
            Gameplay::ProjectileSpec s;
            s.Key = key;
            s.Speed = 10;
            s.Damage = 20;
            s.TwoD = twoD;
            return s;
        }
        void Define(const Gameplay::ProjectileSpec& spec)
        {
            std::string error;
            CHECK(Combat().DefineProjectile(spec, error));
            CHECK(error.empty());
        }
        Gameplay::FireResult Fire(const std::string& key, glm::vec3 direction = { 1, 0, 0 }, Entity owner = {}, Entity target = {}, glm::vec3 origin = {})
        {
            Gameplay::FireRequest r;
            r.Key = key;
            r.Owner = owner ? owner : Hero;
            r.Direction = direction;
            r.Target = target;
            r.Origin = origin;
            return Combat().Fire(r);
        }
        void Run(int frames, float delta = 1.0f / 60)
        {
            for (int i = 0; i < frames; ++i) World.UpdateRuntime(delta);
        }
        std::vector<CombatEvent> Drain(Entity e)
        {
            std::vector<CombatEvent> out;
            while (auto event = Combat().Poll(e)) out.push_back(*event);
            return out;
        }
        double HP(Entity e) { return Rules().Health(e)->Current; }
    };
    [[maybe_unused]] G::EffectSpec Effect(const std::string& key, double duration = 3, G::StackRule rule = G::StackRule::Refresh, int stacks = 1)
    {
        G::EffectSpec s;
        s.Key = key;
        s.Duration = duration;
        s.Rule = rule;
        s.MaxStacks = stacks;
        return s;
    }
    [[maybe_unused]] G::EffectSpec Dot(const std::string& key, double duration, double period, double amount, G::StackRule rule = G::StackRule::Refresh, int stacks = 1)
    {
        auto s = Effect(key, duration, rule, stacks);
        s.Period = period;
        s.Tick = G::TickKind::Damage;
        s.TickAmount = amount;
        return s;
    }
    [[maybe_unused]] G::EffectSpec Buff(const std::string& key, double duration, G::CombatStats add, double speed = 1)
    {
        auto s = Effect(key, duration);
        s.Hostile = false;
        s.Add = add;
        s.Speed = speed;
        return s;
    }
    [[maybe_unused]] G::AreaSpec Area(const std::string& key, double radius, double amount, bool twoD = true)
    {
        G::AreaSpec s;
        s.Key = key;
        s.Radius = radius;
        s.Amount = amount;
        s.TwoD = twoD;
        s.LineOfSight = false;
        return s;
    }
    [[maybe_unused]] void Define(CFixture& f, const G::EffectSpec& s)
    {
        std::string e;
        CHECK(f.Combat().DefineEffect(s, e));
        CHECK(e.empty());
    }
    [[maybe_unused]] void Define(CFixture& f, const G::AreaSpec& s)
    {
        std::string e;
        CHECK(f.Combat().DefineArea(s, e));
        CHECK(e.empty());
    }
    [[maybe_unused]] G::AbilitySpec Ability(const std::string& key, double amount, double cost, double cooldown)
    {
        G::AbilitySpec a;
        a.Key = key;
        a.Amount = amount;
        a.Cost = cost;
        a.Cooldown = cooldown;
        a.Range = 10;
        return a;
    }
    [[maybe_unused]] std::vector<G::RuleEvent> RuleEvents(CFixture& f, Entity e)
    {
        std::vector<G::RuleEvent> out;
        while (auto x = f.Rules().Poll(e)) out.push_back(*x);
        return out;
    }
    [[maybe_unused]] G::RuleQuery Query2D()
    {
        G::RuleQuery q;
        q.TwoD = true;
        q.LineOfSight = false;
        q.Range = 10;
        q.Origin = { 0, 0, 0 };
        return q;
    }
    using Outcome = G::ApplyOutcome;
    [[maybe_unused]] size_t Count(const std::vector<CombatEvent>& events, const std::string& kind)
    {
        return size_t(std::count_if(events.begin(), events.end(), [&](const auto& e) { return e.Kind == kind; }));
    }
    [[maybe_unused]] Visual::Value CCall(Gameplay::Runtime& r, Entity h, const std::string& kind, std::map<std::string, Visual::Value> args = {})
    {
        std::map<std::string, Visual::Value> a;
        for (auto& p : Visual::Catalog().at(kind).Inputs)
            if (p.Kind != Visual::Type::Exec) a[p.Name] = p.Default;
        for (auto& [k, v] : args) a[k] = v;
        return r.Dispatch(h, h, kind, a);
    }
    [[maybe_unused]] bool Throws(const std::function<void()>& f)
    {
        try
        {
            f();
        }
        catch (const std::exception&)
        {
            return true;
        }
        return false;
    }
    [[maybe_unused]] void StaticWall2D(Scene& s, glm::vec3 position, glm::vec2 half)
    {
        auto wall = s.CreateEntity("Wall");
        wall.GetTransform().Translation = position;
        wall.AddComponent<Rigidbody2DComponent>().Type = Rigidbody2DComponent::BodyType::Static;
        wall.AddComponent<BoxCollider2DComponent>().Size = half;
    }
    [[maybe_unused]] void StaticBox3D(Scene& s, glm::vec3 position, glm::vec3 half)
    {
        auto wall = s.CreateEntity("Wall3D");
        wall.GetTransform().Translation = position;
        wall.AddComponent<Rigidbody3DComponent>().Type = Rigidbody3DComponent::BodyType::Static;
        wall.AddComponent<BoxCollider3DComponent>().HalfExtents = half;
    }
}
