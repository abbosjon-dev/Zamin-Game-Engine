#pragma once

#include "Zamin/Physics/PhysicsTypes.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <memory>
#include <vector>

namespace Zamin {

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

        struct Shape
        {
            enum class Kind { Box, Sphere, Capsule } Type = Kind::Box;
            glm::vec3 HalfExtents{ 0.5f };
            float Radius = 0.5f;
            float HalfHeight = 0.5f;
            glm::vec3 Offset{ 0.0f };
            float Friction = 0.5f;
            float Restitution = 0.0f;
            bool IsSensor = false;
        };
        std::vector<Shape> Shapes;
    };

    struct CharacterDesc
    {
        UUID EntityID = 0;
        glm::vec3 Position{ 0.0f };   // feet position
        float Radius = 0.35f;
        float Height = 1.8f;          // total height (capsule)
        float MaxSlopeDegrees = 50.0f;
        float Mass = 70.0f;
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
        // Sleeping bodies that rested on it wake up (and fall when nothing else holds them).
        void DestroyBody(uint32_t bodyID);
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

        RaycastHit CastRay(const glm::vec3& origin, const glm::vec3& direction, float maxDistance) const;

        // Collision begin/end events gathered since the last call.
        std::vector<ContactEvent> TakeEvents();

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };

}
