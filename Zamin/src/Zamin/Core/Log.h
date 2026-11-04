#pragma once

#include <format>
#include <functional>
#include <mutex>
#include <string>
#include <deque>

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
        static void Clear();
        static size_t GetMessageCount();
        static void SetMinLevel(LogLevel level) { s_MinLevel = level; }

        static const char* LevelToString(LogLevel level);

    private:
        static inline std::mutex s_Mutex;
        static inline std::deque<LogMessage> s_Messages;
        static inline LogLevel s_MinLevel = LogLevel::Trace;
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
