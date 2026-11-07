#include "Zamin/Editor/CompilerDiagnostics.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>

namespace fs = std::filesystem;

namespace Zamin {

    namespace {
        constexpr size_t s_MaxSnippetLines = 8;

        std::string_view Trim(std::string_view s)
        {
            while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
                s.remove_prefix(1);
            while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n'))
                s.remove_suffix(1);
            return s;
        }

        // Without the terminal colour codes (ESC [ ... m) some tools print even into a pipe.
        std::string StripAnsi(std::string_view s)
        {
            std::string out;
            out.reserve(s.size());
            for (size_t i = 0; i < s.size(); i++)
            {
                if (s[i] == '\x1b' && i + 1 < s.size() && s[i + 1] == '[')
                {
                    i += 2;
                    while (i < s.size() && !std::isalpha((unsigned char)s[i]))
                        i++;
                    continue;
                }
                out += s[i];
            }
            while (!out.empty() && (out.back() == '\r' || out.back() == '\n'))
                out.pop_back();
            return out;
        }

        bool ToInt(std::string_view s, int& out)
        {
            if (s.empty() || s.size() > 9)
                return false;
            for (char c : s)
                if (!std::isdigit((unsigned char)c))
                    return false;
            std::from_chars(s.data(), s.data() + s.size(), out);
            return true;
        }

        // "file:line:col", "file:line" or something else (a tool's name): false when no line is in it.
        bool ParseLocation(std::string_view text, std::string& file, int& line, int& column)
        {
            size_t last = text.rfind(':');
            if (last == std::string_view::npos)
                return false;
            int a = 0, b = 0;
            if (!ToInt(text.substr(last + 1), b))
                return false;
            std::string_view rest = text.substr(0, last);
            size_t second = rest.rfind(':');
            if (second != std::string_view::npos && ToInt(rest.substr(second + 1), a))
            {
                file = std::string(rest.substr(0, second));
                line = a;
                column = b;
            }
            else
            {
                file = std::string(rest);
                line = b;
                column = 0;
            }
            return !file.empty() && line > 0;
        }

        // "message [-Wunused-variable]" -> the option apart.
        void SplitOption(std::string& message, std::string& option)
        {
            if (message.empty() || message.back() != ']')
                return;
            size_t open = message.rfind(" [-");
            if (open == std::string::npos)
                return;
            option = message.substr(open + 2, message.size() - open - 3);
            message.resize(open);
        }

        bool IsLinkerTool(std::string_view tool)
        {
            size_t slash = tool.find_last_of('/');
            std::string_view name = slash == std::string_view::npos ? tool : tool.substr(slash + 1);
            if (name == "mold" || name == "lld" || name == "collect2" || name == "ld64.lld")
                return true;
            // ld, ld.bfd, ld.gold, ld.lld, x86_64-linux-gnu-ld(.gold)
            return name == "ld" || name.starts_with("ld.") || name.ends_with("-ld") || name.find("-ld.") != std::string_view::npos;
        }

        bool IsObjectFile(std::string_view file)
        {
            return file.ends_with(".o") || file.ends_with(".obj") || file.ends_with(".a") || file.ends_with(".so") || file.find(".so.") != std::string_view::npos;
        }

        // A linker's "where": "/src/Player.cpp:42:(.text+0x2a)", "Player.cpp:(.text+0x1f)", "obj.o".
        void ParseLinkerLocation(std::string_view where, CompilerDiagnostic& d)
        {
            // The section part: ":(.text+0x2a)" / "(.text+0x2a)".
            if (size_t paren = where.find('('); paren != std::string_view::npos)
            {
                where = where.substr(0, paren);
                if (!where.empty() && where.back() == ':')
                    where.remove_suffix(1);
            }
            std::string file;
            int line = 0, column = 0;
            if (ParseLocation(where, file, line, column) && !IsObjectFile(file))
            {
                d.File = file;
                d.Line = line;
                d.Column = column;
            }
            else if (!where.empty() && !IsObjectFile(where) && where.find('.') != std::string_view::npos)
                d.File = std::string(where); // a source without its line
        }
    }

    // ------------------------------------------------------------------ diagnostics
    const char* CompilerDiagnostic::GetSeverityName(DiagnosticSeverity severity)
    {
        switch (severity)
        {
            case DiagnosticSeverity::Error: return "xato";
            case DiagnosticSeverity::Warning: return "ogohlantirish";
            default: return "izoh";
        }
    }

    std::string CompilerDiagnostic::Format() const
    {
        std::string s;
        if (!File.empty())
        {
            s = File;
            if (Line > 0)
                s += ":" + std::to_string(Line);
            if (Line > 0 && Column > 0)
                s += ":" + std::to_string(Column);
            s += ": ";
        }
        s += Severity == DiagnosticSeverity::Error ? "error: " : Severity == DiagnosticSeverity::Warning ? "warning: " : "note: ";
        s += Message;
        if (!Option.empty())
            s += " [" + Option + "]";
        return s;
    }

