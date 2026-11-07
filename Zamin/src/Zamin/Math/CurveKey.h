#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

// The keys every curve of the engine is made of: property animation tracks (PropertyAnimation),
// particle curves (ParticleCurve) and AnimationCurve values (scripts, components).
namespace Zamin {

    // How the value goes from a key to the next one.
    enum class KeyInterpolation : uint8_t { Step = 0, Linear, Bezier };
    // Where a Bezier key's tangents (slopes) come from: Auto - smooth through the neighbours, without
    // overshooting at peaks and level at the first / last key; Flat - level (eases in and out);
    // Free - set by hand (the curve view's handles).
    enum class KeyTangentMode : uint8_t { Auto = 0, Flat, Free };

    // What happens at the end of a clip: Once stops on its last value, Loop starts over, PingPong
    // plays it backwards and forwards again.
    enum class AnimationWrap : uint8_t { Once = 0, Loop, PingPong };

    struct AnimationKey
    {
        float Time = 0.0f;                    // seconds
        glm::vec4 Value{ 0.0f };              // the property's components (x, y, z, w; unused ones are 0)
        KeyInterpolation Interpolation = KeyInterpolation::Linear; // from this key to the next
        KeyTangentMode TangentMode = KeyTangentMode::Auto;
        // Bezier: slopes (value per second) arriving at / leaving the key. The curve between keys a
        // and b is the cubic Bezier through a.Value, a.Value + a.OutTangent * dt / 3,
        // b.Value - b.InTangent * dt / 3 and b.Value (dt = b.Time - a.Time).
        glm::vec4 InTangent{ 0.0f };
        glm::vec4 OutTangent{ 0.0f };

        bool operator==(const AnimationKey&) const = default;
    };

    // The arithmetic of key lists sorted by time (shared by every curve type).
    namespace CurveKeys {

        // The value between two keys `t` seconds after `a` (b follows a).
        glm::vec4 Interpolate(const AnimationKey& a, const AnimationKey& b, float t);
        // The value at `time`: held before the first / after the last key; 0 without keys.
        glm::vec4 Evaluate(const std::vector<AnimationKey>& keys, float time);
        // The same for one number per key (Value.x) without the vectors: what particle curves and
        // AnimationCurve evaluate (the same numbers as Evaluate(...).x).
        float EvaluateScalar(const std::vector<AnimationKey>& keys, float time);
        // The tangents of the Auto and Flat keys (Free ones stay): Auto - smooth through the
        // neighbours without overshooting at peaks, level at the first / last key.
        void UpdateTangents(std::vector<AnimationKey>& keys);
        // Keys back in time order (stable: equal times keep their order), then UpdateTangents.
        void Sort(std::vector<AnimationKey>& keys);
        // A key at `time` with the value the (scalar) curve has there and the interpolation of the
        // key before it (`fallback` when it becomes the first); returns its index. Tangents updated.
        int Insert(std::vector<AnimationKey>& keys, float time, KeyInterpolation fallback = KeyInterpolation::Linear);
        // The smallest / largest Value.x between t0 and t1 (the keys, and samples between them where
        // a Bezier key may overshoot).
        void GetRange(const std::vector<AnimationKey>& keys, float t0, float t1, float& outMin, float& outMax);

    }

}
