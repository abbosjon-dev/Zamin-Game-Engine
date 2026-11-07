// Zamin Player: runs an exported / development game project fullscreen-ready. Its window is made as
// the project's player settings say (size, fullscreen / borderless, vsync, title, icon, frame rate
// limit, running in the background); the command line wins over them.
//   ZaminPlayer [path/to/Game.zproject] [--scene Scenes/Level.zscene | <build index> | <build scene name>]
//               [--fullscreen | --windowed] [--width W --height H] [--no-vsync] [--quality <level name | index>]
//               [--no-debug-draw] [--debug-raycasts] [--debug-contacts] [--debug-contact-details] [--debug-joints]
//               [--debug-velocities]
#include <Zamin/Zamin.h>
#include <Zamin/Core/EntryPoint.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace Zamin {

    // --fullscreen / --windowed / --no-vsync were given: they win over the display options the game saved.
    static bool s_DisplayFromCommandLine = false;
    // --quality <name | index>: wins over the level the game saved (Quality::SaveSettings) and the project's default.
    static std::string s_QualityFromCommandLine;

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
            const PlayerSettings& player = config.Player;
            Window& window = Application::Get().GetWindow();
            window.SetTitle(player.GetWindowTitle(config.Name));
            ZM_INFO("{} {}{}", player.GetProductName(config.Name), player.Version,
                    player.CompanyName.empty() ? std::string() : std::format(" ({})", player.CompanyName));
            // The game's icon (title bar, task bar).
            if (!player.Icon.empty())
            {
                ImageData icon;
                std::string error;
                if (GameIcon::Load(Project::GetIconPath(), icon, &error))
                    window.SetIcon(GameIcon::MakeWindowImages(icon));
                else
                    ZM_WARN("O'yin ikonkasi o'qilmadi ({}): {}", player.Icon.generic_string(), error);
            }
            Application::Get().SetTargetFrameRate(player.TargetFrameRate);
            Application::Get().SetRunInBackground(player.RunInBackground);
            if (player.TargetFrameRate > 0)
                ZM_INFO("Kadr chastotasi cheklovi: {} FPS", player.TargetFrameRate);
            // The player's own audio settings (the game's options menu saved them) over the project's mixer.
            if (Audio::LoadSettings())
                ZM_INFO("Saqlangan ovoz sozlamalari yuklandi");
            // The game's display options change this window (fullscreen, vsync); saved ones come back.
            Display::BindWindow(&Application::Get().GetWindow());
            if (!s_DisplayFromCommandLine && Display::LoadSettings())
                ZM_INFO("Saqlangan ekran sozlamalari yuklandi (to'liq ekran: {}, VSync: {})", Display::IsFullscreen() ? "ha" : "yo'q",
                        Display::IsVSync() ? "ha" : "yo'q");

            // Quality level: --quality, else the one the player chose in the game (saved), else the project's default.
            if (!s_QualityFromCommandLine.empty())
            {
                if (!Quality::SetLevelFromString(s_QualityFromCommandLine))
                {
                    std::string names;
                    for (const std::string& name : Quality::GetLevelNames())
                        names += (names.empty() ? "" : ", ") + name;
                    ZM_WARN("--quality {}: bunday sifat darajasi yo'q (bor: {}) - standart daraja qoldi", s_QualityFromCommandLine, names);
                }
            }
            else if (Quality::LoadSettings())
                ZM_INFO("Saqlangan sifat darajasi yuklandi");
            ZM_INFO("Sifat darajasi: {}", Quality::GetLevelName());

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

            // The start scene: --scene (a path, a build index or a build scene's name), else the
            // project's start scene, else (it is not there / not set) the first build scene.
            std::filesystem::path start = m_SceneOverride.empty() ? config.StartScene : std::filesystem::path(m_SceneOverride);
            if (m_SceneOverride.empty())
                if (std::filesystem::path resolved = Project::ResolveStartScene(config, Project::GetAssetDirectory());
                    !resolved.empty() && resolved != config.StartScene)
                {
                    if (!config.StartScene.empty())
                        ZM_WARN("Boshlang'ich sahna topilmadi ({}) - birinchi build sahnasi ochiladi: {}", config.StartScene.generic_string(),
                                resolved.generic_string());
                    start = resolved;
                }
            int buildIndex = -1;
            if (const std::string& arg = m_SceneOverride; !arg.empty() &&
                std::from_chars(arg.data(), arg.data() + arg.size(), buildIndex).ptr == arg.data() + arg.size())
            {
                start = Project::GetBuildScenePath(buildIndex);
                if (start.empty())
                    ZM_ERROR("--scene {}: build ro'yxatida bunday indeks yo'q ({} ta sahna bor)", buildIndex, SceneManager::GetBuildSceneCount());
            }
            else if (!m_SceneOverride.empty() && !start.has_extension())
                if (std::filesystem::path named = Project::FindBuildScene(m_SceneOverride); !named.empty())
                    start = named;
            if (start.empty() && m_SceneOverride.empty())
                start = Project::GetBuildScenePath(0);
            std::filesystem::path scenePath = start.empty() ? std::filesystem::path() : Project::GetAssetFileSystemPath(start);
            // Every scene of the game runs in the scene manager: additive scenes, DontDestroyOnLoad,
            // background loading and fades between scenes.
            m_Scenes = std::make_unique<SceneManager>();
            m_Scenes->OnViewportResize(window.GetFramebufferWidth(), window.GetFramebufferHeight());
            std::string error;
            if (!m_Scenes->Start(scenePath, &error))
            {
                ZM_CRITICAL("Boshlang'ich sahna yuklanmadi: {} ({})", scenePath.string(), error);
                // An error screen instead of closing at once: what went wrong, Esc to quit.
                std::vector<SceneLoadReport> reports = m_Scenes->TakeLoadReports();
                if (!reports.empty())
                    m_ErrorReport = reports.back();
                else
                {
                    m_ErrorReport.File = scenePath.generic_string();
                    SceneLoadIssue issue;
                    issue.Level = SceneLoadIssue::Severity::Fatal;
                    issue.Message = error.empty() ? std::string("sahna yuklanmadi") : error;
                    m_ErrorReport.Add(std::move(issue));
                }
                m_ErrorScreen = true;
                m_Scenes.reset();
                return;
            }
            m_ReportSerial = m_Scenes->GetLoadReportSerial();
            m_ActiveSerial = m_Scenes->GetActiveSceneSerial();

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
                for (const SceneLoadTiming& load : SceneLoadProfiler::GetHistory())
                    ZM_INFO("Sahna yuklash: {}", load.Format());
            }
            if (m_Scenes)
            {
                m_Scenes->Stop();
                m_Scenes.reset();
            }
            ScriptEngine::UnloadModule();
            Display::BindWindow(nullptr);
        }

        // The game's first scene could not be loaded: what went wrong, instead of a closed window.
        void RenderErrorScreen()
        {
            if (Input::IsKeyJustPressed(Key::Escape) || Input::IsKeyJustPressed(Key::Enter))
            {
                Application::Get().Close();
                return;
            }
            auto& window = Application::Get().GetWindow();
            const uint32_t w = window.GetFramebufferWidth(), h = window.GetFramebufferHeight();
            if (w == 0 || h == 0)
                return;
            RenderCommand::SetViewport(0, 0, w, h);
            RenderCommand::SetClearColor({ 0.055f, 0.06f, 0.075f, 1.0f });
            RenderCommand::Clear();
            RenderCommand::SetDepthTest(false);
            const float scale = std::clamp((float)h / 720.0f, 0.7f, 2.5f);
            Renderer2D::SetDisplayOutput(true);
            Renderer2D::SetLit(false);
            Renderer2D::BeginScene(glm::ortho(0.0f, (float)w, 0.0f, (float)h, -1.0f, 1.0f));
            const Ref<Font> font = Font::GetDefault();
            const float left = 72.0f * scale, right = (float)w - 48.0f * scale;
            float y = (float)h - 96.0f * scale;
            // A red mark down the left, then the lines of text from the top.
            Renderer2D::DrawQuad(glm::vec2(40.0f * scale, (float)h * 0.5f), glm::vec2(8.0f * scale, (float)h - 120.0f * scale),
                                 glm::vec4(0.86f, 0.27f, 0.25f, 1.0f));
            auto line = [&](std::string text, float size, const glm::vec4& color) {
                if (!font || y < 60.0f * scale)
                    return;
                // Cut what does not fit the width.
                const float maxWidth = (right - left) / size;
                if (Renderer2D::MeasureString(text, font).x > maxWidth)
                {
                    while (text.size() > 4 && Renderer2D::MeasureString(text + "...", font).x > maxWidth)
                        text.pop_back();
                    text += "...";
                }
                glm::mat4 transform = glm::translate(glm::mat4(1.0f), glm::vec3(left, y, 0.0f)) * glm::scale(glm::mat4(1.0f), glm::vec3(size));
                Renderer2D::TextParams params;
                params.Color = color;
                Renderer2D::DrawString(text, font, transform, params);
                y -= size * 1.55f;
            };
            const glm::vec4 white(0.93f, 0.94f, 0.96f, 1.0f), grey(0.60f, 0.63f, 0.68f, 1.0f), red(0.95f, 0.45f, 0.42f, 1.0f);
            line("O'yinning sahnasi yuklanmadi", 36.0f * scale, white);
            line(m_ErrorReport.File.empty() ? std::string("(sahna ko'rsatilmagan)") : m_ErrorReport.File, 17.0f * scale, grey);
            y -= 14.0f * scale;
            size_t shown = 0;
            for (const SceneLoadIssue& issue : m_ErrorReport.Issues)
            {
                if (shown++ == 10)
                {
                    line(std::format("... va yana {} ta", m_ErrorReport.Issues.size() - 10), 18.0f * scale, grey);
                    break;
                }
                line(issue.Message, 20.0f * scale, issue.Level == SceneLoadIssue::Severity::Warning ? white : red);
                if (!issue.SourceLine.empty())
                    line("    " + issue.SourceLine, 17.0f * scale, grey);
            }
            y = 70.0f * scale;
            line("Esc - chiqish.  To'liq ma'lumot konsolda (log).", 17.0f * scale, grey);
            Renderer2D::EndScene();
            Renderer2D::SetDisplayOutput(false);
        }

        // A scene the game switched to that could not be loaded (the game stays where it was): a
        // strip over the top of the picture for a few seconds.
        void RenderLoadBanner(uint32_t w, uint32_t h)
        {
            if (m_BannerTime <= 0.0f || m_BannerText.empty() || w == 0 || h == 0)
                return;
            const float alpha = std::clamp(m_BannerTime, 0.0f, 1.0f);
            const float scale = std::clamp((float)h / 720.0f, 0.7f, 2.5f);
            const float height = 44.0f * scale;
            RenderCommand::SetDepthTest(false);
            Renderer2D::SetDisplayOutput(true);
            Renderer2D::SetLit(false);
            Renderer2D::BeginScene(glm::ortho(0.0f, (float)w, 0.0f, (float)h, -1.0f, 1.0f));
            Renderer2D::DrawQuad(glm::vec2((float)w * 0.5f, (float)h - height * 0.5f), glm::vec2((float)w, height),
                                 glm::vec4(0.45f, 0.08f, 0.08f, 0.88f * alpha));
            if (Ref<Font> font = Font::GetDefault())
            {
                const float size = 18.0f * scale;
                glm::mat4 transform = glm::translate(glm::mat4(1.0f), glm::vec3(20.0f * scale, (float)h - height * 0.5f - size * 0.3f, 0.0f)) *
                                      glm::scale(glm::mat4(1.0f), glm::vec3(size));
                Renderer2D::TextParams params;
                params.Color = glm::vec4(1.0f, 0.93f, 0.92f, alpha);
                Renderer2D::DrawString(m_BannerText, font, transform, params);
            }
            Renderer2D::EndScene();
            Renderer2D::SetDisplayOutput(false);
        }

        void OnUpdate(Timestep ts) override
        {
            if (m_ErrorScreen)
            {
                RenderErrorScreen();
                return;
            }
            if (!m_Scenes) return;
            if (!m_ScriptedInput.empty())
                PlayScriptedInput();
            // The audio engine is shut down before OnDetach: its profile is kept from the last frame.
            static const bool logStats = std::getenv("ZAMIN_PLAYER_STATS") != nullptr;
            if (logStats)
                m_LastAudioProfile = AudioEngine::GetProfile().Format();
            auto& window = Application::Get().GetWindow();
            uint32_t w = window.GetFramebufferWidth(), h = window.GetFramebufferHeight();
            m_Scenes->OnViewportResize(w, h);

            Renderer::ResetStats();
            glm::vec2 mouse = m_HasScriptedMouse ? m_ScriptedMouse : Input::GetMousePosition();
            float scale = window.GetWidth() > 0 ? (float)w / (float)window.GetWidth() : 1.0f;
            m_Scenes->SetViewportMouse(mouse * scale, Input::IsMouseButtonPressed(MouseButton::Left) || m_ScriptedClick || m_ScriptedHold);
            m_Scenes->SetViewportScroll(Input::GetMouseScrollDelta() + m_ScriptedScroll);

            // The scene whose camera draws the game: its picture settings (render size, post-processing).
            Scene* view = m_Scenes->GetRenderScene();
            const auto& post = view->GetSettings().Post;
            // The scene is rendered in linear HDR at the render scale of the window's size, then
            // tonemapped (and scaled) to the screen.
            // (A pixel-perfect camera: the art's resolution, blown up with sharp pixels.)
            const glm::uvec2 renderSize = view->GetRuntimeRenderResolution(w, h);
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

            glm::vec4 clear = view->GetClearColor();
            RenderCommand::SetClearColor({ std::pow(clear.r, 2.2f), std::pow(clear.g, 2.2f), std::pow(clear.b, 2.2f), clear.a });
            RenderCommand::Clear();
            m_Scenes->OnUpdate(ts);
            view = m_Scenes->GetRenderScene(); // its frame may have switched its camera off

            {
                m_Target->Unbind();
                RenderCommand::SetViewport(0, 0, w, h);
                PostProcessInput input;
                input.ColorTexture = m_Target->GetColorAttachmentRendererID(0);
                input.Width = m_Target->GetSpecification().Width;
                input.Height = m_Target->GetSpecification().Height;
                input.DepthTexture = m_Target->GetDepthAttachmentRendererID();
                input.Projection = view->GetLastProjection();
                input.OutputWidth = w;
                input.OutputHeight = h;
                input.DeltaTime = ts;
                input.History = &m_PostHistory;
                if (PixelPerfectView pixel; view->GetPixelPerfectView(pixel) && pixel.Upscale &&
                                            pixel.RenderSize == glm::uvec2(input.Width, input.Height))
                {
                    input.PixelZoom = pixel.Zoom;
                    input.PixelOffset = PixelPerfect::GetUpscaleOffset(pixel);
                }
                PostProcessor::Apply(input, post, nullptr);
                // A pixel-perfect game's UI: over the finished picture, at the screen's resolution.
                m_Scenes->RenderDeferredUI();

                // Debug drawing over the finished picture, depth tested against the scene.
                if (view->GetPrimaryCameraEntity())
                {
                    DebugDrawView debugView;
                    debugView.View = view->GetLastView();
                    debugView.Projection = view->GetLastProjection();
                    debugView.Width = w;
                    debugView.Height = h;
                    debugView.DepthTexture = m_Target->GetDepthAttachmentRendererID();
                    debugView.DepthWidth = m_Target->GetSpecification().Width;
                    debugView.DepthHeight = m_Target->GetSpecification().Height;
                    DebugDraw::Render(debugView);
                }
                // A fade between scenes covers everything.
                m_Scenes->RenderTransition(w, h);
                RenderLoadBanner(w, h);
                m_BannerTime -= ts;
            }

            if (m_Scenes->IsQuitRequested())
            {
                ZM_INFO("O'yin chiqishni so'radi");
                m_Scenes->ClearQuitRequest();
                Application::Get().Close();
                return;
            }
            // Scene switches asked for in this frame (LoadScene, async activations, DontDestroyOnLoad).
            m_Scenes->ProcessRequests();
            if (m_Scenes->GetLoadReportSerial() != m_ReportSerial)
            {
                // (They are logged where they happen; only what broke a switch is shown.)
                m_ReportSerial = m_Scenes->GetLoadReportSerial();
                for (const SceneLoadReport& report : m_Scenes->TakeLoadReports())
                    if (report.Failed() || report.Count(SceneLoadIssue::Severity::Error) > 0)
                    {
                        const std::string name = std::filesystem::path(report.File).filename().string();
                        m_BannerText = report.Failed() ? std::format("Sahnaga o'tilmadi: {} - {}", name, report.Summary())
                                                       : std::format("'{}' sahnasi qisman yuklandi: {}", name, report.Summary());
                        m_BannerTime = 6.0f;
                    }
            }
            if (m_Scenes->GetActiveSceneSerial() != m_ActiveSerial)
            {
                m_ActiveSerial = m_Scenes->GetActiveSceneSerial();
                m_PostHistory.Reset(); // the new level adapts at once (not from the last one's brightness)
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
        std::unique_ptr<SceneManager> m_Scenes;
        uint64_t m_ActiveSerial = 0;
        // Load problems: the error screen (the first scene failed), the strip over a running game.
        bool m_ErrorScreen = false;
        SceneLoadReport m_ErrorReport;
        uint64_t m_ReportSerial = 0;
        std::string m_BannerText;
        float m_BannerTime = 0.0f;
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

        const char* projectArg = nullptr;
        std::string scene;
        RuntimeLayer::DebugDrawFlags debugDraw;
        bool fullscreen = false, windowed = false, noVSync = false;
        int width = 0, height = 0;
        for (int i = 1; i < args.Count; i++)
        {
            if (!std::strcmp(args[i], "--fullscreen")) fullscreen = true;
            else if (!std::strcmp(args[i], "--windowed")) windowed = true;
            else if (!std::strcmp(args[i], "--scene") && i + 1 < args.Count) scene = args[++i];
            else if (!std::strcmp(args[i], "--width") && i + 1 < args.Count) width = std::atoi(args[++i]);
            else if (!std::strcmp(args[i], "--height") && i + 1 < args.Count) height = std::atoi(args[++i]);
            else if (!std::strcmp(args[i], "--no-vsync")) noVSync = true;
            else if (!std::strcmp(args[i], "--quality") && i + 1 < args.Count) s_QualityFromCommandLine = args[++i];
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

        // The window as the project's player settings say (read before the engine starts: the window
        // is made with them); a file that cannot be read gets the defaults here and fails to load later.
        ProjectConfig config;
        std::string error;
        if (!ProjectSerializer::ConfigFromString(FileSystem::ReadTextFile(project), config, nullptr, &error))
            config = ProjectConfig();
        spec.Window = config.Player.MakeWindowProps(config.Name);
        // The command line wins.
        if (width > 0) spec.Window.Width = (uint32_t)width;
        if (height > 0) spec.Window.Height = (uint32_t)height;
        if (fullscreen)
            spec.Window.Mode = spec.Window.FullscreenMode;
        else if (windowed)
            spec.Window.Mode = WindowMode::Windowed;
        if (noVSync)
            spec.Window.VSync = false;
        s_DisplayFromCommandLine = fullscreen || windowed || noVSync;
        return new ZaminPlayer(spec, project, scene, debugDraw);
    }

}
