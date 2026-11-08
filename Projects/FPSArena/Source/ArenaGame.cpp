#include "Arena.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <format>

using namespace Zamin;

// Arena o'yini, ikki rejim:
//  - Parkur: yorqin nuqtalardan ketma-ket o'tish, aylana vaqti va rekord (SaveData "FPSArena.BestLap"); yo'l
//    bo'yidagi nishonlarni otish ochko beradi.
//  - Nishon otish: RangeSeconds soniyalik raund - nishonlar tezroq qaytadi, ochko (markaz - x2, ketma-ket tegishlar
//    seriyasi - x4 gacha), aniqlik, rekord (SaveData "FPSArena.BestRange"); oxirida natijalar oynasi.
// HUD (vaqt, nuqta / nishonlar, ochko, seriya, o'q-dori, qayta o'qlash, tegish belgisi, "+ochko" yozuvlari, tezlik,
// holat, chidamlilik), menyular (bosh menyu - orqada bot yuguradi va otadi; pauza - sichqoncha sezgirligi slayderi,
// SaveData "FPSArena.Sensitivity"; natijalar). O'yin paytida kursor qulflanadi.
// Demo: ZAMIN_DEMO=1 (yoki AutoDemo) - bot parkur yo'lagini aylanadi va yo'lda nishonlarni otadi; ZAMIN_DEMO=range -
// bot "Nishon otish" raundlarini o'ynaydi. Jurnalga "Demo: ..." yoziladi.

static const glm::vec3 SpawnPoint{ 0.0f, 0.05f, -20.0f };
static constexpr float SpawnYaw = glm::pi<float>(); // facing +Z (into the arena)
static const glm::vec3 RangeSpawn{ 0.0f, 2.1f, 3.0f };  // on the central platform, facing north
static constexpr float RangeYaw = 0.0f;

void ArenaGame::OnCreate()
{
    s_Instance = this;
    SetUpdateWhenPaused(true);
    const char* demo = std::getenv("ZAMIN_DEMO");
    m_DemoRun = AutoDemo || (demo && *demo && std::string_view(demo) != "0");
    m_DemoMode = demo && std::string_view(demo) == "range" ? Mode::Range : Mode::Parkour;
    m_BestLap = SaveData::GetFloat("FPSArena.BestLap", 0.0f);
    m_BestRange = SaveData::GetInt("FPSArena.BestRange", 0);
    m_Sensitivity = SaveData::GetFloat("FPSArena.Sensitivity", 1.0f);
    if (Entity group = Resolve(Checkpoints))
        m_Points = GetScene().GetChildren(group);
    if (Entity group = Resolve(Popups))
        m_PopupPool = GetScene().GetChildren(group);
}

void ArenaGame::OnStart()
{
    auto click = [this] { PlaySound("Audio/ui_click.wav", 0.8f, 1.0f, "UI"); };
    AddMessageHandler("Play", [this, click](const ScriptMessage&) { click(); StartGame(Mode::Parkour); });
    AddMessageHandler("PlayRange", [this, click](const ScriptMessage&) { click(); StartGame(Mode::Range); });
    AddMessageHandler("Resume", [this, click](const ScriptMessage&) { click(); ClosePause(); });
    AddMessageHandler("Restart", [this, click](const ScriptMessage&) { click(); StartGame(m_Mode); });
    AddMessageHandler("Menu", [this, click](const ScriptMessage&) { click(); ShowMenu(); });
    AddMessageHandler("Quit", [this](const ScriptMessage&) { QuitGame(); });
    AddMessageHandler("Language", [this, click](const ScriptMessage&) {
        click();
        if (Localization::NextLanguage())
            Localization::SaveSettings();
        RefreshTexts();
    });
    Subscribe("UISliderChanged", [this](const ScriptMessage& m) {
        if (m.Sender != Resolve(SensitivitySlider))
            return;
        m_Sensitivity = m.Get<float>();
        if (FirstPersonController* fpc = GetController())
            fpc->SetSensitivityScale(m_Sensitivity);
        SaveData::SetFloat("FPSArena.Sensitivity", m_Sensitivity);
        RefreshTexts();
    });
    if (Entity slider = Resolve(SensitivitySlider))
        GetScene().SetSliderValue(slider, m_Sensitivity, false);
    if (FirstPersonController* fpc = GetController())
        fpc->SetSensitivityScale(m_Sensitivity);
    if (m_DemoRun)
    {
        ZM_INFO("Demo rejimi: {} - bot o'ynaydi (ZAMIN_DEMO)", m_DemoMode == Mode::Range ? "nishon otish" : "parkur yo'lagi");
        StartGame(m_DemoMode);
    }
    else
        ShowMenu();
    RefreshTexts();
}

