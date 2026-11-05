#pragma once

#include "Zamin/Asset/AssetMetadata.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Zamin {

    // Every asset file of a project's asset folder with its handle: handle <-> path in both
    // directions. Components keep referring to assets by path; the handle (from the asset's .zmeta
    // file) is what stays the same when a file is renamed or moved.
    //
    // The editor writes the .zmeta files that are missing (SetWriteMetaFilesByDefault(true) at
    // startup); the player only reads them - an asset without one gets a handle for the session.
    class AssetRegistry
    {
    public:
        struct ScanResult
        {
            size_t Assets = 0;         // registered asset files
            size_t CreatedMeta = 0;    // .zmeta files written for assets that had none
            size_t RepairedMeta = 0;   // .zmeta files rewritten: a UUID another asset has, a wrong type
            size_t BrokenMeta = 0;     // unreadable .zmeta files (left alone; the asset has a session handle)
            size_t OrphanMeta = 0;     // .zmeta files whose asset is gone
            std::vector<std::string> Problems; // one line per broken / duplicate .zmeta
        };

        // Writes .zmeta files when SetWriteMetaFilesByDefault(true) was called.
        AssetRegistry();
        explicit AssetRegistry(bool writeMetaFiles);

        bool GetWriteMetaFiles() const { return m_WriteMetaFiles; }
        void SetWriteMetaFiles(bool write) { m_WriteMetaFiles = write; }
        // Whether registries made with the default constructor write .zmeta files (the editor: yes).
        static void SetWriteMetaFilesByDefault(bool write);
        static bool GetWriteMetaFilesByDefault();

        // Forgets everything and registers every asset below `assetDirectory` (hidden names skipped).
        const ScanResult& Scan(const std::filesystem::path& assetDirectory);
        void Clear();
        bool IsScanned() const { return m_Scanned; }
        const std::filesystem::path& GetAssetDirectory() const { return m_AssetDirectory; }
        const ScanResult& GetLastScan() const { return m_LastScan; }

        // ---- lookups (paths relative to the asset directory as components store them, or absolute)
        const AssetMetadata* Find(AssetHandle handle) const;
        const AssetMetadata* Find(const std::filesystem::path& path) const;
        AssetHandle GetHandle(const std::filesystem::path& path) const;   // invalid when unknown
        std::string GetPath(AssetHandle handle) const;                     // "" when unknown
        bool Contains(AssetHandle handle) const { return Find(handle) != nullptr; }
        size_t GetCount() const { return m_Assets.size(); }
        // Sorted by path; only one type when given.
        std::vector<const AssetMetadata*> GetAssets(std::optional<AssetType> type = std::nullopt) const;

        // "Textures/coin.png" for a path inside the asset directory ("" for one outside it).
        std::string ToRelativePath(const std::filesystem::path& path) const;
        std::filesystem::path ToFileSystemPath(std::string_view relativePath) const;

        // ---- changes on disk (file watcher, content browser); paths absolute or relative
        // A file (or a folder: everything inside it) appeared. Returns how many assets were registered.
        size_t OnFileAdded(const std::filesystem::path& path);
        // A file or a folder is gone. Returns how many assets were forgotten.
        size_t OnFileRemoved(const std::filesystem::path& path);
        // A file or a folder moved (its .zmeta files with it): the handles stay. Returns how many moved.
        size_t OnFileMoved(const std::filesystem::path& from, const std::filesystem::path& to);
        // A .zmeta file changed (or vanished) on disk. Returns the asset when its import settings
        // changed (it should be imported again), otherwise nullptr.
        const AssetMetadata* OnMetaFileChanged(const std::filesystem::path& metaPath);

        // New import settings of an asset (saved to its .zmeta when this registry writes them).
        bool SetImportSettings(AssetHandle handle, const AssetImportSettings& settings);

        // Broken / duplicate .zmeta files met since the last call (outside Scan, whose result lists its own).
        std::vector<std::string> TakeProblems();

    private:
        struct Candidate
        {
            std::string RelativePath;
            AssetType Type = AssetType::Other;
            std::optional<AssetMetadata> Meta;   // what its .zmeta says
            std::string MetaError;               // why it could not be read ("" = missing / fine)
        };

        Candidate ReadCandidate(const std::string& relativePath, AssetType type) const;
        // Registers the asset (a new handle for a duplicate) and writes its .zmeta when needed.
        const AssetMetadata* Insert(const Candidate& candidate, bool duplicate, ScanResult* scan);
        void Erase(AssetHandle handle);
        bool ExistsOnDisk(const std::string& relativePath) const;
        bool SaveMeta(AssetMetadata& metadata);
        void CollectAssets(const std::filesystem::path& directory, std::vector<std::string>& assets,
                           std::vector<std::string>* metaFiles) const;
        static std::string NormalizeRelative(const std::filesystem::path& path);

    private:
        std::filesystem::path m_AssetDirectory;
        std::filesystem::path m_CanonicalDirectory;
        std::unordered_map<AssetHandle, AssetMetadata> m_Assets;
        std::unordered_map<std::string, AssetHandle> m_Paths;
        ScanResult m_LastScan;
        std::vector<std::string> m_Problems;
        bool m_WriteMetaFiles = false;
        bool m_Scanned = false;
    };

}
