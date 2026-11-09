#include "Zamin/Renderer/Font.h"
#include "Zamin/Utils/StringUtils.h"
#include "Zamin/Asset/AssetImportLog.h"
#include "Zamin/Renderer/Renderer.h"
#include "Zamin/Utils/FileSystem.h"
#include "Zamin/Asset/AssetImporter.h"

#include <stb_rect_pack.h>
#include <stb_truetype.h>

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <format>
#include <limits>
#include <vector>

namespace Zamin {

    uint32_t DecodeUTF8(std::string_view s, size_t& i)
    {
        // The shared decoder: broken UTF-8 gives U+FFFD (drawn as '?') one byte at a time.
        return StringUtils::Utf8Decode(s, i);
    }

    const Glyph* FontAtlas::Find(uint32_t codepoint) const
    {
        if (codepoint < m_Lookup.size())
        {
            const uint16_t index = m_Lookup[codepoint] ? m_Lookup[codepoint] : m_Fallback;
            return index ? &m_Dense[index - 1u] : nullptr;
        }
        if (m_Lookup.empty()) // (made by hand, without BuildLookup)
        {
            auto it = Glyphs.find(codepoint);
            if (it != Glyphs.end())
                return &it->second;
            it = Glyphs.find('?');
            return it != Glyphs.end() ? &it->second : nullptr;
        }
        auto it = Glyphs.find(codepoint);
        if (it != Glyphs.end())
            return &it->second;
        return m_Fallback ? &m_Dense[m_Fallback - 1u] : nullptr;
    }

    void FontAtlas::BuildLookup()
    {
        m_Dense.clear();
        m_Lookup.assign(LookupSize, 0);
        for (const auto& [codepoint, glyph] : Glyphs)
            if (codepoint < LookupSize && m_Dense.size() < 0xFFFF)
            {
                m_Dense.push_back(glyph);
                m_Lookup[codepoint] = (uint16_t)m_Dense.size();
            }
        m_Fallback = m_Lookup['?'];
    }

    // ------------------------------------------------------------------ atlas cache
    namespace {
        struct AtlasCacheState
        {
            bool Initialized = false;
            std::filesystem::path Directory;
            FontAtlasCache::Stats Stats;
        };
        AtlasCacheState& AtlasCache()
        {
            static AtlasCacheState s_State;
            if (!s_State.Initialized)
            {
                s_State.Initialized = true;
                const char* env = std::getenv("ZAMIN_FONT_CACHE");
                if (!(env && std::strcmp(env, "0") == 0))
                    s_State.Directory = FileSystem::GetUserCacheDirectory() / "FontAtlases";
            }
            return s_State;
        }

        constexpr uint32_t s_AtlasMagic = 0x4C54415Au; // "ZATL"
        constexpr uint32_t s_AtlasFormat = 1;          // bump when the bake or this file changes

        uint64_t Fnv1a(const void* data, size_t size, uint64_t hash = 1469598103934665603ull)
        {
            const uint8_t* bytes = (const uint8_t*)data;
            for (size_t i = 0; i < size; i++)
            {
                hash ^= bytes[i];
                hash *= 1099511628211ull;
            }
            return hash;
        }

        template<typename T>
        void Put(std::vector<uint8_t>& out, const T& value)
        {
            const size_t at = out.size();
            out.resize(at + sizeof(T));
            std::memcpy(out.data() + at, &value, sizeof(T));
        }

        template<typename T>
        bool Take(const std::vector<uint8_t>& in, size_t& at, T& value)
        {
            if (at + sizeof(T) > in.size())
                return false;
            std::memcpy(&value, in.data() + at, sizeof(T));
            at += sizeof(T);
            return true;
        }
    }

    namespace FontAtlasCache {

        void SetDirectory(const std::filesystem::path& directory)
        {
            AtlasCache().Directory = directory;
        }

        const std::filesystem::path& GetDirectory() { return AtlasCache().Directory; }

        uint64_t MakeKey(const std::vector<uint8_t>& ttf, float pixelHeight, int oversampleH, int oversampleV, int fixedSize, bool mips)
        {
            uint64_t hash = Fnv1a(ttf.data(), ttf.size());
            const int32_t params[] = { (int32_t)std::lround(pixelHeight * 1000.0f), oversampleH, oversampleV, fixedSize, mips ? 1 : 0,
                                       (int32_t)s_AtlasFormat };
            return Fnv1a(params, sizeof(params), hash);
        }

