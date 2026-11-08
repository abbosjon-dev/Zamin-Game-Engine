#pragma once

// Vodiy Poygasi - Zamin Engine namuna o'yini: oddiy mashina fizikasi bilan aylanma trassada poyga.
// Skriptlar: Car (arkada fizikasi - tezlanish, tormoz, rul, yon ilashish va drift (qo'l tormozi), trassa chetidagi
// to'siqlar, o't ustida sekinlashish; g'ildiraklar, kuzov og'ishi, tormoz chiroqlari, tutun, dvigatel ovozi;
// haydovchi bot) va RaceGame (trassa markaziy chizig'i, start svetofori va sanoq, nazorat nuqtalari, aylanalar va
// aylana vaqti, o'rinlar, rekord, kamera, HUD, menyular, demo).

#include <Zamin/Zamin.h>

#include <string>
#include <vector>

// The track's centre line: points every ~2 m around the loop (x, z), their running distance.
struct Track
{
    std::vector<glm::vec2> Points;
    std::vector<float> Distance;    // from the start line to each point
    float Length = 0.0f;
    float HalfWidth = 6.0f;         // the asphalt
    float WallOffset = 10.5f;       // the barriers' distance from the centre line

    bool Parse(const std::string& text);
    int Closest(const glm::vec2& p, int hint) const;    // the nearest point (searched around `hint`; -1: everywhere)
    glm::vec2 At(float s) const;                        // the point at distance s (wraps)
    glm::vec2 Tangent(float s) const;
    float DistanceAt(int index, const glm::vec2& p) const; // the distance along the track of p near point `index`
};

class Car : public Zamin::ScriptableEntity
{
public:
    enum class Driver { None = 0, Player, Bot };

    Zamin::EntityRef Body, WheelFL, WheelFR, WheelRL, WheelRR, SteerFL, SteerFR, BrakeLights, Smoke, Dust;
    float EngineAccel = 15.0f;      // m/s^2 at full throttle (falls off towards the top speed)
    float MaxSpeed = 44.0f;         // m/s
    float BrakeDecel = 28.0f;
    float ReverseSpeed = 9.0f;
    float SteerRate = 2.3f;         // rad/s at full lock and moderate speed
    float Grip = 9.0f;              // how fast sideways sliding dies away
    float DriftGrip = 1.4f;         // ... with the handbrake on
    float Skill = 1.0f;             // the bot: its top speed and corner speed (1: the best)
    float LineOffset = 0.0f;        // the bot: its line, metres from the centre (+ right)

    void OnCreate() override;
    void OnUpdate(Zamin::Timestep ts) override;

    void ResetCar(const glm::vec2& position, float yaw);
    void SetDriver(Driver driver) { m_Driver = driver; }
    Driver GetDriver() const { return m_Driver; }
    // one physics step (RaceGame steps every car in small steps); `first`: read the controls / think
    void Simulate(float dt, float frameDt, bool first);
    void Freeze(bool frozen) { m_Frozen = frozen; }
    void Respawn();                 // back on the track's centre line, facing along it

    glm::vec2 GetPosition() const { return m_Pos; }
    glm::vec2 GetVelocity() const { return m_Vel; }
    void SetPositionVelocity(const glm::vec2& p, const glm::vec2& v) { m_Pos = p; m_Vel = v; }
    glm::vec2 GetForward() const { return { -std::sin(m_Yaw), -std::cos(m_Yaw) }; }
    float GetYaw() const { return m_Yaw; }
    float GetSpeed() const { return glm::dot(m_Vel, GetForward()); }
    float GetDrift() const { return std::abs(m_SideSpeed); }
    bool IsOffRoad() const { return m_OffRoad; }
    int GetTrackIndex() const { return m_TrackIndex; }
    float GetTrackDistance() const { return m_TrackS; }

