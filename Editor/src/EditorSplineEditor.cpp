// Editing a Spline component's points in the viewport (2D and 3D scenes): drag the points (and a
// Bezier path's handles), add points on the curve or at its end, remove them. The points live in
// the entity's own space; the handles are dragged in the plane facing the camera.
#include "EditorLayer.h"
#include "UI.h"

#include <Zamin/Math/Math.h>

#include <imgui.h>
#include <ImGuizmo.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <format>

namespace Zamin {

    bool EditorLayer::IsSplinePointEditing() const
    {
        return m_SceneState == SceneState::Edit && m_SceneHierarchyPanel.IsSplinePointEditing();
    }

    void EditorLayer::UI_SplinePointEditor()
    {
        if (!IsSplinePointEditing())
        {
            m_SplineDrag = m_SplineHover = m_SplinePicked = -1;
            m_SplineHoverCurve = -1.0f;
            m_SplineEntity = 0;
            return;
        }
        Entity e = m_SceneHierarchyPanel.GetSelectedEntity();
        if ((uint64_t)e.GetUUID() != m_SplineEntity)
        {
            // Another spline: nothing is held from before (the automation's pick stays).
            m_SplineDrag = m_SplineHover = -1;
            if (m_SplineEntity != 0)
                m_SplinePicked = -1;
            m_SplineEntity = (uint64_t)e.GetUUID();
        }
        auto& c = e.GetComponent<SplineComponent>();
        Spline& path = c.Path;
        const glm::mat4 world = m_ActiveScene->GetWorldTransform(e);
        const glm::mat4 toLocal = glm::inverse(world);
        const bool bezier = path.Type == SplineType::Bezier;
        const int n = (int)path.Points.size();
        if (m_SplinePicked >= n)
            m_SplinePicked = -1;
        if (m_SplineDrag >= n * 3)
            m_SplineDrag = -1;

        const glm::mat4 viewProjection = OverlayViewProjection();
        const glm::vec2 viewport = m_ViewportBounds[1] - m_ViewportBounds[0];
        const glm::vec3 viewDir = glm::normalize(m_EditorCamera.GetForwardDirection());
        auto toScreen = [&](const glm::vec3& w, bool& visible) {
            const glm::vec4 clip = viewProjection * glm::vec4(w, 1.0f);
            visible = clip.w > 1e-5f;
            const glm::vec2 ndc = visible ? glm::vec2(clip) / clip.w : glm::vec2(0.0f);
            return ImVec2(m_ViewportBounds[0].x + (ndc.x * 0.5f + 0.5f) * viewport.x, m_ViewportBounds[0].y + (0.5f - ndc.y * 0.5f) * viewport.y);
        };
        auto worldOf = [&](const glm::vec3& local) { return glm::vec3(world * glm::vec4(local, 1.0f)); };
        auto localOf = [&](const glm::vec3& w) { return glm::vec3(toLocal * glm::vec4(w, 1.0f)); };

        // The handles: every point; the picked point's Bezier handles.
        auto handleLocal = [&](int handle) {
            const SplinePoint& p = path.Points[(size_t)(handle / 3)];
            switch (handle % 3)
            {
                case 1: return p.Position + p.InTangent;
                case 2: return p.Position + p.OutTangent;
                default: return p.Position;
            }
        };
        std::vector<int> handles;
        for (int i = 0; i < n; i++)
            handles.push_back(i * 3);
        if (bezier && m_SplinePicked >= 0)
        {
            handles.push_back(m_SplinePicked * 3 + 1);
            handles.push_back(m_SplinePicked * 3 + 2);
        }
        std::vector<ImVec2> screen(handles.size());
        for (size_t i = 0; i < handles.size(); i++)
        {
            bool visible = true;
            screen[i] = toScreen(worldOf(handleLocal(handles[i])), visible);
            if (!visible)
                screen[i] = ImVec2(-1e6f, -1e6f);
        }
        // The curve on screen (for clicks on it): samples with their parameters.
        const int segments = path.GetSegmentCount();
        constexpr int steps = 24;
        std::vector<ImVec2> curve;
        std::vector<float> curveU;
        bool curveVisible = true;
        for (int s = 0; s < segments; s++)
            for (int i = (s == 0 ? 0 : 1); i <= steps; i++)
            {
                bool visible = true;
                const float t = (float)i / (float)steps;
                curve.push_back(toScreen(worldOf(path.EvaluateSegment(s, t)), visible));
                curveU.push_back((float)s + t);
                curveVisible &= visible;
            }

        const ImGuiIO& io = ImGui::GetIO();
        const bool ctrl = io.KeyCtrl;
        bool changed = false;
        auto snap = [&](glm::vec3 v) { return io.KeyShift ? glm::round(v * 10.0f) / 10.0f : v; };

        // The picked point's translate gizmo (world axes).
        bool gizmoOver = false;
        if (m_SplinePicked >= 0 && m_SplineDrag < 0)
        {
            ImGuizmo::SetOrthographic(m_EditorCamera.IsOrthographic());
            ImGuizmo::SetDrawlist();
            ImGuizmo::SetRect(m_ViewportBounds[0].x, m_ViewportBounds[0].y, viewport.x, viewport.y);
            glm::mat4 transform = glm::translate(glm::mat4(1.0f), worldOf(path.Points[(size_t)m_SplinePicked].Position));
            glm::mat4 view = m_EditorCamera.GetViewMatrix();
            ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(m_EditorCamera.GetProjection()), ImGuizmo::TRANSLATE, ImGuizmo::WORLD,
                                 glm::value_ptr(transform));
            gizmoOver = ImGuizmo::IsOver() || ImGuizmo::IsUsing();
            if (ImGuizmo::IsUsing())
            {
                const glm::vec3 local = snap(localOf(glm::vec3(transform[3])));
                if (local != path.Points[(size_t)m_SplinePicked].Position)
                {
                    path.Points[(size_t)m_SplinePicked].Position = local;
                    changed = true;
                }
            }
        }

