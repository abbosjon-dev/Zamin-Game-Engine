#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

// Bounding volumes (AABB, Sphere), rays, planes and the camera frustum, with the intersection and
// closest-point tests between them (Zamin::Geometry). Used by the renderer (culling, bounds), the
// editor (picking, gizmos) and game scripts.
//
//   Ray ray = Ray::FromNDC(mouseNdc, glm::inverse(camera.GetViewProjection()));
//   if (auto hit = Geometry::Raycast(ray, box))            // the first point where the ray meets the box
//       DebugDraw::Sphere(hit->Point, 0.1f);
//   if (Geometry::Intersects(Sphere{ position, 2.0f }, enemyBounds)) { ... }
//   glm::vec3 p = Geometry::ClosestPointOnSegment(target, railStart, railEnd);
//
// Conventions: boxes are inclusive (touching counts as intersecting); ray distances are measured
// along Ray::Direction, which the constructor makes unit length (world units then); a Plane is the
// points p with dot(Normal, p) + Distance == 0 and its front is where that is positive.
namespace Zamin {

    struct Sphere;

    // ------------------------------------------------------------------ AABB
    // An axis-aligned box from Min to Max. The default box is the single point at the origin; start
    // from AABB::Empty() to grow one around points or boxes.
    struct AABB
    {
        glm::vec3 Min{ 0.0f };
        glm::vec3 Max{ 0.0f };

        // A box that holds nothing (Min = +FLT_MAX, Max = -FLT_MAX: not IsValid). Encapsulating a
        // point or a box into it makes it exactly that.
        static AABB Empty()
        {
            return { glm::vec3(std::numeric_limits<float>::max()), glm::vec3(-std::numeric_limits<float>::max()) };
        }
        static AABB FromCenterExtents(const glm::vec3& center, const glm::vec3& extents) { return { center - extents, center + extents }; }
        // The box around the points (Empty for none).
        static AABB FromPoints(std::span<const glm::vec3> points);

        // Min <= Max on every axis (a flat or single-point box is valid; Empty() is not).
        bool IsValid() const { return Min.x <= Max.x && Min.y <= Max.y && Min.z <= Max.z; }
        glm::vec3 GetCenter() const { return (Min + Max) * 0.5f; }
        glm::vec3 GetSize() const { return Max - Min; }
        // Half the size.
        glm::vec3 GetExtents() const { return (Max - Min) * 0.5f; }
        // 0 for an invalid box.
        float GetVolume() const;
        float GetSurfaceArea() const;
        // Corner 0..7; index bits: 1 = Max.x (else Min.x), 2 = Max.y, 4 = Max.z.
        glm::vec3 GetCorner(int index) const
        {
            return { (index & 1) ? Max.x : Min.x, (index & 2) ? Max.y : Min.y, (index & 4) ? Max.z : Min.z };
        }

        // Grows to hold the point / the box (an invalid box adds nothing).
        void Encapsulate(const glm::vec3& point)
        {
            Min = glm::min(Min, point);
            Max = glm::max(Max, point);
        }
        void Encapsulate(const AABB& box)
        {
            if (!box.IsValid())
                return;
            Min = glm::min(Min, box.Min);
            Max = glm::max(Max, box.Max);
        }
        AABB Merged(const AABB& other) const
        {
            AABB out = *this;
            out.Encapsulate(other);
            return out;
        }
        // Every side moved out by `amount` (negative: in).
        AABB Expanded(float amount) const { return { Min - glm::vec3(amount), Max + glm::vec3(amount) }; }
        AABB Expanded(const glm::vec3& amount) const { return { Min - amount, Max + amount }; }
        // The axis-aligned box around this one after `transform` (exact: the tightest box around the
        // moved one, Arvo's method). An invalid box stays as it is.
        AABB Transformed(const glm::mat4& transform) const;