void ArenaGame::OnDestroy()
{
    if (m_DemoRun)
        if (const FirstPersonController* fpc = GetController())
            ZM_INFO("Demo yakuni: {} ta aylana, keyingi nuqta {}, {:.0f} m yurildi, {} ta nishon urildi, ochko {}, aniqlik {}%", m_Laps, m_Next + 1,
                    fpc->GetDistanceTravelled(), m_TargetsDown, m_Score, GetAccuracy());
    Cursor::SetLockMode(CursorLockMode::None);
    Cursor::SetVisible(true);
    if (m_DemoRun || m_Laps > 0 || m_TargetsDown > 0)
        SaveData::Save();
    if (s_Instance == this)
        s_Instance = nullptr;
}

void ArenaGame::OnLanguageChanged(const std::string&)
{
    RefreshTexts();
}

FirstPersonController* ArenaGame::GetController() const
{
    return GetScript<FirstPersonController>(Resolve(Player));
}

Blaster* ArenaGame::GetBlaster() const
{
    return GetScript<Blaster>(Resolve(Gun));
}

// ------------------------------------------------------------------------------------------- flow

void ArenaGame::StartSession(bool bot)
{
    m_Bot = bot;
    m_Next = 0;
    m_LapTime = 0.0f;
    m_Laps = 0;
    m_Score = m_Shots = m_Hits = m_Streak = m_BestStreak = m_TargetsDown = 0;
    m_RangeLeft = RangeSeconds;
    m_LastSecond = -1;
    m_DemoLogTimer = 0.0f;
    const bool range = m_Mode == Mode::Range;
    if (FirstPersonController* fpc = GetController())
    {
        fpc->ResetTo(range ? RangeSpawn : SpawnPoint, range ? RangeYaw : SpawnYaw, bot);
        fpc->UseRoute(range ? 1 : 0);
        fpc->SetControlsEnabled(true);
    }
    if (Blaster* gun = GetBlaster())
        gun->Refill();
    for (Target* t : FindObjectsOfType<Target>())
        t->ResetTarget(range);
    SetActiveRef(Checkpoints, !range);
    StyleCheckpoints();
    SetActiveRef(DemoBadge, bot && m_State != State::Menu);
    if (m_State != State::Menu)
    {
        if (range)
            ShowBanner(Localize("hud.range"), Localize("hud.range_sub", (int)RangeSeconds), 2.2f);
        else
            ShowBanner(Localize("hud.start"), Localize("hud.start_sub"), 2.2f);
    }
    ApplyCursor();
    RefreshTexts();
}

void ArenaGame::ShowMenu()
{
    if (IsGamePaused())
        ResumeGame();
    Audio::SetGroupLowPass("Music", 0.0f);
    m_State = State::Menu;
    m_Mode = Mode::Parkour;
    SetActiveRef(PauseMenu, false);
    SetActiveRef(ResultsMenu, false);
    SetActiveRef(Hud, false);
    SetActiveRef(MainMenu, true);
    ZM_INFO("Menyu: bosh menyu");
    if (Entity button = Resolve(PlayButton))
        GetScene().SetSelectedUI(button);
    StartSession(true); // the bot runs the course (and shoots) behind the menu
    RefreshTexts();
}

