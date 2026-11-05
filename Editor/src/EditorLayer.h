#pragma once

#include <Zamin/Zamin.h>

#include "Panels/SceneHierarchyPanel.h"
#include "Panels/AssetInspectorPanel.h"
#include "Panels/ContentBrowserPanel.h"
#include "Panels/ConsolePanel.h"
#include "Panels/ProjectSettingsPanel.h"
#include "Panels/UnusedAssetsPanel.h"
#include "Panels/SpriteEditorPanel.h"
#include "AssetUsageCache.h"

#include <atomic>
#include <filesystem>
#include <optional>
#include <thread>
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

        bool OnKeyPressed(KeyPressedEvent& e);
        bool OnMouseButtonPressed(MouseButtonPressedEvent& e);

        // ---- projects
        void NewProject(const std::filesystem::path& directory, const std::string& name);
        bool OpenProject(const std::filesystem::path& path);
        void OpenProjectDialog();
        void CloseProject();
        void AddRecentProject(const std::filesystem::path& path);
        void LoadRecentProjects();

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
        void MarkDirty() { if (m_SceneState == SceneState::Edit) { m_SceneDirty = true; m_PendingUndo = true; m_AssetUsage.MarkDirty(); } }

        // ---- undo / redo (scene snapshots)
        std::string TakeSnapshot();
        void ResetUndoHistory();
        void CommitUndo();
        void Undo();
        void Redo();
        void RestoreSnapshot(const std::string& snapshot);

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
        void LoadRuntimeScene(const std::filesystem::path& path);

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

        // ---- C++ scripts
        void BuildScripts(bool reloadAfter = true);
        void ReloadScriptModule();
        // Writes Source/<className>.cpp from a template and starts a script build.
        bool CreateScriptClass(const std::string& className, ScriptTemplateKind kind = ScriptTemplateKind::Full);
        void CheckModuleHotReload(Timestep ts);

        // ---- export
        void BuildGame(const std::filesystem::path& outputDirectory);

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
        void UI_ViewMenu();
        void UI_ViewportMessage();
        void UI_MaximizedGame();
        void UI_TransparentOrder();
        void UI_ViewportIcons();
        void UI_Gizmos();
        void UI_SceneSettings();
        void UI_Stats();
        // The profiler: the last frame's scopes as a tree (nested scopes inside), in the Stats panel
        // (`header`: under a collapsing header) and in a window of its own.
        void UI_ProfilerTree(bool header);
        void UI_ProfilerWindow();
        // The statistics overlay over the viewport (FPS graph, draw calls, triangles, culling):
        // the overlay itself, the toolbar's popup and the view menu sharing its settings' widgets.
        void UI_StatsOverlay();
        void UI_StatsOverlayPopup();
        bool UI_StatsOverlaySettings();
        void UI_StatusBar();
        // Selected entity info in the status bar (name, position, components...) up to `maxX`.
        void UI_StatusBarSelection(float maxX);
        void UI_Launcher();
        void UI_Modals();
        void UI_BuildDefaultLayout(unsigned int dockspaceID);

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
        std::filesystem::path m_EditorScenePath;
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
        struct TileStroke
        {
            bool Active = false;
            UUID Entity = 0;
            int Layer = 0;
            SceneHierarchyPanel::TileTool Tool = SceneHierarchyPanel::TileTool::Brush;
            glm::ivec2 Start{ 0 }, Last{ 0 };
            bool Erase = false;
            int Changed = 0;
        } m_TileStroke;
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
        glm::vec2 m_ViewportSize = { 0.0f, 0.0f };
        glm::vec2 m_ViewportBounds[2] = {};

        // view options (snapping, grid, camera bookmarks are saved per user in editor-view.json)
        EditorViewSettings m_ViewSettings;
        bool m_ViewSettingsDirty = false;
        std::string m_ViewportMessage;
        double m_ViewportMessageTime = -100.0;
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
        bool m_2DMode = false;
        bool m_MaximizeOnPlay = false;

        // asset files changed on disk (hot reload)
        AssetHotReload m_AssetHotReload;
        uint64_t m_SavedRegistryRevision = 0;
        double m_RegistrySaveTime = -100.0;

        // panels
        SceneHierarchyPanel m_SceneHierarchyPanel;
        ContentBrowserPanel m_ContentBrowserPanel;
        AssetInspectorPanel m_AssetInspectorPanel; // the Inspector for a file picked in the content browser
        AssetUsageCache m_AssetUsage;              // who uses which asset (inspector, unused assets window)
        UnusedAssetsPanel m_UnusedAssetsPanel;
        bool m_ShowUnusedAssets = false, m_UnusedAssetsShown = false;
        ConsolePanel m_ConsolePanel;
        ProjectSettingsPanel m_ProjectSettingsPanel;
        bool m_ShowHierarchy = true, m_ShowInspector = true, m_ShowContentBrowser = true, m_ShowConsole = true;
        bool m_ShowSceneSettings = true, m_ShowStats = false, m_ShowImGuiDemo = false, m_ShowProfiler = false;
        // Automation (ZAMIN_EDITOR_SCENE_SETTINGS): bring the scene settings tab to the front, and
        // scroll it to a section ("post": post-processing).
        bool m_FocusSceneSettings = false;
        std::string m_SceneSettingsScrollTo;
        bool m_ShowProjectSettings = false;
        SpriteEditorPanel m_SpriteEditorPanel;
        bool m_ShowSpriteEditor = false;
        bool m_PreviewParallax = false;          // the scene view moves parallax layers with the editor camera
        bool m_ShowLauncher = false;
        bool m_ShowAbout = false;
        bool m_LayoutInitialized = false;

        // launcher state
        std::vector<std::filesystem::path> m_RecentProjects;
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
        bool m_OpenNewScriptPopup = false;
        std::string m_NewScriptName = "MyScript";
        ScriptTemplateKind m_NewScriptTemplate = ScriptTemplateKind::Full;

        // undo
        std::vector<std::string> m_UndoStack, m_RedoStack;
        std::string m_LastSnapshot;
        bool m_PendingUndo = false;
        std::string m_Clipboard;

        // game export
        bool m_OpenBuildGamePopup = false;
        std::string m_BuildGameOutput;
        std::atomic<bool> m_Exporting = false;
        std::thread m_ExportThread;
    };

}
