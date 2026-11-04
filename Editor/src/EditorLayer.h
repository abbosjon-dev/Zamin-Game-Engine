#pragma once

#include <Zamin/Zamin.h>

#include "Panels/SceneHierarchyPanel.h"
#include "Panels/ContentBrowserPanel.h"
#include "Panels/ConsolePanel.h"

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
        void MarkDirty() { if (m_SceneState == SceneState::Edit) m_SceneDirty = true; }

        void OnScenePlay();
        void OnSceneSimulate();
        void OnSceneStop();
        void OnScenePause();

        void DuplicateSelectedEntity();
        void DeleteSelectedEntity();
        void FocusEntity(Entity entity);

        // ---- C++ scripts
        void BuildScripts(bool reloadAfter = true);
        void ReloadScriptModule();
        void CreateScriptClass(const std::string& className);
        void CheckModuleHotReload(Timestep ts);

        // ---- export
        void BuildGame(const std::filesystem::path& outputDirectory);

        // ---- UI
        void UI_DockSpace();
        void UI_MenuBar();
        void UI_Viewport();
        void UI_ViewportToolbar();
        void UI_ViewportIcons();
        void UI_Gizmos();
        void UI_SceneSettings();
        void UI_Stats();
        void UI_StatusBar();
        void UI_Launcher();
        void UI_Modals();
        void UI_BuildDefaultLayout(unsigned int dockspaceID);

        void OnOverlayRender();
        void UpdateWindowTitle();

    private:
        std::filesystem::path m_StartupProject;

        Ref<Framebuffer> m_Framebuffer;
        Ref<Scene> m_ActiveScene;
        Ref<Scene> m_EditorScene;
        std::filesystem::path m_EditorScenePath;
        bool m_SceneDirty = false;

        EditorCamera m_EditorCamera;
        SceneState m_SceneState = SceneState::Edit;
        GizmoMode m_GizmoMode = GizmoMode::Translate;
        bool m_GizmoLocal = false;
        Entity m_HoveredEntity;

        bool m_ViewportFocused = false, m_ViewportHovered = false;
        glm::vec2 m_ViewportSize = { 0.0f, 0.0f };
        glm::vec2 m_ViewportBounds[2] = {};

        // view options
        bool m_ShowGrid = true;
        bool m_ShowColliders = true;
        bool m_ShowIcons = true;
        bool m_2DMode = false;

        // panels
        SceneHierarchyPanel m_SceneHierarchyPanel;
        ContentBrowserPanel m_ContentBrowserPanel;
        ConsolePanel m_ConsolePanel;
        bool m_ShowHierarchy = true, m_ShowInspector = true, m_ShowContentBrowser = true, m_ShowConsole = true;
        bool m_ShowSceneSettings = true, m_ShowStats = false, m_ShowImGuiDemo = false;
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

        // game export
        bool m_OpenBuildGamePopup = false;
        std::string m_BuildGameOutput;
        std::atomic<bool> m_Exporting = false;
        std::thread m_ExportThread;
    };

}
