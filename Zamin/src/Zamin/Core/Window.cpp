#include "Zamin/Core/Window.h"

namespace Zamin {

    const char* WindowModeToString(WindowMode mode)
    {
        switch (mode)
        {
            case WindowMode::Windowed:   return "Windowed";
            case WindowMode::Fullscreen: return "Fullscreen";
            case WindowMode::Borderless: return "Borderless";
        }
        return "Windowed";
    }

    bool WindowModeFromString(std::string_view text, WindowMode& out)
    {
        for (WindowMode mode : { WindowMode::Windowed, WindowMode::Fullscreen, WindowMode::Borderless })
            if (text == WindowModeToString(mode))
            {
                out = mode;
                return true;
            }
        return false;
    }

}
