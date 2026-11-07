#include "Zamin/Math/Geometry.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Zamin {

    // ------------------------------------------------------------------ AABB
    AABB AABB::FromPoints(std::span<const glm::vec3> points)
    {
        AABB box = Empty();
        for (const glm::vec3& p : points)
            box.Encapsulate(p);
        return box;
    }

    float AABB::GetVolume() const
    {
        if (!IsValid())
            return 0.0f;
        const glm::vec3 s = GetSize();
        return s.x * s.y * s.z;
    }

    float AABB::GetSurfaceArea() const
    {
        if (!IsValid())
            return 0.0f;
        const glm::vec3 s = GetSize();
        return 2.0f * (s.x * s.y + s.y * s.z + s.z * s.x);
    }

    AABB AABB::Transformed(const glm::mat4& transform) const
    {
        if (!IsValid())
            return *this;
        // Arvo: the new extents along each axis are the sums of the absolute matrix entries
        // times the old half extents (exact for an affine transform).
        glm::vec3 center = (Min + Max) * 0.5f;
        glm::vec3 half = (Max - Min) * 0.5f;
        glm::vec3 newCenter = glm::vec3(transform * glm::vec4(center, 1.0f));
        glm::vec3 newHalf(0.0f);
        for (int axis = 0; axis < 3; axis++)
            for (int k = 0; k < 3; k++)
                newHalf[axis] += std::abs(transform[k][axis]) * half[k];
        return { newCenter - newHalf, newCenter + newHalf };
    }

    bool AABB::Contains(const AABB& box) const
    {
        if (!IsValid() || !box.IsValid())
            return false;
        return box.Min.x >= Min.x && box.Max.x <= Max.x && box.Min.y >= Min.y && box.Max.y <= Max.y && box.Min.z >= Min.z && box.Max.z <= Max.z;
    }

    bool AABB::Intersects(const AABB& box) const
    {
        if (!IsValid() || !box.IsValid())
            return false;
        return Min.x <= box.Max.x && Max.x >= box.Min.x && Min.y <= box.Max.y && Max.y >= box.Min.y && Min.z <= box.Max.z && Max.z >= box.Min.z;
    }

    bool AABB::Intersects(const Sphere& sphere) const
    {
        return Geometry::Intersects(sphere, *this);
    }

    float AABB::DistanceSquared(const glm::vec3& point) const
    {
        const glm::vec3 d = point - ClosestPoint(point);
        return glm::dot(d, d);
    }

    float AABB::Distance(const glm::vec3& point) const
    {
        return std::sqrt(DistanceSquared(point));
    }

    // ------------------------------------------------------------------ Sphere
    Sphere Sphere::FromAABB(const AABB& box)
    {
        if (!box.IsValid())
            return {};
        return { box.GetCenter(), glm::length(box.GetExtents()) };
    }

    Sphere Sphere::FromPoints(std::span<const glm::vec3> points)
    {
        if (points.empty())
            return {};
        // Ritter: a first sphere on two far apart points (the farthest from any point, then the
        // farthest from that one), grown by every point still outside it.
        auto farthestFrom = [&](const glm::vec3& from) {
            size_t best = 0;
            float bestDistance = -1.0f;
            for (size_t i = 0; i < points.size(); i++)
                if (float d = glm::dot(points[i] - from, points[i] - from); d > bestDistance)
                {
                    bestDistance = d;
                    best = i;
                }
            return points[best];
        };
        const glm::vec3 y = farthestFrom(points[0]);
        const glm::vec3 z = farthestFrom(y);
        Sphere ritter{ (y + z) * 0.5f, glm::length(z - y) * 0.5f };
        for (const glm::vec3& p : points)
            ritter.Encapsulate(p);

        // The box's centre with the farthest point as radius: smaller for some shapes (cubes).
        const AABB box = AABB::FromPoints(points);
        Sphere boxed{ box.GetCenter(), 0.0f };
        for (const glm::vec3& p : points)
            boxed.Radius = std::max(boxed.Radius, glm::length(p - boxed.Center));
        return boxed.Radius < ritter.Radius ? boxed : ritter;
    }

    bool Sphere::Contains(const glm::vec3& point) const
    {
        const glm::vec3 d = point - Center;
        return glm::dot(d, d) <= Radius * Radius;
    }

    bool Sphere::Contains(const Sphere& other) const
    {
        return glm::length(other.Center - Center) + other.Radius <= Radius;
    }

    bool Sphere::Intersects(const Sphere& other) const
    {
        return Geometry::Intersects(*this, other);
    }

    bool Sphere::Intersects(const AABB& box) const
    {
        return Geometry::Intersects(*this, box);
    }

    void Sphere::Encapsulate(const glm::vec3& point)
    {
        const glm::vec3 d = point - Center;
        const float distance2 = glm::dot(d, d);
        if (distance2 <= Radius * Radius)
            return;
        // The new sphere spans from the far side of the old one to the point.
        const float distance = std::sqrt(distance2);
        const float radius = (Radius + distance) * 0.5f;
        Center += d * ((radius - Radius) / distance);
        Radius = radius;
    }

    void Sphere::Encapsulate(const Sphere& other)
    {
        const glm::vec3 d = other.Center - Center;
        const float distance = glm::length(d);
        if (distance + other.Radius <= Radius)
            return; // already inside
        if (distance + Radius <= other.Radius)
        {
            *this = other; // this one is inside the other
            return;
        }
        const float radius = (Radius + distance + other.Radius) * 0.5f;
        if (distance > 0.0f)
            Center += d * ((radius - Radius) / distance);
        Radius = radius;
    }

    glm::vec3 Sphere::ClosestPoint(const glm::vec3& point) const
    {
        const glm::vec3 d = point - Center;
        const float distance2 = glm::dot(d, d);
        if (distance2 <= Radius * Radius)
            return point;
        return Center + d * (Radius / std::sqrt(distance2));
    }

    float Sphere::SignedDistance(const glm::vec3& point) const
    {
        return glm::length(point - Center) - Radius;
    }

    Sphere Sphere::Transformed(const glm::mat4& transform) const
    {
        const float scale = std::sqrt(std::max({ glm::dot(glm::vec3(transform[0]), glm::vec3(transform[0])),
                                                 glm::dot(glm::vec3(transform[1]), glm::vec3(transform[1])),
                                                 glm::dot(glm::vec3(transform[2]), glm::vec3(transform[2])) }));
        return { glm::vec3(transform * glm::vec4(Center, 1.0f)), Radius * scale };
    }

    // ------------------------------------------------------------------ Ray
    Ray::Ray(const glm::vec3& origin, const glm::vec3& direction)
        : Origin(origin), Direction(0.0f)
    {
        const float length2 = glm::dot(direction, direction);
        if (length2 > 0.0f && std::isfinite(length2))
            Direction = direction / std::sqrt(length2);
    }

    Ray Ray::FromNDC(const glm::vec2& ndc, const glm::mat4& inverseViewProjection)
    {
        const glm::vec4 nearPoint = inverseViewProjection * glm::vec4(ndc, -1.0f, 1.0f);
        const glm::vec4 farPoint = inverseViewProjection * glm::vec4(ndc, 1.0f, 1.0f);
        Ray ray;
        ray.Direction = glm::vec3(0.0f);
        if (nearPoint.w == 0.0f || farPoint.w == 0.0f)
            return ray;
        ray.Origin = glm::vec3(nearPoint) / nearPoint.w;
        const glm::vec3 d = glm::vec3(farPoint) / farPoint.w - ray.Origin;
        const float length = glm::length(d);
        if (length >= 1e-8f && std::isfinite(length))
            ray.Direction = d / length;
        return ray;
    }

    Ray Ray::Transformed(const glm::mat4& transform) const
    {
        return Ray(glm::vec3(transform * glm::vec4(Origin, 1.0f)), glm::vec3(transform * glm::vec4(Direction, 0.0f)));
    }

    // ------------------------------------------------------------------ Plane
    Plane Plane::FromPointNormal(const glm::vec3& point, const glm::vec3& normal)
    {
        const float length = glm::length(normal);
        if (!(length > 0.0f))
            return { glm::vec3(0.0f), 0.0f };
        const glm::vec3 n = normal / length;
        return { n, -glm::dot(n, point) };
    }

    Plane Plane::FromPoints(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c)
    {
        return FromPointNormal(a, glm::cross(b - a, c - a));
    }

    Plane Plane::FromVector(const glm::vec4& plane)
    {
        const float length = glm::length(glm::vec3(plane));
        if (!(length > 0.0f))
            return { glm::vec3(0.0f), 0.0f };
        return { glm::vec3(plane) / length, plane.w / length };
    }

    Plane Plane::Transformed(const glm::mat4& transform) const
    {
        // A point of the plane moved by the transform, the normal by its inverse transpose.
        const glm::vec3 point = glm::vec3(transform * glm::vec4(-Normal * Distance, 1.0f));
        const glm::vec3 normal = glm::transpose(glm::inverse(glm::mat3(transform))) * Normal;
        return FromPointNormal(point, normal);
    }

    // ------------------------------------------------------------------ Frustum
    Frustum Frustum::FromViewProjection(const glm::mat4& m)
    {
        // Gribb & Hartmann: the planes are sums / differences of the matrix rows
        // (-w <= x, y, z <= w in clip space).
        glm::vec4 row[4];
        for (int i = 0; i < 4; i++)
            row[i] = glm::vec4(m[0][i], m[1][i], m[2][i], m[3][i]);
        Frustum f;
        f.Planes[0] = row[3] + row[0]; // left
        f.Planes[1] = row[3] - row[0]; // right
        f.Planes[2] = row[3] + row[1]; // bottom
        f.Planes[3] = row[3] - row[1]; // top
        f.Planes[4] = row[3] + row[2]; // near
        f.Planes[5] = row[3] - row[2]; // far
        for (glm::vec4& p : f.Planes)
        {
            float len = glm::length(glm::vec3(p));
            if (len > 1e-12f)
                p /= len;
        }
        return f;
    }

    bool Frustum::Intersects(const AABB& box) const
    {
        if (box.Min.x > box.Max.x || box.Min.y > box.Max.y || box.Min.z > box.Max.z)
            return false;
        for (const glm::vec4& p : Planes)
        {
            // The corner farthest along the plane's normal: when even it is outside, all of them are.
            glm::vec3 positive(p.x >= 0.0f ? box.Max.x : box.Min.x, p.y >= 0.0f ? box.Max.y : box.Min.y,
                               p.z >= 0.0f ? box.Max.z : box.Min.z);
            if (glm::dot(glm::vec3(p), positive) + p.w < 0.0f)
                return false;
        }
        return true;
    }

    bool Frustum::Intersects(const Sphere& sphere) const
    {
        for (const glm::vec4& p : Planes)
            if (glm::dot(glm::vec3(p), sphere.Center) + p.w < -sphere.Radius)
                return false;
        return true;
    }

    bool Frustum::Contains(const glm::vec3& point) const
    {
        for (const glm::vec4& p : Planes)
            if (glm::dot(glm::vec3(p), point) + p.w < 0.0f)
                return false;
        return true;
    }

}

