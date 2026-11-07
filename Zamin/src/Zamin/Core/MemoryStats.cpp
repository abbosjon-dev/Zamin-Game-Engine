#include "Zamin/Core/MemoryStats.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <format>
#include <fstream>
#include <mutex>
#include <sstream>

#if defined(__linux__)
    #include <unistd.h>
#endif

namespace Zamin {

    namespace {
        struct Counters
        {
            std::atomic<uint64_t> Bytes{ 0 };
            std::atomic<uint64_t> Count{ 0 };
            std::atomic<uint64_t> Peak{ 0 };
        };
        std::array<Counters, MemoryCategoryCount> s_Counters;

        Counters* Find(MemoryCategory category)
        {
            const size_t index = (size_t)category;
            return index < MemoryCategoryCount ? &s_Counters[index] : nullptr;
        }

        void RaisePeak(Counters& c, uint64_t value)
        {
            uint64_t peak = c.Peak.load(std::memory_order_relaxed);
            while (value > peak && !c.Peak.compare_exchange_weak(peak, value, std::memory_order_relaxed))
            {
            }
        }

        // Subtracts without going below 0.
        void Subtract(std::atomic<uint64_t>& value, uint64_t amount)
        {
            uint64_t current = value.load(std::memory_order_relaxed);
            while (!value.compare_exchange_weak(current, current - std::min(current, amount), std::memory_order_relaxed))
            {
            }
        }

        std::string ReadSmallFile(const char* path)
        {
            std::ifstream file(path);
            if (!file)
                return {};
            std::stringstream text;
            text << file.rdbuf();
            return text.str();
        }
    }

    // ------------------------------------------------------------------ categories
    void MemoryStats::Add(MemoryCategory category, uint64_t bytes, uint64_t count)
    {
        Counters* c = Find(category);
        if (!c)
            return;
        const uint64_t now = c->Bytes.fetch_add(bytes, std::memory_order_relaxed) + bytes;
        c->Count.fetch_add(count, std::memory_order_relaxed);
        RaisePeak(*c, now);
    }

    void MemoryStats::Remove(MemoryCategory category, uint64_t bytes, uint64_t count)
    {
        Counters* c = Find(category);
        if (!c)
            return;
        Subtract(c->Bytes, bytes);
        Subtract(c->Count, count);
    }

    void MemoryStats::Set(MemoryCategory category, uint64_t bytes, uint64_t count)
    {
        Counters* c = Find(category);
        if (!c)
            return;
        c->Bytes.store(bytes, std::memory_order_relaxed);
        c->Count.store(count, std::memory_order_relaxed);
        RaisePeak(*c, bytes);
    }

    MemoryStats::Category MemoryStats::Get(MemoryCategory category)
    {
        Category out;
        if (const Counters* c = Find(category))
        {
            out.Bytes = c->Bytes.load(std::memory_order_relaxed);
            out.Count = c->Count.load(std::memory_order_relaxed);
            out.PeakBytes = std::max(out.Bytes, c->Peak.load(std::memory_order_relaxed));
        }
        return out;
    }

    uint64_t MemoryStats::GetTrackedTotal()
    {
        uint64_t total = 0;
        for (const Counters& c : s_Counters)
            total += c.Bytes.load(std::memory_order_relaxed);
        return total;
    }

