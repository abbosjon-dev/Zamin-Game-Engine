#pragma once

#include <Zamin/Zamin.h>

#include "Panels/SceneHierarchyPanel.h"
#include "Panels/AssetInspectorPanel.h"
#include "Panels/AudioMixerPanel.h"
#include "Panels/LocalizationPanel.h"
#include "Panels/ContentBrowserPanel.h"
#include "Panels/ConsolePanel.h"
#include "Panels/ProjectSettingsPanel.h"
#include "Panels/UnusedAssetsPanel.h"
#include "Panels/SpriteEditorPanel.h"
#include "Panels/AnimationTimelinePanel.h"
#include "Panels/AnimatorGraphPanel.h"
#include "Panels/ScenesPanel.h"
#include "AssetUsageCache.h"
#include "SceneThumbnails.h"
#include "SubScenePreviews.h"

#include <array>
#include <atomic>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace Zamin {

    struct EditorOverlayView;

    class EditorLayer : public Layer
    {
    public:
        explicit EditorLayer(std::filesystem::path startupProject = {});
        ~EditorLayer() override;

        void OnAttach() override;
        void OnDetach() override;
        void OnUpdate(Timestep ts) override;
        void OnImGuiRender() override;
        void OnEvent(Event& e) override;

    private:
        enum class SceneState { Edit = 0, Play = 1, Simulate = 2 };
        enum class GizmoMode { None = -1, Translate = 0, Rotate = 1, Scale = 2 };
        // A scene view's camera and where its picture is on screen (EditorSceneViews.cpp).
        struct SceneViewTarget
        {
            int Index = 0;                 // 0: the main viewport, 1..3: the extra views
            EditorCamera* Camera = nullptr;
            glm::vec2 Min{ 0.0f }, Max{ 0.0f }; // the picture on screen
            bool Hovered = false;
        };

        bool OnKeyPressed(KeyPressedEvent& e);
        bool OnMouseButtonPressed(MouseButtonPressedEvent& e);

        // ---- projects
        void NewProject(const std::filesystem::path& directory, const std::string& name);
        bool OpenProject(const std::filesystem::path& path);
        void OpenProjectDialog();
        void CloseProject();

        // ---- recent projects and scenes (EditorRecentFiles.cpp): per user in recent-projects.json /
        // recent-scenes.json (a scene with the project it belongs to); the File menu, the launcher.
        void AddRecentProject(const std::filesystem::path& path);
        void LoadRecentProjects();
        void LoadRecentScenes();
        void SaveRecentScenes();
        void AddRecentScene(const std::filesystem::path& scene);
        // Opens the scene's project first when another one (or none) is open.
        bool OpenRecentScene(const RecentFileList::Entry& entry);
        void UI_RecentScenesMenu();
        // The launcher's "So'nggi sahnalar" cards (true: one was opened).
        bool UI_LauncherRecentScenes();

        // ---- scenes
        void NewScene();
        void OpenScene();
        bool OpenScene(const std::filesystem::path& path);
        void SaveScene();
        void SaveSceneAs();
        void SerializeScene(const Ref<Scene>& scene, const std::filesystem::path& path);
        // `newView`: another scene or another view of it (open, play, stop, a level switch) - the
        // viewport's auto exposure starts over. False for the same scene rebuilt (undo, references).
        void SetActiveScene(const Ref<Scene>& scene, bool newView = true);
        // The scene changed (edit mode): an undo step once nothing is held any more. `undoName` names
        // the step (the first name given wins); without one it is named from what changed.
        void MarkDirty(std::string_view undoName = {})
        {
            if (m_SceneState != SceneState::Edit)
                return;
            m_SceneDirty = true;
            m_SceneStatsDirty = true;
            m_PendingUndo = true;
            if (!undoName.empty() && m_PendingUndoName.empty())
                m_PendingUndoName = undoName;
            m_AssetUsage.MarkDirty();
        }

        // ---- undo / redo (EditorUndoHistory.cpp): scene snapshots with the selection, each step
        // named; selection changes are steps of their own (preferences); the "Undo tarixi" window.
        std::string TakeSnapshot();
        UndoSelection CaptureUndoSelection() const;
        void ApplyUndoSelection(const UndoSelection& selection);
        // The history starts again from the scene as it is (`name`: its first state; `saved`: the
        // scene's file holds it).
        void ResetUndoHistory(std::string name = {}, bool saved = true);
        void CommitUndo();
        void CommitSelectionUndo();
        // Each frame (after the panels): selection changes become steps (or update the current
        // state), pending steps are made once no mouse button / field / gizmo is held.
        void UpdateUndoTracking();
        void Undo();
        void Redo();
        void JumpToUndoState(size_t index);
        void ApplyUndoMove(const UndoHistory::Move& move);
        void RestoreSnapshot(const std::string& snapshot);
        // Whether the editor is in the middle of an edit (a drag, a field typed into, the gizmo).
        bool IsEditInProgress() const;
        void UI_UndoHistory();
        // ZAMIN_EDITOR_UNDO: steps made one by one for automation (see the OnUpdate hook).
        void ApplyUndoAutomation(std::string_view spec);

        // ---- clipboard / prefabs
        void CopySelectedEntity();
        void PasteEntity();
        void SavePrefab(Entity entity);
        Entity InstantiatePrefabInEditor(const std::filesystem::path& assetPath, Entity parent);
        // A content browser item (asset-relative path) dropped onto the viewport: a scene opens,
        // an image / model / sound / prefab becomes an entity (edit mode). Returns that entity.
        Entity DropAssetOnViewport(const std::string& assetPath);
        // ---- materials dragged from the content browser (edit mode)
        // The material asset (path relative to Assets) for an entity's mesh renderer - the whole
        // renderer (submesh < 0) or one part of a model - as one undo step, with a viewport notice.
        bool ApplyMaterialToEntity(Entity entity, const std::string& assetPath, int submesh = -1);
        // The mesh the mouse is over in the viewport (not locked; null without a mesh renderer).
        Entity GetMeshUnderMouse();
        // A .zmat dragged over the viewport this frame: shown on the mesh under the mouse, applied
        // on drop (Shift on a model with several parts: a menu asks which part gets it).
        void UpdateMaterialDrag(const std::string& assetPath);
        // The label next to the mouse while a material is dragged over the viewport.
        void DrawMaterialDragLabel(ImVec2 at, Entity target);
        void UI_MaterialSlotPopup();

        // ---- project files
        // Files dropped onto the window from outside: a project opens; files from outside the
        // project are copied into Assets (the content browser's folder when dropped on it, else
        // Assets/<type folder>), and onto the viewport they also become entities.
        void HandleFileDrop(const std::vector<std::string>& paths, glm::vec2 mouse);
        // The content browser moved / renamed files: the open scene, its undo history, the
        // clipboard, the scene path, camera bookmarks and the project's start scene follow.
        void OnAssetsMoved(const AssetFiles::MoveResult& result);
        // Assets the watcher saw moved outside the editor ((old, new) paths relative to Assets).
        void OnAssetsMovedOutside(const std::vector<std::pair<std::string, std::string>>& moves);
        // Asset folder watcher (started with the project): the registry follows the files, changed
        // textures / fonts are imported again in place, the content browser is refreshed.
        void StartAssetWatcher();
        void ProcessAssetChanges();
        // Writes the asset registry snapshot to the project's cache folder when it changed (at most
        // every few seconds unless `now`): files moved while the editor is closed are found again.
        void SaveAssetRegistryCache(bool now);
        // Shows a project file in the content browser and the inspector (asset inspector links,
        // the unused assets window).
        void RevealAsset(const std::filesystem::path& path);
        // Shows the Sprite Editor for a texture (path relative to Assets).
        void OpenSpriteEditor(const std::string& texturePath);
        // Play mode: after the scene manager's switches (end of the frame) the editor follows its
        // active scene (the hierarchy, the inspector).
        void SyncRuntimeScenes();
        // The scene whose picture the viewport shows: in play mode the scene manager's render scene
        // (its camera, sky and post-processing), else the active scene.
        Scene& GetViewScene();

        void OnScenePlay();
        void OnSceneSimulate();
        void OnSceneStop();
        void OnScenePause();

        void DuplicateSelectedEntity();
        void DeleteSelectedEntity();
        // Selected subtrees the gizmo moves (locked ones are left out).
        std::vector<Entity> GetGizmoTargets();
        // Eye / lock of the selection (H, Alt+H, Edit menu) with a viewport notice.
        void ToggleSelectionHidden();
        void ToggleSelectionLocked();
        // Alt+Shift+A: switches the selection off / on (TagComponent::Active, game state).
        void ToggleSelectionActive();
        void ShowAllHidden();
        void Set2DMode(bool enabled);

        // ---- editor camera framing / bookmarks
        void FocusEntity(Entity entity);
        // Flies the camera so that the entities (with their children) fill the view. False when
        // there is nothing to frame.
        bool FrameEntities(const std::vector<Entity>& entities);
        void FrameSelection();   // F
        void FrameAll();         // Shift+F
        CameraBookmarks& GetSceneBookmarks();
        void SaveCameraBookmark(int slot);   // Ctrl+1..5
        void RecallCameraBookmark(int slot); // 1..5
        // Short notice drawn over the bottom of the viewport for a moment.
        void ShowViewportMessage(std::string text);

        // ---- viewport preferences (snapping, grid, camera bookmarks): per user
        void LoadViewSettings();
        void SaveViewSettings();
        void MarkViewSettingsDirty() { m_ViewSettingsDirty = true; }

        // ---- editor preferences (EditorPreferencesWindow.cpp): Tahrirlash > Sozlamalar, per user in
        // preferences.json - the theme's accent colour and font size, the play-mode tint, the editor
        // camera's defaults, the console's options.
        void LoadPreferences();
        void SavePreferences();
        void MarkPreferencesDirty() { m_PreferencesDirty = true; }
        // Every view's editor camera takes the preferences' FOV / clip planes, acceleration and mouse
        // settings (and the fly speed: `flySpeed`).
        void ApplyCameraDefaults(bool flySpeed);
        // The ImGui style follows the preferences and the scene state (the play-mode tint): applied
        // at the start of a frame when something changed.
        void UpdateEditorTheme();
        void UI_Preferences();
        bool IsPlayTintActive() const;

        // ---- the game view's resolution while playing (EditorGameView.cpp)
        // Play with a resolution preset other than Free: the game picture is letterboxed / scaled.
        bool IsGameViewSized() const;
        // The viewport picture in the area [areaMin, areaMin + areaSize] (screen): the whole area,
        // or the game view's layout with bars around it. Sets the viewport's bounds (where the
        // picture is on screen) and size (the size it is drawn at).
        void PlaceViewportImage(ImVec2 areaMin, ImVec2 areaSize, ImTextureID texture);
        // A screen point in the viewport's picture (pixels from its top-left corner, at the size it
        // is drawn at - the game's resolution while it is scaled).
        glm::vec2 ScreenToViewportPixel(ImVec2 screen) const;
        void UI_GameViewResolutionPopup();
        bool UI_GameViewResolutionItems();

        // ---- keyboard shortcuts (EditorShortcuts.cpp): one table for the keys the editor dispatches,
        // the menus' labels and the shortcuts window (Yordam > Klaviatura yorliqlari, F1); rebound
        // keys are kept in preferences.json.
        void RegisterShortcuts();
        void ApplyShortcutPreferences();
        void SaveShortcutPreferences();
        // What the editor is doing this frame (ShortcutSituation bits).
        uint32_t GetShortcutSituations() const;
        void HandleShortcuts();
        const char* ShortcutLabel(std::string_view id) const { return m_Shortcuts.GetDisplay(id); }
        void UI_ShortcutsWindow();

        // ---- autosave (EditorAutosaveRecovery.cpp): a safety copy of the open scene every few
        // minutes in <project>/.zamin/autosave (never over the scene's file), the project's lock file
        // (a crashed session is told from a closed one) and the recovery prompt on opening a scene.
        void AcquireProjectLock();
        void ReleaseProjectLock();
        std::filesystem::path GetAutosavePath() const;
        void UpdateAutosave();
        bool WriteAutosave(bool manual);
        // The copy this session wrote for the open scene goes (the scene was saved or closed).
        void DiscardSessionAutosave();
        // After a scene was opened (`path` empty: the project's unsaved scene): a newer copy is
        // offered back, a stale one deleted.
        void CheckAutosaveRecovery(const std::filesystem::path& scenePath);
        // The prompt's answers besides recovering: the copy put aside to be offered again, or deleted.
        void DeferAutosaveRecovery();
        void DeleteOfferedAutosave();
        void RecoverAutosave();
        void UI_AutosaveRecovery();
        void UI_AutosavePreferences(bool& changed);

        // ---- C++ scripts
        void BuildScripts(bool reloadAfter = true);
        void ReloadScriptModule();
        // Writes Source/<className>.cpp from a template and starts a script build.
        bool CreateScriptClass(const std::string& className, ScriptTemplateKind kind = ScriptTemplateKind::Full);
        void CheckModuleHotReload(Timestep ts);

        // ---- the external code editor (EditorExternalTools.cpp): C++ files open in the user's own
        // editor (Sozlamalar > Tashqi muharrir; $VISUAL / $EDITOR, VS Code, xdg-open by default).
        bool OpenInExternalEditor(const std::filesystem::path& file, int line = 0, int column = 0);
        // A script class's source (found in the project's Source folder by its ZM_REGISTER_SCRIPT).
        bool OpenScriptClassSource(const std::string& className);
        // Where a class is written (cached for a few seconds; invalid when not found).
        const SourceLocation& GetScriptSource(const std::string& className);
        void UI_ExternalEditorPreferences(bool& changed);

        // ---- the script build's messages (EditorBuildOutput.cpp): the compiler's / linker's / CMake's
        // errors and warnings with file and line ("Kompilyatsiya" window, the status bar's badge);
        // a double click opens the place in the external editor.
        void BeginBuildOutput(const std::filesystem::path& buildDirectory);
        // A line of the build's output (the build thread).
        void AddBuildOutputLine(const std::string& line);
        void FinishBuildOutput(bool succeeded, double seconds);
        void OpenDiagnostic(const CompilerDiagnostic& diagnostic);
        void UI_BuildOutput();
        void UI_BuildStatusBadge();
        // ZAMIN_EDITOR_BUILD_LOG=<file>: a saved build output read as if a build had just failed with it.
        void LoadBuildLogAutomation(const std::filesystem::path& file);

        // ---- idle throttling (EditorIdleThrottle.cpp): fewer frames while the window is not in front or
        // nothing happens (Sozlamalar > Ishlash); input wakes it up at once.
        void UpdateIdleThrottle();
        // Something worth drawing at full speed for a moment happened without input (a build ended...).
        void WakeEditor() { m_WakeTime = ImGui::GetCurrentContext() ? ImGui::GetTime() : 0.0; }
        void UI_IdlePreferences(bool& changed);

        // ---- export
        void BuildGame(const std::filesystem::path& outputDirectory);
        // The export's report of the assets left out (the log and excluded-assets.txt).
        void ReportExcludedAssets(const ExportAssetPlan& plan, const std::string& gameName, const std::filesystem::path& outputDirectory);

        // ---- UI
        void UI_DockSpace();
        void UI_MenuBar();
        void UI_Viewport();
        void UI_ViewportToolbar();
        void UI_SnapSettingsPopup();
        void UI_GridSettingsPopup();
        void UI_CameraBookmarksPopup();
        void UI_ViewModePopup();
        // Debug drawing: the toolbar's popup and the view menu share the settings' widgets.
        void UI_DebugDrawPopup();
        bool UI_DebugDrawSettings();
        // The viewport's debug draw settings become DebugDraw's (the runtime ones scripts can change
        // too): when they change, and as play / simulation starts and stops.
        void ApplyDebugDrawSettings();
        // The toolbar button / view menu toggle: switches what is shown now (a script may have
        // turned it off) and applies the viewport's settings.
        void ToggleDebugDraw();
        // Component visualisations (camera frustums, light directions, audio radii): the icons'
        // toolbar popup and the view menu share the settings' widgets.
        void UI_ComponentGizmosPopup();
        bool UI_ComponentGizmoSettings();
        void UI_TransformGizmoPopup();
        bool UI_TransformGizmoSettings();
        bool UI_SelectionOutlineSettings();
        void UI_JointLabels();
        // The selected Polygon / Edge collider 2D's points in the viewport: drag, add on an edge,
        // Ctrl + click / Delete removes (Inspector: "Nuqtalarni tahrirlash").
        void UI_ColliderPointEditor();
        // The selected entity's 2D joint in the viewport (Inspector: "Langarlarni tahrirlash"): drag its
        // anchor, connected anchor, a prismatic / wheel axis and the limits (revolute angles, slide range).
        void UI_JointAnchorEditor();
        // The same for a 3D joint: anchors dragged in the plane facing the camera (a picked anchor also
        // gets a translate gizmo), the hinge / slider / cone axis, hinge / cone twist angles, slider
        // limits, distance lengths and the cone's swing angle.
        void UI_Joint3DAnchorEditor();
        void UI_ViewMenu();
        void UI_ViewportMessage();
        // Scene load problems (SceneLoadReport): a dismissable banner over the viewport, the details
        // in the "Sahna yuklash muammolari" window (opening a scene, play-mode scene switches).
        void ShowSceneLoadReport(const SceneLoadReport& report);
        void UI_SceneLoadBanner();
        void UI_SceneLoadReport();
        void UI_MaximizedGame();
        // The game's cursor (Cursor: hidden / confined / locked / its picture) while its view has the focus;
        // Esc gives the editor the cursor back until the game view is clicked again.
        void UpdateGameCursor();
        void UI_TransparentOrder();
        // The icons of cameras, lights and sounds in a view (a click selects).
        void UI_ViewportIcons(const SceneViewTarget& view);
        // ---- screen-space UI in the viewport (EditorLayerUIRect.cpp): the selected UI element's rect
        // gizmo (move / resize handles, pivot, anchors in its parent's rectangle) and picking UI
        // elements by their rectangles (`viewPixel`: view px, origin bottom-left).
        void UI_UIRectGizmo();
        Entity PickUIElement(const glm::vec2& viewPixel);
        // The rect gizmo's `handle` dragged to `mouse` (the element's layout units) from where the
        // drag began (m_UIRect): keepAspect (Shift, corners), symmetric (Alt), snapping (10 units).
        void ApplyUIRectDrag(Entity entity, int handle, const glm::vec2& mouse, bool keepAspect, bool symmetric, bool snapping);
        // ZAMIN_EDITOR_UI_DRAG="<handle>:<dx>,<dy>": a drag of the selected element's handle by view px.
        void ApplyUIRectAutomation(const char* spec);
        bool IsUIRectGizmoHovered() const { return m_UIRect.Hover >= 0; }
        void UI_SceneSettings();
        // The scene settings' "Ma'lumot" section: description, author, tags, format version, thumbnail.
        void UI_SceneMetadata(bool& changed);
        // Draws the open scene's thumbnail file again (SaveScene does it); false when it has no file.
        bool RenderSceneThumbnail();
        void UI_Stats();
        // The profiler: the last frame's scopes as a tree (nested scopes inside), in the Stats panel
        // (`header`: under a collapsing header) and in a window of its own.
        void UI_ProfilerTree(bool header);
        void UI_ProfilerWindow();
        // The recent scene loads, stage by stage (SceneLoadProfiler): the Stats and Profiler windows.
        void UI_SceneLoadTimings(bool header);
        // The audio profiler (Stats panel): voices against the limit, a graph, by kind and mixer group.
        void UI_AudioProfiler();
        void UI_AnimationStats();
        // The statistics overlay over the viewport (FPS graph, draw calls, triangles, culling):
        // the overlay itself, the toolbar's popup and the view menu sharing its settings' widgets.
        void UI_StatsOverlay();
        // The selected Particle System's play / pause / restart / stop and count (EditorParticleOverlay.cpp).
        void UI_ParticleOverlay();
        void UI_StatsOverlayPopup();
        bool UI_StatsOverlaySettings();
        void UI_StatusBar();
        // Selected entity info in the status bar (name, position, components...) up to `maxX`.
        void UI_StatusBarSelection(float maxX);
        void UI_Launcher();
        void UI_Modals();

        // ---- window layouts (EditorLayouts.cpp): the built-in ones (Standart, 2D, Animatsiya,
        // 4 ko'rinish) built by code, the user's saved ones (ImGui's window settings + the open
        // windows, ~/.config/zamin/layouts/*.zlayout); Oyna > Joylashuv.
        struct LayoutPanel
        {
            const char* Key;      // in layout files ("Hierarchy")
            const char* Window;   // the window's ImGui id ("###Hierarchy")
            bool* Open;
        };
        std::vector<LayoutPanel> GetLayoutPanels();
        void RequestBuiltInLayout(BuiltInLayout layout);
        void RequestLayout(const std::string& name);
        // A saved layout is loaded between frames (OnUpdate: ImGui takes window settings then).
        void ApplyPendingLayout();
        // In the frame before the dock space: a built-in layout's dock tree and open windows.
        void UI_BuildLayout(BuiltInLayout layout, unsigned int dockspaceID);
        bool SaveCurrentLayout(const std::string& name);
        void UI_LayoutMenu();
        void UI_LayoutWindows();   // the save prompt and Joylashuvlar (manage) window
        // ZAMIN_EDITOR_LAYOUT=<built-in key | saved name>[,save=<name>][,manage][,saveprompt].
        void ApplyLayoutAutomation(std::string_view spec);

        // ---- scene views (EditorSceneViews.cpp): the main viewport (view 0) and up to three extra
        // ones (Oyna > Ko'rinishlar), each with its own editor camera; axis views (Top / Front /
        // Right ... orthographic) from the toolbar, the axis navigator or the numpad keys.
        SceneViewTarget GetSceneViewTarget(int index);
        // The view camera shortcuts act on: the one under the mouse, else the one with the keyboard.
        int GetShortcutSceneView() const;
        EditorCamera& GetSceneViewCamera(int index);
        bool IsAnySceneViewHovered() const;
        bool IsAnySceneViewFocused() const;
        // An axis view (Free: perspective) for a view with a short notice; the main viewport in the
        // 2D mode leaves it first.
        void SetSceneViewAxis(int index, ViewAxis axis);
        void ToggleSceneViewProjection(int index);
        void OpenSceneView(int extraIndex, bool open);
        // The extra views' pictures (after the main one): each its own camera, grid, selection.
        void RenderSceneViews(Timestep ts);
        void UI_SceneViews();
        void UI_SceneViewToolbar(int extraIndex);
        // The axis navigator in a view's corner: click an axis for its view, the centre toggles the
        // projection. True: the mouse is over it.
        bool UI_ViewAxisNavigator(const SceneViewTarget& view, ImVec2 center, float radius);
        // The axis view menu (Perspektiv / Yuqoridan / Old ...) of a view.
        void UI_ViewAxisItems(int index);
        // The transform gizmo in a view (the main viewport's tools: mode, snapping, pivot).
        bool UI_TransformGizmo(const SceneViewTarget& view);
        // The gizmo drag (of whichever view) is over: no view owns the gizmo any more.
        void EndGizmoDrag() { m_GizmoEdit.End(); m_GizmoOwner = -1; }
        // ---- the editor cameras' settings popup (fly speed, acceleration, FOV, clip planes, mouse)
        void UI_CameraSettingsPopup(int index);
        bool UI_CameraSettings(int index);
        // A fly speed changed with the wheel becomes the preferences' (every view's, the next session's).
        void SyncFlySpeed();
        // Before the dock space: extra views opened from the menu are docked next to the viewport.
        void DockPendingSceneViews();
        // The selection outline in an extra view's picture.
        void RenderSceneViewOutline(int extraIndex);

        // ---- scene statistics (EditorSceneStats.cpp): Oyna > Sahna statistikasi - entities, components,
        // scripts, prefab instances and the assets the scene names with memory estimates; a row
        // selects its entities.
        void UI_SceneStatistics();
        // Counts the scene again when it changed (at most twice a second; while playing once a second).
        void UpdateSceneStatistics(bool force);
        // Selects the entities of a row in the shown scene (`add`: Ctrl, to the selection); `frame`
        // flies the camera to them.
        void SelectStatsEntities(const std::vector<UUID>& ids, bool add, bool frame);
        void UI_StatsEntityTooltip(const std::vector<UUID>& ids);

        // ---- entity names over the scene views (EditorEntityLabels.cpp)
        void UI_EntityLabels(const SceneViewTarget& view, const glm::mat4& viewProjection, const glm::vec3& cameraPosition, bool orthographic);
        void UI_EntityLabelsPopup();
        bool UI_EntityLabelSettings();

        void OnOverlayRender();
        // The overlay's camera frustums, light direction arrows and audio source radii (the view
        // settings' Gizmos) seen through `view`; `hidden`: entities the overlay skips unless they
        // are selected.
        void DrawComponentGizmos(const EditorOverlayView& view, const std::unordered_set<entt::entity>& hidden);
        // Whether an entity gets a visualisation of `visibility` (its selection, the eye, inactive).
        bool ShowsGizmo(entt::entity e, GizmoVisibility visibility, const std::unordered_set<entt::entity>& hidden);
        // Gizmos.Bounds: the boxes around what entities draw (local and / or world).
        void DrawBoundingBoxes(const std::unordered_set<entt::entity>& hidden);
        // Gizmos.Skeletons: the bones (parent joint -> joint) and joints of skinned models.
        void DrawSkeletons(const EditorOverlayView& view, const std::unordered_set<entt::entity>& hidden);
        // Gizmos.Normals: the vertex normals of meshes and models (skinned ones posed).
        void DrawVertexNormals(const EditorOverlayView& view, const std::unordered_set<entt::entity>& hidden);
        // The selection outline: who gets it (the selection, its children, the entity under the
        // mouse) and the pass over the viewport picture after post-processing.
        std::vector<OutlineEntity> CollectSelectionOutline();
        Entity GetOutlineHoverEntity();
        void AppendOutlineHover(std::vector<OutlineEntity>& entities, Entity hovered);
        void RenderSelectionOutline();
        // DebugDraw's queue into the final viewport picture (depth tested against the scene).
        void RenderDebugDraw();
        // Picks for m_PendingClickThrough (an extra ID pass into the viewport framebuffer).
        void ResolveClickThrough();
        void UpdateTilePainting();
        bool IsColliderPointEditing() const;
        bool IsJointAnchorEditing() const;
        bool IsJoint3DAnchorEditing() const;
        // Collider points or joint anchors are being edited: viewport clicks go to their handles (no
        // picking, no transform gizmo, Delete does not delete the entity).
        bool IsViewportHandleEditing() const { return IsColliderPointEditing() || IsJointAnchorEditing() || IsJoint3DAnchorEditing(); }
        // The cell of the selected tilemap under the mouse (or the automation's hover cell); false
        // when the mouse does not hit the tilemap's plane. The cell may lie outside the map.
        bool GetTileCellUnderMouse(Entity tilemap, glm::ivec2& cell);
        // One use of the current tile tool at `cell` (Rectangle: from `from`). Returns cells changed.
        int ApplyTileTool(Entity tilemap, SceneHierarchyPanel::TileTool tool, glm::ivec2 from, glm::ivec2 cell, bool erase);
        // What the tile tool would leave at the mouse now (m_TilePreview), handed to the scene view.
        void UpdateTilePreview(SceneRenderOptions& options);
        // ZAMIN_EDITOR_TILEMAP automation: tool, layer, rule, tile, hover / drag preview, apply.
        void ApplyTilemapAutomation(std::string_view spec);
        bool ViewportMouseToPlane(float planeZ, glm::vec3& out);
        // The editor camera's ray through the mouse (direction normalised); false outside a sized viewport.
        bool ViewportMouseRay(glm::vec3& origin, glm::vec3& direction);
        glm::mat4 OverlayViewProjection();
        // The camera the overlay is drawn with (the editor camera; the game's camera in play
        // mode). False: none (play mode without a camera).
        bool OverlayCamera(glm::mat4& view, glm::mat4& projection);
        // A pixel of the viewport (bottom-left origin) in the scene framebuffer, which is drawn at
        // the render scale (SceneSettings::RenderScale) of the viewport's size.
        glm::ivec2 ViewportToRenderPixel(glm::ivec2 pixel) const;
        // Whether the scene view shows a debug view / wireframe now (edit + simulate only).
        bool IsDebugViewActive() const;
        void UpdateWindowTitle();

    private:
        std::filesystem::path m_StartupProject;

        Ref<Framebuffer> m_Framebuffer;
        Ref<Framebuffer> m_FinalFramebuffer; // after post-processing + overlays
        Ref<Scene> m_ActiveScene;
        Ref<Scene> m_EditorScene;
        // Play mode: every scene of the running game (additive ones, DontDestroyOnLoad); the hierarchy
        // shows one of them at a time (m_ActiveScene).
        std::unique_ptr<SceneManager> m_RuntimeScenes;
        // What the editor showed when play started: when the game has switched to another scene by
        // the time it stops (or the hierarchy shows another loaded scene), the edit scene comes back
        // with this selection and camera view (the game's scenes had other entities).
        struct PlayStartState
        {
            std::vector<UUID> Selection;     // the primary last
            EditorCameraState Camera;
            bool Mode2D = false;
            std::weak_ptr<Scene> RuntimeStart; // the copy of the edit scene the game started with
            std::string SceneName;
            std::string QualityLevel;  // the editor's preview level: back on Stop (a script may change it)
            std::string Language;      // the editor's preview language: back on Stop (a language menu may change it)
        } m_PlayStart;
        // Project scenes the Scenes panel marks to be loaded with Play (additively, next to the edit scene).
        void LoadPlayAdditiveScenes();
        uint64_t m_RuntimeActiveSerial = 0;
        std::filesystem::path m_EditorScenePath;
        // The format version the open scene's file was written in (SceneMigration; older ones are
        // migrated on load and written in the current version when saved).
        int m_EditorSceneFileVersion = SceneMigration::CurrentVersion;
        std::string m_NewSceneTag;          // the scene settings' new tag field
        SceneThumbnails m_SceneThumbnails;  // scene pictures at their own size (inspector, Scenes panel)
        SubScenePreviews m_SubScenePreviews; // the open scene's sub-scenes, drawn in place (edit mode)
        bool m_ScenesPanelDirty = true;     // the Scenes panel reads the project's scene files again
        // Scene files whose thumbnails the Scenes panel asked for: one is loaded and drawn a frame.
        std::deque<std::filesystem::path> m_ThumbnailQueue;
        void ProcessThumbnailQueue();
        bool m_SceneDirty = false;

        EditorCamera m_EditorCamera;
        SceneState m_SceneState = SceneState::Edit;
        GizmoMode m_GizmoMode = GizmoMode::Translate;
        bool m_GizmoLocal = false;
        bool m_GizmoPivotCenter = false;  // multi-selection gizmo at the selection's center (else the primary entity)
        GroupTransformEdit m_GizmoEdit;   // the selection being dragged with the gizmo
        glm::mat4 m_GizmoMatrix{ 1.0f };  // gizmo result of the previous frame while dragging
        GizmoMode m_GizmoEditMode = GizmoMode::None;
        // Alt pressed over the gizmo: the drag moves copies of the selection (the camera does
        // not orbit while the button is held); the copies are made once, when the gizmo first
        // really moves (a plain Alt+click on the gizmo copies nothing).
        bool m_GizmoHovered = false;      // the mouse is over the transform gizmo (last UI pass)
        bool m_GizmoAltDrag = false;
        bool m_GizmoAltDuplicatePending = false;
        Entity m_HoveredEntity;
        glm::ivec2 m_HoveredPixel{ -1, -1 }; // the mouse in the viewport (pixels, bottom-left origin; -1 = outside)
        // A click that landed on a locked entity: picked again next frame from an ID pass
        // without the locked entities, so the click goes through to what is behind.
        struct PendingPick
        {
            glm::ivec2 Pixel{ -1, -1 };
            bool Ctrl = false, Shift = false;
            Scene* SceneAtClick = nullptr;
        };
        std::optional<PendingPick> m_PendingClickThrough;
        // Files dropped onto the window this frame (handled in OnImGuiRender) and where.
        std::vector<std::string> m_PendingFileDrop;
        glm::vec2 m_PendingFileDropPos{ 0.0f };
        // A material dragged over the viewport: set by the UI each frame it is there, drawn on the
        // target mesh by the next render (nothing in the scene changes until it is dropped).
        struct MaterialDrag
        {
            std::string Path;               // asset-relative
            Ref<MaterialAsset> Material;
            UUID Target = 0;                // the mesh under the mouse (0 = none)
            int Submesh = -1;               // the part shown (-1 = all)
            bool Active = false;
        } m_MaterialDrag;
        // A tile tool stroke in progress (mouse held in the viewport): brush / eraser paint along the
        // way, the rectangle is filled when the button comes up.
        // Collider point editing: the point being dragged (-1 none), where the mouse grabbed it,
        // and the hovered point / edge (automation can pin them: ZAMIN_EDITOR_COLLIDER_HOVER).
        int m_ColliderDrag = -1;
        glm::vec2 m_ColliderDragOffset{ 0.0f };
        int m_ColliderHoverVertex = -1, m_ColliderHoverEdge = -1;
        std::string m_ColliderHoverOverride;
        // Joint anchor editing: the handle dragged / hovered (0 anchor, 1 connected anchor, 2 axis,
        // 3 lower limit, 4 upper limit; -1 none), where the mouse grabbed it (automation pins the
        // hovered one: ZAMIN_EDITOR_JOINT_HOVER).
        int m_JointDrag = -1, m_JointHover = -1;
        glm::vec2 m_JointDragOffset{ 0.0f };
        std::string m_JointHoverOverride;
        // The UI rect gizmo: the handle hovered / dragged (-1: none; see EditorLayerUIRect.cpp), the
        // element and where the drag began; a press inside it that does not move it is a click
        // (it selects what is under the mouse when the button comes up); the magnet's guide lines.
        struct UIRectGizmoState
        {
            int Hover = -1, Drag = -1;
            uint64_t Entity = 0;
            UIRect StartRect;
            glm::vec2 StartMouse{ 0.0f };
            glm::vec2 StartScreen{ 0.0f };
            glm::vec2 StartAnchorMin{ 0.0f }, StartAnchorMax{ 0.0f };
            UITransformComponent StartTransform;
            bool Moved = false, ClickPending = false, Ctrl = false, Shift = false;
            bool GuideX = false, GuideY = false;
            float GuideXValue = 0.0f, GuideYValue = 0.0f;
            std::string HoverOverride; // automation: ZAMIN_EDITOR_UI_HOVER=<handle>
        };
        UIRectGizmoState m_UIRect;
        bool m_PickUIInViewport = true;   // clicks in the viewport select UI elements by their rectangles
        Entity m_HoveredUIElement;        // the UI element under the mouse (rect picking)
        // 3D joint anchor editing: the same, plus 5 the cone's swing angle; the picked point (anchor /
        // connected anchor with a translate gizmo) and the joint it belongs to; the drag's plane point.
        int m_Joint3DDrag = -1, m_Joint3DHover = -1, m_Joint3DPicked = -1, m_Joint3DType = -1;
        uint64_t m_Joint3DEntity = 0;
        glm::vec3 m_Joint3DDragOffset{ 0.0f }, m_Joint3DDragPlane{ 0.0f };
        struct TileStroke
        {
            bool Active = false;
            UUID Entity = 0;
            int Layer = 0;
            SceneHierarchyPanel::TileTool Tool = SceneHierarchyPanel::TileTool::Brush;
            glm::ivec2 Start{ 0 }, Last{ 0 };
            bool Erase = false;
            int Changed = 0;
            glm::ivec2 Origin{ 0 }; // where the brush pattern repeats from (its top-left at the first cell)
        } m_TileStroke;
        bool m_ShowTilePalette = false;     // Oyna > Plitka palitrasi
        bool m_TileHoverOverride = false;   // automation: the preview cell below instead of the mouse
        bool m_TileDragHeld = false;        // automation: a rectangle stroke stays held (its preview shows)
        // The tiles of the painted layer as the tool would leave them (drawn by the scene view in
        // place of the layer's own), and the cells it would empty (tinted red by the overlay).
        struct TilePreview
        {
            bool Valid = false;
            UUID Entity = 0;
            int Layer = 0;
            std::vector<int> Tiles;
            std::vector<glm::ivec2> Erased;
        } m_TilePreview;
        glm::ivec2 m_TileHoverCell{ 0 };
        // Shift + drop on a model with several parts: which part gets the material.
        struct MaterialSlotPick
        {
            std::string Path;
            UUID Target = 0;
            bool Open = false;
            glm::vec2 At{ -1.0f };          // where the menu opens (screen; < 0 = at the mouse)
        } m_MaterialSlotPick;
        // Shared materials as they were when play started: edits scripts make to them
        // (GetSharedMaterial) are taken back on stop, like everything else changed while playing.
        MaterialLibrary::Snapshot m_PlayMaterials;

        bool m_ViewportFocused = false, m_ViewportHovered = false;
        bool m_GameCursorReleased = false;   // Esc while the game held the cursor
        int m_GameCursorImGuiFlags = 0;      // ImGui config flags UpdateGameCursor set (it clears only those)
        // The size the viewport's picture is drawn at, and where it is on screen (they differ when a
        // fixed game resolution is scaled down to fit); the panel's whole area (toolbar, bars).
        glm::vec2 m_ViewportSize = { 0.0f, 0.0f };
        glm::vec2 m_ViewportBounds[2] = {};
        glm::vec2 m_ViewportPanelBounds[2] = {};
        GameViewLayout m_GameViewLayout;
        bool m_OpenGameViewMenu = false;  // automation (ZAMIN_EDITOR_GAME_VIEW=...,menu): the resolution popup opens

        // view options (snapping, grid, camera bookmarks are saved per user in editor-view.json)
        EditorViewSettings m_ViewSettings;
        bool m_ViewSettingsDirty = false;
        // Automation hooks change some view settings for their session only (ZAMIN_EDITOR_LABELS,
        // _GIZMOS, _OVERLAY ...): the settings before and right after them. A save writes the file's
        // value back for each such section that was not changed again since.
        struct ViewSettingsOverride
        {
            EditorViewSettings Before, Applied;
        };
        std::optional<ViewSettingsOverride> m_ViewSettingsOverride;
        // preferences.json (Tahrirlash > Sozlamalar)
        EditorPreferences m_Preferences;
        bool m_PreferencesDirty = false;
        bool m_ShowPreferences = false;
        int m_PreferencesPage = 0;
        // What the ImGui style was last made from (UpdateEditorTheme applies it again when it differs).
        struct AppliedTheme
        {
            glm::vec3 Accent{ -1.0f };
            float FontSize = 0.0f;
            bool Tinted = false;
            glm::vec3 Tint{ 0.0f };
        } m_AppliedTheme;
        std::string m_ViewportMessage;
        double m_ViewportMessageTime = -100.0;
        SceneLoadReport m_LoadReport;            // the last scene load that found something
        bool m_LoadReportBanner = false, m_ShowLoadReport = false;
        uint64_t m_RuntimeReportSerial = 0;
        bool m_ShowColliders = true;
        bool m_ShowTransparentOrder = false;     // numbers over transparent surfaces: their drawing order
        std::vector<Renderer3D::TransparentDrawRecord> m_TransparentOrder;
        glm::mat4 m_TransparentOrderViewProjection{ 1.0f };
        bool m_ShowIcons = true;
        // Scene view modes (toolbar): a debug view of the 3D surfaces and wireframe. Edit and
        // simulate only: the game camera while playing always shows the real picture.
        RenderDebugView m_DebugView = RenderDebugView::Lit;
        WireframeMode m_Wireframe = WireframeMode::Off;
        WireframeMode m_WireframeStyle = WireframeMode::Wire; // what the toolbar's toggle switches on
        bool m_OpenViewModeMenu = false;  // automation (ZAMIN_EDITOR_VIEW_MENU): the menu opens
        bool m_OpenDebugDrawMenu = false; // automation (ZAMIN_EDITOR_DEBUG_DRAW=...,menu): the debug draw popup opens
        // The viewport's debug draw settings last given to DebugDraw (none yet: the first frame does).
        std::optional<DebugDrawSettings> m_AppliedDebugDraw;
        bool m_OpenGizmosMenu = false;    // automation (ZAMIN_EDITOR_GIZMOS=...,menu): the gizmos popup opens
        bool m_OpenTransformGizmoMenu = false; // automation (ZAMIN_EDITOR_GIZMO_SIZE=...,menu): the gizmo size popup opens
        bool m_OpenStatsOverlayMenu = false;   // automation (ZAMIN_EDITOR_OVERLAY=...,menu): the overlay's popup opens
        // What the scene drew this frame (the overlay shows the editor's own drawing on top apart).
        RenderStatistics m_SceneStats;
        // The vertex normals drawn last frame (Gizmos.Normals) and whether the limit cut them.
        size_t m_NormalLinesDrawn = 0;
        bool m_NormalLinesLimited = false;
        // The Stats panel's profiler: averaged or the last frame's times, and a frozen frame.
        bool m_ProfilerAverage = true;
        bool m_ProfilerFrozen = false;
        int m_SceneLoadTimingPick = -1; // the load shown in detail (index in the history; -1: the newest)
        std::vector<Profiler::Entry> m_ProfilerFrozenFrame;
        bool m_OutlineCaptured = false;   // the outlined meshes' draws were kept for their silhouettes
        Entity m_OutlineHover;            // the entity the hover outline marked (last frame)
        Entity m_OutlineHoverCaptured;    // the hover entity whose draws this frame's scene kept
        std::string m_ForcedHover;        // automation (ZAMIN_EDITOR_HOVER=<name>): the hover outline's entity
        // Joint names of the drawn skeletons (Gizmos.JointNames), in viewport pixels, for the UI pass.
        struct JointLabel
        {
            glm::vec2 Position{ 0.0f };
            std::string Name;
            bool Selected = false;
        };
        std::vector<JointLabel> m_JointLabels;
        PostProcessHistory m_PostHistory; // the viewport's auto exposure (adapted brightness)
        // Play with a pixel-perfect camera that upscales: how the picture was blown up (mouse picking).
        PixelPerfectView m_PlayPixelZoom;
        bool m_2DMode = false;
        bool m_MaximizeOnPlay = false;

        // asset files changed on disk (hot reload)
        AssetHotReload m_AssetHotReload;
        uint64_t m_SavedRegistryRevision = 0;
        double m_RegistrySaveTime = -100.0;

        // panels
        SceneHierarchyPanel m_SceneHierarchyPanel;
        ScenesPanel m_ScenesPanel;
        ContentBrowserPanel m_ContentBrowserPanel;
        AssetInspectorPanel m_AssetInspectorPanel; // the Inspector for a file picked in the content browser
        AssetUsageCache m_AssetUsage;              // who uses which asset (inspector, unused assets window)
        UnusedAssetsPanel m_UnusedAssetsPanel;
        bool m_ShowUnusedAssets = false, m_UnusedAssetsShown = false;
        ConsolePanel m_ConsolePanel;
        ProjectSettingsPanel m_ProjectSettingsPanel;
        // Writes the .zproject now (the settings window then knows the file has its state).
        void SaveProjectSettingsNow()
        {
            m_ProjectSettingsPanel.MarkDirty();
            m_ProjectSettingsPanel.SaveIfDirty();
        }
        AudioMixerPanel m_AudioMixerPanel;
        bool m_ShowAudioMixer = false;
        LocalizationPanel m_LocalizationPanel;
        bool m_ShowLocalization = false;
        bool m_ShowHierarchy = true, m_ShowInspector = true, m_ShowContentBrowser = true, m_ShowConsole = true;
        bool m_ShowSceneSettings = true, m_ShowStats = false, m_ShowImGuiDemo = false, m_ShowProfiler = false;
        bool m_ShowScenes = true;           // the "Sahnalar" window (the project's scenes, the game's loaded ones)
        // the scene statistics window
        bool m_ShowSceneStatistics = false;
        SceneStatisticsCollector m_SceneStatsCollector;
        SceneStatistics m_SceneStatistics;
        bool m_SceneStatsDirty = true;
        double m_SceneStatsTime = -100.0;
        const Scene* m_SceneStatsScene = nullptr; // the scene the numbers are of
        int m_SceneStatsTab = -1;           // automation: the tab brought to the front (0..3)
        int m_SceneStatsAssetType = -1;     // the assets tab's type filter (AssetType; -1: every type)
        std::string m_SceneStatsSearch;
        std::string m_SceneStatsPicked;     // the row last clicked ("c:<key>", "s:<class>", "p:<prefab>", "a:<path>")
        // Each tab's sort (components, scripts, prefabs, assets): the column and ascending.
        std::array<std::pair<SceneStatsColumn, bool>, 4> m_SceneStatsSort{ { { SceneStatsColumn::Count, false }, { SceneStatsColumn::Count, false },
                                                                             { SceneStatsColumn::Count, false }, { SceneStatsColumn::Memory, false } } };
        // Automation (ZAMIN_EDITOR_SCENE_SETTINGS): bring the scene settings tab to the front, and
        // scroll it to a section ("post": post-processing).
        bool m_FocusSceneSettings = false;
        std::string m_SceneSettingsScrollTo;
        bool m_ShowProjectSettings = false;
        SpriteEditorPanel m_SpriteEditorPanel;
        AnimationTimelinePanel m_AnimationPanel;
        bool m_ShowAnimation = false;
        AnimatorGraphPanel m_AnimatorGraphPanel;
        bool m_ShowAnimatorGraph = false;
        bool m_AnimatorGraphInViewport = false;   // automation: docked over the viewport (screenshots)
        bool m_ShowSpriteEditor = false;
        bool m_PreviewParallax = false;          // the scene view moves parallax layers with the editor camera
        bool m_ShowLauncher = false;
        bool m_ShowAbout = false;
        bool m_LayoutInitialized = false;

        // window layouts
        EditorLayoutLibrary m_Layouts;
        std::optional<BuiltInLayout> m_PendingBuiltInLayout; // built by the next frame's dock space
        std::string m_PendingLayout;          // a saved layout loaded before the next frame
        bool m_ShowLayouts = false;           // the Joylashuvlar window
        bool m_OpenSaveLayout = false;        // the save prompt opens
        std::string m_LayoutName;             // the save prompt's name field
        std::string m_LayoutRenameFrom, m_LayoutRenameTo;
        bool m_LayoutRenameFocus = false;
        std::string m_AutomationLayoutSave;   // ZAMIN_EDITOR_LAYOUT=...,save=<name>: saved at frame 20
        std::string m_LayoutError;

        // the extra scene views (the settings - open, camera - are in m_ViewSettings.SceneViews)
        struct ExtraSceneView
        {
            EditorCamera Camera;
            Ref<Framebuffer> SceneBuffer, FinalBuffer;
            PostProcessHistory PostHistory;
            glm::vec2 Size{ 0.0f };                 // the picture's size (pixels)
            glm::vec2 Bounds[2] = {};               // where it is on screen
            bool Visible = false;                   // its window was drawn last frame
            bool Hovered = false, Focused = false;
            bool GizmoHovered = false, NavigatorHovered = false;
            Entity HoveredEntity;
            bool CameraLoaded = false;              // the camera took the saved state
        };
        std::array<ExtraSceneView, SceneViewSettings::Count> m_ExtraViews;
        int m_LastSceneView = 0;                  // the view last hovered / clicked (shortcuts' fallback)
        int m_GizmoOwner = -1;                    // the view whose transform gizmo is being dragged
        bool m_NavigatorHovered = false;          // the main viewport's axis navigator (last frame)
        float m_ToolbarBottom = 0.0f;             // where the viewport toolbar's rows end (px below the panel's top)
        int m_OpenCameraSettings = -1;            // automation: that view's camera popup opens
        bool m_OpenLabelsMenu = false;            // automation: the labels popup opens
        uint32_t m_PendingViewDock = 0;           // extra views (bits) to dock next to the viewport
        EntityLabelMode m_LabelToggleMode = EntityLabelMode::All; // what the toolbar's names button turns on

        // launcher state
        RecentFileList m_RecentProjects{ 10 };
        RecentFileList m_RecentScenes{ 30 };     // scenes with their project file (Owner)
        std::string m_NewProjectName = "MeningOyinim";
        std::string m_NewProjectLocation;
        std::string m_OpenProjectPath;

        // script build
        std::thread m_BuildThread;
        std::atomic<bool> m_Building = false;
        std::atomic<bool> m_BuildFinished = false;
        std::atomic<bool> m_BuildSucceeded = false;
        bool m_ReloadAfterBuild = true;
        bool m_AutoReload = true;
        float m_HotReloadTimer = 0.0f;
        float m_BuildStartTime = 0.0f;
        // the build's messages: written by the build thread under the mutex, read by the window
        enum class BuildResult { None, Running, Succeeded, Failed };
        std::mutex m_BuildOutputMutex;
        CompilerOutputParser m_BuildParser;
        std::vector<std::string> m_BuildLog;          // every line of the output
        uint64_t m_BuildOutputRevision = 0;           // grows with every line
        uint64_t m_BuildShownRevision = ~0ull;        // the revision the window's copy is of
        std::vector<CompilerDiagnostic> m_BuildDiagnostics; // the window's copy (main thread)
        size_t m_BuildErrors = 0, m_BuildWarnings = 0;
        std::filesystem::path m_BuildDirectory;
        BuildResult m_BuildResult = BuildResult::None;
        double m_BuildSeconds = 0.0;
        bool m_ShowBuildOutput = false, m_FocusBuildOutput = false;
        bool m_BuildShowErrors = true, m_BuildShowWarnings = true, m_BuildShowNotes = true;
        int m_BuildOutputTab = 0;                     // 0: problems, 1: the whole output
        int m_BuildSelected = -1;
        std::string m_BuildSearch;
        // idle throttling: this frame's limit and why
        FrameLimit m_FrameLimit;
        IdleThrottleState m_IdleState;
        double m_WakeTime = 0.0;
        // script classes' source files (the inspector's / statistics' "open the code")
        std::unordered_map<std::string, SourceLocation> m_ScriptSources;
        double m_ScriptSourcesTime = -100.0;
        std::string m_ExternalEditorTest;      // the preferences' test result ("" none)
        bool m_OpenNewScriptPopup = false;
        std::string m_NewScriptName = "MyScript";
        ScriptTemplateKind m_NewScriptTemplate = ScriptTemplateKind::Full;

        // undo
        UndoHistory m_UndoHistory{ 100, 300 };
        bool m_PendingUndo = false;           // the scene changed: a step when nothing is held
        std::string m_PendingUndoName;        // its name, when the change gave one
        bool m_PendingSelectionUndo = false;  // the selection changed: a selection step when idle
        uint64_t m_UndoSelectionRevision = 0; // the selection's revision the history has seen
        bool m_UndoSelectionSync = true;      // back in edit mode: the current state takes the selection
        bool m_ShowUndoHistory = false;
        bool m_UndoHistoryEditsOnly = false;  // the window hides the selection steps
        bool m_UndoHistoryScroll = false;     // scroll the window to the current state
        std::string m_Clipboard;

        // keyboard shortcuts
        ShortcutRegistry m_Shortcuts;
        bool m_ShowShortcuts = false;
        std::string m_OpenMenu;               // automation (ZAMIN_EDITOR_MENU): a menu kept open ("Fayl/recent")
        std::string m_ShortcutSearch;
        std::string m_ShortcutCapture;        // the shortcut whose new keys are awaited ("" none)
        bool m_ShortcutSearchByKey = false;   // the next keys pressed become the key filter
        KeyChord m_ShortcutKeyFilter;         // only the shortcuts on these keys (empty: all)
        bool m_ShortcutsModifiedOnly = false, m_ShortcutsConflictsOnly = false;
        bool m_ShortcutsAutomation = false;   // bindings from ZAMIN_EDITOR_SHORTCUT_BIND (not saved)

        // autosave
        double m_LastAutosaveTime = 0.0;      // the last copy, or when the scene was opened / saved
        std::shared_ptr<const std::string> m_AutosavedScene; // the snapshot the last copy holds
        std::unordered_set<std::string> m_SessionAutosaves;  // copies this session wrote
        std::filesystem::path m_LockedProject;               // the project whose lock file this session holds
        bool m_PreviousSessionCrashed = false;
        struct AutosaveRecovery
        {
            bool Pending = false;             // the prompt opens
            std::filesystem::path Autosave, Scene; // the copy, the scene's file (empty: never saved)
            std::string SceneName;
            std::filesystem::file_time_type AutosaveTime{}, SceneTime{};
            bool SceneExists = false, Crashed = false;
            bool Kept = false;                // a copy put aside earlier ("Keyinroq"), not the usual one
            bool CloseRequested = false;      // the prompt closes (answered from outside it)
        } m_Recovery;
        std::string m_AutosaveNotice;
        double m_AutosaveNoticeTime = -100.0;

        // game export
        bool m_OpenBuildGamePopup = false;
        // The build dialog's view of the assets the export takes (made when it opens / its option changes).
        ExportAssetPlan m_BuildAssetPreview;
        bool m_BuildAssetPreviewValid = false;
        std::string m_BuildGameOutput;
        bool m_BuildGameBinaryScenes = true; // export: every scene also as a binary cache (.zsceneb, smaller)
        std::atomic<bool> m_Exporting = false;
        std::thread m_ExportThread;
    };

}
