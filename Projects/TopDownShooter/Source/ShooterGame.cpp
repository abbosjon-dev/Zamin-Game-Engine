#include "Shooter.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <format>

using namespace Zamin;
using namespace NeonUtil;

// O'yin oqimi. Holatlar: Menu (bosh menyu; orqada bot o'ynaydi - "attract" rejimi), Playing, Paused, GameOver.
// To'lqinlar: e'lon -> dushmanlar portallardan chiqadi -> hammasi o'ldirilsa "to'lqin tozalandi" va bonus.
// Hisob: har bir dushman o'z ochkosi x kombo ko'paytuvchisi (ketma-ket o'ldirishlar, ComboWindow ichida).
// Rekord SaveData'da ("Neon.BestScore", "Neon.BestWave"). Demo: ZAMIN_DEMO=1 (yoki AutoDemo) - bot o'ynaydi,
// HUD'da DEMO belgisi, jurnalga "Demo: ..." qatorlari yoziladi (headless testlar shularni tekshiradi).

ShooterGame* ShooterGame::s_Instance = nullptr;

namespace NeonUtil {

    std::string FormatScore(int value)
    {
        std::string digits = std::to_string(std::abs(value));
        std::string out;
        for (size_t i = 0; i < digits.size(); i++)
        {
            if (i > 0 && (digits.size() - i) % 3 == 0)
                out += ' ';
            out += digits[i];
        }
        return value < 0 ? "-" + out : out;
    }

}

void ShooterGame::OnCreate()
{
    s_Instance = this;
    SetUpdateWhenPaused(true); // opens and closes the pause menu while the game stands still
    const char* demo = std::getenv("ZAMIN_DEMO");
    m_DemoRun = AutoDemo || (demo && *demo && std::string_view(demo) != "0");
    m_BestScore = SaveData::GetInt("Neon.BestScore", 0);
    m_BestWave = SaveData::GetInt("Neon.BestWave", 0);
    // The pool of explosion lights (children of the effects group).
    if (Entity group = Resolve(FlashLights))
        for (Entity child : GetScene().GetDescendants(group))
            if (child.HasComponent<Light2DComponent>())
                m_Flashes.push_back({ child, 0.0f });
}

void ShooterGame::OnStart()
{
    AddMessageHandler("Play", [this](const ScriptMessage&) { Sfx("ui_click"); StartGame(); });
    AddMessageHandler("Resume", [this](const ScriptMessage&) { Sfx("ui_click"); ClosePause(); });
    AddMessageHandler("Restart", [this](const ScriptMessage&) { Sfx("ui_click"); StartGame(); });
    AddMessageHandler("Menu", [this](const ScriptMessage&) { Sfx("ui_click"); ShowMenu(); });
    AddMessageHandler("Quit", [this](const ScriptMessage&) { QuitGame(); });
    AddMessageHandler("Language", [this](const ScriptMessage&) {
        Sfx("ui_click");
        if (Localization::NextLanguage())
            Localization::SaveSettings();
        RefreshMenuTexts();
    });

    if (m_DemoRun)
    {
        ZM_INFO("Demo rejimi: o'yinni bot o'ynaydi (ZAMIN_DEMO)");
        StartGame(); // a demo run: no menu, the bot plays with the HUD on
    }
    else
        ShowMenu();
    RefreshMenuTexts();
}

void ShooterGame::OnDestroy()
{
    if (m_DemoRun)
        LogDemo("yakuni");
    Cursor::SetVisible(true);
    if (s_Instance == this)
        s_Instance = nullptr;
}

void ShooterGame::OnLanguageChanged(const std::string&)
{
    RefreshMenuTexts();
}

bool ShooterGame::IsGameplayActive() const
{
    return m_State == State::Menu || m_State == State::Playing || m_State == State::GameOver;
}

PlayerShip* ShooterGame::GetPlayer() const
{
    return GetScript<PlayerShip>(Resolve(Player));
}

glm::vec2 ShooterGame::GetPlayerPosition() const
{
    const PlayerShip* ship = GetPlayer();
    return ship ? ship->GetPosition() : glm::vec2(0.0f);
}

glm::vec2 ShooterGame::ClampToArena(const glm::vec2& p, float margin) const
{
    return glm::clamp(p, -ArenaHalfSize + margin, ArenaHalfSize - margin);
}

// ------------------------------------------------------------------------------------------- flow

