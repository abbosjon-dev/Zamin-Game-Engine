#include "LoadoutWorld.h"
#include "Zamin/Scene/Entity.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace Zamin::Gameplay {
    LoadoutWorld& LoadoutWorld::For(Scene& s)
    {
        auto& ctx = s.GetRegistry().ctx();
        if (auto* w = ctx.find<LoadoutWorld>()) return *w;
        return ctx.emplace<LoadoutWorld>(&s);
    }
    void ResetLoadouts(Scene& s) { s.GetRegistry().ctx().erase<LoadoutWorld>(); }
    void BeginLoadoutFrame(Scene& s)
    {
        if (auto* w = s.GetRegistry().ctx().find<LoadoutWorld>()) w->BeginFrame();
    }
    void TickLoadouts(Scene& s, double delta)
    {
        if (auto* w = s.GetRegistry().ctx().find<LoadoutWorld>()) w->Tick(delta);
    }
    LoadoutWorld::Record* LoadoutWorld::Find(Entity e)
    {
        if (!e || e.GetScene() != m_Scene) return nullptr;
        auto i = m_Records.find(uint64_t(e.GetUUID()));
        return i != m_Records.end() && i->second.Handle == uint32_t(e) ? &i->second : nullptr;
    }
    const LoadoutWorld::Record* LoadoutWorld::Find(Entity e) const { return const_cast<LoadoutWorld*>(this)->Find(e); }
    LoadoutWorld::Record* LoadoutWorld::Usable(Entity e, std::string& error)
    {
        error.clear();
        auto* r = Find(e);
        if (!m_Scene->IsRunning() || !r || !e.IsActiveInHierarchy())
        {
            error = "Loadout requires active configured actor in Play";
            return nullptr;
        }
        auto h = RuleWorld::For(*m_Scene).Health(e);
        if (!h || h->Dead)
        {
            error = "Loadout requires living health";
            return nullptr;
        }
        return r;
    }
    bool LoadoutWorld::Configure(Entity e, const CombatStats& base, std::string& error)
    {
        error.clear();
        if (!m_Scene->IsRunning() || !e || e.GetScene() != m_Scene || !e.IsActiveInHierarchy() || !ValidateCombatStats(base, false, error))
        {
            if (error.empty()) error = "Invalid loadout actor";
            return false;
        }
        auto h = RuleWorld::For(*m_Scene).Health(e);
        if (!h || h->Dead)
        {
            error = "Configure health first";
            return false;
        }
        auto id = uint64_t(e.GetUUID());
        if (Find(e) || (!m_Records.contains(id) && m_Records.size() >= 64))
        {
            error = "Loadout already configured or actor limit 64";
            return false;
        }
        Record r;
        r.Handle = uint32_t(e);
        r.Base = base;
        r.State.Energy = base[3];
        m_Records.insert_or_assign(id, std::move(r));
        return true;
    }
    bool LoadoutWorld::DefineItem(Entity e, const ItemSpec& spec, std::string& error)
    {
        auto* r = Usable(e, error);
        if (!r || !ValidateItemSpec(spec, error)) return false;
        if (r->Items.size() >= 32 || r->Items.contains(spec.Key))
        {
            error = "Item duplicate or limit 32";
            return false;
        }
        r->Items.emplace(spec.Key, spec);
        return true;
    }
    bool LoadoutWorld::DefineAbility(Entity e, const AbilitySpec& spec, std::string& error)
    {
        auto* r = Usable(e, error);
        if (!r || !ValidateAbilitySpec(spec, error)) return false;
        if (r->Abilities.size() >= 16 || r->Abilities.contains(spec.Key))
        {
            error = "Ability duplicate or limit 16";
            return false;
        }
        r->Abilities.emplace(spec.Key, spec);
        r->State.Abilities.emplace(spec.Key, AbilityProgress{ 0, 0, spec.Unlocked });
        return true;
    }
    void LoadoutWorld::Emit(Record& r, LoadoutEvent e)
    {
        if (r.Events.size() == 32)
        {
            r.Events.pop_front();
            if (r.Dropped != UINT32_MAX) ++r.Dropped;
        }
        r.Events.push_back(std::move(e));
    }
    bool LoadoutWorld::Equip(Entity e, const std::string& key, std::string& error)
    {
        auto* r = Usable(e, error);
        if (!r) return false;
        auto& rules = RuleWorld::For(*m_Scene);
        auto* a = rules.FindActor(e);
        if (!EquipItem(r->Base, r->Items, a->Inventory, r->State, key, error)) return false;
        rules.Emit(*a, { "InventoryChanged", key, uint64_t(e.GetUUID()), uint64_t(e.GetUUID()), -1 });
        Emit(*r, { "Equipped", key, 1 });
        return true;
    }
    bool LoadoutWorld::Unequip(Entity e, EquipSlot slot, std::string& error)
    {
        auto* r = Usable(e, error);
        if (!r) return false;
        auto i = r->State.Equipped.find(slot);
        if (i == r->State.Equipped.end())
        {
            error = "Equipment slot empty";
            return false;
        }
        auto key = i->second;
        auto& rules = RuleWorld::For(*m_Scene);
        auto* a = rules.FindActor(e);
        if (!UnequipItem(r->Base, r->Items, a->Inventory, r->State, slot, error)) return false;
        rules.Emit(*a, { "InventoryChanged", key, uint64_t(e.GetUUID()), uint64_t(e.GetUUID()), 1 });
        Emit(*r, { "Unequipped", key, 1 });
        return true;
    }
    bool LoadoutWorld::Unlock(Entity e, const std::string& key, bool unlocked, std::string& error)
    {
        auto* r = Usable(e, error);
        if (!r) return false;
        auto p = r->State.Abilities.find(key);
        if (p == r->State.Abilities.end())
        {
            error = "Ability not configured";
            return false;
        }
        p->second.Unlocked = unlocked;
        Emit(*r, { unlocked ? "Unlocked" : "Locked", key, 0 });
        return true;
    }
    double LoadoutWorld::Refill(Entity e, double amount)
    {
        if (!std::isfinite(amount) || amount < 0 || amount > 1e6) throw std::runtime_error("Invalid energy refill");
        std::string error;
        auto* r = Usable(e, error);
        if (!r) return 0;
        auto applied = std::min(amount, EffectiveStats(r->Base, r->Items, r->State)[3] - r->State.Energy);
        r->State.Energy += applied;
        if (applied > 0) Emit(*r, { "Energy", "", applied });
        return applied;
    }
    std::optional<LoadoutSnapshot> LoadoutWorld::State(Entity e) const
    {
        auto* r = Find(e);
        if (!r) return {};
        return LoadoutSnapshot{ EffectiveStats(r->Base, r->Items, r->State), r->State };
    }
    std::optional<AbilitySpec> LoadoutWorld::Ability(Entity e, const std::string& key) const
    {
        auto* r = Find(e);
        if (!r) return {};
        auto i = r->Abilities.find(key);
        return i == r->Abilities.end() ? std::optional<AbilitySpec>{} : i->second;
    }
    AbilityResult LoadoutWorld::Cast(Entity actor, Entity victim, const std::string& key, const RuleQuery& query)
    {
        AbilityResult out;
        auto* r = Usable(actor, out.Reason);
        if (!r) return out;
        auto d = r->Abilities.find(key);
        if (d == r->Abilities.end())
        {
            out.Reason = "Ability not defined";
            return out;
        }
        auto& spec = d->second;
        auto& p = r->State.Abilities.at(key);
        if (!p.Unlocked || p.Remaining > 0 || r->State.Energy < spec.Cost)
        {
            out.Reason = !p.Unlocked ? "Ability locked" : p.Remaining > 0 ? "Ability cooldown"
                                                                          : "Insufficient energy";
            return out;
        }
        auto& rules = RuleWorld::For(*m_Scene);
        auto* a = rules.FindActor(actor);
        auto bag = a->Inventory;
        auto next = r->State;
        if (!spec.Ingredient.empty() && !bag.Exchange(spec.Ingredient, spec.Count, true, "", 0, out.Reason)) return out;
        auto stats = EffectiveStats(r->Base, r->Items, r->State);
        double amount = std::min(1e6, spec.Amount + stats[2]);
        if (spec.Kind == AbilityKind::Damage)
        {
            if (!victim || victim.GetScene() != m_Scene || victim == actor)
            {
                out.Reason = "Ability needs a live enemy target";
                return out;
            }
            auto q = query;
            q.Range = spec.Range;
            // Shared perception validates 2D/3D range, layers, LoS and team without spending an attack cooldown.
            if (!rules.Sense(actor, q, "", victim).Success)
            {
                out.Reason = "Ability target invalid or blocked/out of range";
                return out;
            }
            auto hit = rules.Damage(victim, actor, amount, false, false, false);
            out.Applied = hit.Applied;
            out.Reason = hit.Reason;
            out.Target = victim.GetUUID();
        }
        else
        {
            // Support abilities are self-cast; an explicit foreign target is rejected.
            if (victim && victim != actor)
            {
                out.Reason = "Support ability must target self";
                return out;
            }
            out.Applied = rules.Heal(actor, amount, spec.Kind == AbilityKind::Shield);
            out.Target = actor.GetUUID();
        }
        if (out.Applied <= 0)
        {
            if (out.Reason.empty()) out.Reason = "Ability had no effect";
            return out;
        }
        next.Energy -= spec.Cost;
        auto& after = next.Abilities.at(key);
        after.Remaining = spec.Cooldown;
        if (after.Uses != UINT32_MAX) ++after.Uses;
        a->Inventory = std::move(bag);
        r->State = std::move(next);
        if (!spec.Ingredient.empty()) rules.Emit(*a, { "InventoryChanged", spec.Ingredient, uint64_t(actor.GetUUID()), uint64_t(actor.GetUUID()), -double(spec.Count) });
        Emit(*r, { "Cast", key, out.Applied });
        out.Success = true;
        out.Reason.clear();
        return out;
    }
    std::optional<LoadoutEvent> LoadoutWorld::Poll(Entity e)
    {
        auto* r = Find(e);
        if (!r || r->Events.empty()) return {};
        auto x = std::move(r->Events.front());
        r->Events.pop_front();
        return x;
    }
    uint32_t LoadoutWorld::Dropped(Entity e) const
    {
        auto* r = Find(e);
        return r ? r->Dropped : 0;
    }
    void LoadoutWorld::BeginFrame()
    {
        std::erase_if(m_Records, [&](auto& x) {auto e=m_Scene->GetEntityByUUID(UUID(x.first));return !e||uint32_t(e)!=x.second.Handle; });
    }
    void LoadoutWorld::Tick(double delta)
    {
        if (!std::isfinite(delta) || delta < 0 || delta > 1e6) throw std::runtime_error("Invalid loadout tick");
        for (auto& [id, r] : m_Records)
        {
            auto e = m_Scene->GetEntityByUUID(UUID(id));
            if (!e || uint32_t(e) != r.Handle || !e.IsActiveInHierarchy()) continue;
            auto h = RuleWorld::For(*m_Scene).Health(e);
            if (h && !h->Dead) TickLoadoutState(r.Base, r.Items, r.State, delta);
        }
    }
    double LoadoutDamage(Scene& s, Entity source, Entity victim, double amount)
    {
        if (amount <= 0) return amount;
        auto* w = s.GetRegistry().ctx().find<LoadoutWorld>();
        if (!w) return amount;
        if (source && source != victim)
        {
            auto* stats = w->Find(source);
            if (stats) amount += EffectiveStats(stats->Base, stats->Items, stats->State)[0];
        }
        auto* armor = w->Find(victim);
        if (armor) amount *= 100. / (100. + EffectiveStats(armor->Base, armor->Items, armor->State)[1]);
        return std::clamp(amount, 0., 1e6);
    }
}
