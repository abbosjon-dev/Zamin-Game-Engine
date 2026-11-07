#pragma once

#include "Zamin/Core/Window.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Zamin {

    struct ImageData;

    // The game as the player runs it (Project settings > Player): who made it and its version, its
    // window and its icon. Projects without a "Player" section get the defaults - a 1280x720 resizable
    // window with vsync, titled with the project's name, unlimited frame rate, running in the background
    // and no icon: what the player always did. ZaminPlayer applies them when it creates its window; its
    // command line (--fullscreen, --windowed, --width / --height, --no-vsync) and the display options the
    // game saved (Display::SaveSettings) win over them.
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

        // ---- icon
        // A PNG (relative to the assets) for the window and the exported game's desktop entry. Empty:
        // the system's default icon.
        std::filesystem::path Icon;

        static constexpr const char* DefaultVersion = "1.0.0";
        static constexpr uint32_t MinWindowSize = 160, MaxWindowSize = 7680;
        static constexpr int MaxTargetFrameRate = 1000, MinTargetFrameRate = 10;
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

    }

}
