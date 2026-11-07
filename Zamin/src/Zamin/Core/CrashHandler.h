#pragma once

#include <ctime>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Zamin {

    struct CrashHandlerSettings
    {
        // Where crash-<date>.txt is written (the player: its log folder). Made when needed; when it
        // cannot be written, the system's temp folder.
        std::filesystem::path ReportDirectory;
        std::string Application = "Zamin";   // the program ("Zamin Player")
        std::string Product, Version;        // the game
        // This run's log file, named in the report ("" when none).
        std::filesystem::path LogFile;
        // After the report: a window that says where it is. Tried in order: zenity, kdialog, then
        // DialogProgram (with DialogArguments and the report's path after them), then xmessage; when none
        // of them can be started only the terminal hears about it. The window is a separate process
        // (the crashed game exits at once).
        bool ShowDialog = true;
        std::filesystem::path DialogProgram;
        std::vector<std::string> DialogArguments;
    };

    // What a crash report says (the dialog reads it back).
    struct CrashReportSummary
    {
        std::string Application, Product, Version, Engine, Time, Reason, LogFile;
        std::vector<std::string> Backtrace;  // the frames, as written
        std::vector<std::string> LastLog;    // the last log lines before the crash
    };

    // Catches fatal signals (SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS) and uncaught C++ exceptions
    // (std::terminate) and writes a crash report: the reason (the signal, its code and address, or the
    // exception's type and message), the backtrace, the executable's loaded modules and the last log
    // lines. Then it starts the dialog (see CrashHandlerSettings) and lets the signal end the program as
    // it would have (the exit status and core dumps stay the same).
    //
    // The signal handler only uses async-signal-safe calls: everything it needs (the report's header,
    // the dialog commands, the folder) is prepared by Install; the last log lines come from Log's fixed
    // buffers; the backtrace from backtrace() / backtrace_symbols_fd() (loaded beforehand); the dialog
    // is started with a raw clone + execve (no atfork handlers, no allocation). A second fatal signal
    // while the report is being written (another thread, or the handler itself) ends the program at
    // once with the default action.
    class CrashHandler
    {
    public:
        // Prepares and installs the handlers (replaces earlier settings). False when not supported.
        static bool Install(const CrashHandlerSettings& settings);
        // The previous signal handlers / terminate handler back.
        static void Uninstall();
        static bool IsInstalled();
        // Prepares the report's header and the dialog commands without installing anything (tests).
        static void Configure(const CrashHandlerSettings& settings);
        static const CrashHandlerSettings& GetSettings();

        // Writes a report to `fd` the way the handler does (async-signal-safe after Configure).
        // `exception`: the uncaught exception's description (nullptr for a signal).
        static void WriteReport(int fd, int signal, int code, const void* address, const char* exception);

        // "SIGSEGV" ...; "signal N" for others.
        static const char* GetSignalName(int signal);
        // What the signal means, in Uzbek ("xotiraga noto'g'ri murojaat (segmentation fault)").
        static const char* DescribeSignal(int signal);
        // "crash-2026-11-07_14-03-12.txt" for that local time.
        static std::string MakeReportFileName(std::time_t time);

        // A backtrace line with its C++ symbol readable ("libZamin.so(_ZN5Zamin3FooEv+0x1a) [0x..]" ->
        // "libZamin.so(Zamin::Foo()+0x1a) [0x..]"); other lines as they are.
        static std::string DemangleBacktraceLine(std::string_view line);
        // Demangles the backtrace of a report file in place (the dialog does it: the crashed program
        // cannot). How many lines changed.
        static size_t DemangleReport(const std::filesystem::path& report);
        static bool ReadReport(const std::filesystem::path& report, CrashReportSummary& out);

        // The commands the dialog would be tried with for a report (zenity / kdialog only when found in
        // PATH, the dialog program when set, xmessage when found), each with its arguments.
        static std::vector<std::vector<std::string>> GetDialogCommands(const std::filesystem::path& report);
        // The dialog's text ("<product> kutilmaganda to'xtadi ... Hisobot: <path>").
        static std::string MakeDialogMessage(const std::filesystem::path& report);

        // Why the program is about to abort (a failed ZM_ASSERT: Assert calls it right before
        // std::abort()): the report's "Sabab" line names it instead of only SIGABRT. The first reason
        // set stays; one line (newlines become spaces), at most ~2 KB.
        static void SetAbortReason(std::string_view reason);
        static std::string GetAbortReason();   // "" when none was set
        static void ClearAbortReason();

        // The report written by this process ("" before a crash; tests read it in a forked child).
        static std::filesystem::path GetLastReportPath();
    };

}
