#include "Zamin/Core/CrashHandler.h"

#include "Zamin/Core/BuildInfo.h"

#include "Zamin/Core/Log.h"
#include "Zamin/Core/Version.h"
#include "Zamin/Utils/FileSystem.h"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <cxxabi.h>
#include <exception>
#include <format>
#include <sstream>
#include <typeinfo>

#if defined(__linux__)
    #include <csignal>
    #include <execinfo.h>
    #include <fcntl.h>
    #include <sys/stat.h>
    #include <sys/syscall.h>
    #include <sys/wait.h>
    #include <time.h>
    #include <unistd.h>
    #define ZM_CRASH_HANDLER_SUPPORTED 1
extern char** environ;
#else
    #define ZM_CRASH_HANDLER_SUPPORTED 0
#endif

namespace Zamin {

    namespace fs = std::filesystem;

    namespace {

        constexpr size_t PathSize = 1024;
        constexpr size_t HeaderSize = 4096;
        constexpr size_t ArgSize = 2048;
        constexpr int MaxDialogs = 4;
        constexpr int MaxArgs = 12;

        // An argument of a dialog command: text, then (Append) the report's path or the whole message.
        enum class ArgAppend : uint8_t { None = 0, Report, Message };
        struct DialogArg
        {
            char Text[ArgSize] = {};
            ArgAppend Append = ArgAppend::None;
        };
        struct DialogCommand
        {
            char Path[PathSize] = {};
            DialogArg Args[MaxArgs];
            int Count = 0;
        };

        // ---- prepared by Configure (read by the signal handler)
        CrashHandlerSettings s_Settings;
        char s_ReportDirectory[PathSize] = "/tmp";
        char s_FallbackDirectory[PathSize] = "/tmp";
        char s_Header[HeaderSize] = {};
        char s_Program[256] = "Zamin";
        char s_MessagePrefix[ArgSize] = {};
        char s_MessageSuffix[ArgSize] = {};
        DialogCommand s_Dialogs[MaxDialogs];
        int s_DialogCount = 0;
        bool s_ShowDialog = false;
        long s_UtcOffset = 0; // seconds east of UTC (local time in the report)

        // ---- the crash
        char s_LastReport[PathSize] = {};
        std::atomic<int> s_Reporting{ 0 };      // 1 while (or after) a report is written
        std::atomic<long> s_ReporterThread{ 0 };
        std::atomic<int> s_ReportDone{ 0 };

#if ZM_CRASH_HANDLER_SUPPORTED
        constexpr int s_Signals[] = { SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS };
        struct sigaction s_OldActions[sizeof(s_Signals) / sizeof(s_Signals[0])];
        std::terminate_handler s_OldTerminate = nullptr;
        bool s_Installed = false;
        alignas(16) char s_AltStack[64 * 1024];

        // ---------------------------------------------------------- async-signal-safe text helpers
        size_t Length(const char* text)
        {
            size_t n = 0;
            while (text[n])
                n++;
            return n;
        }

        void Append(char* out, size_t capacity, const char* text)
        {
            size_t at = Length(out);
            for (size_t i = 0; text[i] && at + 1 < capacity; i++)
                out[at++] = text[i];
            out[at] = '\0';
        }

        void AppendNumber(char* out, size_t capacity, long long value, int width = 0)
        {
            char digits[24];
            int n = 0;
            const bool negative = value < 0;
            unsigned long long v = negative ? (unsigned long long)(-(value + 1)) + 1 : (unsigned long long)value;
            do
            {
                digits[n++] = (char)('0' + v % 10);
                v /= 10;
            } while (v && n < 22);
            while (n < width && n < 22)
                digits[n++] = '0';
            char text[26];
            int at = 0;
            if (negative)
                text[at++] = '-';
            while (n)
                text[at++] = digits[--n];
            text[at] = '\0';
            Append(out, capacity, text);
        }

