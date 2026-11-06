#pragma once

#include "Zamin/Editor/EditorAutosave.h"

#include <glm/glm.hpp>

#include <filesystem>
#include <map>
#include <span>
#include <string>

namespace Zamin {

    // A named colour the preferences offer as a button (accent colours, play-mode tints).
    struct ColorPreset
    {
        const char* Key;
        const char* Name;
        glm::vec3 Color;
    };

    // The editor's look: the accent colour (selected tabs, checkmarks, sliders, highlights) and the
    // size of the UI font (the rest of the UI - paddings, spacings - scales with it).
    struct EditorThemeSettings
    {
        glm::vec3 Accent{ 0.180f, 0.737f, 0.580f };
        float FontSize = DefaultFontSize; // px at a content scale of 1

        static constexpr float DefaultFontSize = 16.0f;
        static constexpr float MinFontSize = 12.0f, MaxFontSize = 22.0f;

        static glm::vec3 GetDefaultAccent() { return { 0.180f, 0.737f, 0.580f }; }
        // The accent's lighter shade (hovered, active) and darker one (pressed buttons, selected rows).
        static glm::vec3 GetHoverShade(const glm::vec3& accent);
        static glm::vec3 GetDarkShade(const glm::vec3& accent);
        static std::span<const ColorPreset> GetAccentPresets();
        // The preset the accent is (-1: a colour of its own).
        int FindAccentPreset() const;
        // FontSize / DefaultFontSize: how much bigger than the default the UI is drawn.
        float GetUIScale() const { return FontSize / DefaultFontSize; }
        void Sanitize();
    };

    // While the game runs in the editor its UI chrome (window backgrounds, title bars, tabs, the
    // menu and status bars, frames) is tinted, so play mode is never mistaken for edit mode (the
    // changes made while playing are lost on stop). The tint multiplies the colours channel by
    // channel (1 = unchanged, up to MaxChannel); Strength fades it in from no tint at 0.
    struct PlayModeTintSettings
    {
        bool Enabled = true;
        glm::vec3 Color{ 1.7f, 0.9f, 0.9f };
        float Strength = 1.0f;
        bool InSimulate = false; // physics simulation (no game scripts) tinted too

        static constexpr float MaxChannel = 2.0f;

        // Whether the UI is tinted now (play: always when enabled; simulation: InSimulate).
        bool IsActive(bool playing, bool simulating) const { return Enabled && (playing || (simulating && InSimulate)); }
        // The per-channel factor: mix(1, Color, Strength).
        glm::vec3 GetMultiplier() const;
        // A UI colour under the tint (rgb multiplied and clamped to 0..1, alpha kept).
        glm::vec4 Apply(const glm::vec4& color) const;
        static std::span<const ColorPreset> GetPresets();
        int FindPreset() const;
        void Sanitize();
    };

    // The editor camera's projection and fly speed: what a new session starts with.
    struct EditorCameraDefaults
    {
        float FOV = 45.0f;          // degrees (vertical)
        float NearClip = 0.1f;
        float FarClip = 1000.0f;
        float FlySpeed = 6.0f;      // units / second (RMB + WASD)

        static constexpr float MinFOV = 10.0f, MaxFOV = 120.0f;
        static constexpr float MinNear = 0.001f, MaxNear = 10.0f;
        static constexpr float MaxFar = 100000.0f;
        static constexpr float MinFlySpeed = 0.5f, MaxFlySpeed = 200.0f;

        // Far stays at least 10x Near.
        void Sanitize();
    };

    // The console's options that last between sessions (the search text does not).
    struct ConsolePreferences
    {
        bool ShowTimestamps = true;
        bool Collapse = false;
        bool ClearOnPlay = false;
        bool AutoScroll = true;
        bool ShowTrace = false, ShowInfo = true, ShowWarn = true, ShowError = true;
    };

    // The scene's undo history.
    struct UndoPreferences
    {
        // Selection changes are undo steps of their own (Ctrl+Z picks the previous selection
        // again); off: an edit's undo still brings back the selection it had.
        bool RecordSelection = true;
    };

    // The editor's per-user preferences (Tahrirlash > Sozlamalar), stored as JSON in the user's
    // config directory (~/.config/zamin/preferences.json).
    struct EditorPreferences
    {
        EditorThemeSettings Theme;
        PlayModeTintSettings PlayTint;
        EditorCameraDefaults Camera;
        ConsolePreferences Console;
        AutosaveSettings Autosave;
        UndoPreferences Undo;
        // Keyboard shortcuts rebound in the shortcuts window: id -> chord ("" = unbound).
        std::map<std::string, std::string> Shortcuts;

        void Sanitize();
        std::string ToJson() const;
        // Missing fields keep their current values. False (nothing changes) when the text is not a
        // preferences JSON object.
        bool FromJson(const std::string& text);
        bool Save(const std::filesystem::path& path) const;
        bool Load(const std::filesystem::path& path);

        // $XDG_CONFIG_HOME/zamin, else ~/.config/zamin (not created here).
        static std::filesystem::path GetConfigDirectory();
        static std::filesystem::path GetDefaultPath() { return GetConfigDirectory() / "preferences.json"; }
    };

}
