#include "Runner.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <format>

using namespace Zamin;

// Dunyo yuguruvchi bilan birga quriladi: oldinda SpawnAhead metrgacha har SegmentLength metrda bitta to'siq naqshi
// (arava, to'siq, gilam osilgan darvoza, devor, karvon - har doim kamida bitta o'tish yo'li bor), tangalar qatori /
// yoyi, ba'zan bonus (magnit, qalqon); yo'l chetida manzara (minoralar, palmalar, qum tepalari, uylar, ravoqlar).
// Orqada qolganlari o'chiriladi. Yo'l va qum tekisliklari yuguruvchi bilan siljiydi, teksturasi esa joyida turadi
// (UV siljishi) - cheksiz yo'l. Tezlik masofa bilan StartSpeed'dan MaxSpeed'gacha oshadi.
// Hisob: masofa + tanga x 10; rekord SaveData "EndlessRunner.Best" / "EndlessRunner.BestDistance".
// Demo: ZAMIN_DEMO=1 (yoki AutoDemo) - bot yuguradi, jurnalga "Demo: ..." yoziladi; yiqilsa yangi yugurish.

static constexpr int AllLanes = -2;

void RunnerGame::OnCreate()
{
    s_Instance = this;
    SetUpdateWhenPaused(true);
    const char* demo = std::getenv("ZAMIN_DEMO");
    m_DemoRun = AutoDemo || (demo && *demo && std::string_view(demo) != "0");
    m_BestScore = SaveData::GetInt("EndlessRunner.Best", 0);
    m_BestDistance = SaveData::GetFloat("EndlessRunner.BestDistance", 0.0f);
    m_Rng.SetSeed(20261108ull);
}

void RunnerGame::OnStart()
{
    auto click = [this] { PlaySound("Audio/ui_click.wav", 0.8f, 1.0f, "UI"); };
    AddMessageHandler("Play", [this, click](const ScriptMessage&) { click(); StartRun(false); });
    AddMessageHandler("Resume", [this, click](const ScriptMessage&) { click(); ClosePause(); });
    AddMessageHandler("Restart", [this, click](const ScriptMessage&) { click(); StartRun(false); });
    AddMessageHandler("Menu", [this, click](const ScriptMessage&) { click(); ShowMenu(); });
    AddMessageHandler("Quit", [this](const ScriptMessage&) { QuitGame(); });
    AddMessageHandler("Language", [this, click](const ScriptMessage&) {
        click();
        if (Localization::NextLanguage())
            Localization::SaveSettings();
        RefreshTexts();
    });
    if (m_DemoRun)
    {
        ZM_INFO("Demo rejimi: bot yuguradi (ZAMIN_DEMO)");
        StartRun(true);
    }
    else
        ShowMenu();
    RefreshTexts();
}

void RunnerGame::OnDestroy()
{
    if (m_DemoRun)
        ZM_INFO("Demo yakuni: {} ta yugurish, oxirgisi {:.0f} m, {} tanga, ochko {}", m_Runs, m_Distance, m_Coins, GetScore());
    SaveData::Save();
    if (s_Instance == this)
        s_Instance = nullptr;
}

void RunnerGame::OnLanguageChanged(const std::string&)
{
    RefreshTexts();
}

Runner* RunnerGame::GetRunner() const
{
    return GetScript<Runner>(Resolve(Player));
}

float RunnerGame::LaneX(int lane) const
{
    const Runner* r = GetRunner();
    return (float)lane * (r ? r->LaneWidth : 2.3f);
}

// ------------------------------------------------------------------------------------------- flow

void RunnerGame::ClearWorld()
{
    for (Item& item : m_Items)
        if (item.E)
            Destroy(item.E);
    for (Entity e : m_Scenery)
        if (e)
            Destroy(e);
    m_Items.clear();
    m_Scenery.clear();
}

