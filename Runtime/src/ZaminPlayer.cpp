// Zamin Player: runs an exported / development game project fullscreen-ready.
//   ZaminPlayer [path/to/Game.zproject] [--scene Scenes/Level.zscene] [--fullscreen] [--width W --height H]
//               [--no-debug-draw] [--debug-raycasts] [--debug-contacts] [--debug-contact-details] [--debug-joints]
//               [--debug-velocities]
#include <Zamin/Zamin.h>
#include <Zamin/Core/EntryPoint.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <utility>
#include <vector>

namespace Zamin {

    // --fullscreen / --no-vsync were given: they win over the display options the game saved.
    static bool s_DisplayFromCommandLine = false;

    class RuntimeLayer : public Layer
    {
    public:
        // Command line overrides of the project's debug drawing settings.
        struct DebugDrawFlags
        {
            bool Disable = false, Raycasts = false, Contacts = false, ContactDetails = false, Joints = false, Velocities = false;
        };

        RuntimeLayer(std::filesystem::path project, std::string scene, DebugDrawFlags debugDraw)
            : Layer("RuntimeLayer"), m_ProjectPath(std::move(project)), m_SceneOverride(std::move(scene)), m_DebugDrawFlags(debugDraw) {}

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
            // The player's own audio settings (the game's options menu saved them) over the project's mixer.
            if (Audio::LoadSettings())
                ZM_INFO("Saqlangan ovoz sozlamalari yuklandi");
            // The game's display options change this window (fullscreen, vsync); saved ones come back.
            Display::BindWindow(&Application::Get().GetWindow());
            if (!s_DisplayFromCommandLine && Display::LoadSettings())
                ZM_INFO("Saqlangan ekran sozlamalari yuklandi (to'liq ekran: {}, VSync: {})", Display::IsFullscreen() ? "ha" : "yo'q",
                        Display::IsVSync() ? "ha" : "yo'q");

            // Debug drawing: the project's settings, then the command line (Shipping: compiled out).
            DebugDrawSettings& debugDraw = DebugDraw::GetSettings();
            debugDraw = config.DebugDraw;
            if (m_DebugDrawFlags.Disable) debugDraw.Enabled = false;
            if (m_DebugDrawFlags.Raycasts) debugDraw.ShowRaycasts = true;
            if (m_DebugDrawFlags.Contacts) debugDraw.ShowContacts = true;
            if (m_DebugDrawFlags.ContactDetails) debugDraw.ShowContacts = debugDraw.ShowContactDetails = true;
            if (m_DebugDrawFlags.Joints) debugDraw.ShowJoints = true;
            if (m_DebugDrawFlags.Velocities) debugDraw.ShowVelocities = debugDraw.ShowBodyVelocities = true;

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

            // Automation (comparisons, CI): ZAMIN_PLAYER_NO_CULLING=1 / ZAMIN_PLAYER_NO_INSTANCING=1 turn the
            // renderer's frustum culling / GPU instancing off; ZAMIN_PLAYER_STATS=1 logs the last frame's
            // render statistics (and the audio profile) at exit.
            if (std::getenv("ZAMIN_PLAYER_NO_CULLING"))
                Renderer3D::SetFrustumCulling(false);
            if (std::getenv("ZAMIN_PLAYER_NO_INSTANCING"))
                Renderer3D::SetInstancing(false);
            // ZAMIN_PLAYER_INPUT="<frame>:<action>;..." plays input in (screenshots, CI): a key name
            // ("Down", "Enter", "Tab": pressed in that frame, released in the next), "mouse=x,y" (the
            // game's mouse at window px from the top-left from then on), "click" (the left button for
            // one frame), "down" / "up" (the left button held from then on / let go: drags) or
            // "scroll=dy" / "scroll=dx,dy" (the mouse wheel in that frame, notches).
            if (const char* input = std::getenv("ZAMIN_PLAYER_INPUT"))
            {
                std::string_view rest = input;
                while (!rest.empty())
                {
                    const size_t end = rest.find(';');
                    std::string_view item = rest.substr(0, end);
                    rest = end == std::string_view::npos ? std::string_view() : rest.substr(end + 1);
                    const size_t colon = item.find(':');
                    if (colon == std::string_view::npos)
                        continue;
                    m_ScriptedInput.push_back({ std::atoi(std::string(item.substr(0, colon)).c_str()), std::string(item.substr(colon + 1)) });
                }
            }
        }

