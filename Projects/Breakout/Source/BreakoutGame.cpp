#include "Breakout.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <format>

using namespace Zamin;
using namespace BreakoutUtil;

// O'yin oqimi: bosh menyu (orqada bot 1-darajani o'ynaydi), o'yin, pauza, daraja tugashi, o'yin tugashi / g'alaba.
// Darajalar Levels.cpp'dagi qatorlardan quriladi (g'isht shabloni Instantiate qilinadi, qatorma-qator paydo bo'ladi).
// Hisob: g'isht ochkosi x kombo (raketkaga tegmasdan ketma-ket singan g'ishtlar). Bonuslar (taxminan har 7-g'ishtdan):
// keng raketka, uchta to'p, sekin to'p, olov to'pi, lazer, +1 jon. Rekord SaveData'da ("Breakout.BestScore",
// "Breakout.BestLevel", SaveData::SubmitRecord): o'yin tugaganda, pauzadan chiqib ketilganda yoki o'yin yopilganda.
// Demo: ZAMIN_DEMO=1 (yoki AutoDemo) - bot o'ynaydi, jurnalga "Demo: ..." yoziladi; ZAMIN_DEMO_RECORDS=1 bilan demo
// natijasi ham rekordga yoziladi (testlar).

BreakoutGame* BreakoutGame::s_Instance = nullptr;

namespace BreakoutUtil {

    bool DemoRecords()
    {
        const char* value = std::getenv("ZAMIN_DEMO_RECORDS");
        return value && *value && std::string_view(value) != "0";
    }

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

static bool ParseBrick(char c, BrickKind& kind, glm::vec3& color)
{
    static const std::pair<char, glm::vec3> colors[] = {
        { 'r', { 1.0f, 0.3f, 0.35f } }, { 'o', { 1.0f, 0.58f, 0.22f } }, { 'y', { 1.0f, 0.88f, 0.3f } },
        { 'g', { 0.4f, 0.95f, 0.45f } }, { 'c', { 0.3f, 0.88f, 1.0f } }, { 'b', { 0.38f, 0.52f, 1.0f } },
        { 'p', { 0.72f, 0.42f, 1.0f } }, { 'k', { 1.0f, 0.45f, 0.78f } }, { 'w', { 0.92f, 0.94f, 1.0f } },
    };
    switch (c)
    {
        case 'S': kind = BrickKind::Steel; color = { 0.8f, 0.84f, 0.95f }; return true;
        case '#': kind = BrickKind::Gold; color = { 1.0f, 0.8f, 0.35f }; return true;
        case 'X': kind = BrickKind::Bomb; color = { 1.0f, 0.4f, 0.25f }; return true;
        default: break;
    }
    const char lower = (char)std::tolower((unsigned char)c);
    for (const auto& [key, col] : colors)
        if (key == lower)
        {
            kind = c == lower ? BrickKind::Normal : BrickKind::Tough;
            color = col;
            return true;
        }
    return false;
}

void BreakoutGame::OnCreate()
{
    s_Instance = this;
    SetUpdateWhenPaused(true);
    const char* demo = std::getenv("ZAMIN_DEMO");
    m_DemoRun = AutoDemo || (demo && *demo && std::string_view(demo) != "0");
    m_BestScore = SaveData::GetInt("Breakout.BestScore", 0);
    m_BestLevel = SaveData::GetInt("Breakout.BestLevel", 0);
    ZM_INFO("Rekordlar: hisob {}, {}-daraja", m_BestScore, m_BestLevel);
    if (const char* level = std::getenv("ZAMIN_START_LEVEL"))
        StartLevel = std::atoi(level);
}

void BreakoutGame::OnStart()
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
        StartGame();
    }
    else
        ShowMenu();
    RefreshMenuTexts();
}

void BreakoutGame::OnDestroy()
{
    SubmitRecords("o'yin yopildi");
    if (m_DemoRun)
        LogDemo("yakuni");
    Cursor::SetVisible(true);
    if (s_Instance == this)
        s_Instance = nullptr;
}

void BreakoutGame::OnLanguageChanged(const std::string&)
{
    RefreshMenuTexts();
    if (m_Level >= 0 && m_Level < (int)GetLevelLayouts().size())
        SetText(LevelNameText, Localize(GetLevelLayouts()[(size_t)m_Level].NameKey));
}

PaddleController* BreakoutGame::GetPaddle() const
{
    return GetScript<PaddleController>(Resolve(Paddle));
}

float BreakoutGame::GetBallSpeed() const
{
    const Ball* any = nullptr;
    if (Entity tpl = Resolve(BallTemplate))
        any = GetScript<Ball>(tpl);
    const float base = any ? any->Speed : 13.0f;
    const float max = any ? any->MaxSpeed : 22.0f;
    float speed = base + 0.9f * (float)m_Level + 0.03f * (float)m_BricksHitThisLevel;
    speed = std::min(speed, max);
    if (m_SlowTime > 0.0f)
        speed *= 0.65f;
    return speed;
}

