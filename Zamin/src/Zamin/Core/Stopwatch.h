#pragma once

// Measuring real (wall-clock) time: Stopwatch (start / stop / laps, on the steady clock: never
// jumps when the system time is changed) and ScopedLogTimer (how long a scope took, logged or
// written to a variable when it ends). For game time (the timestep, pausing, time scale) use Timer
// (Zamin/Core/Timer.h) or the scripts' After / Every timers; for the frame's breakdown in the
// profiler ZM_PROFILE_SCOPE.
//
//     Stopwatch watch;                         // running from here
//     LoadLevel();
//     double load = watch.Lap();               // ms since the start
//     BuildNavGrid();
//     double grid = watch.Lap();               // ms since the previous lap
//     ZM_INFO("Hammasi: {}", watch.Format());  // "12.4 ms"
//
//     { ZM_LOG_SCOPE_TIME("Navigatsiya"); BuildNavGrid(); }   // "Navigatsiya: 3.21 ms"

#include "Zamin/Core/Log.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace Zamin {

    class Stopwatch
    {
    public:
        using Clock = std::chrono::steady_clock;
        using Duration = Clock::duration;

        // Running at once (false: stopped at 0 until Start).
        explicit Stopwatch(bool start = true)
        {
            if (start)
                Start();
        }
        static Stopwatch StartNew() { return Stopwatch(true); }

        // Goes on from the time already measured (nothing when it already runs).
        void Start()
        {
            if (m_Running)
                return;
            m_Started = Clock::now();
            m_Running = true;
        }
        // The time stops (Elapsed stays where it is until Start / Reset).
        void Stop()
        {
            if (!m_Running)
                return;
            m_Accumulated += Clock::now() - m_Started;
            m_Running = false;
        }
        // Stopped at 0, the laps forgotten.
        void Reset()
        {
            m_Accumulated = Duration::zero();
            m_LapMark = Duration::zero();
            m_Running = false;
            m_Laps.clear();
        }
        // 0 and running.
        void Restart()
        {
            Reset();
            Start();
        }
        bool IsRunning() const { return m_Running; }

        Duration Elapsed() const { return m_Running ? m_Accumulated + (Clock::now() - m_Started) : m_Accumulated; }
        double ElapsedSeconds() const { return std::chrono::duration<double>(Elapsed()).count(); }
        double ElapsedMilliseconds() const { return std::chrono::duration<double, std::milli>(Elapsed()).count(); }
        double ElapsedMicroseconds() const { return std::chrono::duration<double, std::micro>(Elapsed()).count(); }
        int64_t ElapsedNanoseconds() const { return std::chrono::duration_cast<std::chrono::nanoseconds>(Elapsed()).count(); }

        // A lap: the milliseconds since the previous Lap (the first: since the start), also kept in GetLaps().
        double Lap()
        {
            const Duration now = Elapsed();
            const double lap = std::chrono::duration<double, std::milli>(now - m_LapMark).count();
            m_LapMark = now;
            m_Laps.push_back(lap);
            return lap;
        }
        const std::vector<double>& GetLaps() const { return m_Laps; }
        // The milliseconds since the last Lap (or the start), without starting a new one.
        double GetCurrentLapMilliseconds() const { return std::chrono::duration<double, std::milli>(Elapsed() - m_LapMark).count(); }

        // The elapsed time for people: "0.42 ms", "850 ms", "12.4 s", "3 daq 05 s" (StringUtils::FormatDuration).
        std::string Format() const;

    private:
        Clock::time_point m_Started{};
        Duration m_Accumulated = Duration::zero();
        Duration m_LapMark = Duration::zero();   // Elapsed() at the last Lap
        bool m_Running = false;
        std::vector<double> m_Laps;
    };

    // How long its scope took: logged when it ends ("<name>: 3.21 ms"), only when it took at least
    // `thresholdMilliseconds` (slow cases only: ScopedLogTimer t("Yuklash", 16.0)) - or written to a
    // variable instead of the log.
    class ScopedLogTimer
    {
    public:
        explicit ScopedLogTimer(std::string name, double thresholdMilliseconds = 0.0, LogLevel level = LogLevel::Info, bool fromClient = false)
            : m_Name(std::move(name)), m_Threshold(thresholdMilliseconds), m_Level(level), m_FromClient(fromClient)
        {
        }
        // The milliseconds go to `*outMilliseconds` when the scope ends (nothing is logged).
        explicit ScopedLogTimer(double* outMilliseconds) : m_Output(outMilliseconds) {}
        ~ScopedLogTimer();
        ScopedLogTimer(const ScopedLogTimer&) = delete;
        ScopedLogTimer& operator=(const ScopedLogTimer&) = delete;

        double ElapsedMilliseconds() const { return m_Watch.ElapsedMilliseconds(); }
        // Nothing is logged / written when the scope ends.
        void Cancel() { m_Cancelled = true; }

    private:
        Stopwatch m_Watch;
        std::string m_Name;
        double m_Threshold = 0.0;
        LogLevel m_Level = LogLevel::Info;
        bool m_FromClient = false;
        double* m_Output = nullptr;
        bool m_Cancelled = false;
    };

}

#define ZM_STOPWATCH_CONCAT_INNER(a, b) a##b
#define ZM_STOPWATCH_CONCAT(a, b) ZM_STOPWATCH_CONCAT_INNER(a, b)
// Logs how long the rest of the scope takes: engine code (ZM_CORE_*) and game code (ZM_*). The
// _OVER forms only log a scope that took at least `ms` milliseconds (as a warning: it was slow).
#define ZM_CORE_LOG_SCOPE_TIME(name) ::Zamin::ScopedLogTimer ZM_STOPWATCH_CONCAT(zmScopedLogTimer, __LINE__)(name, 0.0, ::Zamin::LogLevel::Info, false)
#define ZM_LOG_SCOPE_TIME(name)      ::Zamin::ScopedLogTimer ZM_STOPWATCH_CONCAT(zmScopedLogTimer, __LINE__)(name, 0.0, ::Zamin::LogLevel::Info, true)
#define ZM_CORE_LOG_SCOPE_TIME_OVER(name, ms) \
    ::Zamin::ScopedLogTimer ZM_STOPWATCH_CONCAT(zmScopedLogTimer, __LINE__)(name, ms, ::Zamin::LogLevel::Warn, false)
#define ZM_LOG_SCOPE_TIME_OVER(name, ms) \
    ::Zamin::ScopedLogTimer ZM_STOPWATCH_CONCAT(zmScopedLogTimer, __LINE__)(name, ms, ::Zamin::LogLevel::Warn, true)
