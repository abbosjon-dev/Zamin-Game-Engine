// Zamin Player: runs an exported / development game project. Its window is made as the project's
// player settings say (size, fullscreen / borderless, vsync, title, icon, frame rate limit, running in
// the background); what the player saved in the game's options menu wins over them, and the command
// line over both (PlayerLaunchSettings::Resolve). A splash screen (the Zamin logo or the game's picture)
// shows while the first scene loads in the background; F11 / Alt+Enter switch fullscreen.
//   ZaminPlayer [path/to/Game.zproject] [--windowed | --fullscreen | --borderless] [--resolution WxH]
//               [--monitor N] [--vsync | --no-vsync] [--fps N] [--scene <path | build index | name>]
//               [--quality <level>] [--lang <code>] [--no-splash] [--gl ...] [--msaa N] [--debug-...]
//               [--log-file <path> | --no-log-file] [--no-crash-handler] [--no-crash-dialog]
//   ZaminPlayer --help lists every option.
// The log goes to ~/.local/share/<company>/<product>/logs (player-<date>.log, the last 5 kept, latest.log);
// a crash writes crash-<date>.txt there and the player shows it in a window of its own
// (ZaminPlayer --crash-dialog <report>, started by the crash handler).
#include <Zamin/Zamin.h>
#include <Zamin/Core/EntryPoint.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <charconv>
#include <csignal>
#include <ctime>
#include <stdexcept>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace Zamin {

    // How long the picture of the first scene fades in from the splash screen's colour.
    static constexpr float s_SplashCoverSeconds = 0.35f;

    class RuntimeLayer : public Layer
    {
    public:
        RuntimeLayer(std::filesystem::path project, PlayerCommandLine commandLine, PlayerLaunchSettings launch)
            : Layer("RuntimeLayer"), m_ProjectPath(std::move(project)), m_SceneOverride(commandLine.Scene), m_CommandLine(std::move(commandLine)),
              m_Launch(std::move(launch)) {}

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
            // The window was made with the resolved settings (the command line > the saved display options >
            // the project); the frame rate limit the same way.
            ZM_INFO("Oyna sozlamalari: {}", m_Launch.Describe());
            Application::Get().SetTargetFrameRate(m_Launch.TargetFrameRate);
            Application::Get().SetRunInBackground(player.RunInBackground);
            if (m_Launch.TargetFrameRate > 0)
                ZM_INFO("Kadr chastotasi cheklovi: {} FPS", m_Launch.TargetFrameRate);
            // The player's own audio settings (the game's options menu saved them) over the project's mixer.
            if (Audio::LoadSettings())
                ZM_INFO("Saqlangan ovoz sozlamalari yuklandi");
            // The game's display options change this window (fullscreen, vsync); the window already is as
            // they were saved (or as the command line says).
            Display::BindWindow(&Application::Get().GetWindow());
            m_FullscreenToggle = player.FullscreenToggle;
            m_HasToggleAction = Project::GetInputActionMap().FindAction("ToggleFullscreen") != nullptr;

            // Quality level: --quality, else the one the player chose in the game (saved), else the project's default.
            if (!m_CommandLine.Quality.empty())
            {
                if (!Quality::SetLevelFromString(m_CommandLine.Quality))
                {
                    std::string names;
                    for (const std::string& name : Quality::GetLevelNames())
                        names += (names.empty() ? "" : ", ") + name;
                    ZM_WARN("--quality {}: bunday sifat darajasi yo'q (bor: {}) - standart daraja qoldi", m_CommandLine.Quality, names);
                }
            }
            else if (Quality::LoadSettings())
                ZM_INFO("Saqlangan sifat darajasi yuklandi");
            ZM_INFO("Sifat darajasi: {}", Quality::GetLevelName());

            // Language: --lang, else the one the player chose in the game (saved), else the project's start language.
            if (!m_CommandLine.Language.empty())
            {
                if (!Localization::SetLanguage(m_CommandLine.Language))
                {
                    std::string codes;
                    for (const std::string& code : Localization::GetLanguages())
                        codes += (codes.empty() ? "" : ", ") + code;
                    ZM_WARN("--lang {}: o'yinda bunday til yo'q (bor: {}) - '{}' qoldi", m_CommandLine.Language, codes, Localization::GetLanguage());
                }
            }
            else if (Localization::LoadSettings())
                ZM_INFO("Saqlangan til yuklandi");
            if (Localization::GetLanguages().size() > 1 || Localization::GetTable(Localization::GetLanguage()))
                ZM_INFO("Til: {} ({})", Localization::GetLanguageName(Localization::GetLanguage()), Localization::GetLanguage());

            // Debug drawing: the project's settings, then the command line (Shipping: compiled out).
            DebugDrawSettings& debugDraw = DebugDraw::GetSettings();
            debugDraw = config.DebugDraw;
            if (m_CommandLine.NoDebugDraw) debugDraw.Enabled = false;
            if (m_CommandLine.DebugRaycasts) debugDraw.ShowRaycasts = true;
            if (m_CommandLine.DebugContacts) debugDraw.ShowContacts = true;
            if (m_CommandLine.DebugContactDetails) debugDraw.ShowContacts = debugDraw.ShowContactDetails = true;
            if (m_CommandLine.DebugJoints) debugDraw.ShowJoints = true;
            if (m_CommandLine.DebugVelocities) debugDraw.ShowVelocities = debugDraw.ShowBodyVelocities = true;

            if (auto module = Project::GetScriptModulePath(); !module.empty())
                ScriptEngine::LoadModule(module);

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
            m_StartScenePath = scenePath;
            // The splash screen while the first scene loads in the background (automation runs - screenshots,
            // CI - go straight to the game unless ZAMIN_PLAYER_SPLASH=1 asks for it).
            const bool splash = m_Launch.Splash && (!Application::IsAutomationRun() || std::getenv("ZAMIN_PLAYER_SPLASH"));
            if (splash)
            {
                m_StartLoad = m_Scenes->LoadSceneAsync(scenePath);
                if (!m_StartLoad)
                {
                    ShowStartError("fayl topilmadi");
                    return;
                }
                m_StartLoad->SetAllowSceneActivation(false); // it starts the game itself, after the splash
                const std::filesystem::path image = player.SplashImage.empty() ? std::filesystem::path()
                                                                               : Project::GetAssetFileSystemPath(player.SplashImage);
                m_Splash.Begin(player.SplashDuration, player.SplashSkippable, player.SplashBackground, image);
                ZM_INFO("Splash ekran ({:.1f} s{}); birinchi sahna fonda yuklanmoqda: {}", player.SplashDuration,
                        player.SplashSkippable ? ", tugma bosilsa o'tadi" : "", start.generic_string());
            }
            else
            {
                std::string error;
                if (!m_Scenes->Start(scenePath, &error))
                {
                    ShowStartError(error);
                    return;
                }
                OnGameStarted();
            }
        }

        // The first scene could not be loaded: an error screen instead of closing at once (what went
        // wrong, Esc to quit).
        void ShowStartError(const std::string& error)
        {
            ZM_CRITICAL("Boshlang'ich sahna yuklanmadi: {} ({})", m_StartScenePath.string(), error);
            std::vector<SceneLoadReport> reports = m_Scenes ? m_Scenes->TakeLoadReports() : std::vector<SceneLoadReport>();
            if (!reports.empty())
                m_ErrorReport = reports.back();
            else
            {
                m_ErrorReport.File = m_StartScenePath.generic_string();
                SceneLoadIssue issue;
                issue.Level = SceneLoadIssue::Severity::Fatal;
                issue.Message = error.empty() ? std::string("sahna yuklanmadi") : error;
                m_ErrorReport.Add(std::move(issue));
            }
            m_ErrorScreen = true;
            m_StartLoad.reset();
            m_Splash.End();
            m_Scenes.reset();
        }

        // The game runs from here on (the first scene started).
        void OnGameStarted()
        {
            m_ReportSerial = m_Scenes->GetLoadReportSerial();
            m_ActiveSerial = m_Scenes->GetActiveSceneSerial();
        }

        // One frame of the splash screen: the first scene loads meanwhile; once both are done the game starts.
        void UpdateSplash(Timestep ts, bool skip)
        {
            m_Scenes->UpdateAsyncLoads(12.0f); // nothing else to do in these frames
            const bool failed = m_StartLoad->HasFailed();
            m_Splash.Update(ts, m_StartLoad->IsReady() || failed, skip);
            auto& window = Application::Get().GetWindow();
            m_Splash.Render(window.GetFramebufferWidth(), window.GetFramebufferHeight(), m_StartLoad->GetProgress());
            if (!m_Splash.IsDone())
                return;
            const SplashTimeline timeline = m_Splash.GetTimeline();
            m_SplashColor = m_Splash.GetBackground();
            m_Splash.End();
            std::string error;
            if (failed || !m_Scenes->Start(m_StartLoad, &error))
            {
                ShowStartError(failed ? m_StartLoad->GetError() : error);
                return;
            }
            m_StartLoad.reset();
            OnGameStarted();
            m_SplashCover = 1.0f; // the game's first picture comes out of the splash's colour
            ZM_INFO("Splash ekran tugadi ({:.1f} s{}), o'yin boshlandi", timeline.GetElapsed(), timeline.WasSkipped() ? ", o'tkazib yuborildi" : "");
        }

        // F11 / Alt+Enter (and the input map's "ToggleFullscreen"): fullscreen <-> the window as it was. True
        // when it switched this frame.
        bool HandleFullscreenToggle()
        {
            if (!m_FullscreenToggle)
                return false;
            // (Alt held, or pressed in the same frame as Enter: a quick chord.)
            const bool alt = Input::IsKeyPressed(Key::LeftAlt) || Input::IsKeyPressed(Key::RightAlt) || Input::IsKeyJustPressed(Key::LeftAlt) ||
                             Input::IsKeyJustPressed(Key::RightAlt);
            bool toggle = Input::IsKeyJustPressed(Key::F11) || (alt && (Input::IsKeyJustPressed(Key::Enter) || Input::IsKeyJustPressed(Key::KPEnter)));
            if (!toggle && m_HasToggleAction)
                toggle = Input::IsActionJustPressed("ToggleFullscreen");
            if (!toggle)
                return false;
            Display::SetFullscreen(!Display::IsFullscreen());
            Display::SaveSettings(); // the next start opens the same way
            ZM_INFO("Ekran: {} (saqlandi)", Display::IsFullscreen() ? "to'liq ekran" : "oynali rejim");
            return true;
        }

        // The start of the game: its first frames fade in from the splash screen's colour.
        void RenderSplashCover(uint32_t w, uint32_t h, float dt)
        {
            if (m_SplashCover <= 0.0f || w == 0 || h == 0)
                return;
            const float t = std::clamp(m_SplashCover, 0.0f, 1.0f);
            RenderCommand::SetDepthTest(false);
            Renderer2D::SetDisplayOutput(true);
            Renderer2D::SetLit(false);
            Renderer2D::BeginScene(glm::ortho(0.0f, (float)w, 0.0f, (float)h, -1.0f, 1.0f));
            Renderer2D::DrawQuad(glm::vec2((float)w * 0.5f, (float)h * 0.5f), glm::vec2((float)w, (float)h),
                                 glm::vec4(m_SplashColor, t * t * (3.0f - 2.0f * t)));
            Renderer2D::EndScene();
            Renderer2D::SetDisplayOutput(false);
            m_SplashCover -= dt / s_SplashCoverSeconds;
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
                // The real frame times (frame pacing: --fps, vsync).
                const FrameTimeHistory& frames = Application::Get().GetFrameTimeHistory();
                const FrameTimeHistory::Summary summary = frames.Summarize();
                ZM_INFO("Kadr vaqti (oxirgi {} kadr): o'rtacha {:.2f} ms ({:.1f} FPS), min {:.2f} ms, max {:.2f} ms, eng sekin 1%: {:.2f} ms", frames.Size(),
                        summary.Average, summary.AverageFPS, summary.Min, summary.Max, summary.Worst1Percent);
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

        // ZAMIN_PLAYER_CRASH_TEST="<frame>:<segv | abort | fpe | exception>": the player crashes on purpose
        // in that frame (the crash handler, its report and dialog are checked with it).
        void RunCrashTest()
        {
            static const char* test = std::getenv("ZAMIN_PLAYER_CRASH_TEST");
            if (!test)
                return;
            static int frames = 0;
            const int at = std::atoi(test);
            const char* colon = std::strchr(test, ':');
            if (frames++ != at || !colon)
                return;
            const std::string_view kind = colon + 1;
            ZM_WARN("ZAMIN_PLAYER_CRASH_TEST: ataylab crash ({}) - {}-kadr", kind, at);
            if (kind == "segv")
                *(volatile int*)(uintptr_t)8 = 1; // a real invalid write
            else if (kind == "abort")
                std::abort();
            else if (kind == "fpe")
            {
                volatile int zero = 0;
                volatile int result = 1 / zero;
                (void)result;
                std::raise(SIGFPE); // where division by zero does not trap
            }
            else if (kind == "exception")
                throw std::runtime_error("ZAMIN_PLAYER_CRASH_TEST sinov istisnosi");
            ZM_WARN("ZAMIN_PLAYER_CRASH_TEST: noma'lum tur '{}' (segv, abort, fpe, exception)", kind);
        }

        void OnUpdate(Timestep ts) override
        {
            RunCrashTest();
            if (!m_ScriptedInput.empty())
                PlayScriptedInput();
            const bool toggled = HandleFullscreenToggle();
            if (m_ErrorScreen)
            {
                RenderErrorScreen();
                return;
            }
            if (!m_Scenes) return;
            if (m_Splash.IsActive())
            {
                UpdateSplash(ts, Input::IsAnyKeyJustPressed() && !toggled);
                return;
            }
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
                RenderSplashCover(w, h, ts);
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
                // (F11 switches fullscreen.)
                if (ev.GetKeyCode() == Key::F10)
                    ZM_INFO("FPS: {:.0f} | audio: {}", Application::Get().GetFPS(), AudioEngine::GetProfile().Format());
                return false;
            });
        }

    private:
        std::filesystem::path m_ProjectPath;
        std::string m_SceneOverride;
        PlayerCommandLine m_CommandLine;
        PlayerLaunchSettings m_Launch;
        std::filesystem::path m_StartScenePath;
        // The splash screen and the first scene loading behind it; then the cover the game fades in from.
        SplashScreen m_Splash;
        Ref<AsyncOperation> m_StartLoad;
        float m_SplashCover = 0.0f;
        glm::vec3 m_SplashColor{ 0.0f };
        bool m_FullscreenToggle = true, m_HasToggleAction = false;
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

    // ZaminPlayer --crash-dialog <report>: the window about a crash (the crash handler starts it after the
    // game crashed; the game itself has already exited). Says what happened and where the report is;
    // opens its folder, copies its path.
    class CrashDialogLayer : public Layer
    {
    public:
        explicit CrashDialogLayer(std::filesystem::path report) : Layer("CrashDialog"), m_Report(std::move(report)) {}

        void OnAttach() override
        {
            // The crashed program could not make the backtrace readable; this one can.
            CrashHandler::DemangleReport(m_Report);
            m_HasReport = CrashHandler::ReadReport(m_Report, m_Summary);
            const std::string product = !m_Summary.Product.empty() ? m_Summary.Product : !m_Summary.Application.empty() ? m_Summary.Application : "O'yin";
            Application::Get().GetWindow().SetTitle(std::format("{} - xato", product));
            m_Product = product;
            ZM_INFO("Crash hisoboti: {} ({})", m_Report.string(), m_HasReport ? m_Summary.Reason : "o'qilmadi");
        }

        void OnUpdate(Timestep) override
        {
            auto& window = Application::Get().GetWindow();
            const uint32_t w = window.GetFramebufferWidth(), h = window.GetFramebufferHeight();
            if (w == 0 || h == 0)
                return;
            const float px = window.GetWidth() > 0 ? (float)w / (float)window.GetWidth() : 1.0f; // window -> framebuffer pixels
            const float scale = std::clamp((float)h / 440.0f, 0.75f, 3.0f);
            const glm::vec2 mouse = Input::GetMousePosition() * px;
            const glm::vec2 mouseUp(mouse.x, (float)h - mouse.y); // bottom-up, like the drawing

            // The buttons along the bottom, right to left.
            struct Button { const char* Label; int Action; glm::vec2 Min, Max; };
            Button buttons[] = { { "Yopish", 0, {}, {} }, { "Yo'lni nusxalash", 1, {}, {} }, { "Papkani ochish", 2, {}, {} } };
            const Ref<Font> font = Font::GetDefault();
            const float buttonText = 17.0f * scale, buttonHeight = 38.0f * scale, gap = 12.0f * scale;
            float right = (float)w - 24.0f * scale;
            for (Button& b : buttons)
            {
                const float width = (font ? Renderer2D::MeasureString(b.Label, font).x * buttonText : 120.0f) + 36.0f * scale;
                b.Min = { right - width, 22.0f * scale };
                b.Max = { right, 22.0f * scale + buttonHeight };
                right -= width + gap;
            }
            int clicked = -1;
            if (Input::IsMouseButtonJustReleased(MouseButton::Left))
                for (const Button& b : buttons)
                    if (mouseUp.x >= b.Min.x && mouseUp.x <= b.Max.x && mouseUp.y >= b.Min.y && mouseUp.y <= b.Max.y)
                        clicked = b.Action;
            if (Input::IsKeyJustPressed(Key::Escape) || Input::IsKeyJustPressed(Key::Enter) || clicked == 0)
            {
                Application::Get().Close();
                return;
            }
            if (Input::IsKeyJustPressed(Key::C) || clicked == 1)
            {
                Input::SetClipboardText(m_Report.string());
                m_Notice = "Yo'l nusxalandi";
                m_NoticeTime = 2.5f;
            }
            if (Input::IsKeyJustPressed(Key::O) || clicked == 2)
                Platform::OpenInFileBrowser(m_Report.parent_path());

            RenderCommand::SetViewport(0, 0, w, h);
            RenderCommand::SetClearColor({ 0.07f, 0.075f, 0.09f, 1.0f });
            RenderCommand::Clear();
            RenderCommand::SetDepthTest(false);
            Renderer2D::SetDisplayOutput(true);
            Renderer2D::SetLit(false);
            Renderer2D::BeginScene(glm::ortho(0.0f, (float)w, 0.0f, (float)h, -1.0f, 1.0f));
            // A red strip down the left.
            Renderer2D::DrawQuad(glm::vec2(14.0f * scale, (float)h * 0.5f), glm::vec2(6.0f * scale, (float)h), glm::vec4(0.86f, 0.27f, 0.25f, 1.0f));
            const float left = 40.0f * scale, maxRight = (float)w - 28.0f * scale;
            float y = (float)h - 56.0f * scale;
            // (keepEnd: a long path loses its start - its end matters; other text loses its end.)
            auto line = [&](std::string text, float size, const glm::vec4& color, float after = 1.5f, bool keepEnd = false) {
                if (!font)
                    return;
                const float maxWidth = (maxRight - left) / size;
                if (Renderer2D::MeasureString(text, font).x > maxWidth)
                {
                    if (keepEnd)
                    {
                        while (text.size() > 4 && Renderer2D::MeasureString("..." + text, font).x > maxWidth)
                            text.erase(0, 1);
                        text = "..." + text;
                    }
                    else
                    {
                        while (text.size() > 4 && Renderer2D::MeasureString(text + "...", font).x > maxWidth)
                            text.pop_back();
                        text += "...";
                    }
                }
                Renderer2D::TextParams params;
                params.Color = color;
                Renderer2D::DrawString(text, font, glm::translate(glm::mat4(1.0f), glm::vec3(left, y, 0.0f)) * glm::scale(glm::mat4(1.0f), glm::vec3(size)),
                                       params);
                y -= size * after;
            };
            const glm::vec4 white(0.94f, 0.95f, 0.97f, 1.0f), grey(0.60f, 0.63f, 0.68f, 1.0f), red(0.96f, 0.50f, 0.46f, 1.0f), dim(0.45f, 0.48f, 0.53f, 1.0f);
            line(std::format("{} kutilmaganda to'xtadi", m_Product), 30.0f * scale, white, 1.7f);
            if (m_HasReport)
            {
                if (!m_Summary.Version.empty() || !m_Summary.Engine.empty())
                    line(std::format("{}{}{}", m_Summary.Version.empty() ? "" : "Versiya " + m_Summary.Version, !m_Summary.Version.empty() && !m_Summary.Engine.empty() ? "  |  " : "",
                                     m_Summary.Engine),
                         15.0f * scale, dim, 1.9f);
                line("Sabab: " + m_Summary.Reason, 18.0f * scale, red, 2.0f);
            }
            else
                line("Hisobot o'qilmadi.", 18.0f * scale, red, 2.0f);
            line("Xato haqida hisobot saqlandi:", 17.0f * scale, white, 1.45f);
            line(m_Report.string(), 16.0f * scale, grey, 2.0f, true);
            line("Iltimos, shu faylni o'yin muallifiga yuboring - u xatoni tuzatishga yordam beradi.", 15.0f * scale, grey, 1.9f);
            // The last things the game logged.
            if (m_HasReport && !m_Summary.LastLog.empty())
            {
                line("Oxirgi log satrlari:", 14.0f * scale, dim, 1.4f);
                const size_t first = m_Summary.LastLog.size() > 4 ? m_Summary.LastLog.size() - 4 : 0;
                for (size_t i = first; i < m_Summary.LastLog.size() && y > 80.0f * scale; i++)
                    line(m_Summary.LastLog[i], 13.0f * scale, dim, 1.35f);
            }
            // Buttons.
            for (const Button& b : buttons)
            {
                const bool hover = mouseUp.x >= b.Min.x && mouseUp.x <= b.Max.x && mouseUp.y >= b.Min.y && mouseUp.y <= b.Max.y;
                const glm::vec4 fill = b.Action == 0 ? (hover ? glm::vec4(0.32f, 0.52f, 0.92f, 1.0f) : glm::vec4(0.25f, 0.44f, 0.82f, 1.0f))
                                                     : (hover ? glm::vec4(0.24f, 0.26f, 0.31f, 1.0f) : glm::vec4(0.17f, 0.18f, 0.22f, 1.0f));
                Renderer2D::DrawQuad((b.Min + b.Max) * 0.5f, b.Max - b.Min, fill);
                if (font)
                {
                    const float width = Renderer2D::MeasureString(b.Label, font).x * buttonText;
                    Renderer2D::TextParams params;
                    params.Color = white;
                    Renderer2D::DrawString(b.Label, font,
                                           glm::translate(glm::mat4(1.0f), glm::vec3((b.Min.x + b.Max.x - width) * 0.5f, b.Min.y + buttonHeight * 0.32f, 0.0f)) *
                                               glm::scale(glm::mat4(1.0f), glm::vec3(buttonText)),
                                           params);
                }
            }
            if (m_NoticeTime > 0.0f && font)
            {
                Renderer2D::TextParams params;
                params.Color = glm::vec4(0.55f, 0.85f, 0.55f, std::clamp(m_NoticeTime, 0.0f, 1.0f));
                Renderer2D::DrawString(m_Notice, font,
                                       glm::translate(glm::mat4(1.0f), glm::vec3(left, 32.0f * scale, 0.0f)) * glm::scale(glm::mat4(1.0f), glm::vec3(15.0f * scale)),
                                       params);
                m_NoticeTime -= Application::Get().GetFrameTime() / 1000.0f;
            }
            Renderer2D::EndScene();
            Renderer2D::SetDisplayOutput(false);
        }

    private:
        std::filesystem::path m_Report;
        CrashReportSummary m_Summary;
        bool m_HasReport = false;
        std::string m_Product;
        std::string m_Notice;
        float m_NoticeTime = 0.0f;
    };

    class CrashDialogApp : public Application
    {
    public:
        CrashDialogApp(const ApplicationSpecification& spec, std::filesystem::path report) : Application(spec)
        {
            PushLayer(new CrashDialogLayer(std::move(report)));
        }
    };

    class ZaminPlayer : public Application
    {
    public:
        ZaminPlayer(const ApplicationSpecification& spec, std::filesystem::path project, PlayerCommandLine commandLine, PlayerLaunchSettings launch)
            : Application(spec)
        {
            PushLayer(new RuntimeLayer(std::move(project), std::move(commandLine), std::move(launch)));
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

        PlayerCommandLine commandLine = PlayerCommandLine::Parse(args.Count, args.Args);
        if (commandLine.Help)
        {
            const std::string program = args.Count > 0 && args[0] ? std::filesystem::path(args[0]).filename().string() : std::string("ZaminPlayer");
            std::fputs(PlayerCommandLine::GetHelpText(program).c_str(), stdout);
            std::exit(0);
        }
        // The window about a crash report (the crash handler started this process): nothing else.
        if (!commandLine.CrashDialog.empty())
        {
            spec.Name = "Zamin Player (xato)";
            spec.Window.Title = "Xato";
            spec.Window.Width = 780;
            spec.Window.Height = 440;
            spec.Window.Resizable = false;
            spec.Window.VSync = true;
            return new CrashDialogApp(spec, std::filesystem::absolute(commandLine.CrashDialog));
        }
        for (const std::string& problem : commandLine.Errors)
            ZM_WARN("Buyruq qatori: {}", problem);

        std::filesystem::path project = FindProject(commandLine.Project.empty() ? nullptr : commandLine.Project.c_str());
        if (project.empty())
        {
            ZM_CRITICAL("Loyiha topilmadi. Foydalanish: ZaminPlayer <Game.zproject> (hamma variantlar: --help)");
            std::exit(1);
        }

        // The window as the project's player settings say (read before the engine starts: the window is
        // made with them); a file that cannot be read gets the defaults here and fails to load later.
        ProjectConfig config;
        std::string error;
        if (!ProjectSerializer::ConfigFromString(FileSystem::ReadTextFile(project), config, nullptr, &error))
            config = ProjectConfig();
        // The display options the game saved (its options menu), from its SaveData file - also before the
        // window is made, so a game left fullscreen opens fullscreen at once.
        SaveData::SetFilePath(SaveData::GetFilePathFor(config.Player.CompanyName, config.Player.GetProductName(config.Name)));
        const SavedDisplaySettings saved = Display::GetSavedSettings();
        SaveData::SetFilePath({}); // the project's own location once it is open (the same file)

        // The log file (from here on every message goes there too; the ones before are written first):
        // --log-file, else ~/.local/share/<company>/<product>/logs/player-<date>.log (the last 5) + latest.log.
        const std::string product = config.Player.GetProductName(config.Name);
        const std::filesystem::path logDirectory = LogFile::GetDefaultDirectory(config.Player.CompanyName, product);
        if (!commandLine.NoLogFile)
        {
            std::string command;
            for (int i = 1; i < args.Count; i++)
                command += std::string(i > 1 ? " " : "") + (args[i] ? args[i] : "");
            char started[32];
            const std::time_t now = std::time(nullptr);
            std::tm tm{};
            localtime_r(&now, &tm);
            std::strftime(started, sizeof(started), "%Y-%m-%d %H:%M:%S", &tm);
            const std::string header = std::format("# {} {} - Zamin Player (Zamin Engine {})\n# Boshlandi: {}\n# Loyiha: {}\n# Buyruq qatori: {}\n", product,
                                                   config.Player.Version, ZM_ENGINE_VERSION, started, project.string(), command.empty() ? "-" : command);
            std::string logError;
            const bool opened = !commandLine.LogFile.empty()
                                    ? LogFile::Open(commandLine.LogFile, header, &logError)
                                    : LogFile::OpenRotating({ logDirectory, "player", 5, true, header }, &logError);
            if (opened)
                ZM_INFO("Log fayli: {}", LogFile::GetPath().string());
            else
                ZM_WARN("Log fayli ochilmadi ({}) - log faqat terminalda", logError);
        }
        // Crashes (fatal signals, uncaught exceptions): a report next to the logs and a window about it
        // (this program again, --crash-dialog; automation runs - screenshots, CI - without the window
        // unless ZAMIN_CRASH_DIALOG=1).
        if (!commandLine.NoCrashHandler)
        {
            CrashHandlerSettings crash;
            crash.ReportDirectory = LogFile::IsOpen() ? LogFile::GetDirectory() : logDirectory;
            crash.Application = "Zamin Player";
            crash.Product = product;
            crash.Version = config.Player.Version;
            crash.LogFile = LogFile::GetPath();
            crash.ShowDialog = !commandLine.NoCrashDialog && (!Application::IsAutomationRun() || std::getenv("ZAMIN_CRASH_DIALOG"));
            crash.DialogProgram = FileSystem::GetExecutablePath();
            crash.DialogArguments = { "--crash-dialog" };
            if (CrashHandler::Install(crash))
                ZM_INFO("Crash hisobotlari: {}", crash.ReportDirectory.string());
        }

        PlayerLaunchSettings launch = PlayerLaunchSettings::Resolve(config, saved, commandLine);
        for (const std::string& warning : launch.Warnings)
            ZM_WARN("{}", warning);
        spec.Window = launch.Window;
        return new ZaminPlayer(spec, project, std::move(commandLine), std::move(launch));
    }

}
