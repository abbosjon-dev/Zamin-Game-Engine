#pragma once

#include "Zamin/Math/Easing.h"

#include <glm/glm.hpp>

#include <optional>
#include <string_view>
#include <vector>

namespace Zamin {

    // A colour of a tint gradient at `Time` (0..1 of the animation's duration).
    struct TintKey
    {
        float Time = 0.0f;
        glm::vec4 Color{ 1.0f };
        bool operator==(const TintKey&) const = default;
    };

    // How a tint animation runs through its gradient:
    // - Once: start -> end, then it stays on the end colour;
    // - Loop: start -> end, again and again;
    // - PingPong: start -> end -> start ... (one cycle = there and back).
    enum class TintPlayMode { Once = 0, Loop, PingPong };

    // The math of TintAnimationComponent (no scene needed: the renderer and the tests use it).
    class TintAnimation
    {
    public:
        // The colour of the gradient at `t` (0..1): the keys' colours blended linearly between their
        // times; before the first key its colour, after the last key that one's. No keys: white.
        // The keys need not be sorted.
        static glm::vec4 EvaluateGradient(const std::vector<TintKey>& keys, float t);

        // Where in the gradient (0..1, eased) the animation is `elapsed` seconds after it started.
        // `cycles` > 0 (Loop / PingPong): it stops after that many cycles - Loop on the end, PingPong
        // back on the start. A duration of 0: the end at once (Once / Loop), the start (PingPong).
        static float GetPhase(TintPlayMode mode, Ease ease, float duration, int cycles, float elapsed);
        // True once a Once animation, or one with a cycle count, has reached its last colour.
        static bool IsFinished(TintPlayMode mode, float duration, int cycles, float elapsed);

        // Keys sorted by time, times clamped to 0..1 (the inspector keeps them like this).
        static void SortKeys(std::vector<TintKey>& keys);

        static const char* ModeToString(TintPlayMode mode);
        static std::optional<TintPlayMode> ModeFromString(std::string_view name);
    };

}