    bool CompilerOutputParser::ParseDiagnosticLine(std::string_view line, CompilerDiagnostic& out)
    {
        struct Marker
        {
            std::string_view Text;
            DiagnosticSeverity Severity;
        };
        static constexpr std::array<Marker, 5> s_Markers = { {
            { "fatal error: ", DiagnosticSeverity::Error },
            { "internal compiler error: ", DiagnosticSeverity::Error },
            { "error: ", DiagnosticSeverity::Error },
            { "warning: ", DiagnosticSeverity::Warning },
            { "note: ", DiagnosticSeverity::Note },
        } };
        // The earliest ": <severity>: " (a message can quote another one later on the line); a line
        // that starts with the severity has no location at all.
        size_t best = std::string_view::npos;
        const Marker* found = nullptr;
        for (const Marker& m : s_Markers)
        {
            if (line.starts_with(m.Text))
            {
                best = 0;
                found = &m;
                break;
            }
            std::string sep = ": " + std::string(m.Text);
            size_t at = line.find(sep);
            if (at != std::string_view::npos && (best == std::string_view::npos || at + 2 < best))
            {
                best = at + 2;
                found = &m;
            }
        }
        if (!found)
            return false;
        CompilerDiagnostic d;
        d.Severity = found->Severity;
        d.Message = std::string(Trim(line.substr(best + found->Text.size())));
        SplitOption(d.Message, d.Option);
        if (best > 0)
        {
            std::string_view prefix = line.substr(0, best - 2);
            std::string file;
            int l = 0, c = 0;
            if (ParseLocation(prefix, file, l, c))
            {
                d.File = std::move(file);
                d.Line = l;
                d.Column = c;
            }
            else if (prefix.find(' ') != std::string_view::npos)
                return false; // prose that happens to contain ": error: "
            else if (prefix.find('.') != std::string_view::npos && prefix.find('/') != std::string_view::npos)
                d.File = std::string(prefix); // a file without a line ("main.cpp: fatal error: ...")
            // else: a tool's own message ("g++: error: ...", "cc1plus: warning: ...")
        }
        out = std::move(d);
        return true;
    }

    CompilerDiagnostic* CompilerOutputParser::Last()
    {
        return m_Diagnostics.empty() ? nullptr : &m_Diagnostics.back();
    }

    void CompilerOutputParser::Add(CompilerDiagnostic d)
    {
        d.Context.insert(d.Context.begin(), m_Context.begin(), m_Context.end());
        m_Context.clear();
        if (!m_PendingNotes.empty())
        {
            d.Notes.insert(d.Notes.begin(), m_PendingNotes.begin(), m_PendingNotes.end());
            m_PendingNotes.clear();
        }
        m_HasLast = true;
        m_LastIsNote = false;
        if (!m_Seen.insert(d.Format()).second)
        {
            m_SkipFollowing = true; // the same message again (another file includes the header)
            return;
        }
        m_SkipFollowing = false;
        if (d.Linker && d.Severity == DiagnosticSeverity::Error)
            m_LinkerFailed = true;
        m_Diagnostics.push_back(std::move(d));
    }

    void CompilerOutputParser::AddNote(CompilerDiagnostic note)
    {
        note.Severity = DiagnosticSeverity::Note;
        if (!m_HasLast || m_Diagnostics.empty())
        {
            m_PendingNotes.push_back(std::move(note)); // before its message (template instantiations)
            return;
        }
        if (!m_SkipFollowing)
        {
            Last()->Notes.push_back(std::move(note));
            m_LastIsNote = true;
        }
    }

