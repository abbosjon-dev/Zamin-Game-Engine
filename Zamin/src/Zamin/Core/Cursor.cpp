#include "Zamin/Core/Cursor.h"

#include "Zamin/Core/Application.h"
#include "Zamin/Core/Input.h"
#include "Zamin/Core/Log.h"
#include "Zamin/Core/Time.h"
#include "Zamin/Project/Project.h"
#include "Zamin/Utils/Image.h"

#include <algorithm>
#include <format>

namespace Zamin {

    namespace {

        struct CursorState
        {
            // The game's wishes.
            bool Visible = true;
            CursorLockMode Lock = CursorLockMode::None;
            CursorShape Shape = CursorShape::Arrow;
            WindowCursorImage Image;
            bool HasImage = false;
            uint64_t ImageSerial = 0;        // changes with every new picture (applied again)
            bool ReleaseWhenPaused = true;
            // The host.
            Window* BoundWindow = nullptr;
            bool HostActive = true;
            // What the window has now.
            CursorAppliedState Applied;
            uint64_t AppliedImageSerial = 0;
            bool AppliedOnce = false;
        };

        CursorState& S()
        {
            static CursorState s;
            return s;
        }

        // An empty picture: hides the cursor in modes that have no hidden variant (Confined).
        const WindowCursorImage& TransparentImage()
        {
            static WindowCursorImage image = [] {
                WindowCursorImage i;
                i.Width = i.Height = 16;
                i.Pixels.assign(16 * 16 * 4, 0);
                return i;
            }();
            return image;
        }

        void Apply(CursorState& s, const CursorAppliedState& wanted, bool force)
        {
            if (!s.BoundWindow)
                return;
            const bool imageChanged = wanted.Image && s.AppliedImageSerial != s.ImageSerial;
            if (!force && s.AppliedOnce && wanted == s.Applied && !imageChanged)
                return;
            Window& window = *s.BoundWindow;
            const bool modeChanged = !s.AppliedOnce || wanted.Mode != s.Applied.Mode;
            // The picture first (a mode change can show the cursor).
            if (wanted.Transparent)
                window.SetCursorImage(&TransparentImage());
            else if (wanted.Image)
                window.SetCursorImage(&s.Image);
            else
            {
                window.SetCursorImage(nullptr);
                window.SetCursorShape(wanted.Shape);
            }
            if (modeChanged || force)
            {
                window.SetCursorMode(wanted.Mode);
                // Locking moves the (virtual) cursor: no jump in the next frame's mouse delta.
                Input::ResetMouseDelta();
            }
            s.Applied = wanted;
            s.AppliedImageSerial = s.ImageSerial;
            s.AppliedOnce = true;
        }

    }

    const char* CursorLockModeToString(CursorLockMode mode)
    {
        switch (mode)
        {
            case CursorLockMode::None:     return "None";
            case CursorLockMode::Confined: return "Confined";
            case CursorLockMode::Locked:   return "Locked";
        }
        return "None";
    }

    CursorAppliedState Cursor::Resolve(bool visible, CursorLockMode mode, bool hasImage, CursorShape shape, bool active, bool paused)
    {
        CursorAppliedState state;
        if (!active)
            return state; // the system's cursor: normal, the arrow
        state.Shape = shape;
        if (paused)
        {
            // A pause menu: free and visible, the game's picture kept.
            state.Image = hasImage;
            return state;
        }
        switch (mode)
        {
            case CursorLockMode::Locked:
                state.Mode = WindowCursorMode::Disabled;
                return state; // invisible whatever the picture
            case CursorLockMode::Confined:
                state.Mode = WindowCursorMode::Captured;
                state.Transparent = !visible;
                state.Image = visible && hasImage;
                return state;
            case CursorLockMode::None:
                state.Mode = visible ? WindowCursorMode::Normal : WindowCursorMode::Hidden;
                state.Image = visible && hasImage;
                return state;
        }
        return state;
    }

