#include <Zamin/Zamin.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <string>
#include <vector>

using namespace Zamin;

// O'yin menyusi (qayta ishlatiladigan namuna: Prefabs/GameMenu.zprefab - istalgan sahnaga sudrab qo'ying).
// Ikki rejim:
//   - bosh menyu (PauseMenu o'chiq, Menu.zscene): doim ko'rinadi - "O'ynash" PlayScene'ga so'nib o'tadi,
//     "Sozlamalar", "Chiqish";
//   - pauza menyusi (PauseMenu yoqiq): Esc / Start o'yinni to'xtatib ochadi - "Davom etish", "Sozlamalar", "Chiqish".
// Sozlamalar: ekran o'lchami (Display::GetResolutions - monitor rejimlari), to'liq ekran, VSync, grafika sifati
// (Quality darajalari), gamma, ovoz (Master / Music / SFX), til. O'lcham, sifat va til tugmalari bosilganda
// keyingisiga o'tadi, tanlangan bo'lsa chap / o'ng strelka (D-pad) oldingi / keyingisini tanlaydi. Hammasi
// SaveData'da saqlanadi (Display / Audio / Quality / Localization::SaveSettings) - player keyingi safar shu
// sozlamalar bilan ochiladi. Sozlamalar panelining burchagida o'yin va dvigatel versiyasi (VersionInfo).
// Klaviatura / gamepad: strelkalar / D-pad tanlaydi, Enter / A bosadi, Esc / B orqaga qaytadi.
class GameMenu : public ScriptableEntity
{
public:
    bool PauseMenu = false;                        // false: the main menu; true: Esc opens it over the game
    std::string PlayScene = "Scenes/Loading.zscene"; // the main menu's "O'ynash"
    float FadeOut = 0.35f, FadeIn = 0.35f;
    EntityRef Overlay, MainPanel, SettingsPanel;
    EntityRef Title;                               // the game's name (Project settings > Player)
    EntityRef PlayButton, SettingsButton, QuitButton;
    EntityRef SettingsScroll;
    EntityRef ResolutionButton, FullscreenToggle, VSyncToggle, QualityButton;
    EntityRef GammaSlider, GammaValue;
    EntityRef MasterSlider, MusicSlider, SfxSlider, MasterValue, MusicValue, SfxValue;
    EntityRef LanguageButton;
    EntityRef VersionText;                         // the settings panel's corner: the game's and the engine's versions
    EntityRef ScreenVersionText;                   // the menu's corner (main menu)

    void OnCreate() override
    {
        SetUpdateWhenPaused(true); // a pause menu works while the game stands still
    }

    void OnStart() override
    {
        AddMessageHandler("Play", [this](const ScriptMessage&) { Play(); });
        AddMessageHandler("Settings", [this](const ScriptMessage&) { OpenSettings(); });
        AddMessageHandler("Back", [this](const ScriptMessage&) { CloseSettings(); });
        AddMessageHandler("Quit", [this](const ScriptMessage&) {
            SaveIfChanged();
            if (PauseMenu)
                Close();
            QuitGame(); // the player closes; the editor stops Play
        });
        AddMessageHandler("ResetSettings", [this](const ScriptMessage&) { ResetSettings(); });
        AddMessageHandler("NextResolution", [this](const ScriptMessage&) { StepResolution(1); });
        AddMessageHandler("NextQuality", [this](const ScriptMessage&) { StepQuality(1); });
        AddMessageHandler("NextLanguage", [this](const ScriptMessage&) { StepLanguage(1); });
        Subscribe("UISliderChanged", [this](const ScriptMessage& m) {
            if (const char* group = GroupOf(m.Sender))
                Audio::SetGroupVolume(group, m.Get<float>());
            else if (m.Sender == Resolve(GammaSlider))
                Display::SetGamma(m.Get<float>());
            else
                return;
            m_Changed = true;
            RefreshLabels();
        });
        Subscribe("UIToggleChanged", [this](const ScriptMessage& m) {
            if (m.Sender == Resolve(FullscreenToggle))
                Display::SetFullscreen(m.Get<bool>());
            else if (m.Sender == Resolve(VSyncToggle))
                Display::SetVSync(m.Get<bool>());
            else
                return;
            m_Changed = true;
            RefreshLabels(); // (fullscreen: the window's size shown changes)
        });

        // The game's name and its version with the engine's (the commit it was built from).
        const VersionInfo version = VersionInfo::ForActiveProject();
        SetText(Title, Application::GetProductName());
        SetText(VersionText, version.FormatShort());
        SetText(ScreenVersionText, std::format("{} v{}", Application::GetProductName(), Application::GetVersion()));
        if (Entity panel = Resolve(SettingsPanel))
            panel.SetActive(false);
        if (PauseMenu)
        {
            // "O'ynash" goes back to the game here: "Davom etish".
            if (Entity play = Resolve(PlayButton))
                if (auto* text = play.TryGetComponent<TextComponent>())
                {
                    text->LocalizationKey = "pause.resume";
                    text->Text = Localize("pause.resume");
                }
            if (Entity overlay = Resolve(Overlay))
                overlay.SetActive(false);
        }
        else
            ShowMain();
        RefreshLabels();
    }

