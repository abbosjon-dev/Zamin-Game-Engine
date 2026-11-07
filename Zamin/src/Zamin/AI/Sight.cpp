#include "Zamin/AI/Sight.h"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace Zamin::Sight {

    namespace {

        glm::vec3 SafeNormalize(const glm::vec3& v, const glm::vec3& fallback)
        {
            const float l = glm::length(v);
            return l > 1e-6f && std::isfinite(l) ? v / l : fallback;
        }

    }

    float AngleTo(const SightCone& cone, const glm::vec3& point)
    {
        const glm::vec3 to = point - cone.Eye;
        const float l = glm::length(to);
        if (l < 1e-6f)
            return 0.0f;
        const glm::vec3 forward = SafeNormalize(cone.Forward, { 0.0f, 0.0f, -1.0f });
        return glm::degrees(std::acos(std::clamp(glm::dot(to / l, forward), -1.0f, 1.0f)));
    }

    bool InView(const SightCone& cone, const glm::vec3& point)
    {
        const float distance = glm::length(point - cone.Eye);
        if (!std::isfinite(distance))
            return false;
        if (distance <= std::max(0.0f, cone.NearDistance))
            return true;
        if (distance > cone.Distance)
            return false;
        return AngleTo(cone, point) <= std::clamp(cone.FieldOfView, 0.0f, 360.0f) * 0.5f;
    }

    void GetTargetPoints(const glm::vec3& boundsMin, const glm::vec3& boundsMax, const glm::vec3& eye, const glm::vec3& up,
                         std::vector<glm::vec3>& out)
    {
        out.clear();
        const glm::vec3 min = glm::min(boundsMin, boundsMax), max = glm::max(boundsMin, boundsMax);
        const glm::vec3 center = (min + max) * 0.5f, half = (max - min) * 0.5f;
        out.push_back(center);
        const glm::vec3 u = SafeNormalize(up, { 0.0f, 1.0f, 0.0f });
        // The box's reach along a direction (its support distance), most of it.
        auto reach = [&](const glm::vec3& d) { return 0.8f * (std::abs(d.x) * half.x + std::abs(d.y) * half.y + std::abs(d.z) * half.z); };
        auto add = [&](const glm::vec3& p) {
            for (const glm::vec3& q : out)
                if (glm::length(p - q) < 1e-4f)
                    return;
            out.push_back(p);
        };
        const float upReach = reach(u);
        add(center + u * upReach);
        add(center - u * upReach);
        // Sideways as the eye sees it.
        glm::vec3 view = center - eye;
        view -= u * glm::dot(view, u);
        glm::vec3 side = glm::cross(SafeNormalize(view, glm::abs(u.y) < 0.9f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f)), u);
        side = SafeNormalize(side, glm::abs(u.x) < 0.9f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 0.0f, 1.0f));
        const float sideReach = reach(side);
        add(center + side * sideReach);
        add(center - side * sideReach);
    }

    void GetConeArc(const SightCone& cone, const glm::vec3& up, int segments, std::vector<glm::vec3>& out)
    {
        out.clear();
        segments = std::clamp(segments, 1, 256);
        const glm::vec3 u = SafeNormalize(up, { 0.0f, 1.0f, 0.0f });
        glm::vec3 forward = cone.Forward - u * glm::dot(cone.Forward, u);
        forward = SafeNormalize(forward, std::abs(u.y) < 0.9f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(0.0f, 0.0f, -1.0f));
        const float half = glm::radians(std::clamp(cone.FieldOfView, 0.0f, 360.0f) * 0.5f);
        for (int i = 0; i <= segments; i++)
        {
            const float a = -half + 2.0f * half * (float)i / (float)segments;
            out.push_back(cone.Eye + glm::angleAxis(a, u) * forward * std::max(0.0f, cone.Distance));
        }
    }

}
