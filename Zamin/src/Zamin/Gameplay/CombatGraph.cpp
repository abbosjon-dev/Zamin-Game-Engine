#include "CombatWorld.h"
#include "Zamin/Scene/Entity.h"
#include "Zamin/Scripting/Visual/VisualGraph.h"
#include <cmath>
#include <stdexcept>
namespace Zamin::Gameplay {
    namespace {
        using V = Visual::Value;
        struct Args
        {
            const std::map<std::string, V>& Values;
            double N(const char* key) const
            {
                const auto& v = Values.at(key);
                if (!v.is_number()) throw std::runtime_error("Combat number expected");
                double n = v.get<double>();
                if (!std::isfinite(n)) throw std::runtime_error("Combat number must be finite");
                return n;
            }
            int I(const char* key, int low, int high) const
            {
                double n = N(key);
                if (n < low || n > high || std::floor(n) != n) throw std::runtime_error("Combat integer outside bounds");
                return int(n);
            }
            bool B(const char* key) const { return Values.at(key).get<bool>(); }
            std::string S(const char* key) const { return Values.at(key).get<std::string>(); }
            glm::dvec3 Vec(const char* key) const
            {
                const auto& v = Values.at(key);
                if (!v.is_array() || v.size() != 3) throw std::runtime_error("Combat vector requires three values");
                glm::dvec3 out{};
                for (int i = 0; i < 3; ++i)
                {
                    if (!v[size_t(i)].is_number()) throw std::runtime_error("Combat vector number expected");
                    out[i] = v[size_t(i)].get<double>();
                    if (!std::isfinite(out[i]) || std::abs(out[i]) > 1e9) throw std::runtime_error("Combat vector out of range");
                }
                return out;
            }
            CombatStats Stats(const char* key) const
            {
                const auto& v = Values.at(key);
                if (!v.is_array() || v.size() != 5) throw std::runtime_error("Combat modifiers require five values");
                CombatStats out{};
                for (size_t i = 0; i < 5; ++i)
                {
                    if (!v[i].is_number()) throw std::runtime_error("Combat modifier number expected");
                    out[i] = v[i].get<double>();
                }
                return out;
            }
            // An entity pin: UINT64_MAX = the host, 0 = none; an unknown UUID is reported by the caller.
            Entity Ref(Scene& scene, Entity host, const char* key, bool& missing) const
            {
                const auto id = Values.at(key).get<uint64_t>();
                missing = false;
                if (id == UINT64_MAX) return host;
                if (!id) return {};
                auto e = scene.GetEntityByUUID(UUID(id));
                missing = !e;
                return e;
            }
            uint32_t Handle(const char* key) const
            {
                double n = N(key);
                if (n < 0 || n > double(UINT32_MAX) || std::floor(n) != n) throw std::runtime_error("Projectile handle must be an integer 0..4294967295");
                return uint32_t(n);
            }
        };
        V Vector(glm::dvec3 v) { return V::array({ v.x, v.y, v.z }); }
        V Status(bool success, std::string reason) { return { { "Success", success }, { "Reason", std::move(reason) } }; }
    }
    std::optional<Visual::Value> DispatchCombat(Entity host, Entity actor, const std::string& kind, const std::map<std::string, Visual::Value>& values)
    {
        if (!kind.starts_with("Gameplay.Projectile") && !kind.starts_with("Gameplay.Area") && !kind.starts_with("Gameplay.Effect") && kind != "Gameplay.CombatEvent" && kind != "Gameplay.CombatModifiers") return {};
        auto& scene = *host.GetScene();
        auto& world = CombatWorld::For(scene);
        Args a{ values };
        if (kind == "Gameplay.ProjectileDefine")
        {
            ProjectileSpec s;
            s.Key = a.S("Projectile");
            s.Kind = PayloadKind(a.I("Kind", 0, 2));
            s.Speed = a.N("Speed");
            s.Lifetime = a.N("Lifetime");
            s.MaxDistance = a.N("MaxDistance");
            s.Radius = a.N("Radius");
            s.TargetRadius = a.N("TargetRadius");
            s.TargetOffset = a.Vec("TargetOffset");
            s.Damage = a.N("Damage");
            s.Pierce = a.I("Pierce", 0, 7);
            s.Gravity = a.Vec("Gravity");
            s.Homing = a.N("Homing");
            s.TwoD = a.B("TwoD");
            s.FriendlyFire = a.B("FriendlyFire");
            s.PierceShield = a.B("PierceShield");
            s.IgnoreGrace = a.B("IgnoreGrace");
            s.DetonateOnExpire = a.B("DetonateOnExpire");
            s.BlockMask = uint16_t(a.I("BlockMask", 0, 65535));
            s.Effect = a.S("Effect");
            s.Area = a.S("Area");
            s.Prefab = a.S("Prefab");
            s.Pool = a.I("Pool", 0, 32);
            std::string error;
            const bool ok = world.DefineProjectile(s, error);
            return Status(ok, error);
        }
        if (kind == "Gameplay.ProjectileFire")
        {
            const auto id = values.at("Victim").get<uint64_t>();
            Entity victim = id == UINT64_MAX ? host : id ? scene.GetEntityByUUID(UUID(id))
                                                         : Entity{};
            if (id && !victim) return V{ { "Success", false }, { "Projectile", 0 }, { "Reason", "Projectile target unavailable" } };
            FireRequest request;
            request.Key = a.S("Projectile");
            request.Owner = actor;
            request.Origin = glm::vec3(a.Vec("Origin"));
            request.Direction = glm::vec3(a.Vec("Direction"));
            request.Target = victim;
            request.Damage = a.N("Damage");
            const auto result = world.Fire(request);
            return V{ { "Success", result.Success }, { "Projectile", result.Handle }, { "Reason", result.Reason } };
        }
        if (kind == "Gameplay.ProjectileState")
        {
            const auto state = world.Projectile(a.Handle("Projectile"));
            return V{ { "Alive", bool(state) }, { "Key", state ? state->Key : "" }, { "Position", Vector(state ? state->Position + state->Muzzle : glm::dvec3(0)) }, { "Velocity", Vector(state ? state->Velocity : glm::dvec3(0)) }, { "Age", state ? state->Age : 0 }, { "Hits", state ? state->HitCount : 0 }, { "Live", world.LiveCount() }, { "Capacity", ProjectilePool::Capacity } };
        }
        if (kind == "Gameplay.ProjectileCancel") return V{ { "Success", world.Cancel(a.Handle("Projectile"), a.B("Detonate")) } };
        if (kind == "Gameplay.AreaDefine")
        {
            AreaSpec s;
            s.Key = a.S("Area");
            s.Kind = PayloadKind(a.I("Kind", 0, 2));
            s.Amount = a.N("Amount");
            s.Radius = a.N("Radius");
            s.Shape = Falloff(a.I("Falloff", 0, 1));
            s.EdgeScale = a.N("EdgeScale");
            s.MaxTargets = a.I("MaxTargets", 1, 32);
            s.TargetOffset = a.Vec("TargetOffset");
            s.TwoD = a.B("TwoD");
            s.LineOfSight = a.B("LineOfSight");
            s.Mask = uint16_t(a.I("Mask", 0, 65535));
            s.AffectOwner = a.B("AffectOwner");
            s.FriendlyFire = a.B("FriendlyFire");
            s.PierceShield = a.B("PierceShield");
            s.IgnoreGrace = a.B("IgnoreGrace");
            s.Effect = a.S("Effect");
            std::string error;
            const bool ok = world.DefineArea(s, error);
            return Status(ok, error);
        }
        if (kind == "Gameplay.AreaApply")
        {
            auto fail = [](const char* reason) { return V{ { "Success", false }, { "Affected", 0 }, { "Applied", 0 }, { "Reason", reason } }; };
            bool missing = false;
            const Entity victim = a.Ref(scene, host, "Victim", missing);
            if (missing) return fail("Area victim unavailable");
            const auto key = a.S("Area");
            glm::dvec3 center = a.Vec("Center");
            // A victim centres the area on its aim point (world position + the area's TargetOffset).
            if (victim)
            {
                auto spec = world.AreaDefinition(key);
                if (!spec) return fail("Area not defined");
                center = glm::dvec3(scene.GetWorldPosition(victim)) + spec->TargetOffset;
            }
            const auto result = world.ApplyArea(key, actor, glm::vec3(center), a.N("Magnitude"));
            return V{ { "Success", result.Success }, { "Affected", result.Affected }, { "Applied", result.Applied }, { "Reason", result.Reason } };
        }
        if (kind == "Gameplay.EffectDefine")
        {
            EffectSpec s;
            s.Key = a.S("Effect");
            s.Duration = a.N("Duration");
            s.Period = a.N("Period");
            s.Tick = TickKind(a.I("Tick", 0, 3));
            s.TickAmount = a.N("TickAmount");
            s.Add = a.Stats("Modifiers");
            s.Speed = a.N("Speed");
            s.Stun = a.B("Stun");
            s.Hostile = a.B("Hostile");
            s.PierceShield = a.B("PierceShield");
            s.Rule = StackRule(a.I("Stacking", 0, 3));
            s.MaxStacks = a.I("MaxStacks", 1, 16);
            std::string error;
            const bool ok = world.DefineEffect(s, error);
            return Status(ok, error);
        }
        if (kind == "Gameplay.EffectApply")
        {
            bool missing = false;
            const Entity source = a.Ref(scene, host, "Source", missing);
            if (missing) return V{ { "Success", false }, { "Outcome", "Rejected" }, { "Stacks", 0 }, { "Remaining", 0 }, { "Reason", "Effect source unavailable" } };
            const double magnitude = a.N("Magnitude");
            const auto result = world.ApplyEffect(actor, a.S("Effect"), source, magnitude);
            const bool ok = result.Outcome != ApplyOutcome::Rejected;
            return V{ { "Success", ok }, { "Outcome", ApplyOutcomeName(result.Outcome) }, { "Stacks", result.Stacks }, { "Remaining", result.Remaining }, { "Reason", result.Reason } };
        }
        if (kind == "Gameplay.EffectRemove") return V{ { "Removed", world.RemoveEffects(actor, a.S("Effect")) } };
        if (kind == "Gameplay.EffectState")
        {
            const auto view = world.Effect(actor, a.S("Effect"));
            return V{ { "Active", view.Active }, { "Stacks", view.Stacks }, { "Instances", view.Instances }, { "Remaining", view.Remaining }, { "Fraction", view.Duration > 0 ? view.Remaining / view.Duration : 0.0 }, { "Magnitude", view.Magnitude } };
        }
        if (kind == "Gameplay.CombatModifiers")
        {
            const auto m = world.Modifiers(actor);
            return V{ { "Stunned", m.Stunned }, { "Speed", m.Speed }, { "Attack", m.Add[0] }, { "Armor", m.Add[1] }, { "Power", m.Add[2] }, { "Maximum", m.Add[3] }, { "Regen", m.Add[4] }, { "Effects", m.Instances } };
        }
        if (kind == "Gameplay.CombatEvent")
        {
            auto e = world.Poll(actor);
            return V{ { "Has", bool(e) }, { "Kind", e ? e->Kind : "" }, { "Key", e ? e->Key : "" }, { "Source", e ? e->Source : 0 }, { "Subject", e ? e->Subject : 0 }, { "Amount", e ? e->Amount : 0 }, { "Position", Vector(e ? glm::dvec3(e->Position) : glm::dvec3(0)) }, { "Dropped", world.Dropped(actor) } };
        }
        throw std::runtime_error("Unknown combat operation");
    }
}
