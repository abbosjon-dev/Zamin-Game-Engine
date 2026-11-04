#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Zamin {

    enum class SaveValueType { None = 0, Int, Float, String, Bool };

    // Persistent key / value storage for game data (settings, records, progress), like
    // Unity's PlayerPrefs. Values live in memory and are written as JSON to
    //     $XDG_DATA_HOME/zamin/<project>/savedata.json   (default ~/.local/share/zamin/<project>/)
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
        // Use a fixed file instead of the per-project location (tests, tools); an empty path
        // goes back to the automatic location. Unsaved data is saved to the old file first.
        static void SetFilePath(const std::filesystem::path& path);

        static const char* TypeToString(SaveValueType type);
    };

}
