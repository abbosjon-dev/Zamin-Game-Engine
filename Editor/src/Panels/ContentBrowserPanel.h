#pragma once

#include <Zamin/Zamin.h>

#include <filesystem>
#include <functional>
#include <unordered_map>

namespace Zamin {

    class ContentBrowserPanel
    {
    public:
        ContentBrowserPanel();

        void SetRoot(const std::filesystem::path& root);
        void OnImGuiRender(bool* open);

        std::function<void(const std::filesystem::path&)> OnOpenScene;
        std::function<void(const std::string&)> OnCreateScript;  // class name
        std::function<void(const std::filesystem::path&)> OnNewScene;

    private:
        Ref<Texture2D> GetThumbnail(const std::filesystem::path& path);
        void DrawBreadcrumbs();
        void DrawContextMenu();

    private:
        std::filesystem::path m_Root;
        std::filesystem::path m_CurrentDirectory;
        std::unordered_map<std::string, Ref<Texture2D>> m_Thumbnails;
        float m_ThumbnailSize = 84.0f;
        std::string m_Search;
        std::string m_NewName;
        int m_NewKind = 0; // 0 folder, 1 scene, 2 script
        bool m_OpenNewPopup = false;
        std::filesystem::path m_PendingDelete;
    };

}
