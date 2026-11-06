// The viewport's screen-space UI tools: picking UI elements by their rectangles and the rect
// gizmo of the selected one - move / resize handles, its pivot, its anchors in the parent's
// rectangle (dragged too) and the parent's outline. The UI is drawn over the scene in screen
// space (2D and 3D scenes alike), so all of it works in view pixels.
#include "EditorLayer.h"
#include "UI.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <format>

namespace Zamin {

    namespace {

        enum UIRectHandle
        {
            HandleMove = 0,
            HandleLeft, HandleRight, HandleBottom, HandleTop,
            HandleBottomLeft, HandleBottomRight, HandleTopLeft, HandleTopRight,
            HandlePivot,
            HandleAnchorBL, HandleAnchorBR, HandleAnchorTL, HandleAnchorTR,
            HandleCount
        };
        constexpr const char* s_HandleNames[HandleCount] = { "move", "left", "right", "bottom", "top", "bottom-left", "bottom-right", "top-left",
                                                             "top-right", "pivot", "anchor-bl", "anchor-br", "anchor-tl", "anchor-tr" };

        int HandleFromName(std::string_view name)
        {
            for (int i = 0; i < HandleCount; i++)
                if (name == s_HandleNames[i])
                    return i;
            return -1;
        }

        bool IsCorner(int h) { return h >= HandleBottomLeft && h <= HandleTopRight; }
        bool IsAnchor(int h) { return h >= HandleAnchorBL && h <= HandleAnchorTR; }
        bool MovesLeft(int h) { return h == HandleLeft || h == HandleBottomLeft || h == HandleTopLeft; }
        bool MovesRight(int h) { return h == HandleRight || h == HandleBottomRight || h == HandleTopRight; }
        bool MovesBottom(int h) { return h == HandleBottom || h == HandleBottomLeft || h == HandleBottomRight; }
        bool MovesTop(int h) { return h == HandleTop || h == HandleTopLeft || h == HandleTopRight; }

        // The nearest of 0, 0.5, 1 within `threshold` (normalized units), else rounded to 0.01.
        float SnapNormalized(float v, float threshold)
        {
            for (float s : { 0.0f, 0.5f, 1.0f })
                if (std::abs(v - s) <= threshold)
                    return s;
            return std::round(v * 100.0f) / 100.0f;
        }

        float DistanceToSegment(const ImVec2& p, const ImVec2& a, const ImVec2& b)
        {
            const float dx = b.x - a.x, dy = b.y - a.y;
            const float len2 = dx * dx + dy * dy;
            float t = len2 > 0.0f ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2 : 0.0f;
            t = std::clamp(t, 0.0f, 1.0f);
            const float x = a.x + t * dx - p.x, y = a.y + t * dy - p.y;
            return std::sqrt(x * x + y * y);
        }

        void DashedLine(ImDrawList* dl, const ImVec2& a, const ImVec2& b, ImU32 color, float thickness = 1.0f, float dash = 5.0f)
        {
            const float dx = b.x - a.x, dy = b.y - a.y;
            const float length = std::sqrt(dx * dx + dy * dy);
            if (length <= 0.0f)
                return;
            for (float t = 0.0f; t < length; t += dash * 2.0f)
            {
                const float e = std::min(length, t + dash);
                dl->AddLine(ImVec2(a.x + dx * t / length, a.y + dy * t / length), ImVec2(a.x + dx * e / length, a.y + dy * e / length), color, thickness);
            }
        }

        void DashedRect(ImDrawList* dl, const ImVec2& min, const ImVec2& max, ImU32 color, float thickness = 1.0f)
        {
            DashedLine(dl, min, ImVec2(max.x, min.y), color, thickness);
            DashedLine(dl, ImVec2(max.x, min.y), max, color, thickness);
            DashedLine(dl, max, ImVec2(min.x, max.y), color, thickness);
            DashedLine(dl, ImVec2(min.x, max.y), min, color, thickness);
        }

    }

