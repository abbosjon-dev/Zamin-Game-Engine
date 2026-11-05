#include "Zamin/Renderer/Font.h"
#include "Zamin/Renderer/Renderer.h"
#include "Zamin/Utils/FileSystem.h"

#include <stb_rect_pack.h>
#include <stb_truetype.h>

#include <cstring>
#include <vector>

namespace Zamin {

    uint32_t DecodeUTF8(const std::string& s, size_t& i)
    {
        unsigned char c = (unsigned char)s[i++];
        if (c < 0x80) return c;
        int extra = (c >= 0xF0) ? 3 : (c >= 0xE0) ? 2 : (c >= 0xC0) ? 1 : 0;
        uint32_t cp = c & (0x3F >> extra);
        for (int k = 0; k < extra && i < s.size(); k++)
            cp = (cp << 6) | ((unsigned char)s[i++] & 0x3F);
        return cp;
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
        const char* what = reload ? "qayta yuklanmadi" : "yuklanmadi";
        std::vector<uint8_t> ttf = FileSystem::ReadBinaryFile(path);
        if (ttf.empty())
        {
            ZM_CORE_ERROR("Shrift {}: '{}' (fayl o'qilmadi)", what, path.string());
            return false;
        }

        stbtt_fontinfo info;
        int offset = stbtt_GetFontOffsetForIndex(ttf.data(), 0);
        if (offset < 0 || !stbtt_InitFont(&info, ttf.data(), offset))
        {
            ZM_CORE_ERROR("Shrift {}: '{}' (formati noto'g'ri)", what, path.string());
            return false;
        }

        constexpr int atlasSize = 2048;
        std::vector<uint8_t> bitmap((size_t)atlasSize * atlasSize);

        struct Range { int First, Count; };
        const Range ranges[] = {
            { 0x0020, 0x7F - 0x20 },    // Basic Latin
            { 0x00A0, 0x100 - 0xA0 },   // Latin-1 Supplement
            { 0x0100, 0x180 - 0x100 },  // Latin Extended-A
            { 0x02BB, 2 },              // ʻ ʼ (o'zbek lotin)
            { 0x0400, 0x500 - 0x400 },  // Cyrillic
            { 0x2018, 0x2027 - 0x2018 },// quotes, bullets, ellipsis
        };

        std::vector<stbtt_pack_range> packRanges;
        std::vector<std::vector<stbtt_packedchar>> packed;
        packed.reserve(std::size(ranges));
        for (const auto& r : ranges)
        {
            packed.emplace_back(r.Count);
            stbtt_pack_range pr{};
            pr.font_size = pixelHeight;
            pr.first_unicode_codepoint_in_range = r.First;
            pr.num_chars = r.Count;
            pr.chardata_for_range = packed.back().data();
            packRanges.push_back(pr);
        }

        stbtt_pack_context pc;
        stbtt_PackBegin(&pc, bitmap.data(), atlasSize, atlasSize, 0, 2, nullptr);
        stbtt_PackSetOversampling(&pc, 2, 2);
        stbtt_PackFontRanges(&pc, ttf.data(), 0, packRanges.data(), (int)packRanges.size());
        stbtt_PackEnd(&pc);

        int ascent, descent, lineGap;
        stbtt_GetFontVMetrics(&info, &ascent, &descent, &lineGap);
        float scale = stbtt_ScaleForPixelHeight(&info, pixelHeight);
        std::unordered_map<uint32_t, Glyph> glyphs;

        for (size_t r = 0; r < std::size(ranges); r++)
        {
            for (int c = 0; c < ranges[r].Count; c++)
            {
                const stbtt_packedchar& pcd = packed[r][c];
                Glyph g;
                // stb: y grows downwards; we flip so +Y is up.
                g.X0 = pcd.xoff / pixelHeight;
                g.X1 = pcd.xoff2 / pixelHeight;
                g.Y0 = -pcd.yoff2 / pixelHeight;
                g.Y1 = -pcd.yoff / pixelHeight;
                g.U0 = pcd.x0 / (float)atlasSize;
                g.U1 = pcd.x1 / (float)atlasSize;
                // Texture rows are uploaded top-first, so v = row / size; flip to GL convention.
                g.V0 = pcd.y1 / (float)atlasSize;
                g.V1 = pcd.y0 / (float)atlasSize;
                g.Advance = pcd.xadvance / pixelHeight;
                glyphs[(uint32_t)(ranges[r].First + c)] = g;
            }
        }

        // Flip bitmap vertically so v=0 is the bottom row (OpenGL convention).
        std::vector<uint8_t> flipped(bitmap.size());
        for (int y = 0; y < atlasSize; y++)
            std::memcpy(&flipped[(size_t)y * atlasSize], &bitmap[(size_t)(atlasSize - 1 - y) * atlasSize], atlasSize);
        for (auto& [cp, g] : glyphs)
        {
            g.V0 = 1.0f - g.V0;
            g.V1 = 1.0f - g.V1;
        }

        m_Glyphs = std::move(glyphs);
        m_Ascent = ascent * scale / pixelHeight;
        m_Descent = descent * scale / pixelHeight;
        m_LineHeight = (ascent - descent + lineGap) * scale / pixelHeight;
        if (!m_Atlas)
        {
            TextureSpecification spec;
            spec.Width = atlasSize;
            spec.Height = atlasSize;
            spec.Format = ImageFormat::R8;
            spec.GenerateMips = true;
            spec.Wrap = TextureWrap::ClampToEdge;
            m_Atlas = Texture2D::Create(spec);
        }
        m_Atlas->SetData(flipped.data(), (uint32_t)flipped.size()); // same atlas object when reloading
        return true;
    }

    const Glyph* Font::GetGlyph(uint32_t codepoint) const
    {
        auto it = m_Glyphs.find(codepoint);
        if (it != m_Glyphs.end())
            return &it->second;
        it = m_Glyphs.find('?');
        return it != m_Glyphs.end() ? &it->second : nullptr;
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