void ShooterGame::StartSession(bool bot, bool demoRun)
{
    ClearWorld();
    m_Bot = bot;
    m_DemoRun = demoRun;
    m_Wave = 0;
    m_Score = 0;
    m_ShownScore = 0.0f;
    m_Combo = 0;
    m_ComboTimer = 0.0f;
    m_Kills = 0;
    m_BestCombo = 0;
    m_NewRecord = false;
    m_RestartTimer = -1.0f;
    m_DeathTimer = -1.0f;
    m_DemoLogTimer = 0.0f;
    m_Sessions++;
    // A human game gets new enemies every time; the bot's demo plays the same way each run (tests).
    if (bot)
        m_Rng.SetSeed(1234 + (uint64_t)m_Sessions);
    else
        m_Rng.Randomize();
    if (Entity player = Resolve(Player))
    {
        player.SetActive(true);
        if (PlayerShip* ship = GetScript<PlayerShip>(player))
            ship->ResetShip({ 0.0f, -2.0f }, bot);
    }
    SetActiveRef(DemoBadge, bot && m_State != State::Menu);
    m_Phase = Phase::Intro;
    m_PhaseTimer = 0.6f;
    SetTimeScale(1.0f);
}

void ShooterGame::ClearWorld()
{
    for (Enemy* e : Live<Enemy>(*this))
        Destroy(e->GetEntity());
    for (Projectile* p : Live<Projectile>(*this))
        Destroy(p->GetEntity());
    for (Pickup* p : Live<Pickup>(*this))
        Destroy(p->GetEntity());
    for (FxAnimator* fx : Live<FxAnimator>(*this))
        Destroy(fx->GetEntity());
    m_Queue.clear();
    m_Pending.clear();
}

void ShooterGame::ShowMenu()
{
    if (IsGamePaused())
        ResumeGame();
    Audio::SetGroupLowPass("Music", 0.0f);
    m_State = State::Menu;
    SetActiveRef(PauseMenu, false);
    SetActiveRef(GameOverMenu, false);
    SetActiveRef(Hud, false);
    SetActiveRef(MainMenu, true);
    SetActiveRef(Reticle, false);
    ZM_INFO("Menyu: bosh menyu");
    Cursor::SetVisible(true);
    if (Entity button = Resolve(PlayButton))
        GetScene().SetSelectedUI(button);
    RefreshMenuTexts();
    StartSession(true, false); // the bot plays behind the menu
}

void ShooterGame::StartGame()
{
    if (IsGamePaused())
        ResumeGame();
    Audio::SetGroupLowPass("Music", 0.0f);
    m_State = State::Playing;
    SetActiveRef(MainMenu, false);
    SetActiveRef(PauseMenu, false);
    SetActiveRef(GameOverMenu, false);
    SetActiveRef(Hud, true);
    GetScene().SetSelectedUI({});
    StartSession(m_DemoRun, m_DemoRun);
    SetActiveRef(Reticle, !m_Bot);
    Cursor::SetVisible(m_Bot);
    if (!m_Bot)
        ZM_INFO("Neon Maydon: o'yin boshlandi");
}

void ShooterGame::OpenPause()
{
    m_State = State::Paused;
    PauseGame();
    SetActiveRef(PauseMenu, true);
    ZM_INFO("Menyu: pauza");
    if (Entity button = Resolve(ResumeButton))
        GetScene().SetSelectedUI(button);
    Audio::SetGroupLowPass("Music", 900.0f);
    Cursor::SetVisible(true);
}

void ShooterGame::ClosePause()
{
    if (m_State != State::Paused)
        return;
    m_State = State::Playing;
    SetActiveRef(PauseMenu, false);
    ZM_INFO("Menyu: pauza yopildi");
    GetScene().SetSelectedUI({});
    Audio::SetGroupLowPass("Music", 0.0f);
    Cursor::SetVisible(m_Bot);
    ResumeGame();
}

