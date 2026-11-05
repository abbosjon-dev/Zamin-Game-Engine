#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Zamin {

    class AssetImportSettings;
    class AssetRegistry;

    // One named frame of a sprite sheet: a rectangle of the texture in pixels, measured like image
    // editors do (x to the right, y down from the top-left corner of the picture).
    struct SpriteFrame
    {
        std::string Name;
        glm::ivec2 Position{ 0 };        // top-left corner, px
        glm::ivec2 Size{ 16 };           // px
        glm::vec2 Pivot{ 0.5f };         // 0..1 inside the frame, (0, 0) = bottom-left, (0.5, 0.5) = centre
        glm::vec4 Border{ 0.0f };        // 9-slice borders in px: left, right, top, bottom

        bool operator==(const SpriteFrame&) const = default;

        // The frame's texture coordinates in a texture of `width` x `height` pixels (OpenGL: v = 0
        // is the bottom row, so the top of the frame has the larger v).
        void GetUV(uint32_t width, uint32_t height, glm::vec2& uvMin, glm::vec2& uvMax) const;
        bool HasBorder() const { return Border.x > 0.0f || Border.y > 0.0f || Border.z > 0.0f || Border.w > 0.0f; }
    };

    // How a texture is cut into a grid of frames (Sprite Editor > Slice).
    struct SpriteSliceSettings
    {
        enum class SliceMode { CellSize = 0, CellCount };
        SliceMode Mode = SliceMode::CellSize;
        glm::ivec2 CellSize{ 16, 16 };   // px (CellSize mode)
        glm::ivec2 CellCount{ 4, 1 };    // columns, rows (CellCount mode)
        glm::ivec2 Offset{ 0 };          // px from the top-left corner to the first cell
        glm::ivec2 Padding{ 0 };         // px between neighbouring cells
        glm::vec2 Pivot{ 0.5f };
        std::string NamePrefix = "sprite"; // frames are named "<prefix>_<index>"
        bool SkipEmpty = false;          // cells whose pixels are all transparent are left out (needs the pixels)

        bool operator==(const SpriteSliceSettings&) const = default;
    };

    // The named frames of a texture. They are kept in the texture's .zmeta file (the "Sprites" array
    // of its import settings), so a texture with frames is a sprite atlas: Sprite Renderers refer to
    // the texture (by path + UUID like any asset) and to a frame by its name.
    class SpriteSheet
    {
    public:
        static constexpr std::string_view ImportKey = "Sprites";

        std::vector<SpriteFrame> Frames;

        bool IsEmpty() const { return Frames.empty(); }
        int FindIndex(std::string_view name) const;     // -1 when there is no such frame
        const SpriteFrame* Find(std::string_view name) const;
        // Frames named "<prefix><number>" or "<prefix>_<number>" ("walk_0", "walk_1", ... "walk_10"),
        // ordered by the number; an empty prefix gives every frame in sheet order.
        std::vector<std::string> FindSequence(std::string_view prefix) const;
        // The distinct prefixes of numbered frames ("walk_3" -> "walk"), in sheet order.
        std::vector<std::string> GetSequencePrefixes() const;
        // `base`, or "<base>_<n>" with the first n no frame uses.
        std::string MakeUniqueName(std::string_view base) const;

        bool operator==(const SpriteSheet&) const = default;

        // ---- slicing
        // The size of one cell in px (CellCount mode: the texture divided evenly after offset and padding).
        static glm::ivec2 GetCellSize(const SpriteSliceSettings& settings, uint32_t width, uint32_t height);
        // The grid of `settings` over a `width` x `height` texture, left to right, top to bottom. Cells
        // that do not fit entirely are left out. With `rgba` (width * height * 4 bytes, rows top
        // first) and SkipEmpty, cells without one visible pixel are left out too.
        static std::vector<SpriteFrame> SliceGrid(const SpriteSliceSettings& settings, uint32_t width, uint32_t height,
                                                  const uint8_t* rgba = nullptr);

        // ---- storage
        // A JSON array of { "Name", "Rect": [x, y, w, h], "Pivot": [x, y], "Border": [l, r, t, b] }.
        std::string ToJson() const;
        // False (and `out` untouched) for text that is not such an array. Frames without a name or
        // with a size below 1 px are skipped; repeated names get a suffix.
        static bool FromJson(std::string_view text, SpriteSheet& out);
        static SpriteSheet FromImportSettings(const AssetImportSettings& import);
        // Sets (or, without frames, removes) the "Sprites" key; other keys stay.
        void WriteTo(AssetImportSettings& import) const;

        // The frames of a texture of the asset registry (its import settings), cached until the
        // registry changes. Nullptr when the registry does not know the file or it has no frames.
        static const SpriteSheet* Get(const AssetRegistry* registry, std::string_view texturePath);
        // The same with the active project's registry.
        static const SpriteSheet* Get(std::string_view texturePath);
        static void ClearCache();
    };

    // The part of a texture a sprite shows: the whole picture, a named frame or a grid cell.
    struct SpriteRegion
    {
        glm::vec2 UVMin{ 0.0f }, UVMax{ 1.0f };
        glm::vec2 Pixels{ 1.0f };        // its size in px
        glm::vec2 Pivot{ 0.5f };
        glm::vec4 Border{ 0.0f };        // px: left, right, top, bottom
        bool Whole = true;               // the whole texture (not a frame or a cell)

        static SpriteRegion WholeTexture(uint32_t width, uint32_t height);
        static SpriteRegion FromFrame(const SpriteFrame& frame, uint32_t width, uint32_t height);
        // Cell `index` of a columns x rows grid, numbered left to right, top to bottom (clamped).
        static SpriteRegion GridCell(int columns, int rows, int index, uint32_t width, uint32_t height);
    };

    // ------------------------------------------------------------------ sprite geometry
    // How a Sprite Renderer fills its rectangle with the picture:
    // - Simple: the picture stretched over the quad (the transform's scale sizes it);
    // - Sliced: 9-slice - the corners keep their size (border px / pixels per unit), the edges stretch
    //   along one axis and the centre in both, over Size (local units). For panels, buttons, frames.
    // - Tiled: like Sliced, but the edges and the centre repeat at their own size instead of stretching
    //   (without a border the whole picture repeats over Size).
    enum class SpriteDrawMode { Simple = 0, Sliced, Tiled };

    // One quad of a sprite, in the sprite's local space.
    struct SpriteQuad
    {
        glm::vec2 Min{ 0.0f }, Max{ 0.0f };
        glm::vec2 UVMin{ 0.0f }, UVMax{ 1.0f };
    };

    struct SpriteGeometry
    {
        SpriteDrawMode Mode = SpriteDrawMode::Simple;
        glm::vec2 Size{ 1.0f };          // Sliced / Tiled: the rectangle in local units (Simple: 1 x 1)
        glm::vec2 Pivot{ 0.5f };         // the local origin inside the rectangle (0..1)
        glm::vec2 UVMin{ 0.0f }, UVMax{ 1.0f };  // the picture's region of the texture
        glm::vec2 RegionPixels{ 1.0f };  // that region's size in px
        glm::vec4 Border{ 0.0f };        // px: left, right, top, bottom
        float PixelsPerUnit = 100.0f;

        // The rectangle the sprite covers in local space (before the transform).
        void GetBounds(glm::vec2& min, glm::vec2& max) const;
        // Appends the quads to draw (Simple: 1, Sliced: up to 9, Tiled: as many as needed, at most
        // `maxQuads`; quads of zero area are left out). Returns how many were added.
        size_t Build(std::vector<SpriteQuad>& out, size_t maxQuads = 4096) const;
    };

    // `quad` (its picture `pixels` texture pixels big) grown by `grow` pixels on every side - corners
    // and UVs alike - so an outline around the picture is not cut at its edge. `uvRect` gets the UV
    // rectangle of the picture itself (min.xy, max.xy). grow <= 0: the quad as it is.
    SpriteQuad GrowSpriteQuad(const SpriteQuad& quad, glm::vec2 pixels, float grow, glm::vec4& uvRect);

}
