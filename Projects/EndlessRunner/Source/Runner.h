#pragma once

// Ipak Yo'li - Zamin Engine namuna o'yini: 3D cheksiz yuguruvchi (endless runner).
// Skriptlar: RunnerGame (dunyo: yo'l bo'laklarini protsedurali yaratish - to'siqlar naqshlari, tangalar, bonuslar,
// manzara; tezlikning oshishi; to'qnashuvlar; hisob va rekord; HUD, menyular, demo rejimi) va Runner (yuguruvchi:
// uch yo'lak orasida o'tish, sakrash, sirpanish, yugurish animatsiyasi; avtomatik o'yinchi - bot).

#include <Zamin/Zamin.h>

#include <string>
#include <vector>

enum class ObstacleKind
{
    Cart = 0,  // a cart in one lane: jump it or go round it
    Fence,     // a low fence across the road: jump
    Gate,      // a carpet hanging across the road: slide under it
    Wall,      // a tall wall in one lane: change lanes
    Caravan    // a long covered wagon in one lane: change lanes
};

enum class PickupKind
{
    Coin = 0,
    Magnet,    // coins fly to the runner for a while
    Shield     // one hit is forgiven
};

class RunnerGame;

class Runner : public Zamin::ScriptableEntity
{
public:
    Zamin::EntityRef Body, Head, ArmL, ArmR, LegL, LegR, Dust, ShieldBubble;
    float LaneWidth = 2.3f;
    float JumpSpeed = 10.5f;
    float Gravity = 32.0f;
    float SlideTime = 0.75f;

    void OnCreate() override;
    void OnUpdate(Zamin::Timestep ts) override;

    void ResetRunner(bool bot);
    // one step of the run (RunnerGame calls it in small steps); `first`: the frame's first step reads the controls
    void Tick(float dt, float frameDt, bool first);
    void SetBot(bool bot) { m_Bot = bot; }
    bool IsBot() const { return m_Bot; }
    void Crash();
    bool IsCrashed() const { return m_Crashed; }
    // the runner's box now (x centre, feet height, z), for collisions
    float GetX() const { return m_X; }
    float GetY() const { return m_Y; }
    float GetZ() const { return m_Z; }
    float GetHeight() const { return m_SlideTimer > 0.0f ? 0.85f : 1.75f; }
    int GetLane() const { return m_Lane; }
    bool IsGrounded() const { return m_Y <= 0.0f && m_VY <= 0.0f; }
    bool IsSliding() const { return m_SlideTimer > 0.0f; }
    void SetShieldVisible(bool visible, float time);

private:
    struct Controls
    {
        int Lane = 0;         // -1 / +1: a lane change this frame
        bool Jump = false;
        bool Slide = false;
    };
    Controls ReadInput();
    Controls ThinkBot(float dt);
    void Animate(float dt);

    bool m_Bot = true;
    bool m_Crashed = false;
    int m_Lane = 0;
    float m_X = 0.0f, m_Y = 0.0f, m_Z = 0.0f, m_VY = 0.0f;
    float m_SlideTimer = 0.0f;
    float m_Phase = 0.0f;
    float m_Tilt = 0.0f;
    float m_CrashTime = 0.0f;
    float m_StickPrev = 0.0f;
    float m_BotCooldown = 0.0f;
    float m_ShieldTime = 0.0f;
    bool m_WasGrounded = true;
    Zamin::RandomGenerator m_Rng;
};

class RunnerGame : public Zamin::ScriptableEntity
{
public:
    // ---- scene references (Tools/samples/gen_endlessrunner.py)
    Zamin::EntityRef Player, CameraRig, Road, Sand, CurbL, CurbR;
    Zamin::EntityRef CartTemplate, FenceTemplate, GateTemplate, WallTemplate, CaravanTemplate;
    Zamin::EntityRef CoinTemplate, MagnetTemplate, ShieldTemplate;
    Zamin::EntityRef TowerTemplate, PalmTemplate, DuneTemplate, HouseTemplate, ArchTemplate;
    Zamin::EntityRef CoinBurst, CrashBurst;
    Zamin::EntityRef Hud, ScoreText, DistanceText, CoinsText, SpeedText, PowerText, BestText, Banner, BannerSub, DemoBadge;
    Zamin::EntityRef MainMenu, MenuBest, PlayButton, LanguageButton, PauseMenu, ResumeButton;
    Zamin::EntityRef GameOverMenu, OverScore, OverStats, OverRecord, RetryButton;
    float StartSpeed = 11.0f;     // m/s
    float MaxSpeed = 30.0f;
    float SpeedGain = 0.01f;      // m/s per metre run
    float SegmentLength = 26.0f;  // one obstacle pattern per segment
    float SpawnAhead = 150.0f;    // how far ahead the world is built
    bool AutoDemo = false;        // the bot runs (also env ZAMIN_DEMO=1)