        // Hover: a handle within 9 px (the Bezier handles over the points), else the curve within 7 px.
        const ImVec2 mouse = ImGui::GetMousePos();
        int hover = -1;
        float hoverCurve = -1.0f;
        if (!m_SplineHoverOverride.empty())
        {
            const char kind = m_SplineHoverOverride[0];
            const char* number = m_SplineHoverOverride.c_str() + 1;
            const int index = std::atoi(number);
            const int handle = kind == 'p' ? index * 3 : kind == 'i' ? index * 3 + 1 : kind == 'o' ? index * 3 + 2 : -1;
            if (kind == 'c')
                hoverCurve = std::clamp((float)std::atof(number), 0.0f, path.GetParameterEnd());
            else if (std::find(handles.begin(), handles.end(), handle) != handles.end())
                hover = handle;
        }
        else if (m_ViewportHovered && m_SplineDrag < 0 && !gizmoOver && curveVisible)
        {
            float best = 9.0f;
            for (size_t i = handles.size(); i-- > 0;)
                if (float d = std::hypot(screen[i].x - mouse.x, screen[i].y - mouse.y); d < best)
                {
                    best = d;
                    hover = handles[i];
                }
            if (hover < 0)
            {
                best = 7.0f;
                for (size_t i = 0; i + 1 < curve.size(); i++)
                {
                    const ImVec2 a = curve[i], b = curve[i + 1];
                    const float dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy;
                    const float t = l2 > 0.0f ? std::clamp(((mouse.x - a.x) * dx + (mouse.y - a.y) * dy) / l2, 0.0f, 1.0f) : 0.0f;
                    if (float d = std::hypot(a.x + dx * t - mouse.x, a.y + dy * t - mouse.y); d < best)
                    {
                        best = d;
                        hoverCurve = curveU[i] + (curveU[i + 1] - curveU[i]) * t;
                    }
                }
                // Not right at a point (that is the point's own click).
                if (hoverCurve >= 0.0f && std::abs(hoverCurve - std::round(hoverCurve)) < 0.03f)
                    hoverCurve = -1.0f;
            }
        }
        m_SplineHover = hover;
        m_SplineHoverCurve = hoverCurve;

        // Where the mouse ray meets the plane through `point` facing the camera.
        auto rayPlane = [&](const glm::vec3& point, glm::vec3& hit) {
            glm::vec3 o, d;
            if (!ViewportMouseRay(o, d))
                return false;
            const float denom = glm::dot(d, viewDir);
            if (std::abs(denom) < 1e-5f)
                return false;
            const float t = glm::dot(point - o, viewDir) / denom;
            if (t < 0.0f)
                return false;
            hit = o + d * t;
            return true;
        };
        auto beginDrag = [&](int handle) {
            m_SplineDrag = handle;
            m_SplineDragPlane = worldOf(handleLocal(handle));
            glm::vec3 hit;
            m_SplineDragOffset = rayPlane(m_SplineDragPlane, hit) ? m_SplineDragPlane - hit : glm::vec3(0.0f);
        };

