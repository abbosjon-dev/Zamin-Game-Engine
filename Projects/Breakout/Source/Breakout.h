#pragma once

// Zamin Breakout (Zamin Engine namuna o'yini).
// Skriptlar: BreakoutGame (o'yin oqimi: menyular, darajalar, jonlar, hisob/kombo, bonuslar, rekord, bot va demo),
// PaddleController (raketka), Ball (to'p: Box2D fizikasi devor va g'ishtlar bilan, raketkaga tegish burchagi skriptda),
// Brick (g'isht turlari), PowerUp (tushayotgan bonus kapsulalari), FxAnimator (halqa / nur / ochko effektlari).

#include <Zamin/Zamin.h>

#include <string>
#include <vector>

enum class BrickKind
{
    Normal = 0,  // one hit
    Tough,       // two hits (cracks after the first)
    Steel,       // three hits
    Gold,        // never breaks
    Bomb         // breaks its neighbours too
};

enum class PowerKind
{
    Wide = 0,  // a wider paddle for a while
    Multi,     // two more balls
    Slow,      // slower balls for a while
    Fire,      // balls go through bricks for a while
    Laser,     // the paddle shoots lasers for a while
    Life       // +1 life
};

// A level: its name's localization key and rows of 13 characters (Levels.cpp has the legend).
struct LevelLayout
{
    const char* NameKey;
    std::vector<const char*> Rows;
};
const std::vector<LevelLayout>& GetLevelLayouts();

class PaddleController;
class Ball;
class Brick;

class BreakoutGame : public Zamin::ScriptableEntity
{
public:
    Zamin::EntityRef Paddle, Bricks, BrickTemplate, BallTemplate;
    Zamin::EntityRef WidePowerUp, MultiPowerUp, SlowPowerUp, FirePowerUp, LaserPowerUp, LifePowerUp;
    Zamin::EntityRef BeamTemplate, RingTemplate, PopupTemplate, Shards, Sparks;
    Zamin::EntityRef Hud, ScoreText, BestText, Lives, ComboText, LevelText, LevelNameText, BricksText, PowerText, HintText;
    Zamin::EntityRef Banner, BannerSub, DemoBadge;
    Zamin::EntityRef MainMenu, PauseMenu, GameOverMenu, MenuBest, PlayButton, LanguageButton, ResumeButton, RetryButton;
    Zamin::EntityRef OverTitle, OverScore, OverLevel, OverRecord;
    float FieldHalfWidth = 10.5f;
    float FieldTop = 11.0f;
    float FieldBottom = -12.5f;
    int StartLives = 3;
    float PowerUpChance = 0.14f;
    bool AutoDemo = false;  // start as a demo (the bot plays) - also env ZAMIN_DEMO=1
    int StartLevel = 1;     // the level a game starts at (testing a level; env ZAMIN_START_LEVEL)

    void OnCreate() override;
    void OnStart() override;
    void OnUpdate(Zamin::Timestep ts) override;
    void OnDestroy() override;
    void OnLanguageChanged(const std::string& language) override;

    static BreakoutGame* Get() { return s_Instance; }
    bool IsBot() const { return m_Bot; }
    PaddleController* GetPaddle() const;
    float GetBallSpeed() const;            // the balls' speed now (level, slow power-up, bricks hit)
    bool IsFireActive() const { return m_FireTime > 0.0f; }

    // events from the other scripts
    void OnBrickHit(Brick& brick, bool broken);
    void OnBallLost(Ball& ball);
    void OnPaddleHit(Ball& ball, float offset);
    void OnPowerUpCaught(PowerKind kind, const glm::vec2& position);
    void QueueBreak(Zamin::Entity brick, float delay);  // bombs' chain reactions

    // effects
    void Burst(const glm::vec2& position, const glm::vec3& color, int shards, int sparks);
    void Ring(const glm::vec2& position, const glm::vec3& color, float scale);
    void Popup(const glm::vec2& position, const std::string& text, const glm::vec4& color, float scale = 1.0f);
    void Sfx(const char* name, float volume = 1.0f, float pitch = 1.0f);

private:
    enum class State { Menu, Playing, Paused, LevelClear, GameOver };

    void StartSession(bool bot, bool demoRun, int level);
    void LoadLevel(int index);
    void ClearField();
    void ServeBall();
    void ShowMenu();
    void StartGame();
    void OpenPause();
    void ClosePause();
    void EndGame(bool victory);
    void UpdateBot(float dt);
    void UpdatePowerTimers(float dt);
    void UpdateHud(float realDt);
    void FireLasers();
    void SpawnPowerUp(const glm::vec2& position);
    void ShowBanner(const std::string& title, const std::string& subtitle, float seconds);
    void RefreshMenuTexts();
    void SetText(const Zamin::EntityRef& ref, const std::string& text);
    void SetActiveRef(const Zamin::EntityRef& ref, bool active);
    int CountBreakable() const;
    std::vector<Ball*> LiveBalls() const;
    void LogDemo(const char* reason);

    static BreakoutGame* s_Instance;

    struct PendingBreak
    {
        Zamin::Entity Brick;
        float Timer;
    };