    void OnUpdate(Timestep) override
    {
        // Left / right on a choice button: the previous / next choice (its click is "next").
        if (IsSettingsOpen())
        {
            int step = 0;
            if (Input::IsKeyJustPressed(Key::Left) || Input::IsKeyJustPressed(Key::A))
                step = -1;
            if (Input::IsKeyJustPressed(Key::Right) || Input::IsKeyJustPressed(Key::D))
                step = 1;
            for (int id = 0; id < Input::MaxGamepads; id++)
                if (Input::IsGamepadConnected(id))
                {
                    if (Input::IsGamepadButtonJustPressed(GamepadButton::DPadLeft, id))
                        step = -1;
                    if (Input::IsGamepadButtonJustPressed(GamepadButton::DPadRight, id))
                        step = 1;
                }
            if (step != 0)
            {
                const Entity selected = GetScene().GetSelectedUI();
                if (selected && selected == Resolve(ResolutionButton))
                    StepResolution(step);
                else if (selected && selected == Resolve(QualityButton))
                    StepQuality(step);
                else if (selected && selected == Resolve(LanguageButton))
                    StepLanguage(step);
            }
        }
        // Esc / Start / B: back one step (the settings -> the menu; a pause menu: open / close).
        bool back = Input::IsActionJustPressed("Pause") || Input::IsKeyJustPressed(Key::Escape);
        for (int id = 0; id < Input::MaxGamepads; id++)
            if (IsOpen() && Input::IsGamepadConnected(id) && Input::IsGamepadButtonJustPressed(GamepadButton::B, id))
                back = true;
        if (!back)
            return;
        if (IsSettingsOpen())
            CloseSettings();
        else if (PauseMenu)
        {
            if (IsOpen())
                Close();
            else
                Open();
        }
    }

    void OnDisable() override
    {
        if (PauseMenu && IsOpen())
            Close(); // the game is not left paused
    }

    // Texts that are not keyed (the choices' values) follow the language.
    void OnLanguageChanged(const std::string&) override { RefreshLabels(); }

    bool IsOpen() const
    {
        Entity overlay = Resolve(Overlay);
        return overlay && overlay.IsActive();
    }

    bool IsSettingsOpen() const
    {
        Entity panel = Resolve(SettingsPanel);
        return IsOpen() && panel && panel.IsActive();
    }

    // A pause menu: the game stops, the menu opens with "Davom etish" selected.
    void Open()
    {
        PauseGame();
        ShowMain();
        ZM_INFO("O'yin menyusi: pauza");
    }

    void Close()
    {
        SaveIfChanged();
        if (Entity overlay = Resolve(Overlay))
            overlay.SetActive(false);
        GetScene().SetSelectedUI({});
        ResumeGame();
    }

private:
    void ShowMain()
    {
        if (Entity panel = Resolve(SettingsPanel))
            panel.SetActive(false);
        if (Entity panel = Resolve(MainPanel))
            panel.SetActive(true);
        if (Entity overlay = Resolve(Overlay))
            overlay.SetActive(true);
        if (Entity button = Resolve(PlayButton))
            GetScene().SetSelectedUI(button);
    }

    // "O'ynash": the main menu goes to the game (a fade); a pause menu goes back to it.
    void Play()
    {
        if (PauseMenu)
        {
            Close();
            return;
        }
        if (m_Started)
            return;
        m_Started = true;
        SaveIfChanged();
        ZM_INFO("O'yin menyusi: '{}' ochilmoqda", PlayScene);
        LoadScene(PlayScene, SceneTransition::Fade(FadeOut, FadeIn));
    }

