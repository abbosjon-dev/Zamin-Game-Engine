#include "Zamin/Scene/Scene.h"
#include "Zamin/Scene/Entity.h"
#include "Zamin/Scene/SceneManager.h"
#include "Zamin/Scene/WorldStreaming.h"
#include "Zamin/Physics/PhysicsWorld2D.h"
#include "Zamin/Physics/PhysicsWorld3D.h"
#include "Zamin/Project/Project.h"
#include "Zamin/Math/Math.h"
#include "Zamin/Math/MathUtils.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace Zamin {
    namespace {
        glm::vec2 WorldNavPlane(glm::vec3 p, int dimension) { return dimension == 3 ? glm::vec2(p.x, p.z) : glm::vec2(p); }
        void Place(Scene& scene, Entity entity, glm::vec3 position)
        {
            auto& transform = entity.GetTransform();
            if (!scene.GetParent(entity))
            {
                transform.Translation = position;
                return;
            }
            auto world = scene.GetWorldTransform(entity);
            const auto scale = transform.Scale;
            world[3] = glm::vec4(position, 1);
            scene.SetWorldTransform(entity, world);
            transform.Scale = scale;
        }
        WorldStreaming* World(Scene& scene)
        {
            auto* manager = scene.GetSceneManager();
            auto* world = manager ? manager->GetWorldStreaming() : nullptr;
            return world && world->IsAnchor(&scene) ? world : nullptr;
        }
        void ClearRoute(NavAgentComponent& a)
        {
            a.Path.clear();
            a.PathLinks.clear();
            a.PathIndex = 0;
            a.PendingTraversalLink = 0;
            a.TraversalTicket = 0;
            a.Remaining = 0;
            a.Velocity = glm::vec3(0);
        }
        float WalkRemaining(const NavAgentComponent& a, glm::vec3 position, int dimension)
        {
            double length = 0;
            for (size_t i = size_t(std::max(0, a.PathIndex)); i < a.Path.size(); ++i)
            {
                if (i >= a.PathLinks.size() || !a.PathLinks[i]) length += glm::length(glm::dvec2(WorldNavPlane(a.Path[i], dimension)) - glm::dvec2(WorldNavPlane(position, dimension)));
                position = a.Path[i];
            }
            return float(std::min(length, double(std::numeric_limits<float>::max())));
        }
    }
    bool Scene::ComputeWorldNavAgentPath(Entity entity, NavAgentComponent& a)
    {
        auto* world = World(*this);
        const auto atlas = world ? world->GetNavigationAtlas() : nullptr;
        a.RepathRequested = false;
        a.RepathTimer = std::isfinite(a.RepathInterval) ? std::max(0.f, a.RepathInterval) : .5f;
        const auto oldVelocity = a.Velocity;
        ++a.PathRequests;
        ClearRoute(a);
        a.AtlasRevision = atlas ? atlas->GetRevision() : 0;
        a.Arrived = false;
        a.PathStatus = NavPathStatus::Failed;
        if (!atlas || !a.HasDestination || !std::isfinite(a.Destination.x) || !std::isfinite(a.Destination.y) || !std::isfinite(a.Destination.z)) return false;
        const int dim = int(world->GetPolicy().NavigationMode);
        const auto from = GetWorldPosition(entity);
        const float offset = entity.HasComponent<CharacterController3DComponent>() ? 0.f : a.BaseOffset;
        if (!std::isfinite(from.x) || !std::isfinite(from.y) || !std::isfinite(from.z) || !std::isfinite(offset) || std::abs(offset) > 1e6f ||
            (dim != 3 && (entity.HasComponent<CharacterController3DComponent>() || entity.HasComponent<Rigidbody3DComponent>())) ||
            (dim == 3 && entity.HasComponent<Rigidbody2DComponent>())) return false;
        NavPathOptions options;
        options.SnapRadius = 0;
        options.MaxExpanded = int(std::clamp(a.WorldMaxExpanded, 1u, 1048576u));
        if (dim == 3) options.StartHeight = from.y - offset;
        const auto route = atlas->FindRoute(WorldNavPlane(from, dim), WorldNavPlane(a.Destination, dim), options);
        a.PathStatus = route.Status;
        if (route.Points.empty()) return false;
        a.Path.push_back(from);
        a.PathLinks.push_back(0);
        // Preserve a start-cell centre when it is a portal entry. Ordinary replans start at the
        // actual position and never turn back towards the centre of the cell already occupied.
        const size_t begin = route.Points.size() > 1 && route.Points[1].IncomingLink ? 0 : 1;
        for (size_t i = begin; i < route.Points.size(); ++i)
        {
            const auto& p = route.Points[i];
            a.Path.push_back(dim == 3 ? glm::vec3(p.Position.x, p.Height + offset, p.Position.y) : glm::vec3(p.Position, from.z));
            a.PathLinks.push_back(p.IncomingLink);
        }
        if (route.Status == NavPathStatus::Complete)
        {
            const auto& last = route.Points.back();
            const auto goal = dim == 3 ? glm::vec3(a.Destination.x, last.Height + offset, a.Destination.z) : glm::vec3(glm::vec2(a.Destination), from.z);
            if (a.Path.size() > 1 && !a.PathLinks.back()) a.Path.back() = goal;
            else
            {
                a.Path.push_back(goal);
                a.PathLinks.push_back(0);
            }
        }
        a.Velocity = oldVelocity;
        a.PathIndex = 1;
        a.Remaining = WalkRemaining(a, from, dim);
        if (a.Path.size() < 2)
        {
            a.Arrived = true;
            ClearRoute(a);
        }
        return true;
    }
    void Scene::ApplyWorldNavVelocity(Entity entity, const glm::vec3& velocity, int dim, float dt)
    {
        if (dim == 3)
        {
            if (auto* cc = entity.TryGetComponent<CharacterController3DComponent>(); cc && cc->RuntimeID != 0xffffffff)
            {
                cc->Move(velocity);
                return;
            }
            if (auto* rb = entity.TryGetComponent<Rigidbody3DComponent>(); rb && rb->RuntimeBodyID != 0xffffffff && rb->Type == Rigidbody3DComponent::BodyType::Dynamic && m_PhysicsWorld3D)
            {
                const auto current = m_PhysicsWorld3D->GetLinearVelocity(rb->RuntimeBodyID);
                m_PhysicsWorld3D->SetLinearVelocity(rb->RuntimeBodyID, { velocity.x, current.y, velocity.z });
                return;
            }
        }
        else if (auto* rb = entity.TryGetComponent<Rigidbody2DComponent>(); rb && rb->RuntimeBody && rb->Type != Rigidbody2DComponent::BodyType::Static)
        {
            PhysicsWorld2D::SetLinearVelocity(static_cast<b2Body*>(rb->RuntimeBody), glm::vec2(velocity));
            return;
        }
        if (glm::dot(velocity, velocity) > 0)
        {
            const auto old = GetWorldPosition(entity);
            auto next = old + velocity * dt;
            auto& a = entity.GetComponent<NavAgentComponent>();
            if (dim == 3 && a.PathIndex > 0 && size_t(a.PathIndex) < a.Path.size())
            {
                const auto start = a.Path[size_t(a.PathIndex) - 1], end = a.Path[size_t(a.PathIndex)];
                const float length = glm::length(WorldNavPlane(end - start, dim));
                const float t = length > 1e-6f ? std::clamp(glm::length(WorldNavPlane(next - start, dim)) / length, 0.f, 1.f) : 1;
                next.y = glm::mix(start.y, end.y, t);
            }
            Place(*this, entity, next);
        }
    }
    uint64_t Scene::GetNavTraversalLink(Entity entity)
    {
        if (!entity || entity.GetScene() != this || !entity.IsValid() || !IsActiveInHierarchy(entity)) return 0;
        const auto* a = entity.TryGetComponent<NavAgentComponent>();
        auto* world = World(*this);
        const auto atlas = world ? world->GetNavigationAtlas() : nullptr;
        return a && a->WorldNavigation && a->HasDestination && a->PendingTraversalLink && atlas && atlas->GetRevision() == a->AtlasRevision ? a->PendingTraversalLink : 0;
    }
    bool Scene::CancelNavTraversal(Entity entity)
    {
        if (!entity || entity.GetScene() != this || !entity.IsValid()) return false;
        auto* a = entity.TryGetComponent<NavAgentComponent>();
        if (!a || !a->WorldNavigation || !a->PendingTraversalLink) return false;
        a->Stop(true);
        ApplyWorldNavVelocity(entity, glm::vec3(0), entity.HasComponent<CharacterController3DComponent>() ? 3 : (World(*this) ? int(World(*this)->GetPolicy().NavigationMode) : 2), 0);
        return true;
    }
    bool Scene::CompleteNavTraversal(Entity entity, uint64_t link, uint64_t revision, uint64_t ticket)
    {
        if (!link || GetNavTraversalLink(entity) != link) return false;
        auto& a = entity.GetComponent<NavAgentComponent>();
        if (!ticket || ticket != a.TraversalTicket || revision != a.AtlasRevision || a.PathIndex < 1 || size_t(a.PathIndex) >= a.Path.size() || a.PathLinks.size() != a.Path.size() || a.PathLinks[size_t(a.PathIndex)] != link) return false;
        const auto exit = a.Path[size_t(a.PathIndex)];
        const auto position = GetWorldPosition(entity);
        if (glm::length(position - a.TraversalEntry) > .15f) return false;
        auto* world = World(*this);
        const int dim = int(world->GetPolicy().NavigationMode);
        // Until rigid-body shape clearance is available, explicit teleport completion is limited
        // to transforms and CharacterControllers. Walking dynamic bodies still uses their velocity.
        if (auto* rb = entity.TryGetComponent<Rigidbody3DComponent>(); rb && rb->RuntimeBodyID != 0xffffffff) return false;
        if (auto* rb = entity.TryGetComponent<Rigidbody2DComponent>(); rb && rb->RuntimeBody) return false;
        if (auto* cc = entity.TryGetComponent<CharacterController3DComponent>())
        {
            if (dim != 3 || cc->RuntimeID == 0xffffffff || !m_PhysicsWorld3D) return false;
            const float radius = std::max(.01f, cc->Radius), height = std::max(cc->Height, radius * 2);
            const auto mask = Project::GetCollisionLayers2D().GetMaskBits(cc->Layer);
            const auto hits = OverlapCapsule3D(exit + glm::vec3(0, radius + .03f, 0), exit + glm::vec3(0, height - radius + .03f, 0), radius * .98f, mask);
            for (auto hit : hits)
                if (hit != entity) return false;
            m_PhysicsWorld3D->SetCharacterPosition(cc->RuntimeID, exit);
            cc->Move(glm::vec3(0));
            cc->VerticalVelocity = 0;
            cc->Velocity = glm::vec3(0);
            cc->Grounded = false;
        }
        Place(*this, entity, exit);
        a.PendingTraversalLink = a.TraversalTicket = 0;
        ++a.PathIndex;
        ++a.TraversalCompletions;
        a.Velocity = glm::vec3(0);
        a.Remaining = WalkRemaining(a, exit, dim);
        return true;
    }
    void Scene::UpdateWorldNavAgents(float dt)
    {
        const auto start = std::chrono::steady_clock::now();
        dt = std::clamp(dt, 0.f, .1f);
        auto* world = World(*this);
        const auto atlas = world ? world->GetNavigationAtlas() : nullptr;
        const uint64_t revision = atlas ? atlas->GetRevision() : 0;
        const int dim = world ? int(world->GetPolicy().NavigationMode) : 0;
        for (auto handle : m_Registry.view<NavAgentComponent>())
        {
            Entity entity(handle, this);
            auto& a = entity.GetComponent<NavAgentComponent>();
            if (!a.WorldNavigation || !IsActiveInHierarchy(entity)) continue;
            ++m_NavFrameStats.Agents;
            const int dimension = dim == 3 || entity.HasComponent<CharacterController3DComponent>() || entity.HasComponent<Rigidbody3DComponent>() ? 3 : 2;
            const auto position = GetWorldPosition(entity);
            if (a.IsFollowingTarget())
            {
                auto target = GetEntityByUUID(a.Target.ID);
                if (!target || target == entity || !IsActiveInHierarchy(target)) a.Stop(true);
                else
                {
                    const auto destination = GetWorldPosition(target);
                    if (!a.HasDestination || glm::length(WorldNavPlane(destination - a.Destination, dimension)) > .1f) a.RepathRequested = true;
                    a.Destination = destination;
                    a.HasDestination = true;
                }
            }
            if (a.AtlasRevision != revision)
            {
                if (a.AtlasRevision) ++a.BlockedRepaths;
                ClearRoute(a);
                a.RepathRequested = a.HasDestination;
            }
            a.RepathTimer -= dt;
            if (a.HasDestination && (a.RepathRequested || (!a.PendingTraversalLink && !a.Arrived && a.RepathInterval > 0 && a.RepathTimer <= 0)))
            {
                ComputeWorldNavAgentPath(entity, a);
                ++m_NavFrameStats.PathRequests;
            }
            auto stop = [&] { a.Velocity = glm::vec3(0); ApplyWorldNavVelocity(entity, glm::vec3(0), dimension, dt); };
            if (!atlas || (dim != 2 && dim != 3) || a.Arrived || !a.HasDestination || a.PathStatus == NavPathStatus::Failed || a.Path.size() != a.PathLinks.size())
            {
                stop();
                continue;
            }
            if (a.PendingTraversalLink)
            {
                stop();
                continue;
            }
            const float tolerance = .025f;
            while (a.PathIndex > 0 && size_t(a.PathIndex) < a.Path.size() && !a.PathLinks[size_t(a.PathIndex)] &&
                   glm::length(WorldNavPlane(a.Path[size_t(a.PathIndex)] - position, dim)) <= (size_t(a.PathIndex) + 1 == a.Path.size() && std::isfinite(a.StoppingDistance) ? std::clamp(a.StoppingDistance, tolerance, 1000.f) : tolerance)) ++a.PathIndex;
            if (a.PathIndex <= 0 || size_t(a.PathIndex) >= a.Path.size())
            {
                a.Arrived = true;
                a.Remaining = 0;
                a.Path.clear();
                a.PathLinks.clear();
                stop();
                continue;
            }
            if (a.PathLinks[size_t(a.PathIndex)])
            {
                a.PendingTraversalLink = a.PathLinks[size_t(a.PathIndex)];
                a.TraversalTicket = uint64_t(UUID());
                a.TraversalEntry = a.Path[size_t(a.PathIndex) - 1];
                a.TraversalExit = a.Path[size_t(a.PathIndex)];
                stop();
                continue;
            }
            const auto delta = WorldNavPlane(a.Path[size_t(a.PathIndex)] - position, dim);
            const float distance = glm::length(delta);
            a.Remaining = WalkRemaining(a, position, dim);
            float speed = std::isfinite(a.Speed) ? std::clamp(a.Speed, 0.f, 1000.f) : 0;
            const float acceleration = std::isfinite(a.Acceleration) ? std::clamp(a.Acceleration, 0.f, 10000.f) : 0;
            if (a.AutoBraking && acceleration > 0) speed = std::min(speed, std::sqrt(2 * acceleration * distance));
            const float oldSpeed = glm::length(WorldNavPlane(a.Velocity, dim));
            float moveSpeed = acceleration > 0 ? Math::MoveTowards(std::isfinite(oldSpeed) ? oldSpeed : 0.f, speed, acceleration * dt) : speed;
            if (dt > 0) moveSpeed = std::min(moveSpeed, distance / dt);
            const auto velocity = distance > 1e-6f ? delta * (moveSpeed / distance) : glm::vec2(0);
            a.Velocity = dim == 3 ? glm::vec3(velocity.x, 0, velocity.y) : glm::vec3(velocity, 0);
            ApplyWorldNavVelocity(entity, a.Velocity, dim, dt);
            if (a.UpdateRotation && a.AngularSpeed > 0 && glm::dot(velocity, velocity) > .0001f)
            {
                auto& transform = entity.GetTransform();
                float& rotation = dim == 3 ? transform.Rotation.y : transform.Rotation.z;
                rotation = Math::MoveTowardsAngle(rotation, dim == 3 ? std::atan2(-velocity.x, -velocity.y) : std::atan2(velocity.y, velocity.x), glm::radians(a.AngularSpeed) * dt);
            }
        }
        m_NavFrameStats.PathMilliseconds += float(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
    }
}