void ArenaGame::StartGame(Mode mode)
{
    if (IsGamePaused())
        ResumeGame();
    Audio::SetGroupLowPass("Music", 0.0f);
    m_State = State::Playing;
    m_Mode = mode;
    SetActiveRef(MainMenu, false);
    SetActiveRef(PauseMenu, false);
    SetActiveRef(ResultsMenu, false);
    SetActiveRef(Hud, true);
    GetScene().SetSelectedUI({});
    StartSession(m_DemoRun);
    if (!m_Bot)
        ZM_INFO("FPS Arena: o'yin boshlandi ({})", mode == Mode::Range ? "nishon otish" : "parkur");
}

void ArenaGame::OpenPause()
{
    m_State = State::Paused;
    PauseGame();
    SetActiveRef(PauseMenu, true);
    ZM_INFO("Menyu: pauza");
    if (Entity button = Resolve(ResumeButton))
        GetScene().SetSelectedUI(button);
    Audio::SetGroupLowPass("Music", 800.0f);
    ApplyCursor();
}

void ArenaGame::ClosePause()
{
    if (m_State != State::Paused)
        return;
    m_State = State::Playing;
    SetActiveRef(PauseMenu, false);
    ZM_INFO("Menyu: pauza yopildi");
    GetScene().SetSelectedUI({});
    Audio::SetGroupLowPass("Music", 0.0f);
    ResumeGame();
    ApplyCursor();
}

void ArenaGame::EndRange()
{
    m_State = State::Results;
    const bool record = !m_Bot && m_Score > m_BestRange;
    if (record)
    {
        m_BestRange = m_Score;
        SaveData::SetInt("FPSArena.BestRange", m_BestRange);
        SaveData::Save();
    }
    PlaySound("Audio/lap.wav", 0.8f);
    SetText(ResultsTitle, Localize("results.title"));
    SetText(ResultsScore, Localize("results.score", m_Score));
    SetText(ResultsStats, Localize("results.stats", m_TargetsDown, GetAccuracy(), m_BestStreak));
    SetText(ResultsRecord, record ? Localize("results.record") : Localize("results.best", m_BestRange));
    SetActiveRef(ResultsMenu, true);
    ZM_INFO("Menyu: natijalar - ochko {}, {} ta nishon", m_Score, m_TargetsDown);
    if (Entity button = Resolve(RetryButton))
        GetScene().SetSelectedUI(button);
    m_ResultsTimer = 0.0f;
    if (m_DemoRun)
        ZM_INFO("Demo: raund tugadi - ochko {}, {} ta nishon, aniqlik {}%, eng uzun seriya {}", m_Score, m_TargetsDown, GetAccuracy(), m_BestStreak);
    ApplyCursor();
    RefreshTexts();
}

void ArenaGame::ApplyCursor()
{
    // locked and hidden while a person plays; free in the menus and while the bot plays
    const bool locked = m_State == State::Playing && !m_Bot;
    Cursor::SetLockMode(locked ? CursorLockMode::Locked : CursorLockMode::None);
    Cursor::SetVisible(!locked);
    if (FirstPersonController* fpc = GetController())
        fpc->SetControlsEnabled((m_State == State::Playing || m_Bot) && m_State != State::Results);
    if (Blaster* gun = GetBlaster())
        gun->SetFireEnabled((m_State == State::Playing || (m_Bot && m_State == State::Menu)) && m_State != State::Results);
}

// ---------------------------------------------------------------------------------------- scoring

void ArenaGame::OnShot(bool hit)
{
    if (m_State == State::Menu)
        return; // the attract mode behind the menu does not count
    m_Shots++;
    if (hit)
        m_Hits++;
    else
        m_Streak = 0;
}

