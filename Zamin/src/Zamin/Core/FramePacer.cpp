#include "Zamin/Core/FramePacer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

namespace Zamin {

    FramePacingPlan FramePacingPlan::Choose(bool vsync, int targetFrameRate, int refreshRate)
    {
        FramePacingPlan plan;
        if (targetFrameRate <= 0)
            return plan;
        if (!vsync || refreshRate <= 0)
        {
            plan.LimiterRate = targetFrameRate;
            return plan;
        }
        if (targetFrameRate >= refreshRate)
            return plan; // vsync alone is at most the limit
        // A limit that divides the monitor's rate (within 2%: 59.94 Hz monitors report 59 or 60).
        const double ratio = (double)refreshRate / (double)targetFrameRate;
        const int blanks = (int)std::lround(ratio);
        if (blanks >= 2 && blanks <= 4 && std::abs(ratio - blanks) <= 0.02 * blanks)
        {
            plan.VSyncInterval = blanks;
            return plan;
        }
        plan.LimiterRate = targetFrameRate;
        return plan;
    }

    void FramePacer::SetTargetFrameRate(double fps)
    {
        const double period = fps > 0.0 ? 1.0 / fps : 0.0;
        if (period != m_Period)
        {
            m_Period = period;
            m_Started = false;
        }
    }

    double FramePacer::Schedule(double now)
    {
        if (m_Period <= 0.0)
        {
            m_Started = false;
            return now;
        }
        if (!m_Started)
        {
            // The first frame: the schedule starts with the next one, now (no wait).
            m_Started = true;
            m_Next = now;
            return now;
        }
        m_Next += m_Period;
        // Late by more than a whole period (a stall): a new schedule from now.
        if (now - m_Next > m_Period)
            m_Next = now;
        return m_Next;
    }

    double FramePacer::Now()
    {
        using clock = std::chrono::steady_clock;
        return std::chrono::duration<double>(clock::now().time_since_epoch()).count();
    }

    double FramePacer::Wait()
    {
        const double start = Now();
        const double deadline = Schedule(start);
        if (deadline <= start)
            return 0.0;
        if (deadline - start > SpinSeconds)
            std::this_thread::sleep_for(std::chrono::duration<double>(deadline - start - SpinSeconds));
        double now = Now();
        while (now < deadline)
        {
            std::this_thread::yield();
            now = Now();
        }
        return now - start;
    }

}
