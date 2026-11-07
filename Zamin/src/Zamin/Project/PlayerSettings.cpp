#include "Zamin/Project/PlayerSettings.h"
#include "Zamin/Utils/StringUtils.h"

#include "Zamin/Utils/Image.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>

namespace Zamin {

    using ordered_json = nlohmann::ordered_json;

    namespace {

        std::string Trim(std::string_view s)
        {
            return std::string(StringUtils::Trim(s));
        }

    }

    void PlayerSettings::Sanitize()
    {
        Width = std::clamp(Width, MinWindowSize, MaxWindowSize);
        Height = std::clamp(Height, MinWindowSize, MaxWindowSize);
        TargetFrameRate = TargetFrameRate <= 0 ? 0 : std::clamp(TargetFrameRate, MinTargetFrameRate, MaxTargetFrameRate);
        if (Mode != WindowMode::Windowed && Mode != WindowMode::Fullscreen && Mode != WindowMode::Borderless)
            Mode = WindowMode::Windowed;
        CompanyName = Trim(CompanyName);
        ProductName = Trim(ProductName);
        Title = Trim(Title);
        Version = Trim(Version);
        if (Version.empty())
            Version = DefaultVersion;
        SplashDuration = std::isfinite(SplashDuration) ? std::clamp(SplashDuration, MinSplashDuration, MaxSplashDuration) : 2.0f;
        for (int i = 0; i < 3; i++)
            SplashBackground[i] = std::isfinite(SplashBackground[i]) ? std::clamp(SplashBackground[i], 0.0f, 1.0f) : 0.0f;
        AspectWidth = std::clamp(AspectWidth, 1u, MaxAspectSide);
        AspectHeight = std::clamp(AspectHeight, 1u, MaxAspectSide);
        for (int i = 0; i < 3; i++)
            LetterboxColor[i] = std::isfinite(LetterboxColor[i]) ? std::clamp(LetterboxColor[i], 0.0f, 1.0f) : 0.0f;
        Logging.KeepFiles = std::clamp(Logging.KeepFiles, PlayerLogSettings::MinKeepFiles, PlayerLogSettings::MaxKeepFiles);
    }

    PlayerSettings::ViewportRect PlayerSettings::ComputeViewport(uint32_t width, uint32_t height) const
    {
        ViewportRect rect;
        rect.Width = std::max(width, 1u);
        rect.Height = std::max(height, 1u);
        if (!FixedAspectRatio || AspectWidth == 0 || AspectHeight == 0 || width == 0 || height == 0)
            return rect;
        const double aspect = (double)AspectWidth / (double)AspectHeight;
        if ((double)width / (double)height > aspect)
        {
            // Wider than the game: bars left and right.
            rect.Width = (uint32_t)std::clamp(std::lround((double)height * aspect), 1l, (long)width);
            rect.X = (width - rect.Width) / 2;
        }
        else
        {
            // Taller: bars at the top and the bottom.
            rect.Height = (uint32_t)std::clamp(std::lround((double)width / aspect), 1l, (long)height);
            rect.Y = (height - rect.Height) / 2;
        }
        rect.Letterboxed = rect.Width != width || rect.Height != height;
        return rect;
    }

    glm::vec2 PlayerSettings::ViewportRect::WindowToViewport(const glm::vec2& point, uint32_t windowHeight) const
    {
        // The picture's top edge, from the window's top.
        const float top = (float)windowHeight - (float)(Y + Height);
        return { point.x - (float)X, point.y - top };
    }

    std::string PlayerSettings::GetProductName(std::string_view projectName) const
    {
        return ProductName.empty() ? std::string(projectName) : ProductName;
    }

    std::string PlayerSettings::GetWindowTitle(std::string_view projectName) const
    {
        std::string title = Title.empty() ? GetProductName(projectName) : Title;
        if (VersionInTitle && !Version.empty())
            title += (title.empty() ? "" : " ") + Version;
        return title;
    }

