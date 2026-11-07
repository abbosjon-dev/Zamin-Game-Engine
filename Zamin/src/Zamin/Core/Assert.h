#pragma once

// Checks with a message:
//
//     ZM_ASSERT(index < count, "indeks {} chegaradan tashqarida (jami {})", index, count);
//     ZM_CORE_ASSERT(window, "oyna yaratilmadi");          // the engine's own (ZM_CORE_*)
//     if (!ZM_VERIFY(texture, "'{}' rasmi yo'q", path))   // always checked, the code goes on
//         return;
//
// ZM_ASSERT / ZM_CORE_ASSERT: compiled in debug builds (and release builds with asserts: CMake
// -DZAMIN_ASSERTS=ON, which defines ZM_ENABLE_ASSERTS=1); elsewhere neither the condition nor the
// message is evaluated. A failure is logged (critical: the expression, the message, file:line and
// the function) and then, as Assert::SetMode / ZAMIN_ASSERT says (Auto by default): with a debugger
// attached the program stops at the assert (a breakpoint at the call site), without one it aborts -
// a player with the crash handler writes its report with the assert as the reason (CrashHandler).
//
// ZM_VERIFY / ZM_CORE_VERIFY: the condition is evaluated in every build and the macro gives its
// result (usable in an if). A failure is logged (an error) and, where asserts are compiled in, stops
// in an attached debugger; it never aborts - the code handles the case. Logged the 1st, 10th, 100th
// ... time per place, so a check failing every frame does not flood the log.
//
// The message is std::format's (checked when compiling); it is only formatted when the check fails.

#include "Zamin/Core/Log.h"

#include <cstdint>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#if !defined(ZM_ENABLE_ASSERTS)
    #if defined(NDEBUG)
        #define ZM_ENABLE_ASSERTS 0
    #else
        #define ZM_ENABLE_ASSERTS 1
    #endif
#endif

#if defined(_MSC_VER)
    #define ZM_DEBUGBREAK() __debugbreak()
#else
    #include <csignal>
    #define ZM_DEBUGBREAK() raise(SIGTRAP)
#endif

namespace Zamin {

    // What happens after a failed check has been logged.
    enum class AssertAction : uint8_t
    {
        Continue = 0,   // the program goes on
        Break,          // stop in the debugger at the check (ZM_DEBUGBREAK)
        Abort           // std::abort() (the crash handler's report, a core dump)
    };

    // The default for failed checks when no handler decides (Assert::SetMode, ZAMIN_ASSERT=<mode>).
    enum class AssertMode : uint8_t
    {
        Auto = 0,   // a debugger attached: Break; else asserts Abort, verifies Continue
        Break,      // always Break (without a debugger that ends the program with SIGTRAP)
        Abort,      // asserts Abort (verifies Break in a debugger, else Continue)
        Continue    // only logged: tests, keeping a session alive
    };

    struct AssertInfo
    {
        const char* Expression = "";
        const char* File = "";      // as __FILE__ wrote it (GetFileName: the name only)
        int Line = 0;
        const char* Function = "";
        std::string Message;        // the formatted message ("" when the check had none)
        bool Core = false;          // the engine's (ZM_CORE_*) or the game's / tool's
        bool Fatal = true;          // ZM_ASSERT; false: ZM_VERIFY (the code goes on)
        uint32_t Count = 1;         // how many times this place has failed (this time included)

        // "index < count - indeks 5 chegaradan tashqarida (Scene.cpp:123, Update)".
        std::string Describe() const;
    };

    class Assert
    {
    public:
        // Decides every failed check (after it was logged) instead of the mode: tests, the editor (a
        // game script's assert must not take the editor down). Returns the previous handler; an empty
        // one brings back the default.
        using Handler = std::function<AssertAction(const AssertInfo&)>;
        static Handler SetHandler(Handler handler);
        static bool HasHandler();

        static void SetMode(AssertMode mode);
        // ZAMIN_ASSERT=auto|break|abort|continue the first time, else what SetMode set.
        static AssertMode GetMode();
        static const char* ModeToString(AssertMode mode);  // "auto", "break", ...
        static std::optional<AssertMode> ParseMode(std::string_view text);

        // A debugger (or another tracer) is attached to this process (Linux: TracerPid in /proc/self/status).
        static bool IsDebuggerAttached();
        // Whether this build compiles ZM_ASSERT in (the engine's own setting; game modules have theirs).
        static constexpr bool AreAssertsEnabled() { return ZM_ENABLE_ASSERTS != 0; }

        // Failed checks since start-up (or ResetCounts); the times one place failed.
        static uint64_t GetFailureCount();
        static uint32_t GetFailureCount(const char* file, int line);
        static void ResetCounts();
        // "Zamin/src/Zamin/Scene/Scene.cpp" -> "Scene.cpp".
        static const char* GetFileName(const char* path);

        // ---- for the macros
        // A failed ZM_ASSERT: logged, counted, decided (handler / mode). Abort does not return.
        static AssertAction Fail(const char* expression, const char* file, int line, const char* function, std::string message, bool core);
        // A failed ZM_VERIFY: the same without aborting (Break stops in here: the caller is one frame up);
        // `checks`: the calling code compiles asserts in. Always false (the macro's value).
        static bool VerifyFailed(const char* expression, const char* file, int line, const char* function, std::string message, bool core,
                                 bool checks);
    };

    namespace Detail {

        inline std::string AssertMessage() { return {}; }
        template<typename... Args>
        std::string AssertMessage(std::format_string<Args...> format, Args&&... args)
        {
            return std::format(format, std::forward<Args>(args)...);
        }

    }

}

#if ZM_ENABLE_ASSERTS
    #define ZM_ASSERT_IMPL(core, cond, ...)                                                                                              \
        do                                                                                                                               \
        {                                                                                                                                \
            if (!(cond)) [[unlikely]]                                                                                                    \
            {                                                                                                                            \
                if (::Zamin::Assert::Fail(#cond, __FILE__, __LINE__, __func__, ::Zamin::Detail::AssertMessage(__VA_ARGS__), core) ==     \
                    ::Zamin::AssertAction::Break)                                                                                        \
                    ZM_DEBUGBREAK();                                                                                                     \
            }                                                                                                                            \
        } while (0)
    #define ZM_CORE_ASSERT(cond, ...) ZM_ASSERT_IMPL(true, cond, __VA_ARGS__)
    #define ZM_ASSERT(cond, ...)      ZM_ASSERT_IMPL(false, cond, __VA_ARGS__)
#else
    // Not evaluated (sizeof only checks that it compiles, and keeps variables used only here "used").
    #define ZM_CORE_ASSERT(cond, ...) ((void)sizeof(!(cond)))
    #define ZM_ASSERT(cond, ...)      ((void)sizeof(!(cond)))
#endif

#define ZM_VERIFY_IMPL(core, cond, ...)                                                                                                  \
    (static_cast<bool>(cond) ? true                                                                                                      \
                             : ::Zamin::Assert::VerifyFailed(#cond, __FILE__, __LINE__, __func__, ::Zamin::Detail::AssertMessage(__VA_ARGS__), \
                                                             core, ZM_ENABLE_ASSERTS != 0))
#define ZM_CORE_VERIFY(cond, ...) ZM_VERIFY_IMPL(true, cond, __VA_ARGS__)
#define ZM_VERIFY(cond, ...)      ZM_VERIFY_IMPL(false, cond, __VA_ARGS__)
