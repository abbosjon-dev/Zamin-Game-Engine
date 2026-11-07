#include "Zamin/Asset/AssetThumbnails.h"
#include "Zamin/Math/Color.h"
#include "Zamin/Asset/AssetImportLog.h"
#include "Zamin/Asset/AssetTypes.h"
#include "Zamin/Utils/FileSystem.h"
#include "Zamin/Utils/Image.h"

#include <stb_image.h>
#include <stb_truetype.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <fstream>

namespace Zamin {

    namespace fs = std::filesystem;

    // ------------------------------------------------------------------ stamps
    ThumbnailStamp ThumbnailStamp::From(uintmax_t size, fs::file_time_type time, uint32_t maxSize)
    {
        ThumbnailStamp stamp;
        stamp.Size = size;
        stamp.Time = (int64_t)time.time_since_epoch().count();
        stamp.MaxSize = maxSize;
        return stamp;
    }

    std::optional<ThumbnailStamp> ThumbnailStamp::Read(const fs::path& file, uint32_t maxSize)
    {
        std::error_code ec;
        uintmax_t size = fs::file_size(file, ec);
        if (ec)
            return std::nullopt;
        fs::file_time_type time = fs::last_write_time(file, ec);
        if (ec)
            return std::nullopt;
        return From(size, time, maxSize);
    }

    ThumbnailStamp ThumbnailStamp::WithDependencies(const std::vector<fs::path>& dependencies) const
    {
        // FNV-1a over each dependency (its path and version), summed so the order does not count.
        auto fnv = [](uint64_t h, const void* data, size_t size) {
            const auto* bytes = (const uint8_t*)data;
            for (size_t i = 0; i < size; i++)
                h = (h ^ bytes[i]) * 0x100000001B3ull;
            return h;
        };
        uint64_t sum = 0;
        for (const fs::path& dependency : dependencies)
        {
            std::string key = ThumbnailDiskCache::MakeKey(dependency);
            uint64_t h = fnv(0xCBF29CE484222325ull, key.data(), key.size());
            std::error_code ec;
            uintmax_t size = fs::file_size(dependency, ec);
            int64_t time = 0;
            if (!ec)
            {
                fs::file_time_type t = fs::last_write_time(dependency, ec);
                time = ec ? 0 : (int64_t)t.time_since_epoch().count();
            }
            else
                size = ~(uintmax_t)0; // missing: differs from any file that is there
            h = fnv(h, &size, sizeof(size));
            h = fnv(h, &time, sizeof(time));
            sum += h;
        }
        ThumbnailStamp stamp = *this;
        if (!dependencies.empty())
            stamp.Time = (int64_t)((uint64_t)Time ^ (sum * 0x9E3779B97F4A7C15ull + dependencies.size()));
        return stamp;
    }

    // ------------------------------------------------------------------ generating
    bool AssetThumbnails::CanGenerate(const fs::path& path)
    {
        AssetType type = AssetTypes::FromPath(path);
        return type == AssetType::Texture || type == AssetType::Environment || type == AssetType::Font;
    }

    static void FitSize(uint32_t width, uint32_t height, uint32_t maxSize, uint32_t& outWidth, uint32_t& outHeight)
    {
        float scale = std::min(1.0f, (float)std::max(1u, maxSize) / (float)std::max(width, height));
        outWidth = std::clamp((uint32_t)std::lround(width * scale), 1u, width);
        outHeight = std::clamp((uint32_t)std::lround(height * scale), 1u, height);
    }

    ThumbnailImage AssetThumbnails::Downscale(const uint8_t* rgba, uint32_t width, uint32_t height, uint32_t maxSize)
    {
        ThumbnailImage out;
        if (!rgba || width == 0 || height == 0)
            return out;
        FitSize(width, height, maxSize, out.Width, out.Height);
        if (out.Width == width && out.Height == height)
        {
            out.Pixels.assign(rgba, rgba + (size_t)width * height * 4);
            return out;
        }
        // Each source pixel adds to the output pixel it falls in; colors weighted by alpha so
        // transparent pixels do not darken the edges.
        size_t count = (size_t)out.Width * out.Height;
        std::vector<float> color(count * 3, 0.0f), alpha(count, 0.0f), samples(count, 0.0f);
        for (uint32_t y = 0; y < height; y++)
        {
            size_t oy = (size_t)y * out.Height / height;
            const uint8_t* row = rgba + (size_t)y * width * 4;
            for (uint32_t x = 0; x < width; x++)
            {
                size_t o = oy * out.Width + (size_t)x * out.Width / width;
                const uint8_t* p = row + (size_t)x * 4;
                float a = p[3] / 255.0f;
                color[o * 3 + 0] += p[0] * a;
                color[o * 3 + 1] += p[1] * a;
                color[o * 3 + 2] += p[2] * a;
                alpha[o] += a;
                samples[o] += 1.0f;
            }
        }
        out.Pixels.resize(count * 4);
        for (size_t i = 0; i < count; i++)
        {
            float a = alpha[i];
            for (int c = 0; c < 3; c++)
                out.Pixels[i * 4 + c] = a > 0.0f ? (uint8_t)std::clamp(std::lround(color[i * 3 + c] / a), 0l, 255l) : 0;
            out.Pixels[i * 4 + 3] = samples[i] > 0.0f ? (uint8_t)std::clamp(std::lround(a / samples[i] * 255.0f), 0l, 255l) : 0;
        }
        return out;
    }

