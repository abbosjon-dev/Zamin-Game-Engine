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
        // Sleeping (Rigidbody2DComponent::Sleeping): CanSleep false - never sleeps; StartAwake false -
        // starts asleep (not simulated until something touches it, a joint pulls it or it is woken).
        bool CanSleep = true;
        bool StartAwake = true;
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

    // An area effector (AreaEffector2DComponent) for one physics step: what it does to every dynamic
    // body that overlaps one of its fixtures (wind, a jet of air, a current).
    struct PhysicsAreaEffector2D
    {
        glm::vec2 Force{ 0.0f };   // world direction x magnitude: N (per kg when ScaleWithMass)
        float Variation = 0.0f;    // a random amount in [-Variation, Variation] added to the magnitude (gusts)
        bool ScaleWithMass = false; // the force is per kg: every body gets the same acceleration
        float Drag = 0.0f;         // linear / angular drag inside (1/s, like Rigidbody 2D damping)
        float AngularDrag = 0.0f;
        uint16_t LayerMask = 0xFFFF; // the bodies' collider layers (category bits) it acts on
    };

    // A buoyancy effector (BuoyancyEffector2DComponent): its fixtures are a fluid up to the surface.
    struct PhysicsBuoyancy2D
    {
        // The surface: this far above the effector body's position along the world's up (against
        // gravity; +Y without gravity). The fluid is the part of the fixtures below it.
        float SurfaceLevel = 0.0f;
        float Density = 1.0f;      // a body whose colliders are less dense floats
        float LinearDrag = 1.0f;   // (1/s) times the submerged part of the body
        float AngularDrag = 1.0f;
        glm::vec2 Flow{ 0.0f };    // world force on a fully submerged body (a current)
        float FlowVariation = 0.0f;
        uint16_t LayerMask = 0xFFFF;
    };

    // What an effector did in a step.
    struct PhysicsEffectorResult2D
    {
        int Bodies = 0;            // dynamic bodies it acted on
        float SubmergedArea = 0.0f; // buoyancy: the area under the surface, over all of them
    };

    // A point where two 2D bodies touch (debug drawing, tests).
    struct PhysicsContactPoint2D
    {
        glm::vec2 Position{ 0.0f };
        glm::vec2 Normal{ 0.0f }; // out of EntityA's surface: the static / kinematic body (A to B between two dynamic ones)
        float Penetration = 0.0f; // how far the shapes overlap there (Box2D keeps a little: its linear slop)
        float Impulse = 0.0f;     // Box2D's normal impulse of the last step (N*s)
        UUID EntityA = 0, EntityB = 0;
        bool Sleeping = false;    // both bodies sleep
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
        static void SetLinearVelocity(b2Body* body, const glm::vec2& velocity);
        // A force on the body's centre for the next step (wakes it; Box2D clears it after the step).
        static void ApplyForceToCenter(b2Body* body, const glm::vec2& force);
        static float GetAngularVelocity(const b2Body* body); // radians per second
        static glm::vec2 GetWorldCenter(const b2Body* body);
        static bool IsDynamic(const b2Body* body);
        static bool IsAwake(const b2Body* body);
        static bool IsEnabled(const b2Body* body);
        // Wakes the body / puts it to sleep. Either way its time of keeping still starts again
        // (Box2D's and, with the world's own sleep timing, this world's): woken, it has the whole
        // sleep time before it may sleep again.
        void SetAwake(b2Body* body, bool awake);

        // ---- simulation
        // One Box2D step of `dt` seconds (Scene: the fixed step / the sub-step count).
        void Step(float dt, int velocityIterations, int positionIterations);
        // Collision begin/end events gathered since the last call.
        std::vector<ContactEvent> TakeEvents();
        // `body` (of `entity`) falls through every one-way surface for `seconds` (and for as long as it
        // is still inside one), counted down by Step.
        void DropThroughPlatforms(UUID entity, b2Body* body, float seconds);

        // ---- sleeping
        // Box2D's own time a body has to keep still (together with every body touching it or joined
        // to it, not counting static ones) before it sleeps.
        static constexpr float DefaultTimeToSleep = 0.5f;
        // allowSleep false: no body sleeps (the sleeping ones wake up). timeToSleep: the seconds of
        // keeping still - Box2D counts its own 0.5 s; for any other time this world counts it, island
        // by island like Box2D (velocities under Box2D's sleep tolerances). Changing between the two
        // wakes every body. Switching sleeping off and on again starts every body's time of keeping
        // still again.
        void SetSleeping(bool allowSleep, float timeToSleep);
        bool IsSleepingAllowed() const;
        float GetTimeToSleep() const;
        // Whether the body may sleep (Rigidbody2DComponent::Sleeping); false wakes it.
        void SetCanSleep(b2Body* body, bool canSleep);
        bool CanSleep(const b2Body* body) const;
        // Bodies, sleeping ones, joints, contacts, steps.
        PhysicsStats2D GetStats() const;

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

        // ---- effectors (called before each step: Box2D clears the forces after it)
        // `effector`'s fixtures push the dynamic bodies overlapping them (sensors) / touching them
        // (solid fixtures); its own body is left alone, and so are sensor fixtures of the others.
        PhysicsEffectorResult2D ApplyAreaEffector(b2Body* effector, const PhysicsAreaEffector2D& settings, float dt);
        // Buoyancy: every overlapping body gets the weight of the fluid it displaces (Density x the
        // area of its colliders under the surface and inside the fixture x gravity) against gravity at
        // the centre of that part, the drags and the flow in proportion to how much of it is under.
        // Edges / chains have no area: they do not float.
        PhysicsEffectorResult2D ApplyBuoyancy(b2Body* effector, const PhysicsBuoyancy2D& settings, float dt);
        // The world's gravity (what buoyancy pushes against).
        glm::vec2 GetGravity() const;

        // ---- queries
        // The closest non-sensor fixture along the ray whose layer is in `layerMask` (category bits).
        RaycastHit CastRay(const glm::vec2& origin, const glm::vec2& direction, float maxDistance, uint16_t layerMask = 0xFFFF) const;
        // Every entity the ray hits (its first hit along the ray), the closest first (ties: by ID).
        // A ray that starts inside a collider does not hit it.
        std::vector<RaycastHit> CastRayAll(const glm::vec2& origin, const glm::vec2& direction, float maxDistance,
                                           uint16_t layerMask = 0xFFFF, bool includeSensors = false) const;
        // The entities with a fixture (on a layer in `layerMask`) that overlaps the circle / the box
        // (half extents, turned by `angle` radians) / contains the point - exact shape tests, edges and
        // chains count for the circle and box only. Each entity once, the nearest body position to
        // the query's centre first (ties: by ID).
        std::vector<UUID> OverlapCircle(const glm::vec2& center, float radius, uint16_t layerMask = 0xFFFF, bool includeSensors = false) const;
        std::vector<UUID> OverlapBox(const glm::vec2& center, const glm::vec2& halfExtents, float angle,
                                     uint16_t layerMask = 0xFFFF, bool includeSensors = false) const;
        std::vector<UUID> OverlapPoint(const glm::vec2& point, uint16_t layerMask = 0xFFFF, bool includeSensors = false) const;
        // The points of the touching, enabled, non-sensor contacts as they are now.
        void GetDebugContacts(std::vector<PhysicsContactPoint2D>& out) const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };

}
