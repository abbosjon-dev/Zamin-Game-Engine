#pragma once

/// @file
/// @brief PhysicsWorld3D: the 3D physics world (Jolt) - bodies, character controllers, joints, layers, queries.

#include "Zamin/Core/Base.h"
#include "Zamin/Physics/PhysicsMaterial.h"
#include "Zamin/Physics/PhysicsTypes.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>
#include <memory>
#include <optional>
#include <vector>

namespace Zamin {

    struct ConvexHull3D;
    struct CollisionMesh3D;

    /// How a 3D collider rubs and bounces against others: its own values or a physics material asset's
    /// (.zphysmat, shared with 2D). Two touching colliders combine theirs as PhysicsMaterial::Combine
    /// says (Default: friction sqrt(a * b), bounce max(a, b) - Jolt's own rule).
    struct PhysicsSurface3D
    {
        /// Jolt's own threshold: slower impacts (m/s along the contact normal) do not bounce.
        static constexpr float DefaultRestitutionThreshold = 1.0f;

        float Friction = 0.5f;
        float Restitution = 0.0f;
        float RestitutionThreshold = DefaultRestitutionThreshold;
        PhysicsCombine FrictionCombine = PhysicsCombine::Default;
        PhysicsCombine RestitutionCombine = PhysicsCombine::Default;

        /// A material's values (its density is not used: a 3D body's mass is the Rigidbody 3D's).
        static PhysicsSurface3D FromMaterial(const PhysicsMaterialData& material);
        bool operator==(const PhysicsSurface3D&) const = default;
    };

    struct PhysicsBody3DDesc
    {
        UUID EntityID = 0;
        int MotionType = 1; ///< 0 static, 1 dynamic, 2 kinematic
        glm::vec3 Position{ 0.0f };
        glm::quat Rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
        float Mass = 1.0f;
        float LinearDamping = 0.05f;
        float AngularDamping = 0.05f;
        float GravityFactor = 1.0f;
        bool LockRotation = false;
        /// The collision layer (CollisionLayers2D: the project's 16 layers, shared with 2D physics).
        int Layer = 0;
        /// Continuous collision detection (Jolt's linear cast motion quality, dynamic bodies): a fast
        /// body is swept along its path every step, so it stops at a thin wall instead of passing it.
        bool ContinuousCollision = false;
        /// Sleeping: AllowSleeping false - never sleeps; StartAsleep - a dynamic / kinematic body starts
        /// asleep (not simulated until something touches it, a joint pulls it or it is woken).
        bool AllowSleeping = true;
        bool StartAsleep = false;

        struct Shape
        {
            enum class Kind { Box, Sphere, Capsule, ConvexHull, Mesh } Type = Kind::Box;
            glm::vec3 HalfExtents{ 0.5f };
            float Radius = 0.5f;
            float HalfHeight = 0.5f;
            /// ConvexHull: the hull (CollisionShapes3D) in the entity's unscaled frame, times Scale.
            /// Each world makes the Jolt shape of a hull once and shares it between its bodies.
            Ref<const ConvexHull3D> Hull;
            /// Mesh: the triangles (CollisionShapes3D::GetMesh) in the entity's unscaled frame, times
            /// Scale - static and kinematic bodies only (a dynamic body leaves it out). The Jolt mesh
            /// shape of a mesh is made once and shared by every world while the mesh lives.
            Ref<const CollisionMesh3D> Mesh;
            glm::vec3 Scale{ 1.0f };
            glm::vec3 Offset{ 0.0f };
            PhysicsSurface3D Surface;
            /// A trigger: reports contacts (OnTriggerBegin / End) without colliding. A body may mix
            /// trigger and solid colliders.
            bool IsSensor = false;
            /// Names the collider among the body's (SetShapeSurface); the scene uses its component kind.
            int Tag = 0;
        };
        std::vector<Shape> Shapes;
    };

    /// A joint between a body (Jolt's body 2: the entity the joint component belongs to) and a
    /// connected body (body 1) or a fixed point in the world, made where the bodies are now: that pose
    /// is the hinge's angle 0 and the slider's position 0. Points and axes in world space, angles in
    /// degrees.
    struct PhysicsJoint3DDesc
    {
        static constexpr uint32_t NoBody = 0xffffffff;

