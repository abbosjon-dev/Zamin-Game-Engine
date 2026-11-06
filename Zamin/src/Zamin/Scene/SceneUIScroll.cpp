// Scroll views of the screen-space UI (UIScrollViewComponent): their view and scrollbars, the
// wheel, dragging the content or a scrollbar, inertia, the elastic spring back and scrolling the
// selected widget into view.
#include "Zamin/Scene/Scene.h"
#include "Zamin/Scene/Components.h"
#include "Zamin/Scene/Entity.h"
#include "Zamin/UI/UIScroll.h"

#include <algorithm>
#include <cmath>

namespace Zamin {

    // The press must move this far (view px) before it drags the content (a click stays a click).
    static constexpr float s_ScrollDragThreshold = 6.0f;

    static UIRect Moved(const UIRect& r, const glm::vec2& delta) { return { r.Min + delta, r.Max + delta }; }

    entt::entity Scene::GetScrollContentEntity(entt::entity scrollView)
    {
        const auto* s = m_Registry.try_get<UIScrollViewComponent>(scrollView);
        if (!s || !s->Content)
            return entt::null;
        auto it = m_EntityMap.find(s->Content.ID);
        if (it == m_EntityMap.end() || !m_Registry.valid(it->second) || it->second == scrollView ||
            !m_Registry.all_of<UITransformComponent>(it->second))
            return entt::null;
        return it->second;
    }

    UIRect Scene::GetScrollViewRectLaidOut(entt::entity scrollView)
    {
        const auto& s = m_Registry.get<UIScrollViewComponent>(scrollView);
        if (s.Viewport)
            if (auto it = m_EntityMap.find(s.Viewport.ID); it != m_EntityMap.end() && it->second != scrollView)
                if (auto node = m_UILayout.find(it->second); node != m_UILayout.end())
                    return node->second.Rect;
        auto node = m_UILayout.find(scrollView);
        return node != m_UILayout.end() ? node->second.ChildArea : UIRect{};
    }

    Scene::ScrollbarGeometry Scene::GetScrollbars(entt::entity scrollView)
    {
        ScrollbarGeometry g;
        const auto& s = m_Registry.get<UIScrollViewComponent>(scrollView);
        g.Show[0] = s.ShowHorizontalBar;
        g.Show[1] = s.ShowVerticalBar;
        if (!g.Show[0] && !g.Show[1])
            return g;
        const UILayoutNode& node = GetUINode(scrollView);
        const UIRect view = GetScrollViewRectLaidOut(scrollView);
        const entt::entity content = GetScrollContentEntity(scrollView);
        const UIRect contentRect = content != entt::null ? GetUINode(content).Rect : view;
        const glm::vec2 n = UIScroll::GetNormalizedPosition(view, contentRect);
        const bool both = g.Show[0] && g.Show[1];
        for (int axis = 0; axis < 2; axis++)
        {
            if (!g.Show[axis])
                continue;
            const bool vertical = axis == 1;
            g.Track[axis] = UIScroll::GetScrollbarTrack(node.Rect, vertical, s.ScrollbarWidth, s.ScrollbarInset, both);
            const float trackLength = vertical ? g.Track[axis].GetHeight() : g.Track[axis].GetWidth();
            g.HandleLength[axis] = UIScroll::GetHandleLength(trackLength, view.GetSize()[axis], contentRect.GetSize()[axis],
                                                             std::max(16.0f, s.ScrollbarWidth * 2.0f));
            g.Handle[axis] = UIScroll::GetScrollbarHandle(g.Track[axis], vertical, g.HandleLength[axis], n[axis]);
        }
        return g;
    }

