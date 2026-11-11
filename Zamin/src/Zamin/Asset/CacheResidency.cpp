#include "Zamin/Asset/CacheResidency.h"
#include <cmath>
namespace Zamin {
    bool ResidencyPressure::Update(uint64_t rss, bool rssValid, uint64_t gpu, uint64_t rssLimit, uint64_t gpuLimit, double resumeRatio)
    {
        if (!std::isfinite(resumeRatio)) resumeRatio = .9;
        resumeRatio = std::clamp(resumeRatio, .1, .99);
        auto over = [&](uint64_t value, uint64_t limit) {
            if (!limit) return false;
            // Byte thresholds round once: .8 is not exactly representable, and 100*.8
            // must not accidentally resume at the documented 80-byte boundary.
            const long double threshold = m_Blocked ? std::max(1.0L, std::round(static_cast<long double>(limit) * resumeRatio)) : static_cast<long double>(limit);
            return static_cast<long double>(value) >= threshold;
        };
        m_Blocked = (rssValid && over(rss, rssLimit)) || over(gpu, gpuLimit);
        return m_Blocked;
    }
}
