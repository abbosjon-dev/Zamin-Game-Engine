// The main viewport's toolbar: the transform tools and snapping (or the game view's resolution
// while playing), the play controls, the camera (framing, bookmarks, the axis view, the camera
// settings) and the view options (2D, grid, view modes, colliders, debug drawing, the statistics
// overlay, icons and gizmos, entity names, local / global, maximize on play).
#include "EditorLayer.h"
#include "UI.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <format>

namespace Zamin {

    void EditorLayer::UI_ViewportToolbar()
    {
        ImVec2 regionMin = ImGui::GetWindowContentRegionMin();
        float width = m_ViewportPanelBounds[1].x - m_ViewportPanelBounds[0].x;
        float buttonSize = ImGui::GetFrameHeight() + 4.0f;
        const float spacing = 2.0f, caretWidth = std::floor(buttonSize * 0.55f);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(spacing, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);

        auto panelBg = [&](ImVec2 pos, float w) {
            ImVec2 a(m_ViewportPanelBounds[0].x + pos.x - 4, m_ViewportPanelBounds[0].y + pos.y - 4);
            ImVec2 b(a.x + w + 8, a.y + buttonSize + 8);
            dl->AddRectFilled(a, b, IM_COL32(18, 20, 24, 210), 7.0f);
        };
        auto beginPanel = [&](ImVec2 pos, float w) {
            panelBg(pos, w);
            ImGui::SetCursorPos(ImVec2(regionMin.x + pos.x, regionMin.y + pos.y));
        };
        ImVec2 size(buttonSize, buttonSize);
        bool editing = m_SceneState == SceneState::Edit;
        auto& snap = m_ViewSettings.Snap;
        auto& grid = m_ViewSettings.Grid;
        // The play controls sit in the middle; the side panels next to them are left out when
        // the viewport is too narrow (snapping / camera stay reachable from the "Ko'rinish" menu).
        // A narrow viewport: the play controls stay right of the tools, the view options' panel on
        // the right moves to a second row when it would cover them (the camera panel next to it,
        // or on a row of its own).
        const float rowStep = buttonSize + 14.0f;
        const float toolsW = buttonSize * 5 + spacing * 4 + caretWidth;
        const float viewW = buttonSize * 10 + caretWidth * 6 + spacing * 10;
        const float centerW = buttonSize * 4 + spacing * 3;
        const float centerX = std::max((width - centerW) * 0.5f, 10.0f + toolsW + 14.0f);
        const float centerMin = centerX - 12.0f, centerMax = centerX + centerW + 12.0f;
        const int viewRow = width - viewW - 10.0f > centerMax ? 0 : 1;
        const ImVec2 viewPos(std::max(width - viewW - 10, 10.0f), 10 + rowStep * viewRow);
        m_ToolbarBottom = viewPos.y + buttonSize + 4.0f;

        // ---- left: gizmo tools | snapping
        float leftEnd = 10.0f;
        {
            ImVec2 pos(10, 10);
            float w = buttonSize * 5 + spacing * 4 + caretWidth;
            beginPanel(pos, w);
            if (UI::ToolbarButton(ICON_FA_ARROW_POINTER, "Tanlash (Q)", m_GizmoMode == GizmoMode::None, size)) m_GizmoMode = GizmoMode::None;
            ImGui::SameLine();
            if (UI::ToolbarButton(ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT, "Ko'chirish (W)\nAlt+sudrash: nusxasini sudrash", m_GizmoMode == GizmoMode::Translate, size)) m_GizmoMode = GizmoMode::Translate;
            ImGui::SameLine();
            if (UI::ToolbarButton(ICON_FA_ROTATE, "Aylantirish (E)", m_GizmoMode == GizmoMode::Rotate, size)) m_GizmoMode = GizmoMode::Rotate;
            ImGui::SameLine();
            if (UI::ToolbarButton(ICON_FA_UP_RIGHT_AND_DOWN_LEFT_FROM_CENTER, "Masshtab (R)", m_GizmoMode == GizmoMode::Scale, size)) m_GizmoMode = GizmoMode::Scale;
            ImGui::SameLine();
            if (UI::ToolbarButton(m_GizmoPivotCenter ? ICON_FA_OBJECT_GROUP : ICON_FA_CIRCLE_DOT,
                                  m_GizmoPivotCenter ? "Gizmo: tanlov markazida (bosing: asosiy obyektda)"
                                                     : "Gizmo: asosiy obyektda (bosing: tanlov markazida)",
                                  m_GizmoPivotCenter, size))
                m_GizmoPivotCenter = !m_GizmoPivotCenter;
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                ImGui::OpenPopup("##TransformGizmo");
            ImGui::SameLine(0.0f, 0.0f);
            // The gizmo's size on screen (and its handles' thickness, the icons' size).
            const auto& tg = m_ViewSettings.TransformGizmo;
            const bool resized = tg.Size != 1.0f || tg.LineThickness != 1.0f || tg.IconSize != 1.0f;
            if (resized) ImGui::PushStyleColor(ImGuiCol_Text, UI::AccentColor());
            std::string tip = std::format("Gizmo o'lchami: x{:.2g} (chiziqlar x{:.2g}, ikonkalar x{:.2g})", tg.Size, tg.LineThickness, tg.IconSize);
            if (UI::ToolbarButton(ICON_FA_CARET_DOWN "###TransformGizmoMenu", tip.c_str(), resized, ImVec2(caretWidth, buttonSize)))
                ImGui::OpenPopup("##TransformGizmo");
            if (resized) ImGui::PopStyleColor();
            if (m_OpenTransformGizmoMenu)
            {
                ImGui::OpenPopup("##TransformGizmo");
                m_OpenTransformGizmoMenu = false;
            }
            // below the caret, its left edge under the caret's
            ImGui::SetNextWindowPos(ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y + 8.0f), ImGuiCond_Appearing);
            UI_TransformGizmoPopup();
            leftEnd = pos.x + w;
        }
        if (m_SceneState == SceneState::Play)
        {
            // While playing (no gizmos to snap): the game view's resolution, next to the tools. The
            // label shrinks to the icon when the viewport is narrow.
            const auto& gameView = m_ViewSettings.GameView;
            std::string label = std::string(ICON_FA_DESKTOP "  ") + gameView.GetLabel();
            if (IsGameViewSized() && m_GameViewLayout.Scale < 0.995f)
                label += std::format(" ({:.0f}%)", m_GameViewLayout.Scale * 100.0f);
            label += "  " ICON_FA_CARET_DOWN;
            const float padding = ImGui::GetStyle().FramePadding.x * 2.0f;
            ImVec2 pos(leftEnd + 14.0f, 10);
            float w = ImGui::CalcTextSize(label.c_str()).x + padding;
            if (pos.x + w >= centerMin)
            {
                label = ICON_FA_DESKTOP " " ICON_FA_CARET_DOWN;
                w = ImGui::CalcTextSize(label.c_str()).x + padding;
            }
            if (pos.x + w < centerMin)
            {
                beginPanel(pos, w);
                const bool sized = gameView.Preset != GameViewPreset::Free;
                std::string tip = std::format("O'yin oynasi o'lchami: {}\nErkin, 16:9, 16:10, 4:3, 21:9, 1920x1080, 1280x720 yoki boshqa",
                                              GameViewResolution::GetName(gameView.Preset));
                if (UI::ToolbarButton((label + "###GameViewResolution").c_str(), tip.c_str(), sized, ImVec2(w, buttonSize)))
                    ImGui::OpenPopup("##GameViewResolution");
                ImGui::SetNextWindowPos(ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y + 8.0f), ImGuiCond_Appearing);
            }
            if (m_OpenGameViewMenu)
            {
                ImGui::OpenPopup("##GameViewResolution");
                m_OpenGameViewMenu = false;
            }
            UI_GameViewResolutionPopup();
        }
        else
        {
            // The step of the current gizmo operation, e.g. "0.5", "15°", "x0.1".
            std::string step = m_GizmoMode == GizmoMode::Rotate ? std::format("{:g}\xc2\xb0", snap.Rotate)
                             : m_GizmoMode == GizmoMode::Scale  ? std::format("x{:g}", snap.Scale)
                                                                : std::format("{:g}", snap.Translate);
            std::string label = step + "  " ICON_FA_CARET_DOWN;
            float stepWidth = ImGui::CalcTextSize(label.c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0f;
            ImVec2 pos(leftEnd + 14.0f, 10);
            float w = buttonSize + spacing + stepWidth;
            if (pos.x + w < centerMin)
            {
                beginPanel(pos, w);
                if (UI::ToolbarButton(ICON_FA_MAGNET, snap.Enabled ? "Snapping yoqilgan (Ctrl bosib turish: vaqtincha o'chirish)"
                                                                   : "Snapping o'chiq (Ctrl bosib turish: vaqtincha yoqish)",
                                      snap.Enabled, size))
                {
                    snap.Enabled = !snap.Enabled;
                    MarkViewSettingsDirty();
                }
                ImGui::SameLine();
                if (UI::ToolbarButton((label + "###SnapStep").c_str(), "Snapping qadamlari (ko'chirish / aylantirish / masshtab)", false,
                                      ImVec2(stepWidth, buttonSize)))
                    ImGui::OpenPopup("##SnapSettings");
            }
            UI_SnapSettingsPopup();
        }

        // ---- center: play controls
        {
            float w = centerW;
            ImVec2 pos(centerX, 10);
            beginPanel(pos, w);

            ImGui::PushStyleColor(ImGuiCol_Text, m_SceneState == SceneState::Play ? ImVec4(0.95f, 0.4f, 0.4f, 1) : ImVec4(0.35f, 0.9f, 0.55f, 1));
            if (UI::ToolbarButton(m_SceneState == SceneState::Play ? ICON_FA_STOP : ICON_FA_PLAY,
                                  m_SceneState == SceneState::Play ? "To'xtatish (Ctrl+P)" : "O'ynash (Ctrl+P)", false, size))
                m_SceneState == SceneState::Play ? OnSceneStop() : OnScenePlay();
            ImGui::PopStyleColor();
            ImGui::SameLine();

            ImGui::PushStyleColor(ImGuiCol_Text, m_SceneState == SceneState::Simulate ? ImVec4(0.95f, 0.4f, 0.4f, 1) : ImVec4(0.45f, 0.7f, 1.0f, 1));
            if (UI::ToolbarButton(m_SceneState == SceneState::Simulate ? ICON_FA_STOP : ICON_FA_GEARS,
                                  m_SceneState == SceneState::Simulate ? "Simulyatsiyani to'xtatish" : "Fizikani simulyatsiya qilish", false, size))
                m_SceneState == SceneState::Simulate ? OnSceneStop() : OnSceneSimulate();
            ImGui::PopStyleColor();
            ImGui::SameLine();

            ImGui::BeginDisabled(editing);
            bool paused = !editing && (m_RuntimeScenes && m_SceneState == SceneState::Play ? m_RuntimeScenes->IsPaused() : m_ActiveScene->IsPaused());
            if (UI::ToolbarButton(ICON_FA_PAUSE, "Pauza", paused, size)) OnScenePause();
            ImGui::SameLine();
            ImGui::BeginDisabled(!paused);
            if (UI::ToolbarButton(ICON_FA_FORWARD_STEP, "Bir kadr oldinga", false, size))
            {
                if (m_RuntimeScenes && m_SceneState == SceneState::Play)
                    m_RuntimeScenes->Step(1);
                else
                    m_ActiveScene->Step(1);
            }
            ImGui::EndDisabled();
            ImGui::EndDisabled();
        }

        // ---- right: camera (frame all, bookmarks, axis view, camera settings) | view options
        {
            bool cameraTools = m_SceneState != SceneState::Play;
            float w = buttonSize * 4 + caretWidth + spacing * 3;
            ImVec2 pos(viewPos.x - 14.0f - w, viewPos.y);
            // No room next to the view options: the row under them, right-aligned.
            if (pos.x <= (viewRow == 0 ? centerMax : 10.0f))
            {
                pos = ImVec2(std::max(width - w - 10, 10.0f), viewPos.y + rowStep);
                m_ToolbarBottom = pos.y + buttonSize + 4.0f;
            }
            {
                beginPanel(pos, w);
                ImGui::BeginDisabled(!cameraTools);
                if (UI::ToolbarButton(ICON_FA_EXPAND, "Hammasini ko'rsatish (Shift+F)", false, size))
                {
                    m_LastSceneView = 0;
                    FrameAll();
                }
                ImGui::SameLine();
                int saved = GetSceneBookmarks().Count();
                std::string tip = std::format("Kamera xatcho'plari ({} / {})\nCtrl+1..5: saqlash   1..5: qaytish", saved, CameraBookmarks::SlotCount);
                if (UI::ToolbarButton(ICON_FA_BOOKMARK, tip.c_str(), saved > 0, size))
                    ImGui::OpenPopup("##CameraBookmarks");
                ImGui::SameLine();
                // The axis view: perspective, or Top / Front / Right ... orthographic.
                const ViewAxis axis = m_EditorCamera.GetViewAxis();
                std::string axisTip = std::format("Ko'rinish: {}\nYuqoridan / old / o'ng ... (Num 7 / 1 / 3, Ctrl bilan teskari tomon), "
                                                  "Num 5: perspektiv / ortografik", m_2DMode ? "2D" : GetViewAxisInfo(axis).Name);
                if (UI::ToolbarButton(ICON_FA_CUBE "###ViewAxis", axisTip.c_str(), axis != ViewAxis::Free, size))
                    ImGui::OpenPopup("##ViewAxisMenu");
                ImGui::SetNextWindowPos(ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y + 8.0f), ImGuiCond_Appearing);
                if (ImGui::BeginPopup("##ViewAxisMenu"))
                {
                    UI_ViewAxisItems(0);
                    ImGui::EndPopup();
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                // The editor camera: fly speed, acceleration, FOV, clip planes, mouse.
                const auto& c = m_Preferences.Camera;
                std::string cameraTip = std::format("Editor kamerasi: tezlik {:.1f}, FOV {:.0f}\xc2\xb0{}{}", m_EditorCamera.FlySpeed, c.FOV,
                                                    c.FlyAcceleration > 0.0f ? ", tezlanish" : "", c.InvertY ? ", Y teskari" : "");
                if (UI::ToolbarButton(ICON_FA_VIDEO, cameraTip.c_str(), false, size))
                    ImGui::OpenPopup("##CameraSettings");
                ImGui::SameLine(0.0f, 0.0f);
                if (UI::ToolbarButton(ICON_FA_CARET_DOWN "###CameraMenu", "Kamera sozlamalari: uchish tezligi, tezlanish, FOV, near / far, "
                                      "sichqoncha", false, ImVec2(caretWidth, buttonSize)))
                    ImGui::OpenPopup("##CameraSettings");
                if (m_OpenCameraSettings == 0)
                {
                    ImGui::OpenPopup("##CameraSettings");
                    m_OpenCameraSettings = -1;
                }
                ImGui::SetNextWindowPos(ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y + 8.0f), ImGuiCond_Appearing);
                UI_CameraSettingsPopup(0);
            }
            UI_CameraBookmarksPopup();
        }
        {
            beginPanel(viewPos, viewW);
            if (UI::ToolbarButton(m_2DMode ? "2D" : "3D", m_2DMode ? "2D rejim (bosing: 3D)" : "3D rejim (bosing: 2D)", m_2DMode, size))
                Set2DMode(!m_2DMode);
            ImGui::SameLine();
            if (UI::ToolbarButton(ICON_FA_BORDER_ALL, "Grid (o'ng tugma: sozlamalar)", grid.Visible, size))
            {
                grid.Visible = !grid.Visible;
                MarkViewSettingsDirty();
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                ImGui::OpenPopup("##GridSettings");
            ImGui::SameLine(0.0f, 0.0f);
            if (UI::ToolbarButton(ICON_FA_CARET_DOWN "###GridMenu", "Grid sozlamalari: o'lcham, rang, ko'rinish", false, ImVec2(caretWidth, buttonSize)))
                ImGui::OpenPopup("##GridSettings");
            UI_GridSettingsPopup();
            ImGui::SameLine();
            // Wireframe toggle + the view mode menu (debug views of the surfaces).
            {
                bool wire = m_Wireframe != WireframeMode::Off;
                std::string tip = std::format("Wireframe: {} (o'ng tugma yoki {}: ko'rinish rejimlari - yorug'lik, normallar, albedo ...)",
                                              wire ? "yoqilgan" : "o'chiq", ICON_FA_CARET_DOWN);
                if (UI::ToolbarButton(ICON_FA_DRAW_POLYGON, tip.c_str(), wire, size))
                    m_Wireframe = wire ? WireframeMode::Off : m_WireframeStyle;
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                    ImGui::OpenPopup("##ViewMode");
                ImGui::SameLine(0.0f, 0.0f);
                const bool debugView = m_DebugView != RenderDebugView::Lit;
                if (debugView) ImGui::PushStyleColor(ImGuiCol_Text, UI::AccentColor());
                if (UI::ToolbarButton(ICON_FA_CARET_DOWN "###ViewModeMenu", "Ko'rinish rejimi: oddiy, faqat yorug'lik, normallar, albedo, "
                                      "roughness, metallic; wireframe turi", debugView, ImVec2(caretWidth, buttonSize)))
                    ImGui::OpenPopup("##ViewMode");
                if (debugView) ImGui::PopStyleColor();
                if (m_OpenViewModeMenu)
                {
                    ImGui::OpenPopup("##ViewMode");
                    m_OpenViewModeMenu = false;
                }
                UI_ViewModePopup();
            }
            ImGui::SameLine();
            if (UI::ToolbarButton(ICON_FA_VECTOR_SQUARE, "Fizik colliderlar", m_ShowColliders, size)) m_ShowColliders = !m_ShowColliders;
            ImGui::SameLine();
            // Debug drawing (scripts' DebugDraw shapes, physics raycasts / contacts) + its settings.
            {
                // What is shown now (a game script may have changed it while playing).
                const DebugDrawSettings& debug = DebugDraw::GetSettings();
                std::string tip = std::format("Debug chizish: {} (o'ng tugma yoki {}: raycastlar, kontakt nuqtalari, qalinlik ...)",
                                              debug.Enabled ? "yoqilgan" : "o'chiq", ICON_FA_CARET_DOWN);
                if (UI::ToolbarButton(ICON_FA_BUG, tip.c_str(), debug.Enabled, size))
                    ToggleDebugDraw();
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                    ImGui::OpenPopup("##DebugDraw");
                ImGui::SameLine(0.0f, 0.0f);
                const bool physicsViews = debug.Enabled && (debug.ShowRaycasts || debug.ShowContacts || debug.ShowJoints ||
                                                            debug.ShowVelocities || debug.ShowBodyVelocities || debug.ShowNavigation);
                if (physicsViews) ImGui::PushStyleColor(ImGuiCol_Text, UI::AccentColor());
                if (UI::ToolbarButton(ICON_FA_CARET_DOWN "###DebugDrawMenu", "Debug chizish sozlamalari: raycastlar, kontakt nuqtalari, "
                                      "chiziq qalinligi, matn o'lchami", physicsViews, ImVec2(caretWidth, buttonSize)))
                    ImGui::OpenPopup("##DebugDraw");
                if (physicsViews) ImGui::PopStyleColor();
                // The popup hangs below the caret, its right edge under the caret's.
                const ImVec2 popupAnchor(ImGui::GetItemRectMax().x, ImGui::GetItemRectMax().y + 8.0f);
                if (m_OpenDebugDrawMenu)
                {
                    ImGui::OpenPopup("##DebugDraw");
                    m_OpenDebugDrawMenu = false;
                }
                ImGui::SetNextWindowPos(popupAnchor, ImGuiCond_Appearing, ImVec2(1.0f, 0.0f));
                UI_DebugDrawPopup();
            }
            ImGui::SameLine();
            // The statistics overlay (FPS graph, draw calls, triangles, culling) + its settings.
            {
                auto& overlay = m_ViewSettings.StatsOverlay;
                std::string tip = std::format("Statistika overlay: {} (o'ng tugma yoki {}: FPS grafigi, draw call, uchburchaklar, "
                                              "culling, burchak)", overlay.Visible ? "yoqilgan" : "o'chiq", ICON_FA_CARET_DOWN);
                if (UI::ToolbarButton(ICON_FA_GAUGE_HIGH, tip.c_str(), overlay.Visible, size))
                {
                    overlay.Visible = !overlay.Visible;
                    MarkViewSettingsDirty();
                }
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                    ImGui::OpenPopup("##StatsOverlay");
                ImGui::SameLine(0.0f, 0.0f);
                if (UI::ToolbarButton(ICON_FA_CARET_DOWN "###StatsOverlayMenu", "Statistika overlay sozlamalari", false,
                                      ImVec2(caretWidth, buttonSize)))
                    ImGui::OpenPopup("##StatsOverlay");
                const ImVec2 popupAnchor(ImGui::GetItemRectMax().x, ImGui::GetItemRectMax().y + 8.0f);
                if (m_OpenStatsOverlayMenu)
                {
                    ImGui::OpenPopup("##StatsOverlay");
                    m_OpenStatsOverlayMenu = false;
                }
                ImGui::SetNextWindowPos(popupAnchor, ImGuiCond_Appearing, ImVec2(1.0f, 0.0f));
                UI_StatsOverlayPopup();
            }
            ImGui::SameLine();
            // Icons + the component visualisations (camera frustums, light directions, audio radii).
            {
                std::string tip = std::format("Ikonkalar: {} (o'ng tugma yoki {}: gizmolar - kamera frustumi, yorug'lik yo'nalishi, "
                                              "audio radiusi)", m_ShowIcons ? "yoqilgan" : "o'chiq", ICON_FA_CARET_DOWN);
                if (UI::ToolbarButton(ICON_FA_EYE, tip.c_str(), m_ShowIcons, size)) m_ShowIcons = !m_ShowIcons;
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                    ImGui::OpenPopup("##ComponentGizmos");
                ImGui::SameLine(0.0f, 0.0f);
                const auto& g = m_ViewSettings.Gizmos;
                const bool showingAll = g.CameraFrustums == GizmoVisibility::All || g.LightDirections == GizmoVisibility::All ||
                                        g.AudioRanges == GizmoVisibility::All || g.Bounds == GizmoVisibility::All ||
                                        g.Skeletons == GizmoVisibility::All || g.Normals != GizmoVisibility::Off ||
                                        g.Joints2D == GizmoVisibility::All || g.ParticleShapes == GizmoVisibility::All;
                if (showingAll) ImGui::PushStyleColor(ImGuiCol_Text, UI::AccentColor());
                if (UI::ToolbarButton(ICON_FA_CARET_DOWN "###ComponentGizmosMenu", "Gizmolar: kamera frustumi, yorug'lik yo'nalishi, "
                                      "audio radiusi, chegaralar, skelet - tanlanganda yoki hammasida; tanlash konturi",
                                      showingAll, ImVec2(caretWidth, buttonSize)))
                    ImGui::OpenPopup("##ComponentGizmos");
                if (showingAll) ImGui::PopStyleColor();
                const ImVec2 popupAnchor(ImGui::GetItemRectMax().x, ImGui::GetItemRectMax().y + 8.0f);
                if (m_OpenGizmosMenu)
                {
                    ImGui::OpenPopup("##ComponentGizmos");
                    m_OpenGizmosMenu = false;
                }
                ImGui::SetNextWindowPos(popupAnchor, ImGuiCond_Appearing, ImVec2(1.0f, 0.0f));
                UI_ComponentGizmosPopup();
            }
            ImGui::SameLine();
            // Entity names over the views (the selection's or everyone's) + their settings.
            {
                auto& labels = m_ViewSettings.EntityLabels;
                const char* mode = labels.Mode == EntityLabelMode::All ? "hammasi" : labels.Mode == EntityLabelMode::Selected ? "tanlanganlar" : "o'chiq";
                std::string tip = std::format("Obyekt nomlari: {} (o'ng tugma yoki {}: tanlanganlar / hammasi, masofada xiralashish)", mode,
                                              ICON_FA_CARET_DOWN);
                if (UI::ToolbarButton(ICON_FA_TAG, tip.c_str(), labels.IsEnabled(), size))
                {
                    labels.Mode = labels.IsEnabled() ? EntityLabelMode::Off : m_LabelToggleMode;
                    MarkViewSettingsDirty();
                }
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                    ImGui::OpenPopup("##EntityLabels");
                ImGui::SameLine(0.0f, 0.0f);
                if (UI::ToolbarButton(ICON_FA_CARET_DOWN "###EntityLabelsMenu", "Obyekt nomlari sozlamalari", false, ImVec2(caretWidth, buttonSize)))
                    ImGui::OpenPopup("##EntityLabels");
                const ImVec2 popupAnchor(ImGui::GetItemRectMax().x, ImGui::GetItemRectMax().y + 8.0f);
                if (m_OpenLabelsMenu)
                {
                    ImGui::OpenPopup("##EntityLabels");
                    m_OpenLabelsMenu = false;
                }
                ImGui::SetNextWindowPos(popupAnchor, ImGuiCond_Appearing, ImVec2(1.0f, 0.0f));
                UI_EntityLabelsPopup();
            }
            ImGui::SameLine();
            if (UI::ToolbarButton(ICON_FA_GLOBE, m_GizmoLocal ? "Lokal koordinatalar" : "Global koordinatalar", m_GizmoLocal, size))
                m_GizmoLocal = !m_GizmoLocal;
            ImGui::SameLine();
            if (UI::ToolbarButton(ICON_FA_UP_RIGHT_AND_DOWN_LEFT_FROM_CENTER, "O'yinda to'liq oyna (Maximize on Play)", m_MaximizeOnPlay, size))
                m_MaximizeOnPlay = !m_MaximizeOnPlay;
        }

        // camera fly speed hint (with the acceleration's boost)
        if (m_EditorCamera.IsFlying())
        {
            ImGui::SetCursorPos(ImVec2(regionMin.x + 12, regionMin.y + buttonSize + 26));
            const float boost = m_EditorCamera.GetFlyBoostNow();
            if (boost > 1.01f)
                ImGui::TextColored(ImVec4(1, 1, 1, 0.75f), ICON_FA_PERSON_RUNNING "  Tezlik: %.1f x%.1f  (g'ildirak bilan o'zgartiring)",
                                   m_EditorCamera.FlySpeed, boost);
            else
                ImGui::TextColored(ImVec4(1, 1, 1, 0.75f), ICON_FA_PERSON_RUNNING "  Tezlik: %.1f  (g'ildirak bilan o'zgartiring)", m_EditorCamera.FlySpeed);
        }

        ImGui::PopStyleVar(2);
    }

}