    WindowProps PlayerSettings::MakeWindowProps(std::string_view projectName) const
    {
        WindowProps props;
        props.Title = GetWindowTitle(projectName);
        props.Width = Width;
        props.Height = Height;
        props.VSync = VSync;
        props.Mode = Mode;
        props.FullscreenMode = Mode == WindowMode::Borderless ? WindowMode::Borderless : WindowMode::Fullscreen;
        props.Resizable = Resizable;
        return props;
    }

    const std::vector<std::string>& PlayerSettings::GetKnownKeys()
    {
        static const std::vector<std::string> s_Keys = { "Company", "Product", "Version", "Width", "Height", "WindowMode", "Resizable", "VSync",
                                                         "Title", "VersionInTitle", "TargetFrameRate", "RunInBackground", "FullscreenToggle", "Icon",
                                                         "Splash", "Aspect", "Log" };
        return s_Keys;
    }

    std::string PlayerSettings::ToJson() const
    {
        ordered_json j;
        j["Company"] = CompanyName;
        j["Product"] = ProductName;
        j["Version"] = Version;
        j["Width"] = Width;
        j["Height"] = Height;
        j["WindowMode"] = WindowModeToString(Mode);
        j["Resizable"] = Resizable;
        j["VSync"] = VSync;
        j["Title"] = Title;
        j["VersionInTitle"] = VersionInTitle;
        j["TargetFrameRate"] = TargetFrameRate;
        j["RunInBackground"] = RunInBackground;
        j["FullscreenToggle"] = FullscreenToggle;
        j["Icon"] = Icon.generic_string();
        ordered_json splash;
        splash["Enabled"] = SplashScreen;
        // (Short decimals: "0.043", not the float's "0.0430000014603138".)
        auto rounded = [](float value, double steps) { return std::round((double)value * steps) / steps; };
        splash["Duration"] = rounded(SplashDuration, 100.0);
        splash["Skippable"] = SplashSkippable;
        splash["Image"] = SplashImage.generic_string();
        splash["Background"] = { rounded(SplashBackground.r, 1000.0), rounded(SplashBackground.g, 1000.0), rounded(SplashBackground.b, 1000.0) };
        j["Splash"] = std::move(splash);
        ordered_json aspect;
        aspect["Fixed"] = FixedAspectRatio;
        aspect["Width"] = AspectWidth;
        aspect["Height"] = AspectHeight;
        aspect["BarColor"] = { rounded(LetterboxColor.r, 1000.0), rounded(LetterboxColor.g, 1000.0), rounded(LetterboxColor.b, 1000.0) };
        j["Aspect"] = std::move(aspect);
        // (Only when changed: projects that never touched it keep their files as they were.)
        if (!(Logging == PlayerLogSettings()))
        {
            ordered_json log;
            log["Engine"] = Log::LevelToName(Logging.Levels.Engine);
            log["App"] = Log::LevelToName(Logging.Levels.App);
            log["File"] = Log::LevelToName(Logging.Levels.File);
            log["WriteFile"] = Logging.WriteFile;
            log["Keep"] = Logging.KeepFiles;
            j["Log"] = std::move(log);
        }
        return j.dump(4);
    }

