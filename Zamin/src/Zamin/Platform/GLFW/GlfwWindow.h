#pragma once

#include "Zamin/Core/Window.h"
#include "Zamin/Renderer/GraphicsContext.h"

struct GLFWwindow;

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
        void SetFullscreen(bool fullscreen) override;
        bool IsFullscreen() const override;
        void SetTitle(const std::string& title) override;
        void SetCursorLocked(bool locked) override;
        bool IsFocused() const override;
        void WaitEvents(double seconds) override;

        void* GetNativeWindow() const override { return m_Window; }

    private:
        GLFWwindow* m_Window = nullptr;
        Scope<GraphicsContext> m_Context;
        int m_WindowedX = 100, m_WindowedY = 100, m_WindowedWidth = 1600, m_WindowedHeight = 900; // before going fullscreen

        struct WindowData
        {
            std::string Title;
            uint32_t Width = 0, Height = 0;
            bool VSync = true;
            EventCallbackFn EventCallback;
        } m_Data;
    };

}
