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

    // One Visual Script graph of an export's graph cook (the report).
    struct CookedGraph
    {
        std::string Path;              // asset-relative
        std::string Action;            // "cooked" (bytecode written), "rejected" (does not validate: kept as it is),
                                       // "kept" (the parent of a rejected graph, which still loads it)
        uint64_t SourceBytes = 0, CookedBytes = 0;
        uint32_t Nodes = 0;            // of the program (parent chain and macros included)
        bool CacheHit = false;
        std::string Note;              // a rejected graph's first error; native method warnings
        bool operator==(const CookedGraph&) const = default;
    };

    struct GraphCookInputs
    {
        std::filesystem::path SourceAssets;   // the project's asset folder (graphs and their parents are read there)
        std::filesystem::path ExportAssets;   // the exported asset folder (cooked graphs are written there)
        std::vector<std::string> Files;       // the plan's asset-relative files
        std::filesystem::path CacheDirectory; // empty: no cache
    };

    struct GraphCookResult
    {
        std::vector<CookedGraph> Graphs;      // sorted by path
        size_t CacheHits = 0;
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
        // Compiles every graph of the plan to bytecode in the exported folder (Visual::Bytecode). The validation
        // is the runtime's (Machine::Load): a graph the Player would reject is kept and reported, never cooked.
        static GraphCookResult CookGraphs(const GraphCookInputs& inputs);
    };

}
