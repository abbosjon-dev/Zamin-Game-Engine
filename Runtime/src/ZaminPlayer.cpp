// Zamin Player: runs an exported / development game project fullscreen-ready.
//   ZaminPlayer [path/to/Game.zproject] [--scene Scenes/Level.zscene] [--fullscreen] [--width W --height H]
#include <Zamin/Zamin.h>
#include <Zamin/Core/EntryPoint.h>

#include <cmath>
#include <cstring>

namespace Zamin {

    class RuntimeLayer : public Layer
    {
    public:
        RuntimeLayer(std::filesystem::path project, std::string scene)
            : Layer("RuntimeLayer"), m_ProjectPath(std::move(project)), m_SceneOverride(std::move(scene)) {}

        void OnAttach() override
        {
            if (!Project::Load(m_ProjectPath))
            {
                ZM_CRITICAL("Loyiha yuklanmadi: {}", m_ProjectPath.string());
                Application::Get().Close();
                return;
            }
            auto& config = Project::GetActive()->GetConfig();
            Application::Get().GetWindow().SetTitle(config.Name);

            if (auto module = Project::GetScriptModulePath(); !module.empty())
                ScriptEngine::LoadModule(module);

            std::filesystem::path scenePath = Project::GetAssetFileSystemPath(m_SceneOverride.empty() ? config.StartScene : std::filesystem::path(m_SceneOverride));
            m_Scene = CreateRef<Scene>();
            SceneSerializer serializer(m_Scene);
            if (!serializer.Deserialize(scenePath))
            {
                ZM_CRITICAL("Boshlang'ich sahna yuklanmadi: {} ({})", scenePath.string(), serializer.GetLastError());
                Application::Get().Close();
                return;
            }
            m_Scene->ResolveAssets();
            auto& window = Application::Get().GetWindow();
            m_Scene->OnViewportResize(window.GetFramebufferWidth(), window.GetFramebufferHeight());
            m_Scene->OnRuntimeStart();
        }

        void OnDetach() override
        {
            if (m_Scene)
            {
                m_Scene->OnRuntimeStop();
                m_Scene.reset();
            }
            ScriptEngine::UnloadModule();
        }

        void OnUpdate(Timestep ts) override
        {
            if (!m_Scene) return;
            auto& window = Application::Get().GetWindow();
            uint32_t w = window.GetFramebufferWidth(), h = window.GetFramebufferHeight();
            m_Scene->OnViewportResize(w, h);

            Renderer::ResetStats();
            glm::vec2 mouse = Input::GetMousePosition();
            float scale = window.GetWidth() > 0 ? (float)w / (float)window.GetWidth() : 1.0f;
            m_Scene->SetViewportMouse(mouse * scale, Input::IsMouseButtonPressed(MouseButton::Left));

            const auto& post = m_Scene->GetSettings().Post;
            // The scene is rendered in linear HDR, then tonemapped to the screen.
            if (!m_Target)
            {
                FramebufferSpecification spec;
                spec.Width = w; spec.Height = h;
                spec.Attachments = { FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::Depth };
                m_Target = Framebuffer::Create(spec);
            }
            else if (m_Target->GetSpecification().Width != w || m_Target->GetSpecification().Height != h)
                m_Target->Resize(w, h);
            m_Target->Bind();

            glm::vec4 clear = m_Scene->GetClearColor();
            RenderCommand::SetClearColor({ std::pow(clear.r, 2.2f), std::pow(clear.g, 2.2f), std::pow(clear.b, 2.2f), clear.a });
            RenderCommand::Clear();
            m_Scene->OnUpdateRuntime(ts);

            {
                m_Target->Unbind();
                RenderCommand::SetViewport(0, 0, w, h);
                PostProcessor::Apply(m_Target->GetColorAttachmentRendererID(0), w, h, post, nullptr,
                                     m_Target->GetDepthAttachmentRendererID(), m_Scene->GetLastProjection());
            }

            if (!m_Scene->GetPendingSceneLoad().empty())
            {
                std::filesystem::path next = Project::GetAssetFileSystemPath(m_Scene->GetPendingSceneLoad());
                m_Scene->ClearPendingSceneLoad();
                Ref<Scene> scene = CreateRef<Scene>();
                if (SceneSerializer(scene).Deserialize(next))
                {
                    m_Scene->OnRuntimeStop();
                    scene->ResolveAssets();
                    scene->OnViewportResize(w, h);
                    m_Scene = scene;
                    m_Scene->OnRuntimeStart();
                }
                else
                    ZM_ERROR("Sahna yuklanmadi: {}", next.string());
            }
        }

        void OnEvent(Event& e) override
        {
            EventDispatcher dispatcher(e);
            dispatcher.Dispatch<KeyPressedEvent>([](KeyPressedEvent& ev) {
                if (ev.GetKeyCode() == Key::F11)
                    ZM_INFO("FPS: {:.0f}", Application::Get().GetFPS());
                return false;
            });
        }

    private:
        std::filesystem::path m_ProjectPath;
        std::string m_SceneOverride;
        Ref<Scene> m_Scene;
        Ref<Framebuffer> m_Target;
    };

    class ZaminPlayer : public Application
    {
    public:
        ZaminPlayer(const ApplicationSpecification& spec, std::filesystem::path project, std::string scene)
            : Application(spec)
        {
            PushLayer(new RuntimeLayer(std::move(project), std::move(scene)));
        }
    };

    static std::filesystem::path FindProject(const char* arg)
    {
        namespace fs = std::filesystem;
        if (arg)
        {
            fs::path p = fs::absolute(arg);
            if (fs::is_directory(p))
                for (auto& e : fs::directory_iterator(p))
                    if (e.path().extension() == ".zproject") return e.path();
            return p;
        }
        // Exported game layout: <exe dir>/Game/<Name>.zproject
        for (const fs::path& dir : { FileSystem::GetExecutableDirectory() / "Game", FileSystem::GetExecutableDirectory() })
        {
            std::error_code ec;
            if (!fs::exists(dir, ec)) continue;
            for (auto& e : fs::directory_iterator(dir, ec))
                if (e.path().extension() == ".zproject") return e.path();
        }
        return {};
    }

    Application* CreateApplication(ApplicationCommandLineArgs args)
    {
        ApplicationSpecification spec;
        spec.Name = "Zamin Player";
        spec.CommandLineArgs = args;
        spec.EnableImGui = false;
        spec.Window.Width = 1280;
        spec.Window.Height = 720;

        const char* projectArg = nullptr;
        std::string scene;
        for (int i = 1; i < args.Count; i++)
        {
            if (!std::strcmp(args[i], "--fullscreen")) spec.Window.Fullscreen = true;
            else if (!std::strcmp(args[i], "--scene") && i + 1 < args.Count) scene = args[++i];
            else if (!std::strcmp(args[i], "--width") && i + 1 < args.Count) spec.Window.Width = (uint32_t)std::atoi(args[++i]);
            else if (!std::strcmp(args[i], "--height") && i + 1 < args.Count) spec.Window.Height = (uint32_t)std::atoi(args[++i]);
            else if (!std::strcmp(args[i], "--no-vsync")) spec.Window.VSync = false;
            else projectArg = args[i];
        }

        std::filesystem::path project = FindProject(projectArg);
        if (project.empty())
        {
            ZM_CRITICAL("Loyiha topilmadi. Foydalanish: ZaminPlayer <Game.zproject>");
            std::exit(1);
        }
        return new ZaminPlayer(spec, project, scene);
    }

}
