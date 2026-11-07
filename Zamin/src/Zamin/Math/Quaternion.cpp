#include "Zamin/Math/Quaternion.h"
#include "Zamin/Math/MathUtils.h"

#include <algorithm>
#include <cmath>

namespace Zamin::Math {

    namespace {
        constexpr float TinySquared = 1e-20f;

        glm::quat NormalizeOrIdentity(const glm::quat& q)
        {
            const float lengthSquared = glm::dot(q, q);
            if (!(lengthSquared > TinySquared) || !std::isfinite(lengthSquared))
                return QuatIdentity;
            return q * (1.0f / std::sqrt(lengthSquared));
        }

        // Any unit vector perpendicular to the unit vector v.
        glm::vec3 AnyPerpendicular(const glm::vec3& v)
        {
            const glm::vec3 other = std::abs(v.x) < 0.9f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
            return glm::normalize(glm::cross(v, other));
        }
    }

    // ------------------------------------------------------------------ making rotations
    glm::quat AngleAxis(float angle, const glm::vec3& axis)
    {
        const float lengthSquared = glm::dot(axis, axis);
        if (!(lengthSquared > TinySquared))
            return QuatIdentity;
        return glm::angleAxis(angle, axis / std::sqrt(lengthSquared));
    }

    void ToAngleAxis(const glm::quat& rotation, float& angle, glm::vec3& axis)
    {
        glm::quat q = NormalizeOrIdentity(rotation);
        if (q.w < 0.0f)
            q = -q; // the short way: 0..Pi
        const glm::vec3 v(q.x, q.y, q.z);
        const float s = glm::length(v);
        angle = 2.0f * std::atan2(s, q.w);
        axis = s > 1e-9f ? v / s : glm::vec3(1.0f, 0.0f, 0.0f);
    }

    glm::quat FromEuler(const glm::vec3& euler)
    {
        return glm::quat(euler);
    }

    glm::vec3 ToEuler(const glm::quat& rotation)
    {
        const glm::quat q = NormalizeOrIdentity(rotation);
        // The rotation matrix's elements (row, column) that R = Rz * Ry * Rx is solved from.
        const float r20 = 2.0f * (q.x * q.z - q.w * q.y);
        const float sinY = std::clamp(-r20, -1.0f, 1.0f);
        glm::vec3 euler;
        if (std::abs(sinY) < 0.999999f)
        {
            euler.x = std::atan2(2.0f * (q.y * q.z + q.w * q.x), 1.0f - 2.0f * (q.x * q.x + q.y * q.y)); // r21, r22
            euler.y = std::asin(sinY);
            euler.z = std::atan2(2.0f * (q.x * q.y + q.w * q.z), 1.0f - 2.0f * (q.y * q.y + q.z * q.z)); // r10, r00
        }
        else
        {
            // Gimbal lock: only X - Z (Y = +90) or X + Z (Y = -90) matters; all of it goes to X.
            const float r01 = 2.0f * (q.x * q.y - q.w * q.z);
            const float r11 = 1.0f - 2.0f * (q.x * q.x + q.z * q.z);
            euler.y = sinY > 0.0f ? HalfPi : -HalfPi;
            euler.x = std::atan2(sinY > 0.0f ? r01 : -r01, r11);
            euler.z = 0.0f;
        }
        return euler;
    }

    glm::quat LookRotation(const glm::vec3& forward, const glm::vec3& up)
    {
        const float forwardSquared = glm::dot(forward, forward);
        if (!(forwardSquared > TinySquared) || !std::isfinite(forwardSquared))
            return QuatIdentity;
        const glm::vec3 f = forward / std::sqrt(forwardSquared);
        glm::vec3 right = glm::cross(f, up);
        float rightSquared = glm::dot(right, right);
        if (!(rightSquared > 1e-10f * std::max(1e-20f, glm::dot(up, up))))
        {
            // Looking along `up` (or no up given): the up a pitch towards it would give.
            const float side = glm::dot(f, up) < 0.0f ? -1.0f : 1.0f;
            const glm::vec3 fallback = (std::abs(f.z) < 0.999f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(1.0f, 0.0f, 0.0f)) * side;
            right = glm::cross(f, fallback);
            rightSquared = glm::dot(right, right);
        }
        right /= std::sqrt(rightSquared);
        glm::mat3 basis;
        basis[0] = right;
        basis[1] = glm::cross(right, f);
        basis[2] = -f;
        return glm::normalize(glm::quat_cast(basis));
    }

