#include "Zamin/Asset/TextureCook.h"

#include "Zamin/Asset/TextureAtlas.h"
#include "Zamin/Utils/FileSystem.h"

#include <stb_image.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <format>
#include <fstream>

extern "C" unsigned char* stbi_zlib_compress(unsigned char* data, int data_len, int* out_len, int quality);

namespace Zamin {

    namespace fs = std::filesystem;

    namespace {
        constexpr size_t HeaderSize = 48;
        constexpr uint8_t FlagCompressed = 1;

        template<typename T> void Put(std::vector<uint8_t>& out, size_t at, T value) { std::memcpy(out.data() + at, &value, sizeof(T)); }
        template<typename T> T Get(const uint8_t* data, size_t at)
        {
            T value;
            std::memcpy(&value, data + at, sizeof(T));
            return value;
        }

        // Per-row prediction (like PNG's filters): 0 none, 1 left, 2 up, 3 average; the row whose residuals are
        // smallest in sum is chosen.
        std::vector<uint8_t> Filter(const uint8_t* pixels, uint32_t width, uint32_t height, uint32_t channels)
        {
            const size_t stride = size_t(width) * channels;
            std::vector<uint8_t> out(height * (stride + 1));
            std::vector<uint8_t> candidate(stride), best(stride);
            for (uint32_t y = 0; y < height; y++)
            {
                const uint8_t* row = pixels + y * stride;
                const uint8_t* up = y ? row - stride : nullptr;
                uint64_t bestScore = ~0ull;
                uint8_t bestType = 0;
                for (uint8_t type = 0; type < 4; type++)
                {
                    uint64_t score = 0;
                    for (size_t i = 0; i < stride; i++)
                    {
                        const int left = i >= channels ? row[i - channels] : 0, above = up ? up[i] : 0;
                        const int predicted = type == 0 ? 0 : type == 1 ? left : type == 2 ? above : (left + above) / 2;
                        const uint8_t residual = uint8_t(row[i] - predicted);
                        candidate[i] = residual;
                        score += residual < 128 ? residual : 256 - residual;
                    }
                    if (score < bestScore)
                    {
                        bestScore = score;
                        bestType = type;
                        best.swap(candidate);
                    }
                }
                out[y * (stride + 1)] = bestType;
                std::copy(best.begin(), best.end(), out.begin() + std::ptrdiff_t(y * (stride + 1) + 1));
            }
            return out;
        }

        bool Unfilter(const uint8_t* filtered, uint32_t width, uint32_t height, uint32_t channels, std::vector<uint8_t>& pixels)
        {
            const size_t stride = size_t(width) * channels;
            pixels.resize(stride * height);
            for (uint32_t y = 0; y < height; y++)
            {
                const uint8_t type = filtered[y * (stride + 1)];
                if (type > 3) return false;
                const uint8_t* in = filtered + y * (stride + 1) + 1;
                uint8_t* row = pixels.data() + y * stride;
                const uint8_t* up = y ? row - stride : nullptr;
                for (size_t i = 0; i < stride; i++)
                {
                    const int left = i >= channels ? row[i - channels] : 0, above = up ? up[i] : 0;
                    const int predicted = type == 0 ? 0 : type == 1 ? left : type == 2 ? above : (left + above) / 2;
                    row[i] = uint8_t(in[i] + predicted);
                }
            }
            return true;
        }
    }

    fs::path TextureCook::DefaultCacheDirectory(const fs::path& projectDirectory) { return projectDirectory / "Build" / "CookCache"; }

    bool TextureCook::IsCooked(const void* data, size_t size) { return size >= HeaderSize && std::memcmp(data, "ZTEX", 4) == 0; }

    bool TextureCook::IsCookedFile(const fs::path& file)
    {
        std::ifstream in(file, std::ios::binary);
        char magic[4] = {};
        return in.read(magic, 4) && std::memcmp(magic, "ZTEX", 4) == 0;
    }

