#pragma once

namespace Zamin {

    // The editor draws fewer frames while nobody needs them: its window does not have the focus
    // (another program is in front), or it has but nothing is happening (no input for a moment, no
    // game running, no preview playing, nothing loading). The frame then waits for the window's
    // events (glfwWaitEventsTimeout): any input wakes it up at once, so it never feels slower.
    struct IdleThrottleSettings
    {
        bool Enabled = true;
        float UnfocusedFPS = 10.0f;   // the editor window is not in front
        float IdleFPS = 30.0f;        // in front, nothing happening
        float IdleDelay = 1.0f;       // seconds of full speed after the last input before idling

        static constexpr float MinFPS = 1.0f, MaxFPS = 120.0f;
        static constexpr float MinDelay = 0.1f, MaxDelay = 10.0f;
        void Sanitize();
    };

    // What the editor is doing this frame.
    struct IdleThrottleState
    {
        bool Focused = true;            // the window has the keyboard focus
        bool Running = false;           // the game plays / physics simulates (not paused)
        bool Animating = false;         // a preview plays, the camera moves, something is being dragged
        bool PendingWork = false;       // loading, building, thumbnails being made
        bool Automation = false;        // a scripted run (ZAMIN_MAX_FRAMES): never throttled
        double SecondsSinceInput = 0.0; // since the last input event (or a change worth showing)
    };

    enum class FrameLimitReason : unsigned char { None = 0, Unfocused, Idle };

    struct FrameLimit
    {
        FrameLimitReason Reason = FrameLimitReason::None;
        float FPS = 0.0f;               // the most frames per second (0 = no limit)

        bool IsLimited() const { return Reason != FrameLimitReason::None && FPS > 0.0f; }
        double GetFrameSeconds() const { return IsLimited() ? 1.0 / (double)FPS : 0.0; }
    };

    class IdleThrottle
    {
    public:
        // The limit for the next frame: none while disabled / automated, the game running or work
        // pending; the unfocused rate when the window is not in front; the idle rate when nothing
        // happened for IdleDelay seconds (a preview playing keeps full speed).
        static FrameLimit Decide(const IdleThrottleSettings& settings, const IdleThrottleState& state);
        // How long to wait for events at the end of a frame that took `frameSeconds` so far (0: none).
        static double GetWaitSeconds(const FrameLimit& limit, double frameSeconds);
        // "fokussiz", "bo'sh turibdi", "cheklanmagan".
        static const char* GetReasonName(FrameLimitReason reason);
    };

}
