#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace Zamin {

    // How long a program took to show its first frame (task 495): the editor and the player both
    // log it ("Ishga tushish: birinchi kadr ... ms"), the editor's Statistics panel shows it, and
    // Tests/startup_bench.sh compares builds with it.
    //
    // The phases are measured from the moment the engine library was loaded (its static
    // initialisation, just before main); the time the system needed before that (exec, the dynamic
    // linker loading and relocating the libraries) is read from /proc/self/stat (Linux, 10 ms steps).
    // Application calls the On* hooks; the profiler's scopes opened during start-up (window, renderer,
    // audio, the layers' OnAttach: project, scene, panels ...) become the start-up profile, printed
    // with ZAMIN_STARTUP_PROFILE=1.
    class StartupTimer
    {
    public:
        struct Timings
        {
            bool Done = false;          // the first frame has been shown
            double ProcessMs = -1.0;    // process start -> engine library loaded (-1: unknown)
            double EngineMs = 0.0;      // Application's constructor: window, GL context, renderer, audio, jobs, ImGui
            double LayersMs = 0.0;      // from there to the main loop: the layers attached (project, scene, editor)
            double FirstFrameMs = 0.0;  // the first frame itself, up to its swap
            double TotalMs = 0.0;       // engine library loaded -> first frame on screen
            std::string Profile;        // the start-up's profiler tree (indented lines)
            std::string Hotspots;       // its slowest scopes (their own time), one per line
            std::string FirstFrameProfile; // the first frame's profiler tree (once the second frame began)
            uint64_t ResidentBytes = 0;     // the process's RSS when the first frame was shown (0: unknown)
            uint64_t PeakResidentBytes = 0; // the most RSS until then

            // Process start -> first frame (TotalMs plus ProcessMs when that is known).
            double FromProcessStartMs() const { return TotalMs + (ProcessMs > 0.0 ? ProcessMs : 0.0); }
        };

        static void OnApplicationCreating();
        static void OnApplicationCreated();
        static void OnRunStarted();     // before the first frame's Profiler::BeginFrame
        static void OnProfileReady();   // right after it: the start-up's scopes are the profiler's last frame
        static void OnFirstFrameShown(); // after the first swap: logs the result

        static const Timings& Get();
        // "1234 ms (dvigatel 300, layer'lar 800, birinchi kadr 134)".
        static std::string Describe(const Timings& timings);

        // The start time (field 22, clock ticks since boot) from the text of /proc/<pid>/stat: false
        // for anything else. The program's name (field 2) can hold spaces and parentheses.
        static bool ParseStartTicks(std::string_view stat, uint64_t& ticks);
        // Process start -> now, in ms, from /proc/self/stat and the boot clock; -1 where unknown.
        static double ReadProcessAgeMs();
        // The time since the engine library was loaded, in ms (the clock every phase uses).
        static double Now();
    };

}
