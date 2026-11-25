#include "Zamin/Project/PlayerLaunch.h"

#include "Zamin/Project/Project.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <format>

namespace Zamin {

    namespace {

        // A whole number (optionally negative) that is all of `text`.
        bool ParseInt(std::string_view text, int& out)
        {
            if (text.empty())
                return false;
            int value = 0;
            const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
            if (ec != std::errc() || end != text.data() + text.size())
                return false;
            out = value;
            return true;
        }

        bool InWindowRange(int value)
        {
            return value >= (int)PlayerSettings::MinWindowSize && value <= (int)PlayerSettings::MaxWindowSize;
        }

        const char* ModeName(WindowMode mode)
        {
            switch (mode)
            {
                case WindowMode::Windowed:   return "windowed";
                case WindowMode::Fullscreen: return "fullscreen";
                case WindowMode::Borderless: return "borderless fullscreen";
            }
            return "windowed";
        }

    }

    bool PlayerCommandLine::ParseResolution(std::string_view text, uint32_t& width, uint32_t& height)
    {
        const size_t split = text.find_first_of("xX*");
        if (split == std::string_view::npos)
            return false;
        int w = 0, h = 0;
        if (!ParseInt(text.substr(0, split), w) || !ParseInt(text.substr(split + 1), h) || !InWindowRange(w) || !InWindowRange(h))
            return false;
        width = (uint32_t)w;
        height = (uint32_t)h;
        return true;
    }

    PlayerCommandLine PlayerCommandLine::Parse(int argc, const char* const* argv)
    {
        std::vector<std::string> args;
        for (int i = 1; i < argc; i++)
            if (argv[i])
                args.emplace_back(argv[i]);
        return Parse(args);
    }

