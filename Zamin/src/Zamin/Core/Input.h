#pragma once

#include "Zamin/Core/KeyCodes.h"

#include <glm/glm.hpp>

namespace Zamin {

    class Input
    {
    public:
        static bool IsKeyPressed(Key key);
        static bool IsMouseButtonPressed(MouseButton button);
        static glm::vec2 GetMousePosition();
        static float GetMouseX() { return GetMousePosition().x; }
        static float GetMouseY() { return GetMousePosition().y; }
    };

}
