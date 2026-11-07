#pragma once

#include "Zamin/Core/LogSettings.h"
#include "Zamin/Core/Window.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Zamin {

    struct ImageData;

    // The player's log (Project settings > Player > Log): the loggers' and the files' levels, whether
    // log files are written (~/.local/share/<company>/<product>/logs/player-<date>.log and latest.log)
    // and how many dated ones are kept. ZAMIN_LOG_LEVEL and --log-level go over the levels, --log-file /
    // --no-log-file over the files.
    struct PlayerLogSettings
    {
        LogLevelSettings Levels;
        bool WriteFile = true;
        int KeepFiles = 5;          // dated logs kept (this run's included)

        static constexpr int MinKeepFiles = 1, MaxKeepFiles = 100;
        bool operator==(const PlayerLogSettings&) const = default;
    };

    // The game as the player runs it (Project settings > Player): who made it and its version, its
    // window and its icon. Projects without a "Player" section get the defaults - a 1280x720 resizable
    // window with vsync, titled with the project's name, unlimited frame rate, running in the background
    // and no icon: what the player always did. ZaminPlayer applies them when it creates its window; its
    // command line (--fullscreen, --windowed, --resolution, --fps ...) and the display options the game
    // saved (Display::SaveSettings) win over them (PlayerLaunchSettings::Resolve).
    struct PlayerSettings
    {
        // ---- identity
        // The studio / author. Empty: the game's SaveData stays in <data home>/zamin/<product>; else it
        // is <data home>/<company>/<product> (~/.local/share/<company>/<product> on Linux).
        std::string CompanyName;
        // The game's name for players (window title, SaveData folder). Empty: the project's name.
        std::string ProductName;
        // A semantic version ("1.4.2", "2.0.0-beta.1"): Application::GetVersion.
        std::string Version = DefaultVersion;

        // ---- window
        uint32_t Width = 1280, Height = 720; // the windowed size (fullscreen uses the monitor's own)
        WindowMode Mode = WindowMode::Windowed;
        bool Resizable = true;
        bool VSync = true;
        std::string Title;                   // empty: the product name
        bool VersionInTitle = false;         // "<title> 1.4.2"
        int TargetFrameRate = 0;             // the most frames per second; 0: no limit (vsync paces it)
        bool RunInBackground = true;         // false: the game stands still while its window is not in front
        // F11 and Alt+Enter (and the input map's "ToggleFullscreen" action, when it has one) switch between
        // fullscreen and the window (the choice is saved like Display::SaveSettings).
        bool FullscreenToggle = true;

        // ---- splash screen (before the first scene, which loads in the background meanwhile)
        bool SplashScreen = true;
        float SplashDuration = 2.0f;         // seconds, fades included (longer while the scene still loads)
        bool SplashSkippable = true;         // any key / click / gamepad button ends it (once the scene is loaded)
        // The game's own picture (PNG / JPG relative to the assets) instead of the Zamin logo.
        std::filesystem::path SplashImage;
        glm::vec3 SplashBackground{ 0.043f, 0.051f, 0.067f };

        // ---- aspect ratio (a game made for one shape of screen)
        // On: the game's picture keeps AspectWidth : AspectHeight whatever the window's shape - bars of
        // LetterboxColor fill the rest (left and right, or top and bottom); the mouse and the UI work in the
        // picture. Off: the picture fills the window (its camera follows the window's shape).
        bool FixedAspectRatio = false;
        uint32_t AspectWidth = 16, AspectHeight = 9;
        glm::vec3 LetterboxColor{ 0.0f };

        // ---- icon
        // A PNG (relative to the assets) for the window and the exported game's desktop entry. Empty:
        // the system's default icon.
        std::filesystem::path Icon;

        // ---- log
        PlayerLogSettings Logging;

        static constexpr const char* DefaultVersion = "1.0.0";
        static constexpr uint32_t MinWindowSize = 160, MaxWindowSize = 7680;
        static constexpr int MaxTargetFrameRate = 1000, MinTargetFrameRate = 10;
        static constexpr float MinSplashDuration = 0.5f, MaxSplashDuration = 10.0f;
        static constexpr uint32_t MaxAspectSide = 64; // 21:9, 32:9, 4:3 ... (sides 1 ... 64)
        // The icon sizes made from the PNG (those not larger than it, at least the smallest).
        static constexpr uint32_t IconSizes[] = { 16, 32, 48, 64, 128, 256 };

        bool operator==(const PlayerSettings& other) const = default;
        bool IsDefault() const { return *this == PlayerSettings(); }
        // Sizes and the frame rate into their ranges (0 stays "no limit"), the text fields trimmed.
        void Sanitize();

        // ProductName, or the project's name.
        std::string GetProductName(std::string_view projectName) const;
        // Title, or the product name; with the version when VersionInTitle.
        std::string GetWindowTitle(std::string_view projectName) const;
        // The window to create (title, size, mode, vsync, resizable).
        WindowProps MakeWindowProps(std::string_view projectName) const;

        // Where the game's picture goes in a window of width x height pixels (GL viewport: X, Y from the
        // bottom-left): the whole window, or with FixedAspectRatio the largest centred rectangle of that
        // shape (whole pixels, at least 1 x 1).
        struct ViewportRect
        {
            uint32_t X = 0, Y = 0, Width = 0, Height = 0;
            bool Letterboxed = false; // there are bars

            bool operator==(const ViewportRect&) const = default;
            // A window point (pixels from the top-left) in the picture (pixels from its top-left; outside
            // it: outside 0 ... size).
            glm::vec2 WindowToViewport(const glm::vec2& point, uint32_t windowHeight) const;
        };
        ViewportRect ComputeViewport(uint32_t width, uint32_t height) const;

        // The "Player" section of a .zproject (a JSON object with every value, in a fixed order) and
        // back. FromJson keeps the defaults for missing / wrong values; false when the
        // text is not a JSON object.
        std::string ToJson() const;
        static bool FromJson(const std::string& text, PlayerSettings& out);
        // The keys ToJson may write (the serializer keeps any other key of the section as it was).
        static const std::vector<std::string>& GetKnownKeys();
    };

    namespace GameIcon {

        // Reads a PNG icon; false with the reason ("fayl topilmadi", "PNG emas" ...) in `error`.
        bool Load(const std::filesystem::path& file, ImageData& out, std::string* error = nullptr);
        // The window icon sizes (PlayerSettings::IconSizes up to the image's size; a non-square image
        // centred on a transparent square), RGBA8 rows top first. `image` must be RGBA8, rows top first.
        std::vector<WindowIconImage> MakeWindowImages(const ImageData& image);
        // An RGBA8 image (rows top first) scaled to size x size: centred on a transparent square, area
        // averaged (with alpha-weighted colours) when it gets smaller.
        WindowIconImage Resize(const ImageData& image, uint32_t size);
        // A warning about the icon for the settings window ("" when it is fine): not square, smaller
        // than 64 px ...
        std::string Check(const ImageData& image);

    }

    namespace ExportFiles {

        // A freedesktop.org desktop entry (<game>.desktop) for an exported game: its name (the product
        // name), a comment with the version and company, the launcher to run (quoted), the folder to run
        // it in and the icon (none when `icon` is empty). Copied to ~/.local/share/applications it puts
        // the game into the application menus.
        std::string MakeDesktopEntry(const std::string& productName, const PlayerSettings& player, const std::filesystem::path& exec,
                                     const std::filesystem::path& icon, const std::filesystem::path& workingDirectory);

        // The entry next to the exported launcher, made to work wherever the folder is moved to: its Exec
        // runs the launcher next to the entry file itself (`sh -c "..." sh %k` - %k is the entry's own path;
        // no Path key, the launcher goes to its folder). Only the icon is absolute (desktop entries take no
        // relative icon paths): where the game was exported to; after a move the launcher (MakeLauncherScript)
        // points it at the new place the first time the game starts there, install-desktop-entry.sh too.
        // `launcher`: the launcher's file name.
        std::string MakeRelocatableDesktopEntry(const std::string& productName, const PlayerSettings& player, const std::string& launcher,
                                                const std::filesystem::path& icon);
        // That entry's Exec value (before the entry's own string escaping).
        std::string MakeRelocatableExec(const std::string& launcher);
        // The exported game's launcher (<Game>.sh, POSIX sh): goes to its own folder and runs ./ZaminPlayer with
        // `projectFile` (relative to it, "Game/Kosmos.zproject") and its own arguments. Before that, when the
        // desktop entry next to it (`desktopEntry`, a file name) names an icon and `icon` (a file name) is
        // there, it points the entry's Icon line at that file in the folder it runs from - the entry's icon
        // follows the folder wherever it is moved (written only when it changed and the entry can be written).
        std::string MakeLauncherScript(const std::string& projectFile, const std::string& desktopEntry, const std::string& icon);

        // This POSIX shell script (install-desktop-entry.sh, next to the launcher) puts the game into the
        // application menus for the folder it is in when it runs - after the folder was moved, unzipped or
        // installed elsewhere: $XDG_DATA_HOME/applications/<id>.desktop (~/.local/share/applications) with
        // that folder's absolute paths, the icon set (icons/hicolor/<size>/apps/<id>.png, WriteIconSet) into
        // $XDG_DATA_HOME/icons/hicolor (the entry then names the icon by <id>, every size there), and the
        // relocatable entry next to the launcher again (its icon path for the new place). "--uninstall"
        // removes the installed entry and icons. `launcher` and `icon` are file names in that folder (the
        // icon is left out when neither the file nor an icon set is there).
        std::string MakeDesktopInstallScript(const std::string& productName, const PlayerSettings& player, const std::string& launcher,
                                             const std::string& icon, const std::string& id);
        // The game's icon in the sizes desktops look for: <directory>/icons/hicolor/<s>x<s>/apps/<id>.png for
        // every size GameIcon::MakeWindowImages makes of `image` (RGBA8, rows top first: up to its size, at
        // least 16). The sizes written (none when `image` is empty or a file cannot be written: `error`).
        std::vector<uint32_t> WriteIconSet(const ImageData& image, const std::filesystem::path& directory, std::string_view id,
                                           std::string* error = nullptr);
        // <directory>/icons/hicolor/<size>x<size>/apps/<id>.png
        std::filesystem::path GetIconSetPath(const std::filesystem::path& directory, std::string_view id, uint32_t size);
        // The installed entry's file name (without ".desktop") for a game: "zamin-" and the name with
        // letters, digits, '-', '_' and '.' kept, anything else '-'.
        std::string MakeDesktopEntryId(std::string_view name);
        // The install script's file name.
        inline constexpr const char* DesktopInstallScriptName = "install-desktop-entry.sh";

    }

}
