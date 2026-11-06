#include "Zamin/Editor/GameViewResolution.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace Zamin {

    namespace {
        struct PresetInfo
        {
            const char* Key;
            const char* Name;
            int Width, Height;
            bool Fixed;
        };
        constexpr PresetInfo s_Presets[GameViewPresetCount] = {
            { "Free", "Erkin (butun viewport)", 0, 0, false },
            { "16:9", "16:9", 16, 9, false },
            { "16:10", "16:10", 16, 10, false },
            { "4:3", "4:3", 4, 3, false },
            { "21:9", "21:9 (keng)", 21, 9, false },
            { "1920x1080", "1920x1080 (Full HD)", 1920, 1080, true },
            { "1280x720", "1280x720 (HD)", 1280, 720, true },
            { "Custom", "Boshqa...", 0, 0, true },
        };

        const PresetInfo& Info(GameViewPreset preset)
        {
            int index = (int)preset;
            return s_Presets[index >= 0 && index < GameViewPresetCount ? index : 0];
        }
    }

    const char* GameViewResolution::GetKey(GameViewPreset preset) { return Info(preset).Key; }
    const char* GameViewResolution::GetName(GameViewPreset preset) { return Info(preset).Name; }

    GameViewPreset GameViewResolution::FromKey(const std::string& key, GameViewPreset fallback)
    {
        for (int i = 0; i < GameViewPresetCount; i++)
            if (key == s_Presets[i].Key)
                return (GameViewPreset)i;
        return fallback;
    }

    bool GameViewResolution::IsFixed() const
    {
        if (Preset == GameViewPreset::Custom)
            return !CustomAspect;
        return Info(Preset).Fixed;
    }

    bool GameViewResolution::IsAspect() const
    {
        if (Preset == GameViewPreset::Free)
            return false;
        return !IsFixed();
    }

    glm::ivec2 GameViewResolution::GetSize() const
    {
        if (Preset == GameViewPreset::Custom) // an aspect ratio may be small numbers (5:4), pixels not
            return glm::clamp(CustomSize, glm::ivec2(CustomAspect ? 1 : MinSize), glm::ivec2(MaxSize));
        const PresetInfo& info = Info(Preset);
        return { info.Width, info.Height };
    }

    std::string GameViewResolution::GetLabel() const
    {
        if (Preset == GameViewPreset::Free)
            return "Erkin";
        const glm::ivec2 size = GetSize();
        return IsFixed() ? std::format("{}x{}", size.x, size.y) : std::format("{}:{}", size.x, size.y);
    }

    void GameViewResolution::Sanitize()
    {
        if ((int)Preset < 0 || (int)Preset >= GameViewPresetCount)
            Preset = GameViewPreset::Free;
        CustomSize = glm::clamp(CustomSize, glm::ivec2(1), glm::ivec2(MaxSize));
    }

    glm::vec2 GameViewLayout::AreaToRender(const glm::vec2& areaPoint) const
    {
        const float scale = Scale > 0.0f ? Scale : 1.0f;
        return (areaPoint - Offset) / scale;
    }

    bool GameViewLayout::Contains(const glm::vec2& areaPoint) const
    {
        return areaPoint.x >= Offset.x && areaPoint.y >= Offset.y && areaPoint.x < Offset.x + DisplaySize.x &&
               areaPoint.y < Offset.y + DisplaySize.y;
    }

    GameViewLayout ComputeGameViewLayout(const GameViewResolution& resolution, const glm::vec2& available)
    {
        GameViewLayout layout;
        const glm::vec2 area = glm::max(glm::floor(glm::vec2(std::isfinite(available.x) ? available.x : 1.0f,
                                                             std::isfinite(available.y) ? available.y : 1.0f)),
                                        glm::vec2(1.0f));
        auto centre = [&](const glm::vec2& size) {
            layout.DisplaySize = size;
            layout.Offset = glm::floor((area - size) * 0.5f);
        };

        if (resolution.IsFixed())
        {
            const glm::vec2 size = glm::vec2(resolution.GetSize());
            layout.RenderSize = glm::uvec2(resolution.GetSize());
            if (size.x <= area.x && size.y <= area.y)
            {
                layout.Scale = 1.0f; // fits: pixel for pixel
                centre(size);
            }
            else
            {
                // Too big: scaled down as a whole (the game still draws all its pixels).
                layout.Scale = std::min(area.x / size.x, area.y / size.y);
                centre(glm::max(glm::floor(size * layout.Scale), glm::vec2(1.0f)));
                layout.Scale = layout.DisplaySize.x / size.x;
            }
            return layout;
        }

        if (resolution.IsAspect())
        {
            const glm::vec2 ratio = glm::vec2(glm::max(resolution.GetSize(), glm::ivec2(1)));
            const float aspect = ratio.x / ratio.y;
            glm::vec2 size = area.x / area.y > aspect ? glm::vec2(area.y * aspect, area.y) : glm::vec2(area.x, area.x / aspect);
            size = glm::clamp(glm::floor(size + 0.5f), glm::vec2(1.0f), area);
            centre(size);
            layout.RenderSize = glm::uvec2(size);
            layout.Scale = 1.0f;
            return layout;
        }

        // Free: the whole area.
        layout.Offset = glm::vec2(0.0f);
        layout.DisplaySize = area;
        layout.RenderSize = glm::uvec2(area);
        layout.Scale = 1.0f;
        return layout;
    }

}
