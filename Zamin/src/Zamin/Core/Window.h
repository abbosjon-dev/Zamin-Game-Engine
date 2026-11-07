#pragma once

#include "Zamin/Core/Base.h"
#include "Zamin/Events/Event.h"

#include <functional>
#include <string>

namespace Zamin {

    struct WindowProps
    {
        std::string Title = "Zamin Engine";
        uint32_t Width = 1600;
        uint32_t Height = 900;
        bool VSync = true;
        bool Maximized = false;
        bool Fullscreen = false;
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
        // The whole primary monitor at its own resolution / back to the window it was.
        virtual void SetFullscreen(bool fullscreen) = 0;
        virtual bool IsFullscreen() const = 0;
        virtual void SetTitle(const std::string& title) = 0;
        virtual void SetCursorLocked(bool locked) = 0;
        // Whether the window has the keyboard focus (in front of the other programs).
        virtual bool IsFocused() const = 0;
        // Waits up to `seconds` for the window's events and handles them (any input ends the wait).
        virtual void WaitEvents(double seconds) = 0;

        virtual void* GetNativeWindow() const = 0;

        static Scope<Window> Create(const WindowProps& props = WindowProps());
    };

}
