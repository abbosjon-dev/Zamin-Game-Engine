#pragma once

#include "Zamin/Math/Geometry.h"
#include "Zamin/Renderer/Mesh.h"

#include <glm/glm.hpp>

#include <array>
#include <cstddef>

// Frustum, AABB and TransformAABB live in Math/Geometry.h (included here).
namespace Zamin {

    // The eight corners of a camera's view volume in world space: `projection` (perspective or
    // orthographic, OpenGL clip space) seen from `cameraWorld` (the camera's world transform; its
    // inverse is the view matrix). Index bits: 1 = right (else left), 2 = top (else bottom),
    // 4 = far (else near). A projection that cannot be inverted gives the camera's position 8 times.
    std::array<glm::vec3, 8> FrustumCorners(const glm::mat4& projection, const glm::mat4& cameraWorld);

    // Conservative bounds of a skinned mesh (in the space the skin matrices map into): every
    // skinned vertex is a weighted mix of its bind position moved by a few joints, so it stays
    // inside the box around the bind bounds moved by each joint. `joints` = skin matrices
    // (joint world x inverse bind); none: the bind bounds.
    AABB SkinnedBounds(const AABB& bindBounds, const glm::mat4* joints, size_t jointCount);

}
