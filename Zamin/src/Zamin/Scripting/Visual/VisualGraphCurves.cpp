#include "VisualGraph.h"
#include <algorithm>
#include <cmath>
// Curve values (Type::Curve): validation, keys <-> JSON and evaluation. The editor's curve preview evaluates with the
// same function the VM runs.
namespace Zamin::Visual {
    namespace {
        bool Finite(const Value& v) { return v.is_number() && std::isfinite(v.get<double>()); }
        bool ModeOf(const Value& v, CurveMode& mode)
        {
            if (!v.is_string()) return false;
            for (auto candidate : { CurveMode::Constant, CurveMode::Linear, CurveMode::Cubic })
                if (v.get_ref<const std::string&>() == CurveModeName(candidate))
                {
                    mode = candidate;
                    return true;
                }
            return false;
        }
    }
    const char* CurveModeName(CurveMode mode)
    {
        switch (mode)
        {
            case CurveMode::Constant: return "Constant";
            case CurveMode::Cubic:    return "Cubic";
            default:                  return "Linear";
        }
    }
    bool IsCurve(const Value& value)
    {
        if (!value.is_object() || value.size() != 1 || !value.contains("Keys")) return false;
        const auto& keys = value.at("Keys");
        if (!keys.is_array() || keys.size() > MaxCurveKeys) return false;
        double previous = -INFINITY;
        for (const auto& key : keys)
        {
            CurveMode mode;
            if (!key.is_object() || key.size() != 5 || !key.contains("Time") || !key.contains("Value") || !key.contains("In") || !key.contains("Out") || !key.contains("Mode")) return false;
            if (!Finite(key.at("Time")) || !Finite(key.at("Value")) || !Finite(key.at("In")) || !Finite(key.at("Out")) || !ModeOf(key.at("Mode"), mode)) return false;
            const double time = key.at("Time").get<double>();
            if (!(time > previous)) return false;
            previous = time;
        }
        return true;
    }
    std::vector<CurveKey> CurveKeys(const Value& curve)
    {
        std::vector<CurveKey> keys;
        if (!IsCurve(curve)) return keys;
        for (const auto& key : curve.at("Keys"))
        {
            CurveKey k{ key.at("Time").get<double>(), key.at("Value").get<double>(), CurveMode::Linear, key.at("In").get<double>(), key.at("Out").get<double>() };
            ModeOf(key.at("Mode"), k.Mode);
            keys.push_back(k);
        }
        return keys;
    }
    Value MakeCurve(std::vector<CurveKey> keys)
    {
        std::stable_sort(keys.begin(), keys.end(), [](const CurveKey& a, const CurveKey& b) { return a.Time < b.Time; });
        Value out = Value::array();
        for (const auto& k : keys) out.push_back({ { "Time", k.Time }, { "Value", k.Value }, { "Mode", CurveModeName(k.Mode) }, { "In", k.In }, { "Out", k.Out } });
        return { { "Keys", std::move(out) } };
    }
    double EvaluateCurve(const std::vector<CurveKey>& keys, double time)
    {
        if (keys.empty()) return 0;
        if (!(time > keys.front().Time)) return keys.front().Value;
        if (time >= keys.back().Time) return keys.back().Value;
        // The segment [a, b] with a.Time < time < b.Time.
        const auto next = std::upper_bound(keys.begin(), keys.end(), time, [](double t, const CurveKey& k) { return t < k.Time; });
        const auto& b = *next;
        const auto& a = *(next - 1);
        const double span = b.Time - a.Time, s = (time - a.Time) / span;
        switch (a.Mode)
        {
            case CurveMode::Constant: return a.Value;
            case CurveMode::Linear:   return a.Value + (b.Value - a.Value) * s;
            default:
            {
                // Cubic Hermite: the tangents are slopes (value per time unit), scaled to the segment.
                const double s2 = s * s, s3 = s2 * s;
                return (2 * s3 - 3 * s2 + 1) * a.Value + (s3 - 2 * s2 + s) * span * a.Out + (-2 * s3 + 3 * s2) * b.Value + (s3 - s2) * span * b.In;
            }
        }
    }
    double EvaluateCurve(const Value& curve, double time) { return EvaluateCurve(CurveKeys(curve), time); }
}
