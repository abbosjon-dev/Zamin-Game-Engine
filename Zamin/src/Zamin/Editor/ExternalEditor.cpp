#include "Zamin/Editor/ExternalEditor.h"

#include "Zamin/Utils/FileSystem.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <format>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace Zamin {

    namespace {
        const char* DefaultGetEnv(const char* name) { return std::getenv(name); }

        // The program's name without its folder ("/usr/bin/code" -> "code").
        std::string ProgramName(std::string_view program)
        {
            size_t slash = program.find_last_of('/');
            return std::string(slash == std::string_view::npos ? program : program.substr(slash + 1));
        }

        struct KnownEditor
        {
            const char* Program;
            const char* Arguments;  // after the program
            bool Terminal;          // runs in a terminal
        };
        // "Go to line" syntaxes of common editors.
        constexpr KnownEditor s_KnownEditors[] = {
            { "code", "-g {file}:{line}:{col}", false },
            { "code-insiders", "-g {file}:{line}:{col}", false },
            { "codium", "-g {file}:{line}:{col}", false },
            { "vscodium", "-g {file}:{line}:{col}", false },
            { "cursor", "-g {file}:{line}:{col}", false },
            { "subl", "{file}:{line}:{col}", false },
            { "sublime_text", "{file}:{line}:{col}", false },
            { "zed", "{file}:{line}:{col}", false },
            { "zeditor", "{file}:{line}:{col}", false },
            { "atom", "{file}:{line}:{col}", false },
            { "kate", "-l {line} -c {col} {file}", false },
            { "kwrite", "-l {line} -c {col} {file}", false },
            { "gedit", "+{line} {file}", false },
            { "gnome-text-editor", "+{line} {file}", false },
            { "geany", "--line={line} {file}", false },
            { "mousepad", "--line={line} {file}", false },
            { "pluma", "+{line} {file}", false },
            { "clion", "--line {line} --column {col} {file}", false },
            { "clion.sh", "--line {line} --column {col} {file}", false },
            { "qtcreator", "-client {file}:{line}:{col}", false },
            { "gvim", "+{line} {file}", false },
            { "emacsclient", "-n +{line}:{col} {file}", false },
            { "emacs", "+{line}:{col} {file}", false },
            { "vim", "+{line} {file}", true },
            { "vi", "+{line} {file}", true },
            { "nvim", "+{line} {file}", true },
            { "nano", "+{line},{col} {file}", true },
            { "micro", "{file}:{line}:{col}", true },
            { "hx", "{file}:{line}:{col}", true },
            { "helix", "{file}:{line}:{col}", true },
            { "kak", "+{line}:{col} {file}", true },
        };

        const KnownEditor* FindKnownEditor(std::string_view program)
        {
            const std::string name = ProgramName(program);
            for (const KnownEditor& e : s_KnownEditors)
                if (name == e.Program)
                    return &e;
            return nullptr;
        }

        // How to run a command in a terminal window ("" = no terminal found).
        std::string TerminalPrefix(const ExternalEditor::GetEnvFn& getEnv, const ExternalEditor::OnPathFn& onPath)
        {
            if (const char* terminal = getEnv("TERMINAL"); terminal && *terminal && onPath(terminal))
                return std::string(terminal) + " -e ";
            struct Terminal { const char* Program; const char* Run; };
            static constexpr Terminal s_Terminals[] = {
                { "x-terminal-emulator", "x-terminal-emulator -e " }, { "gnome-terminal", "gnome-terminal -- " },
                { "konsole", "konsole -e " }, { "xfce4-terminal", "xfce4-terminal -x " }, { "kitty", "kitty " },
                { "alacritty", "alacritty -e " }, { "wezterm", "wezterm start -- " }, { "xterm", "xterm -e " },
            };
            for (const Terminal& t : s_Terminals)
                if (onPath(t.Program))
                    return t.Run;
            return {};
        }

        // A shell-quoted word when it needs quoting (commands are rebuilt from $EDITOR's program).
        std::string QuoteWord(std::string_view word)
        {
            if (!word.empty() && word.find_first_of(" \t'\"\\") == std::string_view::npos)
                return std::string(word);
            std::string out = "'";
            for (char c : word)
                out += c == '\'' ? std::string("'\\''") : std::string(1, c);
            return out + "'";
        }

        bool IsIdentifierChar(char c) { return std::isalnum((unsigned char)c) || c == '_'; }

        // Skips spaces / tabs from `i`.
        size_t SkipSpaces(std::string_view s, size_t i)
        {
            while (i < s.size() && (s[i] == ' ' || s[i] == '\t'))
                i++;
            return i;
        }
    }

    // ------------------------------------------------------------------ commands
    std::string ExternalEditor::CommandForProgram(std::string_view program, const GetEnvFn& getEnvIn, const OnPathFn& onPathIn)
    {
        const GetEnvFn getEnv = getEnvIn ? getEnvIn : GetEnvFn(DefaultGetEnv);
        const OnPathFn onPath = onPathIn ? onPathIn : OnPathFn(IsOnPath);
        const KnownEditor* known = FindKnownEditor(program);
        if (!known)
            return {};
        std::string command = QuoteWord(program) + " " + known->Arguments;
        if (known->Terminal)
        {
            std::string terminal = TerminalPrefix(getEnv, onPath);
            if (terminal.empty())
                return {}; // a terminal editor without a terminal to run it in
            command = terminal + command;
        }
        return command;
    }

    std::string ExternalEditor::GetDefaultCommand(const GetEnvFn& getEnvIn, const OnPathFn& onPathIn)
    {
        const GetEnvFn getEnv = getEnvIn ? getEnvIn : GetEnvFn(DefaultGetEnv);
        const OnPathFn onPath = onPathIn ? onPathIn : OnPathFn(IsOnPath);
        for (const char* variable : { "VISUAL", "EDITOR" })
        {
            const char* value = getEnv(variable);
            if (!value || !*value)
                continue;
            std::vector<std::string> words = SplitCommandLine(value);
            if (words.empty() || !onPath(words[0]))
                continue;
            if (std::string known = CommandForProgram(words[0], getEnv, onPath); !known.empty())
                return known;
            if (FindKnownEditor(words[0]))
                continue; // a terminal editor with no terminal: try the next choice
            // An editor it does not know: its own command line with the file at the end.
            return std::string(value) + " {file}";
        }
        if (onPath("code"))
            return "code -g {file}:{line}:{col}";
        return "xdg-open {file}";
    }

    std::vector<std::string> ExternalEditor::GetInstalledCommands(const OnPathFn& onPathIn)
    {
        const OnPathFn onPath = onPathIn ? onPathIn : OnPathFn(IsOnPath);
        std::vector<std::string> out;
        for (const KnownEditor& e : s_KnownEditors)
            if (!e.Terminal && onPath(e.Program))
                out.push_back(std::string(e.Program) + " " + e.Arguments);
        return out;
    }

    std::vector<std::string> ExternalEditor::SplitCommandLine(std::string_view command)
    {
        std::vector<std::string> words;
        std::string word;
        bool inWord = false;
        char quote = 0;
        for (size_t i = 0; i < command.size(); i++)
        {
            char c = command[i];
            if (quote == '\'')
            {
                if (c == '\'')
                    quote = 0;
                else
                    word += c;
                continue;
            }
            if (c == '\\' && i + 1 < command.size() && (quote == 0 || command[i + 1] == '"' || command[i + 1] == '\\'))
            {
                word += command[++i];
                inWord = true;
                continue;
            }
            if (quote == '"')
            {
                if (c == '"')
                    quote = 0;
                else
                    word += c;
                continue;
            }
            if (c == '\'' || c == '"')
            {
                quote = c;
                inWord = true;
                continue;
            }
            if (c == ' ' || c == '\t' || c == '\n')
            {
                if (inWord)
                    words.push_back(std::move(word));
                word.clear();
                inWord = false;
                continue;
            }
            word += c;
            inWord = true;
        }
        if (inWord)
            words.push_back(std::move(word));
        return words;
    }

    std::vector<std::string> ExternalEditor::ExpandCommand(std::string_view command, const fs::path& file, int line, int column)
    {
        std::vector<std::string> words = SplitCommandLine(command);
        const std::string path = file.string();
        const std::string folder = file.parent_path().string();
        const std::string lineText = std::to_string(std::max(1, line));
        const std::string columnText = std::to_string(std::max(1, column));
        bool hasFile = false;
        for (std::string& w : words)
        {
            std::string out;
            for (size_t i = 0; i < w.size(); i++)
            {
                if (w[i] == '{')
                {
                    size_t close = w.find('}', i);
                    if (close != std::string::npos)
                    {
                        std::string_view name(w.data() + i + 1, close - i - 1);
                        const std::string* value = nullptr;
                        if (name == "file") { value = &path; hasFile = true; }
                        else if (name == "line") value = &lineText;
                        else if (name == "col" || name == "column") value = &columnText;
                        else if (name == "dir") value = &folder;
                        if (value)
                        {
                            out += *value;
                            i = close;
                            continue;
                        }
                    }
                }
                out += w[i];
            }
            w = std::move(out);
        }
        if (!hasFile && !words.empty())
            words.push_back(path);
        return words;
    }

    // ------------------------------------------------------------------ running
    bool ExternalEditor::IsOnPath(std::string_view program)
    {
        if (program.empty())
            return false;
        auto executable = [](const std::string& path) {
            struct stat st{};
            return ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode) && ::access(path.c_str(), X_OK) == 0;
        };
        if (program.find('/') != std::string_view::npos)
            return executable(std::string(program));
        const char* path = std::getenv("PATH");
        std::string_view dirs = path && *path ? path : "/usr/local/bin:/usr/bin:/bin";
        while (!dirs.empty())
        {
            size_t colon = dirs.find(':');
            std::string_view dir = dirs.substr(0, colon);
            if (!dir.empty() && executable(std::string(dir) + "/" + std::string(program)))
                return true;
            if (colon == std::string_view::npos)
                break;
            dirs.remove_prefix(colon + 1);
        }
        return false;
    }

    bool ExternalEditor::Launch(const std::vector<std::string>& args, std::string* error)
    {
        auto fail = [&](std::string reason) {
            if (error)
                *error = std::move(reason);
            return false;
        };
        if (args.empty() || args[0].empty())
            return fail("buyruq bo'sh");
        if (!IsOnPath(args[0]))
            return fail(std::format("'{}' dasturi topilmadi (PATH'da yo'q yoki ishga tushirib bo'lmaydi)", args[0]));

        // Everything the child needs is made before fork (only async-signal-safe calls after it).
        std::vector<char*> argv;
        argv.reserve(args.size() + 1);
        for (const std::string& a : args)
            argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);

        // exec's error comes back through the pipe; a successful exec closes it (O_CLOEXEC).
        int pipeFds[2];
        if (::pipe2(pipeFds, O_CLOEXEC) != 0)
            return fail(std::format("pipe: {}", std::strerror(errno)));
        pid_t child = ::fork();
        if (child < 0)
        {
            int err = errno;
            ::close(pipeFds[0]);
            ::close(pipeFds[1]);
            return fail(std::format("fork: {}", std::strerror(err)));
        }
        if (child == 0)
        {
            // In between: a session of its own, then the program as its child - this one exits at
            // once, so the program is nobody's zombie and is not tied to the editor.
            ::setsid();
            pid_t program = ::fork();
            if (program == 0)
            {
                int devNull = ::open("/dev/null", O_RDONLY);
                if (devNull >= 0)
                {
                    ::dup2(devNull, STDIN_FILENO); // a terminal program does not read the editor's terminal
                    ::close(devNull);
                }
                ::execvp(argv[0], argv.data());
                int err = errno;
                [[maybe_unused]] ssize_t written = ::write(pipeFds[1], &err, sizeof(err));
                ::_exit(127);
            }
            if (program < 0)
            {
                int err = errno;
                [[maybe_unused]] ssize_t written = ::write(pipeFds[1], &err, sizeof(err));
            }
            ::_exit(0);
        }
        ::close(pipeFds[1]);
        int status = 0;
        while (::waitpid(child, &status, 0) < 0 && errno == EINTR)
        {
        }
        int err = 0;
        ssize_t n;
        do
            n = ::read(pipeFds[0], &err, sizeof(err));
        while (n < 0 && errno == EINTR);
        ::close(pipeFds[0]);
        if (n == (ssize_t)sizeof(err))
            return fail(std::format("'{}' ishga tushmadi: {}", args[0], std::strerror(err)));
        return true;
    }

    bool ExternalEditor::Open(std::string_view command, const fs::path& file, int line, int column, std::string* error)
    {
        std::string chosen = command.empty() ? GetDefaultCommand() : std::string(command);
        std::error_code ec;
        fs::path absolute = fs::absolute(file, ec);
        return Launch(ExpandCommand(chosen, ec ? file : absolute.lexically_normal(), line, column), error);
    }

    // ------------------------------------------------------------------ script sources
    bool ExternalEditor::IsSourceFile(const fs::path& path)
    {
        std::string ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        return ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".c" || ext == ".h" || ext == ".hpp" || ext == ".hh" ||
               ext == ".hxx" || ext == ".inl";
    }

    // The line (1-based) and column of "class <name>" / "struct <name>" that defines it (not a
    // forward declaration), or 0.
    static int FindClassDeclaration(const std::vector<std::string_view>& lines, std::string_view name, int* column)
    {
        for (size_t l = 0; l < lines.size(); l++)
        {
            std::string_view line = lines[l];
            size_t first = SkipSpaces(line, 0);
            if (line.substr(first).starts_with("//") || line.substr(first).starts_with("#"))
                continue;
            for (std::string_view keyword : { std::string_view("class"), std::string_view("struct") })
            {
                size_t at = 0;
                while ((at = line.find(keyword, at)) != std::string_view::npos)
                {
                    const size_t end = at + keyword.size();
                    const bool word = (at == 0 || !IsIdentifierChar(line[at - 1])) && end < line.size() && (line[end] == ' ' || line[end] == '\t');
                    if (word)
                    {
                        size_t n = SkipSpaces(line, end);
                        if (line.substr(n).starts_with(name) && (n + name.size() == line.size() || !IsIdentifierChar(line[n + name.size()])))
                        {
                            size_t after = SkipSpaces(line, n + name.size());
                            if (after >= line.size() || line[after] != ';')
                            {
                                if (column)
                                    *column = (int)at + 1;
                                return (int)l + 1;
                            }
                        }
                    }
                    at = end;
                }
            }
        }
        return 0;
    }

    // The line (1-based) of ZM_REGISTER_SCRIPT(<name> ...), or 0.
    static int FindRegistration(const std::vector<std::string_view>& lines, std::string_view name)
    {
        constexpr std::string_view macro = "ZM_REGISTER_SCRIPT";
        for (size_t l = 0; l < lines.size(); l++)
        {
            std::string_view line = lines[l];
            size_t first = SkipSpaces(line, 0);
            if (line.substr(first).starts_with("//") || line.substr(first).starts_with("#"))
                continue; // comments, the macro's own #define
            size_t at = 0;
            while ((at = line.find(macro, at)) != std::string_view::npos)
            {
                size_t i = SkipSpaces(line, at + macro.size());
                at += macro.size();
                if (i >= line.size() || line[i] != '(')
                    continue;
                i = SkipSpaces(line, i + 1);
                if (!line.substr(i).starts_with(name))
                    continue;
                i = SkipSpaces(line, i + name.size());
                if (i < line.size() && (line[i] == ',' || line[i] == ')'))
                    return (int)l + 1;
            }
        }
        return 0;
    }

    static std::vector<std::string_view> SplitLines(const std::string& text)
    {
        std::vector<std::string_view> lines;
        size_t start = 0;
        while (start <= text.size())
        {
            size_t end = text.find('\n', start);
            if (end == std::string::npos)
                end = text.size();
            std::string_view line(text.data() + start, end - start);
            if (!line.empty() && line.back() == '\r')
                line.remove_suffix(1);
            lines.push_back(line);
            start = end + 1;
        }
        return lines;
    }

    SourceLocation ExternalEditor::FindScriptSource(const fs::path& sourceDirectory, std::string_view className)
    {
        SourceLocation found;
        if (className.empty() || sourceDirectory.empty())
            return found;
        std::error_code ec;
        std::vector<fs::path> files;
        for (fs::recursive_directory_iterator it(sourceDirectory, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
             it.increment(ec))
            if (it->is_regular_file(ec) && IsSourceFile(it->path()))
                files.push_back(it->path());
        std::sort(files.begin(), files.end()); // the same answer every time

        SourceLocation declaration; // a file declaring the class (when nothing registers it)
        for (const fs::path& file : files)
        {
            const std::string text = FileSystem::ReadTextFile(file);
            if (text.find(className) == std::string::npos)
                continue;
            const std::vector<std::string_view> lines = SplitLines(text);
            int column = 0;
            if (int registration = FindRegistration(lines, className))
            {
                int classLine = FindClassDeclaration(lines, className, &column);
                found.File = file;
                found.Line = classLine ? classLine : registration;
                found.Column = classLine ? column : 1;
                return found;
            }
            if (!declaration.IsValid())
                if (int classLine = FindClassDeclaration(lines, className, &column))
                    declaration = { file, classLine, column };
        }
        return declaration;
    }

}
