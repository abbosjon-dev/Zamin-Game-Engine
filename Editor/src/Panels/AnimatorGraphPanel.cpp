#include "AnimatorGraphPanel.h"
#include "AnimatorEditing.h"
#include "../UI.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <format>

namespace Zamin {

    static constexpr float s_NodeW = 168.0f, s_NodeH = 50.0f;   // a state (graph units)
    static constexpr float s_AnyW = 140.0f, s_AnyH = 34.0f;     // the any-state node
    static constexpr float s_Snap = 10.0f;

    static const ImU32 s_GridColor = IM_COL32(255, 255, 255, 14);
    static const ImU32 s_GridMajor = IM_COL32(255, 255, 255, 28);
    static const ImU32 s_NodeColor = IM_COL32(52, 56, 64, 255);
    static const ImU32 s_NodeHeader = IM_COL32(70, 76, 88, 255);
    static const ImU32 s_DefaultHeader = IM_COL32(196, 118, 40, 255);
    static const ImU32 s_AnyColor = IM_COL32(38, 120, 128, 255);
    static const ImU32 s_LinkColor = IM_COL32(200, 205, 215, 200);
    static const ImU32 s_FadeColor = IM_COL32(230, 150, 50, 255);

    // ------------------------------------------------------------------ context
    void AnimatorGraphPanel::SetContext(const Ref<Scene>& scene, bool editable)
    {
        if (scene != m_Scene)
        {
            m_Scene = scene;
            m_DragNode = m_LinkFrom = -2;
            // The same entity in the other scene (play starts / stops: same UUIDs).
            if (m_Scene && m_EntityID && !m_Scene->GetEntityByUUID(m_EntityID))
            {
                m_EntityID = 0;
                m_Selection = {};
            }
        }
        m_Editable = editable;
    }

    void AnimatorGraphPanel::SetSelection(Entity selected)
    {
        if (m_Pinned || !selected || !m_Scene)
            return;
        if (selected.GetUUID() != m_EntityID && selected.HasComponent<AnimatorComponent>())
            Open(selected);
    }

    void AnimatorGraphPanel::Open(Entity entity)
    {
        if (!entity || !entity.HasComponent<AnimatorComponent>())
            return;
        if (entity.GetUUID() != m_EntityID)
        {
            m_EntityID = entity.GetUUID();
            m_Selection = {};
            m_DragNode = m_LinkFrom = -2;
            m_FitRequested = true;
        }
    }

    Entity AnimatorGraphPanel::GetEntity() const
    {
        if (!m_Scene || !m_EntityID)
            return {};
        Entity e = m_Scene->GetEntityByUUID(m_EntityID);
        return e && e.HasComponent<AnimatorComponent>() ? e : Entity();
    }

    void AnimatorGraphPanel::MarkModified()
    {
        if (m_Editable && OnModified)
            OnModified();
    }

    void AnimatorGraphPanel::SelectState(int index) { m_Selection = { Kind::State, index }; }
    void AnimatorGraphPanel::SelectTransition(int index) { m_Selection = { Kind::Transition, index }; }

    // ------------------------------------------------------------------ geometry
    ImVec2 AnimatorGraphPanel::ToScreen(const glm::vec2& p) const
    {
        return ImVec2(m_CanvasMin.x + m_Pan.x + p.x * m_Zoom, m_CanvasMin.y + m_Pan.y + p.y * m_Zoom);
    }

    glm::vec2 AnimatorGraphPanel::ToGraph(const ImVec2& p) const
    {
        return { (p.x - m_CanvasMin.x - m_Pan.x) / m_Zoom, (p.y - m_CanvasMin.y - m_Pan.y) / m_Zoom };
    }

    void AnimatorGraphPanel::NodeRect(const AnimatorComponent& c, int state, glm::vec2& min, glm::vec2& max) const
    {
        if (state < 0)
        {
            min = c.AnyStateGraphPosition;
            max = min + glm::vec2(s_AnyW, s_AnyH);
            return;
        }
        min = c.States[(size_t)state].GraphPosition;
        max = min + glm::vec2(s_NodeW, s_NodeH);
    }

    int AnimatorGraphPanel::NodeAt(const AnimatorComponent& c, const glm::vec2& p) const
    {
        // Drawn last = on top: states from the end, the any-state node first.
        for (int i = (int)c.States.size() - 1; i >= -1; i--)
        {
            glm::vec2 min, max;
            NodeRect(c, i, min, max);
            if (p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y)
                return i;
        }
        return -2;
    }