    void OpenSettings()
    {
        SyncFromSettings();
        if (Entity panel = Resolve(MainPanel))
            panel.SetActive(false);
        if (Entity panel = Resolve(SettingsPanel))
            panel.SetActive(true);
        if (Entity scroll = Resolve(SettingsScroll))
            GetScene().SetScrollPosition(scroll, { 0.0f, 1.0f }, false);
        if (Entity button = Resolve(ResolutionButton))
            GetScene().SetSelectedUI(button); // keys / gamepad start at the first setting
    }

    void CloseSettings()
    {
        SaveIfChanged();
        if (Entity panel = Resolve(SettingsPanel))
            panel.SetActive(false);
        if (Entity panel = Resolve(MainPanel))
            panel.SetActive(true);
        if (Entity button = Resolve(SettingsButton))
            GetScene().SetSelectedUI(button);
    }

    void StepResolution(int step)
    {
        const std::vector<ScreenResolution> list = Display::GetResolutions();
        if (list.empty())
            return;
        const ScreenResolution current = Display::GetResolution();
        // The next one from the current size (a size between two of the list: the nearest one up / down).
        int index = -1;
        for (int i = 0; i < (int)list.size(); i++)
            if (list[i] == current)
                index = i;
        if (index < 0)
            index = step > 0 ? -1 : (int)list.size();
        index = ((index + step) % (int)list.size() + (int)list.size()) % (int)list.size();
        if (Display::SetResolution(list[(size_t)index]))
        {
            m_Changed = true;
            ZM_INFO("Ekran o'lchami: {}x{}", list[(size_t)index].Width, list[(size_t)index].Height);
        }
        RefreshLabels();
    }

    void StepQuality(int step)
    {
        const int count = Quality::GetLevelCount();
        if (count <= 0)
            return;
        Quality::SetLevel(((Quality::GetLevel() + step) % count + count) % count);
        m_Changed = true;
        RefreshLabels();
    }

    void StepLanguage(int step)
    {
        const std::vector<std::string> languages = Localization::GetLanguages();
        if (languages.size() < 2)
            return;
        int index = 0;
        for (int i = 0; i < (int)languages.size(); i++)
            if (languages[(size_t)i] == Localization::GetLanguage())
                index = i;
        index = ((index + step) % (int)languages.size() + (int)languages.size()) % (int)languages.size();
        if (Localization::SetLanguage(languages[(size_t)index]))
            Localization::SaveSettings(); // (every keyed text follows at once; OnLanguageChanged the rest)
        RefreshLabels();
    }

    void ResetSettings()
    {
        Audio::ClearSavedSettings(); // the project's mixer again
        // The display as the next start opens it: the project's window mode (fullscreen unless windowed), vsync and
        // window size; gamma and brightness 1.
        Display::ResetOptions(Project::GetPlayerSettings());
        Display::SetResolution(Project::GetPlayerSettings().Width, Project::GetPlayerSettings().Height);
        Display::ClearSavedSettings();
        Quality::ClearSavedSettings();
        Quality::SetLevel(Project::GetActive() ? Project::GetActive()->GetConfig().Quality.DefaultLevel : Quality::GetLevel());
        SyncFromSettings();
        m_Changed = false;
        ZM_INFO("Sozlamalar standart holatga qaytdi");
    }

    // The widgets show the mixer, the display and the quality as they are now.
    void SyncFromSettings()
    {
        const std::pair<EntityRef*, const char*> sliders[] = { { &MasterSlider, "Master" }, { &MusicSlider, "Music" }, { &SfxSlider, "SFX" } };
        for (const auto& [ref, group] : sliders)
            if (Entity slider = Resolve(*ref))
                GetScene().SetSliderValue(slider, Audio::GetGroupVolume(group), false);
        if (Entity toggle = Resolve(FullscreenToggle))
            GetScene().SetToggle(toggle, Display::IsFullscreen(), false);
        if (Entity toggle = Resolve(VSyncToggle))
            GetScene().SetToggle(toggle, Display::IsVSync(), false);
        if (Entity slider = Resolve(GammaSlider))
            GetScene().SetSliderValue(slider, Display::GetGamma(), false);
        RefreshLabels();
    }

