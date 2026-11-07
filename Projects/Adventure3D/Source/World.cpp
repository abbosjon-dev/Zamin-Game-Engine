#include "Adventure.h"

#include <algorithm>
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

    // rotate: RMB drag, Q/E, gamepad right stick. While dragging the cursor is hidden and locked (Cursor API):
    // it stays where the drag began and the mouse's movement keeps turning the camera at the screen's edges.
    glm::vec2 mouse = GetMousePosition();
    bool rmb = Input::IsMouseButtonPressed(MouseButton::Right);
    if (rmb != m_Dragging)
        Cursor::SetLockMode(rmb ? CursorLockMode::Locked : CursorLockMode::None);
    if (rmb && m_Dragging)
    {
        // Locked: the window's (unlimited) movement; else (the editor's game view without the focus) the pointer's.
        glm::vec2 d = Cursor::GetApplied().Mode == WindowCursorMode::Disabled ? Input::GetMouseDelta() : mouse - m_LastMouse;
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
void OrbitFollowCamera::OnDestroy()
{
    // A scene switch in the middle of a drag: the cursor does not stay locked.
    if (m_Dragging)
        Cursor::SetLockMode(CursorLockMode::None);
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
    UpdateSlowMotion();
    if (m_WinTimer > 0.0f)
    {
        m_WinTimer -= ts;
        if (m_WinTimer <= 0.0f) LoadScene("Scenes/Island.zscene");
    }
}

// Slow motion: T / gamepad Y eases the game's clock to SlowMotionScale and back. The ease runs on real time
// (Time::GetUnscaledDeltaTime): it takes as long whatever the scale is. Physics, animations, particles and
// timers follow the clock; the camera orbit keys too (they use the scaled time).
void AdventureGame::UpdateSlowMotion()
{
    if (Input::IsKeyJustPressed(Key::T) || Input::IsGamepadButtonJustPressed(GamepadButton::Y))
    {
        m_SlowMotion = !m_SlowMotion;
        ZM_INFO("Sekinlashtirish: {}", m_SlowMotion ? "yoqildi" : "o'chirildi");
    }
    if (Time::IsPaused())
        return;
    const float target = m_SlowMotion ? std::clamp(SlowMotionScale, 0.05f, 1.0f) : 1.0f;
    const float scale = Time::GetTimeScale();
    if (scale == target)
        return;
    const float step = Time::GetUnscaledDeltaTime() * SlowMotionEase;
    Time::SetTimeScale(scale < target ? std::min(target, scale + step) : std::max(target, scale - step));
    Refresh();
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
        if (const float scale = Time::GetTimeScale(); scale < 0.999f)
            text->Text += std::format("   -   Sekin: {:.0f}%", scale * 100.0f);
    }
}
ZM_REGISTER_SCRIPT(AdventureGame, ZM_FIELD(AdventureGame, SlowMotionScale), ZM_FIELD(AdventureGame, SlowMotionEase))

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

// Darvoza: qahramon yaqinlashsa ochiladi, uzoqlashsa (biroz kutib) yopiladi. Harakatning o'zi
// Animations/DarvozaOchilishi.zanim klipida (Animation Player): ikki tabaqa oshiq atrofida buriladi,
// chiroq qizil -> sariq -> yashil bo'ladi. Ovozlar klip event'laridan: "Gichirlash" (ochilish /
// yopilish boshida) va "Taqillash" (yopilib bo'lganda) - har biri qaysi yo'nalishda kerakligini
// event parametri aytadi.
class ProximityDoor : public ScriptableEntity
{
public:
    std::string Player = "Player";
    std::string Clip = "DarvozaOchilishi";
    float OpenRadius = 4.5f;   // closer than this (XZ): opens
    float CloseRadius = 6.5f;  // farther than this for CloseDelay seconds: closes
    float CloseDelay = 1.0f;
    std::string CreakSound = "Audio/DoorCreak.wav";
    std::string ThudSound = "Audio/DoorThud.wav";
    bool ShowLabel = true;
    float LabelHeight = 2.6f;  // on the door's front, under the lamp (in view of the editor's play mode too)