        Joint3DType Type = Joint3DType::Fixed;
        UUID EntityID = 0;                  ///< the joint's entity (reported when a body goes)
        uint32_t Body = NoBody;
        uint32_t ConnectedBody = NoBody;    ///< NoBody: anchored to the world
        glm::vec3 Anchor{ 0.0f };           ///< the point on Body
        glm::vec3 ConnectedAnchor{ 0.0f };  ///< the point on ConnectedBody (or in the world) it is held to
        /// hinge: what it turns around; slider: where it moves along; cone: what it twists around
        glm::vec3 Axis{ 0.0f, 1.0f, 0.0f };
        bool CollideConnected = false;

        /// Limits: hinge (degrees, lower in -180..0 and upper in 0..180), slider (units along the
        /// axis, lower <= 0 <= upper), cone (the twist: degrees like the hinge's, always on). The other
        /// values are clamped to that.
        bool EnableLimit = false;
        float LowerLimit = 0.0f;
        float UpperLimit = 0.0f;
        /// Motors: hinge (degrees per second, max torque N*m), slider (units per second, max force N).
        /// Positive turns Body counter-clockwise around the axis (right hand) / moves it along +Axis.
        bool EnableMotor = false;
        float MotorSpeed = 0.0f;
        float MaxMotorForce = 0.0f;
        /// Hinge spring (used while the motor is off): pulls Body towards SpringTarget (degrees).
        bool EnableSpring = false;
        float SpringTarget = 0.0f;
        /// Spring oscillations per second and damping ratio (1 = critical): the hinge's spring, the
        /// distance joint's soft ends (0 Hz: rigid).
        float Frequency = 0.0f;
        float DampingRatio = 0.0f;
        /// Distance: the anchors are kept from MinDistance to MaxDistance apart.
        float MinDistance = 0.0f;
        float MaxDistance = 1.0f;
        /// Cone: how far (degrees, 0..180) the axis may swing away from where it was made; the torque
        /// (N*m) that resists turning.
        float SwingLimit = 45.0f;
        float FrictionTorque = 0.0f;
    };

    /// What a 3D joint does now (inspector read-outs, breaking).
    struct PhysicsJoint3DState
    {
        float Force = 0.0f;       ///< the reaction force's magnitude (N), motors not counted
        float Torque = 0.0f;      ///< the reaction torque's magnitude (N*m), motors not counted
        float Angle = 0.0f;       ///< hinge: degrees from the start pose; cone: degrees the axis swung
        float Twist = 0.0f;       ///< cone: degrees of twist around the axis
        float Translation = 0.0f; ///< slider: units along the axis; distance: the anchors' distance
        float Speed = 0.0f;       ///< hinge: degrees per second; slider: units per second; cone: degrees per second
    };

    /// A joint taken down together with a body (DestroyBody): which component it belonged to.
    struct PhysicsJoint3DRef
    {
        UUID EntityID = 0;
        Joint3DType Type = Joint3DType::Fixed;
    };

    struct CharacterDesc
    {
        /// Jolt's own push strength (CharacterVirtualSettings::mMaxStrength).
        static constexpr float DefaultPushForce = 100.0f;

        UUID EntityID = 0;
        glm::vec3 Position{ 0.0f };   ///< feet position
        float Radius = 0.35f;
        float Height = 1.8f;          ///< total height (capsule)
        float MaxSlopeDegrees = 50.0f;
        float Mass = 70.0f;
        int Layer = 0;                ///< collision layer (CollisionLayers2D)
        /// Walking into a dynamic body pushes it (towards the character's speed at the contact) with at
        /// most PushForce newtons; PushBodies false - it is a wall to the character.
        bool PushBodies = true;
        float PushForce = DefaultPushForce;
    };


