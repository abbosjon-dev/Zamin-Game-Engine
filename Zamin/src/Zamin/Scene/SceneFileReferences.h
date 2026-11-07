#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace Zamin {

    // What the entities of scene / prefab files on disk use: their gameplay tags and the 2D sorting
    // layers of their renderers. Read straight from the files' JSON (no scene is loaded), for the
    // project settings' usage counts over the whole project and for renaming a tag everywhere.
    struct SceneFileUsage
    {
        std::map<std::string, size_t> Tags;         // tag -> entities that have it
        std::map<uint32_t, size_t> SortingLayers;   // sorting layer ID -> renderers on it
        size_t Entities = 0;
        size_t Files = 0;

        void Add(const SceneFileUsage& other);
        size_t GetTagCount(std::string_view tag) const;
        size_t GetSortingLayerCount(uint32_t id) const;
    };

    namespace SceneFileReferences {

        // Adds the entities of one scene / prefab file's text to `usage`. False when the text is not
        // such a file (nothing is added).
        bool CountUsage(std::string_view text, SceneFileUsage& usage);

        // Renames a tag on every entity of the file's text (an entity that has both keeps one). The text
        // is written again the way the scene serializer writes it. Returns how many entities changed
        // (0: the text stays exactly as it was), -1 when the text is not a scene / prefab file.
        int RenameTag(std::string& text, std::string_view from, std::string_view to);

        // The .zscene / .zprefab files under `directory` (sorted, generic relative paths).
        std::vector<std::filesystem::path> FindSceneFiles(const std::filesystem::path& directory);

        // Every scene / prefab file under `directory` counted (a file that cannot be read is skipped;
        // `skip` - the scene open in the editor, counted from memory instead - is left out).
        SceneFileUsage ScanDirectory(const std::filesystem::path& directory, const std::filesystem::path& skip = {});

        // Renames the tag in every scene / prefab file under `directory` except `skip` and writes the
        // files that changed. Returns how many files were written; problems go to `errors`.
        size_t RenameTagInDirectory(const std::filesystem::path& directory, std::string_view from, std::string_view to,
                                    const std::filesystem::path& skip = {}, std::vector<std::string>* errors = nullptr);

    }

}
