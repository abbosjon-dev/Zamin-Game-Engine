#include "Sokoban.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <format>

using namespace Zamin;
using namespace Sokoban;

// O'yin oqimi: bosh menyu (orqada bot darajalarni yechadi) -> daraja (bloklar yuqoridan tushib keladi) -> yechildi
// (konfetti, robot aylanadi, natija oynasi: keyingi / qaytadan / menyu) -> ... -> hammasi yechildi.
// Boshqaruv (Loyiha sozlamalari > Input): "Move" vektori (WASD / strelkalar / D-pad / chap stik - bosib turilsa
// takrorlanadi), "Undo" (Z / Backspace / gamepad X), "Restart" (R / gamepad Y), "Pause" (Esc / P / Start).
// Rekord: har daraja uchun eng kam qadam (SaveData::SubmitRecord "Sokoban.Best.N", RecordOrder::Lower), keyingi
// o'ynaladigan daraja "Sokoban.Level".
// Demo: ZAMIN_DEMO=1 (yoki AutoDemo) - bot har darajani yechuvchi bilan yechadi (ikkinchi darajadan boshlab bir
// noto'g'ri qadam qo'yib, uni orqaga qaytaradi - undo ham ko'rinsin); jurnalga "Demo: ..." yoziladi.

static constexpr char UndoLetter = 'z';

// ZAMIN_DEMO_RECORDS=1: the demo bot's solutions count as records too (the headless tests check that a record is
// written and read back the next time; normally the bot never touches the player's records).
static bool DemoRecords()
{
    const char* value = std::getenv("ZAMIN_DEMO_RECORDS");
    return value && *value && std::string_view(value) != "0";
}

void SokobanGame::OnCreate()
{
    SetUpdateWhenPaused(true);
    const char* demo = std::getenv("ZAMIN_DEMO");
    m_DemoRun = AutoDemo || (demo && *demo && std::string_view(demo) != "0");
    m_Level = std::clamp(SaveData::GetInt("Sokoban.Level", 0), 0, (int)GetLevels().size() - 1);
    if (const char* start = std::getenv("ZAMIN_START_LEVEL"))
        m_Level = std::clamp(std::atoi(start) - 1, 0, (int)GetLevels().size() - 1);
    m_PlayerLevel = m_Level;
    std::string bests;
    for (int i = 0; i < (int)GetLevels().size(); i++)
        if (const int best = GetBest(i); best > 0)
            bests += std::format("{}{}-daraja {}", bests.empty() ? "" : ", ", i + 1, best);
    ZM_INFO("Rekordlar: {}/{} daraja yechilgan{}{}", CountSolved(), GetLevels().size(), bests.empty() ? "" : " - qadamlar: ", bests);
}

void SokobanGame::OnStart()
{
    auto click = [this] { PlaySound("Audio/ui_click.wav", 0.8f, 1.0f, "UI"); };
    AddMessageHandler("Play", [this, click](const ScriptMessage&) {
        click();
        // the first level not solved yet, from the last one the player played (not the bot behind the menu)
        const int count = (int)GetLevels().size();
        int level = m_PlayerLevel;
        for (int i = 0; i < count; i++)
            if (GetBest((m_PlayerLevel + i) % count) == 0)
            {
                level = (m_PlayerLevel + i) % count;
                break;
            }
        StartLevel(level, false);
    });
    AddMessageHandler("Levels", [this, click](const ScriptMessage&) { click(); ShowLevels(true); });
    AddMessageHandler("LevelsBack", [this, click](const ScriptMessage&) { click(); ShowLevels(false); });
    AddMessageHandler("Resume", [this, click](const ScriptMessage&) { click(); ClosePause(); });
    AddMessageHandler("Restart", [this, click](const ScriptMessage&) {
        click();
        if (m_State == State::Paused)
            ClosePause();
        StartLevel(m_Level, false);
    });
    AddMessageHandler("Next", [this, click](const ScriptMessage&) {
        click();
        StartLevel((m_Level + 1) % (int)GetLevels().size(), false);
    });
    AddMessageHandler("Menu", [this, click](const ScriptMessage&) { click(); ShowMenu(); });
    AddMessageHandler("Quit", [this](const ScriptMessage&) { QuitGame(); });
    AddMessageHandler("Language", [this, click](const ScriptMessage&) {
        click();
        if (Localization::NextLanguage())
            Localization::SaveSettings();
        RefreshTexts();
    });
    for (int i = 0; i < (int)GetLevels().size(); i++)
        AddMessageHandler(std::format("Level{}", i + 1), [this, click, i](const ScriptMessage&) {
            click();
            SetActiveRef(LevelsMenu, false);
            StartLevel(i, false);
        });
    if (m_DemoRun)
    {
        ZM_INFO("Demo rejimi: bot darajalarni yechadi (ZAMIN_DEMO), {}-darajadan", m_Level + 1);
        StartLevel(m_Level, true);
    }
    else
    {
        ShowMenu();
        // (automation: open the levels menu at once - screenshots of it)
        if (const char* levels = std::getenv("ZAMIN_SHOW_LEVELS"); levels && *levels && std::string_view(levels) != "0")
            ShowLevels(true);
    }
    RefreshTexts();
}

