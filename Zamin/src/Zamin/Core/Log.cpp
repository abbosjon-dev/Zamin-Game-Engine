#include "Zamin/Core/Log.h"

#include <chrono>
#include <cstdio>
#include <ctime>

namespace Zamin {

    const char* Log::LevelToString(LogLevel level)
    {
        switch (level)
        {
            case LogLevel::Trace:    return "TRACE";
            case LogLevel::Info:     return "INFO";
            case LogLevel::Warn:     return "WARN";
            case LogLevel::Error:    return "ERROR";
            case LogLevel::Critical: return "CRITICAL";
        }
        return "?";
    }

    static const char* LevelColor(LogLevel level)
    {
        switch (level)
        {
            case LogLevel::Trace:    return "\033[37m";
            case LogLevel::Info:     return "\033[32m";
            case LogLevel::Warn:     return "\033[33m";
            case LogLevel::Error:    return "\033[31m";
            case LogLevel::Critical: return "\033[1;41m";
        }
        return "";
    }

    void Log::Write(LogLevel level, bool fromClient, std::string text)
    {
        if (level < s_MinLevel)
            return;

        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
#if defined(_WIN32)
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        char timeBuf[16];
        std::strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", &tm);

        std::lock_guard lock(s_Mutex);
        if (s_TerminalOutput)
            std::fprintf(level >= LogLevel::Error ? stderr : stdout, "%s[%s] %s: %s\033[0m\n",
                         LevelColor(level), timeBuf, fromClient ? "APP" : "ZAMIN", text.c_str());

        s_Messages.push_back({ level, fromClient, timeBuf, std::move(text) });
        if (s_Messages.size() > MaxMessages)
            s_Messages.pop_front();
        s_Revision++;
        s_TotalWritten++;
    }

    std::vector<LogMessage> Log::GetMessages(uint64_t* firstIndex)
    {
        std::lock_guard lock(s_Mutex);
        if (firstIndex)
            *firstIndex = s_TotalWritten - s_Messages.size();
        return { s_Messages.begin(), s_Messages.end() };
    }

    uint64_t Log::GetRevision()
    {
        std::lock_guard lock(s_Mutex);
        return s_Revision;
    }

    uint64_t Log::GetTotalWritten()
    {
        std::lock_guard lock(s_Mutex);
        return s_TotalWritten;
    }

    void Log::ForEach(const std::function<void(const LogMessage&)>& fn)
    {
        std::lock_guard lock(s_Mutex);
        for (const auto& m : s_Messages)
            fn(m);
    }

    void Log::Clear()
    {
        std::lock_guard lock(s_Mutex);
        s_Messages.clear();
        s_Revision++;
    }

    size_t Log::GetMessageCount()
    {
        std::lock_guard lock(s_Mutex);
        return s_Messages.size();
    }

}
