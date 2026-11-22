#include "Zamin/UI/UIAudit.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <format>
#include <unordered_map>
#include <unordered_set>

namespace Zamin {

    const char* UIAuditWidgetName(UIAuditWidget widget)
    {
        switch (widget)
        {
            case UIAuditWidget::Text: return "Text";
            case UIAuditWidget::Image: return "Image";
            case UIAuditWidget::Button: return "Button";
            case UIAuditWidget::Slider: return "Slider";
            case UIAuditWidget::Toggle: return "Toggle";
            case UIAuditWidget::Progress: return "Progress";
            case UIAuditWidget::InputField: return "InputField";
            default: return "Element";
        }
    }

    namespace {
        bool Usable(const UIAuditRecord& r)
        {
            return !r.Hidden && r.Interactable &&
                   (r.Widget == UIAuditWidget::Button || r.Widget == UIAuditWidget::Slider || r.Widget == UIAuditWidget::Toggle ||
                    r.Widget == UIAuditWidget::InputField);
        }

        std::string Rect(const UIRect& r) { return std::format("[{:.0f},{:.0f} {:.0f}x{:.0f}]", r.Min.x, r.Min.y, r.GetWidth(), r.GetHeight()); }

        std::string Escape(std::string_view s)
        {
            std::string out;
            out.reserve(s.size() + 2);
            for (char c : s)
            {
                switch (c)
                {
                    case '"': out += "\\\""; break;
                    case '\\': out += "\\\\"; break;
                    case '\n': out += "\\n"; break;
                    case '\t': out += "\\t"; break;
                    default:
                        if ((unsigned char)c < 0x20)
                            out += std::format("\\u{:04x}", (unsigned)(unsigned char)c);
                        else
                            out += c;
                }
            }
            return out;
        }
    }

    std::vector<UIAuditIssue> CheckUIAudit(const UIAuditReport& report, const UIAuditRules& rules)
    {
        std::vector<UIAuditIssue> issues;
        auto add = [&](const char* rule, uint64_t a, uint64_t b, std::string detail) {
            if (issues.size() < 512)
                issues.push_back({ rule, a, b, std::move(detail) });
        };
        const glm::vec2 view(report.Viewport);
        const float slack = std::max(0.0f, rules.Tolerance);
        std::unordered_map<uint64_t, const UIAuditRecord*> byId;
        for (const auto& r : report.Records)
            byId[r.ID] = &r;

        for (const auto& r : report.Records)
        {
            if (r.Hidden)
                continue;
            const bool text = r.HasText;
            if (Usable(r))
            {
                const float m = rules.Margin - slack;
                if (r.Rect.Min.x < m || r.Rect.Min.y < m || r.Rect.Max.x > view.x - m || r.Rect.Max.y > view.y - m)
                    add("Margin", r.ID, 0, std::format("'{}' {} {:.0f} px chegaradan kam ({}x{})", r.Tag, Rect(r.Rect), rules.Margin, report.Viewport.x, report.Viewport.y));
            }
            if (text)
            {
                if (r.Rect.Min.x < -slack || r.Rect.Min.y < -slack || r.Rect.Max.x > view.x + slack || r.Rect.Max.y > view.y + slack)
                    add("Offscreen", r.ID, 0, std::format("'{}' matni {} ekrandan chiqadi", r.Tag, Rect(r.Rect)));
                if (!r.TextFits && !r.TextCutByDesign)
                    add("TextOverflow", r.ID, 0, std::format("'{}' matni {:.0f}x{:.0f} px, joyi {:.0f}x{:.0f}", r.Tag, r.TextSize.x, r.TextSize.y, r.Rect.GetWidth(),
                                                             r.Rect.GetHeight()));
                if (r.FontPx + 0.01f < rules.MinFontPx)
                    add("MinFont", r.ID, 0, std::format("'{}' {:.1f} px (eng kami {:.0f})", r.Tag, r.FontPx, rules.MinFontPx));
            }
        }
        // Usable siblings that overlap (more than the tolerance on both axes).
        std::unordered_map<uint64_t, std::vector<const UIAuditRecord*>> siblings;
        for (const auto& r : report.Records)
            if (Usable(r))
                siblings[r.Parent].push_back(&r);
        for (auto& [parent, list] : siblings)
            for (size_t i = 0; i < list.size(); ++i)
                for (size_t j = i + 1; j < list.size(); ++j)
                {
                    const UIRect& a = list[i]->Rect;
                    const UIRect& b = list[j]->Rect;
                    const float w = std::min(a.Max.x, b.Max.x) - std::max(a.Min.x, b.Min.x), h = std::min(a.Max.y, b.Max.y) - std::max(a.Min.y, b.Min.y);
                    if (w > slack && h > slack)
                        add("Overlap", list[i]->ID, list[j]->ID, std::format("'{}' {} va '{}' {}", list[i]->Tag, Rect(a), list[j]->Tag, Rect(b)));
                }
        // A panel open that has something to choose (a card that only shows does not): the selection inside it and
        // every selectable widget of it reachable from there.
        bool choices = false;
        for (const auto& r : report.Records)
            choices = choices || (report.TopPanelRoot && r.Panel == report.TopPanelRoot && r.Selectable && Usable(r));
        if (choices)
        {
            const auto focus = byId.find(report.Focus);
            if (!report.Focus || focus == byId.end())
                add("Focus", report.Focus, report.TopPanelRoot, std::format("'{}' paneli ochiq, tanlangan widget yo'q", report.TopPanel));
            else if (focus->second->Panel != report.TopPanelRoot || !Usable(*focus->second))
                add("Focus", report.Focus, report.TopPanelRoot,
                    std::format("tanlangan '{}' '{}' panelida emas yoki ishlatib bo'lmaydi", focus->second->Tag, report.TopPanel));
            else
            {
                std::unordered_set<uint64_t> seen{ report.Focus };
                std::deque<uint64_t> queue{ report.Focus };
                while (!queue.empty() && seen.size() <= UIAuditReport::MaxRecords)
                {
                    const auto it = byId.find(queue.front());
                    queue.pop_front();
                    if (it == byId.end())
                        continue;
                    for (uint64_t next : it->second->Nav)
                        if (next && seen.insert(next).second)
                            queue.push_back(next);
                }
                for (const auto& r : report.Records)
                    if (r.Panel == report.TopPanelRoot && r.Selectable && Usable(r) && !seen.contains(r.ID))
                        add("Unreachable", r.ID, report.Focus, std::format("'{}' ga '{}' dan klaviatura bilan yetib bo'lmaydi", r.Tag, focus->second->Tag));
            }
        }
        // Wide views: the outer bands belong to what is anchored to that side (HUD corners) or stretched over the width.
        if (view.y > 0.0f && view.x / view.y > 2.0f)
        {
            const float band = view.x * std::clamp(rules.SideBand, 0.0f, 0.5f);
            for (const auto& r : report.Records)
            {
                if (r.Hidden || r.Depth != 0 || r.Rect.IsEmpty())
                    continue;
                const bool stretched = r.AnchorMin.x <= 0.0f && r.AnchorMax.x >= 1.0f;
                if (stretched)
                    continue;
                const bool left = r.AnchorMin.x == 0.0f && r.AnchorMax.x == 0.0f, right = r.AnchorMin.x == 1.0f && r.AnchorMax.x == 1.0f;
                if ((r.Rect.Min.x < band - slack && !left) || (r.Rect.Max.x > view.x - band + slack && !right))
                    add("SideBand", r.ID, 0, std::format("'{}' {} keng ekranning chetiga kiradi, lekin o'sha tomonga bog'lanmagan", r.Tag, Rect(r.Rect)));
            }
        }
        return issues;
    }