void SokobanGame::OnDestroy()
{
    if (m_DemoRun)
        ZM_INFO("Demo yakuni: {} ta daraja yechildi, oxirgisi {}-daraja ({} qadam)", m_SolvedInRun, m_Level + 1, m_Board.GetMoves());
    SaveData::Save();
}

void SokobanGame::OnLanguageChanged(const std::string&)
{
    RefreshTexts();
}

// ------------------------------------------------------------------------------------------- flow

void SokobanGame::ShowMenu()
{
    if (IsGamePaused())
        ResumeGame();
    Audio::SetGroupLowPass("Music", 0.0f);
    m_State = State::Menu;
    SetActiveRef(PauseMenu, false);
    SetActiveRef(CompleteMenu, false);
    SetActiveRef(LevelsMenu, false);
    SetActiveRef(Hud, false);
    SetActiveRef(MainMenu, true);
    ZM_INFO("Menyu: bosh menyu");
    if (Entity button = Resolve(PlayButton))
        GetScene().SetSelectedUI(button);
    StartLevel(m_PlayerLevel, true); // the bot solves it (and the ones after it) behind the menu
    m_State = State::Menu;
    RefreshTexts();
}

void SokobanGame::ShowLevels(bool show)
{
    SetActiveRef(LevelsMenu, show);
    if (show)
        ZM_INFO("Menyu: darajalar");
    else if (m_State == State::Menu)
        ZM_INFO("Menyu: bosh menyu");
    if (m_State == State::Menu)
        SetActiveRef(MainMenu, !show);
    RefreshLevelButtons();
    if (show)
    {
        if (Entity grid = Resolve(LevelGrid))
            if (std::vector<Entity> buttons = GetScene().GetChildren(grid); !buttons.empty())
                GetScene().SetSelectedUI(buttons[(size_t)std::clamp(m_PlayerLevel, 0, (int)buttons.size() - 1)]);
    }
    else if (Entity button = Resolve(PlayButton))
        GetScene().SetSelectedUI(button);
}

void SokobanGame::StartLevel(int index, bool bot)
{
    if (IsGamePaused())
        ResumeGame();
    m_Level = std::clamp(index, 0, (int)GetLevels().size() - 1);
    m_Bot = bot || m_DemoRun;
    const bool menu = m_State == State::Menu && bot && !m_DemoRun;
    if (menu)
        ZM_INFO("Omborchi: menyu orqasida {}-daraja (bot)", m_Level + 1);
    else
    {
        m_State = State::Playing;
        SetActiveRef(MainMenu, false);
        SetActiveRef(LevelsMenu, false);
        SetActiveRef(PauseMenu, false);
        SetActiveRef(CompleteMenu, false);
        SetActiveRef(Hud, true);
        GetScene().SetSelectedUI({});
        if (!m_Bot)
        {
            m_PlayerLevel = m_Level;
            SaveData::SetInt("Sokoban.Level", m_Level);
            ZM_INFO("Omborchi: {}-daraja ({})", m_Level + 1, GetLevels()[(size_t)m_Level].Name);
        }
    }
    SetActiveRef(CompleteMenu, false);
    if (!m_Board.Load(GetLevels()[(size_t)m_Level].Map))
        ZM_ERROR("Omborchi: {}-daraja noto'g'ri yozilgan (maqsadlar va qutilar soni teng emas)", m_Level + 1);
    BuildBoard();
    m_Anim = {};
    m_QueuedDir = -1;
    m_QueuedUndo = false;
    m_RepeatQueued = false;
    m_Spin = 0.0f;
    m_CompleteTimer = 0.0f;
    m_BotPath.clear();
    m_BotIndex = 0;
    m_BotTimer = 0.0f;
    m_LastOnGoals = m_Board.GetBoxesOnGoals();
    SetActiveRef(DemoBadge, m_Bot && m_State != State::Menu);
    if (m_State != State::Menu)
        ShowBanner(Localize("hud.level", m_Level + 1), LevelName(m_Level), 1.8f);
    PlaySound("Audio/level_start.wav", 0.6f);
    UpdateCamera(0.0f, false);
    RefreshTexts();
}