    /// 3D rigid body simulation (Jolt Physics). Jolt types are hidden from users.
    ///
    /// A Scene owns one while it plays or simulates: OnRuntimeStart makes a body for every active
    /// entity with a Rigidbody 3D and a 3D collider (and a character for a Character Controller 3D),
    /// the scene steps it in fixed steps (SceneSettings::PhysicsStep) and writes the bodies' transforms
    /// back to the entities; collisions come back as ContactEvent (TakeEvents) and reach the scripts as
    /// OnCollisionBegin / OnTriggerBegin ... Bodies, characters and joints are numbered handles.
    /// Scripts use ScriptableEntity's 3D functions (SetVelocity3D, Raycast3D ...) rather than this class.
    class PhysicsWorld3D
    {
    public:
        explicit PhysicsWorld3D(const glm::vec3& gravity);  ///< an empty world with this gravity (m/s^2)
        ~PhysicsWorld3D();

        PhysicsWorld3D(const PhysicsWorld3D&) = delete;
        PhysicsWorld3D& operator=(const PhysicsWorld3D&) = delete;

        /// Returns the body id (0xffffffff on failure).
        uint32_t CreateBody(const PhysicsBody3DDesc& desc);
        /// Sleeping bodies that rested on it wake up (and fall when nothing else holds them). Its joints
        /// go with it; they are added to `destroyedJoints` (when given).
        void DestroyBody(uint32_t bodyID, std::vector<PhysicsJoint3DRef>* destroyedJoints = nullptr);
        /// Wakes the sleeping bodies around this body (one that just appeared where they rest:
        /// Jolt would leave them inside it until something else wakes them).
        void WakeBodiesAround(uint32_t bodyID);
        /// Rebuilds Jolt's broad phase tree after many bodies were added (the scene does it at play start).
        void OptimizeBroadPhase();

        /// Advances the simulation by `dt` seconds (one fixed step); contacts found are kept for TakeEvents.
        void Step(float dt);

        /// The body's position and rotation in the world.
        void GetTransform(uint32_t bodyID, glm::vec3& position, glm::quat& rotation) const;
        /// Teleports the body (and wakes it).
        void SetTransform(uint32_t bodyID, const glm::vec3& position, const glm::quat& rotation);
        /// A kinematic body: the velocity that reaches this position and rotation in `dt` (it pushes what
        /// is in the way, unlike SetTransform).
        void MoveKinematic(uint32_t bodyID, const glm::vec3& position, const glm::quat& rotation, float dt);

        glm::vec3 GetLinearVelocity(uint32_t bodyID) const;                    ///< m/s
        void SetLinearVelocity(uint32_t bodyID, const glm::vec3& velocity);
        glm::vec3 GetAngularVelocity(uint32_t bodyID) const;                   ///< radians / s around each axis
        void SetAngularVelocity(uint32_t bodyID, const glm::vec3& velocity);
        void AddImpulse(uint32_t bodyID, const glm::vec3& impulse);  ///< at the centre of mass: the velocity changes at once
        void AddForce(uint32_t bodyID, const glm::vec3& force);      ///< for the next step only (N)
        void AddTorque(uint32_t bodyID, const glm::vec3& torque);    ///< for the next step only (N m)
        /// Simulated now (false: asleep, static, or not in this world).
        bool IsBodyActive(uint32_t bodyID) const;

        /// @name Continuous collision detection
        /// Continuous collision detection (PhysicsBody3DDesc::ContinuousCollision)
        /// @{
        void SetBodyContinuous(uint32_t bodyID, bool continuous);
        bool IsBodyContinuous(uint32_t bodyID) const;
        /// @}

        /// @name Sleeping
        /// Sleeping. A moving body that keeps still for TimeToSleep seconds (with everything
        /// touching / joined to it) is not simulated until something wakes it: a body that is awake
        /// touches it, a joint pulls it, it is moved, pushed or woken here.
        /// allowSleep false: no body sleeps (the sleeping ones wake up). Jolt's own time: 0.5 s.
        /// @{
        void SetSleeping(bool allowSleep, float timeToSleep);
        bool IsSleepingAllowed() const;
        float GetTimeToSleep() const;
        /// A dynamic / kinematic body of this world that sleeps now.
        bool IsBodySleeping(uint32_t bodyID) const;
        /// Wakes the body / puts it to sleep at once (its velocities stop; it wakes again when hit).
        /// False for a static body or one not in this world.
        bool SetBodyAwake(uint32_t bodyID, bool awake);
        /// Whether the body may sleep (PhysicsBody3DDesc::AllowSleeping); false wakes it.
        void SetBodyAllowSleeping(uint32_t bodyID, bool allow);
        bool GetBodyAllowSleeping(uint32_t bodyID) const;
        PhysicsStats3D GetStats() const;
        /// @}

