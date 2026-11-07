#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Zamin {

    // The editor's layouts that come with it (Oyna > Joylashuv): built by code, always there, never
    // overwritten. FourViews splits the scene area into the viewport and three axis views.
    enum class BuiltInLayout : uint8_t { Default = 0, Mode2D, Animation, FourViews, Count };

    // A saved arrangement of the editor's windows: ImGui's window / docking settings as text (its
    // .ini format, SaveIniSettingsToMemory) and which of the editor's windows were open.
    struct EditorLayout
    {
        std::string Name;
        std::string Ini;
        std::map<std::string, bool> Panels;   // window key ("Hierarchy", "SceneView2") -> open
        int64_t SavedAt = 0;                  // seconds since the Unix epoch

        // Whether `key` was open (keys the layout does not name: `fallback`).
        bool IsPanelOpen(const std::string& key, bool fallback) const;
        std::string ToJson() const;
        // False (nothing changes) when the text is not a layout file. Without a name in the file
        // the name stays what it was (the file's).
        bool FromJson(const std::string& text);
    };

    // The user's layouts: one .zlayout file each in a folder of the editor's config directory
    // (~/.config/zamin/layouts). Names are compared without regard to letter case; built-in
    // layouts' names are reserved.
    class EditorLayoutLibrary
    {
    public:
        static constexpr const char* Extension = ".zlayout";
        static constexpr size_t MaxNameLength = 48;  // bytes

        explicit EditorLayoutLibrary(std::filesystem::path directory = {}) : m_Directory(std::move(directory)) {}

        void SetDirectory(std::filesystem::path directory);
        const std::filesystem::path& GetDirectory() const { return m_Directory; }

        // Reads the folder's layout files again: the names, sorted (letter case aside). Returns how many.
        size_t Refresh();
        const std::vector<std::string>& GetNames() const { return m_Names; }
        bool Contains(std::string_view name) const;
        // When a listed layout was saved (read by Refresh; 0: unknown).
        int64_t GetSavedAt(std::string_view name) const;

        // Writes the layout (one of the same name - any letter case - is replaced, keeping the new
        // spelling). False: an invalid name or the file could not be written.
        bool Save(const EditorLayout& layout, std::string* error = nullptr);
        std::optional<EditorLayout> Load(std::string_view name) const;
        bool Remove(std::string_view name);
        // False: `from` is not there, `to` is invalid or taken by another layout.
        bool Rename(std::string_view from, std::string_view to, std::string* error = nullptr);

        // The name trimmed, runs of white space made one space.
        static std::string CleanName(std::string_view name);
        // Not empty, at most MaxNameLength bytes, not starting with a dot, none of / \ : * ? " < > |
        // or control characters, not a built-in layout's name. `reason` says why not (Uzbek).
        static bool IsValidName(std::string_view name, std::string* reason = nullptr);
        // `base` (cleaned) when it is free, else "base 2", "base 3" ...
        std::string MakeUniqueName(std::string_view base) const;
        std::filesystem::path GetPath(std::string_view name) const;

        static const char* GetBuiltInName(BuiltInLayout layout);  // "Standart", "2D", "Animatsiya", "4 ko'rinish"
        static const char* GetBuiltInKey(BuiltInLayout layout);   // "Default", "2D", "Animation", "FourViews"
        // A built-in layout by its name or key (any letter case).
        static std::optional<BuiltInLayout> FindBuiltIn(std::string_view nameOrKey);

    private:
        // The stored spelling of a name that matches `name` (letter case aside); empty: none.
        std::string FindStored(std::string_view name) const;

    private:
        std::filesystem::path m_Directory;
        std::vector<std::string> m_Names;
        std::map<std::string, int64_t> m_SavedAt;
    };

}