void SokobanGame::BuildBoard()
{
    // the old blocks go, the new ones fall into place row by row
    if (Entity root = Resolve(BoardRoot))
        for (Entity child : GetScene().GetChildren(root))
            Destroy(child);
    m_Tiles.clear();
    m_BoxEntities.clear();
    m_GoalEntities.clear();
    Entity root = Resolve(BoardRoot);
    auto spawn = [&](const EntityRef& tpl, const glm::vec3& pos, float delay) -> Entity {
        Entity t = Resolve(tpl);
        if (!t)
            return {};
        // each template keeps its own height above the floor (a wall's centre is half a block up)
        const glm::vec3 target = pos + glm::vec3(0.0f, t.GetComponent<TransformComponent>().Translation.y, 0.0f);
        Entity e = Instantiate(t, target + glm::vec3(0.0f, 7.0f, 0.0f));
        if (root)
            GetScene().SetParent(e, root, true);
        m_Tiles.push_back({ e, target, delay });
        return e;
    };
    for (int cell = 0; cell < m_Board.GetCellCount(); cell++)
    {
        const glm::vec3 p = CellPosition(cell);
        const float delay = 0.025f * (float)(m_Board.X(cell) + m_Board.Y(cell));
        if (m_Board.IsVisibleWall(cell))
            spawn(WallTemplate, p, delay);
        else if (m_Board.IsFloor(cell))
        {
            spawn(FloorTemplate, p, delay);
            if (m_Board.IsGoal(cell))
                m_GoalEntities[cell] = spawn(GoalTemplate, p, delay + 0.05f);
        }
    }
    for (int box : m_Board.GetBoxes())
        m_BoxEntities.push_back(spawn(BoxTemplate, CellPosition(box), 0.35f + 0.02f * (float)(m_Board.X(box) + m_Board.Y(box))));
    if (Entity t = Resolve(BoxTemplate))
        m_BoxLift = t.GetComponent<TransformComponent>().Translation.y;
    m_IntroTimer = 0.0f;
    if (Entity robot = Resolve(Robot))
        robot.GetComponent<TransformComponent>().Translation = CellPosition(m_Board.GetPlayer()) + glm::vec3(0.0f, 7.0f, 0.0f);
    m_RobotYaw = m_RobotYawGoal = 0.0f;
    StyleBoxesAndGoals(false);
}

void SokobanGame::OpenPause()
{
    m_State = State::Paused;
    PauseGame();
    SetActiveRef(PauseMenu, true);
    ZM_INFO("Menyu: pauza");
    if (Entity button = Resolve(ResumeButton))
        GetScene().SetSelectedUI(button);
    Audio::SetGroupLowPass("Music", 900.0f);
}

void SokobanGame::ClosePause()
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

void SokobanGame::LevelComplete()
{
    const bool menu = m_State == State::Menu;
    const int moves = m_Board.GetMoves(), pushes = m_Board.GetPushes();
    const int best = GetBest(m_Level);
    const bool scored = !m_Bot || (m_DemoRun && !menu && DemoRecords());
    const bool record = scored && SaveData::SubmitRecord(std::format("Sokoban.Best.{}", m_Level + 1), moves, RecordOrder::Lower);
    if (scored)
    {
        SaveData::SetInt("Sokoban.Level", (m_Level + 1) % (int)GetLevels().size()); // the next one, the next time
        SaveData::Save();
    }
    if (record)
        ZM_INFO("Yangi rekord: {}-daraja {} qadam{}", m_Level + 1, moves, best > 0 ? std::format(" (avvalgi {})", best) : std::string());
    if (!menu)
        m_State = State::Complete;
    m_CompleteTimer = 0.0f;
    m_Spin = 1.0f;
    Burst(Confetti, CellPosition(m_Board.GetPlayer()) + glm::vec3(0.0f, 0.8f, 0.0f), 90);
    PlaySound("Audio/level_complete.wav", 0.8f);
    if (m_Bot)
        m_SolvedInRun++;
    if (m_DemoRun)
        ZM_INFO("Demo: {}-daraja yechildi - qadamlar {}, surishlar {}", m_Level + 1, moves, pushes);
    if (menu)
        return;
    const bool last = m_Level == (int)GetLevels().size() - 1;
    ShowBanner(Localize(last ? "hud.all_done" : "hud.solved"), Localize("hud.solved_sub", moves, pushes), 2.2f);
    SetText(CompleteTitle, Localize(last ? "complete.all" : "complete.title", m_Level + 1));
    SetText(CompleteStats, Localize("complete.stats", moves, pushes));
    SetText(CompleteRecord, record ? Localize("complete.record") : best > 0 ? Localize("complete.best", best) : std::string());
    RefreshTexts();
}

// ------------------------------------------------------------------------------------------- update