    std::vector<uint8_t> TextureCook::Encode(const uint8_t* pixels, uint32_t width, uint32_t height, uint32_t channels, bool compress,
                                             const CookedTextureInfo& source)
    {
        std::vector<uint8_t> filtered = Filter(pixels, width, height, channels);
        std::vector<uint8_t> payload;
        bool compressed = false;
        if (compress)
        {
            int length = 0;
            if (unsigned char* z = stbi_zlib_compress(filtered.data(), int(filtered.size()), &length, 8))
            {
                if (length > 0 && size_t(length) < filtered.size())
                {
                    payload.assign(z, z + length);
                    compressed = true;
                }
                std::free(z);
            }
        }
        if (!compressed) payload = std::move(filtered);
        std::vector<uint8_t> out(HeaderSize + payload.size(), 0);
        std::memcpy(out.data(), "ZTEX", 4);
        Put<uint32_t>(out, 4, FormatVersion);
        Put<uint32_t>(out, 8, width);
        Put<uint32_t>(out, 12, height);
        out[16] = uint8_t(channels);
        out[17] = compressed ? FlagCompressed : 0;
        out[18] = uint8_t(std::min(source.BakedSkips, 15u));
        Put<uint32_t>(out, 20, source.SourceWidth ? source.SourceWidth : width);
        Put<uint32_t>(out, 24, source.SourceHeight ? source.SourceHeight : height);
        Put<uint64_t>(out, 32, source.SourceHash);
        Put<uint64_t>(out, 40, payload.size());
        std::copy(payload.begin(), payload.end(), out.begin() + HeaderSize);
        return out;
    }

    static bool ParseHeader(const uint8_t* data, size_t size, CookedTextureInfo& info, uint64_t& payload, std::string* error)
    {
        auto fail = [&](const char* why) {
            if (error) *error = why;
            return false;
        };
        if (!TextureCook::IsCooked(data, size)) return fail("not a cooked texture");
        if (Get<uint32_t>(data, 4) != TextureCook::FormatVersion) return fail("cooked by another engine version");
        info.Width = Get<uint32_t>(data, 8);
        info.Height = Get<uint32_t>(data, 12);
        info.Channels = data[16];
        info.Compressed = (data[17] & FlagCompressed) != 0;
        info.BakedSkips = data[18];
        info.SourceWidth = Get<uint32_t>(data, 20);
        info.SourceHeight = Get<uint32_t>(data, 24);
        info.SourceHash = Get<uint64_t>(data, 32);
        payload = Get<uint64_t>(data, 40);
        if (!info.Width || !info.Height || info.Width > 16384 || info.Height > 16384) return fail("bad size in the cooked header");
        if (info.Channels != 1 && info.Channels != 3 && info.Channels != 4) return fail("bad channel count in the cooked header");
        if (payload > size - HeaderSize) return fail("the cooked file is truncated");
        return true;
    }

    bool TextureCook::Decode(const uint8_t* data, size_t size, std::vector<uint8_t>& pixels, CookedTextureInfo& info, std::string* error)
    {
        uint64_t payload = 0;
        if (!ParseHeader(data, size, info, payload, error)) return false;
        const size_t raw = size_t(info.Height) * (size_t(info.Width) * info.Channels + 1);
        std::vector<uint8_t> filtered;
        const uint8_t* source = data + HeaderSize;
        if (info.Compressed)
        {
            filtered.resize(raw);
            const int written = stbi_zlib_decode_buffer(reinterpret_cast<char*>(filtered.data()), int(raw), reinterpret_cast<const char*>(source), int(payload));
            if (written != int(raw))
            {
                if (error) *error = "the cooked pixels are damaged";
                return false;
            }
            source = filtered.data();
        }
        else if (payload != raw)
        {
            if (error) *error = "the cooked file has the wrong size";
            return false;
        }
        if (!Unfilter(source, info.Width, info.Height, info.Channels, pixels))
        {
            if (error) *error = "the cooked pixels are damaged";
            return false;
        }
        return true;
    }

    bool TextureCook::DecodeFile(const fs::path& file, std::vector<uint8_t>& pixels, CookedTextureInfo& info, std::string* error)
    {
        const std::vector<uint8_t> bytes = FileSystem::ReadBinaryFile(file);
        if (bytes.empty())
        {
            if (error) *error = "file not found";
            return false;
        }
        return Decode(bytes.data(), bytes.size(), pixels, info, error);
    }

    bool TextureCook::ReadInfo(const fs::path& file, CookedTextureInfo& info)
    {
        std::ifstream in(file, std::ios::binary);
        uint8_t header[HeaderSize];
        if (!in.read(reinterpret_cast<char*>(header), HeaderSize)) return false;
        std::error_code ec;
        const uint64_t size = fs::file_size(file, ec);
        uint64_t payload = 0;
        return !ec && ParseHeader(header, size_t(std::min<uint64_t>(size, ~size_t(0))), info, payload, nullptr);
    }

