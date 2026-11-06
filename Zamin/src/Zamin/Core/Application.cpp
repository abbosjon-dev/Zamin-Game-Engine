#include "Zamin/Core/Application.h"

#include "Zamin/ImGui/ImGuiLayer.h"
#include "Zamin/Core/Profiler.h"
#include "Zamin/Core/Input.h"
#include "Zamin/Core/SaveData.h"
#include "Zamin/Renderer/Renderer.h"
#include "Zamin/Renderer/RenderCommand.h"
#include "Zamin/Audio/AudioEngine.h"
#include "Zamin/Utils/Image.h"

#include <GLFW/glfw3.h>

#include <cstdlib>
#include <string>

namespace Zamin {

    Application* Application::s_Instance = nullptr;

    Application::Application(const ApplicationSpecification& specification)
        : m_Specification(specification)
    {
        ZM_CORE_ASSERT(!s_Instance, "Application already exists!");
        s_Instance = this;

        if (!m_Specification.WorkingDirectory.empty())
            std::filesystem::current_path(m_Specification.WorkingDirectory);

        ZM_CORE_INFO("Zamin Engine v0.1.0 ishga tushmoqda ({})", m_Specification.Name);

        WindowProps props = m_Specification.Window;
        if (props.Title.empty())
            props.Title = m_Specification.Name;
        m_Window = Window::Create(props);
        m_Window->SetEventCallback(ZM_BIND_EVENT_FN(OnEvent));

        Renderer::Init();
        AudioEngine::Init();

        if (m_Specification.EnableImGui)
        {
            m_ImGuiLayer = new ImGuiLayer();
            PushOverlay(m_ImGuiLayer);
        }
    }

    Application::~Application()
    {
        SaveData::Save(); // unsaved game data (PlayerPrefs) is never lost on exit
        AudioEngine::Shutdown();
        Renderer::Shutdown();
    }

    void Application::PushLayer(Layer* layer)
    {
        m_LayerStack.PushLayer(layer);
        layer->OnAttach();
    }

    void Application::PushOverlay(Layer* layer)
    {
        m_LayerStack.PushOverlay(layer);
        layer->OnAttach();
    }

    void Application::Close() { m_Running = false; }

    void Application::SubmitToMainThread(const std::function<void()>& fn)
    {
        std::scoped_lock lock(m_MainThreadQueueMutex);
        m_MainThreadQueue.emplace_back(fn);
    }

    void Application::OnEvent(Event& e)
    {
        EventDispatcher dispatcher(e);
        // The input system sees every device event, even ones a layer (e.g. ImGui) handles.
        dispatcher.Dispatch<KeyPressedEvent>([](KeyPressedEvent& ev) {
            if (!ev.IsRepeat()) Input::OnKeyEvent(ev.GetKeyCode(), true);
            return false;
        });
        dispatcher.Dispatch<KeyReleasedEvent>([](KeyReleasedEvent& ev) { Input::OnKeyEvent(ev.GetKeyCode(), false); return false; });
        dispatcher.Dispatch<MouseButtonPressedEvent>([](MouseButtonPressedEvent& ev) { Input::OnMouseButtonEvent(ev.GetMouseButton(), true); return false; });
        dispatcher.Dispatch<MouseButtonReleasedEvent>([](MouseButtonReleasedEvent& ev) { Input::OnMouseButtonEvent(ev.GetMouseButton(), false); return false; });
        dispatcher.Dispatch<MouseMovedEvent>([](MouseMovedEvent& ev) { Input::OnMouseMovedEvent(ev.GetX(), ev.GetY()); return false; });
        dispatcher.Dispatch<MouseScrolledEvent>([](MouseScrolledEvent& ev) { Input::OnMouseScrolledEvent(ev.GetXOffset(), ev.GetYOffset()); return false; });
        dispatcher.Dispatch<WindowCloseEvent>(ZM_BIND_EVENT_FN(OnWindowClose));
        dispatcher.Dispatch<WindowResizeEvent>(ZM_BIND_EVENT_FN(OnWindowResize));

        for (auto it = m_LayerStack.rbegin(); it != m_LayerStack.rend(); ++it)
        {
            if (e.Handled)
                break;
            (*it)->OnEvent(e);
        }
    }