        if (m_SplineDrag >= 0)
        {
            glm::vec3 hit;
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
                m_SplineDrag = -1;
            else if (rayPlane(m_SplineDragPlane, hit))
            {
                SplinePoint& p = path.Points[(size_t)(m_SplineDrag / 3)];
                const glm::vec3 local = localOf(hit + m_SplineDragOffset);
                if (m_SplineDrag % 3 == 0)
                {
                    const glm::vec3 q = snap(local);
                    if (q != p.Position)
                    {
                        p.Position = q;
                        changed = true;
                    }
                }
                else
                {
                    // A handle: the other one mirrors its direction (keeping its length) unless Ctrl.
                    const bool in = m_SplineDrag % 3 == 1;
                    glm::vec3& moved = in ? p.InTangent : p.OutTangent;
                    glm::vec3& other = in ? p.OutTangent : p.InTangent;
                    const glm::vec3 h = snap(local - p.Position);
                    if (h != moved)
                    {
                        moved = h;
                        if (!ctrl && glm::length(h) > 1e-5f)
                            other = -glm::normalize(h) * glm::length(other);
                        changed = true;
                    }
                }
            }
        }
        else if (m_ViewportHovered && !gizmoOver && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyAlt &&
                 !ImGui::IsMouseDown(ImGuiMouseButton_Right))
        {
            if (hover >= 0 && hover % 3 == 0 && ctrl)
            {
                if (n > 2)
                {
                    path.Points.erase(path.Points.begin() + hover / 3);
                    m_SplinePicked = -1;
                    changed = true;
                }
                else
                    ShowViewportMessage(ICON_FA_TRIANGLE_EXCLAMATION "  Kamida 2 ta nuqta kerak");
            }
            else if (hover >= 0)
            {
                if (hover % 3 == 0)
                    m_SplinePicked = hover / 3;
                beginDrag(hover);
            }
            else if (hoverCurve >= 0.0f && !ctrl)
            {
                const int index = path.InsertPoint(hoverCurve);
                if (index >= 0)
                {
                    m_SplinePicked = index;
                    beginDrag(index * 3);
                    changed = true;
                }
            }
            else if (!ctrl && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && n > 0)
            {
                // Empty space: a new last point there (in the plane through the last point).
                glm::vec3 hit;
                if (rayPlane(worldOf(path.Points.back().Position), hit))
                {
                    SplinePoint p;
                    p.Position = snap(localOf(hit));
                    path.Points.push_back(p);
                    if (bezier)
                        path.AutoTangents((int)path.Points.size() - 1);
                    m_SplinePicked = (int)path.Points.size() - 1;
                    changed = true;
                }
            }
            else if (!ctrl)
                m_SplinePicked = -1;
        }
        if (m_SplineDrag < 0 && hover >= 0 && hover % 3 == 0 && m_ViewportHovered && ImGui::IsKeyPressed(ImGuiKey_Delete, false) && n > 2)
        {
            path.Points.erase(path.Points.begin() + hover / 3);
            m_SplinePicked = -1;
            changed = true;
        }
        if (changed)
        {
            MarkDirty();
            return; // drawn with the new points next frame
        }
        if (!curveVisible)
            return;

