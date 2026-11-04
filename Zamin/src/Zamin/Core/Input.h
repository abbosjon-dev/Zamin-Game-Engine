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

        // Gamepads (Xbox / PlayStation layout). id = 0..15
        static bool IsGamepadConnected(int id = 0);
        static bool IsGamepadButtonPressed(GamepadButton button, int id = 0);
        // Returns -1..1 (triggers: -1 released .. 1 pressed). Small values (< deadzone) return 0.
        static float GetGamepadAxis(GamepadAxis axis, int id = 0, float deadzone = 0.15f);
        static const char* GetGamepadName(int id = 0);
    };

}