    PlayerCommandLine PlayerCommandLine::Parse(const std::vector<std::string>& args)
    {
        PlayerCommandLine c;
        // Options that take a value (the next argument, or after '=').
        static const char* const s_ValueOptions[] = { "--scene", "--resolution", "--width", "--height", "--monitor", "--fps", "--jobs",
                                                      "--quality", "--lang", "--language", "--gl", "--msaa", "--log-file", "--log-level",
                                                      "--crash-dialog", "--gamma", "--brightness", "--benchmark", "--benchmark-warmup",
                                                      "--benchmark-out", "--benchmark-max-avg", "--benchmark-max-p95" };
        for (size_t i = 0; i < args.size(); i++)
        {
            const std::string& arg = args[i];
            if (arg.empty())
                continue;
            if (arg[0] != '-' || arg == "-")
            {
                if (c.Project.empty())
                    c.Project = arg;
                else
                    c.Errors.push_back(std::format("extra argument: '{}' (the project is already: '{}')", arg, c.Project));
                continue;
            }
            std::string name = arg;
            std::optional<std::string> inlineValue;
            if (const size_t eq = arg.find('='); eq != std::string::npos && arg.starts_with("--"))
            {
                name = arg.substr(0, eq);
                inlineValue = arg.substr(eq + 1);
            }
            const bool takesValue = std::find(std::begin(s_ValueOptions), std::end(s_ValueOptions), name) != std::end(s_ValueOptions);
            if (takesValue)
            {
                std::string value;
                if (inlineValue)
                    value = *inlineValue;
                else if (i + 1 < args.size())
                    value = args[++i];
                else
                {
                    c.Errors.push_back(std::format("{}: a value is needed (--help)", name));
                    continue;
                }
                int number = 0;
                if (name == "--scene")
                    c.Scene = value;
                else if (name == "--quality")
                    c.Quality = value;
                else if (name == "--lang" || name == "--language")
                    c.Language = value;
                else if (name == "--gl")
                    c.GLVersion = value;
                else if (name == "--log-file")
                {
                    if (value.empty())
                        c.Errors.push_back("--log-file: a file path is needed");
                    else
                        c.LogFile = value;
                }
                else if (name == "--log-level")
                {
                    std::string error;
                    if (!LogLevelOverride::Parse(value, c.LogLevels, &error))
                        c.Errors.push_back(std::format("--log-level {}: {}", value, error));
                }
                else if (name == "--crash-dialog")
                    c.CrashDialog = value;
                else if (name == "--resolution")
                {
                    uint32_t w = 0, h = 0;
                    if (ParseResolution(value, w, h))
                    {
                        c.Width = w;
                        c.Height = h;
                    }
                    else
                        c.Errors.push_back(std::format("--resolution {}: WIDTHxHEIGHT is needed, for example 1920x1080 ({} ... {} px)", value,
                                                       PlayerSettings::MinWindowSize, PlayerSettings::MaxWindowSize));
                }
                else if (name == "--width" || name == "--height")
                {
                    if (ParseInt(value, number) && InWindowRange(number))
                        (name == "--width" ? c.Width : c.Height) = (uint32_t)number;
                    else
                        c.Errors.push_back(std::format("{} {}: a whole number in the range {} ... {} is needed", name, value, PlayerSettings::MinWindowSize,
                                                       PlayerSettings::MaxWindowSize));
                }
                else if (name == "--monitor")
                {
                    if (ParseInt(value, number) && number >= 0)
                        c.Monitor = number;
                    else
                        c.Errors.push_back(std::format("--monitor {}: a monitor number is needed (0 - the primary monitor)", value));
                }
                else if (name == "--fps")
                {
                    if (ParseInt(value, number) && number >= 0)
                        c.TargetFrameRate = number;
                    else
                        c.Errors.push_back(std::format("--fps {}: a frame count is needed (0 - unlimited)", value));
                }
                else if (name == "--jobs")
                {
                    if (ParseInt(value, number) && number >= 0)
                        c.Jobs = number;
                    else
                        c.Errors.push_back(std::format("--jobs {}: a worker thread count is needed (0 - off)", value));
                }
                else if (name == "--gamma" || name == "--brightness")
                {
                    // A number in the slider's range ("1.2", "1,2").
                    std::string text = value;
                    std::replace(text.begin(), text.end(), ',', '.');
                    char* end = nullptr;
                    const float number = std::strtof(text.c_str(), &end);
                    const bool gamma = name == "--gamma";
                    const float lo = gamma ? DisplayAdjustment::MinGamma : DisplayAdjustment::MinBrightness;
                    const float hi = gamma ? DisplayAdjustment::MaxGamma : DisplayAdjustment::MaxBrightness;
                    if (!text.empty() && end && *end == '\0' && std::isfinite(number) && number >= lo && number <= hi)
                        (gamma ? c.Gamma : c.Brightness) = number;
                    else
                        c.Errors.push_back(std::format("{} {}: a number in the range {:.1f} ... {:.1f} is needed (1 - unchanged)", name, value, lo, hi));
                }
                else if (name == "--benchmark" || name == "--benchmark-warmup")
                {
                    const bool frames = name == "--benchmark";
                    if (ParseInt(value, number) && number >= (frames ? 1 : 0) && number <= 1000000)
                        (frames ? c.Benchmark.Frames : c.Benchmark.Warmup) = number;
                    else
                        c.Errors.push_back(std::format("{} {}: a frame count is needed ({} ... 1000000)", name, value, frames ? 1 : 0));
                }
                else if (name == "--benchmark-out")
                {
                    if (value.empty())
                        c.Errors.push_back("--benchmark-out: a file path is needed");
                    else
                        c.Benchmark.Output = value;
                }
                else if (name == "--benchmark-max-avg" || name == "--benchmark-max-p95")
                {
                    std::string text = value;
                    std::replace(text.begin(), text.end(), ',', '.');
                    char* end = nullptr;
                    const double ms = std::strtod(text.c_str(), &end);
                    if (!text.empty() && end && *end == '\0' && std::isfinite(ms) && ms > 0.0)
                        (name == "--benchmark-max-avg" ? c.Benchmark.MaxAverageMs : c.Benchmark.MaxP95Ms) = ms;
                    else
                        c.Errors.push_back(std::format("{} {}: milliseconds are needed (for example 16.7)", name, value));
                }
                else if (name == "--msaa")
                {
                    if (ParseInt(value, number) && number >= 0)
                        c.MSAA = number;
                    else
                        c.Errors.push_back(std::format("--msaa {}: 0, 2, 4, 8 or 16 is needed", value));
                }
                continue;
            }
            if (inlineValue)
            {
                c.Errors.push_back(std::format("{}: this option takes no value", name));
                continue;
            }
            if (name == "--help" || name == "-h" || name == "-?")
                c.Help = true;
            else if (name == "--version" || name == "-v")
                c.Version = true;
            else if (name == "--windowed")
                c.Mode = WindowMode::Windowed;
            else if (name == "--fullscreen")
                c.Mode = WindowMode::Fullscreen;
            else if (name == "--borderless")
                c.Mode = WindowMode::Borderless;
            else if (name == "--vsync")
                c.VSync = true;
            else if (name == "--no-vsync")
                c.VSync = false;
            else if (name == "--no-splash")
                c.NoSplash = true;
            else if (name == "--no-log-file")
                c.NoLogFile = true;
            else if (name == "--no-crash-handler")
                c.NoCrashHandler = true;
            else if (name == "--no-crash-dialog")
                c.NoCrashDialog = true;
            else if (name == "--no-debug-draw")
                c.NoDebugDraw = true;
            else if (name == "--debug-raycasts")
                c.DebugRaycasts = true;
            else if (name == "--debug-contacts")
                c.DebugContacts = true;
            else if (name == "--debug-contact-details")
                c.DebugContactDetails = true;
            else if (name == "--debug-joints")
                c.DebugJoints = true;
            else if (name == "--debug-velocities")
                c.DebugVelocities = true;
            else if (name == "--debug-nav")
                c.DebugNavigation = true;
            else if (name == "--debug-nav-cells")
                c.DebugNavigation = c.DebugNavigationCells = true;
            else if (name == "--gl-debug")
                c.GLDebug = 1;
            else if (name == "--no-gl-debug")
                c.GLDebug = 0;
            else if (name == "--srgb")
                c.SRGB = 1;
            else if (name == "--no-srgb")
                c.SRGB = 0;
            else
                c.Errors.push_back(std::format("unknown option: {} (all of them: --help)", name));
        }
        return c;
    }