        void AppendHex(char* out, size_t capacity, uintptr_t value)
        {
            char text[2 + sizeof(uintptr_t) * 2 + 1];
            text[0] = '0';
            text[1] = 'x';
            for (size_t i = 0; i < sizeof(uintptr_t) * 2; i++)
                text[2 + i] = "0123456789abcdef"[(value >> ((sizeof(uintptr_t) * 2 - 1 - i) * 4)) & 0xF];
            text[2 + sizeof(uintptr_t) * 2] = '\0';
            Append(out, capacity, text);
        }

        void Write(int fd, const char* text, size_t length)
        {
            while (length > 0)
            {
                const ssize_t written = ::write(fd, text, length);
                if (written <= 0)
                {
                    if (written < 0 && errno == EINTR)
                        continue;
                    return;
                }
                text += written;
                length -= (size_t)written;
            }
        }

        void Write(int fd, const char* text) { Write(fd, text, Length(text)); }

        // Local time as "2026-11-07 14:03:12" (file: "2026-11-07_14-03-12"): days since the epoch to a
        // civil date (H. Hinnant's algorithm) - localtime_r is not async-signal-safe.
        void AppendTime(char* out, size_t capacity, time_t time, bool forFile)
        {
            const long long local = (long long)time + s_UtcOffset;
            long long days = local / 86400, seconds = local % 86400;
            if (seconds < 0)
            {
                seconds += 86400;
                days--;
            }
            const long long z = days + 719468;
            const long long era = (z >= 0 ? z : z - 146096) / 146097;
            const long long doe = z - era * 146097;
            const long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
            long long year = yoe + era * 400;
            const long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
            const long long mp = (5 * doy + 2) / 153;
            const long long day = doy - (153 * mp + 2) / 5 + 1;
            const long long month = mp < 10 ? mp + 3 : mp - 9;
            if (month <= 2)
                year++;
            AppendNumber(out, capacity, year, 4);
            Append(out, capacity, "-");
            AppendNumber(out, capacity, month, 2);
            Append(out, capacity, "-");
            AppendNumber(out, capacity, day, 2);
            Append(out, capacity, forFile ? "_" : " ");
            AppendNumber(out, capacity, seconds / 3600, 2);
            Append(out, capacity, forFile ? "-" : ":");
            AppendNumber(out, capacity, seconds / 60 % 60, 2);
            Append(out, capacity, forFile ? "-" : ":");
            AppendNumber(out, capacity, seconds % 60, 2);
        }

        long CurrentThreadId() { return (long)::syscall(SYS_gettid); }

        // fork() without the atfork handlers (glibc's take malloc's locks: a crash inside malloc would
        // hang there). The child only calls setsid / execve / waitpid / _exit.
        pid_t RawFork() { return (pid_t)::syscall(SYS_clone, SIGCHLD, 0, 0, 0, 0); }

        void EmitLogLine(const char* line, size_t length, void* user)
        {
            const int fd = *(const int*)user;
            Write(fd, line, length);
            Write(fd, "\n");
        }

        // The executable mappings of the process (the modules the backtrace's addresses are in).
        void WriteModules(int fd)
        {
            const int maps = ::open("/proc/self/maps", O_RDONLY | O_CLOEXEC);
            if (maps < 0)
                return;
            char buffer[4096];
            char line[512];
            size_t lineLength = 0, total = 0;
            ssize_t got = 0;
            while ((got = ::read(maps, buffer, sizeof(buffer))) > 0 && total < 16 * 1024)
                for (ssize_t i = 0; i < got; i++)
                {
                    const char c = buffer[i];
                    if (c != '\n')
                    {
                        if (lineLength + 1 < sizeof(line))
                            line[lineLength++] = c;
                        continue;
                    }
                    line[lineLength] = '\0';
                    // "start-end r-xp offset dev inode path": the code of a file.
                    bool code = false, file = false;
                    for (size_t k = 0; k + 4 < lineLength; k++)
                        if (line[k] == ' ' && line[k + 1] == 'r' && line[k + 3] == 'x' && line[k + 4] == 'p')
                        {
                            code = true;
                            break;
                        }
                    for (size_t k = 0; k < lineLength; k++)
                        if (line[k] == '/')
                        {
                            file = true;
                            break;
                        }
                    if (code && file)
                    {
                        Write(fd, line, lineLength);
                        Write(fd, "\n");
                        total += lineLength + 1;
                    }
                    lineLength = 0;
                }
            ::close(maps);
        }

