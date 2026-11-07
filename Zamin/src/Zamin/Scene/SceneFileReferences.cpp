#include "Zamin/Scene/SceneFileReferences.h"

#include "Zamin/Utils/FileSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <format>

namespace Zamin {

    using json = nlohmann::json;

    void SceneFileUsage::Add(const SceneFileUsage& other)
    {
        for (const auto& [tag, count] : other.Tags)
            Tags[tag] += count;
        for (const auto& [id, count] : other.SortingLayers)
            SortingLayers[id] += count;
        Entities += other.Entities;
        Files += other.Files;
    }

    size_t SceneFileUsage::GetTagCount(std::string_view tag) const
    {
        auto it = Tags.find(std::string(tag));
        return it != Tags.end() ? it->second : 0;
    }

    size_t SceneFileUsage::GetSortingLayerCount(uint32_t id) const
    {
        auto it = SortingLayers.find(id);
        return it != SortingLayers.end() ? it->second : 0;
    }

    namespace {

        // The "Entities" array of a scene / prefab file (nullptr: not one).
        json* FindEntities(json& root)
        {
            if (!root.is_object())
                return nullptr;
            auto it = root.find("Entities");
            return it != root.end() && it->is_array() ? &*it : nullptr;
        }

        // The 2D renderers of one entity and their sorting layers (the Default layer is not written).
        void CountSortingLayers(const json& entity, SceneFileUsage& usage)
        {
            for (const char* key : { "SpriteRenderer", "CircleRenderer", "Text", "Tilemap", "ParticleSystem" })
            {
                auto it = entity.find(key);
                if (it == entity.end() || !it->is_object())
                    continue;
                uint32_t id = 0;
                if (auto layer = it->find("SortingLayer"); layer != it->end() && layer->is_number_unsigned())
                    id = layer->get<uint32_t>();
                usage.SortingLayers[id]++;
            }
        }

    }

    namespace SceneFileReferences {

        bool CountUsage(std::string_view text, SceneFileUsage& usage)
        {
            json root = json::parse(text, nullptr, false);
            json* entities = root.is_discarded() ? nullptr : FindEntities(root);
            if (!entities)
                return false;
            usage.Files++;
            for (const json& e : *entities)
            {
                if (!e.is_object() || !e.contains("Entity"))
                    continue;
                usage.Entities++;
                if (auto tags = e.find("Tags"); tags != e.end() && tags->is_array())
                {
                    // An entity counts once per tag (a duplicate in a hand-edited file too).
                    std::vector<std::string> seen;
                    for (const json& tag : *tags)
                        if (tag.is_string() && std::find(seen.begin(), seen.end(), tag.get<std::string>()) == seen.end())
                        {
                            seen.push_back(tag.get<std::string>());
                            usage.Tags[seen.back()]++;
                        }
                }
                CountSortingLayers(e, usage);
            }
            return true;
        }

        int RenameTag(std::string& text, std::string_view from, std::string_view to)
        {
            json root = json::parse(text, nullptr, false);
            json* entities = root.is_discarded() ? nullptr : FindEntities(root);
            if (!entities)
                return -1;
            if (from.empty() || to.empty() || from == to)
                return 0;
            int changed = 0;
            for (json& e : *entities)
            {
                if (!e.is_object())
                    continue;
                auto tags = e.find("Tags");
                if (tags == e.end() || !tags->is_array())
                    continue;
                bool hasFrom = false, hasTo = false;
                for (const json& tag : *tags)
                    if (tag.is_string())
                    {
                        hasFrom |= tag.get<std::string>() == from;
                        hasTo |= tag.get<std::string>() == to;
                    }
                if (!hasFrom)
                    continue;
                json renamed = json::array();
                for (const json& tag : *tags)
                {
                    if (tag.is_string() && tag.get<std::string>() == from)
                    {
                        if (!hasTo)
                        {
                            renamed.push_back(std::string(to));
                            hasTo = true;
                        }
                        continue;
                    }
                    renamed.push_back(tag);
                }
                *tags = std::move(renamed);
                changed++;
            }
            if (changed > 0)
                text = root.dump(2); // the scene serializer's own format (sorted keys, 2 spaces)
            return changed;
        }

        std::vector<std::filesystem::path> FindSceneFiles(const std::filesystem::path& directory)
        {
            std::vector<std::filesystem::path> files;
            std::error_code ec;
            for (auto it = std::filesystem::recursive_directory_iterator(directory, std::filesystem::directory_options::skip_permission_denied, ec);
                 !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
            {
                if (!it->is_regular_file(ec))
                    continue;
                const std::filesystem::path ext = it->path().extension();
                if (ext == ".zscene" || ext == ".zprefab")
                    files.push_back(std::filesystem::relative(it->path(), directory, ec).generic_string());
            }
            std::sort(files.begin(), files.end());
            return files;
        }

        static bool SameFile(const std::filesystem::path& a, const std::filesystem::path& b)
        {
            if (b.empty())
                return false;
            std::error_code ec;
            if (std::filesystem::equivalent(a, b, ec))
                return true;
            return std::filesystem::weakly_canonical(a, ec).lexically_normal() == std::filesystem::weakly_canonical(b, ec).lexically_normal();
        }

        SceneFileUsage ScanDirectory(const std::filesystem::path& directory, const std::filesystem::path& skip)
        {
            SceneFileUsage total;
            for (const std::filesystem::path& file : FindSceneFiles(directory))
            {
                const std::filesystem::path full = directory / file;
                if (SameFile(full, skip))
                    continue;
                std::string text = FileSystem::ReadTextFile(full);
                if (!text.empty())
                    CountUsage(text, total);
            }
            return total;
        }

        size_t RenameTagInDirectory(const std::filesystem::path& directory, std::string_view from, std::string_view to,
                                    const std::filesystem::path& skip, std::vector<std::string>* errors)
        {
            size_t written = 0;
            for (const std::filesystem::path& file : FindSceneFiles(directory))
            {
                const std::filesystem::path full = directory / file;
                if (SameFile(full, skip))
                    continue;
                std::string text = FileSystem::ReadTextFile(full);
                if (text.empty())
                    continue;
                const int changed = RenameTag(text, from, to);
                if (changed <= 0)
                    continue;
                if (FileSystem::WriteTextFile(full, text))
                    written++;
                else if (errors)
                    errors->push_back(std::format("{}: yozib bo'lmadi", file.generic_string()));
            }
            return written;
        }

    }

}
