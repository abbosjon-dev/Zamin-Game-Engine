#include "Race.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <format>

using namespace Zamin;

// Poyga: to'rt mashina (o'yinchi va uch raqib - botlar) start chizig'i orqasidan chiqadi. Start svetofori: beshta
// chiroq birin-ketin yonadi, so'ng hammasi o'chadi - GO! Trassa CheckpointCount bo'lakka bo'lingan: nuqtalardan
// tartib bilan o'tib start chizig'ini kesib o'tilsa - aylana. Laps aylanadan keyin marra; o'rinlar trassa bo'ylab
// bosib o'tilgan masofa bo'yicha. Rekord: eng yaxshi aylana (SaveData "Racing.BestLap"). Mashinalar bir-biriga
// urilsa itarishadi. Bosh menyu orqasida to'rt bot poyga qiladi. Demo: ZAMIN_DEMO=1 (yoki AutoDemo) - o'yinchi
// mashinasini bot haydaydi, jurnalga "Demo: ..." yoziladi; marradan keyin yangi poyga.

void RaceGame::OnCreate()
{
    s_Instance = this;
    SetUpdateWhenPaused(true);
    const char* demo = std::getenv("ZAMIN_DEMO");
    m_DemoRun = AutoDemo || (demo && *demo && std::string_view(demo) != "0");
    m_BestLap = SaveData::GetFloat("Racing.BestLap", 0.0f);
    if (!m_Track.Parse(TrackPoints))
        ZM_ERROR("Vodiy Poygasi: trassa nuqtalari o'qilmadi (TrackPoints)");
}

void RaceGame::OnStart()
{
    auto click = [this] { PlaySound("Audio/ui_click.wav", 0.8f, 1.0f, "UI"); };
    AddMessageHandler("Play", [this, click](const ScriptMessage&) { click(); StartRace(false); });
    AddMessageHandler("Resume", [this, click](const ScriptMessage&) { click(); ClosePause(); });
    AddMessageHandler("Restart", [this, click](const ScriptMessage&) { click(); StartRace(false); });
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
        ZM_INFO("Demo rejimi: o'yinchi mashinasini bot haydaydi (ZAMIN_DEMO), trassa {:.0f} m, {} aylana", m_Track.Length, Laps);
        StartRace(true);
    }
    else
        ShowMenu();
    RefreshTexts();
}

void RaceGame::OnDestroy()
{
    if (m_DemoRun)
        if (Car* car = GetScript<Car>(Resolve(PlayerCar)))
            ZM_INFO("Demo yakuni: {} ta poyga, {}-aylana, o'rin {}/4, eng yaxshi aylana {}", m_Races, car->Lap + 1, PlaceOf(*car),
                    car->BestLap > 0.0f ? FormatTime(car->BestLap) : std::string("-"));
    SaveData::Save();
    if (s_Instance == this)
        s_Instance = nullptr;
}

void RaceGame::OnLanguageChanged(const std::string&)
{
    RefreshTexts();
}

std::vector<Car*> RaceGame::GetCars() const
{
    std::vector<Car*> cars;
    for (const EntityRef* ref : { &PlayerCar, &Rival1, &Rival2, &Rival3 })
        if (Car* car = GetScript<Car>(Resolve(*ref)))
            cars.push_back(car);
    return cars;
}

// ------------------------------------------------------------------------------------------- flow

