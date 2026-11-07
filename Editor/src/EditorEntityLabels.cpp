// Entity names over the scene views (Ko'rinish > Obyekt nomlari, the viewport toolbar's names
// button): the selection's or every entity's name above it, fading out with the distance from the
// camera; where labels would overlap the selection, the hovered entity and the nearest are kept.
// A click on a name selects its entity.
#include "EditorLayer.h"
#include "UI.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <format>

namespace Zamin {

    void EditorLayer::UI_EntityLabels(const SceneViewTarget& view, const glm::mat4& viewProjection, const glm::vec3& cameraPosition, bool orthographic)
    {
        const EntityLabelSettings& settings = m_ViewSettings.EntityLabels;
        if (!settings.IsEnabled() || !m_ActiveScene)
            return;
        Scene& scene = *m_ActiveScene;
        const glm::vec2 size = view.Max - view.Min;
        if (size.x < 1.0f || size.y < 1.0f)
            return;

        // Who gets a name: the selection or everyone, never hidden / inactive entities nor the
        // screen-space UI (it is placed in pixels, not in the world).
        SceneRenderOptions skipped;
        skipped.SkipEditorHidden = m_SceneState != SceneState::Play;
        const std::unordered_set<entt::entity> hidden = scene.CollectRenderSkipped(skipped);
        std::vector<Entity> entities;
        if (settings.Mode == EntityLabelMode::Selected)
        {
            for (Entity e : m_SceneHierarchyPanel.GetSelection().GetEntities())
                if (e.IsValid() && e.GetScene() == &scene)
                    entities.push_back(e);
        }
        else
            for (auto e : scene.GetAllEntitiesWith<TransformComponent>())
                entities.emplace_back(e, &scene);

        const Entity hovered = !view.Hovered ? Entity()
                             : view.Index == 0 ? m_HoveredEntity : m_ExtraViews[view.Index - 1].HoveredEntity;
        ImFont* font = ImGui::GetFont();
        const float fontSize = std::round(ImGui::GetFontSize() * settings.Scale);
        const glm::vec2 padding = glm::vec2(5.0f, 2.0f) * settings.Scale;
        const float lift = 9.0f * settings.Scale; // the label sits above the point it names

        std::vector<EntityLabelCandidate> candidates;
        std::vector<Entity> owners;
        std::vector<std::string> texts;
        candidates.reserve(entities.size());
        for (Entity e : entities)
        {
            if (hidden.contains((entt::entity)e) || e.HasComponent<UITransformComponent>())
                continue;
            const glm::vec3 world = scene.GetWorldPosition(e);
            const glm::vec4 clip = viewProjection * glm::vec4(world, 1.0f);
            if (clip.w <= 1e-5f)
                continue; // behind the camera
            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
            if (std::abs(ndc.x) > 1.2f || std::abs(ndc.y) > 1.2f || (!orthographic && ndc.z > 1.0f))
                continue;
            const glm::vec2 point(view.Min.x + (ndc.x * 0.5f + 0.5f) * size.x, view.Min.y + (0.5f - ndc.y * 0.5f) * size.y);
            const bool selected = m_SceneHierarchyPanel.IsSelected(e);
            const float distance = glm::length(world - cameraPosition);
            EntityLabelCandidate c;
            c.Selected = selected;
            c.Hovered = hovered && (entt::entity)hovered == (entt::entity)e;
            c.Alpha = settings.GetAlpha(orthographic ? -1.0f : distance, selected);
            c.Distance = orthographic ? ndc.z : distance;
            std::string text = ShortenEntityLabel(e.GetName());
            const ImVec2 ts = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text.c_str());
            c.Min = { point.x - ts.x * 0.5f - padding.x, point.y - lift - ts.y - padding.y * 2.0f };
            c.Max = { point.x + ts.x * 0.5f + padding.x, point.y - lift };
            candidates.push_back(c);
            owners.push_back(e);
            texts.push_back(std::move(text));
        }

