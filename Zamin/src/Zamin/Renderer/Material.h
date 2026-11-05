#pragma once

#include "Zamin/Renderer/Texture.h"

#include <glm/glm.hpp>

#include <cstdint>

namespace Zamin {

    // How a material is lit. Unlit: albedo (x map) + emission only, no lights, shadows or sky.
    enum class MaterialShader : uint8_t { Lit = 0, Unlit };

    // How a material covers what is behind it. Cutout: pixels whose alpha is below AlphaCutoff are
    // dropped (leaves, fences); Transparent: alpha blended over the opaque scene.
    enum class MaterialBlendMode : uint8_t { Opaque = 0, Cutout, Transparent };

    // Simple metallic/roughness PBR material (what the renderer draws with: values + textures).
    // Materials stored as assets (.zmat) are MaterialAsset objects that hold one of these.
    struct Material
    {
        glm::vec4 Albedo{ 1.0f };
        Ref<Texture2D> AlbedoMap;
        glm::vec2 Tiling{ 1.0f };   // texture coordinates are uv * Tiling + Offset
        glm::vec2 Offset{ 0.0f };
        float Metallic = 0.0f;
        float Roughness = 0.5f;
        glm::vec3 Emission{ 0.0f };
        float EmissionStrength = 0.0f;

        Ref<Texture2D> NormalMap;
        float NormalStrength = 1.0f;
        Ref<Texture2D> MetalRoughMap; // G = roughness, B = metallic (glTF)
        Ref<Texture2D> EmissiveMap;

        MaterialShader Shader = MaterialShader::Lit;
        MaterialBlendMode BlendMode = MaterialBlendMode::Opaque;
        float AlphaCutoff = 0.5f;     // Cutout: alpha below this is not drawn
        bool DoubleSided = false;     // both faces drawn (back faces lit with the flipped normal)
    };

}