namespace Zamin::Geometry {

    namespace {

        // The slab test on a line origin + direction * t (any direction length: t is in its units).
        // enterAxis: the axis of the face the line enters through (-1: it starts inside every slab).
        bool Slabs(const glm::vec3& origin, const glm::vec3& direction, const AABB& box, float& tEnter, float& tExit, int& enterAxis)
        {
            if (!box.IsValid())
                return false;
            float tMin = -Infinity, tMax = Infinity;
            enterAxis = -1;
            for (int i = 0; i < 3; i++)
            {
                if (std::abs(direction[i]) < 1e-30f)
                {
                    // Parallel to this slab: inside it or never.
                    if (origin[i] < box.Min[i] || origin[i] > box.Max[i])
                        return false;
                    continue;
                }
                const float inverse = 1.0f / direction[i];
                float t0 = (box.Min[i] - origin[i]) * inverse;
                float t1 = (box.Max[i] - origin[i]) * inverse;
                if (t0 > t1)
                    std::swap(t0, t1);
                if (t0 > tMin)
                {
                    tMin = t0;
                    enterAxis = i;
                }
                tMax = std::min(tMax, t1);
                if (tMin > tMax)
                    return false;
            }
            if (tMax < 0.0f)
                return false; // behind the origin
            tEnter = tMin;
            tExit = tMax;
            return true;
        }

