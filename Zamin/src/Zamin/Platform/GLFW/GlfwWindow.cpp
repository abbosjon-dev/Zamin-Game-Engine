#include "Zamin/Platform/GLFW/GlfwWindow.h"

#include <algorithm>

#include <GLFW/glfw3.h>

namespace Zamin {

    static uint8_t s_GLFWWindowCount = 0;

    static void GLFWErrorCallback(int error, const char* description)
    {
        // Reading an empty / non-text clipboard (editor paste menus) is not an error.
        if (error == GLFW_FORMAT_UNAVAILABLE)
            return;
        ZM_CORE_ERROR("GLFW xatosi ({}): {}", error, description);
    }

    Scope<Window> Window::Create(const WindowProps& props) { return CreateScope<GlfwWindow>(props); }

    GlfwWindow::GlfwWindow(const WindowProps& props)
    {
        m_Data.Title = props.Title;
        m_Data.Width = props.Width;
        m_Data.Height = props.Height;

        if (s_GLFWWindowCount == 0)
        {
            int ok = glfwInit();
            ZM_CORE_ASSERT(ok, "GLFW ishga tushmadi");
            if (!ok)
            {
                ZM_CORE_CRITICAL("GLFW ishga tushmadi! Displey (X11/Wayland) mavjudligini tekshiring.");
                std::exit(1);
            }
            glfwSetErrorCallback(GLFWErrorCallback);
        }

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
        if (props.Maximized)
            glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);
#ifndef NDEBUG
        glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
#endif

        glfwWindowHint(GLFW_RESIZABLE, props.Resizable ? GLFW_TRUE : GLFW_FALSE);
        m_Resizable = props.Resizable;
        m_WindowedWidth = (int)std::max(1u, props.Width);
        m_WindowedHeight = (int)std::max(1u, props.Height);
        m_FullscreenMode = props.Mode != WindowMode::Windowed ? props.Mode
                         : props.FullscreenMode != WindowMode::Windowed ? props.FullscreenMode : WindowMode::Fullscreen;

