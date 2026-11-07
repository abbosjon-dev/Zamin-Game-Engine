#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace Zamin {

    // A place in a source file (1-based line / column; 0 = unknown).
    struct SourceLocation
    {
        std::filesystem::path File;
        int Line = 0;
        int Column = 0;

        bool IsValid() const { return !File.empty(); }
    };

    // Opening source files in the user's own code editor (the editor's script inspector, content
    // browser, compiler errors). The command is a command line with placeholders:
    //   {file}  the file's absolute path        {line}  the line (1 when unknown)
    //   {col}   the column (1 when unknown)     {dir}   the file's folder
    // e.g. "code -g {file}:{line}:{col}", "subl {file}:{line}", "kate -l {line} {file}". A command
    // without {file} gets the file as its last argument. The program is started on its own (the
    // editor does not wait for it and it keeps running when the editor closes).
    class ExternalEditor
    {
    public:
        // Looks up environment variables / whether a program can be run (tests pass their own).
        using GetEnvFn = std::function<const char*(const char*)>;
        using OnPathFn = std::function<bool(std::string_view)>;

        // The command used when the preferences name none: $VISUAL, else $EDITOR (a known editor
        // gets its "go to line" syntax, a terminal editor - vim, nano... - a terminal window), else
        // VS Code when it is installed ("code -g {file}:{line}:{col}"), else "xdg-open {file}".
        static std::string GetDefaultCommand(const GetEnvFn& getEnv = {}, const OnPathFn& onPath = {});
        // The command for a known editor program (its "go to line" syntax): "code" ->
        // "code -g {file}:{line}:{col}"; a terminal editor runs in a terminal ($TERMINAL,
        // x-terminal-emulator, gnome-terminal, konsole, xterm...). "" for programs it does not know.
        static std::string CommandForProgram(std::string_view program, const GetEnvFn& getEnv = {}, const OnPathFn& onPath = {});
        // The editors offered by the preferences that are installed (their commands).
        static std::vector<std::string> GetInstalledCommands(const OnPathFn& onPath = {});

        // Splits a command line into arguments like a shell does: spaces separate, '...' and "..."
        // quote, a backslash escapes the next character (not inside '...').
        static std::vector<std::string> SplitCommandLine(std::string_view command);
        // The arguments to run: the command split, the placeholders substituted in every argument.
        static std::vector<std::string> ExpandCommand(std::string_view command, const std::filesystem::path& file, int line = 0, int column = 0);

        // Starts the program (args[0], looked up on PATH) without waiting for it, in a session of
        // its own. False when it could not be started (not found, not executable), with the reason.
        static bool Launch(const std::vector<std::string>& args, std::string* error = nullptr);
        // Opens `file` at `line` / `column` with `command` ("" = GetDefaultCommand()).
        static bool Open(std::string_view command, const std::filesystem::path& file, int line = 0, int column = 0, std::string* error = nullptr);

        // Whether `program` can be run: a path to an executable file, or a name found on PATH.
        static bool IsOnPath(std::string_view program);

        // Where a script class is written in a project's source folder (searched recursively:
        // .cpp / .cc / .cxx / .h / .hpp): the file that registers it (ZM_REGISTER_SCRIPT(Class ...)),
        // at the line of its "class Class" / "struct Class" in that file (else the registration's
        // line); without a registration, a file declaring the class. Not found: an invalid location.
        static SourceLocation FindScriptSource(const std::filesystem::path& sourceDirectory, std::string_view className);
        // Files with these extensions open in the external editor (the content browser).
        static bool IsSourceFile(const std::filesystem::path& path);
    };

}