void ArenaGame::OnTargetHit(const Target& target, int points, bool bullseye)
{
    m_HitMarker = 1.0f;
    if (Entity marker = Resolve(HitMarker))
        if (auto* img = marker.TryGetComponent<UIImageComponent>())
            img->Color = bullseye ? glm::vec4(1.0f, 0.35f, 0.25f, 1.0f) : glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
    PlaySound("Audio/ding.wav", 0.55f, bullseye ? 1.35f : 1.0f + 0.04f * (float)std::min(m_Streak, 8), "UI");
    if (m_State == State::Menu)
        return;
    m_Streak++;
    m_BestStreak = std::max(m_BestStreak, m_Streak);
    m_TargetsDown++;
    const int multiplier = 1 + std::min(3, m_Streak / 3);
    const int gained = points * multiplier;
    m_Score += gained;
    if (bullseye)
        ShowPopup(Localize("hud.bullseye", gained), { 1.0f, 0.45f, 0.3f, 1.0f });
    else
        ShowPopup(std::format("+{}", gained), target.GetKind() == TargetKind::Drone ? glm::vec4(0.5f, 0.9f, 1.0f, 1.0f) : glm::vec4(1.0f, 0.9f, 0.5f, 1.0f));
    if (Entity score = Resolve(ScoreText))
        GetScene().PunchUIScale(score, 0.12f, 0.25f);
    if (multiplier > 1 && m_Streak % 3 == 0)
        if (Entity streak = Resolve(StreakText))
            GetScene().PunchUIScale(streak, 0.25f, 0.3f);
    if (m_DemoRun && m_TargetsDown % 5 == 0)
        ZM_INFO("Demo: {} ta nishon urildi, ochko {}, aniqlik {}%", m_TargetsDown, m_Score, GetAccuracy());
}

void ArenaGame::ShowPopup(const std::string& text, const glm::vec4& color)
{
    if (m_PopupPool.empty())
        return;
    Entity popup = m_PopupPool[m_NextPopup++ % m_PopupPool.size()];
    if (auto* t = popup.TryGetComponent<TextComponent>())
    {
        t->Text = text;
        t->Color = color;
    }
    Scene& scene = GetScene();
    scene.CancelUITweens(popup, false);
    scene.TweenUI(popup, UITweenProperty::Offset, glm::vec4(0.0f, 80.0f, 0.0f, 0.0f), 0.8f,
                  { .Easing = Ease::OutCubic, .From = glm::vec4(0.0f) });
    scene.TweenUI(popup, UITweenProperty::Alpha, glm::vec4(0.0f), 0.45f, { .Easing = Ease::InQuad, .Delay = 0.35f, .From = glm::vec4(1.0f) });
    scene.TweenUI(popup, UITweenProperty::Scale, glm::vec4(1.0f), 0.3f, { .Easing = Ease::OutBack, .From = glm::vec4(1.5f) });
}

// ---------------------------------------------------------------------------------------- update

void ArenaGame::OnUpdate(Timestep ts)
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
    }
    if (m_State == State::Paused)
        return;
    if (m_State == State::Results)
    {
        // the demo starts the next round by itself
        m_ResultsTimer += realDt;
        if (m_DemoRun && m_ResultsTimer > 4.0f)
            StartGame(m_Mode);
    }
    else if (m_Mode == Mode::Range && m_State == State::Playing)
        UpdateRange(dt);
    else
        UpdateCourse(dt);
    UpdateHud(realDt);
    if (m_BannerTimer > 0.0f)
    {
        m_BannerTimer -= realDt;
        if (m_BannerTimer <= 0.0f)
            if (Entity banner = Resolve(Banner))
                GetScene().HideUI(banner);
    }
    if (m_DemoRun && m_State == State::Playing)
    {
        m_DemoLogTimer += dt;
        if (m_DemoLogTimer >= 10.0f)
        {
            m_DemoLogTimer = 0.0f;
            if (const FirstPersonController* fpc = GetController())
                ZM_INFO("Demo (holat): keyingi nuqta {}, {} ta aylana, {:.0f} m yurildi, vaqt {}, {} ta nishon, ochko {}", m_Next + 1, m_Laps,
                        fpc->GetDistanceTravelled(), FormatTime(m_Mode == Mode::Range ? m_RangeLeft : m_LapTime), m_TargetsDown, m_Score);
        }
    }
}

