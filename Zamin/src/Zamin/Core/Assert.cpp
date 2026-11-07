#include "Zamin/Core/Assert.h"

#include "Zamin/Core/CrashHandler.h"
#include "Zamin/Utils/StringUtils.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <unordered_map>

namespace Zamin {

    namespace {

        struct State
        {
            std::mutex Mutex;
            Assert::Handler Handler;
            std::unordered_map<std::string, uint32_t> Counts;   // "file:line" -> failures
        };

        State& S()
        {
            static State s;
            return s;
        }

        std::atomic<uint64_t> s_Failures{ 0 };
        std::atomic<int> s_Mode{ -1 };   // -1: not decided yet (ZAMIN_ASSERT is read once)

        std::string PlaceKey(const char* file, int line)
        {
            return std::format("{}:{}", file ? file : "", line);
        }

        // 1, 10, 100, 1000 ...: the times a place's failure is logged.
        bool ShouldLog(uint32_t count)
        {
            while (count >= 10 && count % 10 == 0)
                count /= 10;
            return count == 1;
        }

        AssertAction Decide(const AssertInfo& info, bool checks)
        {
            if (!checks)
                return AssertAction::Continue; // a ZM_VERIFY where asserts are compiled out: only logged
            const bool debugger = Assert::IsDebuggerAttached();
            switch (Assert::GetMode())
            {
                case AssertMode::Continue: return AssertAction::Continue;
                case AssertMode::Break:    return AssertAction::Break;
                case AssertMode::Abort:
                    if (info.Fatal)
                        return AssertAction::Abort;
                    return debugger ? AssertAction::Break : AssertAction::Continue;
                case AssertMode::Auto:
                    break;
            }
            if (info.Fatal)
                return debugger ? AssertAction::Break : AssertAction::Abort;
            return debugger ? AssertAction::Break : AssertAction::Continue;
        }

        AssertAction Report(AssertInfo& info, bool checks)
        {
            Assert::Handler handler;
            {
                State& s = S();
                std::lock_guard lock(s.Mutex);
                info.Count = ++s.Counts[PlaceKey(info.File, info.Line)];
                handler = s.Handler;
            }
            s_Failures.fetch_add(1, std::memory_order_relaxed);

            if (ShouldLog(info.Count))
            {
                const std::string times = info.Count > 1 ? std::format(" [{}-marta]", info.Count) : std::string();
                if (info.Fatal)
                    Log::Write(LogLevel::Critical, !info.Core, std::format("Assert bajarilmadi: {}{}", info.Describe(), times));
                else
                    Log::Write(LogLevel::Error, !info.Core, std::format("Tekshiruv bajarilmadi (ZM_VERIFY): {}{}", info.Describe(), times));
            }

            const AssertAction action = handler ? handler(info) : Decide(info, checks);
            if (action == AssertAction::Abort)
            {
                // The crash handler's report says what failed; what was printed gets out first.
                CrashHandler::SetAbortReason(info.Describe());
                std::fflush(nullptr);
                std::abort();
            }
            return action;
        }

    }

    std::string AssertInfo::Describe() const
    {
        std::string text = Expression ? Expression : "";
        if (!Message.empty())
            text += (text.empty() ? "" : " - ") + Message;
        text += std::format(" ({}:{}{}{})", Assert::GetFileName(File), Line, Function && *Function ? ", " : "", Function ? Function : "");
        return text;
    }

    Assert::Handler Assert::SetHandler(Handler handler)
    {
        State& s = S();
        std::lock_guard lock(s.Mutex);
        Handler previous = std::move(s.Handler);
        s.Handler = std::move(handler);
        return previous;
    }

    bool Assert::HasHandler()
    {
        State& s = S();
        std::lock_guard lock(s.Mutex);
        return (bool)s.Handler;
    }

    void Assert::SetMode(AssertMode mode)
    {
        s_Mode.store((int)mode);
    }