void SokobanGame::OnUpdate(Timestep ts)
{
    const float dt = ts;
    const float realDt = GetUnscaledDeltaTime();
    m_Time += realDt;
    if (Input::IsActionJustPressed("Pause"))
    {
        if (m_State == State::Playing && !m_DemoRun)
            OpenPause();
        else if (m_State == State::Paused)
            ClosePause();
        else if (Entity levels = Resolve(LevelsMenu); m_State == State::Menu && levels && levels.IsActive())
            ShowLevels(false);
    }
    if (m_State == State::Paused)
        return;

    // the blocks fall into place
    m_IntroTimer += dt;
    for (const Tile& t : m_Tiles)
    {
        if (!t.E)
            continue;
        const float k = std::clamp((m_IntroTimer - t.Delay) / 0.45f, 0.0f, 1.0f);
        Entity e = t.E;
        auto& tr = e.GetComponent<TransformComponent>();
        const float drop = 1.0f - ApplyEase(Ease::OutBack, k);
        if (k < 1.0f || tr.Translation.y != t.Target.y)
            tr.Translation = { tr.Translation.x, t.Target.y + drop * 7.0f, tr.Translation.z };
    }
    const bool introDone = m_IntroTimer > 0.85f;

    if (introDone)
    {
        if (m_State == State::Complete || (m_State == State::Menu && m_Board.IsSolved()))
        {
            // (after the last step's animation) the next level comes by itself for the bot; a person uses the results panel
            if (!m_Anim.Active)
            {
                m_CompleteTimer += dt;
                Entity panel = Resolve(CompleteMenu);
                if (m_CompleteTimer > 1.0f && m_State == State::Complete && !m_Bot && panel && !panel.IsActive())
                {
                    SetActiveRef(CompleteMenu, true);
                    ZM_INFO("Menyu: natijalar - {}-daraja yechildi, {} qadam", m_Level + 1, m_Board.GetMoves());
                    if (Entity next = Resolve(NextButton))
                        GetScene().SetSelectedUI(next);
                }
                if (m_Bot && m_CompleteTimer > 2.6f)
                    StartLevel((m_Level + 1) % (int)GetLevels().size(), true);
            }
        }
        else if (m_Bot)
        {
            if (!m_Anim.Active)
                UpdateBot(dt);
        }
        else if (m_State == State::Playing)
            HandleInput(dt); // also while a step moves: presses then wait for it (TryStep / DoUndo queue them)
    }
    UpdateAnimation(dt);
    UpdateCamera(dt, false);
    UpdateHud();
    if (m_BannerTimer > 0.0f)
    {
        m_BannerTimer -= realDt;
        if (m_BannerTimer <= 0.0f)
            if (Entity banner = Resolve(Banner))
                GetScene().HideUI(banner);
    }
}

void SokobanGame::HandleInput(float dt)
{
    // the last box is on its goal: the step is still moving, then the level is complete
    if (m_Board.IsSolved())
        return;
    if (Input::IsActionJustPressed("Undo"))
    {
        DoUndo();
        return;
    }
    if (Input::IsActionJustPressed("Restart"))
    {
        DoRestart();
        return;
    }
    // one step per press; held: again after a moment, then steadily
    const glm::vec2 move = Input::GetVector("Move");
    int dir = -1;
    if (glm::length(move) > 0.5f)
        dir = std::abs(move.x) > std::abs(move.y) ? (move.x > 0.0f ? Right : Left) : (move.y > 0.0f ? Up : Down);
    if (dir != m_HeldDir)
    {
        // a held key's repeat still waiting for the step animation goes when the key is let go / turned; a tap stays
        if (m_RepeatQueued)
            m_QueuedDir = -1;
        m_RepeatQueued = false;
        m_HeldDir = dir;
        m_HoldTimer = 0.0f;
        if (dir >= 0)
            TryStep(dir);
        return;
    }
    if (dir >= 0)
    {
        m_HoldTimer += dt;
        if (m_HoldTimer > 0.28f)
        {
            m_HoldTimer -= 0.12f;
            m_RepeatQueued = m_Anim.Active;
            TryStep(dir);
        }
    }
}

void SokobanGame::TryStep(int dir)
{
    if (m_Anim.Active)
    {
        m_QueuedDir = dir; // one step waits for the current one
        return;
    }
    static const glm::vec3 offsets[4] = { { 0, 0, -1 }, { 0, 0, 1 }, { -1, 0, 0 }, { 1, 0, 0 } };
    m_RobotYawGoal = std::atan2(offsets[dir].x, offsets[dir].z);
    const Board::Move move = m_Board.Step(dir);
    m_Anim = {};
    m_Anim.Active = true;
    m_Anim.Duration = StepTime;
    if (!move.Moved)
    {
        // blocked: a little bump towards the wall
        m_Anim.Bump = true;
        m_Anim.PlayerFrom = CellPosition(m_Board.GetPlayer());
        m_Anim.PlayerTo = m_Anim.PlayerFrom + offsets[dir] * 0.22f;
        m_Anim.Duration = StepTime * 1.2f;
        PlaySound("Audio/bump.wav", 0.5f, 1.0f);
        return;
    }
    m_Anim.PlayerFrom = CellPosition(move.From);
    m_Anim.PlayerTo = CellPosition(move.To);
    if (move.Box >= 0)
    {
        m_Anim.Box = move.Box;
        m_Anim.BoxFrom = BoxPosition(move.BoxFrom);
        m_Anim.BoxTo = BoxPosition(move.BoxTo);
        m_Anim.Duration = StepTime * 1.25f; // pushing is heavier
        PlaySound("Audio/push.wav", 0.6f, 0.95f + 0.1f * (float)(m_Board.GetPushes() % 3));
        Burst(Dust, CellPosition(move.BoxFrom) + glm::vec3(0.0f, 0.05f, 0.0f) - offsets[dir] * 0.4f, 10);
    }
    else
        PlaySound("Audio/step.wav", 0.35f, 0.9f + 0.05f * (float)(m_Board.GetMoves() % 4));
}

