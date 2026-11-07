#pragma once

#include "Zamin/Asset/AssetTypes.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace Zamin {

    class AssetReferenceIndex;
    struct ProjectConfig;

    // What the editor's export takes of the project's assets (Project settings > Build scenes > Export;
    // the .zproject's "Export" section).
    struct ExportSettings
    {
        // Leave out the assets nothing in the game leads to: neither the build scenes (and what they use,
        // through prefabs, materials, models, clips ...) nor the project file (icon, splash) nor a string
        // literal of the C++ scripts ("Audio/jump.wav") nor the folders below.
        bool ExcludeUnusedAssets = true;
        // Always exported, used or not (asset-relative files or folders; '*' matches any part of a name:
        // "Levels/*.zscene", "Data/*"): what the game opens by names it makes itself.
        // The asset folder's Resources/ and the language files' folder are always taken.
        std::vector<std::string> AlwaysInclude;

        bool operator==(const ExportSettings&) const = default;
        bool IsDefault() const { return *this == ExportSettings(); }
        // The "Export" section and back (FromJson keeps the defaults for missing / wrong values).
        std::string ToJson() const;
        static bool FromJson(const std::string& text, ExportSettings& out);
    };

    // An asset left out of the export.
    struct ExcludedAsset
    {
        std::string Path;      // relative to the asset folder
        AssetType Type = AssetType::Other;
        uintmax_t Size = 0;    // bytes (its .zmeta not counted)
        std::string Reason;    // Uzbek: why
    };

    // Which files of the asset folder an export takes.
    struct ExportAssetPlan
    {
        std::vector<std::string> Included;       // asset-relative files (.zmeta files go with their asset), sorted
        std::vector<ExcludedAsset> Excluded;     // sorted by path
        uintmax_t IncludedBytes = 0, ExcludedBytes = 0;
        std::unordered_set<std::string> ExcludedSet;

        bool IsExcluded(std::string_view path) const { return ExcludedSet.count(std::string(path)) != 0; }
        // The file next to the export (excluded-assets.txt): what was left out and why, with sizes.
        std::string FormatReport(std::string_view gameName) const;
    };

    struct ExportAssetInputs
    {
        // The scenes the game is built from (asset-relative; sub-scenes and the start scene included).
        // Empty: every scene of the project.
        std::vector<std::string> BuildScenes;
        ExportSettings Settings;
        // Folders taken whole whatever uses them (asset-relative): the language files' folder.
        std::vector<std::string> AlwaysFolders;
    };

    class ExportAssets
    {
    public:
        // The plan for the assets `index` was built from (AssetReferenceIndex::Build with the project file
        // and the C++ sources). A scene outside a build list is always left out; with ExcludeUnusedAssets
        // also every asset the game cannot reach, and files only those assets use (a removed model's
        // buffers). Files of no asset type that nothing names (notes, data files) are taken.
        static ExportAssetPlan Plan(const AssetReferenceIndex& index, const ExportAssetInputs& inputs);
        // The scenes an export of `config` takes (asset-relative): the enabled build scenes, the start scene
        // when the list lacks it, and the sub-scenes they bring (SubSceneComponent), in that order. Empty
        // without a build list (every scene). `notes`: what was added and why (Uzbek, for the log).
        static std::vector<std::string> CollectScenes(const ProjectConfig& config, const std::filesystem::path& assetDirectory,
                                                      std::vector<std::string>* notes = nullptr);
        // The plan for the active project (its build scenes, Export settings and language folder); empty
        // without a project. `scenes`: CollectScenes' list.
        static ExportAssetPlan PlanForActiveProject(std::vector<std::string>* scenes = nullptr, std::vector<std::string>* notes = nullptr);
        // `path` (asset-relative) matches an AlwaysInclude entry: the file itself, inside the folder, or the
        // '*' pattern.
        static bool MatchesInclude(std::string_view path, std::string_view pattern);
        // "12.4 MB", "830 KB", "512 B".
        static std::string FormatSize(uintmax_t bytes);
        // The report's file name in the export folder.
        static constexpr const char* ReportFileName = "excluded-assets.txt";
    };

}
