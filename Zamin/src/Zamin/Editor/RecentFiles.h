#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace Zamin {

    // Most recently used files (the editor's recent projects and scenes): the newest first, each
    // path once, at most Capacity of them. Paths are compared as written after normalisation
    // ("a/./b" and "a/b" are the same file). Stored as JSON in the user's config directory.
    class RecentFileList
    {
    public:
        struct Entry
        {
            std::string Path;     // absolute
            std::string Owner;    // what it belongs to (a scene: its project file; "" for none)
            int64_t Time = 0;     // last opened, seconds since the Unix epoch
        };

        explicit RecentFileList(size_t capacity = 10) : m_Capacity(capacity == 0 ? 1 : capacity) {}

        // Puts the file first (moving it up when it is listed already); the oldest ones beyond
        // the capacity drop off. `time` < 0: now.
        void Add(std::string_view path, std::string_view owner = {}, int64_t time = -1);
        bool Remove(std::string_view path);
        // Every entry, or only those of `owner`.
        void Clear();
        size_t ClearOwner(std::string_view owner);
        // Drops the entries whose file `exists` says is gone; returns how many.
        size_t Prune(const std::function<bool(const std::string&)>& exists);
        // A file or folder moved: entries under `from` follow it to `to` (owners too).
        size_t Rename(std::string_view from, std::string_view to);

        bool Contains(std::string_view path) const;
        const std::vector<Entry>& GetEntries() const { return m_Entries; }
        // The entries of one owner, newest first (at most `limit`; 0 = all).
        std::vector<Entry> GetForOwner(std::string_view owner, size_t limit = 0) const;
        bool IsEmpty() const { return m_Entries.empty(); }
        size_t GetCapacity() const { return m_Capacity; }

        std::string ToJson() const;
        // Also reads the older format (a plain array of paths). False: not a list.
        bool FromJson(const std::string& text);
        bool Save(const std::filesystem::path& path) const;
        bool Load(const std::filesystem::path& path);

        // The form paths are compared in.
        static std::string Normalize(std::string_view path);

    private:
        std::vector<Entry> m_Entries;
        size_t m_Capacity;
    };

    // "hozirgina", "5 daqiqa oldin", "3 soat oldin", "kecha", "4 kun oldin", "2 hafta oldin", "2024-05-01".
    std::string FormatTimeAgo(int64_t then, int64_t now);
    // Seconds since the Unix epoch.
    int64_t GetUnixTime();

}