void RunnerGame::StartRun(bool bot)
{
    if (IsGamePaused())
        ResumeGame();
    SetTimeScale(1.0f);
    Audio::SetGroupLowPass("Music", 0.0f);
    m_Bot = bot || m_DemoRun;
    if (m_State != State::Menu || !bot || m_DemoRun)
    {
        m_State = State::Playing;
        SetActiveRef(MainMenu, false);
        SetActiveRef(Hud, true);
    }
    SetActiveRef(PauseMenu, false);
    SetActiveRef(GameOverMenu, false);
    GetScene().SetSelectedUI({});
    ClearWorld();
    m_Speed = StartSpeed;
    m_Distance = 0.0f;
    m_Coins = 0;
    m_MagnetTime = m_ShieldTime = 0.0f;
    m_CrashTimer = m_OverTimer = 0.0f;
    m_PrevZ = 0.0f;
    m_Segments = 0;
    m_NextSegmentZ = -20.0f;
    m_NextSceneryZ = 10.0f;
    m_NextArchZ = -40.0f;
    m_DemoLogMark = 250.0f;
    m_MilestoneMark = 500.0f;
    m_Runs++;
    if (Runner* r = GetRunner())
        r->ResetRunner(m_Bot);
    // the first stretch of world at once
    while (m_NextSegmentZ > -SpawnAhead)
    {
        SpawnSegment(m_NextSegmentZ);
        m_NextSegmentZ -= SegmentLength;
    }
    while (m_NextSceneryZ > -SpawnAhead)
    {
        SpawnScenery(m_NextSceneryZ);
        m_NextSceneryZ -= m_Rng.Range(7.0f, 11.0f);
    }
    SetActiveRef(DemoBadge, m_Bot && m_State != State::Menu);
    if (m_State != State::Menu)
        ShowBanner(Localize("hud.go"), Localize("hud.go_sub"), 1.8f);
    if (!m_Bot)
        ZM_INFO("Ipak Yo'li: yugurish boshlandi");
    RefreshTexts();
}

void RunnerGame::ShowMenu()
{
    if (IsGamePaused())
        ResumeGame();
    m_State = State::Menu;
    SetActiveRef(Hud, false);
    SetActiveRef(PauseMenu, false);
    SetActiveRef(GameOverMenu, false);
    SetActiveRef(MainMenu, true);
    ZM_INFO("Menyu: bosh menyu");
    StartRun(true); // the bot runs behind the menu
    // (after StartRun, which clears the selection: Enter / A start the game from the keyboard / a gamepad)
    if (Entity button = Resolve(PlayButton))
        GetScene().SetSelectedUI(button);
    RefreshTexts();
}

void RunnerGame::OpenPause()
{
    m_State = State::Paused;
    PauseGame();
    SetActiveRef(PauseMenu, true);
    ZM_INFO("Menyu: pauza");
    if (Entity button = Resolve(ResumeButton))
        GetScene().SetSelectedUI(button);
    Audio::SetGroupLowPass("Music", 800.0f);
}

void RunnerGame::ClosePause()
{
    if (m_State != State::Paused)
        return;
    m_State = State::Playing;
    SetActiveRef(PauseMenu, false);
    ZM_INFO("Menyu: pauza yopildi");
    GetScene().SetSelectedUI({});
    Audio::SetGroupLowPass("Music", 0.0f);
    ResumeGame();
}

void RunnerGame::OnRunnerCrashed()
{
    if (m_State == State::Crashed || m_State == State::GameOver)
        return;
    Runner* r = GetRunner();
    if (r)
    {
        r->Crash();
        Burst(CrashBurst, { r->GetX(), 0.8f, r->GetZ() - 0.5f }, 60);
    }
    ShakeCamera(1.0f, 0.9f, 18.0f);
    PlaySound("Audio/crash.wav", 1.0f, 1.0f);
    if (m_DemoRun)
        ZM_INFO("Demo: yiqildi - {:.0f} m, {} tanga, ochko {} (to'siq: {}, yo'lak {}, tezlik {:.1f} m/s)", m_Distance, m_Coins, GetScore(),
                (int)m_LastHit, r ? r->GetLane() : 0, m_Speed);
    if (m_State == State::Menu)
    {
        m_CrashTimer = 0.0f;
        return; // behind the menu: a new run in a moment
    }
    m_State = State::Crashed;
    m_CrashTimer = 0.0f;
    SetTimeScale(0.35f);
    Audio::SetGroupLowPass("Music", 700.0f);
}

