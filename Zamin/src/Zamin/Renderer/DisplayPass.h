#pragma once

#include "Zamin/Core/Display.h"

#include <cstdint>

namespace Zamin {

    // The player's last pass: its finished frame (an RGBA8 picture in display colours) drawn over the
    // bound framebuffer's viewport with the player's gamma and brightness (Display::GetAdjustment) -
    // and, while the game is paused in the background, dimmed (`dim`: 0 none .. 1 black).
    class DisplayPass
    {
    public:
        static void Present(uint32_t texture, const DisplayAdjustment& adjustment, float dim = 0.0f);
        // Frees the shader (Renderer::Shutdown).
        static void Shutdown();
    };

}
