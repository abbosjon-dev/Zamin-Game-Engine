#pragma once

/// @file
/// @brief Sprite atlas assets (.zatlas): a folder of sprites packed into one page texture with named frames.

#include "Zamin/Asset/TextureAtlas.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Zamin {

    class AssetRegistry;

    // A .zatlas file: which folder's images are packed, how, and where the page goes. The page is an ordinary
    // texture asset (Output, e.g. "Sprites/Hero.atlas.png") whose .zmeta frames ("Sprites") are the packed
    // sprites, named after their source files (or the source's own frames) - so Sprite Renderers, Tilemaps and
    // sprite clips use it like any sprite sheet, and the unused source folder stays out of an export.
    struct SpriteAtlasDefinition
    {
        static constexpr int CurrentVersion = 1;
        static constexpr const char* Extension = ".zatlas";

        std::string Sources;          // asset-relative folder
        std::string Output;           // asset-relative page texture (.png)
        bool Recursive = false;       // images in sub-folders too (named "<sub>/<name>")
        bool PixelArt = false;        // nearest filtering on the page
        glm::vec2 Pivot{ 0.5f };      // pivot of sprites whose source has none
        // Grid layout (tilesets): every sprite the same size, in name order, Columns per row, no padding - the
        // page is then an ordinary Tilemap tileset of Columns x rows cells (tile i = frame i).
        bool Grid = false;
        uint32_t Columns = 8;
        AtlasSettings Packing;        // MaxPageSize / Padding / MipLevels (one page)
        // Written by Build: the hash of the sources and settings the page was made from, and what it holds.
        uint64_t SourceHash = 0;
        uint32_t SpriteCount = 0, PageWidth = 0, PageHeight = 0;

        std::string ToJson() const;
        static bool FromJson(const std::string& text, SpriteAtlasDefinition& out, std::string* error = nullptr);
        static bool Load(const std::filesystem::path& file, SpriteAtlasDefinition& out, std::string* error = nullptr);
        bool Save(const std::filesystem::path& file) const;
    };

    struct SpriteAtlasBuildResult
    {
        bool Ok = false;
        bool Changed = false;             // the page / frames were written (false: up to date, nothing touched)
        uint32_t Sprites = 0, PageWidth = 0, PageHeight = 0;
        double Occupancy = 0.0;
        uint64_t SourceHash = 0;
        std::vector<std::string> Added, Removed, Kept; // frame names compared with the page's previous frames
        std::string Error;
    };

    class SpriteAtlas
    {
    public:
        // The hash of the source images (bytes, frames of their .zmeta) and the packing settings.
        static uint64_t HashSources(const std::filesystem::path& assetDirectory, const SpriteAtlasDefinition& atlas, std::string* error = nullptr);
        // Packs the sources and writes the page PNG and its .zmeta frames (the page keeps its UUID; a frame of
        // the same name keeps the pivot and borders edited in the Sprite Editor). `force` false: nothing is
        // written when the sources hash is unchanged and the page exists. `registry`: told about the files.
        static SpriteAtlasBuildResult Build(const std::filesystem::path& assetDirectory, const std::filesystem::path& atlasFile,
                                            bool force = false, AssetRegistry* registry = nullptr);
        // Packs into memory only (the export's cook: the page and its frames, nothing written).
        static bool Pack(const std::filesystem::path& assetDirectory, const SpriteAtlasDefinition& atlas, AtlasResult& result,
                         std::vector<std::string>& names, std::vector<glm::vec2>& pivots, std::vector<glm::vec4>& borders, std::string& error);
        // Packs `atlas` (sources under `assetDirectory`) and writes the page PNG to `output` with its .zmeta frames
        // (merged with the .zmeta already there). Build and the export's cook use it.
        static bool WritePage(const std::filesystem::path& assetDirectory, const SpriteAtlasDefinition& atlas, const std::filesystem::path& output,
                              const std::string& atlasPath, SpriteAtlasBuildResult& result, AtlasResult* packed = nullptr);
        static bool IsStale(const std::filesystem::path& assetDirectory, const std::filesystem::path& atlasFile);
        // Content browser: "<folder>.zatlas" next to the folder (and "<folder>.atlas.png"), built at once.
        // Returns the atlas's asset-relative path ("" with `error`).
        static std::string CreateFromFolder(const std::filesystem::path& assetDirectory, const std::string& folder, std::string& error,
                                            AssetRegistry* registry = nullptr);
        // The image files of the source folder, asset-relative and sorted.
        static std::vector<std::string> ListSources(const std::filesystem::path& assetDirectory, const SpriteAtlasDefinition& atlas);
    };

}
