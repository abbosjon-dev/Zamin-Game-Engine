#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#define STB_RECT_PACK_IMPLEMENTATION
#include <stb_rect_pack.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

#include "Zamin/Utils/Image.h"

#include <algorithm>
#include <cstdlib>
#include <mutex>

namespace Zamin {

    // stb_image_write keeps its "flip" option in a global: writers take turns.
    static std::mutex s_WriteMutex;

    bool Image::WritePNG(const std::string& path, uint32_t width, uint32_t height, uint32_t channels,
                         const void* data, bool flipVertically)
    {
        std::lock_guard lock(s_WriteMutex);
        stbi_flip_vertically_on_write(flipVertically ? 1 : 0);
        int ok = stbi_write_png(path.c_str(), (int)width, (int)height, (int)channels, data, (int)(width * channels));
        stbi_flip_vertically_on_write(0);
        return ok != 0;
    }

    std::vector<uint8_t> Image::EncodePNG(uint32_t width, uint32_t height, uint32_t channels, const void* data)
    {
        std::vector<uint8_t> bytes;
        if (!data || width == 0 || height == 0 || channels < 1 || channels > 4)
            return bytes;
        std::lock_guard lock(s_WriteMutex);
        stbi_flip_vertically_on_write(0);
        auto append = [](void* context, void* chunk, int size) {
            auto* out = (std::vector<uint8_t>*)context;
            out->insert(out->end(), (const uint8_t*)chunk, (const uint8_t*)chunk + size);
        };
        if (!stbi_write_png_to_func(append, &bytes, (int)width, (int)height, (int)channels, data, (int)(width * channels)))
            bytes.clear();
        return bytes;
    }

    bool Image::Load(const std::string& path, ImageData& out, uint32_t channels, bool flipVertically, std::string* error)
    {
        out = {};
        channels = channels < 1 ? 1 : channels > 4 ? 4 : channels;
        // Per thread: a loader on another thread (thumbnails) cannot change it in the middle, and
        // once a thread has set it, stb ignores the global setting there.
        stbi_set_flip_vertically_on_load_thread(flipVertically ? 1 : 0);
        int width = 0, height = 0, fileChannels = 0;
        stbi_uc* data = stbi_load(path.c_str(), &width, &height, &fileChannels, (int)channels);
        if (!data)
        {
            if (error)
            {
                const char* reason = stbi_failure_reason();
                *error = reason ? reason : "unknown";
            }
            return false;
        }
        out.Width = (uint32_t)width;
        out.Height = (uint32_t)height;
        out.Channels = channels;
        out.FileChannels = (uint32_t)fileChannels;
        out.Pixels.assign(data, data + (size_t)width * (size_t)height * channels);
        stbi_image_free(data);
        return true;
    }

    ImageDiff Image::Compare(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, uint32_t width, uint32_t height,
                             int threshold, bool diffImage)
    {
        ImageDiff diff;
        diff.Width = width;
        diff.Height = height;
        const size_t pixels = (size_t)width * height;
        if (a.size() != pixels * 4 || b.size() != pixels * 4)
        {
            diff.DifferentPixels = pixels;
            diff.MaxDelta = pixels ? 255 : 0;
            diff.MeanDelta = pixels ? 255.0 : 0.0;
            return diff;
        }
        if (diffImage)
            diff.DiffPixels.resize(pixels * 4);
        uint64_t sum = 0;
        for (size_t i = 0; i < pixels; i++)
        {
            const uint8_t* pa = &a[i * 4];
            const uint8_t* pb = &b[i * 4];
            int delta = 0;
            for (int c = 0; c < 3; c++)
                delta = std::max(delta, std::abs((int)pa[c] - (int)pb[c]));
            sum += (uint64_t)delta;
            diff.MaxDelta = std::max(diff.MaxDelta, delta);
            const bool differs = delta > threshold;
            if (differs)
                diff.DifferentPixels++;
            if (diffImage)
            {
                uint8_t* out = &diff.DiffPixels[i * 4];
                if (differs)
                {
                    // red, brighter with the difference (a small one still clearly visible)
                    out[0] = (uint8_t)std::min(255, 120 + delta * 135 / 255 * 2);
                    out[1] = out[2] = 0;
                }
                else
                {
                    int grey = (pa[0] * 54 + pa[1] * 183 + pa[2] * 19) >> 8; // luma
                    out[0] = out[1] = out[2] = (uint8_t)(grey / 3);
                }
                out[3] = 255;
            }
        }
        diff.MeanDelta = pixels ? (double)sum / (double)pixels : 0.0;
        return diff;
    }

    std::vector<uint8_t> Image::MakeChannelView(const std::vector<uint8_t>& rgba, ImageChannelView view)
    {
        if (view == ImageChannelView::RGBA)
            return rgba;
        std::vector<uint8_t> out(rgba.size());
        int channel = view == ImageChannelView::Red ? 0 : view == ImageChannelView::Green ? 1 : view == ImageChannelView::Blue ? 2 : 3;
        for (size_t i = 0; i + 3 < rgba.size(); i += 4)
        {
            if (view == ImageChannelView::RGB)
            {
                out[i] = rgba[i];
                out[i + 1] = rgba[i + 1];
                out[i + 2] = rgba[i + 2];
            }
            else
                out[i] = out[i + 1] = out[i + 2] = rgba[i + channel];
            out[i + 3] = 255;
        }
        return out;
    }

}
