// Screen-space UI of a scene: layout of the UI rectangles, the mouse and keyboard handling of the
// widgets (buttons, sliders, toggles, input fields) and the UI pass that draws them over the scene.
#include "Zamin/Scene/Scene.h"
#include "Zamin/Asset/AssetRegistry.h"
#include "Zamin/Core/Input.h"
#include "Zamin/Project/Project.h"
#include "Zamin/Renderer/Font.h"
#include "Zamin/Renderer/RenderCommand.h"
#include "Zamin/Renderer/Renderer2D.h"
#include "Zamin/Renderer/SpriteSheet.h"
#include "Zamin/Scene/Components.h"
#include "Zamin/Scene/Entity.h"
#include "Zamin/Scripting/EventBus.h"
#include "Zamin/Scripting/ScriptableEntity.h"
#include "Zamin/UI/TextEdit.h"
#include "Zamin/UI/TextLayout.h"
#include "Zamin/UI/UIWidgets.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <format>

namespace Zamin {

    // ------------------------------------------------------------------ layout
    entt::entity Scene::GetUIParent(entt::entity e)
    {
        const auto* rel = m_Registry.try_get<RelationshipComponent>(e);
        if (!rel || !(uint64_t)rel->Parent)
            return entt::null;
        auto it = m_EntityMap.find(rel->Parent);
        if (it == m_EntityMap.end() || !m_Registry.valid(it->second) || !m_Registry.all_of<UITransformComponent>(it->second))
            return entt::null;
        return it->second;
    }

    void Scene::GetUIChildren(entt::entity e, std::vector<entt::entity>& out)
    {
        out.clear();
        if (const auto* rel = m_Registry.try_get<RelationshipComponent>(e))
            for (UUID id : rel->Children)
                if (auto it = m_EntityMap.find(id); it != m_EntityMap.end() && m_Registry.valid(it->second) &&
                                                    m_Registry.all_of<UITransformComponent>(it->second))
                    out.push_back(it->second);
    }

    // Whether a layout group leaves the child out: switched off itself, or its layout element says so.
    static bool IgnoredByLayout(entt::registry& registry, entt::entity child)
    {
        if (const auto* tag = registry.try_get<TagComponent>(child); tag && !tag->Active)
            return true;
        const auto* element = registry.try_get<UILayoutElementComponent>(child);
        return element && element->IgnoreLayout;
    }

    UILayoutSizes Scene::GetUILayoutGroupSizes(entt::entity group, const glm::vec2& groupSize, std::unordered_map<entt::entity, UILayoutSizes>& sizes)
    {
        if (auto it = sizes.find(group); it != sizes.end())
            return it->second;
        sizes[group] = UILayoutSizes{}; // a guard while its children are measured
        const auto& settings = m_Registry.get<UILayoutGroupComponent>(group);
        std::vector<entt::entity> children;
        GetUIChildren(group, children);
        std::vector<UILayoutItem> items;
        for (auto child : children)
            if (!IgnoredByLayout(m_Registry, child))
                items.push_back(GetUILayoutItem(child, groupSize, sizes));
        const UILayoutSizes result = UILayout::CalcSizes(settings, items, groupSize);
        sizes[group] = result;
        return result;
    }

    UILayoutItem Scene::GetUILayoutItem(entt::entity child, const glm::vec2& parentSize, std::unordered_map<entt::entity, UILayoutSizes>& sizes)
    {
        const auto& t = m_Registry.get<UITransformComponent>(child);
        UILayoutItem item;
        // Its own size: along a stretched axis the anchors' span of the parent plus Size.
        item.Size = glm::abs((t.AnchorMax - t.AnchorMin) * parentSize + t.Size);
        item.Preferred = item.Size;
        if (const auto* group = m_Registry.try_get<UILayoutGroupComponent>(child))
        {
            // A group asks for what its children need.
            const UILayoutSizes content = GetUILayoutGroupSizes(child, item.Size, sizes);
            item.Min = content.Min;
            item.Preferred = content.Preferred;
            item.Flexible = content.Flexible;
            if (group->FitWidth) item.Size.x = content.Preferred.x;
            if (group->FitHeight) item.Size.y = content.Preferred.y;
        }
        if (const auto* element = m_Registry.try_get<UILayoutElementComponent>(child))
            for (int axis = 0; axis < 2; axis++)
            {
                if (element->MinSize[axis] >= 0.0f) item.Min[axis] = element->MinSize[axis];
                if (element->PreferredSize[axis] >= 0.0f) item.Preferred[axis] = element->PreferredSize[axis];
                if (element->FlexibleSize[axis] >= 0.0f) item.Flexible[axis] = element->FlexibleSize[axis];
            }
        return item;
    }

    UIRect Scene::ComputeOwnUIRect(entt::entity e, const UIRect& parent, std::unordered_map<entt::entity, UILayoutSizes>& sizes)
    {
        const auto& t = m_Registry.get<UITransformComponent>(e);
        UIRect rect = t.ComputeRect(parent);
        if (const auto* group = m_Registry.try_get<UILayoutGroupComponent>(e); group && (group->FitWidth || group->FitHeight))
        {
            // Fits its content: that size around its pivot.
            const UILayoutSizes content = GetUILayoutGroupSizes(e, rect.GetSize(), sizes);
            const glm::vec2 pivot = rect.Min + rect.GetSize() * t.Pivot;
            for (int axis = 0; axis < 2; axis++)
                if (axis == 0 ? group->FitWidth : group->FitHeight)
                {
                    const float size = content.Preferred[axis];
                    rect.Min[axis] = pivot[axis] - size * t.Pivot[axis];
                    rect.Max[axis] = rect.Min[axis] + size;
                }
        }
        return rect;
    }

    void Scene::LayoutUINode(entt::entity e, const UIRect& parent, const UIRect& rect, float scale, bool driven,
                             std::unordered_map<entt::entity, UILayoutSizes>& sizes, const UILayoutNode* parentLook)
    {
        // Its look around its pivot (drawn: also its widget's hover / press feedback), after its ancestors'.
        const auto& t = m_Registry.get<UITransformComponent>(e);
        const glm::vec2 pivot = rect.Min + rect.GetSize() * t.Pivot;
        const float feedback = m_Registry.all_of<UIAnimationComponent>(e) ? m_Registry.get<UIAnimationComponent>(e).FeedbackScale : 1.0f;
        UILayoutNode node{ rect, parent, scale, driven };
        auto compose = [&](const glm::vec2& own, const glm::vec2& parentScale, const glm::vec2& parentOffset, glm::vec2& outScale, glm::vec2& outOffset) {
            outScale = parentScale * own;
            outOffset = (pivot * (1.0f - own) + t.VisualOffset) * parentScale + parentOffset;
        };
        auto box = [](const UIRect& r, const glm::vec2& s, const glm::vec2& o) {
            const glm::vec2 a = r.Min * s + o, b = r.Max * s + o;
            return UIRect{ glm::min(a, b), glm::max(a, b) };
        };
        const glm::vec2 one(1.0f), zero(0.0f);
        compose(t.Scale * feedback, parentLook ? parentLook->VisScale : one, parentLook ? parentLook->VisOffset : zero, node.VisScale, node.VisOffset);
        compose(t.Scale, parentLook ? parentLook->HitScale : one, parentLook ? parentLook->HitOffset : zero, node.HitScale, node.HitOffset);
        node.Alpha = (parentLook ? parentLook->Alpha : 1.0f) * std::clamp(t.Alpha, 0.0f, 1.0f);
        node.Visual = box(rect, node.VisScale, node.VisOffset);
        node.HitRect = box(rect, node.HitScale, node.HitOffset);
        // The masks of its UI ancestors clip it.
        if (parentLook && parentLook->ChildClipped)
        {
            node.Clipped = true;
            node.VisClip = parentLook->ChildVisClip;
            node.HitClip = parentLook->ChildHitClip;
        }
        auto* scroll = m_Registry.try_get<UIScrollViewComponent>(e);
        // Its children's space (a scroll view's: without its scrollbars) and its own mask's clip for them.
        auto prepareChildren = [&]() {
            node.ChildArea = scroll ? UIScroll::GetChildArea(rect, scroll->ShowHorizontalBar, scroll->ShowVerticalBar, scroll->ScrollbarWidth,
                                                             scroll->ScrollbarInset)
                                    : rect;
            node.ChildClipped = node.Clipped;
            node.ChildVisClip = node.VisClip;
            node.ChildHitClip = node.HitClip;
            if (const auto* mask = m_Registry.try_get<UIMaskComponent>(e))
            {
                const UIRect area = scroll && !scroll->Viewport ? node.ChildArea : rect;
                // Padding in view px through the look (per axis) and the canvas scale.
                auto clip = [&](const glm::vec2& s, const glm::vec2& o, const UIRect& outer) {
                    const glm::vec2 k = glm::abs(s) * scale;
                    const glm::vec4 padding{ mask->Padding.x * k.x, mask->Padding.y * k.x, mask->Padding.z * k.y, mask->Padding.w * k.y };
                    const UIRect px = box(area, s, o);
                    return UIScroll::MaskClip({ px.Min * scale, px.Max * scale }, padding, node.Clipped, outer);
                };
                node.ChildVisClip = clip(node.VisScale, node.VisOffset, node.VisClip);
                node.ChildHitClip = clip(node.HitScale, node.HitOffset, node.HitClip);
                node.ChildClipped = true;
            }
        };
        prepareChildren();
        m_UILayout[e] = node;
        if (auto* canvas = m_Registry.try_get<UICanvasComponent>(e))
            canvas->CurrentScale = scale; // an inner canvas takes the outer one's scale
        std::vector<entt::entity> children;
        GetUIChildren(e, children);
        if (children.empty())
            return;
        auto layoutChildren = [&]() {
            const UIRect area = node.ChildArea;
            // A layout group places its children (in hierarchy order); the rest follow their own UI Transform.
            std::vector<entt::entity> placed;
            std::vector<UIRect> rects;
            if (const auto* group = m_Registry.try_get<UILayoutGroupComponent>(e))
            {
                std::vector<UILayoutItem> items;
                for (auto child : children)
                    if (!IgnoredByLayout(m_Registry, child))
                    {
                        placed.push_back(child);
                        items.push_back(GetUILayoutItem(child, area.GetSize(), sizes));
                    }
                UILayout::Arrange(area, *group, items, rects);
            }
            size_t next = 0;
            for (auto child : children)
            {
                if (next < placed.size() && placed[next] == child)
                    LayoutUINode(child, area, rects[next++], scale, true, sizes, &node);
                else
                    LayoutUINode(child, area, ComputeOwnUIRect(child, area, sizes), scale, false, sizes, &node);
            }
        };
        layoutChildren();
        if (!scroll)
            return;
        // A scroll view shows a scrollbar while its content does not fit that way (AutoHide), always
        // (Permanent) or never; the children are laid out again when that changed the room they have.
        bool showH = false, showV = false;
        if (scroll->Scrollbars == UIScrollbarVisibility::Permanent)
        {
            showH = scroll->Horizontal;
            showV = scroll->Vertical;
        }
        else if (scroll->Scrollbars == UIScrollbarVisibility::AutoHide)
        {
            const entt::entity content = GetScrollContentEntity(e);
            if (auto it = content != entt::null ? m_UILayout.find(content) : m_UILayout.end(); it != m_UILayout.end())
            {
                const UIRect contentRect = it->second.Rect;
                const UIRect view = GetScrollViewRectLaidOut(e);
                showH = scroll->Horizontal && contentRect.GetWidth() > view.GetWidth() + 0.5f;
                showV = scroll->Vertical && contentRect.GetHeight() > view.GetHeight() + 0.5f;
            }
        }
        if (showH == scroll->ShowHorizontalBar && showV == scroll->ShowVerticalBar)
            return;
        scroll->ShowHorizontalBar = showH;
        scroll->ShowVerticalBar = showV;
        prepareChildren();
        m_UILayout[e] = node;
        layoutChildren();
    }

