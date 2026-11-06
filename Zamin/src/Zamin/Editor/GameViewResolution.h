#pragma once

#include <glm/glm.hpp>

#include <string>

namespace Zamin {

    // The size of the editor's game view while playing: the whole viewport (Free), an aspect ratio
    // the picture is letterboxed to, or a fixed resolution the game is drawn at (shown 1:1 when it
    // fits the viewport, scaled down to fit when it does not).
    enum class GameViewPreset
    {
        Free = 0,
        Aspect16x9,
        Aspect16x10,
        Aspect4x3,
        Aspect21x9,
        Fixed1920x1080,
        Fixed1280x720,
        Custom,
    };
    inline constexpr int GameViewPresetCount = (int)GameViewPreset::Custom + 1;

    struct GameViewResolution
    {
        GameViewPreset Preset = GameViewPreset::Free;
        // Custom: a fixed resolution in pixels, or (CustomAspect) only its aspect ratio.
        glm::ivec2 CustomSize{ 1280, 720 };
        bool CustomAspect = false;

        static constexpr int MinSize = 16, MaxSize = 8192; // pixels (an aspect ratio's sides: 1..MaxSize)

        // "Free", "16:9", "1920x1080", "Custom" ... (stored in the editor view settings).
        static const char* GetKey(GameViewPreset preset);
        static GameViewPreset FromKey(const std::string& key, GameViewPreset fallback);
        // The label of a preset in the toolbar ("Erkin", "16:9", "1920x1080 (Full HD)" ...).
        static const char* GetName(GameViewPreset preset);

        // A fixed resolution (pixels) or an aspect ratio (w : h) for this preset; both false for Free.
        bool IsFixed() const;
        bool IsAspect() const;
        // The fixed resolution / the aspect ratio's two sides (Free: 0, 0).
        glm::ivec2 GetSize() const;
        // The short label of the current choice: "Erkin", "16:9", "1920x1080", "800x600", "5:4".
        std::string GetLabel() const;

        // Custom size brought into 1..MaxSize (GetSize keeps pixel sizes at least MinSize).
        void Sanitize();

        bool operator==(const GameViewResolution&) const = default;
    };

    // Where the game picture goes inside the viewport's area and the size it is drawn at.
    struct GameViewLayout
    {
        glm::vec2 Offset{ 0.0f };       // the picture's top-left corner inside the area (whole pixels)
        glm::vec2 DisplaySize{ 0.0f };  // its size on screen
        glm::uvec2 RenderSize{ 1, 1 };  // the size the game is drawn at
        float Scale = 1.0f;             // DisplaySize / RenderSize (1: pixel for pixel)

        // A point of the area (pixels from its top-left corner) in the game's picture (render
        // pixels from its top-left corner); outside the picture it lies outside 0..RenderSize.
        glm::vec2 AreaToRender(const glm::vec2& areaPoint) const;
        // Whether a point of the area is on the picture (not on the bars around it).
        bool Contains(const glm::vec2& areaPoint) const;
    };

    // The layout of the game view in an area `available` pixels big. Free fills it; an aspect ratio
    // is the biggest rectangle of that shape, centred (bars on two sides); a fixed resolution is
    // centred at 1:1 when it fits, else scaled down to fit (the game still draws at its size).
    GameViewLayout ComputeGameViewLayout(const GameViewResolution& resolution, const glm::vec2& available);

}