    bool CompilerOutputParser::ParseLinkerLine(std::string_view line)
    {
        // lld / mold: ">>> referenced by Player.cpp:42 (/abs/Source/Player.cpp:42)" under "undefined symbol".
        if (line.starts_with(">>>"))
        {
            CompilerDiagnostic* last = Last();
            if (!last || !last->Linker || m_SkipFollowing)
                return true;
            std::string_view rest = Trim(line.substr(3));
            if (rest.starts_with("referenced by ") && !last->HasLocation())
            {
                std::string_view where = rest.substr(14);
                if (size_t open = where.find(" ("); open != std::string_view::npos && where.back() == ')')
                    where = where.substr(open + 2, where.size() - open - 3); // the full path in brackets
                std::string file;
                int l = 0, c = 0;
                if (ParseLocation(Trim(where), file, l, c) && !IsObjectFile(file))
                {
                    last->File = file;
                    last->Line = l;
                    last->Column = c;
                }
            }
            if (last->Snippet.size() < s_MaxSnippetLines)
                last->Snippet.emplace_back(line);
            return true;
        }

        // "clang++: error: linker command failed with exit code 1": the summary of what came before.
        if (line.find(": error: linker command failed") != std::string_view::npos)
        {
            if (!m_LinkerFailed)
            {
                CompilerDiagnostic d;
                d.Linker = true;
                d.Message = "linker command failed";
                Add(std::move(d));
            }
            return true;
        }

        size_t colon = line.find(": ");
        std::string_view rest;
        bool prefixed = false;
        if (colon != std::string_view::npos && line.substr(0, colon).find(' ') == std::string_view::npos && IsLinkerTool(line.substr(0, colon)))
        {
            prefixed = true;
            rest = line.substr(colon + 2);
            // collect2: "error: ld returned 1 exit status" - only when nothing else said why.
            if (line.substr(0, colon).ends_with("collect2"))
            {
                if (!m_LinkerFailed)
                {
                    CompilerDiagnostic d;
                    d.Linker = true;
                    d.Message = std::string(rest.starts_with("error: ") ? rest.substr(7) : rest);
                    Add(std::move(d));
                }
                return true;
            }
        }
        else if (line.find("undefined reference to") != std::string_view::npos || line.find("multiple definition of") != std::string_view::npos)
            rest = line; // older ld: no "ld:" in front
        else
            return false;

        CompilerDiagnostic d;
        d.Linker = true;
        if (rest.starts_with("warning: "))
        {
            d.Severity = DiagnosticSeverity::Warning;
            d.Message = std::string(rest.substr(9));
            Add(std::move(d));
            return true;
        }
        if (rest.starts_with("error: "))
            rest.remove_prefix(7);

        // "<where>: <message>": the place has no spaces ("/src/Player.cpp:42:(.text+0x2a)", "obj.o").
        size_t split = rest.find(": ");
        std::string_view where, message = rest;
        if (split != std::string_view::npos && rest.substr(0, split).find(' ') == std::string_view::npos)
        {
            where = rest.substr(0, split);
            message = rest.substr(split + 2);
        }
        // "obj.o: in function `Player::OnUpdate(Zamin::Timestep)':" names the function the next lines are in.
        if (message.starts_with("in function ") || message.starts_with("In function "))
        {
            m_LinkerFunction = std::string(Trim(message.substr(12)));
            if (!m_LinkerFunction.empty() && m_LinkerFunction.back() == ':')
                m_LinkerFunction.pop_back();
            m_HasLast = false;
            return true;
        }
        if (!prefixed && where.empty())
            return false;
        ParseLinkerLocation(where, d);
        d.Severity = DiagnosticSeverity::Error;
        d.Message = std::string(Trim(message));
        if (!m_LinkerFunction.empty())
            d.Context.push_back("in function " + m_LinkerFunction);
        Add(std::move(d));
        return true;
    }

    bool CompilerOutputParser::ParseCMakeLine(std::string_view line)
    {
        // "CMake Error at CMakeLists.txt:12 (add_library):", "CMake Warning (dev) at x.cmake:3 (message):",
        // "CMake Error: The source directory ... does not exist."
        if (!line.starts_with("CMake Error") && !line.starts_with("CMake Warning") && !line.starts_with("CMake Deprecation Warning"))
            return false;
        FlushCMake();
        m_CMake = CompilerDiagnostic();
        m_CMake.CMake = true;
        m_CMake.Severity = line.starts_with("CMake Error") ? DiagnosticSeverity::Error : DiagnosticSeverity::Warning;
        if (size_t at = line.find(" at "); at != std::string_view::npos && line.ends_with(":"))
        {
            std::string_view where = line.substr(at + 4);
            where.remove_suffix(1);
            if (size_t paren = where.find(" ("); paren != std::string_view::npos)
            {
                m_CMake.Context.push_back(std::string(where.substr(paren + 1)));
                where = where.substr(0, paren);
            }
            std::string file;
            int l = 0, c = 0;
            if (ParseLocation(where, file, l, c))
            {
                m_CMake.File = file;
                m_CMake.Line = l;
            }
        }
        else if (size_t colon = line.find(": "); colon != std::string_view::npos)
            m_CMake.Message = std::string(Trim(line.substr(colon + 2)));
        m_InCMake = true;
        return true;
    }

    void CompilerOutputParser::FlushCMake()
    {
        if (!m_InCMake)
            return;
        m_InCMake = false;
        if (m_CMake.Message.empty())
            m_CMake.Message = m_CMake.Severity == DiagnosticSeverity::Error ? "CMake xatosi" : "CMake ogohlantirishi";
        Add(std::move(m_CMake));
        m_CMake = CompilerDiagnostic();
        m_HasLast = false;
    }