    void Cursor::SetVisible(bool visible) { S().Visible = visible; }
    bool Cursor::IsVisible() { return S().Visible; }
    void Cursor::SetLockMode(CursorLockMode mode) { S().Lock = mode; }
    CursorLockMode Cursor::GetLockMode() { return S().Lock; }
    void Cursor::SetShape(CursorShape shape) { S().Shape = shape; }
    CursorShape Cursor::GetShape() { return S().Shape; }
    bool Cursor::HasImage() { return S().HasImage; }
    const WindowCursorImage& Cursor::GetImage() { return S().Image; }
    void Cursor::SetReleaseWhenPaused(bool release) { S().ReleaseWhenPaused = release; }
    bool Cursor::GetReleaseWhenPaused() { return S().ReleaseWhenPaused; }

    bool Cursor::SetImage(const WindowCursorImage& image)
    {
        if (image.Width == 0 || image.Height == 0 || image.Pixels.size() < (size_t)image.Width * image.Height * 4)
            return false;
        CursorState& s = S();
        s.Image = image;
        s.Image.HotspotX = std::clamp(image.HotspotX, 0, (int)image.Width - 1);
        s.Image.HotspotY = std::clamp(image.HotspotY, 0, (int)image.Height - 1);
        s.HasImage = true;
        s.ImageSerial++;
        return true;
    }

    bool Cursor::SetImage(const std::filesystem::path& file, glm::ivec2 hotspot, std::string* error)
    {
        const std::filesystem::path path = file.is_absolute() || !Project::GetActive() ? file : Project::GetAssetFileSystemPath(file);
        ImageData data;
        std::string reason;
        if (!Image::Load(path.string(), data, 4, false, &reason))
        {
            ZM_CORE_WARN("Kursor rasmi o'qilmadi ({}): {}", file.generic_string(), reason);
            if (error)
                *error = reason;
            return false;
        }
        WindowCursorImage image;
        image.Width = data.Width;
        image.Height = data.Height;
        image.Pixels = std::move(data.Pixels);
        image.HotspotX = hotspot.x;
        image.HotspotY = hotspot.y;
        if (image.Width > 256 || image.Height > 256)
            ZM_CORE_WARN("Kursor rasmi katta ({}x{}): ba'zi tizimlar uni ko'rsatmaydi (32-64 px tavsiya etiladi)", image.Width, image.Height);
        return SetImage(image);
    }

    void Cursor::ClearImage()
    {
        CursorState& s = S();
        s.Image = WindowCursorImage();
        s.HasImage = false;
        s.ImageSerial++;
    }

    void Cursor::Reset()
    {
        CursorState& s = S();
        s.Visible = true;
        s.Lock = CursorLockMode::None;
        s.Shape = CursorShape::Arrow;
        s.ReleaseWhenPaused = true;
        if (s.HasImage)
            ClearImage();
        Update();
    }

    void Cursor::BindWindow(Window* window)
    {
        CursorState& s = S();
        if (s.BoundWindow && s.BoundWindow != window && s.AppliedOnce)
        {
            // The old window's cursor back to normal.
            s.BoundWindow->SetCursorImage(nullptr);
            s.BoundWindow->SetCursorShape(CursorShape::Arrow);
            s.BoundWindow->SetCursorMode(WindowCursorMode::Normal);
        }
        s.BoundWindow = window;
        s.AppliedOnce = false;
        s.Applied = CursorAppliedState();
        if (window)
            Update();
    }

    bool Cursor::HasWindow() { return S().BoundWindow != nullptr; }
    void Cursor::SetHostActive(bool active) { S().HostActive = active; }
    bool Cursor::IsHostActive() { return S().HostActive; }
    const CursorAppliedState& Cursor::GetApplied() { return S().Applied; }

    void Cursor::Update()
    {
        CursorState& s = S();
        if (!s.BoundWindow)
            return;
        // (Automation runs - xvfb, no window manager - count as focused.)
        const bool active = s.HostActive && (s.BoundWindow->IsFocused() || Application::IsAutomationRun());
        const bool paused = s.ReleaseWhenPaused && Time::IsPaused();
        Apply(s, Resolve(s.Visible, s.Lock, s.HasImage, s.Shape, active, paused), false);
    }

}