    bool AnimatorGraphPanel::TransitionSegment(const AnimatorComponent& c, int index, ImVec2& a, ImVec2& b) const
    {
        const AnimatorTransition& tr = c.Transitions[(size_t)index];
        if (tr.To < 0 || tr.To >= (int)c.States.size() || tr.From >= (int)c.States.size() || tr.From == tr.To)
            return false;
        glm::vec2 fmin, fmax, tmin, tmax;
        NodeRect(c, tr.From, fmin, fmax);
        NodeRect(c, tr.To, tmin, tmax);
        ImVec2 p0 = ToScreen((fmin + fmax) * 0.5f), p1 = ToScreen((tmin + tmax) * 0.5f);
        // Both ways between two states: each arrow to its own side.
        bool reverse = false;
        for (const auto& other : c.Transitions)
            reverse |= other.From == tr.To && other.To == tr.From;
        if (reverse)
        {
            ImVec2 d(p1.x - p0.x, p1.y - p0.y);
            const float len = std::sqrt(d.x * d.x + d.y * d.y);
            if (len > 1e-3f)
            {
                const float o = 7.0f * std::max(0.6f, m_Zoom);
                ImVec2 n(-d.y / len * o, d.x / len * o);
                p0 = ImVec2(p0.x + n.x, p0.y + n.y);
                p1 = ImVec2(p1.x + n.x, p1.y + n.y);
            }
        }
        a = p0;
        b = p1;
        return true;
    }

    int AnimatorGraphPanel::TransitionAt(const AnimatorComponent& c, const ImVec2& p) const
    {
        int best = -1;
        float bestDistance = 7.0f;
        for (int i = 0; i < (int)c.Transitions.size(); i++)
        {
            ImVec2 a, b;
            if (!TransitionSegment(c, i, a, b))
                continue;
            ImVec2 ab(b.x - a.x, b.y - a.y), ap(p.x - a.x, p.y - a.y);
            const float len2 = ab.x * ab.x + ab.y * ab.y;
            const float t = len2 > 0.0f ? std::clamp((ap.x * ab.x + ap.y * ab.y) / len2, 0.0f, 1.0f) : 0.0f;
            const float dx = a.x + ab.x * t - p.x, dy = a.y + ab.y * t - p.y;
            const float d = std::sqrt(dx * dx + dy * dy);
            if (d < bestDistance)
            {
                bestDistance = d;
                best = i;
            }
        }
        return best;
    }

    void AnimatorGraphPanel::AutoLayout(AnimatorComponent& c)
    {
        if (c.States.empty())
            return;
        // Only when the positions say nothing (all in one place: an older scene, states added in code).
        bool same = true;
        for (const auto& st : c.States)
            same &= st.GraphPosition == c.States.front().GraphPosition;
        if (!same || (c.States.size() == 1 && c.States.front().GraphPosition != glm::vec2(0.0f)))
            return;
        const int columns = std::max(2, (int)std::ceil(std::sqrt((float)c.States.size())));
        for (size_t i = 0; i < c.States.size(); i++)
            c.States[i].GraphPosition = { 240.0f * (float)(i % (size_t)columns), 120.0f * (float)(i / (size_t)columns) };
        c.AnyStateGraphPosition = { -220.0f, 8.0f };
    }

    void AnimatorGraphPanel::FitView(const AnimatorComponent& c, const ImVec2& size)
    {
        glm::vec2 lo(1e9f), hi(-1e9f);
        for (int i = -1; i < (int)c.States.size(); i++)
        {
            glm::vec2 min, max;
            NodeRect(c, i, min, max);
            lo = glm::min(lo, min);
            hi = glm::max(hi, max);
        }
        if (lo.x > hi.x)
            return;
        const float margin = 24.0f;
        const glm::vec2 extent = glm::max(hi - lo, glm::vec2(1.0f));
        m_Zoom = std::clamp(std::min((size.x - margin * 2.0f) / extent.x, (size.y - margin * 2.0f) / extent.y), 0.5f, 1.25f);
        const glm::vec2 center = (lo + hi) * 0.5f;
        m_Pan = ImVec2(size.x * 0.5f - center.x * m_Zoom, size.y * 0.5f - center.y * m_Zoom);
    }

    // ------------------------------------------------------------------ edits
    void AnimatorGraphPanel::CreateTransition(AnimatorComponent& c, int from, int to)
    {
        if (to < 0 || to >= (int)c.States.size() || from == to)
            return;
        AnimatorTransition tr;
        tr.From = from;
        tr.To = to;
        tr.Fade = c.DefaultFade;
        c.Transitions.push_back(tr);
        m_Selection = { Kind::Transition, (int)c.Transitions.size() - 1 };
        MarkModified();
    }

    void AnimatorGraphPanel::DeleteSelection(AnimatorComponent& c)
    {
        if (m_Selection.Type == Kind::State && m_Selection.Index >= 0 && m_Selection.Index < (int)c.States.size())
        {
            AnimatorEditing::RemoveState(c, m_Selection.Index);
            MarkModified();
        }
        else if (m_Selection.Type == Kind::Transition && m_Selection.Index >= 0 && m_Selection.Index < (int)c.Transitions.size())
        {
            c.Transitions.erase(c.Transitions.begin() + m_Selection.Index);
            MarkModified();
        }
        m_Selection = {};
    }