    Entity EditorLayer::PickUIElement(const glm::vec2& viewPixel)
    {
        if (!m_PickUIInViewport || m_SceneState == SceneState::Play)
            return {};
        Scene& scene = *m_ActiveScene;
        // Hidden ones are not drawn; locked ones are clicked through.
        return scene.PickUIRect(viewPixel, [&](Entity e) { return !scene.IsHiddenInEditor(e) && !scene.IsLockedInEditor(e); });
    }

    void EditorLayer::ApplyUIRectDrag(Entity entity, int handle, const glm::vec2& mouse, bool keepAspect, bool symmetric, bool snapping)
    {
        Scene& scene = *m_ActiveScene;
        UIRectGizmoState& g = m_UIRect;
        const float scale = std::max(1e-3f, scene.GetUIScale(entity));
        const float step = snapping ? 10.0f : 1.0f; // UI units: whole ones, 10 with snapping
        // The mouse's way in whole steps: edges move (and sizes change) by whole units.
        const glm::vec2 d = glm::round((mouse - g.StartMouse) / step) * step;
        g.GuideX = g.GuideY = false;

        if (handle == HandleMove)
        {
            const glm::vec2 size = g.StartRect.GetSize();
            glm::vec2 min = g.StartRect.Min + d;
            // A magnet to the parent's edges and centre (6 px).
            const UIRect parent = scene.GetUIParentRect(entity);
            const float threshold = 6.0f / scale;
            for (int axis = 0; axis < 2; axis++)
            {
                const float edges[3] = { min[axis], min[axis] + size[axis] * 0.5f, min[axis] + size[axis] };
                const float targets[3] = { parent.Min[axis], parent.GetCenter()[axis], parent.Max[axis] };
                float best = threshold, shift = 0.0f, guide = 0.0f;
                bool found = false;
                for (int i = 0; i < 3; i++)
                    for (int j = 0; j < 3; j++)
                        if (float dist = std::abs(targets[j] - edges[i]); dist <= best)
                        {
                            best = dist;
                            shift = targets[j] - edges[i];
                            guide = targets[j];
                            found = true;
                        }
                if (found)
                {
                    min[axis] += shift;
                    (axis == 0 ? g.GuideX : g.GuideY) = true;
                    (axis == 0 ? g.GuideXValue : g.GuideYValue) = guide;
                }
            }
            scene.SetUIRect(entity, { min, min + size });
            return;
        }
        if (handle >= HandleLeft && handle <= HandleTopRight)
        {
            const UIRect s = g.StartRect;
            UIRect r = s;
            if (MovesLeft(handle)) r.Min.x = s.Min.x + d.x;
            if (MovesRight(handle)) r.Max.x = s.Max.x + d.x;
            if (MovesBottom(handle)) r.Min.y = s.Min.y + d.y;
            if (MovesTop(handle)) r.Max.y = s.Max.y + d.y;
            if (keepAspect && IsCorner(handle) && s.GetWidth() > 1e-3f && s.GetHeight() > 1e-3f)
            {
                // The corner's bigger change sets both sides (the opposite corner stays).
                const float aspect = s.GetWidth() / s.GetHeight();
                float w = std::abs(r.GetWidth()), h = std::abs(r.GetHeight());
                if (w / s.GetWidth() >= h / s.GetHeight())
                    h = w / aspect;
                else
                    w = h * aspect;
                if (MovesLeft(handle)) r.Min.x = r.Max.x - w; else r.Max.x = r.Min.x + w;
                if (MovesBottom(handle)) r.Min.y = r.Max.y - h; else r.Max.y = r.Min.y + h;
            }
            if (symmetric)
            {
                // The opposite edges move the other way (it grows / shrinks around its centre).
                if (MovesLeft(handle)) r.Max.x = s.Max.x - (r.Min.x - s.Min.x);
                if (MovesRight(handle)) r.Min.x = s.Min.x - (r.Max.x - s.Max.x);
                if (MovesBottom(handle)) r.Max.y = s.Max.y - (r.Min.y - s.Min.y);
                if (MovesTop(handle)) r.Min.y = s.Min.y - (r.Max.y - s.Max.y);
            }
            // At least one unit; an edge dragged past the other one stops there.
            for (int axis = 0; axis < 2; axis++)
                if (r.Max[axis] - r.Min[axis] < 1.0f)
                {
                    const bool movesMin = axis == 0 ? MovesLeft(handle) : MovesBottom(handle);
                    if (movesMin && !symmetric)
                        r.Min[axis] = r.Max[axis] - 1.0f;
                    else if (!symmetric)
                        r.Max[axis] = r.Min[axis] + 1.0f;
                    else
                    {
                        const float c = s.GetCenter()[axis];
                        r.Min[axis] = c - 0.5f;
                        r.Max[axis] = c + 0.5f;
                    }
                }
            scene.SetUIRect(entity, r);
            return;
        }
        if (handle == HandlePivot)
        {
            const glm::vec2 size = glm::max(g.StartRect.GetSize(), glm::vec2(1e-3f));
            glm::vec2 pivot = (mouse - g.StartRect.Min) / size;
            for (int axis = 0; axis < 2; axis++)
                pivot[axis] = SnapNormalized(pivot[axis], 8.0f / (scale * size[axis]));
            scene.SetUIPivot(entity, pivot, true);
            return;
        }
        if (IsAnchor(handle))
        {
            const UIRect parent = scene.GetUIParentRect(entity);
            const glm::vec2 size = glm::max(parent.GetSize(), glm::vec2(1e-3f));
            glm::vec2 a = glm::clamp((mouse - parent.Min) / size, glm::vec2(0.0f), glm::vec2(1.0f));
            for (int axis = 0; axis < 2; axis++)
                a[axis] = SnapNormalized(a[axis], 8.0f / (scale * size[axis]));
            glm::vec2 min = g.StartAnchorMin, max = g.StartAnchorMax;
            if (g.StartAnchorMin == g.StartAnchorMax)
                min = max = a; // one point: all four go together
            else
            {
                // Each triangle is one corner of the anchors' rectangle.
                const bool left = handle == HandleAnchorBL || handle == HandleAnchorTL;
                const bool bottom = handle == HandleAnchorBL || handle == HandleAnchorBR;
                (left ? min.x : max.x) = a.x;
                (bottom ? min.y : max.y) = a.y;
                // A side dragged past the other one stops at it.
                if (left) min.x = std::min(min.x, max.x); else max.x = std::max(max.x, min.x);
                if (bottom) min.y = std::min(min.y, max.y); else max.y = std::max(max.y, min.y);
            }
            scene.SetUIAnchors(entity, min, max, true);
        }
    }