void RunnerGame::GameOver()
{
    m_State = State::GameOver;
    SetTimeScale(1.0f);
    const int score = GetScore();
    const bool record = !m_Bot && score > m_BestScore;
    if (!m_Bot)
    {
        if (record)
            m_BestScore = score;
        m_BestDistance = std::max(m_BestDistance, m_Distance);
        SaveData::SetInt("EndlessRunner.Best", m_BestScore);
        SaveData::SetFloat("EndlessRunner.BestDistance", m_BestDistance);
        SaveData::Save();
    }
    SetText(OverScore, Localize("over.score", score));
    SetText(OverStats, Localize("over.stats", (int)m_Distance, m_Coins));
    SetText(OverRecord, record ? Localize("over.record") : Localize("over.best", m_BestScore));
    SetActiveRef(GameOverMenu, true);
    ZM_INFO("Menyu: natijalar - ochko {}, {:.0f} m, {} tanga", score, m_Distance, m_Coins);
    if (Entity button = Resolve(RetryButton))
        GetScene().SetSelectedUI(button);
    m_OverTimer = 0.0f;
    RefreshTexts();
}

// ---------------------------------------------------------------------------------------- update

void RunnerGame::OnUpdate(Timestep ts)
{
    const float dt = ts;
    const float realDt = GetUnscaledDeltaTime();
    if (Input::IsActionJustPressed("Pause"))
    {
        if (m_State == State::Playing && !m_DemoRun)
            OpenPause();
        else if (m_State == State::Paused)
            ClosePause();
    }
    if (m_State == State::Paused)
        return;
    Runner* r = GetRunner();
    if (m_State == State::Crashed)
    {
        m_CrashTimer += realDt;
        if (m_CrashTimer > 1.4f)
            GameOver();
    }
    else if (m_State == State::GameOver)
    {
        m_OverTimer += realDt;
        if (m_DemoRun && m_OverTimer > 3.0f)
            StartRun(true);
    }
    else if (r && r->IsCrashed() && m_State == State::Menu)
    {
        m_CrashTimer += realDt;
        if (m_CrashTimer > 1.5f)
            StartRun(true);
    }
    else if (r && IsRunning())
    {
        // faster and faster
        m_Distance = -r->GetZ();
        m_Speed = std::min(MaxSpeed, StartSpeed + m_Distance * SpeedGain);
        m_MagnetTime = std::max(0.0f, m_MagnetTime - dt);
        m_ShieldTime = std::max(0.0f, m_ShieldTime - dt);
        // the run in small steps, a collision check after each: a slow frame does not step over an obstacle
        // or take a jump too coarsely
        const int steps = std::clamp((int)std::ceil(dt * 120.0f), 1, 32);
        for (int i = 0; i < steps && !r->IsCrashed(); i++)
        {
            r->Tick(dt / (float)steps, dt, i == 0);
            CheckCollisions();
        }
        UpdateWorld(dt);
        if (m_Distance >= m_MilestoneMark)
        {
            if (m_State == State::Playing)
                ShowBanner(Localize("hud.milestone", (int)m_MilestoneMark), Localize("hud.faster"), 1.5f);
            PlaySound("Audio/milestone.wav", 0.6f, 1.0f, "UI");
            m_MilestoneMark += 500.0f;
        }
        if (m_DemoRun && m_Distance >= m_DemoLogMark)
        {
            ZM_INFO("Demo: {:.0f} m, {} tanga, tezlik {:.1f} m/s, ochko {}", m_Distance, m_Coins, m_Speed, GetScore());
            m_DemoLogMark += 250.0f;
        }
    }
    // coins spin, power-ups bob
    for (Item& item : m_Items)
        if (!item.IsObstacle && item.E && !item.Taken)
        {
            auto& t = item.E.GetComponent<TransformComponent>();
            t.Rotation.y += dt * (item.Pickup == PickupKind::Coin ? 4.0f : 1.5f);
        }
    UpdateHud();
    if (m_BannerTimer > 0.0f)
    {
        m_BannerTimer -= realDt;
        if (m_BannerTimer <= 0.0f)
            if (Entity banner = Resolve(Banner))
                GetScene().HideUI(banner);
    }
    UpdateCamera(dt);
}

