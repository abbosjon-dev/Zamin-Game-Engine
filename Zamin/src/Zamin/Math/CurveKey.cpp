#include "Zamin/Math/CurveKey.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Zamin::CurveKeys {

    glm::vec4 Interpolate(const AnimationKey& a, const AnimationKey& b, float t)
    {
        const float dt = b.Time - a.Time;
        if (dt <= 0.0f)
            return b.Value;
        if (a.Interpolation == KeyInterpolation::Step)
            return a.Value;
        const float s = std::clamp(t / dt, 0.0f, 1.0f);
        if (a.Interpolation == KeyInterpolation::Linear)
            return glm::mix(a.Value, b.Value, s);
        // Cubic Hermite with slopes = the Bezier with handles a third of the way in.
        const float s2 = s * s, s3 = s2 * s;
        const float h00 = 2.0f * s3 - 3.0f * s2 + 1.0f;
        const float h10 = s3 - 2.0f * s2 + s;
        const float h01 = -2.0f * s3 + 3.0f * s2;
        const float h11 = s3 - s2;
        return h00 * a.Value + h10 * dt * a.OutTangent + h01 * b.Value + h11 * dt * b.InTangent;
    }

    glm::vec4 Evaluate(const std::vector<AnimationKey>& keys, float time)
    {
        if (keys.empty())
            return glm::vec4(0.0f);
        if (keys.size() == 1 || time <= keys.front().Time)
            return keys.front().Value;
        if (time >= keys.back().Time)
            return keys.back().Value;
        auto it = std::upper_bound(keys.begin(), keys.end(), time, [](float t, const AnimationKey& k) { return t < k.Time; });
        const AnimationKey& b = *it;
        const AnimationKey& a = *(it - 1);
        return Interpolate(a, b, time - a.Time);
    }

    float EvaluateScalar(const std::vector<AnimationKey>& keys, float t)
    {
        // Evaluate for one component, without the vectors (particle curves: every particle every
        // frame): the same numbers.
        const size_t n = keys.size();
        if (n == 0)
            return 0.0f;
        const AnimationKey& first = keys.front();
        if (n == 1 || t <= first.Time)
            return first.Value.x;
        const AnimationKey& last = keys.back();
        if (t >= last.Time)
            return last.Value.x;
        size_t i = 1; // the first key after t (a short list: a linear scan; a long one: binary search)
        if (n > 16)
            i = (size_t)(std::upper_bound(keys.begin(), keys.end(), t, [](float time, const AnimationKey& k) { return time < k.Time; }) - keys.begin());
        else
            while (i < n - 1 && !(t < keys[i].Time))
                i++;
        const AnimationKey& a = keys[i - 1];
        const AnimationKey& b = keys[i];
        const float dt = b.Time - a.Time;
        if (dt <= 0.0f)
            return b.Value.x;
        if (a.Interpolation == KeyInterpolation::Step)
            return a.Value.x;
        const float s = std::clamp((t - a.Time) / dt, 0.0f, 1.0f);
        if (a.Interpolation == KeyInterpolation::Linear)
            return a.Value.x * (1.0f - s) + b.Value.x * s;
        const float s2 = s * s, s3 = s2 * s;
        const float h00 = 2.0f * s3 - 3.0f * s2 + 1.0f;
        const float h10 = s3 - 2.0f * s2 + s;
        const float h01 = -2.0f * s3 + 3.0f * s2;
        const float h11 = s3 - s2;
        return h00 * a.Value.x + h10 * dt * a.OutTangent.x + h01 * b.Value.x + h11 * dt * b.InTangent.x;
    }

    void UpdateTangents(std::vector<AnimationKey>& keys)
    {
        const size_t n = keys.size();
        for (size_t i = 0; i < n; i++)
        {
            AnimationKey& k = keys[i];
            if (k.TangentMode == KeyTangentMode::Free)
                continue;
            if (k.TangentMode == KeyTangentMode::Flat || i == 0 || i + 1 == n)
            {
                k.InTangent = k.OutTangent = glm::vec4(0.0f);
                continue;
            }
            const AnimationKey& p = keys[i - 1];
            const AnimationKey& q = keys[i + 1];
            const float dl = k.Time - p.Time, dr = q.Time - k.Time;
            glm::vec4 slope(0.0f);
            if (dl > 0.0f && dr > 0.0f)
            {
                for (int c = 0; c < 4; c++)
                {
                    const float left = (k.Value[c] - p.Value[c]) / dl;
                    const float right = (q.Value[c] - k.Value[c]) / dr;
                    if (left * right <= 0.0f)
                        continue; // a peak or a plateau: level, so the curve does not overshoot
                    float s = (q.Value[c] - p.Value[c]) / (dl + dr);
                    const float limit = 3.0f * std::min(std::abs(left), std::abs(right));
                    slope[c] = std::clamp(s, -limit, limit);
                }
            }
            k.InTangent = k.OutTangent = slope;
        }
    }

    void Sort(std::vector<AnimationKey>& keys)
    {
        std::stable_sort(keys.begin(), keys.end(), [](const AnimationKey& a, const AnimationKey& b) { return a.Time < b.Time; });
        UpdateTangents(keys);
    }

    int Insert(std::vector<AnimationKey>& keys, float time, KeyInterpolation fallback)
    {
        AnimationKey key;
        key.Time = time;
        key.Value = glm::vec4(EvaluateScalar(keys, time), 0.0f, 0.0f, 0.0f);
        key.Interpolation = fallback;
        auto it = std::upper_bound(keys.begin(), keys.end(), time, [](float t, const AnimationKey& k) { return t < k.Time; });
        if (it != keys.begin())
            key.Interpolation = (it - 1)->Interpolation;
        const int index = (int)(it - keys.begin());
        keys.insert(it, key);
        UpdateTangents(keys);
        return index;
    }

    void GetRange(const std::vector<AnimationKey>& keys, float t0, float t1, float& outMin, float& outMax)
    {
        outMin = std::numeric_limits<float>::max();
        outMax = -std::numeric_limits<float>::max();
        if (keys.empty())
        {
            outMin = outMax = 0.0f;
            return;
        }
        auto take = [&](float v) {
            outMin = std::min(outMin, v);
            outMax = std::max(outMax, v);
        };
        for (const AnimationKey& k : keys)
            take(k.Value.x);
        bool smooth = false;
        for (const AnimationKey& k : keys)
            smooth |= k.Interpolation == KeyInterpolation::Bezier;
        if (smooth)
            for (int i = 0; i <= 32; i++)
                take(EvaluateScalar(keys, t0 + (t1 - t0) * (i / 32.0f)));
    }

}
