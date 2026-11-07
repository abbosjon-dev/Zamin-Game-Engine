#pragma once

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <format>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <deque>
#include <vector>

namespace Zamin {

    // Off is only a threshold ("nothing"): no message has it.
    enum class LogLevel : uint8_t { Trace = 0, Info, Warn, Error, Critical, Off };

    // The two loggers: the engine's ("ZAMIN": ZM_CORE_*) and the game's / editor's ("APP": ZM_*).
    enum class LogSource : uint8_t { Engine = 0, App = 1 };

    struct LogMessage
    {
        LogLevel Level = LogLevel::Info;
        bool FromClient = false;   // false = engine ("ZAMIN"), true = game/editor ("APP")
        std::string Time;
        std::string Text;
    };

    // Thread-safe logger: writes colored output to the terminal and keeps the
    // last N messages in memory so the editor console can display them.
    //
    // Levels (LogLevelSettings: --log-level, ZAMIN_LOG_LEVEL, Project settings > Player > Log, the
    // editor's Preferences > Konsol): each logger has its minimum level - a message below it is not
    // written anywhere (and the macros do not even format it); the log files have their own minimum on
    // top (SetFileLevel: e.g. the terminal and the console show Info, the file only warnings).
    // SetMinLevel is one more threshold over both loggers (tests silence expected errors with it).
    class Log
    {
    public:
        static void Write(LogLevel level, bool fromClient, std::string text);

        static void ForEach(const std::function<void(const LogMessage&)>& fn);
        // A copy of the kept messages (oldest first). `firstIndex`: the number the first of them
        // has among all messages written since start-up (messages keep their numbers).
        static std::vector<LogMessage> GetMessages(uint64_t* firstIndex = nullptr);
        static void Clear();
        static size_t GetMessageCount();
        // Changes with every message written and every Clear (the console rebuilds its view then).
        static uint64_t GetRevision();
        // How many messages were written since start-up (also those no longer kept).
        static uint64_t GetTotalWritten();
        static void SetMinLevel(LogLevel level) { s_MinLevel.store(level, std::memory_order_relaxed); }
        static LogLevel GetMinLevel() { return s_MinLevel.load(std::memory_order_relaxed); }
        // A logger's minimum level (Trace: everything, Off: nothing).
        static void SetLevel(LogSource source, LogLevel level) { s_SourceLevels[(int)source].store(level, std::memory_order_relaxed); }
        static LogLevel GetLevel(LogSource source) { return s_SourceLevels[(int)source].load(std::memory_order_relaxed); }
        // The minimum level of the lines written to the log files (LogFile); the console and the
        // terminal still get the others.
        static void SetFileLevel(LogLevel level) { s_FileLevel.store(level, std::memory_order_relaxed); }
        static LogLevel GetFileLevel() { return s_FileLevel.load(std::memory_order_relaxed); }
        // Whether a message of `level` from that logger would be written (the macros ask before formatting).
        static bool IsEnabled(LogLevel level, bool fromClient)
        {
            return level != LogLevel::Off && level >= s_MinLevel.load(std::memory_order_relaxed) &&
                   level >= s_SourceLevels[fromClient ? 1 : 0].load(std::memory_order_relaxed);
        }
        // Whether messages are printed to the terminal too (they are always kept for the console).
        static void SetTerminalOutput(bool enabled) { s_TerminalOutput = enabled; }
        static bool GetTerminalOutput() { return s_TerminalOutput; }

        static const char* LevelToString(LogLevel level);
        // "trace", "info", "warn", "error", "critical", "off" (settings files, --log-level).
        static const char* LevelToName(LogLevel level);
        // For people: "Trace", "Ma'lumot", "Ogohlantirish", "Xato", "Kritik", "O'chiq".
        static const char* LevelDisplayName(LogLevel level);
        // A level by its name (any case): trace / debug / verbose, info, warn / warning, error / err,
        // critical / fatal, off / none; the Uzbek names (ma'lumot, ogohlantirish, xato, kritik, o'chiq);
        // or its number 0 ... 5. Nothing for anything else.
        static std::optional<LogLevel> ParseLevel(std::string_view text);

        // ---- files (LogFile opens / rotates them): every message from then on is also written to
        // these (without colours, flushed at once - a crash loses nothing). Attaching first writes the
        // messages already kept (start-up before the file was opened) after `header`. The files belong
        // to the caller: DetachFiles hands them back (not closed).
        static void AttachFiles(std::FILE* primary, std::FILE* secondary, const std::string& header);
        static void DetachFiles(std::FILE** primary, std::FILE** secondary);
        // One message as a line of a log file: "[14:03:12] INFO  ZAMIN: text".
        static std::string FormatFileLine(const LogMessage& message);

        // ---- the last messages for a crash report (CrashHandler): kept in fixed buffers so that a
        // signal handler can read them without locking or allocating. `emit` gets each line (oldest
        // first, without a newline); a line may be cut at CrashLineSize - 1 bytes.
        static constexpr size_t CrashLineCount = 48, CrashLineSize = 320;
        static void ForEachRecentLineUnsafe(void (*emit)(const char* line, size_t length, void* user), void* user);
        // The attached files' descriptors (-1: none) - the crash handler appends its line with write().
        static void GetFileDescriptorsUnsafe(int* primary, int* secondary);

    private:
        static inline std::mutex s_Mutex;
        static inline std::deque<LogMessage> s_Messages;
        static inline uint64_t s_Revision = 0, s_TotalWritten = 0;
        static inline std::atomic<LogLevel> s_MinLevel{ LogLevel::Trace };
        static inline std::atomic<LogLevel> s_SourceLevels[2] = { LogLevel::Trace, LogLevel::Trace };
        static inline std::atomic<LogLevel> s_FileLevel{ LogLevel::Trace };
        static inline bool s_TerminalOutput = true;
        static constexpr size_t MaxMessages = 2000;
    };

}

// A message below its logger's level is neither formatted nor written.
#define ZM_LOG_IMPL(level, client, ...) \
    (::Zamin::Log::IsEnabled(level, client) ? ::Zamin::Log::Write(level, client, std::format(__VA_ARGS__)) : (void)0)

#define ZM_CORE_TRACE(...)    ZM_LOG_IMPL(::Zamin::LogLevel::Trace,    false, __VA_ARGS__)
#define ZM_CORE_INFO(...)     ZM_LOG_IMPL(::Zamin::LogLevel::Info,     false, __VA_ARGS__)
#define ZM_CORE_WARN(...)     ZM_LOG_IMPL(::Zamin::LogLevel::Warn,     false, __VA_ARGS__)
#define ZM_CORE_ERROR(...)    ZM_LOG_IMPL(::Zamin::LogLevel::Error,    false, __VA_ARGS__)
#define ZM_CORE_CRITICAL(...) ZM_LOG_IMPL(::Zamin::LogLevel::Critical, false, __VA_ARGS__)

#define ZM_TRACE(...)    ZM_LOG_IMPL(::Zamin::LogLevel::Trace,    true, __VA_ARGS__)
#define ZM_INFO(...)     ZM_LOG_IMPL(::Zamin::LogLevel::Info,     true, __VA_ARGS__)
#define ZM_WARN(...)     ZM_LOG_IMPL(::Zamin::LogLevel::Warn,     true, __VA_ARGS__)
#define ZM_ERROR(...)    ZM_LOG_IMPL(::Zamin::LogLevel::Error,    true, __VA_ARGS__)
#define ZM_CRITICAL(...) ZM_LOG_IMPL(::Zamin::LogLevel::Critical, true, __VA_ARGS__)
