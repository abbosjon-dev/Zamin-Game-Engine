#include "GameplayRules.h"
#include "Zamin/Scene/Entity.h"
#include "Zamin/Physics/PhysicsWorld2D.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace Zamin::Gameplay {
    namespace {
        bool Finite(glm::vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
        void Validate(const RuleQuery& q)
        {
            if (!std::isfinite(q.Range) || q.Range <= 0 || q.Range > 100 || !std::isfinite(q.MinDot) || q.MinDot < -1 || q.MinDot > 1 || !Finite(q.Facing) || !Finite(q.Origin) || glm::length(glm::dvec3(q.Origin)) > 1000)
                throw std::runtime_error("Invalid rule query range/facing/origin");
            auto facing = glm::dvec3(q.Facing);
            if (q.TwoD) facing.z = 0;
            if (q.MinDot > -1 && glm::length(facing) < 1e-8) throw std::runtime_error("Facing query requires a nonzero direction");
        }
    }
    RuleWorld& RuleWorld::For(Scene& scene)
    {
        auto& ctx = scene.GetRegistry().ctx();
        if (auto* rules = ctx.find<RuleWorld>()) return *rules;
        return ctx.emplace<RuleWorld>(&scene);
    }
    void ResetRules(Scene& scene) { scene.GetRegistry().ctx().erase<RuleWorld>(); }
    void BeginRulesFrame(Scene& scene)
    {
        if (auto* rules = scene.GetRegistry().ctx().find<RuleWorld>()) rules->BeginFrame();
    }
    void TickRules(Scene& scene, float delta)
    {
        if (auto* rules = scene.GetRegistry().ctx().find<RuleWorld>()) rules->Tick(delta);
    }
    void RuleWorld::Check(Entity entity) const
    {
        if (!entity || entity.GetScene() != m_Scene) throw std::runtime_error("Rule target must be a live entity in this scene");
    }
    RuleWorld::Actor* RuleWorld::FindActor(Entity e)
    {
        Check(e);
        auto it = m_Actors.find(uint64_t(e.GetUUID()));
        return it != m_Actors.end() && it->second.Handle == uint32_t(e) ? &it->second : nullptr;
    }
    const RuleWorld::Actor* RuleWorld::FindActor(Entity e) const { return const_cast<RuleWorld*>(this)->FindActor(e); }
    RuleWorld::Actor& RuleWorld::RequireActor(Entity e)
    {
        Check(e);
        const uint64_t id = e.GetUUID();
        if (!m_Actors.contains(id) && m_Actors.size() >= 512) throw std::runtime_error("Rule actor limit 512");
        auto found = m_Actors.find(id);
        if (found != m_Actors.end() && found->second.Handle != uint32_t(e)) m_Actors.erase(found);
        auto& actor = m_Actors.try_emplace(id).first->second;
        actor.Handle = uint32_t(e);
        return actor;
    }
    void RuleWorld::Tick(float delta)
    {
        if (!std::isfinite(delta) || delta < 0) throw std::runtime_error("Invalid rule clock delta");
        m_Now = std::min(1e15, m_Now + double(delta));
    }
    void RuleWorld::BeginFrame()
    {
        m_Queries = 0;
        auto retired = [&](const auto& item) { auto e=m_Scene->GetEntityByUUID(UUID(item.first));return !e||uint32_t(e)!=item.second.Handle; };
        std::erase_if(m_Actors, retired);
        std::erase_if(m_Interactions, retired);
    }
    void RuleWorld::Budget()
    {
        if (++m_Queries > 128) throw std::runtime_error("Rule query budget 128 per scene frame");
    }
    void RuleWorld::Emit(Actor& actor, RuleEvent event)
    {
        if (actor.Events.size() == 32)
        {
            actor.Events.pop_front();
            if (actor.Dropped != UINT32_MAX) ++actor.Dropped;
        }
        actor.Events.push_back(std::move(event));
    }
    bool RuleWorld::Configure(Entity e, const HealthSettings& settings, bool reset, std::string& error)
    {
        HealthState next;
        if (!ConfigureHealth(next, settings, error)) return false;
        auto& a = RequireActor(e);
        if (a.Health && !reset)
        {
            error = "Health already configured";
            return false;
        }
        a.Health = next;
        return true;
    }
    std::optional<HealthState> RuleWorld::Health(Entity e) const
    {
        auto* a = FindActor(e);
        return a ? a->Health : std::nullopt;
    }
    bool RuleWorld::Dead(Entity e) const
    {
        auto health = Health(e);
        return health && health->Dead;
    }
    DamageResult RuleWorld::Damage(Entity victim, Entity source, double amount, bool friendlyFire, bool pierce, bool ignoreGrace)
    {
        if (!std::isfinite(amount) || amount < 0 || amount > 1e6) throw std::runtime_error("Damage must be finite 0..1000000");
        Check(victim);
        if (source) Check(source);
        DamageResult result;
        auto* actor = FindActor(victim);
        if (!actor || !actor->Health)
        {
            result.Reason = "Health not configured";
            return result;
        }
        if (!m_Scene->IsActiveInHierarchy(victim))
        {
            result.Reason = "Inactive";
            return result;
        }
        if (source && source != victim && !friendlyFire)
        {
            auto other = Health(source);
            if (other && other->Settings.Team && other->Settings.Team == actor->Health->Settings.Team)
            {
                result.Reason = "Same team";
                return result;
            }
        }
        std::string error;
        if (!DamageHealth(*actor->Health, amount, m_Now, pierce, ignoreGrace, result, error)) throw std::runtime_error(error);
        if (result.Applied > 0)
        {
            const uint64_t sender = source ? uint64_t(source.GetUUID()) : 0;
            Emit(*actor, { "Damage", {}, sender, uint64_t(victim.GetUUID()), result.Applied });
            if (result.Killed)
            {
                if (auto* nav = victim.TryGetComponent<NavAgentComponent>()) nav->Stop(true);
                if (auto* c = victim.TryGetComponent<CharacterController3DComponent>()) c->MoveVelocity = {};
                if (auto* body = victim.TryGetComponent<Rigidbody2DComponent>(); body && body->RuntimeBody && m_Scene->GetPhysicsWorld2D())
                {
                    auto velocity = PhysicsWorld2D::GetLinearVelocity((b2Body*)body->RuntimeBody);
                    velocity.x = 0;
                    if (body->GravityScale == 0) velocity.y = 0;
                    PhysicsWorld2D::SetLinearVelocity((b2Body*)body->RuntimeBody, velocity);
                }
                Emit(*actor, { "Died", {}, sender, uint64_t(victim.GetUUID()), result.Health });
            }
        }
        return result;
    }
    double RuleWorld::Heal(Entity e, double amount, bool shield)
    {
        if (!std::isfinite(amount) || amount < 0 || amount > 1e6) throw std::runtime_error("Heal must be finite 0..1000000");
        auto* a = FindActor(e);
        if (!a || !a->Health || !m_Scene->IsActiveInHierarchy(e)) return 0;
        double applied = HealHealth(*a->Health, amount, shield);
        if (applied > 0) Emit(*a, { shield ? "ShieldHealed" : "Healed", {}, 0, uint64_t(e.GetUUID()), applied });
        return applied;
    }
    bool RuleWorld::CanRevive(Entity e, double grace, std::string& error) const
    {
        auto h = Health(e);
        if (!h)
        {
            error = "Health not configured";
            return false;
        }
        if (!m_Scene->IsActiveInHierarchy(e))
        {
            error = "Inactive";
            return false;
        }
        return ReviveHealth(*h, m_Now, grace, error);
    }
    bool RuleWorld::Revive(Entity e, double grace, std::string& error)
    {
        if (!CanRevive(e, grace, error)) return false;
        auto& a = *FindActor(e);
        if (!ReviveHealth(*a.Health, m_Now, grace, error)) return false;
        a.AttackUntil = 0;
        Emit(a, { "Respawned", {}, 0, uint64_t(e.GetUUID()), a.Health->Current });
        return true;
    }
    bool RuleWorld::ConfigureBag(Entity e, int slots, int stack, bool reset, std::string& error)
    {
        Bag next = Inventory(e);
        if (!next.Configure(slots, stack, reset, error)) return false;
        RequireActor(e).Inventory = std::move(next);
        return true;
    }
    bool RuleWorld::ChangeItem(Entity e, const std::string& key, int count, bool remove, std::string& error)
    {
        if (!ValidItem(key) || count < 1 || count > 999999)
        {
            error = "Invalid item/count";
            return false;
        }
        auto next = Inventory(e);
        if (!next.Exchange(remove ? key : "", count, remove, remove ? "" : key, count, error)) return false;
        auto& a = RequireActor(e);
        a.Inventory = std::move(next);
        Emit(a, { "InventoryChanged", key, 0, uint64_t(e.GetUUID()), remove ? -double(count) : double(count) });
        return true;
    }
    Bag RuleWorld::Inventory(Entity e) const
    {
        auto* a = FindActor(e);
        return a ? a->Inventory : Bag{};
    }
    bool RuleWorld::ConfigureInteraction(Entity e, const InteractionSettings& settings, bool reset, std::string& error)
    {
        Check(e);
        error.clear();
        if (settings.Prompt.size() > 96 || settings.Prompt.find('\0') != std::string::npos ||
            (!settings.RequiredItem.empty() && !ValidItem(settings.RequiredItem)) || (!settings.RewardItem.empty() && !ValidItem(settings.RewardItem)) ||
            settings.RequiredCount < 1 || settings.RequiredCount > 999999 || settings.RewardCount < 1 || settings.RewardCount > 999999 ||
            settings.MaxUses < 0 || settings.MaxUses > 999999 || !std::isfinite(settings.Cooldown) || settings.Cooldown < 0 || settings.Cooldown > 60 ||
            !std::isfinite(settings.Heal) || settings.Heal < 0 || settings.Heal > 1e6 || !Finite(settings.Offset) || glm::length(glm::dvec3(settings.Offset)) > 1000)
        {
            error = "Invalid interaction settings";
            return false;
        }
        const uint64_t id = e.GetUUID();
        auto found = m_Interactions.find(id);
        if (found != m_Interactions.end() && found->second.Handle == uint32_t(e) && !reset)
        {
            error = "Interaction already configured";
            return false;
        }
        if (found == m_Interactions.end() && m_Interactions.size() >= 512)
        {
            error = "Interaction limit 512";
            return false;
        }
        m_Interactions[id] = { uint32_t(e), settings, 0, 0 };
        return true;
    }
    bool RuleWorld::Visible(Entity actor, Entity object, const RuleQuery& q, glm::vec3 point, double& distance) const
    {
        auto origin = m_Scene->GetWorldPosition(actor) + q.Origin;
        auto direction = glm::dvec3(point) - glm::dvec3(origin);
        auto facing = glm::dvec3(q.Facing);
        if (q.TwoD)
        {
            direction.z = 0;
            facing.z = 0;
        }
        distance = glm::length(direction);
        if (!std::isfinite(distance) || distance > q.Range) return false;
        if (q.MinDot > -1 && distance > 1e-8 && glm::dot(direction / distance, facing / glm::length(facing)) < q.MinDot) return false;
        if (!q.LineOfSight || distance < .001) return true;
        auto hits = q.TwoD ? m_Scene->Raycast2DAll(glm::vec2(origin), glm::vec2(direction / distance), float(distance), q.Mask, false) : m_Scene->Raycast3DAll(origin, glm::vec3(direction / distance), float(distance), q.Mask, false);
        return std::none_of(hits.begin(), hits.end(), [&](const auto& hit) {
            return hit.EntityID != actor.GetUUID() && hit.EntityID != object.GetUUID() && hit.Distance < distance - .01;
        });
    }
    UseResult RuleWorld::CanUse(Entity actor, Entity object, const RuleQuery& q, const Interaction& objectState) const
    {
        UseResult r;
        r.Target = object.GetUUID();
        r.Prompt = objectState.Settings.Prompt;
        const auto& s = objectState.Settings;
        if (!m_Scene->IsActiveInHierarchy(actor) || !m_Scene->IsActiveInHierarchy(object)) r.Reason = "Inactive";
        else if (Dead(actor)) r.Reason = "Dead";
        else if (!s.Enabled) r.Reason = "Disabled";
        else if (s.MaxUses && objectState.Uses >= uint32_t(s.MaxUses)) r.Reason = "Exhausted";
        else if (m_Now < objectState.Until) r.Reason = "Interaction cooldown";
        else if (!Visible(actor, object, q, m_Scene->GetWorldPosition(object) + s.Offset, r.Distance)) r.Reason = "Out of range/facing or blocked";
        else
        {
            auto next = Inventory(actor);
            if (!next.Exchange(s.RequiredItem, s.RequiredCount, s.Consume, s.RewardItem, s.RewardCount, r.Reason)) return r;
            if (s.Heal > 0)
            {
                auto health = Health(actor);
                if (!health) r.Reason = "Health not configured";
                else if (health->Current >= health->Settings.Maximum && s.RewardItem.empty()) r.Reason = "Health full";
            }
        }
        r.Success = r.Reason.empty();
        return r;
    }
    UseResult RuleWorld::Find(Entity actor, const RuleQuery& q)
    {
        Check(actor);
        Validate(q);
        Budget();
        if (Dead(actor) || !m_Scene->IsActiveInHierarchy(actor)) return {};
        std::vector<std::pair<double, uint64_t>> candidates;
        const auto origin = m_Scene->GetWorldPosition(actor) + q.Origin;
        for (const auto& [id, record] : m_Interactions)
        {
            auto object = m_Scene->GetEntityByUUID(UUID(id));
            if (!object || object == actor || uint32_t(object) != record.Handle || !record.Settings.Enabled || !m_Scene->IsActiveInHierarchy(object) || (record.Settings.MaxUses && record.Uses >= uint32_t(record.Settings.MaxUses))) continue;
            auto delta = glm::dvec3(m_Scene->GetWorldPosition(object) + record.Settings.Offset) - glm::dvec3(origin);
            if (q.TwoD) delta.z = 0;
            if (auto d = glm::length(delta); d <= q.Range) candidates.emplace_back(d, id);
        }
        std::sort(candidates.begin(), candidates.end());
        size_t probes = 0;
        for (const auto& [distance, id] : candidates)
        {
            if (++probes > 64) break;
            auto object = m_Scene->GetEntityByUUID(UUID(id));
            double ignored;
            const auto& record = m_Interactions.at(id);
            if (!Visible(actor, object, q, m_Scene->GetWorldPosition(object) + record.Settings.Offset, ignored)) continue;
            auto r = CanUse(actor, object, q, record);
            r.Distance = distance;
            return r;
        }
        return {};
    }
    UseResult RuleWorld::Interact(Entity actor, Entity object, const RuleQuery& q)
    {
        Check(actor);
        Check(object);
        Validate(q);
        Budget();
        if (actor == object) return { false, 0, 0, 0, "Cannot interact with self", {} };
        auto found = m_Interactions.find(uint64_t(object.GetUUID()));
        if (found == m_Interactions.end() || found->second.Handle != uint32_t(object)) return { false, 0, 0, 0, "Interaction not configured", {} };
        auto result = CanUse(actor, object, q, found->second);
        if (!result.Success) return result;
        auto settings = found->second.Settings;
        auto& a = RequireActor(actor);
        auto next = a.Inventory;
        if (!next.Exchange(settings.RequiredItem, settings.RequiredCount, settings.Consume, settings.RewardItem, settings.RewardCount, result.Reason))
        {
            result.Success = false;
            return result;
        }
        auto health = a.Health;
        if (settings.Heal > 0) result.Healed = HealHealth(*health, settings.Heal, false);
        a.Inventory = std::move(next);
        a.Health = health;
        if (found->second.Uses != UINT32_MAX) ++found->second.Uses;
        found->second.Until = m_Now + settings.Cooldown;
        Emit(a, { "Interacted", {}, uint64_t(actor.GetUUID()), uint64_t(object.GetUUID()), 1 });
        if (settings.Consume && !settings.RequiredItem.empty()) Emit(a, { "InventoryChanged", settings.RequiredItem, uint64_t(actor.GetUUID()), uint64_t(object.GetUUID()), -double(settings.RequiredCount) });
        if (!settings.RewardItem.empty())
        {
            Emit(a, { "InventoryChanged", settings.RewardItem, uint64_t(actor.GetUUID()), uint64_t(object.GetUUID()), double(settings.RewardCount) });
            Emit(a, { "Collected", settings.RewardItem, uint64_t(actor.GetUUID()), uint64_t(object.GetUUID()), double(settings.RewardCount) });
        }
        if (result.Healed > 0) Emit(a, { "Healed", {}, uint64_t(object.GetUUID()), uint64_t(actor.GetUUID()), result.Healed });
        // Commit before lifecycle callbacks. No references into state are used after SetActive.
        if (settings.Deactivate) m_Scene->SetActive(object, false);
        return result;
    }
    AttackResult RuleWorld::Attack(Entity actor, const RuleQuery& q, double amount, double cooldown, bool friendlyFire)
    {
        Check(actor);
        Validate(q);
        if (!std::isfinite(amount) || amount <= 0 || amount > 1e6 || !std::isfinite(cooldown) || cooldown < 0 || cooldown > 60) throw std::runtime_error("Invalid attack damage/cooldown");
        Budget();
        AttackResult result;
        if (Dead(actor) || !m_Scene->IsActiveInHierarchy(actor))
        {
            result.Reason = "Dead/inactive";
            return result;
        }
        auto& source = RequireActor(actor);
        if (m_Now < source.AttackUntil)
        {
            result.Reason = "Attack cooldown";
            return result;
        }
        source.AttackUntil = m_Now + cooldown;
        std::vector<std::pair<double, uint64_t>> candidates;
        const auto origin = m_Scene->GetWorldPosition(actor) + q.Origin;
        for (const auto& [id, target] : m_Actors)
        {
            auto e = m_Scene->GetEntityByUUID(UUID(id));
            if (!e || e == actor || uint32_t(e) != target.Handle || !target.Health || target.Health->Dead || !m_Scene->IsActiveInHierarchy(e)) continue;
            if (!friendlyFire && source.Health && source.Health->Settings.Team && source.Health->Settings.Team == target.Health->Settings.Team) continue;
            auto delta = glm::dvec3(m_Scene->GetWorldPosition(e) + q.Origin) - glm::dvec3(origin);
            if (q.TwoD) delta.z = 0;
            if (auto d = glm::length(delta); d <= q.Range) candidates.emplace_back(d, id);
        }
        std::sort(candidates.begin(), candidates.end());
        size_t probes = 0;
        for (const auto& [distance, id] : candidates)
        {
            (void)distance;
            if (++probes > 64) break;
            auto e = m_Scene->GetEntityByUUID(UUID(id));
            double ignored;
            if (!Visible(actor, e, q, m_Scene->GetWorldPosition(e) + q.Origin, ignored)) continue;
            result.Target = id;
            result.Damage = Damage(e, actor, amount, friendlyFire, false, false);
            result.Reason = result.Damage.Reason;
            return result;
        }
        result.Reason = "No visible target";
        return result;
    }
    UseResult RuleWorld::Sense(Entity actor, const RuleQuery& q, const std::string& tag, Entity explicitTarget, double nearDistance)
    {
        Check(actor);
        Validate(q);
        if (explicitTarget) Check(explicitTarget);
        if (tag.size() > 64 || !std::isfinite(nearDistance) || nearDistance < 0 || nearDistance > q.Range) throw std::runtime_error("Invalid perception filter");
        Budget();
        if (Dead(actor) || !m_Scene->IsActiveInHierarchy(actor)) return {};
        const auto own = Health(actor);
        std::vector<std::pair<double, uint64_t>> candidates;
        const auto origin = m_Scene->GetWorldPosition(actor);
        for (const auto& [id, record] : m_Actors)
        {
            auto e = m_Scene->GetEntityByUUID(UUID(id));
            if (!e || e == actor || uint32_t(e) != record.Handle || !record.Health || record.Health->Dead || !m_Scene->IsActiveInHierarchy(e)) continue;
            if (explicitTarget && e != explicitTarget) continue;
            if (!tag.empty() && !e.HasTag(tag)) continue;
            if (own && own->Settings.Team && own->Settings.Team == record.Health->Settings.Team) continue;
            auto delta = glm::dvec3(m_Scene->GetWorldPosition(e)) - glm::dvec3(origin);
            if (q.TwoD) delta.z = 0;
            if (auto d = glm::length(delta); d <= q.Range) candidates.emplace_back(d, id);
        }
        std::sort(candidates.begin(), candidates.end());
        size_t probes = 0;
        for (const auto& [distance, id] : candidates)
        {
            if (++probes > 64) break;
            auto e = m_Scene->GetEntityByUUID(UUID(id));
            auto test = q;
            if (distance <= nearDistance) test.MinDot = -1;
            double actual;
            if (Visible(actor, e, test, m_Scene->GetWorldPosition(e) + q.Origin, actual)) return { true, id, distance, 0, {}, {} };
        }
        return {};
    }
    AttackResult RuleWorld::Strike(Entity actor, Entity victim, const RuleQuery& q, double amount, double cooldown, bool friendlyFire)
    {
        Check(actor);
        Check(victim);
        Validate(q);
        if (!std::isfinite(amount) || amount <= 0 || amount > 1e6 || !std::isfinite(cooldown) || cooldown < 0 || cooldown > 60) throw std::runtime_error("Invalid strike damage/cooldown");
        Budget();
        AttackResult result;
        if (Dead(actor) || !m_Scene->IsActiveInHierarchy(actor))
        {
            result.Reason = "Dead/inactive";
            return result;
        }
        auto& a = RequireActor(actor);
        if (m_Now < a.AttackUntil)
        {
            result.Reason = "Attack cooldown";
            return result;
        }
        a.AttackUntil = m_Now + cooldown;
        auto h = Health(victim);
        double distance;
        if (actor == victim || !h || h->Dead || !m_Scene->IsActiveInHierarchy(victim) || !Visible(actor, victim, q, m_Scene->GetWorldPosition(victim) + q.Origin, distance))
        {
            result.Reason = "Target invalid or blocked/out of range";
            return result;
        }
        result.Target = victim.GetUUID();
        result.Damage = Damage(victim, actor, amount, friendlyFire, false, false);
        result.Reason = result.Damage.Reason;
        return result;
    }
    std::optional<RuleEvent> RuleWorld::Poll(Entity e)
    {
        auto* a = FindActor(e);
        if (!a || a->Events.empty()) return {};
        auto event = std::move(a->Events.front());
        a->Events.pop_front();
        return event;
    }
    uint32_t RuleWorld::Dropped(Entity e) const
    {
        auto* a = FindActor(e);
        return a ? a->Dropped : 0;
    }
}