    void OnCreate() override;
    void OnStart() override;
    void OnUpdate(Zamin::Timestep ts) override;
    void OnDestroy() override;
    void OnLanguageChanged(const std::string& language) override;

    static RunnerGame* Get() { return s_Instance; }
    float GetSpeed() const { return m_Speed; }
    bool IsRunning() const { return m_State == State::Playing || m_State == State::Menu; }
    float LaneX(int lane) const;
    // for the bot: the nearest thing ahead that blocks `lane` (within `range` m of z); kind / distance / passable
    struct Hazard
    {
        bool Found = false;
        ObstacleKind Kind = ObstacleKind::Cart;
        float Distance = 1e9f;    // from the runner's front to the obstacle's near end
        float Length = 0.0f;      // along the road
        bool AllLanes = false;
    };
    Hazard FindHazard(int lane, float z, float range) const;
    int CountCoins(int lane, float z, float range) const;
    void OnRunnerCrashed();

private:
    enum class State { Menu, Playing, Paused, Crashed, GameOver };

    struct Item
    {
        Zamin::Entity E;
        bool IsObstacle = true;
        ObstacleKind Obstacle = ObstacleKind::Cart;
        PickupKind Pickup = PickupKind::Coin;
        int Lane = 0;             // -2: all lanes
        float Z = 0.0f;           // centre
        float Length = 1.0f;
        float Bottom = 0.0f, Top = 1.0f;
        float Y = 0.8f;           // pickups: height
        bool Taken = false;
        bool Hit = false;
    };

    void StartRun(bool bot);
    void ShowMenu();
    void OpenPause();
    void ClosePause();
    void GameOver();
    void ClearWorld();

    void UpdateWorld(float dt);
    void SpawnSegment(float z0);
    void SpawnObstacle(ObstacleKind kind, int lane, float z);
    void SpawnPickup(PickupKind kind, int lane, float z, float y);
    void SpawnCoinRow(int lane, float z0, int count, bool arc);
    void SpawnScenery(float z);
    void CheckCollisions();
    void UpdateCamera(float dt);
    void UpdateHud();
    void ShowBanner(const std::string& title, const std::string& subtitle, float seconds);
    void RefreshTexts();
    void Burst(const Zamin::EntityRef& ref, const glm::vec3& at, int count);
    void SetText(const Zamin::EntityRef& ref, const std::string& text);
    void SetActiveRef(const Zamin::EntityRef& ref, bool active);
    Runner* GetRunner() const;
    int GetScore() const { return (int)m_Distance + m_Coins * 10; }

    static inline RunnerGame* s_Instance = nullptr;
    State m_State = State::Menu;
    bool m_Bot = true;
    bool m_DemoRun = false;
    std::vector<Item> m_Items;
    std::vector<Zamin::Entity> m_Scenery;
    float m_NextSegmentZ = 0.0f;
    float m_NextSceneryZ = 0.0f;
    float m_NextArchZ = 0.0f;
    int m_Segments = 0;
    float m_Speed = 11.0f;
    float m_Distance = 0.0f;
    float m_PrevZ = 0.0f;           // the runner's z last frame
    int m_Coins = 0;
    float m_MagnetTime = 0.0f, m_ShieldTime = 0.0f;
    int m_BestScore = 0;
    float m_BestDistance = 0.0f;
    float m_CrashTimer = 0.0f;
    float m_OverTimer = 0.0f;
    float m_BannerTimer = 0.0f;
    float m_DemoLogMark = 0.0f;
    float m_MilestoneMark = 0.0f;
    float m_Fov = 62.0f;
    int m_Runs = 0;
    ObstacleKind m_LastHit = ObstacleKind::Cart;
    glm::vec3 m_CamPos{ 0.0f, 3.4f, 7.0f };
    Zamin::RandomGenerator m_Rng;
};