// ------------------------------------------------------------------------------------------- flow

void BreakoutGame::StartSession(bool bot, bool demoRun, int level)
{
    m_Bot = bot;
    m_DemoRun = demoRun;
    m_Lives = StartLives;
    m_Score = 0;
    m_ShownScore = 0.0f;
    m_Combo = m_BestCombo = 0;
    m_BricksBroken = 0;
    m_LevelsCleared = 0;
    m_NewRecord = false;
    m_RecordPending = !bot || (demoRun && DemoRecords());
    m_RestartTimer = -1.0f;
    m_DemoLogTimer = 0.0f;
    m_Sessions++;
    if (bot)
        m_Rng.SetSeed(99 + (uint64_t)m_Sessions);
    else
        m_Rng.Randomize();
    if (PaddleController* paddle = GetPaddle())
        paddle->ResetPaddle(bot);
    SetActiveRef(DemoBadge, bot && m_State != State::Menu);
    LoadLevel(level);
}

void BreakoutGame::ClearField()
{
    for (Ball* b : Live<Ball>(*this))
        Destroy(b->GetEntity());
    for (Brick* b : Live<Brick>(*this))
        Destroy(b->GetEntity());
    for (PowerUp* p : Live<PowerUp>(*this))
        Destroy(p->GetEntity());
    for (FxAnimator* fx : Live<FxAnimator>(*this))
        Destroy(fx->GetEntity());
    m_PendingBreaks.clear();
    m_WideTime = m_SlowTime = m_FireTime = m_LaserTime = 0.0f;
    if (PaddleController* paddle = GetPaddle())
    {
        paddle->SetWide(false);
        paddle->SetLaser(false);
    }
}

void BreakoutGame::LoadLevel(int index)
{
    ClearField();
    const auto& levels = GetLevelLayouts();
    m_Level = std::clamp(index, 0, (int)levels.size() - 1);
    m_BricksHitThisLevel = 0;
    m_Combo = 0;
    const LevelLayout& layout = levels[(size_t)m_Level];
    Entity tpl = Resolve(BrickTemplate);
    Entity parent = Resolve(Bricks);
    const float colWidth = 1.56f, rowHeight = 0.68f;
    int count = 0;
    for (size_t row = 0; row < layout.Rows.size(); row++)
    {
        const std::string_view line = layout.Rows[row];
        for (size_t col = 0; col < line.size() && col < 13; col++)
        {
            BrickKind kind;
            glm::vec3 color;
            if (!ParseBrick(line[col], kind, color) || !tpl)
                continue;
            const glm::vec3 pos{ ((float)col - 6.0f) * colWidth, FieldTop - 1.4f - (float)row * rowHeight, 0.0f };
            Entity e = Instantiate(tpl, pos);
            if (parent)
                GetScene().SetParent(e, parent, true);
            if (Brick* brick = GetScript<Brick>(e))
                brick->Setup(kind, color, 0.25f + 0.05f * (float)row + 0.025f * (float)col);
            count++;
        }
    }
    m_State = m_State == State::Menu ? State::Menu : State::Playing;
    SetText(LevelText, Localize("hud.level", m_Level + 1, (int)levels.size()));
    SetText(LevelNameText, Localize(layout.NameKey));
    ShowBanner(Localize("hud.level_banner", m_Level + 1), Localize(layout.NameKey), 2.2f);
    m_ServeTimer = 0.8f; // the bricks appear first
    if (m_DemoRun)
        ZM_INFO("Demo: {}-daraja ({}) boshlandi - {} ta g'isht (hisob {})", m_Level + 1, Localize(layout.NameKey), count, m_Score);
}

void BreakoutGame::ServeBall()
{
    Entity tpl = Resolve(BallTemplate);
    PaddleController* paddle = GetPaddle();
    if (!tpl || !paddle)
        return;
    Entity e = Instantiate(tpl, glm::vec3(paddle->GetPosition(), 0.0f) + glm::vec3(0.0f, 0.6f, 0.0f));
    if (Ball* ball = GetScript<Ball>(e))
        ball->Hold(m_Rng.Range(-0.6f, 0.6f));
    m_BotHold = 0.7f;
}

void BreakoutGame::ShowMenu()
{
    SubmitRecords("bosh menyuga qaytildi"); // a game left from the pause menu still counts
    if (IsGamePaused())
        ResumeGame();
    Audio::SetGroupLowPass("Music", 0.0f);
    m_State = State::Menu;
    SetActiveRef(PauseMenu, false);
    SetActiveRef(GameOverMenu, false);
    SetActiveRef(Hud, false);
    SetActiveRef(MainMenu, true);
    ZM_INFO("Menyu: bosh menyu");
    Cursor::SetVisible(true);
    if (Entity button = Resolve(PlayButton))
        GetScene().SetSelectedUI(button);
    RefreshMenuTexts();
    StartSession(true, false, 0);
}

