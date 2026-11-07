#pragma once

#include <Zamin/Zamin.h>

#include <imgui.h>

#include <filesystem>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace Zamin {

    // "Project settings" window: the project's settings in categories (a list on the left with a
    // search that also highlights the matching settings, or a combo in a narrow dock): general info,
    // quality levels, physics (collision) layers with presets, audio defaults, the input map (actions,
    // axes and 2D vectors bound to keys / mouse / gamepad, key capture, duplicate warnings, a live
    // preview), tags and sorting layers (usage in the open scene and in the project's files, renaming
    // everywhere), build scenes, debug drawing and the game's SaveData. Changes are kept in memory until
    // Save (Ctrl+S); each category shows when it differs from the saved file and can be reverted.
    class ProjectSettingsPanel
    {
    public:
        // What callers ask to see (Focus): a category, Tags / SortingLayers are the two pages of
        // "Tags and layers".
        enum class Tab { Input = 0, SaveData, Tags, General, SortingLayers, PhysicsLayers, BuildScenes, Quality, Audio, Debug, Player };

        // In the order of ProjectSettingsIndex::GetCategories() (titles, search keywords, file sections).
        enum class Category { General = 0, Player, Quality, Physics, Audio, Input, TagsAndLayers, BuildScenes, Debug, SaveData, Count };

        void OnImGuiRender(bool* show);

        bool IsDirty() const { return m_Dirty; }
        // Something else (e.g. the inspector's tag field) changed the project config.
        void MarkDirty() { m_Dirty = true; }
        // Scene used for the usage counts (the editor's active scene).
        void SetScene(const Ref<Scene>& scene) { m_Scene = scene; }
        // false in Play / Simulate: the scene above is then the saved edit scene, which must
        // not change behind the editor's back (no dirty flag / undo), so tags its entities
        // use cannot be renamed until play stops; the audio defaults are not edited either (the
        // mixer is then the game's).
        void SetSceneEditable(bool editable) { m_SceneEditable = editable; }
        // Renaming a tag also renames it on the scene's entities.
        std::function<void()> OnSceneModified;
        // The scene open in the editor (relative to the assets, "" when unsaved): the build list's
        // "add the open scene" button; it is counted from memory, not from its file.
        void SetOpenScenePath(const std::filesystem::path& path) { m_OpenScenePath = path; }
        // A build scene was double-clicked: open it in the editor.
        std::function<void(const std::filesystem::path&)> OnOpenSceneRequested;
        // The Audio category's button: show the Audio Mixer window.
        std::function<void()> OnAudioMixerRequested;
        // The build scenes page's button: show the Scenes window (every scene of the project).
        std::function<void()> OnScenesPanelRequested;
        // Writes the active .zproject if there are unsaved changes.
        void SaveIfDirty();
        // Call when another project is opened / the project is closed.
        void OnProjectChanged();
        // Bring the window to the front on the next frame (optionally on a specific category).
        void Focus() { m_FocusRequested = true; }
        void Focus(Tab tab);
        void Focus(Category category) { m_FocusRequested = true; m_Category = category; }
        // Automation (screenshots): the window floats at this rect (x, y, w, h) instead of its dock.
        void SetFloatingRect(const ImVec4& rect) { m_FloatingRect = rect; m_FloatingRequested = true; }
        void SetSearch(const std::string& text) { m_Search = text; }
        // Automation: "expand" opens every entry of the input map (and its duplicate list), "presets"
        // the physics presets menu.
        void RequestOpen(const std::string& what) { m_AutomationOpen = what; }
        // Automation: the category page scrolled to y (pixels; < 0: the bottom) for the next frames.
        void RequestScroll(float y) { m_ScrollRequest = y; m_ScrollFrames = 30; }

    private:
        enum class ListKind { None = 0, Action, AxisNegative, AxisPositive };
        struct ListenTarget
        {
            ListKind Kind = ListKind::None;
            int Entry = -1;   // index of the action / axis
            int Binding = -1; // index inside its binding list
            bool operator==(const ListenTarget&) const = default;
        };

        // ---- frame
        void DrawCategoryList(float width, bool compact);
        void DrawCategoryContent(ProjectConfig& config);
        bool IsCategoryDirty(Category category) const;
        void RevertCategory(Category category, ProjectConfig& config);
        void RevertAll(ProjectConfig& config);
        // The saved file's config (to compare with and to revert to).
        void RefreshSaved();
        void UpdateDirtySections(const ProjectConfig& config);
        void ApplyRuntime(Category category, ProjectConfig& config);

        // ---- categories
        void DrawGeneral(ProjectConfig& config);
        // Company / product / version, the window, the icon (ProjectConfig::Player).
        void DrawPlayer(ProjectConfig& config);
        void DrawPlayerIcon(PlayerSettings& player);
        void DrawDebug(ProjectConfig& config);
        void DrawQuality(ProjectConfig& config);
        void DrawAudio(ProjectConfig& config);
        // The game's build scenes: order (drag / arrows), on / off, build indices, the start scene.
        void DrawBuildScenes(ProjectConfig& config);
        void DrawSaveData();
        void DrawTagsAndLayers(ProjectConfig& config);
        void DrawTags(ProjectConfig& config);
        void DrawSortingLayers(ProjectConfig& config);
        // The 16 named collision layers (2D and 3D) and the matrix of which pairs collide, presets.
        void DrawPhysicsLayers(ProjectConfig& config);
        void DrawInputMap(InputActionMap& map);
        bool DrawInputVectors(InputActionMap& map, int openRequest);
        bool DrawBindingList(const char* label, std::vector<InputBinding>& bindings, ListKind kind, int entry, const InputActionMap& map);
        bool DrawBinding(InputBinding& binding, const ListenTarget& target, const std::vector<std::string>& sharedWith);

        void StartListening(const ListenTarget& target);
        void StopListening() { m_Listen = {}; }
        void UpdateListening(InputActionMap& map);
        std::vector<InputBinding>* ResolveList(InputActionMap& map, ListKind kind, int entry);

        // Tag / sorting layer use in the project's scene and prefab files (the open scene left out:
        // it is counted from memory). Scanned when first needed and after a rename.
        void ScanProjectUsage();
        // Renames a tag in the list, on the open scene's entities and (if chosen) in the files.
        void RenameTag(ProjectConfig& config, int index, const std::string& name);

    private:
        bool m_Dirty = false;
        bool m_FocusRequested = false;
        ListenTarget m_Listen;
        bool m_ListenArmed = false; // all inputs were released since listening started

        Category m_Category = Category::General;
        int m_LayersPage = 0;          // Tags and layers: 0 tags, 1 sorting layers
        int m_RequestedLayersPage = -1;
        std::string m_Search;
        bool m_FloatingRequested = false;
        ImVec4 m_FloatingRect{ 0, 0, 0, 0 };

        // The saved .zproject: its config and sections, the sections that differ now.
        const Project* m_SavedFor = nullptr;
        ProjectConfig m_Saved;
        std::vector<std::pair<std::string, std::string>> m_SavedSections;
        std::vector<std::string> m_DirtySections;

        Ref<Scene> m_Scene;
        bool m_SceneEditable = true;
        std::string m_NewTag;
        int m_RenamingTag = -1;
        std::string m_TagRenameBuffer;
        bool m_FocusTagRename = false;
        bool m_RenameInFiles = true;
        std::string m_NewSortingLayer;
        uint32_t m_RenamingLayer = ~0u;
        std::string m_LayerRenameBuffer;
        bool m_FocusLayerRename = false;
        SceneFileUsage m_ProjectUsage;
        bool m_ProjectUsageValid = false;
        const Project* m_UsageFor = nullptr;

        std::filesystem::path m_OpenScenePath;

        // Input: the filter of the action / axis lists; open (1) / close (0) every entry next frame.
        std::string m_InputFilter;
        int m_InputOpenRequest = -1;
        std::string m_AutomationOpen;
        float m_ScrollRequest = 0.0f;
        int m_ScrollFrames = 0;
        // Quality: the level edited below the table, its name being typed.
        int m_QualityLevel = -1;
        std::string m_QualityNameBuffer;
        // Physics: the preset waiting for confirmation (it replaces the layer names).
        int m_PendingPreset = -1;
        // Player: the icon's preview (loaded when the path or the file changes) and its problem.
        Ref<Texture2D> m_IconPreview;
        std::filesystem::path m_IconPreviewPath;
        std::filesystem::file_time_type m_IconPreviewTime{};
        std::string m_IconError, m_IconWarning;
        glm::uvec2 m_IconSize{ 0 };
        std::string m_VersionBuffer;
        bool m_VersionEditing = false;

        // SaveData tab: new entry
        std::string m_NewSaveKey;
        int m_NewSaveType = 0;
    };

}