void SokobanGame::DoUndo()
{
    if (m_Anim.Active)
    {
        m_QueuedUndo = true;
        return;
    }
    Board::Move undone;
    if (!m_Board.Undo(undone))
    {
        PlaySound("Audio/bump.wav", 0.4f, 0.8f);
        return;
    }
    m_Anim = {};
    m_Anim.Active = true;
    m_Anim.Duration = StepTime * 1.1f;
    m_Anim.PlayerFrom = CellPosition(undone.To);
    m_Anim.PlayerTo = CellPosition(undone.From);
    if (undone.Box >= 0)
    {
        m_Anim.Box = undone.Box;
        m_Anim.BoxFrom = BoxPosition(undone.BoxTo);
        m_Anim.BoxTo = BoxPosition(undone.BoxFrom);
    }
    PlaySound("Audio/undo.wav", 0.55f, 1.0f);
    if (m_DemoRun)
        ZM_INFO("Demo: orqaga qaytarildi (undo) - {} qadam", m_Board.GetMoves());
}

void SokobanGame::DoRestart()
{
    if (m_Board.GetMoves() == 0)
        return;
    m_Board.Reset();
    m_QueuedDir = -1;
    m_QueuedUndo = false;
    m_RepeatQueued = false;
    // everything glides back to where it started
    m_Anim = {};
    m_Anim.Active = true;
    m_Anim.Many = true;
    m_Anim.Duration = 0.35f;
    if (Entity robot = Resolve(Robot))
        m_Anim.PlayerFrom = robot.GetComponent<TransformComponent>().Translation;
    m_Anim.PlayerTo = CellPosition(m_Board.GetPlayer());
    PlaySound("Audio/restart.wav", 0.6f, 1.0f);
}

