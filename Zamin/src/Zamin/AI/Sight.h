#pragma once

#include <glm/glm.hpp>

#include <vector>

namespace Zamin {

    // What an AI's eyes cover: everything within Distance of Eye inside the cone of FieldOfView
    // (degrees, the whole angle) around Forward, and anything nearer than NearDistance in any
    // direction (it notices what is right beside / behind it). SightSensorComponent fills one each
    // check; the rays that decide whether something blocks the view are the scene's (Scene::CanSee).
    struct SightCone
    {
        glm::vec3 Eye{ 0.0f };
        glm::vec3 Forward{ 0.0f, 0.0f, -1.0f }; // unit
        float Distance = 10.0f;
        float FieldOfView = 110.0f;
        float NearDistance = 0.0f;
    };

    namespace Sight {

        // Whether the point is in view (not counting what is in the way).
        bool InView(const SightCone& cone, const glm::vec3& point);
        // How far off the view direction the point is, degrees (0 straight ahead, 180 behind).
        float AngleTo(const SightCone& cone, const glm::vec3& point);

        // The points of a target the rays aim at - its box's centre, then near its top and bottom
        // (along `up`) and its left and right sides as seen from `eye` - so a target half behind
        // cover is still seen. Points that coincide (a flat 2D box) are given once.
        void GetTargetPoints(const glm::vec3& boundsMin, const glm::vec3& boundsMax, const glm::vec3& eye, const glm::vec3& up,
                             std::vector<glm::vec3>& out);

        // The cone's rim for drawing: `segments` + 1 points from one edge of the view to the other,
        // Distance from the eye, turned around `up` (on the plane at right angles to it).
        void GetConeArc(const SightCone& cone, const glm::vec3& up, int segments, std::vector<glm::vec3>& out);

    }

}