void RunnerGame::UpdateWorld(float)
{
    const Runner* r = GetRunner();
    if (!r)
        return;
    const float z = r->GetZ();
    // build ahead
    while (m_NextSegmentZ > z - SpawnAhead)
    {
        SpawnSegment(m_NextSegmentZ);
        m_NextSegmentZ -= SegmentLength;
    }
    while (m_NextSceneryZ > z - SpawnAhead)
    {
        SpawnScenery(m_NextSceneryZ);
        m_NextSceneryZ -= m_Rng.Range(7.0f, 11.0f);
    }
    // forget what is behind
    std::erase_if(m_Items, [&](Item& item) {
        if (item.Z - item.Length * 0.5f > z + 14.0f)
        {
            if (item.E)
                Destroy(item.E);
            return true;
        }
        return false;
    });
    std::erase_if(m_Scenery, [&](Entity e) {
        if (!e)
            return true;
        if (e.GetComponent<TransformComponent>().Translation.z > z + 25.0f)
        {
            Destroy(e);
            return true;
        }
        return false;
    });
}

// ------------------------------------------------------------------------------------- spawning

void RunnerGame::SpawnObstacle(ObstacleKind kind, int lane, float z)
{
    static const EntityRef RunnerGame::* templates[] = { &RunnerGame::CartTemplate, &RunnerGame::FenceTemplate, &RunnerGame::GateTemplate,
                                                        &RunnerGame::WallTemplate, &RunnerGame::CaravanTemplate };
    Entity tpl = Resolve(this->*templates[(int)kind]);
    if (!tpl)
        return;
    Item item;
    item.IsObstacle = true;
    item.Obstacle = kind;
    item.Lane = lane;
    item.Z = z;
    switch (kind)
    {
        case ObstacleKind::Cart:    item.Length = 1.8f; item.Bottom = 0.0f; item.Top = 1.1f; break;
        case ObstacleKind::Fence:   item.Length = 0.5f; item.Bottom = 0.0f; item.Top = 0.9f; item.Lane = AllLanes; break;
        case ObstacleKind::Gate:    item.Length = 0.6f; item.Bottom = 1.15f; item.Top = 4.0f; item.Lane = AllLanes; break;
        case ObstacleKind::Wall:    item.Length = 1.2f; item.Bottom = 0.0f; item.Top = 3.0f; break;
        case ObstacleKind::Caravan: item.Length = 9.0f; item.Bottom = 0.0f; item.Top = 2.7f; break;
    }
    const float x = item.Lane == AllLanes ? 0.0f : LaneX(lane);
    item.E = Instantiate(tpl, { x, tpl.GetComponent<TransformComponent>().Translation.y, z });
    m_Items.push_back(item);
}

void RunnerGame::SpawnPickup(PickupKind kind, int lane, float z, float y)
{
    const EntityRef& ref = kind == PickupKind::Coin ? CoinTemplate : kind == PickupKind::Magnet ? MagnetTemplate : ShieldTemplate;
    Entity tpl = Resolve(ref);
    if (!tpl)
        return;
    Item item;
    item.IsObstacle = false;
    item.Pickup = kind;
    item.Lane = lane;
    item.Z = z;
    item.Y = y;
    item.Length = 0.8f;
    item.E = Instantiate(tpl, { LaneX(lane), y, z });
    m_Items.push_back(item);
}

void RunnerGame::SpawnCoinRow(int lane, float z0, int count, bool arc)
{
    for (int i = 0; i < count; i++)
    {
        const float t = count > 1 ? (float)i / (float)(count - 1) : 0.5f;
        const float y = arc ? 0.8f + 1.7f * std::sin(t * glm::pi<float>()) : 0.8f;
        SpawnPickup(PickupKind::Coin, lane, z0 - (float)i * 2.0f, y);
    }
}

