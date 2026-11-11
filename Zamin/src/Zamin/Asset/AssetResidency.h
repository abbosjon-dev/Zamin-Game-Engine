#pragma once
#include "Zamin/Asset/CacheResidency.h"
#include "Zamin/Core/MemoryStats.h"
#include <array>
namespace Zamin {
    class AssetResidency
    {
    public:
        enum Kind
        {
            Models,
            Materials,
            Meshes,
            Textures,
            Count
        };
        struct Snapshot
        {
            std::array<CacheResidencyStats, Count> Caches;
            MemoryStats::ProcessMemory Process;
            uint64_t RequestedGPUBytes = 0;
            size_t Entries = 0, Pinned = 0, Loading = 0;
        };
        // Main thread. Requested GPU bytes are tracked allocations, never physical driver VRAM.
        static Snapshot Inspect();
        // Parent caches first: their destruction can release children. Global eviction allowance.
        static size_t Trim(size_t targetEntriesPerCache, size_t maxEvictions);
        static const char* Name(size_t kind);
    };
}
