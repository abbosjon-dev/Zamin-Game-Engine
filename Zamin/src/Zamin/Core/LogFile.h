#pragma once

#include <ctime>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Zamin {

    // The log written to files (the player: ~/.local/share/<company>/<product>/logs). Every message of
    // Log goes there too from the moment a file is opened - the ones logged before (start-up) are
    // written first. Lines are flushed at once, so the file is complete even after a crash.
    //
    // Rotating: each run gets <dir>/<prefix>-<YYYY-MM-DD_HH-MM-SS>.log and the oldest of them are
    // deleted so that at most `Keep` are left; latest.log is the same log again (always the last run).
    // A single file (ZaminPlayer --log-file <path>): only that file, truncated.
    class LogFile
    {
    public:
        struct Options
        {
            std::filesystem::path Directory;
            std::string Prefix = "player";
            int Keep = 5;                       // dated logs kept (this run's included); at least 1
            bool Latest = true;                 // also latest.log
            // Written at the top of the files ("# ..." lines: the game, its version, the start time).
            std::string Header;
        };

        static bool OpenRotating(const Options& options, std::string* error = nullptr);
        static bool Open(const std::filesystem::path& file, const std::string& header = {}, std::string* error = nullptr);
        // Detaches the files from Log and closes them (also at exit).
        static void Close();
        static bool IsOpen();

        // This run's log (the dated file / the --log-file one) and latest.log ("" when none).
        static std::filesystem::path GetPath();
        static std::filesystem::path GetLatestPath();
        // The folder the logs are in ("" when no file is open).
        static std::filesystem::path GetDirectory();

        // The player's log folder: SaveData::GetDirectoryFor(company, product) / "logs".
        static std::filesystem::path GetDefaultDirectory(std::string_view company, std::string_view product);
        // "<prefix>-2026-11-07_14-03-12.log" for that local time.
        static std::string MakeFileName(std::string_view prefix, std::time_t time);
        // The dated logs of a folder ("<prefix>-*.log", not latest.log), oldest first (by name: the date).
        static std::vector<std::filesystem::path> ListLogs(const std::filesystem::path& directory, std::string_view prefix);
        // Deletes the oldest dated logs until `keep` are left; how many were deleted.
        static size_t RemoveOldLogs(const std::filesystem::path& directory, std::string_view prefix, int keep);

        static constexpr const char* LatestFileName = "latest.log";
    };

}