void ArenaGame::UpdateRange(float dt)
{
    m_RangeLeft = std::max(0.0f, m_RangeLeft - dt);
    // the last five seconds tick
    const int second = (int)std::ceil(m_RangeLeft);
    if (second != m_LastSecond && second <= 5 && second > 0)
    {
        PlaySound("Audio/tick.wav", 0.6f, second == 1 ? 1.25f : 1.0f, "UI");
        if (Entity timer = Resolve(TimerText))
            GetScene().PunchUIScale(timer, 0.15f, 0.25f);
    }
    m_LastSecond = second;
    if (m_RangeLeft <= 0.0f)
        EndRange();
}

void ArenaGame::UpdateCourse(float dt)
{
    m_LapTime += dt;
    // the markers spin and bob; the next one pulses
    for (size_t i = 0; i < m_Points.size(); i++)
    {
        auto& t = m_Points[i].GetComponent<TransformComponent>();
        t.Rotation.y += dt * (i == m_Next ? 2.4f : 0.8f);
        const float pulse = i == m_Next ? 1.0f + 0.12f * std::sin(m_Time * 6.0f) : 0.75f;
        t.Scale = glm::vec3(0.7f * pulse);
    }
    const FirstPersonController* fpc = GetController();
    if (!fpc || m_Points.empty())
        return;
    const glm::vec3 target = GetScene().GetWorldPosition(m_Points[m_Next]);
    if (glm::distance(fpc->GetCenter(), target) < CheckpointRadius)
        ReachCheckpoint();
}

void ArenaGame::ReachCheckpoint()
{
    const size_t reached = m_Next;
    m_Next = (m_Next + 1) % m_Points.size();
    PlaySound("Audio/checkpoint.wav", 0.8f, 1.0f + 0.06f * (float)reached);
    if (m_DemoRun)
        ZM_INFO("Demo: {}-nuqta ({})", reached + 1, FormatTime(m_LapTime));
    if (m_Next == 0)
    {
        // a whole lap
        m_Laps++;
        const bool record = !m_Bot && m_State == State::Playing && (m_BestLap <= 0.0f || m_LapTime < m_BestLap);
        if (record)
        {
            m_BestLap = m_LapTime;
            SaveData::SetFloat("FPSArena.BestLap", m_BestLap);
            SaveData::Save();
        }
        PlaySound("Audio/lap.wav", 0.8f);
        ShowBanner(Localize("hud.lap"), record ? Localize("hud.record", FormatTime(m_LapTime)) : Localize("hud.time", FormatTime(m_LapTime)), 2.5f);
        if (m_DemoRun)
            ZM_INFO("Demo: aylana tugadi - vaqt {}", FormatTime(m_LapTime));
        m_LapTime = 0.0f;
        RefreshTexts();
    }
    StyleCheckpoints();
}

void ArenaGame::StyleCheckpoints()
{
    for (size_t i = 0; i < m_Points.size(); i++)
    {
        // the next one: bright orange; done in this lap: dim green; still to come: blue
        const bool next = i == m_Next;
        const bool done = i < m_Next;
        const glm::vec3 color = next ? glm::vec3(1.0f, 0.55f, 0.15f) : done ? glm::vec3(0.3f, 1.0f, 0.45f) : glm::vec3(0.3f, 0.75f, 1.0f);
        if (auto* mr = m_Points[i].TryGetComponent<MeshRendererComponent>())
        {
            mr->Mat.Albedo = glm::vec4(color, 1.0f);
            mr->Mat.Emission = color;
            mr->Mat.EmissionStrength = next ? 8.0f : done ? 1.5f : 3.0f;
        }
        if (auto* light = m_Points[i].TryGetComponent<PointLightComponent>())
        {
            light->Color = color;
            light->Intensity = next ? 4.0f : 1.2f;
        }
    }
}

