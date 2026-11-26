#include <Zamin/Zamin.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <string>
#include <utility>
#include <vector>

using namespace Zamin;

// The scene flow sample (M15): the main menu (Menu.zscene: GameMenu, GameMenu.cpp) -> the loading screen (Loading.zscene,
// the level loads in the background) -> level 1 (with a fade through black). GameSession is the game session
// manager: it is created in the menu and kept across scene changes with DontDestroyOnLoad - the music does not
// break off, the coins collected in every level add up in the global state (GameState), the per-level statistics
// are in the SessionStats service (Services), and the HUD in the bottom left corner shows only in the
// levels.

// The loading screen: loads the next scene in the background with LoadSceneAsync and shows a progress bar and
// the stage. Even when the scene is ready it stays at least MinSeconds (SetAllowSceneActivation(false)), then
// the screen fades, the scene changes and the new level opens.
class LoadingScreen : public ScriptableEntity
{
public:
    std::string Target = "Scenes/Level1.zscene";
    float MinSeconds = 1.6f;
    float Fade = 0.45f;
    EntityRef Bar, Percent, Stage;

    void OnStart() override
    {
        m_Load = LoadSceneAsync(Target, LoadSceneMode::Single, SceneTransition::Fade(Fade));
        if (m_Load)
            m_Load->SetAllowSceneActivation(false); // the screen decides when (see OnUpdate)
    }

    void OnUpdate(Timestep) override
    {
        const float dt = GetUnscaledDeltaTime();
        m_Time += dt;
        if (!m_Load || m_Load->HasFailed())
        {
            SetText(Stage, m_Load ? Localize("loading.error", m_Load->GetError()) : Localize("loading.not_found", Target));
            return;
        }
        // The bar follows the real progress (0.9 = loaded), never faster than the minimum time.
        const float loaded = std::min(1.0f, m_Load->GetProgress() / 0.9f);
        const float allowed = MinSeconds > 0.0f ? std::min(1.0f, m_Time / MinSeconds) : 1.0f;
        const float goal = std::min(loaded, allowed);
        m_Shown += (goal - m_Shown) * (1.0f - std::exp(-10.0f * dt));
        if (goal >= 1.0f && m_Shown > 0.995f)
            m_Shown = 1.0f;
        if (Entity bar = Resolve(Bar))
            if (auto* progress = bar.TryGetComponent<UIProgressBarComponent>())
                progress->Value = m_Shown;
        SetText(Percent, std::format("{:.0f}%", m_Shown * 100.0f));
        SetText(Stage, StageText());
        if (m_Load->IsReady() && m_Shown >= 1.0f && !m_Load->GetAllowSceneActivation())
        {
            ZM_INFO("Loading screen: '{}' ready ({:.1f} s) - switching", Target, m_Time);
            m_Load->SetAllowSceneActivation(true); // the fade out starts, the switch happens behind it
        }
    }

private:
    std::string StageText() const
    {
        static const char* dots[] = { "", ".", "..", "..." };
        const char* d = dots[(int)(m_Time * 3.0f) % 4];
        switch (m_Load->GetStage())
        {
            // The stage in the player's language (Assets/Localization/<language>.json, "loading.*").
            case AsyncOperation::Stage::Reading:   return Localize("loading.reading", d);
            case AsyncOperation::Stage::Decoding:  return Localize("loading.decoding", m_Load->GetDecodedImageCount(), m_Load->GetImageCount(), d);
            case AsyncOperation::Stage::Building:  return Localize("loading.building", d);
            case AsyncOperation::Stage::Resolving: return Localize("loading.resolving", d);
            case AsyncOperation::Stage::Ready:     return m_Shown >= 1.0f ? Localize("loading.ready") : Localize("loading.preparing", d);
            default:                               return Localize("loading.ready");
        }
    }

    void SetText(const EntityRef& ref, const std::string& text)
    {
        if (Entity e = Resolve(ref))
            if (auto* t = e.TryGetComponent<TextComponent>())
                t->Text = text;
    }

    Ref<AsyncOperation> m_Load;
    float m_Time = 0.0f, m_Shown = 0.0f;
};
ZM_REGISTER_SCRIPT(LoadingScreen,
    ZM_FIELD_TOOLTIP(LoadingScreen, Target, "The scene loaded in the background").Header("Loading"),
    ZM_FIELD_RANGE(LoadingScreen, MinSeconds, 0.0f, 10.0f).Tooltip("The screen stays at least this long (seconds)"),
    ZM_FIELD_RANGE(LoadingScreen, Fade, 0.0f, 2.0f).Tooltip("Fade out / in time (seconds)"),
    ZM_FIELD(LoadingScreen, Bar).Header("UI").Tooltip("UI Progress Bar"),
    ZM_FIELD(LoadingScreen, Percent).Tooltip("Percent text"),
    ZM_FIELD(LoadingScreen, Stage).Tooltip("Stage text"))

// The game session statistics - a C++ service (Services): it lives outside the scenes and any script finds it
// with GetService<SessionStats>(). The coins collected in each level.
struct SessionStats
{
    std::vector<std::pair<std::string, int>> Levels; // the level name, its coins
    void AddCoin(const std::string& level)
    {
        if (Levels.empty() || Levels.back().first != level)
            Levels.emplace_back(level, 0);
        Levels.back().second++;
    }
};

