#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Zamin {

    // Pixels of an image file decoded on the CPU (8 bits per channel).
    struct ImageData
    {
        uint32_t Width = 0, Height = 0;
        uint32_t Channels = 0;          // channels in Pixels (the requested count)
        uint32_t FileChannels = 0;      // channels the file stores
        std::vector<uint8_t> Pixels;    // rows bottom-up when loaded flipped (like file textures)

        bool IsValid() const { return Width > 0 && Height > 0 && !Pixels.empty(); }
    };

    // How two pictures of the same size differ (Image::Compare).
    struct ImageDiff
    {
        uint32_t Width = 0, Height = 0;
        uint64_t DifferentPixels = 0;   // pixels where some channel differs by more than the threshold
        int MaxDelta = 0;               // the largest channel difference (0..255)
        double MeanDelta = 0.0;         // the mean of every pixel's largest channel difference
        // RGBA8, rows as the inputs: the first picture dimmed to grey, the differing pixels over
        // it in red (brighter = larger difference). Empty unless asked for.
        std::vector<uint8_t> DiffPixels;

        uint64_t GetPixelCount() const { return (uint64_t)Width * Height; }
        // The share of differing pixels, in percent.
        double GetDifferentPercent() const { return GetPixelCount() ? 100.0 * (double)DifferentPixels / (double)GetPixelCount() : 0.0; }
    };

    // What an image preview shows of RGBA pixels.
    enum class ImageChannelView { RGBA = 0, RGB, Red, Green, Blue, Alpha };

    class Image
    {
    public:
        static bool WritePNG(const std::string& path, uint32_t width, uint32_t height, uint32_t channels,
                             const void* data, bool flipVertically = false);
        // The PNG file bytes of the pixels (rows top first), empty on failure. Thread-safe.
        static std::vector<uint8_t> EncodePNG(uint32_t width, uint32_t height, uint32_t channels, const void* data);

        // Decodes png / jpg / bmp / tga / hdr (tone-clamped to 8 bits)... into `channels` channels
        // (1-4). False with the reason in `error` when the file cannot be read.
        static bool Load(const std::string& path, ImageData& out, uint32_t channels = 4, bool flipVertically = false,
                         std::string* error = nullptr);

        // RGBA8 pixels showing one part of RGBA8 pixels: RGB = colors without transparency, a single
        // channel as gray (opaque). RGBA returns the pixels unchanged.
        static std::vector<uint8_t> MakeChannelView(const std::vector<uint8_t>& rgba, ImageChannelView view);

        // Compares two RGBA8 pictures of width x height pixels (alpha ignored): a pixel differs when
        // one of its colour channels differs by more than `threshold`. `diffImage`: also make
        // ImageDiff::DiffPixels. Inputs of the wrong size count every pixel as different.
        static ImageDiff Compare(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, uint32_t width, uint32_t height,
                                 int threshold = 0, bool diffImage = false);
    };

}
