// The editor's scene views: the main viewport (view 0) and up to three extra ones ("Ko'rinish 2" ...
// "Ko'rinish 4", Oyna > Ko'rinishlar), each a window with its own editor camera. The extra views are
// drawn after the main picture (the same frame of the scene: grid, selection outline, debug shapes,
// icons, entity names, the transform gizmo); clicking in one selects. Axis views (Top / Front /
// Right ... orthographic) come from the views' menus, the axis navigator in their corner or the
// numpad keys. Also the editor cameras' settings popup (fly speed, acceleration, FOV, clip planes,
// mouse) and the transform gizmo every view shares.
#include "EditorLayer.h"
#include "UI.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <ImGuizmo.h>

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <format>

namespace Zamin {

    namespace {
        std::string ViewWindowId(int extraIndex) { return std::format("###SceneView{}", extraIndex + 2); }

        std::string ViewTitle(int extraIndex)
        {
            return std::format(ICON_FA_CUBE "  Ko'rinish {}###SceneView{}", extraIndex + 2, extraIndex + 2);
        }

        // "Yuqori", "Persp", "Orto" ... for a camera.
        std::string ProjectionLabel(const EditorCamera& camera)
        {
            if (camera.IsRotationLocked())
                return "2D";
            const ViewAxis axis = camera.GetViewAxis();
            if (axis != ViewAxis::Free)
                return GetViewAxisInfo(axis).Short;
            return camera.IsOrthographic() ? "Orto" : "Persp";
        }

        const char* AxisShortcut(ViewAxis axis)
        {
            switch (axis)
            {
                case ViewAxis::Top: return EditorShortcut::ViewTop;
                case ViewAxis::Bottom: return EditorShortcut::ViewBottom;
                case ViewAxis::Front: return EditorShortcut::ViewFront;
                case ViewAxis::Back: return EditorShortcut::ViewBack;
                case ViewAxis::Right: return EditorShortcut::ViewRight;
                case ViewAxis::Left: return EditorShortcut::ViewLeft;
                default: return EditorShortcut::ViewPerspective;
            }
        }

        // Small buttons that set `value` to one of `presets` (the current one highlighted).
        bool ValuePresets(const char* id, float& value, std::initializer_list<float> presets, const char* suffix = "")
        {
            bool changed = false;
            ImGui::PushID(id);
            int i = 0;
            for (float preset : presets)
            {
                if (i++) ImGui::SameLine(0.0f, 3.0f);
                const bool current = std::abs(value - preset) <= 1e-3f * std::max(1.0f, preset);
                if (current) ImGui::PushStyleColor(ImGuiCol_Button, UI::AccentDarkColor(1.0f));
                if (ImGui::SmallButton(std::format("{:g}{}", preset, suffix).c_str()))
                {
                    value = preset;
                    changed = true;
                }
                if (current) ImGui::PopStyleColor();
            }
            ImGui::PopID();
            return changed;
        }
    }

    // ------------------------------------------------------------------ views
    EditorLayer::SceneViewTarget EditorLayer::GetSceneViewTarget(int index)
    {
        SceneViewTarget target;
        target.Index = std::clamp(index, 0, SceneViewSettings::Count);
        if (target.Index == 0)
        {
            target.Camera = &m_EditorCamera;
            target.Min = m_ViewportBounds[0];
            target.Max = m_ViewportBounds[1];
            target.Hovered = m_ViewportHovered;
            return target;
        }
        ExtraSceneView& view = m_ExtraViews[target.Index - 1];
        target.Camera = &view.Camera;
        target.Min = view.Bounds[0];
        target.Max = view.Bounds[1];
        target.Hovered = view.Hovered;
        return target;
    }

    EditorCamera& EditorLayer::GetSceneViewCamera(int index)
    {
        if (index <= 0 || index > SceneViewSettings::Count)
            return m_EditorCamera;
        return m_ExtraViews[index - 1].Camera;
    }

    bool EditorLayer::IsAnySceneViewHovered() const
    {
        for (int i = 0; i < SceneViewSettings::Count; i++)
            if (m_ViewSettings.SceneViews[i].Open && m_ExtraViews[i].Hovered)
                return true;
        return false;
    }

    bool EditorLayer::IsAnySceneViewFocused() const
    {
        for (int i = 0; i < SceneViewSettings::Count; i++)
            if (m_ViewSettings.SceneViews[i].Open && m_ExtraViews[i].Focused)
                return true;
        return false;
    }

    int EditorLayer::GetShortcutSceneView() const
    {
        for (int i = 0; i < SceneViewSettings::Count; i++)
            if (m_ViewSettings.SceneViews[i].Open && m_ExtraViews[i].Hovered)
                return i + 1;
        if (m_ViewportHovered)
            return 0;
        for (int i = 0; i < SceneViewSettings::Count; i++)
            if (m_ViewSettings.SceneViews[i].Open && m_ExtraViews[i].Focused)
                return i + 1;
        if (m_ViewportFocused)
            return 0;
        // Neither: the view used last (still open), else the main one.
        if (m_LastSceneView > 0 && m_LastSceneView <= SceneViewSettings::Count && m_ViewSettings.SceneViews[m_LastSceneView - 1].Open)
            return m_LastSceneView;
        return 0;
    }

    void EditorLayer::SetSceneViewAxis(int index, ViewAxis axis)
    {
        EditorCamera& camera = GetSceneViewCamera(index);
        if (index == 0 && m_2DMode)
        {
            // The 2D mode looks from the front already; any other view leaves it (the camera keeps
            // its focal point and distance and turns there).
            if (axis == ViewAxis::Front)
            {
                ShowViewportMessage(ICON_FA_CUBE "  2D rejim: old tomondan");
                return;
            }
            m_2DMode = false;
        }
        camera.SetViewAxis(axis, 0.3f);
        if (index == 0)
            ShowViewportMessage(std::format(ICON_FA_CUBE "  {}{}", GetViewAxisInfo(axis).Name, axis == ViewAxis::Free ? "" : " (ortografik)"));
        else
            MarkViewSettingsDirty();
    }

