#include <Zamin/Zamin.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <string>
#include <utility>
#include <vector>

using namespace Zamin;

// Sahnalar oqimi namunasi (M15): bosh menyu (Menu.zscene) -> yuklanish ekrani (Loading.zscene, daraja
// fonda yuklanadi) -> 1-daraja (qora rangga so'nib-ochilish bilan). GameSession - o'yin sessiyasi
// menejeri: menyuda yaratiladi va DontDestroyOnLoad bilan sahnalar almashganda ham saqlanadi - musiqa
// uzilmaydi, hamma darajalarda yig'ilgan tangalar global holatda (GameState) jamlanadi, darajalar
// bo'yicha statistika SessionStats servisida (Services), pastki chap burchakdagi HUD faqat
// darajalarda ko'rinadi.

// Bosh menyu: "Boshlash" tugmasi (yoki Space / Enter) yuklanish ekraniga so'nib o'tadi.
class MainMenu : public ScriptableEntity
{
public:
    std::string Target = "Scenes/Loading.zscene";
    float FadeOut = 0.35f;
    float FadeIn = 0.35f;
    EntityRef Title;     // tebranib turadigan sarlavha

    void OnStart() override
    {
        // The button's ClickMessage "Start" comes here (its ClickTarget is this entity).
        AddMessageHandler("Start", [this](const ScriptMessage&) { Begin(); });
        if (Entity title = Resolve(Title))
            m_TitleY = title.GetComponent<UITransformComponent>().Position.y;
    }

    void OnUpdate(Timestep) override
    {
        m_Time += GetUnscaledDeltaTime();
        if (Entity title = Resolve(Title))
            title.GetComponent<UITransformComponent>().Position.y = m_TitleY + std::sin(m_Time * 2.0f) * 6.0f;
        if (Input::IsActionJustPressed("Jump") || Input::IsKeyJustPressed(Key::Enter))
            Begin();
    }

private:
    void Begin()
    {
        if (m_Started)
            return;
        m_Started = true;
        ZM_INFO("Bosh menyu: yuklanish ekraniga o'tilmoqda");
        LoadScene(Target, SceneTransition::Fade(FadeOut, FadeIn));
    }

    float m_Time = 0.0f, m_TitleY = 0.0f;
    bool m_Started = false;
};
ZM_REGISTER_SCRIPT(MainMenu,
    ZM_FIELD_TOOLTIP(MainMenu, Target, "Boshlash bosilganda ochiladigan sahna (yo'l yoki build ro'yxatidagi nomi)").Header("O'tish"),
    ZM_FIELD_RANGE(MainMenu, FadeOut, 0.0f, 2.0f).Tooltip("Ekran qorayishi (sekund)"),
    ZM_FIELD_RANGE(MainMenu, FadeIn, 0.0f, 2.0f).Tooltip("Yangi sahnada ochilishi (sekund)"),
    ZM_FIELD(MainMenu, Title).Tooltip("Tebranib turadigan sarlavha (UI)"))

// Yuklanish ekrani: keyingi sahnani LoadSceneAsync bilan fonda yuklaydi, progress bar va bosqichni
// ko'rsatadi. Sahna tayyor bo'lsa ham kamida MinSeconds turadi (SetAllowSceneActivation(false)), keyin
// ekran so'nadi, sahna almashadi va yangi daraja ochiladi.
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
            SetText(Stage, m_Load ? "Xato: " + m_Load->GetError() : std::format("Sahna topilmadi: {}", Target));
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
            ZM_INFO("Yuklanish ekrani: '{}' tayyor ({:.1f} s) - o'tilmoqda", Target, m_Time);
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
            case AsyncOperation::Stage::Reading:   return std::format("Sahna fayli o'qilmoqda{}", d);
            case AsyncOperation::Stage::Decoding:  return std::format("Rasmlar ochilmoqda ({} / {}){}", m_Load->GetDecodedImageCount(),
                                                                      m_Load->GetImageCount(), d);
            case AsyncOperation::Stage::Building:  return std::format("Obyektlar yaratilmoqda{}", d);
            case AsyncOperation::Stage::Resolving: return std::format("Grafika yuklanmoqda{}", d);
            case AsyncOperation::Stage::Ready:     return m_Shown >= 1.0f ? "Tayyor!" : std::format("Tayyorlanmoqda{}", d);
            default:                               return "Tayyor!";
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
    ZM_FIELD_TOOLTIP(LoadingScreen, Target, "Fonda yuklanadigan sahna").Header("Yuklash"),
    ZM_FIELD_RANGE(LoadingScreen, MinSeconds, 0.0f, 10.0f).Tooltip("Ekran kamida shuncha turadi (sekund)"),
    ZM_FIELD_RANGE(LoadingScreen, Fade, 0.0f, 2.0f).Tooltip("So'nish / ochilish vaqti (sekund)"),
    ZM_FIELD(LoadingScreen, Bar).Header("UI").Tooltip("UI Progress Bar"),
    ZM_FIELD(LoadingScreen, Percent).Tooltip("Foiz matni"),
    ZM_FIELD(LoadingScreen, Stage).Tooltip("Bosqich matni"))