void RunnerGame::SpawnSegment(float z0)
{
    m_Segments++;
    const int lane = m_Rng.Range(-1, 1);
    const int other = lane == 1 ? m_Rng.Range(-1, 0) : lane == -1 ? m_Rng.Range(0, 1) : (m_Rng.Bool() ? -1 : 1);
    const int third = -lane - other; // lanes are -1, 0, 1
    const float z = z0 - m_Rng.Range(4.0f, 10.0f);
    if (m_Segments <= 2)
    {
        // a warm-up stretch: coins only
        SpawnCoinRow(lane, z0 - 4.0f, 6, false);
        return;
    }
    // harder patterns become likelier with distance
    const float hard = std::clamp(m_Distance / 1500.0f, 0.0f, 1.0f);
    const float weights[] = { 1.2f, 0.6f + hard, 0.8f, 0.8f, 0.4f + hard * 1.2f, 0.5f + hard, 0.9f, 0.18f };
    switch (m_Rng.WeightedIndex(weights))
    {
        case 0: // a cart; coins in another lane
            SpawnObstacle(ObstacleKind::Cart, lane, z);
            SpawnCoinRow(other, z + 4.0f, 5, false);
            break;
        case 1: // two carts side by side: one lane free, with coins
            SpawnObstacle(ObstacleKind::Cart, lane, z);
            SpawnObstacle(ObstacleKind::Cart, other, z);
            SpawnCoinRow(third, z + 4.0f, 5, false);
            break;
        case 2: // a fence across the road: jump, coins in an arc over it
            SpawnObstacle(ObstacleKind::Fence, AllLanes, z);
            SpawnCoinRow(lane, z + 4.0f, 5, true);
            break;
        case 3: // a gate with a carpet: slide, coins low under it
            SpawnObstacle(ObstacleKind::Gate, AllLanes, z);
            for (int i = 0; i < 4; i++)
                SpawnPickup(PickupKind::Coin, lane, z + 3.0f - (float)i * 2.0f, 0.45f);
            break;
        case 4: // walls in two lanes, one after the other: weave
            SpawnObstacle(ObstacleKind::Wall, lane, z);
            SpawnObstacle(ObstacleKind::Wall, other, z - 9.0f);
            SpawnCoinRow(third, z + 2.0f, 4, false);
            break;
        case 5: // a caravan in one lane, a cart in another
            SpawnObstacle(ObstacleKind::Caravan, lane, z - 3.0f);
            SpawnObstacle(ObstacleKind::Cart, other, z - 5.0f);
            SpawnCoinRow(third, z + 2.0f, 6, false);
            break;
        case 6: // a wall; coins on a cart's arc in the next lane
            SpawnObstacle(ObstacleKind::Wall, lane, z);
            SpawnObstacle(ObstacleKind::Cart, other, z - 2.0f);
            SpawnCoinRow(other, z + 2.0f, 5, true);
            break;
        case 7: // a power-up
            SpawnPickup(m_Rng.Bool() ? PickupKind::Magnet : PickupKind::Shield, lane, z, 1.0f);
            SpawnCoinRow(other, z + 3.0f, 5, false);
            break;
        default:
            break;
    }
}

void RunnerGame::SpawnScenery(float z)
{
    // both sides of the road: towers with domes, palms, houses, dunes further out; now and then an arch over the road
    for (int side : { -1, 1 })
    {
        const float roll = m_Rng.Float();
        const EntityRef& ref = roll < 0.25f ? TowerTemplate : roll < 0.55f ? PalmTemplate : roll < 0.8f ? HouseTemplate : DuneTemplate;
        if (Entity tpl = Resolve(ref))
        {
            const float x = (float)side * m_Rng.Range(6.5f, 13.0f);
            Entity e = Instantiate(tpl, { x, tpl.GetComponent<TransformComponent>().Translation.y, z + m_Rng.Range(-2.0f, 2.0f) });
            auto& t = e.GetComponent<TransformComponent>();
            t.Rotation.y = m_Rng.Range(0.0f, glm::two_pi<float>());
            t.Scale *= m_Rng.Range(0.85f, 1.25f);
            m_Scenery.push_back(e);
        }
        if (m_Rng.Bool(0.5f))
            if (Entity tpl = Resolve(DuneTemplate))
            {
                Entity e = Instantiate(tpl, { (float)side * m_Rng.Range(16.0f, 30.0f), tpl.GetComponent<TransformComponent>().Translation.y,
                                              z + m_Rng.Range(-4.0f, 4.0f) });
                e.GetComponent<TransformComponent>().Scale *= m_Rng.Range(1.0f, 2.2f);
                m_Scenery.push_back(e);
            }
    }
    if (z < m_NextArchZ)
    {
        if (Entity tpl = Resolve(ArchTemplate))
            m_Scenery.push_back(Instantiate(tpl, { 0.0f, tpl.GetComponent<TransformComponent>().Translation.y, z }));
        m_NextArchZ = z - m_Rng.Range(70.0f, 110.0f);
    }
}

// ------------------------------------------------------------------------------------- collisions