        static std::filesystem::path FileOf(uint64_t key)
        {
            return GetDirectory() / std::format("{:016x}.zatlas", key);
        }

        bool Load(uint64_t key, FontAtlasBake& out)
        {
            AtlasCacheState& cache = AtlasCache();
            if (cache.Directory.empty())
                return false;
            const auto start = std::chrono::steady_clock::now();
            const std::vector<uint8_t> file = FileSystem::ReadBinaryFile(FileOf(key));
            size_t at = 0;
            uint32_t magic = 0, format = 0, glyphs = 0;
            uint64_t storedKey = 0;
            FontAtlasBake bake;
            if (!Take(file, at, magic) || magic != s_AtlasMagic || !Take(file, at, format) || format != s_AtlasFormat ||
                !Take(file, at, storedKey) || storedKey != key || !Take(file, at, bake.Width) || !Take(file, at, bake.Height) ||
                !Take(file, at, glyphs) || bake.Width == 0 || bake.Height == 0 || bake.Width > 8192 || bake.Height > 8192)
            {
                cache.Stats.Misses++;
                return false;
            }
            for (uint32_t i = 0; i < glyphs; i++)
            {
                uint32_t codepoint = 0;
                Glyph g{};
                if (!Take(file, at, codepoint) || !Take(file, at, g))
                {
                    cache.Stats.Misses++;
                    return false;
                }
                bake.Glyphs[codepoint] = g;
            }
            // The pixels: runs of zeros (most of an atlas) and literal bytes after each other.
            const size_t pixels = (size_t)bake.Width * bake.Height;
            bake.Pixels.reserve(pixels);
            while (bake.Pixels.size() < pixels)
            {
                uint32_t zeros = 0, literals = 0;
                if (!Take(file, at, zeros) || !Take(file, at, literals) || bake.Pixels.size() + zeros + literals > pixels ||
                    at + literals > file.size())
                {
                    cache.Stats.Misses++;
                    return false;
                }
                bake.Pixels.insert(bake.Pixels.end(), zeros, 0);
                bake.Pixels.insert(bake.Pixels.end(), file.begin() + (std::ptrdiff_t)at, file.begin() + (std::ptrdiff_t)(at + literals));
                at += literals;
            }
            out = std::move(bake);
            cache.Stats.Hits++;
            cache.Stats.LoadMilliseconds += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            return true;
        }

        bool Store(uint64_t key, const FontAtlasBake& bake)
        {
            AtlasCacheState& cache = AtlasCache();
            if (cache.Directory.empty() || bake.Pixels.size() != (size_t)bake.Width * bake.Height)
                return false;
            std::vector<uint8_t> out;
            out.reserve(bake.Pixels.size() / 4 + bake.Glyphs.size() * (sizeof(Glyph) + 4) + 64);
            Put(out, s_AtlasMagic);
            Put(out, s_AtlasFormat);
            Put(out, key);
            Put(out, bake.Width);
            Put(out, bake.Height);
            Put(out, (uint32_t)bake.Glyphs.size());
            // In codepoint order: the same bake gives the same file.
            std::vector<uint32_t> codepoints;
            codepoints.reserve(bake.Glyphs.size());
            for (const auto& [codepoint, glyph] : bake.Glyphs)
                codepoints.push_back(codepoint);
            std::sort(codepoints.begin(), codepoints.end());
            for (uint32_t codepoint : codepoints)
            {
                Put(out, codepoint);
                Put(out, bake.Glyphs.at(codepoint));
            }
            size_t i = 0;
            const size_t n = bake.Pixels.size();
            while (i < n)
            {
                size_t zeros = 0;
                while (i + zeros < n && bake.Pixels[i + zeros] == 0)
                    zeros++;
                size_t literals = 0;
                // Literals until a run of 8 zeros (shorter runs stay in the literals).
                while (i + zeros + literals < n)
                {
                    size_t run = 0;
                    while (run < 8 && i + zeros + literals + run < n && bake.Pixels[i + zeros + literals + run] == 0)
                        run++;
                    if (run >= 8)
                        break;
                    literals += run > 0 ? run : 1;
                }
                Put(out, (uint32_t)zeros);
                Put(out, (uint32_t)literals);
                out.insert(out.end(), bake.Pixels.begin() + (std::ptrdiff_t)(i + zeros), bake.Pixels.begin() + (std::ptrdiff_t)(i + zeros + literals));
                i += zeros + literals;
            }
            std::error_code ec;
            std::filesystem::create_directories(cache.Directory, ec);
            const std::filesystem::path file = FileOf(key);
            const std::filesystem::path temp = file.string() + std::format(".{}.tmp", (uint64_t)std::chrono::steady_clock::now().time_since_epoch().count());
            if (!FileSystem::WriteBinaryFile(temp, out.data(), out.size()))
                return false;
            std::filesystem::rename(temp, file, ec);
            if (ec)
            {
                std::filesystem::remove(temp, ec);
                return false;
            }
            cache.Stats.Stores++;
            return true;
        }