void RaceGame::StartRace(bool bot)
{
    if (IsGamePaused())
        ResumeGame();
    Audio::SetGroupLowPass("Music", 0.0f);
    m_Bot = bot || m_DemoRun;
    if (m_State != State::Menu || !bot || m_DemoRun)
    {
        m_State = State::Countdown;
        SetActiveRef(MainMenu, false);
        SetActiveRef(Hud, true);
    }
    SetActiveRef(PauseMenu, false);
    SetActiveRef(ResultsMenu, false);
    GetScene().SetSelectedUI({});
    m_Countdown = m_State == State::Menu ? 1.5f : 3.6f;
    m_LastCount = -1;
    m_FinishTimer = 0.0f;
    m_FinishedCount = 0;
    m_Races++;
    // the grid: two rows behind the start line, staggered
    const glm::vec2 t = m_Track.Tangent(0.0f);
    const glm::vec2 n(-t.y, t.x);
    const float yaw = std::atan2(-t.x, -t.y);
    std::vector<Car*> cars = GetCars();
    for (size_t i = 0; i < cars.size(); i++)
    {
        const float back = 9.0f + 7.0f * (float)(i / 2);
        const float side = (i % 2 == 0 ? -2.6f : 2.6f);
        cars[i]->ResetCar(m_Track.At(-back) + n * side, yaw);
        const bool player = cars[i]->GetEntity() == Resolve(PlayerCar);
        cars[i]->SetDriver(player && !m_Bot && m_State != State::Menu ? Car::Driver::Player : Car::Driver::Bot);
        cars[i]->Freeze(true);
    }
    m_CamYaw = yaw;
    StyleCheckpoints();
    SetActiveRef(DemoBadge, m_Bot && m_State != State::Menu);
    if (!m_Bot)
        ZM_INFO("Vodiy Poygasi: poyga boshlandi ({} aylana)", Laps);
    RefreshTexts();
}

void RaceGame::ShowMenu()
{
    if (IsGamePaused())
        ResumeGame();
    m_State = State::Menu;
    SetActiveRef(Hud, false);
    SetActiveRef(PauseMenu, false);
    SetActiveRef(ResultsMenu, false);
    SetActiveRef(MainMenu, true);
    if (Entity button = Resolve(PlayButton))
        GetScene().SetSelectedUI(button);
    StartRace(true); // the bots race behind the menu
    RefreshTexts();
}

void RaceGame::OpenPause()
{
    m_State = State::Paused;
    PauseGame();
    SetActiveRef(PauseMenu, true);
    if (Entity button = Resolve(ResumeButton))
        GetScene().SetSelectedUI(button);
    Audio::SetGroupLowPass("Music", 800.0f);
}

void RaceGame::ClosePause()
{
    if (m_State != State::Paused)
        return;
    m_State = State::Racing;
    SetActiveRef(PauseMenu, false);
    GetScene().SetSelectedUI({});
    Audio::SetGroupLowPass("Music", 0.0f);
    ResumeGame();
}

void RaceGame::FinishRace()
{
    Car* car = GetScript<Car>(Resolve(PlayerCar));
    if (!car)
        return;
    m_State = State::Finished;
    m_FinishTimer = 0.0f;
    car->SetDriver(Car::Driver::Bot); // a cool-down lap by itself
    const bool record = !m_Bot && car->BestLap > 0.0f && (m_BestLap <= 0.0f || car->BestLap < m_BestLap);
    if (record)
    {
        m_BestLap = car->BestLap;
        SaveData::SetFloat("Racing.BestLap", m_BestLap);
        SaveData::Save();
    }
    PlaySound("Audio/finish.wav", 0.9f);
    SetText(ResultsTitle, Localize(car->FinishPlace == 1 ? "results.win" : "results.title"));
    SetText(ResultsPlace, Localize("results.place", car->FinishPlace));
    SetText(ResultsTimes, Localize("results.times", FormatTime(car->RaceTime), FormatTime(car->BestLap)));
    SetText(ResultsRecord, record ? Localize("results.record") : m_BestLap > 0.0f ? Localize("results.best", FormatTime(m_BestLap)) : std::string());
    SetActiveRef(ResultsMenu, true);
    if (Entity button = Resolve(RetryButton))
        GetScene().SetSelectedUI(button);
    if (m_DemoRun)
        ZM_INFO("Demo: poyga tugadi - o'rin {}/4, vaqt {}, eng yaxshi aylana {}", car->FinishPlace, FormatTime(car->RaceTime), FormatTime(car->BestLap));
    RefreshTexts();
}

// ---------------------------------------------------------------------------------------- update

