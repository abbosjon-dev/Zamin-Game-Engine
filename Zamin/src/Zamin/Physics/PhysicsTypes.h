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

    // The kinds of 2D joints (DistanceJoint2DComponent ... WheelJoint2DComponent).
    enum class Joint2DType { Distance = 0, Revolute, Prismatic, Weld, Wheel };

    // The kinds of 3D joints (FixedJoint3DComponent ... SliderJoint3DComponent).
    enum class Joint3DType { Fixed = 0, Hinge, Distance, Slider };

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

    // A point where two bodies touch (debug drawing): world position and the contact normal.
    struct PhysicsContactPoint
    {
        glm::vec3 Position{ 0.0f };
        glm::vec3 Normal{ 0.0f };
    };

}
