#include "GameplayMotor.h"
#include <algorithm>
#include <cmath>
#include <glm/gtc/quaternion.hpp>
namespace Zamin::Gameplay {
    namespace {
        bool Finite(glm::vec2 v) { return std::isfinite(v.x) && std::isfinite(v.y); }
        bool Finite(glm::vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
    }
    bool StepMotor(const MotorSettings& s, MotorState& state, const MotorInput& in, MotorOutput& output, std::string& error)
    {
        error.clear();
        const float values[] = { s.Speed, s.Acceleration, s.Deceleration, s.AirControl, s.SprintMultiplier, s.JumpSpeed, s.CoyoteSeconds, s.BufferSeconds, s.JumpCut, in.Delta, in.VerticalVelocity, state.Grace, state.Buffered, state.Facing };
        for (float v : values)
            if (!std::isfinite(v))
            {
                error = "Motor values must be finite";
                return false;
            }
        if (!Finite(in.Move) || !Finite(in.HorizontalVelocity) || in.Delta < 0 || s.Speed < 0 || s.Speed > 1000 || s.Acceleration < 0 || s.Acceleration > 10000 || s.Deceleration < 0 || s.Deceleration > 10000 || s.AirControl < 0 || s.AirControl > 1 || s.SprintMultiplier < 1 || s.SprintMultiplier > 5 || s.JumpSpeed < 0 || s.JumpSpeed > 100 || s.CoyoteSeconds < 0 || s.CoyoteSeconds > 1 || s.BufferSeconds < 0 || s.BufferSeconds > 1 || s.JumpCut < 0 || s.JumpCut > 1)
        {
            error = "Motor settings outside supported range";
            return false;
        }
        MotorOutput result{ in.HorizontalVelocity, in.VerticalVelocity, false };
        if (in.Delta == 0)
        {
            output = result;
            return true;
        }
        if (!in.Enabled)
        {
            state = {};
            result.Horizontal = {};
            output = result;
            return true;
        }
        auto next = state;
        if (!in.Grounded) next.GroundLock = false;
        const bool ground = in.Grounded && !next.GroundLock && in.VerticalVelocity <= .1f;
        next.Grace = ground ? s.CoyoteSeconds : std::max(0.0f, next.Grace - in.Delta);
        next.Buffered = in.JumpPressed ? s.BufferSeconds : std::max(0.0f, next.Buffered - in.Delta);
        if (ground)
        {
            next.OwnJump = false;
            next.CutApplied = false;
        }
        glm::dvec2 move(in.Move);
        const double length = glm::length(move);
        if (length > 1) move /= length;
        const glm::dvec2 goal = move * double(s.Speed) * double(in.Sprint ? s.SprintMultiplier : 1);
        const double rate = (length > .001 ? s.Acceleration : s.Deceleration) * (ground ? 1 : s.AirControl);
        const auto diff = goal - glm::dvec2(in.HorizontalVelocity);
        const double distance = glm::length(diff), step = rate * std::min(in.Delta, .25f);
        result.Horizontal = distance <= step ? glm::vec2(goal) : glm::vec2(glm::dvec2(in.HorizontalVelocity) + diff * (step / std::max(distance, 1e-12)));
        if (std::abs(move.x) > .01) next.Facing = move.x > 0 ? 1.0f : -1.0f;
        if ((in.JumpPressed || next.Buffered > 0) && (ground || next.Grace > 0) && !next.GroundLock)
        {
            result.Vertical = s.JumpSpeed;
            result.Jumped = true;
            next.Grace = next.Buffered = 0;
            next.GroundLock = true;
            next.OwnJump = true;
            next.CutApplied = false;
        }
        if (next.OwnJump && !in.JumpHeld && !next.CutApplied && result.Vertical > 0)
        {
            result.Vertical *= s.JumpCut;
            next.CutApplied = true;
        }
        state = next;
        output = result;
        return true;
    }
    bool StepCamera(CameraState& state, const CameraInput& in, glm::vec3& position, std::string& error)
    {
        error.clear();
        if (!Finite(in.Current) || !Finite(in.Anchor) || !Finite(in.Offset) || !Finite(in.Look) || !std::isfinite(in.Delta) || in.Delta < 0 || !std::isfinite(in.Damping) || in.Damping < 0 || in.Damping > 10 || !std::isfinite(in.Sensitivity) || std::abs(in.Sensitivity) > 1 || !std::isfinite(in.SnapDistance) || in.SnapDistance < 0 || in.SnapDistance > 10000 || !std::isfinite(state.Yaw) || !std::isfinite(state.Pitch) || glm::length(glm::dvec3(in.Offset)) > 1000)
        {
            error = "Invalid camera settings";
            return false;
        }
        auto next = state;
        if (in.Orbit && in.Delta > 0)
        {
            next.Yaw = float(std::remainder(double(next.Yaw) - double(in.Look.x) * in.Sensitivity, 6.283185307179586));
            next.Pitch = float(std::clamp(double(next.Pitch) - double(in.Look.y) * in.Sensitivity, -1.35, 1.35));
        }
        auto offset = in.Offset;
        if (in.Orbit) offset = glm::angleAxis(next.Yaw, glm::vec3(0, 1, 0)) * glm::angleAxis(next.Pitch, glm::vec3(1, 0, 0)) * offset;
        const auto goal = in.Anchor + offset;
        const bool snap = !next.Initialized || in.Snap || (in.SnapDistance > 0 && glm::length(glm::dvec3(goal) - glm::dvec3(in.Current)) > in.SnapDistance);
        const double alpha = snap || in.Damping == 0 ? 1.0 : -std::expm1(-double(in.Delta) / in.Damping);
        position = glm::vec3(glm::dvec3(in.Current) * (1 - alpha) + glm::dvec3(goal) * alpha);
        next.Initialized = true;
        if (!Finite(position))
        {
            error = "Camera position overflow";
            return false;
        }
        state = next;
        return true;
    }
}
