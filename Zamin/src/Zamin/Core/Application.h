#pragma once

#include "Zamin/Core/Base.h"
#include "Zamin/Core/FramePacer.h"
#include "Zamin/Core/Layer.h"
#include "Zamin/Core/Profiler.h"
#include "Zamin/Core/Window.h"

#include <algorithm>
#include <filesystem>
#include <functional>
#include <mutex>
#include <vector>

namespace Zamin {

    class ImGuiLayer;

    struct ApplicationCommandLineArgs
    {
        int Count = 0;
        char** Args = nullptr;

        const char* operator[](int index) const { return index < Count ? Args[index] : nullptr; }
    };

    struct ApplicationSpecification
    {
        std::string Name = "Zamin Application";
        std::filesystem::path WorkingDirectory;
        ApplicationCommandLineArgs CommandLineArgs;
        WindowProps Window;
        bool EnableImGui = true;
    };

    class Application
    {
    public:
        explicit Application(const ApplicationSpecification& specification);
        virtual ~Application();

        void Run();
        void Close();

        void PushLayer(Layer* layer);
        void PushOverlay(Layer* layer);

        void OnEvent(Event& e);

        Window& GetWindow() { return *m_Window; }
        ImGuiLayer* GetImGuiLayer() { return m_ImGuiLayer; }
        const ApplicationSpecification& GetSpecification() const { return m_Specification; }
        float GetFPS() const { return m_FPS; }
        float GetFrameTime() const { return m_FrameTimeMs; }
        // The real time of each of the last frames (ms, not limited like the timestep): the
        // editor viewport's FPS graph.
        const FrameTimeHistory& GetFrameTimeHistory() const { return m_FrameTimes; }

        // Run a function on the main thread at the start of the next frame.
        void SubmitToMainThread(const std::function<void()>& fn);

        // ---- frame pacing (the editor's idle throttling): the most frames per second (0 = no
        // limit). A frame that ends early waits for the window's events for the rest of its time -
        // any input ends the wait at once. Automation runs (ZAMIN_MAX_FRAMES) are never limited.
        void SetFrameRateLimit(float fps) { m_FrameRateLimit = fps; }
        float GetFrameRateLimit() const { return m_FrameRateLimit; }
        // Seconds since the last event of the window (keys, mouse, scroll, resize, focus, dropped files).
        double GetSecondsSinceInput() const;
        bool IsWindowFocused() const { return m_WindowFocused; }
        // How long the last frame waited for events (ms).
        float GetLastWaitMs() const { return m_LastWaitMs; }
        static bool IsAutomationRun();

        // ---- the game's frame rate limit (Project settings > Player, ZaminPlayer --fps): the most frames
        // per second (0 = none). Unlike the editor's limit above, input does not end the wait - the
        // frames keep their pace. With vsync a limit that divides the monitor's rate is held by vsync
        // itself (2 blanks per frame for 30 FPS on 60 Hz), otherwise by a sleep and a short spin on a
        // fixed schedule (FramePacingPlan, FramePacer). Automation runs are never limited.
        void SetTargetFrameRate(int fps) { m_TargetFrameRate = std::max(0, fps); m_RefreshCheckFrames = 0; }
        int GetTargetFrameRate() const { return m_TargetFrameRate; }
        // How the frames are paced now (vsync blanks per frame, the sleep limiter's rate).
        const FramePacingPlan& GetFramePacingPlan() const { return m_PacingPlan; }
        // false: while the window is not in front the layers are not updated (the game's time stands
        // still, the audio output is paused - every sound keeps its place) and the loop only waits for
        // events; the layers draw the paused picture (Layer::OnRenderPaused: the player dims the last
        // frame and writes "Pauza") when it starts and now and then after it. The focus back: the game
        // goes on where it was. Automation runs always run (ZAMIN_FOCUS_LOSS_TEST=<frame>[:<frames>] plays a
        // focus loss from that frame on, for that many paused frames: screenshots, CI).
        void SetRunInBackground(bool run) { m_RunInBackground = run; }
        bool GetRunInBackground() const { return m_RunInBackground; }
        // The game is standing still now because its window lost the focus.
        bool IsPausedInBackground() const { return m_PausedInBackground; }
        // The window is minimized: nothing is updated or drawn (the loop only waits for events) until it
        // comes back - no frame time passes for the game meanwhile.
        bool IsMinimized() const { return m_Minimized; }

        // ---- versions
        // The engine's version ("0.1.0").
        static const char* GetEngineVersion();
        // The game's version, company and product name (Project settings > Player of the active
        // project; without one: "1.0.0", "", "Zamin"). Scripts can show / send them.
        static std::string GetVersion();
        static std::string GetCompanyName();
        static std::string GetProductName();

        static Application& Get() { return *s_Instance; }
        static bool Exists() { return s_Instance != nullptr; }

    private:
        bool OnWindowClose(WindowCloseEvent& e);
        bool OnWindowResize(WindowResizeEvent& e);
        void ExecuteMainThreadQueue();
        void HandleAutomation();
        // RunInBackground: pauses / resumes the game when its window loses / gets the focus.
        void SetPausedInBackground(bool paused);
        // One frame of the pause: the paused picture (when due) and the wait for events.
        void RunPausedFrame();
        // ZAMIN_FOCUS_LOSS_TEST holds a simulated focus loss now.
        bool IsFocusLossTestActive() const;
        // The end of a frame: the target frame rate's wait (and vsync's interval for it).
        void PaceFrame();

    private:
        ApplicationSpecification m_Specification;
        Scope<Window> m_Window;
        ImGuiLayer* m_ImGuiLayer = nullptr;
        LayerStack m_LayerStack;
        bool m_Running = true;
        bool m_Minimized = false;
        bool m_ZeroSize = false;   // the last resize made the window 0 x 0
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
        bool m_MutedForBackground = false; // the Master group was muted by the pause (not by the game)
        bool m_AudioPausedForBackground = false; // the audio output was paused by the pause
        bool m_PausedRedraw = false;       // the paused picture is drawn again (the pause began, a resize)
        double m_LastPausedRedraw = 0.0;
        // ZAMIN_FOCUS_LOSS_TEST: the frame the simulated focus loss begins (-1 none), its paused frames.
        int64_t m_FocusLossTestFrame = -1;
        int m_FocusLossTestFrames = -1, m_FocusLossTestDone = 0;

        std::vector<std::function<void()>> m_MainThreadQueue;
        std::mutex m_MainThreadQueueMutex;

        static Application* s_Instance;
    };

    // Implemented by client (Editor / Runtime / game).
    Application* CreateApplication(ApplicationCommandLineArgs args);

}