void ShooterGame::GameOver()
{
    m_State = State::GameOver;
    SetTimeScale(1.0f);
    const bool scored = !m_Bot;
    if (scored)
    {
        if (m_Score > m_BestScore)
        {
            m_BestScore = m_Score;
            m_NewRecord = true;
            SaveData::SetInt("Neon.BestScore", m_BestScore);
        }
        if (m_Wave > m_BestWave)
        {
            m_BestWave = m_Wave;
            SaveData::SetInt("Neon.BestWave", m_BestWave);
        }
        SaveData::Save();
    }
    SetText(OverScore, FormatScore(m_Score));
    SetText(OverWave, Localize("over.wave", m_Wave, m_Kills));
    SetText(OverRecord, m_NewRecord ? Localize("over.record") : Localize("over.best", FormatScore(m_BestScore)));
    SetActiveRef(GameOverMenu, true);
    ZM_INFO("Menyu: natijalar - hisob {}, {}-to'lqin", m_Score, m_Wave);
    Sfx("gameover", 0.9f);
    SetActiveRef(Reticle, false);
    Cursor::SetVisible(true);
    if (Entity button = Resolve(RetryButton))
        GetScene().SetSelectedUI(button);
    if (m_NewRecord)
        if (Entity record = Resolve(OverRecord))
            GetScene().TweenUI(record, UITweenProperty::Scale, glm::vec4(1.12f), 0.45f,
                               { .Easing = Ease::InOutQuad, .Loop = UITweenLoop::PingPong });
    if (m_DemoRun)
    {
        LogDemo("o'yin tugadi");
        m_RestartTimer = 3.0f; // a demo run goes on by itself
    }
    else
        ZM_INFO("O'yin tugadi: hisob {}, {}-to'lqin, {} ta dushman{}", m_Score, m_Wave, m_Kills, m_NewRecord ? " - yangi rekord!" : "");
}

// ---------------------------------------------------------------------------------------- update

void ShooterGame::OnUpdate(Timestep ts)
{
    const float dt = ts;
    const float realDt = GetUnscaledDeltaTime();

    // Pause (Esc / P / Start) - also works while paused (this script updates then).
    if (Input::IsActionJustPressed("Pause"))
    {
        if (m_State == State::Playing && !m_DemoRun)
            OpenPause();
        else if (m_State == State::Paused)
            ClosePause();
    }
    if (m_State == State::Paused)
        return;

    // The ship blew up: slow motion for a moment, then the results.
    if (m_DeathTimer >= 0.0f)
    {
        m_DeathTimer -= realDt;
        if (m_DeathTimer < 0.0f)
        {
            SetTimeScale(1.0f);
            if (m_State == State::Menu)
                m_RestartTimer = 1.5f; // behind the menu: the bot just starts again
            else
                GameOver();
        }
    }
    if (m_RestartTimer >= 0.0f)
    {
        m_RestartTimer -= realDt;
        if (m_RestartTimer < 0.0f)
        {
            if (m_State == State::Menu)
                StartSession(true, false);
            else if (m_DemoRun)
            {
                SetActiveRef(GameOverMenu, false);
                m_State = State::Playing;
                StartSession(true, true);
            }
        }
    }

    // Combo: it ends when no kill comes within the window.
    if (m_ComboTimer > 0.0f)
    {
        m_ComboTimer -= dt;
        if (m_ComboTimer <= 0.0f)
            m_Combo = 0;
    }

    PlayerShip* ship = GetPlayer();
    if (ship && ship->IsAlive())
        UpdateWaves(dt);
    UpdateFlashes(dt);
    UpdateHud(realDt);

    if (m_DemoRun && m_State == State::Playing)
    {
        m_DemoLogTimer += dt;
        if (m_DemoLogTimer >= 10.0f)
        {
            m_DemoLogTimer = 0.0f;
            LogDemo("holat");
        }
    }
}

// ----------------------------------------------------------------------------------------- waves

