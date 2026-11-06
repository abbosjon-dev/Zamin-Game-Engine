#pragma once

namespace Zamin {

    class Window;

    // The game's display options (an options menu): fullscreen and vertical sync. The host that owns
    // the game's window binds it (the player: its own window), and the options change that window;
    // without one (the editor's Play, tests) they are only kept - the editor's window stays as it is.
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

        static constexpr const char* SettingsKey = "Zamin.Display";
        // Writes the options to SaveData (and the save file). False without storage.
        static bool SaveSettings();
        // Applies the saved options; false when none were saved.
        static bool LoadSettings();
        static bool HasSavedSettings();
        static void ClearSavedSettings();
    };

}