        // Opens a new report file in `directory` (made when it is not there); -1 when it cannot.
        int OpenReport(const char* directory, time_t now)
        {
            ::mkdir(directory, 0755);
            for (int attempt = 1; attempt < 20; attempt++)
            {
                s_LastReport[0] = '\0';
                Append(s_LastReport, PathSize, directory);
                Append(s_LastReport, PathSize, "/crash-");
                AppendTime(s_LastReport, PathSize, now, true);
                if (attempt > 1)
                {
                    Append(s_LastReport, PathSize, "_");
                    AppendNumber(s_LastReport, PathSize, attempt);
                }
                Append(s_LastReport, PathSize, ".txt");
                const int fd = ::open(s_LastReport, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
                if (fd >= 0)
                    return fd;
                if (errno != EEXIST)
                    break;
            }
            s_LastReport[0] = '\0';
            return -1;
        }

        // The dialog: a detached grandchild tries the commands in order (the crashed program exits
        // meanwhile; its frozen window does not cover the dialog).
        void StartDialog(const char* report)
        {
            if (!s_ShowDialog || s_DialogCount == 0)
                return;
            const pid_t child = RawFork();
            if (child < 0)
                return;
            if (child > 0)
            {
                int status = 0;
                while (::waitpid(child, &status, 0) < 0 && errno == EINTR) {}
                return;
            }
            // ---- the child: a session of its own, then the grandchild does the work.
            ::setsid();
            const pid_t grandchild = RawFork();
            if (grandchild != 0)
                ::_exit(0);
            static char message[ArgSize * 2];
            message[0] = '\0';
            Append(message, sizeof(message), s_MessagePrefix);
            Append(message, sizeof(message), report);
            Append(message, sizeof(message), s_MessageSuffix);
            static char args[MaxArgs][ArgSize * 2];
            char* argv[MaxArgs + 1];
            for (int d = 0; d < s_DialogCount; d++)
            {
                const DialogCommand& command = s_Dialogs[d];
                for (int a = 0; a < command.Count; a++)
                {
                    args[a][0] = '\0';
                    Append(args[a], sizeof(args[a]), command.Args[a].Text);
                    if (command.Args[a].Append == ArgAppend::Report)
                        Append(args[a], sizeof(args[a]), report);
                    else if (command.Args[a].Append == ArgAppend::Message)
                        Append(args[a], sizeof(args[a]), message);
                    argv[a] = args[a];
                }
                argv[command.Count] = nullptr;
                const pid_t tool = RawFork();
                if (tool < 0)
                    continue;
                if (tool == 0)
                {
                    ::execve(command.Path, argv, environ);
                    ::_exit(127);
                }
                int status = 0;
                while (::waitpid(tool, &status, 0) < 0 && errno == EINTR) {}
                // Shown (0, or 1: zenity's "closed"); anything else (no display, not started): the next one.
                if (WIFEXITED(status) && (WEXITSTATUS(status) == 0 || WEXITSTATUS(status) == 1))
                    ::_exit(0);
            }
            ::_exit(1);
        }

        // Everything after the cause is known: the report, the notes in the terminal and the log, the dialog.
        void HandleCrash(int signal, int code, const void* address, const char* exception)
        {
            const time_t now = ::time(nullptr);
            int fd = OpenReport(s_ReportDirectory, now);
            if (fd < 0)
                fd = OpenReport(s_FallbackDirectory, now);
            if (fd >= 0)
            {
                CrashHandler::WriteReport(fd, signal, code, address, exception);
                ::fsync(fd);
                ::close(fd);
            }
            // The terminal and the log file.
            static char note[PathSize * 2];
            note[0] = '\0';
            Append(note, sizeof(note), "[");
            {
                char stamp[32] = {};
                AppendTime(stamp, sizeof(stamp), now, false);
                Append(note, sizeof(note), stamp + 11); // the time of day
            }
            Append(note, sizeof(note), "] CRASH ZAMIN: ");
            Append(note, sizeof(note), s_Program);
            Append(note, sizeof(note), " kutilmaganda to'xtadi: ");
            if (exception)
            {
                Append(note, sizeof(note), "ushlanmagan istisno - ");
                Append(note, sizeof(note), exception);
            }
            else
            {
                Append(note, sizeof(note), CrashHandler::GetSignalName(signal));
                Append(note, sizeof(note), " - ");
                Append(note, sizeof(note), CrashHandler::DescribeSignal(signal));
            }
            Append(note, sizeof(note), s_LastReport[0] ? ". Hisobot: " : ". Hisobot yozilmadi.");
            Append(note, sizeof(note), s_LastReport);
            Append(note, sizeof(note), "\n");
            Write(STDERR_FILENO, "\n");
            Write(STDERR_FILENO, note);
            int logs[2] = { -1, -1 };
            Log::GetFileDescriptorsUnsafe(&logs[0], &logs[1]);
            for (int log : logs)
                if (log >= 0)
                    Write(log, note);
            if (s_LastReport[0])
                StartDialog(s_LastReport);
            s_ReportDone = 1;
        }

        // A second crash: another thread waits for the first report (up to 10 s); the reporting thread
        // itself (a crash inside the handler) ends at once.
        bool WaitForOtherReport()
        {
            if (s_ReporterThread.load() == CurrentThreadId())
                return false;
            for (int i = 0; i < 1000 && !s_ReportDone.load(); i++)
            {
                timespec pause{ 0, 10 * 1000 * 1000 };
                ::nanosleep(&pause, nullptr);
            }
            return true;
        }

        void RaiseDefault(int signal)
        {
            struct sigaction action{};
            action.sa_handler = SIG_DFL;
            sigemptyset(&action.sa_mask);
            ::sigaction(signal, &action, nullptr);
            // Pending until this handler returns (the signal is blocked in it), then the default action;
            // a hardware fault happens again when the instruction runs again.
            ::raise(signal);
        }

        void OnSignal(int signal, siginfo_t* info, void*)
        {
            const int savedErrno = errno;
            if (s_Reporting.exchange(1) != 0)
            {
                WaitForOtherReport();
                RaiseDefault(signal);
                errno = savedErrno;
                return;
            }
            s_ReporterThread = CurrentThreadId();
            HandleCrash(signal, info ? info->si_code : 0, info ? info->si_addr : nullptr, nullptr);
            RaiseDefault(signal);
            errno = savedErrno;
        }

        // An uncaught C++ exception (or std::terminate): what it was, then abort() - without a second report.
        [[noreturn]] void OnTerminate()
        {
            if (s_Reporting.exchange(1) == 0)
            {
                s_ReporterThread = CurrentThreadId();
                std::string what = "std::terminate (istisnosiz)";
                if (std::exception_ptr current = std::current_exception())
                {
                    std::string type = "noma'lum tur";
                    if (const std::type_info* info = abi::__cxa_current_exception_type())
                    {
                        int status = 0;
                        char* demangled = abi::__cxa_demangle(info->name(), nullptr, nullptr, &status);
                        type = status == 0 && demangled ? demangled : info->name();
                        std::free(demangled);
                    }
                    try
                    {
                        std::rethrow_exception(current);
                    }
                    catch (const std::exception& e)
                    {
                        what = type + ": " + e.what();
                    }
                    catch (...)
                    {
                        what = type;
                    }
                }
                HandleCrash(SIGABRT, 0, nullptr, what.c_str());
            }
            struct sigaction action{};
            action.sa_handler = SIG_DFL;
            sigemptyset(&action.sa_mask);
            ::sigaction(SIGABRT, &action, nullptr);
            std::abort();
        }

        // A program in PATH ("" when not found).
        std::string FindInPath(std::string_view name)
        {
            const char* path = std::getenv("PATH");
            if (!path)
                return {};
            std::string_view rest = path;
            while (!rest.empty())
            {
                const size_t colon = rest.find(':');
                const std::string_view dir = rest.substr(0, colon);
                rest = colon == std::string_view::npos ? std::string_view() : rest.substr(colon + 1);
                if (dir.empty())
                    continue;
                const fs::path candidate = fs::path(std::string(dir)) / std::string(name);
                if (::access(candidate.c_str(), X_OK) == 0)
                    return candidate.string();
            }
            return {};
        }
#endif

        void CopyText(char* out, size_t capacity, std::string_view text)
        {
            const size_t n = std::min(text.size(), capacity - 1);
            std::memcpy(out, text.data(), n);
            out[n] = '\0';
        }

    }