    int Scene::ScrollbarAt(entt::entity scrollView, const glm::vec2& point)
    {
        const auto& s = m_Registry.get<UIScrollViewComponent>(scrollView);
        if (!s.ShowHorizontalBar && !s.ShowVerticalBar)
            return -1;
        const UILayoutNode& node = GetUINode(scrollView);
        if (node.Alpha <= 0.001f || (node.Clipped && !node.HitClip.Contains(point)))
            return -1;
        const glm::vec2 p = node.ToLayout(point / std::max(node.Scale, 1e-6f));
        const ScrollbarGeometry g = GetScrollbars(scrollView);
        for (int axis = 1; axis >= 0; axis--)
            if (g.Show[axis] && g.Track[axis].Inset(-2.0f).Contains(p)) // a little slack around a thin bar
                return axis;
        return -1;
    }

    void Scene::MoveScrollContent(entt::entity scrollView, const glm::vec2& delta)
    {
        if (delta == glm::vec2(0.0f))
            return;
        const entt::entity content = GetScrollContentEntity(scrollView);
        if (content == entt::null)
            return;
        m_Registry.get<UITransformComponent>(content).Position += delta;
        LayoutUI();
    }

    bool Scene::RevealInScrollViews(entt::entity e, bool animate, float margin)
    {
        bool any = false;
        for (entt::entity a = GetUIParent(e); a != entt::null; a = GetUIParent(a))
        {
            auto* s = m_Registry.try_get<UIScrollViewComponent>(a);
            if (!s)
                continue;
            const entt::entity content = GetScrollContentEntity(a);
            if (content == entt::null)
                continue;
            // Only what is in the content moves with it.
            bool inside = e == content;
            for (entt::entity b = GetUIParent(e); b != entt::null && !inside; b = GetUIParent(b))
                inside = b == content;
            if (!inside)
                continue;
            const UIRect view = GetScrollViewRectLaidOut(a);
            const UIRect contentRect = GetUINode(content).Rect;
            glm::vec2 delta = UIScroll::RevealDelta(view, GetUINode(e).Rect, margin, { s->Horizontal, s->Vertical });
            if (s->Movement != UIScrollMovement::Unrestricted)
                delta += UIScroll::CalcOffset(view, Moved(contentRect, delta), s->Horizontal, s->Vertical);
            if (glm::length(delta) < 0.5f)
                continue;
            any = true;
            s->Velocity = glm::vec2(0.0f);
            if (animate && m_IsRunning)
            {
                s->RevealTarget = m_Registry.get<UITransformComponent>(content).Position + delta;
                s->Revealing = true;
            }
            else
            {
                s->Revealing = false;
                MoveScrollContent(a, delta);
            }
        }
        return any;
    }