void RaceGame::OnUpdate(Timestep ts)
{
    const float dt = ts;
    const float realDt = GetUnscaledDeltaTime();
    m_Time += realDt;
    if (Input::IsActionJustPressed("Pause"))
    {
        if (m_State == State::Racing && !m_DemoRun)
            OpenPause();
        else if (m_State == State::Paused)
            ClosePause();
    }
    if (m_State == State::Paused)
        return;
    if (m_State != State::Menu && !m_Bot && Input::IsActionJustPressed("Reset"))
        if (Car* car = GetScript<Car>(Resolve(PlayerCar)))
            car->Respawn();

    // the start lights: one more every 0.6 s, then all out - go
    if (m_State == State::Countdown || (m_State == State::Menu && m_Countdown > 0.0f))
    {
        m_Countdown -= dt;
        const int count = (int)std::ceil(m_Countdown / 0.6f);
        if (m_State == State::Countdown && count != m_LastCount && count >= 1 && count <= 5)
        {
            PlaySound("Audio/beep.wav", 0.6f, 1.0f, "UI");
            if (count <= 3)
                ShowBanner(std::to_string(count), "", 0.55f);
        }
        m_LastCount = count;
        if (m_Countdown <= 0.0f)
        {
            for (Car* car : GetCars())
                car->Freeze(false);
            if (m_State == State::Countdown)
            {
                m_State = State::Racing;
                PlaySound("Audio/go.wav", 0.8f, 1.0f, "UI");
                ShowBanner(Localize("hud.go"), Localize("hud.go_sub", Laps), 1.2f);
            }
        }
    }
    UpdateRace(dt);
    UpdateLights();
    if (m_State == State::Finished)
    {
        m_FinishTimer += realDt;
        if (m_DemoRun && m_FinishTimer > 5.0f)
            StartRace(true);
    }
    UpdateCamera(dt);
    UpdateHud();
    if (m_BannerTimer > 0.0f)
    {
        m_BannerTimer -= realDt;
        if (m_BannerTimer <= 0.0f)
            if (Entity banner = Resolve(Banner))
                GetScene().HideUI(banner);
    }
}

void RaceGame::UpdateRace(float dt)
{
    std::vector<Car*> cars = GetCars();
    if (cars.empty() || dt <= 0.0f)
        return;
    // physics in small steps; the cars push each other apart after each
    const int steps = std::clamp((int)std::ceil(dt * 120.0f), 1, 32);
    const float h = dt / (float)steps;
    for (int i = 0; i < steps; i++)
    {
        for (Car* car : cars)
            car->Simulate(h, dt, i == 0);
        SeparateCars(cars);
    }
    const bool running = m_State == State::Racing || m_State == State::Finished || (m_State == State::Menu && m_Countdown <= 0.0f);
    if (!running)
        return;
    for (Car* car : cars)
    {
        if (!car->Finished)
        {
            car->LapTime += dt;
            car->RaceTime += dt;
        }
        UpdateProgress(*car);
    }
    // behind the menu: once someone wins, a new race
    if (m_State == State::Menu && m_FinishedCount > 0)
    {
        m_FinishTimer += dt;
        if (m_FinishTimer > 4.0f)
            StartRace(true);
    }
}

void RaceGame::UpdateProgress(Car& car)
{
    if (car.Finished || m_Track.Length <= 0.0f)
        return;
    const float s = car.GetTrackDistance();
    const float cp = m_Track.Length / (float)std::max(1, CheckpointCount);
    const bool player = car.GetEntity() == Resolve(PlayerCar);
    if (car.NextCheckpoint == 0)
    {
        // crossing the start line for the first time (from behind it)
        if (s < cp * 0.5f)
            car.NextCheckpoint = 1;
        return;
    }
    if (car.NextCheckpoint < CheckpointCount)
    {
        const float at = cp * (float)car.NextCheckpoint;
        if (s >= at && s < at + cp * 0.5f)
        {
            car.NextCheckpoint++;
            if (player && m_State == State::Racing)
            {
                PlaySound("Audio/checkpoint.wav", 0.5f, 1.0f + 0.05f * (float)car.NextCheckpoint, "UI");
                StyleCheckpoints();
            }
        }
        return;
    }
    // every checkpoint passed: the start line again is a lap
    if (s < cp * 0.5f)
    {
        car.Lap++;
        car.NextCheckpoint = 1;
        if (car.BestLap <= 0.0f || car.LapTime < car.BestLap)
            car.BestLap = car.LapTime;
        if (player && m_DemoRun)
            ZM_INFO("Demo: {}-aylana - vaqt {}, o'rin {}/4", car.Lap, FormatTime(car.LapTime), PlaceOf(car));
        if (player && m_State == State::Racing)
        {
            PlaySound("Audio/lap.wav", 0.8f, 1.0f, "UI");
            if (car.Lap == Laps - 1)
                ShowBanner(Localize("hud.last_lap"), Localize("hud.lap_time", FormatTime(car.LapTime)), 2.0f);
            else if (car.Lap < Laps)
                ShowBanner(Localize("hud.lap_n", car.Lap + 1, Laps), Localize("hud.lap_time", FormatTime(car.LapTime)), 2.0f);
            StyleCheckpoints();
        }
        car.LapTime = 0.0f;
        if (car.Lap >= Laps)
        {
            car.Finished = true;
            car.FinishPlace = ++m_FinishedCount;
            m_FinishTimer = 0.0f;
            if (player && m_State == State::Racing)
                FinishRace();
        }
    }
}

