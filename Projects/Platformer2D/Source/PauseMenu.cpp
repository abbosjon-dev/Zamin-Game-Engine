#include <Zamin/Zamin.h>

#include <cmath>
#include <format>

using namespace Zamin;

// Pauza menyusi (1-3 darajalar): Esc yoki gamepad Start o'yinni to'xtatadi va menyuni ochadi -
// "Davom etish", "Sozlamalar", "Chiqish". O'yin vaqti to'xtaydi (PauseGame: fizika, animatsiyalar,
// boshqa skriptlar), UI esa ishlayveradi - bu skript SetUpdateWhenPaused bilan pauzada ham ishlaydi.
// Sozlamalar: ovoz guruhlari (Master, Music, SFX, UI) slayderlari audio mikserga ulangan, to'liq ekran
// va VSync belgilashlari, til tugmasi (o'zbek / rus / ingliz: menyu matnlari lokalizatsiya kalitlari bilan,
// til o'zgarganda o'zi yangilanadi); hammasi SaveData'da saqlanadi (Audio::SaveSettings, Display::SaveSettings) va
// keyingi safar o'yin boshlanganda qaytadi. Sozlamalar ro'yxati scroll view ichida (g'ildirak, sudrash,
// klaviatura bilan tanlanganda o'zi aylanadi). Klaviatura / gamepad: strelkalar / D-pad tanlaydi,
// Enter / A bosadi, Esc / B orqaga qaytadi (sozlamalardan menyuga, menyudan o'yinga).
class PauseMenu : public ScriptableEntity
{
public:
    EntityRef Overlay;          // the dimmed screen holding the panels
    EntityRef MainPanel;
    EntityRef SettingsPanel;
    EntityRef ResumeButton;     // selected when the menu opens
    EntityRef SettingsButton;   // selected again when the settings close
    EntityRef SettingsScroll;   // back to the top when the settings open
    EntityRef MasterSlider, MusicSlider, SfxSlider, UiSlider;
    EntityRef MasterValue, MusicValue, SfxValue, UiValue;
    EntityRef FullscreenToggle, VSyncToggle;
    EntityRef LanguageButton;   // "Til: O'zbekcha": the next language on each click
    float MusicMuffle = 900.0f; // Hz: the music's low-pass while paused (0 = off)

    void OnCreate() override
    {
        SetUpdateWhenPaused(true); // it opens and closes the menu while the game is stopped
    }

    void OnStart() override
    {
        if (Entity overlay = Resolve(Overlay))
            overlay.SetActive(false);
        AddMessageHandler("Resume", [this](const ScriptMessage&) { Close(); });
        AddMessageHandler("Settings", [this](const ScriptMessage&) { OpenSettings(); });
        AddMessageHandler("Back", [this](const ScriptMessage&) { CloseSettings(); });
        AddMessageHandler("Quit", [this](const ScriptMessage&) {
            Close();
            QuitGame(); // the player closes; the editor stops Play
        });
        AddMessageHandler("ResetSettings", [this](const ScriptMessage&) { ResetSettings(); });
        // The language button: uz -> ru -> en ... Every text with a localization key follows at once; the
        // choice is kept for the next game (Localization::SaveSettings, loaded by the player).
        AddMessageHandler("NextLanguage", [this](const ScriptMessage&) {
            if (Localization::NextLanguage())
                Localization::SaveSettings();
            RefreshLanguage();
        });
        RefreshLanguage();
        Subscribe("UISliderChanged", [this](const ScriptMessage& m) {
            if (const char* group = GroupOf(m.Sender))
            {
                Audio::SetGroupVolume(group, m.Get<float>());
                m_Changed = true;
                RefreshLabels();
            }
        });
        Subscribe("UIToggleChanged", [this](const ScriptMessage& m) {
            if (m.Sender == Resolve(FullscreenToggle))
                Display::SetFullscreen(m.Get<bool>());
            else if (m.Sender == Resolve(VSyncToggle))
                Display::SetVSync(m.Get<bool>());
            else
                return;
            m_Changed = true;
        });
    }

    void OnUpdate(Timestep) override
    {
        // Esc / Start: open; while open Esc / B goes back one step.
        bool back = Input::IsActionJustPressed("Pause");
        for (int id = 0; id < Input::MaxGamepads; id++)
            if (IsOpen() && Input::IsGamepadConnected(id) && Input::IsGamepadButtonJustPressed(GamepadButton::B, id))
                back = true;
        if (!back)
            return;
        if (!IsOpen())
            Open();
        else if (IsSettingsOpen())
            CloseSettings();
        else
            Close();
    }

    void OnDisable() override
    {
        if (IsOpen())
            Close(); // the game is not left paused
    }

    void OnLanguageChanged(const std::string&) override { RefreshLanguage(); }

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

    void Open()
    {
        PauseGame();
        if (Entity panel = Resolve(SettingsPanel))
            panel.SetActive(false);
        if (Entity panel = Resolve(MainPanel))
            panel.SetActive(true);
        if (Entity overlay = Resolve(Overlay))
            overlay.SetActive(true); // its show animation plays
        if (Entity button = Resolve(ResumeButton))
            GetScene().SetSelectedUI(button); // keys / gamepad start at "Davom etish"
        m_MusicFilter = Audio::GetGroupFilter("Music");
        if (MusicMuffle > 0.0f)
            Audio::SetGroupLowPass("Music", MusicMuffle);
        ZM_INFO("Pauza");
    }

