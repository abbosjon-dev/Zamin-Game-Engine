#pragma once

#include "Zamin/Core/Base.h"
#include "Zamin/Events/Event.h"
#include "Zamin/Renderer/GraphicsSettings.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace Zamin {

    // How a window covers the screen.
    enum class WindowMode
    {
        Windowed = 0, // a normal window with a title bar (its own size)
        Fullscreen,   // exclusive: the primary monitor at its own video mode
        Borderless    // windowed fullscreen: an undecorated window over the whole primary monitor
    };
    const char* WindowModeToString(WindowMode mode);
    bool WindowModeFromString(std::string_view text, WindowMode& out);

    // One size of a window icon: RGBA8 pixels, rows top first.
    struct WindowIconImage
    {
        uint32_t Width = 0, Height = 0;
        std::vector<uint8_t> Pixels;
    };

    struct WindowProps
    {
        std::string Title = "Zamin Engine";
        uint32_t Width = 1600;  // the windowed size (also the size to go back to from fullscreen)
        uint32_t Height = 900;
        bool VSync = true;
        bool Maximized = false;
        WindowMode Mode = WindowMode::Windowed;
        // What SetFullscreen(true) switches to (a window that starts fullscreen / borderless uses that).
        WindowMode FullscreenMode = WindowMode::Fullscreen;
        bool Resizable = true;
        // The OpenGL context and framebuffer to ask for (version, debug output, MSAA, sRGB). One the driver
        // cannot make falls back to the defaults (4.1 core, no MSAA / sRGB).
        GraphicsSettings Graphics;
    };

    // Platform-independent window interface.
    class Window
    {
    public:
        using EventCallbackFn = std::function<void(Event&)>;

        virtual ~Window() = default;

        virtual void OnUpdate() = 0;

        virtual uint32_t GetWidth() const = 0;
        virtual uint32_t GetHeight() const = 0;
        virtual uint32_t GetFramebufferWidth() const = 0;
        virtual uint32_t GetFramebufferHeight() const = 0;
        virtual float GetContentScale() const = 0;

        virtual void SetEventCallback(const EventCallbackFn& callback) = 0;
        virtual void SetVSync(bool enabled) = 0;
        virtual bool IsVSync() const = 0;
        // The whole primary monitor (in the window's fullscreen mode: exclusive or borderless) / back
        // to the window it was.
        virtual void SetFullscreen(bool fullscreen) = 0;
        // Fullscreen or borderless.
        virtual bool IsFullscreen() const = 0;
        virtual void SetWindowMode(WindowMode mode) = 0;
        virtual WindowMode GetWindowMode() const = 0;
        // Whether the user can resize the window by its edges (windowed mode).
        virtual void SetResizable(bool resizable) = 0;
        virtual bool IsResizable() const = 0;
        // The windowed size (screen coordinates); applied now in windowed mode, else when it goes back.
        virtual void SetWindowedSize(uint32_t width, uint32_t height) = 0;
        // The title bar / taskbar icon: one or more sizes (the system picks the best); empty: the default.
        virtual void SetIcon(const std::vector<WindowIconImage>& images) = 0;
        virtual void SetTitle(const std::string& title) = 0;
        virtual const std::string& GetTitle() const = 0;
        virtual void SetCursorLocked(bool locked) = 0;
        // Whether the window has the keyboard focus (in front of the other programs).
        virtual bool IsFocused() const = 0;
        // Waits up to `seconds` for the window's events and handles them (any input ends the wait).
        virtual void WaitEvents(double seconds) = 0;

        virtual void* GetNativeWindow() const = 0;

        static Scope<Window> Create(const WindowProps& props = WindowProps());
    };

}
