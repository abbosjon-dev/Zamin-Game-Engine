#include "Zamin/Core/Application.h"
#include "Zamin/Asset/AsyncLoader.h"

#include "Zamin/ImGui/ImGuiLayer.h"
#include "Zamin/Core/BuildInfo.h"
#include "Zamin/Core/Profiler.h"
#include "Zamin/Core/Cursor.h"
#include "Zamin/Core/Input.h"
#include "Zamin/Core/JobSystem.h"
#include "Zamin/Core/SaveData.h"
#include "Zamin/Core/Time.h"
#include "Zamin/Core/Version.h"
#include "Zamin/Project/Project.h"
#include "Zamin/Renderer/Renderer.h"
#include "Zamin/Renderer/RenderCommand.h"
#include "Zamin/Audio/AudioEngine.h"
#include "Zamin/Utils/Image.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <format>
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

        ZM_CORE_INFO("{} ishga tushmoqda ({})", BuildInfo::Get().Describe(), m_Specification.Name);

        WindowProps props = m_Specification.Window;
        if (props.Title.empty())
            props.Title = m_Specification.Name;
        m_Window = Window::Create(props);
        m_Window->SetEventCallback(ZM_BIND_EVENT_FN(OnEvent));

        Renderer::Init();
        AudioEngine::Init();
        if (m_Specification.JobWorkers != 0 || std::getenv("ZAMIN_JOBS"))
            JobSystem::Init(m_Specification.JobWorkers > 0 ? (uint32_t)m_Specification.JobWorkers : 0u);
        else
            ZM_CORE_INFO("Vazifalar tizimi o'chiq: hamma ish asosiy oqimda");
        // ZAMIN_CAPTURE_FPS=N: every frame is 1 / N s of game time (headless tests progress the same way on
        // every computer, frame-by-frame recordings).
        if (const char* capture = std::getenv("ZAMIN_CAPTURE_FPS"); capture && *capture)
        {
            const int fps = std::atoi(capture);
            Time::SetCaptureFrameRate(fps);
            if (Time::GetCaptureFrameRate() > 0)
                ZM_CORE_INFO("O'yin vaqti qat'iy: har kadr 1/{} s (ZAMIN_CAPTURE_FPS)", Time::GetCaptureFrameRate());
        }

        if (m_Specification.EnableImGui)
        {
            m_ImGuiLayer = new ImGuiLayer();
            PushOverlay(m_ImGuiLayer);
        }
        // ZAMIN_FOCUS_LOSS_TEST=<frame>[:<frames>]: from that frame on the window counts as not focused (for
        // that many paused frames, else until the end): the pause in the background under automation.
        if (const char* test = std::getenv("ZAMIN_FOCUS_LOSS_TEST"))
        {
            m_FocusLossTestFrame = std::max<int64_t>(0, std::strtoll(test, nullptr, 10));
            if (const char* colon = std::strchr(test, ':'))
                m_FocusLossTestFrames = std::max(0, std::atoi(colon + 1));
        }
    }

    Application::~Application()
    {
        SaveData::Save(); // unsaved game data (PlayerPrefs) is never lost on exit
        AsyncLoader::WaitAll(); // background loads finished while the GPU is still there
        JobSystem::Shutdown();
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

    bool Application::IsAutomationRun()
    {
        // ZAMIN_ALLOW_THROTTLE=1: an automation run that still idles (to see / test the throttling).
        static const bool automation = std::getenv("ZAMIN_MAX_FRAMES") != nullptr && std::getenv("ZAMIN_ALLOW_THROTTLE") == nullptr;
        return automation;
    }

    const char* Application::GetEngineVersion() { return ZM_ENGINE_VERSION; }

    std::string Application::GetVersion() { return Project::GetPlayerSettings().Version; }

    std::string Application::GetCompanyName() { return Project::GetPlayerSettings().CompanyName; }

    std::string Application::GetProductName() { return Project::GetProductName(); }

    void Application::SetPausedInBackground(bool paused)
    {
        if (paused == m_PausedInBackground)
            return;
        m_PausedInBackground = paused;
        // The sound stands still with the game: the output pauses (every sound and the music keep their
        // place); where it cannot, the Master group is muted (unless the game muted it itself).
        if (paused)
        {
            m_AudioPausedForBackground = !AudioEngine::IsOutputPaused() && AudioEngine::SetOutputPaused(true);
            m_MutedForBackground = !m_AudioPausedForBackground && AudioEngine::HasGroup(AudioSettings::Master) &&
                                   !AudioEngine::IsGroupMuted(AudioSettings::Master);
            if (m_MutedForBackground)
                AudioEngine::SetGroupMuted(AudioSettings::Master, true);
            m_PausedRedraw = true; // the paused picture at once
        }
        else
        {
            if (m_AudioPausedForBackground)
                AudioEngine::SetOutputPaused(false);
            if (m_MutedForBackground)
                AudioEngine::SetGroupMuted(AudioSettings::Master, false);
            m_AudioPausedForBackground = m_MutedForBackground = false;
        }
        ZM_CORE_INFO("O'yin {}", paused ? "to'xtatildi (pauza): oyna fokusda emas (fonda ishlash o'chirilgan)" : "davom etmoqda: oyna yana fokusda");
    }

    bool Application::IsFocusLossTestActive() const
    {
        return m_FocusLossTestFrame >= 0 && (int64_t)m_FrameCount >= m_FocusLossTestFrame &&
               (m_FocusLossTestFrames < 0 || m_FocusLossTestDone < m_FocusLossTestFrames);
    }

    void Application::RunPausedFrame()
    {
        Cursor::Update(); // the system's cursor while the window is behind others
        // The paused picture: when the pause began (or the window was resized), then now and then (the
        // window can be uncovered behind others).
        const bool test = IsFocusLossTestActive();
        const double now = glfwGetTime();
        if (m_PausedRedraw || test || now - m_LastPausedRedraw > 0.5)
        {
            m_PausedRedraw = false;
            m_LastPausedRedraw = now;
            bool drew = false;
            for (Layer* layer : m_LayerStack)
                drew |= layer->OnRenderPaused();
            if (test)
            {
                // The simulated pause counts its frames (ZAMIN_MAX_FRAMES, ZAMIN_SCREENSHOT see them).
                m_FocusLossTestDone++;
                m_FrameCount++;
                HandleAutomation();
            }
            if (drew)
                m_Window->SwapBuffers();
        }
        m_Window->WaitEvents(test ? 0.01 : 0.1);
        m_LastFrameTime = (float)glfwGetTime(); // no long step when it comes back
    }

    void Application::PaceFrame()
    {
        // The monitor's refresh rate, looked up now and then (the window can move to another monitor).
        if (m_TargetFrameRate > 0 && --m_RefreshCheckFrames <= 0)
        {
            m_RefreshRate = m_Window->GetRefreshRate();
            m_RefreshCheckFrames = 120;
        }
        const FramePacingPlan plan = m_TargetFrameRate > 0 ? FramePacingPlan::Choose(m_Window->IsVSync(), m_TargetFrameRate, m_RefreshRate)
                                                           : FramePacingPlan();
        if (plan != m_PacingPlan)
        {
            m_PacingPlan = plan;
            m_Window->SetVSyncInterval(plan.VSyncInterval);
            if (m_TargetFrameRate > 0)
            {
                const std::string monitor = m_RefreshRate > 0 ? std::format("{} Hz monitor", m_RefreshRate) : std::string("monitor chastotasi noma'lum");
                if (plan.LimiterRate > 0)
                    ZM_CORE_INFO("Kadr chastotasi: {} FPS - kutish bilan cheklanadi (VSync {}, {})", m_TargetFrameRate,
                                 m_Window->IsVSync() ? "yoqiq" : "o'chiq", monitor);
                else if (plan.VSyncInterval > 1)
                    ZM_CORE_INFO("Kadr chastotasi: {} FPS - VSync har {} ta yangilanishda bir kadr ({})", m_TargetFrameRate, plan.VSyncInterval, monitor);
                else
                    ZM_CORE_INFO("Kadr chastotasi: {} FPS - VSync o'zi ushlab turadi ({})", m_TargetFrameRate, monitor);
            }
        }
        m_FramePacer.SetTargetFrameRate(plan.LimiterRate);
        if (plan.LimiterRate > 0)
            m_FramePacer.Wait();
    }

    double Application::GetSecondsSinceInput() const
    {
        return glfwGetTime() - m_LastInputTime;
    }

    void Application::OnEvent(Event& e)
    {
        m_LastInputTime = glfwGetTime(); // every event is the user (or the system) doing something
        EventDispatcher dispatcher(e);
        // The input system sees every device event, even ones a layer (e.g. ImGui) handles.
        dispatcher.Dispatch<KeyPressedEvent>([](KeyPressedEvent& ev) {
            Input::OnKeyEvent(ev.GetKeyCode(), true, ev.IsRepeat()); // repeats only reach text input
            return false;
        });
        dispatcher.Dispatch<KeyTypedEvent>([](KeyTypedEvent& ev) { Input::OnCharEvent((uint32_t)ev.GetKeyCode()); return false; });
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
            const double frameStart = glfwGetTime();
            // Focus changes wake the editor up like input does.
            if (bool focused = m_Window->IsFocused(); focused != m_WindowFocused)
            {
                m_WindowFocused = focused;
                m_LastInputTime = frameStart;
            }
            // Minimized (iconified, or a 0 x 0 framebuffer): nothing to draw - the layers wait (no update, no
            // swap, no busy loop) until the window comes back; the game's time does not jump then.
            const bool minimized = m_ZeroSize || m_Window->IsMinimized();
            if (minimized != m_Minimized)
            {
                m_Minimized = minimized;
                ZM_CORE_INFO("{}", minimized ? "Oyna kichraytirildi: yangilash va chizish to'xtatildi" : "Oyna tiklandi: yangilash va chizish davom etmoqda");
            }
            if (m_Minimized)
            {
                ExecuteMainThreadQueue();
                Cursor::Update();
                m_Window->WaitEvents(0.1);
                m_LastFrameTime = (float)glfwGetTime();
                continue;
            }
            // A game that does not run in the background stands still while its window is behind others.
            const bool focusLossTest = IsFocusLossTestActive();
            SetPausedInBackground(!m_RunInBackground && ((!m_WindowFocused && !IsAutomationRun()) || focusLossTest));
            if (m_PausedInBackground)
            {
                RunPausedFrame();
                continue;
            }
            float time = (float)frameStart;
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
            // Huge steps after stalls (e.g. a window drag) count as the project's longest frame
            // (Project settings > Time; 0.1 s without one) - or every frame is exactly 1 / N s
            // (Time::SetCaptureFrameRate, ZAMIN_CAPTURE_FPS: recordings, tests).
            Timestep timestep = Time::ComputeFrameDelta(frameSeconds);
            Time::OnFrame(timestep);

            Profiler::BeginFrame();
            JobSystem::EndFrame();
            ExecuteMainThreadQueue();
            // Background loads: what the workers decoded goes onto the GPU (a few ms a frame).
            if (AsyncLoader::GetPendingCount() || AsyncLoader::GetPendingUploadCount())
            {
                ZM_PROFILE_SCOPE("Fon yuklashlari");
                AsyncLoader::Update(4.0);
            }
            // Snapshot keyboard / mouse / gamepads once per frame (JustPressed, actions, axes).
            Input::Update();
            // The game's cursor (hidden / locked / its picture) as its wishes, the focus and a pause say.
            Cursor::Update();
            // Music fades, level meters, finished sounds (also while minimized: fades go on).
            {
                ZM_PROFILE_SCOPE("Audio (mixer)");
                AudioEngine::Update(timestep);
            }

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
            // The game's frame rate limit (Project settings > Player, ZaminPlayer --fps) with vsync.
            if (m_Running && !IsAutomationRun())
                PaceFrame();
            // A limited frame rate (the editor idling): the rest of the frame's time waiting for events.
            m_LastWaitMs = 0.0f;
            if (m_FrameRateLimit > 0.0f && m_Running && !IsAutomationRun())
            {
                const double wait = 1.0 / (double)m_FrameRateLimit - (glfwGetTime() - frameStart);
                if (wait > 0.0)
                {
                    const double before = glfwGetTime();
                    m_Window->WaitEvents(std::min(wait, 1.0));
                    m_LastWaitMs = (float)((glfwGetTime() - before) * 1000.0);
                }
            }
        }
        // However the loop ended: the layers can still use everything (and set the exit code).
        for (Layer* layer : m_LayerStack)
            layer->OnRunEnded();
    }

    // Automation hooks for CI / headless testing:
    //   ZAMIN_MAX_FRAMES=N           close the app after N frames (the frame rate is never limited then,
    //                                unless ZAMIN_ALLOW_THROTTLE=1)
    //   ZAMIN_SCREENSHOT=path.png    save the back buffer at the last frame
    void Application::HandleAutomation()
    {
        if (!m_ScreenshotRequest.empty())
        {
            const std::string path = std::move(m_ScreenshotRequest);
            m_ScreenshotRequest.clear();
            const uint32_t w = m_Window->GetFramebufferWidth(), h = m_Window->GetFramebufferHeight();
            std::vector<uint8_t> pixels = RenderCommand::ReadDefaultFramebuffer(w, h);
            if (Image::WritePNG(path, w, h, 4, pixels.data(), true))
                ZM_CORE_INFO("Screenshot saqlandi: {}", path);
        }
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
        // (A 0 x 0 window - minimized on some systems - is handled by the loop.)
        m_ZeroSize = e.GetWidth() == 0 || e.GetHeight() == 0 || m_Window->GetFramebufferWidth() == 0 || m_Window->GetFramebufferHeight() == 0;
        if (m_ZeroSize)
            return false;
        Renderer::OnWindowResize(m_Window->GetFramebufferWidth(), m_Window->GetFramebufferHeight());
        m_PausedRedraw = true; // (paused in the background: the picture again at the new size)
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
