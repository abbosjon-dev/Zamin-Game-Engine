#include "Zamin/Project/PlayerLaunch.h"

#include "Zamin/Project/Project.h"

#include <algorithm>
#include <charconv>
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
                case WindowMode::Windowed:   return "oynali";
                case WindowMode::Fullscreen: return "to'liq ekran";
                case WindowMode::Borderless: return "chegarasiz to'liq ekran";
            }
            return "oynali";
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
        static const char* const s_ValueOptions[] = { "--scene", "--resolution", "--width", "--height", "--monitor", "--fps",
                                                      "--quality", "--lang", "--language", "--gl", "--msaa" };
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
                    c.Errors.push_back(std::format("ortiqcha argument: '{}' (loyiha allaqachon: '{}')", arg, c.Project));
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
                    c.Errors.push_back(std::format("{}: qiymat kerak (--help)", name));
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
                else if (name == "--resolution")
                {
                    uint32_t w = 0, h = 0;
                    if (ParseResolution(value, w, h))
                    {
                        c.Width = w;
                        c.Height = h;
                    }
                    else
                        c.Errors.push_back(std::format("--resolution {}: KENGLIKxBALANDLIK kerak, masalan 1920x1080 ({} ... {} px)", value,
                                                       PlayerSettings::MinWindowSize, PlayerSettings::MaxWindowSize));
                }
                else if (name == "--width" || name == "--height")
                {
                    if (ParseInt(value, number) && InWindowRange(number))
                        (name == "--width" ? c.Width : c.Height) = (uint32_t)number;
                    else
                        c.Errors.push_back(std::format("{} {}: {} ... {} oralig'idagi butun son kerak", name, value, PlayerSettings::MinWindowSize,
                                                       PlayerSettings::MaxWindowSize));
                }
                else if (name == "--monitor")
                {
                    if (ParseInt(value, number) && number >= 0)
                        c.Monitor = number;
                    else
                        c.Errors.push_back(std::format("--monitor {}: monitor raqami kerak (0 - asosiy monitor)", value));
                }
                else if (name == "--fps")
                {
                    if (ParseInt(value, number) && number >= 0)
                        c.TargetFrameRate = number;
                    else
                        c.Errors.push_back(std::format("--fps {}: kadr soni kerak (0 - cheklovsiz)", value));
                }
                else if (name == "--msaa")
                {
                    if (ParseInt(value, number) && number >= 0)
                        c.MSAA = number;
                    else
                        c.Errors.push_back(std::format("--msaa {}: 0, 2, 4, 8 yoki 16 kerak", value));
                }
                continue;
            }
            if (inlineValue)
            {
                c.Errors.push_back(std::format("{}: bu variant qiymat olmaydi", name));
                continue;
            }
            if (name == "--help" || name == "-h" || name == "-?")
                c.Help = true;
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
            else if (name == "--gl-debug")
                c.GLDebug = 1;
            else if (name == "--no-gl-debug")
                c.GLDebug = 0;
            else if (name == "--srgb")
                c.SRGB = 1;
            else if (name == "--no-srgb")
                c.SRGB = 0;
            else
                c.Errors.push_back(std::format("noma'lum variant: {} (hammasi: --help)", name));
        }
        return c;
    }

    std::string PlayerCommandLine::GetHelpText(std::string_view program)
    {
        return std::format(
            "Zamin Player - o'yinni ishga tushiradi.\n"
            "\n"
            "Foydalanish: {0} [O'yin.zproject | loyiha papkasi] [variantlar]\n"
            "Loyiha ko'rsatilmasa, player yonidagi Game/ papkadagi .zproject ochiladi (eksport qilingan o'yin).\n"
            "Qiymatli variantlar ikki xil yoziladi: --fps 60 yoki --fps=60.\n"
            "\n"
            "Oyna:\n"
            "  --windowed                    oynali rejim\n"
            "  --fullscreen                  to'liq ekran (eksklyuziv, monitorning video rejimi)\n"
            "  --borderless                  chegarasiz to'liq ekran (monitor o'lchamidagi oyna)\n"
            "  --resolution KxB              oyna o'lchami, masalan 1920x1080 (--fullscreen bilan video rejim ham)\n"
            "  --width K, --height B         o'lchamning bir tomoni\n"
            "  --monitor N                   qaysi monitorda ochiladi (0 - asosiy)\n"
            "  --vsync, --no-vsync           vertikal sinxronizatsiya\n"
            "  --fps N                       kadr chastotasi cheklovi (0 - cheklovsiz)\n"
            "  F11 yoki Alt+Enter            o'yin ichida: to'liq ekran <-> oyna (Project settings > Player)\n"
            "\n"
            "O'yin:\n"
            "  --scene <yo'l | indeks | nom> boshlang'ich sahna (Scenes/Level.zscene, build indeksi yoki build sahna nomi)\n"
            "  --quality <nom | indeks>      sifat darajasi (Low, Medium, High ...)\n"
            "  --lang <kod>                  til (uz, ru, en ...); --language ham bo'ladi\n"
            "  --no-splash                   splash ekransiz (Zamin logotipi)\n"
            "\n"
            "Grafika:\n"
            "  --gl <4.1 | 4.3 | 4.5 | 4.6>  OpenGL versiyasi\n"
            "  --msaa <0 | 2 | 4 | 8 | 16>   oyna buferining MSAA'si\n"
            "  --gl-debug, --no-gl-debug     OpenGL debug konteksti\n"
            "  --srgb, --no-srgb             sRGB oyna buferi\n"
            "\n"
            "Debug chizish (Shipping build'da yo'q):\n"
            "  --no-debug-draw               debug chizishni o'chirish\n"
            "  --debug-raycasts, --debug-contacts, --debug-contact-details, --debug-joints, --debug-velocities\n"
            "\n"
            "Boshqa:\n"
            "  -h, --help                    shu yordam\n"
            "\n"
            "Ustunlik: buyruq qatori > o'yinchi saqlagan sozlamalar (o'yin menyusi: to'liq ekran, VSync, sifat, til)\n"
            "          > loyiha sozlamalari (Project settings > Player, Graphics).\n",
            program);
    }

    const char* PlayerSettingSourceToString(PlayerSettingSource source)
    {
        switch (source)
        {
            case PlayerSettingSource::Project:     return "loyiha";
            case PlayerSettingSource::Saved:       return "saqlangan";
            case PlayerSettingSource::CommandLine: return "buyruq qatori";
        }
        return "loyiha";
    }

    PlayerLaunchSettings PlayerLaunchSettings::Resolve(const ProjectConfig& config, const SavedDisplaySettings& saved, const PlayerCommandLine& c)
    {
        using Source = PlayerSettingSource;
        PlayerLaunchSettings r;
        PlayerSettings player = config.Player;
        player.Sanitize();
        r.Window = player.MakeWindowProps(config.Name);

        // The windowed size: the command line, else the project.
        if (c.Width > 0)
            r.Window.Width = std::clamp(c.Width, PlayerSettings::MinWindowSize, PlayerSettings::MaxWindowSize);
        if (c.Height > 0)
            r.Window.Height = std::clamp(c.Height, PlayerSettings::MinWindowSize, PlayerSettings::MaxWindowSize);
        if (c.Width > 0 || c.Height > 0)
            r.SizeSource = Source::CommandLine;

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
        // An exclusive fullscreen start with an asked resolution: that video mode.
        if (r.Window.Mode == WindowMode::Fullscreen && c.Width > 0 && c.Height > 0)
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

        // The OpenGL context: Project settings > Graphics, then the command line.
        GraphicsSettings& graphics = r.Window.Graphics;
        graphics = config.Graphics;
        if (!c.GLVersion.empty() && !GraphicsSettings::ParseVersion(c.GLVersion, graphics.GLMajor, graphics.GLMinor))
            r.Warnings.push_back(std::format("--gl {}: bunday OpenGL versiyasi yo'q (bor: 4.1, 4.3, 4.5, 4.6) - loyihaniki qoldi", c.GLVersion));
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
                           from(SizeSource), ModeName(Window.Mode), from(ModeSource), Window.VSync ? "ha" : "yo'q", from(VSyncSource),
                           TargetFrameRate > 0 ? std::to_string(TargetFrameRate) : std::string("cheklovsiz"), from(FrameRateSource),
                           Window.Monitor >= 0 ? std::to_string(Window.Monitor) : std::string("asosiy"), from(MonitorSource),
                           Splash ? "ha" : "yo'q", from(SplashSource));
    }

}