    void TextureCook::Halve(std::vector<uint8_t>& pixels, uint32_t& width, uint32_t& height, uint32_t channels)
    {
        const uint32_t w = std::max(1u, width / 2), h = std::max(1u, height / 2);
        std::vector<uint8_t> out(size_t(w) * h * channels);
        for (uint32_t y = 0; y < h; y++)
            for (uint32_t x = 0; x < w; x++)
                for (uint32_t c = 0; c < channels; c++)
                {
                    auto at = [&](uint32_t sx, uint32_t sy) { return uint32_t(pixels[(size_t(std::min(sy, height - 1)) * width + std::min(sx, width - 1)) * channels + c]); };
                    out[(size_t(y) * w + x) * channels + c] = uint8_t((at(2 * x, 2 * y) + at(2 * x + 1, 2 * y) + at(2 * x, 2 * y + 1) + at(2 * x + 1, 2 * y + 1) + 2) / 4);
                }
        pixels = std::move(out);
        width = w;
        height = h;
    }

    uint64_t TextureCook::CacheKey(const std::vector<uint8_t>& sourceBytes, const TextureCookSettings& settings, bool resizable)
    {
        uint64_t hash = HashBytes(sourceBytes.data(), sourceBytes.size());
        const uint32_t values[6] = { FormatVersion, settings.Compress ? 1u : 0u, resizable ? settings.MaxTextureSize : 0u, resizable ? settings.QualitySkips : 0u,
                                     resizable ? 1u : 0u, 0x5a544558u };
        return HashBytes(values, sizeof(values), hash);
    }

    static TextureCook::Result FromCache(const fs::path& cacheDirectory, uint64_t key)
    {
        TextureCook::Result r;
        if (cacheDirectory.empty()) return r;
        const fs::path file = cacheDirectory / std::format("{:016x}.ztex", key);
        std::vector<uint8_t> bytes = FileSystem::ReadBinaryFile(file);
        uint64_t payload = 0;
        if (bytes.empty() || !ParseHeader(bytes.data(), bytes.size(), r.Info, payload, nullptr) || payload != bytes.size() - HeaderSize) return r;
        r.Bytes = std::move(bytes);
        r.Ok = r.CacheHit = true;
        return r;
    }

    static void ToCache(const fs::path& cacheDirectory, uint64_t key, const std::vector<uint8_t>& bytes)
    {
        if (cacheDirectory.empty()) return;
        std::error_code ec;
        fs::create_directories(cacheDirectory, ec);
        const fs::path file = cacheDirectory / std::format("{:016x}.ztex", key);
        const fs::path temporary = file.string() + ".tmp";
        if (FileSystem::WriteBinaryFile(temporary, bytes.data(), bytes.size())) fs::rename(temporary, file, ec);
    }

    static TextureCook::Result CookDecoded(std::vector<uint8_t> pixels, uint32_t width, uint32_t height, uint32_t channels, const TextureCookSettings& settings,
                                           bool resizable, uint64_t sourceHash)
    {
        TextureCook::Result r;
        CookedTextureInfo info;
        info.SourceWidth = width;
        info.SourceHeight = height;
        info.SourceHash = sourceHash;
        if (resizable)
        {
            // Quality levels that all skip mips: those levels never show, so they are baked out (compensated at runtime).
            for (uint32_t i = 0; i < settings.QualitySkips && (width > 1 || height > 1); i++)
            {
                TextureCook::Halve(pixels, width, height, channels);
                info.BakedSkips++;
                r.Resized = true;
            }
            while (settings.MaxTextureSize && std::max(width, height) > settings.MaxTextureSize && (width > 1 || height > 1))
            {
                TextureCook::Halve(pixels, width, height, channels);
                r.Resized = true;
            }
        }
        r.Bytes = TextureCook::Encode(pixels.data(), width, height, channels, settings.Compress, info);
        info.Width = width;
        info.Height = height;
        info.Channels = channels;
        info.Compressed = r.Bytes.size() > 17 && (r.Bytes[17] & 1);
        r.Info = info;
        r.Ok = true;
        return r;
    }