    std::string PlayerCommandLine::GetHelpText(std::string_view program)
    {
        return std::format(
            "Zamin Player - runs a game.\n"
            "\n"
            "Usage: {0} [Game.zproject | project folder] [options]\n"
            "Without a project the .zproject in the Game/ folder next to the player is opened (an exported game).\n"
            "Options with a value can be written two ways: --fps 60 or --fps=60.\n"
            "\n"
            "Window:\n"
            "  --windowed                    windowed mode\n"
            "  --fullscreen                  fullscreen (exclusive, the monitor's video mode)\n"
            "  --borderless                  borderless fullscreen (a window the size of the monitor)\n"
            "  --resolution WxH              window size, for example 1920x1080 (with --fullscreen the video mode too)\n"
            "  --width W, --height H         one side of the size\n"
            "  --monitor N                   which monitor to open on (0 - primary)\n"
            "  --vsync, --no-vsync           vertical sync\n"
            "  --fps N                       frame rate limit (0 - unlimited)\n"
            "  F11 or Alt+Enter              in the game: fullscreen <-> window (Project settings > Player)\n"
            "\n"
            "Game:\n"
            "  --scene <path | index | name> start scene (Scenes/Level.zscene, a build index or a build scene name)\n"
            "  --quality <name | index>      quality level (Low, Medium, High ...)\n"
            "  --lang <code>                 language (uz, ru, en ...); --language works too\n"
            "  --no-splash                   no splash screen (the Zamin logo)\n"
            "  --gamma G                     gamma 0.5 ... 2.0 (1 - unchanged, larger - brighter midtones)\n"
            "  --brightness B                brightness 0.5 ... 1.5 (the whole image is multiplied)\n"
            "\n"
            "Graphics:\n"
            "  --gl <4.1 | 4.3 | 4.5 | 4.6>  OpenGL version\n"
            "  --msaa <0 | 2 | 4 | 8 | 16>   MSAA of the window buffer\n"
            "  --gl-debug, --no-gl-debug     OpenGL debug context\n"
            "  --srgb, --no-srgb             sRGB window buffer\n"
            "\n"
            "Performance:\n"
            "  --jobs N                      worker threads of the job system (0 - off, everything on the main thread;\n"
            "                                default: processor threads - 1; also the ZAMIN_JOBS environment variable)\n"
            "\n"
            "Debug draw (not in a Shipping build):\n"
            "  --no-debug-draw               turn debug draw off\n"
            "  --debug-raycasts, --debug-contacts, --debug-contact-details, --debug-joints, --debug-velocities\n"
            "  --debug-nav                   navigation: grids, agent paths, sight cones, patrol paths\n"
            "  --debug-nav-cells             those and the walkable cells too (green)\n"
            "\n"
            "Log and errors:\n"
            "  --log-file <file>             write the log to this file (default: ~/.local/share/<company>/<game>/logs/\n"
            "                                player-<date>.log, the last 5 are kept, latest.log - the last one)\n"
            "  --no-log-file                 no log file (terminal only)\n"
            "  --log-level <level>           log levels: trace, info, warn, error, critical, off - for both loggers,\n"
            "                                or separately: engine=warn,app=info,file=error (file - what goes to the log file);\n"
            "                                wins over the project settings and ZAMIN_LOG_LEVEL\n"
            "  --no-crash-handler            no report is written on a crash (for a debugger, a core dump)\n"
            "  --no-crash-dialog             the crash report is written but no message window is shown\n"
            "\n"
            "Benchmark:\n"
            "  --benchmark N                 measure N frames instead of playing (no splash, FPS limit or VSync) - average,\n"
            "                                median, p95, p99 ms; exit code: 0 - passed, 3 - over the limit, 2 - did not run\n"
            "  --benchmark-warmup N          warm-up frames before measuring (default: {1})\n"
            "  --benchmark-out <file.json>   the result into a JSON file (every frame time too)\n"
            "  --benchmark-max-avg MS        limit for the average frame time\n"
            "  --benchmark-max-p95 MS        limit for the 95th percentile\n"
            "\n"
            "Other:\n"
            "  -v, --version                 game and engine version (commit, build date)\n"
            "  -h, --help                    this help\n"
            "\n"
            "Priority: command line > settings saved by the player (the game's menu: resolution, fullscreen, VSync, gamma, brightness, quality, language)\n"
            "          > project settings (Project settings > Player, Graphics).\n",
            program, PlayerBenchmarkSettings::DefaultWarmup);
    }

