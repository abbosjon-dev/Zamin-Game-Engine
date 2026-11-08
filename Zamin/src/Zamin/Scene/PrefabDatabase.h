#pragma once

#include "Zamin/Core/Base.h"
#include "Zamin/Editor/UndoHistory.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Zamin {

    class Scene;

    // The editor's view of the project's prefab files (PrefabUtility works through it): each
    // .zprefab read once into a TEMPLATE - a scene whose entities have the file's IDs as UUIDs - with
    // the prefabs nested in it brought up to date in memory (a prefab file left stale by a change of
    // a prefab inside it is still right here), a hash of the file together with every prefab it is
    // made of (an instance synced with that hash needs no work), which prefabs use which, and the
    // writes the editor makes (atomic, remembered so the file watcher can tell them from outside
    // changes). Main thread only; paths are relative to the assets ("Prefabs/Enemy.zprefab").
    class PrefabDatabase
    {
    public:
        // Where an entity of a prefab comes from when it belongs to a prefab nested in it.
        struct NestedLink
        {
            std::string Prefab;      // the nested prefab
            uint64_t LocalID = 0;    // the entity's ID in that prefab
            uint64_t Owner = 0;      // the nested instance's root (an ID in this prefab)
        };
        struct Template
        {
            std::string Path;                    // as asked for (normalized)
            std::string Text;                    // the file as read
            std::string Resolved;                // the entities as the prefab makes them now (nested prefabs up to date)
            bool Readable = false;               // the file was there and its entities could be read
            Ref<Scene> Entities;                 // UUID = the ID in the file
            std::vector<uint64_t> Order;         // the IDs depth-first (parents before children)
            uint64_t Root = 0;                   // the first root's ID
            bool Linkable = false;               // one root, every ID set and unique: instances can be linked
            bool IsVariant = false;              // the root itself is an instance of another prefab (`Base`)
            std::string Base;
            std::vector<std::string> Deps;       // the prefabs nested in it (and the base), each once
            std::unordered_map<uint64_t, NestedLink> NestedOwner; // ID -> the nested prefab it belongs to
            std::vector<std::string> Issues;     // why it is not linkable, cycles, nesting too deep ...
            int FileVersion = 0;                 // the format version it was written in (SceneMigration)
            bool Newer = false;                  // written by a newer engine: read as far as understood, never written
            size_t LoadErrors = 0;               // entities / components that could not be read (never written back)

            uint64_t ParentOf(uint64_t id) const;
            std::vector<uint64_t> ChildrenOf(uint64_t id) const; // in order
        };

        PrefabDatabase();
        explicit PrefabDatabase(std::filesystem::path assetRoot);
        ~PrefabDatabase();
        PrefabDatabase(const PrefabDatabase&) = delete;
        PrefabDatabase& operator=(const PrefabDatabase&) = delete;

        // The folder the relative paths are in (empty: the active project's assets).
        void SetAssetRoot(std::filesystem::path root);
        std::filesystem::path GetAssetRoot() const;
        std::filesystem::path ToFileSystemPath(std::string_view relPath) const;
        // "Prefabs\\Enemy.zprefab" -> "Prefabs/Enemy.zprefab".
        static std::string NormalizePath(std::string_view relPath);

        // The template (read when it is asked for the first time, again when the file changed);
        // null when the path is empty. A file that cannot be read gives a template that is not
        // Readable (Issues says why).
        const Template* Get(std::string_view relPath);
        // FNV-1a of the file's bytes with the resolved hashes of the prefabs it is made of (sorted):
        // it changes when the prefab or anything nested in it changes. 0: unreadable.
        uint64_t ResolvedHash(std::string_view relPath);
        // The prefabs that nest `relPath` (or are its variants), directly or through others, each
        // once - the deepest users last (rewrite them in this order).
        std::vector<std::string> GetDependents(std::string_view relPath);
        // `a` is made of `b` (nests it, is its variant, directly or deeper) - or is `b`.
        bool DependsOn(std::string_view a, std::string_view b);

        // Writes a prefab file (atomically) and forgets what was read of it. The write is remembered
        // (IsOwnWrite) and, with `undo`, added to it as a FileEdit (nothing when the file already
        // holds `text`). False when it could not be written - or must not be: a file of a newer
        // format than this engine knows is never written over (what it does not understand would go).
        bool Write(std::string_view relPath, const std::string& text, std::vector<FileEdit>* undo = nullptr);
        // The text was written by this database (the file watcher's change is the editor's own).
        bool IsOwnWrite(const std::filesystem::path& file, uint64_t contentHash) const;
        // What became of a prefab file since this database last looked at it with CheckFile (or wrote
        // it): the file watcher said it changed. The file's text is read and hashed - a change that
        // kept its time and size is found, a touch that kept its text is not one. A file never looked
        // at before counts as changed (its instances are checked; that changes nothing when they are
        // up to date). The template of a changed / added / removed file is forgotten (read again).
        enum class FileChange : uint8_t { None, Changed, Added, Removed, OwnWrite };
        FileChange CheckFile(std::string_view relPath);
        // Forgets the file's template (and the hashes of everything): read again when asked.
        void Invalidate(std::string_view relPath);
        void Clear();

        // How long a template is trusted before the file's time and size are looked at again.
        void SetRecheckInterval(double seconds) { m_RecheckSeconds = seconds; }

        static uint64_t HashText(std::string_view text);
        static constexpr int MaxDepth = 16;   // prefabs nested in prefabs (like SceneManager's sub-scenes)

    private:
        struct Entry;
        Entry* Load(const std::string& path);
        void BuildTemplate(Entry& entry, const std::string& path);
        void ScanDependencies();
        uint64_t ResolvedHashImpl(const std::string& path, std::unordered_set<std::string>& visiting);

        std::filesystem::path m_AssetRoot;
        std::unordered_map<std::string, std::unique_ptr<Entry>> m_Entries;
        std::unordered_map<std::string, uint64_t> m_Hashes;
        std::unordered_map<std::string, uint64_t> m_OwnWrites;          // file (normalized) -> content hash
        struct SeenFile
        {
            bool Exists = false;
            uint64_t Hash = 0;
        };
        std::unordered_map<std::string, SeenFile> m_Seen;               // CheckFile / Write: the text last seen
        std::unordered_map<std::string, std::vector<std::string>> m_Uses; // prefab -> the prefabs nested in it
        bool m_ScanValid = false;
        std::unordered_set<std::string> m_Building;                     // templates being built (cycles)
        int m_Depth = 0;
        double m_RecheckSeconds = 0.5;
    };

}