    void EditorLayer::UI_UIRectGizmo()
    {
        UIRectGizmoState& g = m_UIRect;
        Scene& scene = *m_ActiveScene;
        const ImVec2 b0(m_ViewportBounds[0].x, m_ViewportBounds[0].y), b1(m_ViewportBounds[1].x, m_ViewportBounds[1].y);
        const ImVec2 mouse = ImGui::GetMousePos();
        const glm::vec2 mouseView{ mouse.x - b0.x, b1.y - mouse.y }; // view px, bottom-left
        auto toScreen = [&](const glm::vec2& view) { return ImVec2(b0.x + view.x, b1.y - view.y); };

        // The UI element under the mouse (outlined; a click selects it).
        const bool viewportFree = m_ViewportHovered && m_SceneState != SceneState::Play && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) &&
                                  !m_SceneHierarchyPanel.IsTilePainting() && !IsViewportHandleEditing() && !m_EditorCamera.IsFlying();
        m_HoveredUIElement = viewportFree && g.Drag < 0 ? PickUIElement(mouseView) : Entity();

        Entity entity = m_SceneHierarchyPanel.GetSelectedEntity();
        const bool editable = m_SceneState != SceneState::Play && entity && entity.IsValid() && entity.GetScene() == &scene &&
                              entity.HasComponent<UITransformComponent>() && !scene.IsLockedInEditor(entity) && !IsViewportHandleEditing();
        if (!editable || (g.Drag >= 0 && (uint64_t)entity.GetUUID() != g.Entity))
        {
            g.Drag = g.Hover = -1;
            g.GuideX = g.GuideY = false;
        }

        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->PushClipRect(b0, b1, true);
        const ImU32 shadow = IM_COL32(10, 12, 16, 160);
        // Another UI element under the mouse: a thin outline (what a click would select).
        if (m_HoveredUIElement && m_HoveredUIElement != entity && g.Drag < 0)
        {
            const UIRect r = scene.GetUIVisualRect(m_HoveredUIElement);
            const float s = scene.GetUIScale(m_HoveredUIElement);
            const ImVec2 a = toScreen(r.Min * s), b = toScreen(r.Max * s);
            dl->AddRect(ImVec2(a.x, b.y), ImVec2(b.x, a.y), IM_COL32(255, 255, 255, 110), 0.0f, 0, 1.0f);
        }
        if (!editable)
        {
            dl->PopClipRect();
            return;
        }