    void Scene::LayoutUI()
    {
        m_UILayout.clear();
        const glm::vec2 viewport = glm::max(GetViewportSize(), glm::vec2(0.0f));
        std::unordered_map<entt::entity, UILayoutSizes> sizes;
        for (auto e : m_Registry.view<UITransformComponent>())
        {
            if (GetUIParent(e) != entt::null)
                continue; // laid out with its parent
            // The outermost UI Canvas of its ancestors (itself included, also through plain entities) scales it.
            entt::entity canvas = entt::null;
            entt::entity a = e;
            for (int depth = 0; a != entt::null && depth < 1024; depth++)
            {
                if (m_Registry.all_of<UICanvasComponent>(a))
                    canvas = a;
                const auto* rel = m_Registry.try_get<RelationshipComponent>(a);
                if (!rel || !(uint64_t)rel->Parent)
                    break;
                auto it = m_EntityMap.find(rel->Parent);
                a = it != m_EntityMap.end() && m_Registry.valid(it->second) ? it->second : entt::null;
            }
            float scale = 1.0f;
            if (canvas != entt::null)
            {
                auto& c = m_Registry.get<UICanvasComponent>(canvas);
                scale = c.ComputeScale(viewport);
                c.CurrentScale = scale;
            }
            const UIRect screen{ { 0.0f, 0.0f }, viewport / scale };
            LayoutUINode(e, screen, ComputeOwnUIRect(e, screen, sizes), scale, false, sizes);
        }
    }

    const Scene::UILayoutNode& Scene::GetUINode(entt::entity e)
    {
        auto it = m_UILayout.find(e);
        if (it == m_UILayout.end() && m_Registry.valid(e) && m_Registry.all_of<UITransformComponent>(e))
        {
            LayoutUI(); // added since the frame's layout
            it = m_UILayout.find(e);
        }
        static const UILayoutNode s_None{};
        return it != m_UILayout.end() ? it->second : s_None;
    }

    static bool HasUITransform(Entity entity, Scene* scene)
    {
        return entity && entity.GetScene() == scene && entity.HasComponent<UITransformComponent>();
    }

    UIRect Scene::GetUIRect(Entity entity)
    {
        if (!HasUITransform(entity, this))
            return {};
        LayoutUI();
        return GetUINode(entity).Rect;
    }

    UIRect Scene::GetUIScreenRect(Entity entity)
    {
        if (!HasUITransform(entity, this))
            return {};
        LayoutUI();
        const UILayoutNode& node = GetUINode(entity);
        return { node.Rect.Min * node.Scale, node.Rect.Max * node.Scale };
    }

    UIRect Scene::GetUIVisualRect(Entity entity)
    {
        if (!HasUITransform(entity, this))
            return {};
        LayoutUI();
        return GetUINode(entity).Visual;
    }

    float Scene::GetUIAlpha(Entity entity)
    {
        if (!HasUITransform(entity, this))
            return 1.0f;
        LayoutUI();
        return GetUINode(entity).Alpha;
    }

    glm::vec2 Scene::UILayoutToView(Entity entity, const glm::vec2& point)
    {
        if (!HasUITransform(entity, this))
            return point;
        LayoutUI();
        const UILayoutNode& node = GetUINode(entity);
        return (point * node.HitScale + node.HitOffset) * node.Scale;
    }

    glm::vec2 Scene::UIViewToLayout(Entity entity, const glm::vec2& pixel)
    {
        if (!HasUITransform(entity, this))
            return pixel;
        LayoutUI();
        const UILayoutNode& node = GetUINode(entity);
        return node.ToLayout(pixel / std::max(node.Scale, 1e-6f));
    }

    Entity Scene::PickUIRect(const glm::vec2& point, const std::function<bool(Entity)>& accept)
    {
        LayoutUI();
        std::vector<entt::entity> order = GetUIDrawOrder();
        for (auto it = order.rbegin(); it != order.rend(); ++it)
        {
            const entt::entity e = *it;
            const bool draws = m_Registry.any_of<SpriteRendererComponent, UIImageComponent, CircleRendererComponent, TextComponent>(e) ||
                               m_Registry.any_of<UIButtonComponent, UISliderComponent, UIToggleComponent, UIProgressBarComponent, UIInputFieldComponent>(e) ||
                               m_Registry.any_of<UIScrollViewComponent>(e);
            if (!draws || !IsActiveInHierarchy({ e, this }))
                continue;
            const UILayoutNode& node = GetUINode(e);
            if (node.Alpha <= 0.001f || !node.HitRect.Contains(point / node.Scale) || (node.Clipped && !node.HitClip.Contains(point)))
                continue; // faded out, elsewhere, or hidden there by a mask
            if (accept && !accept({ e, this }))
                continue;
            return { e, this };
        }
        return {};
    }

    float Scene::GetUIScale(Entity entity)
    {
        if (!HasUITransform(entity, this))
            return 1.0f;
        LayoutUI();
        return GetUINode(entity).Scale;
    }

    UIRect Scene::GetUIParentRect(Entity entity)
    {
        if (!HasUITransform(entity, this))
            return {};
        LayoutUI();
        return GetUINode(entity).Parent;
    }

    bool Scene::IsUILayoutDriven(Entity entity)
    {
        if (!HasUITransform(entity, this))
            return false;
        LayoutUI();
        return GetUINode(entity).Driven;
    }

    void Scene::SetUIRect(Entity entity, const UIRect& rect)
    {
        if (!HasUITransform(entity, this))
            return;
        LayoutUI();
        entity.GetComponent<UITransformComponent>().SetRect(GetUINode(entity).Parent, rect);
    }

    void Scene::SetUIAnchors(Entity entity, const glm::vec2& anchorMin, const glm::vec2& anchorMax, bool keepRect)
    {
        if (!HasUITransform(entity, this))
            return;
        LayoutUI();
        const UILayoutNode node = GetUINode(entity);
        auto& t = entity.GetComponent<UITransformComponent>();
        t.AnchorMin = glm::clamp(glm::min(anchorMin, anchorMax), glm::vec2(0.0f), glm::vec2(1.0f));
        t.AnchorMax = glm::clamp(glm::max(anchorMin, anchorMax), glm::vec2(0.0f), glm::vec2(1.0f));
        if (keepRect)
            t.SetRect(node.Parent, node.Rect);
    }

    void Scene::SetUIPivot(Entity entity, const glm::vec2& pivot, bool keepRect)
    {
        if (!HasUITransform(entity, this))
            return;
        LayoutUI();
        const UILayoutNode node = GetUINode(entity);
        auto& t = entity.GetComponent<UITransformComponent>();
        t.Pivot = pivot;
        if (keepRect)
            t.SetRect(node.Parent, node.Rect);
    }

    std::vector<entt::entity> Scene::GetUIDrawOrder(const std::unordered_set<entt::entity>* skip)
    {
        auto view = m_Registry.view<UITransformComponent>();
        std::vector<entt::entity> roots;
        for (auto e : view)
            if (GetUIParent(e) == entt::null)
                roots.push_back(e);
        std::reverse(roots.begin(), roots.end()); // views iterate newest-first; keep creation order for equal Order
        auto byOrder = [&](entt::entity a, entt::entity b) {
            return m_Registry.get<UITransformComponent>(a).Order < m_Registry.get<UITransformComponent>(b).Order;
        };
        std::stable_sort(roots.begin(), roots.end(), byOrder);

        // Depth-first: a parent, then its children (by Order, then hierarchy order) above it.
        std::vector<entt::entity> order, stack(roots.rbegin(), roots.rend()), children;
        order.reserve(view.size());
        while (!stack.empty())
        {
            entt::entity e = stack.back();
            stack.pop_back();
            if (!skip || !skip->contains(e))
                order.push_back(e);
            GetUIChildren(e, children);
            std::stable_sort(children.begin(), children.end(), byOrder);
            stack.insert(stack.end(), children.rbegin(), children.rend());
        }
        return order;
    }

    // Whether the mouse stops at the entity (an interactable widget, or an image that catches it).
    static bool TakesMouse(entt::registry& registry, entt::entity e)
    {
        if (auto* b = registry.try_get<UIButtonComponent>(e); b && b->Interactable) return true;
        if (auto* s = registry.try_get<UISliderComponent>(e); s && s->Interactable) return true;
        if (auto* t = registry.try_get<UIToggleComponent>(e); t && t->Interactable) return true;
        if (auto* f = registry.try_get<UIInputFieldComponent>(e); f && f->Interactable) return true;
        if (auto* i = registry.try_get<UIImageComponent>(e); i && i->RaycastTarget) return true;
        if (registry.all_of<UIScrollViewComponent>(e)) return true; // its view: wheel and drags (and nothing behind it is clicked)
        return false;
    }

    Entity Scene::PickUI(const glm::vec2& point)
    {
        LayoutUI();
        return { PickUILaidOut(point), this };
    }

    entt::entity Scene::PickUILaidOut(const glm::vec2& point)
    {
        std::vector<entt::entity> order = GetUIDrawOrder();
        // Scrollbars are drawn over their scroll view's content: a scroll view with one under the
        // point is found before what is in it.
        std::vector<entt::entity> bars;
        for (auto e : m_Registry.view<UIScrollViewComponent>())
            if (m_Registry.all_of<UITransformComponent>(e) && IsActiveInHierarchy({ e, this }) && ScrollbarAt(e, point) >= 0)
                bars.push_back(e);
        // The topmost first: the last one drawn.
        for (auto it = order.rbegin(); it != order.rend(); ++it)
        {
            entt::entity e = *it;
            if (!TakesMouse(m_Registry, e) || !IsActiveInHierarchy({ e, this }))
                continue; // inactive UI is not drawn and cannot be hovered: the mouse reaches what is below
            const UILayoutNode& node = GetUINode(e);
            // Where it is drawn; faded out: not there; hidden there by a mask: not there either.
            if (node.Alpha <= 0.001f || !node.HitRect.Contains(point / node.Scale) || (node.Clipped && !node.HitClip.Contains(point)))
                continue;
            if (!bars.empty())
                for (entt::entity a = GetUIParent(e); a != entt::null; a = GetUIParent(a))
                    if (std::find(bars.begin(), bars.end(), a) != bars.end())
                        return a;
            return e;
        }
        return entt::null;
    }

    bool Scene::GetUIClipRect(Entity entity, UIRect& clip)
    {
        if (!HasUITransform(entity, this))
            return false;
        LayoutUI();
        const UILayoutNode& node = GetUINode(entity);
        if (!node.Clipped)
            return false;
        clip = node.HitClip;
        return true;
    }

    bool Scene::IsUIClippedAway(entt::entity e)
    {
        const UILayoutNode& node = GetUINode(e);
        if (!node.Clipped)
            return false;
        const UIRect px{ node.HitRect.Min * node.Scale, node.HitRect.Max * node.Scale };
        return px.Intersect(node.HitClip).IsEmpty();
    }

    bool Scene::HasScrollViewAncestor(entt::entity e)
    {
        for (entt::entity a = GetUIParent(e); a != entt::null; a = GetUIParent(a))
            if (m_Registry.all_of<UIScrollViewComponent>(a))
                return true;
        return false;
    }

    Entity Scene::GetHoveredUI()
    {
        return m_HoveredUI != entt::null && m_Registry.valid(m_HoveredUI) ? Entity{ m_HoveredUI, this } : Entity{};
    }

    // ------------------------------------------------------------------ input field text layout
    float Scene::GetUITextSize(const UITransformComponent& t, const UIRect& rect, bool toggleLabel)
    {
        return std::max(1.0f, t.FontSize > 0.0f ? t.FontSize : rect.GetHeight() * (toggleLabel ? 0.55f : 1.0f));
    }

    // The font size (px) of a widget's own text: the UI Transform's, else `share` of its height.
    static float WidgetFontSize(const UITransformComponent& t, const UIRect& rect, float share)
    {
        return std::max(1.0f, t.FontSize > 0.0f ? t.FontSize : rect.GetHeight() * share);
    }

    static const Font* LoadedFont(const Ref<Font>& font) { return font && font->IsLoaded() ? font.get() : nullptr; }

    // The x (px from the text's start) of every caret position of `display`; without a loaded font
    // (headless) every character counts as 0.55 of the size.
    static void LayoutCaretOffsets(const std::string& display, const Font* font, float px, std::vector<float>& offsets)
    {
        offsets.clear();
        offsets.push_back(0.0f);
        float x = 0.0f;
        size_t i = 0;
        while (i < display.size())
        {
            uint32_t cp = DecodeUTF8(display, i);
            if (cp == '\t') cp = ' ';
            float advance = 0.55f;
            if (font)
            {
                const Glyph* g = font->GetGlyph(cp);
                advance = g ? g->Advance : 0.0f;
            }
            x += advance * px;
            offsets.push_back(x);
        }
    }

