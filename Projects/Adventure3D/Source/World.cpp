#include "Adventure.h"

#include <cmath>

using namespace Zamin;

// ------------------------------------------------------------------ camera
void OrbitFollowCamera::OnCreate()
{
    m_LastMouse = GetMousePosition();
}

void OrbitFollowCamera::OnLateUpdate(Timestep ts)
{
    Entity target = Follow ? Resolve(Follow) : FindEntity(Target);
    if (!target) return;

    // rotate: RMB drag, Q/E, gamepad right stick
    glm::vec2 mouse = GetMousePosition();
    bool rmb = Input::IsMouseButtonPressed(MouseButton::Right);
    if (rmb && m_Dragging)
    {
        glm::vec2 d = mouse - m_LastMouse;
        Yaw -= d.x * Sensitivity;
        Pitch = std::clamp(Pitch + d.y * Sensitivity, -0.2f, 1.2f);
    }
    m_Dragging = rmb;
    m_LastMouse = mouse;
    if (Input::IsKeyPressed(Key::Q)) Yaw += 2.0f * (float)ts;
    if (Input::IsKeyPressed(Key::E)) Yaw -= 2.0f * (float)ts;
    Yaw -= Input::GetGamepadAxis(GamepadAxis::RightX) * 2.5f * (float)ts;
    Pitch = std::clamp(Pitch + Input::GetGamepadAxis(GamepadAxis::RightY) * 1.5f * (float)ts, -0.2f, 1.2f);

    glm::vec3 goal = GetScene().GetWorldPosition(target) + glm::vec3(0.0f, Height, 0.0f);
    if (!m_HasFocus) { m_Focus = goal; m_HasFocus = true; }
    m_Focus += (goal - m_Focus) * (1.0f - std::exp(-Smoothness * (float)ts));

    glm::vec3 offset = { std::sin(Yaw) * std::cos(Pitch), std::sin(Pitch), std::cos(Yaw) * std::cos(Pitch) };
    auto& t = Transform();
    t.Translation = m_Focus + offset * Distance;
    glm::vec3 dir = glm::normalize(m_Focus - t.Translation);
    t.Rotation = { std::asin(dir.y), std::atan2(-dir.x, -dir.z), 0.0f };
}
ZM_REGISTER_SCRIPT(OrbitFollowCamera, ZM_FIELD(OrbitFollowCamera, Follow), ZM_FIELD(OrbitFollowCamera, Target), ZM_FIELD(OrbitFollowCamera, Distance),
                   ZM_FIELD(OrbitFollowCamera, Height), ZM_FIELD(OrbitFollowCamera, Yaw), ZM_FIELD(OrbitFollowCamera, Pitch))

// ------------------------------------------------------------------ game state / HUD
void AdventureGame::OnCreate()
{
    m_Total = 0;
    for (Entity e : GetScene().GetEntitiesInOrder())
        if (e.GetName() == "Crystal") m_Total++;
    Refresh();
}

void AdventureGame::OnUpdate(Timestep ts)
{
    if (m_WinTimer > 0.0f)
    {
        m_WinTimer -= ts;
        if (m_WinTimer <= 0.0f) LoadScene("Scenes/Island.zscene");
    }
}

void AdventureGame::Collect()
{
    m_Collected++;
    Refresh();
    if (m_Collected >= m_Total)
    {
        ZM_INFO("Barcha kristallar yig'ildi!");
        m_WinTimer = 4.0f;
    }
}

void AdventureGame::Refresh()
{
    if (auto* text = TryGetComponent<TextComponent>())
    {
        text->Text = std::format("Kristallar: {} / {}", m_Collected, m_Total);
        if (m_Total > 0 && m_Collected >= m_Total) text->Text += "   -   G'alaba!";
    }
}
ZM_REGISTER_SCRIPT(AdventureGame)

// Kristal: aylanadi, suzadi; o'yinchi yaqinlashsa yig'iladi.
class Crystal : public ScriptableEntity
{
public:
    float PickupRadius = 1.1f;
    std::string Effect = "Prefabs/CrystalBurst.zprefab";

    void OnCreate() override { m_BaseY = Transform().Translation.y; m_Phase = Transform().Translation.x * 0.7f; }

    void OnUpdate(Timestep) override
    {
        auto& t = Transform();
        t.Rotation.y = GetTime() * 1.6f + m_Phase;
        t.Translation.y = m_BaseY + std::sin(GetTime() * 2.0f + m_Phase) * 0.15f;

        Entity player = FindEntity("Player");
        if (!player || m_Done) return;
        glm::vec3 p = GetScene().GetWorldPosition(player) + glm::vec3(0, 0.9f, 0);
        if (glm::length(p - GetWorldPosition()) < PickupRadius)
        {
            m_Done = true;
            Instantiate(Effect, GetWorldPosition());
            if (auto* game = GetScript<AdventureGame>(FindEntity("Game"))) game->Collect();
            Destroy();
        }
    }

private:
    float m_BaseY = 0.0f, m_Phase = 0.0f;
    bool m_Done = false;
};
ZM_REGISTER_SCRIPT(Crystal, ZM_FIELD(Crystal, PickupRadius), ZM_FIELD(Crystal, Effect))

// Kinematik platforma (ustidagi qahramonni olib yuradi).
class MovingPlatform3D : public ScriptableEntity
{
public:
    glm::vec3 Offset = { 0.0f, 0.0f, 4.0f };
    float Speed = 0.6f;
    void OnCreate() override { m_Start = Transform().Translation; }
    void OnUpdate(Timestep) override
    {
        float s = (std::sin(GetTime() * Speed) + 1.0f) * 0.5f;
        Transform().Translation = m_Start + Offset * s;
    }
private:
    glm::vec3 m_Start{ 0.0f };
};
ZM_REGISTER_SCRIPT(MovingPlatform3D, ZM_FIELD(MovingPlatform3D, Offset), ZM_FIELD(MovingPlatform3D, Speed))

class Spinner : public ScriptableEntity
{
public:
    glm::vec3 Speed = { 0.0f, 0.8f, 0.0f };
    void OnUpdate(Timestep ts) override { Transform().Rotation += Speed * (float)ts; }
};
ZM_REGISTER_SCRIPT(Spinner, ZM_FIELD(Spinner, Speed))

class AutoDestroy : public ScriptableEntity
{
public:
    float Seconds = 1.5f;
    void OnUpdate(Timestep ts) override { if ((Seconds -= ts) <= 0.0f) Destroy(); }
};
ZM_REGISTER_SCRIPT(AutoDestroy, ZM_FIELD(AutoDestroy, Seconds))
