#include <Zamin/Zamin.h>

#include <format>

using namespace Zamin;

// The UILayout scene: a sample of the UI hierarchy and automatic layout.
// - Canvas: the UI is made for 1920 x 1080 and scales with the screen size (UI Canvas);
// - the top / bottom panels are stretched from left to right (stretch anchors), their contents are tied to
//   their corners;
// - the menu is a vertical layout group (its height fits the buttons), the inventory a grid,
//   the bottom panel a horizontal group (shares the space equally);
// - UI events (event bus): "UIPointerEnter" / "UIPointerExit" (the mouse over it), "UIClick",
//   "UISelected", "UICancel"; the menu buttons send their own message when pressed (UI Button > Message name);
// - keyboard / gamepad: the arrows or the D-pad select, Enter / A presses, Tab - the next one;
// - UI animations (UI Animation): the menu and the inventory slide in from the edge, the buttons pop out one
//   after another, grow when hovered / shrink when pressed; pressing a slot makes the coins label
//   "jump" (PunchUIScale); the "News" panel closes with X (HideUI), "Achievements" brings it back (ShowUI);
// - text: the "News" text wraps by words, what does not fit is cut with an ellipsis; the title is
//   centered, the page number bottom right, and the status line ends with "..." when it is long.
class UILayoutDemo : public ScriptableEntity
{
public:
    EntityRef Status;
    EntityRef Info;
    EntityRef Coins;
    EntityRef News;

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
            m_Last = std::format("{}: {}", m.Sender.GetName(), m.Get<bool>() ? "yoqildi" : "removed");
            Refresh();
        });
        Subscribe("UICancel", [this](const ScriptMessage&) {
            m_Last = "Cancelled (Esc / B)";
            Refresh();
        });
        // The menu buttons' own messages (they come after "UIClick").
        AddMessageHandler("Start", [this](const ScriptMessage&) { SetLast("A new game starts!"); });
        AddMessageHandler("Continue", [this](const ScriptMessage&) { SetLast("The saved game continues"); });
        AddMessageHandler("Settings", [this](const ScriptMessage&) { SetLast("Settings open"); });
        AddMessageHandler("Achievements", [this](const ScriptMessage&) {
            ShowUI(Resolve(News)); // comes back with its show animation
            SetLast("Yutuqlar: 7 / 24");
        });
        AddMessageHandler("CloseNews", [this](const ScriptMessage&) {
            HideUI(Resolve(News)); // its hide animation, then switched off
            SetLast("News closed (Achievements opens it again)");
        });
        AddMessageHandler("Quit", [this](const ScriptMessage&) { SetLast("Quit (disabled in the sample)"); });
        AddMessageHandler("Slot", [this](const ScriptMessage& m) {
            m_Coins += 5;
            if (Entity coins = Resolve(Coins))
                GetScene().PunchUIScale(coins, 0.25f, 0.4f); // the counter jumps
            SetLast(std::format("{} taken: +5 coins", m.Sender.GetName()));
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
            info.GetComponent<TextComponent>().Text = std::format("Screen {:.0f} x {:.0f} px  ->  UI scale {:.2f} (for 1920 x 1080)", view.x, view.y,
                                                                  GetScene().GetUIScale(GetEntity()));
        }
        if (Entity coins = Resolve(Coins); coins && coins.HasComponent<TextComponent>())
            coins.GetComponent<TextComponent>().Text = std::format("Coins: {}", m_Coins);
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
        std::string text = std::format("Hovered: {}   |   Selected: {}", m_Hover.empty() ? "-" : m_Hover, m_Selected.empty() ? "-" : m_Selected);
        if (!m_Last.empty())
            text += "   |   " + m_Last;
        status.GetComponent<TextComponent>().Text = text;
    }

    std::string m_Hover, m_Selected, m_Last;
    int m_Coins = 120;
};

ZM_REGISTER_SCRIPT(UILayoutDemo,
    ZM_FIELD(UILayoutDemo, Status).Header("Labels"),
    ZM_FIELD(UILayoutDemo, Info),
    ZM_FIELD(UILayoutDemo, Coins),
    ZM_FIELD(UILayoutDemo, News))