    // The area the text of an input field is drawn in.
    static UIRect InputTextArea(const UIInputFieldComponent& field, const UIRect& rect)
    {
        return rect.Inset(field.Padding, field.Padding, 0.0f, 0.0f);
    }

    void Scene::ScrollInputField(entt::entity e)
    {
        auto& field = m_Registry.get<UIInputFieldComponent>(e);
        const auto& t = m_Registry.get<UITransformComponent>(e);
        const UIRect rect = GetUINode(e).Rect;
        const UIRect area = InputTextArea(field, rect);
        std::vector<float> offsets;
        LayoutCaretOffsets(TextEdit::GetDisplayText(field.Text, field.ContentType), LoadedFont(field.FontAsset),
                           WidgetFontSize(t, rect, 0.5f), offsets);
        const size_t caret = std::min(TextEdit::CodepointIndexOf(field.Text, field.Caret), offsets.size() - 1);
        field.Scroll = TextEdit::ScrollToCaret(field.Scroll, offsets[caret], offsets.back(), area.GetWidth(), 2.0f);
    }

    // The caret position (bytes) under a view point.
    static size_t InputCaretAt(const UIInputFieldComponent& field, const UITransformComponent& t, const UIRect& rect, const glm::vec2& point)
    {
        const UIRect area = InputTextArea(field, rect);
        std::vector<float> offsets;
        LayoutCaretOffsets(TextEdit::GetDisplayText(field.Text, field.ContentType), LoadedFont(field.FontAsset), WidgetFontSize(t, rect, 0.5f),
                           offsets);
        const size_t index = TextEdit::CaretIndexAt(offsets, point.x - (area.Min.x - field.Scroll));
        return TextEdit::ByteOffsetOf(field.Text, index);
    }

    // ------------------------------------------------------------------ widget changes
    void Scene::DeliverUIChange(const UIChangeEvent& change)
    {
        if (!m_IsRunning || !m_Registry.valid(change.Entity))
            return;
        Entity entity{ change.Entity, this };
        // A button that went inactive (an earlier OnClick switched it off) is not clicked.
        if (change.Kind == UIChange::Click && !IsActiveInHierarchy(entity))
            return;
        UIPointerEvent pointer;
        const UIChange k = change.Kind;
        if (k == UIChange::PointerEnter || k == UIChange::PointerExit || k == UIChange::PointerDown || k == UIChange::PointerUp ||
            k == UIChange::PointerClick)
        {
            pointer.Target = m_Registry.valid(change.Target) ? Entity{ change.Target, this } : Entity{};
            const auto it = m_UILayout.find(change.Entity);
            pointer.Position = change.Screen / (it != m_UILayout.end() ? it->second.Scale : 1.0f);
            pointer.ScreenPosition = change.Screen;
            pointer.Inside = change.On;
        }
        // Like OnClick, the callbacks also reach disabled scripts.
        if (m_ScriptsRunning)
            if (auto* nsc = m_Registry.try_get<NativeScriptComponent>(change.Entity); nsc && nsc->Instance)
            {
                ScriptableEntity* script = nsc->Instance;
                switch (change.Kind)
                {
                    case UIChange::Slider:        script->OnValueChanged(change.Value); break;
                    case UIChange::Toggle:        script->OnToggleChanged(change.On); break;
                    case UIChange::TextChanged:   script->OnTextChanged(change.Text); break;
                    case UIChange::TextSubmitted: script->OnTextSubmit(change.Text); break;
                    case UIChange::TextEndEdit:   script->OnTextEndEdit(change.Text); break;
                    case UIChange::PointerEnter:  script->OnPointerEnter(pointer); break;
                    case UIChange::PointerExit:   script->OnPointerExit(pointer); break;
                    case UIChange::PointerDown:   script->OnPointerDown(pointer); break;
                    case UIChange::PointerUp:     script->OnPointerUp(pointer); break;
                    case UIChange::PointerClick:  script->OnPointerClick(pointer); break;
                    case UIChange::Click:         script->OnClick(); break;
                    case UIChange::Select:        script->OnSelect(); break;
                    case UIChange::Deselect:      script->OnDeselect(); break;
                    case UIChange::Cancel:        script->OnCancel(); break;
                    case UIChange::Scroll:        script->OnScrollChanged(change.Screen); break;
                }
            }
        if (!m_Registry.valid(change.Entity))
            return; // destroyed by its callback
        if (m_EventBus)
            switch (change.Kind)
            {
                case UIChange::Slider:        m_EventBus->Emit("UISliderChanged", change.Value, entity); break;
                case UIChange::Toggle:        m_EventBus->Emit("UIToggleChanged", change.On, entity); break;
                case UIChange::TextChanged:   m_EventBus->Emit("UITextChanged", change.Text, entity); break;
                case UIChange::TextSubmitted: m_EventBus->Emit("UITextSubmitted", change.Text, entity); break;
                case UIChange::TextEndEdit:   m_EventBus->Emit("UITextEndEdit", change.Text, entity); break;
                case UIChange::PointerEnter:  m_EventBus->Emit("UIPointerEnter", pointer, entity); break;
                case UIChange::PointerExit:   m_EventBus->Emit("UIPointerExit", pointer, entity); break;
                case UIChange::PointerDown:   m_EventBus->Emit("UIPointerDown", pointer, entity); break;
                case UIChange::PointerUp:     m_EventBus->Emit("UIPointerUp", pointer, entity); break;
                case UIChange::PointerClick:  m_EventBus->Emit("UIPointerClick", pointer, entity); break;
                case UIChange::Click:         m_EventBus->Emit("UIClick", {}, entity); break;
                case UIChange::Select:        m_EventBus->Emit("UISelected", {}, entity); break;
                case UIChange::Deselect:      m_EventBus->Emit("UIDeselected", {}, entity); break;
                case UIChange::Cancel:        m_EventBus->Emit("UICancel", {}, entity); break;
                case UIChange::Scroll:        m_EventBus->Emit("UIScrollChanged", change.Screen, entity); break;
            }
        // A button's click message: to its target, else to every running script.
        if (change.Kind == UIChange::Click && m_Registry.valid(change.Entity))
            if (const auto* button = m_Registry.try_get<UIButtonComponent>(change.Entity); button && !button->ClickMessage.empty())
            {
                ScriptMessage message{ button->ClickMessage, entity, {} };
                if (button->ClickTarget)
                {
                    if (Entity target = GetEntityByUUID(button->ClickTarget.ID))
                        SendMessage(target, message);
                }
                else
                    Broadcast(message);
            }
    }

    void Scene::DeliverUIChanges(std::vector<UIChangeEvent>& changes)
    {
        for (const UIChangeEvent& change : changes)
            DeliverUIChange(change);
        changes.clear();
    }

    bool Scene::ApplyToggle(entt::entity e, bool isOn, std::vector<UIChangeEvent>* changes)
    {
        auto& toggle = m_Registry.get<UIToggleComponent>(e);
        if (toggle.IsOn == isOn)
            return false;
        if (isOn && !toggle.Group.empty())
        {
            // Radio buttons: the others of the group switch off first.
            std::vector<entt::entity> others;
            for (auto other : m_Registry.view<UIToggleComponent>())
                if (other != e)
                    others.push_back(other);
            SortInSceneOrder(others);
            for (auto other : others)
            {
                auto& o = m_Registry.get<UIToggleComponent>(other);
                if (o.IsOn && o.Group == toggle.Group)
                {
                    o.IsOn = false;
                    o.ValueChanged = true;
                    if (changes)
                        changes->push_back({ other, UIChange::Toggle, 0.0f, false, {} });
                }
            }
        }
        toggle.IsOn = isOn;
        toggle.ValueChanged = true;
        if (changes)
            changes->push_back({ e, UIChange::Toggle, isOn ? 1.0f : 0.0f, isOn, {} });
        return true;
    }

    bool Scene::SetSliderValue(Entity entity, float value, bool notify)
    {
        if (!entity || entity.GetScene() != this)
            return false;
        auto* slider = entity.TryGetComponent<UISliderComponent>();
        if (!slider)
            return false;
        value = UIMath::ClampValue(value, slider->MinValue, slider->MaxValue, slider->WholeNumbers);
        if (value == slider->Value)
            return false;
        slider->Value = value;
        if (notify)
        {
            slider->ValueChanged = true;
            DeliverUIChange({ entity, UIChange::Slider, value, false, {} });
        }
        return true;
    }

    bool Scene::SetToggle(Entity entity, bool isOn, bool notify)
    {
        if (!entity || entity.GetScene() != this || !entity.HasComponent<UIToggleComponent>())
            return false;
        std::vector<UIChangeEvent> changes;
        if (!ApplyToggle(entity, isOn, &changes))
            return false;
        if (notify)
            DeliverUIChanges(changes);
        return true;
    }

    bool Scene::SetInputText(Entity entity, const std::string& text, bool notify)
    {
        if (!entity || entity.GetScene() != this)
            return false;
        auto* field = entity.TryGetComponent<UIInputFieldComponent>();
        if (!field)
            return false;
        std::string filtered = TextEdit::Filter(text, field->ContentType, field->CharacterLimit);
        if (filtered == field->Text)
            return false;
        field->Text = std::move(filtered);
        TextEditState state{ field->Text, field->Caret, field->Anchor };
        TextEdit::Clamp(state);
        field->Caret = state.Caret;
        field->Anchor = state.Anchor;
        if (field->Focused)
            ScrollInputField(entity);
        if (notify)
        {
            field->TextChanged = true;
            DeliverUIChange({ entity, UIChange::TextChanged, 0.0f, false, field->Text });
        }
        return true;
    }

    void Scene::BeginInputEdit(entt::entity e)
    {
        auto& field = m_Registry.get<UIInputFieldComponent>(e);
        m_FocusedInput = e;
        field.Focused = true;
        field.TextBeforeEdit = field.Text;
        field.Caret = field.Text.size();
        field.Anchor = field.SelectAllOnFocus ? 0 : field.Caret;
        field.BlinkTime = 0.0f;
        field.Selecting = false;
        ScrollInputField(e);
    }

    void Scene::EndInputEdit(std::vector<UIChangeEvent>* changes)
    {
        entt::entity e = m_FocusedInput;
        m_FocusedInput = entt::null;
        if (e == entt::null || !m_Registry.valid(e))
            return;
        auto* field = m_Registry.try_get<UIInputFieldComponent>(e);
        if (!field)
            return;
        field->Focused = false;
        field->Selecting = false;
        field->Anchor = field->Caret; // the selection goes
        field->Scroll = 0.0f;         // the start of the text shows again
        if (changes)
            changes->push_back({ e, UIChange::TextEndEdit, 0.0f, false, field->Text });
    }

    void Scene::FocusInputField(Entity entity)
    {
        std::vector<UIChangeEvent> changes;
        const bool valid = entity && entity.GetScene() == this && entity.HasComponent<UIInputFieldComponent>();
        if (valid && m_FocusedInput == (entt::entity)entity)
            return;
        if (m_FocusedInput != entt::null)
            EndInputEdit(&changes);
        if (valid)
            BeginInputEdit(entity);
        DeliverUIChanges(changes);
    }

    Entity Scene::GetFocusedInputField()
    {
        return m_FocusedInput != entt::null && m_Registry.valid(m_FocusedInput) ? Entity{ m_FocusedInput, this } : Entity{};
    }

