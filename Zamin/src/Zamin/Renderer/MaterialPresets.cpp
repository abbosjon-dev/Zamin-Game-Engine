#include "Zamin/Renderer/MaterialPresets.h"
#include "Zamin/Asset/AssetRegistry.h"
#include "Zamin/Core/Log.h"
#include "Zamin/Project/AssetFiles.h"
#include "Zamin/Project/Project.h"
#include "Zamin/Utils/Image.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <format>

namespace Zamin {

    namespace fs = std::filesystem;

    // ------------------------------------------------------------------ the library
    static std::vector<MaterialPreset> MakePresets()
    {
        std::vector<MaterialPreset> list;
        auto add = [&](const char* id, const char* name, const char* file, const char* description) -> MaterialData& {
            MaterialPreset& p = list.emplace_back();
            p.Id = id;
            p.Name = name;
            p.FileName = file;
            p.Description = description;
            return p.Data;
        };

        // Metals: the albedo is the color of the reflection (measured values of the metals).
        {
            MaterialData& d = add("metal", "Metall (po'lat)", "Metal", "Silliq po'lat: to'liq metallik, yaltiroq kulrang aks");
            d.Albedo = { 0.78f, 0.78f, 0.80f, 1.0f };
            d.Metallic = 1.0f;
            d.Roughness = 0.22f;
        }
        {
            MaterialPreset& p = list.emplace_back();
            p.Id = "brushed-metal";
            p.Name = "Cho'tkalangan metall";
            p.FileName = "BrushedMetal";
            p.Description = "Cho'tka izlari bor po'lat: g'adir-budurlik teksturasi (Metal/Roughness) chiziq-chiziq aks beradi";
            p.Texture = MaterialPreset::Pattern::BrushedMetal;
            p.Data.Albedo = { 0.72f, 0.73f, 0.75f, 1.0f };
            p.Data.Metallic = 1.0f;
            p.Data.Roughness = 1.0f; // x the map's G channel
            p.Data.Tiling = { 2.0f, 2.0f };
        }
        {
            MaterialData& d = add("gold", "Oltin", "Gold", "Sariq metall: to'liq metallik, iliq aks");
            d.Albedo = { 1.0f, 0.78f, 0.34f, 1.0f };
            d.Metallic = 1.0f;
            d.Roughness = 0.28f;
        }
        {
            MaterialData& d = add("copper", "Mis", "Copper", "Qizg'ish metall: to'liq metallik");
            d.Albedo = { 0.95f, 0.64f, 0.54f, 1.0f };
            d.Metallic = 1.0f;
            d.Roughness = 0.32f;
        }
        // Dielectrics.
        {
            MaterialData& d = add("plastic", "Plastik", "Plastic", "Yaltiroq qizil plastik: rangli sirt, oq yaltirash");
            d.Albedo = { 0.80f, 0.12f, 0.10f, 1.0f };
            d.Roughness = 0.30f;
        }
        {
            MaterialData& d = add("rubber", "Rezina", "Rubber", "Qora mat rezina: deyarli yaltiramaydi");
            d.Albedo = { 0.06f, 0.06f, 0.065f, 1.0f };
            d.Roughness = 0.92f;
        }
        {
            MaterialPreset& p = list.emplace_back();
            p.Id = "wood";
            p.Name = "Yog'och";
            p.FileName = "Wood";
            p.Description = "Yog'och: yillik halqalar va tolalar teksturasi (protsedural, choksiz takrorlanadi)";
            p.Texture = MaterialPreset::Pattern::Wood;
            p.Data.Roughness = 0.62f;
        }
        {
            MaterialData& d = add("glass", "Shisha", "Glass", "Tiniq shisha: shaffof, aks va yaltirash saqlanadi, ikki tomonlama");
            d.BlendMode = MaterialBlendMode::Transparent;
            d.Albedo = { 0.92f, 0.96f, 1.0f, 0.12f };
            d.Roughness = 0.04f;
            d.PreserveSpecular = true;
            d.DoubleSided = true;
        }
        {
            MaterialData& d = add("ceramic", "Keramika", "Ceramic", "Oq sirlangan keramika (chinni): silliq, yaltiroq");
            d.Albedo = { 0.93f, 0.92f, 0.89f, 1.0f };
            d.Roughness = 0.12f;
        }
        {
            MaterialPreset& p = list.emplace_back();
            p.Id = "concrete";
            p.Name = "Beton";
            p.FileName = "Concrete";
            p.Description = "Kulrang beton: dog'lar va mayda teshiklar teksturasi, mat sirt";
            p.Texture = MaterialPreset::Pattern::Concrete;
            p.Data.Roughness = 0.9f;
        }
        {
            MaterialData& d = add("neon", "Neon (nurli)", "Neon", "Moviy neon: kuchli nurlanish (HDR) - bloom yoqilgan sahnada porlaydi");
            d.Albedo = { 0.05f, 0.05f, 0.05f, 1.0f };
            d.Roughness = 0.4f;
            d.Emission = { 0.15f, 0.85f, 1.0f };
            d.EmissionStrength = 4.0f;
        }
        {
            MaterialData& d = add("water", "Suv", "Water", "Moviy suv: shaffof, silliq, aks saqlanadi");
            d.BlendMode = MaterialBlendMode::Transparent;
            d.Albedo = { 0.16f, 0.42f, 0.52f, 0.45f };
            d.Roughness = 0.06f;
            d.PreserveSpecular = true;
        }
        return list;
    }