        // Drawing: the hovered curve point, the handles, the points, a hint.
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->PushClipRect(ImVec2(m_ViewportBounds[0].x, m_ViewportBounds[0].y), ImVec2(m_ViewportBounds[1].x, m_ViewportBounds[1].y), true);
        const ImU32 lineColor = ImGui::ColorConvertFloat4ToU32(ImVec4(c.Color.r, c.Color.g, c.Color.b, 1.0f));
        const ImU32 shadow = IM_COL32(10, 12, 16, 170), highlight = IM_COL32(255, 230, 120, 255);
        if (curve.size() >= 2)
        {
            dl->AddPolyline(curve.data(), (int)curve.size(), shadow, 0, 4.0f);
            dl->AddPolyline(curve.data(), (int)curve.size(), lineColor, 0, 2.0f);
        }
        if (hoverCurve >= 0.0f)
        {
            bool visible = true;
            const ImVec2 at = toScreen(worldOf(path.Evaluate(hoverCurve)), visible);
            dl->AddCircleFilled(at, 6.0f, IM_COL32(255, 230, 120, 120));
            dl->AddCircle(at, 6.0f, highlight, 0, 1.5f);
            dl->AddLine(ImVec2(at.x - 3.0f, at.y), ImVec2(at.x + 3.5f, at.y), IM_COL32(30, 30, 30, 255), 1.5f);
            dl->AddLine(ImVec2(at.x, at.y - 3.0f), ImVec2(at.x, at.y + 3.5f), IM_COL32(30, 30, 30, 255), 1.5f);
        }
        for (size_t i = 0; i < handles.size(); i++)
        {
            const int h = handles[i];
            const bool hovered = h == hover, dragged = h == m_SplineDrag;
            if (h % 3 != 0)
            {
                // A Bezier handle: a line from its point, a small square.
                bool visible = true;
                const ImVec2 point = toScreen(worldOf(path.Points[(size_t)(h / 3)].Position), visible);
                dl->AddLine(point, screen[i], IM_COL32(230, 235, 245, 200), 1.5f);
                const float r = hovered || dragged ? 5.5f : 4.0f;
                dl->AddRectFilled(ImVec2(screen[i].x - r, screen[i].y - r), ImVec2(screen[i].x + r, screen[i].y + r),
                                  dragged ? IM_COL32(255, 160, 60, 255) : hovered ? highlight : IM_COL32(200, 210, 225, 255));
                dl->AddRect(ImVec2(screen[i].x - r, screen[i].y - r), ImVec2(screen[i].x + r, screen[i].y + r), shadow);
                continue;
            }
            const bool picked = h / 3 == m_SplinePicked, removing = hovered && ctrl;
            const float r = hovered || dragged || picked ? 6.5f : 5.0f;
            const ImU32 fill = removing ? IM_COL32(240, 80, 70, 255) : dragged ? IM_COL32(255, 160, 60, 255) : hovered ? highlight
                             : picked ? UI::AccentU32() : IM_COL32(240, 248, 255, 255);
            dl->AddCircleFilled(screen[i], r + 1.5f, shadow);
            dl->AddCircleFilled(screen[i], r, fill);
            dl->AddCircle(screen[i], r, lineColor, 0, 1.5f);
            if (hovered || dragged || picked)
            {
                const glm::vec3& q = path.Points[(size_t)(h / 3)].Position;
                const std::string label = std::format("{}  ({:.2f}, {:.2f}, {:.2f})", h / 3, q.x, q.y, q.z);
                const ImVec2 at(screen[i].x + 10.0f, screen[i].y - 22.0f);
                const ImVec2 size = ImGui::CalcTextSize(label.c_str());
                dl->AddRectFilled(ImVec2(at.x - 4.0f, at.y - 2.0f), ImVec2(at.x + size.x + 4.0f, at.y + size.y + 2.0f), IM_COL32(14, 16, 20, 210), 3.0f);
                dl->AddText(at, IM_COL32(235, 240, 245, 255), label.c_str());
            }
        }
        const char* hint = bezier ? ICON_FA_BEZIER_CURVE "  Spline nuqtalari:  sudrang  |  chiziqqa bosing - yangi nuqta  |  bo'sh joyga ikki marta - oxiriga  |  "
                                    "Ctrl + bosish / Delete - o'chirish  |  tutqichlar: Ctrl - alohida  |  Shift - 0.1 qadam  |  Esc - tugatish"
                                  : ICON_FA_BEZIER_CURVE "  Spline nuqtalari:  sudrang  |  chiziqqa bosing - yangi nuqta  |  bo'sh joyga ikki marta - oxiriga  |  "
                                    "Ctrl + bosish / Delete - o'chirish  |  Shift - 0.1 qadam  |  Esc - tugatish";
        const ImVec2 hintSize = ImGui::CalcTextSize(hint);
        const ImVec2 hintAt(m_ViewportBounds[0].x + (viewport.x - hintSize.x) * 0.5f, m_ViewportBounds[0].y + 44.0f);
        dl->AddRectFilled(ImVec2(hintAt.x - 10.0f, hintAt.y - 5.0f), ImVec2(hintAt.x + hintSize.x + 10.0f, hintAt.y + hintSize.y + 5.0f),
                          IM_COL32(16, 20, 26, 215), 6.0f);
        dl->AddText(hintAt, IM_COL32(150, 225, 255, 255), hint);
        dl->PopClipRect();
    }

}
