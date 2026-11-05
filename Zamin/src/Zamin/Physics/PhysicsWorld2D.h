#pragma once

#include "Zamin/Physics/PhysicsMaterial.h"
#include "Zamin/Physics/PhysicsTypes.h"

#include <glm/glm.hpp>

#include <memory>
#include <vector>

// Box2D handles stay opaque outside the physics code (the components keep them as void*).
class b2Body;
class b2Fixture;
class b2Joint;

namespace Zamin {

    struct PhysicsBody2DDesc
    {
        UUID EntityID = 0;
        int MotionType = 1; // 0 static, 1 dynamic, 2 kinematic (Rigidbody2DComponent::BodyType)
        glm::vec2 Position{ 0.0f };
        float Angle = 0.0f; // radians
        bool FixedRotation = false;
        float GravityScale = 1.0f;
        float LinearDamping = 0.0f;
        float AngularDamping = 0.0f;
        bool Bullet = false;
    };

    // A platform surface (PlatformEffector2DComponent; BoxCollider2D::OneWay / OneWay tiles are the
    // classic one: one-way, 180 degrees, side friction and bounce kept). Angles in degrees.
    struct PhysicsPlatform2D
    {
        bool Enabled = false;
        // Bodies pass through it unless they touch it from within SurfaceArc around its up (the
        // body's +Y turned by RotationOffset). At 180 a body lands when it was above the surface
        // (OneWayPlatform::ShouldCollide); a smaller arc also leaves out contacts whose normal is
        // further from up; a larger one also holds bodies that touch it from the side first.
        bool OneWay = true;
        float SurfaceArc = 180.0f;
        float RotationOffset = 0.0f;
        // Contacts within SideArc of the surface's left / right: friction / bounce only when on.
        bool UseSideFriction = true;
        bool UseSideBounce = true;
        float SideArc = 1.0f;
    };

    // One fixture of a body (Edge: one per segment). Its user data is the body's entity.
    struct PhysicsShape2DDesc
    {
        enum class Kind { Box, Circle, Polygon, Edge, Chain } Type = Kind::Box;
        glm::vec2 HalfExtents{ 0.5f }; // box
        float Radius = 0.5f;           // circle
        glm::vec2 Offset{ 0.0f };      // centre in the body's frame (polygons / edges: added to the points)
        // Polygon: one convex piece of 3..8 corners (see Polygon2D::Decompose). Edge: two-sided
        // segments through the points. Chain: one smooth one-sided chain, solid on the left of the
        // points' direction. In the body's frame.
        std::vector<glm::vec2> Points;
        bool Loop = false;             // edge / chain: the last point joins the first
        float Density = 0.0f;
        float Friction = 0.2f;
        float Restitution = 0.0f;
        float RestitutionThreshold = 1.0f;
        // How friction / restitution mix with the other collider's (physics material assets).
        PhysicsCombine FrictionCombine = PhysicsCombine::Default;
        PhysicsCombine RestitutionCombine = PhysicsCombine::Default;
        bool IsSensor = false;
        // Collision filtering (CollisionLayers2D): this shape's layer bit and the layers it touches.
        uint16_t CategoryBits = 0x0001;
        uint16_t MaskBits = 0xFFFF;
        // Which part of the entity made it (a component): SetShapeMaterial finds its fixtures by it.
        int Tag = 0;
        // A one-way surface (OneWay tiles, BoxCollider2D::OneWay): bodies pass through it from below
        // and land on its top (its up = the body's +Y). The classic Platform; ignored for sensors.
        bool OneWay = false;
        PhysicsPlatform2D Platform;
    };

    // A joint between a body (Box2D's body B: the entity the joint component belongs to) and a
    // connected body (body A) or a fixed point in the world. Angles in degrees.
    struct PhysicsJoint2DDesc
    {
        Joint2DType Type = Joint2DType::Distance;
        UUID EntityID = 0;               // the joint's entity (reported when the body goes)
        b2Body* Body = nullptr;
        b2Body* ConnectedBody = nullptr; // null: anchored to the world
        glm::vec2 Anchor{ 0.0f };          // on Body, in its frame
        glm::vec2 ConnectedAnchor{ 0.0f }; // on ConnectedBody (its frame), else a world point
        // ConnectedAnchor becomes the point where Anchor is now (CreateJoint writes it back).
        bool AutoConfigureConnectedAnchor = true;
        bool CollideConnected = false;

        // Distance: the length between the anchors (AutoConfigureDistance: the current one,
        // written back). MaxDistanceOnly: a rope - it may get shorter, never longer.
        float Distance = 1.0f;
        bool AutoConfigureDistance = true;
        bool MaxDistanceOnly = false;
        // Springs - distance (along the rope), weld (angular), wheel (suspension): oscillations per
        // second and damping ratio (1 = critical). 0 Hz: rigid (a wheel's suspension: free).
        float Frequency = 0.0f;
        float DampingRatio = 0.0f;
        // Limits: revolute (degrees, the body's rotation relative to the connected body from the
        // pose the joint was made in), prismatic / wheel (units along the axis).
        bool EnableLimit = false;
        float LowerLimit = 0.0f;
        float UpperLimit = 0.0f;
        // Motors: revolute / wheel (degrees per second, max torque), prismatic (units per second,
        // max force). Positive turns the body counter-clockwise / moves it along +Axis.
        bool EnableMotor = false;
        float MotorSpeed = 0.0f;
        float MaxMotorForce = 0.0f;
        // Prismatic (slide direction) / wheel (suspension direction), in Body's frame.
        glm::vec2 Axis{ 1.0f, 0.0f };
    };