        /// @name Surfaces
        /// Surfaces (physics materials): the collider `tag` of the body (PhysicsBody3DDesc::Shape::Tag)
        /// gets new values, used from the next step. False when the body has no such collider.
        /// @{
        bool SetShapeSurface(uint32_t bodyID, int tag, const PhysicsSurface3D& surface);
        /// What the collider touches with now (nullopt: no such body / collider).
        std::optional<PhysicsSurface3D> GetShapeSurface(uint32_t bodyID, int tag) const;
        /// @}

        /// @name Kinematic character controllers
        /// Kinematic character controllers (Jolt CharacterVirtual)
        /// @{
        /// A capsule character (Character Controller 3D) at desc's position; returns its handle.
        uint32_t CreateCharacter(const CharacterDesc& desc);
        /// Its slot is reused by the next CreateCharacter; bodies resting on it wake up.
        void DestroyCharacter(uint32_t id);
        /// Moves the character with `velocity` (includes vertical speed) for dt seconds.
        void UpdateCharacter(uint32_t id, const glm::vec3& velocity, float dt, float stepHeight, bool stickToFloor);
        glm::vec3 GetCharacterPosition(uint32_t id) const;
        void SetCharacterPosition(uint32_t id, const glm::vec3& position);  ///< teleports it
        glm::vec3 GetCharacterVelocity(uint32_t id) const;                  ///< after the last UpdateCharacter
        glm::vec3 GetCharacterGroundVelocity(uint32_t id) const;            ///< of what it stands on (a moving platform)
        bool IsCharacterGrounded(uint32_t id) const;                        ///< standing on walkable ground
        /// How it pushes dynamic bodies (CharacterDesc::PushBodies / PushForce).
        void SetCharacterPush(uint32_t id, bool pushBodies, float pushForce);
        /// @}

        /// @name Collision layers
        /// Collision layers: the project's 16 named layers and their matrix (CollisionLayers2D,
        /// shared with 2D physics). Bit b of masks[a]: bodies on layers a and b collide (symmetric).
        /// Every body / character is on one layer. Takes effect from the next step.
        /// @{
        void SetCollisionMatrix(const std::array<uint16_t, 16>& masks);
        bool CanLayersCollide(int a, int b) const;
        /// Moves a body to another layer (nothing when it is on it already).
        void SetBodyLayer(uint32_t bodyID, int layer);
        int GetBodyLayer(uint32_t bodyID) const; ///< -1: not in this world
        void SetCharacterLayer(uint32_t id, int layer);
        int GetCharacterLayer(uint32_t id) const;
        /// @}