        bool IsZero(const glm::vec3& v) { return !(glm::dot(v, v) > 0.0f); }

        RayHit MakeHit(const Ray& ray, float distance, const glm::vec3& normal)
        {
            RayHit hit;
            hit.Distance = distance;
            hit.Point = ray.GetPoint(distance);
            hit.Normal = normal;
            return hit;
        }

        // The ray started inside a volume.
        RayHit InsideHit(const Ray& ray)
        {
            return MakeHit(ray, 0.0f, glm::vec3(0.0f));
        }

    }

    // ------------------------------------------------------------------ rays
    bool IntersectRayAABB(const Ray& ray, const AABB& box, float& tEnter, float& tExit)
    {
        if (IsZero(ray.Direction))
            return false;
        int axis = -1;
        return Slabs(ray.Origin, ray.Direction, box, tEnter, tExit, axis);
    }

    std::optional<RayHit> Raycast(const Ray& ray, const AABB& box, float maxDistance)
    {
        if (IsZero(ray.Direction))
            return std::nullopt;
        float tEnter = 0.0f, tExit = 0.0f;
        int axis = -1;
        if (!Slabs(ray.Origin, ray.Direction, box, tEnter, tExit, axis))
            return std::nullopt;
        if (tEnter < 0.0f || axis < 0)
            return InsideHit(ray);
        if (tEnter > maxDistance)
            return std::nullopt;
        glm::vec3 normal(0.0f);
        normal[axis] = ray.Direction[axis] > 0.0f ? -1.0f : 1.0f;
        return MakeHit(ray, tEnter, normal);
    }