void BreakoutGame::StartGame()
{
    SubmitRecords("qaytadan boshlandi");
    if (IsGamePaused())
        ResumeGame();
    Audio::SetGroupLowPass("Music", 0.0f);
    m_State = State::Playing;
    SetActiveRef(MainMenu, false);
    SetActiveRef(PauseMenu, false);
    SetActiveRef(GameOverMenu, false);
    SetActiveRef(Hud, true);
    GetScene().SetSelectedUI({});
    StartSession(m_DemoRun, m_DemoRun, std::clamp(StartLevel, 1, (int)GetLevelLayouts().size()) - 1);
    Cursor::SetVisible(m_Bot);
    if (!m_Bot)
        ZM_INFO("Breakout: o'yin boshlandi");
}

void BreakoutGame::OpenPause()
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

void BreakoutGame::ClosePause()
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

void BreakoutGame::EndGame(bool victory)
{
    m_State = State::GameOver;
    const int reached = m_Level + 1;
    SubmitRecords(victory ? "g'alaba" : "o'yin tugadi");
    SetText(OverTitle, Localize(victory ? "over.victory" : "over.title"));
    if (Entity title = Resolve(OverTitle))
        if (auto* t = title.TryGetComponent<TextComponent>())
            t->Color = victory ? glm::vec4(0.5f, 1.0f, 0.6f, 1.0f) : glm::vec4(1.0f, 0.45f, 0.55f, 1.0f);
    SetText(OverScore, FormatScore(m_Score));
    SetText(OverLevel, Localize("over.level", reached, m_BricksBroken));
    SetText(OverRecord, m_NewRecord ? Localize("over.record") : Localize("over.best", FormatScore(m_BestScore)));
    SetActiveRef(GameOverMenu, true);
    ZM_INFO("Menyu: natijalar ({}) - hisob {}, {}-daraja", victory ? "g'alaba" : "o'yin tugadi", m_Score, reached);
    Cursor::SetVisible(true);
    if (Entity button = Resolve(RetryButton))
        GetScene().SetSelectedUI(button);
    if (m_NewRecord)
        if (Entity record = Resolve(OverRecord))
            GetScene().TweenUI(record, UITweenProperty::Scale, glm::vec4(1.12f), 0.45f, { .Easing = Ease::InOutQuad, .Loop = UITweenLoop::PingPong });
    Sfx(victory ? "level_clear" : "gameover", 0.9f);
    if (m_DemoRun)
    {
        LogDemo(victory ? "g'alaba" : "o'yin tugadi");
        m_RestartTimer = 3.0f;
    }
    else
        ZM_INFO("Breakout: {} - hisob {}, {}-daraja", victory ? "g'alaba" : "o'yin tugadi", m_Score, reached);
}

// ---------------------------------------------------------------------------------------- update