    TextureCook::Result TextureCook::CookFile(const fs::path& source, const TextureCookSettings& settings, bool resizable, const fs::path& cacheDirectory)
    {
        Result r;
        const std::vector<uint8_t> bytes = FileSystem::ReadBinaryFile(source);
        if (bytes.empty())
        {
            r.Error = "unreadable";
            return r;
        }
        const uint64_t key = CacheKey(bytes, settings, resizable);
        if (Result cached = FromCache(cacheDirectory, key); cached.Ok)
        {
            cached.SourceBytes = bytes.size();
            cached.Resized = cached.Info.Width != cached.Info.SourceWidth || cached.Info.Height != cached.Info.SourceHeight;
            return cached;
        }
        if (IsCooked(bytes.data(), bytes.size()))
        {
            r.Error = "already cooked";
            return r;
        }
        int width = 0, height = 0, channels = 0;
        stbi_set_flip_vertically_on_load_thread(1); // rows bottom first, as the texture loader keeps them
        stbi_uc* decoded = stbi_load_from_memory(bytes.data(), int(bytes.size()), &width, &height, &channels, 0);
        if (decoded && channels == 2)
        {
            stbi_image_free(decoded);
            decoded = stbi_load_from_memory(bytes.data(), int(bytes.size()), &width, &height, &channels, 4);
            channels = 4;
        }
        stbi_set_flip_vertically_on_load_thread(0);
        if (!decoded)
        {
            r.Error = stbi_failure_reason() ? stbi_failure_reason() : "undecodable";
            return r;
        }
        std::vector<uint8_t> pixels(decoded, decoded + size_t(width) * height * channels);
        stbi_image_free(decoded);
        r = CookDecoded(std::move(pixels), uint32_t(width), uint32_t(height), uint32_t(channels), settings, resizable, HashBytes(bytes.data(), bytes.size()));
        r.SourceBytes = bytes.size();
        ToCache(cacheDirectory, key, r.Bytes);
        return r;
    }

    TextureCook::Result TextureCook::CookPixels(const std::vector<uint8_t>& rgba, uint32_t width, uint32_t height, const TextureCookSettings& settings,
                                                const fs::path& cacheDirectory)
    {
        const uint64_t key = CacheKey(rgba, settings, false) ^ (uint64_t(width) << 32 | height);
        if (Result cached = FromCache(cacheDirectory, key); cached.Ok) return cached;
        std::vector<uint8_t> flipped(rgba.size());
        const size_t stride = size_t(width) * 4;
        for (uint32_t y = 0; y < height; y++) std::copy_n(rgba.data() + (height - 1 - y) * stride, stride, flipped.data() + y * stride);
        Result r = CookDecoded(std::move(flipped), width, height, 4, settings, false, HashBytes(rgba.data(), rgba.size()));
        ToCache(cacheDirectory, key, r.Bytes);
        return r;
    }

    std::string TextureCook::ManifestToJson(const TextureCookSettings& settings, const std::vector<CookedAsset>& assets, const std::string& atlasTable)
    {
        nlohmann::ordered_json j;
        j["Version"] = FormatVersion;
        j["Settings"] = { { "Compress", settings.Compress }, { "MaxTextureSize", settings.MaxTextureSize }, { "QualitySkips", settings.QualitySkips },
                          { "Always", settings.Always } };
        auto& list = j["Textures"] = nlohmann::ordered_json::array();
        for (const CookedAsset& a : assets)
            list.push_back({ { "Path", a.Path }, { "Action", a.Action }, { "SourceBytes", a.SourceBytes }, { "CookedBytes", a.CookedBytes },
                             { "Size", { a.Width, a.Height } }, { "SourceSize", { a.SourceWidth, a.SourceHeight } }, { "CacheHit", a.CacheHit }, { "Note", a.Note } });
        j["Atlases"] = atlasTable.empty() ? nlohmann::ordered_json::array() : nlohmann::ordered_json::parse(atlasTable, nullptr, false);
        return j.dump(2) + "\n";
    }

    bool TextureCook::ManifestFromJson(const std::string& text, std::vector<CookedAsset>& assets)
    {
        const auto j = nlohmann::json::parse(text, nullptr, false);
        if (!j.is_object() || !j.contains("Textures") || !j["Textures"].is_array()) return false;
        assets.clear();
        for (const auto& e : j["Textures"])
        {
            CookedAsset a;
            a.Path = e.value("Path", std::string());
            a.Action = e.value("Action", std::string());
            a.SourceBytes = e.value("SourceBytes", uint64_t(0));
            a.CookedBytes = e.value("CookedBytes", uint64_t(0));
            if (e.contains("Size") && e["Size"].size() == 2)
            {
                a.Width = e["Size"][0];
                a.Height = e["Size"][1];
            }
            if (e.contains("SourceSize") && e["SourceSize"].size() == 2)
            {
                a.SourceWidth = e["SourceSize"][0];
                a.SourceHeight = e["SourceSize"][1];
            }
            a.CacheHit = e.value("CacheHit", false);
            a.Note = e.value("Note", std::string());
            assets.push_back(std::move(a));
        }
        return true;
    }

}
