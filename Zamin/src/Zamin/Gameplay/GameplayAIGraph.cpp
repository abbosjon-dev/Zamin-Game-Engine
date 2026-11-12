#include "GameplayAI.h"
#include "GameplayRules.h"
#include "Zamin/Scene/Entity.h"
#include "Zamin/Scripting/Visual/VisualGraph.h"
#include <cmath>
#include <stdexcept>
namespace Zamin::Gameplay {
    namespace {
        using V = Visual::Value;
        struct Args
        {
            const std::map<std::string, V>& A;
            Entity Host;
            double N(const char* key) const
            {
                auto n = A.at(key).get<double>();
                if (!std::isfinite(n) || std::abs(n) > 1e9) throw std::runtime_error("AI number out of range");
                return n;
            }
            int I(const char* key, int low, int high) const
            {
                auto n = N(key);
                if (n < low || n > high || std::floor(n) != n) throw std::runtime_error("AI integer out of range");
                return int(n);
            }
            bool B(const char* k) const { return A.at(k).get<bool>(); }
            std::string S(const char* k) const { return A.at(k).get<std::string>(); }
            glm::vec3 Vec(const V& v) const
            {
                glm::vec3 p;
                for (int i = 0; i < 3; ++i)
                {
                    double n = v.at(i).get<double>();
                    if (!std::isfinite(n) || std::abs(n) > 1e9) throw std::runtime_error("AI vector out of range");
                    p[i] = float(n);
                }
                return p;
            }
            Entity Ref(Scene& scene, const char* key, Entity fallback = {}) const
            {
                auto id = A.at(key).get<uint64_t>();
                if (id == UINT64_MAX) return Host;
                if (!id) return fallback;
                auto e = scene.GetEntityByUUID(UUID(id));
                if (!e) throw std::runtime_error("AI entity missing");
                return e;
            }
            RuleQuery Query() const { return { N("Range"), N("MinDot"), Vec(A.at("Facing")), Vec(A.at("Origin")), B("TwoD"), B("LineOfSight"), uint16_t(I("Mask", 0, 65535)) }; }
        };
        V Vec(glm::vec3 p) { return V::array({ p.x, p.y, p.z }); }
        V Status(bool success, std::string error = {}) { return { { "Success", success }, { "Reason", std::move(error) } }; }
    }
    std::optional<V> DispatchAI(Entity host, Entity actor, const std::string& kind, const std::map<std::string, V>& values)
    {
        if (!kind.starts_with("Gameplay.AI") && kind != "Gameplay.Sense" && kind != "Gameplay.Strike") return {};
        auto& scene = *host.GetScene();
        Args a{ values, host };
        std::string error;
        if (kind == "Gameplay.Sense")
        {
            auto r = RuleWorld::For(scene).Sense(actor, a.Query(), a.S("Tag"), a.Ref(scene, "Victim"), a.N("Near"));
            auto e = r.Target ? scene.GetEntityByUUID(UUID(r.Target)) : Entity{};
            return V{ { "Visible", r.Success }, { "Victim", r.Target }, { "Distance", r.Distance }, { "Position", Vec(e ? scene.GetWorldPosition(e) : glm::vec3(0)) } };
        }
        if (kind == "Gameplay.Strike")
        {
            auto victim = a.Ref(scene, "Victim");
            if (!victim) return V{ { "Success", false }, { "Applied", 0 }, { "Killed", false }, { "Reason", "Victim missing" } };
            auto r = RuleWorld::For(scene).Strike(actor, victim, a.Query(), a.N("Amount"), a.N("Cooldown"), a.B("FriendlyFire"));
            return V{ { "Success", r.Damage.Applied > 0 }, { "Applied", r.Damage.Applied }, { "Killed", r.Damage.Killed }, { "Reason", r.Reason } };
        }
        auto& world = AIWorld::For(scene);
        if (kind == "Gameplay.AIConfigure")
        {
            AISpec s;
            s.Brain = { float(a.N("View")), float(a.N("Lose")), float(a.N("Near")), float(a.N("FieldOfView")), float(a.N("Memory")), float(a.N("Leash")), float(a.N("AttackEnter")), float(a.N("AttackExit")), float(a.N("Damage")), float(a.N("AttackCooldown")), float(a.N("Wait")), float(a.N("Arrive")), float(a.N("Repath")), float(a.N("Speed")), a.B("TwoD"), a.B("PingPong") };
            s.Recovery = { true, float(a.N("Stall")), float(a.N("RecoveryCooldown")), .05f, uint32_t(a.I("Retries", 1, 16)) };
            s.Tag = a.S("Tag");
            auto target = a.Ref(scene, "Victim");
            s.Target = target ? uint64_t(target.GetUUID()) : 0;
            s.Eye = a.Vec(a.A.at("Eye"));
            s.Facing = a.Vec(a.A.at("Facing"));
            s.Mask = uint16_t(a.I("Mask", 0, 65535));
            s.Relative = a.B("Relative");
            s.Enabled = a.B("Enabled");
            const auto& points = a.A.at("Points");
            if (points.size() > 64) throw std::runtime_error("AI point limit 64");
            for (const auto& p : points) s.Points.push_back(a.Vec(p));
            bool ok = world.Configure(host, actor, s, a.B("Reset"), error);
            return Status(ok, error);
        }
        if (kind == "Gameplay.AIEnable")
        {
            bool ok = world.Enable(actor, a.B("Enabled"));
            return Status(ok, ok ? "" : "AI not configured");
        }
        if (kind == "Gameplay.AIReset")
        {
            bool ok = world.Reset(actor, a.B("Rehome"), error);
            return Status(ok, error);
        }
        if (kind == "Gameplay.AIRemove")
        {
            bool ok = world.Remove(actor);
            return Status(ok, ok ? "" : "AI not configured");
        }
        if (kind == "Gameplay.AIEvent")
        {
            auto event = world.Poll(actor);
            return V{ { "Has", bool(event) }, { "Mode", event ? event->Mode : "" }, { "Victim", event ? event->Target : 0ull } };
        }
        if (kind != "Gameplay.AIState") throw std::runtime_error("Unknown AI operation");
        auto state = world.State(actor);
        AISnapshot empty;
        const auto& r = state ? *state : empty;
        return V{ { "Configured", bool(state) }, { "Mode", BrainName(r.Brain.Mode) }, { "Victim", r.Target }, { "Visible", r.Visible }, { "Enabled", r.Enabled }, { "Home", Vec(r.Home) }, { "Goal", Vec(r.Goal) }, { "LastSeen", Vec(r.LastSeen) }, { "Facing", Vec(r.Facing) }, { "Memory", r.Brain.Remember }, { "Waypoint", r.Brain.Waypoint }, { "Waiting", r.Brain.Waiting }, { "Arrivals", r.Brain.Arrivals }, { "Returns", r.Brain.Returns }, { "Sightings", r.Sightings }, { "Losses", r.Losses }, { "Attacks", r.Attacks }, { "Kills", r.Kills }, { "Transitions", r.Transitions }, { "PathRequests", r.PathRequests }, { "Retries", r.Retries }, { "Status", r.Status }, { "Dropped", r.Dropped } };
    }
}