void ShooterGame::UpdateWaves(float dt)
{
    // portals that are due become enemies
    for (size_t i = 0; i < m_Pending.size();)
    {
        m_Pending[i].Timer -= dt;
        if (m_Pending[i].Timer <= 0.0f)
        {
            SpawnEnemy(m_Pending[i].Kind, m_Pending[i].Position);
            m_Pending.erase(m_Pending.begin() + (long)i);
        }
        else
            i++;
    }

    switch (m_Phase)
    {
        case Phase::Intro:
            m_PhaseTimer -= dt;
            if (m_PhaseTimer <= 0.0f)
                StartWave(m_Wave + 1);
            break;
        case Phase::Spawning:
            m_SpawnTimer -= dt;
            if (!m_Queue.empty() && m_SpawnTimer <= 0.0f)
            {
                m_SpawnTimer = m_SpawnInterval;
                // one to three at a time (more later in the game)
                const int batch = std::min((int)m_Queue.size(), 1 + m_Rng.Range(0, std::min(2, m_Wave / 3)));
                for (int b = 0; b < batch; b++)
                {
                    const EnemyKind kind = m_Queue.front();
                    m_Queue.pop_front();
                    glm::vec2 at;
                    if (!PickSpawnPoint(at, kind == EnemyKind::Tank ? 9.0f : 7.0f))
                        continue;
                    m_Pending.push_back({ kind, at, 0.75f });
                    if (Entity portal = Resolve(PortalTemplate))
                    {
                        Entity p = Instantiate(portal, glm::vec3(at, 0.0f));
                        if (auto* fx = GetScript<FxAnimator>(p))
                            fx->SetScaleMultiplier(kind == EnemyKind::Tank ? 1.8f : kind == EnemyKind::Swarmer ? 1.3f : 1.0f);
                        if (auto* sprite = p.TryGetComponent<SpriteRendererComponent>())
                        {
                            Entity tpl = TemplateFor(kind);
                            const Enemy* e = GetScript<Enemy>(tpl);
                            const glm::vec3 c = e ? e->Color : glm::vec3(1.0f, 0.4f, 1.0f);
                            sprite->Color = glm::vec4(c * 1.8f, 1.0f);
                        }
                    }
                }
                Sfx("spawn", 0.35f, m_Rng.Range(0.9f, 1.15f));
            }
            if (m_Queue.empty() && m_Pending.empty() && CountAliveEnemies() == 0)
            {
                const int bonus = 250 * m_Wave;
                m_Score += bonus;
                m_Phase = Phase::Cleared;
                m_PhaseTimer = 2.4f;
                ShowBanner(Localize("hud.cleared"), Localize("hud.bonus", FormatScore(bonus)), 2.0f);
                Sfx("wave_clear", 0.8f);
                if (PlayerShip* ship = GetPlayer(); ship && m_Wave % 3 == 0)
                    ship->Heal(1); // every third wave: one life back
            }
            break;
        case Phase::Cleared:
            m_PhaseTimer -= dt;
            if (m_PhaseTimer <= 0.0f)
                StartWave(m_Wave + 1);
            break;
    }
}

void ShooterGame::StartWave(int wave)
{
    m_Wave = wave;
    BuildWaveQueue(wave);
    m_Phase = Phase::Spawning;
    m_SpawnInterval = std::max(0.32f, 1.15f - wave * 0.07f);
    m_SpawnTimer = 1.0f;
    const bool tanks = std::find(m_Queue.begin(), m_Queue.end(), EnemyKind::Tank) != m_Queue.end();
    ShowBanner(Localize("hud.wave", wave), tanks && wave % 5 == 0 ? Localize("hud.boss_sub") : Localize("hud.wave_sub", (int)m_Queue.size()), 2.0f);
    Sfx("wave", 0.8f);
    if (m_DemoRun)
        ZM_INFO("Demo: {}-to'lqin boshlandi (hisob {}, o'ldirilgan {})", wave, m_Score, m_Kills);
}

void ShooterGame::BuildWaveQueue(int wave)
{
    m_Queue.clear();
    int budget = 6 + wave * 3;
    if (wave % 5 == 0)
    {
        // every fifth wave opens with tanks
        for (int i = 0; i < wave / 5; i++)
            m_Queue.push_back(EnemyKind::Tank);
        budget -= 4 * (wave / 5);
    }
    struct Option
    {
        EnemyKind Kind;
        int Cost, FromWave;
        float Weight;
    };
    const Option options[] = {
        { EnemyKind::Chaser, 1, 1, 4.0f },  { EnemyKind::Swarmer, 3, 2, 2.5f }, { EnemyKind::Shooter, 3, 3, 2.0f },
        { EnemyKind::Dasher, 3, 4, 2.0f },  { EnemyKind::Tank, 7, 6, 0.8f },
    };
    while (budget > 0)
    {
        std::vector<float> weights;
        for (const Option& o : options)
            weights.push_back(wave >= o.FromWave && o.Cost <= budget ? o.Weight : 0.0f);
        const int pick = m_Rng.WeightedIndex(weights);
        const Option& o = options[pick >= 0 ? pick : 0];
        m_Queue.push_back(o.Kind);
        budget -= o.Cost;
    }
}

bool ShooterGame::PickSpawnPoint(glm::vec2& out, float minPlayerDistance)
{
    const glm::vec2 player = GetPlayerPosition();
    for (int attempt = 0; attempt < 24; attempt++)
    {
        const glm::vec2 p{ m_Rng.Range(-ArenaHalfSize.x + 1.6f, ArenaHalfSize.x - 1.6f), m_Rng.Range(-ArenaHalfSize.y + 1.6f, ArenaHalfSize.y - 1.6f) };
        if (glm::distance(p, player) < minPlayerDistance && attempt < 20)
            continue;
        // not inside an obstacle (the walls' layer)
        if (!OverlapCircle2D(p, 1.3f, LayerMask({ "Wall" })).empty())
            continue;
        out = p;
        return true;
    }
    return false;
}