        /// @name Queries
        /// Queries. layerMask: the layers that count (bits, CollisionLayers2D::GetCategoryBits);
        /// triggers (sensor colliders) only with includeSensors. A ray / shape that starts inside a
        /// collider does not hit it (its own body, a wall it touches); triangles of mesh colliders
        /// are hit from their front side only.
        /// The closest hit along the ray.
        /// @{
        RaycastHit CastRay(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, uint16_t layerMask = 0xFFFF,
                           bool includeSensors = false) const;
        /// Every entity the ray hits (its first hit along the ray), the closest first (ties: by ID).
        std::vector<RaycastHit> CastRayAll(const glm::vec3& origin, const glm::vec3& direction, float maxDistance,
                                           uint16_t layerMask = 0xFFFF, bool includeSensors = false) const;
        /// The entities with a body that overlaps the shape (at `center`, turned by `rotation`): exact
        /// shape tests (mesh colliders: their triangles - a shape wholly inside a closed mesh does not
        /// touch it). Each entity once, the nearest body position to `center` first (ties: by ID).
        std::vector<UUID> Overlap(const PhysicsQueryShape3D& shape, const glm::vec3& center, const glm::quat& rotation,
                                  uint16_t layerMask = 0xFFFF, bool includeSensors = false) const;
        /// The shape swept from `origin` along `direction` for `maxDistance`: the first body it touches.
        /// Distance: how far the shape went; Point: where it touches the body; Normal: that body's
        /// surface there (pointing back at the shape).
        RaycastHit CastShape(const PhysicsQueryShape3D& shape, const glm::vec3& origin, const glm::quat& rotation, const glm::vec3& direction,
                             float maxDistance, uint16_t layerMask = 0xFFFF, bool includeSensors = false) const;
        /// Every entity on the way (its first touch), the closest first (ties: by ID).
        std::vector<RaycastHit> CastShapeAll(const PhysicsQueryShape3D& shape, const glm::vec3& origin, const glm::quat& rotation,
                                             const glm::vec3& direction, float maxDistance, uint16_t layerMask = 0xFFFF,
                                             bool includeSensors = false) const;
        /// @}

        /// Jolt mesh shapes alive in the shared cache (mesh colliders; tests).
        static size_t GetMeshShapeCacheSize();

        /// @name Joints
        /// Joints (Jolt constraints). Handles are never reused: 0 = none.
        /// 0 when the description cannot make one (no / the same body, a body not in this world).
        /// Both bodies wake up.
        /// @{
        uint32_t CreateJoint(const PhysicsJoint3DDesc& desc);
        void DestroyJoint(uint32_t joint);
        /// Destroys the joints of `entity` of `type` except `joint`, which becomes 0 when it is not one
        /// of them (a component holding a copy of another one's handle).
        void ClaimJoint(UUID entity, Joint3DType type, uint32_t& joint);
        /// The settings that can change while the joint lives: limits, motor, spring, distances (the
        /// anchors and the axis stay as they were made). Bodies wake up only when something changed.
        void UpdateJoint(uint32_t joint, const PhysicsJoint3DDesc& desc);
        /// `invDt`: 1 / the last step's time (the reaction forces come from its impulses).
        PhysicsJoint3DState GetJointState(uint32_t joint, float invDt) const;
        /// Where the two anchors are now (the joint's own body first) and the hinge / slider / cone axis in
        /// the world. `connectedAxis`: that axis as it was made, turning with the connected body (a cone's
        /// middle). False for an unknown handle.
        bool GetJointFrame(uint32_t joint, glm::vec3& anchor, glm::vec3& connectedAnchor, glm::vec3& axis,
                           glm::vec3* connectedAxis = nullptr) const;
        bool IsJoint(uint32_t joint) const;     ///< the handle is a living joint of this world
        size_t GetJointCount() const;
        /// Whether the two bodies' shapes may touch (false while a joint between them has
        /// CollideConnected off).
        bool CanBodiesCollide(uint32_t bodyA, uint32_t bodyB) const;
        /// @}

        /// Collision begin/end events gathered since the last call.
        std::vector<ContactEvent> TakeEvents();

        /// @name Contact points for debug drawing
        /// Contact points for debug drawing (off by default: costs a lock per contact)
        /// On: every step records the contact points of the touching shapes; the contacts of
        /// sleeping bodies are kept (Jolt stops reporting them) until a body wakes up or goes.
        /// @{
        void SetDebugContactsEnabled(bool enabled);
        bool IsDebugContactsEnabled() const;
        /// The recorded points and those of the characters, appended to `out` (the points of a body
        /// pair follow each other). The normals point out of the static / kinematic body (EntityA);
        /// between two dynamic bodies they are Jolt's: from the first body toward the second (the
        /// way the second one is pushed out). Each point has the overlap of its contact, the speed of
        /// the impact it began with and its share of the impact's estimated impulse, how long ago it
        /// began (simulated seconds) and whether both bodies sleep.
        void GetDebugContacts(std::vector<PhysicsContactPoint>& out) const;
        /// @}

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };

}
