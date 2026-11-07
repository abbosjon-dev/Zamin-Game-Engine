// Splines in the scene: the Spline component's cached length table, world-space queries for
// scripts and the Spline Followers' motion.
#include "Zamin/Scene/Scene.h"

#include "Zamin/Core/Profiler.h"
#include "Zamin/Physics/PhysicsWorld2D.h"
#include "Zamin/Scene/Components.h"
#include "Zamin/Scene/Entity.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace Zamin {

    // ------------------------------------------------------------------ component
    Spline SplineComponent::DefaultPath()
    {
        Spline s;
        s.Points = { { { -3.0f, 0.0f, 0.0f } }, { { -1.0f, 1.0f, 0.0f } }, { { 1.0f, -1.0f, 0.0f } }, { { 3.0f, 0.0f, 0.0f } } };
        s.AutoTangents();
        return s;
    }

    const SplineLengthTable& SplineComponent::GetLengthTable() const
    {
        if (!m_TableBuilt || !(m_TableSource == Path))
        {
            m_Table.Build(Path, 32);
            m_TableSource = Path;
            m_TableBuilt = true;
        }
        return m_Table;
    }

    float SplineComponent::GetParameterAtDistance(float distance) const
    {
        const SplineLengthTable& table = GetLengthTable();
        const float length = table.GetLength();
        if (!(length > 0.0f) || !std::isfinite(distance))
            return 0.0f;
        if (Path.Closed)
        {
            distance = std::fmod(distance, length);
            if (distance < 0.0f)
                distance += length;
        }
        return table.GetParameter(distance);
    }

    // ------------------------------------------------------------------ queries
    glm::vec3 Scene::GetSplinePosition(Entity spline, float distance)
    {
        if (!spline || !spline.IsValid())
            return glm::vec3(0.0f);
        const glm::mat4 world = GetWorldTransform(spline);
        const auto* c = spline.TryGetComponent<SplineComponent>();
        if (!c)
            return glm::vec3(world[3]);
        return glm::vec3(world * glm::vec4(c->GetPositionAtDistance(distance), 1.0f));
    }

    glm::vec3 Scene::GetSplineTangent(Entity spline, float distance)
    {
        if (!spline || !spline.IsValid())
            return glm::vec3(0.0f, 0.0f, -1.0f);
        const glm::mat4 world = GetWorldTransform(spline);
        const auto* c = spline.TryGetComponent<SplineComponent>();
        glm::vec3 d = c ? glm::mat3(world) * c->GetTangentAtDistance(distance) : -glm::vec3(world[2]);
        return glm::dot(d, d) > 1e-12f ? glm::normalize(d) : glm::vec3(0.0f, 0.0f, -1.0f);
    }

    float Scene::GetSplineLength(Entity spline)
    {
        const auto* c = spline && spline.IsValid() ? spline.TryGetComponent<SplineComponent>() : nullptr;
        return c ? c->GetLength() : 0.0f;
    }

    float Scene::GetSplineClosestDistance(Entity spline, const glm::vec3& worldPoint)
    {
        const auto* c = spline && spline.IsValid() ? spline.TryGetComponent<SplineComponent>() : nullptr;
        if (!c)
            return 0.0f;
        const glm::vec3 local = glm::vec3(glm::inverse(GetWorldTransform(spline)) * glm::vec4(worldPoint, 1.0f));
        const SplineHit hit = c->Path.FindClosest(local);
        return c->GetLengthTable().GetDistance(hit.Parameter);
    }

    Entity Scene::GetSplineFollowerPath(Entity follower)
    {
        const auto* f = follower && follower.IsValid() ? follower.TryGetComponent<SplineFollowerComponent>() : nullptr;
        if (!f)
            return {};
        if (f->Path)
        {
            Entity path = GetEntityByUUID(f->Path.ID);
            return path && path.HasComponent<SplineComponent>() ? path : Entity();
        }
        for (Entity e = GetParent(follower); e; e = GetParent(e))
            if (e.HasComponent<SplineComponent>())
                return e;
        return {};
    }

    float Scene::GetSplineFollowerProgress(Entity follower)
    {
        const auto* f = follower && follower.IsValid() ? follower.TryGetComponent<SplineFollowerComponent>() : nullptr;
        const float length = f ? GetSplineLength(GetSplineFollowerPath(follower)) : 0.0f;
        return length > 0.0f ? std::clamp(f->Distance / length, 0.0f, 1.0f) : 0.0f;
    }

    // ------------------------------------------------------------------ followers
    void Scene::UpdateSplineFollowers(float dt)
    {
        auto view = m_Registry.view<SplineFollowerComponent>();
        if (view.empty())
            return;
        ZM_PROFILE_SCOPE("Spline follower'lar");
        for (auto e : view)
        {
            Entity entity{ e, this };
            if (!IsActiveInHierarchy(entity))
                continue;
            auto& f = m_Registry.get<SplineFollowerComponent>(e);
            Entity pathEntity = GetSplineFollowerPath(entity);
            // A path that moves with its follower would run away from it.
            if (!pathEntity || pathEntity == entity || IsAncestorOf(entity, pathEntity))
                continue;
            const auto& spline = pathEntity.GetComponent<SplineComponent>();
            const float length = spline.GetLength();
            if (!(length > 0.0f))
                continue;
            const glm::mat4 world = GetWorldTransform(pathEntity);

            bool teleport = false;
            if (!f.Initialized)
            {
                f.Initialized = true;
                f.Distance = std::clamp(f.StartPosition, 0.0f, 1.0f) * length;
                f.Direction = 1.0f;
                f.Finished = false;
                if (!f.PlayStateSet)
                    f.Playing = f.PlayOnStart;
                teleport = true;
            }
            else if (f.Playing && dt > 0.0f && std::isfinite(f.Speed) && f.Speed != 0.0f)
            {
                // Speed is in world units: divided by how much the path is stretched here.
                const glm::vec3 localTangent = spline.Path.EvaluateTangent(spline.GetParameterAtDistance(f.Distance));
                float stretch = glm::length(glm::mat3(world) * localTangent);
                if (!(stretch > 1e-6f) || !std::isfinite(stretch))
                    stretch = 1.0f;
                float d = f.Distance + f.Speed * f.Direction * dt / stretch;
                switch (f.Mode)
                {
                    case SplineFollowerComponent::FollowMode::Once:
                        if (d >= length || d <= 0.0f)
                        {
                            d = std::clamp(d, 0.0f, length);
                            f.Finished = true;
                            f.Playing = false;
                        }
                        break;
                    case SplineFollowerComponent::FollowMode::Loop:
                        if (d >= length || d < 0.0f)
                        {
                            d = std::fmod(d, length);
                            if (d < 0.0f)
                                d += length;
                            teleport = !spline.Path.Closed; // an open path starts over at its other end
                        }
                        break;
                    case SplineFollowerComponent::FollowMode::PingPong:
                        // Reflected at the ends (a step longer than the path folds more than once).
                        for (int i = 0; i < 8 && (d > length || d < 0.0f); i++)
                        {
                            d = d > length ? 2.0f * length - d : -d;
                            f.Direction = -f.Direction;
                        }
                        d = std::clamp(d, 0.0f, length);
                        break;
                }
                f.Distance = d;
            }

            // The pose on the path: the path's frame there (x right, y up, z along it).
            const float u = spline.GetParameterAtDistance(f.Distance);
            glm::vec3 position = glm::vec3(world * glm::vec4(spline.Path.Evaluate(u), 1.0f));
            glm::vec3 along = glm::mat3(world) * spline.Path.EvaluateTangent(u);
            along = glm::dot(along, along) > 1e-12f ? glm::normalize(along) : glm::vec3(0.0f, 0.0f, -1.0f);
            const bool flat = f.Rotation == SplineFollowerComponent::RotationMode::Path2D;
            glm::vec3 up;
            if (flat && along.x * along.x + along.y * along.y > 1e-8f)
                up = glm::normalize(glm::vec3(-along.y, along.x, 0.0f)); // the path's left normal in the XY plane
            else
            {
                const glm::vec3 worldUp = std::abs(along.y) < 0.999f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(0.0f, 0.0f, 1.0f);
                up = glm::normalize(worldUp - along * glm::dot(worldUp, along));
            }
            const glm::vec3 right = glm::cross(along, up);
            position += right * f.Offset.x + up * f.Offset.y + along * f.Offset.z;

            // Facing the way it travels.
            const glm::vec3 travel = f.Speed * f.Direction < 0.0f ? -along : along;
            auto& transform = entity.GetComponent<TransformComponent>();
            const glm::vec3 previous = GetWorldPosition(entity);
            glm::quat rotation;
            bool rotate = true;
            switch (f.Rotation)
            {
                case SplineFollowerComponent::RotationMode::Path3D:
                    rotation = glm::quatLookAt(travel, up);
                    break;
                case SplineFollowerComponent::RotationMode::Path2D:
                    rotation = glm::quat(glm::vec3(0.0f, 0.0f, std::atan2(travel.y, travel.x)));
                    break;
                default:
                    rotate = false;
                    break;
            }
            if (!GetParent(entity))
            {
                transform.Translation = position;
                if (rotate)
                    transform.Rotation = glm::eulerAngles(rotation);
            }
            else
            {
                // Under a parent: the world pose through it (the follower's own scale kept).
                const glm::vec3 keepScale = transform.Scale;
                glm::mat4 pose;
                if (rotate)
                    pose = glm::translate(glm::mat4(1.0f), position) * glm::mat4_cast(rotation) * glm::scale(glm::mat4(1.0f), keepScale);
                else
                {
                    pose = GetWorldTransform(entity);
                    pose[3] = glm::vec4(position, 1.0f);
                }
                SetWorldTransform(entity, pose);
                transform.Scale = keepScale;
            }

            // A kinematic 2D body also gets the path's velocity: it carries what stands on it (3D
            // kinematic bodies are moved by velocity from their transforms anyway).
            if (auto* rb = m_Registry.try_get<Rigidbody2DComponent>(e);
                rb && rb->RuntimeBody && rb->Type == Rigidbody2DComponent::BodyType::Kinematic && dt > 0.0f)
            {
                const glm::vec2 velocity = teleport ? glm::vec2(0.0f) : glm::vec2(position - previous) / dt;
                PhysicsWorld2D::SetLinearVelocity((b2Body*)rb->RuntimeBody, velocity);
            }
        }
    }

}