    void EditorLayer::ToggleSceneViewProjection(int index)
    {
        EditorCamera& camera = GetSceneViewCamera(index);
        if (index == 0 && m_2DMode)
        {
            // Out of the 2D mode into perspective, looking the same way.
            m_2DMode = false;
            camera.SetViewAxis(ViewAxis::Free, 0.0f);
            ShowViewportMessage(ICON_FA_CUBE "  Perspektiv (3D)");
            return;
        }
        camera.ToggleOrthographic();
        if (index == 0)
            ShowViewportMessage(camera.IsOrthographic() ? ICON_FA_CUBE "  Ortografik" : ICON_FA_CUBE "  Perspektiv");
        else
            MarkViewSettingsDirty();
    }

    void EditorLayer::OpenSceneView(int extraIndex, bool open)
    {
        if (extraIndex < 0 || extraIndex >= SceneViewSettings::Count)
            return;
        SceneViewSettings& settings = m_ViewSettings.SceneViews[extraIndex];
        ExtraSceneView& view = m_ExtraViews[extraIndex];
        if (settings.Open == open)
            return;
        settings.Open = open;
        MarkViewSettingsDirty();
        if (open)
        {
            // Not docked anywhere (never opened, or the layout left it out): docked next to the
            // viewport by the next frame's dock space.
            ImGuiWindow* window = ImGui::GetCurrentContext() ? ImGui::FindWindowByName(ViewWindowId(extraIndex).c_str()) : nullptr;
            if (!window || window->DockId == 0)
                m_PendingViewDock |= 1u << extraIndex;
            m_LastSceneView = extraIndex + 1;
        }
        else
        {
            view.Hovered = view.Focused = view.Visible = false;
            view.HoveredEntity = {};
            if (m_LastSceneView == extraIndex + 1)
                m_LastSceneView = 0;
            if (m_GizmoOwner == extraIndex + 1)
            {
                m_GizmoEdit.End();
                m_GizmoOwner = -1;
            }
        }
    }

    void EditorLayer::DockPendingSceneViews()
    {
        if (m_PendingViewDock == 0)
            return;
        const uint32_t pending = m_PendingViewDock;
        m_PendingViewDock = 0;
        ImGuiWindow* viewport = ImGui::FindWindowByName("###Viewport");
        if (!viewport || !viewport->DockNode)
            return; // a floating viewport: the views float as well
        for (int i = 0; i < SceneViewSettings::Count; i++)
        {
            if (!(pending & (1u << i)))
                continue;
            // View 2 to the right of the viewport, view 3 below it, view 4 below view 2 (a 2x2 grid
            // when all are opened one after another).
            ImGuiDockNode* target = viewport->DockNode;
            ImGuiDir direction = i == 1 ? ImGuiDir_Down : ImGuiDir_Right;
            if (i == 2)
                if (ImGuiWindow* second = ImGui::FindWindowByName("###SceneView2"); second && second->DockNode && m_ViewSettings.SceneViews[0].Open)
                {
                    target = second->DockNode;
                    direction = ImGuiDir_Down;
                }
            if (!target || target->IsSplitNode())
                continue;
            ImGuiID atDirection = 0, opposite = 0;
            ImGui::DockBuilderSplitNode(target->ID, direction, 0.5f, &atDirection, &opposite);
            if (atDirection == 0)
                continue;
            ImGui::DockBuilderDockWindow(ViewWindowId(i).c_str(), atDirection);
            ImGui::DockBuilderFinish(ImGui::DockNodeGetRootNode(target)->ID);
        }
    }