    bool PlayerSettings::FromJson(const std::string& text, PlayerSettings& out)
    {
        ordered_json j = ordered_json::parse(text, nullptr, false);
        if (j.is_discarded() || !j.is_object())
            return false;
        PlayerSettings s;
        auto readString = [&](const char* key, std::string& value) {
            if (auto it = j.find(key); it != j.end() && it->is_string()) value = it->get<std::string>();
        };
        auto readBool = [&](const char* key, bool& value) {
            if (auto it = j.find(key); it != j.end() && it->is_boolean()) value = it->get<bool>();
        };
        auto readSize = [&](const char* key, uint32_t& value) {
            if (auto it = j.find(key); it != j.end() && it->is_number())
                value = (uint32_t)std::clamp(it->get<double>(), 0.0, (double)MaxWindowSize);
        };
        readString("Company", s.CompanyName);
        readString("Product", s.ProductName);
        readString("Version", s.Version);
        readSize("Width", s.Width);
        readSize("Height", s.Height);
        if (auto it = j.find("WindowMode"); it != j.end() && it->is_string())
            WindowModeFromString(it->get<std::string>(), s.Mode);
        readBool("Resizable", s.Resizable);
        readBool("VSync", s.VSync);
        readString("Title", s.Title);
        readBool("VersionInTitle", s.VersionInTitle);
        if (auto it = j.find("TargetFrameRate"); it != j.end() && it->is_number())
            s.TargetFrameRate = (int)std::clamp(it->get<double>(), 0.0, (double)MaxTargetFrameRate);
        readBool("RunInBackground", s.RunInBackground);
        readBool("FullscreenToggle", s.FullscreenToggle);
        std::string icon;
        readString("Icon", icon);
        s.Icon = icon;
        if (auto splash = j.find("Splash"); splash != j.end() && splash->is_object())
        {
            if (auto it = splash->find("Enabled"); it != splash->end() && it->is_boolean()) s.SplashScreen = it->get<bool>();
            if (auto it = splash->find("Duration"); it != splash->end() && it->is_number()) s.SplashDuration = it->get<float>();
            if (auto it = splash->find("Skippable"); it != splash->end() && it->is_boolean()) s.SplashSkippable = it->get<bool>();
            if (auto it = splash->find("Image"); it != splash->end() && it->is_string()) s.SplashImage = it->get<std::string>();
            if (auto it = splash->find("Background"); it != splash->end() && it->is_array() && it->size() == 3)
                for (size_t i = 0; i < 3; i++)
                    if ((*it)[i].is_number())
                        s.SplashBackground[(int)i] = (*it)[i].get<float>();
        }
        if (auto aspect = j.find("Aspect"); aspect != j.end() && aspect->is_object())
        {
            if (auto it = aspect->find("Fixed"); it != aspect->end() && it->is_boolean()) s.FixedAspectRatio = it->get<bool>();
            if (auto it = aspect->find("Width"); it != aspect->end() && it->is_number())
                s.AspectWidth = (uint32_t)std::clamp(it->get<double>(), 1.0, (double)MaxAspectSide);
            if (auto it = aspect->find("Height"); it != aspect->end() && it->is_number())
                s.AspectHeight = (uint32_t)std::clamp(it->get<double>(), 1.0, (double)MaxAspectSide);
            if (auto it = aspect->find("BarColor"); it != aspect->end() && it->is_array() && it->size() == 3)
                for (size_t i = 0; i < 3; i++)
                    if ((*it)[i].is_number())
                        s.LetterboxColor[(int)i] = (*it)[i].get<float>();
        }
        if (auto log = j.find("Log"); log != j.end() && log->is_object())
        {
            auto readLevel = [&](const char* key, LogLevel& level) {
                if (auto it = log->find(key); it != log->end() && it->is_string())
                    if (const std::optional<LogLevel> parsed = Log::ParseLevel(it->get<std::string>()))
                        level = *parsed;
            };
            readLevel("Engine", s.Logging.Levels.Engine);
            readLevel("App", s.Logging.Levels.App);
            readLevel("File", s.Logging.Levels.File);
            if (auto it = log->find("WriteFile"); it != log->end() && it->is_boolean()) s.Logging.WriteFile = it->get<bool>();
            if (auto it = log->find("Keep"); it != log->end() && it->is_number())
                s.Logging.KeepFiles = (int)std::clamp(it->get<double>(), (double)PlayerLogSettings::MinKeepFiles, (double)PlayerLogSettings::MaxKeepFiles);
        }
        s.Sanitize();
        out = std::move(s);
        return true;
    }

    // ------------------------------------------------------------------ icon
    namespace GameIcon {