    const std::vector<MaterialPreset>& MaterialPresets::GetAll()
    {
        static const std::vector<MaterialPreset> s_Presets = MakePresets();
        return s_Presets;
    }

    const MaterialPreset* MaterialPresets::Find(std::string_view id)
    {
        for (const MaterialPreset& preset : GetAll())
            if (preset.Id == id)
                return &preset;
        return nullptr;
    }

    // ------------------------------------------------------------------ noise (tiles without seams)
    static uint32_t Hash(uint32_t x, uint32_t y, uint32_t seed)
    {
        uint32_t h = seed * 0x9E3779B9u ^ x * 0x85EBCA6Bu ^ y * 0xC2B2AE35u;
        h ^= h >> 16;
        h *= 0x7FEB352Du;
        h ^= h >> 15;
        h *= 0x846CA68Bu;
        h ^= h >> 16;
        return h;
    }

    // Value noise on a lattice of periodX x periodY cells over u, v in [0, 1): it wraps at 1.
    static float ValueNoise(float u, float v, int periodX, int periodY, uint32_t seed)
    {
        float x = u * (float)periodX, y = v * (float)periodY;
        int x0 = (int)std::floor(x), y0 = (int)std::floor(y);
        float fx = x - (float)x0, fy = y - (float)y0;
        fx = fx * fx * (3.0f - 2.0f * fx);
        fy = fy * fy * (3.0f - 2.0f * fy);
        auto at = [&](int cx, int cy) {
            uint32_t wx = (uint32_t)(((cx % periodX) + periodX) % periodX);
            uint32_t wy = (uint32_t)(((cy % periodY) + periodY) % periodY);
            return (float)(Hash(wx, wy, seed) & 0xFFFFFFu) / (float)0xFFFFFFu;
        };
        float a = glm::mix(at(x0, y0), at(x0 + 1, y0), fx);
        float b = glm::mix(at(x0, y0 + 1), at(x0 + 1, y0 + 1), fx);
        return glm::mix(a, b, fy);
    }

    // Octaves of value noise (each twice the frequency, half the weight), 0..1.
    static float Fbm(float u, float v, int periodX, int periodY, int octaves, uint32_t seed)
    {
        float sum = 0.0f, weight = 0.5f, total = 0.0f;
        for (int i = 0; i < octaves; i++)
        {
            sum += ValueNoise(u, v, periodX << i, periodY << i, seed + (uint32_t)i * 101u) * weight;
            total += weight;
            weight *= 0.5f;
        }
        return sum / total;
    }