void BreakoutGame::OnUpdate(Timestep ts)
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

    if (m_RestartTimer >= 0.0f)
    {
        m_RestartTimer -= realDt;
        if (m_RestartTimer < 0.0f)
        {
            if (m_State == State::Menu)
                StartSession(true, false, 0);
            else if (m_DemoRun)
            {
                SetActiveRef(GameOverMenu, false);
                m_State = State::Playing;
                StartSession(true, true, 0);
            }
        }
    }

    // bombs' chain reactions
    for (size_t i = 0; i < m_PendingBreaks.size();)
    {
        m_PendingBreaks[i].Timer -= dt;
        if (m_PendingBreaks[i].Timer <= 0.0f)
        {
            if (Brick* brick = GetScript<Brick>(m_PendingBreaks[i].Brick); brick && !brick->IsBroken())
                brick->Break();
            m_PendingBreaks.erase(m_PendingBreaks.begin() + (long)i);
        }
        else
            i++;
    }

    const bool active = m_State == State::Playing || m_State == State::Menu;
    if (active)
    {
        UpdatePowerTimers(dt);
        if (m_ServeTimer >= 0.0f)
        {
            m_ServeTimer -= dt;
            if (m_ServeTimer < 0.0f)
                ServeBall();
        }
        // the level is done when every breakable brick is gone
        if (m_ServeTimer < 0.0f && m_StateTimer <= 0.0f && CountBreakable() == 0 && m_PendingBreaks.empty())
        {
            const int bonus = 1000 * (m_Level + 1) + 250 * m_Lives;
            m_Score += bonus;
            m_LevelsCleared++;
            ShowBanner(Localize("hud.level_clear"), Localize("hud.level_bonus", FormatScore(bonus)), 2.2f);
            Sfx("level_clear", 0.9f);
            for (Ball* b : Live<Ball>(*this))
            {
                Burst(b->GetPosition(), { 0.6f, 0.9f, 1.0f }, 10, 10);
                Destroy(b->GetEntity());
            }
            for (PowerUp* p : Live<PowerUp>(*this))
                Destroy(p->GetEntity());
            m_StateTimer = 2.6f;
            if (m_DemoRun)
                ZM_INFO("Demo: {}-daraja tugadi (hisob {}, jon {})", m_Level + 1, m_Score, m_Lives);
            if (m_State != State::Menu)
                m_State = State::LevelClear;
        }
        if (m_Bot)
            UpdateBot(dt);
        // lasers: the action button (the bot: whenever it can)
        if (m_LaserTime > 0.0f)
        {
            m_LaserCooldown -= dt;
            const bool want = m_Bot ? true : Input::IsActionPressed("Action");
            const std::vector<Ball*> balls = LiveBalls();
            const bool ballHeld = std::any_of(balls.begin(), balls.end(), [](Ball* b) { return b->IsHeld(); });
            if (want && !ballHeld && m_LaserCooldown <= 0.0f)
                FireLasers();
        }
    }
    if (m_StateTimer > 0.0f)
    {
        m_StateTimer -= dt;
        if (m_StateTimer <= 0.0f)
        {
            if (m_State == State::LevelClear || m_State == State::Menu)
            {
                const int next = m_Level + 1;
                if (next >= (int)GetLevelLayouts().size())
                {
                    if (m_State == State::Menu)
                        StartSession(true, false, 0);
                    else
                        EndGame(true);
                }
                else
                {
                    if (m_State == State::LevelClear)
                        m_State = State::Playing;
                    LoadLevel(next);
                }
            }
        }
    }

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

void BreakoutGame::UpdatePowerTimers(float dt)
{
    PaddleController* paddle = GetPaddle();
    auto tick = [&](float& t, const std::function<void()>& onEnd) {
        if (t <= 0.0f)
            return;
        t -= dt;
        if (t <= 0.0f)
        {
            t = 0.0f;
            onEnd();
        }
    };
    tick(m_WideTime, [&] { if (paddle) paddle->SetWide(false); });
    tick(m_SlowTime, [] {});
    tick(m_FireTime, [] {});
    tick(m_LaserTime, [&] { if (paddle) paddle->SetLaser(false); });
}

// ------------------------------------------------------------------------------------------- bot

void BreakoutGame::UpdateBot(float dt)
{
    PaddleController* paddle = GetPaddle();
    if (!paddle)
        return;
    const std::vector<Ball*> balls = LiveBalls();
    const float top = paddle->GetTop();
    const float left = -FieldHalfWidth + Ball::Radius, right = FieldHalfWidth - Ball::Radius;

    // a held ball: launch it after a moment
    for (Ball* b : balls)
        if (b->IsHeld())
        {
            m_BotHold -= dt;
            paddle->SetBotTarget(std::sin(GetTime() * 0.9f) * 3.0f);
            if (m_BotHold <= 0.0f)
                b->Launch();
            return;
        }

    // the ball that reaches the paddle's line first, and where
    const Ball* urgent = nullptr;
    float bestTime = 1e9f, landing = 0.0f;
    for (const Ball* b : balls)
    {
        const glm::vec2 v = b->GetMoveVelocity();
        if (v.y >= -0.01f)
            continue;
        const float t = (b->GetPosition().y - Ball::Radius - top) / -v.y;
        if (t < -0.05f || t >= bestTime)
            continue;
        // fold the straight line back into the field (wall bounces)
        float x = b->GetPosition().x + v.x * std::max(t, 0.0f);
        const float width = right - left;
        float u = std::fmod(x - left, 2.0f * width);
        if (u < 0.0f)
            u += 2.0f * width;
        x = left + (u <= width ? u : 2.0f * width - u);
        bestTime = t;
        landing = x;
        urgent = b;
    }
    if (urgent)
    {
        // hit it off-centre so it flies towards the side with more bricks left
        float sumX = 0.0f;
        int n = 0;
        for (Brick* brick : Live<Brick>(*this))
            if (brick->IsBreakable() && !brick->IsBroken())
            {
                sumX += brick->GetPosition().x;
                n++;
            }
        const float goal = n > 0 ? sumX / (float)n : 0.0f;
        const float angle = std::clamp(std::atan2(goal - landing, 14.0f), glm::radians(-50.0f), glm::radians(50.0f));
        const float offset = std::clamp(angle / glm::radians(62.0f), -0.75f, 0.75f);
        paddle->SetBotTarget(landing - offset * paddle->GetHalfWidth());
        return;
    }
    // nothing coming down: catch a power-up, else follow the ball
    const PowerUp* catchMe = nullptr;
    for (const PowerUp* p : Live<PowerUp>(*this))
        if (!catchMe || p->GetPosition().y < catchMe->GetPosition().y)
            catchMe = p;
    if (catchMe && catchMe->GetPosition().y > top - 0.5f)
        paddle->SetBotTarget(catchMe->GetPosition().x);
    else if (!balls.empty())
        paddle->SetBotTarget(balls.front()->GetPosition().x);
}

