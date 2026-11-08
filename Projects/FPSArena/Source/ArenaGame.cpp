#include "Arena.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <format>

using namespace Zamin;

// Arena o'yini: parkur yo'lagi (yorqin nuqtalardan ketma-ket o'tish), aylana vaqti va rekord (SaveData
// "FPSArena.BestLap"), HUD (vaqt, nuqta, tezlik, holat, chidamlilik), menyular (bosh menyu - orqada bot yuguradi,
// pauza - sichqoncha sezgirligi slayderi bilan, SaveData "FPSArena.Sensitivity"). O'yin paytida kursor qulflanadi.
// Demo: ZAMIN_DEMO=1 (yoki AutoDemo) - bot yo'lakni aylanadi, jurnalga "Demo: ..." yoziladi.

static const glm::vec3 SpawnPoint{ 0.0f, 0.05f, -20.0f };
static constexpr float SpawnYaw = glm::pi<float>(); // facing +Z (into the arena)

void ArenaGame::OnCreate()
{
    SetUpdateWhenPaused(true);
    const char* demo = std::getenv("ZAMIN_DEMO");
    m_DemoRun = AutoDemo || (demo && *demo && std::string_view(demo) != "0");
    m_BestLap = SaveData::GetFloat("FPSArena.BestLap", 0.0f);
    m_Sensitivity = SaveData::GetFloat("FPSArena.Sensitivity", 1.0f);
    if (Entity group = Resolve(Checkpoints))
        m_Points = GetScene().GetChildren(group);
}

void ArenaGame::OnStart()
{
    AddMessageHandler("Play", [this](const ScriptMessage&) { PlaySound("Audio/ui_click.wav", 0.8f); StartGame(); });
    AddMessageHandler("Resume", [this](const ScriptMessage&) { PlaySound("Audio/ui_click.wav", 0.8f); ClosePause(); });
    AddMessageHandler("Restart", [this](const ScriptMessage&) { PlaySound("Audio/ui_click.wav", 0.8f); StartGame(); });
    AddMessageHandler("Menu", [this](const ScriptMessage&) { PlaySound("Audio/ui_click.wav", 0.8f); ShowMenu(); });
    AddMessageHandler("Quit", [this](const ScriptMessage&) { QuitGame(); });
    AddMessageHandler("Language", [this](const ScriptMessage&) {
        PlaySound("Audio/ui_click.wav", 0.8f);
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
        ZM_INFO("Demo rejimi: yo'lakni bot aylanadi (ZAMIN_DEMO)");
        StartGame();
    }
    else
        ShowMenu();
    RefreshTexts();
}

void ArenaGame::OnDestroy()
{
    if (m_DemoRun)
        if (const FirstPersonController* fpc = GetController())
            ZM_INFO("Demo yakuni: {} ta aylana, keyingi nuqta {}, {:.0f} m yurildi", m_Laps, m_Next + 1, fpc->GetDistanceTravelled());
    Cursor::SetLockMode(CursorLockMode::None);
    Cursor::SetVisible(true);
    if (m_DemoRun || m_Laps > 0)
        SaveData::Save();
}

void ArenaGame::OnLanguageChanged(const std::string&)
{
    RefreshTexts();
}

FirstPersonController* ArenaGame::GetController() const
{
    return GetScript<FirstPersonController>(Resolve(Player));
}

// ------------------------------------------------------------------------------------------- flow

void ArenaGame::StartSession(bool bot)
{
    m_Bot = bot;
    m_Next = 0;
    m_LapTime = 0.0f;
    m_Laps = 0;
    m_DemoLogTimer = 0.0f;
    if (FirstPersonController* fpc = GetController())
    {
        fpc->ResetTo(SpawnPoint, SpawnYaw, bot);
        fpc->SetControlsEnabled(true);
    }
    StyleCheckpoints();
    SetActiveRef(DemoBadge, bot && m_State != State::Menu);
    if (m_State != State::Menu)
        ShowBanner(Localize("hud.start"), Localize("hud.start_sub"), 2.2f);
    ApplyCursor();
}

void ArenaGame::ShowMenu()
{
    if (IsGamePaused())
        ResumeGame();
    Audio::SetGroupLowPass("Music", 0.0f);
    m_State = State::Menu;
    SetActiveRef(PauseMenu, false);
    SetActiveRef(Hud, false);
    SetActiveRef(MainMenu, true);
    if (Entity button = Resolve(PlayButton))
        GetScene().SetSelectedUI(button);
    StartSession(true); // the bot runs the course behind the menu
    RefreshTexts();
}

void ArenaGame::StartGame()
{
    if (IsGamePaused())
        ResumeGame();
    Audio::SetGroupLowPass("Music", 0.0f);
    m_State = State::Playing;
    SetActiveRef(MainMenu, false);
    SetActiveRef(PauseMenu, false);
    SetActiveRef(Hud, true);
    GetScene().SetSelectedUI({});
    StartSession(m_DemoRun);
    if (!m_Bot)
        ZM_INFO("FPS Arena: o'yin boshlandi");
}

void ArenaGame::OpenPause()
{
    m_State = State::Paused;
    PauseGame();
    SetActiveRef(PauseMenu, true);
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
    GetScene().SetSelectedUI({});
    Audio::SetGroupLowPass("Music", 0.0f);
    ResumeGame();
    ApplyCursor();
}

void ArenaGame::ApplyCursor()
{
    // locked and hidden while a person plays; free in the menus and while the bot plays
    const bool locked = m_State == State::Playing && !m_Bot;
    Cursor::SetLockMode(locked ? CursorLockMode::Locked : CursorLockMode::None);
    Cursor::SetVisible(!locked);
    if (FirstPersonController* fpc = GetController())
        fpc->SetControlsEnabled(m_State == State::Playing || m_Bot);
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
    UpdateCourse(dt);
    UpdateHud();
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
                ZM_INFO("Demo (holat): keyingi nuqta {}, {} ta aylana, {:.0f} m yurildi, vaqt {}", m_Next + 1, m_Laps,
                        fpc->GetDistanceTravelled(), FormatTime(m_LapTime));
        }
    }
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
        const bool record = !m_Bot && (m_BestLap <= 0.0f || m_LapTime < m_BestLap);
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