    void Application::Run()
    {
        float fpsTimer = 0.0f;
        int fpsFrames = 0;

        while (m_Running)
        {
            float time = (float)glfwGetTime();
            const float frameSeconds = time - m_LastFrameTime;
            m_LastFrameTime = time;
            // The FPS, its frame time and the frame time history all count the real time of the
            // frames (not the limited timestep below), so they agree on slow frames too. The first
            // frame (the time since start-up) is left out.
            if (m_FrameCount > 0)
            {
                m_FrameTimes.Push(frameSeconds * 1000.0f);
                fpsTimer += frameSeconds;
                fpsFrames++;
                if (fpsTimer >= 0.5f)
                {
                    m_FPS = fpsFrames / fpsTimer;
                    m_FrameTimeMs = 1000.0f * fpsTimer / fpsFrames;
                    fpsTimer = 0.0f;
                    fpsFrames = 0;
                }
            }
            Timestep timestep = frameSeconds;
            if (timestep > 0.1f) // avoid huge steps after stalls (e.g. window drag)
                timestep = 0.1f;

            Profiler::BeginFrame();
            ExecuteMainThreadQueue();
            // Snapshot keyboard / mouse / gamepads once per frame (JustPressed, actions, axes).
            Input::Update();
            // Music fades, level meters, finished sounds (also while minimized: fades go on).
            AudioEngine::Update(timestep);

            if (!m_Minimized)
            {
                ZM_PROFILE_SCOPE("Kadr");
                {
                    ZM_PROFILE_SCOPE("Layer'lar (update)");
                    for (Layer* layer : m_LayerStack)
                        layer->OnUpdate(timestep);
                }

                if (m_ImGuiLayer)
                {
                    ZM_PROFILE_SCOPE("UI (ImGui)");
                    m_ImGuiLayer->Begin();
                    {
                        ZM_PROFILE_SCOPE("Layer'lar (UI)");
                        for (Layer* layer : m_LayerStack)
                            layer->OnImGuiRender();
                    }
                    ZM_PROFILE_SCOPE("ImGui chizish");
                    m_ImGuiLayer->End();
                }
            }

            m_FrameCount++;
            HandleAutomation();
            {
                // Swapping the buffers (waits for vsync) and the window's events.
                ZM_PROFILE_SCOPE("Oyna (swap, hodisalar)");
                m_Window->OnUpdate();
            }
        }
    }

    // Automation hooks for CI / headless testing:
    //   ZAMIN_MAX_FRAMES=N           close the app after N frames
    //   ZAMIN_SCREENSHOT=path.png    save the back buffer at the last frame
    void Application::HandleAutomation()
    {
        static const char* maxFramesEnv = std::getenv("ZAMIN_MAX_FRAMES");
        if (!maxFramesEnv)
            return;
        static const uint64_t maxFrames = std::strtoull(maxFramesEnv, nullptr, 10);
        if (m_FrameCount < maxFrames)
            return;

        if (const char* shot = std::getenv("ZAMIN_SCREENSHOT"))
        {
            uint32_t w = m_Window->GetFramebufferWidth(), h = m_Window->GetFramebufferHeight();
            std::vector<uint8_t> pixels = RenderCommand::ReadDefaultFramebuffer(w, h);
            if (Image::WritePNG(shot, w, h, 4, pixels.data(), true))
                ZM_CORE_INFO("Screenshot saqlandi: {}", shot);
        }
        m_Running = false;
    }

    bool Application::OnWindowClose(WindowCloseEvent&)
    {
        m_Running = false;
        return true;
    }

    bool Application::OnWindowResize(WindowResizeEvent& e)
    {
        if (e.GetWidth() == 0 || e.GetHeight() == 0)
        {
            m_Minimized = true;
            return false;
        }
        m_Minimized = false;
        Renderer::OnWindowResize(m_Window->GetFramebufferWidth(), m_Window->GetFramebufferHeight());
        return false;
    }

    void Application::ExecuteMainThreadQueue()
    {
        std::vector<std::function<void()>> queue;
        {
            std::scoped_lock lock(m_MainThreadQueueMutex);
            queue.swap(m_MainThreadQueue);
        }
        for (auto& fn : queue)
            fn();
    }

}
