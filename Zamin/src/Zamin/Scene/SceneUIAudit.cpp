// The UI audit (Scene::AuditUI, UIAudit.h) and the player's UI scale setting (Scene::SetUIScaleMultiplier).
#include "Zamin/Scene/Scene.h"

#include "Zamin/Gameplay/GameplayUI.h"
#include "Zamin/Renderer/Font.h"
#include "Zamin/Renderer/Renderer.h"
#include "Zamin/Scene/Components.h"
#include "Zamin/Scene/Entity.h"
#include "Zamin/UI/TextLayout.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace Zamin {

    void Scene::SetUIScaleMultiplier(float scale)
    {
        if (!std::isfinite(scale))
            return;
        m_UIScaleMultiplier = std::clamp(scale, MinUIScaleMultiplier, MaxUIScaleMultiplier);
    }

    namespace {
        // The default font's metrics without GPU objects (measuring text in a headless audit), read once.
        const Font* AuditFont()
        {
            static const Ref<Font> s_Font = [] {
                const auto path = Renderer::GetResourcesDirectory() / "fonts" / "Inter-SemiBold.ttf";
                std::error_code ec;
                return std::filesystem::exists(path, ec) ? Font::CreateHeadless(path) : Ref<Font>();
            }();
            return s_Font && s_Font->IsLoaded() ? s_Font.get() : nullptr;
        }
    }

    UIAuditReport Scene::AuditUI(uint32_t width, uint32_t height, bool measureText)
    {
        UIAuditReport report;
        report.Viewport = { width, height };
        report.UserScale = m_UIScaleMultiplier;
        // Laid out for that view; the scene's own view (and its layout) back afterwards.
        const uint32_t ownWidth = m_ViewportWidth, ownHeight = m_ViewportHeight;
        m_ViewportWidth = width;
        m_ViewportHeight = height;
        LayoutUI();

        // The gameplay UI panels: which one each element is in, and the one on top.
        std::unordered_map<uint64_t, std::string> panels;
        if (Gameplay::UIWorld* ui = Gameplay::FindGameplayUI(*this))
        {
            for (const auto& [name, root] : ui->PanelRoots())
                panels[root] = name;
            const Gameplay::UIState state = ui->State();
            report.TopPanel = state.Top;
            report.ModalDepth = state.Depth;
            report.Blocked = state.Blocked;
            for (const auto& [root, name] : panels)
                if (name == report.TopPanel)
                    report.TopPanelRoot = root;
        }
        report.GamePaused = IsGamePaused();
        report.TimeScale = m_TimeScale;
        const Entity selected = GetSelectedUI();
        report.Focus = selected ? (uint64_t)selected.GetUUID() : 0;

        for (entt::entity e : GetUIDrawOrder())
        {
            Entity entity{ e, this };
            if (!IsActiveInHierarchy(entity))
                continue;
            if (report.Records.size() >= UIAuditReport::MaxRecords)
            {
                report.Truncated = true;
                break;
            }
            const UILayoutNode& node = GetUINode(e);
            const auto& t = m_Registry.get<UITransformComponent>(e);
            UIAuditRecord r;
            r.ID = (uint64_t)entity.GetUUID();
            r.Tag = entity.GetName();
            const entt::entity parent = GetUIParent(e);
            r.Parent = parent != entt::null && m_Registry.valid(parent) ? (uint64_t)Entity(parent, this).GetUUID() : 0;
            for (entt::entity a = parent; a != entt::null && r.Depth < 1024; a = GetUIParent(a))
                r.Depth++;
            // The panel: the nearest ancestor (or itself) that is a panel's root.
            for (Entity a = entity; a && r.Panel == 0;)
            {
                if (auto it = panels.find((uint64_t)a.GetUUID()); it != panels.end())
                {
                    r.Panel = it->first;
                    r.PanelName = it->second;
                }
                const auto* rel = a.TryGetComponent<RelationshipComponent>();
                a = rel && (uint64_t)rel->Parent ? GetEntityByUUID(rel->Parent) : Entity{};
            }
            const bool input = m_Registry.all_of<UIInputFieldComponent>(e), slider = m_Registry.all_of<UISliderComponent>(e);
            const auto* toggle = m_Registry.try_get<UIToggleComponent>(e);
            const auto* button = m_Registry.try_get<UIButtonComponent>(e);
            r.Widget = input ? UIAuditWidget::InputField : slider ? UIAuditWidget::Slider : toggle ? UIAuditWidget::Toggle : button ? UIAuditWidget::Button
                     : m_Registry.all_of<UIProgressBarComponent>(e) ? UIAuditWidget::Progress : m_Registry.all_of<TextComponent>(e) ? UIAuditWidget::Text
                     : m_Registry.all_of<UIImageComponent>(e) ? UIAuditWidget::Image : UIAuditWidget::Element;
            r.Interactable = input ? m_Registry.get<UIInputFieldComponent>(e).Interactable
                           : slider ? m_Registry.get<UISliderComponent>(e).Interactable
                           : toggle ? toggle->Interactable : button ? button->Interactable : false;
            r.Selectable = IsUISelectable(entity);
            r.Focused = selected == entity;
            r.Rect = { node.Rect.Min * node.Scale, node.Rect.Max * node.Scale };
            r.AnchorMin = t.AnchorMin;
            r.AnchorMax = t.AnchorMax;
            r.Scale = node.Scale;
            r.Alpha = node.Alpha;
            if (node.Clipped)
            {
                const UIRect& clip = node.VisClip;
                r.Hidden = clip.IsEmpty() || clip.Max.x <= r.Rect.Min.x || clip.Max.y <= r.Rect.Min.y || clip.Min.x >= r.Rect.Max.x || clip.Min.y >= r.Rect.Max.y;
                r.Clipped = r.Hidden || clip.Min.x > r.Rect.Min.x + 0.5f || clip.Min.y > r.Rect.Min.y + 0.5f || clip.Max.x < r.Rect.Max.x - 0.5f ||
                            clip.Max.y < r.Rect.Max.y - 0.5f;
            }
            r.Hidden = r.Hidden || node.Alpha <= 0.01f;
            if (const auto* text = m_Registry.try_get<TextComponent>(e))
            {
                const std::string& shown = text->GetDisplayText();
                if (!shown.empty())
                {
                    r.HasText = true;
                    const UIRect textRect = toggle ? UIMath::GetToggleLabel(node.Rect, toggle->BoxSize, toggle->Style, toggle->LabelSpacing) : node.Rect;
                    const float px = GetUITextSize(t, node.Rect, toggle != nullptr);
                    r.FontPx = px * node.Scale;
                    const Font* font = text->FontAsset && text->FontAsset->IsLoaded() ? text->FontAsset.get() : measureText ? AuditFont() : nullptr;
                    const TextLayoutResult layout = TextLayout::Layout(shown, TextMetrics::Of(font), px, textRect, text->GetLayoutSettings());
                    r.TextSize = layout.Size * node.Scale;
                    r.TextCutByDesign = text->Overflow != TextOverflow::Overflow;
                    // A pixel of slack for rounding; a line's height (1.2 em) may stand over a tight box by an eighth of it.
                    const float slack = 1.0f / std::max(node.Scale, 0.01f);
                    r.TextFits = layout.Size.x <= textRect.GetWidth() + slack && layout.Size.y <= textRect.GetHeight() * 1.125f + slack;
                }
            }
            if (r.Selectable)
            {
                const UINavDirection directions[4] = { UINavDirection::Up, UINavDirection::Down, UINavDirection::Left, UINavDirection::Right };
                for (int d = 0; d < 4; ++d)
                    if (Entity next = FindUINavigationTarget(entity, directions[d]))
                        r.Nav[(size_t)d] = (uint64_t)next.GetUUID();
            }
            report.Records.push_back(std::move(r));
        }

        m_ViewportWidth = ownWidth;
        m_ViewportHeight = ownHeight;
        LayoutUI();
        return report;
    }

}
