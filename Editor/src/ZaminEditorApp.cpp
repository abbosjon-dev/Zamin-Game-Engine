#include <Zamin/Zamin.h>
#include <Zamin/Core/EntryPoint.h>

#include "EditorLayer.h"

namespace Zamin {

    class ZaminEditor : public Application
    {
    public:
        ZaminEditor(const ApplicationSpecification& spec, std::filesystem::path project, const LogLevelOverride& startupLogLevels)
            : Application(spec)
        {
            PushLayer(new EditorLayer(std::move(project), startupLogLevels));
        }
    };

    Application* CreateApplication(ApplicationCommandLineArgs args)
    {
        ApplicationSpecification spec;
        spec.Name = "Zamin Editor";
        spec.CommandLineArgs = args;
        spec.Window.Title = "Zamin Editor";
        spec.Window.Width = 1600;
        spec.Window.Height = 900;
        spec.Window.Maximized = true;

        // ZaminEditor [project] [--log-level <spec>]: the levels win over the preferences' at start-up
        // (ZAMIN_LOG_LEVEL too, under the command line).
        std::filesystem::path project;
        LogLevelOverride levels = LogLevelOverride::FromEnvironment();
        for (int i = 1; i < args.Count; i++)
        {
            const std::string_view arg = args[i] ? args[i] : "";
            if (arg == "--log-level" || arg.starts_with("--log-level="))
            {
                std::string_view value;
                if (arg.size() > 11)
                    value = arg.substr(12);
                else if (i + 1 < args.Count && args[i + 1])
                    value = args[++i];
                std::string error;
                if (!LogLevelOverride::Parse(value, levels, &error))
                    ZM_WARN("Buyruq qatori: --log-level {}: {}", value, error);
            }
            else if (!arg.empty() && arg[0] != '-' && project.empty())
                project = std::filesystem::absolute(arg);
            else if (!arg.empty())
                ZM_WARN("Buyruq qatori: noma'lum argument '{}' (ZaminEditor [loyiha] [--log-level <daraja>])", arg);
        }
        levels.Over(LogLevelSettings::GetCurrent()).Apply();
        return new ZaminEditor(spec, project, levels);
    }

}