    // ------------------------------------------------------------------ keyboard (the input field being edited)
    void Scene::ProcessTextInput(entt::entity e, std::vector<UIChangeEvent>& changes)
    {
        auto& field = m_Registry.get<UIInputFieldComponent>(e);
        const std::vector<TextInputEvent>& events = Input::GetTextInput();
        if (events.empty())
            return;
        TextEditState state{ field.Text, field.Caret, field.Anchor };
        TextEdit::Clamp(state);
        const bool editable = !field.ReadOnly;
        const bool secret = field.ContentType == UIContentType::Password; // never copied out
        bool changed = false, submit = false, cancel = false, any = false;
        for (const TextInputEvent& ev : events)
        {
            if (ev.IsCharacter())
            {
                // Ctrl + key are shortcuts (AltGr = Ctrl + Alt still types on some layouts).
                if (ev.Control && !ev.Alt)
                    continue;
                if (editable)
                    changed |= TextEdit::Insert(state, TextEdit::ToUTF8(ev.Codepoint), field.ContentType, field.CharacterLimit);
                any = true;
                continue;
            }
            const bool word = ev.Control || ev.Alt;
            switch (ev.KeyCode)
            {
                case Key::Left:  TextEdit::MoveLeft(state, ev.Shift, word); any = true; break;
                case Key::Right: TextEdit::MoveRight(state, ev.Shift, word); any = true; break;
                case Key::Up:
                case Key::Home:  TextEdit::MoveHome(state, ev.Shift); any = true; break;
                case Key::Down:
                case Key::End:   TextEdit::MoveEnd(state, ev.Shift); any = true; break;
                case Key::Backspace:
                    if (editable) changed |= TextEdit::Backspace(state, ev.Control);
                    any = true;
                    break;
                case Key::Delete:
                    if (editable) changed |= TextEdit::Delete(state, ev.Control);
                    any = true;
                    break;
                case Key::Enter:
                case Key::KPEnter: submit = true; break;
                case Key::Escape:  cancel = true; break;
                case Key::A:
                    if (ev.Control) { TextEdit::SelectAll(state); any = true; }
                    break;
                case Key::C:
                    if (ev.Control && state.HasSelection() && !secret)
                        Input::SetClipboardText(TextEdit::GetSelectedText(state));
                    break;
                case Key::X:
                    if (ev.Control && state.HasSelection() && !secret && editable)
                    {
                        Input::SetClipboardText(TextEdit::GetSelectedText(state));
                        changed |= TextEdit::DeleteSelection(state);
                        any = true;
                    }
                    break;
                case Key::V:
                    if (ev.Control && editable)
                    {
                        changed |= TextEdit::Insert(state, Input::GetClipboardText(), field.ContentType, field.CharacterLimit);
                        any = true;
                    }
                    break;
                default: break;
            }
            if (submit || cancel)
                break; // editing ends: what came after it in the frame is not for this field
        }
        field.Text = std::move(state.Text);
        field.Caret = state.Caret;
        field.Anchor = state.Anchor;
        if (any)
            field.BlinkTime = 0.0f; // the caret shows while typing
        if (changed)
        {
            field.TextChanged = true;
            changes.push_back({ e, UIChange::TextChanged, 0.0f, false, field.Text });
        }
        if (cancel && field.Text != field.TextBeforeEdit)
        {
            // Escape: the text it had when editing began.
            field.Text = field.TextBeforeEdit;
            field.TextChanged = true;
            changes.push_back({ e, UIChange::TextChanged, 0.0f, false, field.Text });
        }
        if (submit)
        {
            field.Submitted = true;
            changes.push_back({ e, UIChange::TextSubmitted, 0.0f, false, field.Text });
        }
        if (submit || cancel)
            EndInputEdit(&changes);
        else
            ScrollInputField(e);
    }

    void Scene::ResetUIRuntime()
    {
        m_PressedUI = m_FocusedInput = m_LastUIClickEntity = entt::null;
        m_HoveredUI = m_SelectedUI = m_UISubmitFlash = entt::null;
        m_UIHoverChain.clear();
        m_UISelectionHighlight = false;
        m_UINavHeld = -1;
        m_UINavRepeat = m_UISubmitFlashTime = 0.0f;
        m_LastUIClickTime = -1.0f;
        m_MouseWasDown = m_MouseDown;
        m_ScrollPress = entt::null;
        m_ScrollDragging = false;
        m_ViewportScroll = glm::vec2(0.0f);
        for (auto e : m_Registry.view<UIScrollViewComponent>())
            m_Registry.get<UIScrollViewComponent>(e).ResetRuntime();
        for (auto e : m_Registry.view<UIButtonComponent>())
        {
            auto& b = m_Registry.get<UIButtonComponent>(e);
            b.Hovered = b.Pressed = b.Clicked = false;
        }
        for (auto e : m_Registry.view<UISliderComponent>())
        {
            auto& s = m_Registry.get<UISliderComponent>(e);
            s.Hovered = s.Dragging = s.ValueChanged = false;
        }
        for (auto e : m_Registry.view<UIToggleComponent>())
        {
            auto& t = m_Registry.get<UIToggleComponent>(e);
            t.Hovered = t.Pressed = t.ValueChanged = false;
            t.Transition = t.IsOn ? 1.0f : 0.0f;
        }
        for (auto e : m_Registry.view<UIProgressBarComponent>())
        {
            auto& p = m_Registry.get<UIProgressBarComponent>(e);
            p.DisplayValue = std::clamp(p.Value, 0.0f, 1.0f);
        }
        for (auto e : m_Registry.view<UIInputFieldComponent>())
            m_Registry.get<UIInputFieldComponent>(e).ResetRuntime();
        for (auto e : m_Registry.view<UINavigationComponent>())
            m_Registry.get<UINavigationComponent>(e).WasActive = false; // SelectOnEnable: active at the start counts as switched on
    }

    // ------------------------------------------------------------------ keyboard / gamepad navigation
    bool Scene::IsUIWidget(entt::entity e)
    {
        if (!m_Registry.valid(e) || !m_Registry.all_of<UITransformComponent>(e))
            return false;
        if (auto* b = m_Registry.try_get<UIButtonComponent>(e); b && b->Interactable) return true;
        if (auto* s = m_Registry.try_get<UISliderComponent>(e); s && s->Interactable) return true;
        if (auto* t = m_Registry.try_get<UIToggleComponent>(e); t && t->Interactable) return true;
        if (auto* f = m_Registry.try_get<UIInputFieldComponent>(e); f && f->Interactable) return true;
        return false;
    }

    bool Scene::IsUISelectable(Entity entity)
    {
        if (!entity || entity.GetScene() != this || !IsUIWidget(entity) || !IsActiveInHierarchy(entity))
            return false;
        const auto* nav = entity.TryGetComponent<UINavigationComponent>();
        return !nav || nav->NavigationMode != UINavigationComponent::Mode::None;
    }

    void Scene::SelectUI(entt::entity e, bool highlight, std::vector<UIChangeEvent>& changes)
    {
        if (e != entt::null && (!IsUIWidget(e) || !IsActiveInHierarchy({ e, this })))
            e = entt::null;
        m_UISelectionHighlight = highlight && e != entt::null;
        if (e == m_SelectedUI)
            return;
        if (m_SelectedUI != entt::null && m_Registry.valid(m_SelectedUI))
            changes.push_back({ m_SelectedUI, UIChange::Deselect });
        m_SelectedUI = e;
        if (e != entt::null)
        {
            changes.push_back({ e, UIChange::Select });
            // Chosen by keys / gamepad: the scroll views it is in show it.
            if (highlight)
                RevealInScrollViews(e, true, 8.0f);
        }
    }

    Entity Scene::GetSelectedUI()
    {
        return m_SelectedUI != entt::null && m_Registry.valid(m_SelectedUI) ? Entity{ m_SelectedUI, this } : Entity{};
    }

    void Scene::SetSelectedUI(Entity widget, bool highlight)
    {
        std::vector<UIChangeEvent> changes;
        const bool valid = widget && widget.GetScene() == this;
        SelectUI(valid ? (entt::entity)widget : entt::null, highlight, changes);
        DeliverUIChanges(changes);
    }

    Entity Scene::FindUINavigationTarget(Entity from, UINavDirection direction)
    {
        if (!from || from.GetScene() != this || !IsUIWidget(from))
            return {};
        using Mode = UINavigationComponent::Mode;
        const auto* nav = from.TryGetComponent<UINavigationComponent>();
        const Mode mode = nav ? nav->NavigationMode : Mode::Automatic;
        const bool horizontal = direction == UINavDirection::Left || direction == UINavDirection::Right;
        if (mode == Mode::None || (mode == Mode::Horizontal && !horizontal) || (mode == Mode::Vertical && horizontal))
            return {};
        if (mode == Mode::Explicit)
        {
            const EntityRef ref = direction == UINavDirection::Up ? nav->Up : direction == UINavDirection::Down ? nav->Down
                                : direction == UINavDirection::Left ? nav->Left : nav->Right;
            Entity target = ref ? GetEntityByUUID(ref.ID) : Entity{};
            return IsUISelectable(target) ? target : Entity{};
        }
        // Automatic: the nearest selectable widget in that direction (on the screen: canvases may differ).
        auto screenRect = [&](entt::entity e) {
            const UILayoutNode& node = GetUINode(e);
            return UIRect{ node.Rect.Min * node.Scale, node.Rect.Max * node.Scale };
        };
        std::vector<entt::entity> candidates;
        std::vector<UIRect> rects;
        for (auto e : GetUIDrawOrder())
            if (e != (entt::entity)from && IsUISelectable({ e, this }) && (!IsUIClippedAway(e) || HasScrollViewAncestor(e)))
            {
                candidates.push_back(e);
                rects.push_back(screenRect(e));
            }
        const int index = UILayout::FindInDirection(screenRect(from), direction, rects);
        return index >= 0 ? Entity{ candidates[index], this } : Entity{};
    }

    bool Scene::StepSlider(entt::entity e, UINavDirection direction, std::vector<UIChangeEvent>& changes)
    {
        auto& s = m_Registry.get<UISliderComponent>(e);
        // +1: towards the maximum's end of the track.
        int sign = 0;
        switch (s.Direction)
        {
            case UIDirection::LeftToRight: sign = direction == UINavDirection::Right ? 1 : direction == UINavDirection::Left ? -1 : 0; break;
            case UIDirection::RightToLeft: sign = direction == UINavDirection::Left ? 1 : direction == UINavDirection::Right ? -1 : 0; break;
            case UIDirection::BottomToTop: sign = direction == UINavDirection::Up ? 1 : direction == UINavDirection::Down ? -1 : 0; break;
            case UIDirection::TopToBottom: sign = direction == UINavDirection::Down ? 1 : direction == UINavDirection::Up ? -1 : 0; break;
        }
        if (sign == 0)
            return false;
        const float range = s.MaxValue - s.MinValue;
        const float step = s.WholeNumbers ? (range >= 0.0f ? 1.0f : -1.0f) : range * 0.1f; // a tenth of the range (whole numbers: 1)
        const float value = UIMath::ClampValue(s.Value + step * (float)sign, s.MinValue, s.MaxValue, s.WholeNumbers);
        if (value != s.Value)
        {
            s.Value = value;
            s.ValueChanged = true;
            changes.push_back({ e, UIChange::Slider, value });
        }
        return true; // the key is the slider's, also at its end
    }

    bool Scene::MoveUISelection(UINavDirection direction, std::vector<UIChangeEvent>& changes)
    {
        if (m_SelectedUI == entt::null || !IsUISelectable({ m_SelectedUI, this }))
            return false;
        // A slider takes the keys along its track.
        if (auto* s = m_Registry.try_get<UISliderComponent>(m_SelectedUI); s && s->Interactable)
        {
            const bool horizontalKey = direction == UINavDirection::Left || direction == UINavDirection::Right;
            if (UIMath::IsHorizontal(s->Direction) == horizontalKey)
            {
                m_UISelectionHighlight = true;
                return StepSlider(m_SelectedUI, direction, changes);
            }
        }
        // Nowhere to go: nothing changes (a lone HUD button clicked with the mouse does not light up
        // while the arrows move the player).
        Entity target = FindUINavigationTarget({ m_SelectedUI, this }, direction);
        if (!target)
            return false;
        SelectUI(target, true, changes);
        return true;
    }

