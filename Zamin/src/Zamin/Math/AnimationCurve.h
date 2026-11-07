#pragma once

#include "Zamin/Math/CurveKey.h"

#include <optional>
#include <string_view>
#include <vector>

namespace Zamin {

    // A number over time drawn as a curve: keys (time, value, in / out tangents) joined Step /
    // Linear / smooth (Bezier), what happens before the first and after the last key (PreWrap /
    // PostWrap: Once holds the end value, Loop repeats, PingPong goes back and forth). The same keys
    // and arithmetic as the animation clips and particle curves (CurveKey.h).
    //
    // A script field (ZM_FIELD) of this type is edited in the inspector with a curve editor and
    // saved with the scene:
    //   AnimationCurve Jump = AnimationCurve::EaseInOut(0.0f, 0.0f, 0.5f, 2.0f);
    //   ZM_REGISTER_SCRIPT(Hopper, ZM_FIELD(Hopper, Jump))
    //   ... height = Jump.Evaluate(time);
    struct AnimationCurve
    {
        std::vector<AnimationKey> Keys; // by time; the value is Value.x
        AnimationWrap PreWrap = AnimationWrap::Once;
        AnimationWrap PostWrap = AnimationWrap::Once;

        // The value at `time` (wrapped into the keys' range first); 0 without keys.
        float Evaluate(float time) const;
        // `time` mapped into [GetStartTime(), GetEndTime()] by the wrap modes.
        float WrapTime(float time) const;

        bool IsEmpty() const { return Keys.empty(); }
        int GetKeyCount() const { return (int)Keys.size(); }
        float GetStartTime() const { return Keys.empty() ? 0.0f : Keys.front().Time; }
        float GetEndTime() const { return Keys.empty() ? 0.0f : Keys.back().Time; }
        float GetDuration() const { return GetEndTime() - GetStartTime(); }
        // The smallest / largest value between the first and the last key.
        float GetMin() const;
        float GetMax() const;

        // A key (smooth; a key already within 1e-4 s of `time` gets the value instead); returns
        // its index. The automatic tangents are updated.
        int AddKey(float time, float value, KeyInterpolation interpolation = KeyInterpolation::Bezier);
        // A key at `time` keeping the curve's value there.
        int AddKey(float time);
        bool RemoveKey(int index);
        // Moves a key (re-sorted: returns its new index; -1 for a bad index).
        int MoveKey(int index, float time, float value);
        // The key at `time` (within `tolerance`), -1 when none.
        int FindKey(float time, float tolerance = 1e-4f) const;
        // Sets a key's slopes by hand (its tangent mode becomes Free).
        void SetTangents(int index, float inTangent, float outTangent);
        // The key's slopes worked out from its neighbours again (Auto; all keys: -1). Flat:
        // level slopes (eases in and out).
        void SmoothTangents(int index = -1);
        void FlattenTangents(int index = -1);
        void SetInterpolation(int index, KeyInterpolation interpolation);
        // Keys back in time order, then the automatic tangents (after editing Keys directly).
        void Sort();
        void UpdateTangents();

        bool operator==(const AnimationCurve&) const = default;

        // Ready-made curves between (t0, v0) and (t1, v1).
        static AnimationCurve Constant(float t0, float t1, float value);
        static AnimationCurve Linear(float t0, float v0, float t1, float v1);
        static AnimationCurve EaseInOut(float t0, float v0, float t1, float v1);

        static const char* WrapToString(AnimationWrap wrap);
        static std::optional<AnimationWrap> WrapFromString(std::string_view text);
    };

}
