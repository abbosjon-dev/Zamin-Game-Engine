#pragma once

#include <Zamin/Zamin.h>

#include <filesystem>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

namespace Zamin {

    class AssetUsageCache;

    // "Ishlatilmagan assetlar": assets that no scene, prefab, material, model, the project file or
    // the C++ code refers to (or, in the deep mode, that nothing the start scene and the code lead
    // to uses), with their sizes; they can be shown in the content browser or deleted (after a
    // confirmation). Also lists references that lead nowhere.
    class UnusedAssetsPanel
    {
    public:
        void SetUsageCache(AssetUsageCache* cache) { m_Usage = cache; }
        void OnImGuiRender(bool* open);
        // Scans now (the window shows the result when it opens).
        void Refresh();
        // Automation: the deep mode, and a summary line in the log after the next scan.
        void SetReachableOnly(bool reachable) { m_ReachableOnly = reachable; m_Revision = ~0ull; }
        void LogNextResult() { m_LogResult = true; }
        const std::vector<UnusedAsset>& GetUnused() const { return m_Unused; }

        // A row was clicked: the editor shows the file in the content browser and the inspector.
        std::function<void(const std::filesystem::path&)> OnAssetSelected;
        // Files were deleted (the editor refreshes the browser).
        std::function<void(const std::vector<std::filesystem::path>&)> OnAssetsDeleted;

    private:
        void Update();
        void DrawToolbar();
        void DrawList();
        void DrawMissing();
        void DrawDeletePopup();
        bool Matches(const UnusedAsset& asset) const;

    private:
        AssetUsageCache* m_Usage = nullptr;
        bool m_ReachableOnly = false;
        uint64_t m_Revision = ~0ull;  // of the usage index the list was made from
        std::vector<UnusedAsset> m_Unused;
        uintmax_t m_TotalSize = 0;
        size_t m_AssetCount = 0, m_ScannedFiles = 0;
        std::unordered_set<std::string> m_Checked;
        std::string m_Selected;
        std::string m_Search;
        AssetTypeFilter m_Filter;
        bool m_LogResult = false;
        std::vector<std::string> m_PendingDelete;
        std::vector<AssetInUse> m_DeleteUses;   // other files that still name what is being deleted
        bool m_OpenDeletePopup = false;
    };

}
