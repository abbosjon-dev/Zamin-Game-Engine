#pragma once

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Zamin {

    class AssetImportSettings;
    class AssetRegistry;
    struct TilemapComponent;

    // ------------------------------------------------------------------ auto-tiles
    // How an auto-tile picks the tile of a cell from its neighbours of the same terrain:
    // - Edges4: the 4 edge neighbours -> 16 variants, indexed by the mask N=1, W=2, E=4, S=8;
    // - Blob8: all 8 neighbours (a corner only counts when both edges next to it do) -> 47 variants,
    //   indexed by AutoTile::BlobIndex of the mask NW=1, N=2, NE=4, W=8, E=16, SW=32, S=64, SE=128.
    // North is up (+y: tile rows count from the bottom of the map).
    enum class AutoTileMode { Edges4 = 0, Blob8 = 1 };

    // One terrain of a tileset (grass, wall, water ...): the tile of every variant. A cell belongs to
    // the terrain when it holds any of these tiles, so painted maps need nothing else stored.
    struct AutoTileRule
    {
        std::string Name = "Yer";
        AutoTileMode Mode = AutoTileMode::Edges4;
        std::vector<int> Tiles;        // GetVariantCount() entries; -1 = no tile drawn for that variant
        bool ConnectOutside = false;   // cells outside the map count as this terrain (no edge at the map's border)

        bool operator==(const AutoTileRule&) const = default;

        static int GetVariantCount(AutoTileMode mode) { return mode == AutoTileMode::Blob8 ? 47 : 16; }
        int GetVariantCount() const { return GetVariantCount(Mode); }
        // Tiles resized to the mode's variant count (new entries -1).
        void EnsureSize();
        // The variant of a neighbour mask (Edges4: 0..15, Blob8: any 8-bit mask, reduced first).
        int GetVariant(uint8_t mask) const;
        // The tile for a neighbour mask: its variant's tile; a variant without one falls back to the
        // nearest variant that has one (fewest differing neighbours), -1 when the rule has no tiles.
        int Resolve(uint8_t mask) const;
        // One of the rule's tiles (a cell holding it is this terrain).
        bool Contains(int tile) const;
        bool HasTiles() const;
        // The rule with tiles first, first + 1, ... for its variants in order.
        static AutoTileRule Sequential(std::string name, AutoTileMode mode, int firstTile);
    };

    // The auto-tile rules of a tileset texture, kept in its .zmeta (the "AutoTiles" array of its import
    // settings) like a sprite sheet's frames: every tilemap using the tileset shares them.
    class AutoTileSet
    {
    public:
        static constexpr std::string_view ImportKey = "AutoTiles";

        std::vector<AutoTileRule> Rules;

        bool IsEmpty() const { return Rules.empty(); }
        int Find(std::string_view name) const;          // -1 when there is no such rule
        const AutoTileRule* Get(int index) const { return index >= 0 && index < (int)Rules.size() ? &Rules[(size_t)index] : nullptr; }
        // `base`, or "<base> <n>" with the first n no rule uses.
        std::string MakeUniqueName(std::string_view base) const;
        // The rule a tile belongs to (-1: none).
        int FindRuleOf(int tile) const;

        bool operator==(const AutoTileSet&) const = default;

        // A JSON array of { "Name", "Mode": "Edges4" | "Blob8", "Tiles": [...], "ConnectOutside" }.
        std::string ToJson() const;
        // False (and `out` untouched) for text that is not such an array. Rules without a name get
        // one, repeated names a suffix; Tiles are padded / cut to the variant count.
        static bool FromJson(std::string_view text, AutoTileSet& out);
        static AutoTileSet FromImportSettings(const AssetImportSettings& import);
        // Sets (or, without rules, removes) the "AutoTiles" key; other keys stay.
        void WriteTo(AssetImportSettings& import) const;

        // The rules of a tileset of the asset registry, cached until the registry changes. Nullptr
        // when the registry does not know the file or it has no rules.
        static const AutoTileSet* Get(const AssetRegistry* registry, std::string_view tilesetPath);
        static const AutoTileSet* Get(std::string_view tilesetPath); // the active project's registry
        static void ClearCache();
    };

    struct AutoTile
    {
        // Edges4 bits
        static constexpr uint8_t North = 1, West = 2, East = 4, South = 8;
        // Blob8 bits
        static constexpr uint8_t NW = 1, N = 2, NE = 4, W = 8, E = 16, SW = 32, S = 64, SE = 128;

        // A corner bit stays only when both edges next to it are set.
        static uint8_t ReduceMask8(uint8_t mask);
        // The 47 distinct reduced masks, ascending (variant i of a Blob8 rule is BlobMasks()[i]).
        static const std::array<uint8_t, 47>& BlobMasks();
        static int BlobIndex(uint8_t mask);               // 0..46 (the mask is reduced first)
        static uint8_t BlobMask(int index);               // 0 for an index out of range
        // The Edges4 mask of a Blob8 one (its edge bits).
        static uint8_t EdgesOf(uint8_t mask8);

        // Whether cell (x, y) of `layer` is the rule's terrain (outside the map: ConnectOutside).
        static bool IsTerrain(const TilemapComponent& tilemap, int layer, const AutoTileRule& rule, int x, int y);
        // The neighbour mask of a cell for the rule's mode.
        static uint8_t GetMask(const TilemapComponent& tilemap, int layer, const AutoTileRule& rule, int x, int y);
        // Gives every terrain cell of [x0, x1] x [y0, y1] (inclusive, clipped) the tile its neighbours
        // call for. Returns the number of cells that changed.
        static int Refresh(TilemapComponent& tilemap, int layer, const AutoTileRule& rule, int x0, int y0, int x1, int y1);
        // Makes the cells terrain (erase: empty), then refreshes them and their neighbours. Cells
        // outside the map are skipped. Returns the number of cells that changed (0 when the rule has
        // no tiles).
        static int Paint(TilemapComponent& tilemap, int layer, const AutoTileRule& rule, const std::vector<glm::ivec2>& cells, bool erase = false);
        static int Paint(TilemapComponent& tilemap, int layer, const AutoTileRule& rule, int x, int y, bool erase = false);
    };

    // ------------------------------------------------------------------ brushes
    // What the paint tools put down: one tile, or a block of tiles picked in the palette (dragged
    // as a rectangle over the tileset). It repeats in every direction from an origin - the map cell
    // of its top-left tile - so a stroke, a rectangle or a fill continues its pattern without seams.
    struct TileBrush
    {
        static constexpr int MaxSize = 64; // tiles per side

        int Width = 1, Height = 1;
        std::vector<int> Tiles = { 0 }; // Width * Height, row-major, row 0 = the top row (as in the tileset picture)

        bool operator==(const TileBrush&) const = default;

        static TileBrush Single(int tile);
        // The tiles of a tileset `columns` x `rows` big between two of its tiles (inclusive, any
        // order; numbered left to right, top to bottom). Clipped to the tileset and to MaxSize.
        static TileBrush FromTileset(int columns, int rows, int tileA, int tileB);
        bool IsSingle() const { return Width == 1 && Height == 1; }
        int GetTopLeft() const { return Tiles.empty() ? -1 : Tiles.front(); }
        // The tile it gives map cell `cell` when its top-left tile is at `origin` (map rows count
        // up, the brush's rows down).
        int At(glm::ivec2 cell, glm::ivec2 origin) const;
        // The origin that centres the brush on `cell` (a single tile: the cell itself).
        glm::ivec2 OriginFor(glm::ivec2 cell) const { return { cell.x - (Width - 1) / 2, cell.y + (Height - 1) / 2 }; }
        // The cells it covers with its top-left tile at `origin` (not clipped to a map).
        std::vector<glm::ivec2> Footprint(glm::ivec2 origin) const;
        // The footprints of every cell of a stroke, each centred on its cell (duplicates removed, in
        // stroke order).
        std::vector<glm::ivec2> StrokeCells(const std::vector<glm::ivec2>& strokeCells) const;
    };

    // ------------------------------------------------------------------ editing tools
    struct TilemapEdit
    {
        // Paints the cells with `brush` repeating from `origin` (outside the map: skipped). Returns
        // the number of cells changed.
        static int Stamp(TilemapComponent& tilemap, int layer, const std::vector<glm::ivec2>& cells, const TileBrush& brush, glm::ivec2 origin);
        // The cells of `layer` connected to (x, y) through edge neighbours that hold the same tile
        // ((x, y) first, then breadth first). With `terrain` and a start cell of that terrain, any
        // tile of the terrain connects. At most maxCells (0: no limit). Empty outside the map.
        static std::vector<glm::ivec2> FloodRegion(const TilemapComponent& tilemap, int layer, int x, int y,
                                                   const AutoTileRule* terrain = nullptr, size_t maxCells = 0);
        // Fills the region of (x, y) with `tile` (-1 = erase). Returns the number of cells changed.
        static int FloodFill(TilemapComponent& tilemap, int layer, int x, int y, int tile, size_t maxCells = 0);
        // The cells of the rectangle between two corners (inclusive, any order), clipped to the map.
        static std::vector<glm::ivec2> RectCells(const TilemapComponent& tilemap, glm::ivec2 a, glm::ivec2 b);
        // Sets every cell of that rectangle to `tile`. Returns the number of cells changed.
        static int FillRect(TilemapComponent& tilemap, int layer, glm::ivec2 a, glm::ivec2 b, int tile);
        // Sets the cells (outside the map: skipped). Returns the number of cells changed.
        static int SetCells(TilemapComponent& tilemap, int layer, const std::vector<glm::ivec2>& cells, int tile);
        // The cells on the line from a to b (Bresenham, both ends included): a brush stroke without gaps.
        static std::vector<glm::ivec2> LineCells(glm::ivec2 a, glm::ivec2 b);
    };

}
