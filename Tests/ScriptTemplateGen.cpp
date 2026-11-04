// Build-time check: writes the new-script templates (Editor > Skriptlar > Yangi C++ skript)
// to files that the build then compiles (target ZaminScriptTemplateCheck), so a template
// that stops compiling breaks the build instead of the user's first script.
#include <Zamin/Scripting/ScriptTemplate.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

static bool WriteIfChanged(const std::filesystem::path& path, const std::string& text)
{
    std::ifstream in(path, std::ios::binary);
    if (in)
    {
        std::stringstream old;
        old << in.rdbuf();
        if (old.str() == text)
            return true;
    }
    std::ofstream out(path, std::ios::binary);
    out << text;
    return (bool)out;
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "ishlatish: ZaminScriptTemplateGen <papka>\n");
        return 1;
    }
    std::filesystem::path dir = argv[1];
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    using namespace Zamin;
    bool ok = WriteIfChanged(dir / "TemplateFullCheck.cpp", ScriptTemplate::Generate("TemplateFullCheck", ScriptTemplateKind::Full)) &&
              WriteIfChanged(dir / "TemplateMinimalCheck.cpp", ScriptTemplate::Generate("TemplateMinimalCheck", ScriptTemplateKind::Minimal)) &&
              WriteIfChanged(dir / "ProjectStarterCheck.cpp", ScriptTemplate::GenerateProjectStarter());
    if (!ok)
        std::fprintf(stderr, "Shablon fayllari yozilmadi: %s\n", dir.string().c_str());
    return ok ? 0 : 1;
}
