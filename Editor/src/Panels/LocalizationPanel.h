#pragma once

#include <Zamin/Zamin.h>

#include <imgui.h>

#include <filesystem>
#include <string>
#include <vector>

namespace Zamin {

    // "Lokalizatsiya" window: the project's language files as one table - a row per key, a column per
    // language (Project settings > Localization). Missing translations are marked (and counted per
    // language), texts whose {0} placeholders differ from the default language's are warned about,
    // plural entries are edited form by form. Keys can be added, renamed, removed and filtered; keys the
    // open scene's Text components or the running game asked for but no file has are listed with an
    // "add" button. Save writes every <code>.json and the game / editor reload them.
    class LocalizationPanel
    {
    public:
        void OnImGuiRender(bool* show);
        void Focus() { m_FocusRequested = true; }
        // The scene whose Text components' keys are checked (the editor's active scene).
        void SetScene(const Ref<Scene>& scene) { m_Scene = scene; }
        // The language files changed on disk (hot reload): read again unless there are unsaved edits.
        void OnFilesChanged();
        // Another project / the settings' language list changed.
        void Invalidate() { m_Loaded = false; }
        bool IsDirty() const { return m_Dirty; }
        bool Save();
        // Automation (screenshots): the window floats at this rect; the key filter.
        void SetFloatingRect(const ImVec4& rect) { m_FloatingRect = rect; m_FloatingRequested = true; }
        void SetFilter(const std::string& filter) { m_Filter = filter; }

    private:
        void Load();
        void DrawToolbar();
        void DrawSuggestions();
        void DrawTable();
        // One cell: the text (or the plural forms' summary, a click edits them). True when it changed.
        bool DrawCell(const std::string& key, const std::string& language, float width);
        void DrawPluralPopup();
        std::vector<std::string> SceneKeys() const;

    private:
        LocalizationTable m_Table;
        std::vector<std::string> m_Keys; // sorted
        bool m_Loaded = false;
        bool m_Dirty = false;
        bool m_FocusRequested = false;
        const Project* m_LoadedFor = nullptr;
        // The folder the table was read from (Save writes there).
        std::filesystem::path m_LoadedDirectory;
        Ref<Scene> m_Scene;

        std::string m_Filter;
        bool m_OnlyMissing = false;
        std::string m_NewKey;
        std::string m_KeyError;
        // Renaming a key in place.
        std::string m_RenamingKey, m_RenameBuffer;
        bool m_FocusRename = false;
        // The plural forms popup.
        std::string m_PluralKey, m_PluralLanguage;
        bool m_OpenPlural = false;
        std::vector<std::string> m_LoadErrors;

        bool m_FloatingRequested = false;
        ImVec4 m_FloatingRect{ 0, 0, 0, 0 };
    };

}
