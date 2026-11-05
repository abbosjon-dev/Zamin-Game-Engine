#pragma once

#include <optional>
#include <string_view>

namespace Zamin {

    // Easing curves for tweens and animations: t in [0,1] -> eased t (0 -> 0, 1 -> 1; OutBack and
    // OutElastic overshoot in between).
    enum class Ease { Linear = 0, InQuad, OutQuad, InOutQuad, OutCubic, InOutCubic, OutBack, OutElastic, OutBounce };
    constexpr int EaseCount = 9;

    // t is clamped to [0,1] first.
    float ApplyEase(Ease ease, float t);

    // Names for files ("InOutQuad") and back (nullopt for an unknown name).
    const char* EaseToString(Ease ease);
    std::optional<Ease> EaseFromString(std::string_view name);

}
