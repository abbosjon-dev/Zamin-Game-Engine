#pragma once

#include "Zamin/Asset/FileWatcher.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Zamin {

    class AssetRegistry;

    // Keeps a running editor in step with the asset folder: watches it, keeps the asset registry up
    // to date (new files get a .zmeta, removed ones are forgotten) and imports changed files again
    // into the objects already in use - the same texture / model / font / environment object, so
    // components, renderers and editor previews keep working and simply show the new content.
    class AssetHotReload
    {
    public:
        struct Report
        {
            // Asset files and folders that changed (never their .zmeta files).
            std::vector<std::filesystem::path> Added, Modified, Removed;
            // Files imported again into the loaded objects that use them.
            std::vector<std::filesystem::path> Reloaded;
            // Files that could not be read again (the loaded objects keep the old content).
            std::vector<std::filesystem::path> Failed;
            // Broken / duplicate .zmeta files.
            std::vector<std::string> Problems;

            bool IsEmpty() const
            {
                return Added.empty() && Modified.empty() && Removed.empty() && Reloaded.empty() && Failed.empty() && Problems.empty();
            }
            // Files or folders appeared or disappeared (listings are out of date).
            bool HasStructureChanges() const { return !Added.empty() || !Removed.empty(); }
        };

        bool Start(const std::filesystem::path& assetDirectory, const FileWatcherOptions& options = {});
        void Stop();
        bool IsRunning() const { return m_Watcher.IsWatching(); }
        const FileWatcher& GetWatcher() const { return m_Watcher; }

        // Main thread, once a frame: applies the changes that settled. `registry` may be null.
        Report Update(AssetRegistry* registry);
        // Applies changes found some other way (the same as Update does with the watcher's).
        Report Apply(const std::vector<FileChange>& changes, AssetRegistry* registry);

        // Imports one file again into the loaded objects made from it, with its current import
        // settings: textures (every filter variant), models (also when one of their .gltf buffers
        // changed), fonts and HDR environment maps. Returns how many were updated; `failed` is
        // set when the file could not be read (they keep the old content).
        static size_t ReloadAsset(const std::filesystem::path& path, bool* failed = nullptr);
        // Whether something loaded was made from this file (ReloadAsset would update it).
        static bool IsLoaded(const std::filesystem::path& path);
        // Whether files of this kind can be imported again while in use.
        static bool CanReload(const std::filesystem::path& path);

    private:
        FileWatcher m_Watcher;
        // Files deleted while something loaded used them: imported again when they come back.
        std::vector<std::filesystem::path> m_MissingWhileLoaded;
    };

}
