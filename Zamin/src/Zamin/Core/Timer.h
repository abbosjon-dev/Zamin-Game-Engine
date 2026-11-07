#pragma once

// A countdown on game time: you advance it with the frame's timestep (so it pauses with the game and
// follows the time scale - or pass Time::GetUnscaledDeltaTime() for real time) and ask it whether it
// fired. A plain value: no callbacks, nothing registered, it can live in any class, be copied and be
// reset - cooldowns, spawn intervals, delays in game logic and in engine code alike. (Scripts also have
// After / Every, which call a function when the time is up; Stopwatch measures real time.)
//
//     Timer m_Cooldown{ 0.5f };                // 0.5 s, once
//     Timer m_Spawn{ 2.0f, true };             // every 2 s
//
//     void OnUpdate(Timestep ts) override
//     {
//         m_Cooldown.Tick(ts);
//         if (Input::IsKeyPressed(Key::Space) && m_Cooldown.IsFinished())
//         {
//             Shoot();
//             m_Cooldown.Restart();
//         }
//         for (int i = m_Spawn.Tick(ts); i > 0; i--)   // a long frame can fire it more than once
//             SpawnEnemy();
//     }

#include <algorithm>
#include <cmath>

namespace Zamin {

    class Timer
    {
    public:
        Timer() = default;
        // `duration` seconds (at least 0); `repeat`: starts again by itself every time it fires.
        explicit Timer(float duration, bool repeat = false) : m_Duration(std::max(0.0f, duration)), m_Repeat(repeat) {}

        // Advances by `seconds` (negative counts as 0); how many times it fired in them (0 or 1 for a
        // one-shot timer; a repeating one can fire several times in a long step). A paused or finished
        // one-shot timer does not move.
        int Tick(float seconds)
        {
            m_JustFinished = false;
            if (m_Paused || !(seconds > 0.0f))
                return 0;
            if (!m_Repeat)
            {
                if (m_Finished)
                    return 0;
                m_Elapsed += seconds;
                if (m_Elapsed < m_Duration)
                    return 0;
                m_Elapsed = m_Duration;
                m_Finished = m_JustFinished = true;
                m_Completions++;
                return 1;
            }
            m_Elapsed += seconds;
            if (m_Duration <= 0.0f)
            {
                // A zero interval fires once per step (not endlessly).
                m_Elapsed = 0.0f;
                m_JustFinished = true;
                m_Completions++;
                return 1;
            }
            if (m_Elapsed < m_Duration)
                return 0;
            const int fired = (int)std::min(std::floor(m_Elapsed / m_Duration), 1.0e6f);
            m_Elapsed = std::fmod(m_Elapsed, m_Duration);
            m_JustFinished = true;
            m_Completions += fired;
            return fired;
        }

        // A one-shot timer reached its end (a repeating one never finishes).
        bool IsFinished() const { return m_Finished; }
        // It fired during the last Tick.
        bool JustFinished() const { return m_JustFinished; }
        bool IsRunning() const { return !m_Paused && !m_Finished; }

        // From the start again (the same duration; not paused).
        void Restart()
        {
            m_Elapsed = 0.0f;
            m_Finished = m_JustFinished = m_Paused = false;
        }
        // From the start with a new duration.
        void Restart(float duration)
        {
            m_Duration = std::max(0.0f, duration);
            Restart();
        }
        // Already finished (IsFinished true) - e.g. a cooldown that should allow the first shot at once.
        void Finish()
        {
            m_Elapsed = m_Duration;
            m_Finished = !m_Repeat;
            m_JustFinished = false;
        }
        void Pause() { m_Paused = true; }
        void Resume() { m_Paused = false; }
        bool IsPaused() const { return m_Paused; }

        float GetDuration() const { return m_Duration; }
        // The duration changes, the elapsed time stays (a one-shot timer past the new end finishes at the next Tick).
        void SetDuration(float duration)
        {
            m_Duration = std::max(0.0f, duration);
            if (m_Finished && m_Elapsed < m_Duration)
                m_Finished = false;
        }
        bool IsRepeating() const { return m_Repeat; }
        void SetRepeating(bool repeat) { m_Repeat = repeat; }

        float GetElapsed() const { return m_Elapsed; }
        float GetRemaining() const { return std::max(0.0f, m_Duration - m_Elapsed); }
        // 0 at the start ... 1 at the end (1 for a zero duration).
        float GetProgress() const { return m_Duration > 0.0f ? std::clamp(m_Elapsed / m_Duration, 0.0f, 1.0f) : 1.0f; }
        // How many times it has fired since it was made (Restart keeps it).
        int GetCompletions() const { return m_Completions; }

    private:
        float m_Duration = 0.0f;
        float m_Elapsed = 0.0f;
        bool m_Repeat = false;
        bool m_Finished = false;
        bool m_JustFinished = false;
        bool m_Paused = false;
        int m_Completions = 0;
    };

}
