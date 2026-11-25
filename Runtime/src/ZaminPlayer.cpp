// Zamin Player: runs an exported / development game project. Its window is made as the project's
// player settings say (size, fullscreen / borderless, vsync, title, icon, frame rate limit, running in
// the background); what the player saved in the game's options menu wins over them, and the command
// line over both (PlayerLaunchSettings::Resolve). A splash screen (the Zamin logo or the game's picture)
// shows while the first scene loads in the background; F11 / Alt+Enter switch fullscreen. The finished
// frame goes through a last pass with the player's gamma and brightness (the game's options menu,
// --gamma / --brightness); a game that does not run in the background stands still when its window
// loses the focus (time, physics, audio) and shows its last frame dimmed with "Pauza" until it is back.
//   ZaminPlayer [path/to/Game.zproject] [--windowed | --fullscreen | --borderless] [--resolution WxH]
//               [--monitor N] [--vsync | --no-vsync] [--fps N] [--scene <path | build index | name>]
//               [--quality <level>] [--lang <code>] [--no-splash] [--gamma G] [--brightness B] [--gl ...] [--msaa N] [--debug-...]
//               [--log-file <path> | --no-log-file] [--log-level <spec>] [--no-crash-handler] [--no-crash-dialog]
//               [--benchmark N [--benchmark-warmup W] [--benchmark-out <file.json>] [--benchmark-max-avg MS] [--benchmark-max-p95 MS]]
//   ZaminPlayer --help lists every option; ZaminPlayer --version prints the game's and the engine's versions.
// --benchmark N: the performance test - N frames timed after a warm-up (average, median, p95, p99), the result
// in the log and a JSON file, the exit code 0 / 3 (a limit exceeded) / 2 (it could not run).
// The log goes to ~/.local/share/<company>/<product>/logs (player-<date>.log, the last 5 kept, latest.log);
// a crash writes crash-<date>.txt there and the player shows it in a window of its own
// (ZaminPlayer --crash-dialog <report>, started by the crash handler).
#include <Zamin/Zamin.h>
#include <Zamin/Core/EntryPoint.h>
#include <Zamin/Gameplay/GameplayUI.h>
#include <Zamin/UI/UIAudit.h>
#include <Zamin/Scene/WorldTour.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <charconv>
#include <chrono>
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
        using Clock = std::chrono::steady_clock;

        RuntimeLayer(std::filesystem::path project, PlayerCommandLine commandLine, PlayerLaunchSettings launch)
            : Layer("RuntimeLayer"), m_ProjectPath(std::move(project)), m_SceneOverride(commandLine.Scene), m_CommandLine(std::move(commandLine)),
              m_Launch(std::move(launch)), m_Benchmark(m_CommandLine.Benchmark) {}

        void OnAttach() override
        {
            m_AttachTime = Clock::now();
            bool projectLoaded = false;
            {
                ZM_PROFILE_SCOPE("Project::Load");
                projectLoaded = Project::Load(m_ProjectPath) != nullptr;
            }
            if (!projectLoaded)
            {
                ZM_CRITICAL("Project failed to load: {}", m_ProjectPath.string());
                if (m_Benchmark.IsEnabled())
                    Application::Get().SetExitCode(PlayerBenchmarkSettings::ExitFailed);
                Application::Get().Close();
                return;
            }
            m_ProjectLoaded = true;
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
                    ZM_WARN("The game icon could not be read ({}): {}", player.Icon.generic_string(), error);
            }
            // The window was made with the resolved settings (the command line > the saved display options >
            // the project); the frame rate limit the same way.
            ZM_INFO("Window settings: {}", m_Launch.Describe());
            Application::Get().SetTargetFrameRate(m_Launch.TargetFrameRate);
            Application::Get().SetRunInBackground(player.RunInBackground);
            if (m_Launch.TargetFrameRate > 0)
                ZM_INFO("Frame rate limit: {} FPS", m_Launch.TargetFrameRate);
            // The player's own audio settings (the game's options menu saved them) over the project's mixer.
            if (Audio::LoadSettings())
                ZM_INFO("Saved audio settings loaded");
            // The game's display options change this window (fullscreen, vsync); the window already is as
            // they were saved (or as the command line says).
            Display::BindWindow(&Application::Get().GetWindow());
            // The picture's gamma and brightness: --gamma / --brightness, else what the player saved in the game's
            // options menu (Display::SaveSettings), else unchanged.
            {
                const SavedDisplaySettings saved = Display::GetSavedSettings();
                if (saved.Gamma)
                    Display::SetGamma(*saved.Gamma);
                if (saved.Brightness)
                    Display::SetBrightness(*saved.Brightness);
                if (m_CommandLine.Gamma)
                    Display::SetGamma(*m_CommandLine.Gamma);
                if (m_CommandLine.Brightness)
                    Display::SetBrightness(*m_CommandLine.Brightness);
                if (!Display::GetAdjustment().IsIdentity())
                    ZM_INFO("Image: gamma {:.2f} ({}), brightness {:.0f}% ({})", Display::GetGamma(),
                            m_CommandLine.Gamma ? "command line" : saved.Gamma ? "saved" : "standart", Display::GetBrightness() * 100.0f,
                            m_CommandLine.Brightness ? "command line" : saved.Brightness ? "saved" : "standart");
            }
            if (m_Benchmark.IsEnabled())
            {
                // The test measures the game, not a pause: it runs whatever the window's focus.
                Application::Get().SetRunInBackground(true);
                ZM_INFO("Benchmark: {} frames are measured (after {} warm-up frames; no splash, FPS limit{})", m_Benchmark.GetSettings().Frames,
                        m_Benchmark.GetSettings().Warmup, m_Launch.Window.VSync ? "" : " or VSync");
            }
            else if (!player.RunInBackground)
                ZM_INFO("Running in the background is off: the game pauses when the window loses focus (time, physics and sound stop)");
            // The game's cursor wishes (Cursor: hidden, locked, its picture) go to this window.
            Cursor::BindWindow(&Application::Get().GetWindow());
            Cursor::SetHostActive(true);
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
                    ZM_WARN("--quality {}: there is no such quality level (available: {}) - the default level stays", m_CommandLine.Quality, names);
                }
            }
            else if (Quality::LoadSettings())
                ZM_INFO("Saved quality level loaded");
            ZM_INFO("Quality level: {}", Quality::GetLevelName());

            // Language: --lang, else the one the player chose in the game (saved), else the project's start language.
            if (!m_CommandLine.Language.empty())
            {
                if (!Localization::SetLanguage(m_CommandLine.Language))
                {
                    std::string codes;
                    for (const std::string& code : Localization::GetLanguages())
                        codes += (codes.empty() ? "" : ", ") + code;
                    ZM_WARN("--lang {}: the game has no such language (available: {}) - '{}' stays", m_CommandLine.Language, codes, Localization::GetLanguage());
                }
            }
            else if (Localization::LoadSettings())
                ZM_INFO("Saved language loaded");
            if (Localization::GetLanguages().size() > 1 || Localization::GetTable(Localization::GetLanguage()))
                ZM_INFO("Language: {} ({})", Localization::GetLanguageName(Localization::GetLanguage()), Localization::GetLanguage());

            // Debug drawing: the project's settings, then the command line (Shipping: compiled out).
            DebugDrawSettings& debugDraw = DebugDraw::GetSettings();
            debugDraw = config.DebugDraw;
            if (m_CommandLine.NoDebugDraw) debugDraw.Enabled = false;
            if (m_CommandLine.DebugRaycasts) debugDraw.ShowRaycasts = true;
            if (m_CommandLine.DebugContacts) debugDraw.ShowContacts = true;
            if (m_CommandLine.DebugContactDetails) debugDraw.ShowContacts = debugDraw.ShowContactDetails = true;
            if (m_CommandLine.DebugJoints) debugDraw.ShowJoints = true;
            if (m_CommandLine.DebugVelocities) debugDraw.ShowVelocities = debugDraw.ShowBodyVelocities = true;
            if (m_CommandLine.DebugNavigation) debugDraw.ShowNavigation = true;
            if (m_CommandLine.DebugNavigationCells) debugDraw.ShowNavigationWalkable = true;

            if (auto module = Project::GetScriptModulePath(); !module.empty())
            {
                ZM_PROFILE_SCOPE("Script module");
                ScriptEngine::LoadModule(module);
            }

            // Automation (comparisons, CI): ZAMIN_PLAYER_NO_CULLING=1 / ZAMIN_PLAYER_NO_INSTANCING=1 turn the
            // renderer's frustum culling / GPU instancing off; ZAMIN_PLAYER_STATS=1 logs the last frame's
            // render statistics (and the audio profile) at exit.
            if (std::getenv("ZAMIN_PLAYER_NO_CULLING"))
                Renderer3D::SetFrustumCulling(false);
            if (std::getenv("ZAMIN_PLAYER_NO_INSTANCING"))
                Renderer3D::SetInstancing(false);
            ConfigureUIAudit();
            // ZAMIN_SOAK_STATS=N (automation: the slices' attract-loop soak, Tests/slice_bench.py --soak): every N frames a
            // "SOAK frame=... scene=... rss=<MiB> entities=... engine=<MiB>" line - the process's resident memory, the active
            // scene's entities and the memory the engine tracks, sampled to find growth across the game's loops.
            if (const char* soak = std::getenv("ZAMIN_SOAK_STATS"))
                m_SoakEvery = std::clamp(std::atoi(soak), 0, 100000);
            // ZAMIN_WORLD_TOUR=<file.ztour> (automation: world streaming acceptance): the primary camera follows that
            // repeatable path - positions by frame number, see WorldTour - from the game's first frame;
            // ZAMIN_WORLD_REPORT=<file.json> gets every frame's time, the streaming / upload work, RSS and the tracked
            // GPU requests with their summary when the tour ends (or the player closes first: "Complete": false).
            if (const char* tour = std::getenv("ZAMIN_WORLD_TOUR"))
                m_TourFile = tour;
            if (const char* report = std::getenv("ZAMIN_WORLD_REPORT"))
                m_TourReportFile = report;
            // ZAMIN_SCREENSHOT_ON="<text>|<frames>|<file.png>[|quit]" (automation: a picture of a moment the game logs, e.g.
            // its victory screen): the first log line containing <text>, <frames> later (0..600) the frame is saved there;
            // with "|quit" the player then closes (the run ends at that moment, whatever ZAMIN_MAX_FRAMES says).
            if (const char* on = std::getenv("ZAMIN_SCREENSHOT_ON"))
            {
                std::string spec = on;
                if (spec.ends_with("|quit"))
                {
                    m_ShotQuit = true;
                    spec.resize(spec.size() - 5);
                }
                const size_t a = spec.find('|'), b = a == std::string::npos ? a : spec.find('|', a + 1);
                if (a != std::string::npos && b != std::string::npos && a > 0 && b + 1 < spec.size())
                {
                    m_ShotMarker = spec.substr(0, a);
                    m_ShotDelay = std::clamp(std::atoi(spec.substr(a + 1, b - a - 1).c_str()), 0, 600);
                    m_ShotPath = spec.substr(b + 1);
                    m_ShotSeen = Log::GetTotalWritten();
                }
                else
                    ZM_WARN("ZAMIN_SCREENSHOT_ON: expected \"<text>|<frames>|<file.png>\": {}", spec);
            }
            // ZAMIN_PLAYER_INPUT="<frame>:<action>;..." plays input in (screenshots, CI): a key name
            // ("Down", "Enter", "Tab": pressed in that frame, released in the next), "hold=W" / "release=W"
            // (a key held from that frame until its release: driving, running), "mouse=x,y" (the
            // game's mouse at window px from the top-left from then on), "click" (the left button for
            // one frame), "down" / "up" (the left button held from then on / let go: drags),
            // "scroll=dy" / "scroll=dx,dy" (the mouse wheel in that frame, notches), or a simulated gamepad
            // (gamepad 0, connected by its first item): "pad=A" (a button pressed for one frame),
            // "padhold=A" / "padrelease=A" (held until released), "stick=LeftX,0.8" (an axis at that value
            // from then on; triggers 0..1).
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
                        ZM_WARN("Start scene not found ({}) - the first build scene is opened: {}", config.StartScene.generic_string(),
                                resolved.generic_string());
                    start = resolved;
                }
            int buildIndex = -1;
            if (const std::string& arg = m_SceneOverride; !arg.empty() &&
                std::from_chars(arg.data(), arg.data() + arg.size(), buildIndex).ptr == arg.data() + arg.size())
            {
                start = Project::GetBuildScenePath(buildIndex);
                if (start.empty())
                    ZM_ERROR("--scene {}: the build list has no such index (it has {} scenes)", buildIndex, SceneManager::GetBuildSceneCount());
            }
            else if (!m_SceneOverride.empty() && !start.has_extension())
                if (std::filesystem::path named = Project::FindBuildScene(m_SceneOverride); !named.empty())
                    start = named;
            if (start.empty() && m_SceneOverride.empty())
                start = Project::GetBuildScenePath(0);
            std::filesystem::path scenePath = start.empty() ? std::filesystem::path() : Project::GetAssetFileSystemPath(start);
            m_StartSceneName = start.generic_string();
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
                    ShowStartError("file not found");
                    return;
                }
                m_StartLoad->SetAllowSceneActivation(false); // it starts the game itself, after the splash
                const std::filesystem::path image = player.SplashImage.empty() ? std::filesystem::path()
                                                                               : Project::GetAssetFileSystemPath(player.SplashImage);
                m_Splash.Begin(player.SplashDuration, player.SplashSkippable, player.SplashBackground, image);
                ZM_INFO("Splash screen ({:.1f} s{}); the first scene is loading in the background: {}", player.SplashDuration,
                        player.SplashSkippable ? ", skipped by a key press" : "", start.generic_string());
            }
            else
            {
                ZM_PROFILE_SCOPE("Start scene");
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
            ZM_CRITICAL("Start scene failed to load: {} ({})", m_StartScenePath.string(), error);
            std::vector<SceneLoadReport> reports = m_Scenes ? m_Scenes->TakeLoadReports() : std::vector<SceneLoadReport>();
            if (!reports.empty())
                m_ErrorReport = reports.back();
            else
            {
                m_ErrorReport.File = m_StartScenePath.generic_string();
                SceneLoadIssue issue;
                issue.Level = SceneLoadIssue::Severity::Fatal;
                issue.Message = error.empty() ? std::string("the scene failed to load") : error;
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
            m_GameStarted = true;
            m_LoadSeconds = std::chrono::duration<double>(Clock::now() - m_AttachTime).count();
        }

        // --benchmark: one frame of the game timed (from the start of the last one: its update, drawing, the
        // swap) with the update + drawing calls of this layer in it; the result once the frames are all in.
        void BenchmarkFrame(Clock::time_point start, Clock::time_point end)
        {
            if (m_BenchmarkFinished)
                return;
            if (m_ErrorScreen || (!m_Scenes && !m_ProjectLoaded))
            {
                FinishBenchmark("the game did not start (the start scene failed to load)");
                return;
            }
            if (!m_GameStarted || !m_Scenes)
                return;
            if (m_HasLastFrameStart)
            {
                const double frameMs = std::chrono::duration<double, std::milli>(start - m_LastFrameStart).count();
                const double updateMs = std::chrono::duration<double, std::milli>(end - start).count();
                // The slowest measured frame's profile (Profiler::GetLastFrame is that frame now): where a hitch went.
                if (!m_Benchmark.IsWarmingUp() && frameMs > m_SlowestFrameMs)
                {
                    m_SlowestFrameMs = frameMs;
                    m_SlowestFrameIndex = m_Benchmark.GetMeasuredFrames();
                    m_SlowestFrameProfile = Profiler::FormatTree(Profiler::GetLastFrame(), 0.5f);
                }
                if (m_Benchmark.AddFrame(frameMs, updateMs, PlayerBenchmark::GameCodeMilliseconds(Profiler::GetLastFrame())))
                    FinishBenchmark();
            }
            m_LastFrameStart = start;
            m_HasLastFrameStart = true;
        }

        // The result: logged, written to --benchmark-out, the exit code (0 passed, 3 over a limit, 2 it did not
        // run to the end or its report could not be written). `failure`: why it could not run (the result
        // fails, exit code 2). `close`: the player closes now (not when the run already ended without it).
        void FinishBenchmark(std::string_view failure = {}, bool close = true)
        {
            m_BenchmarkFinished = true;
            PlayerBenchmark::Result result = m_Benchmark.GetResult();
            if (Project::GetActive())
            {
                result.Game = Project::GetProductName();
                result.GameVersion = Project::GetPlayerSettings().Version;
            }
            result.Scene = m_StartSceneName;
            result.Engine = BuildInfo::Get().Describe();
            result.Renderer = GraphicsContext::GetInfo().Renderer;
            auto& window = Application::Get().GetWindow();
            result.Width = window.GetFramebufferWidth();
            result.Height = window.GetFramebufferHeight();
            result.LoadSeconds = m_LoadSeconds;
            const bool ran = failure.empty() && m_Benchmark.IsDone();
            if (!failure.empty())
            {
                result.Passed = false;
                result.Failures.insert(result.Failures.begin(), std::string(failure));
            }
            if (m_SlowestFrameMs > 0.0)
                ZM_INFO("Benchmark: slowest frame #{} - {:.1f} ms (profiler, entries above 0.5 ms):\n{}", m_SlowestFrameIndex + 1,
                        m_SlowestFrameMs, m_SlowestFrameProfile);
            const std::string text = PlayerBenchmark::Describe(result);
            if (result.Passed)
                ZM_INFO("{}", text);
            else
                ZM_ERROR("{}", text);
            bool written = true;
            if (const auto& output = m_Benchmark.GetSettings().Output; !output.empty())
            {
                std::string error;
                written = PlayerBenchmark::WriteJson(result, output, &error);
                if (written)
                    ZM_INFO("Benchmark result: {}", std::filesystem::absolute(output).string());
                else
                    ZM_ERROR("Benchmark: result file {}", error);
            }
            // (A report that was asked for and is missing is a failed run: a script cannot tell it from a pass.)
            const int code = !ran || !written ? PlayerBenchmarkSettings::ExitFailed : result.Passed ? 0 : PlayerBenchmarkSettings::ExitLimitExceeded;
            Application::Get().SetExitCode(code);
            if (close)
                Application::Get().Close();
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
            ZM_INFO("Splash screen finished ({:.1f} s{}), the game started", timeline.GetElapsed(), timeline.WasSkipped() ? ", skipped" : "");
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
            ZM_INFO("Display: {} (saved)", Display::IsFullscreen() ? "fullscreen" : "windowed mode");
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

        // ZAMIN_UI_AUDIT (automation: the UI input / scale gate, Tests/slice_ui_test.py), a comma list: "top" - a UI_AUDIT
        // line 12 frames after the active scene or its open gameplay UI panel changed (the panel's show tween over);
        // frame numbers ("120,300") - at those frames (counted from the game's first frame). ZAMIN_UI_AUDIT_SIZES="800x600,..."
        // audits those view sizes (laid out without drawing), else the game view's; ZAMIN_UI_AUDIT_ELEMENTS=1 adds every
        // element to the line. Each line: the scene, the frame, the layout rules' issues (UIAudit.h) as JSON.
        struct UIAuditHook
        {
            bool Enabled = false, Top = false, Elements = false;
            std::vector<int> Frames;
            std::vector<glm::uvec2> Sizes;
            int Frame = 0, Due = -1;
            std::string Last;
        };
        UIAuditHook m_UIAudit;

        void ConfigureUIAudit()
        {
            const char* audit = std::getenv("ZAMIN_UI_AUDIT");
            if (!audit || !*audit)
                return;
            m_UIAudit.Enabled = true;
            for (std::string_view rest = audit; !rest.empty();)
            {
                const size_t comma = rest.find(',');
                const std::string item(rest.substr(0, comma));
                rest = comma == std::string_view::npos ? std::string_view() : rest.substr(comma + 1);
                if (item == "top")
                    m_UIAudit.Top = true;
                else if (const int frame = std::atoi(item.c_str()); frame > 0 && m_UIAudit.Frames.size() < 4096)
                    m_UIAudit.Frames.push_back(frame);
            }
            if (const char* sizes = std::getenv("ZAMIN_UI_AUDIT_SIZES"))
                for (std::string_view rest = sizes; !rest.empty();)
                {
                    const size_t comma = rest.find(',');
                    const std::string item(rest.substr(0, comma));
                    rest = comma == std::string_view::npos ? std::string_view() : rest.substr(comma + 1);
                    unsigned w = 0, h = 0;
                    if (std::sscanf(item.c_str(), "%ux%u", &w, &h) == 2 && w >= 64 && h >= 64 && w <= 16384 && h <= 16384 && m_UIAudit.Sizes.size() < 32)
                        m_UIAudit.Sizes.push_back({ w, h });
                }
            m_UIAudit.Elements = std::getenv("ZAMIN_UI_AUDIT_ELEMENTS") != nullptr;
            ZM_INFO("UI audit: {}on frame {} ({} sizes)", m_UIAudit.Top ? "on every panel change and " : "", m_UIAudit.Frames.size(),
                    m_UIAudit.Sizes.empty() ? size_t(1) : m_UIAudit.Sizes.size());
        }

        void RunUIAudit(uint32_t width, uint32_t height)
        {
            if (!m_UIAudit.Enabled || !m_Scenes || !m_Scenes->GetActiveScene())
                return;
            Scene& scene = *m_Scenes->GetActiveScene();
            const int frame = ++m_UIAudit.Frame;
            bool now = std::find(m_UIAudit.Frames.begin(), m_UIAudit.Frames.end(), frame) != m_UIAudit.Frames.end();
            if (m_UIAudit.Top)
            {
                const Gameplay::UIWorld* ui = Gameplay::FindGameplayUI(scene);
                const std::string key = scene.GetName() + "|" + (ui ? ui->State().Top : std::string());
                if (key != m_UIAudit.Last)
                {
                    m_UIAudit.Last = key;
                    m_UIAudit.Due = frame + 12;
                }
                now = now || frame == m_UIAudit.Due;
            }
            if (!now)
                return;
            std::vector<glm::uvec2> sizes = m_UIAudit.Sizes;
            if (sizes.empty())
                sizes.push_back({ width, height });
            for (const glm::uvec2& size : sizes)
            {
                const UIAuditReport report = scene.AuditUI(size.x, size.y, true);
                UIAuditRules rules;
                rules.MinFontPx = UIAuditRules::MinFontFor(size, report.UserScale);
                ZM_INFO("UI_AUDIT scene={} frame={} {}", scene.GetName(), frame, UIAuditToJson(report, CheckUIAudit(report, rules), m_UIAudit.Elements));
            }
        }

        std::string m_ShotMarker, m_ShotPath;
        int m_ShotDelay = 0, m_ShotDue = -1, m_ShotFrame = 0;
        bool m_ShotQuit = false;
        uint64_t m_ShotSeen = 0;
        void RunMarkerScreenshot()
        {
            if (m_ShotMarker.empty())
                return;
            ++m_ShotFrame;
            if (m_ShotDue < 0)
            {
                if (Log::GetTotalWritten() == m_ShotSeen)
                    return;
                uint64_t first = 0;
                const std::vector<LogMessage> messages = Log::GetMessages(&first);
                for (size_t i = 0; i < messages.size() && m_ShotDue < 0; i++)
                    if (first + i >= m_ShotSeen && messages[i].Text.find(m_ShotMarker) != std::string::npos)
                        m_ShotDue = m_ShotFrame + m_ShotDelay;
                m_ShotSeen = first + messages.size();
            }
            if (m_ShotDue >= 0 && m_ShotFrame == m_ShotDue)
            {
                ZM_INFO("ZAMIN_SCREENSHOT_ON: {1} frames after '{0}'", m_ShotMarker, m_ShotDelay);
                Application::Get().RequestScreenshot(m_ShotPath);
                m_ShotMarker.clear();
                if (m_ShotQuit)
                    Application::Get().Close();
            }
        }

        int m_SoakEvery = 0, m_SoakFrame = 0;

        // ZAMIN_WORLD_TOUR / ZAMIN_WORLD_REPORT: the repeatable camera path and its report.
        std::filesystem::path m_TourFile, m_TourReportFile;
        WorldTourRunner m_Tour;
        Clock::time_point m_TourFrameStart;
        bool m_TourHasFrameStart = false, m_TourFailed = false, m_TourReported = false;
        void UpdateWorldTour()
        {
            if (m_TourFile.empty() || m_TourFailed || !m_Scenes)
                return;
            if (!m_Tour.IsActive())
            {
                WorldTour tour;
                std::string error;
                const std::filesystem::path file = m_TourFile.is_absolute() ? m_TourFile : Project::GetAssetFileSystemPath(m_TourFile);
                if (!WorldTour::Load(file, tour, error) || !m_Tour.Start(std::move(tour), error))
                {
                    ZM_ERROR("World tour: {} ({})", error, file.string());
                    m_TourFailed = true;
                    return;
                }
                ZM_INFO("World tour started: '{}', {} frames after {} warm-up frames", m_Tour.GetTour().Name, m_Tour.GetTour().GetLength() + 1, m_Tour.GetTour().Warmup);
            }
            const Clock::time_point now = Clock::now();
            const double ms = m_TourHasFrameStart ? std::chrono::duration<double, std::milli>(now - m_TourFrameStart).count() : 0.0;
            m_TourFrameStart = now;
            m_TourHasFrameStart = true;
            const bool finished = m_Tour.IsFinished();
            m_Tour.BeginFrame(*m_Scenes, ms);
            if (!finished && m_Tour.IsFinished())
                WriteWorldTourReport();
        }
        void WriteWorldTourReport()
        {
            if (m_TourReported || !m_Tour.IsActive())
                return;
            m_TourReported = true;
            const WorldTourReport::Summary summary = m_Tour.GetReport().Summarize();
            ZM_INFO("World tour report: complete={} frames={} frameMs p50={:.2f} p95={:.2f} p99={:.2f} max={:.2f} worldMsMax={:.2f} uploadMsMax={:.2f} "
                    "uploadBytesMax={} rssPeak={} rssLast={} gpuPeak={} gpuLast={}",
                    m_Tour.IsFinished(), summary.Frames, summary.FrameMs.P50, summary.FrameMs.P95, summary.FrameMs.P99, summary.FrameMs.Max, summary.WorldMs.Max,
                    summary.UploadMs.Max, summary.PeakUploadBytes, summary.PeakRSS, summary.LastRSS, summary.PeakGPU, summary.LastGPU);
            if (m_TourReportFile.empty())
                return;
            std::string error;
            if (!m_Tour.WriteReport(m_TourReportFile, m_Scenes.get(), error))
                ZM_ERROR("World tour: {} ({})", error, m_TourReportFile.string());
        }
        void RunSoakStats()
        {
            if (m_SoakEvery <= 0 || !m_Scenes || !m_Scenes->GetActiveScene() || ++m_SoakFrame % m_SoakEvery != 0)
                return;
            Scene& scene = *m_Scenes->GetActiveScene();
            const MemoryStats::ProcessMemory process = MemoryStats::GetProcessMemory(0.0);
            constexpr double MiB = 1024.0 * 1024.0;
            ZM_INFO("SOAK frame={} scene={} rss={:.1f} entities={} engine={:.1f}", m_SoakFrame, scene.GetName(), double(process.Resident) / MiB,
                    scene.GetRegistry().view<IDComponent>().size(), double(MemoryStats::GetTrackedTotal()) / MiB);
        }

        // Plays the scripted input of this frame (seen by the next Input::Update).
        void PlayScriptedInput()
        {
            for (Key key : m_ReleaseKeys)
                Input::SimulateKey(key, false);
            m_ReleaseKeys.clear();
            for (GamepadButton button : m_ReleaseButtons)
                Input::SimulateGamepadButton(0, button, false);
            m_ReleaseButtons.clear();
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
                else if ((action.starts_with("hold=") || action.starts_with("release=")) &&
                         KeyFromString(std::string_view(action).substr(action.find('=') + 1), key))
                    Input::SimulateKey(key, action.starts_with("hold=")); // held across frames until released
                else if (action.starts_with("pad=") || action.starts_with("padhold=") || action.starts_with("padrelease="))
                {
                    GamepadButton button{};
                    if (GamepadButtonFromString(std::string_view(action).substr(action.find('=') + 1), button))
                    {
                        Input::SimulateGamepadConnected(0, true);
                        Input::SimulateGamepadButton(0, button, !action.starts_with("padrelease="));
                        if (action.starts_with("pad="))
                            m_ReleaseButtons.push_back(button); // a tap: released in the next frame
                    }
                    else
                        ZM_WARN("ZAMIN_PLAYER_INPUT: unknown gamepad button '{}'", action);
                }
                else if (action.starts_with("stick="))
                {
                    const std::string_view spec = std::string_view(action).substr(6);
                    const size_t comma = spec.find(',');
                    GamepadAxis axis{};
                    if (comma != std::string_view::npos && GamepadAxisFromString(spec.substr(0, comma), axis))
                    {
                        float value = std::clamp((float)std::atof(std::string(spec.substr(comma + 1)).c_str()), -1.0f, 1.0f);
                        if (axis == GamepadAxis::LeftTrigger || axis == GamepadAxis::RightTrigger)
                            value = std::max(value, 0.0f) * 2.0f - 1.0f; // triggers read -1 (released) .. 1
                        Input::SimulateGamepadConnected(0, true);
                        Input::SimulateGamepadAxis(0, axis, value);
                    }
                    else
                        ZM_WARN("ZAMIN_PLAYER_INPUT: unknown gamepad axis '{}'", action);
                }
                else if (KeyFromString(action, key))
                {
                    // a tap: held for one frame (the platform's keyboard poll does not undo it)
                    Input::SimulateKey(key, true);
                    m_ReleaseKeys.push_back(key);
                }
                else
                    ZM_WARN("ZAMIN_PLAYER_INPUT: unknown action '{}'", action);
            }
            m_Frame++;
        }

        // The player closes: a performance test that did not measure all its frames (the window closed, the game
        // quit, ZAMIN_MAX_FRAMES) still reports - an incomplete, failed result (exit code 2), never a silent 0.
        void OnRunEnded() override
        {
            if (!m_Benchmark.IsEnabled() || m_BenchmarkFinished)
                return;
            std::string failure;
            if (!m_ProjectLoaded)
                failure = "the project failed to load";
            else if (m_ErrorScreen || (!m_GameStarted && !m_Scenes))
                failure = "the game did not start (the start scene failed to load)";
            else if (!m_GameStarted)
                failure = "the player closed before the game started";
            else
                failure = std::format("the player closed before the benchmark finished ({})",
                                      m_QuitRequested ? "the game asked to quit" : "the window was closed or ZAMIN_MAX_FRAMES");
            FinishBenchmark(failure, false);
        }

        void OnDetach() override
        {
            WriteWorldTourReport(); // (a tour the player's end cut short still reports: "Complete": false)
            if (std::getenv("ZAMIN_PLAYER_STATS"))
            {
                ZM_INFO("Render statistics (last frame): {}", Renderer::GetStats().Format());
                {
                    // The process's memory and what the engine holds of it (task 496: measured per sample).
                    const MemoryStats::ProcessMemory process = MemoryStats::GetProcessMemory(0.0);
                    std::string categories;
                    for (size_t i = 0; i < MemoryCategoryCount; i++)
                        if (const MemoryStats::Category c = MemoryStats::Get((MemoryCategory)i); c.Bytes > 0)
                            categories += std::format(", {} {}", MemoryStats::GetName((MemoryCategory)i), MemoryStats::FormatBytes(c.Bytes));
                    ZM_INFO("Memory: RAM {} (peak {}), engine {}{}", MemoryStats::FormatBytes(process.Resident),
                            MemoryStats::FormatBytes(process.PeakResident), MemoryStats::FormatBytes(MemoryStats::GetTrackedTotal()), categories);
                }
                // What the last frame's scene holds (the performance check of the samples quotes it).
                if (m_Scenes && m_Scenes->GetActiveScene())
                {
                    SceneStatisticsCollector collector;
                    const SceneStatistics stats = collector.Collect(*m_Scenes->GetActiveScene());
                    std::string top;
                    for (size_t i = 0; i < stats.Components.size() && i < 8; i++)
                        top += std::format("{}{} {}", top.empty() ? "" : ", ", stats.Components[i].Key, stats.Components[i].Count());
                    ZM_INFO("Scene (last frame): {} objects ({} active), {} components, {} scripts; most common: {}", stats.Entities, stats.Active,
                            stats.ComponentTotal, stats.ScriptComponents, top);
                }
                // The real frame times (frame pacing: --fps, vsync).
                const FrameTimeHistory& frames = Application::Get().GetFrameTimeHistory();
                const FrameTimeHistory::Summary summary = frames.Summarize();
                ZM_INFO("Frame time (last {} frames): average {:.2f} ms ({:.1f} FPS), min {:.2f} ms, max {:.2f} ms, slowest 1%: {:.2f} ms", frames.Size(),
                        summary.Average, summary.AverageFPS, summary.Min, summary.Max, summary.Worst1Percent);
                ZM_INFO("Profiler (last frame, ms):\n{}", Profiler::FormatTree(Profiler::GetLastFrame()));
                ZM_INFO("Audio profile (last frame): {}", m_LastAudioProfile);
                for (const SceneLoadTiming& load : SceneLoadProfiler::GetHistory())
                    ZM_INFO("Scene load: {}", load.Format());
            }
            if (m_Scenes)
            {
                m_Scenes->Stop();
                m_Scenes.reset();
            }
            ScriptEngine::UnloadModule();
            Display::BindWindow(nullptr);
            Cursor::BindWindow(nullptr);
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
            line("The game's scene failed to load", 36.0f * scale, white);
            line(m_ErrorReport.File.empty() ? std::string("(no scene given)") : m_ErrorReport.File, 17.0f * scale, grey);
            y -= 14.0f * scale;
            size_t shown = 0;
            for (const SceneLoadIssue& issue : m_ErrorReport.Issues)
            {
                if (shown++ == 10)
                {
                    line(std::format("... and {} more", m_ErrorReport.Issues.size() - 10), 18.0f * scale, grey);
                    break;
                }
                line(issue.Message, 20.0f * scale, issue.Level == SceneLoadIssue::Severity::Warning ? white : red);
                if (!issue.SourceLine.empty())
                    line("    " + issue.SourceLine, 17.0f * scale, grey);
            }
            y = 70.0f * scale;
            line("Esc - quit.  Full details in the console (log).", 17.0f * scale, grey);
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

        // The window's size changed (resized, fullscreen switched): logged once it settles (a drag resizes
        // it every frame), with the picture's area when it has bars.
        void OnWindowSize(uint32_t w, uint32_t h, const PlayerSettings::ViewportRect& area)
        {
            if (w == m_WindowWidth && h == m_WindowHeight)
            {
                if (m_SizeSettle > 0 && --m_SizeSettle == 0)
                {
                    if (area.Letterboxed)
                        ZM_INFO("Window size: {}x{} - game image {}x{} ({}:{}, bars at the edges)", w, h, area.Width, area.Height,
                                Project::GetPlayerSettings().AspectWidth, Project::GetPlayerSettings().AspectHeight);
                    else
                        ZM_INFO("Window size: {}x{}", w, h);
                }
                return;
            }
            const bool first = m_WindowWidth == 0;
            m_WindowWidth = w;
            m_WindowHeight = h;
            m_SizeSettle = first && !area.Letterboxed ? 0 : 10;
        }

        // ZAMIN_PLAYER_CRASH_TEST="<frame>:<segv | abort | fpe | exception | assert>": the player crashes on purpose
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
            ZM_WARN("ZAMIN_PLAYER_CRASH_TEST: deliberate crash ({}) - frame {}", kind, at);
            if (kind == "segv")
            {
                // A real invalid write, through a volatile pointer (a constant address made GCC -O2 warn).
                volatile int* volatile invalid = reinterpret_cast<volatile int*>((uintptr_t)8);
                *invalid = 1;
            }
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
                throw std::runtime_error("ZAMIN_PLAYER_CRASH_TEST test exception");
            else if (kind == "assert")
            {
                // A failed assert (where they are compiled in): the report names it as the reason.
                const int lives = 0;
                ZM_ASSERT(lives > 0, "the player's health is {} - ZAMIN_PLAYER_CRASH_TEST test assert (frame {})", lives, at);
                if (!Assert::AreAssertsEnabled())
                    ZM_WARN("ZAMIN_PLAYER_CRASH_TEST: asserts are off in this build (-DZAMIN_ASSERTS=ON)");
                return;
            }
            ZM_WARN("ZAMIN_PLAYER_CRASH_TEST: unknown type '{}' (segv, abort, fpe, exception, assert)", kind);
        }

        void OnUpdate(Timestep ts) override
        {
            const Clock::time_point start = Clock::now();
            UpdateFrame(ts);
            if (m_Benchmark.IsEnabled())
                BenchmarkFrame(start, Clock::now());
        }

        void UpdateFrame(Timestep ts)
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
            UpdateWorldTour(); // (the frame that just ended is recorded, the camera moved for this one)
            // The audio engine is shut down before OnDetach: its profile is kept from the last frame.
            static const bool logStats = std::getenv("ZAMIN_PLAYER_STATS") != nullptr;
            if (logStats)
                m_LastAudioProfile = AudioEngine::GetProfile().Format();
            auto& window = Application::Get().GetWindow();
            uint32_t w = window.GetFramebufferWidth(), h = window.GetFramebufferHeight();
            if (w == 0 || h == 0)
                return; // (minimized: the application does not even call this)
            // The game's picture: the whole window, or (Project settings > Player > fixed aspect ratio) that
            // shape in its middle with bars around it. The scenes' cameras, the UI's canvas scaling and the
            // render targets follow its size every frame (the window resized, fullscreen switched).
            const PlayerSettings::ViewportRect area = Project::GetPlayerSettings().ComputeViewport(w, h);
            OnWindowSize(w, h, area);
            m_Scenes->OnViewportResize(area.Width, area.Height);

            Renderer::ResetStats();
            glm::vec2 mouse = m_HasScriptedMouse ? m_ScriptedMouse : Input::GetMousePosition();
            float scale = window.GetWidth() > 0 ? (float)w / (float)window.GetWidth() : 1.0f;
            // (The mouse in the picture's own pixels; over the bars it is outside it.)
            m_Scenes->SetViewportMouse(area.WindowToViewport(mouse * scale, h),
                                       Input::IsMouseButtonPressed(MouseButton::Left) || m_ScriptedClick || m_ScriptedHold);
            m_Scenes->SetViewportScroll(Input::GetMouseScrollDelta() + m_ScriptedScroll);

            // The scene whose camera draws the game: its picture settings (render size, post-processing).
            Scene* view = m_Scenes->GetRenderScene();
            const auto& post = view->GetSettings().Post;
            // The scene is rendered in linear HDR at the render scale of the picture's size, then
            // tonemapped (and scaled) to the screen.
            // (A pixel-perfect camera: the art's resolution, blown up with sharp pixels.)
            const glm::uvec2 renderSize = view->GetRuntimeRenderResolution(area.Width, area.Height);
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
            // The scene's passes start from the renderers' default state, whatever the overlays of the last
            // frame (splash cover, banner, fade, the display pass, the pause screen) left behind.
            RenderCommand::SetDepthTest(true);
            RenderCommand::SetDepthWrite(true);
            RenderCommand::SetBlendMode(BlendMode::Alpha);

            glm::vec4 clear = view->GetClearColor();
            RenderCommand::SetClearColor({ std::pow(clear.r, 2.2f), std::pow(clear.g, 2.2f), std::pow(clear.b, 2.2f), clear.a });
            RenderCommand::Clear();
            m_Scenes->OnUpdate(ts);
            RunUIAudit(area.Width, area.Height);
            RunSoakStats();
            RunMarkerScreenshot();
            view = m_Scenes->GetRenderScene(); // its frame may have switched its camera off

            // The finished frame goes into the player's own picture first when it is adjusted (gamma,
            // brightness) or the game may pause in the background (the paused picture is the last frame,
            // dimmed): then the last pass puts it on the screen (DisplayPass). Otherwise straight to the screen.
            const DisplayAdjustment adjustment = Display::GetAdjustment();
            const bool viaDisplay = !adjustment.IsIdentity() || !Application::Get().GetRunInBackground();
            {
                m_Target->Unbind();
                if (viaDisplay)
                {
                    if (!m_Display)
                    {
                        FramebufferSpecification spec;
                        spec.Width = w;
                        spec.Height = h;
                        spec.Attachments = { FramebufferTextureFormat::RGBA8 };
                        m_Display = Framebuffer::Create(spec);
                    }
                    else if (m_Display->GetSpecification().Width != w || m_Display->GetSpecification().Height != h)
                        m_Display->Resize(w, h);
                    m_Display->Bind();
                    if (!area.Letterboxed)
                    {
                        RenderCommand::SetClearColor({ 0.0f, 0.0f, 0.0f, 1.0f });
                        RenderCommand::Clear();
                    }
                }
                if (area.Letterboxed)
                {
                    // The bars (the whole window cleared; the picture covers its part).
                    RenderCommand::SetViewport(0, 0, w, h);
                    const glm::vec3& bars = Project::GetPlayerSettings().LetterboxColor;
                    RenderCommand::SetClearColor({ bars.r, bars.g, bars.b, 1.0f });
                    RenderCommand::Clear();
                }
                RenderCommand::SetViewport(area.X, area.Y, area.Width, area.Height);
                PostProcessInput input;
                input.ColorTexture = m_Target->GetColorAttachmentRendererID(0);
                input.Width = m_Target->GetSpecification().Width;
                input.Height = m_Target->GetSpecification().Height;
                input.DepthTexture = m_Target->GetDepthAttachmentRendererID();
                input.Projection = view->GetLastProjection();
                input.OutputWidth = area.Width;
                input.OutputHeight = area.Height;
                input.OutputOrigin = { (int)area.X, (int)area.Y };
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
                    debugView.Width = area.Width;
                    debugView.Height = area.Height;
                    debugView.DepthTexture = m_Target->GetDepthAttachmentRendererID();
                    debugView.DepthWidth = m_Target->GetSpecification().Width;
                    debugView.DepthHeight = m_Target->GetSpecification().Height;
                    DebugDraw::Render(debugView);
                }
                // A fade between scenes covers the game's picture.
                m_Scenes->RenderTransition(area.Width, area.Height);
                // The strips below are the player's own: over the whole window.
                RenderCommand::SetViewport(0, 0, w, h);
                RenderLoadBanner(w, h);
                m_BannerTime -= ts;
                RenderSplashCover(w, h, ts);
                // The last pass: the frame on the screen with the gamma and the brightness.
                if (viaDisplay)
                {
                    m_Display->Unbind();
                    RenderCommand::SetViewport(0, 0, w, h);
                    DisplayPass::Present(m_Display->GetColorAttachmentRendererID(0), adjustment);
                }
                m_HasDisplayFrame = viaDisplay;
            }

            if (m_Scenes->IsQuitRequested())
            {
                ZM_INFO("The game asked to quit");
                m_QuitRequested = true;
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
                        m_BannerText = report.Failed() ? std::format("Could not switch to the scene: {} - {}", name, report.Summary())
                                                       : std::format("Scene '{}' loaded partly: {}", name, report.Summary());
                        m_BannerTime = 6.0f;
                    }
            }
            if (m_Scenes->GetActiveSceneSerial() != m_ActiveSerial)
            {
                m_ActiveSerial = m_Scenes->GetActiveSceneSerial();
                m_PostHistory.Reset(); // the new level adapts at once (not from the last one's brightness)
            }
        }

        // Paused in the background (Project settings > Player > "Fonda ishlash" off, the window lost the focus):
        // the last frame dimmed, "Pauza" over it. Nothing of the game runs meanwhile (Application).
        bool OnRenderPaused() override
        {
            auto& window = Application::Get().GetWindow();
            const uint32_t w = window.GetFramebufferWidth(), h = window.GetFramebufferHeight();
            if (w == 0 || h == 0)
                return false;
            RenderCommand::SetViewport(0, 0, w, h);
            if (m_Display && m_HasDisplayFrame)
                DisplayPass::Present(m_Display->GetColorAttachmentRendererID(0), Display::GetAdjustment(), 0.6f);
            else
            {
                RenderCommand::SetClearColor({ 0.03f, 0.035f, 0.045f, 1.0f });
                RenderCommand::Clear();
            }
            const Ref<Font> font = Font::GetDefault();
            if (!font)
                return true;
            // The game's own words when its language files have them ("player.paused", "player.paused_hint").
            const std::string title = Localization::Has("player.paused") ? Localization::Get("player.paused") : std::string("Paused");
            const std::string hint = Localization::Has("player.paused_hint") ? Localization::Get("player.paused_hint")
                                                                             : std::string("The game window is not focused - select the window to continue");
            const float scale = std::clamp((float)h / 720.0f, 0.6f, 3.0f);
            RenderCommand::SetDepthTest(false);
            // (Colours only: the window stays opaque under the translucent band.)
            RenderCommand::SetColorWriteMask(0, true, false);
            Renderer2D::SetDisplayOutput(true);
            Renderer2D::SetLit(false);
            Renderer2D::BeginScene(glm::ortho(0.0f, (float)w, 0.0f, (float)h, -1.0f, 1.0f));
            auto centred = [&](const std::string& text, float size, float y, const glm::vec4& color) {
                const float width = Renderer2D::MeasureString(text, font).x * size;
                Renderer2D::TextParams params;
                params.Color = color;
                Renderer2D::DrawString(text, font,
                                       glm::translate(glm::mat4(1.0f), glm::vec3(((float)w - width) * 0.5f, y, 0.0f)) * glm::scale(glm::mat4(1.0f), glm::vec3(size)),
                                       params);
            };
            // A darker band behind the words (they stay readable over a bright picture), then the two bars of a
            // pause sign above the word.
            const float hintWidth = Renderer2D::MeasureString(hint, font).x * 20.0f * scale;
            Renderer2D::DrawQuad(glm::vec2((float)w * 0.5f, (float)h * 0.5f + 8.0f * scale),
                                 glm::vec2(std::min((float)w, std::max(hintWidth, 300.0f * scale) + 120.0f * scale), 250.0f * scale),
                                 glm::vec4(0.0f, 0.0f, 0.0f, 0.38f));
            const float barW = 16.0f * scale, barH = 58.0f * scale, cy = (float)h * 0.5f + 70.0f * scale;
            Renderer2D::DrawQuad(glm::vec2((float)w * 0.5f - 18.0f * scale, cy), glm::vec2(barW, barH), glm::vec4(0.95f, 0.96f, 0.98f, 0.92f));
            Renderer2D::DrawQuad(glm::vec2((float)w * 0.5f + 18.0f * scale, cy), glm::vec2(barW, barH), glm::vec4(0.95f, 0.96f, 0.98f, 0.92f));
            centred(title, 64.0f * scale, (float)h * 0.5f - 26.0f * scale, glm::vec4(0.96f, 0.97f, 1.0f, 1.0f));
            centred(hint, 20.0f * scale, (float)h * 0.5f - 72.0f * scale, glm::vec4(0.70f, 0.74f, 0.80f, 1.0f));
            Renderer2D::EndScene();
            Renderer2D::SetDisplayOutput(false);
            RenderCommand::SetColorWriteMask(0, true, true);
            RenderCommand::SetDepthTest(true);
            return true;
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
        std::string m_StartSceneName; // as the project / --scene names it (the benchmark's report)
        bool m_ProjectLoaded = false, m_GameStarted = false;
        // --benchmark: the frames timed, the start of the last one, the time the first scene took.
        PlayerBenchmark m_Benchmark;
        Clock::time_point m_AttachTime{}, m_LastFrameStart{};
        bool m_HasLastFrameStart = false, m_BenchmarkFinished = false, m_QuitRequested = false;
        double m_SlowestFrameMs = 0.0;      // --benchmark: the slowest measured frame and its profiler tree
        int m_SlowestFrameIndex = 0;
        std::string m_SlowestFrameProfile;
        double m_LoadSeconds = 0.0;
        // The splash screen and the first scene loading behind it; then the cover the game fades in from.
        SplashScreen m_Splash;
        Ref<AsyncOperation> m_StartLoad;
        float m_SplashCover = 0.0f;
        glm::vec3 m_SplashColor{ 0.0f };
        bool m_FullscreenToggle = true, m_HasToggleAction = false;
        uint32_t m_WindowWidth = 0, m_WindowHeight = 0; // the last framebuffer size (OnWindowSize)
        int m_SizeSettle = 0;                           // frames until a new size is logged
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
        // The finished frame before the last pass (gamma, brightness; the paused picture): RGBA8, the window's size.
        Ref<Framebuffer> m_Display;
        bool m_HasDisplayFrame = false;
        PostProcessHistory m_PostHistory; // auto exposure: the screen's adapted brightness
        // ZAMIN_PLAYER_INPUT
        std::vector<std::pair<int, std::string>> m_ScriptedInput;
        std::vector<Key> m_ReleaseKeys;
        std::vector<GamepadButton> m_ReleaseButtons; // scripted gamepad taps, released in the next frame
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
            const std::string product = !m_Summary.Product.empty() ? m_Summary.Product : !m_Summary.Application.empty() ? m_Summary.Application : "Game";
            Application::Get().GetWindow().SetTitle(std::format("{} - error", product));
            m_Product = product;
            ZM_INFO("Crash report: {} ({})", m_Report.string(), m_HasReport ? m_Summary.Reason : "unreadable");
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
            Button buttons[] = { { "Close", 0, {}, {} }, { "Copy path", 1, {}, {} }, { "Open folder", 2, {}, {} } };
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
                m_Notice = "Path copied";
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
            // A long text over a few lines, broken at its words (what does not fit in `maxLines` is cut with "...").
            auto wrapped = [&](const std::string& text, float size, const glm::vec4& color, size_t maxLines, float after) {
                if (!font)
                    return;
                const float maxWidth = (maxRight - left) / size;
                std::vector<std::string> lines;
                std::string current;
                for (std::string_view word : StringUtils::SplitView(text, ' ', StringUtils::SplitFlags::SkipEmpty))
                {
                    std::string candidate = current.empty() ? std::string(word) : current + " " + std::string(word);
                    if (!current.empty() && Renderer2D::MeasureString(candidate, font).x > maxWidth)
                    {
                        lines.push_back(std::move(current));
                        current = std::string(word);
                    }
                    else
                        current = std::move(candidate);
                }
                if (!current.empty())
                    lines.push_back(std::move(current));
                if (maxLines > 0 && lines.size() > maxLines)
                {
                    for (size_t i = maxLines; i < lines.size(); i++)
                        lines[maxLines - 1] += " " + lines[i];
                    lines.resize(maxLines);
                }
                for (size_t i = 0; i < lines.size(); i++)
                    line(lines[i], size, color, i + 1 < lines.size() ? 1.3f : after);
            };
            const glm::vec4 white(0.94f, 0.95f, 0.97f, 1.0f), grey(0.60f, 0.63f, 0.68f, 1.0f), red(0.96f, 0.50f, 0.46f, 1.0f), dim(0.45f, 0.48f, 0.53f, 1.0f);
            line(std::format("{} stopped unexpectedly", m_Product), 30.0f * scale, white, 1.7f);
            if (m_HasReport)
            {
                if (!m_Summary.Version.empty() || !m_Summary.Engine.empty())
                    line(std::format("{}{}{}", m_Summary.Version.empty() ? "" : "Version " + m_Summary.Version, !m_Summary.Version.empty() && !m_Summary.Engine.empty() ? "  |  " : "",
                                     m_Summary.Engine),
                         15.0f * scale, dim, 1.9f);
                // (A failed assert's reason is long: the expression, the message, file:line.)
                wrapped("Reason: " + m_Summary.Reason, 18.0f * scale, red, 3, 2.0f);
            }
            else
                line("The report could not be read.", 18.0f * scale, red, 2.0f);
            line("A report about the error was saved:", 17.0f * scale, white, 1.45f);
            line(m_Report.string(), 16.0f * scale, grey, 2.0f, true);
            line("Please send this file to the game's author - it helps to fix the error.", 15.0f * scale, grey, 1.9f);
            // The last things the game logged.
            if (m_HasReport && !m_Summary.LastLog.empty())
            {
                line("Last log lines:", 14.0f * scale, dim, 1.4f);
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
            // The editor's prefab links (PrefabLinkComponent) are not the game's: never read here.
            SceneSerializer::SetReadPrefabLinks(false);
            // The game's files do not change while it runs: a prefab file is looked at once (PrefabCache).
            PrefabCache::SetCheckForChanges(false);
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
        // --version: the game's version (the project given, or the exported game's next to the player) and the
        // engine's build - no window.
        if (commandLine.Version)
        {
            VersionInfo info;
            const std::filesystem::path project = FindProject(commandLine.Project.empty() ? nullptr : commandLine.Project.c_str());
            std::error_code ec;
            ProjectConfig config;
            if (!project.empty() && std::filesystem::is_regular_file(project, ec) &&
                ProjectSerializer::ConfigFromString(FileSystem::ReadTextFile(project), config, nullptr, nullptr))
                info = VersionInfo::ForConfig(config);
            std::fputs(info.Format("Zamin Player").c_str(), stdout);
            std::exit(0);
        }
        // The window about a crash report (the crash handler started this process): nothing else.
        if (!commandLine.CrashDialog.empty())
        {
            spec.Name = "Zamin Player (error)";
            spec.Window.Title = "Error";
            spec.Window.Width = 780;
            spec.Window.Height = 440;
            spec.Window.Resizable = false;
            spec.Window.VSync = true;
            return new CrashDialogApp(spec, std::filesystem::absolute(commandLine.CrashDialog));
        }
        for (const std::string& problem : commandLine.Errors)
            ZM_WARN("Command line: {}", problem);
        if (commandLine.Jobs)
            spec.JobWorkers = *commandLine.Jobs;

        std::filesystem::path project = FindProject(commandLine.Project.empty() ? nullptr : commandLine.Project.c_str());
        if (project.empty())
        {
            ZM_CRITICAL("Project not found. Usage: ZaminPlayer <Game.zproject> (all options: --help)");
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

        // The log levels: the project's (Project settings > Player > Log), ZAMIN_LOG_LEVEL over them and
        // --log-level over both - before the log file opens, so that it gets only its level's lines.
        const PlayerLogSettings& logging = config.Player.Logging;
        const LogLevelSettings levels = commandLine.LogLevels.Over(LogLevelOverride::FromEnvironment().Over(logging.Levels));
        levels.Apply();
        if (!levels.IsDefault())
            ZM_INFO("Log levels: {}", levels.Describe());

        // The log file (from here on every message goes there too; the ones before are written first):
        // --log-file, else ~/.local/share/<company>/<product>/logs/player-<date>.log (the last N, the
        // project's Log > Keep) + latest.log - unless the project turned the files off.
        const std::string product = config.Player.GetProductName(config.Name);
        const std::filesystem::path logDirectory = LogFile::GetDefaultDirectory(config.Player.CompanyName, product);
        if (!commandLine.NoLogFile && !logging.WriteFile && commandLine.LogFile.empty())
            ZM_CORE_INFO("The log file is off (Project settings > Player > Log) - the log is in the terminal only");
        if (!commandLine.NoLogFile && (logging.WriteFile || !commandLine.LogFile.empty()))
        {
            std::string command;
            for (int i = 1; i < args.Count; i++)
                command += std::string(i > 1 ? " " : "") + (args[i] ? args[i] : "");
            char started[32];
            const std::time_t now = std::time(nullptr);
            std::tm tm{};
            localtime_r(&now, &tm);
            std::strftime(started, sizeof(started), "%Y-%m-%d %H:%M:%S", &tm);
            const std::string header = std::format("# {} {} - Zamin Player\n# {}\n# Started: {}\n# Project: {}\n# Command line: {}\n", product,
                                                   config.Player.Version, BuildInfo::Get().Describe(), started, project.string(),
                                                   command.empty() ? "-" : command);
            std::string logError;
            const bool opened = !commandLine.LogFile.empty()
                                    ? LogFile::Open(commandLine.LogFile, header, &logError)
                                    : LogFile::OpenRotating({ logDirectory, "player", logging.KeepFiles, true, header }, &logError);
            if (opened)
                ZM_INFO("Log file: {}", LogFile::GetPath().string());
            else
                ZM_WARN("The log file could not be opened ({}) - the log is in the terminal only", logError);
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
            // (A performance test is a test too: no window that waits for someone.)
            crash.ShowDialog = !commandLine.NoCrashDialog && !commandLine.Benchmark.IsEnabled() &&
                               (!Application::IsAutomationRun() || std::getenv("ZAMIN_CRASH_DIALOG"));
            crash.DialogProgram = FileSystem::GetExecutablePath();
            crash.DialogArguments = { "--crash-dialog" };
            if (CrashHandler::Install(crash))
                ZM_INFO("Crash reports: {}", crash.ReportDirectory.string());
        }

        PlayerLaunchSettings launch = PlayerLaunchSettings::Resolve(config, saved, commandLine);
        for (const std::string& warning : launch.Warnings)
            ZM_WARN("{}", warning);
        spec.Window = launch.Window;
        return new ZaminPlayer(spec, project, std::move(commandLine), std::move(launch));
    }

}
