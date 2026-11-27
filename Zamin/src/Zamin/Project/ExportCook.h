#pragma once

/// @file
/// @brief The export's cook step: sprite atlas pages re-packed when stale, textures cooked in place (TextureCook).

#include "Zamin/Asset/TextureCook.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Zamin {

    struct QualitySettings;

    struct ExportCookInputs
    {
        std::filesystem::path SourceAssets;   // the project's asset folder (atlas sources are read there)
        std::filesystem::path ExportAssets;   // the exported asset folder: the plan's files copied there already
        std::vector<std::string> Files;       // the plan's asset-relative files
        TextureCookSettings Settings;
        std::filesystem::path CacheDirectory; // empty: no cache
        uint32_t Threads = 0;                 // 0: up to 4
    };

    struct ExportCookResult
    {
        std::vector<CookedAsset> Assets;      // sorted by path
        size_t CacheHits = 0, Atlases = 0;    // atlases: pages re-packed because their sources changed
        std::string AtlasTable;               // JSON array: every atlas page and its sprites' rectangles / UVs
        std::vector<std::string> Warnings;
        double Seconds = 0.0;
    };

    class ExportCook
    {
    public:
        // The mip levels every quality level skips (TextureQuality): baked out by MatchQuality.
        static uint32_t QualitySkips(const QualitySettings& quality);
        // Runs the cook over the exported folder and writes <ExportAssets>/../cook-manifest.json.
        static ExportCookResult Run(const ExportCookInputs& inputs);
        // Image files the cook takes (8-bit formats stb decodes; HDR stays as it is).
        static bool IsCookable(const std::string& path);
    };

}