        // Inclusive: a point on a face is inside.
        bool Contains(const glm::vec3& point) const
        {
            return point.x >= Min.x && point.x <= Max.x && point.y >= Min.y && point.y <= Max.y && point.z >= Min.z && point.z <= Max.z;
        }
        // The whole box is inside (invalid boxes are inside nothing and hold nothing).
        bool Contains(const AABB& box) const;
        // Overlapping or touching (an invalid box meets nothing).
        bool Intersects(const AABB& box) const;
        bool Intersects(const Sphere& sphere) const;
        // The point of the (solid) box nearest to `point`: the point itself when it is inside.
        glm::vec3 ClosestPoint(const glm::vec3& point) const { return glm::clamp(point, Min, Max); }
        // From `point` to the box (0 inside).
        float DistanceSquared(const glm::vec3& point) const;
        float Distance(const glm::vec3& point) const;

        bool operator==(const AABB&) const = default;
    };

    // ------------------------------------------------------------------ Sphere
    struct Sphere
    {
        glm::vec3 Center{ 0.0f };
        float Radius = 0.0f;

        // The sphere through the box's corners.
        static Sphere FromAABB(const AABB& box);
        // A sphere around all the points (Ritter's method, or the box's sphere when that is smaller):
        // not always the smallest one (usually within ~10% of its radius). None: radius 0 at the origin.
        static Sphere FromPoints(std::span<const glm::vec3> points);

        bool Contains(const glm::vec3& point) const;
        bool Contains(const Sphere& other) const;
        bool Intersects(const Sphere& other) const;
        bool Intersects(const AABB& box) const;
        // Grows (moving its centre as little as needed) to hold the point / the sphere.
        void Encapsulate(const glm::vec3& point);
        void Encapsulate(const Sphere& other);
        // The point of the solid sphere nearest to `point` (itself when inside).
        glm::vec3 ClosestPoint(const glm::vec3& point) const;
        // From `point` to the surface: negative inside.
        float SignedDistance(const glm::vec3& point) const;
        AABB GetBounds() const { return AABB::FromCenterExtents(Center, glm::vec3(Radius)); }
        // The centre moved, the radius scaled by the transform's largest axis scale (still holds
        // everything the sphere held).
        Sphere Transformed(const glm::mat4& transform) const;

        bool operator==(const Sphere&) const = default;
    };

    // ------------------------------------------------------------------ Ray
    struct Ray
    {
        glm::vec3 Origin{ 0.0f };
        glm::vec3 Direction{ 0.0f, 0.0f, -1.0f };

        Ray() = default;
        // The direction is made unit length (a zero direction stays zero: that ray hits nothing).
        Ray(const glm::vec3& origin, const glm::vec3& direction);
        // From `from` towards `to`.
        static Ray FromPoints(const glm::vec3& from, const glm::vec3& to) { return Ray(from, to - from); }
        // The camera ray through a point of the screen: `ndc` -1..1 (x right, y up), `inverseViewProjection`
        // the inverse of the camera's projection x view (OpenGL clip space, perspective or orthographic).
        // It starts on the near plane. A projection that cannot be inverted: a zero direction.
        static Ray FromNDC(const glm::vec2& ndc, const glm::mat4& inverseViewProjection);

        glm::vec3 GetPoint(float distance) const { return Origin + Direction * distance; }
        // The ray in another space (direction re-normalized: distances along it are that space's).
        Ray Transformed(const glm::mat4& transform) const;
    };

    // ------------------------------------------------------------------ Plane
    // The points p with dot(Normal, p) + Distance == 0; Normal is unit length. The same as the
    // glm::vec4 planes of Frustum (xyz = Normal, w = Distance).
    struct Plane
    {
        glm::vec3 Normal{ 0.0f, 1.0f, 0.0f };
        float Distance = 0.0f;

        // The normal is normalized (zero: an invalid plane, Normal zero).
        static Plane FromPointNormal(const glm::vec3& point, const glm::vec3& normal);
        // Through three points, counter-clockwise seen from the front. Points on a line: Normal zero.
        static Plane FromPoints(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c);
        // xyz = normal, w = distance; normalized.
        static Plane FromVector(const glm::vec4& plane);
        glm::vec4 ToVector() const { return { Normal, Distance }; }