RunnerGame::Hazard RunnerGame::FindHazard(int lane, float z, float range) const
{
    Hazard best;
    const float laneX = LaneX(lane);
    for (const Item& item : m_Items)
    {
        if (!item.IsObstacle || item.Hit)
            continue;
        if (item.Lane != AllLanes && std::abs(LaneX(item.Lane) - laneX) > 0.5f)
            continue;
        const float nearEnd = item.Z + item.Length * 0.5f;     // the end that faces the runner
        const float farEnd = item.Z - item.Length * 0.5f;
        const float distance = z - nearEnd;
        if (farEnd > z + 0.5f || distance > range)
            continue; // behind, or too far ahead
        const float d = std::max(0.0f, distance);
        if (d < best.Distance)
        {
            best.Found = true;
            best.Distance = d;
            best.Kind = item.Obstacle;
            best.Length = item.Length;
            best.AllLanes = item.Lane == AllLanes;
        }
    }
    return best;
}

int RunnerGame::CountCoins(int lane, float z, float range) const
{
    int n = 0;
    for (const Item& item : m_Items)
        if (!item.IsObstacle && !item.Taken && item.Lane == lane && item.Z < z && item.Z > z - range)
            n++;
    return n;
}

void RunnerGame::CheckCollisions()
{
    Runner* r = GetRunner();
    if (!r || r->IsCrashed())
        return;
    const float px = r->GetX(), py = r->GetY(), pz = r->GetZ(), height = r->GetHeight();
    const glm::vec3 center(px, py + height * 0.5f, pz);
    // the stretch run this frame (a slow frame must not step over a thin fence)
    const float zFront = std::min(pz, m_PrevZ) - 0.3f, zBack = std::max(pz, m_PrevZ) + 0.3f;
    m_PrevZ = pz;
    for (Item& item : m_Items)
    {
        if (item.Taken || item.Hit || !item.E)
            continue;
        if (item.IsObstacle)
        {
            if (item.Z - item.Length * 0.5f > zBack || item.Z + item.Length * 0.5f < zFront)
                continue;
            if (item.Lane != AllLanes && std::abs(LaneX(item.Lane) - px) > 1.2f)
                continue;
            if (py + height <= item.Bottom || py >= item.Top)
                continue;
            item.Hit = true;
            if (m_ShieldTime > 0.0f)
            {
                // the shield takes the hit: the obstacle bursts apart
                m_ShieldTime = 0.0f;
                r->SetShieldVisible(false, 0.0f);
                Burst(CrashBurst, { item.Lane == AllLanes ? px : LaneX(item.Lane), 1.0f, item.Z }, 50);
                item.E.SetActive(false);
                ShakeCamera(0.5f, 0.4f);
                PlaySound("Audio/shield_break.wav", 0.9f, 1.0f);
                continue;
            }
            m_LastHit = item.Obstacle;
            OnRunnerCrashed();
            return;
        }
        // pickups; the magnet pulls coins in from the lanes around
        auto& t = item.E.GetComponent<TransformComponent>();
        if (item.Pickup == PickupKind::Coin && m_MagnetTime > 0.0f && item.Z < pz + 1.0f && item.Z > pz - 14.0f)
        {
            t.Translation = Math::Damp(t.Translation, center, 9.0f, GetUnscaledDeltaTime());
            item.Z = t.Translation.z;
        }
        const glm::vec3 p = t.Translation;
        if (std::abs(p.x - px) < 0.9f && std::abs(p.z - pz) < 1.0f && p.y > py - 0.3f && p.y < py + height + 0.3f)
        {
            item.Taken = true;
            item.E.SetActive(false);
            switch (item.Pickup)
            {
                case PickupKind::Coin:
                    m_Coins++;
                    Burst(CoinBurst, p, 10);
                    PlaySound("Audio/coin.wav", 0.45f, 1.0f + 0.03f * (float)(m_Coins % 8));
                    if (Entity coins = Resolve(CoinsText))
                        GetScene().PunchUIScale(coins, 0.1f, 0.2f);
                    break;
                case PickupKind::Magnet:
                    m_MagnetTime = 8.0f;
                    Burst(CoinBurst, p, 30);
                    PlaySound("Audio/powerup.wav", 0.8f, 1.0f);
                    ShowBanner("", Localize("hud.magnet"), 1.2f);
                    break;
                case PickupKind::Shield:
                    m_ShieldTime = 12.0f;
                    r->SetShieldVisible(true, 12.0f);
                    Burst(CoinBurst, p, 30);
                    PlaySound("Audio/powerup.wav", 0.8f, 0.85f);
                    ShowBanner("", Localize("hud.shield"), 1.2f);
                    break;
            }
        }
    }
}

