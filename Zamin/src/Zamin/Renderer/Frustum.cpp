#include "Zamin/Renderer/Frustum.h"

#include <algorithm>
#include <cmath>

namespace Zamin {

    std::array<glm::vec3, 8> FrustumCorners(const glm::mat4& projection, const glm::mat4& cameraWorld)
    {
        std::array<glm::vec3, 8> corners;
        corners.fill(glm::vec3(cameraWorld[3]));
        if (std::abs(glm::determinant(projection)) < 1e-30f)
            return corners;
        // The clip space cube's corners back into view space, then into the world (the view
        // matrix is not inverted: the camera's own transform is exact).
        const glm::mat4 inverse = glm::inverse(projection);
        for (int i = 0; i < 8; i++)
        {
            glm::vec4 ndc((i & 1) ? 1.0f : -1.0f, (i & 2) ? 1.0f : -1.0f, (i & 4) ? 1.0f : -1.0f, 1.0f);
            glm::vec4 view = inverse * ndc;
            if (!(std::abs(view.w) > 1e-30f))
                continue;
            glm::vec3 p = glm::vec3(cameraWorld * glm::vec4(glm::vec3(view) / view.w, 1.0f));
            if (std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z))
                corners[i] = p;
        }
        return corners;
    }

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

    bool Frustum::Contains(const glm::vec3& point) const
    {
        for (const glm::vec4& p : Planes)
            if (glm::dot(glm::vec3(p), point) + p.w < 0.0f)
                return false;
        return true;
    }

    AABB TransformAABB(const AABB& box, const glm::mat4& transform)
    {
        // Arvo: the new extents along each axis are the sums of the absolute matrix entries
        // times the old half extents (exact for an affine transform).
        glm::vec3 center = (box.Min + box.Max) * 0.5f;
        glm::vec3 half = (box.Max - box.Min) * 0.5f;
        glm::vec3 newCenter = glm::vec3(transform * glm::vec4(center, 1.0f));
        glm::vec3 newHalf(0.0f);
        for (int axis = 0; axis < 3; axis++)
            for (int k = 0; k < 3; k++)
                newHalf[axis] += std::abs(transform[k][axis]) * half[k];
        return { newCenter - newHalf, newCenter + newHalf };
    }

    AABB SkinnedBounds(const AABB& bindBounds, const glm::mat4* joints, size_t jointCount)
    {
        if (!joints || jointCount == 0)
            return bindBounds;
        AABB out = TransformAABB(bindBounds, joints[0]);
        for (size_t i = 1; i < jointCount; i++)
        {
            AABB moved = TransformAABB(bindBounds, joints[i]);
            out.Min = glm::min(out.Min, moved.Min);
            out.Max = glm::max(out.Max, moved.Max);
        }
        return out;
    }

}
