#pragma once

#include "Zamin/Core/Base.h"
#include "Zamin/Physics/PhysicsTypes.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>
#include <memory>
#include <vector>

namespace Zamin {

    struct ConvexHull3D;
    struct CollisionMesh3D;

    struct PhysicsBody3DDesc
    {
        UUID EntityID = 0;
        int MotionType = 1; // 0 static, 1 dynamic, 2 kinematic
        glm::vec3 Position{ 0.0f };
        glm::quat Rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
        float Mass = 1.0f;
        float LinearDamping = 0.05f;
        float AngularDamping = 0.05f;
        float GravityFactor = 1.0f;
        bool LockRotation = false;
        // The collision layer (CollisionLayers2D: the project's 16 layers, shared with 2D physics).
        int Layer = 0;

        struct Shape
        {
            enum class Kind { Box, Sphere, Capsule, ConvexHull, Mesh } Type = Kind::Box;
            glm::vec3 HalfExtents{ 0.5f };
            float Radius = 0.5f;
            float HalfHeight = 0.5f;
            // ConvexHull: the hull (CollisionShapes3D) in the entity's unscaled frame, times Scale.
            // Each world makes the Jolt shape of a hull once and shares it between its bodies.
            Ref<const ConvexHull3D> Hull;
            // Mesh: the triangles (CollisionShapes3D::GetMesh) in the entity's unscaled frame, times
            // Scale - static and kinematic bodies only (a dynamic body leaves it out). The Jolt mesh
            // shape of a mesh is made once and shared by every world while the mesh lives.
            Ref<const CollisionMesh3D> Mesh;
            glm::vec3 Scale{ 1.0f };
            glm::vec3 Offset{ 0.0f };
            float Friction = 0.5f;
            float Restitution = 0.0f;
            bool IsSensor = false;
        };
        std::vector<Shape> Shapes;
    };

    // A joint between a body (Jolt's body 2: the entity the joint component belongs to) and a
    // connected body (body 1) or a fixed point in the world, made where the bodies are now: that pose
    // is the hinge's angle 0 and the slider's position 0. Points and axes in world space, angles in
    // degrees.
    struct PhysicsJoint3DDesc
    {
        static constexpr uint32_t NoBody = 0xffffffff;

        Joint3DType Type = Joint3DType::Fixed;
        UUID EntityID = 0;                  // the joint's entity (reported when a body goes)
        uint32_t Body = NoBody;
        uint32_t ConnectedBody = NoBody;    // NoBody: anchored to the world
        glm::vec3 Anchor{ 0.0f };           // the point on Body
        glm::vec3 ConnectedAnchor{ 0.0f };  // the point on ConnectedBody (or in the world) it is held to
        glm::vec3 Axis{ 0.0f, 1.0f, 0.0f }; // hinge: what it turns around; slider: where it moves along
        bool CollideConnected = false;

        // Limits: hinge (degrees, lower in -180..0 and upper in 0..180), slider (units along the
        // axis, lower <= 0 <= upper). The other values are clamped to that.
        bool EnableLimit = false;
        float LowerLimit = 0.0f;
        float UpperLimit = 0.0f;
        // Motors: hinge (degrees per second, max torque N*m), slider (units per second, max force N).
        // Positive turns Body counter-clockwise around the axis (right hand) / moves it along +Axis.
        bool EnableMotor = false;
        float MotorSpeed = 0.0f;
        float MaxMotorForce = 0.0f;
        // Hinge spring (used while the motor is off): pulls Body towards SpringTarget (degrees).
        bool EnableSpring = false;
        float SpringTarget = 0.0f;
        // Spring oscillations per second and damping ratio (1 = critical): the hinge's spring, the
        // distance joint's soft ends (0 Hz: rigid).
        float Frequency = 0.0f;
        float DampingRatio = 0.0f;
        // Distance: the anchors are kept from MinDistance to MaxDistance apart.
        float MinDistance = 0.0f;
        float MaxDistance = 1.0f;
    };

    // What a 3D joint does now (inspector read-outs, breaking).
    struct PhysicsJoint3DState
    {
        float Force = 0.0f;       // the reaction force's magnitude (N), motors not counted
        float Torque = 0.0f;      // the reaction torque's magnitude (N*m), motors not counted
        float Angle = 0.0f;       // hinge: degrees from the start pose
        float Translation = 0.0f; // slider: units along the axis; distance: the anchors' distance
        float Speed = 0.0f;       // hinge: degrees per second; slider: units per second
    };