// ------------------------------------------------------------------------------------- game events

void BreakoutGame::OnBrickHit(Brick& brick, bool broken)
{
    const glm::vec2 pos = brick.GetPosition();
    m_BricksHitThisLevel++;
    if (!broken)
    {
        if (brick.Kind == BrickKind::Gold)
            Sfx("gold", 0.5f, m_Rng.Range(0.95f, 1.05f));
        else
        {
            Sfx("steel", 0.5f, brick.Kind == BrickKind::Steel ? 1.0f : 1.3f);
            m_Score += 10;
        }
        Burst(pos, brick.Color, 0, 6);
        return;
    }
    m_BricksBroken++;
    m_Combo++;
    m_BestCombo = std::max(m_BestCombo, m_Combo);
    const int multiplier = std::min(5, 1 + m_Combo / 4);
    const int points = brick.Points * multiplier;
    m_Score += points;
    Burst(pos, brick.Color, brick.Kind == BrickKind::Bomb ? 30 : 14, 10);
    Sfx("brick", 0.55f, std::min(2.0f, 0.85f + 0.06f * (float)m_Combo));
    Sfx("break", 0.35f, m_Rng.Range(0.9f, 1.15f));
    if (multiplier > 1 || brick.Kind != BrickKind::Normal)
        Popup(pos, "+" + std::to_string(points), glm::vec4(glm::mix(brick.Color, glm::vec3(1.0f), 0.5f) * 1.5f, 1.0f), multiplier > 2 ? 1.25f : 1.0f);
    if (Entity score = Resolve(ScoreText))
        GetScene().PunchUIScale(score, 0.06f, 0.18f);
    if (brick.Kind == BrickKind::Bomb)
    {
        Ring(pos, { 1.0f, 0.5f, 0.2f }, 1.6f);
        Sfx("bomb", 0.9f);
        ShakeCamera(0.55f, 0.4f);
        for (Entity e : OverlapBox2D(pos, { 2.4f, 0.9f }, 0.0f, LayerMask({ "Brick" })))
            if (Brick* other = GetScript<Brick>(e); other && other != &brick && !other->IsBroken() && other->IsBreakable())
                QueueBreak(e, 0.06f + 0.02f * glm::distance(other->GetPosition(), pos));
    }
    else
        ShakeCamera(0.12f, 0.15f);
    if (m_Rng.Bool(PowerUpChance))
        SpawnPowerUp(pos);
}

void BreakoutGame::QueueBreak(Entity brick, float delay)
{
    for (const PendingBreak& p : m_PendingBreaks)
        if (p.Brick == brick)
            return;
    m_PendingBreaks.push_back({ brick, delay });
}

void BreakoutGame::OnPaddleHit(Ball& ball, float offset)
{
    m_Combo = 0;
    if (PaddleController* paddle = GetPaddle())
        paddle->Bump();
    Sfx("paddle", 0.6f, 1.0f + 0.15f * std::abs(offset));
    Burst(ball.GetPosition() - glm::vec2(0.0f, Ball::Radius), { 0.4f, 0.9f, 1.0f }, 0, 6);
}

void BreakoutGame::OnBallLost(Ball& ball)
{
    Burst({ ball.GetPosition().x, FieldBottom + 0.6f }, { 1.0f, 0.3f, 0.6f }, 8, 14);
    // the last ball in play?
    for (Ball* b : LiveBalls())
        if (b != &ball && !b->IsLost())
            return;
    m_Combo = 0;
    m_Lives--;
    m_WideTime = m_SlowTime = m_FireTime = m_LaserTime = 0.0f;
    if (PaddleController* paddle = GetPaddle())
    {
        paddle->SetWide(false);
        paddle->SetLaser(false);
    }
    Sfx("ball_lost", 0.8f);
    ShakeCamera(0.4f, 0.4f);
    if (m_Lives <= 0)
    {
        if (m_State == State::Menu)
            m_RestartTimer = 1.0f;
        else
            EndGame(false);
        return;
    }
    ShowBanner(Localize("hud.ball_lost"), Localize("hud.lives_left", m_Lives), 1.4f);
    m_ServeTimer = 1.2f;
}

