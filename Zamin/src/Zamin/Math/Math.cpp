#include "Zamin/Math/Math.h"

#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtx/matrix_decompose.hpp>

namespace Zamin::Math {

    // Simplified glm::decompose (no skew / perspective). Rotation is XYZ euler in radians.
    bool DecomposeTransform(const glm::mat4& transform, glm::vec3& translation, glm::vec3& rotation, glm::vec3& scale)
    {
        using namespace glm;
        using T = float;

        mat4 localMatrix(transform);
        if (epsilonEqual(localMatrix[3][3], static_cast<T>(0), epsilon<T>()))
            return false;

        translation = vec3(localMatrix[3]);
        localMatrix[3] = vec4(0, 0, 0, localMatrix[3].w);

        vec3 row[3];
        for (length_t i = 0; i < 3; ++i)
            for (length_t j = 0; j < 3; ++j)
                row[i][j] = localMatrix[i][j];

        scale.x = length(row[0]);
        row[0] = detail::scale(row[0], static_cast<T>(1));
        scale.y = length(row[1]);
        row[1] = detail::scale(row[1], static_cast<T>(1));
        scale.z = length(row[2]);
        row[2] = detail::scale(row[2], static_cast<T>(1));

        rotation.y = asin(-row[0][2]);
        if (cos(rotation.y) != 0)
        {
            rotation.x = atan2(row[1][2], row[2][2]);
            rotation.z = atan2(row[0][1], row[0][0]);
        }
        else
        {
            rotation.x = atan2(-row[2][0], row[1][1]);
            rotation.z = 0;
        }
        return true;
    }

}
