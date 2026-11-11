#include "Zamin/AI/NavRecovery.h"
#include <algorithm>
#include <cmath>
namespace Zamin {
    bool NavRecovery::Settings::Validate() const
    {
        return std::isfinite(StallSeconds) && StallSeconds >= .1f && StallSeconds <= 30 &&
               std::isfinite(CooldownSeconds) && CooldownSeconds >= .1f && CooldownSeconds <= 30 &&
               std::isfinite(ProgressDistance) && ProgressDistance >= .001f && ProgressDistance <= 10 && MaxRetries >= 1 && MaxRetries <= 16;
    }
    void NavRecovery::Reset() { *this = NavRecovery{}; }
    const char* NavRecovery::Name(Phase phase)
    {
        switch (phase)
        {
            case Phase::Disabled:    return "Disabled";
            case Phase::Idle:        return "Idle";
            case Phase::Moving:      return "Moving";
            case Phase::WaitingPath: return "WaitingPath";
            case Phase::Paused:      return "Paused";
            case Phase::Cooldown:    return "Cooldown";
            case Phase::Exhausted:   return "Exhausted";
            case Phase::Arrived:     return "Arrived";
            default:                 return "Invalid";
        }
    }
    NavRecovery::Action NavRecovery::Observe(const Settings& settings, const Input& in)
    {
        if (!settings.Enabled)
        {
            Reset();
            m_Phase = Phase::Disabled;
            return Action::None;
        }
        if (!settings.Validate() || !std::isfinite(in.Position.x) || !std::isfinite(in.Position.y) || !std::isfinite(in.Delta) || in.Delta < 0)
        {
            m_Phase = Phase::Invalid;
            return Action::Stop;
        }
        if (!in.HasIntent)
        {
            Reset();
            return Action::None;
        }
        if (in.Arrived)
        {
            Reset();
            m_Phase = Phase::Arrived;
            return Action::None;
        }
        if (IsExhausted()) return Action::Stop;
        const float dt = std::min(in.Delta, .1f);
        m_Cooldown = std::max(0.f, m_Cooldown - dt);
        if (in.Traversing || !in.CanMove)
        {
            m_Phase = Phase::Paused;
            m_Stalled = 0;
            m_Seeded = false;
            return Action::None;
        }
        if (in.WaitingPath)
        {
            m_Phase = Phase::WaitingPath;
            m_Stalled = 0;
            m_Seeded = false;
            return Action::None;
        }
        if (!m_Seeded || glm::length(in.Position - m_Checkpoint) >= settings.ProgressDistance)
        {
            const bool progress = m_Seeded;
            m_Checkpoint = in.Position;
            m_Seeded = true;
            m_Stalled = 0;
            if (progress) m_Retries = 0;
            m_Phase = m_Cooldown > 0 ? Phase::Cooldown : Phase::Moving;
            return Action::None;
        }
        m_Phase = m_Cooldown > 0 ? Phase::Cooldown : Phase::Moving;
        // The cooldown and the stall window are separate: a retry never creates a per-frame loop.
        if (m_Cooldown > 0) return Action::None;
        m_Stalled += dt;
        if (m_Stalled < settings.StallSeconds) return Action::None;
        if (m_Retries >= settings.MaxRetries)
        {
            m_Phase = Phase::Exhausted;
            return Action::Stop;
        }
        ++m_Retries;
        m_Stalled = 0;
        m_Cooldown = settings.CooldownSeconds;
        m_Phase = Phase::Cooldown;
        return Action::Retry;
    }
}
