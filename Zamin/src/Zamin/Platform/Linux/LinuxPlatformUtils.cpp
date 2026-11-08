#include "Zamin/Utils/PlatformUtils.h"
#include "Zamin/Core/Log.h"

#include <GLFW/glfw3.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <sys/wait.h>

namespace Zamin {

    static std::string ShellQuote(const std::string& s)
    {
        std::string out = "'";
        for (char c : s)
        {
            if (c == '\'') out += "'\\''";
            else out += c;
        }
        return out + "'";
    }

    static bool HasProgram(const char* name)
    {
        std::string cmd = std::string("command -v ") + name + " >/dev/null 2>&1";
        return std::system(cmd.c_str()) == 0;
    }

    static std::string ExtractPattern(const char* filter)
    {
        // "Zamin Scene (*.zscene) | *.zscene" -> "*.zscene"
        std::string f = filter ? filter : "";
        size_t bar = f.find('|');
        std::string pattern = bar == std::string::npos ? f : f.substr(bar + 1);
        while (!pattern.empty() && pattern.front() == ' ') pattern.erase(0, 1);
        while (!pattern.empty() && pattern.back() == ' ') pattern.pop_back();
        return pattern;
    }

    static std::string RunDialog(const std::string& cmd)
    {
        std::string output;
        int code = Platform::RunCommand(cmd + " 2>/dev/null", &output);
        if (code != 0)
            return {};
        while (!output.empty() && (output.back() == '\n' || output.back() == '\r'))
            output.pop_back();
        return output;
    }

    bool FileDialogs::IsAvailable() { return HasProgram("zenity") || HasProgram("kdialog"); }

    std::string FileDialogs::OpenFile(const char* filter, const std::filesystem::path& startDir)
    {
        std::string pattern = ExtractPattern(filter);
        std::string dir = startDir.empty() ? std::filesystem::current_path().string() : startDir.string();
        if (HasProgram("zenity"))
            return RunDialog("zenity --file-selection --title='Faylni ochish' --filename=" + ShellQuote(dir + "/") +
                             (pattern.empty() ? "" : " --file-filter=" + ShellQuote(std::string(filter))));
        if (HasProgram("kdialog"))
            return RunDialog("kdialog --getopenfilename " + ShellQuote(dir) + " " + ShellQuote(pattern));
        ZM_CORE_WARN("Fayl dialogi uchun 'zenity' yoki 'kdialog' o'rnating");
        return {};
    }

    std::string FileDialogs::SaveFile(const char* filter, const std::filesystem::path& startDir)
    {
        std::string pattern = ExtractPattern(filter);
        std::string dir = startDir.empty() ? std::filesystem::current_path().string() : startDir.string();
        if (HasProgram("zenity"))
            return RunDialog("zenity --file-selection --save --confirm-overwrite --title='Saqlash' --filename=" + ShellQuote(dir + "/") +
                             (pattern.empty() ? "" : " --file-filter=" + ShellQuote(std::string(filter))));
        if (HasProgram("kdialog"))
            return RunDialog("kdialog --getsavefilename " + ShellQuote(dir) + " " + ShellQuote(pattern));
        ZM_CORE_WARN("Fayl dialogi uchun 'zenity' yoki 'kdialog' o'rnating");
        return {};
    }

    std::string FileDialogs::SelectFolder(const std::filesystem::path& startDir)
    {
        std::string dir = startDir.empty() ? std::filesystem::current_path().string() : startDir.string();
        if (HasProgram("zenity"))
            return RunDialog("zenity --file-selection --directory --title='Papkani tanlash' --filename=" + ShellQuote(dir + "/"));
        if (HasProgram("kdialog"))
            return RunDialog("kdialog --getexistingdirectory " + ShellQuote(dir));
        ZM_CORE_WARN("Fayl dialogi uchun 'zenity' yoki 'kdialog' o'rnating");
        return {};
    }

    float Platform::GetTime() { return (float)glfwGetTime(); }

    void Platform::OpenInFileBrowser(const std::filesystem::path& path)
    {
        std::string cmd = "xdg-open " + ShellQuote(path.string()) + " >/dev/null 2>&1 &";
        if (std::system(cmd.c_str()) != 0)
            ZM_CORE_WARN("Fayl brauzeri ochilmadi: {}", path.string());
    }

    int Platform::RunCommand(const std::string& command, std::string* output)
    {
        FILE* pipe = popen(command.c_str(), "r");
        if (!pipe)
            return -1;
        std::array<char, 512> buffer{};
        while (fgets(buffer.data(), (int)buffer.size(), pipe))
        {
            if (output)
                *output += buffer.data();
        }
        int status = pclose(pipe);
        return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }

}
