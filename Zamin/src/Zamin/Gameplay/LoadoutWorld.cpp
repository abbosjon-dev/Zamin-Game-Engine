#include "LoadoutWorld.h"
#include "CombatWorld.h"
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
    LoadoutWorld::Record* LoadoutWorld::Usable(Entity e, std::string& error, bool action)
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
        if (action && CombatStunned(*m_Scene, e))
        {
            error = "Stunned";
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
        auto* r = Usable(e, error, true);
        if (!r) return false;
        auto& rules = RuleWorld::For(*m_Scene);
        auto* a = rules.FindActor(e);
        if (!EquipItem(r->Base, r->Items, a->Inventory, r->State, key, error, CombatStatBonus(*m_Scene, e))) return false;
        rules.Emit(*a, { "InventoryChanged", key, uint64_t(e.GetUUID()), uint64_t(e.GetUUID()), -1 });
        Emit(*r, { "Equipped", key, 1 });
        return true;
    }
    bool LoadoutWorld::Unequip(Entity e, EquipSlot slot, std::string& error)
    {
        auto* r = Usable(e, error, true);
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
        if (!UnequipItem(r->Base, r->Items, a->Inventory, r->State, slot, error, CombatStatBonus(*m_Scene, e))) return false;
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
        auto applied = std::max(0.0, std::min(amount, EffectiveStats(r->Base, r->Items, r->State, CombatStatBonus(*m_Scene, e))[3] - r->State.Energy));
        r->State.Energy += applied;
        if (applied > 0) Emit(*r, { "Energy", "", applied });
        return applied;
    }
    std::optional<LoadoutSnapshot> LoadoutWorld::State(Entity e) const
    {
        auto* r = Find(e);
        if (!r) return {};
        return LoadoutSnapshot{ EffectiveStats(r->Base, r->Items, r->State, CombatStatBonus(*m_Scene, e)), r->State };
    }
    std::optional<AbilitySpec> LoadoutWorld::Ability(Entity e, const std::string& key) const
    {
        auto* r = Find(e);
        if (!r) return {};
        auto i = r->Abilities.find(key);
        return i == r->Abilities.end() ? std::optional<AbilitySpec>{} : i->second;
    }
    AbilityResult LoadoutWorld::Cast(Entity actor, Entity victim, const std::string& key, const RuleQuery& query, const CastAim& aim)
    {
        AbilityResult out;
        auto* r = Usable(actor, out.Reason, true);
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
        const auto finite = [](glm::vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); };
        if (!finite(aim.Direction) || !finite(aim.Origin) || glm::length(glm::dvec3(aim.Origin)) > 100 || glm::length(glm::dvec3(aim.Direction)) > 1e6)
            throw std::runtime_error("Ability aim must be finite (origin within 100)");
        auto& rules = RuleWorld::For(*m_Scene);
        auto* a = rules.FindActor(actor);
        auto bag = a->Inventory;
        auto next = r->State;
        if (!spec.Ingredient.empty() && !bag.Exchange(spec.Ingredient, spec.Count, true, "", 0, out.Reason)) return out;
        auto stats = EffectiveStats(r->Base, r->Items, r->State, CombatStatBonus(*m_Scene, actor));
        double amount = std::min(1e6, spec.Amount + stats[2]);
        auto fail = [&](std::string reason) {
            out.Reason = std::move(reason);
            out.Applied = 0;
            return out;
        };
        auto q = query;
        q.Range = spec.Range;
        // A hostile victim: shared perception validates 2D/3D range, layers, LoS and team without spending an
        // attack cooldown.
        auto hostile = [&](Entity target) {
            return target && target.GetScene() == m_Scene && target != actor && rules.Sense(actor, q, "", target).Success;
        };
        // A supported ally (support projectiles): a living, active teammate in range and sight, never the caster.
        auto ally = [&](Entity target) {
            if (!target || target.GetScene() != m_Scene || target == actor || !m_Scene->IsActiveInHierarchy(target)) return false;
            const auto own = rules.Health(actor), other = rules.Health(target);
            if (!own || !other || other->Dead || !Admits(PayloadKind::Heal, false, actor.GetUUID(), own->Settings.Team, target.GetUUID(), other->Settings.Team, false)) return false;
            rules.Budget();
            double distance = 0;
            return rules.Visible(actor, target, q, m_Scene->GetWorldPosition(target) + q.Origin, distance);
        };
        CombatWorld* combat = nullptr;
        std::optional<EffectSpec> effect;
        bool reached = false; // a fired projectile, an area that reached a target or an applied effect commits
        if (!spec.Projectile.empty() || !spec.Area.empty() || !spec.Effect.empty())
        {
            combat = &CombatWorld::For(*m_Scene);
            if (!spec.Effect.empty())
            {
                effect = combat->EffectDefinition(spec.Effect);
                if (!effect) return fail("Ability effect not defined");
                if (spec.Kind != AbilityKind::Effect && effect->Hostile != (spec.Kind == AbilityKind::Damage)) return fail("Ability effect kind mismatch");
            }
        }
        if (!spec.Projectile.empty())
        {
            const auto definition = combat->ProjectileDefinition(spec.Projectile);
            if (!definition) return fail("Projectile not defined");
            if (int(definition->Kind) != int(spec.Kind)) return fail("Ability/projectile kind mismatch");
            if (victim)
            {
                const bool valid = spec.Kind == AbilityKind::Damage ? hostile(victim) : ally(victim);
                if (!valid) return fail("Ability target invalid or blocked/out of range");
            }
            else if (glm::length(glm::dvec3(aim.Direction)) < 1e-9) return fail("Ability needs a target or direction");
            FireRequest request;
            request.Key = spec.Projectile;
            request.Owner = actor;
            request.Origin = aim.Origin;
            request.Direction = aim.Direction;
            request.Target = victim;
            request.Damage = amount; // Fire adds the caster's Attack to a damage projectile
            request.Effect = spec.Effect;
            const auto fired = combat->Fire(request);
            if (!fired.Success) return fail(fired.Reason);
            out.Projectile = fired.Handle;
            out.Applied = combat->Projectile(fired.Handle)->Damage;
            out.Target = victim ? uint64_t(victim.GetUUID()) : 0;
            reached = true;
        }
        else if (!spec.Area.empty())
        {
            const auto definition = combat->AreaDefinition(spec.Area);
            if (!definition) return fail("Area not defined");
            if (int(definition->Kind) != int(spec.Kind)) return fail("Ability/area kind mismatch");
            Entity centre = actor;
            if (spec.Kind == AbilityKind::Damage && victim)
            {
                if (!hostile(victim)) return fail("Ability target invalid or blocked/out of range");
                centre = victim;
            }
            else if (spec.Kind != AbilityKind::Damage && victim && victim != actor) return fail("Support ability must target self");
            const auto point = glm::dvec3(m_Scene->GetWorldPosition(centre)) + definition->TargetOffset;
            const auto area = combat->ApplyArea(spec.Area, actor, glm::vec3(point), amount, spec.Effect);
            if (!area.Success) return fail(area.Reason);
            if (area.Affected == 0) return fail("Ability had no effect");
            // An area that reached a target commits, as a projectile hit does (absorbed or full health included).
            out.Applied = area.Applied;
            out.Target = centre.GetUUID();
            reached = true;
        }
        else if (spec.Kind == AbilityKind::Effect)
        {
            // A hostile status needs a victim in range and sight; a beneficial one is self-cast.
            Entity target = actor;
            if (effect->Hostile)
            {
                if (!hostile(victim)) return fail("Ability target invalid or blocked/out of range");
                target = victim;
            }
            else if (victim && victim != actor) return fail("Support ability must target self");
            const auto applied = combat->ApplyEffect(target, spec.Effect, actor);
            if (applied.Outcome == ApplyOutcome::Rejected) return fail(applied.Reason);
            out.Applied = 1;
            out.Target = target.GetUUID();
            reached = true;
        }
        else if (spec.Kind == AbilityKind::Damage)
        {
            if (!victim || victim.GetScene() != m_Scene || victim == actor) return fail("Ability needs a live enemy target");
            if (!hostile(victim)) return fail("Ability target invalid or blocked/out of range");
            auto hit = rules.Damage(victim, actor, amount, false, false, false);
            out.Applied = hit.Applied;
            out.Reason = hit.Reason;
            out.Target = victim.GetUUID();
            // The linked status follows a hit that did damage and left the victim alive.
            if (effect && hit.Applied > 0 && !rules.Dead(victim)) combat->ApplyEffect(victim, spec.Effect, actor);
        }
        else
        {
            // Support abilities are self-cast; an explicit foreign target is rejected.
            if (victim && victim != actor) return fail("Support ability must target self");
            out.Applied = rules.Heal(actor, amount, spec.Kind == AbilityKind::Shield);
            out.Target = actor.GetUUID();
            if (effect && out.Applied > 0) combat->ApplyEffect(actor, spec.Effect, actor);
        }
        if (!reached && out.Applied <= 0)
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
        // `next` was copied before the cast: an effect it put on the caster (a beneficial status lowering MaxEnergy)
        // clamped the old state, so the committed energy is clamped again against the current capacity.
        ClampLoadoutEnergy(*m_Scene, actor);
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
            if (h && !h->Dead) TickLoadoutState(r.Base, r.Items, r.State, delta, CombatStatBonus(*m_Scene, e));
        }
    }
    double LoadoutDamage(Scene& s, Entity source, Entity victim, double amount, bool sourceAttack)
    {
        if (amount <= 0) return amount;
        if (!s.GetRegistry().ctx().find<LoadoutWorld>() && !s.GetRegistry().ctx().find<CombatWorld>()) return amount;
        if (sourceAttack && source && source != victim) amount += LoadoutAttack(s, source);
        // Armor 0 (no loadout, no effect) multiplies by exactly 1, as before status effects existed.
        amount *= 100. / (100. + LoadoutArmor(s, victim));
        return std::clamp(amount, 0., 1e6);
    }
    double LoadoutAttack(Scene& s, Entity source)
    {
        if (!source || source.GetScene() != &s) return 0;
        const auto bonus = CombatStatBonus(s, source);
        auto* w = s.GetRegistry().ctx().find<LoadoutWorld>();
        if (auto* r = w ? w->Find(source) : nullptr) return EffectiveStats(r->Base, r->Items, r->State, bonus)[0];
        return std::clamp(bonus[0], 0., 100000.);
    }
    double LoadoutArmor(Scene& s, Entity victim)
    {
        if (!victim || victim.GetScene() != &s) return 0;
        const auto bonus = CombatStatBonus(s, victim);
        auto* w = s.GetRegistry().ctx().find<LoadoutWorld>();
        if (auto* r = w ? w->Find(victim) : nullptr) return EffectiveStats(r->Base, r->Items, r->State, bonus)[1];
        return std::clamp(bonus[1], 0., 100000.);
    }
    void ClampLoadoutEnergy(Scene& s, Entity e)
    {
        auto* w = s.GetRegistry().ctx().find<LoadoutWorld>();
        auto* r = w ? w->Find(e) : nullptr;
        if (r) r->State.Energy = std::min(r->State.Energy, EffectiveStats(r->Base, r->Items, r->State, CombatStatBonus(s, e))[3]);
    }
}
