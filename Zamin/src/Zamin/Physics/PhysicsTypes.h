#pragma once

#include "Zamin/Core/UUID.h"

#include <glm/glm.hpp>

#include <cstdint>

namespace Zamin {

    struct ContactEvent
    {
        UUID A, B;
        bool Begin;
        bool Sensor = false; // at least one of the two colliders is a sensor (trigger)
    };

    struct RaycastHit
    {
        bool Hit = false;
        UUID EntityID = 0;
        glm::vec3 Point{ 0.0f };
        glm::vec3 Normal{ 0.0f };
        float Distance = 0.0f;

        explicit operator bool() const { return Hit; }
    };

    // The shape of a 3D overlap / shape cast query (Scene::OverlapSphere3D ... CapsuleCast3D), in its
    // own frame: centred on the query's position, turned by its rotation.
    struct PhysicsQueryShape3D
    {
        enum class Kind { Sphere = 0, Box, Capsule };
        Kind Type = Kind::Sphere;
        float Radius = 0.5f;           // sphere, capsule
        glm::vec3 HalfExtents{ 0.5f }; // box
        float HalfHeight = 0.5f;       // capsule: from its centre to each end sphere's centre, along its Y

        static PhysicsQueryShape3D Sphere(float radius)
        {
            PhysicsQueryShape3D s;
            s.Radius = radius;
            return s;
        }
        static PhysicsQueryShape3D Box(const glm::vec3& halfExtents)
        {
            PhysicsQueryShape3D s;
            s.Type = Kind::Box;
            s.HalfExtents = halfExtents;
            return s;
        }
        static PhysicsQueryShape3D Capsule(float radius, float halfHeight)
        {
            PhysicsQueryShape3D s;
            s.Type = Kind::Capsule;
            s.Radius = radius;
            s.HalfHeight = halfHeight;
            return s;
        }
    };

    // The kinds of 2D joints (DistanceJoint2DComponent ... WheelJoint2DComponent).
    enum class Joint2DType { Distance = 0, Revolute, Prismatic, Weld, Wheel };

    // The kinds of 3D joints (FixedJoint3DComponent ... ConeJoint3DComponent).
    enum class Joint3DType { Fixed = 0, Hinge, Distance, Slider, Cone };

    // What a running 2D world holds now (the editor's scene settings, tests).
    struct PhysicsStats2D
    {
        int Bodies = 0;    // every body (static ones too)
        int Moving = 0;    // dynamic and kinematic ones
        int Awake = 0;     // moving bodies being simulated
        int Sleeping = 0;  // moving bodies that sleep
        int Joints = 0;
        int Contacts = 0;  // touching fixture pairs
        uint64_t Steps = 0; // Box2D steps since the world was made (sub-steps count one by one)
    };

    // What a running 3D world holds now (the editor's scene settings, tests).
    struct PhysicsStats3D
    {
        int Bodies = 0;     // every body (static ones too; not the characters' inner bodies)
        int Moving = 0;     // dynamic and kinematic ones
        int Awake = 0;      // moving bodies being simulated
        int Sleeping = 0;   // moving bodies that sleep
        int Joints = 0;
        int Characters = 0;
        int Contacts = 0;   // touching body pairs (triggers too)
        uint64_t Steps = 0; // steps since the world was made
    };

    // A point where two bodies touch (debug drawing, tests; DebugDraw::PhysicsContact). The normal
    // leaves EntityA's surface (it pushes EntityB away): out of the static / kinematic body (the
    // ground under a crate), between two dynamic ones from the first toward the second.
    struct PhysicsContactPoint
    {
        glm::vec3 Position{ 0.0f };
        glm::vec3 Normal{ 0.0f };
        // How far the shapes overlap there (m); a little below 0: a speculative contact - not quite
        // touching yet, the solver already keeps them from going into each other.
        float Penetration = 0.0f;
        // 3D: how fast the bodies ran into each other along the normal when the contact began (m/s;
        // about 0 for a body put down gently, kept while the contact lasts).
        float Speed = 0.0f;
        // The impulse along the normal (N*s). 3D: the impact's, estimated when the contact began - its
        // speed times the pair's effective mass at the point (with the bounce); 2D: Box2D's own normal
        // impulse of the last step (a resting crate: its weight times the step).
        float Impulse = 0.0f;
        // 3D: seconds of simulation since the contact began (an impact is drawn for a moment).
        float Age = 0.0f;
        UUID EntityA = 0, EntityB = 0;
        bool Sleeping = false;  // both bodies sleep (3D: the contact is kept as it was - Jolt reports nothing for them)
        bool Character = false; // a 3D character controller's: EntityB is the character, A the body it touches
    };

    // A running joint for debug drawing (DebugDraw::PhysicsJoint): where its two anchors are now (they
    // drift apart when the joint gives), the bodies' centres, its axis and how hard it pulls.
    struct PhysicsJointDebug
    {
        glm::vec3 Anchor{ 0.0f };          // on the joint's own body
        glm::vec3 ConnectedAnchor{ 0.0f }; // on the connected body (or the world point)
        glm::vec3 BodyCenter{ 0.0f };
        glm::vec3 ConnectedCenter{ 0.0f };
        bool HasConnectedBody = false;     // false: joined to a point in the world
        // The anchors should stay together (fixed, hinge, cone, weld): apart they show the joint
        // giving. False for a rod (distance joint) or a slider, whose anchors are apart by design.
        bool AnchorsMeet = true;
        glm::vec3 Axis{ 0.0f };            // hinge / slider / cone axis (0: none)
        float Force = 0.0f, Torque = 0.0f; // the last step's reaction (N, N*m)
        // How close it is to breaking: the larger of force / break force and torque / break torque
        // (0..1+); below 0 when it never breaks.
        float Load = -1.0f;
        bool Broken = false;               // only Anchor is used: where it was attached
    };

}
