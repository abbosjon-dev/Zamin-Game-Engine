#pragma once

#include <filesystem>
#include <string>

namespace Zamin {

    class FileDialogs
    {
    public:
        // Return empty string if cancelled / unavailable.
        // `filter` example: "Zamin Scene (*.zscene) | *.zscene"
        static std::string OpenFile(const char* filter, const std::filesystem::path& startDir = {});
        static std::string SaveFile(const char* filter, const std::filesystem::path& startDir = {});
        static std::string SelectFolder(const std::filesystem::path& startDir = {});
        static bool IsAvailable();
    };

    class Platform
    {
    public:
        static float GetTime();
        static void OpenInFileBrowser(const std::filesystem::path& path);
        // Runs a shell command, captures stdout+stderr. Returns exit code.
        static int RunCommand(const std::string& command, std::string* output = nullptr);
    };

}