        // ---- where everything is (layout units of the element, then the screen)
        const UIRect rect = scene.GetUIRect(entity);
        const UIRect parent = scene.GetUIParentRect(entity);
        const auto& t = entity.GetComponent<UITransformComponent>();
        const bool driven = scene.IsUILayoutDriven(entity);
        auto screenOf = [&](const glm::vec2& layout) { return toScreen(scene.UILayoutToView(entity, layout)); };
        const ImVec2 rMin = screenOf(rect.Min), rMax = screenOf(rect.Max); // rMin: bottom-left on screen
        const ImVec2 corners[4] = { ImVec2(rMin.x, rMin.y), ImVec2(rMax.x, rMin.y), ImVec2(rMin.x, rMax.y), ImVec2(rMax.x, rMax.y) }; // BL BR TL TR
        const glm::vec2 pivotLayout = rect.Min + rect.GetSize() * t.Pivot;
        const ImVec2 pivot = screenOf(pivotLayout);
        auto anchorAt = [&](float ax, float ay) { return screenOf(parent.Min + parent.GetSize() * glm::vec2(ax, ay)); };
        const ImVec2 anchors[4] = { anchorAt(t.AnchorMin.x, t.AnchorMin.y), anchorAt(t.AnchorMax.x, t.AnchorMin.y),
                                    anchorAt(t.AnchorMin.x, t.AnchorMax.y), anchorAt(t.AnchorMax.x, t.AnchorMax.y) };
        // Each anchor triangle sits just outside its corner of the anchors (BL points down-left ...).
        const float tri = 9.0f;
        const ImVec2 outward[4] = { ImVec2(-1, 1), ImVec2(1, 1), ImVec2(-1, -1), ImVec2(1, -1) };
        ImVec2 anchorTips[4], anchorCenters[4];
        for (int i = 0; i < 4; i++)
        {
            anchorTips[i] = ImVec2(anchors[i].x + outward[i].x * 1.5f, anchors[i].y + outward[i].y * 1.5f);
            anchorCenters[i] = ImVec2(anchorTips[i].x + outward[i].x * tri * 0.45f, anchorTips[i].y + outward[i].y * tri * 0.45f);
        }
        ImVec2 handles[HandleCount];
        handles[HandleMove] = ImVec2((rMin.x + rMax.x) * 0.5f, (rMin.y + rMax.y) * 0.5f);
        handles[HandleLeft] = ImVec2(rMin.x, (rMin.y + rMax.y) * 0.5f);
        handles[HandleRight] = ImVec2(rMax.x, (rMin.y + rMax.y) * 0.5f);
        handles[HandleBottom] = ImVec2((rMin.x + rMax.x) * 0.5f, rMin.y);
        handles[HandleTop] = ImVec2((rMin.x + rMax.x) * 0.5f, rMax.y);
        handles[HandleBottomLeft] = corners[0];
        handles[HandleBottomRight] = corners[1];
        handles[HandleTopLeft] = corners[2];
        handles[HandleTopRight] = corners[3];
        handles[HandlePivot] = pivot;
        for (int i = 0; i < 4; i++)
            handles[HandleAnchorBL + i] = anchorCenters[i];