    // ------------------------------------------------------------------ setup
    void CrashHandler::Configure(const CrashHandlerSettings& settings)
    {
        s_Settings = settings;
        std::error_code ec;
        fs::path temp = fs::temp_directory_path(ec);
        CopyText(s_FallbackDirectory, PathSize, ec || temp.empty() ? std::string("/tmp") : temp.string());
        CopyText(s_ReportDirectory, PathSize, settings.ReportDirectory.empty() ? std::string(s_FallbackDirectory) : settings.ReportDirectory.string());
        if (!settings.ReportDirectory.empty())
            fs::create_directories(settings.ReportDirectory, ec);
        const std::string product = settings.Product.empty() ? settings.Application : settings.Product;
        CopyText(s_Program, sizeof(s_Program), product.empty() ? std::string("Zamin") : product);

        std::string header = "=== Zamin crash hisoboti ===\n";
        header += std::format("Dastur: {}\n", settings.Application);
        if (!settings.Product.empty())
            header += std::format("O'yin: {}{}\n", settings.Product, settings.Version.empty() ? "" : " " + settings.Version);
        header += std::format("Dvigatel: {}\n", BuildInfo::Get().Describe());
        if (!settings.LogFile.empty())
            header += std::format("Log fayli: {}\n", settings.LogFile.string());
        CopyText(s_Header, HeaderSize, header);

        CopyText(s_MessagePrefix, ArgSize, std::format("{} kutilmaganda to'xtadi.\n\nXato haqida hisobot saqlandi:\n", s_Program));
        CopyText(s_MessageSuffix, ArgSize, "\n\nIltimos, shu faylni o'yin muallifiga yuboring.");

        s_ShowDialog = settings.ShowDialog;
        s_DialogCount = 0;
#if ZM_CRASH_HANDLER_SUPPORTED
        auto add = [](const std::string& path, std::initializer_list<std::pair<std::string, ArgAppend>> args) -> DialogCommand* {
            if (path.empty() || s_DialogCount >= MaxDialogs)
                return nullptr;
            DialogCommand& command = s_Dialogs[s_DialogCount++];
            command = DialogCommand();
            CopyText(command.Path, PathSize, path);
            for (const auto& [text, append] : args)
            {
                if (command.Count >= MaxArgs)
                    break;
                CopyText(command.Args[command.Count].Text, ArgSize, text);
                command.Args[command.Count++].Append = append;
            }
            return &command;
        };
        const std::string title = std::format("{} - xato", s_Program);
        add(FindInPath("zenity"), { { "zenity", ArgAppend::None }, { "--error", ArgAppend::None }, { "--title=" + title, ArgAppend::None },
                                    { "--width=560", ArgAppend::None }, { "--text=", ArgAppend::Message } });
        add(FindInPath("kdialog"), { { "kdialog", ArgAppend::None }, { "--title", ArgAppend::None }, { title, ArgAppend::None },
                                     { "--error", ArgAppend::None }, { "", ArgAppend::Message } });
        if (!settings.DialogProgram.empty())
            if (DialogCommand* command = add(settings.DialogProgram.string(), { { settings.DialogProgram.filename().string(), ArgAppend::None } }))
            {
                for (const std::string& arg : settings.DialogArguments)
                    if (command->Count < MaxArgs - 1)
                    {
                        CopyText(command->Args[command->Count].Text, ArgSize, arg);
                        command->Args[command->Count++].Append = ArgAppend::None;
                    }
                command->Args[command->Count].Text[0] = '\0';
                command->Args[command->Count++].Append = ArgAppend::Report;
            }
        add(FindInPath("xmessage"), { { "xmessage", ArgAppend::None }, { "-center", ArgAppend::None }, { "-buttons", ArgAppend::None },
                                      { "OK:0", ArgAppend::None }, { "", ArgAppend::Message } });

        // Local time in the report (the handler cannot call localtime_r).
        const time_t now = ::time(nullptr);
        std::tm tm{};
        localtime_r(&now, &tm);
        s_UtcOffset = tm.tm_gmtoff;
        // backtrace() loads libgcc on its first call (it allocates): not in the handler.
        void* frames[4];
        ::backtrace(frames, 4);
#endif
        s_LastReport[0] = '\0';
    }

