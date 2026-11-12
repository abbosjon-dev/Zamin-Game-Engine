#pragma once
#include <glm/glm.hpp>
#include <string>
namespace Zamin::Gameplay {
    struct MotorSettings
    {
        float Speed = 5, Acceleration = 30, Deceleration = 35, AirControl = .35f;
        float SprintMultiplier = 1.6f, JumpSpeed = 8, CoyoteSeconds = .1f, BufferSeconds = .12f, JumpCut = .5f;
    };
    struct MotorState
    {
        float Grace = 0, Buffered = 0, Facing = 1;
        bool GroundLock = false, OwnJump = false, CutApplied = false;
    };
    struct MotorInput
    {
        glm::vec2 Move{}, HorizontalVelocity{};
        float VerticalVelocity = 0, Delta = 0;
        bool Grounded = false, JumpPressed = false, JumpHeld = true, Sprint = false, Enabled = true;
    };
    struct MotorOutput
    {
        glm::vec2 Horizontal{};
        float Vertical = 0;
        bool Jumped = false;
    };
    // Transactional on invalid input. Diagonals are clamped; timers use elapsed time, acceleration
    // is capped to a 0.25 s hitch. Delta 0 leaves state and velocity untouched.
    bool StepMotor(const MotorSettings&, MotorState&, const MotorInput&, MotorOutput&, std::string& error);
    struct CameraState
    {
        bool Initialized = false;
        float Yaw = 0, Pitch = 0;
    };
    struct CameraInput
    {
        glm::vec3 Current{}, Anchor{}, Offset{ 0, 2, 6 };
        glm::vec2 Look{};
        float Delta = 0, Damping = .15f, Sensitivity = .003f, SnapDistance = 5;
        bool Orbit = false, Snap = false;
    };
    bool StepCamera(CameraState&, const CameraInput&, glm::vec3& position, std::string& error);
}
