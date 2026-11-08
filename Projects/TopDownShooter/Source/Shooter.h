#pragma once

// Neon Maydon - top-down shooter (Zamin Engine namuna o'yini).
// Skriptlar: ShooterGame (o'yin oqimi: menyular, to'lqinlar, hisob/kombo, rekord, effektlar, demo rejimi),
// PlayerShip (o'yinchi kemasi: yurish, nishon olish, otish, siltanish, bonuslar, avtomatik o'yinchi - bot),
// Enemy (5 xil dushman), Projectile (o'qlar), Pickup (bonuslar), FxAnimator (halqa / ochko effektlari).

#include <Zamin/Zamin.h>

#include <deque>
#include <string>
#include <vector>

enum class EnemyKind
{
    Chaser = 0,  // Quvg'in: runs at the player and explodes on contact
    Swarmer,     // To'da: small and fast, comes in groups, weaves
    Shooter,     // O'qchi: keeps its distance, circles and fires
    Dasher,      // Hujumchi: winds up, then charges in a straight line
    Tank         // Tank: slow and armoured, fires rings of shots, splits into swarmers
};

enum class PickupKind
{
    Health = 0,  // +1 life
    Rapid,       // twice the fire rate for a while
    Spread       // three bullets per shot for a while
};

class PlayerShip;
class Enemy;

class ShooterGame : public Zamin::ScriptableEntity
{
public:
    // ---- scene references (set by the generator: Tools/samples/gen_topdownshooter.py)
    Zamin::EntityRef Player, Reticle, Shield, Thruster;
    Zamin::EntityRef BulletTemplate, EnemyBulletTemplate;
    Zamin::EntityRef ChaserTemplate, SwarmerTemplate, ShooterTemplate, DasherTemplate, TankTemplate;
    Zamin::EntityRef HealthPickup, RapidPickup, SpreadPickup;
    Zamin::EntityRef PortalTemplate, ShockwaveTemplate, PopupTemplate;
    Zamin::EntityRef Explosion, Sparks, FlashLights;
    Zamin::EntityRef ScoreText, ComboText, ComboBar, WaveText, EnemiesText, Hearts, BestText, PowerText, DashBar;
    Zamin::EntityRef Banner, BannerSub, DemoBadge, Hud;
    Zamin::EntityRef MainMenu, PauseMenu, GameOverMenu, MenuBest;
    Zamin::EntityRef PlayButton, LanguageButton, ResumeButton, RetryButton;
    Zamin::EntityRef OverScore, OverWave, OverRecord;
    glm::vec2 ArenaHalfSize{ 19.0f, 11.0f };
    float ComboWindow = 2.6f;   // seconds between kills that keep a combo going
    bool AutoDemo = false;      // start as a demo (the bot plays) - also env ZAMIN_DEMO=1

    // ---- lifecycle
    void OnCreate() override;
    void OnStart() override;
    void OnUpdate(Zamin::Timestep ts) override;
    void OnDestroy() override;
    void OnLanguageChanged(const std::string& language) override;

    // ---- for the other scripts
    static ShooterGame* Get() { return s_Instance; }
    bool IsGameplayActive() const;          // enemies move, the ship takes input (also behind the menu)
    bool IsDemo() const { return m_Bot; }   // the bot plays (menu attract mode or a demo run)
    PlayerShip* GetPlayer() const;
    glm::vec2 GetPlayerPosition() const;
    int GetWave() const { return m_Wave; }
    glm::vec2 ClampToArena(const glm::vec2& p, float margin) const;

    Zamin::Entity SpawnBullet(const glm::vec2& position, const glm::vec2& direction, bool fromPlayer, float speedScale = 1.0f);
    void SpawnSwarmers(const glm::vec2& center, int count);
    void OnEnemyKilled(Enemy& enemy, bool scored);
    void OnPlayerHurt(const glm::vec2& position);
    void OnPlayerDied(const glm::vec2& position);
    void OnPickupCollected(PickupKind kind, const glm::vec2& position);

    // effects: particles, a light flash, a shockwave ring, camera shake
    void Explode(const glm::vec2& position, const glm::vec3& color, float size);
    void SparksAt(const glm::vec2& position, const glm::vec3& color, int count);
    void Popup(const glm::vec2& position, const std::string& text, const glm::vec4& color, float scale = 1.0f);
    void Sfx(const char* name, float volume = 1.0f, float pitch = 1.0f);

private:
    enum class State { Menu, Playing, Paused, GameOver };
    enum class Phase { Intro, Spawning, Cleared };

    struct PendingSpawn
    {
        EnemyKind Kind;
        glm::vec2 Position;
        float Timer;
    };
    struct Flash
    {
        Zamin::Entity Light;
        float Intensity = 0.0f;
    };

    void StartSession(bool bot, bool demoRun);
    void ClearWorld();
    void ShowMenu();
    void StartGame();
    void OpenPause();
    void ClosePause();
    void GameOver();

