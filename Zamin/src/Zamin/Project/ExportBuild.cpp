#include "Zamin/Project/ExportBuild.h"

#include "Zamin/Asset/AssetTypes.h"
#include "Zamin/Utils/FileSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <fstream>
#include <sys/wait.h>

namespace Zamin {

    namespace fs = std::filesystem;
    using ordered_json = nlohmann::ordered_json;

    // ------------------------------------------------------------------ commands
    std::string ShellQuote(std::string_view text)
    {
        std::string out = "'";
        for (char c : text)
        {
            if (c == '\'')
                out += "'\\''";
            else
                out += c;
        }
        return out + "'";
    }

    int RunShellCommand(const std::string& command, const std::function<void(const std::string&)>& onLine)
    {
        // (Every part of a compound command sends its errors along too.)
        FILE* pipe = ::popen(("{ " + command + "\n} 2>&1").c_str(), "r");
        if (!pipe)
            return -1;
        char buffer[4096];
        std::string line;
        while (std::fgets(buffer, sizeof(buffer), pipe))
        {
            line += buffer;
            if (line.empty() || line.back() != '\n')
                continue; // a longer line goes on
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
                line.pop_back();
            if (onLine)
                onLine(line);
            line.clear();
        }
        if (!line.empty() && onLine)
            onLine(line);
        const int status = ::pclose(pipe);
        if (status == -1)
            return -1;
        return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }

    fs::path ExportModuleBuild::DefaultBuildDirectory(const fs::path& projectDirectory) { return projectDirectory / "Build" / "ExportRelease"; }

    std::string ExportModuleBuild::MakeConfigureCommand() const
    {
        std::error_code ec;
        const bool fresh = !fs::exists(BuildDirectory / "CMakeCache.txt", ec);
        std::string command = "cmake -S " + ShellQuote(ProjectDirectory.string()) + " -B " + ShellQuote(BuildDirectory.string());
        if (fresh && Ninja)
            command += " -G Ninja";
        command += " -DCMAKE_BUILD_TYPE=Release";
        command += " " + ShellQuote("-DCMAKE_CXX_FLAGS_RELEASE=" + OptimizationFlags);
        command += " " + ShellQuote("-DCMAKE_C_FLAGS_RELEASE=" + OptimizationFlags);
        command += " " + ShellQuote("-DZamin_DIR=" + EngineDirectory.string());
        command += " " + ShellQuote("-DZAMIN_GAME_MODULE_OUTPUT_DIR=" + OutputDirectory.string());
        return command;
    }

    std::string ExportModuleBuild::MakeBuildCommand() const
    {
        return "cmake --build " + ShellQuote(BuildDirectory.string()) + " --config Release --parallel";
    }

    // ------------------------------------------------------------------ strip
    bool BinaryStrip::IsAvailable()
    {
        static const bool available = std::system("command -v strip >/dev/null 2>&1") == 0;
        return available;
    }

    bool BinaryStrip::IsElfFile(const fs::path& file)
    {
        std::ifstream in(file, std::ios::binary);
        char magic[4] = {};
        if (!in.read(magic, 4))
            return false;
        return magic[0] == 0x7f && magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F';
    }

    StripResult BinaryStrip::Strip(const fs::path& file)
    {
        StripResult result;
        std::error_code ec;
        result.SizeBefore = result.SizeAfter = fs::file_size(file, ec);
        if (ec)
        {
            result.Error = "fayl topilmadi";
            return result;
        }
        if (!IsElfFile(file))
        {
            result.Error = "ELF fayl emas";
            return result;
        }
        if (!IsAvailable())
        {
            result.Error = "strip dasturi o'rnatilmagan (binutils)";
            return result;
        }
        std::string output;
        const int code = RunShellCommand("strip --strip-unneeded " + ShellQuote(file.string()), [&](const std::string& line) {
            if (!output.empty())
                output += "; ";
            output += line;
        });
        result.SizeAfter = fs::file_size(file, ec);
        result.Ok = code == 0 && !ec;
        if (!result.Ok)
            result.Error = output.empty() ? std::format("strip xato bilan tugadi ({})", code) : output;
        return result;
    }

