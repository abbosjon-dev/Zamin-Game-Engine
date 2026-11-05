#include "Zamin/Asset/AssetImporter.h"
#include "Zamin/Asset/AssetRegistry.h"
#include "Zamin/Project/Project.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace Zamin {

    static bool EqualsIgnoreCase(std::string_view a, std::string_view b)
    {
        return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
            return std::tolower((unsigned char)x) == std::tolower((unsigned char)y);
        });
    }

    // ------------------------------------------------------------------ names
    const char* AssetImporter::GetFilterKey(TextureFilter filter) { return filter == TextureFilter::Nearest ? "Nearest" : "Linear"; }
    const char* AssetImporter::GetFilterName(TextureFilter filter) { return filter == TextureFilter::Nearest ? "Piksel (nearest)" : "Silliq (linear)"; }

    std::optional<TextureFilter> AssetImporter::FilterFromKey(std::string_view key)
    {
        if (EqualsIgnoreCase(key, "Linear") || EqualsIgnoreCase(key, "Bilinear") || EqualsIgnoreCase(key, "Trilinear")) return TextureFilter::Linear;
        if (EqualsIgnoreCase(key, "Nearest") || EqualsIgnoreCase(key, "Point")) return TextureFilter::Nearest;
        return std::nullopt;
    }

    const char* AssetImporter::GetWrapKey(TextureWrap wrap)
    {
        switch (wrap)
        {
            case TextureWrap::ClampToEdge:    return "Clamp";
            case TextureWrap::MirroredRepeat: return "Mirror";
            default:                          return "Repeat";
        }
    }

    const char* AssetImporter::GetWrapName(TextureWrap wrap)
    {
        switch (wrap)
        {
            case TextureWrap::ClampToEdge:    return "Chetda to'xtash (clamp)";
            case TextureWrap::MirroredRepeat: return "Ko'zgu (mirror)";
            default:                          return "Takrorlash (repeat)";
        }
    }

    std::optional<TextureWrap> AssetImporter::WrapFromKey(std::string_view key)
    {
        if (EqualsIgnoreCase(key, "Repeat")) return TextureWrap::Repeat;
        if (EqualsIgnoreCase(key, "Clamp") || EqualsIgnoreCase(key, "ClampToEdge")) return TextureWrap::ClampToEdge;
        if (EqualsIgnoreCase(key, "Mirror") || EqualsIgnoreCase(key, "MirroredRepeat")) return TextureWrap::MirroredRepeat;
        return std::nullopt;
    }

    const char* AssetImporter::GetUpAxisKey(ModelUpAxis axis) { return axis == ModelUpAxis::Z ? "Z" : "Y"; }
    const char* AssetImporter::GetUpAxisName(ModelUpAxis axis) { return axis == ModelUpAxis::Z ? "Z yuqoriga" : "Y yuqoriga"; }

    std::optional<ModelUpAxis> AssetImporter::UpAxisFromKey(std::string_view key)
    {
        if (EqualsIgnoreCase(key, "Y") || EqualsIgnoreCase(key, "Y-up") || EqualsIgnoreCase(key, "YUp")) return ModelUpAxis::Y;
        if (EqualsIgnoreCase(key, "Z") || EqualsIgnoreCase(key, "Z-up") || EqualsIgnoreCase(key, "ZUp")) return ModelUpAxis::Z;
        return std::nullopt;
    }

    // ------------------------------------------------------------------ textures
    TextureImportSettings AssetImporter::ReadTextureSettings(const AssetImportSettings& import)
    {
        TextureImportSettings s;
        if (auto filter = FilterFromKey(import.GetString("Filter")))
            s.Filter = *filter;
        if (auto wrap = WrapFromKey(import.GetString("Wrap")))
            s.Wrap = *wrap;
        s.Mipmaps = import.GetBool("Mipmaps", s.Mipmaps);
        s.SRGB = import.GetBool("sRGB", s.SRGB);
        return s;
    }

    void AssetImporter::WriteTextureSettings(const TextureImportSettings& settings, AssetImportSettings& import)
    {
        import.SetString("Filter", GetFilterKey(settings.Filter));
        import.SetString("Wrap", GetWrapKey(settings.Wrap));
        import.SetBool("Mipmaps", settings.Mipmaps);
        import.SetBool("sRGB", settings.SRGB);
    }

    // ------------------------------------------------------------------ models
    ModelImportSettings AssetImporter::ReadModelSettings(const AssetImportSettings& import)
    {
        ModelImportSettings s;
        double scale = import.GetNumber("Scale", 1.0);
        s.Scale = std::isfinite(scale) && scale > 0.0 ? std::clamp((float)scale, MinModelScale, MaxModelScale) : 1.0f;
        if (auto axis = UpAxisFromKey(import.GetString("UpAxis")))
            s.UpAxis = *axis;
        s.FlipUVs = import.GetBool("FlipUVs", s.FlipUVs);
        return s;
    }

    void AssetImporter::WriteModelSettings(const ModelImportSettings& settings, AssetImportSettings& import)
    {
        import.SetNumber("Scale", settings.Scale);
        import.SetString("UpAxis", GetUpAxisKey(settings.UpAxis));
        import.SetBool("FlipUVs", settings.FlipUVs);
    }

    // ------------------------------------------------------------------ lookups
    static const AssetMetadata* FindInActiveProject(const std::filesystem::path& file)
    {
        if (!Project::GetActive())
            return nullptr;
        AssetRegistry* registry = Project::GetAssetRegistry();
        return registry ? registry->Find(file) : nullptr;
    }

    TextureImportSettings AssetImporter::GetTextureSettings(const std::filesystem::path& file)
    {
        const AssetMetadata* meta = FindInActiveProject(file);
        return meta ? ReadTextureSettings(meta->Import) : TextureImportSettings{};
    }

    ModelImportSettings AssetImporter::GetModelSettings(const std::filesystem::path& file)
    {
        const AssetMetadata* meta = FindInActiveProject(file);
        return meta ? ReadModelSettings(meta->Import) : ModelImportSettings{};
    }

}