    static bool GenerateImage(const fs::path& path, uint32_t maxSize, ThumbnailImage& out, std::string& error)
    {
        int width = 0, height = 0, channels = 0;
        stbi_uc* data = stbi_load(path.string().c_str(), &width, &height, &channels, 4);
        if (!data)
        {
            error = AssetImportLog::DescribeImageError(stbi_failure_reason());
            return false;
        }
        out = AssetThumbnails::Downscale(data, (uint32_t)width, (uint32_t)height, maxSize);
        stbi_image_free(data);
        return out.IsValid();
    }

    static bool GenerateEnvironment(const fs::path& path, uint32_t maxSize, ThumbnailImage& out, std::string& error)
    {
        int width = 0, height = 0, channels = 0;
        float* data = stbi_loadf(path.string().c_str(), &width, &height, &channels, 3);
        if (!data)
        {
            error = AssetImportLog::DescribeImageError(stbi_failure_reason());
            return false;
        }
        // Averaged in linear light, then tone mapped (Reinhard) and gamma encoded.
        FitSize((uint32_t)width, (uint32_t)height, maxSize, out.Width, out.Height);
        size_t count = (size_t)out.Width * out.Height;
        std::vector<float> sum(count * 3, 0.0f), samples(count, 0.0f);
        for (int y = 0; y < height; y++)
            for (int x = 0; x < width; x++)
            {
                size_t o = ((size_t)y * out.Height / height) * out.Width + (size_t)x * out.Width / width;
                const float* p = data + ((size_t)y * width + x) * 3;
                for (int c = 0; c < 3; c++)
                    sum[o * 3 + c] += std::max(0.0f, p[c]);
                samples[o] += 1.0f;
            }
        stbi_image_free(data);
        out.Pixels.resize(count * 4);
        for (size_t i = 0; i < count; i++)
        {
            for (int c = 0; c < 3; c++)
            {
                float v = samples[i] > 0.0f ? sum[i * 3 + c] / samples[i] : 0.0f;
                v = v / (1.0f + v);
                out.Pixels[i * 4 + c] = (uint8_t)std::clamp(std::lround(Color::LinearToGamma(v) * 255.0f), 0l, 255l);
            }
            out.Pixels[i * 4 + 3] = 255;
        }
        return true;
    }

    static bool GenerateFont(const fs::path& path, uint32_t maxSize, ThumbnailImage& out, std::string& error)
    {
        std::vector<uint8_t> ttf = FileSystem::ReadBinaryFile(path);
        stbtt_fontinfo info;
        int offset = ttf.empty() ? -1 : stbtt_GetFontOffsetForIndex(ttf.data(), 0);
        if (offset < 0 || !stbtt_InitFont(&info, ttf.data(), offset))
        {
            error = ttf.empty() ? "fayl o'qilmadi" : "TrueType / OpenType shrift emas yoki fayl buzilgan";
            return false;
        }
        uint32_t size = std::max(16u, maxSize);
        out.Width = out.Height = size;
        out.Pixels.assign((size_t)size * size * 4, 0);

        // "Aa" in the middle, as large as fits.
        const int text[] = { 'A', 'a' };
        int ascent = 0, descent = 0, gap = 0;
        stbtt_GetFontVMetrics(&info, &ascent, &descent, &gap);
        float scale = stbtt_ScaleForPixelHeight(&info, size * 0.62f);
        auto measure = [&](float s) {
            float width = 0.0f;
            for (size_t i = 0; i < std::size(text); i++)
            {
                int advance = 0, bearing = 0;
                stbtt_GetCodepointHMetrics(&info, text[i], &advance, &bearing);
                width += advance * s;
                if (i + 1 < std::size(text))
                    width += stbtt_GetCodepointKernAdvance(&info, text[i], text[i + 1]) * s;
            }
            return width;
        };
        float width = measure(scale);
        if (width > size * 0.92f && width > 0.0f)
        {
            scale *= size * 0.92f / width;
            width = measure(scale);
        }
        float boxHeight = (ascent - descent) * scale;
        float baseline = (size - boxHeight) * 0.5f + ascent * scale;
        float x = (size - width) * 0.5f;
        for (size_t i = 0; i < std::size(text); i++)
        {
            int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
            stbtt_GetCodepointBitmapBox(&info, text[i], scale, scale, &x0, &y0, &x1, &y1);
            int gw = x1 - x0, gh = y1 - y0;
            if (gw > 0 && gh > 0)
            {
                std::vector<uint8_t> glyph((size_t)gw * gh);
                stbtt_MakeCodepointBitmap(&info, glyph.data(), gw, gh, gw, scale, scale, text[i]);
                int left = (int)std::lround(x) + x0, top = (int)std::lround(baseline) + y0;
                for (int gy = 0; gy < gh; gy++)
                    for (int gx = 0; gx < gw; gx++)
                    {
                        int px = left + gx, py = top + gy;
                        if (px < 0 || py < 0 || px >= (int)size || py >= (int)size)
                            continue;
                        uint8_t* p = &out.Pixels[((size_t)py * size + px) * 4];
                        p[0] = 236;
                        p[1] = 239;
                        p[2] = 246;
                        p[3] = std::max(p[3], glyph[(size_t)gy * gw + gx]);
                    }
            }
            int advance = 0, bearing = 0;
            stbtt_GetCodepointHMetrics(&info, text[i], &advance, &bearing);
            x += advance * scale;
            if (i + 1 < std::size(text))
                x += stbtt_GetCodepointKernAdvance(&info, text[i], text[i + 1]) * scale;
        }
        return true;
    }

