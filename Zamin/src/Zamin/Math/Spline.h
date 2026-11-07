#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

// Smooth paths through points: Catmull-Rom (through every point; centripetal by default, so no
// loops or cusps where points are close together), cubic Bezier (through the points, shaped by
// two handles per point) and straight lines. Paths are evaluated by parameter (u: 0..segment count,
// segment index + fraction) or by distance along them (SplineLengthTable).
//
//   Spline s;
//   s.Points = { { { 0, 0, 0 } }, { { 4, 2, 0 } }, { { 8, 0, 0 } } };
//   SplineLengthTable table(s);
//   glm::vec3 p = s.Evaluate(table.GetParameter(table.GetLength() * 0.5f)); // half way along
namespace Zamin {

    enum class SplineType : uint8_t { CatmullRom = 0, Bezier, Linear };
    constexpr int SplineTypeCount = 3;
    const char* SplineTypeToString(SplineType type);
    std::optional<SplineType> SplineTypeFromString(std::string_view text);

    struct SplinePoint
    {
        glm::vec3 Position{ 0.0f };
        // Bezier: the handles before / after the point, relative to it (other types ignore them).
        glm::vec3 InTangent{ -1.0f, 0.0f, 0.0f };
        glm::vec3 OutTangent{ 1.0f, 0.0f, 0.0f };

        bool operator==(const SplinePoint&) const = default;
    };

    // Where a point of space is nearest to a spline.
    struct SplineHit
    {
        float Parameter = 0.0f;   // u
        glm::vec3 Position{ 0.0f };
        float DistanceSquared = 0.0f;
    };

    struct Spline
    {
        std::vector<SplinePoint> Points;
        SplineType Type = SplineType::CatmullRom;
        bool Closed = false;     // the last point joins the first
        // Catmull-Rom knot spacing: 0 uniform, 0.5 centripetal (no self-intersections or cusps
        // within a segment), 1 chordal.
        float Alpha = 0.5f;

        // Segments: points - 1 (closed: points; fewer than 2 points: none).
        int GetSegmentCount() const;
        // The parameter's range: 0..GetSegmentCount().
        float GetParameterEnd() const { return (float)GetSegmentCount(); }
        // Position / derivative (d position / d u, not normalised) at a parameter (clamped; closed
        // splines wrap). Fewer than 2 points: the point (or the origin), derivative 0.
        glm::vec3 Evaluate(float u) const;
        glm::vec3 EvaluateDerivative(float u) const;
        // A unit direction along the spline at u (falls back to the chord where it stalls).
        glm::vec3 EvaluateTangent(float u) const;
        // One segment at t (0..1).
        glm::vec3 EvaluateSegment(int segment, float t) const;
        glm::vec3 EvaluateSegmentDerivative(int segment, float t) const;

        // The nearest point of the spline to `point`: sampled `samplesPerSegment` times per segment,
        // then refined.
        SplineHit FindClosest(const glm::vec3& point, int samplesPerSegment = 16) const;

        // A new point at parameter u on the curve (the curve's shape kept: Bezier handles split,
        // de Casteljau); returns its index (-1 when u is not inside a segment).
        int InsertPoint(float u);
        // Bezier handles from the Catmull-Rom tangents (smooth through the points): one point or
        // all (-1).
        void AutoTangents(int index = -1);

        bool operator==(const Spline&) const = default;
    };

    // Distance along a spline <-> parameter: the curve sampled per segment, the lengths in between
    // summed (and interpolated). Build it again after the spline changes.
    class SplineLengthTable
    {
    public:
        SplineLengthTable() = default;
        explicit SplineLengthTable(const Spline& spline, int samplesPerSegment = 32) { Build(spline, samplesPerSegment); }

        void Build(const Spline& spline, int samplesPerSegment = 32);
        bool IsEmpty() const { return m_Distances.size() < 2; }
        float GetLength() const { return m_Distances.empty() ? 0.0f : m_Distances.back(); }
        // The parameter `distance` along the spline (clamped to 0..length).
        float GetParameter(float distance) const;
        // How far along the spline a parameter is.
        float GetDistance(float parameter) const;

    private:
        std::vector<float> m_Parameters;
        std::vector<float> m_Distances;
    };

    // The pure curve arithmetic.
    namespace SplineMath {

        // Centripetal (alpha 0.5) / uniform (0) / chordal (1) Catmull-Rom between p1 and p2, with
        // neighbours p0 and p3, at t 0..1 - and its derivative by t.
        glm::vec3 CatmullRom(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3, float t, float alpha = 0.5f);
        glm::vec3 CatmullRomDerivative(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3, float t, float alpha = 0.5f);
        // Cubic Bezier through p0 and p3 with control points c1, c2.
        glm::vec3 Bezier(const glm::vec3& p0, const glm::vec3& c1, const glm::vec3& c2, const glm::vec3& p3, float t);
        glm::vec3 BezierDerivative(const glm::vec3& p0, const glm::vec3& c1, const glm::vec3& c2, const glm::vec3& p3, float t);

    }

}
