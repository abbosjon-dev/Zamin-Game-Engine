#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace Zamin {

    // A scene file (or an entity set: a prefab, copied entities) whose JSON does not parse as a whole
    // - cut off by a crash or a full disk, garbage written into the middle - is salvaged instead of
    // lost: every entry of "Entities" that parses on its own is kept, the other top-level parts
    // ("Scene", "Version", "Settings", "Metadata", "RootOrder" ...) when they do, and what is left
    // between them (an incomplete last entry, a broken one) is skipped. Recovery is never automatic:
    // the editor asks first ("Qisman tiklash"), SceneSerializer::RecoverFromString and
    // EntitySetReadOptions::Recover do it when asked.
    class SceneRecovery
    {
    public:
        struct Result
        {
            bool Recovered = false;           // a scene / an entity set came out (maybe with no entities)
            std::string Text;                 // what was salvaged, as a file (JSON text); "" when nothing
            size_t Entities = 0;              // entries of "Entities" kept
            size_t EntriesDropped = 0;        // entries that did not parse (or were cut off)
            size_t BytesSkipped = 0;          // text left out (broken entries, garbage, the cut-off end)
            bool Truncated = false;           // the text ended before the file did
            std::vector<std::string> LostKeys;// top-level parts that did not parse ("Settings")
            std::string Error;                // why nothing could be salvaged
        };

        // Nesting deeper than this is refused (a hand-made or corrupted file must not exhaust the stack).
        static constexpr int MaxDepth = 512;

        static Result Salvage(const std::string& text);
        // "Level1.zscene" -> "Level1.zscene.corrupt-20261107-143005.bak" (next to it).
        static std::filesystem::path BackupPath(const std::filesystem::path& file,
                                                std::chrono::system_clock::time_point when = std::chrono::system_clock::now());
        // Copies the broken file aside before anything is written over it. False when it cannot be.
        static bool WriteBackup(const std::filesystem::path& file, std::filesystem::path* backup = nullptr);
        // One line for the user: "12 obyekt tiklandi, 340 bayt o'tkazib yuborildi".
        static std::string Summary(const Result& result);
    };

}