    bool AssetThumbnails::Generate(const fs::path& path, uint32_t maxSize, ThumbnailImage& out, std::string* error)
    {
        out = {};
        std::string reason = AssetImportLog::CheckReadable(path);
        bool ok = false;
        if (reason.empty())
        {
            stbi_set_flip_vertically_on_load_thread(0); // top row first (every loader sets its own thread's flag)
            switch (AssetTypes::FromPath(path))
            {
                case AssetType::Texture:     ok = GenerateImage(path, maxSize, out, reason); break;
                case AssetType::Environment: ok = GenerateEnvironment(path, maxSize, out, reason); break;
                case AssetType::Font:        ok = GenerateFont(path, maxSize, out, reason); break;
                default:                     reason = "bu turdagi fayl uchun rasm yasalmaydi"; break;
            }
        }
        if (!ok)
            out = {};
        if (error)
            *error = ok ? std::string() : reason;
        return ok;
    }

    // ------------------------------------------------------------------ disk cache
    static constexpr char s_Magic[4] = { 'Z', 'T', 'H', 'B' };
    static constexpr uint32_t s_Version = 1;

    std::string ThumbnailDiskCache::MakeKey(const fs::path& asset)
    {
        std::error_code ec;
        fs::path abs = fs::absolute(asset, ec);
        return (ec ? asset : abs).lexically_normal().generic_string();
    }

    fs::path ThumbnailDiskCache::GetCacheFile(const fs::path& asset) const
    {
        uint64_t hash = 1469598103934665603ull; // FNV-1a
        for (unsigned char c : MakeKey(asset))
        {
            hash ^= c;
            hash *= 1099511628211ull;
        }
        return m_Directory / std::format("{:016x}.zthumb", hash);
    }

    namespace {

        struct CacheHeader
        {
            ThumbnailStamp Stamp;
            uint32_t Width = 0, Height = 0;
            std::string Key;
            size_t DataOffset = 0, DataSize = 0;
        };

        template<typename T>
        void Put(std::vector<uint8_t>& out, T value)
        {
            const uint8_t* p = (const uint8_t*)&value;
            out.insert(out.end(), p, p + sizeof(T));
        }

        template<typename T>
        bool Get(const std::vector<uint8_t>& in, size_t& at, T& value)
        {
            if (at + sizeof(T) > in.size())
                return false;
            std::memcpy(&value, in.data() + at, sizeof(T));
            at += sizeof(T);
            return true;
        }

        bool ParseHeader(const std::vector<uint8_t>& bytes, CacheHeader& header)
        {
            if (bytes.size() < 4 || std::memcmp(bytes.data(), s_Magic, 4) != 0)
                return false;
            size_t at = 4;
            uint32_t version = 0, keyLength = 0, dataLength = 0;
            uint64_t size = 0;
            if (!Get(bytes, at, version) || version != s_Version || !Get(bytes, at, size) || !Get(bytes, at, header.Stamp.Time) ||
                !Get(bytes, at, header.Stamp.MaxSize) || !Get(bytes, at, header.Width) || !Get(bytes, at, header.Height) ||
                !Get(bytes, at, keyLength) || at + keyLength > bytes.size())
                return false;
            header.Stamp.Size = (uintmax_t)size;
            header.Key.assign((const char*)bytes.data() + at, keyLength);
            at += keyLength;
            if (!Get(bytes, at, dataLength) || at + dataLength != bytes.size())
                return false;
            header.DataOffset = at;
            header.DataSize = dataLength;
            return true;
        }

    }