    void CompilerOutputParser::AddLine(std::string_view raw)
    {
        const std::string clean = StripAnsi(raw);
        std::string_view line = clean;

        // A CMake message: its indented lines, until a blank or unindented one.
        if (m_InCMake)
        {
            if (!line.empty() && (line.starts_with("  ") || line.starts_with("\t")))
            {
                std::string_view text = Trim(line);
                if (!m_CMake.Message.empty())
                    m_CMake.Message += ' ';
                m_CMake.Message += text;
                return;
            }
            if (Trim(line).empty() && m_CMake.Message.empty())
                return; // the blank line after the header
            FlushCMake();
        }
        if (Trim(line).empty())
            return;
        if (ParseCMakeLine(line))
            return;

        // The build tools' own lines end what came before (progress, failed commands).
        std::string_view trimmed = Trim(line);
        if (trimmed.starts_with("FAILED: ") || trimmed.starts_with("ninja: ") || trimmed.starts_with("-- ") ||
            (trimmed.starts_with("[") && trimmed.find("] ") != std::string_view::npos && std::isdigit((unsigned char)trimmed[1])) ||
            (trimmed.starts_with("make") && trimmed.find("***") != std::string_view::npos) || trimmed.starts_with("gmake"))
        {
            m_HasLast = false;
            m_Context.clear();
            m_LinkerFunction.clear();
            return;
        }

        if (ParseLinkerLine(line))
            return;

        CompilerDiagnostic d;
        if (ParseDiagnosticLine(line, d))
        {
            if (d.Severity == DiagnosticSeverity::Note)
                AddNote(std::move(d));
            else
                Add(std::move(d));
            return;
        }

        // "file:line:col:   required from here" (GCC's template instantiation trail).
        if (size_t gap = line.find(":   "); gap != std::string_view::npos)
        {
            std::string file;
            int l = 0, c = 0;
            if (ParseLocation(line.substr(0, gap), file, l, c))
            {
                CompilerDiagnostic note;
                note.File = std::move(file);
                note.Line = l;
                note.Column = c;
                note.Message = std::string(Trim(line.substr(gap + 1)));
                AddNote(std::move(note));
                return;
            }
        }

        // Context lines: "file.cpp: In member function 'void A::f()':", "In file included from a.h:3,",
        // "                 from b.cpp:1:", "file.cpp: At global scope:".
        if (trimmed.starts_with("In file included from ") || (trimmed.starts_with("from ") && (trimmed.ends_with(",") || trimmed.ends_with(":"))) ||
            (trimmed.ends_with(":") && (trimmed.find(": In ") != std::string_view::npos || trimmed.find(": At global scope") != std::string_view::npos)))
        {
            m_Context.emplace_back(trimmed);
            m_HasLast = false;
            return;
        }

        // The source lines and the caret after a message ("   12 |   foo();", "      |   ^~~").
        if (m_HasLast && (line.front() == ' ' || line.front() == '\t'))
        {
            if (m_SkipFollowing || m_Diagnostics.empty())
                return;
            CompilerDiagnostic& last = *Last();
            std::vector<std::string>& snippet = m_LastIsNote && !last.Notes.empty() ? last.Notes.back().Snippet : last.Snippet;
            if (snippet.size() < s_MaxSnippetLines)
                snippet.emplace_back(line);
            return;
        }
        m_HasLast = false;
    }

    void CompilerOutputParser::AddText(std::string_view text)
    {
        size_t start = 0;
        while (start < text.size())
        {
            size_t end = text.find('\n', start);
            if (end == std::string_view::npos)
                end = text.size();
            AddLine(text.substr(start, end - start));
            start = end + 1;
        }
    }

    void CompilerOutputParser::Finish()
    {
        FlushCMake();
        for (CompilerDiagnostic& note : m_PendingNotes)
        {
            note.Severity = DiagnosticSeverity::Note;
            m_Diagnostics.push_back(std::move(note));
        }
        m_PendingNotes.clear();
        m_Context.clear();
        m_HasLast = false;
    }

    void CompilerOutputParser::Clear()
    {
        *this = CompilerOutputParser();
    }

    size_t CompilerOutputParser::GetCount(DiagnosticSeverity severity) const
    {
        return (size_t)std::count_if(m_Diagnostics.begin(), m_Diagnostics.end(), [&](const CompilerDiagnostic& d) { return d.Severity == severity; });
    }

    void CompilerOutputParser::ResolvePaths(const fs::path& directory)
    {
        auto resolve = [&](CompilerDiagnostic& d) {
            if (d.File.empty())
                return;
            fs::path p(d.File);
            if (p.is_relative())
                d.File = (directory / p).lexically_normal().string();
        };
        for (CompilerDiagnostic& d : m_Diagnostics)
        {
            resolve(d);
            for (CompilerDiagnostic& n : d.Notes)
                resolve(n);
        }
    }

}
