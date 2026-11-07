#pragma once

#include <glm/glm.hpp>

#include <optional>

namespace Zamin {

    class Window;

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

        bool IsEmpty() const { return !Fullscreen && !VSync && !Gamma && !Brightness; }
    };

    // The game's display options (an options menu): fullscreen, vertical sync, gamma and brightness. The
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
        // DisplayAdjustment::Gamma / Brightness (kept in their ranges).
        static void SetGamma(float gamma);
        static float GetGamma();
        static void SetBrightness(float brightness);
        static float GetBrightness();
        static DisplayAdjustment GetAdjustment();
        static void SetAdjustment(const DisplayAdjustment& adjustment);

        static constexpr const char* SettingsKey = "Zamin.Display";
        // Writes the options to SaveData (and the save file). False without storage.
        static bool SaveSettings();
        // Applies the saved options; false when none were saved.
        static bool LoadSettings();
        static bool HasSavedSettings();
        // What was saved, without applying it (the player makes its window with it).
        static SavedDisplaySettings GetSavedSettings();
        static void ClearSavedSettings();
    };

}