    // race bookkeeping (RaceGame)
    int Lap = 0;                    // laps done
    int NextCheckpoint = 0;         // 0: the start line still ahead
    float LapTime = 0.0f, BestLap = 0.0f, RaceTime = 0.0f;
    bool Finished = false;
    int FinishPlace = 0;

private:
    struct Controls
    {
        float Throttle = 0.0f, Brake = 0.0f, Steer = 0.0f;
        bool Handbrake = false;
    };
    Controls ReadInput() const;
    Controls ThinkBot(float dt);

    Driver m_Driver = Driver::Bot;
    bool m_Frozen = false;
    Controls m_Controls;
    glm::vec2 m_Pos{ 0.0f }, m_Vel{ 0.0f };
    float m_Yaw = 0.0f, m_YawRate = 0.0f;
    float m_SideSpeed = 0.0f;
    float m_Accel = 0.0f, m_LatAccel = 0.0f;
    float m_Roll = 0.0f, m_Pitch = 0.0f;
    float m_WheelSpin = 0.0f, m_SteerAngle = 0.0f;
    bool m_OffRoad = false;
    int m_TrackIndex = 0;
    float m_TrackS = 0.0f;
    float m_StuckTimer = 0.0f;
    float m_HitCooldown = 0.0f;
};

class RaceGame : public Zamin::ScriptableEntity
{
public:
    Zamin::EntityRef PlayerCar, Rival1, Rival2, Rival3, CameraRig, StartLights, Checkpoints, Sparks;
    Zamin::EntityRef Hud, PositionText, LapText, TimerText, BestText, CheckpointText, SpeedText, SpeedBar, DriftText, Banner, BannerSub, DemoBadge;
    Zamin::EntityRef MainMenu, MenuBest, PlayButton, LanguageButton, PauseMenu, ResumeButton;
    Zamin::EntityRef ResultsMenu, ResultsTitle, ResultsPlace, ResultsTimes, ResultsRecord, RetryButton;
    std::string TrackPoints;        // "x,z;x,z;..." from the start line round the loop (Tools/samples/gen_racing.py)
    int Laps = 3;
    int CheckpointCount = 8;
    bool AutoDemo = false;          // the bot drives the player's car (also env ZAMIN_DEMO=1)

    void OnCreate() override;
    void OnStart() override;
    void OnUpdate(Zamin::Timestep ts) override;
    void OnDestroy() override;
    void OnLanguageChanged(const std::string& language) override;

    static RaceGame* Get() { return s_Instance; }
    const Track& GetTrack() const { return m_Track; }
    void OnCarHitWall(Car& car, const glm::vec2& at, float impact);

private:
    enum class State { Menu, Countdown, Racing, Paused, Finished };

    void StartRace(bool bot);
    void ShowMenu();
    void OpenPause();
    void ClosePause();
    void FinishRace();
    std::vector<Car*> GetCars() const;
    void UpdateRace(float dt);
    void UpdateProgress(Car& car);
    void SeparateCars(const std::vector<Car*>& cars);
    float RaceDistance(const Car& car) const;
    int PlaceOf(const Car& car) const;
    void UpdateCamera(float dt);
    void UpdateLights();
    void StyleCheckpoints();
    void UpdateHud();
    void ShowBanner(const std::string& title, const std::string& subtitle, float seconds);
    void RefreshTexts();
    void SetText(const Zamin::EntityRef& ref, const std::string& text);
    void SetActiveRef(const Zamin::EntityRef& ref, bool active);
    static std::string FormatTime(float seconds);

    static inline RaceGame* s_Instance = nullptr;
    Track m_Track;
    State m_State = State::Menu;
    State m_PausedFrom = State::Racing; // where the pause menu goes back to
    bool m_Bot = true;
    bool m_DemoRun = false;
    float m_Countdown = 0.0f;
    int m_LastCount = -1;
    float m_FinishTimer = 0.0f;
    int m_FinishedCount = 0;
    float m_BestLap = 0.0f;
    bool m_LapRecord = false; // a lap of this race set the lap record
    float m_BannerTimer = 0.0f;
    float m_Fov = 60.0f;
    float m_CamYaw = 0.0f;
    float m_Time = 0.0f;
    int m_Races = 0;
    glm::vec3 m_CamPos{ 0.0f, 5.0f, 10.0f };
};
