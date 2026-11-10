#pragma once

#include <functional>
#include <string>
#include <vector>

namespace Zamin {
    struct ProcessResult
    {
        int ExitCode = -1;
        std::string Error;
        bool Succeeded() const { return ExitCode == 0 && Error.empty(); }
    };

    // Executes argv directly, without a shell. stdout/stderr are merged and streamed by line.
    // The child is always reaped, including when the callback throws.
    ProcessResult RunProcess(const std::vector<std::string>& arguments,
                             const std::function<void(const std::string&)>& onLine = {});
}
