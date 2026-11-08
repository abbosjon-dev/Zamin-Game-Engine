#pragma once

#include "Zamin/Asset/AssetMetadata.h"
#include "Zamin/Audio/AudioClip.h"
#include "Zamin/Renderer/Font.h"
#include "Zamin/Renderer/Model.h"
#include "Zamin/Renderer/Texture.h"

#include <filesystem>
#include <optional>
#include <string_view>

namespace Zamin {

    // Typed import settings of textures and models, kept in the "Import" object of their .zmeta
    // files:
    //   texture: { "Filter": "Linear" | "Nearest", "Wrap": "Repeat" | "Clamp" | "Mirror",
    //              "Mipmaps": true, "sRGB": true }
    //   model:   { "Scale": 1.0, "UpAxis": "Y" | "Z", "FlipUVs": false,
    //              "Clips": [ { "Source": "Take", "Name": "Walk", "Start": 0.5, "End": 1.7 } ],  (trimmed / sub-clips)
    //              "KeepSourceClips": true, "ClipFPS": 30 }
    //   audio:   { "LoadMode": "Auto" | "Decompress" | "Stream", "Preload": false, "Volume": 1.0 }
    //   font:    { "Kerning": false }
    // A missing (or unreadable) value means the default, so assets without settings load as before.
    class AssetImporter
    {
    public:
        static TextureImportSettings ReadTextureSettings(const AssetImportSettings& import);
        // Sets the texture keys of `import` (other keys are kept).
        static void WriteTextureSettings(const TextureImportSettings& settings, AssetImportSettings& import);
        static ModelImportSettings ReadModelSettings(const AssetImportSettings& import);
        static void WriteModelSettings(const ModelImportSettings& settings, AssetImportSettings& import);
        static AudioClipSettings ReadAudioSettings(const AssetImportSettings& import);
        static void WriteAudioSettings(const AudioClipSettings& settings, AssetImportSettings& import);
        static FontImportSettings ReadFontSettings(const AssetImportSettings& import);
        static void WriteFontSettings(const FontImportSettings& settings, AssetImportSettings& import);

        // Settings of a file of the active project's asset folder, from the asset registry;
        // the defaults for other files and without a project.
        static TextureImportSettings GetTextureSettings(const std::filesystem::path& file);
        static ModelImportSettings GetModelSettings(const std::filesystem::path& file);
        static AudioClipSettings GetAudioSettings(const std::filesystem::path& file);
        static FontImportSettings GetFontSettings(const std::filesystem::path& file);

        // Keys stored in .zmeta files and names shown in the editor.
        static const char* GetFilterKey(TextureFilter filter);
        static std::optional<TextureFilter> FilterFromKey(std::string_view key);
        static const char* GetFilterName(TextureFilter filter);
        static const char* GetWrapKey(TextureWrap wrap);
        static std::optional<TextureWrap> WrapFromKey(std::string_view key);
        static const char* GetWrapName(TextureWrap wrap);
        static const char* GetUpAxisKey(ModelUpAxis axis);
        static std::optional<ModelUpAxis> UpAxisFromKey(std::string_view key);
        static const char* GetUpAxisName(ModelUpAxis axis);
        static const char* GetLoadModeKey(AudioLoadMode mode);
        static std::optional<AudioLoadMode> LoadModeFromKey(std::string_view key);
        static const char* GetLoadModeName(AudioLoadMode mode);

        // Smallest / largest model scale accepted (other values are clamped; 0, negative or not a
        // number means 1).
        static constexpr float MinModelScale = 0.0001f;
        static constexpr float MaxModelScale = 10000.0f;
    };

}