// O'yin sessiyasi statistikasi - C++ servis (Services): sahnalardan tashqarida yashaydi, istalgan
// skriptdan GetService<SessionStats>() bilan topiladi. Har darajada yig'ilgan tangalar.
struct SessionStats
{
    std::vector<std::pair<std::string, int>> Levels; // daraja nomi, undagi tangalar
    void AddCoin(const std::string& level)
    {
        if (Levels.empty() || Levels.back().first != level)
            Levels.emplace_back(level, 0);
        Levels.back().second++;
    }
};

// O'yin sessiyasi: DontDestroyOnLoad - menyudan boshlab hamma sahnalarda yashaydi. Musiqasi
// (AudioSource, PlayOnStart + Loop) sahna almashganda uzilmaydi; "CoinCollected" hodisalarini
// global holatga (GameState: "Session.Coins", "Session.Levels", "Session.Level") yozadi, rekord
// ("Session.Best") doimiy kalit - SaveData orqali keyingi o'yinga qoladi.
class GameSession : public ScriptableEntity
{
public:
    EntityRef Hud;   // sessiya matni (UI), faqat darajalarda ko'rinadi

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
        m_Subscription = Subscribe("CoinCollected", [this](const ScriptMessage&) {
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
                const std::string level = GameState::GetString("Session.Level");
                text->Text = std::format("Sessiya: {} tanga  |  {}  |  {} ta daraja  |  rekord: {}", GameState::GetInt("Session.Coins"),
                                         level.empty() ? "-" : level, GameState::GetInt("Session.Levels"), GameState::GetInt("Session.Best"));
            }
    }

    SubscriptionID m_Subscription = 0;
    GameState::ListenerID m_Watch = 0;
};
ZM_REGISTER_SCRIPT(GameSession, ZM_FIELD(GameSession, Hud).Tooltip("Sessiya matni (UI): faqat darajalarda ko'rinadi"))

// Sub-sahnalar namunasi (SubScenes.zscene): bitta "Lager" bo'lagi (Scenes/Parts/Camp.zscene) uch joyda.
// Ikkitasi sahna bilan yuklanadi (SubSceneComponent, "Sahna bilan yuklash"), uchinchisini shu skript
// Delay sekunddan keyin LoadSubScene() bilan yuklaydi - masalan, o'yinchi yaqinlashganda ochiladigan
// xona. Label (ixtiyoriy) holatni ko'rsatadi.
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
                SetLabel(std::format("'{}' yuklandi ({} obyekt)", GetSubScene()->GetName(), GetSubScene()->GetEntitiesInOrder().size()));
            }
            return;
        }
        m_Time += ts;
        SetLabel(std::format("Uchinchi lager {:.1f} s dan keyin yuklanadi", std::max(0.0f, Delay - m_Time)));
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
ZM_REGISTER_SCRIPT(DelayedSubScene, ZM_FIELD_RANGE(DelayedSubScene, Delay, 0.0f, 10.0f).Tooltip("Sub-sahna yuklanguncha kutish (sekund)"),
                   ZM_FIELD(DelayedSubScene, Label).Tooltip("Holat matni (Text komponenti)"))
