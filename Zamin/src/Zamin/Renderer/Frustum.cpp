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

    AABB SkinnedBounds(const AABB& bindBounds, const glm::mat4* joints, size_t jointCount)
    {
        if (!joints || jointCount == 0)
            return bindBounds;
        AABB out = bindBounds.Transformed(joints[0]);
        for (size_t i = 1; i < jointCount; i++)
            out.Encapsulate(bindBounds.Transformed(joints[i]));
        return out;
    }

}
