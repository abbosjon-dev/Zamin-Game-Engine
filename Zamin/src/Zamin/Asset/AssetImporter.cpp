#include "Zamin/Asset/AssetImporter.h"
#include "Zamin/Asset/AssetRegistry.h"
#include "Zamin/Audio/AudioMixer.h"
#include "Zamin/Project/Project.h"

#include <nlohmann/json.hpp>

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

    const char* AssetImporter::GetLoadModeKey(AudioLoadMode mode)
    {
        switch (mode)
        {
            case AudioLoadMode::Decompress: return "Decompress";
            case AudioLoadMode::Stream:     return "Stream";
            default:                        return "Auto";
        }
    }

    const char* AssetImporter::GetLoadModeName(AudioLoadMode mode)
    {
        switch (mode)
        {
            case AudioLoadMode::Decompress: return "Xotiraga (decompress)";
            case AudioLoadMode::Stream:     return "Diskdan oqim (stream)";
            default:                        return "Avtomatik (hajmiga qarab)";
        }
    }

    std::optional<AudioLoadMode> AssetImporter::LoadModeFromKey(std::string_view key)
    {
        if (EqualsIgnoreCase(key, "Auto")) return AudioLoadMode::Auto;
        if (EqualsIgnoreCase(key, "Decompress") || EqualsIgnoreCase(key, "DecompressOnLoad") || EqualsIgnoreCase(key, "Memory")) return AudioLoadMode::Decompress;
        if (EqualsIgnoreCase(key, "Stream") || EqualsIgnoreCase(key, "Streaming")) return AudioLoadMode::Stream;
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
        s.KeepSourceClips = import.GetBool("KeepSourceClips", s.KeepSourceClips);
        const double fps = import.GetNumber("ClipFPS", 30.0);
        s.ClipFrameRate = std::isfinite(fps) && fps >= 1.0 ? std::min((float)fps, 240.0f) : 30.0f;
        // "Clips": [ { "Source": "Take 001", "Name": "Walk", "Start": 0.5, "End": 1.7 }, ... ]
        if (const AssetImportSettings::Value* v = import.Find("Clips"))
            if (const auto* raw = std::get_if<AssetImportSettings::Json>(v))
            {
                nlohmann::json j = nlohmann::json::parse(raw->Text, nullptr, false);
                if (j.is_array())
                    for (const auto& item : j)
                    {
                        if (!item.is_object() || !item.contains("Source") || !item["Source"].is_string())
                            continue;
                        ModelClipSettings clip;
                        clip.Source = item["Source"].get<std::string>();
                        if (auto it = item.find("Name"); it != item.end() && it->is_string())
                            clip.Name = it->get<std::string>();
                        if (auto it = item.find("Start"); it != item.end() && it->is_number())
                            clip.Start = std::max(0.0f, it->get<float>());
                        if (auto it = item.find("End"); it != item.end() && it->is_number())
                            clip.End = it->get<float>();
                        if (!std::isfinite(clip.Start)) clip.Start = 0.0f;
                        if (!std::isfinite(clip.End)) clip.End = -1.0f;
                        if (clip.Name == clip.Source)
                            clip.Name.clear();
                        s.Clips.push_back(std::move(clip));
                    }
            }
        return s;
    }

    void AssetImporter::WriteModelSettings(const ModelImportSettings& settings, AssetImportSettings& import)
    {
        import.SetNumber("Scale", settings.Scale);
        import.SetString("UpAxis", GetUpAxisKey(settings.UpAxis));
        import.SetBool("FlipUVs", settings.FlipUVs);
        // The clip keys only when they say something (files without them stay as they were).
        if (settings.Clips.empty())
            import.Remove("Clips");
        else
        {
            nlohmann::ordered_json clips = nlohmann::ordered_json::array();
            auto rounded = [](float v) { return std::round((double)v * 10000.0) / 10000.0; };
            for (const ModelClipSettings& c : settings.Clips)
            {
                nlohmann::ordered_json item;
                item["Source"] = c.Source;
                if (!c.Name.empty() && c.Name != c.Source)
                    item["Name"] = c.Name;
                item["Start"] = rounded(c.Start);
                item["End"] = c.End < 0.0f ? -1.0 : rounded(c.End);
                clips.push_back(std::move(item));
            }
            import.Set("Clips", AssetImportSettings::Json{ clips.dump() });
        }
        if (settings.KeepSourceClips)
            import.Remove("KeepSourceClips");
        else
            import.SetBool("KeepSourceClips", false);
        if (settings.ClipFrameRate == 30.0f)
            import.Remove("ClipFPS");
        else
            import.SetNumber("ClipFPS", settings.ClipFrameRate);
    }

    // ------------------------------------------------------------------ audio
    AudioClipSettings AssetImporter::ReadAudioSettings(const AssetImportSettings& import)
    {
        AudioClipSettings s;
        if (auto mode = LoadModeFromKey(import.GetString("LoadMode")))
            s.LoadMode = *mode;
        s.Preload = import.GetBool("Preload", s.Preload);
        double volume = import.GetNumber("Volume", 1.0);
        s.Volume = std::isfinite(volume) ? std::clamp((float)volume, 0.0f, AudioSettings::MaxVolume) : 1.0f;
        s.LoopStart = (float)import.GetNumber("LoopStart", 0.0);
        s.LoopEnd = (float)import.GetNumber("LoopEnd", 0.0);
        s.Sanitize();
        return s;
    }

    void AssetImporter::WriteAudioSettings(const AudioClipSettings& settings, AssetImportSettings& import)
    {
        import.SetString("LoadMode", GetLoadModeKey(settings.LoadMode));
        import.SetBool("Preload", settings.Preload);
        import.SetNumber("Volume", std::round((double)settings.Volume * 10000.0) / 10000.0); // 0.9, not 0.89999997
        // The loop region only when there is one (milliseconds are enough).
        AudioClipSettings clean = settings;
        clean.Sanitize();
        if (clean.HasLoopRegion())
        {
            import.SetNumber("LoopStart", std::round((double)clean.LoopStart * 1000.0) / 1000.0);
            import.SetNumber("LoopEnd", std::round((double)clean.LoopEnd * 1000.0) / 1000.0);
        }
        else
        {
            import.Remove("LoopStart");
            import.Remove("LoopEnd");
        }
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

    AudioClipSettings AssetImporter::GetAudioSettings(const std::filesystem::path& file)
    {
        const AssetMetadata* meta = FindInActiveProject(file);
        return meta ? ReadAudioSettings(meta->Import) : AudioClipSettings{};
    }

}