    std::string UIAuditToJson(const UIAuditReport& report, const std::vector<UIAuditIssue>& issues, bool elements)
    {
        std::string out = std::format("{{\"Viewport\":[{},{}],\"UIScale\":{:.3f},\"Top\":\"{}\",\"Depth\":{},\"Paused\":{},\"Blocked\":{},\"TimeScale\":{:.3f},"
                                      "\"Focus\":{},\"Records\":{},\"Truncated\":{},\"Issues\":[",
                                      report.Viewport.x, report.Viewport.y, report.UserScale, Escape(report.TopPanel), report.ModalDepth,
                                      report.GamePaused ? "true" : "false", report.Blocked ? "true" : "false", report.TimeScale, report.Focus,
                                      report.Records.size(), report.Truncated ? "true" : "false");
        for (size_t i = 0; i < issues.size(); ++i)
            out += std::format("{}{{\"Rule\":\"{}\",\"A\":{},\"B\":{},\"Detail\":\"{}\"}}", i ? "," : "", issues[i].Rule, issues[i].A, issues[i].B, Escape(issues[i].Detail));
        out += "]";
        // The smallest font and the usable widgets: what the harness reports per screen.
        float minFont = 0.0f;
        size_t usable = 0, texts = 0;
        for (const auto& r : report.Records)
        {
            if (r.Hidden)
                continue;
            if (r.HasText)
            {
                minFont = texts++ ? std::min(minFont, r.FontPx) : r.FontPx;
            }
            usable += Usable(r) ? 1 : 0;
        }
        out += std::format(",\"Usable\":{},\"Texts\":{},\"MinFontPx\":{:.2f}", usable, texts, minFont);
        if (elements)
        {
            out += ",\"Elements\":[";
            bool first = true;
            for (const auto& r : report.Records)
            {
                if (r.Hidden)
                    continue;
                out += std::format("{}{{\"ID\":{},\"Tag\":\"{}\",\"Widget\":\"{}\",\"Rect\":[{:.1f},{:.1f},{:.1f},{:.1f}],\"Panel\":\"{}\",\"Font\":{:.1f},"
                                   "\"Usable\":{},\"Focused\":{},\"Nav\":[{},{},{},{}]}}",
                                   first ? "" : ",", r.ID, Escape(r.Tag), UIAuditWidgetName(r.Widget), r.Rect.Min.x, r.Rect.Min.y, r.Rect.Max.x, r.Rect.Max.y,
                                   Escape(r.PanelName), r.FontPx, Usable(r) ? "true" : "false", r.Focused ? "true" : "false", r.Nav[0], r.Nav[1], r.Nav[2],
                                   r.Nav[3]);
                first = false;
            }
            out += "]";
        }
        out += "}";
        return out;
    }

}
