#pragma once

#include "Zamin/Core/UUID.h"

#include <glm/glm.hpp>

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

    // A point where two bodies touch (debug drawing): world position and the contact normal.
    struct PhysicsContactPoint
    {
        glm::vec3 Position{ 0.0f };
        glm::vec3 Normal{ 0.0f };
    };

}
