#pragma once

#include "Zamin/Core/Window.h"

#include <glm/glm.hpp>

#include <filesystem>
#include <string>

namespace Zamin {

    // How the game holds the mouse cursor.
    enum class CursorLockMode : uint8_t
    {
        None = 0,  // free
        Confined,  // kept inside the window (still visible unless hidden)
        Locked     // hidden and held in place: Input::GetMouseDelta keeps reporting movement (mouse look)
    };
    const char* CursorLockModeToString(CursorLockMode mode); // "None", "Confined", "Locked"

    // What the window's cursor is made to be for the game's wishes (Cursor::Resolve).
    struct CursorAppliedState
    {
        WindowCursorMode Mode = WindowCursorMode::Normal;
        bool Image = false;        // the game's picture (else Shape)
        bool Transparent = false;  // hidden by an empty picture (Confined + invisible: the system has no such mode)
        CursorShape Shape = CursorShape::Arrow;

        bool operator==(const CursorAppliedState&) const = default;
    };

    // The mouse cursor for scripts: show / hide it, keep it in the window or lock it for mouse look,
    // give it a picture of the game's own or one of the system's shapes.
    //
    //     Cursor::SetLockMode(CursorLockMode::Locked);   // mouse look: Input::GetMouseDelta()
    //     Cursor::SetVisible(false);
    //     Cursor::SetImage("Textures/crosshair.png", { 16, 16 });
    //
    // The wishes are kept and applied to the window of the host (the player's window; the editor's while
    // its game view has the focus) once per frame. The system's cursor comes back - and the game's after
    // it - when the window loses the focus, while the game is paused (Time::SetPaused: a pause menu needs
    // the mouse; SetReleaseWhenPaused(false) keeps the game's then) and when play stops (Reset).
    class Cursor
    {
    public:
        // ---- the game's wishes
        static void SetVisible(bool visible);
        static bool IsVisible();
        static void SetLockMode(CursorLockMode mode);
        static CursorLockMode GetLockMode();
        static bool IsLocked() { return GetLockMode() == CursorLockMode::Locked; }
        // One of the system's shapes (shown while no picture is set).
        static void SetShape(CursorShape shape);
        static CursorShape GetShape();
        // A PNG / JPG (a path relative to the project's assets, or absolute) as the cursor; `hotspot`: the
        // pixel that points, from the top-left (kept inside the picture). False when it cannot be read
        // (the cursor stays as it was).
        static bool SetImage(const std::filesystem::path& file, glm::ivec2 hotspot = glm::ivec2(0), std::string* error = nullptr);
        // RGBA8 pixels, rows top first.
        static bool SetImage(const WindowCursorImage& image);
        static void ClearImage();
        static bool HasImage();
        static const WindowCursorImage& GetImage();
        // Whether a paused game (Time::IsPaused) gets the free, visible cursor (default: yes).
        static void SetReleaseWhenPaused(bool release);
        static bool GetReleaseWhenPaused();
        // Visible, free, the arrow, no picture, released when paused (the editor when play stops).
        static void Reset();

        // ---- the host
        // The window the wishes are applied to (nullptr: none - the window's cursor goes back to normal).
        static void BindWindow(Window* window);
        static bool HasWindow();
        // false: the game does not own the cursor now (the editor's game view is not focused): the
        // system's cursor. The player keeps it true; the window's focus is checked by Update itself.
        static void SetHostActive(bool active);
        static bool IsHostActive();
        // Once per frame (Application::Run): applies what changed (the wishes, the focus, the pause).
        static void Update();
        // What the window shows now (the last applied state).
        static const CursorAppliedState& GetApplied();

        // The window state for the wishes. `active`: the host owns the cursor and its window has the
        // focus; `paused`: the game is paused and releases the cursor then.
        static CursorAppliedState Resolve(bool visible, CursorLockMode mode, bool hasImage, CursorShape shape, bool active, bool paused);
    };

}