    std::optional<RayHit> Raycast(const Ray& ray, const AABB& localBox, const glm::mat4& transform, float maxDistance)
    {
        if (IsZero(ray.Direction) || std::abs(glm::determinant(transform)) < 1e-30f)
            return std::nullopt;
        // The ray in the box's space, its direction NOT normalized: t then stays the world distance.
        const glm::mat4 inverse = glm::inverse(transform);
        const glm::vec3 origin = glm::vec3(inverse * glm::vec4(ray.Origin, 1.0f));
        const glm::vec3 direction = glm::vec3(inverse * glm::vec4(ray.Direction, 0.0f));
        float tEnter = 0.0f, tExit = 0.0f;
        int axis = -1;
        if (!Slabs(origin, direction, localBox, tEnter, tExit, axis))
            return std::nullopt;
        if (tEnter < 0.0f || axis < 0)
            return InsideHit(ray);
        if (tEnter > maxDistance)
            return std::nullopt;
        glm::vec3 local(0.0f);
        local[axis] = direction[axis] > 0.0f ? -1.0f : 1.0f;
        // Normals go through the inverse transpose (non-uniform scale).
        glm::vec3 normal = glm::transpose(glm::mat3(inverse)) * local;
        const float length = glm::length(normal);
        normal = length > 0.0f ? normal / length : glm::vec3(0.0f);
        return MakeHit(ray, tEnter, normal);
    }

    std::optional<RayHit> Raycast(const Ray& ray, const Sphere& sphere, float maxDistance)
    {
        const float a = glm::dot(ray.Direction, ray.Direction);
        if (!(a > 0.0f))
            return std::nullopt;
        // |m + d t|^2 = r^2 with m = origin - centre.
        const glm::vec3 m = ray.Origin - sphere.Center;
        const float b = glm::dot(m, ray.Direction);
        const float c = glm::dot(m, m) - sphere.Radius * sphere.Radius;
        if (c <= 0.0f)
            return InsideHit(ray);
        if (b > 0.0f)
            return std::nullopt; // outside and pointing away
        const float discriminant = b * b - a * c;
        if (discriminant < 0.0f)
            return std::nullopt;
        const float t = (-b - std::sqrt(discriminant)) / a;
        if (t > maxDistance)
            return std::nullopt;
        RayHit hit = MakeHit(ray, std::max(t, 0.0f), glm::vec3(0.0f));
        if (sphere.Radius > 0.0f)
            hit.Normal = glm::normalize(hit.Point - sphere.Center);
        return hit;
    }

    std::optional<RayHit> Raycast(const Ray& ray, const Plane& plane, float maxDistance)
    {
        const float denominator = glm::dot(plane.Normal, ray.Direction);
        if (std::abs(denominator) < 1e-8f)
            return std::nullopt; // along the plane (or a zero direction / normal)
        const float t = -plane.SignedDistance(ray.Origin) / denominator;
        if (t < 0.0f || t > maxDistance)
            return std::nullopt;
        return MakeHit(ray, t, denominator < 0.0f ? plane.Normal : -plane.Normal);
    }