        bool IsValid() const { return glm::dot(Normal, Normal) > 0.0f; }
        // Positive in front (the side the normal points to), negative behind.
        float SignedDistance(const glm::vec3& point) const { return glm::dot(Normal, point) + Distance; }
        bool IsInFront(const glm::vec3& point) const { return SignedDistance(point) > 0.0f; }
        // `point` dropped onto the plane.
        glm::vec3 ClosestPoint(const glm::vec3& point) const { return point - Normal * SignedDistance(point); }
        Plane Flipped() const { return { -Normal, -Distance }; }
        // The plane in another space (normals through the inverse transpose; works with any scale).
        Plane Transformed(const glm::mat4& transform) const;

        bool operator==(const Plane&) const = default;
    };

    // ------------------------------------------------------------------ Frustum
    // The six planes of a camera's view volume, taken from its projection x view matrix
    // (perspective or orthographic, OpenGL clip space). Used to skip what the camera cannot see.
    struct Frustum
    {
        // Left, right, bottom, top, near, far. A point p is inside a plane when
        // dot(plane.xyz, p) + plane.w >= 0; the normals are unit length (w is a distance).
        glm::vec4 Planes[6]{};

        static Frustum FromViewProjection(const glm::mat4& viewProjection);

        Plane GetPlane(int index) const { return { glm::vec3(Planes[index]), Planes[index].w }; }
        // False only when the box is entirely outside one of the planes (conservative: a box
        // near a corner of the volume may still count as visible). An empty box (Min > Max) is
        // never visible.
        bool Intersects(const AABB& box) const;
        // The same for a sphere (conservative in the same way).
        bool Intersects(const Sphere& sphere) const;
        bool Contains(const glm::vec3& point) const;
    };

    // The axis-aligned box around `box` after `transform` (each corner transformed; exact for
    // the box, the tightest axis-aligned box around the rotated one). The same as box.Transformed.
    inline AABB TransformAABB(const AABB& box, const glm::mat4& transform) { return box.Transformed(transform); }

    // ------------------------------------------------------------------ hits
    // Where a ray met something.
    struct RayHit
    {
        float Distance = 0.0f;         // along the ray (world units for a unit direction)
        glm::vec3 Point{ 0.0f };       // Origin + Direction * Distance
        glm::vec3 Normal{ 0.0f };      // the surface's unit normal there, facing the ray's side (zero: the ray started inside)
        glm::vec2 Barycentric{ 0.0f }; // triangles: (u, v) with Point = A * (1 - u - v) + B * u + C * v
        int Triangle = -1;             // triangle lists: which triangle (its first index / 3)
    };

    // Which side of a plane something lies on.
    enum class PlaneSide : uint8_t { Front = 0, Back, Both };

}

namespace Zamin::Geometry {

    constexpr float Infinity = std::numeric_limits<float>::infinity();

    // ------------------------------------------------------------------ rays
    // Rays never hit what lies behind their origin; a ray with a zero direction hits nothing.
    // A ray that starts inside a box or sphere hits it at Distance 0 (at its origin, Normal zero).

    // The slab test: the part of the ray's line inside the box is tEnter..tExit (tEnter < 0: the
    // origin is inside). False when the line misses the box or it lies behind the origin.
    bool IntersectRayAABB(const Ray& ray, const AABB& box, float& tEnter, float& tExit);
    std::optional<RayHit> Raycast(const Ray& ray, const AABB& box, float maxDistance = Infinity);
    // A box in its own space moved by `transform` (an oriented box: an entity's local bounds and its
    // world matrix). Distances, point and normal are in the ray's (world) space.
    std::optional<RayHit> Raycast(const Ray& ray, const AABB& localBox, const glm::mat4& transform, float maxDistance = Infinity);
    std::optional<RayHit> Raycast(const Ray& ray, const Sphere& sphere, float maxDistance = Infinity);
    // Both sides of the plane; none when the ray runs along it.
    std::optional<RayHit> Raycast(const Ray& ray, const Plane& plane, float maxDistance = Infinity);
    // One triangle (Moller-Trumbore). `twoSided` false: only its front (a, b, c counter-clockwise
    // seen from the ray) is hit.
    std::optional<RayHit> Raycast(const Ray& ray, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, bool twoSided = true,
                                  float maxDistance = Infinity);
    // The nearest hit on a triangle list: `indices` three per triangle (empty: the positions taken
    // three at a time). Indices out of range are skipped. RayHit::Triangle says which one.
    std::optional<RayHit> Raycast(const Ray& ray, std::span<const glm::vec3> positions, std::span<const uint32_t> indices, bool twoSided = true,
                                  float maxDistance = Infinity);