    // ------------------------------------------------------------------ the extra views' pictures
    void EditorLayer::RenderSceneViews(Timestep ts)
    {
        for (int i = 0; i < SceneViewSettings::Count; i++)
        {
            ExtraSceneView& view = m_ExtraViews[i];
            SceneViewSettings& settings = m_ViewSettings.SceneViews[i];
            if (!view.CameraLoaded)
            {
                view.Camera.SetViewState(settings.Camera);
                view.CameraLoaded = true;
            }
            if (!settings.Open || !view.Visible || view.Size.x < 1.0f || view.Size.y < 1.0f)
            {
                view.HoveredEntity = {};
                continue;
            }
            ZM_PROFILE_SCOPE("Qo'shimcha ko'rinish");
            const uint32_t width = (uint32_t)view.Size.x, height = (uint32_t)view.Size.y;
            Scene& scene = *m_ActiveScene;
            const glm::uvec2 renderSize = Renderer::GetRenderResolution(width, height, scene.GetSettings().RenderScale);
            if (!view.SceneBuffer)
            {
                FramebufferSpecification spec;
                spec.Attachments = { FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::RED_INTEGER, FramebufferTextureFormat::Depth };
                spec.Width = renderSize.x;
                spec.Height = renderSize.y;
                view.SceneBuffer = Framebuffer::Create(spec);
                FramebufferSpecification finalSpec;
                finalSpec.Attachments = { FramebufferTextureFormat::RGBA8 };
                finalSpec.Width = width;
                finalSpec.Height = height;
                view.FinalBuffer = Framebuffer::Create(finalSpec);
                view.Camera.SetViewportSize((float)width, (float)height);
            }
            if (const auto& spec = view.SceneBuffer->GetSpecification(); spec.Width != renderSize.x || spec.Height != renderSize.y)
                view.SceneBuffer->Resize(renderSize.x, renderSize.y);
            if (const auto& spec = view.FinalBuffer->GetSpecification(); spec.Width != width || spec.Height != height)
            {
                view.FinalBuffer->Resize(width, height);
                view.Camera.SetViewportSize((float)width, (float)height);
            }

            // The camera takes the mouse while its view is hovered (not while the gizmo or the axis
            // navigator under it has the button).
            const bool leftDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
            const bool cameraInput = view.Hovered && !m_GizmoEdit.IsActive() && m_UIRect.Drag < 0 &&
                                     !(leftDown && (view.GizmoHovered || view.NavigatorHovered));
            view.Camera.OnUpdate(ts, cameraInput);

            SceneRenderOptions options;
            options.ShowGrid = settings.ShowGrid;
            options.Grid = m_ViewSettings.Grid;
            options.GridPlane = GetGridPlaneForView(view.Camera.GetForwardDirection(), view.Camera.IsOrthographic(), false);
            const bool editing = m_SceneState != SceneState::Play;
            options.SkipEditorHidden = editing;
            options.DebugView = editing ? m_DebugView : RenderDebugView::Lit;
            options.Wireframe = editing ? m_Wireframe : WireframeMode::Off;
            options.DeferUI = true;      // screen-space UI belongs to the game view
            options.KeepLastView = true; // the main view's matrices stay the scene's last ones

            view.SceneBuffer->Bind();
            const glm::vec4 clear = scene.GetSettings().DrawSky ? glm::vec4(0.1f, 0.1f, 0.12f, 1.0f) : scene.GetClearColor();
            RenderCommand::SetClearColor({ std::pow(clear.r, 2.2f), std::pow(clear.g, 2.2f), std::pow(clear.b, 2.2f), clear.a });
            RenderCommand::Clear();
            view.SceneBuffer->ClearAttachment(1, -1);
            scene.OnRenderEditor(view.Camera, options);
            if (m_SceneState == SceneState::Edit)
                m_SubScenePreviews.Render(view.Camera, options);

            // What the mouse is over (the entity ID picture at the render scale).
            view.HoveredEntity = {};
            if (view.Hovered)
            {
                const ImVec2 mouse = ImGui::GetMousePos();
                const float px = mouse.x - view.Bounds[0].x, py = mouse.y - view.Bounds[0].y;
                if (px >= 0.0f && py >= 0.0f && px < (float)width && py < (float)height)
                {
                    const int x = std::clamp((int)std::floor(px * (float)renderSize.x / (float)width), 0, (int)renderSize.x - 1);
                    const int y = std::clamp((int)std::floor(((float)height - py) * (float)renderSize.y / (float)height), 0, (int)renderSize.y - 1);
                    const int id = view.SceneBuffer->ReadPixel(1, x, y);
                    if (id != -1)
                        if (Entity e((entt::entity)id, &scene); e.IsValid())
                            view.HoveredEntity = e;
                }
            }
            view.SceneBuffer->Unbind();

            // Post-processing into the view's picture (orthographic views get no SSAO), then the
            // selection outline and the debug shapes over it.
            const auto& spec = view.SceneBuffer->GetSpecification();
            PostProcessInput post;
            post.ColorTexture = view.SceneBuffer->GetColorAttachmentRendererID(0);
            post.Width = spec.Width;
            post.Height = spec.Height;
            post.DepthTexture = view.SceneBuffer->GetDepthAttachmentRendererID();
            post.Projection = view.Camera.GetProjection();
            post.OutputWidth = width;
            post.OutputHeight = height;
            post.DeltaTime = ts;
            post.History = &view.PostHistory;
            const bool neutralPost = editing && m_DebugView != RenderDebugView::Lit && !Renderer3D::DebugViewUsesPostProcessing(m_DebugView);
            PostProcessor::Apply(post, neutralPost ? PostProcessSettings::Neutral() : scene.GetSettings().Post, view.FinalBuffer);
            view.FinalBuffer->Bind();
            RenderSceneViewOutline(i);
            DebugDrawView debug;
            debug.View = view.Camera.GetViewMatrix();
            debug.Projection = view.Camera.GetProjection();
            debug.Width = width;
            debug.Height = height;
            debug.DepthTexture = view.SceneBuffer->GetDepthAttachmentRendererID();
            debug.DepthWidth = spec.Width;
            debug.DepthHeight = spec.Height;
            DebugDraw::Render(debug);
            view.FinalBuffer->Unbind();
        }
    }

    void EditorLayer::RenderSceneViewOutline(int extraIndex)
    {
        // The selection (and its children) with the main view's settings; the silhouettes the main
        // picture kept give the hidden parts too.
        ExtraSceneView& view = m_ExtraViews[extraIndex];
        std::vector<OutlineEntity> entities = CollectSelectionOutline();
        if (entities.empty())
            return;
        const auto& render = view.SceneBuffer->GetSpecification();
        const auto& output = view.FinalBuffer->GetSpecification();
        SelectionOutlineInput input;
        input.EntityIDTexture = view.SceneBuffer->GetColorAttachmentRendererID(1);
        input.IDWidth = render.Width;
        input.IDHeight = render.Height;
        input.Width = output.Width;
        input.Height = output.Height;
        input.ViewProjection = view.Camera.GetViewProjection();
        input.Entities = std::move(entities);
        input.Silhouettes = m_OutlineCaptured ? &Renderer3D::GetCapturedDraws() : nullptr;
        SelectionOutlineSettings settings = m_ViewSettings.SelectionOutline;
        settings.Style = SelectionStyle::Outline; // the box style is drawn by the main overlay only
        SelectionOutline::Render(input, settings);
    }

    void EditorLayer::SyncFlySpeed()
    {
        // A speed changed with the wheel while flying (any view) is everyone's once the flight ends.
        EditorCameraDefaults& defaults = m_Preferences.Camera;
        for (int i = 0; i <= SceneViewSettings::Count; i++)
        {
            EditorCamera& camera = GetSceneViewCamera(i);
            if (camera.IsFlying() || std::abs(camera.FlySpeed - defaults.FlySpeed) < 1e-4f)
                continue;
            defaults.FlySpeed = std::clamp(camera.FlySpeed, EditorCameraDefaults::MinFlySpeed, EditorCameraDefaults::MaxFlySpeed);
            for (int j = 0; j <= SceneViewSettings::Count; j++)
                GetSceneViewCamera(j).FlySpeed = defaults.FlySpeed;
            MarkPreferencesDirty();
            break;
        }
    }

