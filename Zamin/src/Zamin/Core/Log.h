#pragma once

#include <cstdio>
#include <format>
#include <functional>
#include <mutex>
#include <string>
#include <deque>
#include <vector>

namespace Zamin {

    enum class LogLevel : uint8_t { Trace = 0, Info, Warn, Error, Critical };

    struct LogMessage
    {
        LogLevel Level = LogLevel::Info;
        bool FromClient = false;   // false = engine ("ZAMIN"), true = game/editor ("APP")
        std::string Time;
        std::string Text;
    };

    // Thread-safe logger: writes colored output to the terminal and keeps the
    // last N messages in memory so the editor console can display them.
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
        static void SetMinLevel(LogLevel level) { s_MinLevel = level; }
        static LogLevel GetMinLevel() { return s_MinLevel; }
        // Whether messages are printed to the terminal too (they are always kept for the console).
        static void SetTerminalOutput(bool enabled) { s_TerminalOutput = enabled; }
        static bool GetTerminalOutput() { return s_TerminalOutput; }

        static const char* LevelToString(LogLevel level);

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
        static inline LogLevel s_MinLevel = LogLevel::Trace;
        static inline bool s_TerminalOutput = true;
        static constexpr size_t MaxMessages = 2000;
    };

}

#define ZM_CORE_TRACE(...)    ::Zamin::Log::Write(::Zamin::LogLevel::Trace,    false, std::format(__VA_ARGS__))
#define ZM_CORE_INFO(...)     ::Zamin::Log::Write(::Zamin::LogLevel::Info,     false, std::format(__VA_ARGS__))
#define ZM_CORE_WARN(...)     ::Zamin::Log::Write(::Zamin::LogLevel::Warn,     false, std::format(__VA_ARGS__))
#define ZM_CORE_ERROR(...)    ::Zamin::Log::Write(::Zamin::LogLevel::Error,    false, std::format(__VA_ARGS__))
#define ZM_CORE_CRITICAL(...) ::Zamin::Log::Write(::Zamin::LogLevel::Critical, false, std::format(__VA_ARGS__))

#define ZM_TRACE(...)    ::Zamin::Log::Write(::Zamin::LogLevel::Trace,    true, std::format(__VA_ARGS__))
#define ZM_INFO(...)     ::Zamin::Log::Write(::Zamin::LogLevel::Info,     true, std::format(__VA_ARGS__))
#define ZM_WARN(...)     ::Zamin::Log::Write(::Zamin::LogLevel::Warn,     true, std::format(__VA_ARGS__))
#define ZM_ERROR(...)    ::Zamin::Log::Write(::Zamin::LogLevel::Error,    true, std::format(__VA_ARGS__))
#define ZM_CRITICAL(...) ::Zamin::Log::Write(::Zamin::LogLevel::Critical, true, std::format(__VA_ARGS__))