    std::optional<RayHit> Raycast(const Ray& ray, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, bool twoSided, float maxDistance)
    {
        // Moller-Trumbore.
        const glm::vec3 e1 = b - a, e2 = c - a;
        const glm::vec3 p = glm::cross(ray.Direction, e2);
        const float det = glm::dot(e1, p);
        // Nearly parallel to the triangle (relative to the sizes involved), or its back when one-sided.
        const float scale = std::sqrt(glm::dot(e1, e1) * glm::dot(e2, e2) * glm::dot(ray.Direction, ray.Direction));
        const float epsilon = 1e-7f * scale;
        if (!(scale > 0.0f) || (twoSided ? std::abs(det) <= epsilon : det <= epsilon))
            return std::nullopt;
        const float inverse = 1.0f / det;
        const glm::vec3 s = ray.Origin - a;
        const float u = glm::dot(s, p) * inverse;
        if (u < 0.0f || u > 1.0f)
            return std::nullopt;
        const glm::vec3 q = glm::cross(s, e1);
        const float v = glm::dot(ray.Direction, q) * inverse;
        if (v < 0.0f || u + v > 1.0f)
            return std::nullopt;
        const float t = glm::dot(e2, q) * inverse;
        if (t < 0.0f || t > maxDistance)
            return std::nullopt;
        glm::vec3 normal = glm::normalize(glm::cross(e1, e2));
        if (glm::dot(normal, ray.Direction) > 0.0f)
            normal = -normal;
        RayHit hit = MakeHit(ray, t, normal);
        hit.Barycentric = { u, v };
        return hit;
    }

    std::optional<RayHit> Raycast(const Ray& ray, std::span<const glm::vec3> positions, std::span<const uint32_t> indices, bool twoSided,
                                  float maxDistance)
    {
        std::optional<RayHit> best;
        const size_t triangles = indices.empty() ? positions.size() / 3 : indices.size() / 3;
        for (size_t i = 0; i < triangles; i++)
        {
            size_t i0 = i * 3, i1 = i * 3 + 1, i2 = i * 3 + 2;
            if (!indices.empty())
            {
                i0 = indices[i0];
                i1 = indices[i1];
                i2 = indices[i2];
                if (i0 >= positions.size() || i1 >= positions.size() || i2 >= positions.size())
                    continue;
            }
            if (auto hit = Raycast(ray, positions[i0], positions[i1], positions[i2], twoSided, best ? best->Distance : maxDistance))
            {
                if (!best || hit->Distance < best->Distance)
                {
                    best = hit;
                    best->Triangle = (int)i;
                }
            }
        }
        return best;
    }

    // ------------------------------------------------------------------ overlaps
    bool Intersects(const AABB& a, const AABB& b)
    {
        return a.Intersects(b);
    }

    bool Intersects(const Sphere& a, const Sphere& b)
    {
        const glm::vec3 d = a.Center - b.Center;
        const float r = a.Radius + b.Radius;
        return glm::dot(d, d) <= r * r;
    }

    bool Intersects(const Sphere& sphere, const AABB& box)
    {
        if (!box.IsValid())
            return false;
        return box.DistanceSquared(sphere.Center) <= sphere.Radius * sphere.Radius;
    }

    PlaneSide Classify(const AABB& box, const Plane& plane)
    {
        // The box's "radius" along the normal against the centre's distance.
        const float radius = glm::dot(box.GetExtents(), glm::abs(plane.Normal));
        const float distance = plane.SignedDistance(box.GetCenter());
        if (distance > radius)
            return PlaneSide::Front;
        if (distance < -radius)
            return PlaneSide::Back;
        return PlaneSide::Both;
    }

    PlaneSide Classify(const Sphere& sphere, const Plane& plane)
    {
        const float distance = plane.SignedDistance(sphere.Center);
        if (distance > sphere.Radius)
            return PlaneSide::Front;
        if (distance < -sphere.Radius)
            return PlaneSide::Back;
        return PlaneSide::Both;
    }

    bool Intersects(const AABB& box, const Plane& plane)
    {
        return box.IsValid() && Classify(box, plane) == PlaneSide::Both;
    }

    bool Intersects(const Sphere& sphere, const Plane& plane)
    {
        return Classify(sphere, plane) == PlaneSide::Both;
    }