void BreakoutGame::OnPowerUpCaught(PowerKind kind, const glm::vec2& position)
{
    PaddleController* paddle = GetPaddle();
    const char* key = "";
    switch (kind)
    {
        case PowerKind::Wide:
            m_WideTime = 15.0f;
            if (paddle)
                paddle->SetWide(true);
            key = "powerup.wide";
            break;
        case PowerKind::Multi:
        {
            key = "powerup.multi";
            std::vector<Ball*> balls = LiveBalls();
            Ball* source = nullptr;
            for (Ball* b : balls)
                if (!b->IsHeld())
                    source = b;
            if (!source && !balls.empty())
            {
                source = balls.front();
                source->Launch();
            }
            if (source && Resolve(BallTemplate))
                for (float angle : { -25.0f, 25.0f })
                {
                    Entity e = Instantiate(Resolve(BallTemplate), glm::vec3(source->GetPosition(), 0.0f));
                    if (Ball* b = GetScript<Ball>(e))
                    {
                        const glm::vec2 v = source->GetMoveVelocity();
                        const float a = glm::radians(angle);
                        const glm::vec2 dir = glm::length(v) > 0.01f ? glm::normalize(v) : glm::vec2(0.0f, 1.0f);
                        b->LaunchDirection({ dir.x * std::cos(a) - dir.y * std::sin(a), dir.x * std::sin(a) + dir.y * std::cos(a) });
                    }
                }
            break;
        }
        case PowerKind::Slow:
            m_SlowTime = 10.0f;
            key = "powerup.slow";
            break;
        case PowerKind::Fire:
            m_FireTime = 8.0f;
            key = "powerup.fire";
            break;
        case PowerKind::Laser:
            m_LaserTime = 10.0f;
            if (paddle)
                paddle->SetLaser(true);
            key = "powerup.laser";
            break;
        case PowerKind::Life:
            m_Lives = std::min(6, m_Lives + 1);
            key = "powerup.life";
            break;
    }
    Popup(position + glm::vec2(0.0f, 0.8f), Localize(key), { 1.6f, 1.6f, 1.2f, 1.0f }, 1.2f);
    Ring(position, { 0.6f, 1.0f, 0.8f }, 0.8f);
    Sfx("powerup", 0.8f);
}

void BreakoutGame::SpawnPowerUp(const glm::vec2& position)
{
    // the life capsule is rarer; the bot's demo gets the same ones each run (seeded)
    const float weights[] = { 1.0f, 1.0f, 0.8f, 0.8f, 0.9f, 0.35f };
    const EntityRef* refs[] = { &WidePowerUp, &MultiPowerUp, &SlowPowerUp, &FirePowerUp, &LaserPowerUp, &LifePowerUp };
    const int pick = m_Rng.WeightedIndex(weights);
    if (Entity tpl = Resolve(*refs[pick >= 0 ? pick : 0]))
    {
        Instantiate(tpl, glm::vec3(position, 0.0f));
        Sfx("powerup_drop", 0.4f);
    }
}

void BreakoutGame::FireLasers()
{
    PaddleController* paddle = GetPaddle();
    if (!paddle)
        return;
    m_LaserCooldown = 0.32f;
    for (const glm::vec2& origin : paddle->GetTurrets())
    {
        const RaycastHit hit = Raycast2D(origin, { 0.0f, 1.0f }, 30.0f, LayerMask({ "Brick", "Wall" }));
        const float length = hit ? hit.Distance : 30.0f;
        if (Entity tpl = Resolve(BeamTemplate))
        {
            Entity beam = Instantiate(tpl, glm::vec3(origin.x, origin.y + length * 0.5f, 0.0f));
            beam.GetComponent<TransformComponent>().Scale = { 0.22f, length, 1.0f };
        }
        if (hit)
        {
            Entity target = GetScene().GetEntityByUUID(hit.EntityID);
            if (Brick* brick = GetScript<Brick>(target))
                brick->Hit(false);
            Burst(glm::vec2(hit.Point), { 1.0f, 0.4f, 0.5f }, 0, 5);
        }
    }
    Sfx("laser", 0.4f, m_Rng.Range(0.95f, 1.1f));
}

// --------------------------------------------------------------------------------------- effects