    // ------------------------------------------------------------------ the extra views' windows
    void EditorLayer::UI_SceneViews()
    {
        bool anyHovered = false;
        const ImGuiIO& io = ImGui::GetIO();
        for (int i = 0; i < SceneViewSettings::Count; i++)
        {
            ExtraSceneView& view = m_ExtraViews[i];
            SceneViewSettings& settings = m_ViewSettings.SceneViews[i];
            view.Visible = false;
            if (!settings.Open)
            {
                view.Hovered = view.Focused = false;
                continue;
            }
            ImGui::SetNextWindowSize(ImVec2(560.0f, 380.0f), ImGuiCond_FirstUseEver);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            bool open = true;
            const bool shown = ImGui::Begin(ViewTitle(i).c_str(), &open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            ImGui::PopStyleVar();
            if (!open)
                OpenSceneView(i, false);
            if (!shown || !open)
            {
                view.Hovered = view.Focused = false;
                ImGui::End();
                continue;
            }
            view.Visible = true;
            view.Focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
            const ImVec2 at = ImGui::GetCursorScreenPos();
            const ImVec2 avail = ImGui::GetContentRegionAvail();
            const ImVec2 size(std::max(std::floor(avail.x), 1.0f), std::max(std::floor(avail.y), 1.0f));
            view.Bounds[0] = { at.x, at.y };
            view.Bounds[1] = { at.x + size.x, at.y + size.y };
            view.Size = { size.x, size.y };
            ImGui::SetNextItemAllowOverlap();
            if (view.FinalBuffer)
                ImGui::Image((ImTextureID)(uint64_t)view.FinalBuffer->GetColorAttachmentRendererID(0), size, ImVec2(0, 1), ImVec2(1, 0));
            else
                ImGui::Dummy(size);
            view.Hovered = ImGui::IsItemHovered();
            const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
            anyHovered |= view.Hovered;
            if (view.Hovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
                                 ImGui::IsMouseClicked(ImGuiMouseButton_Middle)))
                m_LastSceneView = i + 1;

            // A click selects what is under the mouse (Ctrl toggles, Shift adds); icons and names
            // drawn over the picture take their own clicks after it. Not on the gizmo / navigator.
            if (clicked && !io.KeyAlt && !view.GizmoHovered && !view.NavigatorHovered)
            {
                Entity picked = view.HoveredEntity;
                if (picked && (!picked.IsValid() || m_ActiveScene->IsLockedInEditor(picked)))
                    picked = {};
                if (!m_SceneHierarchyPanel.IsTilePainting() && !IsViewportHandleEditing())
                    m_SceneHierarchyPanel.SelectWithModifiers(picked, io.KeyCtrl, io.KeyShift);
            }

            const SceneViewTarget target = GetSceneViewTarget(i + 1);
            bool gizmoHovered = false;
            if (m_SceneState != SceneState::Play)
            {
                if (m_ShowIcons)
                    UI_ViewportIcons(target);
                gizmoHovered = UI_TransformGizmo(target);
            }
            view.GizmoHovered = gizmoHovered;
            if (m_ViewSettings.EntityLabels.IsEnabled())
                UI_EntityLabels(target, view.Camera.GetViewProjection(), view.Camera.GetPosition(), view.Camera.IsOrthographic());

            // The axis navigator in the top-right corner, the toolbar top-left, the view's name.
            const float radius = std::floor(ImGui::GetFontSize() * 2.6f);
            const float top = ImGui::GetFrameHeight() + 18.0f;
            view.NavigatorHovered = UI_ViewAxisNavigator(target, ImVec2(view.Bounds[1].x - radius - 14.0f, view.Bounds[0].y + top + radius), radius);
            UI_SceneViewToolbar(i);

            // While the game runs the views show it from their cameras: framed like the viewport.
            if (m_SceneState != SceneState::Edit)
            {
                const ImU32 col = m_SceneState == SceneState::Play ? IM_COL32(60, 200, 120, 160) : IM_COL32(80, 150, 255, 160);
                ImGui::GetWindowDrawList()->AddRect(ImVec2(view.Bounds[0].x, view.Bounds[0].y), ImVec2(view.Bounds[1].x, view.Bounds[1].y), col, 0.0f, 0, 2.0f);
            }
            ImGui::End();
        }
        // The wheel over an extra view reaches its camera (OnEvent).
        if (anyHovered)
            Application::Get().GetImGuiLayer()->BlockEvents(false);
    }

    void EditorLayer::UI_SceneViewToolbar(int extraIndex)
    {
        ExtraSceneView& view = m_ExtraViews[extraIndex];
        SceneViewSettings& settings = m_ViewSettings.SceneViews[extraIndex];
        const int index = extraIndex + 1;
        const float buttonSize = ImGui::GetFrameHeight() + 2.0f;
        const float caretWidth = std::floor(buttonSize * 0.55f);
        const ImVec2 size(buttonSize, buttonSize);
        ImDrawList* dl = ImGui::GetWindowDrawList();

        const std::string axisLabel = std::format(ICON_FA_CUBE "  {}  " ICON_FA_CARET_DOWN "###ViewAxis", ProjectionLabel(view.Camera));
        const float axisWidth = ImGui::CalcTextSize(axisLabel.c_str(), nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        const float spacing = 2.0f;
        const float width = axisWidth + spacing + buttonSize * 4 + caretWidth + spacing * 4;
        const ImVec2 pos(view.Bounds[0].x + 8.0f, view.Bounds[0].y + 8.0f);
        if (view.Size.x < width + 20.0f)
            return; // too narrow: the menus stay reachable from the main viewport's Ko'rinish menu
        dl->AddRectFilled(ImVec2(pos.x - 4, pos.y - 4), ImVec2(pos.x + width + 4, pos.y + buttonSize + 4), IM_COL32(18, 20, 24, 210), 7.0f);
        ImGui::SetCursorScreenPos(pos);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(spacing, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);

        const std::string tip = std::format("Ko'rinish: {}\nYuqoridan / old / o'ng ... (Num 7 / 1 / 3, Ctrl bilan teskari tomon), "
                                            "Num 5: perspektiv / ortografik", ProjectionLabel(view.Camera));
        if (UI::ToolbarButton(axisLabel.c_str(), tip.c_str(), view.Camera.IsOrthographic(), ImVec2(axisWidth, buttonSize)))
            ImGui::OpenPopup("##ViewAxisMenu");
        ImGui::SetNextWindowPos(ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y + 8.0f), ImGuiCond_Appearing);
        if (ImGui::BeginPopup("##ViewAxisMenu"))
        {
            UI_ViewAxisItems(index);
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        if (UI::ToolbarButton(ICON_FA_BULLSEYE, "Tanlovga fokus (F)", false, size))
        {
            m_LastSceneView = index;
            FrameSelection();
        }
        ImGui::SameLine();
        if (UI::ToolbarButton(ICON_FA_EXPAND "##all", "Hammasini ko'rsatish (Shift+F)", false, size))
        {
            m_LastSceneView = index;
            FrameAll();
        }
        ImGui::SameLine();
        if (UI::ToolbarButton(ICON_FA_BORDER_ALL, "Grid (shu ko'rinishda)", settings.ShowGrid, size))
        {
            settings.ShowGrid = !settings.ShowGrid;
            MarkViewSettingsDirty();
        }
        ImGui::SameLine();
        if (UI::ToolbarButton(ICON_FA_VIDEO, "Kamera: tezlik, tezlanish, FOV, chegaralar, sichqoncha", false, size))
            ImGui::OpenPopup("##CameraSettings");
        ImGui::SameLine(0.0f, 0.0f);
        if (UI::ToolbarButton(ICON_FA_CARET_DOWN "###CameraMenu", "Kamera sozlamalari", false, ImVec2(caretWidth, buttonSize)))
            ImGui::OpenPopup("##CameraSettings");
        if (m_OpenCameraSettings == index)
        {
            ImGui::OpenPopup("##CameraSettings");
            m_OpenCameraSettings = -1;
        }
        ImGui::SetNextWindowPos(ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y + 8.0f), ImGuiCond_Appearing);
        UI_CameraSettingsPopup(index);
        ImGui::PopStyleVar(2);

        // The view's camera while flying: its speed (the wheel changes it).
        if (view.Camera.IsFlying())
        {
            const float boost = view.Camera.GetFlyBoostNow();
            const std::string text = boost > 1.01f ? std::format(ICON_FA_PERSON_RUNNING "  {:.1f} (x{:.1f})", view.Camera.FlySpeed, boost)
                                                   : std::format(ICON_FA_PERSON_RUNNING "  {:.1f}", view.Camera.FlySpeed);
            dl->AddText(ImVec2(pos.x + 2.0f, pos.y + buttonSize + 12.0f), IM_COL32(255, 255, 255, 190), text.c_str());
        }
    }