    bool Intersects(const AABB& box, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c)
    {
        if (!box.IsValid())
            return false;
        // The triangle around the box's centre; the box is then -h..h.
        const glm::vec3 center = box.GetCenter(), h = box.GetExtents();
        const glm::vec3 v[3] = { a - center, b - center, c - center };
        auto separated = [&](const glm::vec3& axis) {
            const float p0 = glm::dot(v[0], axis), p1 = glm::dot(v[1], axis), p2 = glm::dot(v[2], axis);
            const float r = h.x * std::abs(axis.x) + h.y * std::abs(axis.y) + h.z * std::abs(axis.z);
            return std::max({ p0, p1, p2 }) < -r || std::min({ p0, p1, p2 }) > r;
        };
        // 1. The box's own axes: the triangle's box against it.
        for (int i = 0; i < 3; i++)
            if (std::max({ v[0][i], v[1][i], v[2][i] }) < -h[i] || std::min({ v[0][i], v[1][i], v[2][i] }) > h[i])
                return false;
        // 2. The triangle's plane.
        const glm::vec3 edges[3] = { v[1] - v[0], v[2] - v[1], v[0] - v[2] };
        const glm::vec3 normal = glm::cross(edges[0], edges[1]);
        if (glm::dot(normal, normal) > 0.0f && separated(normal))
            return false;
        // 3. Each edge crossed with each box axis.
        for (const glm::vec3& edge : edges)
            for (int i = 0; i < 3; i++)
            {
                glm::vec3 unit(0.0f);
                unit[i] = 1.0f;
                const glm::vec3 axis = glm::cross(unit, edge);
                if (glm::dot(axis, axis) > 1e-20f && separated(axis))
                    return false;
            }
        return true;
    }

    // ------------------------------------------------------------------ closest points
    glm::vec3 ClosestPointOnSegment(const glm::vec3& point, const glm::vec3& a, const glm::vec3& b, float* t)
    {
        const glm::vec3 ab = b - a;
        const float length2 = glm::dot(ab, ab);
        float s = length2 > 0.0f ? std::clamp(glm::dot(point - a, ab) / length2, 0.0f, 1.0f) : 0.0f;
        if (t)
            *t = s;
        return a + ab * s;
    }

    glm::vec3 ClosestPointOnLine(const glm::vec3& point, const glm::vec3& origin, const glm::vec3& direction, float* t)
    {
        const float length2 = glm::dot(direction, direction);
        const float s = length2 > 0.0f ? glm::dot(point - origin, direction) / length2 : 0.0f;
        if (t)
            *t = s;
        return origin + direction * s;
    }

    glm::vec3 ClosestPointOnRay(const glm::vec3& point, const Ray& ray, float* distance)
    {
        float t = 0.0f;
        ClosestPointOnLine(point, ray.Origin, ray.Direction, &t);
        t = std::max(t, 0.0f);
        if (distance)
            *distance = t;
        return ray.Origin + ray.Direction * t;
    }

