#pragma once

#include "Zamin/Core/Base.h"
#include "Zamin/Renderer/GraphicsSettings.h"

namespace Zamin {

    class GraphicsContext
    {
    public:
        virtual ~GraphicsContext() = default;
        virtual void Init() = 0;
        virtual void SwapBuffers() = 0;

        // `requested`: what the window asked for (the info records it next to what the context got).
        static Scope<GraphicsContext> Create(void* window, const GraphicsSettings& requested = GraphicsSettings(), bool fellBack = false);

        // The context of the (last made) window: version, vendor, debug output, MSAA, sRGB. Not Valid headless.
        static const GraphicsContextInfo& GetInfo();
    };

}
