// Patrol routes in the scene: the Patrol Path component's waypoints (its own points, a route
// entity's children or a spline's points) and the patrollers walking round them - with a Nav Agent
// on the nav grid, without one straight to each point.
#include "Zamin/Scene/Scene.h"

#include "Zamin/AI/NavPathfinder.h"
#include "Zamin/Core/Profiler.h"
#include "Zamin/Math/MathUtils.h"
#include "Zamin/Physics/PhysicsWorld2D.h"
#include "Zamin/Physics/PhysicsWorld3D.h"
#include "Zamin/Scene/Components.h"
#include "Zamin/Scene/Entity.h"
#include "Zamin/Scripting/EventBus.h"
#include "Zamin/Scripting/ScriptableEntity.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace Zamin {

    namespace {

        // A point the agent cannot get nearer to for this long is given up (a body in the way, a
        // point off the grid).
        constexpr float StuckTime = 5.0f;
        // A skipped point is waited at for at least this long (no busy loop through unreachable points).
        constexpr float SkipWait = 0.5f;
        // How fast a patroller without a Nav Agent turns to face the way it goes (degrees / s).
        constexpr float TurnSpeed = 540.0f;

        uint32_t NextRandom(uint32_t& state)
        {
            // xorshift32 (never 0).
            uint32_t x = state ? state : 0x9E3779B9u;
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            state = x;
            return x;
        }

        float Random01(uint32_t& state) { return (float)(NextRandom(state) >> 8) / 16777216.0f; }

        glm::vec2 ToPlane(const glm::vec3& p, int dimension) { return dimension == 3 ? glm::vec2(p.x, p.z) : glm::vec2(p.x, p.y); }

        float PlanarDistance(const glm::vec3& a, const glm::vec3& b, int dimension) { return glm::length(ToPlane(a, dimension) - ToPlane(b, dimension)); }

        int NearestPoint(const std::vector<glm::vec3>& points, const glm::vec3& position, int dimension)
        {
            int best = 0;
            float bestDistance = std::numeric_limits<float>::max();
            for (size_t i = 0; i < points.size(); i++)
            {
                const float d = PlanarDistance(points[i], position, dimension);
                if (d < bestDistance)
                {
                    bestDistance = d;
                    best = (int)i;
                }
            }
            return best;
        }

        // The point after Current (Once: false at the last one).
        bool AdvancePatrol(PatrolPathComponent& c, int count)
        {
            if (count <= 1)
            {
                c.Current = 0;
                if (c.Mode == PatrolPathComponent::PatrolMode::Once)
                    return false;
                c.Laps++;
                return true;
            }
            switch (c.Mode)
            {
                case PatrolPathComponent::PatrolMode::Loop:
                    c.Current = (c.Current + 1) % count;
                    if (c.Current == 0)
                        c.Laps++;
                    return true;
                case PatrolPathComponent::PatrolMode::PingPong:
                {
                    int next = c.Current + (c.Direction >= 0 ? 1 : -1);
                    if (next >= count)
                    {
                        c.Direction = -1;
                        next = count - 2;
                        c.Laps++;
                    }
                    else if (next < 0)
                    {
                        c.Direction = 1;
                        next = 1;
                        c.Laps++;
                    }
                    c.Current = std::clamp(next, 0, count - 1);
                    return true;
                }
                case PatrolPathComponent::PatrolMode::Random:
                {
                    // Any point but the one it is at.
                    int next = (int)(NextRandom(c.RandomState) % (uint32_t)(count - 1));
                    if (next >= c.Current)
                        next++;
                    c.Current = std::clamp(next, 0, count - 1);
                    return true;
                }
                case PatrolPathComponent::PatrolMode::Once:
                    if (c.Current + 1 >= count)
                        return false;
                    c.Current++;
                    return true;
            }
            return false;
        }

        void SetWorldPosition(Scene& scene, Entity entity, const glm::vec3& position)
        {
            auto& transform = entity.GetComponent<TransformComponent>();
            if (!scene.GetParent(entity))
            {
                transform.Translation = position;
                return;
            }
            glm::mat4 world = scene.GetWorldTransform(entity);
            world[3] = glm::vec4(position, 1.0f);
            const glm::vec3 keepScale = transform.Scale;
            scene.SetWorldTransform(entity, world);
            transform.Scale = keepScale;
        }

    }

    // ------------------------------------------------------------------ waypoints
    bool Scene::GetPatrolWaypoints(Entity entity, std::vector<glm::vec3>& points, std::vector<float>* waits)
    {
        points.clear();
        if (waits)
            waits->clear();
        const auto* c = entity && entity.IsValid() ? entity.TryGetComponent<PatrolPathComponent>() : nullptr;
        if (!c)
            return false;
        const float defaultWait = std::isfinite(c->WaitTime) ? std::max(0.0f, c->WaitTime) : 0.0f;
        auto push = [&](const glm::vec3& p, float wait) {
            if (points.size() >= PatrolPathComponent::MaxWaypoints || !std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
                return;
            points.push_back(p);
            if (waits)
                waits->push_back(wait >= 0.0f && std::isfinite(wait) ? wait : defaultWait);
        };

        switch (c->Source)
        {
            case PatrolPathComponent::PointSource::Points:
            {
                // RelativeToStart: from where it started (in the editor: where it is now).
                const glm::vec3 origin = !c->RelativeToStart ? glm::vec3(0.0f) : c->Started ? c->StartPosition : GetWorldPosition(entity);
                for (const PatrolPathComponent::Waypoint& w : c->Points)
                    push(origin + w.Position, w.Wait);
                break;
            }
            case PatrolPathComponent::PointSource::Children:
            {
                Entity route = c->Route ? GetEntityByUUID(c->Route.ID) : Entity();
                if (!route || route == entity)
                    break;
                // The active children, in the hierarchy's order.
                for (Entity child : GetChildren(route))
                    if (child != entity && IsActiveInHierarchy(child))
                        push(GetWorldPosition(child), -1.0f);
                break;
            }
            case PatrolPathComponent::PointSource::Spline:
            {
                Entity spline = c->Route ? GetEntityByUUID(c->Route.ID) : Entity();
                if (!c->Route)
                    for (Entity p = GetParent(entity); p && !spline; p = GetParent(p))
                        if (p.HasComponent<SplineComponent>())
                            spline = p;
                const auto* sc = spline ? spline.TryGetComponent<SplineComponent>() : nullptr;
                if (!sc || sc->Path.Points.empty())
                    break;
                const glm::mat4 world = GetWorldTransform(spline);
                const float spacing = std::isfinite(c->SplineSpacing) ? c->SplineSpacing : 0.0f;
                const float localLength = sc->GetLength();
                if (spacing <= 0.0f || !(localLength > 1e-5f) || sc->Path.Points.size() < 2)
                {
                    for (const SplinePoint& p : sc->Path.Points)
                        push(glm::vec3(world * glm::vec4(p.Position, 1.0f)), -1.0f);
                    break;
                }
                // Points `spacing` apart in the world along the curve: its world length from a dense
                // sampling, then evenly spaced distances along it (a closed one does not repeat its start).
                const int samples = std::clamp(sc->Path.GetSegmentCount() * 16, 16, 2048);
                float worldLength = 0.0f;
                glm::vec3 previous = glm::vec3(world * glm::vec4(sc->GetPositionAtDistance(0.0f), 1.0f));
                for (int i = 1; i <= samples; i++)
                {
                    const glm::vec3 p = glm::vec3(world * glm::vec4(sc->GetPositionAtDistance(localLength * (float)i / (float)samples), 1.0f));
                    worldLength += glm::length(p - previous);
                    previous = p;
                }
                const int count = std::clamp((int)std::round(worldLength / std::max(spacing, 1e-3f)), 1, (int)PatrolPathComponent::MaxWaypoints - 1);
                const bool closed = sc->Path.Closed;
                const int last = closed ? count - 1 : count;
                for (int i = 0; i <= last; i++)
                {
                    // A closed path's end is its start: not again.
                    const float d = localLength * (float)i / (float)count;
                    push(glm::vec3(world * glm::vec4(sc->GetPositionAtDistance(closed ? d : std::min(d, localLength)), 1.0f)), -1.0f);
                }
                break;
            }
        }
        return !points.empty();
    }

    bool Scene::PausePatrol(Entity entity, bool stop)
    {
        auto* c = entity && entity.IsValid() ? entity.TryGetComponent<PatrolPathComponent>() : nullptr;
        if (!c)
            return false;
        c->PausePatrol();
        if (stop)
            if (auto* agent = entity.TryGetComponent<NavAgentComponent>())
                agent->Stop();
        return true;
    }

    bool Scene::ResumePatrol(Entity entity, bool nearest)
    {
        auto* c = entity && entity.IsValid() ? entity.TryGetComponent<PatrolPathComponent>() : nullptr;
        if (!c)
            return false;
        c->ResumePatrol(nearest);
        return true;
    }

    bool Scene::SetPatrolWaypoint(Entity entity, int index)
    {
        auto* c = entity && entity.IsValid() ? entity.TryGetComponent<PatrolPathComponent>() : nullptr;
        std::vector<glm::vec3> points;
        if (!c || !GetPatrolWaypoints(entity, points) || index < 0 || index >= (int)points.size())
            return false;
        c->ResumePatrol(false);
        c->Current = index;
        c->Target = points[(size_t)index];
        return true;
    }

    // ------------------------------------------------------------------ the round
    void Scene::UpdatePatrolPaths(float dt)
    {
        auto view = m_Registry.view<PatrolPathComponent>();
        if (view.empty() || !(dt >= 0.0f))
            return;
        ZM_PROFILE_SCOPE("Patrul yo'llari");
        std::vector<glm::vec3> points;
        std::vector<float> waits;
        struct Reached
        {
            UUID Entity;
            int Index;
        };
        std::vector<Reached> reached;

        for (auto e : view)
        {
            Entity entity{ e, this };
            if (!IsActiveInHierarchy(entity))
                continue;
            auto& c = m_Registry.get<PatrolPathComponent>(e);
            if (!c.Started)
            {
                // The first update: where it starts from, whether it patrols (unless a script said).
                c.Started = true;
                c.StartPosition = GetWorldPosition(entity);
                if (!c.PatrolStateSet)
                    c.Patrolling = c.PlayOnStart;
                c.RandomState ^= (uint32_t)(uint64_t)entity.GetUUID() | 1u;
            }
            if (!c.Patrolling || c.Finished)
                continue;
            if (!GetPatrolWaypoints(entity, points, &waits))
                continue;
            const int count = (int)points.size();
            auto* agent = m_Registry.try_get<NavAgentComponent>(e);
            int dim = agent ? GetNavGridDimension(GetNavAgentGrid(entity)) : 0;
            if (dim == 0)
                dim = GetSightDimension(entity);
            const glm::vec3 position = GetWorldPosition(entity);

            // Which point: the first (or the nearest) when it starts, the nearest after
            // ResumePatrol(true), the last one when the list got shorter.
            if (c.Current < 0 || c.Current >= count || c.ResumeNearest)
            {
                const bool nearest = c.ResumeNearest || (c.Current < 0 && c.StartAtNearest);
                c.Current = nearest ? NearestPoint(points, position, dim) : c.Current < 0 ? 0 : count - 1;
                c.ResumeNearest = false;
                c.Waiting = false;
                c.DestinationIssued = false;
            }
            c.Target = points[(size_t)c.Current];

            if (c.Waiting)
            {
                c.WaitTimer -= dt;
                if (c.WaitTimer > 0.0f)
                    continue;
                c.Waiting = false;
                if (!AdvancePatrol(c, count))
                {
                    c.Finished = true;
                    continue;
                }
                c.Target = points[(size_t)c.Current];
                c.DestinationIssued = false;
            }

            const float arrive = std::isfinite(c.ArriveDistance) ? std::max(0.01f, c.ArriveDistance) : 0.25f;
            bool arrived = false, skip = false;
            if (agent)
            {
                // Sent there (again when the point moved: a route's children on a moving platform).
                if (!c.DestinationIssued || PlanarDistance(c.IssuedTarget, c.Target, dim) > arrive)
                {
                    agent->SetDestination(c.Target);
                    c.IssuedTarget = c.Target;
                    c.DestinationIssued = true;
                    c.StuckTimer = 0.0f;
                    c.BestDistance = PlanarDistance(position, c.Target, dim);
                }
                const float distance = PlanarDistance(position, c.Target, dim);
                const bool ours = agent->HasDestination && PlanarDistance(agent->Destination, c.Target, dim) <= 1e-3f;
                const float stop = std::isfinite(agent->StoppingDistance) ? std::max(0.0f, agent->StoppingDistance) : 0.0f;
                if (distance <= arrive + stop)
                    arrived = true;
                else if (ours && !agent->RepathRequested && agent->Arrived)
                    arrived = true; // as near as its path gets (a partial path)
                else if (ours && !agent->RepathRequested && agent->PathStatus == NavPathStatus::Failed)
                    skip = true; // no way there
                else
                {
                    if (distance < c.BestDistance - 0.05f)
                    {
                        c.BestDistance = distance;
                        c.StuckTimer = 0.0f;
                    }
                    else if ((c.StuckTimer += dt) > StuckTime)
                        skip = true;
                }
            }
            else
            {
                // Straight there at Speed (2D: on its own z).
                glm::vec3 to = c.Target - position;
                if (dim == 2)
                    to.z = 0.0f;
                const float distance = glm::length(to);
                const float speed = std::isfinite(c.Speed) ? std::max(0.0f, c.Speed) : 0.0f;
                const float step = speed * dt;
                glm::vec3 velocity(0.0f);
                if (distance > arrive && distance > 1e-6f)
                {
                    const glm::vec3 direction = to / distance;
                    velocity = direction * speed;
                    auto* cc = m_Registry.try_get<CharacterController3DComponent>(e);
                    auto* rb2 = m_Registry.try_get<Rigidbody2DComponent>(e);
                    auto* rb3 = m_Registry.try_get<Rigidbody3DComponent>(e);
                    b2Body* body2 = rb2 ? (b2Body*)rb2->RuntimeBody : nullptr;
                    if (cc && cc->RuntimeID != 0xffffffff)
                        cc->Move(glm::vec3(velocity.x, 0.0f, velocity.z));
                    else if (body2 && rb2->Type == Rigidbody2DComponent::BodyType::Dynamic)
                        PhysicsWorld2D::SetLinearVelocity(body2, glm::vec2(velocity));
                    else if (rb3 && rb3->RuntimeBodyID != 0xffffffff && rb3->Type == Rigidbody3DComponent::BodyType::Dynamic && m_PhysicsWorld3D)
                    {
                        const glm::vec3 current = m_PhysicsWorld3D->GetLinearVelocity(rb3->RuntimeBodyID);
                        m_PhysicsWorld3D->SetLinearVelocity(rb3->RuntimeBodyID, { velocity.x, current.y, velocity.z });
                    }
                    else
                    {
                        glm::vec3 next = distance <= step ? position + to : position + direction * step;
                        SetWorldPosition(*this, entity, next);
                        if (body2 && rb2->Type == Rigidbody2DComponent::BodyType::Kinematic)
                            PhysicsWorld2D::SetLinearVelocity(body2, glm::vec2(velocity));
                    }
                    if (c.FaceMovement)
                    {
                        auto& transform = entity.GetComponent<TransformComponent>();
                        const float maxTurn = glm::radians(TurnSpeed) * dt;
                        if (dim == 3 && direction.x * direction.x + direction.z * direction.z > 1e-6f)
                            transform.Rotation.y = Math::MoveTowardsAngle(transform.Rotation.y, std::atan2(-direction.x, -direction.z), maxTurn);
                        else if (dim == 2 && direction.x * direction.x + direction.y * direction.y > 1e-6f)
                            transform.Rotation.z = Math::MoveTowardsAngle(transform.Rotation.z, std::atan2(direction.y, direction.x), maxTurn);
                    }
                    arrived = distance - step <= arrive;
                }
                else
                    arrived = true;
                if (arrived)
                {
                    // Bodies moved by velocity stop at the point.
                    if (auto* rb2 = m_Registry.try_get<Rigidbody2DComponent>(e); rb2 && rb2->RuntimeBody)
                        PhysicsWorld2D::SetLinearVelocity((b2Body*)rb2->RuntimeBody, glm::vec2(0.0f));
                    if (auto* cc = m_Registry.try_get<CharacterController3DComponent>(e); cc && cc->RuntimeID != 0xffffffff)
                        cc->Move(glm::vec3(0.0f));
                }
            }

            if (!arrived && !skip)
                continue;
            if (arrived)
            {
                c.Arrivals++;
                reached.push_back({ entity.GetUUID(), c.Current });
            }
            else
                c.Skipped++;
            c.DestinationIssued = false;
            if (c.Mode == PatrolPathComponent::PatrolMode::Once && c.Current + 1 >= count)
            {
                // The end of a one-way round.
                c.Finished = true;
                c.Laps++;
                continue;
            }
            float wait = waits[(size_t)c.Current];
            if (c.WaitVariance > 0.0f && std::isfinite(c.WaitVariance))
                wait += (Random01(c.RandomState) * 2.0f - 1.0f) * c.WaitVariance;
            if (skip)
                wait = std::max(wait, SkipWait);
            c.Waiting = true;
            c.WaitTimer = std::max(0.0f, wait);
        }

        // To the patrollers' scripts and the event bus (a script may destroy entities).
        for (const Reached& r : reached)
        {
            Entity entity = GetEntityByUUID(r.Entity);
            if (!entity)
                continue;
            if (m_ScriptsRunning)
                if (auto* nsc = entity.TryGetComponent<NativeScriptComponent>(); nsc && nsc->Instance && !nsc->Instance->m_Destroying)
                    nsc->Instance->OnPatrolPointReached(r.Index);
            if (entity.IsValid())
                m_EventBus->Emit("PatrolPointReached", r.Index, entity);
        }
    }

}