// ------------------------------------------------------------------------------------------- HUD

std::string ArenaGame::FormatTime(float seconds)
{
    const int total = (int)(seconds * 100.0f);
    return std::format("{:02}:{:02}.{:02}", total / 6000, (total / 100) % 60, total % 100);
}

void ArenaGame::UpdateHud(float dt)
{
    const FirstPersonController* fpc = GetController();
    const bool range = m_Mode == Mode::Range;
    SetText(TimerText, FormatTime(range ? m_RangeLeft : m_LapTime));
    if (range)
    {
        SetText(ProgressText, Localize("hud.targets", m_TargetsDown));
        SetText(LapsText, Localize("hud.accuracy", GetAccuracy()));
    }
    else
    {
        SetText(ProgressText, Localize("hud.progress", m_Next + 1, (int)m_Points.size()));
        SetText(LapsText, Localize("hud.laps", m_Laps));
    }
    SetText(ScoreText, Localize("hud.score", m_Score));
    const int multiplier = 1 + std::min(3, m_Streak / 3);
    SetText(StreakText, multiplier > 1 ? Localize("hud.streak", multiplier) : std::string());
    if (fpc)
    {
        SetText(SpeedText, Localize("hud.speed", fpc->GetHorizontalSpeed()));
        SetText(StateText, Localize(fpc->GetStateKey()));
        if (Entity bar = Resolve(StaminaBar))
            if (auto* p = bar.TryGetComponent<UIProgressBarComponent>())
                p->Value = fpc->GetStamina01();
    }
    if (const Blaster* gun = GetBlaster())
    {
        SetText(AmmoText, gun->IsReloading() ? Localize("hud.reloading") : std::format("{} / {}", gun->GetAmmo(), gun->MagazineSize));
        if (Entity text = Resolve(AmmoText))
            if (auto* t = text.TryGetComponent<TextComponent>())
                t->Color = gun->GetAmmo() <= 3 && !gun->IsReloading() ? glm::vec4(1.0f, 0.45f, 0.3f, 1.0f) : glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
        SetActiveRef(ReloadBar, gun->IsReloading());
        if (Entity bar = Resolve(ReloadBar))
            if (auto* p = bar.TryGetComponent<UIProgressBarComponent>())
                p->Value = gun->GetReload01();
    }
    // the hit marker fades
    m_HitMarker = std::max(0.0f, m_HitMarker - dt * 4.0f);
    if (Entity marker = Resolve(HitMarker))
        if (auto* img = marker.TryGetComponent<UIImageComponent>())
            img->Color.a = m_HitMarker;
}

void ArenaGame::ShowBanner(const std::string& title, const std::string& subtitle, float seconds)
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

void ArenaGame::RefreshTexts()
{
    SetText(LanguageButton, Localize("menu.language", Localization::GetLanguageName(Localization::GetLanguage())));
    std::string best;
    if (m_BestLap > 0.0f)
        best = Localize("menu.best", FormatTime(m_BestLap));
    if (m_BestRange > 0)
        best += (best.empty() ? "" : "   \xc2\xb7   ") + Localize("menu.best_range", m_BestRange);
    SetText(MenuBest, best);
    if (m_Mode == Mode::Range)
        SetText(BestText, m_BestRange > 0 ? Localize("hud.best_range", m_BestRange) : std::string());
    else
        SetText(BestText, m_BestLap > 0.0f ? Localize("hud.best", FormatTime(m_BestLap)) : std::string());
    SetText(HintText, Localize(m_Mode == Mode::Range ? "hud.hint_range" : "hud.hint"));
    SetText(SensitivityText, Localize("pause.sensitivity", m_Sensitivity));
}

