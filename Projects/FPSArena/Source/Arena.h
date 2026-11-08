#pragma once

// FPS Arena (Zamin Engine namuna o'yini): birinchi shaxs boshqaruvi va nishonga otish.
// Skriptlar: FirstPersonController (sichqoncha / o'ng stik bilan qarash, yurish, yugurish va chidamlilik, sakrash -
// "coyote time" va oldindan bosish buferi bilan, cho'kkalash, sakratgich, bosh tebranishi, qadam tovushlari, FOV,
// qo'nish silkinishi, otishdagi tepki; avtomatik o'yinchi - bot), Blaster (qurol: raycast bilan otish, o'q-dori va
// qayta o'qlash, tepki, nur izi, uchqunlar, chaqnash; botning nishon tanlashi), Target (nishonlar: turg'un,
// sirpanuvchi, chiquvchi, uchar dron - urilganda yiqiladi / portlaydi va qaytadi), TracerFx (nur izi effekti),
// ArenaGame (ikki rejim: parkur yo'lagi va 60 soniyalik "Nishon otish" raundi; ochko, seriya, aniqlik, rekordlar,
// HUD, menyular, demo rejimi).

#include <Zamin/Zamin.h>

#include <optional>
#include <string>
#include <vector>

class ArenaGame;

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
    std::string BotRangeRoute;          // the bot's waypoints in the target range (a small loop)

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
    bool IsGrounded() const { return m_WasGrounded; }
    // the bot's route: 0 the parkour course, 1 the loop of the target range
    void UseRoute(int route);
    // shooting: the view kicks up (eases back by itself); the bot turns to look at a point
    void AddRecoil(float pitchRadians, float yawRadians);
    void SetAimPoint(const std::optional<glm::vec3>& point) { m_AimPoint = point; }
    Zamin::Entity GetHead() const { return m_Head; }

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
    static std::vector<glm::vec3> ParseRoute(const std::string& text);
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
    float m_RecoilPitch = 0.0f;        // the kick still to be undone (eases back)
    // bot
    std::vector<glm::vec3> m_Routes[2];
    std::vector<glm::vec3> m_Route;
    std::optional<glm::vec3> m_AimPoint;
    size_t m_RouteIndex = 0;
    float m_StuckTimer = 0.0f;
    float m_BotTime = 0.0f;
    Zamin::Entity m_Head;
};

// A target: knocked down by a hit, back up a few seconds later. Static: on a stand; Slider: on a rail
// between two points; PopUp: lies flat, springs up for a while now and then; Drone: floats in a circle, bursts.
enum class TargetKind
{
    Static = 0,
    Slider,
    PopUp,
    Drone
};

class Target : public Zamin::ScriptableEntity
{
public:
    int Kind = 0;                       // TargetKind
    Zamin::EntityRef Hinge;             // turns to knock the target down (Static / Slider / PopUp)
    Zamin::EntityRef Face;              // the part that is hit (a kinematic body)
    glm::vec3 PointB{ 0.0f };           // Slider: the other end of the rail (world); Drone: the circle's radius in x
    float Speed = 2.5f;                 // Slider: m/s; Drone: radians/s
    float RespawnTime = 4.0f;
    int Points = 50;

    void OnCreate() override;
    void OnUpdate(Zamin::Timestep ts) override;

    // ---- for the weapon and the game
    bool IsUp() const { return m_State == State::Up; }
    // a hit at `point` (world): the points it gives (0: it was not up); bullseye: hit in the centre
    int Hit(const glm::vec3& point, const glm::vec3& direction, bool& bullseye);
    glm::vec3 GetAimPoint() const;      // the centre of its face (world), a little ahead of a moving one
    bool Owns(Zamin::Entity entity) const;
    void ResetTarget(bool rangeMode);   // back up at once (a new session); the range: faster respawns
    TargetKind GetKind() const { return (TargetKind)Kind; }

private:
    enum class State { Up, Falling, Down, Rising };

