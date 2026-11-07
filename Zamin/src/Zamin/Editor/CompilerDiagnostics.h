#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace Zamin {

    enum class DiagnosticSeverity : unsigned char { Note = 0, Warning, Error };

    // One message of a build (the editor's "Kompilyatsiya" window): GCC / Clang diagnostics with
    // their file, line and column, linker errors (GNU ld, gold, lld, mold) and CMake errors.
    struct CompilerDiagnostic
    {
        DiagnosticSeverity Severity = DiagnosticSeverity::Error;
        std::string File;                 // as the tool wrote it (ResolvePaths makes it absolute); "" = none
        int Line = 0, Column = 0;         // 1-based; 0 = unknown
        std::string Message;              // without the "error: " part and the [-W...] option
        std::string Option;               // the warning's option ("-Wunused-variable"; "" = none)
        bool Linker = false;              // from the linker (undefined references...)
        bool CMake = false;               // from CMake (configuring the build)
        std::vector<std::string> Context; // what the compiler said before it ("In member function ...", include chain)
        std::vector<std::string> Snippet; // the source lines and the caret it printed after it
        std::vector<CompilerDiagnostic> Notes; // the notes after it ("note: candidate is ...", "required from here")

        bool HasLocation() const { return !File.empty() && Line > 0; }
        // "file:line:col: error: message [-Woption]" (the location part left out when unknown).
        std::string Format() const;
        static const char* GetSeverityName(DiagnosticSeverity severity); // "xato", "ogohlantirish", "izoh"
    };

    // Reads a build's output line by line (as it comes, from any thread that owns the parser) and
    // collects its diagnostics. Notes, the source snippets and the context lines ("In function
    // ...", "In file included from ...") are attached to the message they belong to; the same
    // message printed again (a header's warning in every file including it) is kept once.
    class CompilerOutputParser
    {
    public:
        void AddLine(std::string_view line);
        // Text with several lines.
        void AddText(std::string_view text);
        // The end of the output (a CMake message still being read is kept).
        void Finish();
        void Clear();

        const std::vector<CompilerDiagnostic>& GetDiagnostics() const { return m_Diagnostics; }
        size_t GetCount(DiagnosticSeverity severity) const;
        size_t GetErrorCount() const { return GetCount(DiagnosticSeverity::Error); }
        size_t GetWarningCount() const { return GetCount(DiagnosticSeverity::Warning); }
        // Relative file names are made absolute from `directory` (where the tools ran: the build folder).
        void ResolvePaths(const std::filesystem::path& directory);

        // A diagnostic line "file:line[:col]: severity: message" (GCC / Clang; also "tool: error: ...").
        // False for other lines.
        static bool ParseDiagnosticLine(std::string_view line, CompilerDiagnostic& out);

    private:
        void Add(CompilerDiagnostic diagnostic);
        void AddNote(CompilerDiagnostic note);
        bool ParseLinkerLine(std::string_view line);
        bool ParseCMakeLine(std::string_view line);
        void FlushCMake();
        CompilerDiagnostic* Last();

    private:
        std::vector<CompilerDiagnostic> m_Diagnostics;
        std::vector<std::string> m_Context;      // context lines waiting for the next diagnostic
        std::unordered_set<std::string> m_Seen;  // diagnostics kept (Format()), to drop repeats
        bool m_SkipFollowing = false;            // the last diagnostic was a repeat: its notes / snippet too
        bool m_HasLast = false;                  // notes / snippet lines attach to the last diagnostic
        std::vector<CompilerDiagnostic> m_PendingNotes; // notes before their message (GCC's "required from here")
        bool m_LastIsNote = false;               // snippet lines go to the last diagnostic's last note
        std::string m_LinkerFunction;            // ld: "in function `X'"
        bool m_LinkerFailed = false;             // a linker error was seen (the summary lines are left out)
        bool m_InCMake = false;                  // reading a CMake message's lines
        CompilerDiagnostic m_CMake;
    };

}
