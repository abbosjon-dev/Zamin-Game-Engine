#pragma once

/// @file
/// @brief Cooked textures (ZTEX): export-time decoded/resized/compressed texture data with an incremental cache.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Zamin {

    // How an export cooks its textures (the .zproject's "Export" section).
    struct TextureCookSettings
    {
        bool Enabled = true;
        bool Compress = true;          // deflate with per-row prediction (lossless)
        uint32_t MaxTextureSize = 0;   // px: larger mip-mapped, linear, frameless textures are halved down to it (0: keep)
        // Mip levels every quality level skips (Quality: TextureQuality): those are baked out of mip-mapped,
        // linear, frameless textures and the runtime samples the cooked texture as if they were still there.
        uint32_t QualitySkips = 0;     // (set by the export from the project's quality levels when MatchQuality)
        bool MatchQuality = true;
        bool Always = false;           // replace the source even when the cooked file is not smaller

        bool operator==(const TextureCookSettings&) const = default;
    };

    // What a cooked file says about itself (its header).
    struct CookedTextureInfo
    {
        uint32_t Width = 0, Height = 0, Channels = 0;
        uint32_t SourceWidth = 0, SourceHeight = 0;
        uint32_t BakedSkips = 0;       // mip levels removed at cook time (the runtime skips that many fewer)
        uint64_t SourceHash = 0;
        bool Compressed = false;
    };

    // One texture of an export's cook (the report, cook-manifest.json).
    struct CookedAsset
    {
        std::string Path;              // asset-relative
        std::string Action;            // "cooked" (ZTEX written), "kept" (source smaller), "failed"
        uint64_t SourceBytes = 0, CookedBytes = 0;
        uint32_t Width = 0, Height = 0, SourceWidth = 0, SourceHeight = 0;
        bool CacheHit = false;
        std::string Note;              // why it was kept / resized / what failed
        bool operator==(const CookedAsset&) const = default;
    };

    class TextureCook
    {
    public:
        static constexpr uint32_t FormatVersion = 1;
        static constexpr const char* ManifestFileName = "cook-manifest.json";
        // The cache folder of a project's cooks (<project>/Build/CookCache).
        static std::filesystem::path DefaultCacheDirectory(const std::filesystem::path& projectDirectory);

        // The data / file starts with the ZTEX header (a cooked texture keeps its source's path in an export).
        static bool IsCooked(const void* data, size_t size);
        static bool IsCookedFile(const std::filesystem::path& file);
        // Pixels: 8-bit, `channels` (1, 3 or 4), rows bottom first (as OpenGL and the texture loader keep them).
        static std::vector<uint8_t> Encode(const uint8_t* pixels, uint32_t width, uint32_t height, uint32_t channels, bool compress,
                                           const CookedTextureInfo& source);
        static bool Decode(const uint8_t* data, size_t size, std::vector<uint8_t>& pixels, CookedTextureInfo& info, std::string* error = nullptr);
        static bool DecodeFile(const std::filesystem::path& file, std::vector<uint8_t>& pixels, CookedTextureInfo& info, std::string* error = nullptr);
        static bool ReadInfo(const std::filesystem::path& file, CookedTextureInfo& info);
        // 2x2 box halving of 8-bit pixels (any channel count, odd sizes clamp).
        static void Halve(std::vector<uint8_t>& pixels, uint32_t& width, uint32_t& height, uint32_t channels);

        // The cache key: the source bytes, the settings that change the output and the format version.
        static uint64_t CacheKey(const std::vector<uint8_t>& sourceBytes, const TextureCookSettings& settings, bool resizable);

        struct Result
        {
            bool Ok = false, CacheHit = false, Resized = false;
            std::vector<uint8_t> Bytes;   // the ZTEX file
            CookedTextureInfo Info;
            uint64_t SourceBytes = 0;
            std::string Error;
        };
        // Cooks one image file (any thread): from `cacheDirectory` when a valid entry for its key is there, else
        // decoded, resized (only when `resizable`), encoded and stored there. An empty cache folder: no cache.
        static Result CookFile(const std::filesystem::path& source, const TextureCookSettings& settings, bool resizable,
                               const std::filesystem::path& cacheDirectory);
        // The same for pixels made in memory (an atlas page re-packed by the cook), rows top first, RGBA8.
        static Result CookPixels(const std::vector<uint8_t>& rgbaTopFirst, uint32_t width, uint32_t height, const TextureCookSettings& settings,
                                 const std::filesystem::path& cacheDirectory);

        // cook-manifest.json (next to the exported project): the settings, every texture and the atlas pages.
        static std::string ManifestToJson(const TextureCookSettings& settings, const std::vector<CookedAsset>& assets,
                                          const std::string& atlasTable);
        static bool ManifestFromJson(const std::string& text, std::vector<CookedAsset>& assets);
    };

}