    void UpdateWaves(float dt);
    void StartWave(int wave);
    void BuildWaveQueue(int wave);
    bool PickSpawnPoint(glm::vec2& out, float minPlayerDistance);
    void SpawnEnemy(EnemyKind kind, const glm::vec2& position);
    Zamin::Entity TemplateFor(EnemyKind kind) const;
    int CountAliveEnemies() const;

    void UpdateHud(float dt);
    void UpdateFlashes(float dt);
    void ShowBanner(const std::string& title, const std::string& subtitle, float seconds);
    void RefreshMenuTexts();
    void SetText(const Zamin::EntityRef& ref, const std::string& text);
    void SetActiveRef(const Zamin::EntityRef& ref, bool active);
    void LogDemo(const char* reason);

    static ShooterGame* s_Instance;

    State m_State = State::Menu;
    Phase m_Phase = Phase::Intro;
    bool m_Bot = true;          // the ship is flown by the bot
    bool m_DemoRun = false;     // a demo run (ZAMIN_DEMO / AutoDemo): HUD + badge, logs, restarts itself
    float m_PhaseTimer = 0.0f;
    float m_SpawnTimer = 0.0f;
    float m_SpawnInterval = 1.0f;
    float m_BannerTimer = 0.0f;
    float m_RestartTimer = -1.0f;  // after the bot died: seconds until the next session
    float m_DeathTimer = -1.0f;    // after the ship blew up: real seconds until the game over panel
    float m_DemoLogTimer = 0.0f;
    std::deque<EnemyKind> m_Queue;
    std::vector<PendingSpawn> m_Pending;
    std::vector<Flash> m_Flashes;
    size_t m_NextFlash = 0;

    int m_Wave = 0;
    int m_Score = 0;
    float m_ShownScore = 0.0f;
    int m_Combo = 0;
    float m_ComboTimer = 0.0f;
    int m_Kills = 0;
    int m_BestCombo = 0;
    int m_BestScore = 0;
    int m_BestWave = 0;
    int m_Sessions = 0;
    bool m_NewRecord = false;
    Zamin::RandomGenerator m_Rng{ 1234 };
};

class PlayerShip : public Zamin::ScriptableEntity
{
public:
    float MoveSpeed = 9.0f;
    float Acceleration = 11.0f;
    float FireRate = 11.0f;      // shots per second
    float DashSpeed = 24.0f;
    float DashTime = 0.16f;
    float DashCooldown = 0.9f;
    int MaxHealth = 5;

    void OnCreate() override;
    void OnUpdate(Zamin::Timestep ts) override;

    void ResetShip(const glm::vec2& position, bool bot);
    bool Hurt(int damage, const glm::vec2& from);  // false while invulnerable (just hurt / dashing)
    void Heal(int amount);
    void GivePowerUp(PickupKind kind, float seconds);
    int GetHealth() const { return m_Health; }
    bool IsAlive() const { return m_Health > 0; }
    bool IsBot() const { return m_Bot; }
    float GetDashCharge() const;
    float GetRapidTime() const { return m_RapidTime; }
    float GetSpreadTime() const { return m_SpreadTime; }
    glm::vec2 GetPosition() const { return glm::vec2(m_Position); }

private:
    struct Controls
    {
        glm::vec2 Move{ 0.0f };
        glm::vec2 Aim{ 0.0f };   // a direction (not normalised: zero = keep the last one)
        bool Fire = false;
        bool Dash = false;
        bool MouseAim = false;
    };
    Controls ReadInput();
    Controls ThinkBot(float dt);
    void Fire();

    bool m_Bot = true;
    int m_Health = 5;
    glm::vec3 m_Position{ 0.0f };
    glm::vec2 m_Velocity{ 0.0f };
    glm::vec2 m_Aim{ 0.0f, 1.0f };
    glm::vec2 m_DashDir{ 0.0f };
    float m_DashLeft = 0.0f;
    float m_DashCooldownLeft = 0.0f;
    float m_FireCooldown = 0.0f;
    float m_Invulnerable = 0.0f;
    float m_RapidTime = 0.0f;
    float m_SpreadTime = 0.0f;
    float m_VisualAngle = 0.0f;
    float m_BotWander = 0.0f;
    glm::vec2 m_LastMouse{ -1.0f };
    bool m_UsingMouse = true;
    Zamin::Entity m_Visual, m_Shield, m_Reticle, m_Thruster;
};

class Enemy : public Zamin::ScriptableEntity
{
public:
    EnemyKind Kind = EnemyKind::Chaser;
    float Health = 2.0f;
    float Speed = 5.0f;
    int Score = 100;
    glm::vec3 Color{ 1.0f, 0.3f, 0.4f };  // its effects' colour

    void OnCreate() override;
    void OnUpdate(Zamin::Timestep ts) override;
    void OnCollisionBegin(Zamin::Entity other) override;
    void OnCollisionEnd(Zamin::Entity other) override;