        bool Load(const std::filesystem::path& file, ImageData& out, std::string* error)
        {
            auto fail = [&](std::string reason) {
                if (error)
                    *error = std::move(reason);
                return false;
            };
            std::error_code ec;
            if (file.empty())
                return fail("ikonka tanlanmagan");
            if (!std::filesystem::is_regular_file(file, ec))
                return fail(std::format("fayl topilmadi: {}", file.generic_string()));
            // Only PNG: the window icon wants transparency, desktop entries use the file as it is.
            unsigned char magic[8] = {};
            {
                std::ifstream in(file, std::ios::binary);
                in.read((char*)magic, sizeof(magic));
                if (in.gcount() != (std::streamsize)sizeof(magic))
                    return fail("fayl juda kichik (PNG emas)");
            }
            static const unsigned char s_Png[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
            if (!std::equal(magic, magic + 8, s_Png))
                return fail("PNG emas (ikonka PNG bo'lishi kerak)");
            std::string reason;
            if (!Image::Load(file.string(), out, 4, false, &reason))
                return fail(reason.empty() ? std::string("PNG o'qilmadi") : reason);
            return true;
        }

        WindowIconImage Resize(const ImageData& image, uint32_t size)
        {
            WindowIconImage result;
            if (!image.IsValid() || image.Channels != 4 || size == 0)
                return result;
            result.Width = result.Height = size;
            result.Pixels.assign((size_t)size * size * 4, 0);
            const double w = image.Width, h = image.Height;
            const double side = std::max(w, h); // the transparent square the image is centred on
            const double offsetX = (side - w) * 0.5, offsetY = (side - h) * 0.5;
            const double scale = side / size;
            for (uint32_t ty = 0; ty < size; ty++)
            {
                // This pixel's rows of the image (fractions at the edges).
                const double y0 = ty * scale - offsetY, y1 = (ty + 1) * scale - offsetY;
                for (uint32_t tx = 0; tx < size; tx++)
                {
                    const double x0 = tx * scale - offsetX, x1 = (tx + 1) * scale - offsetX;
                    double r = 0, g = 0, b = 0, a = 0;
                    for (int sy = std::max(0, (int)std::floor(y0)); sy < std::min((int)image.Height, (int)std::ceil(y1)); sy++)
                    {
                        const double wy = std::min(y1, sy + 1.0) - std::max(y0, (double)sy);
                        if (wy <= 0.0)
                            continue;
                        for (int sx = std::max(0, (int)std::floor(x0)); sx < std::min((int)image.Width, (int)std::ceil(x1)); sx++)
                        {
                            const double wx = std::min(x1, sx + 1.0) - std::max(x0, (double)sx);
                            if (wx <= 0.0)
                                continue;
                            const uint8_t* p = &image.Pixels[((size_t)sy * image.Width + (size_t)sx) * 4];
                            const double weight = wx * wy, alpha = p[3] / 255.0 * weight;
                            r += p[0] * alpha;
                            g += p[1] * alpha;
                            b += p[2] * alpha;
                            a += alpha;
                        }
                    }
                    uint8_t* out = &result.Pixels[((size_t)ty * size + tx) * 4];
                    const double area = scale * scale;
                    if (a > 0.0)
                    {
                        out[0] = (uint8_t)std::clamp(std::lround(r / a), 0L, 255L);
                        out[1] = (uint8_t)std::clamp(std::lround(g / a), 0L, 255L);
                        out[2] = (uint8_t)std::clamp(std::lround(b / a), 0L, 255L);
                        out[3] = (uint8_t)std::clamp(std::lround(a / area * 255.0), 0L, 255L);
                    }
                }
            }
            return result;
        }

        std::vector<WindowIconImage> MakeWindowImages(const ImageData& image)
        {
            std::vector<WindowIconImage> images;
            if (!image.IsValid() || image.Channels != 4)
                return images;
            const uint32_t side = std::max(image.Width, image.Height);
            for (uint32_t size : PlayerSettings::IconSizes)
                if (size <= side || images.empty())
                    images.push_back(Resize(image, size));
            return images;
        }

        std::string Check(const ImageData& image)
        {
            if (!image.IsValid())
                return "Rasm bo'sh";
            if (image.Width != image.Height)
                return std::format("Kvadrat emas ({}x{}): shaffof kvadrat o'rtasiga joylanadi", image.Width, image.Height);
            if (image.Width < 64)
                return std::format("Juda kichik ({}x{} px): kamida 64, yaxshisi 256 px", image.Width, image.Height);
            return {};
        }

    }

