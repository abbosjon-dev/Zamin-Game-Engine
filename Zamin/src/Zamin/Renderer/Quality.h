#pragma once

#include "Zamin/Renderer/PostProcess.h"
#include "Zamin/Renderer/Renderer3D.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Zamin {

    // One quality level of the project (Project settings > Quality): the most a scene's rendering
    // settings may ask for on this level. Each scene keeps its own settings (shadows, effects, render
    // scale in Scene settings) and the active level only limits them: a smaller shadow atlas, a
    // cheaper filter, fewer cascades, an effect turned off, a lower render scale. A level that allows
    // everything (Unlimited, or the default "Ultra" for shadows / effects) leaves a scene as it is.
    // The textures' anisotropic filtering and resolution are the level's own (they are not per scene).
    struct QualityLevel
    {
        static constexpr int MaxAnisotropy = 16;
        static constexpr int MaxTextureQuality = 3; // 0 full .. 3 an eighth of the size
        static constexpr float MinScale = 0.25f, MaxScale = 2.0f;

        std::string Name;

        // ---- sun shadows
        bool Shadows = true;                                 // off: the sun casts no shadows at all
        uint32_t ShadowAtlasSize = ShadowSettings::MaxAtlasSize; // largest atlas (1024 / 2048 / 4096)
        ShadowFilter MaxShadowFilter = ShadowFilter::PCF5x5; // the most expensive filter (see FilterCost)
        int MaxShadowCascades = ShadowSettings::MaxCascades;
        float ShadowDistanceScale = 1.0f;                    // x the scene's shadow distance

        // ---- post-processing a scene may use
        bool SSAO = true;
        bool Bloom = true;
        bool FXAA = true;
        bool AutoExposure = true;

        // x the scene's render scale (the result stays within the renderer's limits).
        float RenderScale = 1.0f;

        // ---- textures (every texture read from an image that has mipmaps)
        int Anisotropy = 1;      // anisotropic filtering samples: 1 off, 2, 4, 8, 16 (as the GPU allows)
        int TextureQuality = 0;  // mip levels skipped: 0 full, 1 half, 2 a quarter, 3 an eighth

        bool operator==(const QualityLevel&) const = default;

        // Values from a file brought into range (atlas to 1024 / 2048 / 4096, counts clamped, the
        // anisotropy to a power of two, anything not finite to its default).
        void Sanitize();

        // How expensive a filter is (taps): Hard 0 < PCF 3x3 1 < Poisson (16) 2 < PCF 5x5 3.
        static int FilterCost(ShadowFilter filter);

        // A scene's settings as this level lets them be.
        ShadowSettings ApplyTo(const ShadowSettings& shadows) const;
        PostProcessSettings ApplyTo(const PostProcessSettings& post) const;
        float ApplyRenderScale(float renderScale) const;
        float ApplyShadowDistance(float distance) const;
        bool AllowsSunShadows(bool castShadows) const { return castShadows && Shadows; }

        // What this level changes in a scene with these settings (Uzbek, one line each: the editor's
        // scene settings show it). Empty: the scene is drawn as it is.
        std::vector<std::string> DescribeLimits(const ShadowSettings& shadows, const PostProcessSettings& post, float renderScale,
                                                float shadowDistance) const;

        // Changes nothing: no project open, the tests.
        static QualityLevel Unlimited();
    };

    // The project's quality levels (.zproject "Quality"): Low, Medium, High, Ultra by default; the
    // game starts on DefaultLevel unless the player chose another one (Quality::LoadSettings) or the
    // command line says so (ZaminPlayer --quality). Projects without the section get the defaults with
    // Ultra, whose shadow / effect limits are the renderer's maximum (scenes look as they always did).
    struct QualitySettings
    {
        static constexpr int MaxLevels = 16;

        std::vector<QualityLevel> Levels = DefaultLevels();
        int DefaultLevel = 3;

        static std::vector<QualityLevel> DefaultLevels();

        // Index of the level with this name (case is ignored), -1 when there is none.
        int Find(std::string_view name) const;
        // `base` when no level has it, else base + " 2", " 3" ...
        std::string MakeUniqueName(std::string_view base) const;
        // At least one level, every level sanitized, names not empty and unique, DefaultLevel valid.
        void Sanitize();
        bool IsDefault() const { return *this == QualitySettings(); }
        bool operator==(const QualitySettings&) const = default;

        // { "Default": "Ultra", "Levels": [ { "Name", "Shadows", "ShadowAtlas", ... } ] }; missing values
        // keep their defaults (a level named like a built-in one starts from that one).
        std::string ToJson() const;
        static bool FromJson(std::string_view text, QualitySettings& out);
    };

    // The quality level the renderer uses now. The project's levels are set when it is loaded
    // (Project::Load); the player then picks the saved or the command line's level, the editor shows
    // the level chosen for its preview (Project settings > Quality), a game's options menu calls
    // SetLevel (and SaveSettings to keep the choice). Without levels (no project) nothing is limited.
    class Quality
    {
    public:
        // `keepLevel`: stay on the level with the same name when there is one (the levels were edited),
        // else the settings' default level.
        static void SetSettings(const QualitySettings& settings, bool keepLevel = false);
        static const QualitySettings& GetSettings();
        // Back to no levels at all (Unlimited).
        static void Reset();

        static int GetLevelCount();
        // Index of the active level (-1: none, Unlimited).
        static int GetLevel();
        static std::string GetLevelName();
        static std::vector<std::string> GetLevelNames();
        // False for an unknown index / name (nothing changes).
        static bool SetLevel(int index);
        static bool SetLevel(std::string_view name);
        // "Low", "2" (an index) ... as given on a command line.
        static bool SetLevelFromString(std::string_view text);
        static const QualityLevel& GetActive();
        // Grows whenever the active level or its values change.
        static uint64_t GetChangeCount();

        // ---- the player's choice, kept with SaveData (key SettingsKey + ".Level": the level's name)
        static constexpr const char* SettingsKey = "Zamin.Quality";
        static bool SaveSettings();
        // Applies the saved level; false when none was saved or it is not one of the levels.
        static bool LoadSettings();
        static bool HasSavedSettings();
        static void ClearSavedSettings();
    };

}
