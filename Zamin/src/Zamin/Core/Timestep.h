#pragma once

/// @file
/// @brief Timestep: the time a frame (or a fixed step) covers.

namespace Zamin {

    /// The time one update covers, in seconds. Layer::OnUpdate and the scripts' OnUpdate get the frame's
    /// (already scaled by the game's time scale where the scene runs; capped at the project's longest
    /// frame). Converts to float implicitly: `position += speed * ts;`.
    class Timestep
    {
    public:
        Timestep(float seconds = 0.0f) : m_Time(seconds) {}
        operator float() const { return m_Time; }                       ///< seconds
        float GetSeconds() const { return m_Time; }
        float GetMilliseconds() const { return m_Time * 1000.0f; }
    private:
        float m_Time;
    };

}