        const Stats& GetStats() { return AtlasCache().Stats; }
        void ResetStats() { AtlasCache().Stats = {}; }

    }

    namespace {

        struct CodepointRange { int First, Count; };
        constexpr CodepointRange s_Ranges[] = {
            { 0x0020, 0x7F - 0x20 },    // Basic Latin
            { 0x00A0, 0x100 - 0xA0 },   // Latin-1 Supplement
            { 0x0100, 0x180 - 0x100 },  // Latin Extended-A
            { 0x02BB, 2 },              // ʻ ʼ (o'zbek lotin)
            { 0x0400, 0x500 - 0x400 },  // Cyrillic
            { 0x2018, 0x2027 - 0x2018 },// quotes, bullets, ellipsis
        };

        bool s_SizedAtlases = true;

        // Seconds on a steady clock (when an atlas was last used).
        double Now()
        {
            using namespace std::chrono;
            return duration<double>(steady_clock::now().time_since_epoch()).count();
        }

        // Rasterizes every range at `pixelHeight` into `out` (its glyphs and texture; a texture of
        // the same size is refilled in place). `fixedSize` > 0: a square atlas that big, else the
        // smallest power-of-two one they fit in (at most 4096). False when the font cannot be read
        // or the glyphs do not fit.
        bool RasterizeAtlas(const std::vector<uint8_t>& ttf, float pixelHeight, int oversampleH, int oversampleV, int fixedSize, bool mips,
                            FontAtlasBake& out)
        {
            stbtt_fontinfo info;
            int offset = stbtt_GetFontOffsetForIndex(ttf.data(), 0);
            if (offset < 0 || !stbtt_InitFont(&info, ttf.data(), offset))
                return false;
            // The main atlas keeps the room it always had around its glyphs (mipmaps blend neighbours).
            const int padding = mips ? 2 : 1;
            int width = fixedSize, height = fixedSize;
            if (fixedSize <= 0)
            {
                // The glyphs' boxes as the packer sizes them, plus some room for the packing.
                const float scale = stbtt_ScaleForPixelHeight(&info, pixelHeight);
                double area = 0.0;
                for (const auto& r : s_Ranges)
                    for (int c = 0; c < r.Count; c++)
                    {
                        int x0, y0, x1, y1;
                        stbtt_GetCodepointBitmapBox(&info, r.First + c, scale * oversampleH, scale * oversampleV, &x0, &y0, &x1, &y1);
                        area += (double)(x1 - x0 + padding + oversampleH - 1) * (double)(y1 - y0 + padding + oversampleV - 1);
                    }
                area *= 1.2;
                width = 64;
                while ((double)width * width < area && width < 4096)
                    width *= 2;
                height = (double)width * width * 0.5 >= area ? width / 2 : width;
            }

            std::vector<stbtt_packedchar> packed;
            std::vector<uint8_t> bitmap;
            for (;;)
            {
                packed.assign([] { size_t n = 0; for (const auto& r : s_Ranges) n += (size_t)r.Count; return n; }(), stbtt_packedchar{});
                std::vector<stbtt_pack_range> packRanges;
                size_t first = 0;
                for (const auto& r : s_Ranges)
                {
                    stbtt_pack_range pr{};
                    pr.font_size = pixelHeight;
                    pr.first_unicode_codepoint_in_range = r.First;
                    pr.num_chars = r.Count;
                    pr.chardata_for_range = packed.data() + first;
                    packRanges.push_back(pr);
                    first += (size_t)r.Count;
                }
                bitmap.assign((size_t)width * height, 0);
                stbtt_pack_context pc;
                if (!stbtt_PackBegin(&pc, bitmap.data(), width, height, 0, padding, nullptr))
                    return false;
                stbtt_PackSetOversampling(&pc, (unsigned)oversampleH, (unsigned)oversampleV);
                const int ok = stbtt_PackFontRanges(&pc, ttf.data(), 0, packRanges.data(), (int)packRanges.size());
                stbtt_PackEnd(&pc);
                if (ok || fixedSize > 0)
                    break; // a fixed atlas keeps what fitted (as it always did)
                if (width >= 4096 && height >= 4096)
                    return false;
                if (height < width)
                    height *= 2;
                else
                    width *= 2;
            }

            std::unordered_map<uint32_t, Glyph> glyphs;
            size_t index = 0;
            for (const auto& r : s_Ranges)
                for (int c = 0; c < r.Count; c++)
                {
                    const stbtt_packedchar& pcd = packed[index++];
                    Glyph g;
                    // stb: y grows downwards; we flip so +Y is up.
                    g.X0 = pcd.xoff / pixelHeight;
                    g.X1 = pcd.xoff2 / pixelHeight;
                    g.Y0 = -pcd.yoff2 / pixelHeight;
                    g.Y1 = -pcd.yoff / pixelHeight;
                    g.U0 = pcd.x0 / (float)width;
                    g.U1 = pcd.x1 / (float)width;
                    // The bitmap is flipped below (v = 0 is its bottom row, OpenGL's convention).
                    g.V0 = 1.0f - pcd.y1 / (float)height;
                    g.V1 = 1.0f - pcd.y0 / (float)height;
                    g.Advance = pcd.xadvance / pixelHeight;
                    glyphs[(uint32_t)(r.First + c)] = g;
                }

            out.Width = (uint32_t)width;
            out.Height = (uint32_t)height;
            out.Pixels.resize(bitmap.size());
            for (int y = 0; y < height; y++)
                std::memcpy(&out.Pixels[(size_t)y * width], &bitmap[(size_t)(height - 1 - y) * width], (size_t)width);
            out.Glyphs = std::move(glyphs);
            return true;
        }