    static uint8_t ToByte(float value)
    {
        return (uint8_t)std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f);
    }

    // ------------------------------------------------------------------ patterns
    const char* MaterialPresets::GetTextureSuffix(MaterialPreset::Pattern pattern)
    {
        return pattern == MaterialPreset::Pattern::BrushedMetal ? "MetalRough" : "Albedo";
    }

    PresetTexture MaterialPresets::GenerateTexture(MaterialPreset::Pattern pattern, uint32_t size)
    {
        PresetTexture out;
        if (pattern == MaterialPreset::Pattern::None)
            return out;
        size = std::max(size, 16u);
        out.Width = out.Height = size;
        out.Pixels.resize((size_t)size * size * 4);
        for (uint32_t py = 0; py < size; py++)
            for (uint32_t px = 0; px < size; px++)
            {
                float u = ((float)px + 0.5f) / (float)size, v = ((float)py + 0.5f) / (float)size;
                glm::vec3 color(0.0f);
                switch (pattern)
                {
                    case MaterialPreset::Pattern::Wood:
                    {
                        // Annual rings run along V (the board's length), bent by slow noise; fine
                        // fibres and pores along them. Colors are sRGB.
                        float warp = (Fbm(u, v, 2, 1, 3, 11u) - 0.5f) * 1.6f + (Fbm(u, v, 3, 4, 2, 13u) - 0.5f) * 0.5f;
                        float rings = u * 9.0f + warp;
                        float t = rings - std::floor(rings);
                        // light early wood, then a darker late-wood band with a sharp edge
                        float ring = glm::smoothstep(0.45f, 0.9f, t) * (1.0f - glm::smoothstep(0.92f, 1.0f, t));
                        float fibre = Fbm(u, v, 96, 4, 3, 23u);
                        float pore = ValueNoise(u, v, 192, 10, 37u);
                        float shade = std::clamp(0.12f + ring * 0.6f + (fibre - 0.5f) * 0.7f, 0.0f, 1.0f);
                        const glm::vec3 light(0.80f, 0.60f, 0.38f), dark(0.46f, 0.29f, 0.15f);
                        color = glm::mix(light, dark, shade);
                        if (pore < 0.1f)
                            color *= 0.85f;
                        color *= 0.93f + (Fbm(u, v, 2, 2, 2, 41u) - 0.5f) * 0.22f; // uneven color over the board
                        break;
                    }
                    case MaterialPreset::Pattern::Concrete:
                    {
                        float base = Fbm(u, v, 6, 6, 5, 53u);
                        float stain = Fbm(u, v, 3, 3, 3, 59u);
                        float speck = ValueNoise(u, v, 96, 96, 61u);
                        float grey = 0.50f + (base - 0.5f) * 0.32f - std::max(0.0f, 0.42f - stain) * 0.25f;
                        if (speck < 0.1f)
                            grey *= 0.62f;  // pores
                        else if (speck > 0.94f)
                            grey *= 1.12f;  // light grains
                        color = glm::vec3(grey * 1.02f, grey, grey * 0.96f);
                        break;
                    }
                    case MaterialPreset::Pattern::BrushedMetal:
                    {
                        // glTF layout: G = roughness, B = metallic. Brush strokes run along U.
                        float strokes = Fbm(u, v, 2, 96, 3, 71u);
                        float scratch = ValueNoise(u, v, 4, 256, 73u);
                        float roughness = 0.30f + strokes * 0.22f + (scratch > 0.9f ? 0.12f : 0.0f);
                        color = glm::vec3(1.0f, roughness, 1.0f);
                        break;
                    }
                    case MaterialPreset::Pattern::None:
                        break;
                }
                uint8_t* p = out.Pixels.data() + ((size_t)py * size + px) * 4;
                p[0] = ToByte(color.r);
                p[1] = ToByte(color.g);
                p[2] = ToByte(color.b);
                p[3] = 255;
            }
        return out;
    }

    // ------------------------------------------------------------------ files
    static std::string ToMaterialPath(const fs::path& file)
    {
        return Project::GetActive() ? Project::GetAssetRelativePath(file) : file.generic_string();
    }

    static void Register(const fs::path& file)
    {
        if (!Project::GetActive())
            return;
        if (AssetRegistry* registry = Project::GetAssetRegistry())
            registry->OnFileAdded(file);
    }

    fs::path MaterialPresets::GetSharedFolder()
    {
        return Project::GetActive() ? Project::GetAssetDirectory() / "Materials" / "Standart" : fs::path();
    }

    fs::path MaterialPresets::GetTexturePath(const MaterialPreset& preset, const fs::path& folder)
    {
        if (preset.Texture == MaterialPreset::Pattern::None)
            return {};
        std::string name = std::format("{}_{}.png", preset.FileName, GetTextureSuffix(preset.Texture));
        fs::path local = folder / "Textures" / name;
        fs::path shared = GetSharedFolder();
        std::error_code ec;
        if (shared.empty() || fs::exists(local, ec))
            return local; // one made here before (an older editor wrote one per folder) stays in use
        return shared / "Textures" / name;
    }

    bool MaterialPresets::Create(const MaterialPreset& preset, const fs::path& folder, bool shared, CreateResult& result, std::string* error)
    {
        result = {};
        auto fail = [&](std::string reason) {
            if (error)
                *error = std::move(reason);
            return false;
        };
        if (folder.empty())
            return fail("papka berilmagan");
        fs::path file = shared ? folder / (preset.FileName + ".zmat") : AssetFiles::MakeUniquePath(folder, preset.FileName + ".zmat");
        std::error_code ec;
        if (shared && fs::exists(file, ec))
        {
            result.Material = file;
            result.Existed = true;
            return true;
        }
        fs::create_directories(folder, ec);
        if (ec)
            return fail(std::format("papka yaratilmadi: {}", ec.message()));

        MaterialData data = preset.Data;
        if (preset.Texture != MaterialPreset::Pattern::None)
        {
            fs::path texture = GetTexturePath(preset, folder);
            if (!fs::exists(texture, ec))
            {
                PresetTexture image = GenerateTexture(preset.Texture);
                fs::create_directories(texture.parent_path(), ec);
                if (!image.IsValid() || !Image::WritePNG(texture.string(), image.Width, image.Height, 4, image.Pixels.data()))
                    return fail(std::format("tekstura yozilmadi: {}", texture.string()));
                result.Written.push_back(texture);
                Register(texture); // its UUID is written next to the path in the .zmat
            }
            (preset.Texture == MaterialPreset::Pattern::BrushedMetal ? data.MetalRoughMap : data.AlbedoMap) = ToMaterialPath(texture);
        }
        if (!MaterialSerializer::Serialize(data, file))
            return fail(std::format("material fayli yozilmadi: {}", file.string()));
        Register(file);
        result.Material = file;
        result.Written.push_back(file);
        return true;
    }

}
