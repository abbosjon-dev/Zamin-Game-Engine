#include "Zamin/Debug/MeshNormals.h"

#include <algorithm>
#include <cmath>

namespace Zamin {

    static bool Finite(const glm::vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

    size_t AppendNormalLines(const MeshGeometry& geometry, const glm::mat4& transform, const std::vector<glm::mat4>* jointMatrices,
                             const NormalLineOptions& options, std::vector<NormalLine>& out)
    {
        const size_t count = std::min(geometry.Positions.size(), geometry.Normals.size());
        if (count == 0 || out.size() >= options.MaxLines || !(options.Length > 0.0f) || !std::isfinite(options.Length))
            return 0;
        // More vertices than room: every `stride`th one.
        const size_t room = options.MaxLines - out.size();
        const size_t stride = count > room ? (count + room - 1) / room : 1;
        const bool skinned = geometry.IsSkinned() && jointMatrices && !jointMatrices->empty();
        const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(transform)));
        const glm::vec3 forward = glm::length(options.CameraForward) > 1e-6f ? glm::normalize(options.CameraForward) : glm::vec3(0, 0, -1);
        const size_t before = out.size();

        for (size_t i = 0; i < count && out.size() < options.MaxLines; i += stride)
        {
            glm::vec4 position(geometry.Positions[i], 1.0f);
            glm::vec3 normal = geometry.Normals[i];
            if (skinned)
            {
                const glm::ivec4& joints = geometry.Joints[i];
                const glm::vec4& weights = geometry.Weights[i];
                glm::mat4 skin(0.0f);
                float total = 0.0f;
                for (int k = 0; k < 4; k++)
                {
                    if (weights[k] == 0.0f)
                        continue;
                    const int j = joints[k];
                    const glm::mat4 joint = j >= 0 && (size_t)j < jointMatrices->size() ? (*jointMatrices)[(size_t)j] : glm::mat4(1.0f);
                    skin += weights[k] * joint;
                    total += weights[k];
                }
                if (std::abs(total) > 1e-6f)
                {
                    position = skin * position;
                    normal = glm::mat3(skin) * normal;
                }
            }
            const glm::vec3 from = glm::vec3(transform * position);
            glm::vec3 n = normalMatrix * normal;
            const float length = glm::length(n);
            if (!Finite(from) || !std::isfinite(length) || length < 1e-8f)
                continue;
            n /= length;
            if (options.FacingOnly)
            {
                const glm::vec3 toCamera = options.Orthographic ? -forward : options.CameraPosition - from;
                if (glm::dot(n, toCamera) <= 0.0f)
                    continue;
            }
            out.push_back({ from, from + n * options.Length, n });
        }
        return out.size() - before;
    }

}