Entity ShooterGame::TemplateFor(EnemyKind kind) const
{
    switch (kind)
    {
        case EnemyKind::Chaser:  return Resolve(ChaserTemplate);
        case EnemyKind::Swarmer: return Resolve(SwarmerTemplate);
        case EnemyKind::Shooter: return Resolve(ShooterTemplate);
        case EnemyKind::Dasher:  return Resolve(DasherTemplate);
        case EnemyKind::Tank:    return Resolve(TankTemplate);
    }
    return {};
}

void ShooterGame::SpawnEnemy(EnemyKind kind, const glm::vec2& position)
{
    if (kind == EnemyKind::Swarmer)
    {
        SpawnSwarmers(position, 4);
        return;
    }
    Entity tpl = TemplateFor(kind);
    if (!tpl)
        return;
    Entity e = Instantiate(tpl, glm::vec3(position, 0.0f));
    if (Enemy* enemy = GetScript<Enemy>(e))
    {
        // a little faster and tougher wave by wave
        enemy->Speed *= 1.0f + std::min(0.45f, 0.035f * (float)(m_Wave - 1));
        enemy->Health *= 1.0f + 0.08f * (float)((m_Wave - 1) / 3);
    }
}

void ShooterGame::SpawnSwarmers(const glm::vec2& center, int count)
{
    Entity tpl = Resolve(SwarmerTemplate);
    if (!tpl)
        return;
    for (int i = 0; i < count; i++)
    {
        const glm::vec2 offset = Rotate({ 0.0f, 0.9f }, glm::two_pi<float>() * (float)i / (float)count);
        Entity e = Instantiate(tpl, glm::vec3(ClampToArena(center + offset, 1.0f), 0.0f));
        if (Enemy* enemy = GetScript<Enemy>(e))
            enemy->Speed *= 1.0f + std::min(0.45f, 0.035f * (float)std::max(0, m_Wave - 1));
    }
}

int ShooterGame::CountAliveEnemies() const
{
    int alive = 0;
    for (const Enemy* e : Live<Enemy>(*this))
        if (!e->IsDead())
            alive++;
    return alive;
}

// ------------------------------------------------------------------------------------ gameplay events

Entity ShooterGame::SpawnBullet(const glm::vec2& position, const glm::vec2& direction, bool fromPlayer, float speedScale)
{
    Entity tpl = Resolve(fromPlayer ? BulletTemplate : EnemyBulletTemplate);
    if (!tpl)
        return {};
    Entity b = Instantiate(tpl, glm::vec3(position, 0.0f));
    if (Projectile* p = GetScript<Projectile>(b))
        p->Launch(direction, speedScale);
    return b;
}

void ShooterGame::OnEnemyKilled(Enemy& enemy, bool scored)
{
    const glm::vec2 pos = enemy.GetPosition();
    const bool big = enemy.Kind == EnemyKind::Tank;
    Explode(pos, enemy.Color, big ? 2.2f : enemy.Kind == EnemyKind::Swarmer ? 0.7f : 1.0f);
    if (big)
        SpawnSwarmers(pos, 3); // the tank breaks apart
    if (!scored)
        return;
    m_Kills++;
    m_Combo++;
    m_BestCombo = std::max(m_BestCombo, m_Combo);
    m_ComboTimer = ComboWindow;
    const int multiplier = std::min(8, 1 + m_Combo / 5);
    const int points = enemy.Score * multiplier;
    m_Score += points;
    Popup(pos + glm::vec2(0.0f, 0.6f), "+" + std::to_string(points), glm::vec4(glm::mix(enemy.Color, glm::vec3(1.0f), 0.55f) * 1.4f, 1.0f),
          multiplier > 1 ? 1.25f : 1.0f);
    if (Entity score = Resolve(ScoreText))
        GetScene().PunchUIScale(score, 0.08f, 0.2f);
    if (m_Combo > 0 && m_Combo % 10 == 0)
    {
        ShowBanner("", Localize("hud.streak", m_Combo), 1.2f);
        Sfx("powerup", 0.5f, 1.2f);
    }
    // a bonus now and then (a tank always drops one)
    const float chance = big ? 1.0f : enemy.Kind == EnemyKind::Swarmer ? 0.03f : 0.08f;
    if (m_Rng.Bool(chance))
    {
        const PlayerShip* ship = GetPlayer();
        const bool hurt = ship && ship->GetHealth() < ship->MaxHealth;
        const float weights[] = { hurt ? 1.4f : 0.3f, 1.0f, 1.0f };
        const int kind = m_Rng.WeightedIndex(weights);
        const EntityRef& ref = kind == 0 ? HealthPickup : kind == 1 ? RapidPickup : SpreadPickup;
        if (Entity tpl = Resolve(ref))
            Instantiate(tpl, glm::vec3(ClampToArena(pos, 1.0f), 0.0f));
    }
}