        // The atlas of `ttf` at `pixelHeight` into `out`: read from FontAtlasCache when it has it, else
        // rasterized (and stored there). `textures`: its texture made / refilled (one of the same size in
        // place). False when the font cannot be read or the glyphs do not fit.
        bool BakeAtlas(const std::vector<uint8_t>& ttf, float pixelHeight, int oversampleH, int oversampleV, int fixedSize, bool mips,
                       FontAtlas& out, bool textures = true)
        {
            FontAtlasBake bake;
            const uint64_t key = FontAtlasCache::MakeKey(ttf, pixelHeight, oversampleH, oversampleV, fixedSize, mips);
            if (!FontAtlasCache::Load(key, bake))
            {
                const auto start = std::chrono::steady_clock::now();
                if (!RasterizeAtlas(ttf, pixelHeight, oversampleH, oversampleV, fixedSize, mips, bake))
                    return false;
                AtlasCache().Stats.BakeMilliseconds += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
                FontAtlasCache::Store(key, bake);
            }
            out.PixelSize = pixelHeight;
            out.Glyphs = std::move(bake.Glyphs);
            out.BuildLookup();
            if (!textures)
                return true;
            if (!out.Texture || out.Texture->GetWidth() != bake.Width || out.Texture->GetHeight() != bake.Height)
            {
                TextureSpecification spec;
                spec.Width = bake.Width;
                spec.Height = bake.Height;
                spec.Format = ImageFormat::R8;
                spec.GenerateMips = mips;
                spec.Wrap = TextureWrap::ClampToEdge;
                out.Texture = Texture2D::Create(spec);
            }
            out.Texture->SetData(bake.Pixels.data(), (uint32_t)bake.Pixels.size()); // the same texture object when reloading
            return true;
        }

    }

    // ------------------------------------------------------------------ kerning pairs (GPOS)
    // stb_truetype reads the old 'kern' table and simple GPOS pair lookups only; most fonts today (Inter
    // among them) keep their pairs in GPOS 'kern' lookups behind Extension subtables, with other value
    // formats. This reads those: PairPos format 1 (glyph pairs) and 2 (class pairs), directly or through
    // Extension (type 9) lookups, of the lookups the 'kern' feature uses - the horizontal advance of the
    // first glyph's value record.
    namespace {
        struct OpenTypeReader
        {
            const uint8_t* Data = nullptr;
            size_t Size = 0;

