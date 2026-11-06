#pragma once

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