void ShooterGame::OnPlayerHurt(const glm::vec2& position)
{
    m_Combo = 0;
    m_ComboTimer = 0.0f;
    Explode(position, { 0.4f, 0.9f, 1.0f }, 0.6f);
    ShakeCamera(0.6f, 0.45f);
    Sfx("player_hurt", 0.9f);
}

void ShooterGame::OnPlayerDied(const glm::vec2& position)
{
    Explode(position, { 0.4f, 0.9f, 1.0f }, 2.6f);
    ShakeCamera(1.0f, 1.0f, 18.0f);
    Sfx("big_explode", 1.0f, 0.9f);
    SetTimeScale(0.3f);
    m_DeathTimer = 1.3f;
    SetActiveRef(Reticle, false);
}

void ShooterGame::OnPickupCollected(PickupKind kind, const glm::vec2& position)
{
    PlayerShip* ship = GetPlayer();
    if (!ship)
        return;
    switch (kind)
    {
        case PickupKind::Health:
            ship->Heal(1);
            Popup(position, Localize("pickup.health"), { 2.2f, 0.8f, 1.0f, 1.0f }, 1.1f);
            break;
        case PickupKind::Rapid:
            ship->GivePowerUp(kind, 7.0f);
            Popup(position, Localize("pickup.rapid"), { 2.4f, 2.0f, 0.6f, 1.0f }, 1.1f);
            break;
        case PickupKind::Spread:
            ship->GivePowerUp(kind, 8.0f);
            Popup(position, Localize("pickup.spread"), { 0.7f, 2.2f, 1.0f, 1.0f }, 1.1f);
            break;
    }
    Sfx(kind == PickupKind::Health ? "pickup" : "powerup", 0.8f);
    SparksAt(position, { 1.0f, 1.0f, 1.0f }, 18);
}

// --------------------------------------------------------------------------------------- effects

void ShooterGame::Explode(const glm::vec2& position, const glm::vec3& color, float size)
{
    if (Entity e = Resolve(Explosion))
    {
        e.GetComponent<TransformComponent>().Translation = glm::vec3(position, 0.0f);
        if (auto* ps = e.TryGetComponent<ParticleSystemComponent>())
        {
            ps->StartColor = glm::vec4(glm::mix(color, glm::vec3(1.0f), 0.35f) * 1.8f, 1.0f);
            ps->EndColor = glm::vec4(color * 0.8f, 0.0f);
            ps->SpeedMax = 8.0f + 4.0f * size;
            ps->StartSize = 0.3f + 0.08f * size;
        }
        GetScene().EmitParticles(e, (int)(28.0f * size + 10.0f));
    }
    SparksAt(position, color, (int)(10.0f * size));
    if (Entity ring = Resolve(ShockwaveTemplate))
    {
        Entity r = Instantiate(ring, glm::vec3(position, 0.0f));
        if (auto* fx = GetScript<FxAnimator>(r))
            fx->SetScaleMultiplier(0.6f + 0.5f * size);
        if (auto* sprite = r.TryGetComponent<SpriteRendererComponent>())
            sprite->Color = glm::vec4(color * 2.2f, 1.0f);
    }
    if (!m_Flashes.empty())
    {
        Flash& flash = m_Flashes[m_NextFlash++ % m_Flashes.size()];
        if (flash.Light)
        {
            flash.Light.GetComponent<TransformComponent>().Translation = glm::vec3(position, 0.0f);
            auto& light = flash.Light.GetComponent<Light2DComponent>();
            light.Color = color;
            light.Radius = 4.0f + 2.5f * size;
            flash.Intensity = 2.2f + 0.6f * size;
        }
    }
    ShakeCamera(std::min(1.0f, 0.18f + 0.14f * size), 0.25f + 0.1f * size);
    Sfx(size > 1.5f ? "big_explode" : "explode", std::min(1.0f, 0.45f + 0.2f * size), m_Rng.Range(0.9f, 1.12f));
}

