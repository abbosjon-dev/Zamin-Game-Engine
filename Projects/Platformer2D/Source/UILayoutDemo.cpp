#include <Zamin/Zamin.h>

#include <format>

using namespace Zamin;

// UILayout sahnasi: UI ierarxiyasi va avtomatik joylashtirish namunasi.
// - Canvas: UI 1920 x 1080 uchun yasalgan, ekran o'lchami bilan birga masshtablanadi (UI Canvas);
// - yuqori / pastki panellar chapdan o'ngga cho'zilgan (stretch anchor), ichidagilar ularning
//   burchaklariga bog'langan;
// - menyu - vertikal layout guruhi (balandligi tugmalarga moslashadi), inventar - panjara,
//   pastki panel - gorizontal guruh (joyni teng bo'ladi);
// - UI hodisalari (event bus): "UIPointerEnter" / "UIPointerExit" (sichqoncha ustida), "UIClick",
//   "UISelected", "UICancel"; menyu tugmalari bosilganda o'z xabarini (UI Button > Xabar nomi) yuboradi;
// - klaviatura / gamepad: strelkalar yoki D-pad tanlaydi, Enter / A bosadi, Tab - keyingisi.
class UILayoutDemo : public ScriptableEntity
{
public:
    EntityRef Status;
    EntityRef Info;
    EntityRef Coins;

    void OnStart() override
    {
        Subscribe("UIPointerEnter", [this](const ScriptMessage& m) {
            if (IsWidget(m.Sender))
            {
                m_Hover = m.Sender.GetName();
                Refresh();
            }
        });
        Subscribe("UIPointerExit", [this](const ScriptMessage& m) {
            if (m.Sender && m.Sender.GetName() == m_Hover)
            {
                m_Hover.clear();
                Refresh();
            }
        });
        Subscribe("UISelected", [this](const ScriptMessage& m) {
            m_Selected = m.Sender.GetName();
            Refresh();
        });
        Subscribe("UIDeselected", [this](const ScriptMessage& m) {
            if (m.Sender && m.Sender.GetName() == m_Selected)
                m_Selected.clear();
            Refresh();
        });
        Subscribe("UIClick", [this](const ScriptMessage& m) {
            m_Last = std::format("Bosildi: {}", m.Sender.GetName());
            Refresh();
        });
        Subscribe("UIToggleChanged", [this](const ScriptMessage& m) {
            m_Last = std::format("{}: {}", m.Sender.GetName(), m.Get<bool>() ? "yoqildi" : "o'chirildi");
            Refresh();
        });
        Subscribe("UICancel", [this](const ScriptMessage&) {
            m_Last = "Bekor qilindi (Esc / B)";
            Refresh();
        });
        // The menu buttons' own messages (they come after "UIClick").
        AddMessageHandler("Start", [this](const ScriptMessage&) { SetLast("Yangi o'yin boshlanadi!"); });
        AddMessageHandler("Continue", [this](const ScriptMessage&) { SetLast("Saqlangan o'yin davom etadi"); });
        AddMessageHandler("Settings", [this](const ScriptMessage&) { SetLast("Sozlamalar ochiladi"); });
        AddMessageHandler("Achievements", [this](const ScriptMessage&) { SetLast("Yutuqlar: 7 / 24"); });
        AddMessageHandler("Quit", [this](const ScriptMessage&) { SetLast("Chiqish (namunada o'chirilgan)"); });
        AddMessageHandler("Slot", [this](const ScriptMessage& m) {
            m_Coins += 5;
            SetLast(std::format("{} olindi: +5 tanga", m.Sender.GetName()));
        });
        // A widget selected before this script started (UI Navigation > Faollashganda tanlash).
        if (Entity selected = GetScene().GetSelectedUI())
            m_Selected = selected.GetName();
        Refresh();
    }

    void OnUpdate(Timestep) override
    {
        if (Entity info = Resolve(Info); info && info.HasComponent<TextComponent>())
        {
            const glm::vec2 view = GetScene().GetViewportSize();
            info.GetComponent<TextComponent>().Text = std::format("Ekran {:.0f} x {:.0f} px  ->  UI masshtabi {:.2f} (1920 x 1080 uchun)", view.x, view.y,
                                                                  GetScene().GetUIScale(GetEntity()));
        }
        if (Entity coins = Resolve(Coins); coins && coins.HasComponent<TextComponent>())
            coins.GetComponent<TextComponent>().Text = std::format("Tangalar: {}", m_Coins);
    }

private:
    bool IsWidget(Entity e) const
    {
        return e && (e.HasComponent<UIButtonComponent>() || e.HasComponent<UIToggleComponent>() || e.HasComponent<UISliderComponent>());
    }

    void SetLast(const std::string& text)
    {
        m_Last = text;
        Refresh();
    }

    void Refresh()
    {
        Entity status = Resolve(Status);
        if (!status || !status.HasComponent<TextComponent>())
            return;
        std::string text = std::format("Ustida: {}   |   Tanlangan: {}", m_Hover.empty() ? "-" : m_Hover, m_Selected.empty() ? "-" : m_Selected);
        if (!m_Last.empty())
            text += "   |   " + m_Last;
        status.GetComponent<TextComponent>().Text = text;
    }

    std::string m_Hover, m_Selected, m_Last;
    int m_Coins = 120;
};

ZM_REGISTER_SCRIPT(UILayoutDemo,
    ZM_FIELD(UILayoutDemo, Status).Header("Yozuvlar"),
    ZM_FIELD(UILayoutDemo, Info),
    ZM_FIELD(UILayoutDemo, Coins))
