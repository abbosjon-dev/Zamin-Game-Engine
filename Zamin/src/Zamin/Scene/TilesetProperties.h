#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Zamin {

    class AssetImportSettings;
    class AssetRegistry;
    struct TilemapComponent;

    // How a tile of a tileset collides where a tilemap holds it on a layer with Collision:
    // - Solid: a full box (the default, so maps made before this setting stay as they were);
    // - None: nothing (grass, flowers, signs painted on the main layer);
    // - OneWay: a platform: bodies jump up through it from below and stand on it from above
    //   (see OneWayPlatform).
    enum class TileCollision : uint8_t { Solid = 0, None = 1, OneWay = 2 };

    const char* TileCollisionName(TileCollision collision); // "Solid", "None", "OneWay" (scene / meta files)

    // An animated tile: where a map holds Tile, Frames are shown in turn, FPS frames a second (every
    // tilemap with that tileset, in the editor's scene view too). The physics uses Tile's collision.
    struct TileAnimation
    {
        static constexpr int MaxFrames = 64;

        int Tile = 0;
        std::vector<int> Frames;
        float FPS = 6.0f;

        bool operator==(const TileAnimation&) const = default;

        // The tile shown `time` seconds after the scene started: Tile without frames, the first
        // frame while FPS <= 0.
        int GetFrame(double time) const;
        int GetFrameIndex(double time) const; // -1 without frames
        float GetDuration() const;            // one loop, seconds (0 without frames or FPS)
    };

    // The per-tile settings of a tileset texture (collision type, animation), kept in its .zmeta
    // (the "TileProperties" key of its import settings) next to its sprite frames and auto-tile
    // rules: every tilemap using the tileset shares them.
    class TilesetProperties
    {
    public:
        static constexpr std::string_view ImportKey = "TileProperties";
        static constexpr int MaxTile = 65535; // larger tile ids are ignored

        TileCollision GetCollision(int tile) const;
        void SetCollision(int tile, TileCollision collision);
        // The tiles with that (non-default) collision, ascending.
        std::vector<int> GetTilesWith(TileCollision collision) const;
        bool HasCollisionSettings() const;

        const std::vector<TileAnimation>& GetAnimations() const { return m_Animations; } // by Tile, ascending
        const TileAnimation* FindAnimation(int tile) const;
        // Adds or replaces the animation of animation.Tile (frames past MaxFrames and bad ones are
        // dropped; with no frame left it is removed). False for a bad tile id.
        bool SetAnimation(TileAnimation animation);
        bool RemoveAnimation(int tile);
        bool HasAnimations() const { return !m_Animations.empty(); }
        // The tile to draw for `tile` at `time` (the tile itself when it is not animated).
        int Resolve(int tile, double time) const
        {
            if (tile < 0 || tile >= (int)m_AnimationIndex.size())
                return tile;
            int index = m_AnimationIndex[(size_t)tile];
            return index < 0 ? tile : m_Animations[(size_t)index].GetFrame(time);
        }

        bool IsEmpty() const { return !HasCollisionSettings() && m_Animations.empty(); }
        bool operator==(const TilesetProperties& other) const;

        // { "Collision": { "None": [...], "OneWay": [...] }, "Animations": [ { "Tile", "Frames", "FPS" } ] }
        std::string ToJson() const;
        // False (and `out` untouched) for text that is not such an object. Unknown collision names
        // and bad tile ids are skipped.
        static bool FromJson(std::string_view text, TilesetProperties& out);
        static TilesetProperties FromImportSettings(const AssetImportSettings& import);
        // Sets (or, when empty, removes) the "TileProperties" key; other keys stay.
        void WriteTo(AssetImportSettings& import) const;

        // The properties of a tileset of the asset registry, cached until the registry changes.
        // Nullptr when the registry does not know the file or it has none.
        static const TilesetProperties* Get(const AssetRegistry* registry, std::string_view tilesetPath);
        static const TilesetProperties* Get(std::string_view tilesetPath); // the active project's registry
        static void ClearCache();

    private:
        void RebuildIndex();

        std::vector<TileCollision> m_Collision;   // by tile id; Solid past the end
        std::vector<TileAnimation> m_Animations;  // by Tile, ascending
        std::vector<int> m_AnimationIndex;        // by tile id: index into m_Animations or -1
    };

    // ------------------------------------------------------------------ tilemap collision
    // A box of the tilemap's static body: cells [X0, X1) of row Y, all of one collision type.
    struct TileColliderBox
    {
        int X0 = 0, X1 = 0, Y = 0;
        TileCollision Type = TileCollision::Solid;

        bool operator==(const TileColliderBox&) const = default;
    };

    struct TilemapCollision
    {
        // What cell (x, y) is for the physics: of its tiles on the layers with Collision, Solid wins
        // over OneWay, OneWay over None; an empty cell (or one outside the map) is None. Without
        // properties every tile is Solid.
        static TileCollision GetCell(const TilemapComponent& tilemap, int x, int y, const TilesetProperties* properties);
        // The boxes the physics body is made of: horizontal runs of Solid cells and of OneWay cells
        // merged into one box each (fewer fixtures, no seams), row by row from the bottom.
        static std::vector<TileColliderBox> BuildBoxes(const TilemapComponent& tilemap, const TilesetProperties* properties);
    };

    // ------------------------------------------------------------------ one-way platforms
    // The rule a one-way surface (OneWay tiles, a BoxCollider2D with OneWay) follows for a body
    // touching it, decided again every physics step.
    struct OneWayPlatform
    {
        static constexpr float Tolerance = 0.05f;  // units: a resting body sinks this far into the surface
        static constexpr float MaxUpSpeed = 0.5f;  // units / s: faster up (relative) = passing through / leaving

        // Whether it collides now: `bottom` = the body's lowest point and `top` = the surface's top
        // along the surface's up axis, `upVelocity` = the body's velocity relative to the surface
        // along that axis, `stepTime` = the physics step. A body moving up never collides (it jumps
        // through from below or leaves); otherwise it does when its bottom is above the top - less
        // the tolerance and how far it fell in the last step, so a fast fall still lands.
        static bool ShouldCollide(float bottom, float top, float upVelocity, float stepTime)
        {
            if (upVelocity > MaxUpSpeed)
                return false;
            float sink = Tolerance + (upVelocity < 0.0f ? -upVelocity * stepTime : 0.0f);
            return bottom >= top - sink;
        }
    };

}
