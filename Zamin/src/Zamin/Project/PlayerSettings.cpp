#include "Zamin/Project/PlayerSettings.h"

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
            const size_t b = s.find_first_not_of(" \t\r\n");
            if (b == std::string_view::npos)
                return {};
            const size_t e = s.find_last_not_of(" \t\r\n");
            return std::string(s.substr(b, e - b + 1));
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
                                                         "Title", "VersionInTitle", "TargetFrameRate", "RunInBackground", "Icon" };
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
        j["Icon"] = Icon.generic_string();
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
        std::string icon;
        readString("Icon", icon);
        s.Icon = icon;
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
            std::string QuotedExec(const std::filesystem::path& exec)
            {
                std::string quoted = "\"";
                for (char c : exec.string())
                {
                    if (c == '"' || c == '`' || c == '$' || c == '\\')
                        quoted += '\\';
                    quoted += c;
                }
                quoted += '"';
                return EntryValue(quoted);
            }
        }

        std::string MakeDesktopEntry(const std::string& productName, const PlayerSettings& player, const std::filesystem::path& exec,
                                     const std::filesystem::path& icon, const std::filesystem::path& workingDirectory)
        {
            std::string comment = productName + " " + player.Version;
            if (!player.CompanyName.empty())
                comment += " (" + player.CompanyName + ")";
            std::string entry = "[Desktop Entry]\n"
                                "Type=Application\n"
                                "Version=1.5\n";
            entry += "Name=" + EntryValue(productName.empty() ? std::string("Game") : productName) + "\n";
            entry += "Comment=" + EntryValue(comment) + "\n";
            entry += "Exec=" + QuotedExec(exec) + "\n";
            if (!workingDirectory.empty())
                entry += "Path=" + EntryValue(workingDirectory.string()) + "\n";
            if (!icon.empty())
                entry += "Icon=" + EntryValue(icon.string()) + "\n";
            entry += "Terminal=false\n"
                     "Categories=Game;\n";
            entry += "X-Zamin-Version=" + EntryValue(player.Version) + "\n";
            return entry;
        }

    }

}