    glm::vec3 ClosestPointOnTriangle(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, glm::vec3* barycentric)
    {
        // Ericson, Real-Time Collision Detection 5.1.5: which Voronoi region of the triangle p is in.
        auto result = [&](const glm::vec3& weights) {
            if (barycentric)
                *barycentric = weights;
            return a * weights.x + b * weights.y + c * weights.z;
        };
        const glm::vec3 ab = b - a, ac = c - a, ap = p - a;
        const float d1 = glm::dot(ab, ap), d2 = glm::dot(ac, ap);
        if (d1 <= 0.0f && d2 <= 0.0f)
            return result({ 1.0f, 0.0f, 0.0f }); // vertex a
        const glm::vec3 bp = p - b;
        const float d3 = glm::dot(ab, bp), d4 = glm::dot(ac, bp);
        if (d3 >= 0.0f && d4 <= d3)
            return result({ 0.0f, 1.0f, 0.0f }); // vertex b
        const float vc = d1 * d4 - d3 * d2;
        if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f)
        {
            const float v = d1 / (d1 - d3);
            return result({ 1.0f - v, v, 0.0f }); // edge ab
        }
        const glm::vec3 cp = p - c;
        const float d5 = glm::dot(ab, cp), d6 = glm::dot(ac, cp);
        if (d6 >= 0.0f && d5 <= d6)
            return result({ 0.0f, 0.0f, 1.0f }); // vertex c
        const float vb = d5 * d2 - d1 * d6;
        if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f)
        {
            const float w = d2 / (d2 - d6);
            return result({ 1.0f - w, 0.0f, w }); // edge ac
        }
        const float va = d3 * d6 - d5 * d4;
        if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f)
        {
            const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
            return result({ 0.0f, 1.0f - w, w }); // edge bc
        }
        const float sum = va + vb + vc;
        if (!(std::abs(sum) > 0.0f))
            return result({ 1.0f, 0.0f, 0.0f }); // degenerate
        const float denominator = 1.0f / sum;
        const float v = vb * denominator, w = vc * denominator;
        return result({ 1.0f - v - w, v, w }); // inside the face
    }

    float ClosestPointsOnSegments(const glm::vec3& p1, const glm::vec3& q1, const glm::vec3& p2, const glm::vec3& q2, glm::vec3& c1, glm::vec3& c2,
                                  float* sOut, float* tOut)
    {
        // Ericson 5.1.9.
        constexpr float Epsilon = 1e-12f;
        const glm::vec3 d1 = q1 - p1, d2 = q2 - p2, r = p1 - p2;
        const float a = glm::dot(d1, d1), e = glm::dot(d2, d2), f = glm::dot(d2, r);
        float s = 0.0f, t = 0.0f;
        if (a <= Epsilon && e <= Epsilon)
        {
            s = t = 0.0f; // both are points
        }
        else if (a <= Epsilon)
        {
            t = std::clamp(f / e, 0.0f, 1.0f); // the first is a point
        }
        else
        {
            const float c = glm::dot(d1, r);
            if (e <= Epsilon)
                s = std::clamp(-c / a, 0.0f, 1.0f); // the second is a point
            else
            {
                const float b = glm::dot(d1, d2);
                const float denominator = a * e - b * b;
                // Parallel: any s will do, 0.
                s = denominator > Epsilon * a * e ? std::clamp((b * f - c * e) / denominator, 0.0f, 1.0f) : 0.0f;
                t = (b * s + f) / e;
                if (t < 0.0f)
                {
                    t = 0.0f;
                    s = std::clamp(-c / a, 0.0f, 1.0f);
                }
                else if (t > 1.0f)
                {
                    t = 1.0f;
                    s = std::clamp((b - c) / a, 0.0f, 1.0f);
                }
            }
        }
        c1 = p1 + d1 * s;
        c2 = p2 + d2 * t;
        if (sOut)
            *sOut = s;
        if (tOut)
            *tOut = t;
        const glm::vec3 d = c1 - c2;
        return glm::dot(d, d);
    }

    bool ClosestPointsOnLines(const glm::vec3& origin1, const glm::vec3& direction1, const glm::vec3& origin2, const glm::vec3& direction2,
                              float& t1, float& t2)
    {
        const glm::vec3 r = origin1 - origin2;
        const float a = glm::dot(direction1, direction1), e = glm::dot(direction2, direction2);
        const float b = glm::dot(direction1, direction2);
        const float denominator = a * e - b * b;
        // sin^2 of the angle between them below 1e-5 (as the editor's line handles had it): parallel.
        if (!(a > 0.0f) || !(e > 0.0f) || denominator < 1e-5f * a * e)
            return false;
        const float c = glm::dot(direction1, r), f = glm::dot(direction2, r);
        t1 = (b * f - c * e) / denominator;
        t2 = (a * f - b * c) / denominator;
        return true;
    }

    // ------------------------------------------------------------------ triangles
    glm::vec3 TriangleNormal(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c)
    {
        const glm::vec3 n = glm::cross(b - a, c - a);
        const float length = glm::length(n);
        return length > 0.0f ? n / length : glm::vec3(0.0f);
    }

    float TriangleArea(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c)
    {
        return 0.5f * glm::length(glm::cross(b - a, c - a));
    }

    glm::vec3 Barycentric(const glm::vec3& point, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c)
    {
        const glm::vec3 v0 = b - a, v1 = c - a, v2 = point - a;
        const float d00 = glm::dot(v0, v0), d01 = glm::dot(v0, v1), d11 = glm::dot(v1, v1);
        const float d20 = glm::dot(v2, v0), d21 = glm::dot(v2, v1);
        const float denominator = d00 * d11 - d01 * d01;
        if (!(std::abs(denominator) > 1e-20f))
            return { 1.0f, 0.0f, 0.0f };
        const float v = (d11 * d20 - d01 * d21) / denominator;
        const float w = (d00 * d21 - d01 * d20) / denominator;
        return { 1.0f - v - w, v, w };
    }

}
