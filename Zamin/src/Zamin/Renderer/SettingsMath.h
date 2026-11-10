#pragma once

#include <algorithm>
#include <cmath>

namespace Zamin::SettingsMath {
    inline float SanitizeStep(float value, float fallback, float minimum)
    {
        return std::isfinite(value) ? std::max(value, minimum) : fallback;
    }
}
