#pragma once

#include "Zamin/Renderer/Texture.h"

#include <glm/glm.hpp>

namespace Zamin {

    // Simple metallic/roughness PBR material.
    struct Material
    {
        glm::vec4 Albedo{ 1.0f };
        Ref<Texture2D> AlbedoMap;
        float Tiling = 1.0f;
        float Metallic = 0.0f;
        float Roughness = 0.5f;
        glm::vec3 Emission{ 0.0f };
        float EmissionStrength = 0.0f;
    };

}
