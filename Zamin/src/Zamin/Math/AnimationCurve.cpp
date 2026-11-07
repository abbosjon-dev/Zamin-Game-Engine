#include "Zamin/Math/AnimationCurve.h"

#include <algorithm>
#include <cmath>

namespace Zamin {

    float AnimationCurve::WrapTime(float time) const
    {
        if (!std::isfinite(time))
            return GetStartTime();
        if (Keys.size() < 2)
            return time;
        const float start = GetStartTime(), end = GetEndTime(), duration = end - start;
        if (duration <= 0.0f || (time >= start && time <= end))
            return time;
        const AnimationWrap wrap = time < start ? PreWrap : PostWrap;
        switch (wrap)
        {
            case AnimationWrap::Loop:
            {
                float t = std::fmod(time - start, duration);
                if (t < 0.0f)
                    t += duration;
                return start + t;
            }
            case AnimationWrap::PingPong:
            {
                float t = std::fmod(time - start, duration * 2.0f);
                if (t < 0.0f)
                    t += duration * 2.0f;
                return start + (t <= duration ? t : duration * 2.0f - t);
            }
            default:
                return std::clamp(time, start, end);
        }
    }

    float AnimationCurve::Evaluate(float time) const
    {
        return CurveKeys::EvaluateScalar(Keys, WrapTime(time));
    }

    float AnimationCurve::GetMin() const
    {
        float lo, hi;
        CurveKeys::GetRange(Keys, GetStartTime(), GetEndTime(), lo, hi);
        return lo;
    }

    float AnimationCurve::GetMax() const
    {
        float lo, hi;
        CurveKeys::GetRange(Keys, GetStartTime(), GetEndTime(), lo, hi);
        return hi;
    }

    int AnimationCurve::FindKey(float time, float tolerance) const
    {
        for (size_t i = 0; i < Keys.size(); i++)
            if (std::abs(Keys[i].Time - time) <= tolerance)
                return (int)i;
        return -1;
    }

    int AnimationCurve::AddKey(float time, float value, KeyInterpolation interpolation)
    {
        if (!std::isfinite(time) || !std::isfinite(value))
            return -1;
        if (int index = FindKey(time); index >= 0)
        {
            Keys[(size_t)index].Value.x = value;
            UpdateTangents();
            return index;
        }
        AnimationKey key;
        key.Time = time;
        key.Value.x = value;
        key.Interpolation = interpolation;
        auto it = std::upper_bound(Keys.begin(), Keys.end(), time, [](float t, const AnimationKey& k) { return t < k.Time; });
        const int index = (int)(it - Keys.begin());
        Keys.insert(it, key);
        UpdateTangents();
        return index;
    }

    int AnimationCurve::AddKey(float time)
    {
        if (!std::isfinite(time))
            return -1;
        if (Keys.empty())
            return AddKey(time, 0.0f);
        if (int index = FindKey(time); index >= 0)
            return index;
        return CurveKeys::Insert(Keys, time, KeyInterpolation::Bezier);
    }

    bool AnimationCurve::RemoveKey(int index)
    {
        if (index < 0 || index >= (int)Keys.size())
            return false;
        Keys.erase(Keys.begin() + index);
        UpdateTangents();
        return true;
    }

    int AnimationCurve::MoveKey(int index, float time, float value)
    {
        if (index < 0 || index >= (int)Keys.size() || !std::isfinite(time) || !std::isfinite(value))
            return -1;
        AnimationKey key = Keys[(size_t)index];
        Keys.erase(Keys.begin() + index);
        key.Time = time;
        key.Value.x = value;
        auto it = std::upper_bound(Keys.begin(), Keys.end(), time, [](float t, const AnimationKey& k) { return t < k.Time; });
        const int moved = (int)(it - Keys.begin());
        Keys.insert(it, key);
        UpdateTangents();
        return moved;
    }

    void AnimationCurve::SetTangents(int index, float inTangent, float outTangent)
    {
        if (index < 0 || index >= (int)Keys.size())
            return;
        AnimationKey& k = Keys[(size_t)index];
        k.TangentMode = KeyTangentMode::Free;
        k.InTangent = glm::vec4(inTangent, 0.0f, 0.0f, 0.0f);
        k.OutTangent = glm::vec4(outTangent, 0.0f, 0.0f, 0.0f);
    }

    void AnimationCurve::SmoothTangents(int index)
    {
        for (size_t i = 0; i < Keys.size(); i++)
            if (index < 0 || (int)i == index)
                Keys[i].TangentMode = KeyTangentMode::Auto;
        UpdateTangents();
    }

    void AnimationCurve::FlattenTangents(int index)
    {
        for (size_t i = 0; i < Keys.size(); i++)
            if (index < 0 || (int)i == index)
                Keys[i].TangentMode = KeyTangentMode::Flat;
        UpdateTangents();
    }

    void AnimationCurve::SetInterpolation(int index, KeyInterpolation interpolation)
    {
        for (size_t i = 0; i < Keys.size(); i++)
            if (index < 0 || (int)i == index)
                Keys[i].Interpolation = interpolation;
        UpdateTangents();
    }

    void AnimationCurve::Sort()
    {
        CurveKeys::Sort(Keys);
    }

    void AnimationCurve::UpdateTangents()
    {
        CurveKeys::UpdateTangents(Keys);
    }

    AnimationCurve AnimationCurve::Constant(float t0, float t1, float value)
    {
        return Linear(t0, value, t1, value);
    }

    AnimationCurve AnimationCurve::Linear(float t0, float v0, float t1, float v1)
    {
        AnimationCurve c;
        AnimationKey a;
        a.Time = std::min(t0, t1);
        a.Value.x = t0 <= t1 ? v0 : v1;
        AnimationKey b = a;
        b.Time = std::max(t0, t1);
        b.Value.x = t0 <= t1 ? v1 : v0;
        c.Keys = { a };
        if (b.Time > a.Time)
            c.Keys.push_back(b);
        return c;
    }

    AnimationCurve AnimationCurve::EaseInOut(float t0, float v0, float t1, float v1)
    {
        AnimationCurve c = Linear(t0, v0, t1, v1);
        for (AnimationKey& k : c.Keys)
        {
            k.Interpolation = KeyInterpolation::Bezier;
            k.TangentMode = KeyTangentMode::Flat;
        }
        c.UpdateTangents();
        return c;
    }

    const char* AnimationCurve::WrapToString(AnimationWrap wrap)
    {
        switch (wrap)
        {
            case AnimationWrap::Loop: return "Loop";
            case AnimationWrap::PingPong: return "PingPong";
            default: return "Once";
        }
    }

    std::optional<AnimationWrap> AnimationCurve::WrapFromString(std::string_view text)
    {
        if (text == "Once") return AnimationWrap::Once;
        if (text == "Loop") return AnimationWrap::Loop;
        if (text == "PingPong") return AnimationWrap::PingPong;
        return std::nullopt;
    }

}
