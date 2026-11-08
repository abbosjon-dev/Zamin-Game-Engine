#include "Zamin/Scene/PrefabCache.h"

#include "Zamin/Asset/AssetImportLog.h"
#include "Zamin/Scene/SceneSerializerJson.h"
#include "Zamin/Utils/FileSystem.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace Zamin {

    namespace {

        struct Parsed
        {
            json Root;                       // in the current format (PrepareEntitySet)
            SceneMigrationResult Migration;  // what bringing it there did
        };

        struct CacheEntry
        {
            std::shared_ptr<const Parsed> Data;
            fs::file_time_type Time{};
            uintmax_t Size = 0;
        };

        std::mutex s_Mutex;
        std::unordered_map<std::string, CacheEntry> s_Entries;
        std::atomic<bool> s_CheckForChanges{ true };
        std::atomic<uint64_t> s_Reads{ 0 }, s_Hits{ 0 };

        std::string KeyOf(const fs::path& file) { return FileSystem::NormalizePath(file); }

        void Erase(const std::string& key)
        {
            std::lock_guard lock(s_Mutex);
            s_Entries.erase(key);
        }

    }

    std::vector<Entity> PrefabCache::Instantiate(Scene& scene, const fs::path& file, const SceneSerializer::EntitySetReadOptions& options,
                                                 std::string* unreadable)
    {
        if (unreadable)
            unreadable->clear();
        const std::string key = KeyOf(file);
        CacheEntry cached;
        {
            std::lock_guard lock(s_Mutex);
            if (auto it = s_Entries.find(key); it != s_Entries.end())
                cached = it->second;
        }
        std::shared_ptr<const Parsed> data = cached.Data;
        if (data && s_CheckForChanges.load(std::memory_order_relaxed))
        {
            // Changed (or gone) since it was read: read again.
            std::error_code ec;
            const fs::file_time_type time = fs::last_write_time(file, ec);
            const uintmax_t size = ec ? 0 : fs::file_size(file, ec);
            if (ec || time != cached.Time || size != cached.Size)
                data = nullptr;
        }
        if (data)
            s_Hits.fetch_add(1, std::memory_order_relaxed);
        else
        {
            if (std::string why = AssetImportLog::CheckReadable(file); !why.empty())
            {
                Erase(key);
                if (unreadable)
                    *unreadable = std::move(why);
                return {};
            }
            CacheEntry entry;
            std::error_code ec;
            entry.Time = fs::last_write_time(file, ec);
            entry.Size = ec ? 0 : fs::file_size(file, ec);
            const std::string text = FileSystem::ReadTextFile(file);
            s_Reads.fetch_add(1, std::memory_order_relaxed);
            auto parsed = std::make_shared<Parsed>();
            parsed->Root = json::parse(text, nullptr, false);
            if (parsed->Root.is_discarded())
            {
                // Not JSON: the reader says why (and salvages it when asked to); nothing is kept.
                Erase(key);
                return SceneSerializer::DeserializeEntities(scene, text, options);
            }
            if (!SceneSerializerJson::PrepareEntitySet(parsed->Root, options, parsed->Migration))
            {
                Erase(key);
                return {};
            }
            data = parsed;
            entry.Data = std::move(parsed);
            std::lock_guard lock(s_Mutex);
            s_Entries[key] = std::move(entry);
        }
        if (options.Migration)
            *options.Migration = data->Migration;
        std::vector<Entity> made = SceneSerializerJson::ReadEntitySet(scene, data->Root, options);
        if (made.empty())
            Erase(key); // no entities: read again next time (and reported again by the caller's rules)
        return made;
    }

    void PrefabCache::SetCheckForChanges(bool check) { s_CheckForChanges.store(check, std::memory_order_relaxed); }

    bool PrefabCache::GetCheckForChanges() { return s_CheckForChanges.load(std::memory_order_relaxed); }

    void PrefabCache::Invalidate(const fs::path& file) { Erase(KeyOf(file)); }

    void PrefabCache::Clear()
    {
        std::lock_guard lock(s_Mutex);
        s_Entries.clear();
    }

    PrefabCache::Stats PrefabCache::GetStats()
    {
        Stats stats;
        stats.Reads = s_Reads.load(std::memory_order_relaxed);
        stats.Hits = s_Hits.load(std::memory_order_relaxed);
        std::lock_guard lock(s_Mutex);
        stats.Entries = s_Entries.size();
        return stats;
    }

    void PrefabCache::ResetStats()
    {
        s_Reads.store(0, std::memory_order_relaxed);
        s_Hits.store(0, std::memory_order_relaxed);
    }

}