    void Close()
    {
        SaveIfChanged();
        if (Entity overlay = Resolve(Overlay))
            overlay.SetActive(false);
        GetScene().SetSelectedUI({});
        Audio::SetGroupFilter("Music", m_MusicFilter);
        ResumeGame();
    }

private:
    void OpenSettings()
    {
        SyncFromSettings();
        if (Entity panel = Resolve(MainPanel))
            panel.SetActive(false);
        if (Entity panel = Resolve(SettingsPanel))
            panel.SetActive(true); // the first slider is selected (SelectOnEnable)
        if (Entity scroll = Resolve(SettingsScroll))
            GetScene().SetScrollPosition(scroll, { 0.0f, 1.0f }, false);
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

    void ResetSettings()
    {
        Audio::ClearSavedSettings(); // the project's mixer again
        Display::SetVSync(true);
        Display::SetFullscreen(false);
        Display::ClearSavedSettings();
        SyncFromSettings();
        m_Changed = false;
        ZM_INFO("Sozlamalar standart holatga qaytdi");
    }

    // The widgets show the mixer and the display as they are now.
    void SyncFromSettings()
    {
        const std::pair<EntityRef*, const char*> sliders[] = { { &MasterSlider, "Master" }, { &MusicSlider, "Music" }, { &SfxSlider, "SFX" },
                                                               { &UiSlider, "UI" } };
        for (const auto& [ref, group] : sliders)
            if (Entity slider = Resolve(*ref))
                GetScene().SetSliderValue(slider, Audio::GetGroupVolume(group), false);
        if (Entity toggle = Resolve(FullscreenToggle))
            GetScene().SetToggle(toggle, Display::IsFullscreen(), false);
        if (Entity toggle = Resolve(VSyncToggle))
            GetScene().SetToggle(toggle, Display::IsVSync(), false);
        RefreshLabels();
    }

    // "Til: O'zbekcha" / "Язык: Русский" / "Language: English".
    void RefreshLanguage()
    {
        if (Entity button = Resolve(LanguageButton))
            if (auto* text = button.TryGetComponent<TextComponent>())
                text->Text = Localize("settings.language", Localization::GetLanguageName(Localization::GetLanguage()));
    }

    void RefreshLabels()
    {
        const std::pair<EntityRef*, EntityRef*> rows[] = { { &MasterSlider, &MasterValue }, { &MusicSlider, &MusicValue }, { &SfxSlider, &SfxValue },
                                                           { &UiSlider, &UiValue } };
        for (const auto& [sliderRef, labelRef] : rows)
        {
            Entity slider = Resolve(*sliderRef), label = Resolve(*labelRef);
            if (slider && label && slider.HasComponent<UISliderComponent>() && label.HasComponent<TextComponent>())
                label.GetComponent<TextComponent>().Text = std::format("{}%", (int)std::round(slider.GetComponent<UISliderComponent>().Value * 100.0f));
        }
    }

    const char* GroupOf(Entity slider) const
    {
        if (!slider) return nullptr;
        if (slider == Resolve(MasterSlider)) return "Master";
        if (slider == Resolve(MusicSlider)) return "Music";
        if (slider == Resolve(SfxSlider)) return "SFX";
        if (slider == Resolve(UiSlider)) return "UI";
        return nullptr;
    }

    void SaveIfChanged()
    {
        if (!m_Changed)
            return;
        Audio::SaveSettings();
        Display::SaveSettings();
        m_Changed = false;
        ZM_INFO("Sozlamalar saqlandi");
    }

    bool m_Changed = false;
    AudioFilterSettings m_MusicFilter;
};

ZM_REGISTER_SCRIPT(PauseMenu,
    ZM_FIELD(PauseMenu, Overlay).Header("Panellar"),
    ZM_FIELD(PauseMenu, MainPanel),
    ZM_FIELD(PauseMenu, SettingsPanel),
    ZM_FIELD(PauseMenu, ResumeButton),
    ZM_FIELD(PauseMenu, SettingsButton),
    ZM_FIELD(PauseMenu, SettingsScroll),
    ZM_FIELD(PauseMenu, MasterSlider).Header("Ovoz"),
    ZM_FIELD(PauseMenu, MusicSlider),
    ZM_FIELD(PauseMenu, SfxSlider),
    ZM_FIELD(PauseMenu, UiSlider),
    ZM_FIELD(PauseMenu, MasterValue),
    ZM_FIELD(PauseMenu, MusicValue),
    ZM_FIELD(PauseMenu, SfxValue),
    ZM_FIELD(PauseMenu, UiValue),
    ZM_FIELD(PauseMenu, FullscreenToggle).Header("Ekran"),
    ZM_FIELD(PauseMenu, VSyncToggle),
    ZM_FIELD(PauseMenu, LanguageButton).Header("Til").Tooltip("Bosilganda keyingi til (Localization::NextLanguage); matni joriy til nomi"),
    ZM_FIELD_RANGE(PauseMenu, MusicMuffle, 0.0f, 20000.0f).Label("Musiqa bo'g'iqligi (Hz)").Tooltip("Pauzada musiqaga low-pass filtr (0 - o'chiq)"))
