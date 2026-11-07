#include "Zamin/Platform/GLFW/GlfwWindow.h"

#include <algorithm>
#include <format>
#include <vector>

#include <GLFW/glfw3.h>

namespace Zamin {

    static uint8_t s_GLFWWindowCount = 0;
    // A window is being made with fallbacks: a context the driver cannot make is only a warning then.
    static bool s_TryingContext = false;

    static void GLFWErrorCallback(int error, const char* description)
    {
        // Reading an empty / non-text clipboard (editor paste menus) is not an error.
        if (error == GLFW_FORMAT_UNAVAILABLE)
            return;
        if (s_TryingContext)
        {
            ZM_CORE_WARN("GLFW ({}): {}", error, description);
            return;
        }
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

        GraphicsSettings graphics = props.Graphics;
        graphics.Sanitize();
        // The window's hints: the context (version, debug output), its framebuffer (MSAA, sRGB) and the window.
        auto setHints = [&](const GraphicsSettings& g) {
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, g.GLMajor);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, g.GLMinor);
            glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
            glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
            glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, g.WantsDebugContext() ? GLFW_TRUE : GLFW_FALSE);
            glfwWindowHint(GLFW_SAMPLES, g.MSAASamples);
            glfwWindowHint(GLFW_SRGB_CAPABLE, g.SRGBFramebuffer ? GLFW_TRUE : GLFW_FALSE);
            if (props.Maximized)
                glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);
            glfwWindowHint(GLFW_RESIZABLE, props.Resizable ? GLFW_TRUE : GLFW_FALSE);
        };
        setHints(graphics);
        m_Resizable = props.Resizable;
        m_WindowedWidth = (int)std::max(1u, props.Width);
        m_WindowedHeight = (int)std::max(1u, props.Height);
        m_FullscreenMode = props.Mode != WindowMode::Windowed ? props.Mode
                         : props.FullscreenMode != WindowMode::Windowed ? props.FullscreenMode : WindowMode::Fullscreen;

        m_FullscreenWidth = props.FullscreenWidth;
        m_FullscreenHeight = props.FullscreenHeight;

        // The monitor to open on (--monitor N): its index in the system's list, else the primary.
        int monitorCount = 0;
        GLFWmonitor** monitors = glfwGetMonitors(&monitorCount);
        GLFWmonitor* target = glfwGetPrimaryMonitor();
        if (props.Monitor >= 0)
        {
            if (props.Monitor < monitorCount)
                target = monitors[props.Monitor];
            else
                ZM_CORE_WARN("Monitor {} yo'q ({} ta monitor bor) - asosiy monitor ishlatiladi", props.Monitor, monitorCount);
        }
        if (monitorCount > 1)
        {
            std::string list;
            for (int i = 0; i < monitorCount; i++)
            {
                const GLFWvidmode* m = glfwGetVideoMode(monitors[i]);
                const char* name = glfwGetMonitorName(monitors[i]);
                list += std::format("{}{}: {} {}x{}@{}{}", i ? ", " : "", i, name ? name : "?", m ? m->width : 0, m ? m->height : 0,
                                    m ? m->refreshRate : 0, monitors[i] == glfwGetPrimaryMonitor() ? " (asosiy)" : "");
            }
            ZM_CORE_INFO("Monitorlar: {}", list);
        }

        // The windowed size fits the monitor (a size saved on a larger one, or chosen in the editor's Play): never
        // a window larger than the screen.
        if (const GLFWvidmode* screen = target ? glfwGetVideoMode(target) : nullptr)
        {
            const ScreenResolution asked{ (uint32_t)m_WindowedWidth, (uint32_t)m_WindowedHeight };
            const ScreenResolution fitted = FitResolution(asked, { (uint32_t)screen->width, (uint32_t)screen->height });
            if (fitted != asked)
            {
                ZM_CORE_WARN("Oyna o'lchami {}x{} monitordan ({}x{}) katta - {}x{} qilindi", asked.Width, asked.Height, screen->width, screen->height,
                             fitted.Width, fitted.Height);
                m_WindowedWidth = (int)fitted.Width;
                m_WindowedHeight = (int)fitted.Height;
                if (props.Mode == WindowMode::Windowed)
                {
                    m_Data.Width = fitted.Width;
                    m_Data.Height = fitted.Height;
                }
            }
        }

        // Fullscreen: the monitor's own video mode (or the asked one); borderless: an undecorated window of its size.
        const GLFWvidmode* video = props.Mode != WindowMode::Windowed && target ? glfwGetVideoMode(target) : nullptr;
        WindowMode mode = video ? props.Mode : WindowMode::Windowed;
        if (props.Mode != WindowMode::Windowed && !video)
            ZM_CORE_WARN("To'liq ekran: monitor topilmadi - oynali rejim");
        if (video)
        {
            const bool ownMode = mode == WindowMode::Fullscreen && m_FullscreenWidth > 0 && m_FullscreenHeight > 0;
            m_Data.Width = ownMode ? m_FullscreenWidth : (uint32_t)video->width;
            m_Data.Height = ownMode ? m_FullscreenHeight : (uint32_t)video->height;
        }
        if (mode == WindowMode::Borderless)
            glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);

        // The driver cannot make the asked context: the same framebuffer (MSAA, sRGB) on OpenGL 4.1, then
        // the defaults (4.1 core, no MSAA, no sRGB; the debug output as asked).
        std::vector<GraphicsSettings> attempts = { graphics };
        if (graphics.GLMajor != GraphicsSettings::DefaultMajor || graphics.GLMinor != GraphicsSettings::DefaultMinor)
        {
            GraphicsSettings sameBuffer = graphics;
            sameBuffer.GLMajor = GraphicsSettings::DefaultMajor;
            sameBuffer.GLMinor = GraphicsSettings::DefaultMinor;
            attempts.push_back(sameBuffer);
        }
        if (graphics.MSAASamples > 0 || graphics.SRGBFramebuffer)
        {
            GraphicsSettings plain;
            plain.DebugContext = graphics.DebugContext;
            attempts.push_back(plain);
        }
        bool fellBack = false;
        s_TryingContext = attempts.size() > 1;
        for (size_t i = 0; i < attempts.size() && !m_Window; i++)
        {
            if (i > 0)
            {
                ZM_CORE_WARN("OpenGL {} core konteksti (MSAA {}, sRGB {}) yaratilmadi - OpenGL {} (MSAA {}, sRGB {}) bilan qayta urinish",
                             attempts[i - 1].GetVersionString(), attempts[i - 1].MSAASamples, attempts[i - 1].SRGBFramebuffer ? "ha" : "yo'q",
                             attempts[i].GetVersionString(), attempts[i].MSAASamples, attempts[i].SRGBFramebuffer ? "ha" : "yo'q");
                glfwDefaultWindowHints();
                setHints(attempts[i]);
                if (mode == WindowMode::Borderless)
                    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
            }
            s_TryingContext = i + 1 < attempts.size(); // the last attempt's failure is an error
            m_Window = glfwCreateWindow((int)m_Data.Width, (int)m_Data.Height, m_Data.Title.c_str(),
                                        mode == WindowMode::Fullscreen ? target : nullptr, nullptr);
            if (m_Window)
            {
                fellBack = i > 0;
                graphics = attempts[i];
            }
        }
        s_TryingContext = false;
        glfwDefaultWindowHints();
        if (!m_Window)
        {
            ZM_CORE_CRITICAL("Oyna yaratilmadi (OpenGL {} core qo'llab-quvvatlanadimi?)", graphics.GetVersionString());
            std::exit(1);
        }
        ++s_GLFWWindowCount;
        m_Mode = mode;
        // (Wayland places windows itself: GLFW reports positions as unavailable there.)
        const bool canPlace = glfwGetPlatform() != GLFW_PLATFORM_WAYLAND;
        // The window's place in windowed mode: the middle of the monitor's work area (also where a window
        // that starts fullscreen goes back to).
        if (canPlace && target)
        {
            MonitorArea work;
            glfwGetMonitorWorkarea(target, &work.X, &work.Y, &work.Width, &work.Height);
            if (work.Width > 0 && work.Height > 0)
                CenterInArea(work, m_WindowedWidth, m_WindowedHeight, m_WindowedX, m_WindowedY);
        }
        if (canPlace && mode == WindowMode::Borderless)
        {
            int mx = 0, my = 0;
            glfwGetMonitorPos(target, &mx, &my);
            glfwSetWindowPos(m_Window, mx, my);
        }
        else if (canPlace && mode == WindowMode::Windowed && !props.Maximized && target)
            glfwSetWindowPos(m_Window, m_WindowedX, m_WindowedY);
        ZM_CORE_INFO("Oyna yaratildi: {} ({}x{}, {})", m_Data.Title, m_Data.Width, m_Data.Height,
                     mode == WindowMode::Fullscreen ? "to'liq ekran" : mode == WindowMode::Borderless ? "chegarasiz to'liq ekran" : "oynali");

        int w, h;
        glfwGetWindowSize(m_Window, &w, &h);
        m_Data.Width = (uint32_t)w;
        m_Data.Height = (uint32_t)h;

        GraphicsSettings requested = props.Graphics;
        requested.Sanitize();
        m_Context = GraphicsContext::Create(m_Window, requested, fellBack);
        m_Context->Init();

        glfwSetWindowUserPointer(m_Window, &m_Data);
        SetVSync(props.VSync);

        // ---- callbacks ----
        glfwSetWindowSizeCallback(m_Window, [](GLFWwindow* window, int width, int height) {
            WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
            data.Width = width;
            data.Height = height;
            data.WindowedSizePending = false; // the window manager answered (with the size or one of its own)
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
        if (m_CustomCursor)
            glfwDestroyCursor(m_CustomCursor);
        for (GLFWcursor* cursor : m_ShapeCursors)
            if (cursor)
                glfwDestroyCursor(cursor);
        glfwDestroyWindow(m_Window);
        if (--s_GLFWWindowCount == 0)
            glfwTerminate();
    }

    void GlfwWindow::OnUpdate()
    {
        glfwPollEvents();
        m_Context->SwapBuffers();
    }

    void GlfwWindow::SwapBuffers() { m_Context->SwapBuffers(); }

    bool GlfwWindow::IsFocused() const
    {
        return glfwGetWindowAttrib(m_Window, GLFW_FOCUSED) != 0;
    }

    bool GlfwWindow::IsMinimized() const
    {
        return glfwGetWindowAttrib(m_Window, GLFW_ICONIFIED) != 0;
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
        glfwSwapInterval(enabled ? m_VSyncInterval : 0);
        m_Data.VSync = enabled;
    }

    void GlfwWindow::SetVSyncInterval(int interval)
    {
        interval = std::clamp(interval, 1, 4);
        if (interval == m_VSyncInterval)
            return;
        m_VSyncInterval = interval;
        if (m_Data.VSync)
            glfwSwapInterval(interval);
    }

    GLFWmonitor* GlfwWindow::FindCurrentMonitor() const
    {
        if (GLFWmonitor* fullscreen = glfwGetWindowMonitor(m_Window))
            return fullscreen;
        int count = 0;
        GLFWmonitor** monitors = glfwGetMonitors(&count);
        if (count == 0)
            return nullptr;
        if (count == 1 || glfwGetPlatform() == GLFW_PLATFORM_WAYLAND)
            return glfwGetPrimaryMonitor();
        std::vector<MonitorArea> areas((size_t)count);
        for (int i = 0; i < count; i++)
        {
            glfwGetMonitorPos(monitors[i], &areas[(size_t)i].X, &areas[(size_t)i].Y);
            if (const GLFWvidmode* video = glfwGetVideoMode(monitors[i]))
            {
                areas[(size_t)i].Width = video->width;
                areas[(size_t)i].Height = video->height;
            }
        }
        int x = 0, y = 0, w = 0, h = 0;
        glfwGetWindowPos(m_Window, &x, &y);
        glfwGetWindowSize(m_Window, &w, &h);
        const int index = FindMonitorForRect(areas, x, y, w, h);
        return index >= 0 ? monitors[index] : glfwGetPrimaryMonitor();
    }

    int GlfwWindow::GetRefreshRate() const
    {
        GLFWmonitor* monitor = FindCurrentMonitor();
        const GLFWvidmode* video = monitor ? glfwGetVideoMode(monitor) : nullptr;
        return video ? std::max(0, video->refreshRate) : 0;
    }

    int GlfwWindow::GetMonitorIndex() const
    {
        GLFWmonitor* monitor = FindCurrentMonitor();
        int count = 0;
        GLFWmonitor** monitors = glfwGetMonitors(&count);
        for (int i = 0; i < count; i++)
            if (monitors[i] == monitor)
                return i;
        return -1;
    }

    int GlfwWindow::GetMonitorCount() const
    {
        int count = 0;
        glfwGetMonitors(&count);
        return count;
    }

    void GlfwWindow::SetFullscreen(bool fullscreen)
    {
        SetWindowMode(fullscreen ? m_FullscreenMode : WindowMode::Windowed);
    }

    void GlfwWindow::SetWindowMode(WindowMode mode)
    {
        if (mode == m_Mode)
            return;
        // The monitor the window is on now (a window moved to the second monitor goes fullscreen there).
        GLFWmonitor* monitor = FindCurrentMonitor();
        const GLFWvidmode* video = monitor ? glfwGetVideoMode(monitor) : nullptr;
        if (mode != WindowMode::Windowed && !video)
        {
            ZM_CORE_WARN("To'liq ekran: monitor topilmadi");
            return;
        }
        // Leaving the window: where it was, to come back to (a size asked for that the window manager has not
        // applied yet stays the one to come back to).
        if (m_Mode == WindowMode::Windowed)
        {
            if (glfwGetPlatform() != GLFW_PLATFORM_WAYLAND)
                glfwGetWindowPos(m_Window, &m_WindowedX, &m_WindowedY);
            if (!m_Data.WindowedSizePending)
                glfwGetWindowSize(m_Window, &m_WindowedWidth, &m_WindowedHeight);
            m_Data.WindowedSizePending = false;
        }
        switch (mode)
        {
            case WindowMode::Windowed:
                glfwSetWindowAttrib(m_Window, GLFW_DECORATED, GLFW_TRUE);
                glfwSetWindowMonitor(m_Window, nullptr, m_WindowedX, m_WindowedY, std::max(1, m_WindowedWidth), std::max(1, m_WindowedHeight), 0);
                break;
            case WindowMode::Fullscreen:
            {
                // The asked video mode (--resolution with --fullscreen; GLFW picks the closest), else the monitor's own.
                const bool ownMode = m_FullscreenWidth > 0 && m_FullscreenHeight > 0;
                glfwSetWindowAttrib(m_Window, GLFW_DECORATED, GLFW_TRUE);
                glfwSetWindowMonitor(m_Window, monitor, 0, 0, ownMode ? (int)m_FullscreenWidth : video->width,
                                     ownMode ? (int)m_FullscreenHeight : video->height, ownMode ? GLFW_DONT_CARE : video->refreshRate);
                break;
            }
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
        if (m_Mode != WindowMode::Windowed)
            return;
        int w = 0, h = 0;
        glfwGetWindowSize(m_Window, &w, &h);
        if (w == m_WindowedWidth && h == m_WindowedHeight)
            return;
        // (A window manager resizes later: until it answers, the size is the one asked for - an options menu
        // shows it at once.)
        m_Data.WindowedSizePending = true;
        glfwSetWindowSize(m_Window, m_WindowedWidth, m_WindowedHeight);
    }

    ScreenResolution GlfwWindow::GetWindowedSize() const
    {
        if (m_Mode == WindowMode::Windowed && !m_Data.WindowedSizePending)
        {
            int w = 0, h = 0;
            glfwGetWindowSize(m_Window, &w, &h);
            if (w > 0 && h > 0)
                return { (uint32_t)w, (uint32_t)h };
        }
        return { (uint32_t)std::max(1, m_WindowedWidth), (uint32_t)std::max(1, m_WindowedHeight) };
    }

    void GlfwWindow::SetFullscreenResolution(uint32_t width, uint32_t height)
    {
        if (width == m_FullscreenWidth && height == m_FullscreenHeight)
            return;
        m_FullscreenWidth = width;
        m_FullscreenHeight = height;
        if (m_Mode != WindowMode::Fullscreen)
            return;
        // Exclusive fullscreen now: the monitor switches to that video mode (GLFW picks the closest).
        GLFWmonitor* monitor = glfwGetWindowMonitor(m_Window);
        if (!monitor)
            monitor = FindCurrentMonitor();
        const GLFWvidmode* video = monitor ? glfwGetVideoMode(monitor) : nullptr;
        if (!video)
            return;
        const bool ownMode = width > 0 && height > 0;
        glfwSetWindowMonitor(m_Window, monitor, 0, 0, ownMode ? (int)width : video->width, ownMode ? (int)height : video->height,
                             ownMode ? GLFW_DONT_CARE : video->refreshRate);
        SetVSync(m_Data.VSync);
    }

    std::vector<ScreenResolution> GlfwWindow::GetVideoModes() const
    {
        std::vector<ScreenResolution> sizes;
        GLFWmonitor* monitor = FindCurrentMonitor();
        if (!monitor)
            return sizes;
        int count = 0;
        const GLFWvidmode* modes = glfwGetVideoModes(monitor, &count);
        for (int i = 0; modes && i < count; i++)
            if (modes[i].width > 0 && modes[i].height > 0)
                sizes.push_back({ (uint32_t)modes[i].width, (uint32_t)modes[i].height });
        std::sort(sizes.begin(), sizes.end(), [](const ScreenResolution& a, const ScreenResolution& b) {
            return (uint64_t)a.Width * a.Height != (uint64_t)b.Width * b.Height ? (uint64_t)a.Width * a.Height < (uint64_t)b.Width * b.Height : a.Width < b.Width;
        });
        sizes.erase(std::unique(sizes.begin(), sizes.end()), sizes.end());
        return sizes;
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
        SetCursorMode(locked ? WindowCursorMode::Disabled : WindowCursorMode::Normal);
    }

    void GlfwWindow::SetCursorMode(WindowCursorMode mode)
    {
        int value = GLFW_CURSOR_NORMAL;
        switch (mode)
        {
            case WindowCursorMode::Normal:   value = GLFW_CURSOR_NORMAL; break;
            case WindowCursorMode::Hidden:   value = GLFW_CURSOR_HIDDEN; break;
            case WindowCursorMode::Captured: value = GLFW_CURSOR_CAPTURED; break;
            case WindowCursorMode::Disabled: value = GLFW_CURSOR_DISABLED; break;
        }
        glfwSetInputMode(m_Window, GLFW_CURSOR, value);
        // Raw mouse motion for mouse look (no acceleration) where the system has it.
        if (glfwRawMouseMotionSupported())
            glfwSetInputMode(m_Window, GLFW_RAW_MOUSE_MOTION, mode == WindowCursorMode::Disabled ? GLFW_TRUE : GLFW_FALSE);
        m_CursorMode = mode;
    }

    void GlfwWindow::SetCursorImage(const WindowCursorImage* image)
    {
        GLFWcursor* previous = m_CustomCursor;
        m_CustomCursor = nullptr;
        if (image && image->Width > 0 && image->Height > 0 && image->Pixels.size() >= (size_t)image->Width * image->Height * 4)
        {
            GLFWimage glfwImage{ (int)image->Width, (int)image->Height, const_cast<unsigned char*>(image->Pixels.data()) };
            m_CustomCursor = glfwCreateCursor(&glfwImage, image->HotspotX, image->HotspotY);
        }
        if (m_CustomCursor)
            glfwSetCursor(m_Window, m_CustomCursor);
        else
            SetCursorShape(m_CursorShape);
        if (previous)
            glfwDestroyCursor(previous);
    }

    void GlfwWindow::SetCursorShape(CursorShape shape)
    {
        m_CursorShape = shape;
        if (m_CustomCursor)
            return; // the image is shown until it is cleared
        if (shape == CursorShape::Arrow)
        {
            glfwSetCursor(m_Window, nullptr); // the system's default
            return;
        }
        GLFWcursor*& cursor = m_ShapeCursors[(int)shape];
        if (!cursor)
        {
            int glfwShape = GLFW_ARROW_CURSOR;
            switch (shape)
            {
                case CursorShape::Arrow:            glfwShape = GLFW_ARROW_CURSOR; break;
                case CursorShape::IBeam:            glfwShape = GLFW_IBEAM_CURSOR; break;
                case CursorShape::Crosshair:        glfwShape = GLFW_CROSSHAIR_CURSOR; break;
                case CursorShape::Hand:             glfwShape = GLFW_POINTING_HAND_CURSOR; break;
                case CursorShape::ResizeHorizontal: glfwShape = GLFW_RESIZE_EW_CURSOR; break;
                case CursorShape::ResizeVertical:   glfwShape = GLFW_RESIZE_NS_CURSOR; break;
                case CursorShape::ResizeAll:        glfwShape = GLFW_RESIZE_ALL_CURSOR; break;
                case CursorShape::NotAllowed:       glfwShape = GLFW_NOT_ALLOWED_CURSOR; break;
            }
            cursor = glfwCreateStandardCursor(glfwShape);
        }
        glfwSetCursor(m_Window, cursor);
    }

}
