#include "Zamin/Renderer/Environment.h"
#include "Zamin/Core/Log.h"

#include <stb_image.h>
#include <glm/gtc/constants.hpp>

#include <cmath>
#include <unordered_map>

namespace Zamin {

    static std::unordered_map<std::string, Ref<EnvironmentMap>> s_EnvCache;

    std::array<glm::vec3, 9> EnvironmentMap::ComputeIrradianceSH(const float* img, int width, int height, int channels)
    {
        std::array<glm::vec3, 9> sh{};
        double weightSum = 0.0;
        int step = std::max(1, width / 256);
        const float pi = glm::pi<float>();
        for (int y = 0; y < height; y += step)
        {
            // image row 0 is the bottom (stb flipped): v = (y + 0.5) / h, latitude = (v - 0.5) * PI
            float v = (y + 0.5f) / height;
            float lat = (v - 0.5f) * pi;
            float cosLat = std::cos(lat);
            for (int x = 0; x < width; x += step)
            {
                float u = (x + 0.5f) / width;
                float lon = (u - 0.5f) * 2.0f * pi;
                glm::vec3 d = { std::cos(lat) * std::cos(lon), std::sin(lat), std::cos(lat) * std::sin(lon) };
                const float* p = img + ((size_t)y * width + x) * channels;
                glm::vec3 c = { p[0], p[1], p[2] };
                float w = cosLat; // solid angle weight
                float basis[9] = {
                    0.282095f,
                    0.488603f * d.y, 0.488603f * d.z, 0.488603f * d.x,
                    1.092548f * d.x * d.y, 1.092548f * d.y * d.z, 0.315392f * (3.0f * d.z * d.z - 1.0f),
                    1.092548f * d.x * d.z, 0.546274f * (d.x * d.x - d.y * d.y),
                };
                for (int i = 0; i < 9; i++) sh[i] += c * (basis[i] * w);
                weightSum += w;
            }
        }
        // normalize to the sphere (4 PI), then convolve with the clamped cosine lobe and divide by PI
        float norm = (float)(4.0 * pi / weightSum);
        const float A[9] = { pi, 2.0f * pi / 3.0f, 2.0f * pi / 3.0f, 2.0f * pi / 3.0f, pi / 4.0f, pi / 4.0f, pi / 4.0f, pi / 4.0f, pi / 4.0f };
        for (int i = 0; i < 9; i++) sh[i] *= norm * A[i] / pi;
        return sh;
    }

    bool EnvironmentMap::Build(std::string& error)
    {
        int w, h, c;
        stbi_set_flip_vertically_on_load(1);
        float* data = stbi_loadf(m_Path.string().c_str(), &w, &h, &c, 4); // LDR images are linearized by stb
        if (!data)
        {
            const char* reason = stbi_failure_reason();
            error = reason ? reason : "noma'lum xato";
            return false;
        }
        m_SH = ComputeIrradianceSH(data, w, h, 4);

        TextureSpecification spec;
        spec.Width = (uint32_t)w;
        spec.Height = (uint32_t)h;
        spec.Format = ImageFormat::RGBA32F;
        spec.GenerateMips = true;
        spec.Wrap = TextureWrap::Repeat;
        m_Texture = Texture2D::Create(spec);
        m_Texture->SetData(data, (uint32_t)(w * h * 16));
        m_MaxLod = std::floor(std::log2((float)std::max(w, h)));
        stbi_image_free(data);
        return true;
    }

    Ref<EnvironmentMap> EnvironmentMap::Load(const std::filesystem::path& path)
    {
        std::string key = path.string();
        if (auto it = s_EnvCache.find(key); it != s_EnvCache.end())
            return it->second;

        auto env = CreateRef<EnvironmentMap>();
        env->m_Path = path;
        std::string error;
        if (!env->Build(error))
        {
            ZM_CORE_ERROR("Muhit xaritasi yuklanmadi: '{}' ({})", key, error);
            s_EnvCache[key] = nullptr;
            return nullptr;
        }
        ZM_CORE_INFO("Muhit xaritasi (IBL) yuklandi: {} ({}x{})", path.filename().string(),
                     env->m_Texture->GetWidth(), env->m_Texture->GetHeight());
        s_EnvCache[key] = env;
        return env;
    }

    bool EnvironmentMap::Reload()
    {
        std::string error;
        if (Build(error))
            return true;
        ZM_CORE_ERROR("Muhit xaritasi qayta yuklanmadi: '{}' ({}) - avvalgisi qoldi", m_Path.string(), error);
        return false;
    }

    static bool SameFile(const std::filesystem::path& a, const std::filesystem::path& b)
    {
        std::error_code ea, eb;
        std::filesystem::path ca = std::filesystem::weakly_canonical(a, ea), cb = std::filesystem::weakly_canonical(b, eb);
        return (ea ? a : ca) == (eb ? b : cb);
    }

    size_t EnvironmentMap::ReloadFile(const std::filesystem::path& path, bool* failed)
    {
        size_t reloaded = 0;
        for (auto it = s_EnvCache.begin(); it != s_EnvCache.end();)
        {
            if (!SameFile(it->first, path))
            {
                ++it;
                continue;
            }
            if (!it->second)
            {
                it = s_EnvCache.erase(it); // failed before: the next Load tries again
                continue;
            }
            if (it->second->Reload())
                reloaded++;
            else if (failed)
                *failed = true;
            ++it;
        }
        return reloaded;
    }

    bool EnvironmentMap::IsFileLoaded(const std::filesystem::path& path)
    {
        for (const auto& [key, env] : s_EnvCache)
            if (env && SameFile(key, path))
                return true;
        return false;
    }

    void EnvironmentMap::ClearCache() { s_EnvCache.clear(); }

}
