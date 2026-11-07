#include "Zamin/Editor/ConsoleFilter.h"
#include "Zamin/Utils/StringUtils.h"

#include <algorithm>
#include <format>
#include <unordered_map>

namespace Zamin {

    static char ToLowerAscii(char c) { return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c; }

    static std::string LowerAscii(std::string_view text)
    {
        std::string out(text);
        for (char& c : out)
            c = ToLowerAscii(c);
        return out;
    }

    bool ContainsIgnoreCase(std::string_view text, std::string_view needle)
    {
        return StringUtils::ContainsIgnoreCase(text, needle); // (an empty needle is found at 0)
    }

    bool ConsoleFilterSettings::AcceptsLevel(LogLevel level) const
    {
        switch (level)
        {
            case LogLevel::Trace: return ShowTrace;
            case LogLevel::Info:  return ShowInfo;
            case LogLevel::Warn:  return ShowWarn;
            default:              return ShowError; // Error, Critical
        }
    }

    void ConsoleTextMatcher::Set(const std::string& text, bool regex, bool caseSensitive)
    {
        if (text == m_Text && regex == m_Regex && caseSensitive == m_CaseSensitive)
            return;
        m_Text = text;
        m_Regex = regex;
        m_CaseSensitive = caseSensitive;
        m_Lower = LowerAscii(text);
        m_Pattern.reset();
        m_LongPattern.reset();
        m_Error.clear();
        if (!regex || text.empty())
            return;
        auto flags = std::regex::ECMAScript | std::regex::optimize;
        if (!caseSensitive)
            flags |= std::regex::icase;
        try
        {
            m_Pattern = std::make_shared<std::regex>(text, flags);
        }
        catch (const std::regex_error& e)
        {
            m_Error = e.what();
            return;
        }
#if defined(__GLIBCXX__)
        // libstdc++'s polynomial executor (a Thompson NFA: all alternatives advance together, the
        // recursion only follows the pattern's empty transitions). Searching restarts it at every
        // position (quadratic); a lazy "anything" in front and a match from the first position
        // only is the same search in one pass. It has no back-references: those stay without.
        try
        {
            m_LongPattern = std::make_shared<std::regex>("[\\s\\S]*?(?:" + text + ")", flags | std::regex_constants::__polynomial);
        }
        catch (const std::regex_error&)
        {
            m_LongPattern.reset();
        }
#endif
    }

    bool ConsoleTextMatcher::Matches(std::string_view text) const
    {
        if (m_Text.empty())
            return true;
        if (m_Regex)
        {
            if (!m_Pattern)
                return true; // invalid pattern: no filtering (the console marks the field)
            if (text.size() <= RegexDirectLimit)
                return std::regex_search(text.begin(), text.end(), *m_Pattern);
            if (m_LongPattern)
            {
                const std::string_view head = text.substr(0, RegexTextLimit);
                return std::regex_search(head.begin(), head.end(), *m_LongPattern, std::regex_constants::match_continuous);
            }
            const std::string_view head = text.substr(0, RegexDirectLimit);
            return std::regex_search(head.begin(), head.end(), *m_Pattern);
        }
        if (m_CaseSensitive)
            return text.find(m_Text) != std::string_view::npos;
        return ContainsIgnoreCase(text, m_Lower);
    }

    ConsoleView BuildConsoleView(std::span<const LogMessage> messages, const ConsoleFilterSettings& filter, const ConsoleTextMatcher& matcher)
    {
        ConsoleView view;
        view.Rows.reserve(messages.size());
        // Collapse key: level, source and text (a message repeated from another source is another row).
        std::unordered_map<std::string, size_t> rowOf;
        std::string key;
        for (size_t i = 0; i < messages.size(); i++)
        {
            const LogMessage& m = messages[i];
            if (!matcher.Matches(m.Text))
                continue;
            const int level = std::clamp((int)m.Level, 0, (int)view.LevelCounts.size() - 1);
            view.LevelCounts[level]++;
            if (!filter.AcceptsLevel(m.Level))
                continue;
            if (filter.Collapse)
            {
                key.clear();
                key.push_back((char)('0' + level));
                key.push_back(m.FromClient ? 'A' : 'Z');
                key.append(m.Text);
                auto [it, inserted] = rowOf.try_emplace(key, view.Rows.size());
                if (!inserted)
                {
                    ConsoleRow& row = view.Rows[it->second];
                    row.Last = i;
                    row.Count++;
                    continue;
                }
            }
            view.Rows.push_back({ i, i, 1 });
        }
        return view;
    }

    std::string FormatConsoleMessage(const LogMessage& message, bool timestamp, uint32_t count)
    {
        std::string out;
        if (timestamp)
            out += std::format("[{}] ", message.Time);
        out += std::format("[{}] {}: {}", Log::LevelToString(message.Level), message.FromClient ? "APP" : "ZAMIN", message.Text);
        if (count > 1)
            out += std::format(" (x{})", count);
        return out;
    }

}
