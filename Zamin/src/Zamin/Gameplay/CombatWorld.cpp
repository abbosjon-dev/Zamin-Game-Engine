#include "CombatWorld.h"
#include "LoadoutWorld.h"
#include "Zamin/Core/Log.h"
#include "Zamin/Core/Profiler.h"
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
        // Effect/area payload registries are not part of the projectile lifecycle; until they exist a key would
        // silently do nothing, so it is rejected.
        if (!spec.Effect.empty() || !spec.Area.empty())
        {
            error = "Projectile effect/area payloads are not available";
            return false;
        }
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
        if (spec.Kind == PayloadKind::Damage) damage = std::min(1e6, damage + LoadoutAttack(*m_Scene, request.Owner));
        const uint32_t slot = *m_Pool.Acquire();
        ++m_Fires;
        auto& s = m_Slots[slot];
        auto& state = s.State;
        state.Key = spec.Key;
        state.Owner = owner;
        state.Target = request.Target ? uint64_t(request.Target.GetUUID()) : 0;
        state.Team = health->Settings.Team;
        state.Position = aim;
        state.Velocity = direction * spec.Speed;
        state.Muzzle = muzzle;
        state.Age = state.Travelled = 0;
        state.Damage = damage;
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
        (void)detonate; // detonation needs an area payload; without one a cancelled projectile is released
        if (!m_Pool.Live(handle)) return false;
        const uint32_t slot = ProjectilePool::Slot(handle);
        auto& s = m_Slots[slot];
        if (auto owner = Resolve(s.State.Owner, s.OwnerHandle))
            Emit(owner, "Cancelled", s.State.Key, s.State.Owner, 0, 0, s.State.Position + s.State.Muzzle);
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
    void CombatWorld::Record(uint64_t source, uint64_t victim, const std::string& key, double amount, PayloadKind kind, bool killed, const glm::dvec3& position)
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
    size_t CombatWorld::Recent(Entity actor, bool outgoing, bool incoming, double maxAge, std::span<HitRecord> out) const
    {
        Check(actor);
        if (!std::isfinite(maxAge) || maxAge < 0) throw std::runtime_error("Hit record age must be finite and nonnegative");
        const uint64_t id = actor.GetUUID();
        size_t count = 0;
        for (uint32_t i = 0; i < m_HitCount && count < out.size(); ++i)
        {
            const auto& r = m_HitRing[(m_HitHead + HitCapacity - 1 - i) % HitCapacity];
            if (m_Now - r.Time > maxAge) break; // the ring is in time order
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
        v.Hits = m_Hits;
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
        return nlohmann::json{ { "Projectiles", projectiles } }.dump();
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
        m_Fires = m_Instantiations = 0;
        m_Steps = m_Sweeps = m_ActorTests = 0;
        std::erase_if(m_Events, [&](const auto& item) {
            auto e = m_Scene->GetEntityByUUID(UUID(item.first));
            return !e || uint32_t(e) != item.second.Handle;
        });
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
    }
    void CombatWorld::Step(double h)
    {
        m_Now += h;
        ++m_Steps;
        if (m_Pool.Count() == 0) return;
        ZM_PROFILE_SCOPE("Snaryadlar");
        auto& rules = RuleWorld::For(*m_Scene);
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
                Release(slot); // area payloads are not defined, so nothing detonates
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
            if (spent)
            {
                Release(slot);
                continue;
            }
            if (blocker <= 1)
            {
                state.Position = Lerp(segment, blocker);
                if (owner) Emit(owner, "Impact", spec.Key, state.Owner, 0, 0, state.Position);
                Release(slot);
                continue;
            }
            // Lifetime and distance round up to whole fixed steps; the final segment was swept in full.
            if (state.Age >= spec.Lifetime - 1e-9 || (spec.MaxDistance > 0 && state.Travelled >= spec.MaxDistance - 1e-9))
            {
                if (owner) Emit(owner, "Expired", spec.Key, state.Owner, 0, 0, state.Position);
                Release(slot);
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
}
