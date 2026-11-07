#include "Zamin/Core/Log.h"

#include "Zamin/Utils/StringUtils.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace Zamin {

    namespace {

        // The log files (Log::AttachFiles), written under Log's mutex.
        std::FILE* s_FilePrimary = nullptr;
        std::FILE* s_FileSecondary = nullptr;
        std::atomic<int> s_FileDescriptors[2] = { -1, -1 }; // theirs, for the crash handler

        // The last lines for a crash report: a ring of fixed buffers (no allocation, no lock to read).
        char s_CrashLines[Log::CrashLineCount][Log::CrashLineSize];
        std::atomic<uint32_t> s_CrashNext{ 0 };   // the slot the next line goes to (counts up, wraps by modulo)

        void RememberForCrash(const char* time, LogLevel level, bool fromClient, const std::string& text)
        {
            const uint32_t slot = s_CrashNext.load(std::memory_order_relaxed) % Log::CrashLineCount;
            char* line = s_CrashLines[slot];
            const int written = std::snprintf(line, Log::CrashLineSize, "[%s] %s %s: %s", time, Log::LevelToString(level), fromClient ? "APP" : "ZAMIN",
                                              text.c_str());
            if (written < 0)
                line[0] = '\0';
            // (Multi-line messages stay on one line of the report.)
            for (char* c = line; *c; c++)
                if (*c == '\n' || *c == '\r')
                    *c = ' ';
            s_CrashNext.store(s_CrashNext.load(std::memory_order_relaxed) + 1, std::memory_order_release);
        }

        void WriteFileLine(std::FILE* file, const LogMessage& message)
        {
            if (!file || message.Level < Log::GetFileLevel())
                return;
            const std::string line = Log::FormatFileLine(message);
            std::fwrite(line.data(), 1, line.size(), file);
            std::fputc('\n', file);
            std::fflush(file); // a crash right after this message still finds it in the file
        }

    }

    const char* Log::LevelToString(LogLevel level)
    {
        switch (level)
        {
            case LogLevel::Trace:    return "TRACE";
            case LogLevel::Info:     return "INFO";
            case LogLevel::Warn:     return "WARN";
            case LogLevel::Error:    return "ERROR";
            case LogLevel::Critical: return "CRITICAL";
            case LogLevel::Off:      return "OFF";
        }
        return "?";
    }

    const char* Log::LevelToName(LogLevel level)
    {
        switch (level)
        {
            case LogLevel::Trace:    return "trace";
            case LogLevel::Info:     return "info";
            case LogLevel::Warn:     return "warn";
            case LogLevel::Error:    return "error";
            case LogLevel::Critical: return "critical";
            case LogLevel::Off:      return "off";
        }
        return "trace";
    }

    const char* Log::LevelDisplayName(LogLevel level)
    {
        switch (level)
        {
            case LogLevel::Trace:    return "Trace";
            case LogLevel::Info:     return "Ma'lumot";
            case LogLevel::Warn:     return "Ogohlantirish";
            case LogLevel::Error:    return "Xato";
            case LogLevel::Critical: return "Kritik";
            case LogLevel::Off:      return "O'chiq";
        }
        return "Trace";
    }

    std::optional<LogLevel> Log::ParseLevel(std::string_view text)
    {
        text = StringUtils::Trim(text);
        if (text.size() == 1 && text[0] >= '0' && text[0] <= '5')
            return (LogLevel)(text[0] - '0');
        static const struct { const char* Name; LogLevel Level; } s_Names[] = {
            { "trace", LogLevel::Trace }, { "debug", LogLevel::Trace }, { "verbose", LogLevel::Trace }, { "all", LogLevel::Trace },
            { "info", LogLevel::Info }, { "information", LogLevel::Info }, { "ma'lumot", LogLevel::Info }, { "malumot", LogLevel::Info },
            { "warn", LogLevel::Warn }, { "warning", LogLevel::Warn }, { "ogohlantirish", LogLevel::Warn },
            { "error", LogLevel::Error }, { "err", LogLevel::Error }, { "xato", LogLevel::Error },
            { "critical", LogLevel::Critical }, { "fatal", LogLevel::Critical }, { "kritik", LogLevel::Critical },
            { "off", LogLevel::Off }, { "none", LogLevel::Off }, { "o'chiq", LogLevel::Off },
        };
        for (const auto& entry : s_Names)
            if (StringUtils::EqualsIgnoreCase(text, entry.Name))
                return entry.Level;
        return std::nullopt;
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
            case LogLevel::Off:      return "";
        }
        return "";
    }

    void Log::Write(LogLevel level, bool fromClient, std::string text)
    {
        if (!IsEnabled(level, fromClient))
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

        RememberForCrash(timeBuf, level, fromClient, text);
        s_Messages.push_back({ level, fromClient, timeBuf, std::move(text) });
        WriteFileLine(s_FilePrimary, s_Messages.back());
        WriteFileLine(s_FileSecondary, s_Messages.back());
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

    std::string Log::FormatFileLine(const LogMessage& message)
    {
        return std::format("[{}] {:<5} {}: {}", message.Time, LevelToString(message.Level), message.FromClient ? "APP" : "ZAMIN", message.Text);
    }

    void Log::AttachFiles(std::FILE* primary, std::FILE* secondary, const std::string& header)
    {
        std::lock_guard lock(s_Mutex);
        for (std::FILE* file : { primary, secondary })
        {
            if (!file)
                continue;
            if (!header.empty())
            {
                std::fwrite(header.data(), 1, header.size(), file);
                if (header.back() != '\n')
                    std::fputc('\n', file);
            }
            // What was logged before the file was opened (start-up).
            for (const LogMessage& message : s_Messages)
                WriteFileLine(file, message);
            std::fflush(file);
        }
        s_FilePrimary = primary;
        s_FileSecondary = secondary;
        s_FileDescriptors[0] = primary ? fileno(primary) : -1;
        s_FileDescriptors[1] = secondary ? fileno(secondary) : -1;
    }

    void Log::DetachFiles(std::FILE** primary, std::FILE** secondary)
    {
        std::lock_guard lock(s_Mutex);
        if (primary)
            *primary = s_FilePrimary;
        if (secondary)
            *secondary = s_FileSecondary;
        s_FilePrimary = s_FileSecondary = nullptr;
        s_FileDescriptors[0] = s_FileDescriptors[1] = -1;
    }

    void Log::GetFileDescriptorsUnsafe(int* primary, int* secondary)
    {
        if (primary)
            *primary = s_FileDescriptors[0].load();
        if (secondary)
            *secondary = s_FileDescriptors[1].load();
    }

    void Log::ForEachRecentLineUnsafe(void (*emit)(const char* line, size_t length, void* user), void* user)
    {
        // No lock: called from a signal handler. A line being written at that moment may come out torn.
        const uint32_t next = s_CrashNext.load(std::memory_order_acquire);
        const uint32_t count = next < CrashLineCount ? next : (uint32_t)CrashLineCount;
        for (uint32_t i = next - count; i != next; i++)
        {
            const char* line = s_CrashLines[i % CrashLineCount];
            size_t length = 0;
            while (length < CrashLineSize && line[length])
                length++;
            emit(line, length, user);
        }
    }

}