    void SetFaceGlow(float strength, const glm::vec3& color);
    void Knock(const glm::vec3& direction);
    void Raise();

    State m_State = State::Up;
    float m_Timer = 0.0f;
    float m_Angle = 0.0f;               // the hinge's angle now (0: up, -90 deg: down)
    float m_Phase = 0.0f;               // Slider / Drone motion
    float m_Flash = 0.0f;
    float m_PopTimer = 0.0f;            // PopUp: how long it stays up
    float m_DownWait = 0.0f;            // how long it lies down this time
    float m_RespawnScale = 1.0f;
    glm::vec3 m_Start{ 0.0f };
    glm::vec3 m_Velocity{ 0.0f };       // of the face, for aiming ahead
    glm::vec3 m_LastFace{ 0.0f };
    glm::vec3 m_BaseEmission{ 0.0f };
    float m_BaseEmissionStrength = 0.0f;
    Zamin::Entity m_Hinge, m_Face;
    Zamin::RandomGenerator m_Rng;
};

// The beam of a shot: a thin glowing bar from the muzzle to what it hit, thinning out in a moment.
class TracerFx : public Zamin::ScriptableEntity
{
public:
    float Lifetime = 0.09f;
    void Setup(const glm::vec3& from, const glm::vec3& to, const glm::vec3& color);
    void OnUpdate(Zamin::Timestep ts) override;

private:
    float m_Age = 0.0f;
    float m_Width = 0.03f;
};

// The weapon (a child of the camera): hold "Fire" to shoot (a ray from the eye), "Reload" (or an empty
// magazine) reloads. A shot kicks the view up, flashes the muzzle, draws a beam and throws sparks where it
// lands; dynamic bodies (the crates) are pushed, targets fall.
class Blaster : public Zamin::ScriptableEntity
{
public:
    Zamin::EntityRef Player;            // the FirstPersonController
    Zamin::EntityRef Muzzle;            // the barrel's tip: the flash light and the beam's start
    Zamin::EntityRef MuzzleFlash;       // an emissive blob shown for a moment
    Zamin::EntityRef TracerTemplate, ImpactSparks, ImpactLight;
    float FireRate = 7.0f;              // shots per second
    int MagazineSize = 12;
    float ReloadTime = 1.1f;
    float Range = 120.0f;
    float Spread = 0.004f;              // radians standing still; more when moving / in the air
    float Recoil = 0.018f;              // radians the view kicks up per shot
    float PushImpulse = 9.0f;           // what a shot gives a dynamic body

    void OnCreate() override;
    // after the controller has turned the view this frame: the shot goes where the camera now looks
    void OnLateUpdate(Zamin::Timestep ts) override;

    void SetFireEnabled(bool enabled) { m_FireEnabled = enabled; }
    void Refill();
    int GetAmmo() const { return m_Ammo; }
    bool IsReloading() const { return m_ReloadTimer > 0.0f; }
    float GetReload01() const;

private:
    void Fire(class FirstPersonController& fpc);
    void Impact(const glm::vec3& point, const glm::vec3& normal, bool target);
    void StartReload();
    void ThinkBot(class FirstPersonController& fpc, float dt, bool& wantFire);
    Target* FindTarget(Zamin::Entity hit) const;

    bool m_FireEnabled = true;
    int m_Ammo = 12;
    float m_Cooldown = 0.0f;
    float m_ReloadTimer = 0.0f;
    float m_Kick = 0.0f, m_KickVelocity = 0.0f;   // the gun's own kick back (a spring)
    float m_FlashTimer = 0.0f;
    float m_LightTimer = 0.0f;
    float m_Sway = 0.0f;
    float m_WallPull = 0.0f;                      // pulled back when the barrel would go into a wall
    glm::vec3 m_Rest{ 0.0f };                     // the gun's local position at rest
    // the bot
    Target* m_BotTarget = nullptr;
    float m_BotSearch = 0.0f;
    float m_BotLock = 0.0f;
    float m_BotCalm = 0.0f;                       // a short pause between targets
    glm::vec3 m_BotJitter{ 0.0f };
    Zamin::RandomGenerator m_Rng;
};

