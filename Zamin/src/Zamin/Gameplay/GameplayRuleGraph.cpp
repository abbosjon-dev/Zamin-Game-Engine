#include "GameplayRules.h"
#include "Zamin/Scene/Entity.h"
#include "Zamin/Scripting/Visual/VisualGraph.h"
#include <algorithm>
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
                double n = Values.at(key).get<double>();
                if (!std::isfinite(n)) throw std::runtime_error("Rule number must be finite");
                return n;
            }
            int I(const char* key, int low, int high) const
            {
                auto n = N(key);
                if (n < low || n > high || std::floor(n) != n) throw std::runtime_error("Rule integer out of range");
                return int(n);
            }
            bool B(const char* key) const { return Values.at(key).get<bool>(); }
            std::string S(const char* key) const { return Values.at(key).get<std::string>(); }
            glm::vec3 Vec(const char* key) const
            {
                const auto& value = Values.at(key);
                glm::vec3 result{};
                for (int i = 0; i < 3; ++i)
                {
                    double v = value.at(i).get<double>();
                    if (!std::isfinite(v) || std::abs(v) > 1e9) throw std::runtime_error("Rule vector out of range");
                    result[i] = float(v);
                }
                return result;
            }
            Entity Ref(Scene& scene, const char* key, Entity fallback = {}) const
            {
                uint64_t id = Values.at(key).get<uint64_t>();
                if (!id) return fallback;
                auto e = scene.GetEntityByUUID(UUID(id));
                if (!e) throw std::runtime_error("Rule entity not found");
                return e;
            }
            RuleQuery Query() const { return { N("Range"), N("MinDot"), Vec("Facing"), Vec("Origin"), B("TwoD"), B("LineOfSight"), uint16_t(I("Mask", 0, 65535)) }; }
        };
        V Status(bool success, std::string error) { return { { "Success", success }, { "Reason", std::move(error) } }; }
        V Use(const UseResult& r) { return { { "Success", r.Success }, { "Object", r.Target }, { "Distance", r.Distance }, { "Prompt", r.Prompt }, { "Reason", r.Reason }, { "Healed", r.Healed } }; }
        V Damage(const DamageResult& r) { return { { "Success", r.Applied > 0 }, { "Applied", r.Applied }, { "HealthDamage", r.Health }, { "ShieldDamage", r.Shield }, { "Killed", r.Killed }, { "Reason", r.Reason } }; }
    }
    std::optional<Visual::Value> DispatchRules(Entity host, Entity target, const std::string& kind, const std::map<std::string, Visual::Value>& values)
    {
        static const std::vector<std::string> names = { "Gameplay.HealthConfigure", "Gameplay.Health", "Gameplay.Damage", "Gameplay.Heal", "Gameplay.InventoryConfigure", "Gameplay.InventoryAdd", "Gameplay.InventoryRemove", "Gameplay.InventoryCount", "Gameplay.InteractionConfigure", "Gameplay.InteractionFind", "Gameplay.Interact", "Gameplay.Attack", "Gameplay.RuleEvent" };
        if (std::find(names.begin(), names.end(), kind) == names.end()) return {};
        auto& scene = *host.GetScene();
        auto& world = RuleWorld::For(scene);
        Args a{ values };
        std::string error;
        if (kind == "Gameplay.HealthConfigure")
        {
            HealthSettings s{ a.N("Maximum"), a.N("Shield"), a.N("HitGrace"), a.N("RespawnDelay"), uint16_t(a.I("Team", 0, 65535)) };
            bool ok = world.Configure(target, s, a.B("Reset"), error);
            return Status(ok, error);
        }
        if (kind == "Gameplay.Health")
        {
            auto h = world.Health(target);
            return V{ { "Configured", bool(h) }, { "Current", h ? h->Current : 0 }, { "Maximum", h ? h->Settings.Maximum : 0 }, { "Shield", h ? h->Shield : 0 }, { "MaximumShield", h ? h->Settings.Shield : 0 }, { "Dead", h && h->Dead }, { "Invulnerable", h && world.Now() < h->ImmuneUntil }, { "Deaths", h ? h->Deaths : 0u }, { "RespawnIn", h && h->Dead ? std::max(0., h->DiedAt + h->Settings.RespawnDelay - world.Now()) : 0 } };
        }
        if (kind == "Gameplay.Damage") return Damage(world.Damage(target, a.Ref(scene, "Source"), a.N("Amount"), a.B("FriendlyFire"), a.B("PierceShield"), a.B("IgnoreGrace")));
        if (kind == "Gameplay.Heal")
        {
            double applied = world.Heal(target, a.N("Amount"), a.B("Shield"));
            return V{ { "Success", applied > 0 }, { "Applied", applied } };
        }
        if (kind == "Gameplay.InventoryConfigure")
        {
            bool ok = world.ConfigureBag(target, a.I("Slots", 1, 64), a.I("Stack", 1, 999999), a.B("Reset"), error);
            return Status(ok, error);
        }
        if (kind == "Gameplay.InventoryCount")
        {
            auto bag = world.Inventory(target);
            auto item = a.S("Item");
            if (!ValidItem(item)) throw std::runtime_error("Invalid inventory item");
            return V{ { "Count", bag.Count(item) }, { "UsedSlots", bag.Items.size() }, { "Slots", bag.Slots }, { "Stack", bag.Stack } };
        }
        if (kind == "Gameplay.InventoryAdd" || kind == "Gameplay.InventoryRemove")
        {
            bool ok = world.ChangeItem(target, a.S("Item"), a.I("Count", 1, 999999), kind == "Gameplay.InventoryRemove", error);
            return Status(ok, error);
        }
        if (kind == "Gameplay.InteractionConfigure")
        {
            InteractionSettings s;
            s.Prompt = a.S("Prompt");
            s.RequiredItem = a.S("RequiredItem");
            s.RewardItem = a.S("RewardItem");
            s.RequiredCount = a.I("RequiredCount", 1, 999999);
            s.RewardCount = a.I("RewardCount", 1, 999999);
            s.MaxUses = a.I("MaxUses", 0, 999999);
            s.Consume = a.B("Consume");
            s.Deactivate = a.B("Deactivate");
            s.Enabled = a.B("Enabled");
            s.Cooldown = a.N("Cooldown");
            s.Heal = a.N("Heal");
            s.Offset = a.Vec("Offset");
            bool ok = world.ConfigureInteraction(target, s, a.B("Reset"), error);
            return Status(ok, error);
        }
        if (kind == "Gameplay.InteractionFind")
        {
            auto r = world.Find(target, a.Query());
            auto result = Use(r);
            result["Found"] = r.Target != 0;
            return result;
        }
        if (kind == "Gameplay.Interact") return Use(world.Interact(a.Ref(scene, "Actor", host), target, a.Query()));
        if (kind == "Gameplay.Attack")
        {
            auto r = world.Attack(target, a.Query(), a.N("Amount"), a.N("Cooldown"), a.B("FriendlyFire"));
            auto result = Damage(r.Damage);
            result["Victim"] = r.Target;
            result["Reason"] = r.Reason;
            return result;
        }
        auto event = world.Poll(target);
        return V{ { "Has", bool(event) }, { "Kind", event ? event->Kind : "" }, { "Item", event ? event->Item : "" }, { "Source", event ? event->Source : 0ull }, { "Subject", event ? event->Subject : 0ull }, { "Amount", event ? event->Amount : 0 }, { "Dropped", world.Dropped(target) } };
    }
}