void RaceGame::SeparateCars(const std::vector<Car*>& cars)
{
    // circles of 1.25 m: pushed apart, the closing speed shared out (equal masses, a little bounce)
    constexpr float radius = 1.25f;
    for (size_t i = 0; i < cars.size(); i++)
        for (size_t j = i + 1; j < cars.size(); j++)
        {
            glm::vec2 pa = cars[i]->GetPosition(), pb = cars[j]->GetPosition();
            const glm::vec2 d = pb - pa;
            const float dist = glm::length(d);
            if (dist >= radius * 2.0f || dist < 1e-4f)
                continue;
            const glm::vec2 n = d / dist;
            const float push = (radius * 2.0f - dist) * 0.5f;
            pa -= n * push;
            pb += n * push;
            glm::vec2 va = cars[i]->GetVelocity(), vb = cars[j]->GetVelocity();
            const float closing = glm::dot(va - vb, n);
            if (closing > 0.0f)
            {
                const float impulse = closing * 0.6f;
                va -= n * impulse;
                vb += n * impulse;
            }
            cars[i]->SetPositionVelocity(pa, va);
            cars[j]->SetPositionVelocity(pb, vb);
        }
}

float RaceGame::RaceDistance(const Car& car) const
{
    float s = car.GetTrackDistance();
    if (car.NextCheckpoint == 0 && s > m_Track.Length * 0.5f)
        s -= m_Track.Length; // still behind the start line
    return (float)car.Lap * m_Track.Length + s;
}

int RaceGame::PlaceOf(const Car& car) const
{
    if (car.Finished)
        return car.FinishPlace;
    int place = 1;
    const float mine = RaceDistance(car);
    for (const Car* other : GetCars())
        if (other != &car && (other->Finished || RaceDistance(*other) > mine))
            place++;
    return place;
}

void RaceGame::OnCarHitWall(Car& car, const glm::vec2& at, float impact)
{
    if (Entity sparks = Resolve(Sparks))
    {
        sparks.GetComponent<TransformComponent>().Translation = { at.x, 0.5f, at.y };
        GetScene().EmitParticles(sparks, (int)std::min(60.0f, impact * 5.0f));
    }
    const bool player = car.GetEntity() == Resolve(PlayerCar);
    PlaySoundAt("Audio/hit.wav", { at.x, 0.5f, at.y }, std::min(1.0f, impact / 12.0f), 1.0f);
    if (player)
        ShakeCamera(std::min(0.8f, impact * 0.05f), 0.35f);
}

// ------------------------------------------------------------------------------------ presentation