    const CrashHandlerSettings& CrashHandler::GetSettings() { return s_Settings; }

    bool CrashHandler::Install(const CrashHandlerSettings& settings)
    {
        Configure(settings);
#if ZM_CRASH_HANDLER_SUPPORTED
        if (s_Installed)
            return true;
        // A stack of its own: a stack overflow can be reported too (the main thread's).
        stack_t stack{};
        stack.ss_sp = s_AltStack;
        stack.ss_size = sizeof(s_AltStack);
        stack.ss_flags = 0;
        ::sigaltstack(&stack, nullptr);
        for (size_t i = 0; i < std::size(s_Signals); i++)
        {
            struct sigaction action{};
            action.sa_sigaction = OnSignal;
            sigemptyset(&action.sa_mask);
            action.sa_flags = SA_SIGINFO | SA_ONSTACK;
            ::sigaction(s_Signals[i], &action, &s_OldActions[i]);
        }
        s_OldTerminate = std::set_terminate(OnTerminate);
        s_Reporting = 0;
        s_ReportDone = 0;
        s_Installed = true;
        return true;
#else
        return false;
#endif
    }

    void CrashHandler::Uninstall()
    {
#if ZM_CRASH_HANDLER_SUPPORTED
        if (!s_Installed)
            return;
        for (size_t i = 0; i < std::size(s_Signals); i++)
            ::sigaction(s_Signals[i], &s_OldActions[i], nullptr);
        std::set_terminate(s_OldTerminate);
        s_Installed = false;
#endif
    }

