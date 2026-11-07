// Formations in the scene: the Formation component's slots round its leader (AI/Formation.h) and
// the members walking to them - with a Nav Agent on the nav grid, without one straight there.
#include "Zamin/Scene/Scene.h"

#include "Zamin/AI/Formation.h"
#include "Zamin/AI/NavGrid.h"
#include "Zamin/Core/Profiler.h"
#include "Zamin/Math/MathUtils.h"
#include "Zamin/Physics/PhysicsWorld2D.h"
#include "Zamin/Physics/PhysicsWorld3D.h"
#include "Zamin/Scene/Components.h"
#include "Zamin/Scene/Entity.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace Zamin {

    namespace {

        // How fast the formation's heading follows the leader's way (degrees / s): a turning leader
        // swings its slots round smoothly.
        constexpr float HeadingTurnSpeed = 300.0f;
        // A heading turned this far since the slots were handed out: they are handed out again
        // (members on the left would otherwise cross over to the right).
        constexpr float ReassignAngle = 75.0f;
        // How fast a member in its slot turns to face the formation's way (degrees / s), when it has
        // no Nav Agent to say.
        constexpr float FaceTurnSpeed = 360.0f;
        // The leader counts as moving above this speed (units / s).
        constexpr float MovingSpeed = 0.15f;
        // Members without a Nav Agent close on their slot at this rate (1 / s: the gap shrinks by this
        // part of itself each second) on top of moving with the leader.
        constexpr float FollowGain = 4.0f;

        glm::vec2 ToPlane(const glm::vec3& p, int dimension) { return dimension == 3 ? glm::vec2(p.x, p.z) : glm::vec2(p.x, p.y); }

        glm::vec3 FromPlane(const glm::vec2& q, float height, int dimension)
        {
            return dimension == 3 ? glm::vec3(q.x, height, q.y) : glm::vec3(q.x, q.y, height);
        }

        float PlanarDistance(const glm::vec3& a, const glm::vec3& b, int dimension) { return glm::length(ToPlane(a, dimension) - ToPlane(b, dimension)); }

        // The leader's right on the plane, from its forward (3D: XZ seen from above, -Z ahead; 2D: XY).
        glm::vec2 RightOf(const glm::vec2& forward, int dimension)
        {
            return dimension == 3 ? glm::vec2(-forward.y, forward.x) : glm::vec2(forward.y, -forward.x);
        }

        // The way an entity faces on the plane: 3D its -Z, 2D its +X.
        glm::vec2 FacingOf(Scene& scene, Entity entity, int dimension)
        {
            const glm::mat4 world = scene.GetWorldTransform(entity);
            glm::vec2 facing = dimension == 3 ? glm::vec2(-world[2].x, -world[2].z) : glm::vec2(world[0].x, world[0].y);
            const float length = glm::length(facing);
            if (!(length > 1e-5f))
                return dimension == 3 ? glm::vec2(0.0f, -1.0f) : glm::vec2(1.0f, 0.0f);
            return facing / length;
        }

        // `from` turned towards `to` by at most maxAngle radians (both unit vectors).
        glm::vec2 TurnTowards(const glm::vec2& from, const glm::vec2& to, float maxAngle)
        {
            const float a = std::atan2(from.y, from.x), b = std::atan2(to.y, to.x);
            const float turned = Math::MoveTowardsAngle(a, b, maxAngle);
            return { std::cos(turned), std::sin(turned) };
        }

        uint64_t Signature(const std::vector<Entity>& members)
        {
            uint64_t hash = 1469598103934665603ull;
            for (Entity m : members)
            {
                hash ^= (uint64_t)m.GetUUID();
                hash *= 1099511628211ull;
            }
            return hash ^ (uint64_t)members.size();
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

        // Turns the entity to face `direction` (on the plane) at `degreesPerSecond`.
        void TurnToFace(Entity entity, const glm::vec2& direction, int dimension, float degreesPerSecond, float dt)
        {
            if (glm::dot(direction, direction) < 1e-8f || !(degreesPerSecond > 0.0f))
                return;
            auto& transform = entity.GetComponent<TransformComponent>();
            const float maxTurn = glm::radians(degreesPerSecond) * dt;
            if (dimension == 3)
                transform.Rotation.y = Math::MoveTowardsAngle(transform.Rotation.y, std::atan2(-direction.x, -direction.y), maxTurn);
            else
                transform.Rotation.z = Math::MoveTowardsAngle(transform.Rotation.z, std::atan2(direction.y, direction.x), maxTurn);
        }

        // The grid the formation keeps its slots on: the leader's Nav Agent's, else the one under it.
        const NavGrid* FormationGrid(Scene& scene, Entity leader, int dimension, const glm::vec3& position)
        {
            Entity grid = leader.HasComponent<NavAgentComponent>() ? scene.GetNavAgentGrid(leader) : scene.FindNavGridEntity(position, dimension);
            return grid ? scene.GetNavGrid(grid, dimension) : nullptr;
        }

        int FormationDimension(Scene& scene, Entity leader)
        {
            int dimension = leader.HasComponent<NavAgentComponent>() ? Scene::GetNavGridDimension(scene.GetNavAgentGrid(leader)) : 0;
            return dimension ? dimension : scene.GetSightDimension(leader);
        }

        // The slots round the leader for `count` members facing `heading` (world positions).
        void ComputeSlots(Scene& scene, Entity leader, const FormationComponent& c, int count, int dimension, const glm::vec2& heading,
                          std::vector<glm::vec3>& out)
        {
            out.clear();
            std::vector<glm::vec2> offsets;
            Formation::ComputeOffsets({ c.Shape, c.Spacing, c.WedgeAngle }, count, offsets);
            const glm::vec3 position = scene.GetWorldPosition(leader);
            const glm::vec2 p = ToPlane(position, dimension);
            // A ring does not turn with the leader (its members would run round it at every turn).
            glm::vec2 forward = heading;
            if (c.Shape == FormationShape::Circle || glm::dot(forward, forward) < 1e-8f)
                forward = dimension == 3 ? glm::vec2(0.0f, -1.0f) : glm::vec2(0.0f, 1.0f);
            const glm::vec2 right = RightOf(forward, dimension);
            const NavGrid* grid = c.KeepOnGrid ? FormationGrid(scene, leader, dimension, position) : nullptr;
            const bool leaderOnGrid = grid && grid->IsWalkable(grid->WorldToCell(p));
            out.reserve(offsets.size());
            for (const glm::vec2& offset : offsets)
            {
                glm::vec2 s = Formation::ToWorld(p, forward, right, offset);
                if (grid)
                {
                    // Behind a wall from the leader: as far out towards it as the leader can see.
                    if (leaderOnGrid && !grid->HasLineOfSight(p, s))
                    {
                        float lo = 0.0f, hi = 1.0f;
                        for (int i = 0; i < 8; i++)
                        {
                            const float mid = (lo + hi) * 0.5f;
                            if (grid->HasLineOfSight(p, p + (s - p) * mid))
                                lo = mid;
                            else
                                hi = mid;
                        }
                        s = p + (s - p) * lo;
                    }
                    // In a blocked cell: the nearest walkable one.
                    glm::ivec2 cell = grid->WorldToCell(s), free;
                    if (!grid->IsWalkable(cell) && grid->FindNearestWalkable(cell, 6, free))
                        s = grid->CellToWorld(free);
                }
                float height = dimension == 3 ? position.y : position.z;
                if (dimension == 3 && grid && grid->HasHeights())
                    height = grid->SampleFloorHeight(s);
                out.push_back(FromPlane(s, height, dimension));
            }
        }

        // Members (indices into `members`) for the slots: the least walking in all, or the list's order.
        void HandOutSlots(Scene& scene, const std::vector<Entity>& members, const std::vector<glm::vec3>& slots, bool nearest, int dimension,
                          std::vector<int>& slotToMember)
        {
            if (!nearest)
            {
                slotToMember.resize(slots.size());
                for (size_t i = 0; i < slots.size(); i++)
                    slotToMember[i] = i < members.size() ? (int)i : -1;
                return;
            }
            std::vector<glm::vec2> from, to;
            from.reserve(members.size());
            to.reserve(slots.size());
            for (Entity m : members)
                from.push_back(ToPlane(scene.GetWorldPosition(m), dimension));
            for (const glm::vec3& s : slots)
                to.push_back(ToPlane(s, dimension));
            Formation::AssignSlots(from, to, slotToMember);
        }

    }

    // ------------------------------------------------------------------ queries
    std::vector<Entity> Scene::GetFormationMembers(Entity leader)
    {
        std::vector<Entity> members;
        const auto* c = leader && leader.IsValid() ? leader.TryGetComponent<FormationComponent>() : nullptr;
        if (!c)
            return members;
        std::unordered_set<uint64_t> seen;
        seen.insert((uint64_t)leader.GetUUID());
        auto add = [&](Entity e) {
            if (members.size() >= FormationComponent::MaxMembers || !e || !e.IsValid() || !IsActiveInHierarchy(e))
                return;
            if (seen.insert((uint64_t)e.GetUUID()).second)
                members.push_back(e);
        };
        for (const EntityRef& ref : c->Members)
            if (ref)
                add(GetEntityByUUID(ref.ID));
        if (!c->MemberTag.empty())
            for (Entity e : FindEntitiesWithTag(c->MemberTag))
                add(e);
        return members;
    }

    bool Scene::GetFormationSlots(Entity leader, std::vector<glm::vec3>& slots, std::vector<Entity>* members)
    {
        slots.clear();
        if (members)
            members->clear();
        auto* c = leader && leader.IsValid() ? leader.TryGetComponent<FormationComponent>() : nullptr;
        if (!c)
            return false;
        // While the game runs: what the last update handed out.
        if (c->Started && !c->SlotMembers.empty() && c->Slots.size() == c->SlotMembers.size())
        {
            slots = c->Slots;
            if (members)
                for (UUID id : c->SlotMembers)
                    members->push_back(GetEntityByUUID(id));
            return true;
        }
        const std::vector<Entity> list = GetFormationMembers(leader);
        if (list.empty())
            return false;
        const int dimension = FormationDimension(*this, leader);
        const glm::vec2 heading = glm::dot(c->Heading, c->Heading) > 1e-8f ? c->Heading : FacingOf(*this, leader, dimension);
        ComputeSlots(*this, leader, *c, (int)list.size(), dimension, heading, slots);
        if (members)
        {
            std::vector<int> slotToMember;
            HandOutSlots(*this, list, slots, c->AssignNearest, dimension, slotToMember);
            for (int m : slotToMember)
                members->push_back(m >= 0 ? list[(size_t)m] : Entity());
        }
        return true;
    }

    bool Scene::SetFormationShape(Entity leader, FormationShape shape)
    {
        auto* c = leader && leader.IsValid() ? leader.TryGetComponent<FormationComponent>() : nullptr;
        if (!c)
            return false;
        c->Shape = shape; // the next update hands the slots out again (the shape differs from the assigned one)
        return true;
    }

    bool Scene::AddFormationMember(Entity leader, Entity member)
    {
        auto* c = leader && leader.IsValid() ? leader.TryGetComponent<FormationComponent>() : nullptr;
        if (!c || !member || !member.IsValid() || member == leader || c->Members.size() >= FormationComponent::MaxMembers)
            return false;
        const uint64_t id = (uint64_t)member.GetUUID();
        if (std::any_of(c->Members.begin(), c->Members.end(), [&](const EntityRef& r) { return r.ID == id; }))
            return false;
        c->Members.push_back(EntityRef{ id });
        return true;
    }

    bool Scene::RemoveFormationMember(Entity leader, Entity member)
    {
        auto* c = leader && leader.IsValid() ? leader.TryGetComponent<FormationComponent>() : nullptr;
        if (!c || !member)
            return false;
        const uint64_t id = (uint64_t)member.GetUUID();
        const size_t before = c->Members.size();
        std::erase_if(c->Members, [&](const EntityRef& r) { return r.ID == id; });
        if (c->Members.size() == before)
            return false;
        if (auto* agent = member.IsValid() ? member.TryGetComponent<NavAgentComponent>() : nullptr)
            agent->Stop(); // no longer sent anywhere: it stays where it is
        return true;
    }

    Entity Scene::GetFormationLeader(Entity member)
    {
        if (!member || !member.IsValid())
            return {};
        for (auto e : m_Registry.view<FormationComponent>())
        {
            Entity leader{ e, this };
            if (leader == member)
                continue;
            for (Entity m : GetFormationMembers(leader))
                if (m == member)
                    return leader;
        }
        return {};
    }

    bool Scene::GetFormationSlot(Entity member, glm::vec3& slot)
    {
        Entity leader = GetFormationLeader(member);
        if (!leader)
            return false;
        std::vector<glm::vec3> slots;
        std::vector<Entity> members;
        if (!GetFormationSlots(leader, slots, &members))
            return false;
        for (size_t i = 0; i < slots.size() && i < members.size(); i++)
            if (members[i] == member)
            {
                slot = slots[i];
                return true;
            }
        return false;
    }

    // ------------------------------------------------------------------ moving the members
    void Scene::UpdateFormations(float dt)
    {
        auto view = m_Registry.view<FormationComponent>();
        if (view.empty() || !(dt >= 0.0f))
            return;
        ZM_PROFILE_SCOPE("Formatsiyalar");
        std::vector<glm::vec3> slots;
        std::vector<int> slotToMember;
        const float reassignCos = std::cos(glm::radians(ReassignAngle));

        for (auto e : view)
        {
            Entity leader{ e, this };
            if (!IsActiveInHierarchy(leader))
                continue;
            auto& c = m_Registry.get<FormationComponent>(e);
            const std::vector<Entity> members = GetFormationMembers(leader);
            const int dimension = FormationDimension(*this, leader);
            const glm::vec3 position = GetWorldPosition(leader);
            const glm::vec2 p = ToPlane(position, dimension);
            if (!c.Started)
            {
                // The first update: the leader's facing is where the formation faces.
                c.Started = true;
                c.LastLeaderPosition = position;
                c.Heading = FacingOf(*this, leader, dimension);
                c.Timer = 0.0f;
            }

            // The heading follows the way the leader goes (its Nav Agent's velocity, else how it moved).
            glm::vec2 velocity(0.0f);
            if (const auto* agent = m_Registry.try_get<NavAgentComponent>(e))
                velocity = ToPlane(agent->Velocity, dimension);
            if (glm::dot(velocity, velocity) < MovingSpeed * MovingSpeed && dt > 0.0f)
                velocity = (p - ToPlane(c.LastLeaderPosition, dimension)) / dt;
            c.LastLeaderPosition = position;
            const float speed = glm::length(velocity);
            c.LeaderMoving = std::isfinite(speed) && speed > MovingSpeed;
            if (c.LeaderMoving)
            {
                const glm::vec2 way = velocity / speed;
                c.Heading = glm::dot(c.Heading, c.Heading) < 1e-8f ? way : TurnTowards(c.Heading, way, glm::radians(HeadingTurnSpeed) * dt);
            }

            const int count = (int)members.size();
            if (count == 0)
            {
                c.SlotMembers.clear();
                c.Slots.clear();
                c.Issued.clear();
                c.InPlace = 0;
                continue;
            }
            ComputeSlots(*this, leader, c, count, dimension, c.Heading, slots);

            // Who stands where: handed out again when the members, the shape or the heading changed.
            const uint64_t signature = Signature(members);
            bool reassign = c.SlotMembers.size() != (size_t)count || signature != c.MemberSignature || c.AssignedShape != c.Shape;
            if (!reassign && c.Shape != FormationShape::Circle && c.AssignNearest && glm::dot(c.AssignedHeading, c.AssignedHeading) > 1e-8f &&
                glm::dot(c.Heading, c.AssignedHeading) < reassignCos)
                reassign = true;
            if (reassign)
            {
                HandOutSlots(*this, members, slots, c.AssignNearest, dimension, slotToMember);
                c.SlotMembers.assign((size_t)count, UUID(0));
                for (int s = 0; s < count; s++)
                    if (const int m = slotToMember[(size_t)s]; m >= 0)
                        c.SlotMembers[(size_t)s] = members[(size_t)m].GetUUID();
                c.MemberSignature = signature;
                c.AssignedShape = c.Shape;
                c.AssignedHeading = c.Heading;
                c.Assignments++;
                c.Issued.assign((size_t)count, glm::vec3(std::numeric_limits<float>::max()));
                c.Timer = 0.0f;
            }
            c.Slots = slots;

            // Sending the members: Nav Agents every UpdateInterval (when their slot moved), the
            // others straight there every frame.
            c.Timer -= dt;
            const bool resend = c.Timer <= 0.0f;
            if (resend)
                c.Timer = std::max(0.05f, std::isfinite(c.UpdateInterval) ? c.UpdateInterval : 0.25f);
            const float tolerance = std::isfinite(c.SlotTolerance) ? std::max(0.01f, c.SlotTolerance) : 0.35f;
            c.InPlace = 0;
            for (int s = 0; s < count; s++)
            {
                Entity member = GetEntityByUUID(c.SlotMembers[(size_t)s]);
                if (!member)
                    continue;
                const glm::vec3 slot = slots[(size_t)s];
                const glm::vec3 at = GetWorldPosition(member);
                const float distance = PlanarDistance(at, slot, dimension);
                // Where it faces once in place: the formation's way (a ring: outwards).
                glm::vec2 face = c.Heading;
                if (c.Shape == FormationShape::Circle)
                {
                    const glm::vec2 out = ToPlane(slot, dimension) - p;
                    face = glm::dot(out, out) > 1e-8f ? glm::normalize(out) : c.Heading;
                }

                if (auto* agent = member.TryGetComponent<NavAgentComponent>())
                {
                    const float stop = std::isfinite(agent->StoppingDistance) ? std::max(0.0f, agent->StoppingDistance) : 0.0f;
                    const bool inPlace = distance <= tolerance + stop;
                    if (inPlace)
                        c.InPlace++;
                    // While the leader walks: where the slot will be by the next send (it keeps walking
                    // instead of braking at every send).
                    glm::vec3 aim = slot;
                    if (c.LeaderMoving)
                        aim += FromPlane(velocity * std::max(0.05f, c.UpdateInterval), 0.0f, dimension);
                    const bool moved = PlanarDistance(c.Issued[(size_t)s], aim, dimension) > tolerance * 0.5f;
                    if (moved && (resend || !agent->HasDestination) && !(inPlace && !c.LeaderMoving))
                    {
                        agent->SetDestination(aim);
                        c.Issued[(size_t)s] = aim;
                    }
                    // Standing in its slot: the formation's way (its agent turns it while it walks).
                    if (inPlace && c.FaceHeading && agent->UpdateRotation && glm::dot(agent->Velocity, agent->Velocity) < 0.0025f)
                        TurnToFace(member, face, dimension, agent->AngularSpeed > 0.0f ? agent->AngularSpeed : FaceTurnSpeed, dt);
                    continue;
                }

                // Without a Nav Agent: straight there - moving with the leader (its velocity) plus a
                // pull towards the slot, at most Speed (the leader's speed when that is more), so it
                // keeps its slot while the group walks instead of trailing behind it.
                const float moveSpeed = std::isfinite(c.Speed) ? std::max(0.0f, c.Speed) : 0.0f;
                glm::vec3 to = slot - at;
                if (dimension == 2)
                    to.z = 0.0f; // keeps its own depth
                else
                    to.y = 0.0f;
                auto* cc = member.TryGetComponent<CharacterController3DComponent>();
                auto* rb2 = member.TryGetComponent<Rigidbody2DComponent>();
                auto* rb3 = member.TryGetComponent<Rigidbody3DComponent>();
                b2Body* body2 = rb2 ? (b2Body*)rb2->RuntimeBody : nullptr;
                const bool dynamic3D = rb3 && rb3->RuntimeBodyID != 0xffffffff && rb3->Type == Rigidbody3DComponent::BodyType::Dynamic && m_PhysicsWorld3D;
                const bool inPlace = distance <= tolerance;
                if (inPlace)
                    c.InPlace++;
                if (moveSpeed <= 0.0f || (!c.LeaderMoving && (distance <= tolerance * 0.5f || distance < 1e-5f)))
                {
                    // At rest in its slot (or told not to move).
                    if (cc && cc->RuntimeID != 0xffffffff)
                        cc->Move(glm::vec3(0.0f));
                    else if (body2 && rb2->Type != Rigidbody2DComponent::BodyType::Static)
                        PhysicsWorld2D::SetLinearVelocity(body2, glm::vec2(0.0f));
                    else if (dynamic3D)
                    {
                        const glm::vec3 current = m_PhysicsWorld3D->GetLinearVelocity(rb3->RuntimeBodyID);
                        m_PhysicsWorld3D->SetLinearVelocity(rb3->RuntimeBodyID, { 0.0f, current.y, 0.0f });
                    }
                    if (c.FaceHeading)
                        TurnToFace(member, face, dimension, FaceTurnSpeed, dt);
                    continue;
                }
                const glm::vec3 carried = c.LeaderMoving ? FromPlane(velocity, 0.0f, dimension) : glm::vec3(0.0f);
                glm::vec3 velocity3 = carried + to * FollowGain;
                const float limit = std::max(moveSpeed, glm::length(carried));
                if (const float length = glm::length(velocity3); length > limit && length > 1e-6f)
                    velocity3 *= limit / length;
                if (cc && cc->RuntimeID != 0xffffffff)
                    cc->Move(glm::vec3(velocity3.x, 0.0f, velocity3.z));
                else if (body2 && rb2->Type == Rigidbody2DComponent::BodyType::Dynamic)
                    PhysicsWorld2D::SetLinearVelocity(body2, glm::vec2(velocity3));
                else if (dynamic3D)
                {
                    const glm::vec3 current = m_PhysicsWorld3D->GetLinearVelocity(rb3->RuntimeBodyID);
                    m_PhysicsWorld3D->SetLinearVelocity(rb3->RuntimeBodyID, { velocity3.x, current.y, velocity3.z });
                }
                else
                {
                    glm::vec3 next = at + velocity3 * dt;
                    if (dimension == 3)
                        next.y = slot.y; // on the floor under its slot
                    SetWorldPosition(*this, member, next);
                    if (body2 && rb2->Type == Rigidbody2DComponent::BodyType::Kinematic)
                        PhysicsWorld2D::SetLinearVelocity(body2, glm::vec2(velocity3));
                }
                // Walking to its slot: the way it goes; in it (walking along): the formation's way.
                if (c.FaceHeading)
                    TurnToFace(member, inPlace ? face : ToPlane(velocity3, dimension), dimension, FaceTurnSpeed, dt);
            }
        }
    }

}