void ShooterGame::SparksAt(const glm::vec2& position, const glm::vec3& color, int count)
{
    if (Entity e = Resolve(Sparks))
    {
        e.GetComponent<TransformComponent>().Translation = glm::vec3(position, 0.0f);
        if (auto* ps = e.TryGetComponent<ParticleSystemComponent>())
        {
            ps->StartColor = glm::vec4(glm::mix(color, glm::vec3(1.0f), 0.5f) * 2.0f, 1.0f);
            ps->EndColor = glm::vec4(color, 0.0f);
        }
        GetScene().EmitParticles(e, count);
    }
}

void ShooterGame::Popup(const glm::vec2& position, const std::string& text, const glm::vec4& color, float scale)
{
    Entity tpl = Resolve(PopupTemplate);
    if (!tpl)
        return;
    Entity p = Instantiate(tpl, glm::vec3(position, 0.0f));
    if (auto* t = p.TryGetComponent<TextComponent>())
    {
        t->Text = text;
        t->Color = color;
    }
    if (auto* fx = GetScript<FxAnimator>(p))
        fx->SetScaleMultiplier(scale);
}

void ShooterGame::Sfx(const char* name, float volume, float pitch)
{
    PlaySound(std::format("Audio/{}.wav", name), volume, pitch);
}

void ShooterGame::UpdateFlashes(float dt)
{
    for (Flash& f : m_Flashes)
    {
        if (!f.Light)
            continue;
        f.Intensity = std::max(0.0f, f.Intensity - dt * 9.0f);
        f.Light.GetComponent<Light2DComponent>().Intensity = f.Intensity;
    }
}

// ------------------------------------------------------------------------------------------- HUD

void ShooterGame::UpdateHud(float realDt)
{
    m_ShownScore += ((float)m_Score - m_ShownScore) * std::min(1.0f, 10.0f * realDt);
    if (std::abs((float)m_Score - m_ShownScore) < 1.0f)
        m_ShownScore = (float)m_Score;
    SetText(ScoreText, FormatScore((int)std::round(m_ShownScore)));
    const int multiplier = std::min(8, 1 + m_Combo / 5);
    SetText(ComboText, m_Combo >= 2 ? Localize("hud.combo", multiplier, m_Combo) : std::string());
    if (Entity bar = Resolve(ComboBar))
        if (auto* p = bar.TryGetComponent<UIProgressBarComponent>())
            p->Value = m_Combo >= 2 ? std::clamp(m_ComboTimer / ComboWindow, 0.0f, 1.0f) : 0.0f;
    SetText(WaveText, m_Wave > 0 ? Localize("hud.wave", m_Wave) : Localize("hud.get_ready"));
    const int left = CountAliveEnemies() + (int)m_Pending.size() + (int)m_Queue.size();
    SetText(EnemiesText, m_Phase == Phase::Spawning ? Localize("hud.enemies", left) : std::string());
    SetText(BestText, Localize("hud.best", FormatScore(std::max(m_BestScore, m_Bot ? 0 : m_Score))));

    const PlayerShip* ship = GetPlayer();
    if (Entity hearts = Resolve(Hearts); hearts && ship)
    {
        int i = 0;
        for (Entity heart : GetScene().GetChildren(hearts))
        {
            if (auto* img = heart.TryGetComponent<UIImageComponent>())
                img->SpriteName = i < ship->GetHealth() ? "heart" : "heart_empty";
            i++;
        }
    }
    std::string power;
    if (ship && ship->GetRapidTime() > 0.0f)
        power += Localize("hud.rapid", std::format("{:.1f}", ship->GetRapidTime()));
    if (ship && ship->GetSpreadTime() > 0.0f)
        power += (power.empty() ? "" : "     ") + Localize("hud.spread", std::format("{:.1f}", ship->GetSpreadTime()));
    SetText(PowerText, power);
    if (Entity bar = Resolve(DashBar); bar && ship)
        if (auto* p = bar.TryGetComponent<UIProgressBarComponent>())
            p->Value = ship->GetDashCharge();

    if (m_BannerTimer > 0.0f)
    {
        m_BannerTimer -= realDt;
        if (m_BannerTimer <= 0.0f)
            if (Entity banner = Resolve(Banner))
                GetScene().HideUI(banner);
    }
}

void ShooterGame::ShowBanner(const std::string& title, const std::string& subtitle, float seconds)
{
    if (m_State == State::Menu)
        return; // the attract mode plays quietly behind the menu
    SetText(Banner, title);
    SetText(BannerSub, subtitle);
    if (Entity banner = Resolve(Banner))
    {
        GetScene().CancelUITweens(banner, false);
        banner.SetActive(false);
        GetScene().ShowUI(banner);
    }
    m_BannerTimer = seconds;
}