    void OnCreate() override
    {
        AddAnimationEventHandler("Gichirlash", [this](const AnimationEventInfo& e) {
            // The clip has a creak near each end; the one for the way the door moves is heard.
            if ((e.Parameter == "ochilish") != m_Opening)
                return;
            Sound(CreakSound, 0.9f, { 0.9f, 1.1f });
            m_LastEvent = std::format("{} ({})", e.Name, e.Parameter);
        });
        AddAnimationEventHandler("Taqillash", [this](const AnimationEventInfo& e) {
            if (!m_Opening)
            {
                Sound(ThudSound, 1.0f, { 0.95f, 1.05f });
                m_LastEvent = e.Name;
            }
        });
    }

    void OnUpdate(Timestep ts) override
    {
        Entity player = FindEntity(Player);
        auto* anim = TryGetComponent<AnimationPlayerComponent>();
        if (!player || !anim)
            return;
        glm::vec3 d = GetScene().GetWorldPosition(player) - GetWorldPosition();
        d.y = 0.0f;
        const float distance = glm::length(d);
        if (!m_Opening && distance < OpenRadius)
            Turn(*anim, true);
        else if (m_Opening && distance > CloseRadius)
        {
            if ((m_Away += ts) >= CloseDelay)
                Turn(*anim, false);
        }
        else
            m_Away = 0.0f;

        if (ShowLabel)
        {
            const glm::vec3 p = GetWorldPosition() + glm::vec3(0.0f, LabelHeight, 0.9f);
            const char* state = anim->IsPlaying() ? (m_Opening ? "ochilmoqda" : "yopilmoqda") : (m_Opening ? "ochiq" : "yopiq");
            DebugDraw::Text(p, std::format("Darvoza: {}", state), glm::vec4(0.9f, 0.92f, 0.98f, 1.0f));
            if (!m_LastEvent.empty())
                DebugDraw::Text(p - glm::vec3(0.0f, 0.4f, 0.0f), "event: " + m_LastEvent, glm::vec4(1.0f, 0.82f, 0.3f, 1.0f));
        }
    }

private:
    // Plays the clip forwards (open) or backwards (close) from where it is now.
    void Turn(AnimationPlayerComponent& anim, bool open)
    {
        m_Opening = open;
        m_Away = 0.0f;
        const bool started = anim.Current >= 0;
        const float from = started ? anim.GetTime() : (open ? 0.0f : anim.GetDuration());
        anim.PlayFrom(Clip, from, open ? 1.0f : -1.0f);
    }

    void Sound(const std::string& path, float volume, glm::vec2 pitch)
    {
        SoundParams params;
        params.Volume = volume;
        params.PitchRandom = pitch;
        params.Spatial = true;
        params.Position = GetWorldPosition() + glm::vec3(0.0f, 1.8f, 0.0f);
        params.MinDistance = 2.0f;
        params.MaxDistance = 30.0f;
        PlaySound(path, params);
    }

    bool m_Opening = false;
    float m_Away = 0.0f;
    std::string m_LastEvent;
};
ZM_REGISTER_SCRIPT(ProximityDoor, ZM_FIELD(ProximityDoor, Player), ZM_FIELD(ProximityDoor, Clip), ZM_FIELD(ProximityDoor, OpenRadius),
                   ZM_FIELD(ProximityDoor, CloseRadius), ZM_FIELD(ProximityDoor, CloseDelay), ZM_FIELD(ProximityDoor, CreakSound),
                   ZM_FIELD(ProximityDoor, ThudSound), ZM_FIELD(ProximityDoor, ShowLabel), ZM_FIELD(ProximityDoor, LabelHeight))
