#include "Zamin/Core/Process.h"

#include <cerrno>
#include <cstring>
#include <exception>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace Zamin {
    ProcessResult RunProcess(const std::vector<std::string>& arguments,
                             const std::function<void(const std::string&)>& onLine)
    {
        if (arguments.empty() || arguments.front().empty()) return { -1, "Empty process arguments" };
        std::vector<char*> argv;
        for (const auto& argument : arguments)
        {
            if (argument.find('\0') != std::string::npos) return { -1, "NUL in process argument" };
            argv.push_back(const_cast<char*>(argument.c_str()));
        }
        argv.push_back(nullptr);
        int pipeFD[2];
        if (::pipe2(pipeFD, O_CLOEXEC) != 0) return { -1, std::strerror(errno) };
        for (int& fd : pipeFD)
        {
            if (fd >= 3) continue;
            const int copy = ::fcntl(fd, F_DUPFD_CLOEXEC, 3);
            if (copy < 0)
            {
                const std::string message = std::strerror(errno);
                ::close(pipeFD[0]);
                ::close(pipeFD[1]);
                return { -1, message };
            }
            ::close(fd);
            fd = copy;
        }
        posix_spawn_file_actions_t actions;
        int error = ::posix_spawn_file_actions_init(&actions);
        const bool initialized = error == 0;
        if (!error)
        {
            error = ::posix_spawn_file_actions_adddup2(&actions, pipeFD[1], STDOUT_FILENO);
            if (!error) error = ::posix_spawn_file_actions_adddup2(&actions, pipeFD[1], STDERR_FILENO);
            if (!error) error = ::posix_spawn_file_actions_addclose(&actions, pipeFD[0]);
            if (!error && pipeFD[1] != STDOUT_FILENO && pipeFD[1] != STDERR_FILENO)
                error = ::posix_spawn_file_actions_addclose(&actions, pipeFD[1]);
        }
        pid_t child = -1;
        if (!error) error = ::posix_spawnp(&child, argv[0], &actions, nullptr, argv.data(), environ);
        if (initialized) ::posix_spawn_file_actions_destroy(&actions);
        ::close(pipeFD[1]);
        if (error)
        {
            ::close(pipeFD[0]);
            return { -1, std::strerror(error) };
        }
        std::exception_ptr callbackError;
        std::string line;
        char buffer[4096];
        auto deliver = [&]() {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (onLine && !callbackError)
            {
                try
                {
                    onLine(line);
                }
                catch (...)
                {
                    callbackError = std::current_exception();
                }
            }
            line.clear();
        };
        std::string readError;
        for (;;)
        {
            const ssize_t count = ::read(pipeFD[0], buffer, sizeof(buffer));
            if (count < 0 && errno == EINTR) continue;
            if (count < 0) readError = std::strerror(errno);
            if (count <= 0) break;
            for (ssize_t i = 0; i < count; ++i)
            {
                if (buffer[i] == '\n') deliver();
                else
                {
                    line += buffer[i];
                    // Bound a malformed compiler's line without stopping pipe drainage.
                    if (line.size() >= 65536) deliver();
                }
            }
        }
        ::close(pipeFD[0]);
        if (!line.empty()) deliver();
        int status = 0;
        pid_t waited;
        do {
            waited = ::waitpid(child, &status, 0);
        } while (waited < 0 && errno == EINTR);
        if (callbackError) std::rethrow_exception(callbackError);
        if (waited < 0) return { -1, std::strerror(errno) };
        return { WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status), readError };
    }
}