void BreakoutGame::Burst(const glm::vec2& position, const glm::vec3& color, int shards, int sparks)
{
    if (Entity e = Resolve(Shards); e && shards > 0)
    {
        e.GetComponent<TransformComponent>().Translation = glm::vec3(position, 0.0f);
        if (auto* ps = e.TryGetComponent<ParticleSystemComponent>())
        {
            ps->StartColor = glm::vec4(color * 1.5f, 1.0f);
            ps->EndColor = glm::vec4(color, 0.0f);
        }
        GetScene().EmitParticles(e, shards);
    }
    if (Entity e = Resolve(Sparks); e && sparks > 0)
    {
        e.GetComponent<TransformComponent>().Translation = glm::vec3(position, 0.0f);
        if (auto* ps = e.TryGetComponent<ParticleSystemComponent>())
        {
            ps->StartColor = glm::vec4(glm::mix(color, glm::vec3(1.0f), 0.5f) * 2.2f, 1.0f);
            ps->EndColor = glm::vec4(color, 0.0f);
        }
        GetScene().EmitParticles(e, sparks);
    }
}

void BreakoutGame::Ring(const glm::vec2& position, const glm::vec3& color, float scale)
{
    if (Entity tpl = Resolve(RingTemplate))
    {
        Entity r = Instantiate(tpl, glm::vec3(position, 0.0f));
        if (auto* fx = GetScript<FxAnimator>(r))
            fx->SetScaleMultiplier(scale);
        if (auto* sprite = r.TryGetComponent<SpriteRendererComponent>())
            sprite->Color = glm::vec4(color * 2.0f, 1.0f);
    }
}

void BreakoutGame::Popup(const glm::vec2& position, const std::string& text, const glm::vec4& color, float scale)
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

void BreakoutGame::Sfx(const char* name, float volume, float pitch)
{
    PlaySound(std::format("Audio/{}.wav", name), volume, pitch);
}

// ------------------------------------------------------------------------------------------- HUD

void BreakoutGame::UpdateHud(float realDt)
{
    m_ShownScore += ((float)m_Score - m_ShownScore) * std::min(1.0f, 10.0f * realDt);
    if (std::abs((float)m_Score - m_ShownScore) < 1.0f)
        m_ShownScore = (float)m_Score;
    SetText(ScoreText, FormatScore((int)std::round(m_ShownScore)));
    SetText(BestText, FormatScore(std::max(m_BestScore, m_Bot ? 0 : m_Score)));
    const int multiplier = std::min(5, 1 + m_Combo / 4);
    SetText(ComboText, multiplier > 1 ? Localize("hud.combo", multiplier) : std::string());
    SetText(BricksText, Localize("hud.bricks", CountBreakable()));
    if (Entity lives = Resolve(Lives))
    {
        int i = 0;
        for (Entity icon : GetScene().GetChildren(lives))
        {
            if (icon.IsActive() != (i < m_Lives))
                icon.SetActive(i < m_Lives);
            i++;
        }
    }
    std::string power;
    auto add = [&](float t, const char* key) {
        if (t > 0.0f)
            power += (power.empty() ? "" : "\n") + Localize(key, std::format("{:.1f}", t));
    };
    add(m_WideTime, "hud.wide");
    add(m_SlowTime, "hud.slow");
    add(m_FireTime, "hud.fire");
    add(m_LaserTime, "hud.laser");
    SetText(PowerText, power);
    bool held = false;
    for (Ball* b : LiveBalls())
        held = held || b->IsHeld();
    SetText(HintText, held && !m_Bot ? Localize("hud.serve") : std::string());

    if (m_BannerTimer > 0.0f)
    {
        m_BannerTimer -= realDt;
        if (m_BannerTimer <= 0.0f)
            if (Entity banner = Resolve(Banner))
                GetScene().HideUI(banner);
    }
}

void BreakoutGame::ShowBanner(const std::string& title, const std::string& subtitle, float seconds)
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

void BreakoutGame::RefreshMenuTexts()
{
    SetText(LanguageButton, Localize("menu.language", Localization::GetLanguageName(Localization::GetLanguage())));
    SetText(MenuBest, m_BestScore > 0 ? Localize("menu.best", FormatScore(m_BestScore), m_BestLevel) : std::string());
}

void BreakoutGame::SetText(const EntityRef& ref, const std::string& text)
{
    if (Entity e = Resolve(ref))
        if (auto* t = e.TryGetComponent<TextComponent>(); t && t->Text != text)
            t->Text = text;
}

void BreakoutGame::SetActiveRef(const EntityRef& ref, bool active)
{
    if (Entity e = Resolve(ref); e && e.IsActive() != active)
        e.SetActive(active);
}

int BreakoutGame::CountBreakable() const
{
    int n = 0;
    for (const Brick* b : Live<Brick>(*this))
        if (b->IsBreakable() && !b->IsBroken())
            n++;
    return n;
}

std::vector<Ball*> BreakoutGame::LiveBalls() const
{
    std::vector<Ball*> balls = Live<Ball>(*this);
    std::erase_if(balls, [](Ball* b) { return b->IsLost(); });
    return balls;
}