            bool Has(size_t at, size_t bytes) const { return at <= Size && bytes <= Size - at; }
            uint16_t U16(size_t at) const { return Has(at, 2) ? (uint16_t)(Data[at] << 8 | Data[at + 1]) : 0; }
            int16_t S16(size_t at) const { return (int16_t)U16(at); }
            uint32_t U32(size_t at) const { return Has(at, 4) ? (uint32_t)U16(at) << 16 | U16(at + 2) : 0; }

            // The glyph's index in a Coverage table, -1 when it is not covered.
            int CoverageIndex(size_t table, int glyph) const
            {
                const uint16_t format = U16(table);
                if (format == 1)
                {
                    int lo = 0, hi = (int)U16(table + 2) - 1;
                    while (lo <= hi)
                    {
                        const int mid = (lo + hi) / 2;
                        const int g = U16(table + 4 + 2 * (size_t)mid);
                        if (glyph < g) hi = mid - 1;
                        else if (glyph > g) lo = mid + 1;
                        else return mid;
                    }
                }
                else if (format == 2)
                {
                    int lo = 0, hi = (int)U16(table + 2) - 1;
                    while (lo <= hi)
                    {
                        const int mid = (lo + hi) / 2;
                        const size_t range = table + 4 + 6 * (size_t)mid;
                        if (glyph < U16(range)) hi = mid - 1;
                        else if (glyph > U16(range + 2)) lo = mid + 1;
                        else return U16(range + 4) + glyph - U16(range);
                    }
                }
                return -1;
            }

            // The glyph's class in a ClassDef table (0 for every glyph it does not list).
            int GlyphClass(size_t table, int glyph) const
            {
                const uint16_t format = U16(table);
                if (format == 1)
                {
                    const int start = U16(table + 2), count = U16(table + 4);
                    return glyph >= start && glyph < start + count ? U16(table + 6 + 2 * (size_t)(glyph - start)) : 0;
                }
                if (format == 2)
                {
                    int lo = 0, hi = (int)U16(table + 2) - 1;
                    while (lo <= hi)
                    {
                        const int mid = (lo + hi) / 2;
                        const size_t range = table + 4 + 6 * (size_t)mid;
                        if (glyph < U16(range)) hi = mid - 1;
                        else if (glyph > U16(range + 2)) lo = mid + 1;
                        else return U16(range + 4);
                    }
                }
                return 0;
            }

            static size_t ValueRecordSize(uint16_t format) { return 2 * (size_t)std::popcount((unsigned)(format & 0xFF)); }
            // XAdvance of a value record (0 when the format has none).
            int XAdvance(size_t record, uint16_t format) const
            {
                if (!(format & 0x0004))
                    return 0;
                return S16(record + 2 * (size_t)std::popcount((unsigned)(format & 0x0003)));
            }

            // One PairPos subtable: true when it applies to the pair (`value` gets its XAdvance).
            bool PairPos(size_t table, int left, int right, int& value) const
            {
                const uint16_t format = U16(table);
                const int coverage = CoverageIndex(table + U16(table + 2), left);
                if (coverage < 0)
                    return false;
                const uint16_t format1 = U16(table + 4), format2 = U16(table + 6);
                const size_t size1 = ValueRecordSize(format1), size2 = ValueRecordSize(format2);
                if (format == 1)
                {
                    if (coverage >= (int)U16(table + 8))
                        return false;
                    const size_t set = table + U16(table + 10 + 2 * (size_t)coverage);
                    const size_t stride = 2 + size1 + size2;
                    int lo = 0, hi = (int)U16(set) - 1;
                    while (lo <= hi)
                    {
                        const int mid = (lo + hi) / 2;
                        const size_t pair = set + 2 + stride * (size_t)mid;
                        const int second = U16(pair);
                        if (right < second) hi = mid - 1;
                        else if (right > second) lo = mid + 1;
                        else
                        {
                            value = XAdvance(pair + 2, format1);
                            return true;
                        }
                    }
                    return false; // the next subtable may have the pair
                }
                if (format == 2)
                {
                    const int class1 = GlyphClass(table + U16(table + 8), left);
                    const int class2 = GlyphClass(table + U16(table + 10), right);
                    const int count1 = U16(table + 12), count2 = U16(table + 14);
                    if (class1 >= count1 || class2 >= count2)
                        return false;
                    const size_t record = table + 16 + ((size_t)class1 * (size_t)count2 + (size_t)class2) * (size1 + size2);
                    value = XAdvance(record, format1);
                    return true;
                }
                return false;
            }
        };
    }

