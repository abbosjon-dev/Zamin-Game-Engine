#include "GameplayAI.h"
#include "GameplayRules.h"
#include "Zamin/Scene/Entity.h"
#include "Zamin/Scene/SceneManager.h"
#include "Zamin/Scene/WorldStreaming.h"
#include "Zamin/Scripting/Visual/VisualGraphHost.h"
#include "Zamin/Physics/PhysicsWorld2D.h"
#include "Zamin/Physics/PhysicsWorld3D.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace Zamin::Gameplay {
    namespace {
        bool Finite(glm::vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) && glm::length(glm::dvec3(v)) <= 1e9; }
        glm::vec2 Plane(glm::vec3 p, bool two) { return two ? glm::vec2(p) : glm::vec2(p.x, p.z); }
        float Distance(glm::vec3 a, glm::vec3 b, bool two) { return two ? float(glm::length(glm::dvec2(a) - glm::dvec2(b))) : float(glm::length(glm::dvec3(a) - glm::dvec3(b))); }
        void Increment(uint32_t& v)
        {
            if (v != UINT32_MAX) ++v;
        }
        WorldStreaming* World(Scene& s)
        {
            auto* manager = s.GetSceneManager();
            auto* world = manager ? manager->GetWorldStreaming() : nullptr;
            return world && world->IsAnchor(&s) ? world : nullptr;
        }
        bool PrepareRoute(const AISpec& spec, glm::vec3 home, std::vector<glm::vec3>& points, std::string& error)
        {
            if (spec.Points.size() > 64)
            {
                error = "AI patrol limit 64";
                return false;
            }
            for (auto p : spec.Points)
            {
                if (!Finite(p))
                {
                    error = "Invalid AI waypoint";
                    return false;
                }
                auto world = spec.Relative ? home + p : p;
                if (!Finite(world) || Distance(world, home, spec.Brain.TwoD) > spec.Brain.Leash)
                {
                    error = "AI waypoint exceeds home leash";
                    return false;
                }
                points.push_back(world);
            }
            return true;
        }
    }
    AIWorld& AIWorld::For(Scene& scene)
    {
        auto& ctx = scene.GetRegistry().ctx();
        if (auto* ai = ctx.find<AIWorld>()) return *ai;
        return ctx.emplace<AIWorld>(&scene);
    }
    void ResetAI(Scene& scene)
    {
        if (auto* ai = scene.GetRegistry().ctx().find<AIWorld>()) ai->Clear();
        scene.GetRegistry().ctx().erase<AIWorld>();
    }
    void BeginAIFrame(Scene& scene)
    {
        if (auto* ai = scene.GetRegistry().ctx().find<AIWorld>()) ai->Prune();
    }
    void TickAI(Scene& scene, float delta)
    {
        if (auto* ai = scene.GetRegistry().ctx().find<AIWorld>()) ai->Tick(delta);
    }
    void AIWorld::Check(Entity e) const
    {
        if (!e || e.GetScene() != m_Scene) throw std::runtime_error("AI target must be live and in this scene");
    }
    AIWorld::Record* AIWorld::Find(Entity e)
    {
        Check(e);
        auto i = m_Records.find(uint64_t(e.GetUUID()));
        return i != m_Records.end() && i->second.Handle == uint32_t(e) ? &i->second : nullptr;
    }
    const AIWorld::Record* AIWorld::Find(Entity e) const { return const_cast<AIWorld*>(this)->Find(e); }
    void AIWorld::Stop(Entity e, Record& r)
    {
        if (auto* world = World(*m_Scene)) world->CancelNavigationQuery(e.GetUUID());
        if (auto* nav = e.TryGetComponent<NavAgentComponent>()) nav->Stop(true);
        if (auto* cc = e.TryGetComponent<CharacterController3DComponent>()) cc->MoveVelocity = {};
        if (auto* b = e.TryGetComponent<Rigidbody2DComponent>(); b && b->RuntimeBody && m_Scene->GetPhysicsWorld2D())
        {
            auto v = PhysicsWorld2D::GetLinearVelocity((b2Body*)b->RuntimeBody);
            v.x = 0;
            if (r.Spec.Brain.TwoD) v.y = 0;
            PhysicsWorld2D::SetLinearVelocity((b2Body*)b->RuntimeBody, v);
        }
        if (auto* b = e.TryGetComponent<Rigidbody3DComponent>(); b && b->Type != Rigidbody3DComponent::BodyType::Static && b->RuntimeBodyID != ~0u && m_Scene->GetPhysicsWorld3D())
        {
            auto* world = m_Scene->GetPhysicsWorld3D();
            auto v = world->GetLinearVelocity(b->RuntimeBodyID);
            v.x = v.z = 0;
            world->SetLinearVelocity(b->RuntimeBodyID, v);
        }
        r.Issued = false;
    }
    void AIWorld::Release(Entity e, Record& r)
    {
        Stop(e, r);
        if (auto* nav = e.TryGetComponent<NavAgentComponent>())
        {
            nav->Speed = r.OriginalSpeed;
            nav->StoppingDistance = r.OriginalStop;
            nav->RepathInterval = r.OriginalRepath;
            nav->RecoverySettings = r.OriginalRecovery;
        }
    }
    void AIWorld::Clear()
    {
        for (auto& [id, r] : m_Records)
        {
            auto e = m_Scene->GetEntityByUUID(UUID(id));
            if (e && uint32_t(e) == r.Handle) Release(e, r);
        }
        m_Records.clear();
    }
    void AIWorld::Transition(Record& r, BrainMode mode)
    {
        if (r.State.Brain.Mode == mode) return;
        r.State.Brain.Mode = mode;
        Increment(r.State.Transitions);
        if (r.Events.size() == 32)
        {
            r.Events.pop_front();
            Increment(r.State.Dropped);
        }
        r.Events.push_back({ BrainName(mode), r.State.Target });
    }
    bool AIWorld::Configure(Entity owner, Entity e, const AISpec& spec, bool reset, std::string& error)
    {
        Check(owner);
        Check(e);
        error.clear();
        if (!m_Scene->IsRunning())
        {
            error = "AI configuration requires Play";
            return false;
        }
        if (!spec.Brain.Validate(error)) return false;
        if (!spec.Recovery.Enabled || !spec.Recovery.Validate() || spec.Tag.size() > 64 || !Finite(spec.Eye) || glm::length(spec.Eye) > 1000 || !Finite(spec.Facing) || glm::length(Plane(spec.Facing, spec.Brain.TwoD)) < 1e-5f)
        {
            error = "Invalid AI perception/recovery settings";
            return false;
        }
        if (spec.Target)
        {
            auto target = m_Scene->GetEntityByUUID(UUID(spec.Target));
            if (!target || target == e)
            {
                error = "AI explicit target missing or self";
                return false;
            }
        }
        if ((spec.Brain.TwoD && (e.HasComponent<CharacterController3DComponent>() || e.HasComponent<Rigidbody3DComponent>())) || (!spec.Brain.TwoD && e.HasComponent<Rigidbody2DComponent>()))
        {
            error = "AI movement dimension differs from body";
            return false;
        }
        if (auto* b = e.TryGetComponent<Rigidbody2DComponent>(); b && b->Type == Rigidbody2DComponent::BodyType::Static)
        {
            error = "AI cannot move a static body";
            return false;
        }
        if (auto* b = e.TryGetComponent<Rigidbody3DComponent>(); b && b->Type == Rigidbody3DComponent::BodyType::Static)
        {
            error = "AI cannot move a static body";
            return false;
        }
        auto grid = m_Scene->GetNavAgentGrid(e);
        if (grid && Scene::GetNavGridDimension(grid) != (spec.Brain.TwoD ? 2 : 3))
        {
            error = "AI movement dimension differs from grid";
            return false;
        }
        auto* nav = e.TryGetComponent<NavAgentComponent>();
        if (!nav)
        {
            error = "AI requires NavAgent";
            return false;
        }
        if (auto* patrol = e.TryGetComponent<PatrolPathComponent>(); patrol && (patrol->PlayOnStart || patrol->IsPatrolling()))
        {
            error = "AI and active PatrolPath cannot own the same agent";
            return false;
        }
        if (auto* spline = e.TryGetComponent<SplineFollowerComponent>(); spline && (spline->PlayOnStart || spline->Playing))
        {
            error = "AI and active SplineFollower cannot own the same actor";
            return false;
        }
        auto health = RuleWorld::For(*m_Scene).Health(e);
        if (!health)
        {
            error = "Configure AI health first";
            return false;
        }
        auto home = m_Scene->GetWorldPosition(e);
        std::vector<glm::vec3> points;
        if (!Finite(home) || !PrepareRoute(spec, home, points, error)) return false;
        auto old = m_Records.find(uint64_t(e.GetUUID()));
        if (old != m_Records.end() && old->second.Handle == uint32_t(e) && !reset)
        {
            error = "AI already configured";
            return false;
        }
        if (old == m_Records.end() && m_Records.size() >= 32)
        {
            error = "AI brain limit 32";
            return false;
        }
        if (old != m_Records.end() && old->second.Handle == uint32_t(e)) Release(e, old->second);
        Record r;
        r.Handle = uint32_t(e);
        r.Owner = owner.GetUUID();
        r.OwnerHandle = uint32_t(owner);
        r.ScriptOwner = owner.HasComponent<NativeScriptComponent>();
        r.Deaths = health->Deaths;
        r.Spec = spec;
        r.Route = std::move(points);
        r.OriginalSpeed = nav->Speed;
        r.OriginalStop = nav->StoppingDistance;
        r.OriginalRepath = nav->RepathInterval;
        r.OriginalRecovery = nav->RecoverySettings;
        r.State.Home = r.State.Goal = r.State.LastSeen = home;
        auto facing = spec.Facing;
        if (spec.Brain.TwoD) facing.z = 0;
        else facing.y = 0;
        r.State.Facing = glm::normalize(facing);
        r.State.Enabled = spec.Enabled;
        nav->Speed = spec.Brain.Speed;
        nav->StoppingDistance = std::min(spec.Brain.Arrive, .1f);
        nav->RepathInterval = 0;
        nav->RecoverySettings.Enabled = false;
        Stop(e, r);
        m_Records[uint64_t(e.GetUUID())] = std::move(r);
        return true;
    }
    bool AIWorld::Enable(Entity e, bool enabled)
    {
        auto* r = Find(e);
        if (!r) return false;
        r->Spec.Enabled = r->State.Enabled = enabled;
        Stop(e, *r);
        r->Recovery.Reset();
        r->State.Target = 0;
        r->State.Brain.Remember = 0;
        Transition(*r, enabled ? BrainMode::Return : BrainMode::Disabled);
        return true;
    }
    bool AIWorld::Reset(Entity e, bool rehome, std::string& error)
    {
        auto* r = Find(e);
        if (!r)
        {
            error = "AI not configured";
            return false;
        }
        auto home = rehome ? m_Scene->GetWorldPosition(e) : r->State.Home;
        std::vector<glm::vec3> points;
        if (!Finite(home) || !PrepareRoute(r->Spec, home, points, error)) return false;
        Stop(e, *r);
        r->Recovery.Reset();
        r->Route = std::move(points);
        r->State.Home = home;
        r->State.Target = 0;
        r->State.LastSeen = home;
        r->State.Brain.Remember = 0;
        r->State.Brain.Waiting = false;
        r->State.Brain.WaitLeft = 0;
        r->State.Brain.Waypoint = 0;
        r->State.Brain.Direction = 1;
        Transition(*r, BrainMode::Return);
        error.clear();
        return true;
    }
    bool AIWorld::Remove(Entity e)
    {
        auto* r = Find(e);
        if (!r) return false;
        Release(e, *r);
        m_Records.erase(uint64_t(e.GetUUID()));
        return true;
    }
    std::optional<AISnapshot> AIWorld::State(Entity e) const
    {
        auto* r = Find(e);
        return r ? std::optional(r->State) : std::nullopt;
    }
    std::optional<AIEvent> AIWorld::Poll(Entity e)
    {
        auto* r = Find(e);
        if (!r || r->Events.empty()) return {};
        auto event = std::move(r->Events.front());
        r->Events.pop_front();
        return event;
    }
    void AIWorld::Prune()
    {
        for (auto it = m_Records.begin(); it != m_Records.end();)
        {
            auto& r = it->second;
            auto e = m_Scene->GetEntityByUUID(UUID(it->first));
            auto owner = m_Scene->GetEntityByUUID(UUID(r.Owner));
            if (!e || uint32_t(e) != r.Handle || !owner || uint32_t(owner) != r.OwnerHandle || (r.ScriptOwner && !owner.HasComponent<NativeScriptComponent>()))
            {
                if (e && uint32_t(e) == r.Handle) Release(e, r);
                it = m_Records.erase(it);
            }
            else ++it;
        }
    }
    void AIWorld::Tick(float delta)
    {
        if (!std::isfinite(delta) || delta < 0) throw std::runtime_error("Invalid AI timestep");
        auto& rules = RuleWorld::For(*m_Scene);
        for (auto it = m_Records.begin(); it != m_Records.end();)
        {
            auto& r = it->second;
            auto e = m_Scene->GetEntityByUUID(UUID(it->first));
            auto owner = m_Scene->GetEntityByUUID(UUID(r.Owner));
            if (!e || uint32_t(e) != r.Handle || !owner || uint32_t(owner) != r.OwnerHandle || (r.ScriptOwner && !owner.HasComponent<NativeScriptComponent>()))
            {
                if (e && uint32_t(e) == r.Handle) Release(e, r);
                it = m_Records.erase(it);
                continue;
            }
            ++it;
            auto* nav = e.TryGetComponent<NavAgentComponent>();
            if (!nav)
            {
                Stop(e, r);
                r.State.Status = "NavAgent missing";
                Transition(r, BrainMode::Blocked);
                continue;
            }
            auto health = rules.Health(e);
            if (!health || health->Dead)
            {
                Stop(e, r);
                r.State.Target = 0;
                r.State.Brain.Remember = 0;
                r.State.Visible = false;
                r.State.Status = health ? "Dead" : "Health missing";
                Transition(r, health ? BrainMode::Dead : BrainMode::Blocked);
                continue;
            }
            if (r.Deaths != health->Deaths)
            {
                r.Deaths = health->Deaths;
                Stop(e, r);
                r.Recovery.Reset();
                r.State.Target = 0;
                r.State.Brain.Remember = 0;
                Transition(r, BrainMode::Return);
            }
            bool hostEnabled = true;
            if (auto* n = owner.TryGetComponent<NativeScriptComponent>())
            {
                hostEnabled = n->Enabled && (!r.ScriptOwner || n->Instance != nullptr);
                if (auto* graph = dynamic_cast<VisualGraphHost*>(n->Instance)) hostEnabled = hostEnabled && !graph->VM.Paused && graph->VM.Error().empty();
            }
            if (!r.Spec.Enabled || !hostEnabled || !m_Scene->IsActiveInHierarchy(e) || !m_Scene->IsActiveInHierarchy(owner) || delta == 0)
            {
                Stop(e, r);
                r.State.Visible = false;
                r.State.Status = !r.Spec.Enabled ? "Disabled" : !hostEnabled ? "Host suspended"
                                                            : delta == 0     ? "Paused"
                                                                             : "Inactive";
                if (!r.Spec.Enabled) Transition(r, BrainMode::Disabled);
                continue;
            }
            if (rules.QueryBudgetRemaining() < 2)
            {
                Stop(e, r);
                r.State.Visible = false;
                r.State.Status = "Query budget";
                continue;
            }
            const auto& s = r.Spec.Brain;
            auto position = m_Scene->GetWorldPosition(e);
            if (!Finite(position))
            {
                Stop(e, r);
                r.State.Status = "Invalid position";
                Transition(r, BrainMode::Blocked);
                continue;
            }
            Entity target = r.State.Target ? m_Scene->GetEntityByUUID(UUID(r.State.Target)) : Entity{};
            if (target && uint32_t(target) != r.TargetHandle) target = {};
            if (target)
            {
                auto h = rules.Health(target);
                if (!h || h->Dead || !m_Scene->IsActiveInHierarchy(target)) target = {};
            }
            const bool retained = bool(target);
            RuleQuery query;
            query.TwoD = s.TwoD;
            query.Origin = r.Spec.Eye;
            query.Facing = r.State.Facing;
            query.Mask = r.Spec.Mask;
            query.MinDot = std::cos(glm::radians(double(s.FieldOfView)) * .5);
            query.Range = retained ? s.Lose : s.View;
            UseResult seen;
            if (r.State.Brain.Mode != BrainMode::Return && Distance(position, r.State.Home, s.TwoD) <= s.Leash)
            {
                Entity filter = target;
                if (!filter && r.Spec.Target) filter = m_Scene->GetEntityByUUID(UUID(r.Spec.Target));
                // An explicit target that was destroyed never falls back to a different actor.
                if (!r.Spec.Target || filter) seen = rules.Sense(e, query, r.Spec.Tag, filter, s.Near);
            }
            const bool wasVisible = r.State.Visible;
            r.State.Visible = seen.Success;
            if (seen.Success)
            {
                target = m_Scene->GetEntityByUUID(UUID(seen.Target));
                r.State.Target = seen.Target;
                r.TargetHandle = uint32_t(target);
                r.State.LastSeen = m_Scene->GetWorldPosition(target);
                if (!wasVisible) Increment(r.State.Sightings);
            }
            else if (wasVisible) Increment(r.State.Losses);
            if (!target)
            {
                r.State.Target = 0;
                r.TargetHandle = ~0u;
            }
            r.RepathLeft = std::max(0.f, r.RepathLeft - delta);
            const auto oldMode = r.State.Brain.Mode;
            glm::vec3 goal = r.State.Home;
            if (target) goal = r.State.LastSeen;
            else if (!r.Route.empty()) goal = r.Route[std::min(size_t(r.State.Brain.Waypoint), r.Route.size() - 1)];
            auto* world = World(*m_Scene);
            bool pending = world && nav->WorldNavigation && world->HasNavigationQuery(e.GetUUID());
            bool portal = nav->PendingTraversalLink != 0;
            bool exhausted = r.Recovery.IsExhausted();
            BrainInput input{ delta, Distance(position, r.State.Home, s.TwoD), target ? Distance(position, m_Scene->GetWorldPosition(target), s.TwoD) : 0, target ? Distance(m_Scene->GetWorldPosition(target), r.State.Home, s.TwoD) : 0, Distance(position, goal, s.TwoD), uint32_t(r.Route.size()), true, true, bool(target), seen.Success, exhausted };
            auto memory = r.State.Brain;
            BrainDecision decision;
            std::string error;
            if (!StepBrain(s, memory, input, decision, error))
            {
                Stop(e, r);
                r.State.Status = error;
                Transition(r, BrainMode::Blocked);
                continue;
            }
            r.State.Brain = memory;
            r.State.Brain.Mode = oldMode;
            Transition(r, decision.Mode);
            if (decision.Mode == BrainMode::Return)
            {
                r.State.Target = 0;
                target = {};
                goal = r.State.Home;
            }
            else if (decision.Mode == BrainMode::Patrol && !r.Route.empty()) goal = r.Route[r.State.Brain.Waypoint];
            else if (target) goal = r.State.LastSeen;
            if (oldMode != decision.Mode)
            {
                r.Recovery.Reset();
                r.Issued = false;
            }
            if (decision.Move)
            {
                auto direction = goal - position;
                if (s.TwoD) direction.z = 0;
                else direction.y = 0;
                if (glm::length(direction) > 1e-5f) r.State.Facing = glm::normalize(direction);
                NavRecovery::Input observe{ Plane(position, s.TwoD), delta, true, nav->Speed > 0, pending, portal, Distance(position, goal, s.TwoD) <= s.Arrive };
                const auto action = r.Recovery.Observe(r.Spec.Recovery, observe);
                if (action == NavRecovery::Action::Stop)
                {
                    Stop(e, r);
                    r.State.Status = "Recovery exhausted";
                    Transition(r, BrainMode::Blocked);
                }
                else if (r.Recovery.GetPhase() == NavRecovery::Phase::Cooldown)
                {
                    Stop(e, r);
                    r.State.Status = "Recovery cooldown";
                }
                else if (!pending && !portal && (!r.Issued || action == NavRecovery::Action::Retry || (r.RepathLeft == 0 && Distance(goal, r.State.Goal, s.TwoD) > .25f)))
                {
                    m_Scene->SetNavDestination(e, goal);
                    r.Issued = true;
                    r.RepathLeft = s.Repath;
                    r.State.Goal = goal;
                    Increment(r.State.PathRequests);
                    r.State.Status = nav->PathStatus == NavPathStatus::Failed ? "No path" : "Moving";
                }
                else r.State.Status = portal ? "Traversal pending" : pending ? "Waiting path"
                                                                             : "Moving";
            }
            else
            {
                Stop(e, r);
                r.State.Goal = goal;
                r.State.Status = BrainName(decision.Mode);
                if (decision.Attack && target)
                {
                    query.Range = s.AttackExit;
                    query.MinDot = -1;
                    auto hit = rules.Strike(e, target, query, s.Damage, s.AttackCooldown, false);
                    if (hit.Damage.Applied > 0) Increment(r.State.Attacks);
                    if (hit.Damage.Killed) Increment(r.State.Kills);
                }
            }
            r.State.Retries = r.Recovery.GetRetries();
        }
    }
}
