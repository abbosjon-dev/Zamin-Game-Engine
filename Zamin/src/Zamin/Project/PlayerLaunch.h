#pragma once

#include "Zamin/Core/Display.h"
#include "Zamin/Core/LogSettings.h"
#include "Zamin/Core/Window.h"
#include "Zamin/Project/PlayerBenchmark.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Zamin {

    struct ProjectConfig;

    // What ZaminPlayer's command line asked for (ZaminPlayer --help lists it). Options that were not
    // given stay unset: the player's saved settings, then the project's, are used for them. Values
    // follow their option ("--fps 60") or an '=' ("--fps=60").
    struct PlayerCommandLine
    {
        std::string Project;                // the .zproject (or its folder): the argument that is not an option
        std::string Scene;                  // --scene <path | build index | build scene name>
        std::optional<WindowMode> Mode;     // --windowed | --fullscreen | --borderless (the last one given)
        uint32_t Width = 0, Height = 0;     // --resolution WxH, --width W, --height H (0: not given)
        std::optional<int> Monitor;         // --monitor N (0: the primary monitor)
        std::optional<bool> VSync;          // --vsync | --no-vsync
        std::optional<int> TargetFrameRate; // --fps N (0: no limit)
        std::optional<int> Jobs;            // --jobs N: the job system's worker threads (0: off, all work on the main thread)
        std::string Quality;                // --quality <level name | index>
        std::string Language;               // --lang | --language <code>
        bool NoSplash = false;              // --no-splash
        // --gamma G, --brightness B: the picture (Display::SetGamma / SetBrightness) over what was saved.
        std::optional<float> Gamma, Brightness;
        bool Help = false;                  // --help | -h | -?
        // --version | -v: the game's and the engine's versions (the commit, the build's date) and nothing else.
        bool Version = false;
        // --benchmark N [--benchmark-warmup W] [--benchmark-out <file.json>] [--benchmark-max-avg MS]
        // [--benchmark-max-p95 MS]: the performance test (PlayerBenchmark) instead of playing; no splash,
        // no frame rate limit and no vsync unless the command line asks for them.
        PlayerBenchmarkSettings Benchmark;
        // The log file: --log-file <path> writes only that file (else the rotating logs in
        // ~/.local/share/<company>/<product>/logs); --no-log-file none.
        std::string LogFile;
        bool NoLogFile = false;
        // --log-level <spec>: the loggers' / the log file's levels over the project's and ZAMIN_LOG_LEVEL
        // ("warn", "engine=error,app=info,file=warn"; LogLevelOverride).
        LogLevelOverride LogLevels;
        // Crashes: --no-crash-handler leaves fatal signals to the system (a debugger, core dumps);
        // --no-crash-dialog writes the report without showing a window.
        bool NoCrashHandler = false, NoCrashDialog = false;
        // --crash-dialog <report>: only the window about a crash report (the crash handler starts the
        // player like this after the game crashed); not listed in --help.
        std::string CrashDialog;
        // Debug drawing (compiled out of Shipping builds).
        bool NoDebugDraw = false, DebugRaycasts = false, DebugContacts = false, DebugContactDetails = false, DebugJoints = false,
             DebugVelocities = false;
        // --debug-nav: the navigation overlays (grids, agents' paths, sight cones, patrol rounds);
        // --debug-nav-cells: the walkable cells too.
        bool DebugNavigation = false, DebugNavigationCells = false;
        // The OpenGL context (wins over Project settings > Graphics).
        std::string GLVersion;              // --gl 4.1 | 4.3 | 4.5 | 4.6
        int MSAA = -1;                      // --msaa 0 | 2 | 4 | 8 | 16 (-1: not given)
        int GLDebug = -1, SRGB = -1;        // --gl-debug / --no-gl-debug, --srgb / --no-srgb: 1 / 0 (-1: not given)
        // What could not be understood (unknown options, missing / wrong values), a message each (the
        // player logs them as warnings and uses the rest).
        std::vector<std::string> Errors;

        // `args` without the program's name.
        static PlayerCommandLine Parse(const std::vector<std::string>& args);
        // argv[0] is the program.
        static PlayerCommandLine Parse(int argc, const char* const* argv);
        // --help: every option, grouped, and the precedence of the settings.
        static std::string GetHelpText(std::string_view program = "ZaminPlayer");
        // "1920x1080" ('x', 'X' or '*' between) -> 1920, 1080; false for anything else or sizes outside
        // PlayerSettings' window size range.
        static bool ParseResolution(std::string_view text, uint32_t& width, uint32_t& height);
    };

    // Where a resolved player setting came from (the log says it).
    enum class PlayerSettingSource { Project = 0, Saved, CommandLine };
    const char* PlayerSettingSourceToString(PlayerSettingSource source); // "loyiha", "saqlangan", "buyruq qatori"

    // The player's window, frame rate and splash screen, each value from the strongest place that has it:
    //     the command line  >  what the player saved in the game's options menu (Display::SaveSettings:
    //     fullscreen, vsync)  >  the project (Project settings > Player, > Graphics).
    // Resolved before the window is made, so a game the player left fullscreen opens fullscreen at once.
    struct PlayerLaunchSettings
    {
        WindowProps Window;
        int TargetFrameRate = 0;
        bool Splash = true;
        PlayerSettingSource SizeSource = PlayerSettingSource::Project, ModeSource = PlayerSettingSource::Project,
                            VSyncSource = PlayerSettingSource::Project, FrameRateSource = PlayerSettingSource::Project,
                            MonitorSource = PlayerSettingSource::Project, SplashSource = PlayerSettingSource::Project;
        // Values of the command line that were not usable (an unknown OpenGL version ...).
        std::vector<std::string> Warnings;

        static PlayerLaunchSettings Resolve(const ProjectConfig& config, const SavedDisplaySettings& saved, const PlayerCommandLine& commandLine);
        // One line for the log: "1280x720 (loyiha), to'liq ekran (saqlangan), VSync: ha (loyiha), ...".
        std::string Describe() const;
    };

}