// The result of a scored game (a player's, or a demo's with ZAMIN_DEMO_RECORDS): once, when it ends or is left.
void BreakoutGame::SubmitRecords(const char* when)
{
    if (!m_RecordPending)
        return;
    m_RecordPending = false;
    const int reached = m_Level + 1;
    if (m_Score > 0 && SaveData::SubmitRecord("Breakout.BestScore", m_Score))
    {
        m_BestScore = m_Score;
        m_NewRecord = true;
        ZM_INFO("Yangi rekord: hisob {} ({}-daraja; {})", m_Score, reached, when);
    }
    if (SaveData::SubmitRecord("Breakout.BestLevel", reached))
        m_BestLevel = reached;
    SaveData::Save();
}

void BreakoutGame::LogDemo(const char* reason)
{
    ZM_INFO("Demo ({}): hisob {}, {}-daraja, singan g'ishtlar {}, tugagan darajalar {}, eng katta kombo {}", reason, m_Score, m_Level + 1,
            m_BricksBroken, m_LevelsCleared, m_BestCombo);
}

ZM_REGISTER_SCRIPT(BreakoutGame,
    ZM_FIELD(BreakoutGame, Paddle).Header("O'yin obyektlari"),
    ZM_FIELD(BreakoutGame, Bricks).Tooltip("Daraja g'ishtlari shu obyekt ichida yaratiladi"),
    ZM_FIELD(BreakoutGame, BrickTemplate).Header("Shablonlar"),
    ZM_FIELD(BreakoutGame, BallTemplate),
    ZM_FIELD(BreakoutGame, WidePowerUp),
    ZM_FIELD(BreakoutGame, MultiPowerUp),
    ZM_FIELD(BreakoutGame, SlowPowerUp),
    ZM_FIELD(BreakoutGame, FirePowerUp),
    ZM_FIELD(BreakoutGame, LaserPowerUp),
    ZM_FIELD(BreakoutGame, LifePowerUp),
    ZM_FIELD(BreakoutGame, BeamTemplate),
    ZM_FIELD(BreakoutGame, RingTemplate),
    ZM_FIELD(BreakoutGame, PopupTemplate),
    ZM_FIELD(BreakoutGame, Shards).Header("Effektlar"),
    ZM_FIELD(BreakoutGame, Sparks),
    ZM_FIELD(BreakoutGame, Hud).Header("HUD"),
    ZM_FIELD(BreakoutGame, ScoreText),
    ZM_FIELD(BreakoutGame, BestText),
    ZM_FIELD(BreakoutGame, Lives),
    ZM_FIELD(BreakoutGame, ComboText),
    ZM_FIELD(BreakoutGame, LevelText),
    ZM_FIELD(BreakoutGame, LevelNameText),
    ZM_FIELD(BreakoutGame, BricksText),
    ZM_FIELD(BreakoutGame, PowerText),
    ZM_FIELD(BreakoutGame, HintText),
    ZM_FIELD(BreakoutGame, Banner),
    ZM_FIELD(BreakoutGame, BannerSub),
    ZM_FIELD(BreakoutGame, DemoBadge),
    ZM_FIELD(BreakoutGame, MainMenu).Header("Menyular"),
    ZM_FIELD(BreakoutGame, PauseMenu),
    ZM_FIELD(BreakoutGame, GameOverMenu),
    ZM_FIELD(BreakoutGame, MenuBest),
    ZM_FIELD(BreakoutGame, PlayButton),
    ZM_FIELD(BreakoutGame, LanguageButton),
    ZM_FIELD(BreakoutGame, ResumeButton),
    ZM_FIELD(BreakoutGame, RetryButton),
    ZM_FIELD(BreakoutGame, OverTitle),
    ZM_FIELD(BreakoutGame, OverScore),
    ZM_FIELD(BreakoutGame, OverLevel),
    ZM_FIELD(BreakoutGame, OverRecord),
    ZM_FIELD(BreakoutGame, FieldHalfWidth).Header("Maydon"),
    ZM_FIELD(BreakoutGame, FieldTop),
    ZM_FIELD(BreakoutGame, FieldBottom).Tooltip("Shu chiziqdan pastga tushgan to'p yo'qoladi"),
    ZM_FIELD_RANGE(BreakoutGame, StartLives, 1, 6).Header("Qoidalar"),
    ZM_FIELD_RANGE(BreakoutGame, PowerUpChance, 0.0f, 1.0f).Tooltip("Singan g'ishtdan bonus tushish ehtimoli"),
    ZM_FIELD(BreakoutGame, AutoDemo).Tooltip("Demo rejimida boshlash: bot o'ynaydi (ZAMIN_DEMO=1 ham shunday qiladi)"),
    ZM_FIELD_RANGE(BreakoutGame, StartLevel, 1, 6).Tooltip("O'yin shu darajadan boshlanadi (darajani sinash uchun; ZAMIN_START_LEVEL ham)"))