    void Scene::TabUISelection(bool backwards, std::vector<UIChangeEvent>& changes)
    {
        std::vector<entt::entity> order;
        for (auto e : GetUIDrawOrder())
            if (IsUISelectable({ e, this }) && (!IsUIClippedAway(e) || HasScrollViewAncestor(e))) // hidden by a mask: not reached (in a scroll view: scrolled to)
                order.push_back(e);
        if (order.empty())
            return;
        auto it = std::find(order.begin(), order.end(), m_SelectedUI);
        size_t index = 0;
        if (it == order.end())
            index = backwards ? order.size() - 1 : 0;
        else
        {
            const size_t current = (size_t)(it - order.begin());
            index = backwards ? (current + order.size() - 1) % order.size() : (current + 1) % order.size();
        }
        const entt::entity next = order[index];
        SelectUI(next, true, changes);
        // Into an input field: typing goes to it (as in a form).
        if (auto* f = m_Registry.try_get<UIInputFieldComponent>(next); f && f->Interactable && m_FocusedInput != next)
        {
            if (m_FocusedInput != entt::null)
                EndInputEdit(&changes);
            BeginInputEdit(next);
        }
    }

    bool Scene::SubmitWidget(entt::entity e, std::vector<UIChangeEvent>& changes)
    {
        bool any = false;
        if (auto* b = m_Registry.try_get<UIButtonComponent>(e); b && b->Interactable)
        {
            b->Clicked = true;
            changes.push_back({ e, UIChange::Click });
            m_UISubmitFlash = e; // looks pressed for a moment
            m_UISubmitFlashTime = 0.12f;
            any = true;
        }
        if (auto* t = m_Registry.try_get<UIToggleComponent>(e); t && t->Interactable)
        {
            // A radio button that is on stays on (unless the group allows none).
            if (!(t->IsOn && !t->Group.empty() && !t->AllowSwitchOff))
                ApplyToggle(e, !t->IsOn, &changes);
            any = true;
        }
        if (auto* f = m_Registry.try_get<UIInputFieldComponent>(e); f && f->Interactable)
        {
            if (m_FocusedInput != e)
            {
                if (m_FocusedInput != entt::null)
                    EndInputEdit(&changes);
                BeginInputEdit(e); // its keys from the next frame on: the Enter that started it is not typed into it
            }
            any = true;
        }
        return any;
    }

    bool Scene::NavigateUI(UINavDirection direction)
    {
        std::vector<UIChangeEvent> changes;
        LayoutUI();
        const bool moved = MoveUISelection(direction, changes);
        DeliverUIChanges(changes);
        return moved;
    }

    bool Scene::SubmitUI()
    {
        if (m_SelectedUI == entt::null || !IsUISelectable({ m_SelectedUI, this }))
            return false;
        std::vector<UIChangeEvent> changes;
        const bool done = SubmitWidget(m_SelectedUI, changes);
        DeliverUIChanges(changes);
        return done;
    }

    bool Scene::CancelUI()
    {
        if (m_SelectedUI == entt::null || !m_Registry.valid(m_SelectedUI))
            return false;
        DeliverUIChange({ m_SelectedUI, UIChange::Cancel });
        return true;
    }

    void Scene::ProcessUINavigation(float dt, bool editing, std::vector<UIChangeEvent>& changes)
    {
        // An input field edited at the start of the frame kept its keys (arrows, Enter, Escape -
        // also those that ended its editing); Tab still moves on.
        if (m_UIKeyboardEnabled)
        {
            for (const TextInputEvent& ev : Input::GetTextInput())
            {
                if (ev.IsCharacter())
                    continue;
                if (ev.KeyCode == Key::Tab && !ev.Control && !ev.Alt)
                {
                    if (m_FocusedInput != entt::null)
                        EndInputEdit(&changes);
                    TabUISelection(ev.Shift, changes);
                    continue;
                }
                if (editing || m_FocusedInput != entt::null || m_SelectedUI == entt::null)
                    continue;
                // Arrows with the keyboard's auto-repeat.
                switch (ev.KeyCode)
                {
                    case Key::Up:    MoveUISelection(UINavDirection::Up, changes); break;
                    case Key::Down:  MoveUISelection(UINavDirection::Down, changes); break;
                    case Key::Left:  MoveUISelection(UINavDirection::Left, changes); break;
                    case Key::Right: MoveUISelection(UINavDirection::Right, changes); break;
                    default: break;
                }
            }
        }
        const bool keySubmit = m_UIKeyboardEnabled && (Input::IsKeyJustPressed(Key::Enter) || Input::IsKeyJustPressed(Key::KPEnter));
        const bool keyCancel = m_UIKeyboardEnabled && Input::IsKeyJustPressed(Key::Escape);

        // Gamepads: the D-pad or the left stick (held: repeats), A submits, B cancels.
        int held = -1;
        bool padSubmit = false, padCancel = false;
        for (int id = 0; id < Input::MaxGamepads; id++)
        {
            if (!Input::IsGamepadConnected(id))
                continue;
            padSubmit |= Input::IsGamepadButtonJustPressed(GamepadButton::A, id);
            padCancel |= Input::IsGamepadButtonJustPressed(GamepadButton::B, id);
            if (held >= 0)
                continue;
            const float x = Input::GetGamepadAxis(GamepadAxis::LeftX, id), y = Input::GetGamepadAxis(GamepadAxis::LeftY, id);
            if (Input::IsGamepadButtonPressed(GamepadButton::DPadUp, id) || y < -0.5f) held = (int)UINavDirection::Up; // stick up is -1
            else if (Input::IsGamepadButtonPressed(GamepadButton::DPadDown, id) || y > 0.5f) held = (int)UINavDirection::Down;
            else if (Input::IsGamepadButtonPressed(GamepadButton::DPadLeft, id) || x < -0.5f) held = (int)UINavDirection::Left;
            else if (Input::IsGamepadButtonPressed(GamepadButton::DPadRight, id) || x > 0.5f) held = (int)UINavDirection::Right;
        }
        bool padMove = false;
        if (held != m_UINavHeld)
        {
            m_UINavHeld = held;
            m_UINavRepeat = 0.4f; // the first repeat waits a little longer
            padMove = held >= 0;
        }
        else if (held >= 0 && (m_UINavRepeat -= dt) <= 0.0f)
        {
            m_UINavRepeat = 0.12f;
            padMove = true;
        }
        if (editing || m_FocusedInput != entt::null)
            return;
        if (padMove && m_SelectedUI != entt::null)
            MoveUISelection((UINavDirection)held, changes);
        if ((keySubmit || padSubmit) && m_SelectedUI != entt::null && IsUISelectable({ m_SelectedUI, this }))
        {
            m_UISelectionHighlight = true;
            SubmitWidget(m_SelectedUI, changes);
        }
        else if ((keyCancel || padCancel) && m_SelectedUI != entt::null)
            changes.push_back({ m_SelectedUI, UIChange::Cancel });
    }

