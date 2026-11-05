#pragma once

#include "Zamin/Renderer/Mesh.h"

#include <glm/glm.hpp>

#include <cstddef>

namespace Zamin {

    // The six planes of a camera's view volume, taken from its projection x view matrix
    // (perspective or orthographic, OpenGL clip space). Used to skip what the camera cannot see.
    struct Frustum
    {
        // Left, right, bottom, top, near, far. A point p is inside a plane when
        // dot(plane.xyz, p) + plane.w >= 0; the normals are unit length (w is a distance).
        glm::vec4 Planes[6]{};

        static Frustum FromViewProjection(const glm::mat4& viewProjection);

        // False only when the box is entirely outside one of the planes (conservative: a box
        // near a corner of the volume may still count as visible). An empty box (Min > Max) is
        // never visible.
        bool Intersects(const AABB& box) const;
        bool Contains(const glm::vec3& point) const;
    };

    // The axis-aligned box around `box` after `transform` (each corner transformed; exact for
    // the box, the tightest axis-aligned box around the rotated one).
    AABB TransformAABB(const AABB& box, const glm::mat4& transform);

    // Conservative bounds of a skinned mesh (in the space the skin matrices map into): every
    // skinned vertex is a weighted mix of its bind position moved by a few joints, so it stays
    // inside the box around the bind bounds moved by each joint. `joints` = skin matrices
    // (joint world x inverse bind); none: the bind bounds.
    AABB SkinnedBounds(const AABB& bindBounds, const glm::mat4* joints, size_t jointCount);

}