    bool CrashHandler::IsInstalled()
    {
#if ZM_CRASH_HANDLER_SUPPORTED
        return s_Installed;
#else
        return false;
#endif
    }

    fs::path CrashHandler::GetLastReportPath() { return s_LastReport[0] ? fs::path(s_LastReport) : fs::path(); }

    // ------------------------------------------------------------------ the report
    void CrashHandler::WriteReport(int fd, int signal, int code, const void* address, const char* exception)
    {
#if ZM_CRASH_HANDLER_SUPPORTED
        static char line[PathSize * 2];
        Write(fd, s_Header);
        line[0] = '\0';
        Append(line, sizeof(line), "Vaqt: ");
        AppendTime(line, sizeof(line), ::time(nullptr), false);
        Append(line, sizeof(line), "\n");
        Write(fd, line);

        line[0] = '\0';
        Append(line, sizeof(line), "Sabab: ");
        if (exception)
        {
            Append(line, sizeof(line), "ushlanmagan C++ istisnosi - ");
            Write(fd, line);
            Write(fd, exception);
            Write(fd, "\n");
        }
        else
        {
            Append(line, sizeof(line), GetSignalName(signal));
            Append(line, sizeof(line), " - ");
            Append(line, sizeof(line), DescribeSignal(signal));
            Append(line, sizeof(line), "\n");
            Write(fd, line);
        }
        line[0] = '\0';
        Append(line, sizeof(line), "Signal: ");
        AppendNumber(line, sizeof(line), signal);
        Append(line, sizeof(line), ", kod ");
        AppendNumber(line, sizeof(line), code);
        Append(line, sizeof(line), ", manzil ");
        AppendHex(line, sizeof(line), (uintptr_t)address);
        Append(line, sizeof(line), "\nPID: ");
        AppendNumber(line, sizeof(line), (long long)::getpid());
        Append(line, sizeof(line), ", oqim: ");
        AppendNumber(line, sizeof(line), CurrentThreadId());
        Append(line, sizeof(line), "\n");
        Write(fd, line);

        Write(fd, "\n--- Backtrace (birinchi qatorlar - crash handler'ning o'zi) ---\n");
        void* frames[64];
        const int count = ::backtrace(frames, 64);
        ::backtrace_symbols_fd(frames, count, fd);

        Write(fd, "\n--- Oxirgi log satrlari ---\n");
        Log::ForEachRecentLineUnsafe(EmitLogLine, &fd);

        Write(fd, "\n--- Yuklangan modullar (/proc/self/maps) ---\n");
        WriteModules(fd);
        Write(fd, "=== Hisobot oxiri ===\n");
#else
        (void)fd; (void)signal; (void)code; (void)address; (void)exception;
#endif
    }

