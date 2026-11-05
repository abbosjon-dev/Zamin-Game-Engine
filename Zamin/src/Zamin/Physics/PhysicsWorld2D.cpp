#include "Zamin/Physics/PhysicsWorld2D.h"
#include "Zamin/Physics/Polygon2D.h"
#include "Zamin/Scene/TilesetProperties.h"

#include <box2d/b2_body.h>
#include <box2d/b2_circle_shape.h>
#include <box2d/b2_chain_shape.h>
#include <box2d/b2_collision.h>
#include <box2d/b2_contact.h>
#include <box2d/b2_distance_joint.h>
#include <box2d/b2_edge_shape.h>
#include <box2d/b2_fixture.h>
#include <box2d/b2_polygon_shape.h>
#include <box2d/b2_prismatic_joint.h>
#include <box2d/b2_revolute_joint.h>
#include <box2d/b2_weld_joint.h>
#include <box2d/b2_wheel_joint.h>
#include <box2d/b2_world.h>
#include <box2d/b2_world_callbacks.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace Zamin {

    namespace {

        b2Vec2 ToB2(const glm::vec2& v) { return { v.x, v.y }; }
        glm::vec2 FromB2(const b2Vec2& v) { return { v.x, v.y }; }
        float ToRadians(float degrees) { return degrees * (b2_pi / 180.0f); }
        float ToDegrees(float radians) { return radians * (180.0f / b2_pi); }

        // ------------------------------------------------------------------ contact listener
        // What the world remembers of a fixture: the part of the entity that made it, how its
        // friction / restitution mix, and its platform settings.
        struct FixtureInfo
        {
            int Tag = 0;
            PhysicsCombine FrictionCombine = PhysicsCombine::Default;
            PhysicsCombine RestitutionCombine = PhysicsCombine::Default;
            PhysicsPlatform2D Platform;

            // Contacts with it need PreSolve (mixing / platform rules).
            bool IsSpecial() const
            {
                return Platform.Enabled || FrictionCombine != PhysicsCombine::Default || RestitutionCombine != PhysicsCombine::Default;
            }
            bool IsOneWay() const { return Platform.Enabled && Platform.OneWay; }
        };

        // Collects the contact events of a physics step for the scripts, mixes friction / bounciness
        // of colliders with combine modes, and decides the contacts of the platforms (one-way
        // surfaces, side friction): Box2D re-enables every contact each step and asks PreSolve,
        // which switches it off while the body passes through. Such a contact is a collision for the
        // scripts only while it is on (Begin when it starts holding the body up, End when it lets
        // go), so jumping up through a platform reports nothing.
        class ContactListener2D : public b2ContactListener, public b2DestructionListener
        {
        public:
            void BeginContact(b2Contact* contact) override
            {
                if (IsOneWay(contact))
                {
                    m_OneWayContacts[contact] = {}; // reported once PreSolve lets it collide
                    return;
                }
                Push(contact, true);
            }
            void EndContact(b2Contact* contact) override
            {
                if (auto it = m_OneWayContacts.find(contact); it != m_OneWayContacts.end())
                {
                    if (it->second.Reported)
                        Push(contact, false);
                    m_OneWayContacts.erase(it);
                    return;
                }
                Push(contact, false);
            }
            void PreSolve(b2Contact* contact, const b2Manifold*) override
            {
                if (m_SpecialFixtures == 0)
                    return;
                b2Fixture* fa = contact->GetFixtureA();
                b2Fixture* fb = contact->GetFixtureB();
                const FixtureInfo* ia = Find(fa);
                const FixtureInfo* ib = Find(fb);
                if (!(ia && ia->IsSpecial()) && !(ib && ib->IsSpecial()))
                    return;
                if (auto it = m_OneWayContacts.find(contact); it != m_OneWayContacts.end())
                {
                    bool collide = ShouldCollide(contact, it->second);
                    contact->SetEnabled(collide);
                    if (collide != it->second.Reported)
                    {
                        it->second.Reported = collide;
                        Push(contact, collide);
                    }
                    if (!collide)
                        return;
                }
                const PhysicsCombine noMode = PhysicsCombine::Default;
                float friction = PhysicsMaterial::CombineFriction(fa->GetFriction(), ia ? ia->FrictionCombine : noMode,
                                                                  fb->GetFriction(), ib ? ib->FrictionCombine : noMode);
                float restitution = PhysicsMaterial::CombineRestitution(fa->GetRestitution(), ia ? ia->RestitutionCombine : noMode,
                                                                        fb->GetRestitution(), ib ? ib->RestitutionCombine : noMode);
                // Platform sides: no friction / bounce unless the platform wants them.
                for (int i = 0; i < 2; i++)
                {
                    const FixtureInfo* info = i == 0 ? ia : ib;
                    if (!info || !info->Platform.Enabled || (info->Platform.UseSideFriction && info->Platform.UseSideBounce))
                        continue;
                    b2Fixture* platform = i == 0 ? fa : fb;
                    float angle = NormalAngle(contact, platform, info->Platform);
                    if (std::abs(angle - 90.0f) <= std::clamp(info->Platform.SideArc, 0.0f, 180.0f) * 0.5f + 1e-3f)
                    {
                        if (!info->Platform.UseSideFriction) friction = 0.0f;
                        if (!info->Platform.UseSideBounce) restitution = 0.0f;
                    }
                }
                contact->SetFriction(friction);
                contact->SetRestitution(restitution);
            }

            // b2DestructionListener: a body's fixtures go with it.
            void SayGoodbye(b2Joint*) override {}
            void SayGoodbye(b2Fixture* fixture) override { Forget(fixture); }

            void Remember(const b2Fixture* fixture, const FixtureInfo& info)
            {
                Forget(fixture);
                Fixtures[fixture] = info;
                if (info.IsSpecial()) m_SpecialFixtures++;
                if (info.IsOneWay()) m_OneWayFixtures++;
            }
            void Forget(const b2Fixture* fixture)
            {
                auto it = Fixtures.find(fixture);
                if (it == Fixtures.end())
                    return;
                if (it->second.IsSpecial()) m_SpecialFixtures--;
                if (it->second.IsOneWay()) m_OneWayFixtures--;
                Fixtures.erase(it);
            }
            const FixtureInfo* Find(const b2Fixture* fixture) const
            {
                auto it = Fixtures.find(fixture);
                return it != Fixtures.end() ? &it->second : nullptr;
            }

            std::vector<ContactEvent> Events;
            std::unordered_map<const b2Fixture*, FixtureInfo> Fixtures;
            // Entities that fall through one-way surfaces for a while (DropThroughPlatforms): seconds left.
            std::unordered_map<uint64_t, float> DropThrough;
            float StepTime = 1.0f / 60.0f;

        private:
            struct OneWayContact
            {
                bool Reported = false; // a Begin went to the scripts
                int8_t Side = -1;      // arcs over 180: touched first from within the arc (1) or not (0)
            };

            void Push(b2Contact* contact, bool begin)
            {
                b2Fixture* fa = contact->GetFixtureA();
                b2Fixture* fb = contact->GetFixtureB();
                uint64_t a = (uint64_t)fa->GetUserData().pointer;
                uint64_t b = (uint64_t)fb->GetUserData().pointer;
                if (a && b)
                    Events.push_back({ a, b, begin, fa->IsSensor() || fb->IsSensor() });
            }

            bool IsOneWayFixture(const b2Fixture* fixture) const
            {
                const FixtureInfo* info = Find(fixture);
                return info && info->IsOneWay();
            }

            bool IsOneWay(b2Contact* contact) const
            {
                if (m_OneWayFixtures == 0)
                    return false;
                const b2Fixture* fa = contact->GetFixtureA();
                const b2Fixture* fb = contact->GetFixtureB();
                if (fa->IsSensor() || fb->IsSensor())
                    return false;
                return IsOneWayFixture(fa) || IsOneWayFixture(fb);
            }

            // The platform's up in the world.
            static b2Vec2 SurfaceUp(const b2Fixture* platform, const PhysicsPlatform2D& settings)
            {
                const b2Vec2 up = b2Mul(b2Rot(ToRadians(settings.RotationOffset)), b2Vec2(0.0f, 1.0f));
                return b2Mul(platform->GetBody()->GetTransform().q, up);
            }

            // Degrees between the platform's up and the contact normal pointing out of the platform.
            static float NormalAngle(b2Contact* contact, const b2Fixture* platform, const PhysicsPlatform2D& settings)
            {
                b2WorldManifold manifold;
                contact->GetWorldManifold(&manifold);
                b2Vec2 normal = contact->GetFixtureA() == platform ? manifold.normal : -manifold.normal; // Box2D: A to B
                const float c = std::clamp(b2Dot(normal, SurfaceUp(platform, settings)), -1.0f, 1.0f);
                return ToDegrees(std::acos(c));
            }

            // The extent of a fixture along a world direction.
            static void Project(const b2Fixture* fixture, const b2Vec2& dir, float& min, float& max)
            {
                const b2Transform& xf = fixture->GetBody()->GetTransform();
                const b2Shape* shape = fixture->GetShape();
                if (shape->GetType() == b2Shape::e_circle)
                {
                    const auto* circle = (const b2CircleShape*)shape;
                    float p = b2Dot(b2Mul(xf, circle->m_p), dir);
                    min = p - circle->m_radius;
                    max = p + circle->m_radius;
                    return;
                }
                if (shape->GetType() == b2Shape::e_polygon)
                {
                    const auto* polygon = (const b2PolygonShape*)shape;
                    min = std::numeric_limits<float>::max();
                    max = -std::numeric_limits<float>::max();
                    for (int32 i = 0; i < polygon->m_count; i++)
                    {
                        float p = b2Dot(b2Mul(xf, polygon->m_vertices[i]), dir);
                        min = std::min(min, p);
                        max = std::max(max, p);
                    }
                    return;
                }
                if (shape->GetType() == b2Shape::e_edge)
                {
                    const auto* edge = (const b2EdgeShape*)shape;
                    float p1 = b2Dot(b2Mul(xf, edge->m_vertex1), dir), p2 = b2Dot(b2Mul(xf, edge->m_vertex2), dir);
                    min = std::min(p1, p2);
                    max = std::max(p1, p2);
                    return;
                }
                const b2AABB& box = fixture->GetAABB(0);
                min = std::numeric_limits<float>::max();
                max = -std::numeric_limits<float>::max();
                for (b2Vec2 corner : { box.lowerBound, box.upperBound, b2Vec2(box.lowerBound.x, box.upperBound.y), b2Vec2(box.upperBound.x, box.lowerBound.y) })
                {
                    float p = b2Dot(corner, dir);
                    min = std::min(min, p);
                    max = std::max(max, p);
                }
            }

            bool ShouldCollide(b2Contact* contact, OneWayContact& state) const
            {
                b2Fixture* platform = contact->GetFixtureA();
                b2Fixture* other = contact->GetFixtureB();
                if (!IsOneWayFixture(platform))
                    std::swap(platform, other);
                const PhysicsPlatform2D& settings = Find(platform)->Platform;
                b2Body* platformBody = platform->GetBody();
                b2Body* body = other->GetBody();
                if (!DropThrough.empty())
                    if (auto it = DropThrough.find((uint64_t)body->GetUserData().pointer); it != DropThrough.end() && it->second > 0.0f)
                        return false;
                const b2Vec2 up = SurfaceUp(platform, settings);
                float platformMin, top, bottom, otherMax;
                Project(platform, up, platformMin, top);
                Project(other, up, bottom, otherMax);
                b2Vec2 relative = body->GetLinearVelocity() - platformBody->GetLinearVelocity();
                const bool onTop = OneWayPlatform::ShouldCollide(bottom, top, b2Dot(relative, up), StepTime);
                const float arc = std::clamp(settings.SurfaceArc, 0.0f, 360.0f);
                if (std::abs(arc - 180.0f) < 0.01f)
                    return onTop; // the classic one-way platform
                const float angle = NormalAngle(contact, platform, settings);
                if (arc < 180.0f)
                    return onTop && angle <= arc * 0.5f + 0.5f;
                // Wider: also held at the sides - decided when the body first touches it (once it is
                // part way in, the normal no longer says where it came from).
                if (onTop)
                    return true;
                if (state.Side < 0)
                    state.Side = angle <= arc * 0.5f ? 1 : 0;
                return state.Side == 1;
            }

            std::unordered_map<b2Contact*, OneWayContact> m_OneWayContacts; // touching one-way contacts
            size_t m_SpecialFixtures = 0, m_OneWayFixtures = 0;
        };

        b2BodyType ToBox2DBodyType(int motionType)
        {
            switch (motionType)
            {
                case 0: return b2_staticBody;
                case 1: return b2_dynamicBody;
                case 2: return b2_kinematicBody;
            }
            return b2_staticBody;
        }

        Joint2DType FromBox2DJointType(b2JointType type)
        {
            switch (type)
            {
                case e_revoluteJoint:  return Joint2DType::Revolute;
                case e_prismaticJoint: return Joint2DType::Prismatic;
                case e_weldJoint:      return Joint2DType::Weld;
                case e_wheelJoint:     return Joint2DType::Wheel;
                default:               return Joint2DType::Distance;
            }
        }

        // The distance joint's Box2D settings: a rigid rod (min = max = length), a rope (up to the
        // length) or a spring around the length; every value as Box2D stores it (clamped), so a
        // comparison with the joint's own values tells whether anything changed.
        struct DistanceSettings
        {
            float Length, MinLength, MaxLength, Stiffness = 0.0f, Damping = 0.0f;
        };
        DistanceSettings MakeDistanceSettings(const PhysicsJoint2DDesc& d, const b2Body* a, const b2Body* b)
        {
            DistanceSettings s;
            s.Length = std::max(d.Distance, b2_linearSlop);
            if (d.Frequency > 0.0f)
                b2LinearStiffness(s.Stiffness, s.Damping, d.Frequency, std::max(0.0f, d.DampingRatio), a, b);
            if (d.MaxDistanceOnly)
            {
                s.MinLength = b2_linearSlop;
                s.MaxLength = s.Length;
            }
            else if (s.Stiffness > 0.0f)
            {
                s.MinLength = b2_linearSlop;
                s.MaxLength = b2_maxFloat;
            }
            else
                s.MinLength = s.MaxLength = s.Length;
            s.MinLength = std::max(s.MinLength, b2_linearSlop);
            s.MaxLength = std::max(s.MaxLength, s.MinLength);
            return s;
        }

        void Wake(b2Joint* joint)
        {
            joint->GetBodyA()->SetAwake(true);
            joint->GetBodyB()->SetAwake(true);
        }

        // The axis of a prismatic / wheel joint: given in the joint body's (B) frame, Box2D wants it in A's.
        b2Vec2 AxisInConnectedFrame(const PhysicsJoint2DDesc& d, const b2Body* a, const b2Body* b)
        {
            b2Vec2 axis = ToB2(d.Axis);
            if (axis.Normalize() < b2_epsilon)
                axis.Set(1.0f, 0.0f);
            return a->GetLocalVector(b->GetWorldVector(axis));
        }

        // ------------------------------------------------------------------ effectors / queries
        // A fixture's shape as a convex polygon in the world (a circle: a polygon of the same area);
        // false for shapes without area (edges, chains).
        bool WorldPolygon(const b2Fixture* fixture, std::vector<glm::vec2>& out)
        {
            out.clear();
            const b2Transform& xf = fixture->GetBody()->GetTransform();
            const b2Shape* shape = fixture->GetShape();
            if (shape->GetType() == b2Shape::e_polygon)
            {
                const auto* polygon = (const b2PolygonShape*)shape;
                for (int32 i = 0; i < polygon->m_count; i++)
                    out.push_back(FromB2(b2Mul(xf, polygon->m_vertices[i])));
                return out.size() >= 3;
            }
            if (shape->GetType() == b2Shape::e_circle)
            {
                const auto* circle = (const b2CircleShape*)shape;
                out = Polygon2D::CirclePolygon(FromB2(b2Mul(xf, circle->m_p)), circle->m_radius, 24);
                return true;
            }
            return false;
        }

        // The rotational inertia about the centre of mass (Box2D's is about the body's origin).
        float CentralInertia(const b2Body* body)
        {
            const b2Vec2 c = body->GetLocalCenter();
            return std::max(0.0f, body->GetInertia() - body->GetMass() * b2Dot(c, c));
        }

        // The other side of a touching contact of `self`: its fixture when an effector may act on it
        // (a dynamic body's solid fixture on a layer in `mask`), else null. `mine`: self's fixture.
        b2Fixture* EffectorTarget(b2Contact* contact, const b2Body* self, uint16_t mask, b2Fixture*& mine)
        {
            if (!contact->IsTouching())
                return nullptr;
            mine = contact->GetFixtureA();
            b2Fixture* other = contact->GetFixtureB();
            if (mine->GetBody() != self)
                std::swap(mine, other);
            const b2Body* body = other->GetBody();
            if (body == self || body->GetType() != b2_dynamicBody || !body->IsEnabled() || other->IsSensor())
                return nullptr;
            if ((other->GetFilterData().categoryBits & mask) == 0)
                return nullptr;
            return other;
        }

        bool PassesQuery(b2Fixture* fixture, uint16_t mask, bool sensors)
        {
            if (fixture->IsSensor() && !sensors)
                return false;
            return (fixture->GetFilterData().categoryBits & mask) != 0 && fixture->GetUserData().pointer != 0;
        }

        // The entities of the fixtures whose shape overlaps `shape` at `xf`, each once, the nearest
        // body position to the shape's first.
        std::vector<UUID> OverlapShape(const b2World& world, const b2Shape& shape, const b2Transform& xf, uint16_t mask, bool sensors)
        {
            b2AABB box;
            shape.ComputeAABB(&box, xf, 0);
            struct Query : b2QueryCallback
            {
                std::vector<b2Fixture*> Found;
                bool ReportFixture(b2Fixture* fixture) override
                {
                    Found.push_back(fixture);
                    return true; // all of them
                }
            } query;
            world.QueryAABB(&query, box);
            // A chain reports a fixture once per child in the box.
            std::sort(query.Found.begin(), query.Found.end());
            query.Found.erase(std::unique(query.Found.begin(), query.Found.end()), query.Found.end());
            std::vector<std::pair<float, uint64_t>> hits;
            for (b2Fixture* fixture : query.Found)
            {
                if (!PassesQuery(fixture, mask, sensors))
                    continue;
                const b2Transform& bodyXf = fixture->GetBody()->GetTransform();
                bool overlap = false;
                for (int32 child = 0; child < fixture->GetShape()->GetChildCount() && !overlap; child++)
                    overlap = b2TestOverlap(box, fixture->GetAABB(child)) && b2TestOverlap(&shape, 0, fixture->GetShape(), child, xf, bodyXf);
                if (overlap)
                    hits.push_back({ b2DistanceSquared(bodyXf.p, xf.p), (uint64_t)fixture->GetUserData().pointer });
            }
            std::sort(hits.begin(), hits.end());
            std::vector<UUID> out;
            std::unordered_set<uint64_t> seen;
            for (const auto& [distance, id] : hits)
                if (seen.insert(id).second)
                    out.push_back(UUID(id));
            return out;
        }

    }

    // ------------------------------------------------------------------ world
    struct PhysicsWorld2D::Impl
    {
        ContactListener2D Listener; // outlives the world
        b2World World;
        b2Body* Ground = nullptr; // anchors joints without a connected body (created on demand)
        uint32_t RandomState = 0x9E3779B9u; // effector variations (xorshift: the same run every time)

        // A random number in [-1, 1].
        float RandomSigned()
        {
            RandomState ^= RandomState << 13;
            RandomState ^= RandomState >> 17;
            RandomState ^= RandomState << 5;
            return (float)(RandomState & 0xFFFFFF) / (float)0xFFFFFF * 2.0f - 1.0f;
        }

        explicit Impl(const glm::vec2& gravity)
            : World(ToB2(gravity))
        {
            World.SetContactListener(&Listener);
            World.SetDestructionListener(&Listener);
        }

        b2Body* GetGround()
        {
            if (!Ground)
            {
                b2BodyDef def;
                def.type = b2_staticBody;
                Ground = World.CreateBody(&def); // no fixtures, no entity
            }
            return Ground;
        }
    };

    PhysicsWorld2D::PhysicsWorld2D(const glm::vec2& gravity)
        : m_Impl(std::make_unique<Impl>(gravity))
    {
    }

    PhysicsWorld2D::~PhysicsWorld2D() = default;

    // ------------------------------------------------------------------ bodies
    b2Body* PhysicsWorld2D::CreateBody(const PhysicsBody2DDesc& desc)
    {
        b2BodyDef bodyDef;
        bodyDef.type = ToBox2DBodyType(desc.MotionType);
        bodyDef.position.Set(desc.Position.x, desc.Position.y);
        bodyDef.angle = desc.Angle;
        bodyDef.fixedRotation = desc.FixedRotation;
        bodyDef.gravityScale = desc.GravityScale;
        bodyDef.linearDamping = desc.LinearDamping;
        bodyDef.angularDamping = desc.AngularDamping;
        bodyDef.bullet = desc.Bullet;
        bodyDef.userData.pointer = (uintptr_t)(uint64_t)desc.EntityID;
        return m_Impl->World.CreateBody(&bodyDef);
    }

    b2Fixture* PhysicsWorld2D::AddShape(b2Body* body, const PhysicsShape2DDesc& desc)
    {
        if (!body)
            return nullptr;
        b2FixtureDef fixtureDef;
        fixtureDef.density = desc.Density;
        fixtureDef.friction = desc.Friction;
        fixtureDef.restitution = desc.Restitution;
        fixtureDef.restitutionThreshold = desc.RestitutionThreshold;
        fixtureDef.isSensor = desc.IsSensor;
        fixtureDef.filter.categoryBits = desc.CategoryBits;
        fixtureDef.filter.maskBits = desc.MaskBits;
        fixtureDef.userData.pointer = body->GetUserData().pointer;

        FixtureInfo info;
        info.Tag = desc.Tag;
        info.FrictionCombine = desc.FrictionCombine;
        info.RestitutionCombine = desc.RestitutionCombine;
        info.Platform = desc.Platform;
        if (desc.OneWay)
        {
            info.Platform = PhysicsPlatform2D();
            info.Platform.Enabled = true;
        }
        if (desc.IsSensor)
            info.Platform.Enabled = false;

        b2Fixture* first = nullptr;
        auto create = [&](const b2Shape& shape) {
            fixtureDef.shape = &shape;
            b2Fixture* fixture = body->CreateFixture(&fixtureDef);
            m_Impl->Listener.Remember(fixture, info);
            if (!first)
                first = fixture;
        };
        std::vector<b2Vec2> points;
        for (const glm::vec2& p : desc.Points)
            points.push_back(ToB2(p + desc.Offset));
        switch (desc.Type)
        {
            case PhysicsShape2DDesc::Kind::Box:
            {
                b2PolygonShape box;
                box.SetAsBox(std::max(desc.HalfExtents.x, 0.001f), std::max(desc.HalfExtents.y, 0.001f), ToB2(desc.Offset), 0.0f);
                create(box);
                break;
            }
            case PhysicsShape2DDesc::Kind::Circle:
            {
                b2CircleShape circle;
                circle.m_p = ToB2(desc.Offset);
                circle.m_radius = std::max(desc.Radius, 0.001f);
                create(circle);
                break;
            }
            case PhysicsShape2DDesc::Kind::Polygon:
            {
                std::vector<glm::vec2> local;
                for (const glm::vec2& p : desc.Points)
                    local.push_back(p + desc.Offset);
                std::vector<glm::vec2> piece = Polygon2D::MakeConvexPiece(local);
                if (piece.size() < 3)
                    break; // no area: Box2D would assert
                b2Vec2 vertices[b2_maxPolygonVertices];
                for (size_t i = 0; i < piece.size(); i++)
                    vertices[i] = ToB2(piece[i]);
                b2PolygonShape polygon;
                polygon.Set(vertices, (int32)piece.size());
                create(polygon);
                break;
            }
            case PhysicsShape2DDesc::Kind::Edge:
            {
                std::vector<glm::vec2> local;
                for (const glm::vec2& p : desc.Points)
                    local.push_back(p + desc.Offset);
                std::vector<glm::vec2> line = Polygon2D::CleanPolyline(local, desc.Loop);
                const size_t segments = line.size() < 2 ? 0 : desc.Loop ? line.size() : line.size() - 1;
                for (size_t i = 0; i < segments; i++)
                {
                    b2EdgeShape edge;
                    edge.SetTwoSided(ToB2(line[i]), ToB2(line[(i + 1) % line.size()]));
                    create(edge);
                }
                break;
            }
            case PhysicsShape2DDesc::Kind::Chain:
            {
                std::vector<glm::vec2> local;
                for (const glm::vec2& p : desc.Points)
                    local.push_back(p + desc.Offset);
                std::vector<glm::vec2> line = Polygon2D::CleanPolyline(local, desc.Loop);
                if (line.empty())
                    break;
                // Box2D's chains collide on the right of their direction: the points go the other
                // way round, so the solid side is on the left of the points as given.
                std::reverse(line.begin(), line.end());
                std::vector<b2Vec2> vertices;
                for (const glm::vec2& p : line)
                    vertices.push_back(ToB2(p));
                b2ChainShape chain;
                if (desc.Loop)
                    chain.CreateLoop(vertices.data(), (int32)vertices.size());
                else
                {
                    // Ghost vertices straight on: the ends behave like the rest of the chain.
                    const b2Vec2 prev = vertices[0] + (vertices[0] - vertices[1]);
                    const size_t n = vertices.size();
                    const b2Vec2 next = vertices[n - 1] + (vertices[n - 1] - vertices[n - 2]);
                    chain.CreateChain(vertices.data(), (int32)n, prev, next);
                }
                create(chain);
                break;
            }
        }
        return first;
    }

    void PhysicsWorld2D::SetShapeMaterial(b2Body* body, int tag, const PhysicsMaterialData& material)
    {
        if (!body)
            return;
        bool density = false, changed = false;
        for (b2Fixture* f = body->GetFixtureList(); f; f = f->GetNext())
        {
            auto it = m_Impl->Listener.Fixtures.find(f);
            if (it == m_Impl->Listener.Fixtures.end() || it->second.Tag != tag)
                continue;
            if (f->GetDensity() != material.Density && f->GetShape()->GetType() != b2Shape::e_edge && f->GetShape()->GetType() != b2Shape::e_chain)
            {
                f->SetDensity(material.Density);
                density = true;
            }
            f->SetFriction(material.Friction);
            f->SetRestitution(material.Restitution);
            f->SetRestitutionThreshold(material.RestitutionThreshold);
            FixtureInfo info = it->second;
            info.FrictionCombine = material.FrictionCombine;
            info.RestitutionCombine = material.RestitutionCombine;
            m_Impl->Listener.Remember(f, info);
            changed = true;
        }
        if (!changed)
            return;
        if (density)
            body->ResetMassData();
        // Contacts copy the mixed values when they start: the touching ones mix again.
        for (b2ContactEdge* edge = body->GetContactList(); edge; edge = edge->next)
        {
            edge->contact->ResetFriction();
            edge->contact->ResetRestitution();
            edge->contact->ResetRestitutionThreshold();
        }
        body->SetAwake(true);
    }

    int PhysicsWorld2D::GetFixtureCount(const b2Body* body)
    {
        int count = 0;
        if (body)
            for (const b2Fixture* f = body->GetFixtureList(); f; f = f->GetNext())
                count++;
        return count;
    }

    PhysicsMaterialData PhysicsWorld2D::GetShapeMaterial(const b2Fixture* fixture) const
    {
        PhysicsMaterialData d;
        if (!fixture)
            return d;
        d.Friction = fixture->GetFriction();
        d.Restitution = fixture->GetRestitution();
        d.RestitutionThreshold = fixture->GetRestitutionThreshold();
        d.Density = fixture->GetDensity();
        if (const FixtureInfo* info = m_Impl->Listener.Find(fixture))
        {
            d.FrictionCombine = info->FrictionCombine;
            d.RestitutionCombine = info->RestitutionCombine;
        }
        return d;
    }

    void PhysicsWorld2D::GetShapeFilter(const b2Fixture* fixture, uint16_t& categoryBits, uint16_t& maskBits)
    {
        categoryBits = maskBits = 0;
        if (!fixture)
            return;
        const b2Filter& filter = fixture->GetFilterData();
        categoryBits = filter.categoryBits;
        maskBits = filter.maskBits;
    }

    void PhysicsWorld2D::DestroyBody(b2Body* body, std::vector<PhysicsJoint2DRef>* destroyedJoints)
    {
        if (!body)
            return;
        if (destroyedJoints)
            for (b2JointEdge* edge = body->GetJointList(); edge; edge = edge->next)
                destroyedJoints->push_back({ UUID((uint64_t)edge->joint->GetUserData().pointer), FromBox2DJointType(edge->joint->GetType()) });
        m_Impl->World.DestroyBody(body); // its fixtures and joints go with it
    }

    void PhysicsWorld2D::WakeBodiesOverlapping(b2Body* body)
    {
        if (!body)
            return;
        struct Waker : b2QueryCallback
        {
            b2Body* Self = nullptr;
            bool ReportFixture(b2Fixture* fixture) override
            {
                b2Body* other = fixture->GetBody();
                if (other != Self && other->GetType() != b2_staticBody && !other->IsAwake())
                    other->SetAwake(true);
                return true; // keep going
            }
        } waker;
        waker.Self = body;
        for (b2Fixture* f = body->GetFixtureList(); f; f = f->GetNext())
            for (int32 child = 0; child < f->GetShape()->GetChildCount(); child++)
                m_Impl->World.QueryAABB(&waker, f->GetAABB(child));
    }

    glm::vec2 PhysicsWorld2D::GetPosition(const b2Body* body) { return FromB2(body->GetPosition()); }
    float PhysicsWorld2D::GetAngle(const b2Body* body) { return body->GetAngle(); }
    void PhysicsWorld2D::SetTransform(b2Body* body, const glm::vec2& position, float angle) { body->SetTransform(ToB2(position), angle); }
    glm::vec2 PhysicsWorld2D::GetLinearVelocity(const b2Body* body) { return FromB2(body->GetLinearVelocity()); }
    void PhysicsWorld2D::SetLinearVelocity(b2Body* body, const glm::vec2& velocity) { body->SetLinearVelocity(ToB2(velocity)); }
    float PhysicsWorld2D::GetAngularVelocity(const b2Body* body) { return body->GetAngularVelocity(); }
    glm::vec2 PhysicsWorld2D::GetWorldCenter(const b2Body* body) { return FromB2(body->GetWorldCenter()); }
    bool PhysicsWorld2D::IsDynamic(const b2Body* body) { return body->GetType() == b2_dynamicBody; }
    bool PhysicsWorld2D::IsAwake(const b2Body* body) { return body->IsAwake(); }
    bool PhysicsWorld2D::IsEnabled(const b2Body* body) { return body->IsEnabled(); }
    void PhysicsWorld2D::SetAwake(b2Body* body, bool awake) { body->SetAwake(awake); }

    // ------------------------------------------------------------------ simulation
    void PhysicsWorld2D::Step(float dt, int velocityIterations, int positionIterations)
    {
        ContactListener2D& listener = m_Impl->Listener;
        listener.StepTime = dt;
        m_Impl->World.Step(dt, velocityIterations, positionIterations);
        if (!listener.DropThrough.empty())
            std::erase_if(listener.DropThrough, [&](auto& item) { return (item.second -= dt) <= 0.0f; });
    }

    std::vector<ContactEvent> PhysicsWorld2D::TakeEvents()
    {
        std::vector<ContactEvent> events = std::move(m_Impl->Listener.Events);
        m_Impl->Listener.Events.clear();
        return events;
    }

    void PhysicsWorld2D::DropThroughPlatforms(UUID entity, b2Body* body, float seconds)
    {
        auto& dropThrough = m_Impl->Listener.DropThrough;
        float& left = dropThrough[(uint64_t)entity];
        left = std::max(left, std::max(0.0f, seconds));
        if (left <= 0.0f)
            dropThrough.erase((uint64_t)entity);
        if (body)
            body->SetAwake(true);
    }

    // ------------------------------------------------------------------ joints
    b2Joint* PhysicsWorld2D::CreateJoint(PhysicsJoint2DDesc& d)
    {
        b2Body* bodyB = d.Body;
        b2Body* bodyA = d.ConnectedBody ? d.ConnectedBody : m_Impl->GetGround();
        if (!bodyB || bodyA == bodyB)
            return nullptr;
        // The ground body sits at the origin unrotated: its local points are world points.
        const b2Vec2 worldAnchor = bodyB->GetWorldPoint(ToB2(d.Anchor));
        if (d.AutoConfigureConnectedAnchor)
            d.ConnectedAnchor = FromB2(bodyA->GetLocalPoint(worldAnchor));
        const b2Vec2 localA = ToB2(d.ConnectedAnchor), localB = ToB2(d.Anchor);
        const float referenceAngle = bodyB->GetAngle() - bodyA->GetAngle();
        const float lower = std::min(d.LowerLimit, d.UpperLimit), upper = std::max(d.LowerLimit, d.UpperLimit);

        b2Joint* joint = nullptr;
        auto finish = [&](b2JointDef& def) {
            def.bodyA = bodyA;
            def.bodyB = bodyB;
            def.collideConnected = d.CollideConnected;
            def.userData.pointer = (uintptr_t)(uint64_t)d.EntityID;
            joint = m_Impl->World.CreateJoint(&def);
        };
        switch (d.Type)
        {
            case Joint2DType::Distance:
            {
                if (d.AutoConfigureDistance)
                    d.Distance = b2Distance(bodyA->GetWorldPoint(localA), worldAnchor);
                b2DistanceJointDef def;
                def.localAnchorA = localA;
                def.localAnchorB = localB;
                DistanceSettings s = MakeDistanceSettings(d, bodyA, bodyB);
                def.length = s.Length;
                def.minLength = s.MinLength;
                def.maxLength = s.MaxLength;
                def.stiffness = s.Stiffness;
                def.damping = s.Damping;
                finish(def);
                break;
            }
            case Joint2DType::Revolute:
            {
                b2RevoluteJointDef def;
                def.localAnchorA = localA;
                def.localAnchorB = localB;
                def.referenceAngle = referenceAngle;
                def.enableLimit = d.EnableLimit;
                def.lowerAngle = ToRadians(lower);
                def.upperAngle = ToRadians(upper);
                def.enableMotor = d.EnableMotor;
                def.motorSpeed = ToRadians(d.MotorSpeed);
                def.maxMotorTorque = std::max(0.0f, d.MaxMotorForce);
                finish(def);
                break;
            }
            case Joint2DType::Prismatic:
            {
                b2PrismaticJointDef def;
                def.localAnchorA = localA;
                def.localAnchorB = localB;
                def.localAxisA = AxisInConnectedFrame(d, bodyA, bodyB);
                def.referenceAngle = referenceAngle;
                def.enableLimit = d.EnableLimit;
                def.lowerTranslation = lower;
                def.upperTranslation = upper;
                def.enableMotor = d.EnableMotor;
                def.motorSpeed = d.MotorSpeed;
                def.maxMotorForce = std::max(0.0f, d.MaxMotorForce);
                finish(def);
                break;
            }
            case Joint2DType::Weld:
            {
                b2WeldJointDef def;
                def.localAnchorA = localA;
                def.localAnchorB = localB;
                def.referenceAngle = referenceAngle;
                if (d.Frequency > 0.0f)
                    b2AngularStiffness(def.stiffness, def.damping, d.Frequency, std::max(0.0f, d.DampingRatio), bodyA, bodyB);
                finish(def);
                break;
            }
            case Joint2DType::Wheel:
            {
                b2WheelJointDef def;
                def.localAnchorA = localA;
                def.localAnchorB = localB;
                def.localAxisA = AxisInConnectedFrame(d, bodyA, bodyB);
                if (d.Frequency > 0.0f)
                    b2LinearStiffness(def.stiffness, def.damping, d.Frequency, std::max(0.0f, d.DampingRatio), bodyA, bodyB);
                def.enableLimit = d.EnableLimit;
                def.lowerTranslation = lower;
                def.upperTranslation = upper;
                def.enableMotor = d.EnableMotor;
                def.motorSpeed = ToRadians(d.MotorSpeed);
                def.maxMotorTorque = std::max(0.0f, d.MaxMotorForce);
                finish(def);
                break;
            }
        }
        return joint;
    }

    void PhysicsWorld2D::DestroyJoint(b2Joint* joint)
    {
        if (joint)
            m_Impl->World.DestroyJoint(joint);
    }

    void PhysicsWorld2D::ClaimJoint(UUID entity, Joint2DType type, b2Joint*& joint)
    {
        b2Joint* keep = nullptr;
        for (b2Joint* j = m_Impl->World.GetJointList(); j;)
        {
            b2Joint* next = j->GetNext();
            if ((uint64_t)j->GetUserData().pointer == (uint64_t)entity && FromBox2DJointType(j->GetType()) == type)
            {
                if (j == joint)
                    keep = j;
                else
                    m_Impl->World.DestroyJoint(j);
            }
            j = next;
        }
        joint = keep;
    }

    void PhysicsWorld2D::UpdateJoint(b2Joint* joint, const PhysicsJoint2DDesc& d)
    {
        if (!joint)
            return;
        const float lower = std::min(d.LowerLimit, d.UpperLimit), upper = std::max(d.LowerLimit, d.UpperLimit);
        // The Box2D setters of limits / motors wake the bodies themselves, and only on a change.
        switch (joint->GetType())
        {
            case e_distanceJoint:
            {
                auto* j = (b2DistanceJoint*)joint;
                DistanceSettings s = MakeDistanceSettings(d, j->GetBodyA(), j->GetBodyB());
                if (j->GetLength() != s.Length || j->GetMinLength() != s.MinLength || j->GetMaxLength() != s.MaxLength ||
                    j->GetStiffness() != s.Stiffness || j->GetDamping() != s.Damping)
                {
                    j->SetLength(s.Length);
                    j->SetMinLength(b2_linearSlop); // min <= max on the way
                    j->SetMaxLength(s.MaxLength);
                    j->SetMinLength(s.MinLength);
                    j->SetStiffness(s.Stiffness);
                    j->SetDamping(s.Damping);
                    Wake(joint);
                }
                break;
            }
            case e_revoluteJoint:
            {
                auto* j = (b2RevoluteJoint*)joint;
                j->EnableLimit(d.EnableLimit);
                j->SetLimits(ToRadians(lower), ToRadians(upper));
                j->EnableMotor(d.EnableMotor);
                j->SetMotorSpeed(ToRadians(d.MotorSpeed));
                j->SetMaxMotorTorque(std::max(0.0f, d.MaxMotorForce));
                break;
            }
            case e_prismaticJoint:
            {
                auto* j = (b2PrismaticJoint*)joint;
                j->EnableLimit(d.EnableLimit);
                j->SetLimits(lower, upper);
                j->EnableMotor(d.EnableMotor);
                j->SetMotorSpeed(d.MotorSpeed);
                j->SetMaxMotorForce(std::max(0.0f, d.MaxMotorForce));
                break;
            }
            case e_weldJoint:
            {
                auto* j = (b2WeldJoint*)joint;
                float stiffness = 0.0f, damping = 0.0f;
                if (d.Frequency > 0.0f)
                    b2AngularStiffness(stiffness, damping, d.Frequency, std::max(0.0f, d.DampingRatio), j->GetBodyA(), j->GetBodyB());
                if (j->GetStiffness() != stiffness || j->GetDamping() != damping)
                {
                    j->SetStiffness(stiffness);
                    j->SetDamping(damping);
                    Wake(joint);
                }
                break;
            }
            case e_wheelJoint:
            {
                auto* j = (b2WheelJoint*)joint;
                float stiffness = 0.0f, damping = 0.0f;
                if (d.Frequency > 0.0f)
                    b2LinearStiffness(stiffness, damping, d.Frequency, std::max(0.0f, d.DampingRatio), j->GetBodyA(), j->GetBodyB());
                if (j->GetStiffness() != stiffness || j->GetDamping() != damping)
                {
                    j->SetStiffness(stiffness);
                    j->SetDamping(damping);
                    Wake(joint);
                }
                j->EnableLimit(d.EnableLimit);
                j->SetLimits(lower, upper);
                j->EnableMotor(d.EnableMotor);
                j->SetMotorSpeed(ToRadians(d.MotorSpeed));
                j->SetMaxMotorTorque(std::max(0.0f, d.MaxMotorForce));
                break;
            }
            default:
                break;
        }
    }

    PhysicsJoint2DState PhysicsWorld2D::GetJointState(const b2Joint* constJoint, float invDt)
    {
        PhysicsJoint2DState state;
        if (!constJoint)
            return state;
        // Box2D's getters are not all const.
        auto* joint = const_cast<b2Joint*>(constJoint);
        state.Force = joint->GetReactionForce(invDt).Length();
        state.Torque = std::abs(joint->GetReactionTorque(invDt));
        switch (joint->GetType())
        {
            case e_distanceJoint:
                state.Translation = ((b2DistanceJoint*)joint)->GetCurrentLength();
                break;
            case e_revoluteJoint:
                state.Angle = ToDegrees(((b2RevoluteJoint*)joint)->GetJointAngle());
                state.Speed = ToDegrees(((b2RevoluteJoint*)joint)->GetJointSpeed());
                break;
            case e_prismaticJoint:
                state.Translation = ((b2PrismaticJoint*)joint)->GetJointTranslation();
                state.Speed = ((b2PrismaticJoint*)joint)->GetJointSpeed();
                break;
            case e_wheelJoint:
                state.Translation = ((b2WheelJoint*)joint)->GetJointTranslation();
                state.Speed = ToDegrees(((b2WheelJoint*)joint)->GetJointAngularSpeed());
                break;
            default:
                break;
        }
        return state;
    }

    void PhysicsWorld2D::GetJointAnchors(const b2Joint* constJoint, glm::vec2& anchor, glm::vec2& connectedAnchor)
    {
        auto* joint = const_cast<b2Joint*>(constJoint);
        anchor = FromB2(joint->GetAnchorB());
        connectedAnchor = FromB2(joint->GetAnchorA());
    }

    size_t PhysicsWorld2D::GetJointCount() const { return (size_t)m_Impl->World.GetJointCount(); }

    // ------------------------------------------------------------------ effectors
    PhysicsEffectorResult2D PhysicsWorld2D::ApplyAreaEffector(b2Body* effector, const PhysicsAreaEffector2D& s, float dt)
    {
        PhysicsEffectorResult2D result;
        if (!effector || !effector->IsEnabled())
            return result;
        std::vector<b2Body*> bodies;
        for (b2ContactEdge* edge = effector->GetContactList(); edge; edge = edge->next)
        {
            b2Fixture* mine = nullptr;
            if (b2Fixture* other = EffectorTarget(edge->contact, effector, s.LayerMask, mine))
                if (std::find(bodies.begin(), bodies.end(), other->GetBody()) == bodies.end())
                    bodies.push_back(other->GetBody());
        }
        // Drag is a velocity change of Drag x dt per step: kept below the velocity itself.
        const float limit = dt > 0.0f ? 0.9f / dt : FLT_MAX;
        const float baseMagnitude = glm::length(s.Force);
        const b2Vec2 dir = baseMagnitude > 0.0f ? ToB2(s.Force / baseMagnitude) : b2Vec2(0.0f, 0.0f);
        for (b2Body* body : bodies)
        {
            const float mass = body->GetMass();
            float magnitude = baseMagnitude;
            if (s.Variation != 0.0f && baseMagnitude > 0.0f)
                magnitude += s.Variation * m_Impl->RandomSigned();
            b2Vec2 force = (s.ScaleWithMass ? mass : 1.0f) * magnitude * dir;
            if (s.Drag > 0.0f)
            {
                const b2Vec2 v = body->GetLinearVelocity() - effector->GetLinearVelocityFromWorldPoint(body->GetWorldCenter());
                force -= (std::min(s.Drag, limit) * mass) * v;
            }
            if (force.LengthSquared() > 0.0f) // a sleeping body without a push stays asleep
                body->ApplyForceToCenter(force, true);
            if (s.AngularDrag > 0.0f)
            {
                const float w = body->GetAngularVelocity() - effector->GetAngularVelocity();
                if (w != 0.0f)
                    body->ApplyTorque(-std::min(s.AngularDrag, limit) * CentralInertia(body) * w, true);
            }
            result.Bodies++;
        }
        return result;
    }

    PhysicsEffectorResult2D PhysicsWorld2D::ApplyBuoyancy(b2Body* effector, const PhysicsBuoyancy2D& s, float dt)
    {
        PhysicsEffectorResult2D result;
        if (!effector || !effector->IsEnabled())
            return result;
        const b2Vec2 gravity = m_Impl->World.GetGravity();
        const float g = gravity.Length();
        const glm::vec2 up = g > 0.0f ? -FromB2(gravity) / g : glm::vec2(0.0f, 1.0f);
        const glm::vec2 surface = FromB2(effector->GetPosition()) + up * s.SurfaceLevel;

        // The parts of the bodies' fixtures inside the fluid: area and area-weighted centre.
        struct Submerged
        {
            b2Body* Body;
            float Area = 0.0f;
            glm::vec2 Moment{ 0.0f };
        };
        std::vector<Submerged> parts;
        std::vector<glm::vec2> fluid, shape;
        for (b2ContactEdge* edge = effector->GetContactList(); edge; edge = edge->next)
        {
            b2Fixture* mine = nullptr;
            b2Fixture* other = EffectorTarget(edge->contact, effector, s.LayerMask, mine);
            if (!other || !WorldPolygon(mine, fluid) || !WorldPolygon(other, shape))
                continue;
            const std::vector<glm::vec2> under = Polygon2D::ClipHalfPlane(Polygon2D::ClipConvex(shape, fluid), surface, up);
            float area;
            glm::vec2 centroid;
            if (!Polygon2D::AreaCentroid(under, area, centroid))
                continue;
            auto it = std::find_if(parts.begin(), parts.end(), [&](const Submerged& p) { return p.Body == other->GetBody(); });
            if (it == parts.end())
                it = parts.insert(parts.end(), Submerged{ other->GetBody() });
            it->Area += area;
            it->Moment += centroid * area;
        }

        const float limit = dt > 0.0f ? 0.9f / dt : FLT_MAX;
        const float flowMagnitude = glm::length(s.Flow);
        for (const Submerged& part : parts)
        {
            b2Body* body = part.Body;
            // How much of the body is under: its solid area against the submerged one.
            float bodyArea = 0.0f;
            for (const b2Fixture* f = body->GetFixtureList(); f; f = f->GetNext())
            {
                float area;
                glm::vec2 centroid;
                if (!f->IsSensor() && WorldPolygon(f, shape) && Polygon2D::AreaCentroid(shape, area, centroid))
                    bodyArea += area;
            }
            const float fraction = bodyArea > 0.0f ? std::clamp(part.Area / bodyArea, 0.0f, 1.0f) : 1.0f;
            const glm::vec2 center = part.Moment / part.Area;
            const b2Vec2 at = ToB2(center);
            const float mass = body->GetMass();

            glm::vec2 force = up * (std::max(0.0f, s.Density) * part.Area * g);
            if (s.LinearDrag > 0.0f)
            {
                const glm::vec2 v = FromB2(body->GetLinearVelocityFromWorldPoint(at) - effector->GetLinearVelocityFromWorldPoint(at));
                force -= v * (std::min(s.LinearDrag * fraction, limit) * mass);
            }
            if (flowMagnitude > 0.0f)
            {
                float magnitude = flowMagnitude;
                if (s.FlowVariation != 0.0f)
                    magnitude += s.FlowVariation * m_Impl->RandomSigned();
                force += s.Flow / flowMagnitude * (magnitude * fraction);
            }
            body->ApplyForce(ToB2(force), at, true);
            if (s.AngularDrag > 0.0f)
            {
                const float w = body->GetAngularVelocity() - effector->GetAngularVelocity();
                if (w != 0.0f)
                    body->ApplyTorque(-std::min(s.AngularDrag * fraction, limit) * CentralInertia(body) * w, true);
            }
            result.Bodies++;
            result.SubmergedArea += part.Area;
        }
        return result;
    }

    glm::vec2 PhysicsWorld2D::GetGravity() const { return FromB2(m_Impl->World.GetGravity()); }

    // ------------------------------------------------------------------ queries
    RaycastHit PhysicsWorld2D::CastRay(const glm::vec2& origin, const glm::vec2& direction, float maxDistance, uint16_t layerMask) const
    {
        RaycastHit hit;
        if (glm::length(direction) == 0.0f || !(maxDistance > 0.0f))
            return hit;
        glm::vec2 dir = glm::normalize(direction);

        struct Callback : b2RayCastCallback
        {
            RaycastHit* Out;
            float MaxDistance;
            uint16_t Mask;
            float ReportFixture(b2Fixture* fixture, const b2Vec2& point, const b2Vec2& normal, float fraction) override
            {
                if (fixture->IsSensor()) return -1.0f; // ignore triggers
                if ((fixture->GetFilterData().categoryBits & Mask) == 0) return -1.0f; // another layer
                Out->Hit = true;
                Out->EntityID = (uint64_t)fixture->GetUserData().pointer;
                Out->Point = { point.x, point.y, 0.0f };
                Out->Normal = { normal.x, normal.y, 0.0f };
                Out->Distance = fraction * MaxDistance;
                return fraction; // clip to closest
            }
        } cb;
        cb.Out = &hit;
        cb.MaxDistance = maxDistance;
        cb.Mask = layerMask;
        b2Vec2 p1(origin.x, origin.y), p2(origin.x + dir.x * maxDistance, origin.y + dir.y * maxDistance);
        m_Impl->World.RayCast(&cb, p1, p2);
        return hit;
    }

    std::vector<RaycastHit> PhysicsWorld2D::CastRayAll(const glm::vec2& origin, const glm::vec2& direction, float maxDistance,
                                                       uint16_t layerMask, bool includeSensors) const
    {
        std::vector<RaycastHit> hits;
        if (glm::length(direction) == 0.0f || !(maxDistance > 0.0f) || !std::isfinite(maxDistance))
            return hits;
        const glm::vec2 dir = glm::normalize(direction);
        struct Callback : b2RayCastCallback
        {
            std::vector<RaycastHit>* Out;
            float MaxDistance;
            uint16_t Mask;
            bool Sensors;
            float ReportFixture(b2Fixture* fixture, const b2Vec2& point, const b2Vec2& normal, float fraction) override
            {
                if (!PassesQuery(fixture, Mask, Sensors))
                    return -1.0f; // ignore it, go on
                RaycastHit hit;
                hit.Hit = true;
                hit.EntityID = (uint64_t)fixture->GetUserData().pointer;
                hit.Point = { point.x, point.y, 0.0f };
                hit.Normal = { normal.x, normal.y, 0.0f };
                hit.Distance = fraction * MaxDistance;
                Out->push_back(hit);
                return 1.0f; // the whole ray: every fixture on it
            }
        } cb;
        cb.Out = &hits;
        cb.MaxDistance = maxDistance;
        cb.Mask = layerMask;
        cb.Sensors = includeSensors;
        m_Impl->World.RayCast(&cb, ToB2(origin), ToB2(origin + dir * maxDistance));
        // Box2D reports them in no particular order: by distance, each entity's first hit only.
        std::sort(hits.begin(), hits.end(), [](const RaycastHit& a, const RaycastHit& b) {
            return a.Distance != b.Distance ? a.Distance < b.Distance : (uint64_t)a.EntityID < (uint64_t)b.EntityID;
        });
        std::unordered_set<uint64_t> seen;
        std::erase_if(hits, [&](const RaycastHit& hit) { return !seen.insert((uint64_t)hit.EntityID).second; });
        return hits;
    }

    std::vector<UUID> PhysicsWorld2D::OverlapCircle(const glm::vec2& center, float radius, uint16_t layerMask, bool includeSensors) const
    {
        if (!(radius > 0.0f) || !std::isfinite(radius) || !std::isfinite(center.x) || !std::isfinite(center.y))
            return {};
        b2CircleShape circle;
        circle.m_radius = radius;
        b2Transform xf(ToB2(center), b2Rot(0.0f));
        return OverlapShape(m_Impl->World, circle, xf, layerMask, includeSensors);
    }

    std::vector<UUID> PhysicsWorld2D::OverlapBox(const glm::vec2& center, const glm::vec2& halfExtents, float angle, uint16_t layerMask,
                                                 bool includeSensors) const
    {
        const glm::vec2 h = glm::abs(halfExtents);
        if (!(h.x > 0.0f && h.y > 0.0f) || !std::isfinite(h.x) || !std::isfinite(h.y) || !std::isfinite(center.x) || !std::isfinite(center.y) ||
            !std::isfinite(angle))
            return {};
        b2PolygonShape box;
        box.SetAsBox(std::max(h.x, b2_linearSlop), std::max(h.y, b2_linearSlop));
        b2Transform xf(ToB2(center), b2Rot(angle));
        return OverlapShape(m_Impl->World, box, xf, layerMask, includeSensors);
    }

    std::vector<UUID> PhysicsWorld2D::OverlapPoint(const glm::vec2& point, uint16_t layerMask, bool includeSensors) const
    {
        if (!std::isfinite(point.x) || !std::isfinite(point.y))
            return {};
        struct Query : b2QueryCallback
        {
            b2Vec2 Point;
            uint16_t Mask;
            bool Sensors;
            std::vector<std::pair<float, uint64_t>> Hits;
            bool ReportFixture(b2Fixture* fixture) override
            {
                if (PassesQuery(fixture, Mask, Sensors) && fixture->TestPoint(Point))
                    Hits.push_back({ b2DistanceSquared(fixture->GetBody()->GetPosition(), Point), (uint64_t)fixture->GetUserData().pointer });
                return true;
            }
        } query;
        query.Point = ToB2(point);
        query.Mask = layerMask;
        query.Sensors = includeSensors;
        b2AABB box;
        box.lowerBound = query.Point - b2Vec2(1e-4f, 1e-4f);
        box.upperBound = query.Point + b2Vec2(1e-4f, 1e-4f);
        m_Impl->World.QueryAABB(&query, box);
        std::sort(query.Hits.begin(), query.Hits.end());
        std::vector<UUID> out;
        std::unordered_set<uint64_t> seen;
        for (const auto& [distance, id] : query.Hits)
            if (seen.insert(id).second)
                out.push_back(UUID(id));
        return out;
    }

    void PhysicsWorld2D::GetDebugContacts(std::vector<PhysicsContactPoint2D>& out) const
    {
        for (b2Contact* contact = m_Impl->World.GetContactList(); contact; contact = contact->GetNext())
        {
            if (!contact->IsTouching() || !contact->IsEnabled() || contact->GetFixtureA()->IsSensor() || contact->GetFixtureB()->IsSensor())
                continue;
            const int count = contact->GetManifold()->pointCount;
            if (count <= 0)
                continue;
            b2WorldManifold manifold;
            contact->GetWorldManifold(&manifold);
            // Box2D's normal points from A to B: turned so that it leaves a static / kinematic
            // body (the ground's normal under the player), as the 3D world's.
            glm::vec2 normal = FromB2(manifold.normal);
            if (contact->GetFixtureA()->GetBody()->GetType() == b2_dynamicBody && contact->GetFixtureB()->GetBody()->GetType() != b2_dynamicBody)
                normal = -normal;
            UUID entityA((uint64_t)contact->GetFixtureA()->GetUserData().pointer);
            for (int i = 0; i < count && i < b2_maxManifoldPoints; i++)
                out.push_back({ FromB2(manifold.points[i]), normal, entityA });
        }
    }

}
