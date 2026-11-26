#pragma once

#include <Zamin/Zamin.h>

// The game state: the crystal count and the HUD text; T (gamepad Y) - slow motion (Time::SetTimeScale).
class AdventureGame : public Zamin::ScriptableEntity
{
public:
    // The game's clock in slow motion, and how fast it eases there and back (per real second).
    float SlowMotionScale = 0.3f;
    float SlowMotionEase = 3.0f;

    void OnCreate() override;
    void OnUpdate(Zamin::Timestep ts) override;
    void Collect();

private:
    void Refresh();
    void UpdateSlowMotion();
    int m_Collected = 0;
    int m_Total = 0;
    float m_WinTimer = -1.0f;
    bool m_SlowMotion = false;
};

// The camera orbits the target: drag with the right mouse button, Q/E, or the gamepad's right stick.
class OrbitFollowCamera : public Zamin::ScriptableEntity
{
public:
    std::string Target = "Player";  // used when Follow is not set
    Zamin::EntityRef Follow;        // drag the target from the hierarchy
    float Distance = 7.0f;
    float Height = 1.6f;
    float Yaw = 0.0f;     // radians
    float Pitch = 0.35f;  // radians
    float Sensitivity = 0.006f;
    float Smoothness = 10.0f;

    void OnCreate() override;
    void OnLateUpdate(Zamin::Timestep ts) override;
    void OnDestroy() override;

private:
    glm::vec2 m_LastMouse{ 0.0f };
    bool m_Dragging = false;
    glm::vec3 m_Focus{ 0.0f };
    bool m_HasFocus = false;
};