    const char* CrashHandler::GetSignalName(int signal)
    {
#if ZM_CRASH_HANDLER_SUPPORTED
        switch (signal)
        {
            case SIGSEGV: return "SIGSEGV";
            case SIGABRT: return "SIGABRT";
            case SIGFPE:  return "SIGFPE";
            case SIGILL:  return "SIGILL";
            case SIGBUS:  return "SIGBUS";
            default:      break;
        }
#endif
        (void)signal;
        return "signal";
    }

    const char* CrashHandler::DescribeSignal(int signal)
    {
#if ZM_CRASH_HANDLER_SUPPORTED
        switch (signal)
        {
            case SIGSEGV: return "xotiraga noto'g'ri murojaat (segmentation fault)";
            case SIGABRT: return "dastur to'xtatildi (abort: assert yoki ichki xato)";
            case SIGFPE:  return "arifmetik xato (nolga bo'lish)";
            case SIGILL:  return "noto'g'ri protsessor buyrug'i";
            case SIGBUS:  return "xotira shinasi xatosi (bus error)";
            default:      break;
        }
#endif
        (void)signal;
        return "kutilmagan signal";
    }

    std::string CrashHandler::MakeReportFileName(std::time_t time)
    {
        std::tm tm{};
        localtime_r(&time, &tm);
        char stamp[32];
        std::strftime(stamp, sizeof(stamp), "%Y-%m-%d_%H-%M-%S", &tm);
        return std::format("crash-{}.txt", stamp);
    }

    // ------------------------------------------------------------------ reading it back
    std::string CrashHandler::DemangleBacktraceLine(std::string_view line)
    {
        // "<module>(<symbol>+0x1a) [0x...]"
        const size_t open = line.find('(');
        if (open == std::string_view::npos)
            return std::string(line);
        const size_t end = line.find_first_of("+)", open + 1);
        if (end == std::string_view::npos || end == open + 1)
            return std::string(line);
        const std::string symbol(line.substr(open + 1, end - open - 1));
        if (!symbol.starts_with("_Z"))
            return std::string(line);
        int status = 0;
        char* demangled = abi::__cxa_demangle(symbol.c_str(), nullptr, nullptr, &status);
        if (status != 0 || !demangled)
        {
            std::free(demangled);
            return std::string(line);
        }
        std::string out = std::string(line.substr(0, open + 1)) + demangled + std::string(line.substr(end));
        std::free(demangled);
        return out;
    }

