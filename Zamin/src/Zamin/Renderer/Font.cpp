#include "Zamin/Renderer/Font.h"
#include "Zamin/Asset/AssetImportLog.h"
#include "Zamin/Renderer/Renderer.h"
#include "Zamin/Utils/FileSystem.h"

#include <stb_rect_pack.h>
#include <stb_truetype.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <vector>

namespace Zamin {

    uint32_t DecodeUTF8(std::string_view s, size_t& i)
    {
        unsigned char c = (unsigned char)s[i++];
        if (c < 0x80) return c;
        int extra = (c >= 0xF0) ? 3 : (c >= 0xE0) ? 2 : (c >= 0xC0) ? 1 : 0;
        uint32_t cp = c & (0x3F >> extra);
        for (int k = 0; k < extra && i < s.size(); k++)
            cp = (cp << 6) | ((unsigned char)s[i++] & 0x3F);
        return cp;
    }

    const Glyph* FontAtlas::Find(uint32_t codepoint) const
    {
        auto it = Glyphs.find(codepoint);
        if (it != Glyphs.end())
            return &it->second;
        it = Glyphs.find('?');
        return it != Glyphs.end() ? &it->second : nullptr;
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
        bool BakeAtlas(const std::vector<uint8_t>& ttf, float pixelHeight, int oversampleH, int oversampleV, int fixedSize, bool mips,
                       FontAtlas& out)
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

            std::vector<uint8_t> flipped(bitmap.size());
            for (int y = 0; y < height; y++)
                std::memcpy(&flipped[(size_t)y * width], &bitmap[(size_t)(height - 1 - y) * width], (size_t)width);

            out.PixelSize = pixelHeight;
            out.Glyphs = std::move(glyphs);
            if (!out.Texture || out.Texture->GetWidth() != (uint32_t)width || out.Texture->GetHeight() != (uint32_t)height)
            {
                TextureSpecification spec;
                spec.Width = (uint32_t)width;
                spec.Height = (uint32_t)height;
                spec.Format = ImageFormat::R8;
                spec.GenerateMips = mips;
                spec.Wrap = TextureWrap::ClampToEdge;
                out.Texture = Texture2D::Create(spec);
            }
            out.Texture->SetData(flipped.data(), (uint32_t)flipped.size()); // the same texture object when reloading
            return true;
        }

    }

    Font::Font(const std::filesystem::path& path, float pixelHeight) : m_Path(path), m_PixelHeight(pixelHeight)
    {
        Build(false);
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
        if (!BakeAtlas(ttf, pixelHeight, 2, 2, 2048, true, main))
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
        if (size <= 0.0f || m_Data.empty() || !m_Main.Texture)
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