// ------------------------------------------------------------------------------------ presentation

void RunnerGame::UpdateCamera(float dt)
{
    const Runner* r = GetRunner();
    Entity cam = Resolve(CameraRig);
    if (!r || !cam)
        return;
    const float x = r->GetX(), y = r->GetY(), z = r->GetZ();
    // behind and above, a little to the side the runner moves to; the world moves with it
    const glm::vec3 goal(x * 0.45f, 4.4f + y * 0.35f, z + 7.8f);
    m_CamPos.x = Math::Damp(m_CamPos.x, goal.x, 6.0f, dt);
    m_CamPos.y = Math::Damp(m_CamPos.y, goal.y, 5.0f, dt);
    m_CamPos.z = goal.z;
    const glm::vec3 look(x * 0.55f, 0.9f + y * 0.25f, z - 10.0f);
    const glm::vec3 d = glm::normalize(look - m_CamPos);
    auto& t = cam.GetComponent<TransformComponent>();
    t.Translation = m_CamPos;
    t.Rotation = { std::asin(std::clamp(d.y, -1.0f, 1.0f)), std::atan2(-d.x, -d.z), (m_CamPos.x - goal.x) * 0.04f };
    // a wider view at speed
    const float speed01 = std::clamp((m_Speed - StartSpeed) / std::max(MaxSpeed - StartSpeed, 0.1f), 0.0f, 1.0f);
    m_Fov = Math::Damp(m_Fov, 60.0f + 14.0f * speed01, 3.0f, dt);
    if (auto* c = cam.TryGetComponent<CameraComponent>())
        c->Camera.SetPerspectiveVerticalFOV(glm::radians(m_Fov));

    // the endless road and sand: the planes follow, their textures stay put
    auto follow = [&](const EntityRef& ref, float tile) {
        if (Entity e = Resolve(ref))
        {
            auto& et = e.GetComponent<TransformComponent>();
            et.Translation.z = z - 100.0f;
            if (auto* mr = e.TryGetComponent<MeshRendererComponent>(); mr && tile > 0.0f)
                mr->Mat.Offset.y = -std::fmod(et.Translation.z / tile, 1.0f);
        }
    };
    follow(Road, 8.0f);
    follow(Sand, 10.0f);
    follow(CurbL, 0.0f);
    follow(CurbR, 0.0f);
}

void RunnerGame::UpdateHud()
{
    SetText(ScoreText, std::to_string(GetScore()));
    SetText(DistanceText, Localize("hud.distance", (int)m_Distance));
    SetText(CoinsText, Localize("hud.coins", m_Coins));
    SetText(SpeedText, Localize("hud.speed", (int)std::lround(m_Speed * 3.6f)));
    std::string power;
    if (m_MagnetTime > 0.0f)
        power += Localize("hud.magnet_time", (int)std::ceil(m_MagnetTime));
    if (m_ShieldTime > 0.0f)
        power += (power.empty() ? "" : "   ") + Localize("hud.shield_time", (int)std::ceil(m_ShieldTime));
    SetText(PowerText, power);
}