    size_t CrashHandler::DemangleReport(const fs::path& report)
    {
        std::string text = FileSystem::ReadTextFile(report);
        if (text.empty())
            return 0;
        std::istringstream in(text);
        std::string out, line;
        size_t changed = 0;
        bool inBacktrace = false;
        while (std::getline(in, line))
        {
            if (line.starts_with("--- "))
                inBacktrace = line.starts_with("--- Backtrace");
            else if (inBacktrace)
            {
                std::string readable = DemangleBacktraceLine(line);
                if (readable != line)
                {
                    changed++;
                    line = std::move(readable);
                }
            }
            out += line;
            out += '\n';
        }
        if (changed > 0)
            FileSystem::WriteTextFile(report, out);
        return changed;
    }

    bool CrashHandler::ReadReport(const fs::path& report, CrashReportSummary& out)
    {
        const std::string text = FileSystem::ReadTextFile(report);
        if (text.find("=== Zamin crash hisoboti ===") == std::string::npos)
            return false;
        out = CrashReportSummary();
        std::istringstream in(text);
        std::string line;
        enum class Section { Header, Backtrace, Log, Other } section = Section::Header;
        auto value = [&](std::string_view key, std::string& into) {
            if (line.starts_with(key))
            {
                into = line.substr(key.size());
                return true;
            }
            return false;
        };
        while (std::getline(in, line))
        {
            if (line.starts_with("=== "))
                continue;
            if (line.starts_with("--- "))
            {
                section = line.starts_with("--- Backtrace") ? Section::Backtrace : line.starts_with("--- Oxirgi log") ? Section::Log : Section::Other;
                continue;
            }
            if (line.empty())
                continue;
            switch (section)
            {
                case Section::Header:
                {
                    std::string game;
                    if (value("Dastur: ", out.Application) || value("Dvigatel: ", out.Engine) || value("Vaqt: ", out.Time) ||
                        value("Sabab: ", out.Reason) || value("Log fayli: ", out.LogFile))
                        break;
                    if (value("O'yin: ", game))
                    {
                        // "<product> <version>": the version is the last word.
                        const size_t space = game.rfind(' ');
                        if (space != std::string::npos && space + 1 < game.size() && std::isdigit((unsigned char)game[space + 1]))
                        {
                            out.Product = game.substr(0, space);
                            out.Version = game.substr(space + 1);
                        }
                        else
                            out.Product = game;
                    }
                    break;
                }
                case Section::Backtrace: out.Backtrace.push_back(line); break;
                case Section::Log:       out.LastLog.push_back(line); break;
                case Section::Other:     break;
            }
        }
        return true;
    }

    std::vector<std::vector<std::string>> CrashHandler::GetDialogCommands(const fs::path& report)
    {
        std::vector<std::vector<std::string>> commands;
        const std::string message = MakeDialogMessage(report);
        for (int d = 0; d < s_DialogCount; d++)
        {
            std::vector<std::string> command;
            command.push_back(s_Dialogs[d].Path);
            for (int a = 1; a < s_Dialogs[d].Count; a++)
            {
                std::string arg = s_Dialogs[d].Args[a].Text;
                if (s_Dialogs[d].Args[a].Append == ArgAppend::Report)
                    arg += report.string();
                else if (s_Dialogs[d].Args[a].Append == ArgAppend::Message)
                    arg += message;
                command.push_back(std::move(arg));
            }
            commands.push_back(std::move(command));
        }
        return commands;
    }

    std::string CrashHandler::MakeDialogMessage(const fs::path& report)
    {
        return std::string(s_MessagePrefix) + report.string() + s_MessageSuffix;
    }

}
