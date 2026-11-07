#include "Zamin/Core/Window.h"

#include <algorithm>
#include <limits>

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

    int FindMonitorForRect(const std::vector<MonitorArea>& monitors, int x, int y, int width, int height)
    {
        int best = -1;
        long long bestArea = 0;
        for (size_t i = 0; i < monitors.size(); i++)
        {
            const MonitorArea& m = monitors[i];
            const long long w = std::max(0, std::min(x + width, m.X + m.Width) - std::max(x, m.X));
            const long long h = std::max(0, std::min(y + height, m.Y + m.Height) - std::max(y, m.Y));
            if (w * h > bestArea)
            {
                bestArea = w * h;
                best = (int)i;
            }
        }
        if (best >= 0 || monitors.empty())
            return best;
        // Off every monitor: the nearest one.
        const double cx = x + width * 0.5, cy = y + height * 0.5;
        double bestDistance = std::numeric_limits<double>::max();
        for (size_t i = 0; i < monitors.size(); i++)
        {
            const MonitorArea& m = monitors[i];
            const double dx = m.X + m.Width * 0.5 - cx, dy = m.Y + m.Height * 0.5 - cy;
            if (dx * dx + dy * dy < bestDistance)
            {
                bestDistance = dx * dx + dy * dy;
                best = (int)i;
            }
        }
        return best;
    }

    void CenterInArea(const MonitorArea& area, int width, int height, int& x, int& y)
    {
        x = area.X + std::max(0, (area.Width - width) / 2);
        y = area.Y + std::max(0, (area.Height - height) / 2);
    }

}
