#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Zamin {

    class Scene;

    // How the game's time runs (Project settings > Time). Projects without a "Time" section get the
    // defaults - 60 physics steps a second, at most 5 a frame, the clock at normal speed, frames longer
    // than 0.1 s counted as 0.1 s: what the engine always did.
    struct TimeSettings
    {
        static constexpr int MinStepRate = 20, MaxStepRate = 240, MaxStepsLimit = 20;
        static constexpr float MaxTimeScale = 10.0f;
        static constexpr float MinMaxDeltaTime = 1.0f / 60.0f, MaxMaxDeltaTime = 1.0f;

        // Physics (2D and 3D) steps a second (1 / 50 ... 1 / 240 s each) for the scenes that do not set
        // their own (Scene settings > Physics): new scenes and older ones that kept the engine's.
        int FixedStepRate = 60;
        // The most steps one frame takes (a slower frame drops the rest: the game slows down instead
        // of falling further behind); also for the scenes that do not set their own.
        int MaxStepsPerFrame = 5;
        // The game's clock when play starts (Time::SetTimeScale changes it while it runs): 1 normal,
        // 0.5 half speed ... up to MaxTimeScale.
        float DefaultTimeScale = 1.0f;
        // The longest frame the game sees (seconds): a longer one (a stall, a window being dragged) counts
        // as this much - the game slows down there instead of jumping.
        float MaxDeltaTime = 0.1f;

        float GetFixedDeltaTime() const { return 1.0f / (float)FixedStepRate; }
        bool operator==(const TimeSettings&) const = default;
        bool IsDefault() const { return *this == TimeSettings(); }
        // Everything into its range (a NaN to the default).
        void Sanitize();

        // The rates offered as presets (50, 60, 100, 120, 144, 240 Hz).
        static const std::vector<int>& GetStepRatePresets();

        // The "Time" section of a .zproject (a JSON object, every value in a fixed order) and back.
        // FromJson keeps the defaults for missing / wrong values; false when the text is not a JSON object.
        std::string ToJson() const;
        static bool FromJson(const std::string& text, TimeSettings& out);
    };

    // The game's clock for scripts and the engine: one static API wherever it is called from.
    //   Time::SetTimeScale(0.25f);                   // slow motion (every loaded scene: physics, animations,
    //                                                // particles, timers and tweens follow; the UI does not)
    //   float t = Time::GetUnscaledDeltaTime();      // real time: menus, a slow-motion effect's own fade
    // The game values come from the running game ("the game clock": the scene being played, with every
    // scene of its SceneManager); without a running game the scaled values are the real ones.
    class Time
    {
    public:
        // This frame's game time (seconds, scaled by the time scale; 0 while the game is paused).
        static float GetDeltaTime();
        // This frame's real time (seconds; at most TimeSettings::MaxDeltaTime).
        static float GetUnscaledDeltaTime();
        // The physics step of the game (1 / step rate seconds).
        static float GetFixedDeltaTime();
        // Game time since play started (scaled) / real time since play started.
        static float GetTime();
        static float GetUnscaledTime();
        // Real seconds since the program started (not limited, not scaled).
        static double GetTimeSinceStartup();
        // Frames since the program started.
        static uint64_t GetFrameCount();

        // The game's clock: 1 normal, 0.5 half speed, 0 paused (at most TimeSettings::MaxTimeScale).
        // Without a running game there is nothing to change (a game starts at the project's DefaultTimeScale).
        static void SetTimeScale(float scale);
        static float GetTimeScale();
        // Time scale 0 and back to the scale it had (a pause menu).
        static void SetPaused(bool paused);
        static bool IsPaused();

        // The project's settings (the defaults without a project).
        static const TimeSettings& GetSettings();

        // ---- engine side
        // Application: a frame begins (`unscaledDelta`: its real time, already limited).
        static void OnFrame(float unscaledDelta);
        // Scenes: the scene whose clock the API uses (set when a game starts / updates, cleared when it
        // stops or goes away). ClearGameClock only clears it when it is `scene`.
        static void SetGameClock(Scene* scene);
        static void ClearGameClock(const Scene* scene);
        static Scene* GetGameClock();
    };

}
