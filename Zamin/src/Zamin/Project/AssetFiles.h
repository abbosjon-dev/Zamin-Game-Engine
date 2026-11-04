#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Zamin {

    // What a file of the project is, judged by its extension (content browser icons, the type
    // filter, where dropped files are imported to).
    enum class AssetType : uint8_t
    {
        Folder = 0, Scene, Prefab, Texture, Model, Audio, Font, Environment, Script, Project, Other,
        Count
    };

    // Type filter of the content browser: one bit per AssetType; nothing set = every type.
    struct AssetTypeFilter
    {
        uint32_t Mask = 0;

        bool IsActive() const { return Mask != 0; }
        bool Has(AssetType type) const;
        void Set(AssetType type, bool enabled);
        void Toggle(AssetType type) { Set(type, !Has(type)); }
        void Clear() { Mask = 0; }
        int Count() const;
        // Folders never match an active filter (the listing walks every sub folder instead);
        // the project file counts as "Other".
        bool Matches(AssetType type) const;
        // The types the filter offers, in menu order (no Folder / Project).
        static const std::vector<AssetType>& GetFilterableTypes();
    };

    // One file or folder of a content browser listing.
    struct AssetEntry
    {
        std::filesystem::path Path;   // absolute
        std::string Name;             // file name
        AssetType Type = AssetType::Other;
        bool IsDirectory = false;
        uintmax_t Size = 0;           // bytes (folders: 0)
        std::filesystem::file_time_type Modified{};
    };

    enum class AssetSortColumn : int { Name = 0, Type, Size, Modified };

    struct AssetListOptions
    {
        // Part of the name (any case). Not empty: every sub folder is searched, too.
        std::string Search;
        // Active: every sub folder is searched and only files of these types are listed.
        AssetTypeFilter Filter;
        // Folders left out together with everything inside them (Build, Binaries...).
        std::vector<std::filesystem::path> Excluded;
        AssetSortColumn SortBy = AssetSortColumn::Name;
        bool Ascending = true;
    };

    // File operations of the editor's content browser: listing, moving files into folders (the
    // scenes, prefabs and models that refer to them are updated) and importing files dropped
    // onto the editor window from outside the project.
    class AssetFiles
    {
    public:
        // ---- types
        static AssetType GetType(const std::filesystem::path& path, bool isDirectory = false);
        static const char* GetTypeName(AssetType type);   // shown in the editor ("Rasm", "Model"...)
        static const char* GetTypeKey(AssetType type);    // stored in settings ("Texture", "Model"...)
        static std::optional<AssetType> FindTypeByKey(std::string_view key);
        // Assets sub folder imported files of this type go to ("Textures", "Models"...; "" = Assets).
        static const char* GetImportFolder(AssetType type);

        // ---- listing
        // Hidden names (".git") are skipped. Sorted by options.SortBy, folders first.
        static std::vector<AssetEntry> List(const std::filesystem::path& directory, const AssetListOptions& options);
        static void Sort(std::vector<AssetEntry>& entries, AssetSortColumn column, bool ascending);
        // Case-insensitive, numbers in numeric order ("Level2" before "Level10"). <0, 0, >0.
        static int CompareNames(std::string_view a, std::string_view b);
        static std::string FormatSize(uintmax_t bytes);   // "512 B", "1.5 KB", "3.2 MB"

        // ---- paths
        // `path` is `directory` itself or anything below it (both made absolute and normalized).
        static bool IsInside(const std::filesystem::path& path, const std::filesystem::path& directory);
        // directory / fileName, or "name_1.ext", "name_2.ext"... when that is taken.
        static std::filesystem::path MakeUniquePath(const std::filesystem::path& directory, const std::filesystem::path& fileName);
        // Asset path `value` after `from` (a file, or a folder: everything below it too) moved to
        // `to`. Nothing when `value` is not affected.
        static std::optional<std::string> RemapPath(std::string_view value, std::string_view from, std::string_view to);
        // Calls `remap` for every string value of a JSON text (object keys are not values) with
        // the key the value belongs to ("" for array items). A returned string replaces the value
        // in place; the rest of the text stays byte for byte. Returns how many were replaced.
        using JsonStringRemap = std::function<std::optional<std::string>(std::string_view key, const std::string& value)>;
        static size_t RewriteJsonStrings(std::string& text, const JsonStringRemap& remap);

        // ---- moving (content browser drag & drop, rename)
        struct Move
        {
            std::filesystem::path From, To; // absolute
            bool IsDirectory = false;
        };
        struct MoveResult
        {
            std::vector<Move> Moved;
            std::vector<std::string> Errors;                   // what could not be moved, and why
            std::vector<std::filesystem::path> UpdatedFiles;   // scenes / prefabs / models rewritten
            size_t UpdatedReferences = 0;
            std::vector<std::filesystem::path> SourceMentions; // C++ files spelling an old path (left alone)

            // Where `path` (a moved item or anything inside a moved folder) is now; others unchanged.
            std::filesystem::path MapPath(const std::filesystem::path& path) const;
        };
        // Why `source` cannot be moved into `destinationDir` (empty = it can): missing files,
        // a folder into itself, already there, the name taken, or in / out of the asset folder.
        static std::string CheckMove(const std::filesystem::path& source, const std::filesystem::path& destinationDir,
                                     const std::filesystem::path& assetDirectory);
        // Moves the files / folders into `destinationDir` and rewrites what refers to them: asset
        // paths in every .zscene / .zprefab under `assetDirectory` and the relative file names
        // (uri) of .gltf models. C++ files under `sourceDirectory` that spell an old path are only
        // reported. Items that cannot move are skipped (see Errors).
        static MoveResult MoveInto(const std::vector<std::filesystem::path>& sources, const std::filesystem::path& destinationDir,
                                   const std::filesystem::path& assetDirectory, const std::filesystem::path& sourceDirectory = {});
        // A move inside the same folder (the references follow it the same way).
        static MoveResult Rename(const std::filesystem::path& source, const std::string& newName,
                                 const std::filesystem::path& assetDirectory, const std::filesystem::path& sourceDirectory = {});

        // ---- importing (files dropped onto the editor window)
        struct ImportResult
        {
            std::vector<std::filesystem::path> Imported; // the new files / folders (absolute)
            std::vector<std::string> Errors;
        };
        // Copies files / folders into `destinationDir` under names that are free there (nothing is
        // overwritten). A .gltf that uses separate files (buffers, textures) gets a folder of its
        // own with those files next to it.
        static ImportResult Import(const std::vector<std::filesystem::path>& sources, const std::filesystem::path& destinationDir);
        // Existing files a model refers to by relative name (.gltf buffers and images), absolute.
        static std::vector<std::filesystem::path> GetModelDependencies(const std::filesystem::path& modelPath);
    };

}
