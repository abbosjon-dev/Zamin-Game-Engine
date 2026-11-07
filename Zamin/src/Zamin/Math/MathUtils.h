#pragma once

#include <glm/glm.hpp>

#include <cmath>
#include <limits>

// Everyday number helpers for gameplay code (scripts get them through ScriptableEntity.h):
// ranges (Clamp01, Lerp, InverseLerp, Remap), wrapping (Repeat, PingPong, Wrap), angles (WrapAngle,
// DeltaAngle, LerpAngle, MoveTowardsAngle), moving towards a target at a set speed (MoveTowards) or
// smoothly (SmoothDamp - a critically damped spring - and Damp - frame-rate independent exponential
// smoothing) and comparing floats (Approximately).
//
// Angles are radians, like TransformComponent::Rotation (Math::Deg2Rad / Rad2Deg convert).
//
//   m_Speed = Math::MoveTowards(m_Speed, MaxSpeed, Acceleration * dt);       // at most this much a frame
//   pos = Math::SmoothDamp(pos, target, m_Velocity, 0.3f, dt);               // ~0.3 s to get there, no overshoot
//   float t = Math::InverseLerp(MinHeight, MaxHeight, y);                    // 0..1
//   float volume = Math::Remap(distance, 2.0f, 20.0f, 1.0f, 0.0f);           // not clamped (RemapClamped is)
//   yaw = Math::MoveTowardsAngle(yaw, targetYaw, TurnSpeed * dt);            // the short way round
namespace Zamin::Math {

    constexpr float Pi = 3.14159265358979323846f;
    constexpr float TwoPi = 6.28318530717958647692f;
    constexpr float HalfPi = 1.57079632679489661923f;
    constexpr float Deg2Rad = Pi / 180.0f;
    constexpr float Rad2Deg = 180.0f / Pi;
    constexpr float Infinity = std::numeric_limits<float>::infinity();
    // The tolerance Approximately uses by default (relative to the size of the numbers, at least
    // this much absolutely).
    constexpr float DefaultEpsilon = 1e-5f;

