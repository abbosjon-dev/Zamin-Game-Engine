#include "LoadoutWorld.h"
#include "Zamin/Scene/Entity.h"
#include "Zamin/Scripting/Visual/VisualGraph.h"
#include <cmath>
namespace Zamin::Gameplay {
    std::optional<Visual::Value> DispatchLoadout(Entity host, Entity actor, const std::string& kind, const std::map<std::string, Visual::Value>& a)
    {
        using V = Visual::Value;
        if (!kind.starts_with("Gameplay.Loadout") && !kind.starts_with("Gameplay.ItemDefine") && !kind.starts_with("Gameplay.Equipment") && !kind.starts_with("Gameplay.Ability")) return {};
        auto& w = LoadoutWorld::For(*host.GetScene());
        std::string error;
        bool ok = false;
        auto text = [&](const char* k) { return a.at(k).get<std::string>(); };
        auto number = [&](const char* k) {auto& v=a.at(k);if(!v.is_number())throw std::runtime_error("Loadout number expected");double n=v.get<double>();if(!std::isfinite(n))throw std::runtime_error("Loadout requires finite number");return n; };
        auto integer = [&](const char* k, int max) {double n=number(k);if(n<0||n>max||std::floor(n)!=n)throw std::runtime_error("Loadout integer outside bounds");return int(n); };
        auto stats = [&](const char* k) {auto& v=a.at(k);if(!v.is_array()||v.size()!=5)throw std::runtime_error("Stats require five values");CombatStats out{};for(size_t i=0;i<5;++i){if(!v[i].is_number())throw std::runtime_error("Stats number expected");out[i]=v[i].get<double>();}return out; };
        if (kind == "Gameplay.LoadoutConfigure") ok = w.Configure(actor, stats("Stats"), error);
        else if (kind == "Gameplay.ItemDefine") ok = w.DefineItem(actor, { text("Item"), EquipSlot(integer("Slot", 3)), stats("Modifiers") }, error);
        else if (kind == "Gameplay.EquipmentEquip") ok = w.Equip(actor, text("Item"), error);
        else if (kind == "Gameplay.EquipmentUnequip") ok = w.Unequip(actor, EquipSlot(integer("Slot", 3)), error);
        else if (kind == "Gameplay.EquipmentState")
        {
            auto s = w.State(actor);
            auto slot = EquipSlot(integer("Slot", 3));
            auto key = s && s->State.Equipped.contains(slot) ? s->State.Equipped.at(slot) : "";
            return V{ { "Configured", bool(s) }, { "Equipped", !key.empty() }, { "Item", key } };
        }
        else if (kind == "Gameplay.LoadoutStats")
        {
            auto s = w.State(actor);
            auto b = s ? s->Stats : CombatStats{};
            return V{ { "Configured", bool(s) }, { "Attack", b[0] }, { "Armor", b[1] }, { "Power", b[2] }, { "Maximum", b[3] }, { "Regen", b[4] }, { "Energy", s ? s->State.Energy : 0 }, { "Fraction", s ? s->State.Energy / b[3] : 0 } };
        }
        else if (kind == "Gameplay.AbilityDefine")
        {
            AbilitySpec s;
            s.Key = text("Ability");
            s.Kind = AbilityKind(integer("Kind", 2));
            s.Amount = number("Amount");
            s.Cost = number("Cost");
            s.Cooldown = number("Cooldown");
            s.Range = number("Range");
            s.Ingredient = text("Ingredient");
            s.Count = integer("Count", 999999);
            s.Unlocked = a.at("Unlocked").get<bool>();
            ok = w.DefineAbility(actor, s, error);
        }
        else if (kind == "Gameplay.AbilityUnlock") ok = w.Unlock(actor, text("Ability"), a.at("Unlocked").get<bool>(), error);
        else if (kind == "Gameplay.AbilityCast")
        {
            auto id = a.at("Victim").get<uint64_t>();
            auto victim = id == UINT64_MAX ? host : id ? host.GetScene()->GetEntityByUUID(UUID(id))
                                                       : Entity{};
            if (id && !victim) return V{ { "Success", false }, { "Applied", 0 }, { "Victim", 0 }, { "Reason", "Ability target unavailable" } };
            RuleQuery q;
            q.TwoD = a.at("TwoD").get<bool>();
            q.LineOfSight = a.at("LineOfSight").get<bool>();
            q.Mask = uint16_t(integer("Mask", 65535));
            q.Origin = { 0, 0, 0 };
            auto result = w.Cast(actor, victim, text("Ability"), q);
            return V{ { "Success", result.Success }, { "Applied", result.Applied }, { "Victim", result.Target }, { "Reason", result.Reason } };
        }
        else if (kind == "Gameplay.AbilityState")
        {
            auto s = w.State(actor);
            auto d = w.Ability(actor, text("Ability"));
            auto p = s && d ? s->State.Abilities.at(text("Ability")) : AbilityProgress{};
            auto health = actor && actor.GetScene() == host.GetScene() ? RuleWorld::For(*host.GetScene()).Health(actor) : std::optional<HealthState>{};
            bool resources = d && (d->Ingredient.empty() || RuleWorld::For(*host.GetScene()).Inventory(actor).Count(d->Ingredient) >= d->Count);
            return V{ { "Configured", bool(d) }, { "Unlocked", bool(d) && p.Unlocked }, { "Ready", bool(d) && health && !health->Dead && actor.IsActiveInHierarchy() && p.Unlocked && resources && p.Remaining <= 0 && s->State.Energy >= d->Cost }, { "Remaining", p.Remaining }, { "Fraction", d && d->Cooldown > 0 ? 1 - p.Remaining / d->Cooldown : 1 }, { "Uses", p.Uses }, { "Cost", d ? d->Cost : 0 } };
        }
        else if (kind == "Gameplay.LoadoutRefill") return V{ { "Applied", w.Refill(actor, number("Amount")) } };
        else if (kind == "Gameplay.LoadoutEvent")
        {
            auto e = w.Poll(actor);
            return V{ { "Has", bool(e) }, { "Kind", e ? e->Kind : "" }, { "Key", e ? e->Key : "" }, { "Amount", e ? e->Amount : 0 }, { "Dropped", w.Dropped(actor) } };
        }
        else throw std::runtime_error("Unknown loadout operation");
        if (!ok && error.empty()) error = "Loadout operation unavailable";
        return V{ { "Success", ok }, { "Reason", error } };
    }
}