    // The font's tables for its kerning pairs, and the pairs asked for so far (em).
    struct Font::KerningData
    {
        stbtt_fontinfo Info{};
        float Scale = 0.0f;                // font units -> em
        OpenTypeReader Reader;             // the whole file (GPOS offsets are relative to it)
        size_t Gpos = 0;                   // the GPOS table (0: none)
        std::vector<uint16_t> Lookups;     // GPOS lookups of the 'kern' feature
        std::vector<float> Ascii;          // printable ASCII pairs (95 x 95), NaN: not asked yet
        std::unordered_map<uint64_t, float> Pairs;

        void FindKernLookups()
        {
            if (!Gpos || Reader.U16(Gpos) != 1)
                return;
            const size_t features = Gpos + Reader.U16(Gpos + 6);
            const uint16_t featureCount = Reader.U16(features);
            for (uint16_t i = 0; i < featureCount; i++)
            {
                const size_t record = features + 2 + 6 * (size_t)i;
                if (!Reader.Has(record, 6) || std::memcmp(Reader.Data + record, "kern", 4) != 0)
                    continue;
                const size_t feature = features + Reader.U16(record + 4);
                const uint16_t count = Reader.U16(feature + 2);
                for (uint16_t k = 0; k < count; k++)
                {
                    const uint16_t lookup = Reader.U16(feature + 4 + 2 * (size_t)k);
                    if (std::find(Lookups.begin(), Lookups.end(), lookup) == Lookups.end())
                        Lookups.push_back(lookup);
                }
            }
            std::sort(Lookups.begin(), Lookups.end()); // applied in the lookup list's order
        }

        // The pair's kerning in font units from the 'kern' lookups; false when the font has none of them.
        bool GposKerning(int left, int right, int& value) const
        {
            if (Lookups.empty())
                return false;
            value = 0;
            const size_t list = Gpos + Reader.U16(Gpos + 8);
            const uint16_t lookupCount = Reader.U16(list);
            for (uint16_t index : Lookups)
            {
                if (index >= lookupCount)
                    continue;
                const size_t lookup = list + Reader.U16(list + 2 + 2 * (size_t)index);
                const uint16_t type = Reader.U16(lookup);
                const uint16_t subtables = Reader.U16(lookup + 4);
                for (uint16_t k = 0; k < subtables; k++)
                {
                    size_t table = lookup + Reader.U16(lookup + 6 + 2 * (size_t)k);
                    uint16_t tableType = type;
                    if (type == 9) // Extension: the real subtable further away
                    {
                        tableType = Reader.U16(table + 2);
                        table += Reader.U32(table + 4);
                    }
                    int v = 0;
                    if (tableType == 2 && Reader.PairPos(table, left, right, v))
                    {
                        value += v;
                        break; // one subtable per lookup applies
                    }
                }
            }
            return true;
        }
    };

    Font::Font(const std::filesystem::path& path, float pixelHeight) : Font(path, pixelHeight, true) {}

    Font::Font(const std::filesystem::path& path, float pixelHeight, bool textures) : m_Path(path), m_PixelHeight(pixelHeight), m_Textures(textures)
    {
        Build(false);
    }

    Font::~Font() = default;

    Ref<Font> Font::CreateHeadless(const std::filesystem::path& path, float pixelHeight)
    {
        Ref<Font> font(new Font(path, pixelHeight, false));
        return font->IsLoaded() ? font : nullptr;
    }

    void Font::SetKerningEnabled(bool enabled)
    {
        if (m_Kerning == enabled)
            return;
        m_Kerning = enabled;
        m_Revision++;
    }