    void RefreshLabels()
    {
        const ScreenResolution r = Display::GetResolution();
        SetText(ResolutionButton, Localize("settings.resolution", r.IsValid() ? std::format("{}x{}", r.Width, r.Height) : std::string("-")));
        SetText(QualityButton, Localize("settings.quality", Quality::GetLevelName()));
        SetText(LanguageButton, Localize("settings.language", Localization::GetLanguageName(Localization::GetLanguage())));
        const std::pair<EntityRef*, EntityRef*> rows[] = { { &MasterSlider, &MasterValue }, { &MusicSlider, &MusicValue }, { &SfxSlider, &SfxValue } };
        for (const auto& [sliderRef, labelRef] : rows)
            if (Entity slider = Resolve(*sliderRef); slider && slider.HasComponent<UISliderComponent>())
                SetText(*labelRef, std::format("{}%", (int)std::round(slider.GetComponent<UISliderComponent>().Value * 100.0f)));
        SetText(GammaValue, std::format("{:.2f}", Display::GetGamma()));
    }

    const char* GroupOf(Entity slider) const
    {
        if (!slider) return nullptr;
        if (slider == Resolve(MasterSlider)) return "Master";
        if (slider == Resolve(MusicSlider)) return "Music";
        if (slider == Resolve(SfxSlider)) return "SFX";
        return nullptr;
    }

    void SetText(const EntityRef& ref, const std::string& text)
    {
        if (Entity e = Resolve(ref))
            if (auto* t = e.TryGetComponent<TextComponent>())
                t->Text = text;
    }

    void SaveIfChanged()
    {
        if (!m_Changed)
            return;
        Audio::SaveSettings();
        Display::SaveSettings();
        Quality::SaveSettings();
        m_Changed = false;
        ZM_INFO("Sozlamalar saqlandi");
    }

    bool m_Changed = false, m_Started = false;
};

ZM_REGISTER_SCRIPT(GameMenu,
    ZM_FIELD_TOOLTIP(GameMenu, PauseMenu, "O'chiq: bosh menyu (doim ko'rinadi). Yoqiq: Esc / Start o'yinni to'xtatib ochadi").Header("Rejim"),
    ZM_FIELD_TOOLTIP(GameMenu, PlayScene, "Bosh menyuda \"O'ynash\" ochadigan sahna (yo'l yoki build sahna nomi)"),
    ZM_FIELD_RANGE(GameMenu, FadeOut, 0.0f, 2.0f).Tooltip("Ekran qorayishi (sekund)"),
    ZM_FIELD_RANGE(GameMenu, FadeIn, 0.0f, 2.0f).Tooltip("Yangi sahnada ochilishi (sekund)"),
    ZM_FIELD(GameMenu, Overlay).Header("Panellar"),
    ZM_FIELD(GameMenu, MainPanel),
    ZM_FIELD(GameMenu, SettingsPanel),
    ZM_FIELD(GameMenu, Title).Tooltip("O'yin nomi yoziladigan matn"),
    ZM_FIELD(GameMenu, PlayButton).Tooltip("Menyu ochilganda tanlanadi"),
    ZM_FIELD(GameMenu, SettingsButton),
    ZM_FIELD(GameMenu, QuitButton),
    ZM_FIELD(GameMenu, SettingsScroll),
    ZM_FIELD(GameMenu, ResolutionButton).Header("Ekran").Tooltip("Bosilganda keyingi o'lcham (Display::GetResolutions); chap / o'ng - oldingi / keyingi"),
    ZM_FIELD(GameMenu, FullscreenToggle),
    ZM_FIELD(GameMenu, VSyncToggle),
    ZM_FIELD(GameMenu, QualityButton).Tooltip("Bosilganda keyingi sifat darajasi (Project settings > Quality)"),
    ZM_FIELD(GameMenu, GammaSlider).Tooltip("Display::SetGamma (0.5 ... 2.0)"),
    ZM_FIELD(GameMenu, GammaValue),
    ZM_FIELD(GameMenu, MasterSlider).Header("Ovoz"),
    ZM_FIELD(GameMenu, MusicSlider),
    ZM_FIELD(GameMenu, SfxSlider),
    ZM_FIELD(GameMenu, MasterValue),
    ZM_FIELD(GameMenu, MusicValue),
    ZM_FIELD(GameMenu, SfxValue),
    ZM_FIELD(GameMenu, LanguageButton).Header("Til").Tooltip("Bosilganda keyingi til; matni joriy til nomi"),
    ZM_FIELD(GameMenu, VersionText).Header("Versiya").Tooltip("Sozlamalar burchagi: o'yin va dvigatel versiyasi (VersionInfo::FormatShort)"),
    ZM_FIELD(GameMenu, ScreenVersionText).Tooltip("Ekran burchagi: o'yin nomi va versiyasi"))
