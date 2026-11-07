#pragma once

#include "Zamin/Core/Base.h"
#include "Zamin/Core/Layer.h"
#include "Zamin/Core/Profiler.h"
#include "Zamin/Core/Window.h"

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

        static Application& Get() { return *s_Instance; }
        static bool Exists() { return s_Instance != nullptr; }

    private:
        bool OnWindowClose(WindowCloseEvent& e);
        bool OnWindowResize(WindowResizeEvent& e);
        void ExecuteMainThreadQueue();
        void HandleAutomation();

    private:
        ApplicationSpecification m_Specification;
        Scope<Window> m_Window;
        ImGuiLayer* m_ImGuiLayer = nullptr;
        LayerStack m_LayerStack;
        bool m_Running = true;
        bool m_Minimized = false;
        float m_LastFrameTime = 0.0f;
        float m_FPS = 0.0f;
        float m_FrameTimeMs = 0.0f;
        uint64_t m_FrameCount = 0;
        FrameTimeHistory m_FrameTimes;
        float m_FrameRateLimit = 0.0f;
        double m_LastInputTime = 0.0;
        bool m_WindowFocused = true;
        float m_LastWaitMs = 0.0f;

        std::vector<std::function<void()>> m_MainThreadQueue;
        std::mutex m_MainThreadQueueMutex;

        static Application* s_Instance;
    };

    // Implemented by client (Editor / Runtime / game).
    Application* CreateApplication(ApplicationCommandLineArgs args);

}