void RunnerGame::ShowBanner(const std::string& title, const std::string& subtitle, float seconds)
{
    if (m_State == State::Menu)
        return;
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

void RunnerGame::RefreshTexts()
{
    SetText(LanguageButton, Localize("menu.language", Localization::GetLanguageName(Localization::GetLanguage())));
    SetText(MenuBest, m_BestScore > 0 ? Localize("menu.best", m_BestScore, (int)m_BestDistance) : std::string());
    SetText(BestText, m_BestScore > 0 ? Localize("hud.best", m_BestScore) : std::string());
}

void RunnerGame::Burst(const EntityRef& ref, const glm::vec3& at, int count)
{
    if (Entity e = Resolve(ref))
    {
        e.GetComponent<TransformComponent>().Translation = at;
        GetScene().EmitParticles(e, count);
    }
}

void RunnerGame::SetText(const EntityRef& ref, const std::string& text)
{
    if (Entity e = Resolve(ref))
        if (auto* t = e.TryGetComponent<TextComponent>(); t && t->Text != text)
            t->Text = text;
}

void RunnerGame::SetActiveRef(const EntityRef& ref, bool active)
{
    if (Entity e = Resolve(ref); e && e.IsActive() != active)
        e.SetActive(active);
}

ZM_REGISTER_SCRIPT(RunnerGame,
    ZM_FIELD(RunnerGame, Player).Header("Dunyo"),
    ZM_FIELD(RunnerGame, CameraRig),
    ZM_FIELD(RunnerGame, Road).Tooltip("Yo'l tekisligi (yuguruvchi bilan siljiydi, teksturasi joyida turadi)"),
    ZM_FIELD(RunnerGame, Sand),
    ZM_FIELD(RunnerGame, CurbL),
    ZM_FIELD(RunnerGame, CurbR),
    ZM_FIELD(RunnerGame, CartTemplate).Header("To'siqlar"),
    ZM_FIELD(RunnerGame, FenceTemplate),
    ZM_FIELD(RunnerGame, GateTemplate),
    ZM_FIELD(RunnerGame, WallTemplate),
    ZM_FIELD(RunnerGame, CaravanTemplate),
    ZM_FIELD(RunnerGame, CoinTemplate).Header("Tangalar va bonuslar"),
    ZM_FIELD(RunnerGame, MagnetTemplate),
    ZM_FIELD(RunnerGame, ShieldTemplate),
    ZM_FIELD(RunnerGame, TowerTemplate).Header("Manzara"),
    ZM_FIELD(RunnerGame, PalmTemplate),
    ZM_FIELD(RunnerGame, DuneTemplate),
    ZM_FIELD(RunnerGame, HouseTemplate),
    ZM_FIELD(RunnerGame, ArchTemplate),
    ZM_FIELD(RunnerGame, CoinBurst).Header("Effektlar"),
    ZM_FIELD(RunnerGame, CrashBurst),
    ZM_FIELD(RunnerGame, Hud).Header("HUD"),
    ZM_FIELD(RunnerGame, ScoreText),
    ZM_FIELD(RunnerGame, DistanceText),
    ZM_FIELD(RunnerGame, CoinsText),
    ZM_FIELD(RunnerGame, SpeedText),
    ZM_FIELD(RunnerGame, PowerText),
    ZM_FIELD(RunnerGame, BestText),
    ZM_FIELD(RunnerGame, Banner),
    ZM_FIELD(RunnerGame, BannerSub),
    ZM_FIELD(RunnerGame, DemoBadge),
    ZM_FIELD(RunnerGame, MainMenu).Header("Menyular"),
    ZM_FIELD(RunnerGame, MenuBest),
    ZM_FIELD(RunnerGame, PlayButton),
    ZM_FIELD(RunnerGame, LanguageButton),
    ZM_FIELD(RunnerGame, PauseMenu),
    ZM_FIELD(RunnerGame, ResumeButton),
    ZM_FIELD(RunnerGame, GameOverMenu),
    ZM_FIELD(RunnerGame, OverScore),
    ZM_FIELD(RunnerGame, OverStats),
    ZM_FIELD(RunnerGame, OverRecord),
    ZM_FIELD(RunnerGame, RetryButton),
    ZM_FIELD_RANGE(RunnerGame, StartSpeed, 2.0f, 40.0f).Header("Qoidalar").Tooltip("Boshlang'ich tezlik (m/s)"),
    ZM_FIELD_RANGE(RunnerGame, MaxSpeed, 2.0f, 60.0f),
    ZM_FIELD_RANGE(RunnerGame, SpeedGain, 0.0f, 0.1f).Tooltip("Har metrda tezlik qanchaga oshadi (m/s)"),
    ZM_FIELD_RANGE(RunnerGame, SegmentLength, 10.0f, 60.0f).Tooltip("Har shuncha metrda bitta to'siq naqshi"),
    ZM_FIELD_RANGE(RunnerGame, SpawnAhead, 40.0f, 400.0f).Tooltip("Dunyo oldinda qancha masofagacha quriladi"),
    ZM_FIELD(RunnerGame, AutoDemo).Tooltip("Demo rejimida boshlash: bot yuguradi (ZAMIN_DEMO=1 ham shunday qiladi)"))
