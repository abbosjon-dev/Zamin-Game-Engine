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

    // A monitor's area on the desktop (screen coordinates).
    struct MonitorArea
    {
        int X = 0, Y = 0, Width = 0, Height = 0;
    };
    // The monitor a rectangle (a window) covers most; when it covers none, the one whose centre is
    // nearest to its centre. -1 for an empty list.
    int FindMonitorForRect(const std::vector<MonitorArea>& monitors, int x, int y, int width, int height);
    // The top-left corner that puts a width x height window in the middle of `area` (the area's
    // corner when the window is larger).
    void CenterInArea(const MonitorArea& area, int width, int height, int& x, int& y);

    // One size of a window icon: RGBA8 pixels, rows top first.
    struct WindowIconImage
    {
        uint32_t Width = 0, Height = 0;
        std::vector<uint8_t> Pixels;
    };

    // How the window treats the mouse cursor (Cursor resolves the game's wishes into one of these).
    enum class WindowCursorMode : uint8_t
    {
        Normal = 0, // visible, free
        Hidden,     // invisible over the window, free
        Captured,   // visible, kept inside the window
        Disabled    // invisible and locked: unlimited movement (mouse look), the position is virtual
    };

    // A cursor picture: RGBA8 pixels, rows top first; the hotspot (the pixel that points) from the top-left.
    struct WindowCursorImage
    {
        uint32_t Width = 0, Height = 0;
        std::vector<uint8_t> Pixels;
        int HotspotX = 0, HotspotY = 0;
    };

    // The system's cursor shapes.
    enum class CursorShape : uint8_t { Arrow = 0, IBeam, Crosshair, Hand, ResizeHorizontal, ResizeVertical, ResizeAll, NotAllowed };
    inline constexpr int CursorShapeCount = (int)CursorShape::NotAllowed + 1;

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
        // The monitor to open on: its index in the system's list (0: the primary monitor). -1 or one
        // that is not there: the primary.
        int Monitor = -1;
        // Exclusive fullscreen at this video mode (the monitor's closest one); 0: the monitor's own.
        uint32_t FullscreenWidth = 0, FullscreenHeight = 0;
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
        // With vsync: how many vertical blanks each frame waits for (1: the monitor's rate, 2: half
        // of it ...; 1 ... 4). The player's frame rate limit uses it for rates that divide the monitor's.
        virtual void SetVSyncInterval(int interval) = 0;
        virtual int GetVSyncInterval() const = 0;
        // The refresh rate of the monitor the window is on (Hz; 0: unknown).
        virtual int GetRefreshRate() const = 0;
        // The monitor the window is on (the one it covers most; index in the system's list, 0: the
        // primary; -1: none found) and how many there are.
        virtual int GetMonitorIndex() const = 0;
        virtual int GetMonitorCount() const = 0;
        // The whole monitor the window is on (in the window's fullscreen mode: exclusive or borderless)
        // / back to the window it was (its size and place).
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
        // Locked: WindowCursorMode::Disabled; else Normal.
        virtual void SetCursorLocked(bool locked) = 0;
        virtual void SetCursorMode(WindowCursorMode mode) = 0;
        virtual WindowCursorMode GetCursorMode() const = 0;
        // The cursor's picture over the window: an image (nullptr: the shape) / one of the system's shapes.
        virtual void SetCursorImage(const WindowCursorImage* image) = 0;
        virtual void SetCursorShape(CursorShape shape) = 0;
        // Whether the window has the keyboard focus (in front of the other programs).
        virtual bool IsFocused() const = 0;
        // Whether the window is minimized (iconified: only on the task bar).
        virtual bool IsMinimized() const = 0;
        // Waits up to `seconds` for the window's events and handles them (any input ends the wait).
        virtual void WaitEvents(double seconds) = 0;

        virtual void* GetNativeWindow() const = 0;

        static Scope<Window> Create(const WindowProps& props = WindowProps());
    };

}
