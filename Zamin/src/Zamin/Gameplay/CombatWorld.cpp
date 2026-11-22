#include "CombatWorld.h"
#include "LoadoutWorld.h"
#include "Zamin/Core/Log.h"
#include "Zamin/Core/Profiler.h"
#include "Zamin/Debug/DebugDraw.h"
#include "Zamin/Project/Project.h"
#include "Zamin/Utils/FileSystem.h"
#include <nlohmann/json.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace Zamin::Gameplay {
    namespace {
        const char* const VisualTag = "Projectile visual";
        bool Finite(glm::vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
        glm::dvec3 D(glm::vec3 v) { return glm::dvec3(v); }
        template<typename... T>
        bool HasAny(Entity e) { return (e.HasComponent<T>() || ...); }
        glm::dvec3 Lerp(const Segment& s, double t) { return s.From + (s.To - s.From) * t; }
        constexpr double ImpactBackoff = .02; // as WorldSweep's query start: 2 cm off a struck surface
        // Effects carried by a payload must agree with it: damage carries hostile effects, support beneficial ones.
        bool Matches(PayloadKind kind, const EffectSpec& effect) { return effect.Hostile == (kind == PayloadKind::Damage); }
        template<typename T>
        bool DefineShared(std::map<std::string, T>& registry, const T& spec, size_t limit, std::string& error)
        {
            if (auto found = registry.find(spec.Key); found != registry.end())
            {
                if (found->second == spec) return true;
                error = "Definition differs";
                return false;
            }
            if (registry.size() >= limit)
            {
                error = "Definition limit 32";
                return false;
            }
            registry.emplace(spec.Key, spec);
            return true;
        }
    }
    CombatWorld& CombatWorld::For(Scene& scene)
    {
        auto& ctx = scene.GetRegistry().ctx();
        if (auto* world = ctx.find<CombatWorld>()) return *world;
        return ctx.emplace<CombatWorld>(&scene);
    }
    void ResetCombat(Scene& scene, bool destroyVisuals)
    {
        auto& ctx = scene.GetRegistry().ctx();
        if (auto* world = ctx.find<CombatWorld>(); world && destroyVisuals) world->DestroyVisuals();
        ctx.erase<CombatWorld>();
    }
    void BeginCombatFrame(Scene& scene)
    {
        if (auto* world = scene.GetRegistry().ctx().find<CombatWorld>()) world->BeginFrame();
    }
    void TickCombat(Scene& scene, double delta)
    {
        if (auto* world = scene.GetRegistry().ctx().find<CombatWorld>()) world->Tick(delta);
    }
    CombatWorld::CombatWorld(Scene* scene) : m_Scene(scene)
    {
        m_Probes.reserve(MaxActors);
        m_ProbeEntities.reserve(MaxActors);
        m_Ticks.reserve(EffectBook::Capacity * size_t(MaxTicksPerStep));
        m_Expired.reserve(EffectBook::Capacity);
        m_AreaTargets.reserve(MaxActors);
    }
    CombatStats CombatStatBonus(Scene& scene, Entity e)
    {
        auto* world = scene.GetRegistry().ctx().find<CombatWorld>();
        return world && e && e.GetScene() == &scene ? world->Modifiers(e).Add : CombatStats{};
    }
    double CombatSpeedScale(Scene& scene, Entity e)
    {
        auto* world = scene.GetRegistry().ctx().find<CombatWorld>();
        return world && e && e.GetScene() == &scene ? world->Modifiers(e).Speed : 1.0;
    }
    bool CombatStunned(Scene& scene, Entity e)
    {
        auto* world = scene.GetRegistry().ctx().find<CombatWorld>();
        return world && e && e.GetScene() == &scene && world->Modifiers(e).Stunned;
    }
    void OnCombatDeath(Scene& scene, Entity e)
    {
        if (auto* world = scene.GetRegistry().ctx().find<CombatWorld>(); world && e && e.GetScene() == &scene) world->ClearEffects(e);
    }
    void CombatWorld::Check(Entity e) const
    {
        if (!e || e.GetScene() != m_Scene) throw std::runtime_error("Combat target must be a live entity in this scene");
    }
    Entity CombatWorld::Resolve(uint64_t id, uint32_t& handle) const
    {
        if (!id || handle == ~0u) return {};
        auto e = m_Scene->GetEntityByUUID(UUID(id));
        if (!e || uint32_t(e) != handle)
        {
            handle = ~0u; // destroyed or recreated: the reference is stale for good
            return {};
        }
        return e;
    }
    bool CombatWorld::ValidateVisual(Entity root, std::string& error) const
    {
        std::vector<Entity> pending{ root };
        size_t visited = 0;
        while (!pending.empty())
        {
            auto e = pending.back();
            pending.pop_back();
            if (++visited > 256)
            {
                error = "Projectile visual prefab exceeds 256 entities";
                return false;
            }
            if (HasAny<Rigidbody2DComponent, BoxCollider2DComponent, CircleCollider2DComponent, PolygonCollider2DComponent, EdgeCollider2DComponent,
                       Rigidbody3DComponent, BoxCollider3DComponent, SphereCollider3DComponent, CapsuleCollider3DComponent, ConvexHullCollider3DComponent,
                       MeshCollider3DComponent, CharacterController3DComponent, NativeScriptComponent, NavAgentComponent, NavObstacleComponent>(e))
            {
                error = "Projectile visual prefab must not have bodies, colliders, characters, scripts or navigation";
                return false;
            }
            for (UUID child : e.GetComponent<RelationshipComponent>().Children)
                if (auto c = m_Scene->GetEntityByUUID(child)) pending.push_back(c);
        }
        return true;
    }
    Entity CombatWorld::Instantiate(Definition& def, std::string& error)
    {
        ++m_Instantiations;
        auto e = m_Scene->InstantiatePrefab(def.Prefab);
        if (!e)
        {
            error = "Projectile visual prefab failed to load";
            def.VisualsDisabled = true;
            return {};
        }
        if (!ValidateVisual(e, error))
        {
            ZM_CORE_WARN("Snaryad vizual prefabi rad etildi: {} ({})", def.Spec.Prefab, error);
            m_Scene->DestroyEntity(e);
            def.VisualsDisabled = true;
            return {};
        }
        e.AddTag(VisualTag);
        m_Scene->SetActive(e, false);
        def.Visuals.push_back({ uint64_t(e.GetUUID()), uint32_t(e), -1, false });
        return e;
    }
    bool CombatWorld::DefineProjectile(const ProjectileSpec& spec, std::string& error)
    {
        error.clear();
        if (!m_Scene->IsRunning())
        {
            error = "Combat definitions require Play";
            return false;
        }
        if (!ValidateProjectileSpec(spec, error)) return false;
        // Effect/area keys are resolved at Fire, so definitions may come in any order (Fire fails while missing).
        if (auto found = m_Definitions.find(spec.Key); found != m_Definitions.end())
        {
            if (found->second.Spec == spec) return true;
            error = "Definition differs";
            return false;
        }
        if (m_Definitions.size() >= MaxDefinitions)
        {
            error = "Definition limit 32";
            return false;
        }
        Definition def;
        def.Spec = spec;
        if (!spec.Prefab.empty())
        {
            const auto root = Project::GetAssetDirectory();
            const auto path = FileSystem::SafeJoin(root, spec.Prefab);
            std::error_code ec;
            if (path.empty() || path.extension() != ".zprefab" || !FileSystem::IsSubPathOnDisk(path, root) || !std::filesystem::is_regular_file(path, ec))
            {
                error = "Projectile prefab must be a .zprefab inside Assets";
                return false;
            }
            def.Prefab = path;
        }
        auto& stored = m_Definitions.emplace(spec.Key, std::move(def)).first->second;
        stored.Visuals.reserve(size_t(spec.Pool));
        // The first pooled instance validates the prefab (one instantiation, counted in this frame's budget).
        if (!stored.Prefab.empty() && spec.Pool > 0 && !Instantiate(stored, error))
        {
            m_Definitions.erase(spec.Key);
            return false;
        }
        return true;
    }
    std::optional<ProjectileSpec> CombatWorld::ProjectileDefinition(const std::string& key) const
    {
        auto found = m_Definitions.find(key);
        return found == m_Definitions.end() ? std::optional<ProjectileSpec>{} : found->second.Spec;
    }
    bool CombatWorld::DefineArea(const AreaSpec& spec, std::string& error)
    {
        error.clear();
        if (!m_Scene->IsRunning())
        {
            error = "Combat definitions require Play";
            return false;
        }
        return ValidateAreaSpec(spec, error) && DefineShared(m_Areas, spec, MaxDefinitions, error);
    }
    std::optional<AreaSpec> CombatWorld::AreaDefinition(const std::string& key) const
    {
        auto found = m_Areas.find(key);
        return found == m_Areas.end() ? std::optional<AreaSpec>{} : found->second;
    }
    bool CombatWorld::DefineEffect(const EffectSpec& spec, std::string& error)
    {
        error.clear();
        if (!m_Scene->IsRunning())
        {
            error = "Combat definitions require Play";
            return false;
        }
        return ValidateEffectSpec(spec, error) && DefineShared(m_EffectSpecs, spec, MaxDefinitions, error);
    }
    std::optional<EffectSpec> CombatWorld::EffectDefinition(const std::string& key) const
    {
        auto found = m_EffectSpecs.find(key);
        return found == m_EffectSpecs.end() ? std::optional<EffectSpec>{} : found->second;
    }
    std::string CombatWorld::AreaError(const AreaSpec& area) const
    {
        if (area.Effect.empty()) return {};
        auto effect = m_EffectSpecs.find(area.Effect);
        if (effect == m_EffectSpecs.end()) return "Area effect not defined";
        if (!Matches(area.Kind, effect->second)) return "Area effect kind mismatch";
        return {};
    }
    std::string CombatWorld::EffectError(PayloadKind kind, const std::string& key, const char* missing, const char* mismatch) const
    {
        if (key.empty()) return {};
        auto effect = m_EffectSpecs.find(key);
        if (effect == m_EffectSpecs.end()) return missing;
        if (!Matches(kind, effect->second)) return mismatch;
        return {};
    }
    std::string CombatWorld::PayloadError(const ProjectileSpec& spec) const
    {
        if (auto error = EffectError(spec.Kind, spec.Effect, "Projectile effect not defined", "Projectile effect kind mismatch"); !error.empty()) return error;
        if (!spec.Area.empty())
        {
            auto area = m_Areas.find(spec.Area);
            if (area == m_Areas.end()) return "Projectile area not defined";
            return AreaError(area->second);
        }
        return {};
    }
    FireResult CombatWorld::Fire(const FireRequest& request)
    {
        Check(request.Owner);
        if (request.Target) Check(request.Target);
        if (!Finite(request.Origin) || glm::length(D(request.Origin)) > 100 || !Finite(request.Direction) || glm::length(D(request.Direction)) > 1e6)
            throw std::runtime_error("Projectile origin must be within 100 and direction finite");
        if (!std::isfinite(request.Damage) || request.Damage > 1e6) throw std::runtime_error("Projectile damage must be finite, at most 1000000");
        FireResult out;
        auto fail = [&](const char* reason) {
            out.Reason = reason;
            return out;
        };
        if (!m_Scene->IsRunning()) return fail("Combat requires Play");
        auto found = m_Definitions.find(request.Key);
        if (found == m_Definitions.end()) return fail("Projectile not defined");
        auto& def = found->second;
        const auto& spec = def.Spec;
        auto& rules = RuleWorld::For(*m_Scene);
        const auto health = rules.Health(request.Owner);
        if (!m_Scene->IsActiveInHierarchy(request.Owner)) return fail("Owner inactive");
        if (!health) return fail("Owner health not configured");
        if (health->Dead) return fail("Owner dead");
        if (Modifiers(request.Owner).Stunned) return fail("Stunned");
        // Payload keys resolve now: definitions are immutable, so they still exist when the projectile hits.
        if (auto payload = PayloadError(spec); !payload.empty())
        {
            out.Reason = payload;
            return out;
        }
        if (!request.Effect.empty() && !ValidItem(request.Effect)) throw std::runtime_error("Projectile effect override must be empty or a 1..64 character key");
        if (auto payload = EffectError(spec.Kind, request.Effect, "Projectile effect not defined", "Projectile effect kind mismatch"); !payload.empty())
        {
            out.Reason = payload;
            return out;
        }
        const glm::dvec3 aim = D(m_Scene->GetWorldPosition(request.Owner)) + spec.TargetOffset;
        auto muzzle = D(request.Origin);
        if (spec.TwoD) muzzle.z = 0;
        auto direction = D(request.Direction);
        if (spec.TwoD) direction.z = 0;
        if (glm::length(direction) < 1e-9 && request.Target)
        {
            direction = D(m_Scene->GetWorldPosition(request.Target)) + spec.TargetOffset - (aim + muzzle);
            if (spec.TwoD) direction.z = 0;
        }
        if (!(glm::length(direction) >= 1e-9)) return fail("Projectile needs a direction or target");
        direction = glm::normalize(direction);
        if (m_Fires >= FireBudget) return fail("Projectile spawn budget 32");
        if (m_Pool.Count() >= ProjectilePool::Capacity) return fail("Projectile limit 128");
        const uint64_t owner = request.Owner.GetUUID();
        size_t owned = 0;
        for (uint32_t slot = 0; slot < ProjectilePool::Capacity; ++slot)
            if (m_Pool.Active(slot) && m_Slots[slot].State.Owner == owner && m_Slots[slot].OwnerHandle == uint32_t(request.Owner)) ++owned;
        if (owned >= MaxPerOwner) return fail("Owner projectile limit 32");
        // Outgoing damage is snapshotted now (base + owner Attack); armor and shields apply at impact.
        double damage = request.Damage < 0 ? spec.Damage : request.Damage;
        const double attack = std::min(1e6, LoadoutAttack(*m_Scene, request.Owner));
        if (spec.Kind == PayloadKind::Damage) damage = std::min(1e6, damage + attack);
        const uint32_t slot = *m_Pool.Acquire();
        ++m_Fires;
        auto& s = m_Slots[slot];
        auto& state = s.State;
        state.Key = spec.Key;
        state.Effect = request.Effect;
        state.Owner = owner;
        state.Target = request.Target ? uint64_t(request.Target.GetUUID()) : 0;
        state.Team = health->Settings.Team;
        state.Position = aim;
        state.Velocity = direction * spec.Speed;
        state.Muzzle = muzzle;
        state.Age = state.Travelled = 0;
        state.Damage = damage;
        state.Attack = attack;
        state.Hits.fill(0);
        state.HitCount = 0;
        state.PierceLeft = uint8_t(spec.Pierce);
        state.Phase = ProjectilePhase::Flying;
        s.Def = &def;
        s.OwnerHandle = uint32_t(request.Owner);
        s.TargetHandle = request.Target ? uint32_t(request.Target) : ~0u;
        s.Visual = -1;
        out.Success = true;
        out.Handle = m_Pool.Handle(slot);
        Emit(request.Owner, "Fired", spec.Key, owner, state.Target, damage, aim + muzzle);
        return out;
    }
    bool CombatWorld::Cancel(uint32_t handle, bool detonate)
    {
        if (!m_Pool.Live(handle)) return false;
        const uint32_t slot = ProjectilePool::Slot(handle);
        auto& s = m_Slots[slot];
        auto& state = s.State;
        if (auto owner = Resolve(state.Owner, s.OwnerHandle))
            Emit(owner, "Cancelled", state.Key, state.Owner, 0, 0, state.Position + state.Muzzle);
        if (detonate && !s.Def->Spec.Area.empty())
        {
            // Detonates where it is now (a launch that never stepped: at the launch point) on the next step.
            state.Position += state.Muzzle;
            state.Muzzle = {};
            state.Phase = ProjectilePhase::Detonating;
            return true;
        }
        Release(slot);
        return true;
    }
    std::optional<ProjectileState> CombatWorld::Projectile(uint32_t handle) const
    {
        if (!m_Pool.Live(handle)) return std::nullopt;
        return m_Slots[ProjectilePool::Slot(handle)].State;
    }
    void CombatWorld::Release(uint32_t slot)
    {
        auto& s = m_Slots[slot];
        if (s.Def && s.Visual >= 0 && size_t(s.Visual) < s.Def->Visuals.size()) s.Def->Visuals[size_t(s.Visual)].Slot = -1;
        s.Visual = -1;
        s.OwnerHandle = s.TargetHandle = ~0u;
        m_Pool.Release(slot);
    }
    void CombatWorld::Emit(Entity actor, const char* kind, const std::string& key, uint64_t source, uint64_t subject, double amount, const glm::dvec3& position)
    {
        const uint64_t id = actor.GetUUID();
        auto found = m_Events.find(id);
        if (found == m_Events.end())
        {
            if (m_Events.size() >= MaxActors) return;
            found = m_Events.try_emplace(id).first;
            found->second.Handle = uint32_t(actor);
        }
        auto& q = found->second;
        if (q.Handle != uint32_t(actor))
        {
            q.Handle = uint32_t(actor);
            q.Head = q.Count = q.Dropped = 0;
        }
        if (q.Count == EventCapacity)
        {
            q.Head = (q.Head + 1) % EventCapacity;
            --q.Count;
            if (q.Dropped != UINT32_MAX) ++q.Dropped;
        }
        // Ring entries are reused in place: their strings keep their capacity, so steady state allocates nothing.
        auto& e = q.Ring[(q.Head + q.Count) % EventCapacity];
        e.Kind = kind;
        e.Key = key;
        e.Source = source;
        e.Subject = subject;
        e.Amount = amount;
        e.Position = glm::vec3(position);
        ++q.Count;
    }
    void CombatWorld::Record(uint64_t source, uint64_t victim, const std::string& key, double amount, PayloadKind kind, bool killed, const glm::dvec3& position, bool periodic)
    {
        auto& r = m_HitRing[m_HitHead];
        r.Sequence = ++m_HitSequence;
        r.Source = source;
        r.Victim = victim;
        r.Key = key;
        r.Amount = amount;
        r.Time = m_Now;
        r.Kind = kind;
        r.Killed = killed;
        r.Periodic = periodic;
        r.Position = glm::vec3(position);
        m_HitHead = (m_HitHead + 1) % HitCapacity;
        m_HitCount = std::min<uint32_t>(HitCapacity, m_HitCount + 1);
    }
    std::optional<CombatEvent> CombatWorld::Poll(Entity e)
    {
        Check(e);
        auto found = m_Events.find(uint64_t(e.GetUUID()));
        if (found == m_Events.end() || found->second.Handle != uint32_t(e) || found->second.Count == 0) return std::nullopt;
        auto& q = found->second;
        CombatEvent event = q.Ring[q.Head];
        q.Head = (q.Head + 1) % EventCapacity;
        --q.Count;
        return event;
    }
    uint32_t CombatWorld::Dropped(Entity e) const
    {
        Check(e);
        auto found = m_Events.find(uint64_t(e.GetUUID()));
        return found == m_Events.end() || found->second.Handle != uint32_t(e) ? 0 : found->second.Dropped;
    }
    size_t CombatWorld::Recent(Entity actor, bool outgoing, bool incoming, double maxAge, std::span<HitRecord> out, bool periodic) const
    {
        Check(actor);
        if (!std::isfinite(maxAge) || maxAge < 0) throw std::runtime_error("Hit record age must be finite and nonnegative");
        const uint64_t id = actor.GetUUID();
        size_t count = 0;
        for (uint32_t i = 0; i < m_HitCount && count < out.size(); ++i)
        {
            const auto& r = m_HitRing[(m_HitHead + HitCapacity - 1 - i) % HitCapacity];
            if (m_Now - r.Time > maxAge) break; // the ring is in time order
            if (r.Periodic && !periodic) continue;
            if ((outgoing && r.Source == id) || (incoming && r.Victim == id)) out[count++] = r;
        }
        return count;
    }
    CombatStatsView CombatWorld::Stats() const
    {
        CombatStatsView v;
        v.Live = uint32_t(m_Pool.Count());
        for (const auto& [key, def] : m_Definitions)
            for (const auto& visual : def.Visuals)
            {
                ++v.Pooled;
                if (visual.Shown) ++v.Visuals;
            }
        v.Steps = m_Steps;
        v.Sweeps = m_Sweeps;
        v.ActorTests = m_ActorTests;
        v.Ticks = m_TickCount;
        v.Rays = m_Rays;
        v.Hits = m_Hits;
        v.Detonations = m_Detonations;
        v.Effects = m_EffectCount;
        v.DroppedTime = m_DroppedTime;
        return v;
    }
    std::string CombatWorld::Signature() const
    {
        auto projectiles = nlohmann::json::object();
        for (const auto& [key, def] : m_Definitions)
        {
            const auto& s = def.Spec;
            projectiles[key] = { s.Effect, s.Area, int(s.Kind), s.Speed, s.Lifetime, s.MaxDistance, s.Radius, s.TargetRadius, s.Damage, s.Homing, { s.Gravity.x, s.Gravity.y, s.Gravity.z }, { s.TargetOffset.x, s.TargetOffset.y, s.TargetOffset.z }, s.Pierce, s.BlockMask, s.TwoD, s.FriendlyFire, s.PierceShield, s.IgnoreGrace, s.DetonateOnExpire };
        }
        auto areas = nlohmann::json::object(), effects = nlohmann::json::object();
        for (const auto& [key, a] : m_Areas)
            areas[key] = { a.Effect, int(a.Kind), a.Radius, a.Amount, a.EdgeScale, int(a.Shape), a.MaxTargets, { a.TargetOffset.x, a.TargetOffset.y, a.TargetOffset.z }, a.Mask, a.TwoD, a.LineOfSight, a.AffectOwner, a.FriendlyFire, a.PierceShield, a.IgnoreGrace };
        for (const auto& [key, e] : m_EffectSpecs)
            effects[key] = { e.Duration, e.Period, e.TickAmount, int(e.Tick), e.Add, e.Speed, e.Stun, e.Hostile, e.PierceShield, int(e.Rule), e.MaxStacks };
        return nlohmann::json{ { "Projectiles", projectiles }, { "Areas", areas }, { "Effects", effects } }.dump();
    }
    void CombatWorld::Clear()
    {
        for (uint32_t slot = 0; slot < ProjectilePool::Capacity; ++slot)
            if (m_Pool.Active(slot)) Release(slot);
        for (auto& [key, def] : m_Definitions)
            for (auto& visual : def.Visuals)
            {
                visual.Slot = -1;
                auto e = m_Scene->GetEntityByUUID(UUID(visual.ID));
                if (visual.Shown && e && uint32_t(e) == visual.Handle) m_Scene->SetActive(e, false);
                visual.Shown = false;
            }
        // Effects go too; a lost MaxEnergy bonus clamps each actor's energy to its capacity.
        auto books = std::move(m_Books);
        m_Books.clear();
        m_EffectCount = 0;
        for (const auto& [id, book] : books)
            if (auto e = m_Scene->GetEntityByUUID(UUID(id)); e && uint32_t(e) == book.Handle && !book.Effects.Instances.empty()) ClampLoadoutEnergy(*m_Scene, e);
        m_Events.clear();
        m_HitHead = m_HitCount = 0;
        m_Accumulator = 0;
    }
    void CombatWorld::DestroyVisuals()
    {
        for (uint32_t slot = 0; slot < ProjectilePool::Capacity; ++slot) m_Slots[slot].Visual = -1;
        for (auto& [key, def] : m_Definitions)
        {
            for (const auto& visual : def.Visuals)
                if (auto e = m_Scene->GetEntityByUUID(UUID(visual.ID)); e && uint32_t(e) == visual.Handle) m_Scene->DestroyEntity(e);
            def.Visuals.clear();
        }
    }
    void CombatWorld::BeginFrame()
    {
        m_Fires = m_Instantiations = m_EffectCalls = m_AreaCalls = 0;
        m_Steps = m_Sweeps = m_ActorTests = m_TickCount = m_Rays = 0;
        auto retired = [&](const auto& item) {
            auto e = m_Scene->GetEntityByUUID(UUID(item.first));
            return !e || uint32_t(e) != item.second.Handle;
        };
        std::erase_if(m_Events, retired);
        for (auto it = m_Books.begin(); it != m_Books.end();)
        {
            if (!retired(*it))
            {
                ++it;
                continue;
            }
            m_EffectCount -= it->second.Effects.Instances.size();
            it = m_Books.erase(it);
        }
    }
    void CombatWorld::BuildProbes()
    {
        m_Probes.clear();
        m_ProbeEntities.clear();
        auto* rules = m_Scene->GetRegistry().ctx().find<RuleWorld>();
        if (!rules) return;
        // RuleWorld's map is ordered by UUID, so probes (and the lookups below) are in UUID order.
        for (const auto& [id, actor] : rules->m_Actors)
        {
            if (!actor.Health || actor.Health->Dead) continue;
            auto e = m_Scene->GetEntityByUUID(UUID(id));
            if (!e || uint32_t(e) != actor.Handle || !m_Scene->IsActiveInHierarchy(e)) continue;
            if (m_Probes.size() >= MaxActors) break;
            m_Probes.push_back({ id, D(m_Scene->GetWorldPosition(e)), actor.Health->Settings.Team });
            m_ProbeEntities.push_back(e);
        }
    }
    bool CombatWorld::IgnoredBlocker(UUID id) const
    {
        auto e = m_Scene->GetEntityByUUID(id);
        if (!e) return true;
        auto* rules = m_Scene->GetRegistry().ctx().find<RuleWorld>();
        // Health actors (and their collider children) belong to the actor channel; pooled visuals never block.
        for (int depth = 0; e && depth <= 8; ++depth)
        {
            const uint64_t uuid = e.GetUUID();
            if (rules)
                if (auto a = rules->m_Actors.find(uuid); a != rules->m_Actors.end() && a->second.Handle == uint32_t(e) && a->second.Health) return true;
            for (const auto& [key, def] : m_Definitions)
                for (const auto& visual : def.Visuals)
                    if (visual.ID == uuid) return true;
            e = m_Scene->GetParent(e);
        }
        return false;
    }
    double CombatWorld::WorldSweep(const ProjectileSpec& spec, const Segment& segment, bool first)
    {
        constexpr double None = 2; // no blocker on this segment (a hit at its very end has T = 1)
        if (spec.BlockMask == 0) return None;
        auto delta = segment.To - segment.From;
        if (spec.TwoD) delta.z = 0;
        const double length = glm::length(delta);
        if (!(length > 1e-9)) return None;
        const auto direction = delta / length;
        // After the first step the query starts slightly behind the segment: a step that ended on a surface (or
        // inside a collider's skin) must not let the next query start inside it, which queries ignore.
        const double back = first ? 0.0 : std::min(.02, length);
        const auto start = segment.From - direction * back;
        double blocker = None;
        auto consider = [&](const std::vector<RaycastHit>& hits) {
            for (const auto& hit : hits)
            {
                if (IgnoredBlocker(hit.EntityID)) continue;
                blocker = std::min(blocker, std::clamp((double(hit.Distance) - back) / length, 0.0, 1.0));
                break; // closest first
            }
        };
        ++m_Sweeps;
        const float distance = float(length + back);
        if (spec.TwoD)
        {
            const glm::vec2 from(start), towards(direction);
            if (spec.Radius <= 0) consider(m_Scene->Raycast2DAll(from, towards, distance, spec.BlockMask, false));
            else
            {
                // Three parallel rays (centre, +-radius along the normal) approximate the swept circle.
                const glm::vec2 side = glm::vec2(-towards.y, towards.x) * float(spec.Radius);
                for (float k : { 0.0f, 1.0f, -1.0f }) consider(m_Scene->Raycast2DAll(from + side * k, towards, distance, spec.BlockMask, false));
            }
        }
        else
        {
            const glm::vec3 from(start), towards(direction);
            consider(spec.Radius > 0 ? m_Scene->SphereCast3DAll(from, float(spec.Radius), towards, distance, spec.BlockMask, false)
                                     : m_Scene->Raycast3DAll(from, towards, distance, spec.BlockMask, false));
        }
        return blocker;
    }
    void CombatWorld::ApplyPayload(Slot& s, Entity owner, Entity victim, const glm::dvec3& point)
    {
        const auto& spec = s.Def->Spec;
        const auto& state = s.State;
        auto& rules = RuleWorld::For(*m_Scene);
        double applied = 0;
        bool killed = false;
        if (spec.Kind == PayloadKind::Damage)
        {
            DamageRequest request;
            request.Source = owner; // null when ownerless: no kill credit
            request.Amount = state.Damage;
            request.SourceTeam = state.Team;
            request.FriendlyFire = spec.FriendlyFire;
            request.PierceShield = spec.PierceShield;
            request.IgnoreGrace = spec.IgnoreGrace;
            request.SourceAttack = false; // Attack is already in the launch snapshot
            request.Cause = spec.Key;
            const auto result = rules.Damage(victim, request);
            applied = result.Applied;
            killed = result.Killed;
        }
        else applied = rules.Heal(victim, state.Damage, spec.Kind == PayloadKind::Shield, owner, spec.Key);
        const uint64_t source = owner ? uint64_t(owner.GetUUID()) : 0, subject = victim.GetUUID();
        Record(source, subject, spec.Key, applied, spec.Kind, killed, point);
        if (owner) Emit(owner, "Hit", spec.Key, source, subject, applied, point);
        Emit(victim, "Hit", spec.Key, source, subject, applied, point);
        ++m_Hits;
        // The payload's status effect follows every committed hit on a target that survived it (an absorbed
        // or zero-damage hit included). Admission was decided by the payload; the effect matches its kind.
        const auto& payload = state.Effect.empty() ? spec.Effect : state.Effect;
        if (!payload.empty() && !killed && !rules.Dead(victim))
            if (auto effect = m_EffectSpecs.find(payload); effect != m_EffectSpecs.end())
                Apply(victim, effect->second, owner, state.Team, spec.FriendlyFire, -1);
    }
    const CombatWorld::Book* CombatWorld::FindBook(Entity e) const
    {
        Check(e);
        auto found = m_Books.find(uint64_t(e.GetUUID()));
        return found == m_Books.end() || found->second.Handle != uint32_t(e) ? nullptr : &found->second;
    }
    void CombatWorld::Recompose(Book& book, Entity e)
    {
        book.Modifiers = ComposeModifiers(book.Effects, m_EffectSpecs);
        // A lost MaxEnergy bonus clamps energy at once, so loadout state never exceeds its capacity.
        ClampLoadoutEnergy(*m_Scene, e);
    }
    ApplyResult CombatWorld::Apply(Entity target, const EffectSpec& spec, Entity source, uint16_t team, bool friendlyFire, double magnitude)
    {
        ApplyResult out;
        auto reject = [&](const char* reason) {
            out.Reason = reason;
            return out;
        };
        auto* rules = m_Scene->GetRegistry().ctx().find<RuleWorld>();
        const auto* actor = rules ? rules->FindActor(target) : nullptr;
        if (!actor || !actor->Health) return reject("Effect target health not configured");
        if (!m_Scene->IsActiveInHierarchy(target)) return reject("Effect target inactive");
        if (actor->Health->Dead) return reject("Effect target dead");
        const uint64_t id = target.GetUUID();
        auto found = m_Books.find(id);
        if (found != m_Books.end() && found->second.Handle != uint32_t(target))
        {
            // A recreated entity with the same UUID starts with no effects.
            m_EffectCount -= found->second.Effects.Instances.size();
            m_Books.erase(found);
            found = m_Books.end();
        }
        size_t same = 0;
        if (found != m_Books.end())
            for (const auto& i : found->second.Effects.Instances) same += i.Key == spec.Key;
        const bool adds = same == 0 || (spec.Rule == StackRule::Independent && same < size_t(spec.MaxStacks));
        if (adds && m_EffectCount >= MaxEffects) return reject("Scene effect limit 1024");
        if (found == m_Books.end())
        {
            if (m_Books.size() >= MaxActors) std::erase_if(m_Books, [](const auto& item) { return item.second.Effects.Instances.empty(); });
            if (m_Books.size() >= MaxActors) return reject("Combat actor limit 512");
            found = m_Books.try_emplace(id).first;
            found->second.Handle = uint32_t(target);
            found->second.Effects.Instances.reserve(EffectBook::Capacity);
        }
        auto& book = found->second;
        EffectApplication app;
        app.Source = source ? uint64_t(source.GetUUID()) : 0;
        app.SourceHandle = source ? uint32_t(source) : ~0u;
        app.Team = team;
        app.FriendlyFire = friendlyFire;
        app.Magnitude = magnitude;
        const size_t before = book.Effects.Instances.size();
        out = Gameplay::ApplyEffect(book.Effects, spec, app);
        m_EffectCount = m_EffectCount + book.Effects.Instances.size() - before;
        if (out.Outcome == ApplyOutcome::Rejected) return out;
        Recompose(book, target);
        const bool fresh = out.Outcome == ApplyOutcome::Added || out.Outcome == ApplyOutcome::Replaced;
        Emit(target, fresh ? "EffectApplied" : "EffectRefreshed", spec.Key, app.Source, id, out.Stacks, D(m_Scene->GetWorldPosition(target)));
        return out;
    }
    ApplyResult CombatWorld::ApplyEffect(Entity target, const std::string& key, Entity source, double magnitude)
    {
        Check(target);
        if (source) Check(source);
        if (!std::isfinite(magnitude) || magnitude > 1e5) throw std::runtime_error("Effect magnitude must be finite, at most 100000");
        ApplyResult out;
        auto reject = [&](const char* reason) {
            out.Reason = reason;
            return out;
        };
        if (!m_Scene->IsRunning()) return reject("Combat requires Play");
        auto found = m_EffectSpecs.find(key);
        if (found == m_EffectSpecs.end()) return reject("Effect not defined");
        const auto& spec = found->second;
        auto& rules = RuleWorld::For(*m_Scene);
        uint16_t team = 0;
        if (source)
            if (auto health = rules.Health(source)) team = health->Settings.Team;
        // With a source the team rule applies (hostile: not allies; beneficial: allies and self). An explicit self
        // target is always allowed; without a source (environment) any actor may be affected.
        if (source && source != target)
        {
            const auto victim = rules.Health(target);
            if (victim && !Admits(spec.Hostile ? PayloadKind::Damage : PayloadKind::Heal, false, source.GetUUID(), team, target.GetUUID(), victim->Settings.Team, false))
                return reject("Effect target not allowed");
        }
        if (m_EffectCalls >= EffectBudget) return reject("Effect budget 64");
        ++m_EffectCalls;
        return Apply(target, spec, source, team, false, magnitude);
    }
    size_t CombatWorld::RemoveEffects(Entity e, const std::string& key)
    {
        Check(e);
        if (!key.empty() && !ValidItem(key)) throw std::runtime_error("Effect key must be empty or a 1..64 character key");
        auto found = m_Books.find(uint64_t(e.GetUUID()));
        if (found == m_Books.end() || found->second.Handle != uint32_t(e)) return 0;
        const size_t removed = Gameplay::RemoveEffects(found->second.Effects, key);
        if (removed == 0) return 0;
        m_EffectCount -= removed;
        Recompose(found->second, e);
        Emit(e, "EffectRemoved", key, 0, e.GetUUID(), double(removed), D(m_Scene->GetWorldPosition(e)));
        return removed;
    }
    void CombatWorld::ClearEffects(Entity e)
    {
        auto found = m_Books.find(uint64_t(e.GetUUID()));
        if (found == m_Books.end() || found->second.Handle != uint32_t(e) || found->second.Effects.Instances.empty()) return;
        const size_t removed = found->second.Effects.Instances.size();
        found->second.Effects.Instances.clear();
        m_EffectCount -= removed;
        Recompose(found->second, e);
        Emit(e, "EffectCleared", {}, 0, e.GetUUID(), double(removed), D(m_Scene->GetWorldPosition(e)));
    }
    EffectView CombatWorld::Effect(Entity e, const std::string& key) const
    {
        EffectView view;
        const auto* book = FindBook(e);
        auto spec = m_EffectSpecs.find(key);
        if (!book || spec == m_EffectSpecs.end()) return view;
        const bool refresh = spec->second.Rule == StackRule::Refresh;
        for (const auto& i : book->Effects.Instances)
        {
            if (i.Key != key) continue;
            ++view.Instances;
            view.Stacks = refresh ? i.Stacks : view.Instances;
            view.Remaining = std::max(view.Remaining, i.Remaining);
            view.Magnitude += i.Magnitude * (refresh ? double(i.Stacks) : 1.0);
        }
        view.Active = view.Instances > 0;
        view.Duration = EffectDurationCap(spec->second);
        return view;
    }
    EffectModifiers CombatWorld::Modifiers(Entity e) const
    {
        const auto* book = FindBook(e);
        return book ? book->Modifiers : EffectModifiers{};
    }
    size_t CombatWorld::Effects(Entity e, std::span<EffectInstance> out) const
    {
        const auto* book = FindBook(e);
        if (!book) return 0;
        const size_t count = std::min(out.size(), book->Effects.Instances.size());
        std::copy_n(book->Effects.Instances.begin(), count, out.begin());
        return count;
    }
    bool CombatWorld::Visible(const AreaSpec& spec, const glm::dvec3& from, const glm::dvec3& to)
    {
        auto delta = to - from;
        if (spec.TwoD) delta.z = 0;
        const double distance = glm::length(delta);
        if (!(distance >= .001)) return true;
        ++m_Rays;
        const auto direction = delta / distance;
        const auto hits = spec.TwoD ? m_Scene->Raycast2DAll(glm::vec2(from), glm::vec2(direction), float(distance), spec.Mask, false)
                                    : m_Scene->Raycast3DAll(glm::vec3(from), glm::vec3(direction), float(distance), spec.Mask, false);
        // As RuleWorld::Visible: a blocker counts only before the target point; actors never occlude.
        for (const auto& hit : hits)
            if (!IgnoredBlocker(hit.EntityID) && double(hit.Distance) < distance - .01) return false;
        return true;
    }
    AreaResult CombatWorld::Area(const AreaSpec& spec, Entity source, uint64_t sourceID, uint16_t team, bool teams, glm::dvec3 center, double magnitude, const EffectSpec* effect)
    {
        ZM_PROFILE_SCOPE("Hududlar");
        AreaResult out;
        out.Success = true;
        ++m_Detonations;
        auto* rules = m_Scene->GetRegistry().ctx().find<RuleWorld>();
        if (!effect && !spec.Effect.empty())
            if (auto found = m_EffectSpecs.find(spec.Effect); found != m_EffectSpecs.end()) effect = &found->second;
        m_AreaTargets.clear();
        if (rules)
            for (const auto& [id, actor] : rules->m_Actors)
            {
                if (!actor.Health || actor.Health->Dead) continue;
                auto e = m_Scene->GetEntityByUUID(UUID(id));
                if (!e || uint32_t(e) != actor.Handle || !m_Scene->IsActiveInHierarchy(e)) continue;
                // Without a source (environment) the area ignores teams; otherwise the payload admission applies. A
                // projectile always has a team snapshot, so an ownerless one (sourceID 0 after a load) still uses it.
                if (teams && !Admits(spec.Kind, spec.FriendlyFire, sourceID, team, id, actor.Health->Settings.Team, spec.AffectOwner)) continue;
                auto delta = D(m_Scene->GetWorldPosition(e)) + spec.TargetOffset - center;
                if (spec.TwoD) delta.z = 0;
                if (const double distance = glm::length(delta); distance <= spec.Radius && m_AreaTargets.size() < MaxActors) m_AreaTargets.emplace_back(distance, id);
            }
        // With line of sight every ray is spent on the nearest candidates: at most 32 rays per area.
        OrderAreaTargets(m_AreaTargets, spec.LineOfSight ? int(MaxAreaRays) : spec.MaxTargets);
        for (const auto& [distance, id] : m_AreaTargets)
        {
            if (out.Affected >= uint32_t(spec.MaxTargets)) break;
            auto victim = m_Scene->GetEntityByUUID(UUID(id));
            if (!victim || rules->Dead(victim) || !m_Scene->IsActiveInHierarchy(victim)) continue;
            const auto point = D(m_Scene->GetWorldPosition(victim)) + spec.TargetOffset;
            if (spec.LineOfSight && !Visible(spec, center, point)) continue;
            const double amount = std::min(1e6, magnitude * AreaScale(spec, distance));
            double applied = 0;
            bool killed = false;
            if (spec.Kind == PayloadKind::Damage)
            {
                DamageRequest request;
                request.Source = source;
                request.Amount = amount;
                request.SourceTeam = team;
                request.FriendlyFire = spec.FriendlyFire || !teams;
                request.PierceShield = spec.PierceShield;
                request.IgnoreGrace = spec.IgnoreGrace;
                request.SourceAttack = false; // Attack is part of the magnitude (launch or cast snapshot)
                request.Cause = spec.Key;
                const auto result = rules->Damage(victim, request);
                applied = result.Applied;
                killed = result.Killed;
            }
            else applied = rules->Heal(victim, amount, spec.Kind == PayloadKind::Shield, source, spec.Key);
            const uint64_t credited = source ? uint64_t(source.GetUUID()) : 0;
            Record(credited, id, spec.Key, applied, spec.Kind, killed, point);
            Emit(victim, "Hit", spec.Key, credited, id, applied, point);
            ++out.Affected;
            out.Applied += applied;
            if (effect && !killed && !rules->Dead(victim)) Apply(victim, *effect, source, team, spec.FriendlyFire || !teams, -1);
        }
        if (source) Emit(source, "Detonated", spec.Key, source.GetUUID(), 0, double(out.Affected), center);
        if (DebugDraw::IsShowingRaycasts())
        {
            const auto c = glm::vec3(center);
            if (spec.TwoD) DebugDraw::Circle(c, float(spec.Radius), DebugColor::Orange, .5f);
            else DebugDraw::WireSphere(c, float(spec.Radius), DebugColor::Orange, .5f);
        }
        return out;
    }
    AreaResult CombatWorld::ApplyArea(const std::string& key, Entity source, glm::vec3 center, double magnitude, const std::string& effect)
    {
        if (!effect.empty() && !ValidItem(effect)) throw std::runtime_error("Area effect override must be empty or a 1..64 character key");
        if (source) Check(source);
        if (!Finite(center) || glm::length(D(center)) > 1e9) throw std::runtime_error("Area center must be finite, within 1e9");
        if (!std::isfinite(magnitude) || magnitude > 1e6) throw std::runtime_error("Area magnitude must be finite, at most 1000000");
        AreaResult out;
        auto fail = [&](std::string reason) {
            out.Reason = std::move(reason);
            return out;
        };
        if (!m_Scene->IsRunning()) return fail("Combat requires Play");
        auto found = m_Areas.find(key);
        if (found == m_Areas.end()) return fail("Area not defined");
        const auto& spec = found->second;
        if (auto error = AreaError(spec); !error.empty()) return fail(error);
        if (auto error = EffectError(spec.Kind, effect, "Area effect not defined", "Area effect kind mismatch"); !error.empty()) return fail(error);
        uint16_t team = 0;
        if (source)
        {
            if (!m_Scene->IsActiveInHierarchy(source)) return fail("Area source inactive");
            if (auto health = RuleWorld::For(*m_Scene).Health(source))
            {
                if (health->Dead) return fail("Area source dead");
                team = health->Settings.Team;
            }
            if (Modifiers(source).Stunned) return fail("Stunned");
        }
        if (m_AreaCalls >= AreaBudget) return fail("Area budget 16");
        ++m_AreaCalls;
        double amount = magnitude < 0 ? spec.Amount : magnitude;
        // The source's Attack joins before the falloff, exactly as a projectile's launch snapshot does.
        if (spec.Kind == PayloadKind::Damage && source) amount = std::min(1e6, amount + LoadoutAttack(*m_Scene, source));
        return Area(spec, source, source ? uint64_t(source.GetUUID()) : 0, team, bool(source), D(center), amount, effect.empty() ? nullptr : &m_EffectSpecs.at(effect));
    }
    void CombatWorld::Detonate(Slot& s, Entity owner)
    {
        const auto& spec = s.Def->Spec;
        const auto& state = s.State;
        auto area = m_Areas.find(spec.Area);
        if (area == m_Areas.end()) return; // resolved at Fire; definitions are immutable
        double magnitude = area->second.Amount;
        if (area->second.Kind == PayloadKind::Damage) magnitude = std::min(1e6, magnitude + state.Attack);
        // An ownerless projectile keeps its team and still excludes its former owner's UUID (0 when unknown).
        Area(area->second, owner, state.Owner, state.Team, true, state.Position + state.Muzzle, magnitude, nullptr);
    }
    void CombatWorld::EffectsPhase(double h)
    {
        ZM_PROFILE_SCOPE("Effektlar");
        auto* rules = m_Scene->GetRegistry().ctx().find<RuleWorld>();
        if (!rules) return;
        // Books are in UUID order; instances and their ticks in Sequence order.
        for (auto& [id, book] : m_Books)
        {
            if (book.Effects.Instances.empty()) continue;
            auto e = m_Scene->GetEntityByUUID(UUID(id));
            if (!e || uint32_t(e) != book.Handle) continue;
            const auto* actor = rules->FindActor(e);
            // Inactive (and, transiently, dead) actors keep their effects frozen: no ticks, no expiry.
            if (!actor || !actor->Health || actor->Health->Dead || !m_Scene->IsActiveInHierarchy(e)) continue;
            m_Ticks.clear();
            m_Expired.clear();
            const size_t before = book.Effects.Instances.size();
            TickEffects(book.Effects, m_EffectSpecs, h, m_Ticks, m_Expired);
            m_EffectCount -= before - book.Effects.Instances.size();
            const auto position = D(m_Scene->GetWorldPosition(e));
            if (!m_Expired.empty())
            {
                Recompose(book, e);
                for (const auto& x : m_Expired)
                    if (x.Spec) Emit(e, "EffectExpired", x.Spec->Key, x.Source, id, 0, position);
            }
            for (const auto& tick : m_Ticks)
            {
                if (rules->Dead(e) || !m_Scene->IsActiveInHierarchy(e)) break; // died: its effects were cleared
                uint32_t handle = tick.SourceHandle;
                const Entity source = Resolve(tick.Source, handle);
                double applied = 0;
                bool killed = false;
                if (tick.Kind == TickKind::Damage)
                {
                    DamageRequest request;
                    request.Source = source; // gone: no kill credit, the team snapshot still applies
                    request.Amount = tick.Amount;
                    request.SourceTeam = tick.Team;
                    request.FriendlyFire = tick.FriendlyFire;
                    request.PierceShield = tick.Spec->PierceShield;
                    request.IgnoreGrace = true;   // ticks are not stopped by hit grace ...
                    request.StartGrace = false;   // ... and grant none
                    request.SourceAttack = false; // only the victim's Armor applies
                    request.Cause = tick.Spec->Key;
                    const auto result = rules->Damage(e, request);
                    applied = result.Applied;
                    killed = result.Killed;
                }
                else applied = rules->Heal(e, tick.Amount, tick.Kind == TickKind::Shield, source, tick.Spec->Key);
                ++m_TickCount;
                // A tick that changed nothing (a heal at full health, damage fully absorbed) is no hit: it would only
                // push real hits out of the HUD feed.
                if (!(applied > 0) && !killed) continue;
                const auto kind = tick.Kind == TickKind::Damage ? PayloadKind::Damage : tick.Kind == TickKind::Heal ? PayloadKind::Heal
                                                                                                                    : PayloadKind::Shield;
                Record(source ? uint64_t(source.GetUUID()) : 0, id, tick.Spec->Key, applied, kind, killed, position, true);
            }
        }
    }
    void CombatWorld::Step(double h)
    {
        m_Now += h;
        ++m_Steps;
        // Effects first: a damage-over-time kill this step makes its victim untouchable for the projectiles.
        if (m_EffectCount > 0) EffectsPhase(h);
        if (m_Pool.Count() == 0) return;
        ZM_PROFILE_SCOPE("Snaryadlar");
        auto& rules = RuleWorld::For(*m_Scene);
        uint32_t detonations = 0;
        // At most 16 detonations per step; the rest keep their slot and detonate on a later step, by slot.
        auto detonate = [&](uint32_t slot, Entity owner) {
            m_Slots[slot].State.Phase = ProjectilePhase::Detonating;
            if (detonations >= DetonationBudget) return;
            ++detonations;
            Detonate(m_Slots[slot], owner);
            Release(slot);
        };
        for (uint32_t slot = 0; slot < ProjectilePool::Capacity; ++slot)
        {
            if (!m_Pool.Active(slot)) continue;
            auto& s = m_Slots[slot];
            const auto& spec = s.Def->Spec;
            auto& state = s.State;
            const Entity owner = Resolve(state.Owner, s.OwnerHandle);
            const Entity target = Resolve(state.Target, s.TargetHandle);
            if (state.Phase == ProjectilePhase::Detonating)
            {
                detonate(slot, owner);
                continue;
            }
            const bool first = state.Age <= 0;
            glm::dvec3 homing{};
            const bool homes = spec.Homing > 0 && target && m_Scene->IsActiveInHierarchy(target);
            if (homes) homing = D(m_Scene->GetWorldPosition(target)) + spec.TargetOffset;
            const auto segment = AdvanceProjectile(spec, state, h, homes ? &homing : nullptr);
            const size_t candidates = SweepActors(spec, state, segment, m_Probes, m_Candidates);
            m_ActorTests += uint32_t(m_Probes.size());
            const double blocker = WorldSweep(spec, segment, first);
            bool spent = false;
            for (size_t i = 0; i < candidates && !spent; ++i)
            {
                const auto& c = m_Candidates[i];
                if (c.T >= blocker) break; // behind the wall
                auto probe = std::lower_bound(m_Probes.begin(), m_Probes.end(), c.ID, [](const ActorProbe& p, uint64_t id) { return p.ID < id; });
                if (probe == m_Probes.end() || probe->ID != c.ID) continue;
                const Entity victim = m_ProbeEntities[size_t(probe - m_Probes.begin())];
                // Killed earlier this step (another projectile, lower slot) or deactivated: pass through.
                if (!victim.IsValid() || rules.Dead(victim) || !m_Scene->IsActiveInHierarchy(victim)) continue;
                const auto point = Lerp(segment, c.T);
                ApplyPayload(s, owner, victim, point);
                // A rejected hit (grace, zero damage, full health) still consumes the shot: "absorbed".
                if (state.HitCount < ProjectileState::MaxHits) state.Hits[state.HitCount++] = c.ID;
                if (state.PierceLeft == 0)
                {
                    state.Position = point;
                    spent = true;
                }
                else --state.PierceLeft;
            }
            const bool area = !spec.Area.empty();
            if (spent)
            {
                // The impact point is the area centre: the direct victim is inside an area at least as large as
                // Radius + TargetRadius.
                if (area) detonate(slot, owner);
                else Release(slot);
                continue;
            }
            if (blocker <= 1)
            {
                state.Position = Lerp(segment, blocker);
                if (owner) Emit(owner, "Impact", spec.Key, state.Owner, 0, 0, state.Position);
                if (area)
                {
                    // The area is centred 2 cm in front of the surface: its line-of-sight rays must not start on
                    // (or inside) the wall that was hit, which physics queries ignore, and so see through it.
                    auto travel = segment.To - segment.From;
                    if (spec.TwoD) travel.z = 0;
                    if (const double length = glm::length(travel); length > 1e-9) state.Position -= travel / length * ImpactBackoff;
                    detonate(slot, owner);
                }
                else Release(slot);
                continue;
            }
            // Lifetime and distance round up to whole fixed steps; the final segment was swept in full.
            if (state.Age >= spec.Lifetime - 1e-9 || (spec.MaxDistance > 0 && state.Travelled >= spec.MaxDistance - 1e-9))
            {
                if (owner) Emit(owner, "Expired", spec.Key, state.Owner, 0, 0, state.Position);
                if (area && spec.DetonateOnExpire) detonate(slot, owner);
                else Release(slot);
            }
        }
    }
    void CombatWorld::SyncVisuals()
    {
        size_t total = 0;
        for (auto& [key, def] : m_Definitions)
        {
            bool erased = false;
            for (size_t i = 0; i < def.Visuals.size();)
            {
                auto e = m_Scene->GetEntityByUUID(UUID(def.Visuals[i].ID));
                if (e && uint32_t(e) == def.Visuals[i].Handle)
                {
                    ++i;
                    continue;
                }
                // Destroyed by something else: dropped from the pool; gameplay is unaffected.
                if (def.Visuals[i].Slot >= 0) m_Slots[size_t(def.Visuals[i].Slot)].Visual = -1;
                def.Visuals.erase(def.Visuals.begin() + std::ptrdiff_t(i));
                erased = true;
            }
            if (erased)
                for (size_t i = 0; i < def.Visuals.size(); ++i)
                    if (def.Visuals[i].Slot >= 0) m_Slots[size_t(def.Visuals[i].Slot)].Visual = int(i);
            total += def.Visuals.size();
        }
        for (uint32_t slot = 0; slot < ProjectilePool::Capacity; ++slot)
        {
            if (!m_Pool.Active(slot) || m_Slots[slot].Visual >= 0) continue;
            auto& s = m_Slots[slot];
            auto& def = *s.Def;
            if (def.Prefab.empty() || def.VisualsDisabled || def.Spec.Pool <= 0) continue;
            int chosen = -1;
            for (size_t i = 0; i < def.Visuals.size() && chosen < 0; ++i)
                if (def.Visuals[i].Slot < 0) chosen = int(i);
            if (chosen < 0 && def.Visuals.size() < size_t(def.Spec.Pool) && total < MaxVisuals && m_Instantiations < VisualBudget)
            {
                std::string error;
                if (Instantiate(def, error))
                {
                    chosen = int(def.Visuals.size()) - 1;
                    ++total;
                }
            }
            if (chosen < 0) continue; // flies without a visual; later frames fill the pool
            def.Visuals[size_t(chosen)].Slot = int(slot);
            s.Visual = chosen;
        }
        for (auto& [key, def] : m_Definitions)
            for (auto& visual : def.Visuals)
            {
                auto e = m_Scene->GetEntityByUUID(UUID(visual.ID));
                if (visual.Slot < 0)
                {
                    if (visual.Shown) m_Scene->SetActive(e, false);
                    visual.Shown = false;
                    continue;
                }
                const auto& state = m_Slots[size_t(visual.Slot)].State;
                auto& t = e.GetTransform();
                t.Translation = glm::vec3(state.Position + state.Muzzle);
                const auto v = state.Velocity;
                if (def.Spec.TwoD) t.Rotation = { 0, 0, float(std::atan2(v.y, v.x)) };
                else if (glm::length(v) > 1e-9)
                {
                    const auto forward = glm::vec3(glm::normalize(v));
                    const auto up = std::abs(forward.y) > .99f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
                    t.Rotation = glm::eulerAngles(glm::quatLookAt(forward, up));
                }
                if (!visual.Shown) m_Scene->SetActive(e, true);
                visual.Shown = true;
            }
    }
    void CombatWorld::Tick(double delta)
    {
        if (!std::isfinite(delta) || delta < 0 || delta > 1e6) throw std::runtime_error("Invalid combat clock delta");
        ZM_PROFILE_SCOPE("Jang: snaryad va effektlar");
        // The physics fixed step and its frame cap: a frame at the cap drops the remaining time, as physics does.
        auto rate = m_Scene->GetSettings().PhysicsStep;
        rate.Sanitize();
        const double h = 1.0 / double(rate.StepRate);
        m_Accumulator += delta;
        // Physics has stepped already this frame: positions are final for all of this frame's combat steps.
        if (m_Pool.Count() > 0) BuildProbes();
        int steps = 0;
        while (m_Accumulator + 1e-9 >= h && steps < rate.MaxStepsPerFrame)
        {
            Step(h);
            m_Accumulator -= h;
            ++steps;
        }
        if (steps == rate.MaxStepsPerFrame)
        {
            m_DroppedTime += std::max(0.0, m_Accumulator);
            m_Accumulator = 0;
        }
        SyncVisuals();
    }
    // ---------------------------------------------------------------------------------------------- save v4
    std::vector<SavedEffect> CombatWorld::SaveEffects(Entity e, const Referencer& reference) const
    {
        std::vector<SavedEffect> out;
        const auto* book = FindBook(e);
        if (!book) return out;
        out.reserve(book->Effects.Instances.size());
        for (const auto& i : book->Effects.Instances)
        {
            SavedEffect saved;
            saved.Key = i.Key;
            // A source that is gone (destroyed or recreated) is saved as none: no kill credit after the load.
            if (i.Source)
                if (auto source = m_Scene->GetEntityByUUID(UUID(i.Source)); source && uint32_t(source) == i.SourceHandle) saved.Source = reference(i.Source);
            saved.Team = i.SourceTeam;
            saved.FriendlyFire = i.FriendlyFire;
            saved.Stacks = i.Stacks;
            saved.Remaining = i.Remaining;
            saved.Tick = i.UntilTick;
            saved.Magnitude = i.Magnitude;
            saved.Sequence = i.Sequence;
            out.push_back(std::move(saved));
        }
        return out;
    }
    SavedCombat CombatWorld::SaveCombat(const Referencer& reference) const
    {
        SavedCombat out;
        out.Signature = Signature();
        auto live = [&](uint64_t id, uint32_t handle) {
            auto e = id && handle != ~0u ? m_Scene->GetEntityByUUID(UUID(id)) : Entity{};
            return e && uint32_t(e) == handle ? reference(id) : std::string{};
        };
        for (uint32_t slot = 0; slot < ProjectilePool::Capacity; ++slot)
        {
            if (!m_Pool.Active(slot)) continue;
            const auto& s = m_Slots[slot];
            const auto& state = s.State;
            SavedProjectile p;
            p.Slot = slot;
            p.Generation = m_Pool.Generation(slot);
            p.Key = state.Key;
            p.Effect = state.Effect;
            p.Owner = live(state.Owner, s.OwnerHandle); // ownerless: saved without an owner ...
            if (p.Owner.empty() && state.Owner) p.FormerOwner = reference(state.Owner); // ... but still spared
            p.Target = live(state.Target, s.TargetHandle);
            p.Team = state.Team;
            p.Position = state.Position;
            p.Velocity = state.Velocity;
            p.Muzzle = state.Muzzle;
            p.Age = state.Age;
            p.Travelled = state.Travelled;
            p.Damage = state.Damage;
            p.Attack = state.Attack;
            p.PierceLeft = state.PierceLeft;
            p.Phase = uint32_t(state.Phase);
            for (size_t i = 0; i < state.HitCount; ++i) p.Hits.push_back(state.Hits[i] ? reference(state.Hits[i]) : std::string{});
            out.Projectiles.push_back(std::move(p));
        }
        return out;
    }
    void CombatWorld::BeginRestore(RestorePlan& plan, const std::vector<uint64_t>& bound) const
    {
        // Unbound actors keep their runtime state (as their health does); bound ones take the snapshot's effects.
        plan.Books = m_Books;
        for (uint64_t id : bound) plan.Books.erase(id);
        // Every projectile in flight is replaced by the snapshot's; releasing bumps generations, so handles from
        // before the load never name a restored or later projectile by accident.
        plan.Pool = m_Pool;
        plan.Pool.Clear();
        plan.Slots = {};
    }
    CombatStats CombatWorld::StageEffects(RestorePlan& plan, Entity e, const std::vector<SavedEffect>& saved, const Resolver& resolve) const
    {
        if (saved.empty()) return {};
        if (saved.size() > EffectBook::Capacity) throw std::runtime_error("Saved effect limit 16");
        Book book;
        book.Handle = uint32_t(e);
        book.Effects.Instances.reserve(EffectBook::Capacity);
        for (const auto& s : saved)
        {
            EffectInstance i;
            i.Key = s.Key;
            const auto source = resolve(s.Source);
            i.Source = source.Live ? uint64_t(source.Live.GetUUID()) : 0; // unresolved: no kill credit
            i.SourceHandle = source.Live ? uint32_t(source.Live) : ~0u;
            i.SourceTeam = uint16_t(s.Team);
            i.FriendlyFire = s.FriendlyFire;
            i.Stacks = s.Stacks;
            i.Remaining = s.Remaining;
            i.UntilTick = s.Tick;
            i.Magnitude = s.Magnitude;
            i.Sequence = s.Sequence;
            book.Effects.Instances.push_back(std::move(i));
        }
        book.Effects.NextSequence = book.Effects.Instances.back().Sequence + 1;
        std::string error;
        if (!ValidateEffectBook(book.Effects, m_EffectSpecs, error)) throw std::runtime_error("Invalid saved effects: " + error);
        book.Modifiers = ComposeModifiers(book.Effects, m_EffectSpecs);
        size_t total = book.Effects.Instances.size();
        for (const auto& [id, other] : plan.Books)
            if (id != uint64_t(e.GetUUID())) total += other.Effects.Instances.size();
        if (total > MaxEffects) throw std::runtime_error("Saved effects exceed the scene effect limit 1024");
        const auto bonus = book.Modifiers.Add;
        plan.Books.insert_or_assign(uint64_t(e.GetUUID()), std::move(book));
        if (plan.Books.size() > MaxActors) throw std::runtime_error("Saved effects exceed the combat actor limit 512");
        return bonus;
    }
    void CombatWorld::StageProjectiles(RestorePlan& plan, const SavedCombat& saved, const Resolver& resolve)
    {
        std::map<uint64_t, size_t> owned;
        for (const auto& p : saved.Projectiles)
        {
            auto found = m_Definitions.find(p.Key);
            if (found == m_Definitions.end()) throw std::runtime_error("Saved projectile not defined");
            const auto& spec = found->second.Spec;
            ProjectileState state;
            state.Key = p.Key;
            state.Effect = p.Effect;
            // An owner that no longer resolves leaves the projectile ownerless (no credit, team snapshot kept); its
            // UUID, or the saved former owner's, stays as the exclusion, exactly as for an owner gone at runtime.
            const auto owner = resolve(p.Owner), target = resolve(p.Target);
            state.Owner = owner.Live ? uint64_t(owner.Live.GetUUID()) : owner.ID;
            if (!state.Owner) state.Owner = resolve(p.FormerOwner).ID;
            state.Target = target.Live ? uint64_t(target.Live.GetUUID()) : 0;
            state.Team = uint16_t(p.Team);
            state.Position = p.Position;
            state.Velocity = p.Velocity;
            state.Muzzle = p.Muzzle;
            state.Age = p.Age;
            state.Travelled = p.Travelled;
            state.Damage = p.Damage;
            state.Attack = p.Attack;
            if (p.Hits.size() > ProjectileState::MaxHits || p.PierceLeft > 7 || p.Phase > 1) throw std::runtime_error("Invalid saved projectile hits/pierce/phase");
            state.HitCount = uint8_t(p.Hits.size());
            // Hits only exclude targets: an authored "uuid:" reference keeps its UUID even when nothing has it.
            for (size_t i = 0; i < p.Hits.size(); ++i) state.Hits[i] = resolve(p.Hits[i]).ID;
            state.PierceLeft = uint8_t(p.PierceLeft);
            state.Phase = ProjectilePhase(p.Phase);
            std::string error;
            if (!ValidateProjectileState(spec, state, error)) throw std::runtime_error("Invalid saved projectile: " + error);
            if (auto payload = EffectError(spec.Kind, state.Effect, "Saved projectile effect not defined", "Saved projectile effect kind mismatch"); !payload.empty()) throw std::runtime_error(payload);
            if (owner.Live && ++owned[state.Owner] > MaxPerOwner) throw std::runtime_error("Saved projectiles exceed the owner limit 32");
            if (!plan.Pool.Place(p.Slot, p.Generation)) throw std::runtime_error("Invalid or duplicate saved projectile slot");
            auto& slot = plan.Slots[p.Slot];
            slot.State = std::move(state);
            slot.Def = &found->second;
            slot.OwnerHandle = owner.Live ? uint32_t(owner.Live) : ~0u;
            slot.TargetHandle = target.Live ? uint32_t(target.Live) : ~0u;
            slot.Visual = -1;
        }
    }
    void CombatWorld::CommitRestore(RestorePlan& plan)
    {
        for (auto& [key, def] : m_Definitions)
            for (auto& visual : def.Visuals)
            {
                visual.Slot = -1;
                auto e = m_Scene->GetEntityByUUID(UUID(visual.ID));
                if (visual.Shown && e && uint32_t(e) == visual.Handle) m_Scene->SetActive(e, false);
                visual.Shown = false;
            }
        m_Books.swap(plan.Books);
        m_EffectCount = 0;
        for (const auto& [id, book] : m_Books) m_EffectCount += book.Effects.Instances.size();
        m_Pool = plan.Pool;
        m_Slots.swap(plan.Slots);
        for (auto& slot : m_Slots) slot.Visual = -1;
        m_Events.clear();
        m_HitHead = m_HitCount = 0;
        m_Accumulator = 0;
    }
}
