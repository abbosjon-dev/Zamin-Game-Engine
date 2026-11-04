#pragma once

#include "Zamin/Renderer/Texture.h"

#include <filesystem>
#include <unordered_map>

namespace Zamin {

    struct Glyph
    {
        // Quad relative to the pen position, in em units (1.0 = font pixel size).
        float X0, Y0, X1, Y1;
        // Atlas texture coordinates.
        float U0, V0, U1, V1;
        float Advance;
    };

    // TrueType font baked into a texture atlas (stb_truetype).
    // Covers Latin, Latin Extended (o'zbek: Oʻ, Gʻ), Cyrillic.
    class Font
    {
    public:
        explicit Font(const std::filesystem::path& path, float pixelHeight = 64.0f);

        const Glyph* GetGlyph(uint32_t codepoint) const;
        const Ref<Texture2D>& GetAtlasTexture() const { return m_Atlas; }
        float GetAscent() const { return m_Ascent; }
        float GetDescent() const { return m_Descent; }
        float GetLineHeight() const { return m_LineHeight; }
        bool IsLoaded() const { return m_Atlas != nullptr; }
        const std::filesystem::path& GetPath() const { return m_Path; }

        static Ref<Font> GetDefault();
        static Ref<Font> Load(const std::filesystem::path& path);
        static void ClearCache();

    private:
        std::filesystem::path m_Path;
        Ref<Texture2D> m_Atlas;
        std::unordered_map<uint32_t, Glyph> m_Glyphs;
        float m_Ascent = 0.8f, m_Descent = -0.2f, m_LineHeight = 1.2f;
    };

    // Decodes the next UTF-8 codepoint from `s` starting at index `i` (advances i).
    uint32_t DecodeUTF8(const std::string& s, size_t& i);

}