    // A joint taken down together with a body (DestroyBody): which component it belonged to.
    struct PhysicsJoint3DRef
    {
        UUID EntityID = 0;
        Joint3DType Type = Joint3DType::Fixed;
    };

    struct CharacterDesc
    {
        UUID EntityID = 0;
        glm::vec3 Position{ 0.0f };   // feet position
        float Radius = 0.35f;
        float Height = 1.8f;          // total height (capsule)
        float MaxSlopeDegrees = 50.0f;
        float Mass = 70.0f;
        int Layer = 0;                // collision layer (CollisionLayers2D)
    };

    // 3D rigid body simulation (Jolt Physics). Jolt types are hidden from users.
    class PhysicsWorld3D
    {
    public:
        explicit PhysicsWorld3D(const glm::vec3& gravity);
        ~PhysicsWorld3D();

        PhysicsWorld3D(const PhysicsWorld3D&) = delete;
        PhysicsWorld3D& operator=(const PhysicsWorld3D&) = delete;

        // Returns the body id (0xffffffff on failure).
        uint32_t CreateBody(const PhysicsBody3DDesc& desc);
        // Sleeping bodies that rested on it wake up (and fall when nothing else holds them). Its joints
        // go with it; they are added to `destroyedJoints` (when given).
        void DestroyBody(uint32_t bodyID, std::vector<PhysicsJoint3DRef>* destroyedJoints = nullptr);
        // Wakes the sleeping bodies around this body (one that just appeared where they rest:
        // Jolt would leave them inside it until something else wakes them).
        void WakeBodiesAround(uint32_t bodyID);
        void OptimizeBroadPhase();

        void Step(float dt);

        void GetTransform(uint32_t bodyID, glm::vec3& position, glm::quat& rotation) const;
        void SetTransform(uint32_t bodyID, const glm::vec3& position, const glm::quat& rotation);
        void MoveKinematic(uint32_t bodyID, const glm::vec3& position, const glm::quat& rotation, float dt);

        glm::vec3 GetLinearVelocity(uint32_t bodyID) const;
        void SetLinearVelocity(uint32_t bodyID, const glm::vec3& velocity);
        glm::vec3 GetAngularVelocity(uint32_t bodyID) const;
        void SetAngularVelocity(uint32_t bodyID, const glm::vec3& velocity);
        void AddImpulse(uint32_t bodyID, const glm::vec3& impulse);
        void AddForce(uint32_t bodyID, const glm::vec3& force);
        void AddTorque(uint32_t bodyID, const glm::vec3& torque);
        // Simulated now (false: asleep, static, or not in this world).
        bool IsBodyActive(uint32_t bodyID) const;

        // ---- kinematic character controllers (Jolt CharacterVirtual)
        uint32_t CreateCharacter(const CharacterDesc& desc);
        // Its slot is reused by the next CreateCharacter; bodies resting on it wake up.
        void DestroyCharacter(uint32_t id);
        // Moves the character with `velocity` (includes vertical speed) for dt seconds.
        void UpdateCharacter(uint32_t id, const glm::vec3& velocity, float dt, float stepHeight, bool stickToFloor);
        glm::vec3 GetCharacterPosition(uint32_t id) const;
        void SetCharacterPosition(uint32_t id, const glm::vec3& position);
        glm::vec3 GetCharacterVelocity(uint32_t id) const;
        glm::vec3 GetCharacterGroundVelocity(uint32_t id) const;
        bool IsCharacterGrounded(uint32_t id) const;

        // ---- collision layers: the project's 16 named layers and their matrix (CollisionLayers2D,
        // shared with 2D physics). Bit b of masks[a]: bodies on layers a and b collide (symmetric).
        // Every body / character is on one layer. Takes effect from the next step.
        void SetCollisionMatrix(const std::array<uint16_t, 16>& masks);
        bool CanLayersCollide(int a, int b) const;
        // Moves a body to another layer (nothing when it is on it already).
        void SetBodyLayer(uint32_t bodyID, int layer);
        int GetBodyLayer(uint32_t bodyID) const; // -1: not in this world
        void SetCharacterLayer(uint32_t id, int layer);
        int GetCharacterLayer(uint32_t id) const;

