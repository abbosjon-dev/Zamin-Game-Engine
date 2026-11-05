#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Zamin {

    // A small picture of an asset for the content browser: RGBA8, top row first.
    struct ThumbnailImage
    {
        uint32_t Width = 0, Height = 0;
        std::vector<uint8_t> Pixels;

        bool IsValid() const { return Width > 0 && Height > 0 && Pixels.size() == (size_t)Width * Height * 4; }
    };

    // The version of a file a thumbnail was made from (and the size it was made for).
    struct ThumbnailStamp
    {
        uintmax_t Size = 0;
        int64_t Time = 0;        // file clock ticks
        uint32_t MaxSize = 0;

        bool operator==(const ThumbnailStamp&) const = default;
        // Nothing when the file cannot be read.
        static std::optional<ThumbnailStamp> Read(const std::filesystem::path& file, uint32_t maxSize);
        static ThumbnailStamp From(uintmax_t size, std::filesystem::file_time_type time, uint32_t maxSize);
    };

    // Thumbnails made on the CPU (any thread): images, HDR environment maps (tone mapped) and fonts
    // ("Aa" drawn with the font). Models need the GPU (the editor renders them).
    class AssetThumbnails
    {
    public:
        static bool CanGenerate(const std::filesystem::path& path);
        // Decoded and scaled down to fit maxSize x maxSize (the aspect kept). False, with the
        // reason in `error`, when the file cannot be read.
        static bool Generate(const std::filesystem::path& path, uint32_t maxSize, ThumbnailImage& out, std::string* error = nullptr);
        // Box filtered (alpha weighted) RGBA8 pixels, top row first, scaled to fit maxSize (never up).
        static ThumbnailImage Downscale(const uint8_t* rgba, uint32_t width, uint32_t height, uint32_t maxSize);
    };

    // Thumbnails saved in a folder (the project's ".zamin/Thumbnails"): one small file per asset -
    // a header with the asset's path and stamp, then a PNG - so a project opens with its thumbnails
    // without decoding every image again. A thumbnail is out of date once its file changed (size
    // or time). Safe to use from several threads (each call reads / writes whole files).
    class ThumbnailDiskCache
    {
    public:
        ThumbnailDiskCache() = default;
        explicit ThumbnailDiskCache(std::filesystem::path directory) : m_Directory(std::move(directory)) {}

        void SetDirectory(std::filesystem::path directory) { m_Directory = std::move(directory); }
        const std::filesystem::path& GetDirectory() const { return m_Directory; }
        bool IsEnabled() const { return !m_Directory.empty(); }

        // Where the thumbnail of `asset` is kept (named by a hash of its absolute path).
        std::filesystem::path GetCacheFile(const std::filesystem::path& asset) const;
        // False when there is none, it is of another version of the file, or it is broken.
        bool Load(const std::filesystem::path& asset, const ThumbnailStamp& stamp, ThumbnailImage& out) const;
        bool Save(const std::filesystem::path& asset, const ThumbnailStamp& stamp, const ThumbnailImage& image) const;
        void Remove(const std::filesystem::path& asset) const;
        // Removes the thumbnails of files that are gone (and broken cache files). Returns how many.
        size_t Prune() const;
        // Removes every thumbnail. Returns how many.
        size_t Clear() const;

        // The key the cache uses for a file: absolute, normalized, '/' separators.
        static std::string MakeKey(const std::filesystem::path& asset);

    private:
        std::filesystem::path m_Directory;
    };

}
