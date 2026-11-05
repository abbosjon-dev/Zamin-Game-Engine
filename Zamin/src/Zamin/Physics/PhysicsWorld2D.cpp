#include "Zamin/Physics/PhysicsWorld2D.h"
#include "Zamin/Scene/TilesetProperties.h"

#include <box2d/b2_body.h>
#include <box2d/b2_circle_shape.h>
#include <box2d/b2_contact.h>
#include <box2d/b2_distance_joint.h>
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
        // Collects the contact events of a physics step for the scripts, and decides the contacts of
        // the one-way surfaces (OneWay tiles, BoxCollider2D::OneWay): Box2D re-enables every contact
        // each step and asks PreSolve, which switches it off while the body passes through. Such a
        // contact is a collision for the scripts only while it is on (Begin when it starts holding the
        // body up, End when it lets go), so jumping up through a platform reports nothing.
        class ContactListener2D : public b2ContactListener, public b2DestructionListener
        {
        public:
            void BeginContact(b2Contact* contact) override
            {
                if (IsOneWay(contact))
                {
                    m_OneWayContacts[contact] = false; // reported once PreSolve lets it collide
                    return;
                }
                Push(contact, true);
            }
            void EndContact(b2Contact* contact) override
            {
                if (auto it = m_OneWayContacts.find(contact); it != m_OneWayContacts.end())
                {
                    if (it->second)
                        Push(contact, false);
                    m_OneWayContacts.erase(it);
                    return;
                }
                Push(contact, false);
            }
            void PreSolve(b2Contact* contact, const b2Manifold*) override
            {
                auto it = m_OneWayContacts.find(contact);
                if (it == m_OneWayContacts.end())
                    return;
                bool collide = ShouldCollide(contact);
                contact->SetEnabled(collide);
                if (collide != it->second)
                {
                    it->second = collide;
                    Push(contact, collide);
                }
            }

            // b2DestructionListener: a body's fixtures go with it.
            void SayGoodbye(b2Joint*) override {}
            void SayGoodbye(b2Fixture* fixture) override { OneWayFixtures.erase(fixture); }

            std::vector<ContactEvent> Events;
            std::unordered_set<const b2Fixture*> OneWayFixtures;
            // Entities that fall through one-way surfaces for a while (DropThroughPlatforms): seconds left.
            std::unordered_map<uint64_t, float> DropThrough;
            float StepTime = 1.0f / 60.0f;

        private:
            void Push(b2Contact* contact, bool begin)
            {
                b2Fixture* fa = contact->GetFixtureA();
                b2Fixture* fb = contact->GetFixtureB();
                uint64_t a = (uint64_t)fa->GetUserData().pointer;
                uint64_t b = (uint64_t)fb->GetUserData().pointer;
                if (a && b)
                    Events.push_back({ a, b, begin, fa->IsSensor() || fb->IsSensor() });
            }

            bool IsOneWay(b2Contact* contact) const
            {
                if (OneWayFixtures.empty())
                    return false;
                const b2Fixture* fa = contact->GetFixtureA();
                const b2Fixture* fb = contact->GetFixtureB();
                if (fa->IsSensor() || fb->IsSensor())
                    return false;
                return OneWayFixtures.contains(fa) || OneWayFixtures.contains(fb);
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

            bool ShouldCollide(b2Contact* contact) const
            {
                b2Fixture* platform = contact->GetFixtureA();
                b2Fixture* other = contact->GetFixtureB();
                if (!OneWayFixtures.contains(platform))
                    std::swap(platform, other);
                b2Body* platformBody = platform->GetBody();
                b2Body* body = other->GetBody();
                if (!DropThrough.empty())
                    if (auto it = DropThrough.find((uint64_t)body->GetUserData().pointer); it != DropThrough.end() && it->second > 0.0f)
                        return false;
                const b2Vec2 up = b2Mul(platformBody->GetTransform().q, b2Vec2(0.0f, 1.0f));
                float platformMin, top, bottom, otherMax;
                Project(platform, up, platformMin, top);
                Project(other, up, bottom, otherMax);
                b2Vec2 relative = body->GetLinearVelocity() - platformBody->GetLinearVelocity();
                return OneWayPlatform::ShouldCollide(bottom, top, b2Dot(relative, up), StepTime);
            }

            std::unordered_map<b2Contact*, bool> m_OneWayContacts; // touching one-way contacts: reported as colliding?
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

    }

    // ------------------------------------------------------------------ world
    struct PhysicsWorld2D::Impl
    {
        ContactListener2D Listener; // outlives the world
        b2World World;
        b2Body* Ground = nullptr; // anchors joints without a connected body (created on demand)

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
        b2PolygonShape box;
        b2CircleShape circle;
        b2FixtureDef fixtureDef;
        if (desc.Type == PhysicsShape2DDesc::Kind::Circle)
        {
            circle.m_p = ToB2(desc.Offset);
            circle.m_radius = desc.Radius;
            fixtureDef.shape = &circle;
        }
        else
        {
            box.SetAsBox(desc.HalfExtents.x, desc.HalfExtents.y, ToB2(desc.Offset), 0.0f);
            fixtureDef.shape = &box;
        }
        fixtureDef.density = desc.Density;
        fixtureDef.friction = desc.Friction;
        fixtureDef.restitution = desc.Restitution;
        fixtureDef.restitutionThreshold = desc.RestitutionThreshold;
        fixtureDef.isSensor = desc.IsSensor;
        fixtureDef.userData.pointer = body->GetUserData().pointer;
        b2Fixture* fixture = body->CreateFixture(&fixtureDef);
        if (desc.OneWay && !desc.IsSensor)
            m_Impl->Listener.OneWayFixtures.insert(fixture);
        return fixture;
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

    // ------------------------------------------------------------------ queries
    RaycastHit PhysicsWorld2D::CastRay(const glm::vec2& origin, const glm::vec2& direction, float maxDistance) const
    {
        RaycastHit hit;
        if (glm::length(direction) == 0.0f)
            return hit;
        glm::vec2 dir = glm::normalize(direction);

        struct Callback : b2RayCastCallback
        {
            RaycastHit* Out;
            float MaxDistance;
            float ReportFixture(b2Fixture* fixture, const b2Vec2& point, const b2Vec2& normal, float fraction) override
            {
                if (fixture->IsSensor()) return -1.0f; // ignore triggers
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
        b2Vec2 p1(origin.x, origin.y), p2(origin.x + dir.x * maxDistance, origin.y + dir.y * maxDistance);
        m_Impl->World.RayCast(&cb, p1, p2);
        return hit;
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
