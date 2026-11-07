#pragma once

namespace Zamin {

    // How a game's frames are paced: vsync and its frame rate limit (PlayerSettings::TargetFrameRate,
    // ZaminPlayer --fps). Choose picks it:
    //   - no limit: vsync as it is (1 blank per frame), or nothing;
    //   - vsync and a limit that divides the monitor's rate (30 or 20 on 60 Hz, 72 on 144 Hz): vsync waits
    //     for that many blanks per frame - the frames are exactly even, the sleep limiter is not needed;
    //   - vsync and a limit at or above the monitor's rate: vsync alone already holds it;
    //   - vsync and any other limit (50 on 60 Hz), vsync off, or an unknown refresh rate: the sleep
    //     limiter (FramePacer) holds the limit (with vsync the frames then alternate between one and two
    //     blanks - not perfectly even).
    struct FramePacingPlan
    {
        int VSyncInterval = 1; // vertical blanks per frame (when vsync is on)
        int LimiterRate = 0;   // the sleep limiter's frames per second (0: off)

        bool operator==(const FramePacingPlan&) const = default;

        static FramePacingPlan Choose(bool vsync, int targetFrameRate, int refreshRate);
    };

    // Holds a fixed frame rate with a sleep and a short spin. The frames' start times follow a
    // schedule one period apart (not "a period after the last frame ended"), so the average rate is
    // exact although single sleeps overshoot a little: a frame that ends a bit late is made up by the
    // next one. A stall longer than a period (loading, a window drag) starts a new schedule instead of
    // a burst of fast frames to catch up.
    class FramePacer
    {
    public:
        // Frames per second; 0 (or less): no limit (the schedule starts again).
        void SetTargetFrameRate(double fps);
        double GetTargetFrameRate() const { return m_Period > 0.0 ? 1.0 / m_Period : 0.0; }
        double GetPeriod() const { return m_Period; }
        void Reset() { m_Started = false; }

        // The schedule (seconds on any steady clock): called when a frame has ended at `now`; returns
        // the time the next frame should start (`now` or earlier: at once). The first call starts it.
        double Schedule(double now);
        // Waits until the next frame's time: asleep until SpinSeconds before it, then spinning (the
        // system's sleep is not that exact). Returns the seconds it waited.
        double Wait();
        // Seconds on the steady clock Wait uses.
        static double Now();

        static constexpr double SpinSeconds = 0.0015;

    private:
        double m_Period = 0.0;
        double m_Next = 0.0; // the scheduled start of the current frame
        bool m_Started = false;
    };

}
