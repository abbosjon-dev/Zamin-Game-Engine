#pragma once

/// @file
/// @brief The application: the window, the main loop and the layer stack (editor, player, tools).

#include "Zamin/Core/Base.h"
#include "Zamin/Core/FramePacer.h"
#include "Zamin/Core/Layer.h"
#include "Zamin/Core/Profiler.h"
#include "Zamin/Core/Window.h"

#include <algorithm>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace Zamin {

    class ImGuiLayer;

    /// The process's command line (`main`'s argc / argv) as the application got it.
    struct ApplicationCommandLineArgs
    {
        int Count = 0;          ///< argc
        char** Args = nullptr;  ///< argv

        /// The argument at `index`, nullptr past the end.
        const char* operator[](int index) const { return index < Count ? Args[index] : nullptr; }
    };

    /// How an Application starts: what CreateApplication fills in before the constructor runs.
    struct ApplicationSpecification
    {
        std::string Name = "Zamin Application";  ///< logged at start-up; the window title when Window.Title is empty
        /// Made the current directory before anything else (empty: left as it is).
        std::filesystem::path WorkingDirectory;
        ApplicationCommandLineArgs CommandLineArgs;  ///< the process's arguments
        WindowProps Window;                          ///< the main window (size, title, mode, vsync ...)
        /// true: an ImGuiLayer is pushed as an overlay (the editor); the player runs without it.
        bool EnableImGui = true;
        /// The job system's worker threads (JobSystem::Init): -1 the default (hardware threads - 1),
        /// 0 none (every job on the main thread), N that many. `ZAMIN_JOBS=<n>` wins over it.
        int JobWorkers = -1;
    };

    /// The engine's application: one per process (Application::Get).
    ///
    /// The constructor creates the window and initializes the renderer, the audio engine and the job
    /// system (and the ImGui overlay when ApplicationSpecification::EnableImGui); the destructor saves
    /// SaveData and shuts them down again. Run() is the main loop - every frame: Time, the profiler,
    /// the main-thread queue (SubmitToMainThread), Input::Update, the cursor, AudioEngine::Update,
    /// Layer::OnUpdate of every layer (first to last), Layer::OnImGuiRender inside the ImGui frame, the
    /// automation hooks (`ZAMIN_MAX_FRAMES`, `ZAMIN_SCREENSHOT`), the buffer swap and the window's
    /// events, then the frame pacing. Window events go to OnEvent, which feeds Input and hands the
    /// event to the layers last to first until one handles it.
    ///
    /// A program implements CreateApplication (Zamin/Core/EntryPoint.h supplies `main`):
    /// @code
    /// class MyApp : public Zamin::Application
    /// {
    /// public:
    ///     explicit MyApp(const Zamin::ApplicationSpecification& spec) : Application(spec) { PushLayer(new MyLayer()); }
    /// };
    /// Zamin::Application* Zamin::CreateApplication(Zamin::ApplicationCommandLineArgs args)
    /// {
    ///     Zamin::ApplicationSpecification spec;
    ///     spec.Name = "My tool";
    ///     spec.CommandLineArgs = args;
    ///     return new MyApp(spec);
    /// }
    /// @endcode
    class Application
    {
    public:
        /// Creates the window and initializes the engine's systems (asserts that no other application exists).
        explicit Application(const ApplicationSpecification& specification);
        /// Saves SaveData, then shuts the job system, the audio engine and the renderer down; the layer
        /// stack detaches and deletes its layers.
        virtual ~Application();

        /// The main loop: runs until Close(), the window is closed or `ZAMIN_MAX_FRAMES` frames have
        /// passed; then Layer::OnRunEnded of every layer.
        void Run();
        /// Ends the main loop after the current frame.
        void Close();
        /// The process's exit code (main returns it after Run): 0 unless a layer sets one - the player's
        /// performance test (2: it could not run, 3: a limit was exceeded).
        void SetExitCode(int code) { m_ExitCode = code; }
        int GetExitCode() const { return m_ExitCode; }  ///< see SetExitCode
        /// The window's picture at the end of this frame (once the UI is drawn, before the buffers swap) into
        /// a PNG file - automation (the editor's smoke test); ZAMIN_SCREENSHOT is the last frame's.
        void RequestScreenshot(std::string path) { m_ScreenshotRequest = std::move(path); }

        /// Adds a layer below the overlays and calls its OnAttach. The stack owns it from now on.
        void PushLayer(Layer* layer);
        /// Adds a layer on top of everything (it gets events first) and calls its OnAttach.
        void PushOverlay(Layer* layer);

        /// A window event: Input sees every device event first, the application handles closing and
        /// resizing, then the layers get it from the top down until one sets Event::Handled.
        void OnEvent(Event& e);

        Window& GetWindow() { return *m_Window; }                    ///< the main window
        ImGuiLayer* GetImGuiLayer() { return m_ImGuiLayer; }         ///< nullptr without ImGui
        const ApplicationSpecification& GetSpecification() const { return m_Specification; }
        float GetFPS() const { return m_FPS; }                       ///< frames per second, averaged over half a second
        float GetFrameTime() const { return m_FrameTimeMs; }         ///< the average frame time of that half second (ms)
        /// The real time of each of the last frames (ms, not limited like the timestep): the
        /// editor viewport's FPS graph.
        const FrameTimeHistory& GetFrameTimeHistory() const { return m_FrameTimes; }

        /// Run a function on the main thread at the start of the next frame.
        void SubmitToMainThread(const std::function<void()>& fn);

        /// @name Frame pacing
        /// Frame pacing (the editor's idle throttling): the most frames per second (0 = no
        /// limit). A frame that ends early waits for the window's events for the rest of its time -
        /// any input ends the wait at once. Automation runs (ZAMIN_MAX_FRAMES) are never limited.
        /// @{
        void SetFrameRateLimit(float fps) { m_FrameRateLimit = fps; }  ///< 0: no limit
        float GetFrameRateLimit() const { return m_FrameRateLimit; }
        /// Seconds since the last event of the window (keys, mouse, scroll, resize, focus, dropped files).
        double GetSecondsSinceInput() const;
        bool IsWindowFocused() const { return m_WindowFocused; }  ///< as of the start of this frame
        /// How long the last frame waited for events (ms).
        float GetLastWaitMs() const { return m_LastWaitMs; }
        /// `ZAMIN_MAX_FRAMES` is set (and `ZAMIN_ALLOW_THROTTLE` is not): no frame rate limits, no pause
        /// in the background.
        static bool IsAutomationRun();
        /// @}

        /// @name The game's frame rate limit
        /// The game's frame rate limit (Project settings > Player, ZaminPlayer --fps): the most frames
        /// per second (0 = none). Unlike the editor's limit above, input does not end the wait - the
        /// frames keep their pace. With vsync a limit that divides the monitor's rate is held by vsync
        /// itself (2 blanks per frame for 30 FPS on 60 Hz), otherwise by a sleep and a short spin on a
        /// fixed schedule (FramePacingPlan, FramePacer). Automation runs are never limited.
        /// @{
        void SetTargetFrameRate(int fps) { m_TargetFrameRate = std::max(0, fps); m_RefreshCheckFrames = 0; }
        int GetTargetFrameRate() const { return m_TargetFrameRate; }  ///< 0: none
        /// How the frames are paced now (vsync blanks per frame, the sleep limiter's rate).
        const FramePacingPlan& GetFramePacingPlan() const { return m_PacingPlan; }
        /// false: while the window is not in front the layers are not updated (the game's time stands
        /// still, the audio output is paused - every sound keeps its place) and the loop only waits for
        /// events; the layers draw the paused picture (Layer::OnRenderPaused: the player dims the last
        /// frame and writes "Pauza") when it starts and now and then after it. The focus back: the game
        /// goes on where it was. Automation runs always run (`ZAMIN_FOCUS_LOSS_TEST=<frame>[:<frames>]` plays a
        /// focus loss from that frame on, for that many paused frames: screenshots, CI).
        void SetRunInBackground(bool run) { m_RunInBackground = run; }
        bool GetRunInBackground() const { return m_RunInBackground; }  ///< see SetRunInBackground
        /// The game is standing still now because its window lost the focus.
        bool IsPausedInBackground() const { return m_PausedInBackground; }
        /// The window is minimized: nothing is updated or drawn (the loop only waits for events) until it
        /// comes back - no frame time passes for the game meanwhile.
        bool IsMinimized() const { return m_Minimized; }
        /// @}

        /// @name Versions
        /// @{
        /// The engine's version ("0.1.0").
        static const char* GetEngineVersion();
        /// The game's version, company and product name (Project settings > Player of the active
        /// project; without one: "1.0.0", "", "Zamin"). Scripts can show / send them.
        static std::string GetVersion();
        static std::string GetCompanyName();
        static std::string GetProductName();
        /// @}

        static Application& Get() { return *s_Instance; }        ///< the running application (Exists first when unsure)
        static bool Exists() { return s_Instance != nullptr; }   ///< false in headless tools and the unit tests

    private:
        bool OnWindowClose(WindowCloseEvent& e);
        bool OnWindowResize(WindowResizeEvent& e);
        void ExecuteMainThreadQueue();
        void HandleAutomation();
        /// RunInBackground: pauses / resumes the game when its window loses / gets the focus.
        void SetPausedInBackground(bool paused);
        /// One frame of the pause: the paused picture (when due) and the wait for events.
        void RunPausedFrame();
        /// ZAMIN_FOCUS_LOSS_TEST holds a simulated focus loss now.
        bool IsFocusLossTestActive() const;
        /// The end of a frame: the target frame rate's wait (and vsync's interval for it).
        void PaceFrame();

    private:
        ApplicationSpecification m_Specification;
        Scope<Window> m_Window;
        ImGuiLayer* m_ImGuiLayer = nullptr;
        LayerStack m_LayerStack;
        bool m_Running = true;
        int m_ExitCode = 0;
        std::string m_ScreenshotRequest;
        bool m_Minimized = false;
        bool m_ZeroSize = false;   ///< the last resize made the window 0 x 0
        float m_LastFrameTime = 0.0f;
        float m_FPS = 0.0f;
        float m_FrameTimeMs = 0.0f;
        uint64_t m_FrameCount = 0;
        FrameTimeHistory m_FrameTimes;
        float m_FrameRateLimit = 0.0f;
        double m_LastInputTime = 0.0;
        bool m_WindowFocused = true;
        float m_LastWaitMs = 0.0f;
        int m_TargetFrameRate = 0;
        FramePacer m_FramePacer;
        FramePacingPlan m_PacingPlan;
        int m_RefreshRate = 0, m_RefreshCheckFrames = 0;
        bool m_RunInBackground = true;
        bool m_PausedInBackground = false;
        bool m_MutedForBackground = false; ///< the Master group was muted by the pause (not by the game)
        bool m_AudioPausedForBackground = false; ///< the audio output was paused by the pause
        bool m_PausedRedraw = false;       ///< the paused picture is drawn again (the pause began, a resize)
        double m_LastPausedRedraw = 0.0;
        /// ZAMIN_FOCUS_LOSS_TEST: the frame the simulated focus loss begins (-1 none), its paused frames.
        int64_t m_FocusLossTestFrame = -1;
        int m_FocusLossTestFrames = -1, m_FocusLossTestDone = 0;

        std::vector<std::function<void()>> m_MainThreadQueue;
        std::mutex m_MainThreadQueueMutex;

        static Application* s_Instance;
    };

    /// Implemented by the program (the editor, the player, a tool): makes the application `main` runs
    /// (Zamin/Core/EntryPoint.h).
    Application* CreateApplication(ApplicationCommandLineArgs args);

}