    // ------------------------------------------------------------------ export
    namespace ExportFiles {

        namespace {
            // A desktop entry string value: no line breaks, backslashes escaped.
            std::string EntryValue(std::string_view text)
            {
                std::string out;
                for (char c : text)
                {
                    if (c == '\\') out += "\\\\";
                    else if (c == '\n') out += "\\n";
                    else if (c == '\t') out += "\\t";
                    else if (c == '\r') out += "\\r";
                    else out += c;
                }
                return out;
            }

            // The Exec key's program, quoted: ", `, $ and \ escaped inside the quotes (then the string
            // value's own escaping of the backslashes).
            // One quoted argument of an Exec key: ", `, $ and \ escaped inside the quotes, a % doubled (the
            // field codes), before the string value's own escaping.
            std::string QuotedArgument(std::string_view text)
            {
                std::string quoted = "\"";
                for (char c : text)
                {
                    if (c == '"' || c == '`' || c == '$' || c == '\\')
                        quoted += '\\';
                    else if (c == '%')
                        quoted += '%';
                    quoted += c;
                }
                quoted += '"';
                return quoted;
            }

            // The Exec key's program, quoted (then the string value's own escaping of the backslashes).
            std::string QuotedExec(const std::filesystem::path& exec)
            {
                return EntryValue(QuotedArgument(exec.string()));
            }

            // The entry's lines before Exec / Path / Icon and after them (they do not depend on where the game is).
            std::string EntryHead(const std::string& productName, const PlayerSettings& player)
            {
                std::string comment = productName + " " + player.Version;
                if (!player.CompanyName.empty())
                    comment += " (" + player.CompanyName + ")";
                std::string head = "[Desktop Entry]\n"
                                   "Type=Application\n"
                                   "Version=1.5\n";
                head += "Name=" + EntryValue(productName.empty() ? std::string("Game") : productName) + "\n";
                head += "Comment=" + EntryValue(comment) + "\n";
                return head;
            }

            std::string EntryTail(const PlayerSettings& player)
            {
                return "Terminal=false\n"
                       "Categories=Game;\n"
                       "X-Zamin-Version=" + EntryValue(player.Version) + "\n";
            }

            // A shell word in single quotes (a ' inside as '\'').
            std::string ShellQuote(std::string_view text)
            {
                std::string out = "'";
                for (char c : text)
                {
                    if (c == '\'') out += "'\\''";
                    else out += c;
                }
                return out + "'";
            }
        }

        std::string MakeDesktopEntry(const std::string& productName, const PlayerSettings& player, const std::filesystem::path& exec,
                                     const std::filesystem::path& icon, const std::filesystem::path& workingDirectory)
        {
            std::string entry = EntryHead(productName, player);
            entry += "Exec=" + QuotedExec(exec) + "\n";
            if (!workingDirectory.empty())
                entry += "Path=" + EntryValue(workingDirectory.string()) + "\n";
            if (!icon.empty())
                entry += "Icon=" + EntryValue(icon.string()) + "\n";
            return entry + EntryTail(player);
        }

        std::string MakeRelocatableExec(const std::string& launcher)
        {
            // %k: the entry file's path (a file:// URI from some launchers); the launcher is next to it.
            const std::string script = "d=$(dirname -- \"${1#file://}\") && cd -- \"$d\" && exec ./" + ShellQuote(launcher);
            return "sh -c " + QuotedArgument(script) + " sh %k";
        }

