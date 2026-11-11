#pragma once
#include <glm/glm.hpp>
#include <cstdint>
namespace Zamin {
    // Bounded owner-thread progress watchdog. Intent/topology changes explicitly reset it;
    // waiting for a query or gameplay portal is not a physical stall.
    class NavRecovery
    {
    public:
        struct Settings
        {
            bool Enabled = false;
            float StallSeconds = .75f, CooldownSeconds = .5f, ProgressDistance = .05f;
            uint32_t MaxRetries = 3;
            bool Validate() const;
        };
        enum class Phase
        {
            Disabled,
            Idle,
            Moving,
            WaitingPath,
            Paused,
            Cooldown,
            Exhausted,
            Arrived,
            Invalid
        };
        enum class Action
        {
            None,
            Retry,
            Stop
        };
        struct Input
        {
            glm::vec2 Position{ 0 };
            float Delta = 0;
            bool HasIntent = false, CanMove = true, WaitingPath = false, Traversing = false, Arrived = false;
        };
        Action Observe(const Settings& settings, const Input& input);
        void Reset();
        Phase GetPhase() const { return m_Phase; }
        bool IsExhausted() const { return m_Phase == Phase::Exhausted || m_Phase == Phase::Invalid; }
        uint32_t GetRetries() const { return m_Retries; }
        float GetStalledSeconds() const { return m_Stalled; }
        float GetCooldown() const { return m_Cooldown; }
        static const char* Name(Phase phase);
    private:
        Phase m_Phase = Phase::Idle;
        uint32_t m_Retries = 0;
        glm::vec2 m_Checkpoint{ 0 };
        float m_Stalled = 0, m_Cooldown = 0;
        bool m_Seeded = false;
    };
}
