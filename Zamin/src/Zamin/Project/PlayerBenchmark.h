#pragma once

#include "Zamin/Core/Profiler.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Zamin {

    // ZaminPlayer --benchmark N: what to measure.
    struct PlayerBenchmarkSettings
    {
        static constexpr int DefaultWarmup = 60;
        // Exit codes of a benchmark run (0: the limits held).
        static constexpr int ExitLimitExceeded = 3; // a limit (--benchmark-max-avg / --benchmark-max-p95) was exceeded
        static constexpr int ExitFailed = 2;        // it could not run (the scene did not load ...)

        int Frames = 0;                 // the measured frames (0: no benchmark)
        int Warmup = DefaultWarmup;     // frames before them (shaders compiled, caches warm, the first scene settled)
        std::filesystem::path Output;   // the JSON report ("": only the log)
        double MaxAverageMs = 0.0;      // the measured frames' average may not be slower (0: no limit)
        double MaxP95Ms = 0.0;          // their 95th percentile may not be slower (0: no limit)

        bool IsEnabled() const { return Frames > 0; }
    };

    // The player's performance test (ZaminPlayer --benchmark N): the game runs as fast as it can (no splash, no
    // frame limit, no vsync unless asked), the first Warmup frames are skipped, then N frames are timed - each
    // one whole (update, drawing, the swap: from one frame's start to the next's) and the CPU time of the
    // game's own update and drawing calls in it. The result goes to the log and a JSON file; the player's exit
    // code says whether the limits held (Tests/player_bench.sh runs the samples under xvfb).
    class PlayerBenchmark
    {
    public:
        struct Result
        {
            // What was measured.
            std::string Game, GameVersion, Scene, Engine, Renderer;
            uint32_t Width = 0, Height = 0;
            int Warmup = 0;
            // The frames: whole frames and the update + drawing calls in them (ms).
            FrameTimeStats Frame, Update;
            double Seconds = 0.0;       // the measured frames' wall time
            double LoadSeconds = 0.0;   // from the start of the player to the first measured frame
            // The limits (0: none) and what broke them ("p95 7.20 ms > 6.00 ms").
            double MaxAverageMs = 0.0, MaxP95Ms = 0.0;
            bool Passed = true;
            std::vector<std::string> Failures;
            // Every measured frame's time (ms, in order): the JSON report's "Frames".
            std::vector<double> FrameTimes;
        };

        PlayerBenchmark() = default;
        explicit PlayerBenchmark(PlayerBenchmarkSettings settings);

        const PlayerBenchmarkSettings& GetSettings() const { return m_Settings; }
        bool IsEnabled() const { return m_Settings.IsEnabled(); }
        // One frame finished: its whole time and the game's update + drawing in it (ms). True once the last
        // measured frame is in.
        bool AddFrame(double frameMs, double updateMs);
        bool IsWarmingUp() const { return m_Seen < m_Settings.Warmup; }
        bool IsDone() const { return IsEnabled() && (int)m_Frames.size() >= m_Settings.Frames; }
        int GetFramesSeen() const { return m_Seen; }
        int GetMeasuredFrames() const { return (int)m_Frames.size(); }

        // The statistics so far and the limits checked (the caller fills in what was run: the game, the scene ...).
        Result GetResult() const;

        // The report: {"Game": ..., "Frame": {"Average": ..., "P95": ...}, "Update": {...}, "Frames": [...]}.
        static std::string ToJson(const Result& result);
        static bool WriteJson(const Result& result, const std::filesystem::path& path, std::string* error = nullptr);
        // Lines for the log: what ran, the frames, the update, the limits.
        static std::string Describe(const Result& result);

    private:
        PlayerBenchmarkSettings m_Settings;
        int m_Seen = 0;
        std::vector<double> m_Frames, m_Updates;
    };

}