    State m_State = State::Menu;
    bool m_Bot = true;
    bool m_DemoRun = false;
    int m_Level = 0;           // 0-based index into the layouts
    int m_Lives = 3;
    int m_Score = 0;
    float m_ShownScore = 0.0f;
    int m_Combo = 0;
    int m_BestCombo = 0;
    int m_BricksBroken = 0;
    int m_LevelsCleared = 0;
    int m_BricksHitThisLevel = 0;
    int m_BestScore = 0;
    int m_BestLevel = 0;
    bool m_NewRecord = false;
    int m_Sessions = 0;
    float m_WideTime = 0.0f, m_SlowTime = 0.0f, m_FireTime = 0.0f, m_LaserTime = 0.0f;
    float m_LaserCooldown = 0.0f;
    float m_StateTimer = 0.0f;   // LevelClear: until the next level; after a lost ball: until the serve
    float m_ServeTimer = -1.0f;
    float m_BotHold = 0.0f;      // the bot waits a moment before launching
    float m_BannerTimer = 0.0f;
    float m_DemoLogTimer = 0.0f;
    float m_RestartTimer = -1.0f;
    std::vector<PendingBreak> m_PendingBreaks;
    Zamin::RandomGenerator m_Rng{ 99 };
};

class PaddleController : public Zamin::ScriptableEntity
{
public:
    float Speed = 26.0f;  // units per second at most
    float Width = 3.2f;

    void OnCreate() override;
    void OnUpdate(Zamin::Timestep ts) override;

    void ResetPaddle(bool bot);
    void SetBotTarget(float x) { m_BotTarget = x; }
    void SetWide(bool wide) { m_TargetWidth = wide ? Width * 1.6f : Width; }
    void SetLaser(bool on);
    void Bump();
    float GetHalfWidth() const { return m_Width * 0.5f; }
    float GetTop() const { return m_Position.y + 0.27f; }
    glm::vec2 GetPosition() const { return m_Position; }
    float GetVelocityX() const { return m_VelocityX; }
    std::vector<glm::vec2> GetTurrets() const;

private:
    bool m_Bot = true;
    bool m_Laser = false;
    float m_Width = 3.2f, m_TargetWidth = 3.2f;
    float m_BotTarget = 0.0f;
    float m_VelocityX = 0.0f;
    float m_Bump = 0.0f;
    glm::vec2 m_Position{ 0.0f };
    glm::vec2 m_LastMouse{ -1.0f };
    bool m_UsingMouse = false;
    Zamin::Entity m_Visual, m_TurretLeft, m_TurretRight;
};

class Ball : public Zamin::ScriptableEntity
{
public:
    float Speed = 13.0f;     // the base speed at level 1
    float MaxSpeed = 22.0f;

    void OnCreate() override;
    void OnUpdate(Zamin::Timestep ts) override;
    void OnLateUpdate(Zamin::Timestep ts) override;
    void OnCollisionBegin(Zamin::Entity other) override;

    void Hold(float offset);
    void Launch();
    void LaunchDirection(const glm::vec2& direction);
    bool IsHeld() const { return m_Held; }
    bool IsLost() const { return m_Lost; }
    glm::vec2 GetPosition() const { return m_Position; }
    glm::vec2 GetMoveVelocity() const { return m_Velocity; }
    static constexpr float Radius = 0.28f;

private:
    void SetLook(bool fire);
    bool m_Held = true;
    bool m_Lost = false;
    bool m_RestoreVelocity = false;
    bool m_FireLook = false;
    float m_HoldOffset = 0.0f;
    glm::vec2 m_Position{ 0.0f };
    glm::vec2 m_Velocity{ 0.0f, 1.0f };
    Zamin::Entity m_Visual, m_Trail;
};

class Brick : public Zamin::ScriptableEntity
{
public:
    BrickKind Kind = BrickKind::Normal;
    int HitPoints = 1;
    glm::vec3 Color{ 1.0f };
    int Points = 50;

    void OnCreate() override;
    void OnUpdate(Zamin::Timestep ts) override;

    void Setup(BrickKind kind, const glm::vec3& color, float appearDelay);
    bool Hit(bool fire);      // true when it broke
    void Break();
    bool IsBreakable() const { return Kind != BrickKind::Gold; }
    bool IsBroken() const { return m_Broken; }
    glm::vec2 GetPosition() { return glm::vec2(Transform().Translation); }

private:
    void Refresh();
    bool m_Broken = false;
    float m_Appear = 0.0f;   // seconds until it starts to appear (negative: appearing / shown)
    float m_Punch = 0.0f;
    glm::vec3 m_BaseScale{ 1.5f, 0.6f, 1.0f };
    Zamin::Entity m_Visual;
};

class PowerUp : public Zamin::ScriptableEntity
{
public:
    PowerKind Kind = PowerKind::Wide;
    float FallSpeed = 4.5f;

    void OnCreate() override;
    void OnUpdate(Zamin::Timestep ts) override;
    glm::vec2 GetPosition() const { return glm::vec2(m_Position); }
    bool IsGood() const { return true; }

private:
    float m_Age = 0.0f;
    bool m_Done = false;
    glm::vec3 m_Position{ 0.0f };
};

// A short-lived effect: grows, rises and fades, then destroys itself (Sprite Renderer or Text).
class FxAnimator : public Zamin::ScriptableEntity
{
public:
    float Lifetime = 0.5f;
    float StartScale = 1.0f, EndScale = 1.0f;
    float Rise = 0.0f;
    float FadeOut = 0.3f;
    bool Pop = false;
    bool KeepScale = false;  // the spawner sets the scale (a laser beam's length)

    void OnUpdate(Zamin::Timestep ts) override;
    void SetScaleMultiplier(float scale) { m_Multiplier = scale; }

private:
    float m_Age = 0.0f;
    float m_Multiplier = 1.0f;
    glm::vec4 m_Color{ 1.0f };
};

namespace BreakoutUtil {

    template<typename T>
    std::vector<T*> Live(const Zamin::ScriptableEntity& self)
    {
        // the templates' scripts (in the switched-off "Shablonlar" group) are left out
        std::vector<T*> all = self.FindObjectsOfType<T>();
        std::erase_if(all, [](T* script) { return !script->IsActiveInHierarchy(); });
        return all;
    }
    std::string FormatScore(int value);

}
