#include "Zamin/Core/BuildInfo.h"

#include "Zamin/Core/Version.h"
#include "Zamin/Project/Project.h"

// ZM_BUILD_COMMIT, ZM_BUILD_COMMIT_DATE, ZM_BUILD_DIRTY, ZM_BUILD_DATE, ZM_BUILD_TYPE (the build writes it).
#include "ZaminBuildInfo.inc"

#include <format>

namespace Zamin {

    namespace {

        std::string CompilerName()
        {
#if defined(__clang__)
            return std::format("Clang {}.{}.{}", __clang_major__, __clang_minor__, __clang_patchlevel__);
#elif defined(__GNUC__)
            return std::format("GCC {}.{}.{}", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#elif defined(_MSC_VER)
            return std::format("MSVC {}", _MSC_VER);
#else
            return "noma'lum kompilyator";
#endif
        }

    }

    const BuildInfo& BuildInfo::Get()
    {
        static const BuildInfo s_Info = []() {
            BuildInfo info;
            info.EngineVersion = ZM_ENGINE_VERSION;
            info.Commit = ZM_BUILD_COMMIT;
            info.Dirty = ZM_BUILD_DIRTY != 0 && !info.Commit.empty();
            info.CommitDate = ZM_BUILD_COMMIT_DATE;
            info.BuildDate = ZM_BUILD_DATE;
            info.BuildType = ZM_BUILD_TYPE;
            info.Compiler = CompilerName();
#ifdef ZAMIN_SHIPPING
            info.Shipping = true;
#endif
            return info;
        }();
        return s_Info;
    }

    std::string BuildInfo::GetCommitText() const
    {
        if (Commit.empty())
            return {};
        return Dirty ? Commit + "-dirty" : Commit;
    }

    std::string BuildInfo::Describe() const
    {
        std::string details;
        auto add = [&](const std::string& part) {
            if (!part.empty())
                details += (details.empty() ? "" : ", ") + part;
        };
        add(Commit.empty() ? std::string() : "kommit " + GetCommitText());
        add(BuildDate.empty() ? std::string() : "build " + BuildDate);
        add(BuildType + (Shipping ? " Shipping" : ""));
        add(Compiler);
        return std::format("Zamin Engine {}{}", EngineVersion, details.empty() ? std::string() : " (" + details + ")");
    }

    std::string BuildInfo::GetShortVersion() const
    {
        if (Commit.empty())
            return EngineVersion;
        return std::format("{} ({}{})", EngineVersion, Commit.substr(0, 7), Dirty ? "*" : "");
    }

    VersionInfo VersionInfo::ForActiveProject()
    {
        if (const Ref<Project> project = Project::GetActive())
            return ForConfig(project->GetConfig());
        return VersionInfo();
    }

    VersionInfo VersionInfo::ForConfig(const ProjectConfig& config)
    {
        VersionInfo info;
        info.Product = config.Player.GetProductName(config.Name);
        info.Company = config.Player.CompanyName;
        info.Version = config.Player.Version;
        info.HasGame = true;
        return info;
    }

    std::string VersionInfo::Format(const std::string& program) const
    {
        std::string text;
        if (HasGame)
            text += std::format("{} {}{}\n", Product, Version, Company.empty() ? std::string() : " - " + Company);
        else
            text += program + "\n";
        text += std::format("Zamin Engine {}{}\n", Engine.EngineVersion, Engine.Shipping ? " (Shipping)" : "");
        if (!Engine.Commit.empty())
            text += std::format("  Kommit:  {}{}{}\n", Engine.Commit, Engine.CommitDate.empty() ? std::string() : " (" + Engine.CommitDate + ")",
                                Engine.Dirty ? "  [o'zgarishlar bilan]" : "");
        else
            text += "  Kommit:  noma'lum (git'siz build)\n";
        std::string build = Engine.BuildDate;
        for (const std::string& part : { Engine.BuildType, Engine.Compiler })
            if (!part.empty())
                build += (build.empty() ? "" : ", ") + part;
        text += std::format("  Build:   {}\n", build.empty() ? std::string("-") : build);
        return text;
    }

    std::string VersionInfo::FormatShort() const
    {
        const std::string engine = "Zamin Engine " + Engine.GetShortVersion();
        if (!HasGame)
            return engine;
        return std::format("{} v{}  |  {}", Product, Version, engine);
    }

}
