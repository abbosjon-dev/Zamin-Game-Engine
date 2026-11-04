#pragma once

#include "Zamin/Renderer/Texture.h"

#include <glm/glm.hpp>
#include <array>
#include <filesystem>

namespace Zamin {

    // Image based lighting from an equirectangular (lat-long) HDR/LDR panorama.
    //  * diffuse: 9 spherical-harmonic coefficients (irradiance / PI)
    //  * specular: the panorama's mip chain sampled by roughness
    class EnvironmentMap
    {
    public:
        const Ref<Texture2D>& GetTexture() const { return m_Texture; }
        const std::array<glm::vec3, 9>& GetSH() const { return m_SH; }
        float GetMaxLod() const { return m_MaxLod; }
        const std::filesystem::path& GetPath() const { return m_Path; }

        static Ref<EnvironmentMap> Load(const std::filesystem::path& path);
        static void ClearCache();

        // Exposed for tests: projects an RGB float image (w*h*3 linear) onto SH9 irradiance.
        static std::array<glm::vec3, 9> ComputeIrradianceSH(const float* rgb, int width, int height, int channels);

    private:
        Ref<Texture2D> m_Texture;
        std::array<glm::vec3, 9> m_SH{};
        float m_MaxLod = 0.0f;
        std::filesystem::path m_Path;
    };

}
