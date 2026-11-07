// The game view's resolution while playing: presets (Free, aspect ratios, fixed resolutions, a
// custom one), the letterboxed / scaled picture in the viewport and the mouse mapped into it.
#include "EditorLayer.h"
#include "UI.h"

#include <algorithm>
#include <format>

namespace Zamin {

    // The game view's resolution: the chosen preset; "Free" with a project of a fixed aspect ratio (Project
    // settings > Player) is that ratio - Play looks like the player.
    static GameViewResolution EffectiveGameView(const GameViewResolution& chosen)
    {
        if (chosen.Preset != GameViewPreset::Free)
            return chosen;
        const PlayerSettings& player = Project::GetPlayerSettings();
        if (!player.FixedAspectRatio)
            return chosen;
        GameViewResolution aspect;
        aspect.Preset = GameViewPreset::Custom;
        aspect.CustomAspect = true;
        aspect.CustomSize = { (int)player.AspectWidth, (int)player.AspectHeight };
        return aspect;
    }

    bool EditorLayer::IsGameViewSized() const
    {
        return m_SceneState == SceneState::Play && EffectiveGameView(m_ViewSettings.GameView).Preset != GameViewPreset::Free;
    }

    void EditorLayer::PlaceViewportImage(ImVec2 areaMin, ImVec2 areaSize, ImTextureID texture)
    {
        const glm::vec2 area = { std::max(1.0f, areaSize.x), std::max(1.0f, areaSize.y) };
        m_ViewportPanelBounds[0] = { areaMin.x, areaMin.y };
        m_ViewportPanelBounds[1] = { areaMin.x + areaSize.x, areaMin.y + areaSize.y };
        if (!IsGameViewSized())
        {
            // The whole area, pixel for pixel.
            m_GameViewLayout = {};
            m_GameViewLayout.DisplaySize = area;
            m_GameViewLayout.RenderSize = glm::uvec2(area);
            m_ViewportBounds[0] = m_ViewportPanelBounds[0];
            m_ViewportBounds[1] = m_ViewportPanelBounds[1];
            m_ViewportSize = area;
            ImGui::SetCursorScreenPos(areaMin);
            ImGui::Image(texture, ImVec2(area.x, area.y), ImVec2(0, 1), ImVec2(1, 0));
            return;
        }

        // The bars around the picture, then the picture where the layout puts it.
        const GameViewResolution view = EffectiveGameView(m_ViewSettings.GameView);
        m_GameViewLayout = ComputeGameViewLayout(view, area);
        // (The project's own bars' colour when it is its aspect ratio.)
        ImU32 bars = IM_COL32(6, 7, 9, 255);
        if (m_ViewSettings.GameView.Preset == GameViewPreset::Free)
        {
            const glm::vec3& c = Project::GetPlayerSettings().LetterboxColor;
            bars = ImGui::ColorConvertFloat4ToU32(ImVec4(c.r, c.g, c.b, 1.0f));
        }
        ImGui::GetWindowDrawList()->AddRectFilled(areaMin, ImVec2(areaMin.x + area.x, areaMin.y + area.y), bars);
        const ImVec2 at(areaMin.x + m_GameViewLayout.Offset.x, areaMin.y + m_GameViewLayout.Offset.y);
        const ImVec2 size(m_GameViewLayout.DisplaySize.x, m_GameViewLayout.DisplaySize.y);
        m_ViewportBounds[0] = { at.x, at.y };
        m_ViewportBounds[1] = { at.x + size.x, at.y + size.y };
        m_ViewportSize = glm::vec2(m_GameViewLayout.RenderSize);
        ImGui::SetCursorScreenPos(at);
        ImGui::Image(texture, size, ImVec2(0, 1), ImVec2(1, 0));
        // The picture's frame: where the game ends.
        ImGui::GetWindowDrawList()->AddRect(ImVec2(at.x - 1.0f, at.y - 1.0f), ImVec2(at.x + size.x + 1.0f, at.y + size.y + 1.0f),
                                            IM_COL32(255, 255, 255, 28));
    }

    glm::vec2 EditorLayer::ScreenToViewportPixel(ImVec2 screen) const
    {
        const glm::vec2 shown = m_ViewportBounds[1] - m_ViewportBounds[0];
        const glm::vec2 local = glm::vec2(screen.x, screen.y) - m_ViewportBounds[0];
        if (shown.x <= 0.0f || shown.y <= 0.0f)
            return local;
        return local * (m_ViewportSize / shown);
    }

    bool EditorLayer::UI_GameViewResolutionItems()
    {
        auto& view = m_ViewSettings.GameView;
        bool changed = false;
        for (int i = 0; i < GameViewPresetCount; i++)
        {
            const GameViewPreset preset = (GameViewPreset)i;
            if (preset == GameViewPreset::Custom)
                ImGui::Separator();
            const bool current = view.Preset == preset;
            if (ImGui::MenuItem(GameViewResolution::GetName(preset), nullptr, current) && !current)
            {
                view.Preset = preset;
                changed = true;
            }
            if (preset == GameViewPreset::Free)
                ImGui::Separator();
        }
        // The custom size: pixels, or only its aspect ratio.
        if (view.Preset == GameViewPreset::Custom)
        {
            ImGui::Indent();
            int size[2] = { view.CustomSize.x, view.CustomSize.y };
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.0f);
            if (ImGui::DragInt2("##customSize", size, 1.0f, view.CustomAspect ? 1 : GameViewResolution::MinSize, GameViewResolution::MaxSize,
                                view.CustomAspect ? "%d" : "%d px"))
            {
                view.CustomSize = { size[0], size[1] };
                view.Sanitize();
                changed = true;
            }
            ImGui::SameLine();
            ImGui::TextDisabled(view.CustomAspect ? "nisbat" : "piksel");
            changed |= ImGui::Checkbox(Tr("Faqat nisbat (o'lchamga moslashadi)"), &view.CustomAspect);
            ImGui::Unindent();
        }
        return changed;
    }

    void EditorLayer::UI_GameViewResolutionPopup()
    {
        ImGui::SetNextWindowSizeConstraints(ImVec2(ImGui::GetFontSize() * 15.0f, 0), ImVec2(FLT_MAX, FLT_MAX));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 5));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 8));
        if (!ImGui::BeginPopup("##GameViewResolution"))
        {
            ImGui::PopStyleVar(2);
            return;
        }
        ImGui::TextColored(UI::AccentColor(), ICON_FA_DESKTOP "  O'yin oynasi o'lchami");
        ImGui::Separator();
        if (UI_GameViewResolutionItems())
            MarkViewSettingsDirty();
        if (IsGameViewSized())
        {
            ImGui::Separator();
            const GameViewLayout& layout = m_GameViewLayout;
            ImGui::TextDisabled("O'yin %ux%u da chiziladi, ko'rinishi %.0fx%.0f (%.0f%%)", layout.RenderSize.x, layout.RenderSize.y,
                                layout.DisplaySize.x, layout.DisplaySize.y, layout.Scale * 100.0f);
        }
        ImGui::EndPopup();
        ImGui::PopStyleVar(2);
    }

}
