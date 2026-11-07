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

        GLFWmonitor* monitor = props.Fullscreen ? glfwGetPrimaryMonitor() : nullptr;
        if (monitor)
        {
            const GLFWvidmode* mode = glfwGetVideoMode(monitor);
            m_Data.Width = mode->width;
            m_Data.Height = mode->height;
        }

        m_Window = glfwCreateWindow((int)m_Data.Width, (int)m_Data.Height, m_Data.Title.c_str(), monitor, nullptr);
        if (!m_Window)
        {
            ZM_CORE_CRITICAL("Oyna yaratilmadi (OpenGL 4.1 core qo'llab-quvvatlanadimi?)");
            std::exit(1);
        }
        ++s_GLFWWindowCount;
        ZM_CORE_INFO("Oyna yaratildi: {} ({}x{})", m_Data.Title, m_Data.Width, m_Data.Height);

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

    bool GlfwWindow::IsFullscreen() const { return glfwGetWindowMonitor(m_Window) != nullptr; }

    void GlfwWindow::SetFullscreen(bool fullscreen)
    {
        if (fullscreen == IsFullscreen())
            return;
        if (fullscreen)
        {
            GLFWmonitor* monitor = glfwGetPrimaryMonitor();
            const GLFWvidmode* mode = monitor ? glfwGetVideoMode(monitor) : nullptr;
            if (!mode)
            {
                ZM_CORE_WARN("To'liq ekran: monitor topilmadi");
                return;
            }
            glfwGetWindowPos(m_Window, &m_WindowedX, &m_WindowedY);
            glfwGetWindowSize(m_Window, &m_WindowedWidth, &m_WindowedHeight);
            glfwSetWindowMonitor(m_Window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
        }
        else
            glfwSetWindowMonitor(m_Window, nullptr, m_WindowedX, m_WindowedY, std::max(1, m_WindowedWidth), std::max(1, m_WindowedHeight), 0);
        SetVSync(m_Data.VSync); // some drivers forget the swap interval with the mode
        ZM_CORE_INFO("Oyna: {}", fullscreen ? "to'liq ekran" : "oynali rejim");
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
