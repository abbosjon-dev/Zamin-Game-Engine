#pragma once

#include <cstdint>
#include <string>
#include <string_view>

// What the engine holds in memory, by category - the Stats panel's "Xotira" section and the
// viewport's statistics overlay. Each module counts its own: textures and render targets (width x
// height x pixel size on the GPU, mipmaps included), vertex / index buffers, decoded sounds, the
// scenes' component data, particles and the animation pose caches. The numbers are what the engine
// asked for (a driver may pad, malloc adds its own), so the process's resident memory (RSS, from
// /proc/self/statm) is shown next to them.
//
//   TrackedMemory m_Memory{ MemoryCategory::Textures };  // a member of the owner
//   m_Memory.Set(width * height * 4);                     // after (re)allocating; gone with the owner
namespace Zamin {

    enum class MemoryCategory : uint8_t { Textures = 0, RenderTargets, Meshes, Audio, Scenes, Particles, Animation, Count };
    constexpr size_t MemoryCategoryCount = (size_t)MemoryCategory::Count;

    class MemoryStats
    {
    public:
        struct Category
        {
            uint64_t Bytes = 0;
            uint64_t Count = 0;     // objects holding it (textures, buffers, scenes, sounds ...)
            uint64_t PeakBytes = 0; // the most it has been (since the start / ResetPeaks)
        };

        // A module's memory going up / down (thread-safe). Removing more than there is stops at 0.
        static void Add(MemoryCategory category, uint64_t bytes, uint64_t count = 1);
        static void Remove(MemoryCategory category, uint64_t bytes, uint64_t count = 1);
        // For a module that recounts a whole category (the sounds decoded now): the new totals.
        static void Set(MemoryCategory category, uint64_t bytes, uint64_t count);
        static Category Get(MemoryCategory category);
        // Every category together.
        static uint64_t GetTrackedTotal();
        static void ResetPeaks();
        // "Teksturalar", "Render buferlari" ...
        static const char* GetName(MemoryCategory category);
        // What a category holds, for a tooltip.
        static const char* GetDescription(MemoryCategory category);

        // ---- the process as the system sees it (Linux; Valid false elsewhere)
        struct ProcessMemory
        {
            bool Valid = false;
            uint64_t Resident = 0;     // in RAM now (RSS)
            uint64_t Virtual = 0;      // the address space it has reserved
            uint64_t Shared = 0;       // resident pages shared with other processes (libraries, the GPU driver's)
            uint64_t PeakResident = 0; // the most RSS so far (VmHWM)
        };
        // Read at most every `maxAgeSeconds` (0: now) - the Stats panel and the overlay ask every frame.
        static ProcessMemory GetProcessMemory(double maxAgeSeconds = 0.5);
        // /proc/self/statm ("size resident shared ..." in pages) -> bytes; false for anything else.
        static bool ParseStatm(std::string_view text, uint64_t pageSize, ProcessMemory& out);
        // A "Key:   1234 kB" line of /proc/self/status, in bytes (0 when missing).
        static uint64_t ParseStatusBytes(std::string_view status, std::string_view key);

        // "512 B", "12.3 KB", "4.50 MB", "1.25 GB".
        static std::string FormatBytes(uint64_t bytes);
    };

    // Memory that belongs to an object (a texture, a buffer, a scene): Set changes how much, the
    // destructor gives it back. While it holds more than 0 bytes it counts as one object of its
    // category. Not copyable (a copy would count it twice); moves with its owner.
    class TrackedMemory
    {
    public:
        explicit TrackedMemory(MemoryCategory category) : m_Category(category) {}
        ~TrackedMemory() { Set(0); }
        TrackedMemory(const TrackedMemory&) = delete;
        TrackedMemory& operator=(const TrackedMemory&) = delete;
        TrackedMemory(TrackedMemory&& other) noexcept : m_Category(other.m_Category), m_Bytes(other.m_Bytes) { other.m_Bytes = 0; }
        TrackedMemory& operator=(TrackedMemory&& other) noexcept;

        void Set(uint64_t bytes);
        uint64_t Get() const { return m_Bytes; }
        MemoryCategory GetCategory() const { return m_Category; }

    private:
        MemoryCategory m_Category;
        uint64_t m_Bytes = 0;
    };

}