        std::string MakeLauncherScript(const std::string& projectFile, const std::string& desktopEntry, const std::string& icon)
        {
            std::string script = "#!/bin/sh\n"
                                 "# Starts the game: ZaminPlayer with its project (the arguments go to the player).\n"
                                 "CDPATH= cd -- \"$(dirname -- \"$0\")\" || exit 1\n";
            if (!desktopEntry.empty() && !icon.empty())
            {
                // (The Icon value is escaped like the entry's own strings: every backslash doubled.)
                script += "# The desktop entry next to it names its icon by an absolute path (desktop entries take no relative\n"
                          "# ones): after the folder was moved it is pointed at the icon here again.\n";
                script += "entry=" + ShellQuote(desktopEntry) + "\n";
                script += "icon=" + ShellQuote(icon) + "\n";
                script += R"SH(dir=$(pwd)
if [ -f "$entry" ] && [ -w "$entry" ] && [ -f "$icon" ] && grep -q '^Icon=' "$entry" 2>/dev/null; then
    case "$dir" in
    *'
'*) ;;
    *)
        line="Icon=$(printf '%s' "$dir/$icon" | sed -e 's/\\/\\\\/g')"
        if ! grep -qxF -- "$line" "$entry"; then
            if ZAMIN_ICON_LINE=$line awk '/^Icon=/ { print ENVIRON["ZAMIN_ICON_LINE"]; next } { print }' "$entry" >"$entry.tmp" 2>/dev/null; then
                cat "$entry.tmp" >"$entry" 2>/dev/null
            fi
            rm -f "$entry.tmp" 2>/dev/null
        fi
        ;;
    esac
fi
)SH";
            }
            script += "exec ./ZaminPlayer " + ShellQuote(projectFile) + " \"$@\"\n";
            return script;
        }

        std::string MakeRelocatableDesktopEntry(const std::string& productName, const PlayerSettings& player, const std::string& launcher,
                                                const std::filesystem::path& icon)
        {
            std::string entry = EntryHead(productName, player);
            entry += "Exec=" + EntryValue(MakeRelocatableExec(launcher)) + "\n";
            if (!icon.empty())
                entry += "Icon=" + EntryValue(icon.string()) + "\n";
            return entry + EntryTail(player);
        }

        std::filesystem::path GetIconSetPath(const std::filesystem::path& directory, std::string_view id, uint32_t size)
        {
            return directory / "icons" / "hicolor" / std::format("{}x{}", size, size) / "apps" / (std::string(id) + ".png");
        }

        std::vector<uint32_t> WriteIconSet(const ImageData& image, const std::filesystem::path& directory, std::string_view id, std::string* error)
        {
            std::vector<uint32_t> sizes;
            if (!image.IsValid() || image.Channels != 4)
            {
                if (error)
                    *error = "ikonka rasmi bo'sh";
                return sizes;
            }
            for (const WindowIconImage& icon : GameIcon::MakeWindowImages(image))
            {
                const std::filesystem::path file = GetIconSetPath(directory, id, icon.Width);
                std::error_code ec;
                std::filesystem::create_directories(file.parent_path(), ec);
                if (!Image::WritePNG(file.string(), icon.Width, icon.Height, 4, icon.Pixels.data()))
                {
                    if (error)
                        *error = std::format("yozilmadi: {}", file.string());
                    return {};
                }
                sizes.push_back(icon.Width);
            }
            return sizes;
        }

        std::string MakeDesktopEntryId(std::string_view name)
        {
            std::string id = "zamin-";
            for (char c : name)
            {
                const bool keep = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
                id += keep ? c : '-';
            }
            if (id.size() == 6)
                id += "game";
            return id;
        }

