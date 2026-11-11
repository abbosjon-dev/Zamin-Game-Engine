#pragma once
#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <limits>
#include <unordered_map>
#include <vector>

namespace Zamin {
    struct CacheResidencyStats
    {
        size_t Entries = 0, Pinned = 0, Loading = 0;
        uint64_t RequestedBytes = 0;
    };
    // Main-thread cache policy. Inspection never copies shared_ptrs (that would pin everything).
    // Last use is a cache acquisition, not a GPU draw. External owners and unfinished loads pin entries.
    template<class Key>
    class CacheRecency
    {
    public:
        void Touch(const Key& key) { m_Use[key] = ++m_Clock; }
        template<class Map, class Ready, class Bytes>
        CacheResidencyStats Inspect(const Map& cache, Ready ready, Bytes bytes) const
        {
            CacheResidencyStats out;
            out.Entries = cache.size();
            for (const auto& [key, value] : cache)
            {
                (void)key;
                const bool loading = value && !ready(value);
                out.Loading += loading;
                out.Pinned += value && (value.use_count() > 1 || loading);
                const uint64_t count = value ? bytes(value) : 0;
                out.RequestedBytes += std::min(count, UINT64_MAX - out.RequestedBytes);
            }
            return out;
        }
        // Returns keys in deterministic LRU order. No mutation, no waiting, no file or GPU operations.
        template<class Map, class Ready, class Bytes>
        std::vector<Key> Select(const Map& cache, size_t targetEntries, uint64_t targetBytes,
                                size_t maxEvictions, Ready ready, Bytes bytes)
        {
            std::erase_if(m_Use, [&](const auto& entry) { return !cache.contains(entry.first); });
            auto stats = Inspect(cache, ready, bytes);
            // A two-word sum keeps planning correct even if the reported (saturated) byte
            // counter overflows. Subtracting from a saturated total could otherwise stop early.
            uint64_t low = 0, high = 0;
            for (const auto& [key, value] : cache)
            {
                (void)key;
                const uint64_t amount = value ? bytes(value) : 0;
                if (amount > UINT64_MAX - low) ++high;
                low += amount;
            }
            std::vector<Key> candidates;
            if (stats.Entries <= targetEntries && high == 0 && low <= targetBytes) return candidates;
            for (const auto& [key, value] : cache)
                if (!value || (value.use_count() == 1 && ready(value))) candidates.push_back(key);
            auto age = [&](const Key& key) { auto it = m_Use.find(key); return it == m_Use.end() ? 0 : it->second; };
            std::sort(candidates.begin(), candidates.end(), [&](const Key& a, const Key& b) {
                return age(a) != age(b) ? age(a) < age(b) : a < b;
            });
            size_t count = 0;
            for (const auto& key : candidates)
            {
                if (count >= maxEvictions || (stats.Entries <= targetEntries && high == 0 && low <= targetBytes)) break;
                const auto& value = cache.at(key);
                const uint64_t amount = value ? bytes(value) : 0;
                if (low < amount) --high;
                low -= amount;
                --stats.Entries;
                ++count;
            }
            candidates.resize(count);
            return candidates;
        }
    private:
        uint64_t m_Clock = 0;
        std::unordered_map<Key, uint64_t> m_Use;
    };
    // Stateful high/low-watermark gate. A zero limit disables that signal. Missing RSS is never
    // treated as a measurement of zero: GPU pressure can still be evaluated independently.
    class ResidencyPressure
    {
    public:
        bool Update(uint64_t rss, bool rssValid, uint64_t gpu, uint64_t rssLimit, uint64_t gpuLimit, double resumeRatio);
        bool IsBlocked() const { return m_Blocked; }
    private:
        bool m_Blocked = false;
    };
}