    // ------------------------------------------------------------------ update (every frame of the game, also while paused)
    void Scene::UpdateUI(Timestep ts)
    {
        const float dt = std::max(0.0f, (float)ts);
        m_UITime += dt;
        LayoutUI();
        const glm::vec2 viewport = GetViewportSize();
        const glm::vec2 mouse = { m_MousePosition.x, viewport.y - m_MousePosition.y }; // to bottom-left origin, view px
        // The mouse in an element's UI units, where its layout is (its look undone: a scaled /
        // moved widget is used where it is drawn).
        auto local = [&](entt::entity e) {
            const UILayoutNode& node = GetUINode(e);
            return node.ToLayout(mouse / node.Scale);
        };
        const bool pressedNow = m_MouseDown && !m_MouseWasDown;
        const bool released = !m_MouseDown && m_MouseWasDown;
        m_MouseWasDown = m_MouseDown;
        if (m_UISubmitFlashTime > 0.0f && (m_UISubmitFlashTime -= dt) <= 0.0f)
            m_UISubmitFlash = entt::null;

        // A widget that went inactive (or lost its component) while held down is not clicked when it comes back.
        if (m_PressedUI != entt::null && (!m_Registry.valid(m_PressedUI) || !IsActiveInHierarchy({ m_PressedUI, this })))
            m_PressedUI = entt::null;
        std::vector<UIChangeEvent> changes;
        if (m_FocusedInput != entt::null)
        {
            const bool alive = m_Registry.valid(m_FocusedInput) && m_Registry.all_of<UIInputFieldComponent, UITransformComponent>(m_FocusedInput);
            if (!alive)
                m_FocusedInput = entt::null;
            else if (!IsActiveInHierarchy({ m_FocusedInput, this }) || !m_Registry.get<UIInputFieldComponent>(m_FocusedInput).Interactable)
                EndInputEdit(&changes);
        }
        // The selected widget went away / inactive / not interactable: nothing is selected.
        if (m_SelectedUI != entt::null && (!m_Registry.valid(m_SelectedUI) || !IsUIWidget(m_SelectedUI) || !IsActiveInHierarchy({ m_SelectedUI, this })))
        {
            if (!m_Registry.valid(m_SelectedUI))
                m_SelectedUI = entt::null;
            else
                SelectUI(entt::null, false, changes);
        }
        // SelectOnEnable: a widget that became active (the first one in scene order wins).
        {
            std::vector<entt::entity> enabled;
            for (auto e : m_Registry.view<UINavigationComponent>())
            {
                auto& nav = m_Registry.get<UINavigationComponent>(e);
                const bool active = IsUIWidget(e) && IsActiveInHierarchy({ e, this });
                if (active && !nav.WasActive && nav.SelectOnEnable)
                    enabled.push_back(e);
                nav.WasActive = active;
            }
            if (!enabled.empty())
            {
                SortInSceneOrder(enabled);
                SelectUI(enabled.front(), true, changes);
            }
        }

        // One-frame flags and the hover state start over.
        for (auto e : m_Registry.view<UIButtonComponent>())
        {
            auto& b = m_Registry.get<UIButtonComponent>(e);
            b.Clicked = b.Hovered = false;
        }
        for (auto e : m_Registry.view<UISliderComponent>())
        {
            auto& s = m_Registry.get<UISliderComponent>(e);
            s.ValueChanged = s.Hovered = false;
        }
        for (auto e : m_Registry.view<UIToggleComponent>())
        {
            auto& t = m_Registry.get<UIToggleComponent>(e);
            t.ValueChanged = t.Hovered = false;
        }
        for (auto e : m_Registry.view<UIInputFieldComponent>())
        {
            auto& f = m_Registry.get<UIInputFieldComponent>(e);
            f.TextChanged = f.Submitted = f.Hovered = false;
        }

        // ---- the mouse
        const entt::entity hovered = PickUILaidOut(mouse);
        if (hovered != entt::null)
        {
            if (auto* b = m_Registry.try_get<UIButtonComponent>(hovered); b && b->Interactable) b->Hovered = true;
            if (auto* s = m_Registry.try_get<UISliderComponent>(hovered); s && s->Interactable) s->Hovered = true;
            if (auto* t = m_Registry.try_get<UIToggleComponent>(hovered); t && t->Interactable) t->Hovered = true;
            if (auto* f = m_Registry.try_get<UIInputFieldComponent>(hovered); f && f->Interactable) f->Hovered = true;
        }
        auto pointer = [&](UIChange kind, entt::entity e, entt::entity target, bool inside) {
            UIChangeEvent change{ e, kind };
            change.Target = target;
            change.Screen = mouse;
            change.On = inside;
            changes.push_back(change);
        };
        // Enter / exit: the hovered element and its UI ancestors (exits from the deepest one up,
        // enters from the outermost one down).
        {
            std::vector<entt::entity> chain;
            for (entt::entity a = hovered; a != entt::null && chain.size() < 1024; a = GetUIParent(a))
                chain.push_back(a);
            auto contains = [](const std::vector<entt::entity>& v, entt::entity e) { return std::find(v.begin(), v.end(), e) != v.end(); };
            const entt::entity previous = m_HoveredUI;
            for (auto e : m_UIHoverChain)
                if (!contains(chain, e) && m_Registry.valid(e))
                    pointer(UIChange::PointerExit, e, previous, false);
            for (auto it = chain.rbegin(); it != chain.rend(); ++it)
                if (!contains(m_UIHoverChain, *it))
                    pointer(UIChange::PointerEnter, *it, hovered, true);
            m_UIHoverChain = std::move(chain);
            m_HoveredUI = hovered;
        }

        auto sliderFollowMouse = [&](entt::entity e) {
            auto& s = m_Registry.get<UISliderComponent>(e);
            const float t = UIMath::SliderNormalizedAt(GetUINode(e).Rect, s.Direction, s.HandleSize, local(e));
            const float value = UIMath::Denormalize(t, s.MinValue, s.MaxValue, s.WholeNumbers);
            if (value != s.Value)
            {
                s.Value = value;
                s.ValueChanged = true;
                changes.push_back({ e, UIChange::Slider, value });
            }
        };

        if (pressedNow)
        {
            m_PressedUI = hovered;
            if (hovered != entt::null)
                pointer(UIChange::PointerDown, hovered, hovered, true);
            // A press selects the widget under it (no highlight: that is for keys), elsewhere nothing.
            SelectUI(hovered != entt::null && IsUIWidget(hovered) ? hovered : entt::null, false, changes);
            // A press anywhere but on the field being edited ends its editing.
            if (m_FocusedInput != entt::null && hovered != m_FocusedInput)
                EndInputEdit(&changes);
            if (hovered != entt::null)
            {
                if (auto* s = m_Registry.try_get<UISliderComponent>(hovered); s && s->Interactable)
                    sliderFollowMouse(hovered); // the handle jumps to the click
                if (auto* f = m_Registry.try_get<UIInputFieldComponent>(hovered); f && f->Interactable)
                {
                    const bool doubleClick = m_LastUIClickEntity == hovered && m_UITime - m_LastUIClickTime < 0.35f;
                    const bool focusing = m_FocusedInput != hovered;
                    if (focusing)
                        BeginInputEdit(hovered);
                    if (doubleClick)
                    {
                        // Double click: everything.
                        f->Anchor = 0;
                        f->Caret = f->Text.size();
                        f->Selecting = false;
                    }
                    else if (!(focusing && f->SelectAllOnFocus))
                    {
                        const auto& t = m_Registry.get<UITransformComponent>(hovered);
                        f->Caret = f->Anchor = InputCaretAt(*f, t, GetUINode(hovered).Rect, local(hovered));
                        f->Selecting = true;
                    }
                    f->BlinkTime = 0.0f;
                    ScrollInputField(hovered);
                }
                m_LastUIClickTime = m_UITime;
            }
            m_LastUIClickEntity = hovered;
        }

        // ---- scroll views: the wheel, a press that becomes a drag of the content (the widget it
        // went down on is then not clicked), scrollbars, inertia, the spring back.
        UpdateUIScrollViews(dt, mouse, pressedNow, released, hovered, changes);

        // Held down: a slider follows the mouse anywhere; an input field selects up to it (the
        // position the button came up at still counts).
        if ((m_MouseDown || released) && m_PressedUI != entt::null)
        {
            if (auto* s = m_Registry.try_get<UISliderComponent>(m_PressedUI); s && s->Interactable && !pressedNow)
                sliderFollowMouse(m_PressedUI);
            if (auto* f = m_Registry.try_get<UIInputFieldComponent>(m_PressedUI); f && f->Selecting && m_FocusedInput == m_PressedUI && !pressedNow)
            {
                const auto& t = m_Registry.get<UITransformComponent>(m_PressedUI);
                f->Caret = InputCaretAt(*f, t, GetUINode(m_PressedUI).Rect, local(m_PressedUI));
                ScrollInputField(m_PressedUI);
            }
        }

        // Up (where the press went down; inside: a click of it).
        if (released && m_PressedUI != entt::null && m_Registry.valid(m_PressedUI))
        {
            const bool inside = hovered == m_PressedUI;
            pointer(UIChange::PointerUp, m_PressedUI, hovered, inside);
            if (inside)
                pointer(UIChange::PointerClick, m_PressedUI, hovered, true);
        }
        std::vector<entt::entity> clicked;
        for (auto e : m_Registry.view<UIButtonComponent>())
        {
            auto& b = m_Registry.get<UIButtonComponent>(e);
            b.Pressed = m_MouseDown && m_PressedUI == e && b.Hovered;
            if (released && m_PressedUI == e && b.Hovered)
            {
                b.Clicked = true;
                clicked.push_back(e);
            }
        }
        for (auto e : clicked)
            changes.push_back({ e, UIChange::Click });
        for (auto e : m_Registry.view<UISliderComponent>())
        {
            auto& s = m_Registry.get<UISliderComponent>(e);
            s.Dragging = m_MouseDown && m_PressedUI == e && s.Interactable;
        }
        std::vector<entt::entity> switched;
        for (auto e : m_Registry.view<UIToggleComponent>())
        {
            auto& t = m_Registry.get<UIToggleComponent>(e);
            t.Pressed = m_MouseDown && m_PressedUI == e && t.Hovered;
            if (released && m_PressedUI == e && t.Hovered)
                switched.push_back(e);
        }
        for (auto e : switched)
        {
            auto& t = m_Registry.get<UIToggleComponent>(e);
            // A radio button that is on stays on (unless the group allows none).
            if (t.IsOn && !t.Group.empty() && !t.AllowSwitchOff)
                continue;
            ApplyToggle(e, !t.IsOn, &changes);
        }
        if (released)
        {
            if (m_PressedUI != entt::null && m_Registry.valid(m_PressedUI))
                if (auto* f = m_Registry.try_get<UIInputFieldComponent>(m_PressedUI))
                    f->Selecting = false;
            m_PressedUI = entt::null;
        }

        // ---- the keyboard: the input field being edited, then navigation (which sees the field as
        // it was before this frame's keys: the Enter that submitted it does not start it again).
        const bool editedBefore = m_FocusedInput != entt::null;
        if (m_FocusedInput != entt::null && m_UIKeyboardEnabled)
            ProcessTextInput(m_FocusedInput, changes);
        if (m_UINavigationEnabled)
            ProcessUINavigation(dt, editedBefore, changes);

        // ---- what animates by itself (the UI's own time: it goes on while the game is paused)
        if (m_FocusedInput != entt::null)
            m_Registry.get<UIInputFieldComponent>(m_FocusedInput).BlinkTime += dt;
        for (auto e : m_Registry.view<UIToggleComponent>())
        {
            auto& t = m_Registry.get<UIToggleComponent>(e);
            const float target = t.IsOn ? 1.0f : 0.0f, step = dt / 0.12f;
            t.Transition = t.Transition < target ? std::min(target, t.Transition + step) : std::max(target, t.Transition - step);
        }
        for (auto e : m_Registry.view<UIProgressBarComponent>())
        {
            auto& p = m_Registry.get<UIProgressBarComponent>(e);
            const float target = std::clamp(p.Value, 0.0f, 1.0f);
            if (p.SmoothSpeed <= 0.0f)
                p.DisplayValue = target;
            else
            {
                const float step = p.SmoothSpeed * dt;
                p.DisplayValue = p.DisplayValue < target ? std::min(target, p.DisplayValue + step) : std::max(target, p.DisplayValue - step);
            }
        }

        // ---- UI animations: shows of what became active, the widgets' feedback, the tweens
        UpdateUIAnimations(dt);

        // ---- the scripts hear of it, in the order it happened
        DeliverUIChanges(changes);
    }

    // ------------------------------------------------------------------ drawing helpers
    static glm::mat4 RectTransform(const UIRect& r)
    {
        const glm::vec2 c = r.GetCenter(), size = r.GetSize();
        return glm::translate(glm::mat4(1.0f), glm::vec3(c, 0.0f)) * glm::scale(glm::mat4(1.0f), glm::vec3(size, 1.0f));
    }

    static void DrawRectFilled(const UIRect& r, const glm::vec4& color, int id)
    {
        if (r.IsEmpty() || color.a <= 0.0f)
            return;
        Renderer2D::DrawQuad(RectTransform(r), color, id);
    }

    static void DrawRectOutline(const UIRect& r, float width, const glm::vec4& color, int id)
    {
        if (r.IsEmpty() || color.a <= 0.0f || width <= 0.0f)
            return;
        width = std::min(width, std::min(r.GetWidth(), r.GetHeight()) * 0.5f);
        DrawRectFilled({ r.Min, { r.Max.x, r.Min.y + width } }, color, id);                                   // bottom
        DrawRectFilled({ { r.Min.x, r.Max.y - width }, r.Max }, color, id);                                   // top
        DrawRectFilled({ { r.Min.x, r.Min.y + width }, { r.Min.x + width, r.Max.y - width } }, color, id);    // left
        DrawRectFilled({ { r.Max.x - width, r.Min.y + width }, { r.Max.x, r.Max.y - width } }, color, id);    // right
    }

    static void DrawDisc(const glm::vec2& center, const glm::vec2& size, const glm::vec4& color, int id)
    {
        if (size.x <= 0.0f || size.y <= 0.0f || color.a <= 0.0f)
            return;
        const float fade = std::clamp(2.0f / std::max(1.0f, std::min(size.x, size.y)), 0.002f, 0.5f); // about a pixel of edge
        Renderer2D::DrawCircle(glm::translate(glm::mat4(1.0f), glm::vec3(center, 0.0f)) * glm::scale(glm::mat4(1.0f), glm::vec3(size, 1.0f)),
                               color, 1.0f, fade, id);
    }

    // A bar with round ends (a pill) when `rounded`, else a rectangle.
    static void DrawBar(const UIRect& r, const glm::vec4& color, bool rounded, int id)
    {
        if (r.IsEmpty() || color.a <= 0.0f)
            return;
        if (!rounded)
        {
            DrawRectFilled(r, color, id);
            return;
        }
        const glm::vec2 size = r.GetSize(), c = r.GetCenter();
        if (size.x >= size.y)
        {
            const float radius = size.y * 0.5f;
            if (size.x <= size.y)
            {
                DrawDisc(c, size, color, id);
                return;
            }
            DrawRectFilled({ { r.Min.x + radius, r.Min.y }, { r.Max.x - radius, r.Max.y } }, color, id);
            DrawDisc({ r.Min.x + radius, c.y }, glm::vec2(size.y), color, id);
            DrawDisc({ r.Max.x - radius, c.y }, glm::vec2(size.y), color, id);
        }
        else
        {
            const float radius = size.x * 0.5f;
            DrawRectFilled({ { r.Min.x, r.Min.y + radius }, { r.Max.x, r.Max.y - radius } }, color, id);
            DrawDisc({ c.x, r.Min.y + radius }, glm::vec2(size.x), color, id);
            DrawDisc({ c.x, r.Max.y - radius }, glm::vec2(size.x), color, id);
        }
    }

