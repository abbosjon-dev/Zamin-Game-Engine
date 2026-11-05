#pragma once

#include "Zamin/Asset/AssetTypes.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Zamin {

    class AssetRegistry;

    // How a file refers to an asset (bits of AssetReferrer::Kinds).
    enum class AssetReferenceKind : uint8_t
    {
        Path = 1,       // an asset path in a scene / prefab / material / the project file
        UUID = 2,       // the asset's UUID ("<Key>UUID"), also when the path next to it is out of date
        ModelFile = 4,  // a file name (uri) inside a .gltf model: buffers, images
        Code = 8,       // a string literal in the project's C++ code ("Prefabs/Ball.zprefab")
    };

    // A file that refers to an asset.
    struct AssetReferrer
    {
        std::filesystem::path File;   // absolute
        std::string Name;             // relative to the project folder ("Assets/Scenes/Level1.zscene", "Source/Player.cpp")
        std::string AssetPath;        // the referring file inside the asset folder ("Scenes/Level1.zscene"; "" outside it)
        uint32_t Count = 0;           // references to the asset in the file
        uint8_t Kinds = 0;            // AssetReferenceKind bits
        bool Unsaved = false;         // not a file: an open scene that was never saved (File is made up)

        bool Has(AssetReferenceKind kind) const { return (Kinds & (uint8_t)kind) != 0; }
    };

    // An asset path that leads nowhere (the file is gone and no UUID finds it).
    struct MissingAssetReference
    {
        std::filesystem::path File;   // the referring file (absolute)
        std::string Name;             // its name relative to the project folder
        std::string Path;             // the asset path it names
        bool Unsaved = false;         // named by an open scene that has no file yet
    };

    // An asset that is about to go (deleted) and the files that would be left referring to it.
    struct AssetInUse
    {
        std::string Path;                       // relative to the asset folder
        AssetType Type = AssetType::Other;
        std::vector<AssetReferrer> Referrers;   // by name; none of them is being deleted itself
        uint32_t GetReferenceCount() const;
    };

    struct UnusedAsset
    {
        std::string Path;             // relative to the asset folder
        AssetType Type = AssetType::Other;
        uintmax_t Size = 0;           // bytes
    };

    // Who refers to which asset in a project: asset paths and UUIDs in scenes, prefabs, materials
    // and the project file, file names inside .gltf models and string literals in C++ scripts.
    // Answers "where is this used" (the asset inspector, deleting) and "what is not used at all".
    class AssetReferenceIndex
    {
    public:
        // Something open in the editor that has no file yet (a new scene never saved): scanned
        // like a scene. `File` is a made-up absolute path where no file is - it only tells the
        // referrer apart -, `Name` is what the lists show for it.
        struct UnsavedFile
        {
            std::filesystem::path File;
            std::string Name;
            std::string Text;
        };

        struct Sources
        {
            std::filesystem::path AssetDirectory;
            std::filesystem::path ProjectDirectory;  // names are relative to it ("" = the asset folder's parent)
            std::filesystem::path ProjectFile;       // the .zproject (start scene); "" = none
            std::filesystem::path SourceDirectory;   // C++ scripts; "" = none
            // UUID references and the list of assets (nullptr: every file of the asset folder whose
            // type has a .zmeta - scenes, prefabs, textures, models, audio, fonts, HDRIs, materials).
            const AssetRegistry* Registry = nullptr;
            // Text to read instead of a file on disk, by absolute path (an open scene with unsaved changes).
            std::unordered_map<std::string, std::string> Overrides;
            std::vector<UnsavedFile> Unsaved;
        };

        // Reads every file that can refer to assets (replaces what was built before).
        void Build(const Sources& sources);
        // The same for the active project (its registry included); false without a project.
        bool BuildForActiveProject(std::unordered_map<std::string, std::string> overrides = {}, std::vector<UnsavedFile> unsaved = {});
        void Clear();
        bool IsBuilt() const { return m_Built; }

        // Files that refer to an asset (relative to the asset folder, or absolute), by name. A file
        // naming itself (a scene that reloads itself) does not count.
        const std::vector<AssetReferrer>& GetReferrers(const std::filesystem::path& asset) const;
        bool IsReferenced(const std::filesystem::path& asset) const { return !GetReferrers(asset).empty(); }
        // What deleting `paths` (files or folders, absolute or asset-relative) would break: the
        // assets among them that other files refer to, sorted by path. Files that are deleted
        // together (a model and its buffer, a folder of scenes using each other) do not count.
        std::vector<AssetInUse> FindUsesOutside(const std::vector<std::filesystem::path>& paths) const;
        // Assets a file (absolute or asset-relative) refers to, as asset-relative paths, sorted.
        std::vector<std::string> GetReferences(const std::filesystem::path& file) const;
        // Asset paths that lead nowhere, by referring file.
        const std::vector<MissingAssetReference>& GetMissing() const { return m_Missing; }

        // Assets that nothing uses, sorted by path. reachableOnly = false: no file refers to them.
        // true: only what the project file (the start scene) and the C++ code lead to - directly or
        // through scenes, prefabs, models and materials - is used; a scene nothing opens and
        // everything only it refers to are unused as well.
        std::vector<UnusedAsset> FindUnused(bool reachableOnly = false) const;

        // The registered assets that were considered (asset-relative, sorted).
        const std::vector<std::string>& GetAssets() const { return m_AssetList; }
        size_t GetScannedFileCount() const { return m_ScannedFiles; }
        size_t GetReferenceCount() const { return m_ReferenceCount; }
        const std::filesystem::path& GetAssetDirectory() const { return m_AssetDirectory; }

    private:
        std::string ToAssetPath(const std::filesystem::path& path) const;  // "" when outside the asset folder
        std::string ToKey(const std::filesystem::path& file) const;        // absolute, normalized
        bool AssetExists(const std::string& relativePath) const;
        void AddReference(const std::filesystem::path& file, const std::string& asset, AssetReferenceKind kind);
        void AddMissing(const std::filesystem::path& file, const std::string& path);
        // A referring file's name in the lists (relative to the project folder; an unsaved one's own).
        std::string GetReferrerName(const std::string& key, bool* unsaved = nullptr) const;
        std::string ReadText(const std::filesystem::path& file) const;

        void ScanJson(const std::filesystem::path& file, const std::string& text);
        void ScanModel(const std::filesystem::path& file, const std::string& text);
        void ScanCode(const std::filesystem::path& file, const std::string& text);

    private:
        Sources m_Sources;
        std::filesystem::path m_AssetDirectory, m_ProjectDirectory;
        std::unordered_set<std::string> m_AssetSet;         // registered assets (asset-relative)
        std::vector<std::string> m_AssetList;
        std::unordered_set<std::string> m_TopFolders;       // first folders of the asset folder (code literals)
        std::unordered_map<std::string, std::vector<AssetReferrer>> m_Referrers;  // asset -> files
        std::unordered_map<std::string, std::vector<std::string>> m_Outgoing;     // file key -> assets
        std::unordered_set<std::string> m_Roots;            // file keys: the project file, C++ files
        std::unordered_map<std::string, std::string> m_UnsavedNames; // file key -> name (Sources::Unsaved)
        std::vector<MissingAssetReference> m_Missing;
        size_t m_ScannedFiles = 0;
        size_t m_ReferenceCount = 0;
        bool m_Built = false;
    };

}
