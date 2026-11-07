#pragma once

#include "Zamin/Core/Log.h"

#include <array>
#include <cstdint>
#include <memory>
#include <regex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Zamin {

    // What the editor console shows: a text search (a case-insensitive substring by default, or a
    // regular expression), the message levels and whether identical messages are collapsed.
    struct ConsoleFilterSettings
    {
        std::string Text;
        bool Regex = false;
        bool CaseSensitive = false;
        bool ShowTrace = false, ShowInfo = true, ShowWarn = true, ShowError = true;
        // Identical messages (same level, source and text) shown once, with how many there were.
        bool Collapse = false;

        // Error covers Critical too.
        bool AcceptsLevel(LogLevel level) const;
        bool operator==(const ConsoleFilterSettings&) const = default;
    };

    // The filter's text, prepared once (lower-cased / compiled) and matched against messages.
    class ConsoleTextMatcher
    {
    public:
        // std::regex backtracks recursively (in libstdc++ a stack frame or more per character), so a
        // long message could overflow the stack. Messages up to RegexDirectLimit bytes are searched
        // as they are; a longer one in a single pass that keeps every alternative at once (stack
        // depth by the pattern only, time linear in the text), up to its first RegexTextLimit bytes.
        // Where that is not available (a back-reference, another standard library) only the first
        // RegexDirectLimit bytes of a long message are searched.
        static constexpr size_t RegexDirectLimit = 1024;
        static constexpr size_t RegexTextLimit = 16 * 1024;

        void Set(const std::string& text, bool regex, bool caseSensitive);
        // Empty text matches everything; an invalid regular expression too (IsValid tells).
        bool Matches(std::string_view text) const;
        bool IsEmpty() const { return m_Text.empty(); }
        bool IsValid() const { return m_Error.empty(); }
        const std::string& GetError() const { return m_Error; }
        // Long messages are matched in full (up to RegexTextLimit), not only their beginning.
        bool HasLongTextPattern() const { return m_LongPattern != nullptr; }

    private:
        std::string m_Text, m_Lower;
        bool m_Regex = false, m_CaseSensitive = false;
        std::shared_ptr<std::regex> m_Pattern;
        std::shared_ptr<std::regex> m_LongPattern; // the single-pass form for long messages
        std::string m_Error;
    };

    // One line of the console: a message, or (collapsed) every message identical to it.
    struct ConsoleRow
    {
        size_t First = 0;    // index of the first such message
        size_t Last = 0;     // the latest one (its time is shown); First when not collapsed
        uint32_t Count = 1;
    };

    struct ConsoleView
    {
        std::vector<ConsoleRow> Rows;
        // Messages per level (LogLevel order) that match the text, whatever the level toggles.
        std::array<size_t, 5> LevelCounts{};
        size_t ErrorCount() const { return LevelCounts[(int)LogLevel::Error] + LevelCounts[(int)LogLevel::Critical]; }
    };

    // The rows to show for `messages` (oldest first): those whose level is on and whose text
    // matches; collapsed, a row sits where its first message is and counts all identical ones.
    ConsoleView BuildConsoleView(std::span<const LogMessage> messages, const ConsoleFilterSettings& filter, const ConsoleTextMatcher& matcher);

    // Case-insensitive (ASCII letters) substring search; an empty needle is found everywhere.
    bool ContainsIgnoreCase(std::string_view text, std::string_view needle);
    // One message as a line for the clipboard: "[12:30:01] [WARN] APP: text" (time optional).
    std::string FormatConsoleMessage(const LogMessage& message, bool timestamp, uint32_t count = 1);

}
