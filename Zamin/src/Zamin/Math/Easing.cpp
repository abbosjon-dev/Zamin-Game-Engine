#include "Zamin/Math/Easing.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

namespace Zamin {

    float ApplyEase(Ease ease, float t)
    {
        t = std::clamp(t, 0.0f, 1.0f);
        switch (ease)
        {
            case Ease::Linear:     return t;
            case Ease::InQuad:     return t * t;
            case Ease::OutQuad:    return 1.0f - (1.0f - t) * (1.0f - t);
            case Ease::InOutQuad:  return t < 0.5f ? 2.0f * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) * 0.5f;
            case Ease::OutCubic:   return 1.0f - std::pow(1.0f - t, 3.0f);
            case Ease::InOutCubic: return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) * 0.5f;
            case Ease::OutBack:
            {
                const float c1 = 1.70158f, c3 = c1 + 1.0f;
                return 1.0f + c3 * std::pow(t - 1.0f, 3.0f) + c1 * std::pow(t - 1.0f, 2.0f);
            }
            case Ease::OutElastic:
            {
                if (t == 0.0f || t == 1.0f) return t;
                const float c4 = glm::two_pi<float>() / 3.0f;
                return std::pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * c4) + 1.0f;
            }
            case Ease::OutBounce:
            {
                const float n1 = 7.5625f, d1 = 2.75f;
                if (t < 1.0f / d1) return n1 * t * t;
                if (t < 2.0f / d1) { t -= 1.5f / d1; return n1 * t * t + 0.75f; }
                if (t < 2.5f / d1) { t -= 2.25f / d1; return n1 * t * t + 0.9375f; }
                t -= 2.625f / d1; return n1 * t * t + 0.984375f;
            }
        }
        return t;
    }

    static constexpr const char* s_EaseNames[EaseCount] = { "Linear", "InQuad", "OutQuad", "InOutQuad", "OutCubic",
                                                            "InOutCubic", "OutBack", "OutElastic", "OutBounce" };

    const char* EaseToString(Ease ease)
    {
        int i = (int)ease;
        return i >= 0 && i < EaseCount ? s_EaseNames[i] : s_EaseNames[0];
    }

    std::optional<Ease> EaseFromString(std::string_view name)
    {
        for (int i = 0; i < EaseCount; i++)
            if (name == s_EaseNames[i])
                return (Ease)i;
        return std::nullopt;
    }

}