    const char* PlayerSettingSourceToString(PlayerSettingSource source)
    {
        switch (source)
        {
            case PlayerSettingSource::Project:     return "project";
            case PlayerSettingSource::Saved:       return "saved";
            case PlayerSettingSource::CommandLine: return "command line";
        }
        return "project";
    }

    PlayerLaunchSettings PlayerLaunchSettings::Resolve(const ProjectConfig& config, const SavedDisplaySettings& saved, const PlayerCommandLine& c)
    {
        using Source = PlayerSettingSource;
        PlayerLaunchSettings r;
        PlayerSettings player = config.Player;
        player.Sanitize();
        r.Window = player.MakeWindowProps(config.Name);

        // The windowed size: the command line, else the resolution the player chose in the game's options menu
        // (Display::SetResolution, saved), else the project.
        if (c.Width > 0)
            r.Window.Width = std::clamp(c.Width, PlayerSettings::MinWindowSize, PlayerSettings::MaxWindowSize);
        if (c.Height > 0)
            r.Window.Height = std::clamp(c.Height, PlayerSettings::MinWindowSize, PlayerSettings::MaxWindowSize);
        if (c.Width > 0 || c.Height > 0)
            r.SizeSource = Source::CommandLine;
        else if (saved.Resolution && saved.Resolution->IsValid())
        {
            r.Window.Width = std::clamp(saved.Resolution->Width, PlayerSettings::MinWindowSize, PlayerSettings::MaxWindowSize);
            r.Window.Height = std::clamp(saved.Resolution->Height, PlayerSettings::MinWindowSize, PlayerSettings::MaxWindowSize);
            r.SizeSource = Source::Saved;
        }

        // The mode: the command line, else the player's saved choice (fullscreen in the project's kind of
        // fullscreen), else the project.
        if (c.Mode)
        {
            r.Window.Mode = *c.Mode;
            if (*c.Mode != WindowMode::Windowed)
                r.Window.FullscreenMode = *c.Mode; // what F11 / Display::SetFullscreen switch to as well
            r.ModeSource = Source::CommandLine;
        }
        else if (saved.Fullscreen)
        {
            r.Window.Mode = *saved.Fullscreen ? r.Window.FullscreenMode : WindowMode::Windowed;
            r.ModeSource = Source::Saved;
        }
        // An exclusive fullscreen start with an asked (or saved) resolution: that video mode.
        if (r.Window.Mode == WindowMode::Fullscreen && ((c.Width > 0 && c.Height > 0) || r.SizeSource == Source::Saved))
        {
            r.Window.FullscreenWidth = r.Window.Width;
            r.Window.FullscreenHeight = r.Window.Height;
        }

        if (c.VSync)
        {
            r.Window.VSync = *c.VSync;
            r.VSyncSource = Source::CommandLine;
        }
        else if (saved.VSync)
        {
            r.Window.VSync = *saved.VSync;
            r.VSyncSource = Source::Saved;
        }

        if (c.Monitor)
        {
            r.Window.Monitor = *c.Monitor;
            r.MonitorSource = Source::CommandLine;
        }

        r.TargetFrameRate = player.TargetFrameRate;
        if (c.TargetFrameRate)
        {
            const int fps = *c.TargetFrameRate;
            r.TargetFrameRate = fps <= 0 ? 0 : std::clamp(fps, PlayerSettings::MinTargetFrameRate, PlayerSettings::MaxTargetFrameRate);
            r.FrameRateSource = Source::CommandLine;
        }

        r.Splash = player.SplashScreen;
        if (c.NoSplash)
        {
            r.Splash = false;
            r.SplashSource = Source::CommandLine;
        }

        // The performance test runs as fast as it can: no splash, no frame limit and no vsync - unless the
        // command line itself asks for them (--fps, --vsync).
        if (c.Benchmark.IsEnabled())
        {
            r.Splash = false;
            r.SplashSource = Source::CommandLine;
            if (!c.TargetFrameRate)
            {
                r.TargetFrameRate = 0;
                r.FrameRateSource = Source::CommandLine;
            }
            if (!c.VSync)
            {
                r.Window.VSync = false;
                r.VSyncSource = Source::CommandLine;
            }
        }

        // The OpenGL context: Project settings > Graphics, then the command line.
        GraphicsSettings& graphics = r.Window.Graphics;
        graphics = config.Graphics;
        if (!c.GLVersion.empty() && !GraphicsSettings::ParseVersion(c.GLVersion, graphics.GLMajor, graphics.GLMinor))
            r.Warnings.push_back(std::format("--gl {}: there is no such OpenGL version (available: 4.1, 4.3, 4.5, 4.6) - the project's stays", c.GLVersion));
        if (c.MSAA >= 0)
            graphics.MSAASamples = GraphicsSettings::SanitizeSamples(c.MSAA);
        if (c.GLDebug >= 0)
            graphics.DebugContext = c.GLDebug ? GraphicsDebugContext::On : GraphicsDebugContext::Off;
        if (c.SRGB >= 0)
            graphics.SRGBFramebuffer = c.SRGB == 1;
        graphics.Sanitize();
        return r;
    }

    std::string PlayerLaunchSettings::Describe() const
    {
        auto from = [](PlayerSettingSource source) { return PlayerSettingSourceToString(source); };
        return std::format("{}x{} ({}), {} ({}), VSync: {} ({}), FPS: {} ({}), monitor: {} ({}), splash: {} ({})", Window.Width, Window.Height,
                           from(SizeSource), ModeName(Window.Mode), from(ModeSource), Window.VSync ? "yes" : "no", from(VSyncSource),
                           TargetFrameRate > 0 ? std::to_string(TargetFrameRate) : std::string("unlimited"), from(FrameRateSource),
                           Window.Monitor >= 0 ? std::to_string(Window.Monitor) : std::string("primary"), from(MonitorSource),
                           Splash ? "yes" : "no", from(SplashSource));
    }

}