    // ------------------------------------------------------------------ report
    namespace {
        constexpr std::array<const char*, (size_t)ExportCategory::Count> s_CategoryNames = {
            "Binarlar", "Teksturalar", "Modellar", "Audio", "Sahnalar", "Shriftlar", "Ma'lumotlar", "Boshqa"
        };
        constexpr std::array<const char*, (size_t)ExportCategory::Count> s_CategoryKeys = {
            "Binaries", "Textures", "Models", "Audio", "Scenes", "Fonts", "Data", "Other"
        };

        std::string Lower(std::string text)
        {
            for (char& c : text)
                c = (char)std::tolower((unsigned char)c);
            return text;
        }

        bool IsReportFile(const std::string& relative)
        {
            return relative == ExportReport::JsonFileName || relative == ExportReport::TextFileName || relative == ExportAssets::ReportFileName;
        }
    }

    bool ExportReport::operator==(const ExportReport& o) const
    {
        auto excludedEqual = [](const std::vector<ExcludedAsset>& a, const std::vector<ExcludedAsset>& b) {
            return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](const ExcludedAsset& x, const ExcludedAsset& y) {
                       return x.Path == y.Path && x.Size == y.Size && x.Reason == y.Reason && x.Type == y.Type;
                   });
        };
        return Game == o.Game && Version == o.Version && Company == o.Company && OutputDirectory == o.OutputDirectory && Date == o.Date &&
               std::abs(Seconds - o.Seconds) < 1e-3 && Succeeded == o.Succeeded && TotalBytes == o.TotalBytes && FileCount == o.FileCount &&
               Categories == o.Categories && Largest == o.Largest && excludedEqual(Excluded, o.Excluded) && ExcludedBytes == o.ExcludedBytes &&
               ScriptModule == o.ScriptModule && StrippedBytes == o.StrippedBytes && Warnings == o.Warnings && Errors == o.Errors;
    }

    ExportCategory ExportReport::Categorize(const fs::path& path, bool elf)
    {
        const std::string name = Lower(path.filename().string());
        if (elf || name.ends_with(".so") || name.find(".so.") != std::string::npos)
            return ExportCategory::Binaries;
        const std::string extension = Lower(path.extension().string());
        if (extension == ".bin" || extension == ".gltf" || extension == ".glb")
            return ExportCategory::Models;
        if (extension == ".zsceneb")
            return ExportCategory::Scenes;
        if (extension == ".png" || extension == ".ico" || extension == ".svg")
        {
            // The game's icons next to the launcher are not the game's textures.
            const std::string first = path.has_parent_path() ? Lower(path.begin()->string()) : std::string();
            if (first == "icons" || !path.has_parent_path())
                return ExportCategory::Other;
        }
        switch (AssetTypes::FromExtension(extension))
        {
            case AssetType::Texture:
            case AssetType::Environment: return ExportCategory::Textures;
            case AssetType::Model: return ExportCategory::Models;
            case AssetType::Audio: return ExportCategory::Audio;
            case AssetType::Scene:
            case AssetType::Prefab: return ExportCategory::Scenes;
            case AssetType::Font: return ExportCategory::Fonts;
            case AssetType::Material:
            case AssetType::PhysicsMaterial:
            case AssetType::Animation:
            case AssetType::SpriteAnimation:
            case AssetType::ParticlePreset:
            case AssetType::Project: return ExportCategory::Data;
            default: break;
        }
        static const char* s_Data[] = { ".zmeta", ".json", ".zlang", ".zreg", ".glsl", ".vert", ".frag", ".csv", ".zsettings" };
        for (const char* data : s_Data)
            if (extension == data)
                return ExportCategory::Data;
        return ExportCategory::Other;
    }

    const char* ExportReport::GetCategoryName(ExportCategory category)
    {
        return category < ExportCategory::Count ? s_CategoryNames[(size_t)category] : "?";
    }

    const char* ExportReport::GetCategoryKey(ExportCategory category)
    {
        return category < ExportCategory::Count ? s_CategoryKeys[(size_t)category] : "Other";
    }

    void ExportReport::ScanDirectory(const fs::path& directory, size_t largest)
    {
        TotalBytes = 0;
        FileCount = 0;
        Categories = {};
        Largest.clear();
        std::vector<ExportReportFile> files;
        std::error_code ec;
        for (auto it = fs::recursive_directory_iterator(directory, fs::directory_options::skip_permission_denied, ec);
             !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
        {
            std::error_code e;
            if (!it->is_regular_file(e))
                continue;
            const std::string relative = fs::relative(it->path(), directory, e).generic_string();
            if (e || IsReportFile(relative))
                continue;
            ExportReportFile file;
            file.Path = relative;
            file.Size = it->file_size(e);
            const bool elf = !it->path().has_extension() && BinaryStrip::IsElfFile(it->path());
            file.Category = Categorize(relative, elf);
            TotalBytes += file.Size;
            FileCount++;
            CategoryTotal& total = Categories[(size_t)file.Category];
            total.Bytes += file.Size;
            total.Files++;
            files.push_back(std::move(file));
        }
        std::sort(files.begin(), files.end(), [](const ExportReportFile& a, const ExportReportFile& b) {
            return a.Size != b.Size ? a.Size > b.Size : a.Path < b.Path;
        });
        if (files.size() > largest)
            files.resize(largest);
        Largest = std::move(files);
    }

    void ExportReport::SetExcluded(const ExportAssetPlan& plan)
    {
        Excluded = plan.Excluded;
        ExcludedBytes = plan.ExcludedBytes;
    }

    std::string ExportReport::ToJson() const
    {
        ordered_json j;
        j["Game"] = Game;
        j["Version"] = Version;
        j["Company"] = Company;
        j["OutputDirectory"] = OutputDirectory;
        j["Date"] = Date;
        j["Seconds"] = std::round(Seconds * 1000.0) / 1000.0;
        j["Succeeded"] = Succeeded;
        j["TotalBytes"] = TotalBytes;
        j["FileCount"] = FileCount;
        ordered_json categories = ordered_json::object();
        for (size_t i = 0; i < Categories.size(); i++)
            categories[GetCategoryKey((ExportCategory)i)] = { { "Bytes", Categories[i].Bytes }, { "Files", Categories[i].Files } };
        j["Categories"] = categories;
        ordered_json largest = ordered_json::array();
        for (const ExportReportFile& file : Largest)
            largest.push_back({ { "Path", file.Path }, { "Size", file.Size }, { "Category", GetCategoryKey(file.Category) } });
        j["Largest"] = largest;
        ordered_json excluded = ordered_json::array();
        for (const ExcludedAsset& asset : Excluded)
            excluded.push_back({ { "Path", asset.Path }, { "Size", asset.Size }, { "Reason", asset.Reason } });
        j["Excluded"] = excluded;
        j["ExcludedBytes"] = ExcludedBytes;
        j["ScriptModule"] = ScriptModule;
        j["StrippedBytes"] = StrippedBytes;
        j["Warnings"] = Warnings;
        j["Errors"] = Errors;
        return j.dump(4);
    }

    bool ExportReport::FromJson(const std::string& text, ExportReport& out)
    {
        const ordered_json j = ordered_json::parse(text, nullptr, false);
        if (j.is_discarded() || !j.is_object())
            return false;
        ExportReport r;
        auto str = [&](const char* key, std::string& value) {
            if (auto it = j.find(key); it != j.end() && it->is_string())
                value = it->get<std::string>();
        };
        auto size = [](const ordered_json& o, const char* key) -> uintmax_t {
            if (auto it = o.find(key); it != o.end() && it->is_number_unsigned())
                return it->get<uintmax_t>();
            return 0;
        };
        str("Game", r.Game);
        str("Version", r.Version);
        str("Company", r.Company);
        str("OutputDirectory", r.OutputDirectory);
        str("Date", r.Date);
        str("ScriptModule", r.ScriptModule);
        if (auto it = j.find("Seconds"); it != j.end() && it->is_number())
            r.Seconds = it->get<double>();
        if (auto it = j.find("Succeeded"); it != j.end() && it->is_boolean())
            r.Succeeded = it->get<bool>();
        r.TotalBytes = size(j, "TotalBytes");
        r.FileCount = (size_t)size(j, "FileCount");
        r.ExcludedBytes = size(j, "ExcludedBytes");
        r.StrippedBytes = size(j, "StrippedBytes");
        if (auto it = j.find("Categories"); it != j.end() && it->is_object())
            for (size_t i = 0; i < r.Categories.size(); i++)
                if (auto c = it->find(GetCategoryKey((ExportCategory)i)); c != it->end() && c->is_object())
                {
                    r.Categories[i].Bytes = size(*c, "Bytes");
                    r.Categories[i].Files = (size_t)size(*c, "Files");
                }
        auto category = [](const std::string& key) {
            for (size_t i = 0; i < (size_t)ExportCategory::Count; i++)
                if (key == s_CategoryKeys[i])
                    return (ExportCategory)i;
            return ExportCategory::Other;
        };
        if (auto it = j.find("Largest"); it != j.end() && it->is_array())
            for (const auto& item : *it)
                if (item.is_object() && item.contains("Path") && item["Path"].is_string())
                    r.Largest.push_back({ item["Path"].get<std::string>(), size(item, "Size"),
                                          category(item.value("Category", std::string("Other"))) });
        if (auto it = j.find("Excluded"); it != j.end() && it->is_array())
            for (const auto& item : *it)
                if (item.is_object() && item.contains("Path") && item["Path"].is_string())
                {
                    ExcludedAsset asset;
                    asset.Path = item["Path"].get<std::string>();
                    asset.Type = AssetTypes::FromPath(asset.Path);
                    asset.Size = size(item, "Size");
                    asset.Reason = item.value("Reason", std::string());
                    r.Excluded.push_back(std::move(asset));
                }
        for (auto [key, list] : { std::pair<const char*, std::vector<std::string>*>{ "Warnings", &r.Warnings }, { "Errors", &r.Errors } })
            if (auto it = j.find(key); it != j.end() && it->is_array())
                for (const auto& item : *it)
                    if (item.is_string())
                        list->push_back(item.get<std::string>());
        out = std::move(r);
        return true;
    }

    std::string ExportReport::ToText() const
    {
        std::string text = std::format("{} {} - eksport hisoboti\n", Game.empty() ? std::string("O'yin") : Game, Version);
        if (!Company.empty())
            text += "Kompaniya: " + Company + "\n";
        text += std::format("Papka: {}\nSana: {}\nHolat: {} ({:.1f} s)\n", OutputDirectory, Date, Succeeded ? "muvaffaqiyatli" : "xatolar bilan", Seconds);
        text += std::format("Jami: {} ta fayl, {}\n", FileCount, ExportAssets::FormatSize(TotalBytes));
        if (!ScriptModule.empty())
            text += "Skript moduli: " + ScriptModule + "\n";
        if (StrippedBytes > 0)
            text += "strip: binarlar " + ExportAssets::FormatSize(StrippedBytes) + " kichraydi\n";

        text += "\nTurlar bo'yicha:\n";
        for (size_t i = 0; i < Categories.size(); i++)
        {
            const CategoryTotal& c = Categories[i];
            if (c.Files == 0)
                continue;
            const double percent = TotalBytes ? 100.0 * (double)c.Bytes / (double)TotalBytes : 0.0;
            text += std::format("  {:<14}{:>10}  {:>5.1f}%  {} ta fayl\n", GetCategoryName((ExportCategory)i), ExportAssets::FormatSize(c.Bytes), percent,
                                c.Files);
        }
        if (!Largest.empty())
        {
            text += "\nEng katta fayllar:\n";
            for (const ExportReportFile& file : Largest)
                text += std::format("  {:>10}  {:<12}  {}\n", ExportAssets::FormatSize(file.Size), GetCategoryName(file.Category), file.Path);
        }
        text += std::format("\nChiqarib tashlangan assetlar: {} ta ({})\n", Excluded.size(), ExportAssets::FormatSize(ExcludedBytes));
        for (const ExcludedAsset& asset : Excluded)
            text += std::format("  {:>10}  {}  ({})\n", ExportAssets::FormatSize(asset.Size), asset.Path, asset.Reason);
        text += std::format("\nOgohlantirishlar: {}\n", Warnings.size());
        for (const std::string& warning : Warnings)
            text += "  - " + warning + "\n";
        if (!Errors.empty())
        {
            text += std::format("\nXatolar: {}\n", Errors.size());
            for (const std::string& error : Errors)
                text += "  - " + error + "\n";
        }
        return text;
    }

    bool ExportReport::Write(const fs::path& directory) const
    {
        const bool json = FileSystem::WriteTextFile(directory / JsonFileName, ToJson());
        const bool text = FileSystem::WriteTextFile(directory / TextFileName, ToText());
        return json && text;
    }

    bool ExportReport::Read(const fs::path& directory, ExportReport& out)
    {
        std::error_code ec;
        const fs::path file = directory / JsonFileName;
        if (!fs::exists(file, ec))
            return false;
        return FromJson(FileSystem::ReadTextFile(file), out);
    }

}
