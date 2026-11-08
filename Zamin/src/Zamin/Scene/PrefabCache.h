#pragma once

#include "Zamin/Scene/Entity.h"
#include "Zamin/Scene/SceneSerializer.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Zamin {

    // The game's prefab files read and parsed once: Scene::InstantiatePrefab and a script's
    // Instantiate(path) make their entities from the parsed file (already in the current format)
    // instead of reading and parsing it again for every spawn. Keyed by the file; while
    // CheckForChanges is on (the editor) its time and size are looked at on every use and a changed
    // file is read again, the player turns it off (its files do not change while it runs: a file is
    // looked at the first time only). Only a file whose entities could be made stays cached - a
    // missing or broken one is read again the next time it is asked for (its error is reported by the
    // caller, once: AssetImportLog). Any thread (a mutex guards the cache).
    class PrefabCache
    {
    public:
        struct Stats
        {
            uint64_t Reads = 0;    // files read and parsed
            uint64_t Hits = 0;     // spawns made from a parsed file
            size_t Entries = 0;    // files cached now
        };

        // The prefab file's entities made in `scene` as SceneSerializer::DeserializeEntities(scene,
        // <its text>, options) makes them. Empty when it cannot be read (`unreadable`: why -
        // "fayl topilmadi" ...) or has no entities (options.Error says why).
        static std::vector<Entity> Instantiate(Scene& scene, const std::filesystem::path& file,
                                               const SceneSerializer::EntitySetReadOptions& options, std::string* unreadable = nullptr);

        static void SetCheckForChanges(bool check);
        static bool GetCheckForChanges();
        // The file is read again the next time (the editor writes prefab files).
        static void Invalidate(const std::filesystem::path& file);
        static void Clear();
        static Stats GetStats();
        static void ResetStats();
    };

}