void RaceGame::UpdateCamera(float dt)
{
    Entity cam = Resolve(CameraRig);
    std::vector<Car*> cars = GetCars();
    if (!cam || cars.empty())
        return;
    // the player's car; behind the menu: the leader
    Car* follow = GetScript<Car>(Resolve(PlayerCar));
    if (m_State == State::Menu)
        for (Car* c : cars)
            if (PlaceOf(*c) == 1)
                follow = c;
    if (!follow)
        return;
    const glm::vec2 p = follow->GetPosition();
    // the camera trails the direction of travel (smoothly), a little to the side in a drift
    const glm::vec2 v = follow->GetVelocity();
    const float travelYaw = glm::length(v) > 3.0f ? std::atan2(-v.x, -v.y) : follow->GetYaw();
    m_CamYaw += Math::DeltaAngle(m_CamYaw, travelYaw) * std::min(1.0f, 3.5f * dt);
    const glm::vec2 back(std::sin(m_CamYaw), std::cos(m_CamYaw));
    const float speed01 = std::clamp(std::abs(follow->GetSpeed()) / 44.0f, 0.0f, 1.0f);
    const float distance = m_State == State::Menu ? 9.5f : 7.6f + speed01 * 1.2f;
    const glm::vec3 goal(p.x + back.x * distance, 3.1f + speed01 * 0.4f, p.y + back.y * distance);
    m_CamPos = Math::Damp(m_CamPos, goal, 9.0f, dt);
    const glm::vec3 look(p.x - back.x * 5.0f, 1.0f, p.y - back.y * 5.0f);
    const glm::vec3 d = glm::normalize(look - m_CamPos);
    auto& t = cam.GetComponent<TransformComponent>();
    t.Translation = m_CamPos;
    t.Rotation = { std::asin(std::clamp(d.y, -1.0f, 1.0f)), std::atan2(-d.x, -d.z), 0.0f };
    m_Fov = Math::Damp(m_Fov, 58.0f + 14.0f * speed01, 3.0f, dt);
    if (auto* c = cam.TryGetComponent<CameraComponent>())
        c->Camera.SetPerspectiveVerticalFOV(glm::radians(m_Fov));
}

void RaceGame::UpdateLights()
{
    Entity group = Resolve(StartLights);
    if (!group)
        return;
    std::vector<Entity> lights = GetScene().GetChildren(group);
    // red ones come on one by one; at go: green for a moment, then out
    const bool counting = m_State == State::Countdown;
    const int on = counting ? std::clamp(6 - (int)std::ceil(m_Countdown / 0.6f), 0, 5) : 0;
    const bool green = !counting && m_State == State::Racing && GetScript<Car>(Resolve(PlayerCar)) &&
                       GetScript<Car>(Resolve(PlayerCar))->RaceTime < 1.5f;
    for (size_t i = 0; i < lights.size(); i++)
        if (auto* mr = lights[i].TryGetComponent<MeshRendererComponent>())
        {
            const bool lit = green || (int)i < on;
            const glm::vec3 color = green ? glm::vec3(0.2f, 1.0f, 0.3f) : glm::vec3(1.0f, 0.12f, 0.08f);
            mr->Mat.Emission = color;
            mr->Mat.EmissionStrength = lit ? 14.0f : 0.0f;
            mr->Mat.Albedo = lit ? glm::vec4(color, 1.0f) : glm::vec4(0.08f, 0.08f, 0.09f, 1.0f);
        }
}

void RaceGame::StyleCheckpoints()
{
    Entity group = Resolve(Checkpoints);
    const Car* car = GetScript<Car>(Resolve(PlayerCar));
    if (!group || !car)
        return;
    std::vector<Entity> gates = GetScene().GetChildren(group);
    for (size_t i = 0; i < gates.size(); i++)
    {
        // the next one glows bright
        const bool next = (int)i + 1 == car->NextCheckpoint && m_State != State::Menu;
        for (Entity post : GetScene().GetChildren(gates[i]))
            if (auto* mr = post.TryGetComponent<MeshRendererComponent>())
                mr->Mat.EmissionStrength = next ? 9.0f : 1.2f;
    }
}

std::string RaceGame::FormatTime(float seconds)
{
    const int total = (int)(seconds * 100.0f);
    return std::format("{:02}:{:02}.{:02}", total / 6000, (total / 100) % 60, total % 100);
}

void RaceGame::UpdateHud()
{
    Car* car = GetScript<Car>(Resolve(PlayerCar));
    if (!car)
        return;
    SetText(PositionText, std::format("{}/4", PlaceOf(*car)));
    SetText(LapText, Localize("hud.lap", std::min(car->Lap + 1, Laps), Laps));
    SetText(TimerText, FormatTime(car->LapTime));
    SetText(CheckpointText, Localize("hud.checkpoint", std::max(1, car->NextCheckpoint), CheckpointCount));
    const float kmh = std::abs(car->GetSpeed()) * 3.6f;
    SetText(SpeedText, std::to_string((int)std::lround(kmh)));
    if (Entity bar = Resolve(SpeedBar))
        if (auto* p = bar.TryGetComponent<UIProgressBarComponent>())
            p->Value = std::clamp(kmh / 170.0f, 0.0f, 1.0f);
    SetText(DriftText, car->GetDrift() > 4.0f && std::abs(car->GetSpeed()) > 10.0f ? Localize("hud.drift") : std::string());
}

