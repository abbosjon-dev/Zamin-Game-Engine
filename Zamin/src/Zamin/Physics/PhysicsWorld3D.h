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
        void DestroyBody(uint32_t bodyID);
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

        RaycastHit CastRay(const glm::vec3& origin, const glm::vec3& direction, float maxDistance) const;

        // Collision begin/end events gathered since the last call.
        std::vector<ContactEvent> TakeEvents();

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };

}
