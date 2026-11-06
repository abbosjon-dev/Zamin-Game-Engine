#include <Zamin/Zamin.h>

#include <cmath>
#include <format>

using namespace Zamin;

// UIWidgets sahnasi: ekran UI vidjetlarining namunasi - slayderlar, belgilashlar (guruh bilan),
// progress bar, radial to'ldirish (qayta tiklanish), matn maydoni va tugma. Vidjetlar o'zgarganda
// event bus xabarlari ("UISliderChanged", "UIToggleChanged", "UITextSubmitted") shu skriptga keladi.
class UIWidgetsDemo : public ScriptableEntity
{
public:
    EntityRef MusicSlider;
    EntityRef MusicValue;
    EntityRef EffectsSlider;
    EntityRef EffectsValue;
    EntityRef Loading;
    EntityRef Cooldown;
    EntityRef NameField;
    EntityRef SaveButton;
    EntityRef Status;
    float LoadingSeconds = 4.0f;
    float CooldownSeconds = 3.0f;

    void OnStart() override
    {
        // The name field is ready for typing at once (a click elsewhere ends editing).
        if (Entity field = Resolve(NameField))
            GetScene().FocusInputField(field);
        UpdateValueLabels();

        Subscribe("UISliderChanged", [this](const ScriptMessage& m) {
            UpdateValueLabels();
            if (m.Sender == Resolve(MusicSlider))
                Audio::SetGroupVolume("Music", m.Get<float>());
        });
        Subscribe("UIToggleChanged", [this](const ScriptMessage& m) {
            if (m.Get<bool>())
                SetStatus(std::format("Tanlandi: {}", m.Sender.GetName()));
        });
        Subscribe("UITextSubmitted", [this](const ScriptMessage& m) { Greet(m.Get<std::string>()); });
    }

    void OnUpdate(Timestep ts) override
    {
        m_Time += ts;
        if (Entity bar = Resolve(Loading))
            if (auto* p = bar.TryGetComponent<UIProgressBarComponent>())
                p->Value = std::fmod(m_Time, LoadingSeconds + 1.0f) / LoadingSeconds; // fills, waits a second, starts over
        if (Entity icon = Resolve(Cooldown))
            if (auto* image = icon.TryGetComponent<UIImageComponent>())
                image->FillAmount = std::min(1.0f, std::fmod(m_Time, CooldownSeconds + 0.5f) / CooldownSeconds);
        Entity button = Resolve(SaveButton), field = Resolve(NameField);
        if (button && field && button.HasComponent<UIButtonComponent>() && field.HasComponent<UIInputFieldComponent>() &&
            button.GetComponent<UIButtonComponent>().Clicked)
            Greet(field.GetComponent<UIInputFieldComponent>().Text);
    }

private:
    void UpdateValueLabels()
    {
        Entity music = Resolve(MusicSlider), musicLabel = Resolve(MusicValue);
        if (music && musicLabel && music.HasComponent<UISliderComponent>() && musicLabel.HasComponent<TextComponent>())
            musicLabel.GetComponent<TextComponent>().Text = std::format("{}%", (int)std::round(music.GetComponent<UISliderComponent>().Value * 100.0f));
        Entity effects = Resolve(EffectsSlider), effectsLabel = Resolve(EffectsValue);
        if (effects && effectsLabel && effects.HasComponent<UISliderComponent>() && effectsLabel.HasComponent<TextComponent>())
            effectsLabel.GetComponent<TextComponent>().Text = std::format("{}", (int)effects.GetComponent<UISliderComponent>().Value);
    }

    void Greet(const std::string& name)
    {
        SetStatus(name.empty() ? std::string("Avval ismingizni yozing") : std::format("Salom, {}!", name));
    }

    void SetStatus(const std::string& text)
    {
        if (Entity status = Resolve(Status); status && status.HasComponent<TextComponent>())
            status.GetComponent<TextComponent>().Text = text;
    }

    float m_Time = 0.0f;
};

ZM_REGISTER_SCRIPT(UIWidgetsDemo,
    ZM_FIELD(UIWidgetsDemo, MusicSlider).Header("Vidjetlar"),
    ZM_FIELD(UIWidgetsDemo, MusicValue),
    ZM_FIELD(UIWidgetsDemo, EffectsSlider),
    ZM_FIELD(UIWidgetsDemo, EffectsValue),
    ZM_FIELD(UIWidgetsDemo, Loading),
    ZM_FIELD(UIWidgetsDemo, Cooldown),
    ZM_FIELD(UIWidgetsDemo, NameField),
    ZM_FIELD(UIWidgetsDemo, SaveButton),
    ZM_FIELD(UIWidgetsDemo, Status),
    ZM_FIELD_RANGE(UIWidgetsDemo, LoadingSeconds, 0.5f, 20.0f).Header("Vaqt").Label("Yuklanish (s)"),
    ZM_FIELD_RANGE(UIWidgetsDemo, CooldownSeconds, 0.5f, 20.0f).Label("Qayta tiklanish (s)"))