        // Plays the scripted input of this frame (seen by the next Input::Update).
        void PlayScriptedInput()
        {
            for (Key key : m_ReleaseKeys)
                Input::OnKeyEvent(key, false);
            m_ReleaseKeys.clear();
            m_ScriptedClick = false;
            m_ScriptedScroll = glm::vec2(0.0f);
            for (const auto& [frame, action] : m_ScriptedInput)
            {
                if (frame != m_Frame)
                    continue;
                Key key{};
                if (action.starts_with("mouse="))
                {
                    float x = 0.0f, y = 0.0f;
                    if (std::sscanf(action.c_str() + 6, "%f,%f", &x, &y) == 2)
                    {
                        m_ScriptedMouse = { x, y };
                        m_HasScriptedMouse = true;
                    }
                }
                else if (action == "click")
                    m_ScriptedClick = true;
                else if (action == "down" || action == "up")
                    m_ScriptedHold = action == "down";
                else if (action.starts_with("scroll="))
                {
                    float x = 0.0f, y = 0.0f;
                    if (std::sscanf(action.c_str() + 7, "%f,%f", &x, &y) == 2)
                        m_ScriptedScroll = { x, y };
                    else
                        m_ScriptedScroll = { 0.0f, (float)std::atof(action.c_str() + 7) };
                }
                else if (KeyFromString(action, key))
                {
                    Input::OnKeyEvent(key, true);
                    m_ReleaseKeys.push_back(key);
                }
                else
                    ZM_WARN("ZAMIN_PLAYER_INPUT: noma'lum amal '{}'", action);
            }
            m_Frame++;
        }

        void OnDetach() override
        {
            if (std::getenv("ZAMIN_PLAYER_STATS"))
            {
                ZM_INFO("Render statistikasi (oxirgi kadr): {}", Renderer::GetStats().Format());
                ZM_INFO("Profiler (oxirgi kadr, ms):\n{}", Profiler::FormatTree(Profiler::GetLastFrame()));
                ZM_INFO("Audio profili (oxirgi kadr): {}", m_LastAudioProfile);
            }
            if (m_Scene)
            {
                m_Scene->OnRuntimeStop();
                m_Scene.reset();
            }
            ScriptEngine::UnloadModule();
            Display::BindWindow(nullptr);
        }

