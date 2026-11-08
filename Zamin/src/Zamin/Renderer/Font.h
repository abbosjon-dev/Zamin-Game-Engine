#pragma once

#include "Zamin/Renderer/Texture.h"

#include <filesystem>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Zamin {

    struct Glyph
    {
        // Quad relative to the pen position, in em units (1.0 = font pixel size).
        float X0, Y0, X1, Y1;
        // Atlas texture coordinates.
        float U0, V0, U1, V1;
        float Advance;
    };

    // The glyphs of a font rasterized at one pixel size into a texture.
    struct FontAtlas
    {
        float PixelSize = 0.0f;
        Ref<Texture2D> Texture;
        std::unordered_map<uint32_t, Glyph> Glyphs;
        uint64_t LastUse = 0; // sized atlases: when it was last asked for (the least recently used goes first)

        const Glyph* Find(uint32_t codepoint) const;
    };

    // TrueType font baked into texture atlases (stb_truetype).
    // Covers Latin, Latin Extended (o'zbek: Oʻ, Gʻ), Cyrillic.
    // The main atlas (64 px, mipmapped) gives the metrics every layout uses; text is drawn from an
    // atlas baked at about the size it has on the screen (GetAtlas), so small and large text both
    // stay crisp. Those are made the first time a size is drawn and kept per font (at most
    // MaxSizedAtlases; the least recently used one goes).
    class Font
    {
    public:
        explicit Font(const std::filesystem::path& path, float pixelHeight = 64.0f);

        // Metrics (and the main atlas's quads): the same whatever size the text is drawn at.
        const Glyph* GetGlyph(uint32_t codepoint) const; // '?' for a character the font does not have
        bool HasGlyph(uint32_t codepoint) const { return m_Main.Glyphs.contains(codepoint); }
        const Ref<Texture2D>& GetAtlasTexture() const { return m_Main.Texture; }
        float GetAscent() const { return m_Ascent; }
        float GetDescent() const { return m_Descent; }
        float GetLineHeight() const { return m_LineHeight; }
        bool IsLoaded() const { return m_Main.Texture != nullptr; }
        const std::filesystem::path& GetPath() const { return m_Path; }

        // The atlas to draw text `pixelSize` screen pixels high (1 em) with: one baked at
        // GetAtlasPixelSize(pixelSize) (made now when there is none yet), else the main atlas.
        const FontAtlas& GetAtlas(float pixelSize);
        const FontAtlas& GetMainAtlas() const { return m_Main; }
        size_t GetSizedAtlasCount() const { return m_Sized.size(); }

        // Reads the file again into this font (same object; the atlas texture is updated in
        // place). False - and the old glyphs stay - when it cannot be read.
        bool Reload();

        // The size the atlas for text `pixelSize` px high is baked at: the nearest of a ladder of
        // sizes (whole pixels for small text, coarser steps further up) that is not much smaller;
        // 0 (the main atlas) below MinAtlasPixelSize, when sized atlases are off or for <= 0.
        // Above MaxAtlasPixelSize: that size (magnified).
        static float GetAtlasPixelSize(float pixelSize);
        static constexpr float MinAtlasPixelSize = 5.0f;
        static constexpr float MaxAtlasPixelSize = 128.0f;
        // A game's UI uses a dozen sizes or so (HUD, menus, banners): fewer slots would bake the same sizes again
        // and again as banners come and go (each bake a hitch of several ms).
        static constexpr size_t MaxSizedAtlases = 16;
        // Sized atlases on (default) / off (everything from the main atlas, as before them).
        static void SetSizedAtlasesEnabled(bool enabled);
        static bool AreSizedAtlasesEnabled();

        static Ref<Font> GetDefault();
        static Ref<Font> Load(const std::filesystem::path& path);
        // Re-reads a file into the cached font made from it. Returns how many fonts were updated;
        // `failed` is set when the file could not be read.
        static size_t ReloadFile(const std::filesystem::path& path, bool* failed = nullptr);
        static bool IsFileLoaded(const std::filesystem::path& path);
        static void ClearCache();

    private:
        bool Build(bool reload);

    private:
        std::filesystem::path m_Path;
        float m_PixelHeight = 64.0f;
        std::vector<uint8_t> m_Data; // the font file (sized atlases are baked from it)
        FontAtlas m_Main;
        std::vector<FontAtlas> m_Sized;
        uint64_t m_UseCounter = 0;
        float m_Ascent = 0.8f, m_Descent = -0.2f, m_LineHeight = 1.2f;
    };

    // Decodes the next UTF-8 codepoint from `s` starting at index `i` (advances i).
    uint32_t DecodeUTF8(std::string_view s, size_t& i);

}
