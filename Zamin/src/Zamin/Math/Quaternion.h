#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

// Rotation helpers on glm::quat with the engine's conventions:
// - an entity looks along -Z, +Y is up and +X right (TransformComponent::GetForward / GetUp / GetRight);
// - TransformComponent::Rotation is Euler angles in radians, turned about X first, then Y, then Z
//   (R = Rz * Ry * Rx, what glm::quat(euler) builds): FromEuler / ToEuler convert both ways;
// - angles are radians.
//
//   Transform().Rotation = Math::ToEuler(Math::LookRotation(target - position));   // face the target
//   glm::quat q = Math::RotateTowards(current, goal, TurnSpeed * dt);              // at most this far a frame
//   glm::quat tilt = Math::FromToRotation(glm::vec3(0, 1, 0), groundNormal);       // stand on a slope
namespace Zamin::Math {

    constexpr glm::quat QuatIdentity{ 1.0f, 0.0f, 0.0f, 0.0f };

    // ------------------------------------------------------------------ making rotations
    // `angle` radians about `axis` (any length; a zero axis: no rotation).
    glm::quat AngleAxis(float angle, const glm::vec3& axis);
    // The angle (0..Pi) and the unit axis of a rotation (for no rotation: 0 about +X).
    void ToAngleAxis(const glm::quat& rotation, float& angle, glm::vec3& axis);

    // The rotation of TransformComponent::Rotation (Euler radians, X then Y then Z).
    glm::quat FromEuler(const glm::vec3& euler);
    // Back to such Euler angles: FromEuler(ToEuler(q)) is q (as a rotation). X and Z come out in
    // -Pi..Pi, Y in -Pi/2..Pi/2; facing straight up or down (Y = +-Pi/2, gimbal lock) the turn is
    // put on X and Z is 0.
    glm::vec3 ToEuler(const glm::quat& rotation);

    // The rotation that makes an entity look along `forward` (its -Z there) with its up as close to
    // `up` as it can be. `forward` need not be unit length; zero: no rotation. When forward and up
    // are parallel, another up is taken (+Z, or +X when that is parallel too).
    glm::quat LookRotation(const glm::vec3& forward, const glm::vec3& up = glm::vec3(0.0f, 1.0f, 0.0f));
    // The shortest rotation that turns the direction `from` onto `to` (any lengths; a zero one: no
    // rotation). Opposite directions: half a turn about an axis perpendicular to them.
    glm::quat FromToRotation(const glm::vec3& from, const glm::vec3& to);

    // ------------------------------------------------------------------ between rotations
    // Spherical interpolation the short way round, t clamped to 0..1 (SlerpUnclamped: any t, beyond
    // the ends it keeps turning). Nearly equal rotations are blended linearly (and normalized).
    glm::quat Slerp(const glm::quat& a, const glm::quat& b, float t);
    glm::quat SlerpUnclamped(const glm::quat& a, const glm::quat& b, float t);
    // Normalized linear blend the short way round: cheaper than Slerp, not at an even speed.
    glm::quat Nlerp(const glm::quat& a, const glm::quat& b, float t);
    // `from` turned towards `to` by at most `maxAngle` radians (exactly `to` when it is nearer;
    // a negative maxAngle turns away, at most until it faces the opposite way).
    glm::quat RotateTowards(const glm::quat& from, const glm::quat& to, float maxAngle);
    // The angle between two rotations (0..Pi): how far one has to turn to become the other.
    float Angle(const glm::quat& a, const glm::quat& b);
    // The angle between two directions (0..Pi; 0 when one of them is zero).
    float Angle(const glm::vec3& from, const glm::vec3& to);
    // The same, signed by the turn about `axis` (positive: counter-clockwise looking down the axis).
    float SignedAngle(const glm::vec3& from, const glm::vec3& to, const glm::vec3& axis);
    // The same rotation within `epsilon` radians (q and -q are the same rotation).
    bool Approximately(const glm::quat& a, const glm::quat& b, float epsilon = 1e-4f);

    // ------------------------------------------------------------------ directions
    // Where a rotation turns the entity's -Z / +Y / +X (TransformComponent::GetForward and so on).
    glm::vec3 Forward(const glm::quat& rotation);
    glm::vec3 Up(const glm::quat& rotation);
    glm::vec3 Right(const glm::quat& rotation);
    // The Euler angles of an entity looking along `direction` with +Y up, as a camera would have
    // them: X the pitch (-Pi/2..Pi/2), Y the yaw (-Pi..Pi), Z 0 - the same rotation as
    // LookRotation(direction). Zero: no rotation.
    glm::vec3 LookEuler(const glm::vec3& direction);

}