void ArenaGame::UpdateHud()
{
    const FirstPersonController* fpc = GetController();
    SetText(TimerText, FormatTime(m_LapTime));
    SetText(ProgressText, Localize("hud.progress", m_Next + 1, (int)m_Points.size()));
    SetText(LapsText, Localize("hud.laps", m_Laps));
    if (fpc)
    {
        SetText(SpeedText, Localize("hud.speed", fpc->GetHorizontalSpeed()));
        SetText(StateText, Localize(fpc->GetStateKey()));
        if (Entity bar = Resolve(StaminaBar))
            if (auto* p = bar.TryGetComponent<UIProgressBarComponent>())
                p->Value = fpc->GetStamina01();
    }
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
    SetText(MenuBest, m_BestLap > 0.0f ? Localize("menu.best", FormatTime(m_BestLap)) : std::string());
    SetText(BestText, m_BestLap > 0.0f ? Localize("hud.best", FormatTime(m_BestLap)) : std::string());
    SetText(HintText, Localize("hud.hint"));
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
    ZM_FIELD(ArenaGame, Checkpoints).Tooltip("Parkur nuqtalari - shu obyektning bolalari, tartib bilan"),
    ZM_FIELD(ArenaGame, JumpPad),
    ZM_FIELD(ArenaGame, Hud).Header("HUD"),
    ZM_FIELD(ArenaGame, TimerText),
    ZM_FIELD(ArenaGame, BestText),
    ZM_FIELD(ArenaGame, ProgressText),
    ZM_FIELD(ArenaGame, LapsText),
    ZM_FIELD(ArenaGame, SpeedText),
    ZM_FIELD(ArenaGame, StateText),
    ZM_FIELD(ArenaGame, StaminaBar),
    ZM_FIELD(ArenaGame, HintText),
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
    ZM_FIELD_RANGE(ArenaGame, CheckpointRadius, 0.5f, 5.0f).Header("Qoidalar").Tooltip("Nuqtaga shu masofada yetilgan hisoblanadi (m)"),
    ZM_FIELD(ArenaGame, AutoDemo).Tooltip("Demo rejimida boshlash: bot o'ynaydi (ZAMIN_DEMO=1 ham shunday qiladi)"))