        // Fullscreen: the monitor's own video mode; borderless: an undecorated window of its size.
        GLFWmonitor* primary = props.Mode != WindowMode::Windowed ? glfwGetPrimaryMonitor() : nullptr;
        const GLFWvidmode* video = primary ? glfwGetVideoMode(primary) : nullptr;
        WindowMode mode = video ? props.Mode : WindowMode::Windowed;
        if (props.Mode != WindowMode::Windowed && !video)
            ZM_CORE_WARN("To'liq ekran: monitor topilmadi - oynali rejim");
        if (video)
        {
            m_Data.Width = (uint32_t)video->width;
            m_Data.Height = (uint32_t)video->height;
        }
        if (mode == WindowMode::Borderless)
            glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);

        m_Window = glfwCreateWindow((int)m_Data.Width, (int)m_Data.Height, m_Data.Title.c_str(),
                                    mode == WindowMode::Fullscreen ? primary : nullptr, nullptr);
        glfwDefaultWindowHints();
        if (!m_Window)
        {
            ZM_CORE_CRITICAL("Oyna yaratilmadi (OpenGL 4.1 core qo'llab-quvvatlanadimi?)");
            std::exit(1);
        }
        ++s_GLFWWindowCount;
        m_Mode = mode;
        // (Wayland places windows itself: GLFW reports positions as unavailable there.)
        const bool canPlace = glfwGetPlatform() != GLFW_PLATFORM_WAYLAND;
        if (canPlace && mode == WindowMode::Borderless)
        {
            int mx = 0, my = 0;
            glfwGetMonitorPos(primary, &mx, &my);
            glfwSetWindowPos(m_Window, mx, my);
        }
        else if (canPlace && mode == WindowMode::Windowed && !props.Maximized)
        {
            // In the middle of the primary monitor's work area (where it goes back to from fullscreen too).
            if (GLFWmonitor* monitor = glfwGetPrimaryMonitor())
            {
                int wx = 0, wy = 0, ww = 0, wh = 0;
                glfwGetMonitorWorkarea(monitor, &wx, &wy, &ww, &wh);
                if (ww > 0 && wh > 0)
                {
                    m_WindowedX = wx + std::max(0, (ww - m_WindowedWidth) / 2);
                    m_WindowedY = wy + std::max(0, (wh - m_WindowedHeight) / 2);
                    glfwSetWindowPos(m_Window, m_WindowedX, m_WindowedY);
                }
            }
        }
        ZM_CORE_INFO("Oyna yaratildi: {} ({}x{}, {})", m_Data.Title, m_Data.Width, m_Data.Height,
                     mode == WindowMode::Fullscreen ? "to'liq ekran" : mode == WindowMode::Borderless ? "chegarasiz to'liq ekran" : "oynali");

        int w, h;
        glfwGetWindowSize(m_Window, &w, &h);
        m_Data.Width = (uint32_t)w;
        m_Data.Height = (uint32_t)h;

        m_Context = GraphicsContext::Create(m_Window);
        m_Context->Init();

        glfwSetWindowUserPointer(m_Window, &m_Data);
        SetVSync(props.VSync);

        // ---- callbacks ----
        glfwSetWindowSizeCallback(m_Window, [](GLFWwindow* window, int width, int height) {
            WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
            data.Width = width;
            data.Height = height;
            WindowResizeEvent event(width, height);
            if (data.EventCallback) data.EventCallback(event);
        });

        glfwSetWindowCloseCallback(m_Window, [](GLFWwindow* window) {
            WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
            WindowCloseEvent event;
            if (data.EventCallback) data.EventCallback(event);
        });

        glfwSetKeyCallback(m_Window, [](GLFWwindow* window, int key, int, int action, int) {
            if (key < 0) return;
            WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
            if (!data.EventCallback) return;
            switch (action)
            {
                case GLFW_PRESS:   { KeyPressedEvent e((Key)key, false); data.EventCallback(e); break; }
                case GLFW_RELEASE: { KeyReleasedEvent e((Key)key);       data.EventCallback(e); break; }
                case GLFW_REPEAT:  { KeyPressedEvent e((Key)key, true);  data.EventCallback(e); break; }
            }
        });

        glfwSetCharCallback(m_Window, [](GLFWwindow* window, unsigned int keycode) {
            WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
            KeyTypedEvent event((Key)keycode);
            if (data.EventCallback) data.EventCallback(event);
        });

        glfwSetMouseButtonCallback(m_Window, [](GLFWwindow* window, int button, int action, int) {
            WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
            if (!data.EventCallback) return;
            if (action == GLFW_PRESS) { MouseButtonPressedEvent e((MouseButton)button); data.EventCallback(e); }
            else if (action == GLFW_RELEASE) { MouseButtonReleasedEvent e((MouseButton)button); data.EventCallback(e); }
        });

        glfwSetScrollCallback(m_Window, [](GLFWwindow* window, double xOffset, double yOffset) {
            WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
            MouseScrolledEvent event((float)xOffset, (float)yOffset);
            if (data.EventCallback) data.EventCallback(event);
        });

        glfwSetCursorPosCallback(m_Window, [](GLFWwindow* window, double x, double y) {
            WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
            MouseMovedEvent event((float)x, (float)y);
            if (data.EventCallback) data.EventCallback(event);
        });

        glfwSetDropCallback(m_Window, [](GLFWwindow* window, int count, const char** paths) {
            WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
            std::vector<std::string> list(paths, paths + count);
            WindowDropEvent event(std::move(list));
            if (data.EventCallback) data.EventCallback(event);
        });
    }

    GlfwWindow::~GlfwWindow()
    {
        glfwDestroyWindow(m_Window);
        if (--s_GLFWWindowCount == 0)
            glfwTerminate();
    }

    void GlfwWindow::OnUpdate()
    {
        glfwPollEvents();
        m_Context->SwapBuffers();
    }

    bool GlfwWindow::IsFocused() const
    {
        return glfwGetWindowAttrib(m_Window, GLFW_FOCUSED) != 0;
    }

    void GlfwWindow::WaitEvents(double seconds)
    {
        if (seconds > 0.0)
            glfwWaitEventsTimeout(seconds);
    }

    uint32_t GlfwWindow::GetFramebufferWidth() const
    {
        int w, h;
        glfwGetFramebufferSize(m_Window, &w, &h);
        return (uint32_t)w;
    }

    uint32_t GlfwWindow::GetFramebufferHeight() const
    {
        int w, h;
        glfwGetFramebufferSize(m_Window, &w, &h);
        return (uint32_t)h;
    }

    float GlfwWindow::GetContentScale() const
    {
        float x = 1.0f, y = 1.0f;
        glfwGetWindowContentScale(m_Window, &x, &y);
        return x;
    }

    void GlfwWindow::SetVSync(bool enabled)
    {
        glfwSwapInterval(enabled ? 1 : 0);
        m_Data.VSync = enabled;
    }

    void GlfwWindow::SetFullscreen(bool fullscreen)
    {
        SetWindowMode(fullscreen ? m_FullscreenMode : WindowMode::Windowed);
    }

    void GlfwWindow::SetWindowMode(WindowMode mode)
    {
        if (mode == m_Mode)
            return;
        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* video = monitor ? glfwGetVideoMode(monitor) : nullptr;
        if (mode != WindowMode::Windowed && !video)
        {
            ZM_CORE_WARN("To'liq ekran: monitor topilmadi");
            return;
        }
        // Leaving the window: where it was, to come back to.
        if (m_Mode == WindowMode::Windowed)
        {
            if (glfwGetPlatform() != GLFW_PLATFORM_WAYLAND)
                glfwGetWindowPos(m_Window, &m_WindowedX, &m_WindowedY);
            glfwGetWindowSize(m_Window, &m_WindowedWidth, &m_WindowedHeight);
        }
        switch (mode)
        {
            case WindowMode::Windowed:
                glfwSetWindowAttrib(m_Window, GLFW_DECORATED, GLFW_TRUE);
                glfwSetWindowMonitor(m_Window, nullptr, m_WindowedX, m_WindowedY, std::max(1, m_WindowedWidth), std::max(1, m_WindowedHeight), 0);
                break;
            case WindowMode::Fullscreen:
                glfwSetWindowAttrib(m_Window, GLFW_DECORATED, GLFW_TRUE);
                glfwSetWindowMonitor(m_Window, monitor, 0, 0, video->width, video->height, video->refreshRate);
                break;
            case WindowMode::Borderless:
            {
                int mx = 0, my = 0;
                glfwGetMonitorPos(monitor, &mx, &my); // (0, 0) on Wayland, where the compositor places it
                if (glfwGetWindowMonitor(m_Window))
                    glfwSetWindowMonitor(m_Window, nullptr, mx, my, video->width, video->height, 0);
                glfwSetWindowAttrib(m_Window, GLFW_DECORATED, GLFW_FALSE);
                glfwSetWindowMonitor(m_Window, nullptr, mx, my, video->width, video->height, 0);
                break;
            }
        }
        m_Mode = mode;
        if (mode != WindowMode::Windowed)
            m_FullscreenMode = mode;
        SetVSync(m_Data.VSync); // some drivers forget the swap interval with the mode
        ZM_CORE_INFO("Oyna: {}", mode == WindowMode::Fullscreen ? "to'liq ekran" : mode == WindowMode::Borderless ? "chegarasiz to'liq ekran" : "oynali rejim");
    }

    void GlfwWindow::SetResizable(bool resizable)
    {
        m_Resizable = resizable;
        glfwSetWindowAttrib(m_Window, GLFW_RESIZABLE, resizable ? GLFW_TRUE : GLFW_FALSE);
    }

    void GlfwWindow::SetWindowedSize(uint32_t width, uint32_t height)
    {
        m_WindowedWidth = (int)std::max(1u, width);
        m_WindowedHeight = (int)std::max(1u, height);
        if (m_Mode == WindowMode::Windowed)
            glfwSetWindowSize(m_Window, m_WindowedWidth, m_WindowedHeight);
    }

    void GlfwWindow::SetIcon(const std::vector<WindowIconImage>& images)
    {
        // Wayland has no window icons (the desktop file's icon is used): GLFW would report an error.
        if (glfwGetPlatform() == GLFW_PLATFORM_WAYLAND)
            return;
        std::vector<GLFWimage> list;
        for (const WindowIconImage& image : images)
            if (image.Width > 0 && image.Height > 0 && image.Pixels.size() >= (size_t)image.Width * image.Height * 4)
                list.push_back({ (int)image.Width, (int)image.Height, const_cast<unsigned char*>(image.Pixels.data()) });
        glfwSetWindowIcon(m_Window, (int)list.size(), list.empty() ? nullptr : list.data());
    }

    void GlfwWindow::SetTitle(const std::string& title)
    {
        m_Data.Title = title;
        glfwSetWindowTitle(m_Window, title.c_str());
    }

    void GlfwWindow::SetCursorLocked(bool locked)
    {
        glfwSetInputMode(m_Window, GLFW_CURSOR, locked ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    }

}
