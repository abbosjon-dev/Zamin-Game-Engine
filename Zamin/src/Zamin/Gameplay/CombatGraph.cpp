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
        if (!kind.starts_with("Gameplay.Projectile") && kind != "Gameplay.CombatEvent") return {};
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
        if (kind == "Gameplay.CombatEvent")
        {
            auto e = world.Poll(actor);
            return V{ { "Has", bool(e) }, { "Kind", e ? e->Kind : "" }, { "Key", e ? e->Key : "" }, { "Source", e ? e->Source : 0 }, { "Subject", e ? e->Subject : 0 }, { "Amount", e ? e->Amount : 0 }, { "Position", Vector(e ? glm::dvec3(e->Position) : glm::dvec3(0)) }, { "Dropped", world.Dropped(actor) } };
        }
        throw std::runtime_error("Unknown combat operation");
    }
}