void SokobanGame::UpdateAnimation(float dt)
{
    Entity robot = Resolve(Robot);
    if (!robot)
        return;
    auto& rt = robot.GetComponent<TransformComponent>();
    if (m_IntroTimer < 0.85f)
    {
        // the robot drops in last
        const float k = std::clamp((m_IntroTimer - 0.45f) / 0.4f, 0.0f, 1.0f);
        rt.Translation = CellPosition(m_Board.GetPlayer()) + glm::vec3(0.0f, (1.0f - ApplyEase(Ease::OutBounce, k)) * 7.0f, 0.0f);
    }
    bool moving = false;
    if (m_Anim.Active)
    {
        moving = !m_Anim.Bump;
        m_Anim.Time += dt;
        const float k = std::clamp(m_Anim.Time / m_Anim.Duration, 0.0f, 1.0f);
        if (m_Anim.Bump)
        {
            const float b = std::sin(k * glm::pi<float>());
            rt.Translation = glm::mix(m_Anim.PlayerFrom, m_Anim.PlayerTo, b);
        }
        else if (m_Anim.Many)
        {
            const float e = ApplyEase(Ease::InOutCubic, k);
            rt.Translation = glm::mix(m_Anim.PlayerFrom, m_Anim.PlayerTo, e);
            for (size_t i = 0; i < m_BoxEntities.size(); i++)
                if (Entity box = m_BoxEntities[i])
                {
                    auto& bt = box.GetComponent<TransformComponent>().Translation;
                    bt = glm::mix(bt, BoxPosition(m_Board.GetBoxes()[i]), std::min(1.0f, e * 1.2f));
                }
        }
        else
        {
            const float e = ApplyEase(Ease::InOutQuad, k);
            rt.Translation = glm::mix(m_Anim.PlayerFrom, m_Anim.PlayerTo, e);
            if (m_Anim.Box >= 0 && m_Anim.Box < (int)m_BoxEntities.size())
                if (Entity box = m_BoxEntities[(size_t)m_Anim.Box])
                    box.GetComponent<TransformComponent>().Translation = glm::mix(m_Anim.BoxFrom, m_Anim.BoxTo, e);
        }
        if (k >= 1.0f)
        {
            m_Anim.Active = false;
            if (m_Anim.Many)
                for (size_t i = 0; i < m_BoxEntities.size(); i++)
                    if (Entity box = m_BoxEntities[i])
                        box.GetComponent<TransformComponent>().Translation = BoxPosition(m_Board.GetBoxes()[i]);
            if (!m_Anim.Bump)
                rt.Translation = m_Anim.PlayerTo;
            // a box reached / left a goal: the goals light up, a sparkle and a chime
            const int onGoals = m_Board.GetBoxesOnGoals();
            if (onGoals > m_LastOnGoals && m_Anim.Box >= 0)
            {
                Burst(GoalBurst, m_Anim.BoxTo + glm::vec3(0.0f, 0.1f, 0.0f), 26);
                PlaySound("Audio/goal.wav", 0.7f, 1.0f + 0.12f * (float)onGoals);
            }
            m_LastOnGoals = onGoals;
            StyleBoxesAndGoals(false);
            if (m_Board.IsSolved() && !m_Anim.Many && m_State != State::Complete)
                LevelComplete();
            else if (m_QueuedUndo)
            {
                m_QueuedUndo = false;
                DoUndo();
            }
            else if (m_QueuedDir >= 0)
            {
                const int dir = m_QueuedDir;
                m_QueuedDir = -1;
                m_RepeatQueued = false;
                TryStep(dir);
            }
        }
    }

    // ---- the robot's own motion: turns to face where it goes, bobs, leans in when it moves, spins when it wins
    m_RobotYaw += Math::DeltaAngle(m_RobotYaw, m_RobotYawGoal) * std::min(1.0f, 16.0f * dt);
    m_Lean = Math::Damp(m_Lean, moving ? (m_Anim.Box >= 0 ? 0.32f : 0.16f) : 0.0f, 14.0f, dt);
    m_Bob += dt * (moving ? 18.0f : 3.0f);
    float hop = 0.0f;
    if (m_Spin > 0.0f)
    {
        m_Spin = std::max(0.0f, m_Spin - dt * 0.9f);
        const float s = 1.0f - m_Spin;
        hop = std::sin(std::min(1.0f, s * 1.6f) * glm::pi<float>()) * 0.6f;
        m_RobotYaw += dt * 14.0f * m_Spin;
    }
    rt.Rotation = { 0.0f, m_RobotYaw, 0.0f };
    if (Entity body = Resolve(RobotBody))
    {
        auto& bt = body.GetComponent<TransformComponent>();
        bt.Translation = { 0.0f, std::abs(std::sin(m_Bob)) * (moving ? 0.06f : 0.025f) + hop, 0.0f };
        bt.Rotation = { m_Lean, 0.0f, std::sin(m_Bob * 0.5f) * (moving ? 0.05f : 0.015f) };
    }
    if (Entity eye = Resolve(RobotEye))
        if (auto* mr = eye.TryGetComponent<MeshRendererComponent>())
        {
            // a blink now and then
            const float blink = std::fmod(m_Time, 3.7f) < 0.12f ? 0.2f : 1.0f;
            mr->Mat.EmissionStrength = 6.0f * blink;
        }
}

void SokobanGame::StyleBoxesAndGoals(bool)
{
    for (size_t i = 0; i < m_BoxEntities.size(); i++)
    {
        Entity box = m_BoxEntities[i];
        if (!box)
            continue;
        const bool on = m_Board.IsGoal(m_Board.GetBoxes()[i]);
        if (auto* mr = box.TryGetComponent<MeshRendererComponent>())
        {
            mr->Mat.Emission = on ? glm::vec3(0.25f, 1.0f, 0.45f) : glm::vec3(0.0f);
            mr->Mat.EmissionStrength = on ? 0.18f : 0.0f;
            mr->Mat.Albedo = on ? glm::vec4(0.75f, 1.0f, 0.75f, 1.0f) : glm::vec4(1.0f);
        }
    }
    for (auto& [cell, goal] : m_GoalEntities)
    {
        if (!goal)
            continue;
        const bool filled = m_Board.BoxAt(cell) >= 0;
        const glm::vec3 color = filled ? glm::vec3(0.3f, 1.0f, 0.45f) : glm::vec3(0.25f, 0.8f, 1.0f);
        if (auto* mr = goal.TryGetComponent<MeshRendererComponent>())
        {
            mr->Mat.Emission = color;
            mr->Mat.EmissionStrength = filled ? 5.0f : 3.0f;
        }
        for (Entity child : GetScene().GetChildren(goal))
            if (auto* light = child.TryGetComponent<PointLightComponent>())
            {
                light->Color = color;
                light->Intensity = filled ? 2.2f : 1.4f;
            }
    }
}

// ------------------------------------------------------------------------------------------- bot