        // ---- queries. layerMask: the layers that count (bits, CollisionLayers2D::GetCategoryBits);
        // triggers (sensor bodies) only with includeSensors. A ray / shape that starts inside a
        // collider does not hit it (its own body, a wall it touches); triangles of mesh colliders
        // are hit from their front side only.
        // The closest hit along the ray.
        RaycastHit CastRay(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, uint16_t layerMask = 0xFFFF,
                           bool includeSensors = false) const;
        // Every entity the ray hits (its first hit along the ray), the closest first (ties: by ID).
        std::vector<RaycastHit> CastRayAll(const glm::vec3& origin, const glm::vec3& direction, float maxDistance,
                                           uint16_t layerMask = 0xFFFF, bool includeSensors = false) const;
        // The entities with a body that overlaps the shape (at `center`, turned by `rotation`): exact
        // shape tests (mesh colliders: their triangles - a shape wholly inside a closed mesh does not
        // touch it). Each entity once, the nearest body position to `center` first (ties: by ID).
        std::vector<UUID> Overlap(const PhysicsQueryShape3D& shape, const glm::vec3& center, const glm::quat& rotation,
                                  uint16_t layerMask = 0xFFFF, bool includeSensors = false) const;
        // The shape swept from `origin` along `direction` for `maxDistance`: the first body it touches.
        // Distance: how far the shape went; Point: where it touches the body; Normal: that body's
        // surface there (pointing back at the shape).
        RaycastHit CastShape(const PhysicsQueryShape3D& shape, const glm::vec3& origin, const glm::quat& rotation, const glm::vec3& direction,
                             float maxDistance, uint16_t layerMask = 0xFFFF, bool includeSensors = false) const;
        // Every entity on the way (its first touch), the closest first (ties: by ID).
        std::vector<RaycastHit> CastShapeAll(const PhysicsQueryShape3D& shape, const glm::vec3& origin, const glm::quat& rotation,
                                             const glm::vec3& direction, float maxDistance, uint16_t layerMask = 0xFFFF,
                                             bool includeSensors = false) const;

        // Jolt mesh shapes alive in the shared cache (mesh colliders; tests).
        static size_t GetMeshShapeCacheSize();

        // ---- joints (Jolt constraints). Handles are never reused: 0 = none.
        // 0 when the description cannot make one (no / the same body, a body not in this world).
        // Both bodies wake up.
        uint32_t CreateJoint(const PhysicsJoint3DDesc& desc);
        void DestroyJoint(uint32_t joint);
        // Destroys the joints of `entity` of `type` except `joint`, which becomes 0 when it is not one
        // of them (a component holding a copy of another one's handle).
        void ClaimJoint(UUID entity, Joint3DType type, uint32_t& joint);
        // The settings that can change while the joint lives: limits, motor, spring, distances (the
        // anchors and the axis stay as they were made). Bodies wake up only when something changed.
        void UpdateJoint(uint32_t joint, const PhysicsJoint3DDesc& desc);
        // `invDt`: 1 / the last step's time (the reaction forces come from its impulses).
        PhysicsJoint3DState GetJointState(uint32_t joint, float invDt) const;
        // Where the two anchors are now (the joint's own body first) and the hinge / slider axis in the
        // world. False for an unknown handle.
        bool GetJointFrame(uint32_t joint, glm::vec3& anchor, glm::vec3& connectedAnchor, glm::vec3& axis) const;
        bool IsJoint(uint32_t joint) const;
        size_t GetJointCount() const;
        // Whether the two bodies' shapes may touch (false while a joint between them has
        // CollideConnected off).
        bool CanBodiesCollide(uint32_t bodyA, uint32_t bodyB) const;

        // Collision begin/end events gathered since the last call.
        std::vector<ContactEvent> TakeEvents();

        // ---- contact points for debug drawing (off by default: costs a lock per contact)
        // On: every step records the contact points of the touching shapes; the contacts of
        // sleeping bodies are kept (Jolt stops reporting them) until a body wakes up or goes.
        void SetDebugContactsEnabled(bool enabled);
        bool IsDebugContactsEnabled() const;
        // The recorded points and those of the characters, appended to `out`. The normals point out
        // of the static / kinematic body; between two dynamic bodies they are Jolt's: from the
        // first body toward the second (the way the second one is pushed out).
        void GetDebugContacts(std::vector<PhysicsContactPoint>& out) const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };

}