    // ------------------------------------------------------------------ window
    void AnimatorGraphPanel::OnImGuiRender(bool* show)
    {
        if (!*show)
            return;
        if (m_FocusRequested)
        {
            ImGui::SetNextWindowFocus();
            m_FocusRequested = false;
        }
        ImGui::SetNextWindowSize(ImVec2(1000.0f, 420.0f), ImGuiCond_FirstUseEver);
        if (m_DefaultDock)
            ImGui::SetNextWindowDockID(m_DefaultDock, ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(Tr(ICON_FA_DIAGRAM_PROJECT "  Animator grafi###AnimatorGraph"), show))
        {
            ImGui::End();
            return;
        }
        Entity entity = GetEntity();
        AnimatorComponent* c = entity ? &entity.GetComponent<AnimatorComponent>() : nullptr;
        DrawToolbar(c);
        if (!c)
        {
            ImVec2 avail = ImGui::GetContentRegionAvail();
            const char* text = "Animator komponenti bo'lgan obyektni tanlang (ierarxiyada)";
            ImVec2 ts = ImGui::CalcTextSize(text);
            ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + std::max(0.0f, (avail.x - ts.x) * 0.5f),
                                       ImGui::GetCursorPosY() + std::max(0.0f, (avail.y - ts.y) * 0.5f)));
            ImGui::TextDisabled("%s", text);
            ImGui::End();
            return;
        }
        auto* mr = entity.TryGetComponent<MeshRendererComponent>();
        const Model* model = mr && mr->ModelAsset ? mr->ModelAsset.get() : nullptr;
        if (m_LaidOutFor != m_EntityID)
        {
            AutoLayout(*c);
            m_LaidOutFor = m_EntityID;
            m_FitRequested = true;
        }
        // Drop a selection that no longer exists (undo, the inspector removed it).
        if ((m_Selection.Type == Kind::State && m_Selection.Index >= (int)c->States.size()) ||
            (m_Selection.Type == Kind::Transition && m_Selection.Index >= (int)c->Transitions.size()))
            m_Selection = {};

        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const float side = std::clamp(m_SideWidth, 240.0f, std::max(240.0f, avail.x * 0.55f));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(28, 30, 35, 255));
        ImGui::BeginChild("##canvas", ImVec2(std::max(100.0f, avail.x - side - spacing), avail.y), ImGuiChildFlags_Borders,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove);
        ImGui::PopStyleColor();
        DrawCanvas(*c, model);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##side", ImVec2(side, avail.y), ImGuiChildFlags_Borders);
        DrawSidePanel(*c, model);
        ImGui::EndChild();
        ImGui::End();
    }

    void AnimatorGraphPanel::DrawToolbar(AnimatorComponent* c)
    {
        Entity entity = GetEntity();
        ImGui::AlignTextToFramePadding();
        if (entity)
            ImGui::TextColored(UI::AccentColor(), ICON_FA_PERSON_RUNNING "  %s", entity.GetName().c_str());
        else
            ImGui::TextDisabled(ICON_FA_PERSON_RUNNING "  (obyekt yo'q)");
        ImGui::SameLine();
        if (UI::ToolbarButton(m_Pinned ? ICON_FA_LOCK : ICON_FA_LOCK_OPEN, m_Pinned ? "Mahkamlangan: tanlov o'zgarsa ham shu obyekt"
                                                                                    : "Ierarxiya tanlovini kuzatadi (bosib mahkamlash)", m_Pinned))
            m_Pinned = !m_Pinned;
        if (!c)
            return;
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        auto center = [&]() { return ToGraph(ImVec2(m_CanvasMin.x + 200.0f, m_CanvasMin.y + 80.0f)); };
        auto mr = entity.TryGetComponent<MeshRendererComponent>();
        const Model* model = mr && mr->ModelAsset ? mr->ModelAsset.get() : nullptr;
        if (ImGui::Button(Tr(ICON_FA_PLUS "  Holat")))
        {
            glm::vec2 p = center();
            m_Selection = { Kind::State, AnimatorEditing::AddState(*c, model, false, glm::round(p / s_Snap) * s_Snap) };
            MarkModified();
        }
        ImGui::SameLine();
        if (ImGui::Button(Tr(ICON_FA_SHUFFLE "  Blend tree")))
        {
            glm::vec2 p = center();
            m_Selection = { Kind::State, AnimatorEditing::AddState(*c, model, true, glm::round(p / s_Snap) * s_Snap) };
            MarkModified();
        }
        if (ImGui::IsItemHovered())
            UI::Tooltip("1D blend tree: float parametr bo'yicha kliplar aralashadi (masalan Speed bo'yicha turish / yurish / yugurish)");
        ImGui::SameLine();
        if (ImGui::Button(Tr(ICON_FA_EXPAND "  Moslash")))
            m_FitRequested = true;
        if (ImGui::IsItemHovered())
            UI::Tooltip("Hamma node'lar ko'rinadigan qilish (F)");
        ImGui::SameLine();
        ImGui::TextDisabled(ICON_FA_CIRCLE_INFO);
        if (ImGui::IsItemHovered())
            UI::Tooltip("Sichqoncha: node'ni sudrash - joyini o'zgartirish; Shift + sudrash (yoki node menyusi) - o'tish chizish; "
                        "strelka yoki node'ni bosish - o'ng panelda tahrirlash; Delete - o'chirish; o'rta / o'ng tugma bilan sudrash - "
                        "surish; g'ildirak - masshtab; o'ng tugma - menyu");
        if (!m_Editable)
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.3f, 1.0f), ICON_FA_PLAY "  O'yin: jonli holat (o'zgarishlar saqlanmaydi)");
        }
    }

    void AnimatorGraphPanel::DrawCanvas(AnimatorComponent& c, const Model* model)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 min = ImGui::GetCursorScreenPos();
        const ImVec2 size = ImGui::GetContentRegionAvail();
        const ImVec2 max(min.x + size.x, min.y + size.y);
        m_CanvasMin = min;
        if (m_FitRequested && size.x > 50.0f && size.y > 50.0f)
        {
            FitView(c, size);
            m_FitRequested = false;
        }
        ImGui::InvisibleButton("##graph", ImVec2(std::max(1.0f, size.x), std::max(1.0f, size.y)),
                               ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
        const bool hovered = ImGui::IsItemHovered();
        const bool active = ImGui::IsItemActive();
        ImGuiIO& io = ImGui::GetIO();
        const glm::vec2 mouse = ToGraph(io.MousePos);
        dl->PushClipRect(min, max, true);

        // ---- view: pan (middle / right drag), zoom (wheel, around the mouse)
        if (active && (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f) || ImGui::IsMouseDragging(ImGuiMouseButton_Right, 4.0f)))
        {
            m_Pan.x += io.MouseDelta.x;
            m_Pan.y += io.MouseDelta.y;
        }
        if (hovered && io.MouseWheel != 0.0f)
        {
            const float zoom = std::clamp(m_Zoom * std::pow(1.12f, io.MouseWheel), 0.3f, 2.0f);
            // The point under the mouse stays there.
            m_Pan.x = io.MousePos.x - min.x - mouse.x * zoom;
            m_Pan.y = io.MousePos.y - min.y - mouse.y * zoom;
            m_Zoom = zoom;
        }

        // ---- grid
        {
            const float step = 20.0f * m_Zoom;
            if (step > 4.0f)
            {
                const float ox = std::fmod(m_Pan.x, step * 5.0f), oy = std::fmod(m_Pan.y, step * 5.0f);
                int k = 0;
                for (float x = min.x + ox - step * 5.0f; x < max.x; x += step, k++)
                    dl->AddLine(ImVec2(x, min.y), ImVec2(x, max.y), k % 5 == 0 ? s_GridMajor : s_GridColor);
                k = 0;
                for (float y = min.y + oy - step * 5.0f; y < max.y; y += step, k++)
                    dl->AddLine(ImVec2(min.x, y), ImVec2(max.x, y), k % 5 == 0 ? s_GridMajor : s_GridColor);
            }
        }

        // ---- interaction with nodes and arrows (left mouse)
        const int nodeUnder = hovered ? NodeAt(c, mouse) : -2;
        const int linkUnder = hovered && nodeUnder == -2 ? TransitionAt(c, io.MousePos) : -1;
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            if (m_LinkFrom != -2 && m_LinkByClick)
            {
                // Picking the target of a transition started from the menu.
                if (nodeUnder >= 0)
                    CreateTransition(c, m_LinkFrom, nodeUnder);
                m_LinkFrom = -2;
                m_LinkByClick = false;
            }
            else if (nodeUnder != -2)
            {
                m_Selection = { nodeUnder < 0 ? Kind::AnyState : Kind::State, nodeUnder };
                if (io.KeyShift)
                    m_LinkFrom = nodeUnder; // a new transition, dropped on its target
                else
                {
                    m_DragNode = nodeUnder;
                    glm::vec2 nmin, nmax;
                    NodeRect(c, nodeUnder, nmin, nmax);
                    m_DragOffset = mouse - nmin;
                }
            }
            else if (linkUnder >= 0)
                m_Selection = { Kind::Transition, linkUnder };
            else
                m_Selection = {};
        }
        if (m_DragNode != -2)
        {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && (m_DragNode < 0 || m_DragNode < (int)c.States.size()))
            {
                if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f))
                {
                    glm::vec2 p = mouse - m_DragOffset;
                    if (!io.KeyCtrl) // Ctrl: free, else on the grid
                        p = glm::round(p / s_Snap) * s_Snap;
                    glm::vec2& at = m_DragNode < 0 ? c.AnyStateGraphPosition : c.States[(size_t)m_DragNode].GraphPosition;
                    if (p != at)
                    {
                        at = p;
                        MarkModified();
                    }
                }
            }
            else
                m_DragNode = -2;
        }
        if (m_LinkFrom != -2 && !m_LinkByClick && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            const int target = NodeAt(c, mouse);
            if (target >= 0)
                CreateTransition(c, m_LinkFrom, target);
            m_LinkFrom = -2;
        }
        if (m_LinkFrom != -2 && (ImGui::IsKeyPressed(ImGuiKey_Escape) || m_LinkFrom >= (int)c.States.size()))
        {
            m_LinkFrom = -2;
            m_LinkByClick = false;
        }

        // ---- context menus (right click without dragging)
        if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right) && !ImGui::IsMouseDragPastThreshold(ImGuiMouseButton_Right, 4.0f))
        {
            m_ContextNode = nodeUnder;
            m_ContextTransition = linkUnder;
            m_ContextPoint = mouse;
            ImGui::OpenPopup("##graphMenu");
        }
        if (ImGui::BeginPopup("##graphMenu"))
        {
            if (m_ContextNode >= 0 && m_ContextNode < (int)c.States.size())
            {
                ImGui::TextDisabled("%s", c.States[(size_t)m_ContextNode].Name.c_str());
                ImGui::Separator();
                if (ImGui::MenuItem(Tr(ICON_FA_ARROW_RIGHT "  O'tish yaratish")))
                {
                    m_LinkFrom = m_ContextNode;
                    m_LinkByClick = true;
                }
                if (ImGui::MenuItem(Tr(ICON_FA_FLAG "  Boshlang'ich holat qilish"), nullptr, false, c.DefaultState != m_ContextNode))
                {
                    c.DefaultState = m_ContextNode;
                    if (m_Editable)
                        c.CurrentState = -1; // the editor previews it
                    MarkModified();
                }
                if (ImGui::MenuItem(Tr(ICON_FA_TRASH "  O'chirish"), "Delete"))
                {
                    m_Selection = { Kind::State, m_ContextNode };
                    DeleteSelection(c);
                }
            }
            else if (m_ContextNode == -1)
            {
                ImGui::TextDisabled("Har qanday holat");
                ImGui::Separator();
                if (ImGui::MenuItem(Tr(ICON_FA_ARROW_RIGHT "  O'tish yaratish")))
                {
                    m_LinkFrom = -1;
                    m_LinkByClick = true;
                }
            }
            else if (m_ContextTransition >= 0 && m_ContextTransition < (int)c.Transitions.size())
            {
                const auto& tr = c.Transitions[(size_t)m_ContextTransition];
                ImGui::TextDisabled("%s  " ICON_FA_ARROW_RIGHT "  %s", AnimatorEditing::StateName(c, tr.From).c_str(),
                                    AnimatorEditing::StateName(c, tr.To).c_str());
                ImGui::Separator();
                if (ImGui::MenuItem(Tr(ICON_FA_TRASH "  O'tishni o'chirish"), "Delete"))
                {
                    m_Selection = { Kind::Transition, m_ContextTransition };
                    DeleteSelection(c);
                }
            }
            else
            {
                const glm::vec2 at = glm::round(m_ContextPoint / s_Snap) * s_Snap;
                if (ImGui::MenuItem(Tr(ICON_FA_PLUS "  Yangi holat")))
                {
                    m_Selection = { Kind::State, AnimatorEditing::AddState(c, model, false, at) };
                    MarkModified();
                }
                if (ImGui::MenuItem(Tr(ICON_FA_SHUFFLE "  Yangi blend tree")))
                {
                    m_Selection = { Kind::State, AnimatorEditing::AddState(c, model, true, at) };
                    MarkModified();
                }
                ImGui::Separator();
                if (ImGui::MenuItem(Tr(ICON_FA_EXPAND "  Ko'rinishni moslash"), "F"))
                    m_FitRequested = true;
            }
            ImGui::EndPopup();
        }

        // ---- keys (the window has focus, no text field active)
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::IsAnyItemActive() && !io.WantTextInput)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && m_Selection.Type != Kind::None && m_Selection.Type != Kind::AnyState)
                DeleteSelection(c);
            if (ImGui::IsKeyPressed(ImGuiKey_F, false) && hovered)
                m_FitRequested = true;
        }

        // ---- arrows (behind the nodes)
        const float thickness = std::clamp(2.0f * m_Zoom, 1.2f, 3.0f);
        auto arrow = [&](ImVec2 a, ImVec2 b, ImU32 color, float width, int count) {
            dl->AddLine(a, b, color, width);
            ImVec2 d(b.x - a.x, b.y - a.y);
            const float len = std::sqrt(d.x * d.x + d.y * d.y);
            if (len < 1.0f)
                return;
            d = ImVec2(d.x / len, d.y / len);
            const ImVec2 n(-d.y, d.x);
            const float s = std::clamp(7.0f * m_Zoom, 5.0f, 10.0f);
            // One head in the middle (three for several transitions on the same arrow).
            for (int k = 0; k < std::min(count, 3); k++)
            {
                const float along = (float)(k - (std::min(count, 3) - 1) * 0.5f) * s * 1.4f;
                const ImVec2 m(a.x + (b.x - a.x) * 0.5f + d.x * along, a.y + (b.y - a.y) * 0.5f + d.y * along);
                dl->AddTriangleFilled(ImVec2(m.x + d.x * s, m.y + d.y * s), ImVec2(m.x - d.x * s * 0.6f + n.x * s * 0.8f, m.y - d.y * s * 0.6f + n.y * s * 0.8f),
                                      ImVec2(m.x - d.x * s * 0.6f - n.x * s * 0.8f, m.y - d.y * s * 0.6f - n.y * s * 0.8f), color);
            }
        };
        const bool fading = c.IsFading() && c.UsesStateMachine();
        for (int i = 0; i < (int)c.Transitions.size(); i++)
        {
            ImVec2 a, b;
            if (!TransitionSegment(c, i, a, b))
                continue;
            const auto& tr = c.Transitions[(size_t)i];
            int count = 0, first = -1;
            for (int k = 0; k < (int)c.Transitions.size(); k++)
                if (c.Transitions[(size_t)k].From == tr.From && c.Transitions[(size_t)k].To == tr.To)
                {
                    count++;
                    if (first < 0) first = k;
                }
            if (first != i && !(m_Selection.Type == Kind::Transition && m_Selection.Index == i))
                continue; // drawn once per pair (the selected one on top)
            const bool selected = m_Selection.Type == Kind::Transition && m_Selection.Index == i;
            const bool live = fading && tr.To == c.CurrentState && (tr.From < 0 || tr.From == c.PreviousState);
            ImU32 color = selected ? UI::AccentU32() : live ? s_FadeColor : (linkUnder == i ? IM_COL32(240, 240, 250, 255) : s_LinkColor);
            arrow(a, b, color, selected || live ? thickness * 1.6f : thickness, count);
        }
        // A transition being drawn.
        if (m_LinkFrom != -2)
        {
            glm::vec2 fmin, fmax;
            NodeRect(c, m_LinkFrom, fmin, fmax);
            arrow(ToScreen((fmin + fmax) * 0.5f), io.MousePos, UI::AccentU32(0.9f), thickness * 1.4f, 1);
        }

        // ---- nodes
        const float rounding = 6.0f * m_Zoom;
        ImFont* font = ImGui::GetFont();
        const float fontSize = ImGui::GetFontSize() * std::clamp(m_Zoom, 0.8f, 1.3f);
        auto text = [&](ImVec2 at, ImU32 color, const std::string& s, float maxWidth, ImFont* f = nullptr) {
            ImGui::PushClipRect(at, ImVec2(at.x + maxWidth, at.y + fontSize * 1.5f), true);
            dl->AddText(f ? f : font, fontSize, at, color, s.c_str());
            ImGui::PopClipRect();
        };
        {
            // The any-state node.
            glm::vec2 nmin, nmax;
            NodeRect(c, -1, nmin, nmax);
            const ImVec2 a = ToScreen(nmin), b = ToScreen(nmax);
            const bool selected = m_Selection.Type == Kind::AnyState;
            dl->AddRectFilled(a, b, s_AnyColor, rounding);
            dl->AddRect(a, b, selected ? IM_COL32(255, 255, 255, 230) : IM_COL32(255, 255, 255, 50), rounding, 0, selected ? 2.5f : 1.0f);
            const std::string label = ICON_FA_ASTERISK "  Har qanday holat";
            const ImVec2 ts = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, label.c_str());
            text(ImVec2((a.x + b.x - ts.x) * 0.5f, (a.y + b.y - ts.y) * 0.5f), IM_COL32(235, 250, 250, 255), label, b.x - a.x);
        }
        const AnimatorState* tree = c.GetBlendTreeState();
        for (int i = 0; i < (int)c.States.size(); i++)
        {
            const AnimatorState& st = c.States[(size_t)i];
            glm::vec2 nmin, nmax;
            NodeRect(c, i, nmin, nmax);
            const ImVec2 a = ToScreen(nmin), b = ToScreen(nmax);
            if (b.x < min.x || a.x > max.x || b.y < min.y || a.y > max.y)
                continue;
            const bool selected = m_Selection.Type == Kind::State && m_Selection.Index == i;
            const bool current = c.CurrentState == i && c.UsesStateMachine();
            const bool previous = fading && c.PreviousState == i;
            const float header = std::max(fontSize + 6.0f * m_Zoom, (b.y - a.y) * 0.48f);
            // Shadow, body, header.
            dl->AddRectFilled(ImVec2(a.x + 3.0f, a.y + 4.0f), ImVec2(b.x + 3.0f, b.y + 4.0f), IM_COL32(0, 0, 0, 90), rounding);
            dl->AddRectFilled(a, b, s_NodeColor, rounding);
            dl->AddRectFilled(a, ImVec2(b.x, a.y + header), c.DefaultState == i ? s_DefaultHeader : s_NodeHeader, rounding, ImDrawFlags_RoundCornersTop);
            const std::string title = std::format("{}  {}", st.IsBlendTree() ? ICON_FA_SHUFFLE : ICON_FA_FILM, st.Name);
            text(ImVec2(a.x + 8.0f * m_Zoom, a.y + (header - fontSize) * 0.5f), IM_COL32(245, 245, 250, 255), title, b.x - a.x - 12.0f * m_Zoom,
                 ImGuiLayer::GetBoldFont());
            // A blend tree playing: its weights as small bars at the right of the body.
            const bool weights = current && tree == &st && c.BlendWeights.size() == st.Motions.size() && !st.Motions.empty();
            const float barW = std::max(3.0f, 5.0f * m_Zoom), barGap = std::max(2.0f, 3.0f * m_Zoom);
            const float weightsW = weights ? (barW + barGap) * (float)st.Motions.size() + 6.0f * m_Zoom : 0.0f;
            std::string sub;
            if (st.IsBlendTree())
                sub = std::format("{} ({} klip)", st.BlendParameter.empty() ? "?" : st.BlendParameter.c_str(), st.Motions.size());
            else
                sub = st.Clip.empty() ? std::string("(birinchi klip)") : st.Clip;
            if (c.DefaultState == i)
                sub += "  " ICON_FA_FLAG;
            text(ImVec2(a.x + 8.0f * m_Zoom, a.y + header + (b.y - a.y - header - fontSize) * 0.5f - 2.0f * m_Zoom), IM_COL32(175, 180, 190, 255),
                 sub, b.x - a.x - 12.0f * m_Zoom - weightsW);
            if (weights)
            {
                const float top = a.y + header + 4.0f * m_Zoom, bottom = b.y - std::max(3.0f, 4.0f * m_Zoom) - 6.0f;
                for (size_t k = 0; k < st.Motions.size(); k++)
                {
                    const float x1 = b.x - 6.0f * m_Zoom - (barW + barGap) * (float)(st.Motions.size() - 1 - k);
                    const float h = std::max(1.0f, (bottom - top) * std::clamp(c.BlendWeights[k], 0.0f, 1.0f));
                    dl->AddRectFilled(ImVec2(x1 - barW, top), ImVec2(x1, bottom), IM_COL32(0, 0, 0, 90), 1.0f);
                    dl->AddRectFilled(ImVec2(x1 - barW, bottom - h), ImVec2(x1, bottom), IM_COL32(110, 170, 255, 235), 1.0f);
                }
            }
            // The state playing now: its progress along the bottom; a blend tree's weights as ticks.
            if (current)
            {
                const float n = c.GetNormalizedTime(model);
                const float f = c.Loop ? n - std::floor(n) : std::clamp(n, 0.0f, 1.0f);
                const float barH = std::max(3.0f, 4.0f * m_Zoom);
                dl->AddRectFilled(ImVec2(a.x + 4.0f, b.y - barH - 3.0f), ImVec2(b.x - 4.0f, b.y - 3.0f), IM_COL32(0, 0, 0, 120), 2.0f);
                dl->AddRectFilled(ImVec2(a.x + 4.0f, b.y - barH - 3.0f), ImVec2(a.x + 4.0f + (b.x - a.x - 8.0f) * f, b.y - 3.0f), UI::AccentU32(), 2.0f);
            }
            ImU32 border = selected ? IM_COL32(255, 255, 255, 235) : current ? UI::AccentU32() : previous ? s_FadeColor
                         : nodeUnder == i ? IM_COL32(255, 255, 255, 110) : IM_COL32(255, 255, 255, 40);
            dl->AddRect(a, b, border, rounding, 0, selected || current ? 2.5f : previous ? 2.0f : 1.0f);
        }
        // A hint over an empty graph / while drawing a transition.
        if (c.States.empty() || m_LinkFrom != -2)
        {
            const char* hint = m_LinkFrom != -2 ? "O'tish: maqsad holatni bosing (Esc - bekor)"
                                                : "Holatlar yo'q: o'ng tugma - Yangi holat / Yangi blend tree";
            dl->AddText(ImVec2(min.x + 12.0f, max.y - ImGui::GetFontSize() - 10.0f), IM_COL32(200, 200, 210, 200), hint);
        }
        // Zoom in the corner.
        {
            const std::string z = std::format("{:.0f}%", m_Zoom * 100.0f);
            const ImVec2 ts = ImGui::CalcTextSize(z.c_str());
            dl->AddText(ImVec2(max.x - ts.x - 10.0f, max.y - ts.y - 8.0f), IM_COL32(150, 150, 160, 200), z.c_str());
        }
        dl->PopClipRect();
    }

    void AnimatorGraphPanel::DrawSidePanel(AnimatorComponent& c, const Model* model)
    {
        bool changed = false;
        const bool live = !m_Editable;
        switch (m_Selection.Type)
        {
            case Kind::State:
                if (m_Selection.Index >= 0 && m_Selection.Index < (int)c.States.size())
                {
                    ImGui::SeparatorText(c.States[(size_t)m_Selection.Index].IsBlendTree() ? ICON_FA_SHUFFLE "  Blend tree holati" : ICON_FA_FILM "  Holat");
                    changed |= AnimatorEditing::DrawState(c, m_Selection.Index, model);
                    ImGui::Spacing();
                    // Its transitions, to pick one.
                    int out = 0;
                    for (int i = 0; i < (int)c.Transitions.size(); i++)
                    {
                        const auto& tr = c.Transitions[(size_t)i];
                        if (tr.From != m_Selection.Index)
                            continue;
                        if (out++ == 0)
                            ImGui::TextDisabled("Chiquvchi o'tishlar");
                        ImGui::PushID(i);
                        std::string label = std::format(ICON_FA_ARROW_RIGHT "  {}  ({} shart)", AnimatorEditing::StateName(c, tr.To), tr.Conditions.size());
                        if (ImGui::Selectable(label.c_str()))
                            m_Selection = { Kind::Transition, i };
                        ImGui::PopID();
                    }
                    if (m_Selection.Type == Kind::State)
                    {
                        if (ImGui::Button(Tr(ICON_FA_ARROW_RIGHT "  O'tish yaratish")))
                        {
                            m_LinkFrom = m_Selection.Index;
                            m_LinkByClick = true;
                        }
                        ImGui::SameLine();
                        if (ImGui::Button(Tr(ICON_FA_TRASH "  O'chirish")))
                            DeleteSelection(c);
                    }
                }
                break;
            case Kind::AnyState:
                ImGui::SeparatorText(Tr(ICON_FA_ASTERISK "  Har qanday holat"));
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextDisabled("Bu node'dan chiqqan o'tishlar qaysi holatda bo'lsa ham tekshiriladi (masalan sakrash, zarba olish).");
                ImGui::PopTextWrapPos();
                if (ImGui::Button(Tr(ICON_FA_ARROW_RIGHT "  O'tish yaratish")))
                {
                    m_LinkFrom = -1;
                    m_LinkByClick = true;
                }
                break;
            case Kind::Transition:
                if (m_Selection.Index >= 0 && m_Selection.Index < (int)c.Transitions.size())
                {
                    ImGui::SeparatorText(Tr(ICON_FA_ARROW_RIGHT "  O'tish"));
                    changed |= AnimatorEditing::DrawTransition(c, m_Selection.Index);
                    ImGui::Spacing();
                    if (ImGui::Button(Tr(ICON_FA_TRASH "  O'tishni o'chirish")))
                        DeleteSelection(c);
                }
                break;
            default:
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextDisabled(ICON_FA_CIRCLE_INFO "  Holat yoki o'tish strelkasini bosing - shu yerda tahrirlanadi. "
                                    "Shift bilan node'dan boshqasiga sudrash - yangi o'tish.");
                ImGui::PopTextWrapPos();
                break;
        }
        // The parameters and the live state below what is selected.
        ImGui::Spacing();
        if (ImGui::CollapsingHeader(live ? ICON_FA_SLIDERS "  Parametrlar (jonli)" : ICON_FA_SLIDERS "  Parametrlar", ImGuiTreeNodeFlags_DefaultOpen))
            changed |= AnimatorEditing::DrawParameters(c, live);
        if (c.UsesStateMachine() && ImGui::CollapsingHeader(live ? ICON_FA_PLAY "  Holat (jonli)" : ICON_FA_EYE "  Holat (ko'rib chiqish)",
                                                            ImGuiTreeNodeFlags_DefaultOpen))
            AnimatorEditing::DrawLiveStatus(c, model, false);
        if (changed)
        {
            if (m_Editable)
                c.CurrentState = c.CurrentState >= (int)c.States.size() ? -1 : c.CurrentState;
            MarkModified();
        }
    }

}
