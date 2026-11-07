#pragma once

#include "Zamin/Project/ExportAssets.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace Zamin {

    // ------------------------------------------------------------------ the script module in Release
    // How an export compiles the game's C++ scripts: Release (-O2, NDEBUG) in a build folder of its own,
    // the module written to OutputDirectory (cmake/ZaminGame.cmake: ZAMIN_GAME_MODULE_OUTPUT_DIR) - the
    // editor's hot reload module (<project>/Binaries, a Debug or Release build of its own) stays as it is.
    struct ExportModuleBuild
    {
        std::filesystem::path ProjectDirectory; // its CMakeLists.txt
        std::filesystem::path BuildDirectory;   // DefaultBuildDirectory
        std::filesystem::path EngineDirectory;  // the engine's build folder (ZaminConfig.cmake): -DZamin_DIR
        std::filesystem::path OutputDirectory;  // where lib<Name>.so goes
        bool Ninja = false;                     // the Ninja generator (only for a new build folder)
        std::string OptimizationFlags = "-O2 -DNDEBUG";

        // <project>/Build/ExportRelease.
        static std::filesystem::path DefaultBuildDirectory(const std::filesystem::path& projectDirectory);
        // The shell commands (every path quoted): configure (the generator only for a folder that has no
        // CMakeCache.txt yet - one made with another stays as it is) and build.
        std::string MakeConfigureCommand() const;
        std::string MakeBuildCommand() const;
    };

    // A shell word in single quotes (a ' inside as '\'').
    std::string ShellQuote(std::string_view text);

    // Runs `command` with /bin/sh; every line it prints (stdout and stderr) goes to `onLine`. The exit
    // code (-1: it could not be started).
    int RunShellCommand(const std::string& command, const std::function<void(const std::string&)>& onLine = {});

    // ------------------------------------------------------------------ strip
    struct StripResult
    {
        bool Ok = false;
        uintmax_t SizeBefore = 0, SizeAfter = 0;
        std::string Error; // Uzbek: why not
        uintmax_t Saved() const { return SizeBefore > SizeAfter ? SizeBefore - SizeAfter : 0; }
    };

    class BinaryStrip
    {
    public:
        // `strip` is installed (binutils).
        static bool IsAvailable();
        // The file starts with the ELF magic (an executable / shared library of this system).
        static bool IsElfFile(const std::filesystem::path& file);
        // Removes the debug information and the local symbols of an ELF file in place
        // (strip --strip-unneeded): the dynamic symbols stay - it still loads, links and its exported
        // functions keep their names in backtraces. Not an ELF file / no strip: Ok false.
        static StripResult Strip(const std::filesystem::path& file);
    };

    // ------------------------------------------------------------------ the export report
    // What the files of an exported game are, by what they hold.
    enum class ExportCategory : uint8_t
    {
        Binaries = 0, // the player, the engine, the script module (ELF files)
        Textures,     // images (.png, .jpg, .hdr ...)
        Models,       // .gltf, .glb and their .bin buffers, .obj ...
        Audio,        // .wav, .ogg, .mp3, .flac
        Scenes,       // .zscene, .zsceneb, .zprefab
        Fonts,        // .ttf, .otf
        Data,         // the rest of the engine's files: materials, clips, presets, .zmeta, the project, JSON ...
        Other,        // launchers, desktop entries, icons, notes
        Count
    };

    struct ExportReportFile
    {
        std::string Path; // relative to the export folder ('/' separated)
        uintmax_t Size = 0;
        ExportCategory Category = ExportCategory::Other;

        bool operator==(const ExportReportFile&) const = default;
    };

    // The export's report: what the folder holds (total, per category, the largest files), the assets
    // left out, how the script module was made and what went wrong. The editor shows it when an export
    // ends and writes it next to the game (export-report.json for tools, export-report.txt to read).
    struct ExportReport
    {
        struct CategoryTotal
        {
            uintmax_t Bytes = 0;
            size_t Files = 0;
            bool operator==(const CategoryTotal&) const = default;
        };

        std::string Game, Version, Company;
        std::string OutputDirectory;
        std::string Date;               // "2026-11-07 14:03:12" (local time)
        double Seconds = 0.0;           // how long the export took
        bool Succeeded = true;
        uintmax_t TotalBytes = 0;
        size_t FileCount = 0;
        std::array<CategoryTotal, (size_t)ExportCategory::Count> Categories{};
        std::vector<ExportReportFile> Largest; // the largest first
        std::vector<ExcludedAsset> Excluded;   // the assets left out (ExportAssetPlan)
        uintmax_t ExcludedBytes = 0;
        // How the script module was made ("Release (-O2), 6.2 s" / "editor moduli nusxalandi" / "skriptsiz").
        std::string ScriptModule;
        uintmax_t StrippedBytes = 0;    // what strip took off the binaries
        std::vector<std::string> Warnings, Errors;

        static constexpr size_t DefaultLargestCount = 15;
        static constexpr const char* JsonFileName = "export-report.json";
        static constexpr const char* TextFileName = "export-report.txt";

        bool operator==(const ExportReport&) const;

        // The category of a file of the export by its name ("lib*.so*" and `elf` files: binaries).
        static ExportCategory Categorize(const std::filesystem::path& path, bool elf = false);
        static const char* GetCategoryName(ExportCategory category); // Uzbek ("Binarlar", "Teksturalar" ...)
        static const char* GetCategoryKey(ExportCategory category);  // in the JSON ("Binaries", "Textures" ...)

        // Counts every file under `directory` (the report's own files and excluded-assets.txt left out):
        // the totals, the categories (ELF files without an extension are binaries) and the `largest` largest.
        void ScanDirectory(const std::filesystem::path& directory, size_t largest = DefaultLargestCount);
        // The excluded assets of the plan.
        void SetExcluded(const ExportAssetPlan& plan);

        std::string ToJson() const;
        static bool FromJson(const std::string& text, ExportReport& out);
        // A plain text report (Uzbek) for people.
        std::string ToText() const;
        // Writes export-report.json and export-report.txt into `directory`.
        bool Write(const std::filesystem::path& directory) const;
        // Reads `directory`/export-report.json.
        static bool Read(const std::filesystem::path& directory, ExportReport& out);
    };

}