void SokobanGame::PlanBot()
{
    const auto t0 = std::chrono::steady_clock::now();
    size_t states = 0;
    m_BotPath = Solve(m_Board, 500000, &states);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    m_BotIndex = 0;
    if (m_BotPath.empty())
    {
        ZM_WARN("Omborchi: {}-daraja uchun yechim topilmadi ({} holat)", m_Level + 1, states);
        return;
    }
    if (m_DemoRun)
        ZM_INFO("Demo: {}-daraja yechimi topildi - {} qadam, {} holat, {:.1f} ms", m_Level + 1, m_BotPath.size(), states, ms);
    // from the second level on (a demo run): a wrong step first, then taken back - the undo at work
    if (m_DemoRun && m_Level > 0)
    {
        const int first = DirFromLetter(m_BotPath[0]);
        for (int d = 0; d < 4; d++)
        {
            Board probe = m_Board;
            if (d != first && probe.Step(d).Moved)
            {
                static const char letters[4] = { 'u', 'd', 'l', 'r' };
                m_BotPath = std::string(1, letters[d]) + UndoLetter + m_BotPath;
                break;
            }
        }
    }
}

void SokobanGame::UpdateBot(float dt)
{
    if (m_Board.IsSolved())
        return;
    if (m_BotPath.empty())
    {
        PlanBot();
        if (m_BotPath.empty())
            return;
        m_BotTimer = 0.4f;
    }
    m_BotTimer -= dt;
    if (m_BotTimer > 0.0f || m_BotIndex >= m_BotPath.size())
        return;
    const char letter = m_BotPath[m_BotIndex++];
    m_BotTimer = m_State == State::Menu ? 0.2f : 0.16f;
    if (letter == UndoLetter)
    {
        DoUndo();
        m_BotTimer = 0.45f;
    }
    else if (const int dir = DirFromLetter(letter); dir >= 0)
        TryStep(dir);
}

// --------------------------------------------------------------------------------------- presentation

glm::vec3 SokobanGame::BoxPosition(int cell) const
{
    return CellPosition(cell) + glm::vec3(0.0f, m_BoxLift, 0.0f);
}

glm::vec3 SokobanGame::CellPosition(int cell) const
{
    const float x = (float)m_Board.X(cell) - (float)(m_Board.GetWidth() - 1) * 0.5f;
    const float z = (float)m_Board.Y(cell) - (float)(m_Board.GetHeight() - 1) * 0.5f;
    return { x, 0.0f, z };
}

void SokobanGame::UpdateCamera(float dt, bool snap)
{
    Entity cam = Resolve(CameraRig);
    if (!cam)
        return;
    // far enough to see the whole level (a wide level needs less than a deep one: the view is wider than tall)
    const float w = (float)m_Board.GetWidth(), h = (float)m_Board.GetHeight();
    const float distance = 3.0f + std::max(w * 0.95f, h * 1.45f);
    const float pitch = glm::radians(CameraPitch);
    const glm::vec3 sway(std::sin(m_Time * 0.23f) * 0.35f, 0.0f, std::sin(m_Time * 0.17f) * 0.2f);
    const glm::vec3 goal = glm::vec3(0.0f, std::sin(pitch) * distance, std::cos(pitch) * distance + 0.4f) + sway;
    m_CameraPos = snap ? goal : Math::Damp(m_CameraPos, goal, 2.5f, dt);
    auto& t = cam.GetComponent<TransformComponent>();
    t.Translation = m_CameraPos;
    t.Rotation = { -pitch, sway.x * 0.03f, 0.0f };
}

void SokobanGame::UpdateHud()
{
    SetText(MovesText, Localize("hud.moves", m_Board.GetMoves()));
    SetText(PushesText, Localize("hud.pushes", m_Board.GetPushes()));
    SetText(BoxesText, Localize("hud.boxes", m_Board.GetBoxesOnGoals(), (int)m_Board.GetBoxes().size()));
}

void SokobanGame::RefreshTexts()
{
    SetText(LanguageButton, Localize("menu.language", Localization::GetLanguageName(Localization::GetLanguage())));
    SetText(LevelText, Localize("hud.level_name", m_Level + 1, LevelName(m_Level)));
    const int best = GetBest(m_Level);
    SetText(BestText, best > 0 ? Localize("hud.best", best) : Localize("hud.no_best"));
    SetText(HintText, Localize("hud.hint"));
    SetText(MenuProgress, Localize("menu.progress", CountSolved(), (int)GetLevels().size()));
    RefreshLevelButtons();
}

void SokobanGame::RefreshLevelButtons()
{
    Entity grid = Resolve(LevelGrid);
    if (!grid)
        return;
    std::vector<Entity> buttons = GetScene().GetChildren(grid);
    for (size_t i = 0; i < buttons.size() && i < GetLevels().size(); i++)
    {
        const int best = GetBest((int)i);
        if (auto* b = buttons[i].TryGetComponent<UIButtonComponent>())
        {
            // solved: green, not yet: blue
            b->NormalColor = best > 0 ? glm::vec4(0.25f, 0.7f, 0.4f, 1.0f) : glm::vec4(0.3f, 0.45f, 0.85f, 1.0f);
            b->HoverColor = best > 0 ? glm::vec4(0.35f, 0.85f, 0.5f, 1.0f) : glm::vec4(0.45f, 0.6f, 1.0f, 1.0f);
        }
        const std::string label = best > 0 ? std::format("{}\n{}", i + 1, Localize("levels.moves", best)) : std::to_string(i + 1);
        if (auto* t = buttons[i].TryGetComponent<TextComponent>(); t && t->Text != label)
            t->Text = label;
    }
}

