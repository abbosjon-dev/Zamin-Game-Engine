#pragma once

#include <Zamin/Zamin.h>

#include "../SceneThumbnails.h"

#include <algorithm>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace Zamin {

    // "Sahnalar" window: every scene of the project as a card - its thumbnail, name, build index,
    // whether it is the start scene, its format version and metadata (tooltip) - to open one, put it
    // into (or out of) the build list, make it the start scene. While the game runs (play mode) the
    // scenes it has loaded are listed on top - the active one starred, DontDestroyOnLoad, loads in
    // progress - to make one active, unload it, show it in the hierarchy, or load another project
    // scene into the running game (single or additively).
    class ScenesPanel
    {
    public:
        void OnImGuiRender(bool* open);

        void SetThumbnails(SceneThumbnails* thumbnails) { m_Thumbnails = thumbnails; }
        // Play mode: the game's scenes (null in edit mode / simulation) and the scene the hierarchy shows.
        void SetSceneManager(SceneManager* manager) { m_SceneManager = manager; }
        void SetShownScene(const Scene* scene) { m_ShownScene = scene; }
        // The scene open in the editor (absolute path, "" when unsaved).
        void SetOpenScenePath(const std::filesystem::path& path) { m_OpenScenePath = path; }
        // The project's scene files are read again (a scene was saved, files changed); without a
        // call they are checked every few seconds.
        void MarkDirty() { m_Dirty = true; }
        void OnProjectChanged() { m_Items.clear(); m_Selected.clear(); m_Dirty = true; }
        void Focus() { m_FocusRequested = true; }
        // Automation: select a scene card (asset-relative path).
        void Select(const std::string& relativePath) { m_Selected = relativePath; }
        void SetListView(bool list) { m_ListView = list; }
        // Scenes loaded additively with Play next to the edit scene (asset-relative; this session only).
        const std::vector<std::string>& GetPlayAdditiveScenes() const { return m_PlayAdditive; }
        bool IsPlayAdditive(const std::string& relative) const { return std::find(m_PlayAdditive.begin(), m_PlayAdditive.end(), relative) != m_PlayAdditive.end(); }
        void SetPlayAdditive(const std::string& relative, bool on);
        // Where the window docks the first time it is opened (a tab next to the content browser).
        void SetDefaultDock(unsigned int dock) { m_DefaultDock = dock; }

        std::function<void(const std::filesystem::path&)> OnOpenScene;     // absolute path
        std::function<void(const std::filesystem::path&)> OnRevealScene;   // in the content browser
        std::function<void()> OnProjectModified;                           // build list / start scene changed
        std::function<void(const Ref<Scene>&)> OnShowRuntimeScene;          // play: show it in the hierarchy
        // Draw these scenes' thumbnails (absolute paths; the editor loads and draws one a frame).
        std::function<void(const std::vector<std::filesystem::path>&)> OnRenderThumbnails;

    private:
        struct Item
        {
            std::filesystem::path Path;   // absolute
            std::string Relative;         // relative to the assets ('/' separators)
            SceneFileInfo Info;
            uintmax_t Size = 0;
            std::filesystem::file_time_type Time{};
            bool Seen = false;
        };

        void Refresh(bool force);
        void DrawToolbar();
        void DrawRuntimeScenes();
        // Play: the game's GameState values (editable) and registered services.
        void DrawGlobalState();
        void DrawCards(const std::vector<const Item*>& items);
        void DrawList(const std::vector<const Item*>& items);
        void DrawItemMenu(const Item& item);
        void DrawItemTooltip(const Item& item);
        void DrawSelectedActions();
        const Item* FindItem(const std::string& relative) const;

        // The build list of the active project (helpers over ProjectConfig::BuildScenes).
        static int FindBuildEntry(const std::string& relative);
        void AddToBuild(const std::string& relative);
        void RemoveFromBuild(const std::string& relative);
        void SetBuildEnabled(const std::string& relative, bool enabled);
        void SetStartScene(const std::string& relative);
        static bool IsStartScene(const std::string& relative);
        void Modified();

        std::vector<Item> m_Items;
        std::string m_Selected;           // relative path of the selected card
        std::string m_Search;
        std::vector<std::string> m_PlayAdditive;
        std::string m_StateFilter;
        bool m_OnlyBuild = false;
        bool m_ListView = false;
        float m_CardWidth = 176.0f;
        bool m_Dirty = true;
        double m_LastScan = -100.0;
        bool m_FocusRequested = false;
        unsigned int m_DefaultDock = 0;
        SceneThumbnails* m_Thumbnails = nullptr;
        SceneManager* m_SceneManager = nullptr;
        const Scene* m_ShownScene = nullptr;
        std::filesystem::path m_OpenScenePath;
    };

}