void RaceGame::ShowBanner(const std::string& title, const std::string& subtitle, float seconds)
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

void RaceGame::RefreshTexts()
{
    SetText(LanguageButton, Localize("menu.language", Localization::GetLanguageName(Localization::GetLanguage())));
    SetText(MenuBest, m_BestLap > 0.0f ? Localize("menu.best", FormatTime(m_BestLap)) : std::string());
    SetText(BestText, m_BestLap > 0.0f ? Localize("hud.best", FormatTime(m_BestLap)) : std::string());
}

void RaceGame::SetText(const EntityRef& ref, const std::string& text)
{
    if (Entity e = Resolve(ref))
        if (auto* t = e.TryGetComponent<TextComponent>(); t && t->Text != text)
            t->Text = text;
}

void RaceGame::SetActiveRef(const EntityRef& ref, bool active)
{
    if (Entity e = Resolve(ref); e && e.IsActive() != active)
        e.SetActive(active);
}

ZM_REGISTER_SCRIPT(RaceGame,
    ZM_FIELD(RaceGame, PlayerCar).Header("Mashinalar"),
    ZM_FIELD(RaceGame, Rival1),
    ZM_FIELD(RaceGame, Rival2),
    ZM_FIELD(RaceGame, Rival3),
    ZM_FIELD(RaceGame, CameraRig).Header("Trassa"),
    ZM_FIELD(RaceGame, StartLights).Tooltip("Start svetoforining chiroqlari (bolalari)"),
    ZM_FIELD(RaceGame, Checkpoints).Tooltip("Nazorat nuqtalari ustunlari (bolalari, tartib bilan)"),
    ZM_FIELD(RaceGame, Sparks),
    ZM_FIELD(RaceGame, TrackPoints).Tooltip("Trassa markaziy chizig'i: \"x,z;x,z;...\" start chizig'idan boshlab"),
    ZM_FIELD(RaceGame, Hud).Header("HUD"),
    ZM_FIELD(RaceGame, PositionText),
    ZM_FIELD(RaceGame, LapText),
    ZM_FIELD(RaceGame, TimerText),
    ZM_FIELD(RaceGame, BestText),
    ZM_FIELD(RaceGame, CheckpointText),
    ZM_FIELD(RaceGame, SpeedText),
    ZM_FIELD(RaceGame, SpeedBar),
    ZM_FIELD(RaceGame, DriftText),
    ZM_FIELD(RaceGame, Banner),
    ZM_FIELD(RaceGame, BannerSub),
    ZM_FIELD(RaceGame, DemoBadge),
    ZM_FIELD(RaceGame, MainMenu).Header("Menyular"),
    ZM_FIELD(RaceGame, MenuBest),
    ZM_FIELD(RaceGame, PlayButton),
    ZM_FIELD(RaceGame, LanguageButton),
    ZM_FIELD(RaceGame, PauseMenu),
    ZM_FIELD(RaceGame, ResumeButton),
    ZM_FIELD(RaceGame, ResultsMenu),
    ZM_FIELD(RaceGame, ResultsTitle),
    ZM_FIELD(RaceGame, ResultsPlace),
    ZM_FIELD(RaceGame, ResultsTimes),
    ZM_FIELD(RaceGame, ResultsRecord),
    ZM_FIELD(RaceGame, RetryButton),
    ZM_FIELD_RANGE(RaceGame, Laps, 1, 20).Header("Qoidalar"),
    ZM_FIELD_RANGE(RaceGame, CheckpointCount, 2, 32),
    ZM_FIELD(RaceGame, AutoDemo).Tooltip("O'yinchi mashinasini bot haydaydi (ZAMIN_DEMO=1 ham shunday qiladi)"))
