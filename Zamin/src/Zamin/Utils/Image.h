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
    };

}
