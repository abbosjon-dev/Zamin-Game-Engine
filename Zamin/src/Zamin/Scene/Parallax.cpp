#include "Zamin/Scene/Parallax.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Zamin {

    bool ParallaxMath::GetRepeatRange(float center, float size, float spacing, float viewMin, float viewMax,
                                      int& first, int& last, int maxCopies)
    {
        if (!(spacing > 1e-6f) || !(viewMax > viewMin) || !std::isfinite(viewMin) || !std::isfinite(viewMax))
            return false;
        const float half = std::abs(size) * 0.5f;
        // Copy k overlaps the view when center + k * spacing - half < viewMax and ... + half > viewMin.
        double lo = std::floor(((double)viewMin - center - half) / spacing) + 1.0;
        double hi = std::ceil(((double)viewMax - center + half) / spacing) - 1.0;
        if (hi < lo)
            return false;
        maxCopies = std::max(1, maxCopies);
        if (hi - lo + 1.0 > (double)maxCopies)
        {
            double middle = std::round(((viewMin + viewMax) * 0.5 - center) / spacing);
            lo = middle - (double)(maxCopies / 2);
            hi = lo + (double)maxCopies - 1.0;
        }
        constexpr double limit = (double)std::numeric_limits<int>::max() / 2;
        first = (int)std::clamp(lo, -limit, limit);
        last = (int)std::clamp(hi, -limit, limit);
        return true;
    }

    bool ParallaxMath::GetViewRectAtDepth(const glm::mat4& viewProjection, float z, glm::vec2& min, glm::vec2& max)
    {
        const glm::mat4 inverse = glm::inverse(viewProjection);
        min = glm::vec2(std::numeric_limits<float>::max());
        max = glm::vec2(-std::numeric_limits<float>::max());
        for (int corner = 0; corner < 4; corner++)
        {
            float x = (corner & 1) ? 1.0f : -1.0f, y = (corner & 2) ? 1.0f : -1.0f;
            glm::vec4 nearPoint = inverse * glm::vec4(x, y, -1.0f, 1.0f);
            glm::vec4 farPoint = inverse * glm::vec4(x, y, 1.0f, 1.0f);
            if (std::abs(nearPoint.w) < 1e-12f || std::abs(farPoint.w) < 1e-12f)
                return false;
            glm::vec3 a = glm::vec3(nearPoint) / nearPoint.w, b = glm::vec3(farPoint) / farPoint.w;
            float dz = b.z - a.z;
            glm::vec3 p;
            if (std::abs(dz) < 1e-7f)
            {
                // The ray runs along the plane: only a ray in it has a point there.
                if (std::abs(a.z - z) > 1e-4f)
                    return false;
                p = a;
            }
            else
            {
                // Where the corner's ray (its whole line, also outside near..far) meets the plane.
                float t = (z - a.z) / dz;
                p = a + (b - a) * t;
            }
            min = glm::min(min, glm::vec2(p));
            max = glm::max(max, glm::vec2(p));
        }
        return min.x <= max.x && min.y <= max.y && std::isfinite(min.x) && std::isfinite(max.x);
    }

}
