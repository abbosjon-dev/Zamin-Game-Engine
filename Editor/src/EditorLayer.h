#pragma once

#include <Zamin/Zamin.h>

#include "Panels/SceneHierarchyPanel.h"
#include "Panels/ContentBrowserPanel.h"
#include "Panels/ConsolePanel.h"
#include "Panels/ProjectSettingsPanel.h"

#include <atomic>
#include <filesystem>
#include <thread>

namespace Zamin {

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
        void SetActiveScene(const Ref<Scene>& scene);
        void MarkDirty() { if (m_SceneState == SceneState::Edit) { m_SceneDirty = true; m_PendingUndo = true; } }

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
        void LoadRuntimeScene(const std::filesystem::path& path);

        void OnScenePlay();
        void OnSceneSimulate();
        void OnSceneStop();
        void OnScenePause();

        void DuplicateSelectedEntity();
        void DeleteSelectedEntity();
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
        void UI_ViewMenu();
        void UI_ViewportMessage();
        void UI_MaximizedGame();
        void UI_ViewportIcons();
        void UI_Gizmos();
        void UI_SceneSettings();
        void UI_Stats();
        void UI_StatusBar();
        void UI_Launcher();
        void UI_Modals();
        void UI_BuildDefaultLayout(unsigned int dockspaceID);

        void OnOverlayRender();
        void UpdateTilePainting();
        bool ViewportMouseToPlane(float planeZ, glm::vec3& out);
        glm::mat4 OverlayViewProjection();
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
        // not orbit while the button is held); the copies are made once, when the drag starts.
        bool m_GizmoAltDrag = false;
        bool m_GizmoAltDuplicatePending = false;
        Entity m_HoveredEntity;

        bool m_ViewportFocused = false, m_ViewportHovered = false;
        glm::vec2 m_ViewportSize = { 0.0f, 0.0f };
        glm::vec2 m_ViewportBounds[2] = {};

        // view options (snapping, grid, camera bookmarks are saved per user in editor-view.json)
        EditorViewSettings m_ViewSettings;
        bool m_ViewSettingsDirty = false;
        std::string m_ViewportMessage;
        double m_ViewportMessageTime = -100.0;
        bool m_ShowColliders = true;
        bool m_ShowIcons = true;
        bool m_2DMode = false;
        bool m_MaximizeOnPlay = false;

        // panels
        SceneHierarchyPanel m_SceneHierarchyPanel;
        ContentBrowserPanel m_ContentBrowserPanel;
        ConsolePanel m_ConsolePanel;
        ProjectSettingsPanel m_ProjectSettingsPanel;
        bool m_ShowHierarchy = true, m_ShowInspector = true, m_ShowContentBrowser = true, m_ShowConsole = true;
        bool m_ShowSceneSettings = true, m_ShowStats = false, m_ShowImGuiDemo = false;
        bool m_ShowProjectSettings = false;
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
