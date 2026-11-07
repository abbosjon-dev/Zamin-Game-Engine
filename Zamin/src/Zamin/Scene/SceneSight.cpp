// Sight for AI: Sight Sensors' view cones, the rays that decide whether something is in the way,
// and the targets they spot / lose while the game runs.
#include "Zamin/Scene/Scene.h"

#include "Zamin/AI/Sight.h"
#include "Zamin/Core/Profiler.h"
#include "Zamin/Math/Geometry.h"
#include "Zamin/Physics/PhysicsWorld2D.h"
#include "Zamin/Physics/PhysicsWorld3D.h"
#include "Zamin/Scene/Components.h"
#include "Zamin/Scene/Entity.h"
#include "Zamin/Scripting/EventBus.h"
#include "Zamin/Scripting/ScriptableEntity.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Zamin {

    namespace {

        const SightSensorComponent s_DefaultSight;

        bool IsSelfOrBelow(Scene& scene, Entity entity, Entity root)
        {
            for (int steps = 0; entity && steps < 256; steps++)
            {
                if (entity == root)
                    return true;
                entity = scene.GetParent(entity);
            }
            return false;
        }

        glm::vec3 SafeNormalize(const glm::vec3& v, const glm::vec3& fallback)
        {
            const float l = glm::length(v);
            return l > 1e-6f && std::isfinite(l) ? v / l : fallback;
        }

        void Expand(glm::vec3& min, glm::vec3& max, const glm::vec3& p)
        {
            min = glm::min(min, p);
            max = glm::max(max, p);
        }

        // The box around a target's body: its collider / character (what rays can hit), else what it
        // draws (with its children), else its position.
        void GetTargetBounds(Scene& scene, Entity target, int dimension, glm::vec3& min, glm::vec3& max)
        {
            const glm::mat4 world = scene.GetWorldTransform(target);
            const glm::vec3 position(world[3]);
            min = glm::vec3(std::numeric_limits<float>::max());
            max = glm::vec3(std::numeric_limits<float>::lowest());
            bool any = false;
            auto box = [&](const glm::vec3& center, const glm::vec3& half) {
                for (int i = 0; i < 8; i++)
                    Expand(min, max, glm::vec3(world * glm::vec4(center + glm::vec3(i & 1 ? half.x : -half.x, i & 2 ? half.y : -half.y,
                                                                                     i & 4 ? half.z : -half.z), 1.0f)));
                any = true;
            };
            const glm::vec3 scale(glm::length(glm::vec3(world[0])), glm::length(glm::vec3(world[1])), glm::length(glm::vec3(world[2])));
            const float maxScale = std::max({ scale.x, scale.y, scale.z, 1e-6f });
            if (dimension == 3)
            {
                if (const auto* cc = target.TryGetComponent<CharacterController3DComponent>())
                {
                    // Its origin is at the feet.
                    Expand(min, max, position + glm::vec3(-cc->Radius, 0.0f, -cc->Radius));
                    Expand(min, max, position + glm::vec3(cc->Radius, std::max(cc->Height, 0.01f), cc->Radius));
                    any = true;
                }
                if (const auto* b = target.TryGetComponent<BoxCollider3DComponent>(); b && !b->IsSensor)
                    box(b->Offset, b->HalfExtents);
                if (const auto* sc = target.TryGetComponent<SphereCollider3DComponent>(); sc && !sc->IsSensor)
                {
                    const glm::vec3 c(world * glm::vec4(sc->Offset, 1.0f));
                    Expand(min, max, c - sc->Radius * maxScale);
                    Expand(min, max, c + sc->Radius * maxScale);
                    any = true;
                }
                if (const auto* cap = target.TryGetComponent<CapsuleCollider3DComponent>(); cap && !cap->IsSensor)
                    box(cap->Offset, glm::vec3(cap->Radius, cap->HalfHeight + cap->Radius, cap->Radius));
            }
            else
            {
                if (const auto* b = target.TryGetComponent<BoxCollider2DComponent>(); b && !b->IsSensor)
                    box(glm::vec3(b->Offset, 0.0f), glm::vec3(b->Size, 0.0f));
                if (const auto* cc = target.TryGetComponent<CircleCollider2DComponent>(); cc && !cc->IsSensor)
                {
                    const glm::vec3 c(world * glm::vec4(cc->Offset, 0.0f, 1.0f));
                    const float r = cc->Radius * std::max(scale.x, scale.y);
                    Expand(min, max, c - glm::vec3(r, r, 0.0f));
                    Expand(min, max, c + glm::vec3(r, r, 0.0f));
                    any = true;
                }
            }
            if (any)
                return;
            AABB bounds;
            if (scene.ComputeBounds({ target }, bounds, true))
            {
                min = bounds.Min;
                max = bounds.Max;
                return;
            }
            min = max = position;
        }

    }

    int Scene::GetSightDimension(Entity viewer)
    {
        if (!viewer || !viewer.IsValid())
            return 3;
        if (viewer.HasComponent<Rigidbody2DComponent>() || viewer.HasComponent<BoxCollider2DComponent>() ||
            viewer.HasComponent<CircleCollider2DComponent>() || viewer.HasComponent<PolygonCollider2DComponent>())
            return 2;
        if (viewer.HasComponent<CharacterController3DComponent>() || viewer.HasComponent<Rigidbody3DComponent>())
            return 3;
        if (viewer.HasComponent<NavAgentComponent>())
            if (const int d = GetNavGridDimension(GetNavAgentGrid(viewer)))
                return d;
        // The scene's bodies: only 2D ones - a 2D game.
        const bool bodies3D = !m_Registry.view<Rigidbody3DComponent>().empty() || !m_Registry.view<CharacterController3DComponent>().empty();
        if (bodies3D)
            return 3;
        if (!m_Registry.view<Rigidbody2DComponent>().empty() || !m_Registry.view<TilemapComponent>().empty())
            return 2;
        // No physics at all: an orthographic camera looks at a 2D game.
        if (Entity camera = GetPrimaryCameraEntity())
            if (camera.GetComponent<CameraComponent>().Camera.GetProjectionType() == SceneCamera::ProjectionType::Orthographic)
                return 2;
        return 3;
    }

    bool Scene::GetSightCone(Entity viewer, SightCone& out)
    {
        if (!viewer || !viewer.IsValid())
            return false;
        const auto* sensor = viewer.TryGetComponent<SightSensorComponent>();
        const SightSensorComponent& s = sensor ? *sensor : s_DefaultSight;
        const glm::mat4 world = GetWorldTransform(viewer);
        const glm::vec3 position(world[3]);
        if (GetSightDimension(viewer) == 3)
        {
            out.Eye = position + glm::vec3(0.0f, s.EyeHeight, 0.0f);
            out.Forward = SafeNormalize(-glm::vec3(world[2]), { 0.0f, 0.0f, -1.0f });
        }
        else
        {
            out.Eye = position;
            out.Forward = SafeNormalize(glm::vec3(world[0].x, world[0].y, 0.0f), { 1.0f, 0.0f, 0.0f });
        }
        out.Distance = std::max(0.0f, s.ViewDistance);
        out.FieldOfView = std::clamp(s.FieldOfView, 0.0f, 360.0f);
        out.NearDistance = std::max(0.0f, s.NearDistance);
        return true;
    }

    bool Scene::CanSee(Entity viewer, Entity target, glm::vec3* seenPoint)
    {
        if (!viewer || !target || !viewer.IsValid() || !target.IsValid() || viewer == target || !IsActiveInHierarchy(target))
            return false;
        SightCone cone;
        if (!GetSightCone(viewer, cone))
            return false;
        const auto* sensor = viewer.TryGetComponent<SightSensorComponent>();
        const uint16_t mask = sensor ? sensor->BlockingMask : s_DefaultSight.BlockingMask;
        const int dimension = GetSightDimension(viewer);

        glm::vec3 min, max;
        GetTargetBounds(*this, target, dimension, min, max);
        // Quick rejection: the whole box out of reach.
        const glm::vec3 nearest = glm::clamp(cone.Eye, min, max);
        glm::vec3 offset = nearest - cone.Eye;
        if (dimension == 2)
            offset.z = 0.0f;
        if (glm::length(offset) > std::max(cone.Distance, cone.NearDistance))
            return false;

        std::vector<glm::vec3> points;
        Sight::GetTargetPoints(min, max, cone.Eye, dimension == 3 ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(0.0f, 0.0f, 1.0f), points);
        for (glm::vec3 point : points)
        {
            if (dimension == 2)
                point.z = cone.Eye.z; // the XY plane: depth (sorting) does not count
            if (!Sight::InView(cone, point))
                continue;
            const glm::vec3 to = point - cone.Eye;
            const float distance = glm::length(to);
            bool blocked = false;
            if (distance > 1e-4f)
            {
                const std::vector<RaycastHit> hits = dimension == 3
                    ? Raycast3DAll(cone.Eye, to / distance, distance, mask, false)
                    : Raycast2DAll(glm::vec2(cone.Eye), glm::vec2(to) / distance, distance, mask, false);
                for (const RaycastHit& hit : hits)
                {
                    Entity other = GetEntityByUUID(hit.EntityID);
                    if (!other || IsSelfOrBelow(*this, other, viewer) || IsSelfOrBelow(*this, other, target))
                        continue;
                    if (hit.Distance < distance - 1e-3f)
                    {
                        blocked = true;
                        break;
                    }
                }
            }
            if (!blocked)
            {
                if (seenPoint)
                    *seenPoint = point;
                return true;
            }
        }
        return false;
    }

    void Scene::UpdateSightSensors(float dt)
    {
        auto view = m_Registry.view<SightSensorComponent>();
        if (view.empty())
            return;
        ZM_PROFILE_SCOPE("Ko'rish sensorlari");
        dt = std::isfinite(dt) ? std::max(0.0f, dt) : 0.0f;
        struct SightEvent
        {
            UUID Sensor, Target;
            bool Spotted;
        };
        std::vector<SightEvent> events;
        std::vector<Entity> candidates;
        for (auto e : view)
        {
            Entity entity{ e, this };
            auto& s = m_Registry.get<SightSensorComponent>(e);
            if (!IsActiveInHierarchy(entity))
                continue;
            if (!s.CanSee)
                s.TimeSinceSeen += dt;
            s.CheckTimer -= dt;
            if (s.CheckTimer > 0.0f)
                continue;
            s.CheckTimer = std::max(0.0f, s.CheckInterval);
            s.Checks++;

            candidates.clear();
            if (s.Target)
            {
                if (Entity t = GetEntityByUUID(s.Target.ID))
                    candidates.push_back(t);
            }
            else if (!s.TargetTag.empty())
                candidates = FindEntitiesWithTag(s.TargetTag);

            // The nearest target it sees.
            const glm::vec3 position = GetWorldPosition(entity);
            Entity best;
            float bestDistance = std::numeric_limits<float>::max();
            int visible = 0;
            for (Entity candidate : candidates)
            {
                if (candidate == entity || IsSelfOrBelow(*this, candidate, entity) || !CanSee(entity, candidate))
                    continue;
                visible++;
                const float d = glm::length(GetWorldPosition(candidate) - position);
                if (d < bestDistance)
                {
                    bestDistance = d;
                    best = candidate;
                }
            }
            s.VisibleCount = visible;
            const bool saw = s.CanSee;
            const UUID before = s.SeenTarget;
            if (best)
            {
                s.CanSee = s.HasSeen = true;
                s.SeenTarget = best.GetUUID();
                s.LastSeenPosition = GetWorldPosition(best);
                s.TimeSinceSeen = 0.0f;
            }
            else
            {
                s.CanSee = false;
                s.SeenTarget = 0;
            }
            if (saw && (!best || before != s.SeenTarget))
                events.push_back({ entity.GetUUID(), before, false });
            if (best && (!saw || before != s.SeenTarget))
                events.push_back({ entity.GetUUID(), s.SeenTarget, true });
        }

        // To the sensors' scripts and the event bus, once the sensors are done (a script may destroy
        // entities).
        for (const SightEvent& event : events)
        {
            Entity sensor = GetEntityByUUID(event.Sensor);
            if (!sensor)
                continue;
            Entity target = GetEntityByUUID(event.Target);
            if (m_ScriptsRunning)
                if (auto* nsc = sensor.TryGetComponent<NativeScriptComponent>(); nsc && nsc->Instance && !nsc->Instance->m_Destroying)
                    event.Spotted ? nsc->Instance->OnTargetSpotted(target) : nsc->Instance->OnTargetLost(target);
            if (sensor.IsValid())
                m_EventBus->Emit(event.Spotted ? "TargetSpotted" : "TargetLost", target, sensor);
        }
    }

}