class ArenaGame : public Zamin::ScriptableEntity
{
public:
    Zamin::EntityRef Player, Gun, Checkpoints, JumpPad, Targets;
    Zamin::EntityRef Hud, TimerText, BestText, ProgressText, LapsText, SpeedText, StateText, StaminaBar, HintText;
    Zamin::EntityRef ScoreText, StreakText, AmmoText, ReloadBar, HitMarker, Popups;
    Zamin::EntityRef Banner, BannerSub, DemoBadge;
    Zamin::EntityRef MainMenu, PauseMenu, MenuBest, PlayButton, LanguageButton, ResumeButton, SensitivitySlider, SensitivityText;
    Zamin::EntityRef ResultsMenu, ResultsTitle, ResultsScore, ResultsStats, ResultsRecord, RetryButton;
    float CheckpointRadius = 1.8f;
    float RangeSeconds = 60.0f;   // the target range round
    bool AutoDemo = false;        // start as a demo (the bot plays) - also env ZAMIN_DEMO=1 (ZAMIN_DEMO=range: the range)

    void OnCreate() override;
    void OnStart() override;
    void OnUpdate(Zamin::Timestep ts) override;
    void OnDestroy() override;
    void OnLanguageChanged(const std::string& language) override;

    static ArenaGame* Get() { return s_Instance; }
    // ---- from the weapon
    void OnShot(bool hit);
    void OnTargetHit(const Target& target, int points, bool bullseye);
    bool IsBotPlaying() const { return m_Bot; }

private:
    enum class State { Menu, Playing, Paused, Results };
    enum class Mode { Parkour, Range };

    void StartSession(bool bot);
    void ShowMenu();
    void StartGame(Mode mode);
    void OpenPause();
    void ClosePause();
    void EndRange();
    void UpdateCourse(float dt);
    void UpdateRange(float dt);
    void StyleCheckpoints();
    void ReachCheckpoint();
    void UpdateHud(float dt);
    void ShowBanner(const std::string& title, const std::string& subtitle, float seconds);
    void ShowPopup(const std::string& text, const glm::vec4& color);
    void RefreshTexts();
    void ApplyCursor();
    void SetText(const Zamin::EntityRef& ref, const std::string& text);
    void SetActiveRef(const Zamin::EntityRef& ref, bool active);
    FirstPersonController* GetController() const;
    Blaster* GetBlaster() const;
    int GetAccuracy() const { return m_Shots > 0 ? (int)std::lround(100.0 * m_Hits / m_Shots) : 0; }
    static std::string FormatTime(float seconds);

    static inline ArenaGame* s_Instance = nullptr;
    State m_State = State::Menu;
    Mode m_Mode = Mode::Parkour;
    bool m_Bot = true;
    bool m_DemoRun = false;
    Mode m_DemoMode = Mode::Parkour;
    std::vector<Zamin::Entity> m_Points;
    std::vector<Zamin::Entity> m_PopupPool;
    size_t m_NextPopup = 0;
    size_t m_Next = 0;
    float m_LapTime = 0.0f;
    float m_BestLap = 0.0f;   // 0: none yet
    int m_BestRange = 0;
    int m_Laps = 0;
    int m_Score = 0, m_Shots = 0, m_Hits = 0, m_Streak = 0, m_BestStreak = 0, m_TargetsDown = 0;
    float m_RangeLeft = 0.0f;
    float m_ResultsTimer = 0.0f;
    float m_HitMarker = 0.0f;
    float m_BannerTimer = 0.0f;
    float m_DemoLogTimer = 0.0f;
    float m_Sensitivity = 1.0f;
    float m_Time = 0.0f;
    int m_LastSecond = -1;
};