    bool ThumbnailDiskCache::Load(const fs::path& asset, const ThumbnailStamp& stamp, ThumbnailImage& out) const
    {
        if (!IsEnabled())
            return false;
        fs::path file = GetCacheFile(asset);
        std::error_code ec;
        if (!fs::is_regular_file(file, ec))
            return false;
        std::vector<uint8_t> bytes = FileSystem::ReadBinaryFile(file);
        CacheHeader header;
        if (!ParseHeader(bytes, header) || header.Key != MakeKey(asset) || !(header.Stamp == stamp))
            return false;
        stbi_set_flip_vertically_on_load_thread(0);
        int width = 0, height = 0, channels = 0;
        stbi_uc* pixels = stbi_load_from_memory(bytes.data() + header.DataOffset, (int)header.DataSize, &width, &height, &channels, 4);
        if (!pixels)
            return false;
        bool ok = (uint32_t)width == header.Width && (uint32_t)height == header.Height;
        if (ok)
        {
            out.Width = (uint32_t)width;
            out.Height = (uint32_t)height;
            out.Pixels.assign(pixels, pixels + (size_t)width * height * 4);
        }
        stbi_image_free(pixels);
        return ok;
    }

    bool ThumbnailDiskCache::Save(const fs::path& asset, const ThumbnailStamp& stamp, const ThumbnailImage& image) const
    {
        if (!IsEnabled() || !image.IsValid())
            return false;
        std::vector<uint8_t> png = Image::EncodePNG(image.Width, image.Height, 4, image.Pixels.data());
        if (png.empty())
            return false;
        std::string key = MakeKey(asset);
        std::vector<uint8_t> bytes(s_Magic, s_Magic + 4);
        Put(bytes, s_Version);
        Put(bytes, (uint64_t)stamp.Size);
        Put(bytes, stamp.Time);
        Put(bytes, stamp.MaxSize);
        Put(bytes, image.Width);
        Put(bytes, image.Height);
        Put(bytes, (uint32_t)key.size());
        bytes.insert(bytes.end(), key.begin(), key.end());
        Put(bytes, (uint32_t)png.size());
        bytes.insert(bytes.end(), png.begin(), png.end());

        std::error_code ec;
        fs::create_directories(m_Directory, ec);
        fs::path file = GetCacheFile(asset);
        fs::path temp = file;
        temp += ".tmp";
        {
            std::ofstream stream(temp, std::ios::binary | std::ios::trunc);
            if (!stream)
                return false;
            stream.write((const char*)bytes.data(), (std::streamsize)bytes.size());
            if (!stream)
                return false;
        }
        fs::rename(temp, file, ec); // whole files only: a reader never sees half of one
        if (ec)
        {
            fs::remove(temp, ec);
            return false;
        }
        return true;
    }

    void ThumbnailDiskCache::Remove(const fs::path& asset) const
    {
        if (!IsEnabled())
            return;
        std::error_code ec;
        fs::remove(GetCacheFile(asset), ec);
    }

    size_t ThumbnailDiskCache::Prune() const
    {
        size_t removed = 0;
        std::error_code ec;
        if (!IsEnabled() || !fs::is_directory(m_Directory, ec))
            return 0;
        std::vector<fs::path> gone;
        for (fs::directory_iterator it(m_Directory, ec), end; !ec && it != end; it.increment(ec))
        {
            const fs::path& file = it->path();
            std::string ext = file.extension().string();
            if (ext == ".tmp")
            {
                gone.push_back(file);
                continue;
            }
            if (ext != ".zthumb")
                continue;
            CacheHeader header;
            std::error_code fec;
            if (!ParseHeader(FileSystem::ReadBinaryFile(file), header) || !fs::exists(fs::path(header.Key), fec))
                gone.push_back(file);
        }
        for (const fs::path& file : gone)
        {
            std::error_code rec;
            if (fs::remove(file, rec))
                removed++;
        }
        return removed;
    }

    size_t ThumbnailDiskCache::Clear() const
    {
        size_t removed = 0;
        std::error_code ec;
        if (!IsEnabled() || !fs::is_directory(m_Directory, ec))
            return 0;
        std::vector<fs::path> files;
        for (fs::directory_iterator it(m_Directory, ec), end; !ec && it != end; it.increment(ec))
            if (it->path().extension() == ".zthumb" || it->path().extension() == ".tmp")
                files.push_back(it->path());
        for (const fs::path& file : files)
        {
            std::error_code rec;
            if (fs::remove(file, rec))
                removed++;
        }
        return removed;
    }

}
