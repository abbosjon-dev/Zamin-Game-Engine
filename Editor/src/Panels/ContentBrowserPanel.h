#pragma once

#include <Zamin/Zamin.h>

#include <imgui.h>

#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Zamin {

    // Project files as tiles or a sortable list, with search and a file type filter. Files are
    // selected with Ctrl / Shift like the hierarchy and moved by dragging them onto a folder (a
    // folder tile / row or a breadcrumb); scenes and prefabs that use them are updated.
    class ContentBrowserPanel
    {
    public:
        ContentBrowserPanel();

        void SetRoot(const std::filesystem::path& root);
        void OnImGuiRender(bool* open);

        // View preferences (grid / list, tile size, type filter, sorting), owned by the editor
        // layer and saved per user. nullptr = the panel's own defaults.
        void SetSettings(ContentBrowserSettings* settings) { m_Settings = settings ? settings : &m_LocalSettings; }

        const std::filesystem::path& GetCurrentDirectory() const { return m_CurrentDirectory; }
        // Opens a folder of the project (others are ignored).
        void SetCurrentDirectory(const std::filesystem::path& directory);
        // Whether a point (ImGui screen coordinates) was over the panel the last frame it was drawn.
        bool ContainsScreenPoint(float x, float y) const;
        // Opens the folder of the first path and selects every path that is in it.
        void Reveal(const std::vector<std::filesystem::path>& paths);
        // Short message in the panel's footer (moves, imports).
        void ShowNotice(std::string text, bool warning = false);
        // Re-reads the folder (files changed outside the panel).
        void Refresh() { m_ListDirty = true; m_Thumbnails.clear(); }
        // The listing is read again next frame (thumbnails stay).
        void MarkListDirty() { m_ListDirty = true; }
        // A file changed on disk: its thumbnail is looked up again (a broken image may be fixed now).
        void InvalidateThumbnail(const std::filesystem::path& path) { m_Thumbnails.erase(path.string()); }
        const std::vector<std::filesystem::path>& GetSelection() const { return m_Selection; }

        // Icon (and color) the browser shows for a type of file.
        static const char* GetTypeIcon(AssetType type, ImU32* color = nullptr);

        std::function<void(const std::filesystem::path&)> OnOpenScene;
        // An item was clicked: the file or folder the selection is about now (empty when the
        // click / Escape cleared it). The editor shows the file in the inspector.
        std::function<void(const std::filesystem::path&)> OnAssetSelected;
        std::function<void()> OnRequestNewScript;  // opens the editor's "new C++ script" dialog
        std::function<void(const std::filesystem::path&)> OnNewScene;
        // Files / folders were moved or renamed. The files on disk that refer to them are already
        // rewritten; the editor updates what it keeps in memory (the open scene, undo history...).
        std::function<void(const AssetFiles::MoveResult&)> OnAssetsMoved;
        // m_Settings changed (the editor saves its view settings).
        std::function<void()> OnSettingsModified;
        // Files were deleted or created here (what refers to what may have changed).
        std::function<void()> OnFilesChanged;
        // "Ishlatilmagan assetlarni topish" in the panel's menu.
        std::function<void()> OnFindUnusedAssets;

    private:
        Ref<Texture2D> GetThumbnail(const std::filesystem::path& path);
        void NavigateTo(const std::filesystem::path& directory);
        void RefreshEntries();
        bool IsRecursiveListing() const;
        void MarkSettingsModified() { if (OnSettingsModified) OnSettingsModified(); }

        void DrawToolbar();
        void DrawFilterPopup();
        void DrawFilterBar();
        void DrawGrid();
        void DrawList();
        void DrawFooter();
        void DrawContextMenu();
        void DrawItemMenu(const AssetEntry& entry);
        // Selection, drag source, folder drop target, double click and menu of the item drawn last.
        void HandleItemInteraction(const AssetEntry& entry);
        // Content browser items dropped on the item drawn last move into `folder`.
        void DrawFolderDropTarget(const std::filesystem::path& folder);
        void HandleKeyboard();
        void Open(const AssetEntry& entry);

        // ---- selection (in the order of m_Entries for ranges)
        bool IsSelected(const std::filesystem::path& path) const;
        void SelectOnly(const std::filesystem::path& path);
        void Click(const std::filesystem::path& path, bool ctrl, bool shift);
        std::vector<std::filesystem::path> GetSelectionInOrder() const;

        // ---- moving
        bool IsProtected(const std::filesystem::path& path) const;
        // Why none of `paths` can move into `folder` ("" = at least one can).
        std::string CheckMoveAll(const std::vector<std::filesystem::path>& paths, const std::filesystem::path& folder) const;
        void MoveItems(const std::vector<std::filesystem::path>& paths, const std::filesystem::path& folder);
        void ReportMove(const AssetFiles::MoveResult& result, const std::filesystem::path& folder, bool rename);

    private:
        std::filesystem::path m_Root;
        std::filesystem::path m_CurrentDirectory;
        std::unordered_map<std::string, Ref<Texture2D>> m_Thumbnails;
        ContentBrowserSettings m_LocalSettings;
        ContentBrowserSettings* m_Settings = &m_LocalSettings;
        std::string m_Search;

        // listing (re-read when something changes, at most once a second otherwise)
        std::vector<AssetEntry> m_Entries;
        std::string m_ListKey;
        double m_ListTime = -100.0;
        bool m_ListDirty = true;

        // selection
        std::vector<std::filesystem::path> m_Selection;
        std::filesystem::path m_Anchor;          // where a Shift+click range starts
        std::filesystem::path m_ClickedSelected; // pressed on a selected item: decided on release
        bool m_ScrollToSelection = false;

        // drag & drop moves
        std::vector<std::filesystem::path> m_DragPaths; // what the current drag carries
        bool m_DragStarted = false;
        std::filesystem::path m_DropCheckFolder;        // CheckMoveAll cache of the hovered folder
        std::string m_DropCheckReason;
        struct PendingMove { std::vector<std::filesystem::path> Paths; std::filesystem::path Destination; };
        PendingMove m_PendingMove;                      // applied after the panel is drawn

        // window rectangle of the last frame (files dropped onto the editor window)
        bool m_Visible = false;
        ImVec2 m_WindowMin{ 0, 0 }, m_WindowMax{ 0, 0 };

        // footer message
        std::string m_Notice;
        bool m_NoticeWarning = false;
        double m_NoticeTime = -100.0;

        // popups
        std::string m_NewName;
        int m_NewKind = 0; // 0 folder, 1 scene
        bool m_OpenNewPopup = false;
        std::vector<std::filesystem::path> m_PendingDelete;
        std::filesystem::path m_RenameTarget;
        std::string m_RenameBuffer;
        bool m_OpenRenamePopup = false;
    };

}
