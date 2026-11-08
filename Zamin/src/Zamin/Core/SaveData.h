#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Zamin {

    enum class SaveValueType { None = 0, Int, Float, String, Bool };

    // Which value of a record wins (SaveData::SubmitRecord): the bigger one (scores, levels reached) or the
    // smaller one (lap times, moves).
    enum class RecordOrder { Higher = 0, Lower };

    // Persistent key / value storage for game data (settings, records, progress), like
    // Unity's PlayerPrefs. Values live in memory and are written as JSON to
    //     $XDG_DATA_HOME/zamin/<product>/savedata.json   (default ~/.local/share/zamin/<product>/)
    // or, when the project names its company (Project settings > Player),
    //     $XDG_DATA_HOME/<company>/<product>/savedata.json
    // (<product>: the product name, else the project's name)
    // by Save(), which also runs automatically when play mode stops, a scene is unloaded
    // and the application exits. The file is loaded on first use and follows the active
    // project (data of a previous project is saved before switching).
    //
    // Numbers convert between Int and Float on read; other type mismatches return the default.
    class SaveData
    {
    public:
        static void SetInt(std::string_view key, int value);
        static void SetFloat(std::string_view key, float value);
        static void SetString(std::string_view key, std::string_view value);
        static void SetBool(std::string_view key, bool value);

        static int GetInt(std::string_view key, int defaultValue = 0);
        static float GetFloat(std::string_view key, float defaultValue = 0.0f);
        static std::string GetString(std::string_view key, std::string_view defaultValue = {});
        static bool GetBool(std::string_view key, bool defaultValue = false);

        // Records (high scores, best times): stores `value` under `key` when it beats the value saved there -
        // or nothing (no number) is saved there yet - and returns whether it did, i.e. whether it is a new
        // record. A record saved as Int and one saved as Float compare as numbers; NaN never wins. Written to
        // the file by the next Save(), like any other value.
        //     if (SaveData::SubmitRecord("Game.BestScore", score)) ShowBanner("Yangi rekord!");
        //     SaveData::SubmitRecord("Game.BestLap", lapSeconds, RecordOrder::Lower);
        static bool SubmitRecord(std::string_view key, int value, RecordOrder order = RecordOrder::Higher);
        static bool SubmitRecord(std::string_view key, float value, RecordOrder order = RecordOrder::Higher);
        // Whether `value` would be a new record under `key` (SubmitRecord without storing it).
        static bool IsNewRecord(std::string_view key, double value, RecordOrder order = RecordOrder::Higher);

        static bool HasKey(std::string_view key);
        static SaveValueType GetType(std::string_view key);
        static void DeleteKey(std::string_view key);
        static void DeleteAll();
        static std::vector<std::string> GetKeys(); // sorted

        // Writes the file when something changed (atomically: temp file + rename).
        static bool Save();
        // Drops unsaved changes and reads the file again.
        static bool Reload();
        static bool IsDirty();

        // The file the current data belongs to.
        static std::filesystem::path GetFilePath();
        // <data home>/zamin/<project name> (the name is made file-system safe).
        static std::filesystem::path GetDirectoryForProject(std::string_view projectName);
        // <data home>/<company>/<product>; without a company <data home>/zamin/<product> (both names
        // made file-system safe).
        static std::filesystem::path GetDirectoryFor(std::string_view company, std::string_view product);
        // The save file in that folder (GetDirectoryFor / savedata.json).
        static std::filesystem::path GetFilePathFor(std::string_view company, std::string_view product);
        // Use a fixed file instead of the per-project location (tests, tools); an empty path
        // goes back to the automatic location. Unsaved data is saved to the old file first.
        static void SetFilePath(const std::filesystem::path& path);

        static const char* TypeToString(SaveValueType type);
    };

}