    glm::quat FromToRotation(const glm::vec3& from, const glm::vec3& to)
    {
        const float fromSquared = glm::dot(from, from);
        const float toSquared = glm::dot(to, to);
        if (!(fromSquared > TinySquared) || !(toSquared > TinySquared))
            return QuatIdentity;
        const glm::vec3 a = from / std::sqrt(fromSquared);
        const glm::vec3 b = to / std::sqrt(toSquared);
        const float d = glm::dot(a, b);
        if (d >= 1.0f - 1e-7f)
            return QuatIdentity;
        if (d <= -1.0f + 1e-6f)
            return glm::angleAxis(Pi, AnyPerpendicular(a));
        // Half way between "no rotation" and the double rotation (1 + cos, sin * axis).
        const glm::vec3 c = glm::cross(a, b);
        return glm::normalize(glm::quat(1.0f + d, c.x, c.y, c.z));
    }

    // ------------------------------------------------------------------ between rotations
    glm::quat SlerpUnclamped(const glm::quat& a, const glm::quat& b, float t)
    {
        float cosine = glm::dot(a, b);
        glm::quat end = b;
        if (cosine < 0.0f)
        {
            end = -b;
            cosine = -cosine;
        }
        if (cosine > 0.9995f)
            return NormalizeOrIdentity(a * (1.0f - t) + end * t);
        const float angle = std::acos(std::min(cosine, 1.0f));
        const float sine = std::sin(angle);
        return NormalizeOrIdentity((a * std::sin((1.0f - t) * angle) + end * std::sin(t * angle)) * (1.0f / sine));
    }

    glm::quat Slerp(const glm::quat& a, const glm::quat& b, float t)
    {
        return SlerpUnclamped(a, b, Clamp01(t));
    }

    glm::quat Nlerp(const glm::quat& a, const glm::quat& b, float t)
    {
        t = Clamp01(t);
        const glm::quat end = glm::dot(a, b) < 0.0f ? -b : b;
        return NormalizeOrIdentity(a * (1.0f - t) + end * t);
    }

    glm::quat RotateTowards(const glm::quat& from, const glm::quat& to, float maxAngle)
    {
        const float angle = Angle(from, to);
        if (angle <= 0.0f)
            return NormalizeOrIdentity(to);
        if (maxAngle >= angle)
            return NormalizeOrIdentity(to);
        // Turning away stops when it faces away from `to` (Pi between them).
        const float t = std::max(maxAngle / angle, -(Pi - angle) / angle);
        return SlerpUnclamped(NormalizeOrIdentity(from), NormalizeOrIdentity(to), t);
    }

    float Angle(const glm::quat& a, const glm::quat& b)
    {
        // The rotation from a to b; atan2 keeps small angles exact (acos of the dot does not).
        glm::quat d = glm::conjugate(NormalizeOrIdentity(a)) * NormalizeOrIdentity(b);
        const float s = glm::length(glm::vec3(d.x, d.y, d.z));
        return 2.0f * std::atan2(s, std::abs(d.w));
    }

    float Angle(const glm::vec3& from, const glm::vec3& to)
    {
        if (!(glm::dot(from, from) > TinySquared) || !(glm::dot(to, to) > TinySquared))
            return 0.0f;
        return std::atan2(glm::length(glm::cross(from, to)), glm::dot(from, to));
    }

    float SignedAngle(const glm::vec3& from, const glm::vec3& to, const glm::vec3& axis)
    {
        const float angle = Angle(from, to);
        return glm::dot(axis, glm::cross(from, to)) < 0.0f ? -angle : angle;
    }

    bool Approximately(const glm::quat& a, const glm::quat& b, float epsilon)
    {
        return Angle(a, b) <= epsilon;
    }

    // ------------------------------------------------------------------ directions
    glm::vec3 Forward(const glm::quat& rotation) { return rotation * glm::vec3(0.0f, 0.0f, -1.0f); }
    glm::vec3 Up(const glm::quat& rotation) { return rotation * glm::vec3(0.0f, 1.0f, 0.0f); }
    glm::vec3 Right(const glm::quat& rotation) { return rotation * glm::vec3(1.0f, 0.0f, 0.0f); }

    glm::vec3 LookEuler(const glm::vec3& direction)
    {
        const float lengthSquared = glm::dot(direction, direction);
        if (!(lengthSquared > TinySquared))
            return glm::vec3(0.0f);
        const glm::vec3 d = direction / std::sqrt(lengthSquared);
        return { std::asin(std::clamp(d.y, -1.0f, 1.0f)), std::atan2(-d.x, -d.z), 0.0f };
    }

}