        void OnUpdate(Timestep ts) override
        {
            if (!m_Scene) return;
            if (!m_ScriptedInput.empty())
                PlayScriptedInput();
            // The audio engine is shut down before OnDetach: its profile is kept from the last frame.
            static const bool logStats = std::getenv("ZAMIN_PLAYER_STATS") != nullptr;
            if (logStats)
                m_LastAudioProfile = AudioEngine::GetProfile().Format();
            auto& window = Application::Get().GetWindow();
            uint32_t w = window.GetFramebufferWidth(), h = window.GetFramebufferHeight();
            m_Scene->OnViewportResize(w, h);

            Renderer::ResetStats();
            glm::vec2 mouse = m_HasScriptedMouse ? m_ScriptedMouse : Input::GetMousePosition();
            float scale = window.GetWidth() > 0 ? (float)w / (float)window.GetWidth() : 1.0f;
            m_Scene->SetViewportMouse(mouse * scale, Input::IsMouseButtonPressed(MouseButton::Left) || m_ScriptedClick || m_ScriptedHold);
            m_Scene->SetViewportScroll(Input::GetMouseScrollDelta() + m_ScriptedScroll);

            const auto& post = m_Scene->GetSettings().Post;
            // The scene is rendered in linear HDR at the render scale of the window's size, then
            // tonemapped (and scaled) to the screen.
            // (A pixel-perfect camera: the art's resolution, blown up with sharp pixels.)
            const glm::uvec2 renderSize = m_Scene->GetRuntimeRenderResolution(w, h);
            if (!m_Target)
            {
                FramebufferSpecification spec;
                spec.Width = renderSize.x; spec.Height = renderSize.y;
                spec.Attachments = { FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::Depth };
                m_Target = Framebuffer::Create(spec);
            }
            else if (m_Target->GetSpecification().Width != renderSize.x || m_Target->GetSpecification().Height != renderSize.y)
                m_Target->Resize(renderSize.x, renderSize.y);
            m_Target->Bind();

            glm::vec4 clear = m_Scene->GetClearColor();
            RenderCommand::SetClearColor({ std::pow(clear.r, 2.2f), std::pow(clear.g, 2.2f), std::pow(clear.b, 2.2f), clear.a });
            RenderCommand::Clear();
            m_Scene->OnUpdateRuntime(ts);

            {
                m_Target->Unbind();
                RenderCommand::SetViewport(0, 0, w, h);
                PostProcessInput input;
                input.ColorTexture = m_Target->GetColorAttachmentRendererID(0);
                input.Width = m_Target->GetSpecification().Width;
                input.Height = m_Target->GetSpecification().Height;
                input.DepthTexture = m_Target->GetDepthAttachmentRendererID();
                input.Projection = m_Scene->GetLastProjection();
                input.OutputWidth = w;
                input.OutputHeight = h;
                input.DeltaTime = ts;
                input.History = &m_PostHistory;
                if (PixelPerfectView pixel; m_Scene->GetPixelPerfectView(pixel) && pixel.Upscale &&
                                            pixel.RenderSize == glm::uvec2(input.Width, input.Height))
                {
                    input.PixelZoom = pixel.Zoom;
                    input.PixelOffset = PixelPerfect::GetUpscaleOffset(pixel);
                }
                PostProcessor::Apply(input, post, nullptr);
                // A pixel-perfect game's UI: over the finished picture, at the screen's resolution.
                if (m_Scene->HasDeferredUI())
                    m_Scene->RenderDeferredUI();

                // Debug drawing over the finished picture, depth tested against the scene.
                if (m_Scene->GetPrimaryCameraEntity())
                {
                    DebugDrawView view;
                    view.View = m_Scene->GetLastView();
                    view.Projection = m_Scene->GetLastProjection();
                    view.Width = w;
                    view.Height = h;
                    view.DepthTexture = m_Target->GetDepthAttachmentRendererID();
                    view.DepthWidth = m_Target->GetSpecification().Width;
                    view.DepthHeight = m_Target->GetSpecification().Height;
                    DebugDraw::Render(view);
                }
            }

            if (m_Scene->IsQuitRequested())
            {
                ZM_INFO("O'yin chiqishni so'radi");
                m_Scene->ClearQuitRequest();
                Application::Get().Close();
                return;
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
                    m_PostHistory.Reset(); // the new level adapts at once (not from the last one's brightness)
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
                    ZM_INFO("FPS: {:.0f} | audio: {}", Application::Get().GetFPS(), AudioEngine::GetProfile().Format());
                return false;
            });
        }

    private:
        std::filesystem::path m_ProjectPath;
        std::string m_SceneOverride;
        DebugDrawFlags m_DebugDrawFlags;
        std::string m_LastAudioProfile = "-";
        Ref<Scene> m_Scene;
        Ref<Framebuffer> m_Target;
        PostProcessHistory m_PostHistory; // auto exposure: the screen's adapted brightness
        // ZAMIN_PLAYER_INPUT
        std::vector<std::pair<int, std::string>> m_ScriptedInput;
        std::vector<Key> m_ReleaseKeys;
        glm::vec2 m_ScriptedMouse{ 0.0f };
        bool m_HasScriptedMouse = false, m_ScriptedClick = false, m_ScriptedHold = false;
        glm::vec2 m_ScriptedScroll{ 0.0f };
        int m_Frame = 0;
    };

    class ZaminPlayer : public Application
    {
    public:
        ZaminPlayer(const ApplicationSpecification& spec, std::filesystem::path project, std::string scene, RuntimeLayer::DebugDrawFlags debugDraw)
            : Application(spec)
        {
            PushLayer(new RuntimeLayer(std::move(project), std::move(scene), debugDraw));
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
        RuntimeLayer::DebugDrawFlags debugDraw;
        for (int i = 1; i < args.Count; i++)
        {
            if (!std::strcmp(args[i], "--fullscreen")) spec.Window.Fullscreen = s_DisplayFromCommandLine = true;
            else if (!std::strcmp(args[i], "--scene") && i + 1 < args.Count) scene = args[++i];
            else if (!std::strcmp(args[i], "--width") && i + 1 < args.Count) spec.Window.Width = (uint32_t)std::atoi(args[++i]);
            else if (!std::strcmp(args[i], "--height") && i + 1 < args.Count) spec.Window.Height = (uint32_t)std::atoi(args[++i]);
            else if (!std::strcmp(args[i], "--no-vsync"))
            {
                spec.Window.VSync = false;
                s_DisplayFromCommandLine = true;
            }
            else if (!std::strcmp(args[i], "--no-debug-draw")) debugDraw.Disable = true;
            else if (!std::strcmp(args[i], "--debug-raycasts")) debugDraw.Raycasts = true;
            else if (!std::strcmp(args[i], "--debug-contacts")) debugDraw.Contacts = true;
            else if (!std::strcmp(args[i], "--debug-contact-details")) debugDraw.ContactDetails = true;
            else if (!std::strcmp(args[i], "--debug-joints")) debugDraw.Joints = true;
            else if (!std::strcmp(args[i], "--debug-velocities")) debugDraw.Velocities = true;
            else projectArg = args[i];
        }

        std::filesystem::path project = FindProject(projectArg);
        if (project.empty())
        {
            ZM_CRITICAL("Loyiha topilmadi. Foydalanish: ZaminPlayer <Game.zproject>");
            std::exit(1);
        }
        return new ZaminPlayer(spec, project, scene, debugDraw);
    }

}