    void EditorLayer::UI_ViewAxisItems(int index)
    {
        EditorCamera& camera = GetSceneViewCamera(index);
        const ViewAxis current = camera.GetViewAxis();
        const bool locked = index == 0 && m_2DMode;
        ImGui::TextColored(UI::AccentColor(), ICON_FA_CUBE "  Ko'rinish o'qi");
        if (ImGui::MenuItem(ICON_FA_CUBE "  Erkin (perspektiv)", ShortcutLabel(EditorShortcut::ViewPerspective), !camera.IsOrthographic() && !locked))
            SetSceneViewAxis(index, ViewAxis::Free);
        ImGui::Separator();
        for (ViewAxis axis : { ViewAxis::Top, ViewAxis::Bottom, ViewAxis::Front, ViewAxis::Back, ViewAxis::Right, ViewAxis::Left })
            if (ImGui::MenuItem(GetViewAxisInfo(axis).Name, ShortcutLabel(AxisShortcut(axis)), current == axis || (locked && axis == ViewAxis::Front)))
                SetSceneViewAxis(index, axis);
        ImGui::Separator();
        if (ImGui::MenuItem("Ortografik proyeksiya", ShortcutLabel(EditorShortcut::ViewToggleOrtho), camera.IsOrthographic()))
            ToggleSceneViewProjection(index);
        if (locked)
            ImGui::TextDisabled("2D rejim: boshqa ko'rinish tanlansa 3D ga o'tiladi");
        else
            ImGui::TextDisabled("Ortografik o'q ko'rinishini aylantirish (Alt + chap tugma) perspektivga qaytaradi");
    }