    void Scene::UpdateUIScrollViews(float dt, const glm::vec2& mouse, bool pressedNow, bool released, entt::entity hovered,
                                    std::vector<UIChangeEvent>& changes)
    {
        const glm::vec2 wheel = m_ViewportScroll;
        m_ViewportScroll = glm::vec2(0.0f); // used up
        auto view = m_Registry.view<UIScrollViewComponent>();
        if (view.empty())
        {
            m_ScrollPress = entt::null;
            m_ScrollDragging = false;
            return;
        }
        auto alive = [&](entt::entity sv) {
            return sv != entt::null && m_Registry.valid(sv) && m_Registry.all_of<UIScrollViewComponent, UITransformComponent>(sv) &&
                   IsActiveInHierarchy({ sv, this });
        };
        // The mouse in an element's layout units (its look undone).
        auto local = [&](entt::entity e) {
            const UILayoutNode& node = GetUINode(e);
            return node.ToLayout(mouse / std::max(node.Scale, 1e-6f));
        };
        // The press became a drag (of the content or of a scrollbar's handle): what it went down on -
        // a widget in the view, or the view itself (its background, its scrollbar) - is let go without
        // a click.
        auto letGoOfPress = [&]() {
            if (m_PressedUI != entt::null && m_Registry.valid(m_PressedUI))
            {
                UIChangeEvent up{ m_PressedUI, UIChange::PointerUp };
                up.Target = hovered;
                up.Screen = mouse;
                up.On = false;
                changes.push_back(up);
            }
            m_PressedUI = entt::null;
        };
        for (auto sv : view)
        {
            auto& s = m_Registry.get<UIScrollViewComponent>(sv);
            s.HoveredBar = -1;
            s.ScrollChanged = false;
        }
        // The scroll views around what the mouse is over, innermost first.
        std::vector<entt::entity> around;
        for (entt::entity a = hovered; a != entt::null && around.size() < 1024; a = GetUIParent(a))
            if (m_Registry.all_of<UIScrollViewComponent>(a))
                around.push_back(a);
        if (hovered != entt::null && m_Registry.all_of<UIScrollViewComponent>(hovered))
            m_Registry.get<UIScrollViewComponent>(hovered).HoveredBar = ScrollbarAt(hovered, mouse);

        // ---- a press: on a scrollbar it takes the handle (on the track the handle jumps under the
        // mouse first); elsewhere in a view it may become a drag of the content.
        if (pressedNow)
        {
            m_ScrollPress = entt::null;
            m_ScrollDragging = false;
            if (hovered != entt::null && m_Registry.all_of<UIScrollViewComponent>(hovered))
                if (const int bar = ScrollbarAt(hovered, mouse); bar >= 0 && GetScrollContentEntity(hovered) != entt::null)
                {
                    auto& s = m_Registry.get<UIScrollViewComponent>(hovered);
                    const ScrollbarGeometry g = GetScrollbars(hovered);
                    const glm::vec2 p = local(hovered);
                    const UIRect& handle = g.Handle[bar];
                    m_ScrollbarGrab = handle.Contains(p) ? p[bar] - handle.GetCenter()[bar] : 0.0f;
                    s.DraggedBar = bar;
                    s.Dragging = true;
                    s.Velocity = glm::vec2(0.0f);
                    s.Revealing = false;
                    m_ScrollPress = hovered;
                    m_ScrollPressPoint = mouse;
                }
            if (m_ScrollPress == entt::null)
            {
                // A slider or an input field drags on its own.
                const bool ownDrag = hovered != entt::null && m_Registry.any_of<UISliderComponent, UIInputFieldComponent>(hovered);
                if (!ownDrag)
                    for (auto sv : around)
                    {
                        const auto& s = m_Registry.get<UIScrollViewComponent>(sv);
                        if (s.Draggable && (s.Horizontal || s.Vertical) && GetScrollContentEntity(sv) != entt::null)
                        {
                            m_ScrollPress = sv;
                            break;
                        }
                    }
                if (m_ScrollPress != entt::null)
                {
                    m_ScrollPressPoint = mouse;
                    m_ScrollPressLocal = local(m_ScrollPress);
                }
            }
        }
        if (m_ScrollPress != entt::null && !alive(m_ScrollPress))
        {
            if (m_Registry.valid(m_ScrollPress))
                if (auto* s = m_Registry.try_get<UIScrollViewComponent>(m_ScrollPress))
                {
                    s->Dragging = false;
                    s->DraggedBar = -1;
                }
            m_ScrollPress = entt::null;
            m_ScrollDragging = false;
        }

        // ---- held down (the position the button came up at still counts)
        if (m_ScrollPress != entt::null && (m_MouseDown || released))
        {
            const entt::entity sv = m_ScrollPress;
            auto& s = m_Registry.get<UIScrollViewComponent>(sv);
            const entt::entity content = GetScrollContentEntity(sv);
            if (content != entt::null && s.DraggedBar >= 0)
            {
                // The scrollbar's handle follows the mouse; the content follows the handle.
                const int bar = s.DraggedBar;
                if (m_PressedUI != entt::null && glm::length(mouse - m_ScrollPressPoint) >= s_ScrollDragThreshold)
                    letGoOfPress();
                const ScrollbarGeometry g = GetScrollbars(sv);
                if (g.Show[bar])
                {
                    const UIRect viewRect = GetScrollViewRectLaidOut(sv), contentRect = GetUINode(content).Rect;
                    glm::vec2 n = UIScroll::GetNormalizedPosition(viewRect, contentRect);
                    n[bar] = UIScroll::ScrollbarValueAt(g.Track[bar], bar == 1, g.HandleLength[bar], local(sv), m_ScrollbarGrab);
                    MoveScrollContent(sv, UIScroll::MoveToNormalized(viewRect, contentRect, n, { bar == 0, bar == 1 }));
                }
            }
            else if (content != entt::null)
            {
                if (!m_ScrollDragging && glm::length(mouse - m_ScrollPressPoint) >= s_ScrollDragThreshold)
                {
                    m_ScrollDragging = true;
                    s.Dragging = true;
                    s.Velocity = glm::vec2(0.0f);
                    s.Revealing = false;
                    m_ScrollContentStart = m_Registry.get<UITransformComponent>(content).Position;
                    letGoOfPress();
                }
                if (m_ScrollDragging)
                {
                    glm::vec2 pointerDelta = local(sv) - m_ScrollPressLocal;
                    if (!s.Horizontal) pointerDelta.x = 0.0f;
                    if (!s.Vertical) pointerDelta.y = 0.0f;
                    const glm::vec2 current = m_Registry.get<UITransformComponent>(content).Position;
                    glm::vec2 delta = m_ScrollContentStart + pointerDelta - current;
                    if (s.Movement != UIScrollMovement::Unrestricted)
                    {
                        // Past an edge: Clamped stops there, Elastic follows the mouse less and less.
                        const UIRect viewRect = GetScrollViewRectLaidOut(sv);
                        const glm::vec2 offset = UIScroll::CalcOffset(viewRect, Moved(GetUINode(content).Rect, delta), s.Horizontal, s.Vertical);
                        delta += offset;
                        if (s.Movement == UIScrollMovement::Elastic)
                            for (int axis = 0; axis < 2; axis++)
                                if (offset[axis] != 0.0f)
                                    delta[axis] -= UIScroll::RubberDelta(offset[axis], viewRect.GetSize()[axis]);
                    }
                    if (dt > 0.0f)
                        s.Velocity = glm::mix(s.Velocity, delta / dt, std::min(1.0f, dt * 10.0f));
                    MoveScrollContent(sv, delta);
                }
            }
        }
        if (released && m_ScrollPress != entt::null)
        {
            if (auto* s = m_Registry.try_get<UIScrollViewComponent>(m_ScrollPress))
            {
                s->Dragging = false;
                if (s->DraggedBar >= 0)
                    s->Velocity = glm::vec2(0.0f);
                s->DraggedBar = -1;
            }
            m_ScrollPress = entt::null;
            m_ScrollDragging = false;
        }

        // ---- the wheel: the innermost scroll view under the mouse that scrolls that way (one that
        // scrolls only sideways takes the wheel's up / down too); it never pulls past an edge.
        if (wheel != glm::vec2(0.0f))
            for (auto sv : around)
            {
                auto& s = m_Registry.get<UIScrollViewComponent>(sv);
                const entt::entity content = GetScrollContentEntity(sv);
                if (content == entt::null)
                    continue;
                glm::vec2 delta(0.0f);
                if (wheel.y != 0.0f && s.Vertical)
                    delta.y = -wheel.y * s.ScrollSensitivity; // the wheel away: the content goes down, the view shows what is above
                else if (wheel.y != 0.0f && s.Horizontal)
                    delta.x = wheel.y * s.ScrollSensitivity;
                if (wheel.x != 0.0f && s.Horizontal)
                    delta.x += wheel.x * s.ScrollSensitivity;
                if (delta == glm::vec2(0.0f))
                    continue;
                s.Velocity = glm::vec2(0.0f);
                s.Revealing = false;
                if (s.Movement != UIScrollMovement::Unrestricted)
                    delta += UIScroll::CalcOffset(GetScrollViewRectLaidOut(sv), Moved(GetUINode(content).Rect, delta), s.Horizontal, s.Vertical);
                MoveScrollContent(sv, delta);
                break;
            }

        // ---- every scroll view: showing the selection, the spring back, inertia, the clamp; where it is now
        std::vector<entt::entity> scrollViews(view.begin(), view.end());
        SortInSceneOrder(scrollViews);
        for (auto sv : scrollViews)
        {
            if (!alive(sv))
                continue;
            auto& s = m_Registry.get<UIScrollViewComponent>(sv);
            const entt::entity content = GetScrollContentEntity(sv);
            if (content == entt::null)
                continue;
            const bool dragged = m_ScrollPress == sv && (m_ScrollDragging || s.DraggedBar >= 0);
            const UIRect viewRect = GetScrollViewRectLaidOut(sv);
            const UIRect contentRect = GetUINode(content).Rect;
            glm::vec2 delta(0.0f);
            if (s.Revealing && !dragged)
            {
                // Glides to show the selected widget.
                const glm::vec2 position = m_Registry.get<UITransformComponent>(content).Position;
                const glm::vec2 left = s.RevealTarget - position;
                if (glm::length(left) < 0.5f)
                {
                    delta = left;
                    s.Revealing = false;
                }
                else
                    delta = left * (1.0f - std::exp(-dt * 14.0f));
                s.Velocity = glm::vec2(0.0f);
            }
            else if (!dragged)
            {
                const glm::vec2 offset = s.Movement == UIScrollMovement::Unrestricted
                                             ? glm::vec2(0.0f)
                                             : UIScroll::CalcOffset(viewRect, contentRect, s.Horizontal, s.Vertical);
                for (int axis = 0; axis < 2; axis++)
                {
                    if (!(axis == 0 ? s.Horizontal : s.Vertical))
                    {
                        s.Velocity[axis] = 0.0f;
                        continue;
                    }
                    if (s.Movement == UIScrollMovement::Elastic && std::abs(offset[axis]) > 1e-3f)
                    {
                        // Past an edge: it springs back.
                        float speed = s.Velocity[axis];
                        const float from = contentRect.Min[axis], to = from + offset[axis];
                        const float next = UIScroll::SmoothDamp(from, to, speed, s.Elasticity, dt);
                        if (std::abs(to - next) < 0.1f) // a tenth of a unit from the edge: there
                        {
                            delta[axis] = offset[axis];
                            speed = 0.0f;
                        }
                        else
                            delta[axis] = next - from;
                        s.Velocity[axis] = speed;
                    }
                    else if (s.Inertia)
                    {
                        s.Velocity[axis] = UIScroll::Decelerate(s.Velocity[axis], s.DecelerationRate, dt);
                        delta[axis] = s.Velocity[axis] * dt;
                    }
                    else
                        s.Velocity[axis] = 0.0f;
                }
                if (s.Movement == UIScrollMovement::Clamped)
                {
                    const glm::vec2 back = UIScroll::CalcOffset(viewRect, Moved(contentRect, delta), s.Horizontal, s.Vertical);
                    delta += back;
                    for (int axis = 0; axis < 2; axis++)
                        if (back[axis] != 0.0f)
                            s.Velocity[axis] = 0.0f; // stopped at the edge
                }
            }
            MoveScrollContent(sv, delta);
            const glm::vec2 n = UIScroll::GetNormalizedPosition(GetScrollViewRectLaidOut(sv), GetUINode(content).Rect);
            if (s.PositionKnown && glm::any(glm::greaterThan(glm::abs(n - s.NormalizedPosition), glm::vec2(1e-4f))))
            {
                s.ScrollChanged = true;
                UIChangeEvent change{ sv, UIChange::Scroll };
                change.Screen = n;
                changes.push_back(change);
            }
            s.NormalizedPosition = n;
            s.PositionKnown = true;
        }
    }

