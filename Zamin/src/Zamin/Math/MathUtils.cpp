#include "Zamin/Math/MathUtils.h"

#include <algorithm>

namespace Zamin::Math {

    namespace {
        template<typename V>
        float InverseLerpSegment(const V& a, const V& b, const V& value)
        {
            const V ab = b - a;
            const float lengthSquared = glm::dot(ab, ab);
            if (!(lengthSquared > 0.0f))
                return 0.0f;
            return Clamp01(glm::dot(value - a, ab) / lengthSquared);
        }

        template<typename V>
        bool ApproximatelyAll(const V& a, const V& b, float epsilon)
        {
            for (int i = 0; i < V::length(); i++)
                if (!Approximately(a[i], b[i], epsilon))
                    return false;
            return true;
        }

        template<typename V>
        V MoveTowardsVector(const V& current, const V& target, float maxDelta)
        {
            const V delta = target - current;
            const float distanceSquared = glm::dot(delta, delta);
            if (distanceSquared == 0.0f || (maxDelta >= 0.0f && distanceSquared <= maxDelta * maxDelta))
                return target;
            return current + delta / std::sqrt(distanceSquared) * maxDelta;
        }

        // Unity's SmoothDamp for vectors: the change is limited in length, the overshoot test is
        // along the move.
        template<typename V>
        V SmoothDampVector(const V& current, V target, V& velocity, float smoothTime, float dt, float maxSpeed)
        {
            if (!(dt > 0.0f))
                return current;
            smoothTime = std::max(0.0001f, smoothTime);
            const float omega = 2.0f / smoothTime;
            const float x = omega * dt;
            const float decay = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
            V change = current - target;
            const V originalTarget = target;
            if (maxSpeed < Infinity)
            {
                const float maxChange = std::max(0.0f, maxSpeed) * smoothTime;
                const float lengthSquared = glm::dot(change, change);
                if (lengthSquared > maxChange * maxChange)
                    change = change / std::sqrt(lengthSquared) * maxChange;
                target = current - change;
            }
            const V temp = (velocity + omega * change) * dt;
            velocity = (velocity - omega * temp) * decay;
            V output = target + (change + temp) * decay;
            // Never past the target.
            if (glm::dot(originalTarget - current, output - originalTarget) > 0.0f)
            {
                output = originalTarget;
                velocity = V(0.0f);
            }
            return output;
        }
    }

    float InverseLerp(const glm::vec2& a, const glm::vec2& b, const glm::vec2& value) { return InverseLerpSegment(a, b, value); }
    float InverseLerp(const glm::vec3& a, const glm::vec3& b, const glm::vec3& value) { return InverseLerpSegment(a, b, value); }

    // ------------------------------------------------------------------ wrapping
    float Repeat(float t, float length)
    {
        if (!(length > 0.0f))
            return 0.0f;
        return std::clamp(t - std::floor(t / length) * length, 0.0f, length);
    }

    float PingPong(float t, float length)
    {
        if (!(length > 0.0f))
            return 0.0f;
        t = Repeat(t, length * 2.0f);
        return length - std::abs(t - length);
    }

    float Wrap(float value, float min, float max)
    {
        return min + Repeat(value - min, max - min);
    }

    // ------------------------------------------------------------------ angles
    float WrapAngle(float angle)
    {
        angle = std::fmod(angle + Pi, TwoPi);
        if (angle < 0.0f)
            angle += TwoPi;
        return angle - Pi;
    }

    float DeltaAngle(float current, float target)
    {
        return WrapAngle(target - current);
    }

    float LerpAngle(float a, float b, float t)
    {
        return a + DeltaAngle(a, b) * Clamp01(t);
    }

    // ------------------------------------------------------------------ comparing
    bool Approximately(float a, float b, float epsilon)
    {
        if (a == b)
            return true; // infinities too
        if (!std::isfinite(a) || !std::isfinite(b))
            return false;
        const float scale = std::max({ 1.0f, std::abs(a), std::abs(b) });
        return std::abs(a - b) <= epsilon * scale;
    }

    bool Approximately(const glm::vec2& a, const glm::vec2& b, float epsilon) { return ApproximatelyAll(a, b, epsilon); }
    bool Approximately(const glm::vec3& a, const glm::vec3& b, float epsilon) { return ApproximatelyAll(a, b, epsilon); }
    bool Approximately(const glm::vec4& a, const glm::vec4& b, float epsilon) { return ApproximatelyAll(a, b, epsilon); }

    // ------------------------------------------------------------------ moving towards
    float MoveTowards(float current, float target, float maxDelta)
    {
        if (maxDelta >= 0.0f && std::abs(target - current) <= maxDelta)
            return target;
        return current + Sign(target - current) * maxDelta;
    }

    glm::vec2 MoveTowards(const glm::vec2& current, const glm::vec2& target, float maxDelta) { return MoveTowardsVector(current, target, maxDelta); }
    glm::vec3 MoveTowards(const glm::vec3& current, const glm::vec3& target, float maxDelta) { return MoveTowardsVector(current, target, maxDelta); }
    glm::vec4 MoveTowards(const glm::vec4& current, const glm::vec4& target, float maxDelta) { return MoveTowardsVector(current, target, maxDelta); }

    float MoveTowardsAngle(float current, float target, float maxDelta)
    {
        const float delta = DeltaAngle(current, target);
        if (maxDelta >= 0.0f && std::abs(delta) <= maxDelta)
            return target;
        return MoveTowards(current, current + delta, maxDelta);
    }

    // ------------------------------------------------------------------ smoothing
    float SmoothDamp(float current, float target, float& velocity, float smoothTime, float dt, float maxSpeed)
    {
        if (!(dt > 0.0f))
            return current;
        smoothTime = std::max(0.0001f, smoothTime);
        const float omega = 2.0f / smoothTime;
        const float x = omega * dt;
        const float decay = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
        float change = current - target;
        const float originalTarget = target;
        if (maxSpeed < Infinity)
        {
            const float maxChange = std::max(0.0f, maxSpeed) * smoothTime;
            change = std::clamp(change, -maxChange, maxChange);
            target = current - change;
        }
        const float temp = (velocity + omega * change) * dt;
        velocity = (velocity - omega * temp) * decay;
        float output = target + (change + temp) * decay;
        // Never past the target.
        if ((originalTarget - current > 0.0f) == (output > originalTarget))
        {
            output = originalTarget;
            velocity = 0.0f;
        }
        return output;
    }

    glm::vec2 SmoothDamp(const glm::vec2& current, const glm::vec2& target, glm::vec2& velocity, float smoothTime, float dt, float maxSpeed)
    {
        return SmoothDampVector(current, target, velocity, smoothTime, dt, maxSpeed);
    }

    glm::vec3 SmoothDamp(const glm::vec3& current, const glm::vec3& target, glm::vec3& velocity, float smoothTime, float dt, float maxSpeed)
    {
        return SmoothDampVector(current, target, velocity, smoothTime, dt, maxSpeed);
    }

    float SmoothDampAngle(float current, float target, float& velocity, float smoothTime, float dt, float maxSpeed)
    {
        return SmoothDamp(current, current + DeltaAngle(current, target), velocity, smoothTime, dt, maxSpeed);
    }

}
