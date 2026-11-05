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
            // With the previous snapshot (Scan(directory, &snapshot)): assets found at a new path -
            // moved while no registry watched (TakeDetectedMoves lists them) ...
            size_t MovedOutside = 0;
            // ... of them, files moved without their .zmeta that got their UUID and settings back.
            size_t RecoveredMeta = 0;
            std::vector<std::string> Problems; // one line per broken / duplicate .zmeta
        };

        // The registry as one document: every asset with its handle, type, path, import settings and
        // the size / time of its file. The editor keeps one in the project's cache folder (files moved
        // while it was closed are recognized at the next scan, even without their .zmeta) and puts one
        // into exported games (the player reads it instead of every .zmeta file).
        struct SnapshotEntry
        {
            AssetHandle Handle;
            AssetType Type = AssetType::Other;
            std::string Path;          // relative to the asset directory
            uintmax_t Size = 0;
            int64_t Time = 0;          // file clock ticks
            bool HasMetaFile = false;
            AssetImportSettings Import;
            bool operator==(const SnapshotEntry&) const = default;
        };
        using Snapshot = std::vector<SnapshotEntry>;
        static constexpr int SnapshotVersion = 1;

        // Writes .zmeta files when SetWriteMetaFilesByDefault(true) was called.
        AssetRegistry();
        explicit AssetRegistry(bool writeMetaFiles);

        bool GetWriteMetaFiles() const { return m_WriteMetaFiles; }
        void SetWriteMetaFiles(bool write) { m_WriteMetaFiles = write; }
        // Whether registries made with the default constructor write .zmeta files (the editor: yes).
        static void SetWriteMetaFilesByDefault(bool write);
        static bool GetWriteMetaFilesByDefault();

        // Forgets everything and registers every asset below `assetDirectory` (hidden names skipped).
        // With the snapshot of an earlier registry of the same folder, assets that moved since then
        // keep their handles even when their .zmeta was left behind (same type, size and
        // modification time, the old path gone, only one such file) and the moves are reported
        // (ScanResult::MovedOutside, TakeDetectedMoves).
        const ScanResult& Scan(const std::filesystem::path& assetDirectory, const Snapshot* previous = nullptr);
        // Registers the assets of a snapshot without reading the folder (an exported game's
        // registry file). With `verify`, nothing is registered and false returned when one of the
        // files is missing (the folder changed since: Scan it instead).
        bool Load(const std::filesystem::path& assetDirectory, const Snapshot& snapshot, bool verify = true);
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

        // The form Scan keeps a directory in (absolute, normalized, no trailing separator).
        static std::filesystem::path NormalizeDirectory(const std::filesystem::path& directory);
        // "Textures/coin.png" for a path inside the asset directory ("" for one outside it).
        std::string ToRelativePath(const std::filesystem::path& path) const;
        std::filesystem::path ToFileSystemPath(std::string_view relativePath) const;

        // ---- changes on disk (file watcher, content browser); paths absolute or relative
        // A file (or a folder: everything inside it) appeared. Returns how many assets were registered.
        // A file without a .zmeta that has the type, size and modification time of an asset whose
        // file just went away is that asset moved: it keeps its handle and settings, its old .zmeta
        // is removed and the move is reported (TakeDetectedMoves).
        size_t OnFileAdded(const std::filesystem::path& path);
        // A file or a folder is gone. Returns how many assets were forgotten.
        size_t OnFileRemoved(const std::filesystem::path& path);
        // An asset file was written: its size and time are read again. False when it is unknown.
        bool OnFileModified(const std::filesystem::path& path);
        // A file or a folder moved (its .zmeta files with it): the handles stay. Returns how many moved.
        size_t OnFileMoved(const std::filesystem::path& from, const std::filesystem::path& to);
        // A .zmeta file changed (or vanished) on disk. Returns the asset when its import settings
        // changed (it should be imported again), otherwise nullptr.
        const AssetMetadata* OnMetaFileChanged(const std::filesystem::path& metaPath);

        // New import settings of an asset (saved to its .zmeta when this registry writes them).
        bool SetImportSettings(AssetHandle handle, const AssetImportSettings& settings);

        // ---- serialization (the snapshot of this registry, sorted by path)
        Snapshot GetSnapshot() const;
        // JSON text: { "Version": 1, "Assets": [ { "UUID", "Type", "Path", "Size", "Time", "Meta", "Import" } ] }.
        std::string Serialize() const;
        static std::string SerializeSnapshot(const Snapshot& snapshot);
        // False, with the reason in `error`, for text that is not a registry document. Entries without
        // a valid UUID / path, or repeating one, are skipped (`skipped` counts them).
        static bool Deserialize(std::string_view text, Snapshot& out, std::string* error = nullptr, size_t* skipped = nullptr);
        // Writes Serialize() to `file` (folders created; a file that already holds it is not touched).
        bool SaveSnapshot(const std::filesystem::path& file) const;
        // Nothing when the file is missing (`error` empty) or not a registry document (`error` says why).
        static std::optional<Snapshot> LoadSnapshot(const std::filesystem::path& file, std::string* error = nullptr);
        // Changes whenever an asset is registered, forgotten, moved or gets new settings (the editor
        // saves its snapshot when it changed).
        uint64_t GetRevision() const { return m_Revision; }

        // Broken / duplicate .zmeta files met since the last call (outside Scan, whose result lists its own).
        std::vector<std::string> TakeProblems();

        // Assets that were moved or renamed outside the editor since the last call, as (old, new)
        // relative paths: a file removed and one added with the same UUID (its .zmeta went with
        // it), in either order. The editor updates the references to them.
        using PathMove = std::pair<std::string, std::string>;
        std::vector<PathMove> TakeDetectedMoves();

    private:
        struct Candidate
        {
            std::string RelativePath;
            AssetType Type = AssetType::Other;
            std::optional<AssetMetadata> Meta;   // what its .zmeta says
            std::string MetaError;               // why it could not be read ("" = missing / fine)
            // Meta came from where the file was before (moved without its .zmeta): written now.
            bool Recovered = false;
        };
        // A forgotten asset, kept until the next scan: a file added later with its UUID - or, without
        // a .zmeta, with its type, size and time - is that asset moved.
        struct RemovedAsset
        {
            std::string Path;
            AssetType Type = AssetType::Other;
            uintmax_t Size = 0;
            int64_t Time = 0;
            bool HasMetaFile = false;
            AssetImportSettings Import;
        };

        Candidate ReadCandidate(const std::string& relativePath, AssetType type) const;
        // Registers the asset (a new handle for a duplicate) and writes its .zmeta when needed.
        const AssetMetadata* Insert(const Candidate& candidate, bool duplicate, ScanResult* scan);
        void Erase(AssetHandle handle);
        bool ExistsOnDisk(const std::string& relativePath) const;
        // Size and modification time of an asset file; false when it cannot be read.
        bool ReadStamp(const std::string& relativePath, uintmax_t& size, int64_t& time) const;
        // A file without a .zmeta that is a removed (or vanished) asset moved: gets its identity.
        bool AdoptMovedIdentity(Candidate& candidate);
        // The .zmeta left behind at `oldPath` (holding `handle`) is deleted - the asset took it along.
        void RemoveLeftBehindMeta(const std::string& oldPath, AssetHandle handle);
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
        // Removed since the last scan (a file added later with that identity moved there).
        std::unordered_map<AssetHandle, RemovedAsset> m_Removed;
        std::vector<PathMove> m_DetectedMoves;
        uint64_t m_Revision = 0;
        bool m_WriteMetaFiles = false;
        bool m_Scanned = false;
    };

}