void ShooterGame::RefreshMenuTexts()
{
    SetText(LanguageButton, Localize("menu.language", Localization::GetLanguageName(Localization::GetLanguage())));
    SetText(MenuBest, m_BestScore > 0 ? Localize("menu.best", FormatScore(m_BestScore), m_BestWave) : std::string());
}

void ShooterGame::SetText(const EntityRef& ref, const std::string& text)
{
    if (Entity e = Resolve(ref))
        if (auto* t = e.TryGetComponent<TextComponent>(); t && t->Text != text)
            t->Text = text;
}

void ShooterGame::SetActiveRef(const EntityRef& ref, bool active)
{
    if (Entity e = Resolve(ref); e && e.IsActive() != active)
        e.SetActive(active);
}

void ShooterGame::LogDemo(const char* reason)
{
    ZM_INFO("Demo ({}): hisob {}, {}-to'lqin, o'ldirilgan {}, eng katta kombo {}", reason, m_Score, m_Wave, m_Kills, m_BestCombo);
}

ZM_REGISTER_SCRIPT(ShooterGame,
    ZM_FIELD(ShooterGame, Player).Header("O'yinchi"),
    ZM_FIELD(ShooterGame, Reticle),
    ZM_FIELD(ShooterGame, Shield),
    ZM_FIELD(ShooterGame, Thruster),
    ZM_FIELD(ShooterGame, BulletTemplate).Header("Shablonlar"),
    ZM_FIELD(ShooterGame, EnemyBulletTemplate),
    ZM_FIELD(ShooterGame, ChaserTemplate),
    ZM_FIELD(ShooterGame, SwarmerTemplate),
    ZM_FIELD(ShooterGame, ShooterTemplate),
    ZM_FIELD(ShooterGame, DasherTemplate),
    ZM_FIELD(ShooterGame, TankTemplate),
    ZM_FIELD(ShooterGame, HealthPickup),
    ZM_FIELD(ShooterGame, RapidPickup),
    ZM_FIELD(ShooterGame, SpreadPickup),
    ZM_FIELD(ShooterGame, PortalTemplate),
    ZM_FIELD(ShooterGame, ShockwaveTemplate),
    ZM_FIELD(ShooterGame, PopupTemplate),
    ZM_FIELD(ShooterGame, Explosion).Header("Effektlar"),
    ZM_FIELD(ShooterGame, Sparks),
    ZM_FIELD(ShooterGame, FlashLights).Tooltip("Portlash chiroqlari guruhi (Light 2D bolalari navbat bilan ishlatiladi)"),
    ZM_FIELD(ShooterGame, Hud).Header("HUD"),
    ZM_FIELD(ShooterGame, ScoreText),
    ZM_FIELD(ShooterGame, ComboText),
    ZM_FIELD(ShooterGame, ComboBar),
    ZM_FIELD(ShooterGame, WaveText),
    ZM_FIELD(ShooterGame, EnemiesText),
    ZM_FIELD(ShooterGame, Hearts),
    ZM_FIELD(ShooterGame, BestText),
    ZM_FIELD(ShooterGame, PowerText),
    ZM_FIELD(ShooterGame, DashBar),
    ZM_FIELD(ShooterGame, Banner),
    ZM_FIELD(ShooterGame, BannerSub),
    ZM_FIELD(ShooterGame, DemoBadge),
    ZM_FIELD(ShooterGame, MainMenu).Header("Menyular"),
    ZM_FIELD(ShooterGame, PauseMenu),
    ZM_FIELD(ShooterGame, GameOverMenu),
    ZM_FIELD(ShooterGame, MenuBest),
    ZM_FIELD(ShooterGame, PlayButton),
    ZM_FIELD(ShooterGame, LanguageButton),
    ZM_FIELD(ShooterGame, ResumeButton),
    ZM_FIELD(ShooterGame, RetryButton),
    ZM_FIELD(ShooterGame, OverScore),
    ZM_FIELD(ShooterGame, OverWave),
    ZM_FIELD(ShooterGame, OverRecord),
    ZM_FIELD(ShooterGame, ArenaHalfSize).Header("Qoidalar").Tooltip("Maydonning yarim o'lchami (birlik)"),
    ZM_FIELD_RANGE(ShooterGame, ComboWindow, 0.5f, 6.0f).Tooltip("Kombo davom etishi uchun o'ldirishlar orasidagi eng uzoq vaqt (s)"),
    ZM_FIELD(ShooterGame, AutoDemo).Tooltip("Demo rejimida boshlash: bot o'ynaydi (ZAMIN_DEMO=1 ham shunday qiladi)"))
