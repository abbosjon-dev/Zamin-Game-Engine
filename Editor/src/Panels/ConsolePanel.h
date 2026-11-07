#pragma once

#include <Zamin/Core/Log.h>
#include <Zamin/Editor/ConsoleFilter.h>
#include <Zamin/Editor/EditorPreferences.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Zamin {

    // The editor console: the log's messages with a text search (substring or regular expression,
    // case-insensitive by default), level toggles with counts, collapsing of identical messages
    // (a count badge), timestamps on / off, a click selects a message (its whole text below, Ctrl+C
    // or the context menu copies it).
    class ConsolePanel
    {
    public:
        void OnImGuiRender(bool* open);

        // The options kept between sessions (preferences.json): the editor hands them in at start-up
        // and saves them when ConsumePreferencesChanged says they changed here.
        void SetPreferences(const ConsolePreferences& preferences) { m_Prefs = preferences; }
        const ConsolePreferences& GetPreferences() const { return m_Prefs; }
        bool ConsumePreferencesChanged();
        // The options menu changed a logger's level (Log::SetLevel already applied): the editor keeps it.
        bool ConsumeLogLevelsChanged();

        void SetFilterText(const std::string& text) { m_FilterText = text; }
        // The console window (or its search field) had the keyboard focus last frame.
        bool IsFocused() const { return m_Focused; }
        // Automation (ZAMIN_EDITOR_CONSOLE): "filter=<text>;regex;case;collapse;trace;select=<n>;options"
        // (options: the options menu kept open, the console's tab in front).
        void ApplyAutomation(const std::string& spec);

    private:
        void RefreshView();
        void DrawToolbar();
        void DrawMessages();
        void DrawDetails(float height);
        void CopyRows(const std::vector<size_t>& rows, bool textOnly);
        ConsoleFilterSettings GetFilter() const;

    private:
        ConsolePreferences m_Prefs;
        bool m_PrefsChanged = false;
        bool m_LogLevelsChanged = false;
        bool m_OpenOptions = false;          // automation: the options menu kept open (screenshots)
        bool m_FocusWindow = false;          // automation: the console's window to the front once
        std::string m_FilterText;
        bool m_Regex = false, m_CaseSensitive = false;
        ConsoleTextMatcher m_Matcher;

        // The log as last read and what it shows (rebuilt when the log or the filter changes).
        std::vector<LogMessage> m_Messages;
        uint64_t m_FirstIndex = 0;          // the number of m_Messages[0] among all messages written
        ConsoleView m_View;
        uint64_t m_Revision = UINT64_MAX;
        ConsoleFilterSettings m_ViewFilter;

        // The selected row, by the number of its first message (stays while new messages arrive).
        uint64_t m_Selected = UINT64_MAX;
        int m_AutomationSelect = -1;        // automation: select this row once the view is built
        uint64_t m_LastTotal = 0;
        bool m_ScrollToBottom = false;
        bool m_ScrollToSelected = false;    // the list scrolls to the selected row once
        bool m_Focused = false;
    };

}