        // ---- hover (the gizmo's tool must be on: the select tool only shows it)
        const bool handlesOn = m_GizmoMode != GizmoMode::None && !driven;
        const ImGuiIO& io = ImGui::GetIO();
        if (g.Drag < 0)
        {
            int hover = -1;
            if (!g.HoverOverride.empty())
                hover = HandleFromName(g.HoverOverride);
            else if (handlesOn && viewportFree && !io.KeyAlt && !ImGui::IsMouseDown(ImGuiMouseButton_Right))
            {
                auto near = [&](const ImVec2& p, float r) { return (mouse.x - p.x) * (mouse.x - p.x) + (mouse.y - p.y) * (mouse.y - p.y) <= r * r; };
                if (near(pivot, 7.0f))
                    hover = HandlePivot;
                for (int i = HandleBottomLeft; i <= HandleTopRight && hover < 0; i++)
                    if (near(handles[i], 7.0f))
                        hover = i;
                if (hover < 0)
                {
                    const ImVec2 bl = corners[0], br = corners[1], tl = corners[2], tr = corners[3];
                    const float edge = 5.0f;
                    if (DistanceToSegment(mouse, bl, tl) <= edge) hover = HandleLeft;
                    else if (DistanceToSegment(mouse, br, tr) <= edge) hover = HandleRight;
                    else if (DistanceToSegment(mouse, bl, br) <= edge) hover = HandleBottom;
                    else if (DistanceToSegment(mouse, tl, tr) <= edge) hover = HandleTop;
                }
                for (int i = 0; i < 4 && hover < 0; i++)
                    if (near(anchorCenters[i], 8.0f))
                        hover = HandleAnchorBL + i;
                const float minX = std::min(rMin.x, rMax.x), maxX = std::max(rMin.x, rMax.x);
                const float minY = std::min(rMin.y, rMax.y), maxY = std::max(rMin.y, rMax.y);
                if (hover < 0 && mouse.x >= minX && mouse.x <= maxX && mouse.y >= minY && mouse.y <= maxY)
                {
                    // Inside: moves it - unless another UI element drawn over it is under the mouse
                    // (a click selects that one).
                    if (!m_HoveredUIElement || m_HoveredUIElement == entity)
                        hover = HandleMove;
                }
            }
            g.Hover = hover;
        }

