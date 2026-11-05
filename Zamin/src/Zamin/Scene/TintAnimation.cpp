#include "Zamin/Scene/TintAnimation.h"

#include <algorithm>
#include <cmath>

namespace Zamin {

    glm::vec4 TintAnimation::EvaluateGradient(const std::vector<TintKey>& keys, float t)
    {
        if (keys.empty())
            return glm::vec4(1.0f);
        if (!std::isfinite(t))
            t = 0.0f;
        // The keys before / after t (sorted or not).
        const TintKey* before = nullptr;
        const TintKey* after = nullptr;
        for (const TintKey& k : keys)
        {
            if (k.Time <= t && (!before || k.Time >= before->Time))
                before = &k;
            if (k.Time >= t && (!after || k.Time < after->Time))
                after = &k;
        }
        if (!before) return after->Color;
        if (!after) return before->Color;
        float span = after->Time - before->Time;
        if (span <= 1e-6f)
            return after->Color;
        return glm::mix(before->Color, after->Color, (t - before->Time) / span);
    }

    float TintAnimation::GetPhase(TintPlayMode mode, Ease ease, float duration, int cycles, float elapsed)
    {
        elapsed = std::isfinite(elapsed) ? std::max(0.0f, elapsed) : 0.0f;
        if (duration <= 1e-6f || !std::isfinite(duration))
            return mode == TintPlayMode::PingPong ? 0.0f : 1.0f;
        const float cycle = mode == TintPlayMode::PingPong ? duration * 2.0f : duration;
        if (mode == TintPlayMode::Once || (cycles > 0 && elapsed >= cycle * (float)cycles))
        {
            if (mode == TintPlayMode::Once)
                return ApplyEase(ease, std::min(1.0f, elapsed / duration));
            return mode == TintPlayMode::PingPong ? 0.0f : 1.0f; // finished: where the last cycle ends
        }
        float local = std::fmod(elapsed, cycle) / duration; // 0..1 (Loop), 0..2 (PingPong)
        if (mode == TintPlayMode::PingPong && local > 1.0f)
            local = 2.0f - local;
        return ApplyEase(ease, local);
    }

    bool TintAnimation::IsFinished(TintPlayMode mode, float duration, int cycles, float elapsed)
    {
        if (mode == TintPlayMode::Once)
            return elapsed >= duration;
        if (cycles <= 0)
            return false;
        const float cycle = mode == TintPlayMode::PingPong ? duration * 2.0f : duration;
        return elapsed >= cycle * (float)cycles;
    }

    void TintAnimation::SortKeys(std::vector<TintKey>& keys)
    {
        for (TintKey& k : keys)
            k.Time = std::isfinite(k.Time) ? std::clamp(k.Time, 0.0f, 1.0f) : 0.0f;
        std::stable_sort(keys.begin(), keys.end(), [](const TintKey& a, const TintKey& b) { return a.Time < b.Time; });
    }

    const char* TintAnimation::ModeToString(TintPlayMode mode)
    {
        switch (mode)
        {
            case TintPlayMode::Once: return "Once";
            case TintPlayMode::Loop: return "Loop";
            case TintPlayMode::PingPong: return "PingPong";
        }
        return "PingPong";
    }

    std::optional<TintPlayMode> TintAnimation::ModeFromString(std::string_view name)
    {
        if (name == "Once") return TintPlayMode::Once;
        if (name == "Loop") return TintPlayMode::Loop;
        if (name == "PingPong") return TintPlayMode::PingPong;
        return std::nullopt;
    }

}