    float Font::GetKerning(uint32_t left, uint32_t right) const
    {
        if (!m_Kerning || !m_KerningData)
            return 0.0f;
        KerningData& k = *m_KerningData;
        const bool ascii = left >= 32 && left < 127 && right >= 32 && right < 127;
        float* slot = nullptr;
        if (ascii)
        {
            if (k.Ascii.empty())
                k.Ascii.assign(95 * 95, std::numeric_limits<float>::quiet_NaN());
            slot = &k.Ascii[(size_t)(left - 32) * 95 + (right - 32)];
            if (!std::isnan(*slot))
                return *slot;
        }
        else if (auto it = k.Pairs.find((uint64_t)left << 32 | right); it != k.Pairs.end())
            return it->second;
        const int g1 = stbtt_FindGlyphIndex(&k.Info, (int)left), g2 = stbtt_FindGlyphIndex(&k.Info, (int)right);
        int units = 0;
        if (g1 > 0 && g2 > 0 && !k.GposKerning(g1, g2, units))
            units = stbtt_GetGlyphKernAdvance(&k.Info, g1, g2); // the old 'kern' table (and simple GPOS)
        const float value = (float)units * k.Scale;
        if (slot)
            *slot = value;
        else
            k.Pairs.emplace((uint64_t)left << 32 | right, value);
        return value;
    }

    bool Font::Reload()
    {
        return Build(true);
    }

    bool Font::Build(bool reload)
    {
        const std::filesystem::path& path = m_Path;
        const float pixelHeight = m_PixelHeight;
        std::vector<uint8_t> ttf = FileSystem::ReadBinaryFile(path);
        if (ttf.empty())
        {
            std::string reason = AssetImportLog::CheckReadable(path);
            AssetImportLog::Report(path, reason.empty() ? "fayl o'qilmadi" : reason, reload);
            return false;
        }

        stbtt_fontinfo info;
        int offset = stbtt_GetFontOffsetForIndex(ttf.data(), 0);
        if (offset < 0 || !stbtt_InitFont(&info, ttf.data(), offset))
        {
            AssetImportLog::Report(path, "TrueType / OpenType shrift emas yoki fayl buzilgan", reload);
            return false;
        }

        // The main atlas: 2048 px, 2 x 2 oversampling, mipmapped (also for tiny and far text).
        FontAtlas main;
        main.Texture = m_Main.Texture; // refilled in place when reloading
        if (!BakeAtlas(ttf, pixelHeight, 2, 2, 2048, true, main, m_Textures))
        {
            AssetImportLog::Report(path, "TrueType / OpenType shrift emas yoki fayl buzilgan", reload);
            return false;
        }

        int ascent, descent, lineGap;
        stbtt_GetFontVMetrics(&info, &ascent, &descent, &lineGap);
        float scale = stbtt_ScaleForPixelHeight(&info, pixelHeight);
        m_Main = std::move(main);
        m_Ascent = ascent * scale / pixelHeight;
        m_Descent = descent * scale / pixelHeight;
        m_LineHeight = (ascent - descent + lineGap) * scale / pixelHeight;
        m_Data = std::move(ttf);
        m_Sized.clear(); // baked again from the new file when drawn
        // The kerning pairs come from the file in m_Data (stb_truetype reads them where they are).
        m_KerningData = std::make_unique<KerningData>();
        if (stbtt_InitFont(&m_KerningData->Info, m_Data.data(), stbtt_GetFontOffsetForIndex(m_Data.data(), 0)))
        {
            m_KerningData->Scale = stbtt_ScaleForPixelHeight(&m_KerningData->Info, pixelHeight) / pixelHeight;
            m_KerningData->Reader = { m_Data.data(), m_Data.size() };
            m_KerningData->Gpos = (size_t)m_KerningData->Info.gpos;
            m_KerningData->FindKernLookups();
        }
        else
            m_KerningData.reset();
        m_Loaded = true;
        m_Revision++;
        AssetImportLog::Clear(path);
        return true;
    }

    const Glyph* Font::GetGlyph(uint32_t codepoint) const
    {
        return m_Main.Find(codepoint);
    }

    void Font::SetSizedAtlasesEnabled(bool enabled) { s_SizedAtlases = enabled; }
    bool Font::AreSizedAtlasesEnabled() { return s_SizedAtlases; }

    float Font::GetAtlasPixelSize(float pixelSize)
    {
        // Whole pixels up to 24 px, then coarser: a size is drawn from an atlas at most ~1/6
        // bigger (slightly minified, never blurred by magnification).
        static constexpr float s_Ladder[] = { 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 26, 28, 30, 32,
                                              34, 36, 40, 44, 48, 52, 56, 60, 64, 72, 80, 88, 96, 112, 128 };
        static_assert(s_Ladder[0] == MinAtlasPixelSize && s_Ladder[std::size(s_Ladder) - 1] == MaxAtlasPixelSize);
        if (!s_SizedAtlases || !(pixelSize >= MinAtlasPixelSize - 0.5f) || !std::isfinite(pixelSize))
            return 0.0f;
        const float wanted = pixelSize * 0.98f; // 24.3 px: the 24 px atlas, not the 26 px one
        for (float size : s_Ladder)
            if (size >= wanted)
                return size;
        return MaxAtlasPixelSize;
    }