    // What a joint does now (inspector read-outs, breaking).
    struct PhysicsJoint2DState
    {
        float Force = 0.0f;       // the reaction force's magnitude (N)
        float Torque = 0.0f;      // the reaction torque's magnitude (N*m)
        float Angle = 0.0f;       // revolute: degrees from the start pose
        float Translation = 0.0f; // prismatic / wheel: units along the axis; distance: the current length
        float Speed = 0.0f;       // revolute / wheel: degrees per second; prismatic: units per second
    };

    // A joint taken down together with a body (DestroyBody): which component it belonged to.
    struct PhysicsJoint2DRef
    {
        UUID EntityID = 0;
        Joint2DType Type = Joint2DType::Distance;
    };

    // A point where two 2D bodies touch (debug drawing) and the entity of the first fixture.
    struct PhysicsContactPoint2D
    {
        glm::vec2 Position{ 0.0f };
        glm::vec2 Normal{ 0.0f }; // out of the static / kinematic body (A to B between two dynamic ones)
        UUID EntityA = 0;
    };

    // 2D rigid body simulation (Box2D): bodies, fixtures, joints, contact events, one-way surfaces
    // and queries. Box2D types are hidden from users (bodies / fixtures / joints are opaque handles).
    class PhysicsWorld2D
    {
    public:
        explicit PhysicsWorld2D(const glm::vec2& gravity);
        ~PhysicsWorld2D();

        PhysicsWorld2D(const PhysicsWorld2D&) = delete;
        PhysicsWorld2D& operator=(const PhysicsWorld2D&) = delete;

        // ---- bodies
        b2Body* CreateBody(const PhysicsBody2DDesc& desc);
        // The (first) fixture, or null when the shape has nothing Box2D can use (a polygon without
        // area, an edge without length...).
        b2Fixture* AddShape(b2Body* body, const PhysicsShape2DDesc& shape);
        // New friction / bounciness / density for the fixtures of `body` made with `tag` (a physics
        // material changed): touching contacts take the new values at once, the mass follows.
        void SetShapeMaterial(b2Body* body, int tag, const PhysicsMaterialData& material);
        static int GetFixtureCount(const b2Body* body);
        // The surface a fixture has now: friction, restitution, threshold, density and combine modes.
        PhysicsMaterialData GetShapeMaterial(const b2Fixture* fixture) const;
        // A fixture's filter bits (tests, tools).
        static void GetShapeFilter(const b2Fixture* fixture, uint16_t& categoryBits, uint16_t& maskBits);
        // Its fixtures and joints go with it; the joints are added to `destroyedJoints` (when given).
        void DestroyBody(b2Body* body, std::vector<PhysicsJoint2DRef>* destroyedJoints = nullptr);
        // Box2D does not wake sleeping bodies that a new body appears on or in (a platform or tilemap
        // switched on again, a spawned wall): they would stay inside it until something else wakes them.
        void WakeBodiesOverlapping(b2Body* body);

        static glm::vec2 GetPosition(const b2Body* body);
        static float GetAngle(const b2Body* body);
        static void SetTransform(b2Body* body, const glm::vec2& position, float angle);
        static glm::vec2 GetLinearVelocity(const b2Body* body);
        static glm::vec2 GetWorldCenter(const b2Body* body);
        static bool IsDynamic(const b2Body* body);
        static bool IsAwake(const b2Body* body);
        static bool IsEnabled(const b2Body* body);
        static void SetAwake(b2Body* body, bool awake);

        // ---- simulation
        void Step(float dt, int velocityIterations, int positionIterations);
        // Collision begin/end events gathered since the last call.
        std::vector<ContactEvent> TakeEvents();
        // `body` (of `entity`) falls through every one-way surface for `seconds` (and for as long as it
        // is still inside one), counted down by Step.
        void DropThroughPlatforms(UUID entity, b2Body* body, float seconds);

        // ---- joints
        // Null when the description cannot make one (no body). The auto-configured values (connected
        // anchor, distance) are written back into `desc`.
        b2Joint* CreateJoint(PhysicsJoint2DDesc& desc);
        void DestroyJoint(b2Joint* joint);
        // Destroys the joints of `entity` of `type` except `joint`, which stays null when it is not one
        // of them (a component holding a copy of another one's handle).
        void ClaimJoint(UUID entity, Joint2DType type, b2Joint*& joint);
        // The settings that can change while the joint lives: limits, motor, springs, distance (the
        // anchors and the axis stay as they were made). Bodies wake up only when something changed.
        void UpdateJoint(b2Joint* joint, const PhysicsJoint2DDesc& desc);
        // `invDt`: 1 / the last step's time (the reaction forces come from its impulses).
        static PhysicsJoint2DState GetJointState(const b2Joint* joint, float invDt);
        // The world positions of the two anchors (the joint's own body first).
        static void GetJointAnchors(const b2Joint* joint, glm::vec2& anchor, glm::vec2& connectedAnchor);
        size_t GetJointCount() const;

        // ---- queries
        // The closest non-sensor fixture along the ray whose layer is in `layerMask` (category bits).
        RaycastHit CastRay(const glm::vec2& origin, const glm::vec2& direction, float maxDistance, uint16_t layerMask = 0xFFFF) const;
        // The points of the touching, enabled, non-sensor contacts as they are now.
        void GetDebugContacts(std::vector<PhysicsContactPoint2D>& out) const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };

}
