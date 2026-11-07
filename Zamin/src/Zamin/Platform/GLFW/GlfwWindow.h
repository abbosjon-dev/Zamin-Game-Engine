#pragma once

#include "Zamin/Core/Window.h"
#include "Zamin/Renderer/GraphicsContext.h"

struct GLFWwindow;
struct GLFWmonitor;
struct GLFWcursor;

namespace Zamin {

    // Desktop window built on GLFW (Linux X11/Wayland, later Windows & macOS).
    class GlfwWindow : public Window
    {
    public:
        explicit GlfwWindow(const WindowProps& props);
        ~GlfwWindow() override;

        void OnUpdate() override;

        uint32_t GetWidth() const override { return m_Data.Width; }
        uint32_t GetHeight() const override { return m_Data.Height; }
        uint32_t GetFramebufferWidth() const override;
        uint32_t GetFramebufferHeight() const override;
        float GetContentScale() const override;

        void SetEventCallback(const EventCallbackFn& callback) override { m_Data.EventCallback = callback; }
        void SetVSync(bool enabled) override;
        bool IsVSync() const override { return m_Data.VSync; }
        void SetVSyncInterval(int interval) override;
        int GetVSyncInterval() const override { return m_VSyncInterval; }
        int GetRefreshRate() const override;
        int GetMonitorIndex() const override;
        int GetMonitorCount() const override;
        void SetFullscreen(bool fullscreen) override;
        bool IsFullscreen() const override { return m_Mode != WindowMode::Windowed; }
        void SetWindowMode(WindowMode mode) override;
        WindowMode GetWindowMode() const override { return m_Mode; }
        void SetResizable(bool resizable) override;
        bool IsResizable() const override { return m_Resizable; }
        void SetWindowedSize(uint32_t width, uint32_t height) override;
        void SetIcon(const std::vector<WindowIconImage>& images) override;
        void SetTitle(const std::string& title) override;
        const std::string& GetTitle() const override { return m_Data.Title; }
        void SetCursorLocked(bool locked) override;
        void SetCursorMode(WindowCursorMode mode) override;
        WindowCursorMode GetCursorMode() const override { return m_CursorMode; }
        void SetCursorImage(const WindowCursorImage* image) override;
        void SetCursorShape(CursorShape shape) override;
        bool IsFocused() const override;
        bool IsMinimized() const override;
        void WaitEvents(double seconds) override;

        void* GetNativeWindow() const override { return m_Window; }

    private:
        // The monitor the window is on: its fullscreen monitor, else the one it covers most (Wayland,
        // where windows have no known position: the primary).
        GLFWmonitor* FindCurrentMonitor() const;

    private:
        GLFWwindow* m_Window = nullptr;
        Scope<GraphicsContext> m_Context;
        int m_WindowedX = 100, m_WindowedY = 100, m_WindowedWidth = 1600, m_WindowedHeight = 900; // before going fullscreen
        WindowMode m_Mode = WindowMode::Windowed;
        WindowMode m_FullscreenMode = WindowMode::Fullscreen; // what SetFullscreen(true) uses
        bool m_Resizable = true;
        int m_VSyncInterval = 1;                              // vertical blanks per frame with vsync
        uint32_t m_FullscreenWidth = 0, m_FullscreenHeight = 0; // exclusive fullscreen's video mode (0: the monitor's)
        WindowCursorMode m_CursorMode = WindowCursorMode::Normal;
        GLFWcursor* m_CustomCursor = nullptr;                 // SetCursorImage's
        GLFWcursor* m_ShapeCursors[CursorShapeCount] = {};    // made when first used
        CursorShape m_CursorShape = CursorShape::Arrow;

        struct WindowData
        {
            std::string Title;
            uint32_t Width = 0, Height = 0;
            bool VSync = true;
            EventCallbackFn EventCallback;
        } m_Data;
    };

}