    AssertMode Assert::GetMode()
    {
        int mode = s_Mode.load();
        if (mode < 0)
        {
            AssertMode fromEnvironment = AssertMode::Auto;
            if (const char* text = std::getenv("ZAMIN_ASSERT"))
            {
                if (const std::optional<AssertMode> parsed = ParseMode(text))
                    fromEnvironment = *parsed;
                else
                    ZM_CORE_WARN("ZAMIN_ASSERT={}: auto, break, abort yoki continue kerak - auto ishlatiladi", text);
            }
            int expected = -1;
            s_Mode.compare_exchange_strong(expected, (int)fromEnvironment);
            mode = s_Mode.load();
        }
        return (AssertMode)mode;
    }

    const char* Assert::ModeToString(AssertMode mode)
    {
        switch (mode)
        {
            case AssertMode::Auto:     return "auto";
            case AssertMode::Break:    return "break";
            case AssertMode::Abort:    return "abort";
            case AssertMode::Continue: return "continue";
        }
        return "auto";
    }

    std::optional<AssertMode> Assert::ParseMode(std::string_view text)
    {
        text = StringUtils::Trim(text);
        if (StringUtils::EqualsIgnoreCase(text, "auto") || text.empty())
            return AssertMode::Auto;
        if (StringUtils::EqualsIgnoreCase(text, "break") || StringUtils::EqualsIgnoreCase(text, "debug"))
            return AssertMode::Break;
        if (StringUtils::EqualsIgnoreCase(text, "abort") || StringUtils::EqualsIgnoreCase(text, "crash"))
            return AssertMode::Abort;
        if (StringUtils::EqualsIgnoreCase(text, "continue") || StringUtils::EqualsIgnoreCase(text, "log") ||
            StringUtils::EqualsIgnoreCase(text, "ignore"))
            return AssertMode::Continue;
        return std::nullopt;
    }

    bool Assert::IsDebuggerAttached()
    {
#if defined(__linux__)
        std::ifstream status("/proc/self/status");
        std::string line;
        while (std::getline(status, line))
            if (line.starts_with("TracerPid:"))
                return StringUtils::ParseInt<long>(std::string_view(line).substr(10)).value_or(0) != 0;
        return false;
#else
        return false;
#endif
    }

    uint64_t Assert::GetFailureCount()
    {
        return s_Failures.load(std::memory_order_relaxed);
    }

    uint32_t Assert::GetFailureCount(const char* file, int line)
    {
        State& s = S();
        std::lock_guard lock(s.Mutex);
        auto it = s.Counts.find(PlaceKey(file, line));
        return it != s.Counts.end() ? it->second : 0;
    }

    void Assert::ResetCounts()
    {
        State& s = S();
        std::lock_guard lock(s.Mutex);
        s.Counts.clear();
        s_Failures.store(0);
    }

    const char* Assert::GetFileName(const char* path)
    {
        if (!path)
            return "";
        const char* name = path;
        for (const char* c = path; *c; c++)
            if (*c == '/' || *c == '\\')
                name = c + 1;
        return name;
    }

    AssertAction Assert::Fail(const char* expression, const char* file, int line, const char* function, std::string message, bool core)
    {
        AssertInfo info;
        info.Expression = expression;
        info.File = file;
        info.Line = line;
        info.Function = function;
        info.Message = std::move(message);
        info.Core = core;
        info.Fatal = true;
        return Report(info, true);
    }

    bool Assert::VerifyFailed(const char* expression, const char* file, int line, const char* function, std::string message, bool core, bool checks)
    {
        AssertInfo info;
        info.Expression = expression;
        info.File = file;
        info.Line = line;
        info.Function = function;
        info.Message = std::move(message);
        info.Core = core;
        info.Fatal = false;
        if (Report(info, checks) == AssertAction::Break)
            ZM_DEBUGBREAK();
        return false;
    }

}