        std::string MakeDesktopInstallScript(const std::string& productName, const PlayerSettings& player, const std::string& launcher,
                                             const std::string& icon, const std::string& id)
        {
            // The head / tail lines and the relocatable Exec are fixed text (quoted here-documents: nothing in
            // them is expanded); the menu entry's Exec, Path and Icon are escaped at run time the way
            // MakeDesktopEntry does it: inside the Exec quotes ", `, $ and \ get a backslash and % is doubled,
            // then every backslash of a value is doubled.
            const std::string localEntry = std::filesystem::path(launcher).stem().string() + ".desktop";
            std::string script = "#!/bin/sh\n"
                                 "# " + EntryValue(productName) + ": puts the game into the application menus for the folder it is in now.\n"
                                 "# Run it again after moving the folder; \"--uninstall\" takes the menu entry and its icons away.\n"
                                 "set -e\n";
            script += "launcher=" + ShellQuote(launcher) + "\n";
            script += "icon=" + ShellQuote(icon) + "\n";
            script += "local_entry=" + ShellQuote(localEntry) + "\n";
            script += "id=" + ShellQuote(id) + "\n";
            script += R"SH(dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
case "$dir" in
*'
'*) echo "Papka yo'lida qator tashlash bor: desktop fayli yozilmaydi" >&2; exit 1 ;;
esac
data="${XDG_DATA_HOME:-$HOME/.local/share}"
apps="$data/applications"
installed="$apps/$id.desktop"
if [ "$1" = "--uninstall" ]; then
    rm -f "$installed"
    for file in "$data"/icons/hicolor/*/apps/"$id.png"; do
        if [ -f "$file" ]; then rm -f "$file"; fi
    done
    echo "Dasturlar menyusidan olib tashlandi: $installed"
    exit 0
fi
# The icon set (every size the export made) into the user's icon theme: the menu entry names it by its id.
icons_installed=0
for file in "$dir"/icons/hicolor/*/apps/"$id.png"; do
    [ -f "$file" ] || continue
    size=$(basename -- "$(dirname -- "$(dirname -- "$file")")")
    mkdir -p "$data/icons/hicolor/$size/apps"
    cp -f "$file" "$data/icons/hicolor/$size/apps/$id.png"
    icons_installed=1
done
exec_value=$(printf '%s' "$dir/$launcher" | sed -e 's/[\\"`$]/\\&/g' -e 's/%/%%/g' -e 's/\\/\\\\/g')
path_value=$(printf '%s' "$dir" | sed -e 's/\\/\\\\/g')
icon_value=$(printf '%s' "$dir/$icon" | sed -e 's/\\/\\\\/g')
# write_entry menu | local
write_entry()
{
    cat <<'ZAMIN_DESKTOP_HEAD'
)SH";
            script += EntryHead(productName, player);
            script += R"SH(ZAMIN_DESKTOP_HEAD
    if [ "$1" = local ]; then
        cat <<'ZAMIN_DESKTOP_EXEC'
)SH";
            script += "Exec=" + EntryValue(MakeRelocatableExec(launcher)) + "\n";
            script += R"SH(ZAMIN_DESKTOP_EXEC
    else
        printf 'Exec="%s"\n' "$exec_value"
        printf 'Path=%s\n' "$path_value"
    fi
    if [ "$1" != local ] && [ "$icons_installed" = 1 ]; then
        printf 'Icon=%s\n' "$id"
    elif [ -n "$icon" ] && [ -f "$dir/$icon" ]; then
        printf 'Icon=%s\n' "$icon_value"
    fi
    cat <<'ZAMIN_DESKTOP_TAIL'
)SH";
            script += EntryTail(player);
            script += R"SH(ZAMIN_DESKTOP_TAIL
}
mkdir -p "$apps"
write_entry menu > "$installed"
chmod +x "$installed"
# The entry next to the launcher too (skipped when the folder cannot be written, e.g. a system folder).
if { write_entry local > "$dir/$local_entry"; } 2>/dev/null; then
    chmod +x "$dir/$local_entry" 2>/dev/null || true
fi
if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "$apps" >/dev/null 2>&1 || true
fi
echo "Dasturlar menyusiga qo'shildi: $installed"
)SH";
            return script;
        }

    }

}