    // ------------------------------------------------------------------ from code
    static bool IsScrollView(Entity entity, Scene* scene)
    {
        return entity && entity.GetScene() == scene && entity.HasComponent<UIScrollViewComponent>() && entity.HasComponent<UITransformComponent>();
    }

    Entity Scene::GetScrollContent(Entity scrollView)
    {
        if (!IsScrollView(scrollView, this))
            return {};
        const entt::entity content = GetScrollContentEntity(scrollView);
        return content != entt::null ? Entity{ content, this } : Entity{};
    }

    UIRect Scene::GetScrollViewRect(Entity scrollView)
    {
        if (!IsScrollView(scrollView, this))
            return {};
        LayoutUI();
        return GetScrollViewRectLaidOut(scrollView);
    }

    glm::vec2 Scene::GetScrollPosition(Entity scrollView)
    {
        if (!IsScrollView(scrollView, this))
            return { 0.0f, 1.0f };
        LayoutUI();
        const entt::entity content = GetScrollContentEntity(scrollView);
        if (content == entt::null)
            return { 0.0f, 1.0f };
        return UIScroll::GetNormalizedPosition(GetScrollViewRectLaidOut(scrollView), GetUINode(content).Rect);
    }

    bool Scene::SetScrollPosition(Entity scrollView, const glm::vec2& normalized, bool notify)
    {
        if (!IsScrollView(scrollView, this))
            return false;
        LayoutUI();
        const entt::entity content = GetScrollContentEntity(scrollView);
        if (content == entt::null)
            return false;
        auto& s = scrollView.GetComponent<UIScrollViewComponent>();
        const UIRect viewRect = GetScrollViewRectLaidOut(scrollView);
        const glm::vec2 n = glm::clamp(normalized, glm::vec2(0.0f), glm::vec2(1.0f));
        s.Velocity = glm::vec2(0.0f);
        s.Revealing = false;
        MoveScrollContent(scrollView, UIScroll::MoveToNormalized(viewRect, GetUINode(content).Rect, n, { s.Horizontal, s.Vertical }));
        const glm::vec2 now = UIScroll::GetNormalizedPosition(viewRect, GetUINode(content).Rect);
        const bool changed = !s.PositionKnown || glm::any(glm::greaterThan(glm::abs(now - s.NormalizedPosition), glm::vec2(1e-4f)));
        s.NormalizedPosition = now;
        s.PositionKnown = true;
        if (changed && notify)
        {
            s.ScrollChanged = true;
            UIChangeEvent change{ scrollView, UIChange::Scroll };
            change.Screen = now;
            DeliverUIChange(change);
        }
        return true;
    }