void ArenaGame::SetText(const EntityRef& ref, const std::string& text)
{
    if (Entity e = Resolve(ref))
        if (auto* t = e.TryGetComponent<TextComponent>(); t && t->Text != text)
            t->Text = text;
}

void ArenaGame::SetActiveRef(const EntityRef& ref, bool active)
{
    if (Entity e = Resolve(ref); e && e.IsActive() != active)
        e.SetActive(active);
}

ZM_REGISTER_SCRIPT(ArenaGame,
    ZM_FIELD(ArenaGame, Player).Header("O'yin obyektlari"),
    ZM_FIELD(ArenaGame, Gun).Tooltip("Qurol (Blaster)"),
    ZM_FIELD(ArenaGame, Checkpoints).Tooltip("Parkur nuqtalari - shu obyektning bolalari, tartib bilan"),
    ZM_FIELD(ArenaGame, JumpPad),
    ZM_FIELD(ArenaGame, Targets).Tooltip("Nishonlar guruhi"),
    ZM_FIELD(ArenaGame, Hud).Header("HUD"),
    ZM_FIELD(ArenaGame, TimerText),
    ZM_FIELD(ArenaGame, BestText),
    ZM_FIELD(ArenaGame, ProgressText),
    ZM_FIELD(ArenaGame, LapsText),
    ZM_FIELD(ArenaGame, SpeedText),
    ZM_FIELD(ArenaGame, StateText),
    ZM_FIELD(ArenaGame, StaminaBar),
    ZM_FIELD(ArenaGame, HintText),
    ZM_FIELD(ArenaGame, ScoreText),
    ZM_FIELD(ArenaGame, StreakText),
    ZM_FIELD(ArenaGame, AmmoText),
    ZM_FIELD(ArenaGame, ReloadBar),
    ZM_FIELD(ArenaGame, HitMarker),
    ZM_FIELD(ArenaGame, Popups).Tooltip("\"+ochko\" yozuvlari - shu obyektning bolalari navbat bilan ishlatiladi"),
    ZM_FIELD(ArenaGame, Banner),
    ZM_FIELD(ArenaGame, BannerSub),
    ZM_FIELD(ArenaGame, DemoBadge),
    ZM_FIELD(ArenaGame, MainMenu).Header("Menyular"),
    ZM_FIELD(ArenaGame, PauseMenu),
    ZM_FIELD(ArenaGame, MenuBest),
    ZM_FIELD(ArenaGame, PlayButton),
    ZM_FIELD(ArenaGame, LanguageButton),
    ZM_FIELD(ArenaGame, ResumeButton),
    ZM_FIELD(ArenaGame, SensitivitySlider),
    ZM_FIELD(ArenaGame, SensitivityText),
    ZM_FIELD(ArenaGame, ResultsMenu),
    ZM_FIELD(ArenaGame, ResultsTitle),
    ZM_FIELD(ArenaGame, ResultsScore),
    ZM_FIELD(ArenaGame, ResultsStats),
    ZM_FIELD(ArenaGame, ResultsRecord),
    ZM_FIELD(ArenaGame, RetryButton),
    ZM_FIELD_RANGE(ArenaGame, CheckpointRadius, 0.5f, 5.0f).Header("Qoidalar").Tooltip("Nuqtaga shu masofada yetilgan hisoblanadi (m)"),
    ZM_FIELD_RANGE(ArenaGame, RangeSeconds, 10.0f, 300.0f).Tooltip("\"Nishon otish\" raundining davomiyligi (soniya)"),
    ZM_FIELD(ArenaGame, AutoDemo).Tooltip("Demo rejimida boshlash: bot o'ynaydi (ZAMIN_DEMO=1 ham shunday qiladi, ZAMIN_DEMO=range - nishon otish)"))