        // ---- dragging
        bool changed = false;
        if (g.Drag >= 0)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_Escape))
            {
                // Cancelled: back to how it was.
                entity.GetComponent<UITransformComponent>() = g.StartTransform;
                g.Drag = -1;
                g.ClickPending = false;
                g.GuideX = g.GuideY = false;
                MarkDirty();
            }
            else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                // Up. A press inside it that did not move it is a click: select what is under the mouse.
                if (g.Drag == HandleMove && !g.Moved && g.ClickPending)
                {
                    Entity picked = PickUIElement(mouseView);
                    if (!picked)
                        picked = m_HoveredEntity && !scene.IsLockedInEditor(m_HoveredEntity) ? m_HoveredEntity : Entity();
                    m_SceneHierarchyPanel.SelectWithModifiers(picked, g.Ctrl, g.Shift);
                }
                g.Drag = -1;
                g.ClickPending = false;
                g.GuideX = g.GuideY = false;
            }
            else
            {
                const float dx = mouse.x - g.StartScreen.x, dy = mouse.y - g.StartScreen.y;
                if (!g.Moved && dx * dx + dy * dy >= 9.0f)
                    g.Moved = true; // 3 px: a drag, not a click
                if (g.Moved || g.Drag != HandleMove)
                {
                    const bool snapping = m_ViewSettings.Snap.IsActive(io.KeyCtrl);
                    ApplyUIRectDrag(entity, g.Drag, scene.UIViewToLayout(entity, mouseView), io.KeyShift, io.KeyAlt, snapping);
                    changed = true;
                }
            }
        }
        else if (g.Hover >= 0 && g.HoverOverride.empty() && viewportFree && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyAlt)
        {
            g.Drag = g.Hover;
            g.Entity = entity.GetUUID();
            g.StartRect = rect;
            g.StartMouse = scene.UIViewToLayout(entity, mouseView);
            g.StartScreen = { mouse.x, mouse.y };
            g.StartAnchorMin = t.AnchorMin;
            g.StartAnchorMax = t.AnchorMax;
            g.StartTransform = t;
            g.Moved = false;
            g.ClickPending = g.Drag == HandleMove;
            g.Ctrl = io.KeyCtrl;
            g.Shift = io.KeyShift;
        }
        if (changed)
            MarkDirty();

        // ---- drawing (from the state after this frame's change)
        const UIRect now = scene.GetUIRect(entity);
        const UIRect nowParent = scene.GetUIParentRect(entity);
        const auto& tn = entity.GetComponent<UITransformComponent>();
        const ImVec2 nMin = screenOf(now.Min), nMax = screenOf(now.Max);
        const ImVec2 nCorners[4] = { ImVec2(nMin.x, nMin.y), ImVec2(nMax.x, nMin.y), ImVec2(nMin.x, nMax.y), ImVec2(nMax.x, nMax.y) };
        const ImVec2 npMin = screenOf(nowParent.Min), npMax = screenOf(nowParent.Max);
        auto nAnchorAt = [&](float ax, float ay) { return screenOf(nowParent.Min + nowParent.GetSize() * glm::vec2(ax, ay)); };
        const ImVec2 nAnchors[4] = { nAnchorAt(tn.AnchorMin.x, tn.AnchorMin.y), nAnchorAt(tn.AnchorMax.x, tn.AnchorMin.y),
                                     nAnchorAt(tn.AnchorMin.x, tn.AnchorMax.y), nAnchorAt(tn.AnchorMax.x, tn.AnchorMax.y) };
        const ImU32 blue = IM_COL32(70, 170, 255, 255), drivenColor = IM_COL32(150, 170, 200, 220);
        const ImU32 white = IM_COL32(245, 248, 255, 255), yellow = IM_COL32(255, 220, 90, 255), orange = IM_COL32(255, 160, 60, 255);
        const int active = g.Drag >= 0 ? g.Drag : g.Hover;

        // The parent's rectangle (what the anchors are fractions of).
        DashedRect(dl, ImVec2(npMin.x, npMax.y), ImVec2(npMax.x, npMin.y), IM_COL32(255, 255, 255, 70));
        if (!driven)
        {
            // The anchors: a flower of four triangles (one point) or one at each corner of their
            // rectangle, a thin line from each to the element's corner it holds.
            for (int i = 0; i < 4; i++)
                DashedLine(dl, nAnchors[i], nCorners[i], IM_COL32(255, 255, 255, 90), 1.0f, 3.0f);
            for (int i = 0; i < 4; i++)
            {
                const ImVec2 tip(nAnchors[i].x + outward[i].x * 1.5f, nAnchors[i].y + outward[i].y * 1.5f);
                const ImVec2 p1(tip.x + outward[i].x * tri, tip.y), p2(tip.x, tip.y + outward[i].y * tri);
                const bool hot = active == HandleAnchorBL + i;
                dl->AddTriangleFilled(tip, p1, p2, hot ? yellow : white);
                dl->AddTriangle(tip, p1, p2, shadow, 1.0f);
            }
        }
        // The element's rectangle.
        const ImU32 rectColor = driven ? drivenColor : blue;
        dl->AddRect(ImVec2(nMin.x, nMax.y), ImVec2(nMax.x, nMin.y), shadow, 0.0f, 0, 3.5f);
        if (driven)
            DashedRect(dl, ImVec2(nMin.x, nMax.y), ImVec2(nMax.x, nMin.y), rectColor, 1.5f);
        else
            dl->AddRect(ImVec2(nMin.x, nMax.y), ImVec2(nMax.x, nMin.y), rectColor, 0.0f, 0, 1.5f);
        if (active >= HandleLeft && active <= HandleTop)
        {
            // The edge being dragged / hovered, thicker.
            ImVec2 a, b;
            switch (active)
            {
                case HandleLeft:   a = nCorners[0]; b = nCorners[2]; break;
                case HandleRight:  a = nCorners[1]; b = nCorners[3]; break;
                case HandleBottom: a = nCorners[0]; b = nCorners[1]; break;
                default:           a = nCorners[2]; b = nCorners[3]; break;
            }
            dl->AddLine(a, b, yellow, 3.0f);
        }
        if (handlesOn)
        {
            const ImVec2 nHandles[8] = { ImVec2(nMin.x, (nMin.y + nMax.y) * 0.5f), ImVec2(nMax.x, (nMin.y + nMax.y) * 0.5f),
                                         ImVec2((nMin.x + nMax.x) * 0.5f, nMin.y), ImVec2((nMin.x + nMax.x) * 0.5f, nMax.y),
                                         nCorners[0], nCorners[1], nCorners[2], nCorners[3] };
            for (int i = 0; i < 8; i++)
            {
                const int h = HandleLeft + i;
                const bool hot = active == h;
                const float s = hot ? 5.0f : 4.0f;
                const ImVec2 c = nHandles[i];
                dl->AddRectFilled(ImVec2(c.x - s - 1, c.y - s - 1), ImVec2(c.x + s + 1, c.y + s + 1), shadow);
                dl->AddRectFilled(ImVec2(c.x - s, c.y - s), ImVec2(c.x + s, c.y + s), hot ? yellow : white);
                dl->AddRect(ImVec2(c.x - s, c.y - s), ImVec2(c.x + s, c.y + s), blue, 0.0f, 0, 1.5f);
            }
            // The pivot: a ring.
            const ImVec2 np = screenOf(now.Min + now.GetSize() * tn.Pivot);
            const bool hot = active == HandlePivot;
            dl->AddCircleFilled(np, 6.5f, shadow);
            dl->AddCircle(np, 5.0f, hot ? yellow : blue, 0, 2.5f);
            dl->AddCircleFilled(np, 1.8f, hot ? yellow : white);
        }
        // Snapped to the parent's edge / centre: a guide line across it.
        if (g.Drag == HandleMove && (g.GuideX || g.GuideY))
        {
            if (g.GuideX)
            {
                const ImVec2 a = screenOf({ g.GuideXValue, nowParent.Min.y }), b = screenOf({ g.GuideXValue, nowParent.Max.y });
                dl->AddLine(a, b, IM_COL32(255, 90, 200, 220), 1.5f);
            }
            if (g.GuideY)
            {
                const ImVec2 a = screenOf({ nowParent.Min.x, g.GuideYValue }), b = screenOf({ nowParent.Max.x, g.GuideYValue });
                dl->AddLine(a, b, IM_COL32(255, 90, 200, 220), 1.5f);
            }
        }

        // A label: the size (moving: the position; pivot / anchors: their values), or why it cannot be moved.
        std::string label;
        if (driven)
            label = ICON_FA_LIST "  Layout guruhi joylashtiradi";
        else if (active == HandlePivot)
            label = std::format("Pivot {:.2f}, {:.2f}", tn.Pivot.x, tn.Pivot.y);
        else if (IsAnchor(active))
            label = tn.AnchorMin == tn.AnchorMax ? std::format("Langar {:.2f}, {:.2f}", tn.AnchorMin.x, tn.AnchorMin.y)
                                                 : std::format("Langar {:.2f}, {:.2f} - {:.2f}, {:.2f}", tn.AnchorMin.x, tn.AnchorMin.y, tn.AnchorMax.x, tn.AnchorMax.y);
        else if (g.Drag == HandleMove && g.Moved)
            label = std::format("{:.0f}, {:.0f}", tn.Position.x, tn.Position.y);
        else
            label = std::format("{:.0f} x {:.0f}", now.GetWidth(), now.GetHeight());
        const ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
        const float top = std::min(nMin.y, nMax.y);
        ImVec2 at((nMin.x + nMax.x) * 0.5f - textSize.x * 0.5f, top - textSize.y - 12.0f);
        if (at.y < b0.y + 4.0f)
            at.y = std::max(nMin.y, nMax.y) + 10.0f; // no room above: below it
        dl->AddRectFilled(ImVec2(at.x - 6, at.y - 3), ImVec2(at.x + textSize.x + 6, at.y + textSize.y + 3), IM_COL32(18, 20, 26, 215), 4.0f);
        dl->AddText(at, driven ? drivenColor : (g.Drag >= 0 ? orange : white), label.c_str());
        dl->PopClipRect();

        // While over a handle the transform gizmo / camera leave the mouse alone.
        if (g.Hover >= 0 || g.Drag >= 0)
            m_GizmoHovered = true;
    }

    void EditorLayer::ApplyUIRectAutomation(const char* spec)
    {
        // "<handle>:<dx>,<dy>" - the selected UI element's handle dragged by that many view px
        // (screenshots, CI): the same path as a mouse drag.
        Entity entity = m_SceneHierarchyPanel.GetSelectedEntity();
        if (!spec || !entity || !entity.HasComponent<UITransformComponent>())
            return;
        std::string_view v(spec);
        const size_t colon = v.find(':');
        const int handle = HandleFromName(v.substr(0, colon));
        float dx = 0.0f, dy = 0.0f;
        if (handle < 0 || colon == std::string_view::npos || std::sscanf(std::string(v.substr(colon + 1)).c_str(), "%f,%f", &dx, &dy) != 2)
        {
            ZM_WARN("ZAMIN_EDITOR_UI_DRAG: noto'g'ri qiymat '{}' (masalan: right:40,0)", spec);
            return;
        }
        Scene& scene = *m_ActiveScene;
        UIRectGizmoState& g = m_UIRect;
        const auto& t = entity.GetComponent<UITransformComponent>();
        const UIRect rect = scene.GetUIRect(entity);
        // The handle's own point as the press.
        glm::vec2 start = rect.GetCenter();
        if (MovesLeft(handle)) start.x = rect.Min.x;
        if (MovesRight(handle)) start.x = rect.Max.x;
        if (MovesBottom(handle)) start.y = rect.Min.y;
        if (MovesTop(handle)) start.y = rect.Max.y;
        if (handle == HandlePivot) start = rect.Min + rect.GetSize() * t.Pivot;
        const glm::vec2 startView = scene.UILayoutToView(entity, start);
        g.Entity = entity.GetUUID();
        g.StartRect = rect;
        g.StartMouse = start;
        g.StartAnchorMin = t.AnchorMin;
        g.StartAnchorMax = t.AnchorMax;
        g.StartTransform = t;
        ApplyUIRectDrag(entity, handle, scene.UIViewToLayout(entity, startView + glm::vec2(dx, dy)), false, false, false);
        g.GuideX = g.GuideY = false;
        MarkDirty();
        ZM_INFO("UI gizmo: '{}' {} ga surildi -> {:.0f} x {:.0f}", entity.GetName(), s_HandleNames[handle], scene.GetUIRect(entity).GetWidth(),
                scene.GetUIRect(entity).GetHeight());
    }

}
