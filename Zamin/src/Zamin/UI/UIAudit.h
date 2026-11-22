#pragma once
// The UI audit (P5 UI input / scale gate): what every visible screen-space UI element of a scene is at a view size -
// its widget kind, where it is laid out on the screen (px), its font size, how much room its text needs, whether it
// can be used and where keyboard / gamepad navigation goes from it - and the layout rules a shipped screen keeps at
// every resolution and UI scale. Scene::AuditUI makes the report (any view size, without drawing: headless tests can
// audit a menu at 6 resolutions in a frame); CheckUIAudit is pure, so the rules have their own unit tests; the player
// prints both (ZAMIN_UI_AUDIT). Bounded: at most MaxRecords elements per report.
#include "Zamin/UI/UIWidgets.h"

#include <glm/glm.hpp>
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Zamin {

    enum class UIAuditWidget : uint8_t { Element = 0, Text, Image, Button, Slider, Toggle, Progress, InputField };
    const char* UIAuditWidgetName(UIAuditWidget widget);

    struct UIAuditRecord
    {
        uint64_t ID = 0, Parent = 0; // UUIDs (Parent: its UI parent, 0 at a canvas's top)
        uint64_t Panel = 0;          // the root of the gameplay UI panel (UIConfigure) it is in, 0: none (HUD, backdrop)
        std::string Tag, PanelName;
        UIAuditWidget Widget = UIAuditWidget::Element;
        UIRect Rect;                 // px, origin bottom-left, as laid out (tweens and press feedback left out)
        glm::vec2 AnchorMin{ 0.0f }, AnchorMax{ 0.0f };
        float Scale = 1.0f;          // view px per UI unit (its canvas's, the UI scale setting included)
        float Alpha = 1.0f;          // with its UI ancestors' (0: faded out - not checked)
        // its text: font size (px), the room the laid-out text needs (px, measured with its font or the default one)
        bool HasText = false, TextFits = true, TextCutByDesign = false;
        float FontPx = 0.0f;
        glm::vec2 TextSize{ 0.0f };
        std::string RawHint;         // a control hint its text shows as written ("[Interact]": FindUnexpandedInputToken)
        bool Interactable = false, Selectable = false, Focused = false;
        bool Clipped = false;        // some of it is cut off by a UI Mask (all of it: Hidden)
        bool Hidden = false;         // faded out or masked away
        int Depth = 0;               // UI parents above it
        std::array<uint64_t, 4> Nav{}; // where Up / Down / Left / Right navigation goes from it (0: nowhere)
    };

    struct UIAuditReport
    {
        static constexpr size_t MaxRecords = 4096;
        glm::uvec2 Viewport{ 0 };
        float UserScale = 1.0f;      // Scene::GetUIScaleMultiplier
        std::string TopPanel;        // the open gameplay UI panel on top ("" none)
        uint64_t TopPanelRoot = 0, Focus = 0;
        size_t ModalDepth = 0;       // gameplay UI panels open
        bool GamePaused = false, Blocked = false; // the game's time stopped / its input fenced by an open panel
        float TimeScale = 1.0f;
        bool Truncated = false;      // more than MaxRecords elements: the rest were left out
        std::vector<UIAuditRecord> Records; // in drawing order (bottom first)
    };

    struct UIAuditRules
    {
        float Margin = 8.0f;          // px between a usable widget and the view's edge
        float MinFontPx = 12.0f;      // the smallest text on the screen
        float Tolerance = 1.0f;       // px of slack for overlaps / text room (rounding)
        float SideBand = 0.10f;       // wide views (aspect > 2): the outer share of the width only side-anchored UI may use
        // The smallest font for a view: 12 px at 800 x 600 and smaller, 14 px from 1024 x 768 up - at the default UI scale;
        // a smaller UI scale the player chose shrinks it in proportion (80 %: 9.6 / 11.2 px).
        static float MinFontFor(glm::uvec2 viewport, float userScale)
        {
            return (viewport.y > 600 ? 14.0f : 12.0f) * std::clamp(userScale, 0.5f, 1.0f);
        }
    };

    struct UIAuditIssue
    {
        std::string Rule;            // Margin, Overlap, TextOverflow, MinFont, RawHint, Offscreen, Focus, Unreachable, SideBand
        uint64_t A = 0, B = 0;
        std::string Detail;
    };

    // The rules (rect rules on the elements that are not hidden):
    //   Margin       every usable widget (interactable button / slider / toggle / input field) lies Margin px inside the view
    //   Offscreen    every text with something to show lies inside the view
    //   Overlap      usable widgets that are siblings do not overlap
    //   TextOverflow a text's laid-out lines fit its rectangle (unless it cuts them by design: Truncate / Ellipsis)
    //   MinFont      no text with something to show is smaller than MinFontPx
    //   RawHint      no text shows a control hint token of the input map as written ("[Interact] ..." instead of "E ...")
    //   Focus        with a panel open that has usable widgets, the selected widget is one of them
    //   Unreachable  ... and every selectable widget in it is reached by navigation from the selected one
    //   SideBand     on a wide view (aspect > 2) nothing at a canvas's top that is not anchored to that side (or
    //                stretched over the width) reaches into the outer SideBand of the width
    std::vector<UIAuditIssue> CheckUIAudit(const UIAuditReport& report, const UIAuditRules& rules);

    // {"Viewport":[w,h],"UIScale":s,"Top":"..","Depth":n,"Paused":b,"Blocked":b,"TimeScale":t,"Focus":id,"Records":n,"Truncated":b,
    //  "Issues":[{"Rule","A","B","Detail"}...]
    //  [,"Elements":[...]]}: one line (the player's UI_AUDIT log line).
    std::string UIAuditToJson(const UIAuditReport& report, const std::vector<UIAuditIssue>& issues, bool elements);

}