    const FontAtlas& Font::GetAtlas(float pixelSize)
    {
        const float size = GetAtlasPixelSize(pixelSize);
        if (size <= 0.0f || m_Data.empty() || !m_Main.Texture || !m_Textures)
            return m_Main;
        const double now = Now();
        for (FontAtlas& atlas : m_Sized)
            if (atlas.PixelSize == size)
            {
                atlas.LastUse = (uint64_t)(now * 1000.0);
                return atlas.Texture ? atlas : m_Main; // no texture: it could not be baked
            }
        // A new one. Over the limit the least recently used one goes - if it has not been drawn for
        // a while (a busy UI with many sizes grows the set instead of baking atlases every frame,
        // up to a hard limit; past it the main atlas draws).
        if (m_Sized.size() >= MaxSizedAtlases)
        {
            auto oldest = std::min_element(m_Sized.begin(), m_Sized.end(), [](const FontAtlas& a, const FontAtlas& b) { return a.LastUse < b.LastUse; });
            const bool stale = (double)oldest->LastUse / 1000.0 < now - 2.0;
            if (stale)
                m_Sized.erase(oldest);
            else if (m_Sized.size() >= MaxSizedAtlases * 4)
                return m_Main;
        }
        // Small text: 2 x 2 oversampling (positions between pixels stay sharp); bigger: less.
        const int oversampleH = size <= 72.0f ? 2 : 1;
        const int oversampleV = size <= 36.0f ? 2 : 1;
        FontAtlas atlas;
        if (!BakeAtlas(m_Data, size, oversampleH, oversampleV, 0, false, atlas))
        {
            atlas.Texture = nullptr;
            atlas.PixelSize = size;
            atlas.Glyphs.clear();
        }
        atlas.LastUse = (uint64_t)(now * 1000.0);
        m_Sized.push_back(std::move(atlas));
        return m_Sized.back().Texture ? m_Sized.back() : m_Main;
    }

    static std::unordered_map<std::string, Ref<Font>>& FontCache()
    {
        static std::unordered_map<std::string, Ref<Font>> cache;
        return cache;
    }

    Ref<Font> Font::Load(const std::filesystem::path& path)
    {
        auto& cache = FontCache();
        std::string key = path.string();
        auto it = cache.find(key);
        if (it != cache.end())
            return it->second;
        if (!AssetImportLog::ShouldRetry(path))
            return nullptr; // failed before and not changed since
        auto font = CreateRef<Font>(path);
        if (!font->IsLoaded())
            return nullptr;
        font->SetKerningEnabled(AssetImporter::GetFontSettings(path).Kerning);
        cache[key] = font;
        return font;
    }

    Ref<Font> Font::GetDefault()
    {
        return Load(Renderer::GetResourcesDirectory() / "fonts" / "Inter-SemiBold.ttf");
    }

    static bool SameFile(const std::filesystem::path& a, const std::filesystem::path& b)
    {
        std::error_code ea, eb;
        std::filesystem::path ca = std::filesystem::weakly_canonical(a, ea), cb = std::filesystem::weakly_canonical(b, eb);
        return (ea ? a : ca) == (eb ? b : cb);
    }

    size_t Font::ReloadFile(const std::filesystem::path& path, bool* failed)
    {
        size_t reloaded = 0;
        for (auto& [key, font] : FontCache())
        {
            if (!SameFile(font->GetPath(), path))
                continue;
            font->SetKerningEnabled(AssetImporter::GetFontSettings(font->GetPath()).Kerning); // (its .zmeta may be what changed)
            if (font->Reload())
                reloaded++;
            else if (failed)
                *failed = true;
        }
        return reloaded;
    }

    bool Font::IsFileLoaded(const std::filesystem::path& path)
    {
        for (const auto& [key, font] : FontCache())
            if (SameFile(font->GetPath(), path))
                return true;
        return false;
    }

    void Font::ClearCache() { FontCache().clear(); }

}
