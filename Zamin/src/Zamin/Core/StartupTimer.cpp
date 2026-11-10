#include "Zamin/Core/StartupTimer.h"

#include "Zamin/Core/Log.h"
#include "Zamin/Core/MemoryStats.h"
#include "Zamin/Core/Profiler.h"

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <format>
#include <fstream>
#include <sstream>

#if defined(__linux__)
    #include <unistd.h>
#endif

namespace Zamin {

    namespace {
        // The engine library's static initialisation: the clock's zero.
        const std::chrono::steady_clock::time_point s_LibraryLoaded = std::chrono::steady_clock::now();
        // How old the process was then (read once, at the first use: the same moment within a few ms).
        double s_ProcessAgeAtLoad = -2.0;

        StartupTimer::Timings s_Timings;
        double s_CreateStart = 0.0, s_Created = 0.0, s_RunStart = 0.0;
        Profiler::ScopeToken s_Scope;
        bool s_ScopeOpen = false;
    }

    double StartupTimer::Now()
    {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - s_LibraryLoaded).count();
    }

    bool StartupTimer::ParseStartTicks(std::string_view stat, uint64_t& ticks)
    {
        // "pid (comm) state ppid ...": the fields after the last ')' start with field 3.
        const size_t close = stat.rfind(')');
        if (close == std::string_view::npos)
            return false;
        std::string_view rest = stat.substr(close + 1);
        int field = 2;
        size_t i = 0;
        while (i < rest.size())
        {
            while (i < rest.size() && rest[i] == ' ')
                i++;
            if (i >= rest.size())
                break;
            size_t end = i;
            while (end < rest.size() && rest[end] != ' ' && rest[end] != '\n')
                end++;
            field++;
            if (field == 22)
            {
                uint64_t value = 0;
                if (end == i)
                    return false;
                for (size_t k = i; k < end; k++)
                {
                    if (rest[k] < '0' || rest[k] > '9')
                        return false;
                    value = value * 10 + (uint64_t)(rest[k] - '0');
                }
                ticks = value;
                return true;
            }
            i = end;
        }
        return false;
    }

    double StartupTimer::ReadProcessAgeMs()
    {
#if defined(__linux__)
        std::ifstream file("/proc/self/stat");
        if (!file)
            return -1.0;
        std::stringstream text;
        text << file.rdbuf();
        uint64_t ticks = 0;
        const long hz = sysconf(_SC_CLK_TCK);
        timespec boot{};
        if (!ParseStartTicks(text.str(), ticks) || hz <= 0 || clock_gettime(CLOCK_BOOTTIME, &boot) != 0)
            return -1.0;
        const double nowMs = (double)boot.tv_sec * 1000.0 + (double)boot.tv_nsec / 1.0e6;
        const double startMs = (double)ticks * 1000.0 / (double)hz;
        return nowMs >= startMs ? nowMs - startMs : -1.0;
#else
        return -1.0;
#endif
    }

    void StartupTimer::OnApplicationCreating()
    {
        if (s_ProcessAgeAtLoad < -1.0)
        {
            const double age = ReadProcessAgeMs();
            s_ProcessAgeAtLoad = age >= 0.0 ? std::max(0.0, age - Now()) : -1.0;
        }
        s_Timings = {};
        s_Timings.ProcessMs = s_ProcessAgeAtLoad;
        s_CreateStart = Now();
        // Everything until the first frame goes under one scope (the profiler records before its first frame too).
        s_Scope = Profiler::BeginScope("Ishga tushish");
        s_ScopeOpen = true;
    }

    void StartupTimer::OnApplicationCreated()
    {
        s_Created = Now();
        s_Timings.EngineMs = s_Created - s_CreateStart;
    }

    void StartupTimer::OnRunStarted()
    {
        if (s_Timings.Done || s_RunStart > 0.0)
            return;
        s_RunStart = Now();
        s_Timings.LayersMs = s_RunStart - (s_Created > 0.0 ? s_Created : s_CreateStart);
        if (s_ScopeOpen)
        {
            Profiler::EndScope(s_Scope, (float)(s_RunStart - s_CreateStart));
            s_ScopeOpen = false;
        }
    }

    void StartupTimer::OnProfileReady()
    {
        const std::vector<Profiler::Entry>& entries = Profiler::GetLastFrame();
        if (s_Timings.Done)
        {
            // The first frame's tree (shaders the driver compiles at their first draw, font atlases, uploads ...).
            if (s_Timings.FirstFrameProfile.empty())
            {
                s_Timings.FirstFrameProfile = Profiler::FormatTree(entries, 1.0f);
                if (const char* profile = std::getenv("ZAMIN_STARTUP_PROFILE"); profile && *profile && *profile != '0')
                    ZM_CORE_INFO("Birinchi kadr profili:\n{}", s_Timings.FirstFrameProfile);
            }
            return;
        }
        if (!s_Timings.Profile.empty())
            return;
        s_Timings.Profile = Profiler::FormatTree(entries, 0.5f);
        std::string hot;
        for (int index : Profiler::GetHotspots(entries, 6))
        {
            const Profiler::Entry& e = entries[(size_t)index];
            hot += std::format("{} {:.1f} ms\n", e.Name, e.GetSelfMilliseconds());
        }
        s_Timings.Hotspots = std::move(hot);
    }

    void StartupTimer::OnFirstFrameShown()
    {
        if (s_Timings.Done)
            return;
        const double now = Now();
        s_Timings.FirstFrameMs = now - (s_RunStart > 0.0 ? s_RunStart : now);
        s_Timings.TotalMs = now;
        s_Timings.Done = true;
        const MemoryStats::ProcessMemory memory = MemoryStats::GetProcessMemory(0.0);
        s_Timings.ResidentBytes = memory.Resident;
        s_Timings.PeakResidentBytes = memory.PeakResident;
        ZM_CORE_INFO("Ishga tushish: birinchi kadr {}", Describe(s_Timings));
        if (const char* profile = std::getenv("ZAMIN_STARTUP_PROFILE"); profile && *profile && *profile != '0')
            ZM_CORE_INFO("Ishga tushish profili:\n{}Eng sekin joylar (o'z vaqti):\n{}", s_Timings.Profile, s_Timings.Hotspots);
    }

    const StartupTimer::Timings& StartupTimer::Get() { return s_Timings; }

    std::string StartupTimer::Describe(const Timings& t)
    {
        std::string out = std::format("{:.0f} ms (dvigatel {:.0f}, layer'lar {:.0f}, birinchi kadr {:.0f} ms", t.TotalMs, t.EngineMs, t.LayersMs,
                                      t.FirstFrameMs);
        if (t.ProcessMs >= 0.0)
            out += std::format("; jarayon boshidan ~{:.0f} ms", t.FromProcessStartMs());
        out += ")";
        if (t.ResidentBytes > 0)
            out += std::format(", RAM {} (eng ko'pi {})", MemoryStats::FormatBytes(t.ResidentBytes), MemoryStats::FormatBytes(t.PeakResidentBytes));
        return out;
    }

}
