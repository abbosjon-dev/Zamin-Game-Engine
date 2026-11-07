#include "Zamin/Math/Spline.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Zamin {

    const char* SplineTypeToString(SplineType type)
    {
        switch (type)
        {
            case SplineType::Bezier: return "Bezier";
            case SplineType::Linear: return "Linear";
            default: return "CatmullRom";
        }
    }

    std::optional<SplineType> SplineTypeFromString(std::string_view text)
    {
        if (text == "CatmullRom") return SplineType::CatmullRom;
        if (text == "Bezier") return SplineType::Bezier;
        if (text == "Linear") return SplineType::Linear;
        return std::nullopt;
    }

    // ------------------------------------------------------------------ curve arithmetic
    namespace SplineMath {

        // The Catmull-Rom segment as a cubic Hermite: its end tangents (per t) for the knot spacing.
        // (Barry-Goldman's pyramid gives the same curve; this form also gives the derivative.)
        static bool CatmullRomTangents(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3, float alpha,
                                       glm::vec3& m1, glm::vec3& m2)
        {
            alpha = std::clamp(alpha, 0.0f, 1.0f);
            const float k1 = std::pow(glm::length(p2 - p1), alpha);
            if (!(k1 > 1e-6f))
                return false; // p1 == p2: a point
            float k0 = std::pow(glm::length(p1 - p0), alpha);
            float k2 = std::pow(glm::length(p3 - p2), alpha);
            // A neighbour on top of its point (the ends of a path): spaced like the segment.
            if (!(k0 > 1e-6f)) k0 = k1;
            if (!(k2 > 1e-6f)) k2 = k1;
            m1 = ((p1 - p0) / k0 - (p2 - p0) / (k0 + k1) + (p2 - p1) / k1) * k1;
            m2 = ((p2 - p1) / k1 - (p3 - p1) / (k1 + k2) + (p3 - p2) / k2) * k1;
            return true;
        }

        glm::vec3 CatmullRom(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3, float t, float alpha)
        {
            glm::vec3 m1, m2;
            if (!CatmullRomTangents(p0, p1, p2, p3, alpha, m1, m2))
                return p1;
            const float t2 = t * t, t3 = t2 * t;
            return (2.0f * t3 - 3.0f * t2 + 1.0f) * p1 + (t3 - 2.0f * t2 + t) * m1 + (-2.0f * t3 + 3.0f * t2) * p2 + (t3 - t2) * m2;
        }

        glm::vec3 CatmullRomDerivative(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3, float t, float alpha)
        {
            glm::vec3 m1, m2;
            if (!CatmullRomTangents(p0, p1, p2, p3, alpha, m1, m2))
                return glm::vec3(0.0f);
            const float t2 = t * t;
            return (6.0f * t2 - 6.0f * t) * p1 + (3.0f * t2 - 4.0f * t + 1.0f) * m1 + (-6.0f * t2 + 6.0f * t) * p2 + (3.0f * t2 - 2.0f * t) * m2;
        }

        glm::vec3 Bezier(const glm::vec3& p0, const glm::vec3& c1, const glm::vec3& c2, const glm::vec3& p3, float t)
        {
            const float s = 1.0f - t;
            return s * s * s * p0 + 3.0f * s * s * t * c1 + 3.0f * s * t * t * c2 + t * t * t * p3;
        }

        glm::vec3 BezierDerivative(const glm::vec3& p0, const glm::vec3& c1, const glm::vec3& c2, const glm::vec3& p3, float t)
        {
            const float s = 1.0f - t;
            return 3.0f * s * s * (c1 - p0) + 6.0f * s * t * (c2 - c1) + 3.0f * t * t * (p3 - c2);
        }

    }

    // ------------------------------------------------------------------ spline
    int Spline::GetSegmentCount() const
    {
        const int n = (int)Points.size();
        if (n < 2)
            return 0;
        return Closed ? n : n - 1;
    }

    namespace {

        // The four Catmull-Rom points of a segment: open ends get a mirrored neighbour.
        void SegmentPoints(const Spline& s, int segment, glm::vec3& p0, glm::vec3& p1, glm::vec3& p2, glm::vec3& p3)
        {
            const int n = (int)s.Points.size();
            auto at = [&](int i) { return s.Points[(size_t)(((i % n) + n) % n)].Position; };
            p1 = at(segment);
            p2 = at(segment + 1);
            if (s.Closed)
            {
                p0 = at(segment - 1);
                p3 = at(segment + 2);
            }
            else
            {
                p0 = segment > 0 ? at(segment - 1) : 2.0f * p1 - p2;
                p3 = segment + 2 < n ? at(segment + 2) : 2.0f * p2 - p1;
            }
        }

        // Parameter -> segment and t (closed splines wrap, open ones clamp).
        void Locate(const Spline& s, float u, int& segment, float& t)
        {
            const int count = s.GetSegmentCount();
            if (!std::isfinite(u))
                u = 0.0f;
            if (s.Closed)
            {
                u = std::fmod(u, (float)count);
                if (u < 0.0f)
                    u += (float)count;
            }
            u = std::clamp(u, 0.0f, (float)count);
            segment = std::min((int)std::floor(u), count - 1);
            t = u - (float)segment;
        }

    }

    glm::vec3 Spline::EvaluateSegment(int segment, float t) const
    {
        const int count = GetSegmentCount();
        if (count == 0)
            return Points.empty() ? glm::vec3(0.0f) : Points.front().Position;
        segment = std::clamp(segment, 0, count - 1);
        t = std::clamp(t, 0.0f, 1.0f);
        const SplinePoint& a = Points[(size_t)segment];
        const SplinePoint& b = Points[(size_t)((segment + 1) % (int)Points.size())];
        switch (Type)
        {
            case SplineType::Linear:
                return a.Position + (b.Position - a.Position) * t;
            case SplineType::Bezier:
                return SplineMath::Bezier(a.Position, a.Position + a.OutTangent, b.Position + b.InTangent, b.Position, t);
            default:
            {
                glm::vec3 p0, p1, p2, p3;
                SegmentPoints(*this, segment, p0, p1, p2, p3);
                return SplineMath::CatmullRom(p0, p1, p2, p3, t, Alpha);
            }
        }
    }

    glm::vec3 Spline::EvaluateSegmentDerivative(int segment, float t) const
    {
        const int count = GetSegmentCount();
        if (count == 0)
            return glm::vec3(0.0f);
        segment = std::clamp(segment, 0, count - 1);
        t = std::clamp(t, 0.0f, 1.0f);
        const SplinePoint& a = Points[(size_t)segment];
        const SplinePoint& b = Points[(size_t)((segment + 1) % (int)Points.size())];
        switch (Type)
        {
            case SplineType::Linear:
                return b.Position - a.Position;
            case SplineType::Bezier:
                return SplineMath::BezierDerivative(a.Position, a.Position + a.OutTangent, b.Position + b.InTangent, b.Position, t);
            default:
            {
                glm::vec3 p0, p1, p2, p3;
                SegmentPoints(*this, segment, p0, p1, p2, p3);
                return SplineMath::CatmullRomDerivative(p0, p1, p2, p3, t, Alpha);
            }
        }
    }

    glm::vec3 Spline::Evaluate(float u) const
    {
        if (GetSegmentCount() == 0)
            return Points.empty() ? glm::vec3(0.0f) : Points.front().Position;
        int segment;
        float t;
        Locate(*this, u, segment, t);
        return EvaluateSegment(segment, t);
    }

    glm::vec3 Spline::EvaluateDerivative(float u) const
    {
        if (GetSegmentCount() == 0)
            return glm::vec3(0.0f);
        int segment;
        float t;
        Locate(*this, u, segment, t);
        return EvaluateSegmentDerivative(segment, t);
    }

    glm::vec3 Spline::EvaluateTangent(float u) const
    {
        const int count = GetSegmentCount();
        if (count == 0)
            return glm::vec3(0.0f, 0.0f, -1.0f);
        int segment;
        float t;
        Locate(*this, u, segment, t);
        glm::vec3 d = EvaluateSegmentDerivative(segment, t);
        if (glm::dot(d, d) > 1e-12f)
            return glm::normalize(d);
        // Stalled (a Bezier end with a zero handle, coincident points): a little further, else the chord.
        const float step = t < 0.5f ? 1e-3f : -1e-3f;
        d = (EvaluateSegment(segment, t + step) - EvaluateSegment(segment, t)) * (step > 0.0f ? 1.0f : -1.0f);
        if (glm::dot(d, d) > 1e-14f)
            return glm::normalize(d);
        d = Points[(size_t)((segment + 1) % (int)Points.size())].Position - Points[(size_t)segment].Position;
        return glm::dot(d, d) > 1e-14f ? glm::normalize(d) : glm::vec3(0.0f, 0.0f, -1.0f);
    }

    SplineHit Spline::FindClosest(const glm::vec3& point, int samplesPerSegment) const
    {
        SplineHit hit;
        const int count = GetSegmentCount();
        if (count == 0)
        {
            hit.Position = Evaluate(0.0f);
            const glm::vec3 d = hit.Position - point;
            hit.DistanceSquared = glm::dot(d, d);
            return hit;
        }
        samplesPerSegment = std::clamp(samplesPerSegment, 2, 512);
        const int samples = count * samplesPerSegment;
        auto distance2 = [&](float u) {
            const glm::vec3 d = Evaluate(u) - point;
            return glm::dot(d, d);
        };
        float bestU = 0.0f, best = std::numeric_limits<float>::max();
        for (int i = 0; i <= samples; i++)
        {
            const float u = (float)i / (float)samplesPerSegment;
            if (const float d = distance2(u); d < best)
            {
                best = d;
                bestU = u;
            }
        }
        // Golden-section search around the best sample (the distance is unimodal that close).
        const float h = 1.0f / (float)samplesPerSegment;
        float lo = bestU - h, hi = bestU + h;
        if (!Closed)
        {
            lo = std::max(lo, 0.0f);
            hi = std::min(hi, (float)count);
        }
        constexpr float ratio = 0.61803398875f;
        float x1 = hi - ratio * (hi - lo), x2 = lo + ratio * (hi - lo);
        float f1 = distance2(x1), f2 = distance2(x2);
        for (int i = 0; i < 24; i++)
        {
            if (f1 < f2)
            {
                hi = x2;
                x2 = x1;
                f2 = f1;
                x1 = hi - ratio * (hi - lo);
                f1 = distance2(x1);
            }
            else
            {
                lo = x1;
                x1 = x2;
                f1 = f2;
                x2 = lo + ratio * (hi - lo);
                f2 = distance2(x2);
            }
        }
        float u = (lo + hi) * 0.5f;
        float d = distance2(u);
        if (best < d)
        {
            u = bestU;
            d = best;
        }
        if (Closed)
        {
            u = std::fmod(u, (float)count);
            if (u < 0.0f)
                u += (float)count;
        }
        hit.Parameter = u;
        hit.Position = Evaluate(u);
        hit.DistanceSquared = d;
        return hit;
    }

    int Spline::InsertPoint(float u)
    {
        const int count = GetSegmentCount();
        if (count == 0 || !std::isfinite(u))
            return -1;
        int segment;
        float t;
        Locate(*this, u, segment, t);
        if (t <= 1e-4f || t >= 1.0f - 1e-4f)
            return -1;
        const int n = (int)Points.size();
        const int next = (segment + 1) % n;
        SplinePoint point;
        if (Type == SplineType::Bezier)
        {
            // de Casteljau: the two halves keep the segment's shape exactly.
            SplinePoint& a = Points[(size_t)segment];
            SplinePoint& b = Points[(size_t)next];
            const glm::vec3 p0 = a.Position, c1 = a.Position + a.OutTangent, c2 = b.Position + b.InTangent, p3 = b.Position;
            const glm::vec3 q0 = glm::mix(p0, c1, t), q1 = glm::mix(c1, c2, t), q2 = glm::mix(c2, p3, t);
            const glm::vec3 r0 = glm::mix(q0, q1, t), r1 = glm::mix(q1, q2, t);
            const glm::vec3 s = glm::mix(r0, r1, t);
            a.OutTangent = q0 - p0;
            b.InTangent = q2 - p3;
            point.Position = s;
            point.InTangent = r0 - s;
            point.OutTangent = r1 - s;
            Points.insert(Points.begin() + segment + 1, point);
            return segment + 1;
        }
        point.Position = EvaluateSegment(segment, t);
        const glm::vec3 d = EvaluateSegmentDerivative(segment, t) * (1.0f / 3.0f);
        point.InTangent = -d * 0.5f;
        point.OutTangent = d * 0.5f;
        Points.insert(Points.begin() + segment + 1, point);
        return segment + 1;
    }

    void Spline::AutoTangents(int index)
    {
        const int n = (int)Points.size();
        if (n < 2)
            return;
        // The Catmull-Rom segments' end derivatives: Bezier handles of a third of them draw the
        // same curve.
        Spline cr = *this;
        cr.Type = SplineType::CatmullRom;
        const int count = cr.GetSegmentCount();
        for (int i = 0; i < n; i++)
        {
            if (index >= 0 && i != index)
                continue;
            const int before = Closed ? (i - 1 + n) % n : i - 1; // the segment ending at i
            const int after = Closed ? i % n : i;                // the segment starting at i
            glm::vec3 in(0.0f), out(0.0f);
            if (after < count)
                out = cr.EvaluateSegmentDerivative(after, 0.0f) / 3.0f;
            if (before >= 0 && before < count)
                in = -cr.EvaluateSegmentDerivative(before, 1.0f) / 3.0f;
            if (after >= count)
                out = -in; // the last point of an open path
            if (before < 0)
                in = -out; // the first one
            Points[(size_t)i].InTangent = in;
            Points[(size_t)i].OutTangent = out;
        }
    }

    // ------------------------------------------------------------------ length table
    void SplineLengthTable::Build(const Spline& spline, int samplesPerSegment)
    {
        m_Parameters.clear();
        m_Distances.clear();
        const int count = spline.GetSegmentCount();
        if (count == 0)
            return;
        samplesPerSegment = std::clamp(samplesPerSegment, 1, 1024);
        const int samples = count * samplesPerSegment;
        m_Parameters.reserve((size_t)samples + 1);
        m_Distances.reserve((size_t)samples + 1);
        glm::vec3 previous = spline.EvaluateSegment(0, 0.0f);
        float total = 0.0f;
        m_Parameters.push_back(0.0f);
        m_Distances.push_back(0.0f);
        for (int s = 0; s < count; s++)
            for (int i = 1; i <= samplesPerSegment; i++)
            {
                const float t = (float)i / (float)samplesPerSegment;
                const glm::vec3 p = spline.EvaluateSegment(s, t);
                total += glm::length(p - previous);
                previous = p;
                m_Parameters.push_back((float)s + t);
                m_Distances.push_back(total);
            }
    }

    float SplineLengthTable::GetParameter(float distance) const
    {
        if (IsEmpty())
            return 0.0f;
        if (!std::isfinite(distance) || distance <= 0.0f)
            return 0.0f;
        if (distance >= m_Distances.back())
            return m_Parameters.back();
        const size_t i = (size_t)(std::upper_bound(m_Distances.begin(), m_Distances.end(), distance) - m_Distances.begin());
        const float d0 = m_Distances[i - 1], d1 = m_Distances[i];
        const float f = d1 > d0 ? (distance - d0) / (d1 - d0) : 0.0f;
        return m_Parameters[i - 1] + (m_Parameters[i] - m_Parameters[i - 1]) * f;
    }

    float SplineLengthTable::GetDistance(float parameter) const
    {
        if (IsEmpty() || !std::isfinite(parameter) || parameter <= 0.0f)
            return 0.0f;
        if (parameter >= m_Parameters.back())
            return m_Distances.back();
        const size_t i = (size_t)(std::upper_bound(m_Parameters.begin(), m_Parameters.end(), parameter) - m_Parameters.begin());
        const float u0 = m_Parameters[i - 1], u1 = m_Parameters[i];
        const float f = u1 > u0 ? (parameter - u0) / (u1 - u0) : 0.0f;
        return m_Distances[i - 1] + (m_Distances[i] - m_Distances[i - 1]) * f;
    }

}
