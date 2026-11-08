#pragma once

// FPS Arena (Zamin Engine namuna o'yini): birinchi shaxs boshqaruvi.
// Skriptlar: FirstPersonController (sichqoncha / o'ng stik bilan qarash, yurish, yugurish va chidamlilik, sakrash -
// "coyote time" va oldindan bosish buferi bilan, cho'kkalash, sakratgich, bosh tebranishi, qadam tovushlari, FOV,
// qo'nish silkinishi; avtomatik o'yinchi - bot), ArenaGame (parkur yo'lagi: nuqtalar, aylana vaqti va rekord,
// HUD, menyular, demo rejimi).

#include <Zamin/Zamin.h>

#include <string>
#include <vector>

class FirstPersonController : public Zamin::ScriptableEntity
{
public:
    Zamin::EntityRef Head;              // the camera (a child at eye height): pitch, bob, roll, FOV
    float WalkSpeed = 5.5f;
    float SprintSpeed = 9.0f;
    float CrouchSpeed = 2.6f;
    float Acceleration = 14.0f;         // on the ground
    float AirAcceleration = 3.5f;       // steering in the air
    float JumpHeight = 1.35f;           // metres
    float MouseSensitivity = 0.0022f;   // radians per pixel (x the pause menu's multiplier)
    float GamepadLookSpeed = 3.2f;      // radians per second at full deflection
    bool InvertY = false;
    float FieldOfView = 68.0f;          // degrees (vertical)
    float SprintFovBoost = 7.0f;
    float EyeHeight = 1.62f;
    float CrouchEyeHeight = 1.05f;
    float HeadBob = 0.045f;
    float StaminaSeconds = 4.0f;        // how long a full sprint lasts
    float JumpPadSpeed = 15.0f;         // upwards speed a jump pad gives
    std::string BotRoute;               // the bot's waypoints: "x,y,z;x,y,z;..."

    void OnCreate() override;
    void OnUpdate(Zamin::Timestep ts) override;

    void ResetTo(const glm::vec3& position, float yaw, bool bot);
    void SetControlsEnabled(bool enabled) { m_ControlsEnabled = enabled; }
    void SetSensitivityScale(float scale) { m_SensitivityScale = scale; }
    bool IsBot() const { return m_Bot; }
    float GetHorizontalSpeed() const { return glm::length(m_Velocity); }
    float GetStamina01() const { return m_Stamina / std::max(StaminaSeconds, 0.01f); }
    const char* GetStateKey() const;    // localization key of what it is doing (hud.walk, hud.sprint ...)
    glm::vec3 GetCenter() const { return m_Position + glm::vec3(0.0f, 0.9f, 0.0f); }
    float GetDistanceTravelled() const { return m_Distance; }

private:
    struct Controls
    {
        glm::vec2 Move{ 0.0f };   // x right, y forward
        glm::vec2 Look{ 0.0f };   // radians this frame (yaw, pitch)
        bool Jump = false;        // pressed this frame
        bool Sprint = false;
        bool Crouch = false;
    };
    Controls ReadInput(float dt);
    Controls ThinkBot(float dt);
    void ParseRoute();
    void UpdateCamera(float dt, bool grounded, float speed01);

    bool m_Bot = true;
    bool m_ControlsEnabled = true;
    float m_SensitivityScale = 1.0f;
    float m_Yaw = 0.0f, m_Pitch = 0.0f;
    glm::vec3 m_Position{ 0.0f };
    glm::vec2 m_Velocity{ 0.0f };      // horizontal (x, z)
    float m_CoyoteTimer = 0.0f;
    float m_JumpBuffer = 0.0f;
    float m_Stamina = 4.0f;
    bool m_Exhausted = false;
    bool m_Sprinting = false;
    bool m_Crouching = false;
    bool m_WasGrounded = true;
    bool m_PadLaunch = false;
    float m_FallSpeed = 0.0f;          // the vertical speed just before landing
    float m_BobPhase = 0.0f;
    float m_Eye = 1.62f;
    float m_LandDip = 0.0f, m_LandDipVelocity = 0.0f;
    float m_Roll = 0.0f;
    float m_Fov = 68.0f;
    float m_Distance = 0.0f;
    int m_LastStep = 0;
    // bot
    std::vector<glm::vec3> m_Route;
    size_t m_RouteIndex = 0;
    float m_StuckTimer = 0.0f;
    float m_BotTime = 0.0f;
    Zamin::Entity m_Head;
};

class ArenaGame : public Zamin::ScriptableEntity
{
public:
    Zamin::EntityRef Player, Checkpoints, JumpPad;
    Zamin::EntityRef Hud, TimerText, BestText, ProgressText, LapsText, SpeedText, StateText, StaminaBar, HintText;
    Zamin::EntityRef Banner, BannerSub, DemoBadge;
    Zamin::EntityRef MainMenu, PauseMenu, MenuBest, PlayButton, LanguageButton, ResumeButton, SensitivitySlider, SensitivityText;
    float CheckpointRadius = 1.8f;
    bool AutoDemo = false;  // start as a demo (the bot plays) - also env ZAMIN_DEMO=1

    void OnCreate() override;
    void OnStart() override;
    void OnUpdate(Zamin::Timestep ts) override;
    void OnDestroy() override;
    void OnLanguageChanged(const std::string& language) override;

private:
    enum class State { Menu, Playing, Paused };

    void StartSession(bool bot);
    void ShowMenu();
    void StartGame();
    void OpenPause();
    void ClosePause();
    void UpdateCourse(float dt);
    void StyleCheckpoints();
    void ReachCheckpoint();
    void UpdateHud();
    void ShowBanner(const std::string& title, const std::string& subtitle, float seconds);
    void RefreshTexts();
    void ApplyCursor();
    void SetText(const Zamin::EntityRef& ref, const std::string& text);
    void SetActiveRef(const Zamin::EntityRef& ref, bool active);
    FirstPersonController* GetController() const;
    static std::string FormatTime(float seconds);

    State m_State = State::Menu;
    bool m_Bot = true;
    bool m_DemoRun = false;
    std::vector<Zamin::Entity> m_Points;
    size_t m_Next = 0;
    float m_LapTime = 0.0f;
    float m_BestLap = 0.0f;   // 0: none yet
    int m_Laps = 0;
    float m_BannerTimer = 0.0f;
    float m_DemoLogTimer = 0.0f;
    float m_Sensitivity = 1.0f;
    float m_Time = 0.0f;
};
