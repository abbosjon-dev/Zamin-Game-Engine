#pragma once

#include "Zamin/Core/Window.h"

#include <glm/glm.hpp>

#include <optional>
#include <vector>

namespace Zamin {

    // The picture's gamma and brightness the player shows the game with (an options menu's sliders): the
    // whole finished frame - the scene, its UI, the fades - on its way to the screen.
    struct DisplayAdjustment
    {
        static constexpr float MinGamma = 0.5f, MaxGamma = 2.0f;
        static constexpr float MinBrightness = 0.5f, MaxBrightness = 1.5f;
        // 1: as the game draws it; above 1 the mid tones get brighter (shadows show more), below 1 darker
        // (the screen shows colour^(1 / Gamma)). Black and white stay black and white.
        float Gamma = 1.0f;
        // The whole picture times this (before the gamma; white clips above 1).
        float Brightness = 1.0f;

        bool operator==(const DisplayAdjustment&) const = default;
        // Both 1 (within a slider's step): nothing to do.
        bool IsIdentity() const;
        // Into the ranges above (a NaN to 1).
        void Sanitize();
        // What the screen shows for a display (sRGB) colour 0..1 of the game: the shader's sum on the CPU.
        glm::vec3 Apply(const glm::vec3& displayColor) const;
    };

    // The display options the game saved (Display::SaveSettings) - only what was saved.
    struct SavedDisplaySettings
    {
        std::optional<bool> Fullscreen, VSync;
        std::optional<float> Gamma, Brightness;
        std::optional<ScreenResolution> Resolution; // the windowed size (and exclusive fullscreen's video mode)

        bool IsEmpty() const { return !Fullscreen && !VSync && !Gamma && !Brightness && !Resolution; }
    };

    // The game's display options (an options menu): fullscreen, vertical sync, the resolution, gamma and brightness. The
    // host that owns the game's window binds it (the player: its own window), and the options change that
    // window; without one (the editor's Play, tests) they are only kept - the editor's window stays as it
    // is. The gamma and the brightness are the player's (GetAdjustment: its last pass over every frame).
    // SaveSettings / LoadSettings keep them with SaveData (keys under SettingsKey), apart from the
    // project; the player loads them when it starts.
    class Display
    {
    public:
        // The window the options apply to (nullptr: none). Binding takes its current state unless
        // the options were set before (then they are applied to it).
        static void BindWindow(Window* window);
        static bool HasWindow();

        static void SetFullscreen(bool fullscreen);
        static bool IsFullscreen();
        static void SetVSync(bool enabled);
        static bool IsVSync();
        // The resolution: the window's size in windowed mode (and the video mode of exclusive fullscreen; a
        // borderless window always covers its monitor). Without a window it is only kept. Sizes outside the
        // player's window size range are refused (false).
        static bool SetResolution(uint32_t width, uint32_t height);
        static bool SetResolution(const ScreenResolution& resolution) { return SetResolution(resolution.Width, resolution.Height); }
        // The window's windowed size; without one what was set (0 x 0: nothing).
        static ScreenResolution GetResolution();
        // What an options menu offers: the sizes of the window's monitor (MakeResolutionList), without a window
        // the common ones; the current resolution is always among them.
        static std::vector<ScreenResolution> GetResolutions();
        // The list of resolutions from a monitor's video modes: each once, at least 640 x 360 and not larger than
        // the largest mode (the monitor), the smallest first; no modes: the common 16:9 sizes (1280 x 720 ...
        // 2560 x 1440). `current` is added when it is not there (a window size of its own).
        static std::vector<ScreenResolution> MakeResolutionList(const std::vector<ScreenResolution>& modes, const ScreenResolution& current = {});
        // DisplayAdjustment::Gamma / Brightness (kept in their ranges).
        static void SetGamma(float gamma);
        static float GetGamma();
        static void SetBrightness(float brightness);
        static float GetBrightness();
        static DisplayAdjustment GetAdjustment();
        static void SetAdjustment(const DisplayAdjustment& adjustment);

        static constexpr const char* SettingsKey = "Zamin.Display";
        // Writes the options to SaveData (and the save file; the resolution once SetResolution chose one). False
        // without storage.
        static bool SaveSettings();
        // Applies the saved options; false when none were saved.
        static bool LoadSettings();
        static bool HasSavedSettings();
        // What was saved, without applying it (the player makes its window with it).
        static SavedDisplaySettings GetSavedSettings();
        static void ClearSavedSettings();
    };

}