    void Damage(float amount, const glm::vec2& direction);
    void Kill(bool scored);
    bool IsDead() const { return m_Dead; }
    bool IsSpawning() const { return m_Age < SpawnTime; }
    glm::vec2 GetPosition() const { return glm::vec2(m_Position); }
    glm::vec2 GetMoveVelocity() const { return m_Velocity; }

    static constexpr float SpawnTime = 0.3f;

private:
    glm::vec2 Steer(const glm::vec2& toPlayer, float distance, float dt);
    void FireAt(const glm::vec2& target, int shots, float spreadDegrees);
    void FireRing(int shots);
    void Face(const glm::vec2& direction, float dt, float turnSpeed);

    bool m_Dead = false;
    bool m_TouchingPlayer = false;
    float m_Age = 0.0f;
    float m_Timer = 0.0f;       // per kind: next shot / dash phase
    int m_DashPhase = 0;        // Dasher: 0 approach, 1 wind-up, 2 charge, 3 recover
    glm::vec2 m_DashDir{ 0.0f };
    float m_Phase = 0.0f;       // weave / orbit phase
    float m_Orbit = 1.0f;       // Shooter: circling direction
    float m_Angle = 0.0f;
    float m_MaxHealth = 1.0f;
    glm::vec3 m_Position{ 0.0f };
    glm::vec2 m_Velocity{ 0.0f };
    glm::vec2 m_Knock{ 0.0f };
    glm::vec3 m_VisualScale{ 1.0f };
    Zamin::Entity m_Visual;
};

class Projectile : public Zamin::ScriptableEntity
{
public:
    float Damage = 1.0f;
    float Lifetime = 1.1f;
    float Speed = 26.0f;
    bool FromPlayer = true;

    void Launch(const glm::vec2& direction, float speedScale = 1.0f);
    void OnUpdate(Zamin::Timestep ts) override;
    void OnTriggerBegin(Zamin::Entity other) override;
    glm::vec2 GetDirection() const { return m_Direction; }
    float GetSpeed() const { return m_Speed; }

private:
    void Spend(const glm::vec2& where, bool sparks);
    glm::vec2 m_Direction{ 0.0f, 1.0f };
    float m_Speed = 0.0f;
    float m_Age = 0.0f;
    bool m_Spent = false;
};

class Pickup : public Zamin::ScriptableEntity
{
public:
    PickupKind Kind = PickupKind::Health;
    float Lifetime = 9.0f;

    void OnCreate() override;
    void OnUpdate(Zamin::Timestep ts) override;
    void OnTriggerBegin(Zamin::Entity other) override;

private:
    float m_Age = 0.0f;
    bool m_Taken = false;
    glm::vec3 m_Base{ 0.0f };
    Zamin::Entity m_Ring, m_Visual;
};

// A short-lived effect (a spawn portal, a shockwave ring, a "+100" popup): grows, rises, spins and fades,
// then destroys itself. Works with a Sprite Renderer or a Text.
class FxAnimator : public Zamin::ScriptableEntity
{
public:
    float Lifetime = 0.5f;
    float StartScale = 1.0f, EndScale = 1.0f;
    float Rise = 0.0f;     // units per second upwards (slowing down)
    float Spin = 0.0f;     // radians per second
    float FadeIn = 0.0f, FadeOut = 0.3f;
    bool Pop = false;      // overshoots its scale at the start (OutBack)

    void OnCreate() override;
    void OnUpdate(Zamin::Timestep ts) override;
    void SetScaleMultiplier(float scale) { m_Multiplier = scale; }

private:
    float m_Age = 0.0f;
    float m_Multiplier = 1.0f;
    glm::vec4 m_Color{ 1.0f };
};

// Small shared helpers.
namespace NeonUtil {

    // The running scripts of type T on active entities: the templates parked in the switched-off
    // "Shablonlar" group have script instances too (created when play starts), they are left out.
    template<typename T>
    std::vector<T*> Live(const Zamin::ScriptableEntity& self)
    {
        std::vector<T*> all = self.FindObjectsOfType<T>();
        std::erase_if(all, [](T* script) { return !script->IsActiveInHierarchy(); });
        return all;
    }

    inline glm::vec2 SafeNormalize(const glm::vec2& v, const glm::vec2& fallback = { 0.0f, 1.0f })
    {
        const float len = glm::length(v);
        return len > 1e-4f ? v / len : fallback;
    }
    inline float AngleOf(const glm::vec2& dir) { return std::atan2(dir.y, dir.x) - glm::half_pi<float>(); }
    inline glm::vec2 Rotate(const glm::vec2& v, float radians)
    {
        const float c = std::cos(radians), s = std::sin(radians);
        return { v.x * c - v.y * s, v.x * s + v.y * c };
    }
    // 12500 -> "12 500"
    std::string FormatScore(int value);

}