// The game session: DontDestroyOnLoad - it lives in every scene from the menu on. Its music
// (AudioSource, PlayOnStart + Loop) does not break off when the scene changes; it writes the "CoinCollected"
// events to the global state (GameState: "Session.Coins", "Session.Levels", "Session.Level"), and the record
// ("Session.Best") is a persistent key - it stays for the next game through SaveData.
class GameSession : public ScriptableEntity
{
public:
    EntityRef Hud;   // the session text (UI), shown only in the levels

    void OnCreate() override
    {
        // Already one from an earlier visit to the menu: this copy is not needed.
        if (SceneManager* scenes = GetSceneManager())
            if (const Ref<Scene>& persistent = scenes->GetDontDestroyOnLoadScene(); persistent && persistent->FindEntityByName(GetName()))
            {
                Destroy();
                return;
            }
        DontDestroyOnLoad(); // moves (with its HUD) at the end of this frame
        // The global state of a new session (the record comes from the last game's SaveData).
        GameState::SetPersistent("Session.Best");
        GameState::Load();
        GameState::SetInt("Session.Coins", 0);
        GameState::SetInt("Session.Levels", 0);
        GetOrCreateService<SessionStats>() = {};
    }

    void OnEnable() override
    {
        m_Subscription = Subscribe("CoinCollected", [](const ScriptMessage&) {
            GameState::Increment("Session.Coins");
            GetOrCreateService<SessionStats>().AddCoin(GameState::GetString("Session.Level"));
        });
        // The HUD follows the state, whoever changes it (also the editor's Scenes window).
        m_Watch = WatchGameState("", [this](std::string_view key) {
            if (key.starts_with("Session."))
                Refresh();
        });
    }

    void OnDisable() override
    {
        Unsubscribe(m_Subscription);
        UnwatchGameState(m_Watch);
        m_Subscription = m_Watch = 0;
    }

    // The language changed (the menu's L, the pause menu's language button): the HUD in the new one.
    void OnLanguageChanged(const std::string&) override { Refresh(); }

    // Scene event: another scene became the active one (the menu, the loading screen, a level).
    // A level is counted and named; the HUD is only shown in levels; the record is saved.
    void OnActiveSceneChanged(const SceneEventInfo& /*previous*/, const SceneEventInfo& current) override
    {
        const bool level = current.Name.starts_with("Level");
        if (level)
        {
            GameState::Increment("Session.Levels");
            GameState::SetString("Session.Level", current.Name);
        }
        const int coins = GameState::GetInt("Session.Coins");
        if (coins > GameState::GetInt("Session.Best"))
        {
            GameState::SetInt("Session.Best", coins);
            GameState::Save(); // the persistent keys to SaveData
        }
        Refresh();
        if (Entity hud = Resolve(Hud); hud && hud.IsActive() != level)
            SetActive(hud, level);
    }

private:
    void Refresh()
    {
        if (Entity hud = Resolve(Hud))
            if (auto* text = hud.TryGetComponent<TextComponent>())
            {
                // "Session: 5 coins | Level1 | 2 levels | best: 12" - the counts with their plural forms
                // ("5 монет", "2 уровня"), from the language files.
                const std::string level = GameState::GetString("Session.Level");
                text->Text = Localize("hud.session", LocalizePlural("hud.session_coins", GameState::GetInt("Session.Coins")),
                                      level.empty() ? std::string("-") : level, LocalizePlural("hud.session_levels", GameState::GetInt("Session.Levels")),
                                      GameState::GetInt("Session.Best"));
            }
    }

    SubscriptionID m_Subscription = 0;
    GameState::ListenerID m_Watch = 0;
};
ZM_REGISTER_SCRIPT(GameSession, ZM_FIELD(GameSession, Hud).Tooltip("Session text (UI): shown only in the levels"))

// The sub-scene sample (SubScenes.zscene): one "Camp" piece (Scenes/Parts/Camp.zscene) in three places.
// Two of them load with the scene (SubSceneComponent, "Load with the scene"), this script loads the third
// with LoadSubScene() after Delay seconds - for example a room that opens when the player comes near.
// Label (optional) shows the state.
class DelayedSubScene : public ScriptableEntity
{
public:
    float Delay = 1.5f;
    EntityRef Label;

    void OnUpdate(Timestep ts) override
    {
        if (m_Requested)
        {
            if (!m_Shown && GetSubScene())
            {
                m_Shown = true;
                SetLabel(std::format("'{}' loaded ({} objects)", GetSubScene()->GetName(), GetSubScene()->GetEntitiesInOrder().size()));
            }
            return;
        }
        m_Time += ts;
        SetLabel(std::format("The third camp loads in {:.1f} s", std::max(0.0f, Delay - m_Time)));
        if (m_Time >= Delay)
            m_Requested = LoadSubScene(); // at the end of this frame, placed by this entity
    }

private:
    void SetLabel(const std::string& text)
    {
        if (Entity label = Resolve(Label))
            if (auto* t = label.TryGetComponent<TextComponent>())
                t->Text = text;
    }

    float m_Time = 0.0f;
    bool m_Requested = false;
    bool m_Shown = false;
};
ZM_REGISTER_SCRIPT(DelayedSubScene, ZM_FIELD_RANGE(DelayedSubScene, Delay, 0.0f, 10.0f).Tooltip("Wait until the sub-scene is loaded (seconds)"),
                   ZM_FIELD(DelayedSubScene, Label).Tooltip("Status text (a Text component)"))