    bool EditorLayer::UI_ViewAxisNavigator(const SceneViewTarget& view, ImVec2 center, float radius)
    {
        EditorCamera& camera = *view.Camera;
        if (camera.IsRotationLocked())
            return false;
        if (view.Max.x - view.Min.x < radius * 6.0f || view.Max.y - view.Min.y < radius * 4.0f)
            return false; // a small view keeps its picture
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImGuiIO& io = ImGui::GetIO();
        const glm::mat3 rotation = glm::mat3(camera.GetViewMatrix());
        const float knob = std::max(radius * 0.24f, 7.0f);
        const float reach = radius - knob;

        struct Arm
        {
            glm::vec3 Direction;
            ImU32 Color;
            const char* Label;
            bool Positive;
            ImVec2 At;
            float Depth;
        };
        Arm arms[6] = {
            { { 1, 0, 0 }, IM_COL32(232, 80, 82, 255), "X", true, {}, 0 },
            { { 0, 1, 0 }, IM_COL32(126, 200, 70, 255), "Y", true, {}, 0 },
            { { 0, 0, 1 }, IM_COL32(72, 140, 238, 255), "Z", true, {}, 0 },
            { { -1, 0, 0 }, IM_COL32(232, 80, 82, 255), "", false, {}, 0 },
            { { 0, -1, 0 }, IM_COL32(126, 200, 70, 255), "", false, {}, 0 },
            { { 0, 0, -1 }, IM_COL32(72, 140, 238, 255), "", false, {}, 0 },
        };
        for (Arm& arm : arms)
        {
            const glm::vec3 v = rotation * arm.Direction; // view space: x right, y up, z towards the viewer
            arm.At = ImVec2(center.x + v.x * reach, center.y - v.y * reach);
            arm.Depth = v.z;
        }
        int order[6] = { 0, 1, 2, 3, 4, 5 };
        std::sort(std::begin(order), std::end(order), [&](int a, int b) { return arms[a].Depth < arms[b].Depth; }); // far first

        const ImVec2 mouse = io.MousePos;
        const float dx = mouse.x - center.x, dy = mouse.y - center.y;
        const bool windowHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
        const bool areaHovered = windowHovered && dx * dx + dy * dy <= (radius + 4.0f) * (radius + 4.0f) && !m_GizmoEdit.IsActive() &&
                                 !ImGui::IsMouseDown(ImGuiMouseButton_Right) && !ImGui::IsMouseDown(ImGuiMouseButton_Middle);
        // The knob under the mouse (the nearest to the viewer first).
        int hoveredArm = -1;
        if (areaHovered)
            for (int k = 5; k >= 0 && hoveredArm < 0; k--)
            {
                const Arm& arm = arms[order[k]];
                const float ax = mouse.x - arm.At.x, ay = mouse.y - arm.At.y;
                if (ax * ax + ay * ay <= (knob + 2.0f) * (knob + 2.0f))
                    hoveredArm = order[k];
            }
        const bool centerHovered = areaHovered && hoveredArm < 0 && dx * dx + dy * dy <= (knob * 1.2f) * (knob * 1.2f);

        dl->AddCircleFilled(center, radius + 3.0f, areaHovered ? IM_COL32(255, 255, 255, 34) : IM_COL32(18, 20, 24, 90), 40);
        for (int k : order)
        {
            const Arm& arm = arms[k];
            auto dim = [&](ImU32 c, float f) {
                const ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
                return ImGui::GetColorU32(ImVec4(v.x * f, v.y * f, v.z * f, v.w));
            };
            if (arm.Positive)
            {
                dl->AddLine(center, arm.At, dim(arm.Color, 0.85f), 2.0f);
                dl->AddCircleFilled(arm.At, knob, arm.Color, 24);
                const ImVec2 ts = ImGui::CalcTextSize(arm.Label);
                dl->AddText(ImVec2(arm.At.x - ts.x * 0.5f, arm.At.y - ts.y * 0.5f), IM_COL32(18, 20, 24, 255), arm.Label);
            }
            else
            {
                dl->AddCircleFilled(arm.At, knob * 0.82f, (dim(arm.Color, 0.45f) & ~IM_COL32_A_MASK) | IM_COL32(0, 0, 0, 200), 24);
                dl->AddCircle(arm.At, knob * 0.82f, arm.Color, 24, 1.5f);
            }
            if (k == hoveredArm)
                dl->AddCircle(arm.At, knob + 1.5f, IM_COL32(255, 255, 255, 230), 24, 2.0f);
        }
        dl->AddCircleFilled(center, knob * 0.45f, centerHovered ? IM_COL32(255, 255, 255, 230) : IM_COL32(200, 205, 215, 150), 16);

        // What the view is now, under the navigator.
        const std::string label = ProjectionLabel(camera);
        const ImVec2 ts = ImGui::CalcTextSize(label.c_str());
        dl->AddText(ImVec2(center.x - ts.x * 0.5f, center.y + radius + 6.0f), camera.GetViewAxis() != ViewAxis::Free ? UI::AccentU32() : IM_COL32(220, 224, 232, 220),
                    label.c_str());

        if (hoveredArm >= 0)
        {
            const ViewAxis axis = GetViewAxisFromSide(arms[hoveredArm].Direction);
            const ViewAxis target = camera.GetViewAxis() == axis ? GetOppositeViewAxis(axis) : axis;
            UI::Tooltip(std::format("{} ({})", GetViewAxisInfo(target).Name, ShortcutLabel(AxisShortcut(target))).c_str());
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                m_LastSceneView = view.Index;
                SetSceneViewAxis(view.Index, target); // the axis shown already: from the other side
            }
        }
        else if (centerHovered)
        {
            UI::Tooltip(camera.IsOrthographic() ? "Perspektivga o'tish (Num 5)" : "Ortografikka o'tish (Num 5)");
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                m_LastSceneView = view.Index;
                ToggleSceneViewProjection(view.Index);
            }
        }
        return areaHovered;
    }

    // ------------------------------------------------------------------ icons and the gizmo
    void EditorLayer::UI_ViewportIcons(const SceneViewTarget& view)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const glm::mat4 viewProj = view.Camera->GetViewProjection();
        const glm::vec2 size = view.Max - view.Min;
        const ImVec2 mouse = ImGui::GetMousePos();
        const bool gizmoOver = view.Index == 0 ? m_GizmoHovered : m_ExtraViews[view.Index - 1].GizmoHovered;

        // Hidden and inactive entities have no icon; locked ones are drawn faint and cannot be clicked.
        SceneRenderOptions skipped;
        skipped.SkipEditorHidden = true;
        std::unordered_set<entt::entity> hidden = m_ActiveScene->CollectRenderSkipped(skipped);
        // Their size: the view settings' factor (the glyph scales with the circle).
        const float iconScale = m_ViewSettings.TransformGizmo.IconSize;
        const float iconRadius = m_ViewSettings.TransformGizmo.GetIconRadius();
        ImFont* font = ImGui::GetFont();
        const float fontSize = ImGui::GetFontSize() * iconScale;
        auto drawIcon = [&](Entity e, const char* icon, ImU32 color) {
            if (hidden.contains(e)) return;
            glm::vec4 clip = viewProj * glm::vec4(m_ActiveScene->GetWorldPosition(e), 1.0f);
            if (clip.w <= 0.0f) return;
            glm::vec3 ndc = glm::vec3(clip) / clip.w;
            if (std::abs(ndc.x) > 1.1f || std::abs(ndc.y) > 1.1f) return;
            ImVec2 p(view.Min.x + (ndc.x * 0.5f + 0.5f) * size.x, view.Min.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * size.y);
            const float r = iconRadius;
            const bool locked = m_ActiveScene->IsLockedInEditor(e);
            bool hovered = !locked && (mouse.x - p.x) * (mouse.x - p.x) + (mouse.y - p.y) * (mouse.y - p.y) < r * r;
            bool isSelected = m_SceneHierarchyPanel.IsSelected(e);
            if (locked)
                color = (color & ~IM_COL32_A_MASK) | IM_COL32(0, 0, 0, 110);
            dl->AddCircleFilled(p, r, isSelected ? IM_COL32(30, 140, 110, 230) : (hovered ? IM_COL32(50, 55, 65, 230) : IM_COL32(20, 22, 26, 200)));
            dl->AddCircle(p, r, color, 0, 1.5f * std::max(1.0f, iconScale * 0.8f));
            ImVec2 ts = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, icon);
            dl->AddText(font, fontSize, ImVec2(p.x - ts.x * 0.5f, p.y - ts.y * 0.5f), color, icon);
            if (hovered && view.Hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !gizmoOver)
                m_SceneHierarchyPanel.SelectWithModifiers(e, ImGui::GetIO().KeyCtrl, ImGui::GetIO().KeyShift);
        };

        for (auto e : m_ActiveScene->GetAllEntitiesWith<CameraComponent>())
            drawIcon({ e, m_ActiveScene.get() }, ICON_FA_CAMERA, IM_COL32(220, 220, 230, 255));
        for (auto e : m_ActiveScene->GetAllEntitiesWith<DirectionalLightComponent>())
            drawIcon({ e, m_ActiveScene.get() }, ICON_FA_SUN, IM_COL32(255, 210, 90, 255));
        for (auto e : m_ActiveScene->GetAllEntitiesWith<PointLightComponent>())
            drawIcon({ e, m_ActiveScene.get() }, ICON_FA_LIGHTBULB, IM_COL32(255, 230, 140, 255));
        for (auto e : m_ActiveScene->GetAllEntitiesWith<SpotLightComponent>())
            drawIcon({ e, m_ActiveScene.get() }, ICON_FA_BULLHORN, IM_COL32(255, 220, 120, 255));
        for (auto e : m_ActiveScene->GetAllEntitiesWith<Light2DComponent>())
            drawIcon({ e, m_ActiveScene.get() }, ICON_FA_FIRE, IM_COL32(255, 190, 90, 255));
        for (auto e : m_ActiveScene->GetAllEntitiesWith<AudioSourceComponent>())
            drawIcon({ e, m_ActiveScene.get() }, ICON_FA_VOLUME_HIGH, IM_COL32(220, 140, 220, 255));
        for (auto e : m_ActiveScene->GetAllEntitiesWith<AmbientZoneComponent>())
            drawIcon({ e, m_ActiveScene.get() }, ICON_FA_TREE, IM_COL32(120, 220, 140, 255));
    }

    bool EditorLayer::UI_TransformGizmo(const SceneViewTarget& view)
    {
        // While another view drags the selection, this one shows no gizmo (one drag at a time).
        if (m_GizmoEdit.IsActive() && m_GizmoOwner != view.Index)
            return false;
        Entity primary = m_SceneHierarchyPanel.GetSelectedEntity();
        // Screen-space UI is placed by its UI Transform (pixels), not by the Transform: no gizmo for it.
        if (!primary || !primary.IsValid() || m_GizmoMode == GizmoMode::None || m_SceneHierarchyPanel.IsTilePainting() || IsViewportHandleEditing() ||
            primary.HasComponent<UITransformComponent>())
        {
            m_GizmoEdit.End();
            return false;
        }
        // Every selected subtree moves together (an entity inside another selected one follows
        // it); locked ones stay where they are.
        std::vector<Entity> targets = GetGizmoTargets();
        if (targets.empty())
        {
            m_GizmoEdit.End();
            return false;
        }
        if (m_ActiveScene->IsLockedInEditor(primary))
            primary = targets.front(); // the pivot follows an entity that moves

        const EditorCamera& camera = *view.Camera;
        ImGuizmo::PushID(view.Index);
        ImGuizmo::SetOrthographic(camera.IsOrthographic());
        ImGuizmo::SetDrawlist();
        // The gizmo's size on screen and the thickness of its handles (the view settings).
        {
            const TransformGizmoSettings& size = m_ViewSettings.TransformGizmo;
            ImGuizmo::SetGizmoSizeClipSpace(size.GetClipSpaceSize());
            static const ImGuizmo::Style defaults;
            ImGuizmo::Style& style = ImGuizmo::GetStyle();
            const float t = size.LineThickness;
            style.TranslationLineThickness = defaults.TranslationLineThickness * t;
            style.TranslationLineArrowSize = defaults.TranslationLineArrowSize * t;
            style.RotationLineThickness = defaults.RotationLineThickness * t;
            style.RotationOuterLineThickness = defaults.RotationOuterLineThickness * t;
            style.ScaleLineThickness = defaults.ScaleLineThickness * t;
            style.ScaleLineCircleSize = defaults.ScaleLineCircleSize * t;
            style.HatchedAxisLineThickness = defaults.HatchedAxisLineThickness * t;
            style.CenterCircleSize = defaults.CenterCircleSize * t;
        }
        ImGuizmo::SetRect(view.Min.x, view.Min.y, view.Max.x - view.Min.x, view.Max.y - view.Min.y);

        const glm::mat4& projection = camera.GetProjection();
        glm::mat4 viewMatrix = camera.GetViewMatrix();

        // Snapping: the toolbar toggle (magnet), inverted while Ctrl is held; steps per operation.
        bool ctrl = Input::IsKeyPressed(Key::LeftControl) || Input::IsKeyPressed(Key::RightControl);
        bool snap = m_ViewSettings.Snap.IsActive(ctrl);
        float snapValue = m_ViewSettings.Snap.GetStep((int)m_GizmoMode);
        float snapValues[3] = { snapValue, snapValue, snapValue };

        ImGuizmo::OPERATION op = m_GizmoMode == GizmoMode::Translate ? ImGuizmo::TRANSLATE :
                                 m_GizmoMode == GizmoMode::Rotate ? ImGuizmo::ROTATE : ImGuizmo::SCALE;
        ImGuizmo::MODE mode = (m_GizmoLocal || m_GizmoMode == GizmoMode::Scale) ? ImGuizmo::LOCAL : ImGuizmo::WORLD;

        // Pivot: the primary entity, or the center of the selection. While dragging, the
        // gizmo's own previous result is fed back (ImGuizmo works incrementally).
        if (m_GizmoEdit.IsActive() && m_GizmoEditMode != m_GizmoMode)
            m_GizmoEdit.End(); // W/E/R pressed mid-drag: start over with the new operation
        bool dragging = m_GizmoEdit.IsActive() && m_GizmoEdit.GetScene() == m_ActiveScene.get();
        bool center = m_GizmoPivotCenter && targets.size() > 1;
        glm::mat4 pivot = dragging ? m_GizmoMatrix
                                   : GroupTransformEdit::ComputePivot(*m_ActiveScene, targets, primary, center, mode == ImGuizmo::LOCAL);
        glm::mat4 transform = pivot;
        ImGuizmo::Manipulate(glm::value_ptr(viewMatrix), glm::value_ptr(projection), op, mode, glm::value_ptr(transform),
                             nullptr, snap ? snapValues : nullptr);
        const bool hovered = ImGuizmo::IsOver();
        const bool using_ = ImGuizmo::IsUsing();
        ImGuizmo::PopID();
        if (view.Index == 0)
            m_GizmoHovered = hovered;

        if (using_)
        {
            if (!dragging)
            {
                m_GizmoEdit.Begin(*m_ActiveScene, targets, pivot);
                m_GizmoEditMode = m_GizmoMode;
                m_GizmoOwner = view.Index;
            }
            m_GizmoMatrix = transform;
            if (view.Index == 0 && m_GizmoAltDuplicatePending && m_SceneState == SceneState::Edit)
            {
                // Alt + drag: the copies move, the originals stay where they were. ImGuizmo is
                // "using" from the click on, so the copies are made only once the gizmo really
                // moves past the drag threshold: a plain Alt+click leaves nothing behind. Until
                // then nothing was applied, so the copies sit exactly on the originals and the
                // drag restarts from its first pivot with them.
                if (!ImGui::IsMouseDragging(ImGuiMouseButton_Left) || !m_GizmoEdit.HasMoved(transform))
                    return hovered;
                m_GizmoAltDuplicatePending = false;
                if (size_t count = m_SceneHierarchyPanel.DuplicateSelection(true))
                {
                    m_GizmoEdit.Begin(*m_ActiveScene, GetGizmoTargets(), m_GizmoEdit.GetStartPivot());
                    ShowViewportMessage(count > 1 ? std::format(ICON_FA_CLONE "  {} ta nusxa sudralmoqda", count)
                                                  : std::string(ICON_FA_CLONE "  Nusxa sudralmoqda"));
                }
            }
            m_GizmoEdit.Update(transform, (GroupTransformEdit::Operation)(int)m_GizmoMode);
            MarkDirty();
        }
        else if (m_GizmoEdit.IsActive())
        {
            m_GizmoEdit.End();
            m_GizmoOwner = -1;
        }
        return hovered;
    }

    // ------------------------------------------------------------------ camera settings
    void EditorLayer::UI_CameraSettingsPopup(int index)
    {
        ImGui::SetNextWindowSizeConstraints(ImVec2(ImGui::GetFontSize() * 21.0f, 0), ImVec2(ImGui::GetFontSize() * 28.0f, FLT_MAX));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 5));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 8));
        if (ImGui::BeginPopup("##CameraSettings"))
        {
            UI_CameraSettings(index);
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar(2);
    }

    bool EditorLayer::UI_CameraSettings(int index)
    {
        EditorCameraDefaults& c = m_Preferences.Camera;
        EditorCamera& camera = GetSceneViewCamera(index);
        const float em = ImGui::GetFontSize();
        const float field = em * 11.0f;
        bool speed = false, other = false;
        ImGui::TextColored(UI::AccentColor(), ICON_FA_VIDEO "  Editor kamerasi");
        ImGui::SameLine();
        ImGui::TextDisabled("(%s)", index == 0 ? "viewport" : std::format("ko'rinish {}", index + 1).c_str());

        ImGui::SeparatorText("Uchish: o'ng tugma + WASD / QE");
        float flySpeed = camera.FlySpeed;
        ImGui::SetNextItemWidth(field);
        if (ImGui::SliderFloat("Tezlik", &flySpeed, EditorCameraDefaults::MinFlySpeed, EditorCameraDefaults::MaxFlySpeed, "%.1f m/s",
                               ImGuiSliderFlags_Logarithmic))
            speed = true;
        if (ValuePresets("speed", flySpeed, { 1.0f, 3.0f, 6.0f, 15.0f, 40.0f }))
            speed = true;
        if (speed)
            c.FlySpeed = flySpeed;
        ImGui::TextDisabled("Uchayotganda g'ildirak tezlikni o'zgartiradi, Shift - 3x");
        ImGui::SetNextItemWidth(field);
        other |= ImGui::SliderFloat("Tezlanish", &c.FlyAcceleration, 0.0f, EditorCameraDefaults::MaxAcceleration,
                                    c.FlyAcceleration > 0.0f ? "+%.1fx / s" : "yo'q");
        if (ImGui::IsItemHovered())
            UI::Tooltip(std::format("Tugmalar uzoq bosilsa tezlik oshib boradi: har soniyada shuncha marta qo'shiladi, ko'pi bilan {:g}x",
                                    MaxFlyBoost).c_str());

        ImGui::SeparatorText("Sichqoncha");
        ImGui::SetNextItemWidth(field);
        other |= ImGui::SliderFloat("Sezgirlik", &c.LookSensitivity, EditorCameraDefaults::MinSensitivity, EditorCameraDefaults::MaxSensitivity,
                                    "%.2fx", ImGuiSliderFlags_Logarithmic);
        other |= ImGui::Checkbox("Y o'qini teskari qilish", &c.InvertY);
        if (ImGui::IsItemHovered())
            UI::Tooltip("Sichqoncha yuqoriga - kamera pastga qaraydi (uchishda va aylanib qarashda)");

        ImGui::SeparatorText("Proyeksiya");
        ImGui::SetNextItemWidth(field);
        other |= ImGui::SliderFloat("Ko'rish burchagi", &c.FOV, EditorCameraDefaults::MinFOV, EditorCameraDefaults::MaxFOV, "%.0f\xc2\xb0");
        other |= ValuePresets("fov", c.FOV, { 30.0f, 45.0f, 60.0f, 75.0f, 90.0f }, "\xc2\xb0");
        ImGui::SetNextItemWidth(field);
        other |= ImGui::DragFloat("Yaqin chegara", &c.NearClip, 0.005f, EditorCameraDefaults::MinNear, EditorCameraDefaults::MaxNear, "%.3f");
        ImGui::SetNextItemWidth(field);
        other |= ImGui::DragFloat("Uzoq chegara", &c.FarClip, 10.0f, 1.0f, EditorCameraDefaults::MaxFar, "%.0f");
        const bool locked = index == 0 && m_2DMode;
        bool ortho = camera.IsOrthographic();
        ImGui::BeginDisabled(locked);
        if (ImGui::Checkbox("Ortografik (shu ko'rinish)", &ortho))
            ToggleSceneViewProjection(index);
        ImGui::EndDisabled();
        if (locked && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            UI::Tooltip("2D rejim doim ortografik");

        ImGui::Separator();
        if (ImGui::Button(ICON_FA_ARROW_ROTATE_LEFT "  Standart qiymatlar"))
        {
            c = EditorCameraDefaults();
            speed = other = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Hamma ko'rinishlarga");
        if (speed || other)
        {
            c.Sanitize();
            ApplyCameraDefaults(speed);
            MarkPreferencesDirty();
        }
        return speed || other;
    }

}
