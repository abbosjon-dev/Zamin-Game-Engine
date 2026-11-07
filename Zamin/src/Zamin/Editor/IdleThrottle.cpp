#include "Zamin/Editor/IdleThrottle.h"

#include <algorithm>
#include <cmath>

namespace Zamin {

    void IdleThrottleSettings::Sanitize()
    {
        auto fps = [](float v, float fallback) { return std::isfinite(v) ? std::clamp(v, MinFPS, MaxFPS) : fallback; };
        UnfocusedFPS = fps(UnfocusedFPS, 10.0f);
        IdleFPS = fps(IdleFPS, 30.0f);
        IdleDelay = std::isfinite(IdleDelay) ? std::clamp(IdleDelay, MinDelay, MaxDelay) : 1.0f;
    }

    FrameLimit IdleThrottle::Decide(const IdleThrottleSettings& settings, const IdleThrottleState& state)
    {
        if (!settings.Enabled || state.Automation || state.Running || state.PendingWork)
            return {};
        if (!state.Focused)
            return { FrameLimitReason::Unfocused, std::clamp(settings.UnfocusedFPS, IdleThrottleSettings::MinFPS, IdleThrottleSettings::MaxFPS) };
        if (state.Animating || state.SecondsSinceInput < (double)settings.IdleDelay)
            return {};
        return { FrameLimitReason::Idle, std::clamp(settings.IdleFPS, IdleThrottleSettings::MinFPS, IdleThrottleSettings::MaxFPS) };
    }

    double IdleThrottle::GetWaitSeconds(const FrameLimit& limit, double frameSeconds)
    {
        if (!limit.IsLimited())
            return 0.0;
        return std::max(0.0, limit.GetFrameSeconds() - std::max(0.0, frameSeconds));
    }

    const char* IdleThrottle::GetReasonName(FrameLimitReason reason)
    {
        switch (reason)
        {
            case FrameLimitReason::Unfocused: return "fokussiz";
            case FrameLimitReason::Idle: return "bo'sh turibdi";
            default: return "cheklanmagan";
        }
    }

}
