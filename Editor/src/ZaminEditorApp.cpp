#include <Zamin/Zamin.h>
#include <Zamin/Core/EntryPoint.h>

#include "EditorLayer.h"

namespace Zamin {

    class ZaminEditor : public Application
    {
    public:
        ZaminEditor(const ApplicationSpecification& spec, std::filesystem::path project)
            : Application(spec)
        {
            PushLayer(new EditorLayer(std::move(project)));
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

        std::filesystem::path project;
        if (args.Count > 1)
            project = std::filesystem::absolute(args[1]);
        return new ZaminEditor(spec, project);
    }

}
