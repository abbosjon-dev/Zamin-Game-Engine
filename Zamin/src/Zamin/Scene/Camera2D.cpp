#include "Zamin/Scene/Camera2D.h"

#include "Zamin/Scene/Components.h"
#include "Zamin/Scene/Parallax.h"
#include "Zamin/Math/Noise.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace Zamin {

    // ------------------------------------------------------------------ shake
    float CameraShake::Noise(uint32_t seed, float t)
    {
        // Value noise with a smootherstep blend (Math/Noise): no kinks between samples.
        return Noise::Value(t, seed);
    }

    void CameraShake::Add(float intensity, float duration, float frequency)
    {
        if (!(intensity > 0.0f))
            return;
        float remaining = GetRemaining();
        Trauma = std::min(1.0f, Trauma + std::min(intensity, 1.0f));
        float lasts = std::max({ std::isfinite(duration) ? duration : 0.0f, remaining, MinDuration });
        Decay = Trauma / lasts;
        if (frequency > 0.0f && std::isfinite(frequency))
            Frequency = frequency;
    }

    void CameraShake::Update(float dt)
    {
        if (!(dt > 0.0f))
            return;
        Time += dt;
        if (Trauma > 0.0f)
        {
            Trauma = std::max(0.0f, Trauma - Decay * dt);
            if (Trauma <= 0.0f)
                Decay = 0.0f;
        }
    }

    float CameraShake::GetAmount() const
    {
        if (!(Trauma > 0.0f))
            return 0.0f;
        return std::pow(std::min(Trauma, 1.0f), std::max(Exponent, 0.1f));
    }

    glm::vec3 CameraShake::GetOffset() const
    {
        float amount = GetAmount();
        if (amount <= 0.0f)
            return glm::vec3(0.0f);
        // Three unrelated curves (one seed each) for x, y and the roll.
        float t = Time * std::max(Frequency, 0.0f);
        return { MaxOffset.x * amount * Noise(Seed, t),
                 MaxOffset.y * amount * Noise(Seed ^ 0x68E31DA4u, t),
                 glm::radians(MaxAngle) * amount * Noise(Seed ^ 0xB5297A4Du, t) };
    }

    glm::mat4 CameraShake::GetMatrix() const
    {
        glm::vec3 o = GetOffset();
        if (o == glm::vec3(0.0f))
            return glm::mat4(1.0f);
        return glm::translate(glm::mat4(1.0f), { o.x, o.y, 0.0f }) * glm::rotate(glm::mat4(1.0f), o.z, { 0.0f, 0.0f, 1.0f });
    }

    // ------------------------------------------------------------------ follow
    float Camera2DFollow::Damp(float current, float goal, float damping, float dt)
    {
        if (!(damping > 0.0f) || !std::isfinite(damping))
            return goal;
        if (!(dt > 0.0f))
            return current;
        return goal + (current - goal) * std::exp(-dt / damping);
    }

    glm::vec2 Camera2DFollow::Damp(const glm::vec2& current, const glm::vec2& goal, const glm::vec2& damping, float dt)
    {
        return { Damp(current.x, goal.x, damping.x, dt), Damp(current.y, goal.y, damping.y, dt) };
    }

    glm::vec2 Camera2DFollow::ApplyDeadZone(const glm::vec2& center, const glm::vec2& point, const glm::vec2& size)
    {
        glm::vec2 half = glm::max(size, glm::vec2(0.0f)) * 0.5f;
        glm::vec2 result = center;
        for (int axis = 0; axis < 2; axis++)
        {
            float d = point[axis] - center[axis];
            if (d > half[axis])
                result[axis] = point[axis] - half[axis];
            else if (d < -half[axis])
                result[axis] = point[axis] + half[axis];
        }
        return result;
    }

    glm::vec2 Camera2DFollow::ClampToBounds(const glm::vec2& center, const glm::vec2& halfView, const glm::vec2& min, const glm::vec2& max)
    {
        glm::vec2 result = center;
        for (int axis = 0; axis < 2; axis++)
        {
            float lo = std::min(min[axis], max[axis]), hi = std::max(min[axis], max[axis]);
            float half = std::max(halfView[axis], 0.0f);
            if (hi - lo <= half * 2.0f)
                result[axis] = (lo + hi) * 0.5f;
            else
                result[axis] = std::clamp(center[axis], lo + half, hi - half);
        }
        return result;
    }

    glm::vec2 Camera2DFollow::GetHalfView(const glm::mat4& projection, const glm::mat4& cameraWorld, float depth)
    {
        glm::vec2 min, max;
        if (!ParallaxMath::GetViewRectAtDepth(projection * glm::inverse(cameraWorld), depth, min, max))
            return glm::vec2(0.0f);
        return glm::max((max - min) * 0.5f, glm::vec2(0.0f));
    }

    glm::vec2 Camera2DFollow::GetSnapPosition(const Camera2DFollowComponent& follow, const glm::vec2& target, const glm::vec2& halfView)
    {
        glm::vec2 goal = target + follow.Offset;
        if (follow.UseBounds)
            goal = ClampToBounds(goal, halfView, follow.BoundsMin, follow.BoundsMax);
        return goal;
    }

    glm::vec2 Camera2DFollow::Step(Camera2DFollowComponent& follow, const glm::vec2& camera, const glm::vec2& target,
                                   const glm::vec2& halfView, float dt)
    {
        if (!follow.Initialized || follow.SnapRequested)
        {
            const bool snap = follow.SnapRequested || follow.SnapOnStart;
            follow.Initialized = true;
            follow.SnapRequested = false;
            follow.LastTarget = target;
            follow.LookAheadDirection = glm::vec2(0.0f);
            follow.LookAheadOffset = glm::vec2(0.0f);
            if (snap)
                return GetSnapPosition(follow, target, halfView);
            dt = 0.0f; // start from where the camera is; no motion of the target known yet
        }

        // Look-ahead: towards where the target last moved (it stays when the target stands still).
        if (dt > 0.0f)
        {
            glm::vec2 velocity = (target - follow.LastTarget) / dt;
            for (int axis = 0; axis < 2; axis++)
                if (std::abs(velocity[axis]) > std::max(follow.LookAheadMinSpeed, 0.0f))
                    follow.LookAheadDirection[axis] = velocity[axis] > 0.0f ? 1.0f : -1.0f;
        }
        follow.LastTarget = target;
        glm::vec2 lookGoal = follow.LookAheadDirection * follow.LookAhead;
        follow.LookAheadOffset = Damp(follow.LookAheadOffset, lookGoal, glm::vec2(follow.LookAheadDamping), dt);

        glm::vec2 point = target + follow.Offset + follow.LookAheadOffset;
        glm::vec2 desired = ApplyDeadZone(camera, point, follow.DeadZone);
        glm::vec2 result = Damp(camera, desired, follow.Damping, dt);
        if (follow.UseBounds)
            result = ClampToBounds(result, halfView, follow.BoundsMin, follow.BoundsMax);
        return result;
    }

}