void SokobanGame::ShowBanner(const std::string& title, const std::string& subtitle, float seconds)
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

void SokobanGame::Burst(const EntityRef& ref, const glm::vec3& at, int count)
{
    if (Entity e = Resolve(ref))
    {
        e.GetComponent<TransformComponent>().Translation = at;
        GetScene().EmitParticles(e, count);
    }
}

std::string SokobanGame::LevelName(int index) const
{
    const Level& level = GetLevels()[(size_t)std::clamp(index, 0, (int)GetLevels().size() - 1)];
    return Localization::GetLanguage() == "uz" ? level.Name : level.NameEn;
}

int SokobanGame::GetBest(int index) const
{
    return SaveData::GetInt(std::format("Sokoban.Best.{}", index + 1), 0);
}

int SokobanGame::CountSolved() const
{
    int solved = 0;
    for (int i = 0; i < (int)GetLevels().size(); i++)
        if (GetBest(i) > 0)
            solved++;
    return solved;
}

void SokobanGame::SetText(const EntityRef& ref, const std::string& text)
{
    if (Entity e = Resolve(ref))
        if (auto* t = e.TryGetComponent<TextComponent>(); t && t->Text != text)
            t->Text = text;
}

void SokobanGame::SetActiveRef(const EntityRef& ref, bool active)
{
    if (Entity e = Resolve(ref); e && e.IsActive() != active)
        e.SetActive(active);
}

ZM_REGISTER_SCRIPT(SokobanGame,
    ZM_FIELD(SokobanGame, BoardRoot).Header("Daraja").Tooltip("Daraja bloklari shu obyektning bolalari bo'ladi"),
    ZM_FIELD(SokobanGame, WallTemplate),
    ZM_FIELD(SokobanGame, FloorTemplate),
    ZM_FIELD(SokobanGame, GoalTemplate),
    ZM_FIELD(SokobanGame, BoxTemplate),
    ZM_FIELD(SokobanGame, Robot).Header("Robot va kamera"),
    ZM_FIELD(SokobanGame, RobotBody),
    ZM_FIELD(SokobanGame, RobotEye),
    ZM_FIELD(SokobanGame, CameraRig),
    ZM_FIELD(SokobanGame, Dust).Header("Effektlar"),
    ZM_FIELD(SokobanGame, GoalBurst),
    ZM_FIELD(SokobanGame, Confetti),
    ZM_FIELD(SokobanGame, Hud).Header("HUD"),
    ZM_FIELD(SokobanGame, LevelText),
    ZM_FIELD(SokobanGame, MovesText),
    ZM_FIELD(SokobanGame, PushesText),
    ZM_FIELD(SokobanGame, BoxesText),
    ZM_FIELD(SokobanGame, BestText),
    ZM_FIELD(SokobanGame, HintText),
    ZM_FIELD(SokobanGame, Banner),
    ZM_FIELD(SokobanGame, BannerSub),
    ZM_FIELD(SokobanGame, DemoBadge),
    ZM_FIELD(SokobanGame, MainMenu).Header("Menyular"),
    ZM_FIELD(SokobanGame, MenuProgress),
    ZM_FIELD(SokobanGame, PlayButton),
    ZM_FIELD(SokobanGame, LanguageButton),
    ZM_FIELD(SokobanGame, LevelsMenu),
    ZM_FIELD(SokobanGame, LevelGrid).Tooltip("Daraja tugmalari (bolalari, tartib bilan)"),
    ZM_FIELD(SokobanGame, LevelsBackButton),
    ZM_FIELD(SokobanGame, PauseMenu),
    ZM_FIELD(SokobanGame, ResumeButton),
    ZM_FIELD(SokobanGame, CompleteMenu),
    ZM_FIELD(SokobanGame, CompleteTitle),
    ZM_FIELD(SokobanGame, CompleteStats),
    ZM_FIELD(SokobanGame, CompleteRecord),
    ZM_FIELD(SokobanGame, NextButton),
    ZM_FIELD_RANGE(SokobanGame, StepTime, 0.05f, 0.5f).Header("Sozlamalar").Tooltip("Bir qadam necha soniya davom etadi"),
    ZM_FIELD_RANGE(SokobanGame, CameraPitch, 20.0f, 89.0f).Tooltip("Kamera pastga qancha qaraydi (gradus)"),
    ZM_FIELD(SokobanGame, AutoDemo).Tooltip("Demo rejimida boshlash: bot o'ynaydi (ZAMIN_DEMO=1 ham shunday qiladi)"))
