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

        std::vector<std::function<void()>> m_MainThreadQueue;
        std::mutex m_MainThreadQueueMutex;

        static Application* s_Instance;
    };

    // Implemented by client (Editor / Runtime / game).
    Application* CreateApplication(ApplicationCommandLineArgs args);

}