    // ------------------------------------------------------------------ ranges
    // 0..1 (NaN stays NaN).
    constexpr float Clamp01(float value) { return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value); }

    // a .. b by t (t clamped to 0..1); for floats and glm vectors. LerpUnclamped extrapolates.
    template<typename T>
    constexpr T LerpUnclamped(const T& a, const T& b, float t) { return a + (b - a) * t; }
    template<typename T>
    constexpr T Lerp(const T& a, const T& b, float t) { return a + (b - a) * Clamp01(t); }

    // Where `value` is between a and b: 0 at a, 1 at b (clamped; 0 when a == b). The inverse of Lerp.
    constexpr float InverseLerpUnclamped(float a, float b, float value) { return a == b ? 0.0f : (value - a) / (b - a); }
    constexpr float InverseLerp(float a, float b, float value) { return Clamp01(InverseLerpUnclamped(a, b, value)); }
    // Along the segment a..b: 0 at a, 1 at b, for the point of it nearest to `value` (clamped).
    float InverseLerp(const glm::vec2& a, const glm::vec2& b, const glm::vec2& value);
    float InverseLerp(const glm::vec3& a, const glm::vec3& b, const glm::vec3& value);

    // `value` from the range inMin..inMax to outMin..outMax (outside it: further out, Remap; held at
    // the ends, RemapClamped). Either range may run backwards (Remap(d, 0, 10, 1, 0)).
    constexpr float Remap(float value, float inMin, float inMax, float outMin, float outMax)
    {
        return LerpUnclamped(outMin, outMax, InverseLerpUnclamped(inMin, inMax, value));
    }
    constexpr float RemapClamped(float value, float inMin, float inMax, float outMin, float outMax)
    {
        return LerpUnclamped(outMin, outMax, InverseLerp(inMin, inMax, value));
    }

    // The smooth 0..1 step of GLSL: 0 below edge0, 1 above edge1, an S curve (3t^2 - 2t^3) between.
    // SmootherStep is flatter at the ends (6t^5 - 15t^4 + 10t^3, Perlin). edge0 == edge1: a hard step.
    constexpr float SmoothStep(float edge0, float edge1, float x)
    {
        if (edge0 == edge1)
            return x < edge0 ? 0.0f : 1.0f;
        const float t = Clamp01((x - edge0) / (edge1 - edge0));
        return t * t * (3.0f - 2.0f * t);
    }
    constexpr float SmootherStep(float edge0, float edge1, float x)
    {
        if (edge0 == edge1)
            return x < edge0 ? 0.0f : 1.0f;
        const float t = Clamp01((x - edge0) / (edge1 - edge0));
        return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
    }

    // -1, 0 or 1. SignNonZero: 1 for 0 too (a direction that is never none).
    constexpr float Sign(float value) { return value > 0.0f ? 1.0f : (value < 0.0f ? -1.0f : 0.0f); }
    constexpr float SignNonZero(float value) { return value < 0.0f ? -1.0f : 1.0f; }

    // ------------------------------------------------------------------ wrapping
    // `t` wrapped into 0..length (like a modulo that is never negative: Repeat(-1, 5) = 4). 0 for
    // a length of 0 or less.
    float Repeat(float t, float length);
    // `t` going 0 -> length -> 0 -> ... (a triangle wave: PingPong(7, 5) = 3).
    float PingPong(float t, float length);
    // `value` wrapped into min..max (the same as Repeat, from min).
    float Wrap(float value, float min, float max);

    // ------------------------------------------------------------------ angles (radians)
    // -Pi .. Pi.
    float WrapAngle(float angle);
    // The shortest turn from `current` to `target` (-Pi .. Pi; positive = counter-clockwise).
    float DeltaAngle(float current, float target);
    // Lerp that goes the short way round (t clamped to 0..1). The result is not wrapped.
    float LerpAngle(float a, float b, float t);

    // ------------------------------------------------------------------ comparing
    // a and b are equal within `epsilon` of the larger of them (at least `epsilon` itself, for
    // numbers near 0): Approximately(0.1f + 0.2f, 0.3f) is true.
    bool Approximately(float a, float b, float epsilon = DefaultEpsilon);
    // Every component (vectors).
    bool Approximately(const glm::vec2& a, const glm::vec2& b, float epsilon = DefaultEpsilon);
    bool Approximately(const glm::vec3& a, const glm::vec3& b, float epsilon = DefaultEpsilon);
    bool Approximately(const glm::vec4& a, const glm::vec4& b, float epsilon = DefaultEpsilon);

    // ------------------------------------------------------------------ moving towards a target
    // `current` moved towards `target` by at most `maxDelta` (it never goes past it; a negative
    // maxDelta moves away). Vectors move along the straight line between them.
    float MoveTowards(float current, float target, float maxDelta);
    glm::vec2 MoveTowards(const glm::vec2& current, const glm::vec2& target, float maxDelta);
    glm::vec3 MoveTowards(const glm::vec3& current, const glm::vec3& target, float maxDelta);
    glm::vec4 MoveTowards(const glm::vec4& current, const glm::vec4& target, float maxDelta);
    // MoveTowards for angles: the short way round, landing exactly on `target` (radians).
    float MoveTowardsAngle(float current, float target, float maxDelta);

    // A critically damped spring from `current` towards `target` (Unity's SmoothDamp): the value
    // after `dt` seconds. `velocity` belongs to the caller and is kept between the calls (start it
    // at 0). `smoothTime` is about how long getting there takes (seconds); it moves at most
    // `maxSpeed` units per second and never overshoots the target. dt 0: nothing changes.
    float SmoothDamp(float current, float target, float& velocity, float smoothTime, float dt, float maxSpeed = Infinity);
    glm::vec2 SmoothDamp(const glm::vec2& current, const glm::vec2& target, glm::vec2& velocity, float smoothTime, float dt,
                         float maxSpeed = Infinity);
    glm::vec3 SmoothDamp(const glm::vec3& current, const glm::vec3& target, glm::vec3& velocity, float smoothTime, float dt,
                         float maxSpeed = Infinity);
    // SmoothDamp for angles (the short way round, radians; `velocity` in radians per second).
    float SmoothDampAngle(float current, float target, float& velocity, float smoothTime, float dt, float maxSpeed = Infinity);

    // Exponential smoothing that looks the same at any frame rate: `current` moves the fraction
    // 1 - e^(-sharpness * dt) of the way to `target` (after one second e^-sharpness of the distance is
    // left: sharpness 5 leaves 0.7%). For floats and glm vectors; sharpness 0 or less: no move.
    //   m_Focus = Math::Damp(m_Focus, goal, Smoothness, dt);
    template<typename T>
    T Damp(const T& current, const T& target, float sharpness, float dt)
    {
        if (!(sharpness > 0.0f) || !(dt > 0.0f))
            return current;
        return current + (target - current) * (1.0f - std::exp(-sharpness * dt));
    }

}