    // ------------------------------------------------------------------ overlaps (touching counts)
    bool Intersects(const AABB& a, const AABB& b);
    bool Intersects(const Sphere& a, const Sphere& b);
    bool Intersects(const Sphere& sphere, const AABB& box);
    inline bool Intersects(const AABB& box, const Sphere& sphere) { return Intersects(sphere, box); }
    bool Intersects(const AABB& box, const Plane& plane);
    bool Intersects(const Sphere& sphere, const Plane& plane);
    // The triangle a, b, c and the box (separating axis test, Akenine-Moller).
    bool Intersects(const AABB& box, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c);
    // Front: entirely in front of the plane; Back: entirely behind; Both: it crosses (or touches) it.
    PlaneSide Classify(const AABB& box, const Plane& plane);
    PlaneSide Classify(const Sphere& sphere, const Plane& plane);

    // ------------------------------------------------------------------ closest points
    // On the segment a..b; `t` (optional) gets where along it (0 = a, 1 = b).
    glm::vec3 ClosestPointOnSegment(const glm::vec3& point, const glm::vec3& a, const glm::vec3& b, float* t = nullptr);
    // On the infinite line origin + direction * t (any direction length; zero: the origin).
    glm::vec3 ClosestPointOnLine(const glm::vec3& point, const glm::vec3& origin, const glm::vec3& direction, float* t = nullptr);
    // On the ray (never behind its origin); `distance` gets how far along it.
    glm::vec3 ClosestPointOnRay(const glm::vec3& point, const Ray& ray, float* distance = nullptr);
    inline glm::vec3 ClosestPointOnAABB(const glm::vec3& point, const AABB& box) { return box.ClosestPoint(point); }
    inline glm::vec3 ClosestPointOnSphere(const glm::vec3& point, const Sphere& sphere) { return sphere.ClosestPoint(point); }
    inline glm::vec3 ClosestPointOnPlane(const glm::vec3& point, const Plane& plane) { return plane.ClosestPoint(point); }
    // On the (solid) triangle; `barycentric` (optional) gets its weights for a, b, c.
    glm::vec3 ClosestPointOnTriangle(const glm::vec3& point, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c,
                                     glm::vec3* barycentric = nullptr);
    // The nearest points c1 on p1..q1 and c2 on p2..q2 (s, t: where along each, 0..1). Returns the
    // squared distance between them.
    float ClosestPointsOnSegments(const glm::vec3& p1, const glm::vec3& q1, const glm::vec3& p2, const glm::vec3& q2, glm::vec3& c1, glm::vec3& c2,
                                  float* s = nullptr, float* t = nullptr);
    // Where two infinite lines (origin + direction * t) come closest: t1 along the first, t2 along
    // the second. False (nothing written) when they are parallel or a direction is zero.
    bool ClosestPointsOnLines(const glm::vec3& origin1, const glm::vec3& direction1, const glm::vec3& origin2, const glm::vec3& direction2,
                              float& t1, float& t2);

    // ------------------------------------------------------------------ triangles
    // The unit normal of a, b, c counter-clockwise (zero for a degenerate triangle).
    glm::vec3 TriangleNormal(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c);
    float TriangleArea(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c);
    // The weights (wa, wb, wc; summing to 1) of `point` dropped onto the triangle's plane. A degenerate
    // triangle: (1, 0, 0).
    glm::vec3 Barycentric(const glm::vec3& point, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c);

}