    bool Scene::ScrollBy(Entity scrollView, const glm::vec2& delta, bool notify)
    {
        if (!IsScrollView(scrollView, this))
            return false;
        LayoutUI();
        const entt::entity content = GetScrollContentEntity(scrollView);
        if (content == entt::null)
            return false;
        auto& s = scrollView.GetComponent<UIScrollViewComponent>();
        glm::vec2 d = delta;
        if (!s.Horizontal) d.x = 0.0f;
        if (!s.Vertical) d.y = 0.0f;
        if (s.Movement != UIScrollMovement::Unrestricted)
            d += UIScroll::CalcOffset(GetScrollViewRectLaidOut(scrollView), Moved(GetUINode(content).Rect, d), s.Horizontal, s.Vertical);
        s.Velocity = glm::vec2(0.0f);
        s.Revealing = false;
        MoveScrollContent(scrollView, d);
        const glm::vec2 now = UIScroll::GetNormalizedPosition(GetScrollViewRectLaidOut(scrollView), GetUINode(content).Rect);
        const bool changed = glm::any(glm::greaterThan(glm::abs(now - s.NormalizedPosition), glm::vec2(1e-4f)));
        s.NormalizedPosition = now;
        s.PositionKnown = true;
        if (changed && notify)
        {
            s.ScrollChanged = true;
            UIChangeEvent change{ scrollView, UIChange::Scroll };
            change.Screen = now;
            DeliverUIChange(change);
        }
        return true;
    }

    bool Scene::ScrollIntoView(Entity entity, bool animate, float margin)
    {
        if (!entity || entity.GetScene() != this || !entity.HasComponent<UITransformComponent>())
            return false;
        LayoutUI();
        return RevealInScrollViews(entity, animate, margin);
    }

}
