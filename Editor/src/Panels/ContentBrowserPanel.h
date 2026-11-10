#pragma once

#include <Zamin/Zamin.h>

#include "../ThumbnailCache.h"

#include <imgui.h>

#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Zamin {

    class AssetUsageCache;

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
        // Re-reads the folder (files changed outside the panel); thumbnails come back from the disk cache.
        void Refresh() { m_ListDirty = true; m_Thumbnails.Clear(); }
        // The listing is read again next frame (thumbnails stay).
        void MarkListDirty() { m_ListDirty = true; }
        // A file changed on disk: its thumbnail is made again (a broken image may be fixed now).
        void InvalidateThumbnail(const std::filesystem::path& path) { m_Thumbnails.Invalidate(path); }
        ThumbnailCache& GetThumbnails() { return m_Thumbnails; }
        const std::vector<std::filesystem::path>& GetSelection() const { return m_Selection; }
        // What the drag from this panel carries (its multi-selection; empty when nothing is dragged
        // from it). The payload names one of them.
        const std::vector<std::filesystem::path>& GetDraggedPaths() const { return m_DragPaths; }
        // Asks to delete these files / folders (the confirmation lists what still uses them).
        void RequestDelete(std::vector<std::filesystem::path> paths) { m_PendingDelete = std::move(paths); }
        // The "Yaratish" prompt (0: a folder, 1: a scene) / the rename prompt for a file, as their menu
        // items open them; CancelPrompts: every prompt closed unanswered (the editor's smoke test).
        void RequestNewItem(int kind)
        {
            m_NewKind = kind == 1 ? 1 : 0;
            m_NewName = m_NewKind == 1 ? "NewScene" : "NewFolder";
            m_OpenNewPopup = true;
        }
        void RequestRename(const std::filesystem::path& path)
        {
            m_RenameTarget = path;
            m_RenameBuffer = path.filename().string();
            m_OpenRenamePopup = true;
        }
        void CancelPrompts()
        {
            m_PendingDelete.clear();
            m_OpenNewPopup = m_OpenRenamePopup = false;
        }
        // "Yangi material": a material asset with default values under a free name ("NewMaterial",
        // "NewMaterial_1"...) in the open folder - Assets/Materials when the open folder is outside
        // the asset folder - selected and shown in the inspector. Returns the file (empty on failure).
        // With a preset (the built-in library: metal, wood, glass...): a copy of it ("Wood.zmat",
        // its generated texture in Textures/ next to it).
        std::filesystem::path CreateMaterial(const MaterialPreset* preset = nullptr);
        // "Yangi animatsiya klipi": an empty property clip (.zanim, 1 s, looping) under a free name.
        std::filesystem::path CreateAnimationClip();
        // A .zsanim in the current folder (from the selected texture's frames when one is selected).
        std::filesystem::path CreateSpriteClip();
        // The "Yangi material" menu items: an empty material and the standard library. Returns true
        // when one was chosen.
        bool DrawNewMaterialMenu();
        // "Yangi fizik material": a .zphysmat (2D collider surface) in the open folder - Assets/Physics
        // when it is outside the asset folder - with the values of a preset (0 = the defaults: ice,
        // rubber...). Selected and shown in the inspector. Returns the file (empty on failure).
        std::filesystem::path CreatePhysicsMaterial(int preset = 0);
        bool DrawNewPhysicsMaterialMenu();
        // The search text (AssetSearch syntax); the listing follows next frame.
        void SetSearch(std::string text) { m_Search = std::move(text); m_ListDirty = true; }
        const std::string& GetSearch() const { return m_Search; }

        // Icon (and color) the browser shows for a type of file.
        static const char* GetTypeIcon(AssetType type, ImU32* color = nullptr);
        // The assets about to be deleted that other files use, with those files (a scrolling list).
        // Returns true when a file was clicked (`clicked` is set to it).
        static bool DrawAssetsInUse(const std::vector<AssetInUse>& uses, std::filesystem::path* clicked);

        // Where "who uses this" comes from (deleting warns about assets in use). Owned by the editor.
        void SetUsageCache(AssetUsageCache* cache) { m_Usage = cache; }

        std::function<void(const std::filesystem::path&)> OnOpenScene;
        // A .zanim clip was double-clicked: the Animation window opens it.
        std::function<void(const std::filesystem::path&)> OnOpenAnimation;
        std::function<void(const std::filesystem::path&)> OnOpenVisualGraph;
        // A .zprefab was double-clicked: it opens in prefab mode.
        std::function<void(const std::filesystem::path&)> OnOpenPrefab;
        // "Variant yaratish" on a prefab: a new prefab made of it.
        std::function<void(const std::filesystem::path&)> OnCreatePrefabVariant;
        // "Nusxalash" on a prefab (or Ctrl+C on one): the clipboard gets it, pasted as a linked instance.
        std::function<void(const std::filesystem::path&)> OnCopyPrefab;
        // Whether the panel had the keyboard focus last frame (Ctrl+C copies its prefab then).
        bool IsFocused() const { return m_Focused; }
        // The prefab Ctrl+C copies (the selected one; "" when no prefab is selected).
        std::filesystem::path GetSelectedPrefab() const;
        // Whether a prefab file is a variant (its tile's badge).
        std::function<bool(const std::filesystem::path&)> IsPrefabVariant;
        // A C++ source / header (double click, "Tashqi muharrirda ochish"): opened in the external editor.
        std::function<void(const std::filesystem::path&)> OnOpenSourceFile;
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
        // The item menu changed the project's settings (build list, start scene, the game's icon).
        std::function<void()> OnProjectSettingsModified;
        // Files were deleted or created here (what refers to what may have changed).
        std::function<void()> OnFilesChanged;
        // "Ishlatilmagan assetlarni topish" in the panel's menu.
        std::function<void()> OnFindUnusedAssets;

    private:
        Ref<Texture2D> GetThumbnail(const AssetEntry& entry);
        // The thumbnail fitted into a box (the aspect kept), drawn into the window's draw list.
        static void DrawThumbnail(const Ref<Texture2D>& texture, ImVec2 min, ImVec2 max, float rounding);
        void NavigateTo(const std::filesystem::path& directory);
        void RefreshEntries();
        bool IsRecursiveListing() const;
        void MarkSettingsModified() { if (OnSettingsModified) OnSettingsModified(); }

        // Import errors (AssetImportLog) of the listed files, looked up when the log or the listing changed.
        void RefreshEntryErrors();
        const std::string* FindEntryError(const std::filesystem::path& path) const;
        // A path of the log (canonical) as the browser spells paths (under m_Root).
        std::filesystem::path FromLogPath(const std::filesystem::path& file) const;
        void DrawImportErrors();

        void DrawToolbar();
        void DrawFilterPopup();
        void DrawFilterBar();
        // What is being searched for and where, with the number of results.
        void DrawSearchBar();
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
        ThumbnailCache m_Thumbnails;
        ContentBrowserSettings m_LocalSettings;
        ContentBrowserSettings* m_Settings = &m_LocalSettings;
        std::string m_Search;

        // listing (re-read when something changes, at most once a second otherwise)
        std::vector<AssetEntry> m_Entries;
        std::filesystem::path m_ListDirectory;   // where the listing starts (the project folder for a project-wide search)
        bool m_SearchActive = false;             // m_Search has something to look for
        std::string m_ListKey;
        double m_ListTime = -100.0;
        bool m_ListDirty = true;

        // import errors of listed files (folders: something inside), by entry path
        std::unordered_map<std::string, std::string> m_EntryErrors;
        uint64_t m_EntryErrorsRevision = ~0ull;
        bool m_EntryErrorsDirty = true;

        // selection
        std::vector<std::filesystem::path> m_Selection;
        std::filesystem::path m_Anchor;          // where a Shift+click range starts
        std::filesystem::path m_ClickedSelected; // pressed on a selected item: decided on release
        bool m_ScrollToSelection = false;

        // drag & drop moves
        std::vector<std::filesystem::path> m_DragPaths; // what the current drag carries
        bool m_Focused = false;
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
        std::vector<AssetInUse> m_DeleteUses;      // what the pending delete would break
        AssetUsageCache* m_Usage = nullptr;
        std::filesystem::path m_RenameTarget;
        std::string m_RenameBuffer;
        bool m_OpenRenamePopup = false;
    };

}
