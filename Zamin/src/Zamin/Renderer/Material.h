#pragma once

#include "Zamin/Renderer/Texture.h"

#include <glm/glm.hpp>

#include <cstdint>

namespace Zamin {

    // How a material is lit. Unlit: albedo (x map) + emission only, no lights, shadows or sky.
    enum class MaterialShader : uint8_t { Lit = 0, Unlit };

    // How a material covers what is behind it. Opaque: alpha is ignored; Cutout: pixels whose alpha
    // is below AlphaCutoff are dropped (leaves, fences), the rest is opaque and the shadow has the
    // same holes; Transparent: alpha blended over the opaque scene (sorted back to front, no depth
    // write, no shadow of its own).
    enum class MaterialBlendMode : uint8_t { Opaque = 0, Cutout, Transparent };

    // Transparent surfaces are drawn by priority (lower first, so a higher one ends up over the
    // others wherever they are), then back to front. Range of Material::SortPriority.
    constexpr int MinMaterialSortPriority = -100;
    constexpr int MaxMaterialSortPriority = 100;

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
        bool DoubleSided = false;     // both faces drawn (back faces lit with the flipped normal); else back faces are culled
        // Transparent: reflections and highlights keep their full strength, only the diffuse part
        // fades with alpha (glass, water). false: the whole surface fades (fading objects out).
        bool PreserveSpecular = true;
        // Transparent: drawn after the transparent surfaces of a lower priority (over them), before
        // those of a higher one; equal priorities are sorted back to front. For surfaces that
        // overlap where sorting by distance cannot tell (water and the glass in it, decals, UI).
        int SortPriority = 0;

        // The light the surface gives off (linear HDR): Emission x EmissionStrength (x EmissiveMap).
        // Above 1 it glows with the scene's bloom.
        glm::vec3 GetEmissionRadiance() const { return Emission * EmissionStrength; }
    };

    // An emissive map multiplies the emission color x strength: with no emission set (black, or a
    // strength of 0) a newly given map would show nothing, so the emission becomes white x 1.
    // Returns true when it changed something. For Material and MaterialData (same field names).
    template<typename T>
    bool EnableEmissionForMap(T& material)
    {
        bool black = material.Emission.r <= 0.0f && material.Emission.g <= 0.0f && material.Emission.b <= 0.0f;
        if (!black && material.EmissionStrength > 0.0f)
            return false;
        if (black)
            material.Emission = glm::vec3(1.0f);
        if (material.EmissionStrength <= 0.0f)
            material.EmissionStrength = 1.0f;
        return true;
    }

}