    // A thick line from a to b (px), drawn as a rotated quad.
    static void DrawStroke(const glm::vec2& a, const glm::vec2& b, float thickness, const glm::vec4& color, int id)
    {
        const glm::vec2 d = b - a;
        const float length = glm::length(d);
        if (length <= 0.0f || thickness <= 0.0f)
            return;
        const glm::vec2 mid = (a + b) * 0.5f;
        glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(mid, 0.0f)) * glm::rotate(glm::mat4(1.0f), std::atan2(d.y, d.x), glm::vec3(0, 0, 1)) *
                      glm::scale(glm::mat4(1.0f), glm::vec3(length + thickness * 0.5f, thickness, 1.0f));
        Renderer2D::DrawQuad(m, color, id);
    }

    static glm::vec4 Lighten(const glm::vec4& c, float amount) { return glm::vec4(glm::mix(glm::vec3(c), glm::vec3(1.0f), amount), c.a); }
    static glm::vec4 Darken(const glm::vec4& c, float amount) { return glm::vec4(glm::vec3(c) * (1.0f - amount), c.a); }
    static glm::vec4 Disabled(const glm::vec4& c) { return glm::vec4(glm::vec3(c) * 0.6f, c.a * 0.8f); }

    // One line of text with its baseline placed so the line is centred vertically on `centerY`,
    // starting at `x` (px).
    static void DrawTextLine(const std::string& text, const Ref<Font>& font, float px, float x, float centerY, const glm::vec4& color, int id)
    {
        if (!font || text.empty() || color.a <= 0.0f)
            return;
        const float baseline = centerY + (font->GetLineHeight() * 0.5f - font->GetAscent()) * px;
        glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(x, baseline, 0.0f)) * glm::scale(glm::mat4(1.0f), glm::vec3(px, px, 1.0f));
        Renderer2D::DrawString(text, font, m, Renderer2D::TextParams{ color, 0.0f, 0.0f }, id);
    }

    // The part of the texture an image shows: its sprite sheet frame, else the whole texture.
    static SpriteRegion ImageRegion(const UIImageComponent& image, const AssetRegistry* registry)
    {
        if (!image.Texture)
            return SpriteRegion();
        const uint32_t w = image.Texture->GetWidth(), h = image.Texture->GetHeight();
        if (!image.SpriteName.empty() && registry)
            if (const SpriteSheet* sheet = SpriteSheet::Get(registry, image.TexturePath))
                if (const SpriteFrame* frame = sheet->Find(image.SpriteName))
                    return SpriteRegion::FromFrame(*frame, w, h);
        return SpriteRegion::WholeTexture(w, h);
    }

    static void DrawUIImage(UIImageComponent& image, const UIRect& rect, const glm::vec4& color, int id, const AssetRegistry* registry)
    {
        if (!image.Texture && !image.TexturePath.empty())
            image.Texture = TextureLibrary::Load(Project::GetAssetFileSystemPath(image.TexturePath), image.PixelArt ? TextureFilter::Nearest : TextureFilter::Linear);
        if (rect.IsEmpty() || color.a <= 0.0f)
            return;
        const SpriteRegion region = ImageRegion(image, registry);
        const float aspect = region.Pixels.y > 0.0f ? region.Pixels.x / region.Pixels.y : 1.0f;
        switch (image.Type)
        {
            case UIImageType::Simple:
            {
                const UIRect r = image.PreserveAspect && image.Texture ? UIMath::FitAspect(rect, aspect) : rect;
                if (image.Texture)
                    Renderer2D::DrawQuad(RectTransform(r), image.Texture, region.UVMin, region.UVMax, color, id);
                else
                    Renderer2D::DrawQuad(RectTransform(r), color, id);
                return;
            }
            case UIImageType::Sliced:
            case UIImageType::Tiled:
            {
                SpriteGeometry g;
                g.Mode = image.Type == UIImageType::Sliced ? SpriteDrawMode::Sliced : SpriteDrawMode::Tiled;
                g.Size = rect.GetSize();
                g.Pivot = glm::vec2(0.5f);
                g.UVMin = region.UVMin;
                g.UVMax = region.UVMax;
                g.RegionPixels = region.Pixels;
                g.Border = image.HasSliceBorder() ? image.SliceBorder : region.Border;
                g.PixelsPerUnit = 1.0f / std::max(0.01f, image.BorderScale); // 1 px of the picture = BorderScale px on screen
                if (!image.Texture)
                {
                    g.Mode = SpriteDrawMode::Sliced; // no picture: one plain rectangle
                    g.Border = glm::vec4(0.0f);
                }
                static thread_local std::vector<SpriteQuad> quads;
                quads.clear();
                g.Build(quads);
                const glm::vec2 center = rect.GetCenter();
                for (const SpriteQuad& q : quads)
                {
                    const UIRect piece{ center + q.Min, center + q.Max };
                    if (image.Texture)
                        Renderer2D::DrawQuad(RectTransform(piece), image.Texture, q.UVMin, q.UVMax, color, id);
                    else
                        Renderer2D::DrawQuad(RectTransform(piece), color, id);
                }
                return;
            }
            case UIImageType::Filled:
            {
                const UIRect r = image.PreserveAspect && image.Texture ? UIMath::FitAspect(rect, aspect) : rect;
                static thread_local std::vector<UIQuad> quads;
                quads.clear();
                UIMath::BuildFill(r, region.UVMin, region.UVMax, image.FillMethod, image.FillOrigin, image.FillAmount, image.FillClockwise, quads);
                for (const UIQuad& q : quads)
                    Renderer2D::DrawQuadCorners(q.Position, q.UV, image.Texture, color, id);
                return;
            }
        }
    }

    static void DrawUISlider(const UISliderComponent& s, const UIRect& rect, bool selected, const glm::vec4& tint, int id)
    {
        const bool horizontal = UIMath::IsHorizontal(s.Direction);
        const float thicknessOfRect = horizontal ? rect.GetHeight() : rect.GetWidth();
        const float thickness = s.TrackThickness > 0.0f ? std::min(s.TrackThickness, thicknessOfRect) : thicknessOfRect;
        const glm::vec2 c = rect.GetCenter();
        const UIRect track = horizontal ? UIRect{ { rect.Min.x, c.y - thickness * 0.5f }, { rect.Max.x, c.y + thickness * 0.5f } }
                                        : UIRect{ { c.x - thickness * 0.5f, rect.Min.y }, { c.x + thickness * 0.5f, rect.Max.y } };
        auto look = [&](const glm::vec4& color) { return (s.Interactable ? color : Disabled(color)) * tint; };
        DrawBar(track, look(s.TrackColor), s.Rounded, id);

        const float handle = UIMath::GetSliderHandleSize(rect, s.Direction, s.HandleSize);
        const glm::vec2 h = UIMath::GetSliderHandleCenter(rect, s.Direction, s.HandleSize, s.GetNormalizedValue());
        UIRect fill = track;
        switch (s.Direction)
        {
            case UIDirection::LeftToRight: fill.Max.x = h.x; break;
            case UIDirection::RightToLeft: fill.Min.x = h.x; break;
            case UIDirection::BottomToTop: fill.Max.y = h.y; break;
            case UIDirection::TopToBottom: fill.Min.y = h.y; break;
        }
        DrawBar(fill, look(s.FillColor), s.Rounded, id);

        glm::vec4 handleColor = s.HandleColor;
        if (s.Interactable)
            handleColor = s.Dragging ? Darken(handleColor, 0.15f) : (s.Hovered || selected) ? Lighten(handleColor, 0.25f) : handleColor;
        if (s.Rounded)
        {
            // A soft shadow ring under the knob, then the knob.
            DrawDisc(h, glm::vec2(handle), glm::vec4(0.0f, 0.0f, 0.0f, 0.25f * handleColor.a) * glm::vec4(1, 1, 1, tint.a), id);
            DrawDisc(h, glm::vec2(handle * 0.86f), look(handleColor), id);
        }
        else
        {
            const glm::vec2 size = horizontal ? glm::vec2(handle * 0.5f, handle) : glm::vec2(handle, handle * 0.5f);
            DrawRectFilled(UIRect::FromCenter(h, size), look(handleColor), id);
        }
    }

    static void DrawUIToggle(const UIToggleComponent& t, const UIRect& rect, bool running, bool selected, const glm::vec4& tint, int id)
    {
        const UIRect box = UIMath::GetToggleBox(rect, t.BoxSize, t.Style);
        if (box.IsEmpty())
            return;
        const float k = running ? std::clamp(t.Transition, 0.0f, 1.0f) : (t.IsOn ? 1.0f : 0.0f);
        glm::vec4 boxColor = glm::mix(t.BoxColor, t.OnColor, k);
        if (!t.Interactable)
            boxColor = Disabled(boxColor);
        else if (t.Pressed)
            boxColor = Darken(boxColor, 0.15f);
        else if (t.Hovered || selected)
            boxColor = Lighten(boxColor, 0.15f);
        boxColor *= tint;
        const glm::vec4 markColor = (t.Interactable ? t.MarkColor : Disabled(t.MarkColor)) * tint;
        const float size = box.GetHeight();

        if (t.Style == UIToggleStyle::Switch)
        {
            DrawBar(box, boxColor, true, id);
            const float knob = size * 0.76f;
            const float x = glm::mix(box.Min.x + size * 0.5f, box.Max.x - size * 0.5f, k);
            DrawDisc({ x, box.GetCenter().y }, glm::vec2(knob), markColor, id);
            return;
        }

        DrawRectFilled(box, boxColor, id);
        DrawRectOutline(box, std::max(1.0f, size * 0.06f), glm::vec4(glm::vec3(Lighten(boxColor, 0.2f)), boxColor.a), id);
        if (k <= 0.01f)
            return;
        glm::vec4 mark = markColor;
        mark.a *= k;
        auto at = [&](float u, float v) { return box.Min + glm::vec2(u, v) * box.GetSize(); };
        switch (t.Mark)
        {
            case UIToggleMark::Check:
            {
                const float stroke = std::max(1.5f, size * 0.13f);
                DrawStroke(at(0.22f, 0.53f), at(0.42f, 0.32f), stroke, mark, id);
                DrawStroke(at(0.42f, 0.32f), at(0.79f, 0.71f), stroke, mark, id);
                break;
            }
            case UIToggleMark::Square:
                DrawRectFilled(box.Inset(size * 0.25f), mark, id);
                break;
            case UIToggleMark::Dot:
                DrawDisc(box.GetCenter(), glm::vec2(size * 0.46f), mark, id);
                break;
        }
    }

    static void DrawUIProgressBar(const UIProgressBarComponent& p, const UITransformComponent& t, const UIRect& rect, bool running,
                                  const glm::vec4& tint, int id)
    {
        const float value = std::clamp(running ? p.DisplayValue : p.Value, 0.0f, 1.0f);
        DrawRectFilled(rect, p.BackgroundColor * tint, id);
        const UIRect fill = UIMath::FillRect(rect.Inset(p.Padding), p.Direction, value);
        DrawRectFilled(fill, p.GetFillColor(value) * tint, id);
        if (p.ShowPercentage)
            if (Ref<Font> font = Font::GetDefault())
            {
                const std::string label = std::format("{}%", (int)std::round(value * 100.0f));
                const float px = WidgetFontSize(t, rect, 0.6f);
                const float width = Renderer2D::MeasureString(label, font).x * px;
                DrawTextLine(label, font, px, rect.GetCenter().x - width * 0.5f, rect.GetCenter().y, p.TextColor * tint, id);
            }
    }

    // `outerClip`: the masks' clip it is drawn in (view px), kept after it.
    static void DrawUIInputField(UIInputFieldComponent& f, const UITransformComponent& t, const UIRect& rect, float scale, const glm::vec2& viewport,
                                 const UIRect* outerClip, const glm::vec2& lookScale, const glm::vec2& lookOffset, const glm::vec4& tint, int id)
    {
        if (!f.FontAsset)
            f.FontAsset = f.FontPath.empty() ? Font::GetDefault() : Font::Load(Project::GetAssetFileSystemPath(f.FontPath));
        glm::vec4 background = f.BackgroundColor;
        if (!f.Interactable)
            background = Disabled(background);
        else if (f.Hovered && !f.Focused)
            background = Lighten(background, 0.05f);
        DrawRectFilled(rect, background * tint, id);
        if (f.Focused)
            DrawRectOutline(rect, 2.0f, f.FocusColor * tint, id);
        else
            DrawRectOutline(rect, 1.0f, f.OutlineColor * tint, id);

        const Ref<Font>& font = f.FontAsset;
        if (!font || !font->IsLoaded())
            return;
        const UIRect area = InputTextArea(f, rect);
        if (area.IsEmpty())
            return;
        const float px = WidgetFontSize(t, rect, 0.5f);
        const bool placeholder = f.Text.empty();
        const std::string display = placeholder ? f.Placeholder : TextEdit::GetDisplayText(f.Text, f.ContentType);
        const float scroll = f.Focused ? f.Scroll : 0.0f;
        const float x0 = area.Min.x - scroll;
        const float cy = rect.GetCenter().y;
        const float baseline = cy + (font->GetLineHeight() * 0.5f - font->GetAscent()) * px;
        const float top = baseline + font->GetAscent() * px, bottom = baseline + font->GetDescent() * px;

        // Only inside the field (a long text scrolls under its edges) - and inside the masks it is in.
        const glm::vec2 v = glm::max(viewport, glm::vec2(1.0f)); // the clip is 0..1 of the view
        UIRect clip = area.Inset(-1.0f, -1.0f, 0.0f, 0.0f).Intersect(rect);
        const glm::vec2 a = clip.Min * lookScale + lookOffset, b = clip.Max * lookScale + lookOffset; // as drawn
        clip = { glm::min(a, b) * scale, glm::max(a, b) * scale };
        if (outerClip)
            clip = clip.Intersect(*outerClip);
        Renderer2D::SetClipRect(true, clip.Min / v, clip.Max / v);

        std::vector<float> offsets;
        if (!placeholder)
            LayoutCaretOffsets(display, font.get(), px, offsets);
        if (f.Focused && !placeholder && f.Caret != f.Anchor)
        {
            const size_t a = std::min(TextEdit::CodepointIndexOf(f.Text, std::min(f.Caret, f.Anchor)), offsets.size() - 1);
            const size_t b = std::min(TextEdit::CodepointIndexOf(f.Text, std::max(f.Caret, f.Anchor)), offsets.size() - 1);
            DrawRectFilled({ { x0 + offsets[a], bottom }, { x0 + offsets[b], top } }, f.SelectionColor * tint, id);
        }
        DrawTextLine(display, font, px, x0, cy, (placeholder ? f.PlaceholderColor : f.TextColor) * tint, id);
        if (f.Focused && !f.ReadOnly)
        {
            const bool visible = f.CaretBlinkRate <= 0.0f || std::fmod(f.BlinkTime, f.CaretBlinkRate * 2.0f) < f.CaretBlinkRate;
            if (visible)
            {
                const size_t c = placeholder ? 0 : std::min(TextEdit::CodepointIndexOf(f.Text, f.Caret), offsets.size() - 1);
                const float x = x0 + (placeholder ? 0.0f : offsets[c]);
                const float width = std::max(1.5f, px * 0.07f);
                DrawRectFilled({ { x - width * 0.5f, bottom }, { x + width * 0.5f, top } }, f.CaretColor * tint, id);
            }
        }
        if (outerClip)
            Renderer2D::SetClipRect(true, outerClip->Min / v, outerClip->Max / v);
        else
            Renderer2D::SetClipRect(false);
    }

    // ------------------------------------------------------------------ the UI pass
    void Scene::RenderUI(const std::unordered_set<entt::entity>* skip)
    {
        // ---------------- screen-space UI (UI units, bottom-left origin; a canvas scales them to px)
        auto uiView = m_Registry.view<UITransformComponent>();
        if (uiView.empty() || m_ViewportWidth == 0 || m_ViewportHeight == 0)
            return;
        const glm::vec2 viewport = GetViewportSize();
        // What became active since the UI update shows up already in its show's first look.
        if (m_IsRunning)
            StartPendingUIShows();
        LayoutUI();
        std::vector<entt::entity> ui = GetUIDrawOrder(skip);
        const AssetRegistry* registry = Project::GetActive() ? Project::GetAssetRegistry() : nullptr;

        RenderCommand::SetDepthTest(false);
        Renderer2D::SetLit(false);
        // One projection per canvas scale and look: the view is viewport / scale UI units big; an
        // element with a look (scale / offset - tweens) is drawn through it, with its children.
        float currentScale = 0.0f;
        glm::vec2 currentLookScale(1.0f), currentLookOffset(0.0f);
        glm::vec2 canvas = viewport;
        auto useView = [&](float scale, const glm::vec2& lookScale, const glm::vec2& lookOffset) {
            if (scale == currentScale && lookScale == currentLookScale && lookOffset == currentLookOffset)
                return;
            if (currentScale > 0.0f)
                Renderer2D::EndScene();
            currentScale = scale;
            currentLookScale = lookScale;
            currentLookOffset = lookOffset;
            canvas = viewport / scale;
            glm::mat4 projection = glm::ortho(0.0f, canvas.x, 0.0f, canvas.y, -1.0f, 1.0f);
            if (lookScale != glm::vec2(1.0f) || lookOffset != glm::vec2(0.0f))
                projection = projection * glm::translate(glm::mat4(1.0f), glm::vec3(lookOffset, 0.0f)) * glm::scale(glm::mat4(1.0f), glm::vec3(lookScale, 1.0f));
            Renderer2D::BeginScene(projection);
        };
        auto useNode = [&](const UILayoutNode& node) { useView(node.Scale, node.VisScale, node.VisOffset); };
        useView(1.0f, glm::vec2(1.0f), glm::vec2(0.0f));
        // The masks' clip (view px) the next draws go through.
        const glm::vec2 view01 = glm::max(viewport, glm::vec2(1.0f));
        bool clipOn = false;
        UIRect clipRect;
        auto useClip = [&](bool on, const UIRect& r) {
            if (on == clipOn && (!on || (r.Min == clipRect.Min && r.Max == clipRect.Max)))
                return;
            clipOn = on;
            clipRect = r;
            if (on)
                Renderer2D::SetClipRect(true, r.Min / view01, r.Max / view01);
            else
                Renderer2D::SetClipRect(false);
        };
        // Scroll views draw their scrollbars over their content: when their subtree has been drawn.
        struct AfterSubtree
        {
            entt::entity Entity;
            int Depth;
        };
        std::vector<AfterSubtree> after;
        auto depthOf = [&](entt::entity e) {
            int depth = 0;
            for (entt::entity a = GetUIParent(e); a != entt::null && depth < 1024; a = GetUIParent(a))
                depth++;
            return depth;
        };
        auto drawScrollbars = [&](entt::entity sv) {
            const UILayoutNode& node = GetUINode(sv);
            useNode(node);
            useClip(node.Clipped, node.VisClip);
            const auto& s = m_Registry.get<UIScrollViewComponent>(sv);
            const ScrollbarGeometry g = GetScrollbars(sv);
            glm::vec4 animated = m_Registry.all_of<TintAnimationComponent>(sv) ? m_Registry.get<TintAnimationComponent>(sv).GetColor() : glm::vec4(1.0f);
            animated.a *= node.Alpha;
            for (int axis = 0; axis < 2; axis++)
            {
                if (!g.Show[axis])
                    continue;
                glm::vec4 handle = s.HandleColor;
                if (s.DraggedBar == axis)
                    handle = Darken(handle, 0.15f);
                else if (s.HoveredBar == axis)
                    handle = Lighten(handle, 0.25f);
                DrawBar(g.Track[axis], s.TrackColor * animated, true, (int)(uint32_t)sv);
                DrawBar(g.Handle[axis], handle * animated, true, (int)(uint32_t)sv);
            }
        };
        const bool highlight = IsUISelectionHighlighted();
        for (auto e : ui)
        {
            const int depth = depthOf(e);
            while (!after.empty() && after.back().Depth >= depth)
            {
                drawScrollbars(after.back().Entity);
                after.pop_back();
            }
            const auto& t = m_Registry.get<UITransformComponent>(e);
            const UILayoutNode& node = GetUINode(e);
            if (node.Alpha <= 0.001f)
                continue; // faded out (its children too)
            if (node.Clipped && node.VisClip.IsEmpty())
                continue; // all of it is outside its masks (its children too)
            useNode(node);
            useClip(node.Clipped, node.VisClip);
            if (const auto* s = m_Registry.try_get<UIScrollViewComponent>(e); s && (s->ShowHorizontalBar || s->ShowVerticalBar))
                after.push_back({ e, depth });
            // A mask that only clips does not draw its own picture.
            const auto* mask = m_Registry.try_get<UIMaskComponent>(e);
            const bool graphic = !mask || mask->ShowGraphic;
            const UIRect uiRect = node.Rect;
            const glm::vec2 center = uiRect.GetCenter(), uiSize = uiRect.GetSize();
            int id = (int)(uint32_t)e;
            glm::mat4 rect = glm::translate(glm::mat4(1.0f), glm::vec3(center, 0.0f)) * glm::scale(glm::mat4(1.0f), glm::vec3(uiSize, 1.0f));
            // Selected by keyboard / gamepad: it looks hovered (and gets an outline at the end).
            const bool selected = highlight && e == m_SelectedUI;

            glm::vec4 tint(1.0f);
            if (auto* b = m_Registry.try_get<UIButtonComponent>(e))
            {
                const bool pressed = b->Pressed || (e == m_UISubmitFlash && m_UISubmitFlashTime > 0.0f);
                tint = !b->Interactable ? glm::vec4(0.5f, 0.5f, 0.5f, 1.0f) : pressed ? b->PressedColor : (b->Hovered || selected) ? b->HoverColor : b->NormalColor;
            }
            // A tint animation colours all of it (the button's own colours included).
            glm::vec4 animated = m_Registry.all_of<TintAnimationComponent>(e) ? m_Registry.get<TintAnimationComponent>(e).GetColor() : glm::vec4(1.0f);
            animated.a *= node.Alpha; // its alpha and its UI ancestors' fade all of it
            tint *= animated;

            auto* image = graphic ? m_Registry.try_get<UIImageComponent>(e) : nullptr;
            if (auto* s = graphic ? m_Registry.try_get<SpriteRendererComponent>(e) : nullptr)
            {
                glm::vec4 c = s->Color * tint;
                const AssetRegistry* sheets = !s->SpriteName.empty() ? registry : nullptr;
                if (s->DrawMode == SpriteDrawMode::Simple)
                    DrawSprite(rect, *s, nullptr, c, id, sheets);
                else // 9-slice / tiled panels: borders in UI units
                    DrawSprite(glm::translate(glm::mat4(1.0f), glm::vec3(center, 0.0f)), *s, nullptr, c, id, sheets, &uiSize);
            }
            else if (graphic && m_Registry.all_of<UIButtonComponent>(e) && !m_Registry.all_of<UIImageComponent>(e))
                Renderer2D::DrawQuad(rect, tint, id);
            if (image)
                DrawUIImage(*image, uiRect, image->Color * tint, id, registry);

            if (auto* c = graphic ? m_Registry.try_get<CircleRendererComponent>(e) : nullptr)
                Renderer2D::DrawCircle(rect, c->Color * animated, c->Thickness, c->Fade, id);

            // Widgets draw over the entity's own picture.
            if (auto* p = m_Registry.try_get<UIProgressBarComponent>(e))
                DrawUIProgressBar(*p, t, uiRect, m_IsRunning, animated, id);
            if (auto* s = m_Registry.try_get<UISliderComponent>(e))
                DrawUISlider(*s, uiRect, selected, animated, id);
            auto* toggle = m_Registry.try_get<UIToggleComponent>(e);
            if (toggle)
                DrawUIToggle(*toggle, uiRect, m_IsRunning, selected, animated, id);
            if (auto* f = m_Registry.try_get<UIInputFieldComponent>(e))
                DrawUIInputField(*f, t, uiRect, node.Scale, viewport, clipOn ? &clipRect : nullptr, node.VisScale, node.VisOffset, animated, id);

            if (auto* text = m_Registry.try_get<TextComponent>(e))
            {
                if (!text->FontAsset) text->FontAsset = Font::GetDefault();
                Renderer2D::TextParams params{ text->Color * animated, text->Kerning, text->LineSpacing };
                // A toggle's label goes right of its box.
                const UIRect textRect = toggle ? UIMath::GetToggleLabel(uiRect, toggle->BoxSize, toggle->Style, toggle->LabelSpacing) : uiRect;
                const float px = GetUITextSize(t, uiRect, toggle != nullptr);
                // Aligned / wrapped / cut in its rectangle; lines on whole screen pixels.
                TextLayout::Draw(text->Text, text->Layout(textRect, px), text->FontAsset, px, glm::mat4(1.0f), params, id,
                                 node.HasLook() ? 0.0f : node.Scale);
            }
        }

        while (!after.empty())
        {
            drawScrollbars(after.back().Entity);
            after.pop_back();
        }

        // The keyboard / gamepad selection's outline, over everything (inside the masks it is in).
        if (highlight && m_Registry.valid(m_SelectedUI) && m_UILayout.contains(m_SelectedUI) && (!skip || !skip->contains(m_SelectedUI)) &&
            IsActiveInHierarchy({ m_SelectedUI, this }))
        {
            const auto* nav = m_Registry.try_get<UINavigationComponent>(m_SelectedUI);
            if (!nav || nav->ShowHighlight)
            {
                const UILayoutNode& node = GetUINode(m_SelectedUI);
                useNode(node);
                useClip(node.Clipped, node.VisClip);
                const glm::vec4 color = nav ? nav->HighlightColor : UINavigationComponent{}.HighlightColor;
                // Pulses softly so it is noticed.
                const float pulse = 0.75f + 0.25f * std::cos(m_UITime * 5.0f);
                DrawRectOutline(node.Rect.Inset(-4.0f), 3.0f, glm::vec4(glm::vec3(color), color.a * pulse * node.Alpha), (int)(uint32_t)m_SelectedUI);
            }
        }
        useClip(false, {});
        Renderer2D::EndScene();
        RenderCommand::SetDepthTest(true);
    }

}