    void MemoryStats::ResetPeaks()
    {
        for (Counters& c : s_Counters)
            c.Peak.store(c.Bytes.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }

    const char* MemoryStats::GetName(MemoryCategory category)
    {
        switch (category)
        {
            case MemoryCategory::Textures: return "Teksturalar";
            case MemoryCategory::RenderTargets: return "Render buferlari";
            case MemoryCategory::Meshes: return "GPU buferlari";
            case MemoryCategory::Audio: return "Audio";
            case MemoryCategory::Scenes: return "Sahnalar";
            case MemoryCategory::Particles: return "Particle'lar";
            case MemoryCategory::Animation: return "Animatsiya keshi";
            default: return "?";
        }
    }

    const char* MemoryStats::GetDescription(MemoryCategory category)
    {
        switch (category)
        {
            case MemoryCategory::Textures:
                return "GPU'dagi teksturalar (rasmlar, shrift atlaslari, sprite'lar): eni x bo'yi x piksel hajmi, mipmap'lari bilan";
            case MemoryCategory::RenderTargets:
                return "Framebuffer'lar (viewport, post-process, soyalar, kamera tasvirlari): rang va chuqurlik qatlamlari, MSAA bilan";
            case MemoryCategory::Meshes:
                return "Vertex va indeks buferlari: meshlar, modellar, 2D partiyalar, particle instance buferlari";
            case MemoryCategory::Audio:
                return "Xotiraga ochilgan (decode qilingan) ovoz fayllari - oqim (stream) bilan o'qiladiganlar kirmaydi";
            case MemoryCategory::Scenes:
                return "Ochiq sahnalarning komponent ma'lumotlari (taxminiy: komponentlar hajmi, skelet pozalari)";
            case MemoryCategory::Particles:
                return "Particle tizimlarining zarrachalari, izlari va hodisalari";
            case MemoryCategory::Animation:
                return "Animator'larning poza keshi (CachePoses: kliplar oldindan hisoblangan pozalar)";
            default:
                return "";
        }
    }

    // ------------------------------------------------------------------ the process
    bool MemoryStats::ParseStatm(std::string_view text, uint64_t pageSize, ProcessMemory& out)
    {
        uint64_t values[3] = {};
        const char* p = text.data();
        const char* end = text.data() + text.size();
        for (uint64_t& value : values)
        {
            while (p < end && (*p == ' ' || *p == '\t'))
                p++;
            const auto [next, error] = std::from_chars(p, end, value);
            if (error != std::errc() || next == p)
                return false;
            p = next;
        }
        out.Virtual = values[0] * pageSize;
        out.Resident = values[1] * pageSize;
        out.Shared = values[2] * pageSize;
        out.Valid = true;
        return true;
    }

    uint64_t MemoryStats::ParseStatusBytes(std::string_view status, std::string_view key)
    {
        size_t at = 0;
        while ((at = status.find(key, at)) != std::string_view::npos)
        {
            // The key at the start of a line, followed by ':'.
            if ((at == 0 || status[at - 1] == '\n') && at + key.size() < status.size() && status[at + key.size()] == ':')
            {
                const char* p = status.data() + at + key.size() + 1;
                const char* end = status.data() + status.size();
                while (p < end && (*p == ' ' || *p == '\t'))
                    p++;
                uint64_t kilobytes = 0;
                const auto [next, error] = std::from_chars(p, end, kilobytes);
                return error == std::errc() && next != p ? kilobytes * 1024 : 0;
            }
            at += key.size();
        }
        return 0;
    }

    MemoryStats::ProcessMemory MemoryStats::GetProcessMemory(double maxAgeSeconds)
    {
        static std::mutex mutex;
        static ProcessMemory cached;
        static std::chrono::steady_clock::time_point readAt;
        static bool read = false;
        std::lock_guard lock(mutex);
        const auto now = std::chrono::steady_clock::now();
        if (read && std::chrono::duration<double>(now - readAt).count() < maxAgeSeconds)
            return cached;
        ProcessMemory memory;
#if defined(__linux__)
        const long page = sysconf(_SC_PAGESIZE);
        if (ParseStatm(ReadSmallFile("/proc/self/statm"), page > 0 ? (uint64_t)page : 4096u, memory))
            memory.PeakResident = std::max(memory.Resident, ParseStatusBytes(ReadSmallFile("/proc/self/status"), "VmHWM"));
#endif
        cached = memory;
        readAt = now;
        read = true;
        return cached;
    }

    std::string MemoryStats::FormatBytes(uint64_t bytes)
    {
        if (bytes < 1024)
            return std::format("{} B", bytes);
        const double kb = (double)bytes / 1024.0;
        if (kb < 1024.0)
            return std::format("{:.1f} KB", kb);
        const double mb = kb / 1024.0;
        if (mb < 1024.0)
            return std::format("{:.2f} MB", mb);
        return std::format("{:.2f} GB", mb / 1024.0);
    }

    // ------------------------------------------------------------------ TrackedMemory
    TrackedMemory& TrackedMemory::operator=(TrackedMemory&& other) noexcept
    {
        if (this != &other)
        {
            Set(0);
            m_Category = other.m_Category;
            m_Bytes = other.m_Bytes;
            other.m_Bytes = 0;
        }
        return *this;
    }

    void TrackedMemory::Set(uint64_t bytes)
    {
        if (bytes == m_Bytes)
            return;
        if (bytes > m_Bytes)
            MemoryStats::Add(m_Category, bytes - m_Bytes, m_Bytes == 0 ? 1 : 0);
        else
            MemoryStats::Remove(m_Category, m_Bytes - bytes, bytes == 0 ? 1 : 0);
        m_Bytes = bytes;
    }

}