        // Not under the view's toolbar: labels reaching into its rows are left out.
        const float toolbarBottom = view.Index == 0 ? m_ViewportPanelBounds[0].y + m_ToolbarBottom
                                                    : view.Min.y + ImGui::GetFrameHeight() + 16.0f;
        if (toolbarBottom > view.Min.y)
            for (EntityLabelCandidate& c : candidates)
                if (c.Min.y < toolbarBottom)
                    c.Alpha = 0.0f;
        const std::vector<size_t> shown = LayoutEntityLabels(candidates, settings, view.Min, view.Max);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->PushClipRect(ImVec2(view.Min.x, view.Min.y), ImVec2(view.Max.x, view.Max.y), true);
        const ImVec2 mouse = ImGui::GetMousePos();
        const ImVec4 accent = UI::AccentColor();
        std::optional<size_t> clicked;
        for (size_t index : shown)
        {
            const EntityLabelCandidate& c = candidates[index];
            const float a = std::clamp(c.Alpha, 0.0f, 1.0f);
            const ImVec2 min(c.Min.x, c.Min.y), max(c.Max.x, c.Max.y);
            const bool mouseOver = view.Hovered && mouse.x >= min.x && mouse.x <= max.x && mouse.y >= min.y && mouse.y <= max.y;
            if (settings.Background)
                dl->AddRectFilled(min, max, IM_COL32(16, 18, 22, (int)(185.0f * a)), 4.0f * settings.Scale);
            if (c.Selected)
                dl->AddRect(min, max, ImGui::GetColorU32(ImVec4(accent.x, accent.y, accent.z, 0.9f * a)), 4.0f * settings.Scale, 0, 1.5f);
            else if (mouseOver || c.Hovered)
                dl->AddRect(min, max, IM_COL32(170, 210, 255, (int)(170.0f * a)), 4.0f * settings.Scale);
            // A tick from the label down to the point it names.
            const float midX = (min.x + max.x) * 0.5f;
            dl->AddLine(ImVec2(midX, max.y), ImVec2(midX, max.y + lift - 2.0f), IM_COL32(220, 224, 232, (int)(120.0f * a)), 1.0f);
            const ImU32 text = c.Selected ? ImGui::GetColorU32(ImVec4(accent.x * 0.5f + 0.5f, accent.y * 0.5f + 0.5f, accent.z * 0.5f + 0.5f, a))
                             : (mouseOver || c.Hovered) ? IM_COL32(255, 255, 255, (int)(255.0f * a))
                                                        : IM_COL32(224, 228, 236, (int)(235.0f * a));
            dl->AddText(font, fontSize, ImVec2(min.x + padding.x, min.y + padding.y), text, texts[index].c_str());
            if (mouseOver && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                clicked = index; // the last drawn (the one on top) wins
        }
        dl->PopClipRect();
        // A click on a name selects its entity (Ctrl toggles, Shift adds), over what the picture picked.
        const bool gizmoOver = view.Index == 0 ? m_GizmoHovered : m_ExtraViews[view.Index - 1].GizmoHovered;
        if (clicked && !ImGui::GetIO().KeyAlt && !m_GizmoEdit.IsActive() && !gizmoOver)
        {
            Entity e = owners[*clicked];
            if (!scene.IsLockedInEditor(e))
                m_SceneHierarchyPanel.SelectWithModifiers(e, ImGui::GetIO().KeyCtrl, ImGui::GetIO().KeyShift);
        }
    }

    bool EditorLayer::UI_EntityLabelSettings()
    {
        EntityLabelSettings& s = m_ViewSettings.EntityLabels;
        const float field = ImGui::GetFontSize() * 10.0f;
        bool changed = false;
        int mode = (int)s.Mode;
        changed |= ImGui::RadioButton(Tr("O'chiq"), &mode, (int)EntityLabelMode::Off);
        ImGui::SameLine();
        changed |= ImGui::RadioButton(Tr("Tanlanganlar"), &mode, (int)EntityLabelMode::Selected);
        ImGui::SameLine();
        changed |= ImGui::RadioButton(Tr("Hammasi"), &mode, (int)EntityLabelMode::All);
        s.Mode = (EntityLabelMode)mode;
        if (s.Mode != EntityLabelMode::Off)
            m_LabelToggleMode = s.Mode;

        ImGui::BeginDisabled(s.Mode == EntityLabelMode::Off);
        changed |= ImGui::Checkbox(Tr("Uzoqlashganda xiralashsin"), &s.Fade);
        ImGui::BeginDisabled(!s.Fade);
        ImGui::SetNextItemWidth(field);
        changed |= ImGui::DragFloatRange2("Masofa##fade", &s.FadeStart, &s.FadeEnd, 0.5f, 0.0f, 100000.0f, "%.0f m dan", "%.0f m gacha",
                                          ImGuiSliderFlags_AlwaysClamp);
        if (ImGui::IsItemHovered())
            UI::Tooltip("Kameradan shu masofagacha nom to'liq ko'rinadi, keyin asta yo'qoladi (ortografik ko'rinishda xiralashmaydi)");
        changed |= ImGui::Checkbox(Tr("Tanlanganlar ham xiralashsin"), &s.FadeSelected);
        ImGui::EndDisabled();
        changed |= ImGui::Checkbox(Tr("Ustma-ust tushganlarini yashirish"), &s.HideOverlapping);
        if (ImGui::IsItemHovered())
            UI::Tooltip("Nomlar bir-birini yopsa, tanlangan, sichqoncha ostidagi va eng yaqini qoladi");
        changed |= ImGui::Checkbox(Tr("Fon"), &s.Background);
        ImGui::SetNextItemWidth(field);
        changed |= ImGui::SliderFloat("O'lcham", &s.Scale, EntityLabelSettings::MinScale, EntityLabelSettings::MaxScale, "%.2fx");
        ImGui::SetNextItemWidth(field);
        changed |= ImGui::SliderInt("Ko'pi bilan", &s.MaxLabels, 10, 1000, "%d ta");
        changed |= ImGui::Checkbox(Tr("O'yin rejimida ham (viewport)"), &s.ShowInPlay);
        ImGui::EndDisabled();
        if (changed)
            s.Sanitize();
        return changed;
    }

    void EditorLayer::UI_EntityLabelsPopup()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 5));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 8));
        if (ImGui::BeginPopup("##EntityLabels"))
        {
            ImGui::TextColored(UI::AccentColor(), ICON_FA_TAG "  Obyekt nomlari");
            if (UI_EntityLabelSettings())
                MarkViewSettingsDirty();
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar(2);
    }

}
