#pragma once

// Internal: per-frame device state shared by Input.cpp and the platform backend.

#include "Zamin/Core/Input.h"
#include "Zamin/Core/InputActions.h"

#include <array>
#include <string>
#include <vector>

namespace Zamin::Internal {

    inline constexpr int InputKeyCount = 349; // GLFW_KEY_LAST + 1
    inline constexpr int InputFirstKey = 32;  // GLFW_KEY_SPACE (lower codes are not keys)

    struct ButtonState
    {
        bool Live = false;           // latest known state (events / platform poll)
        bool Down = false;           // this frame
        bool WasDown = false;        // previous frame
        bool PendingPress = false;   // press event since the last Update()
        bool PendingRelease = false; // release event since the last Update()
        bool Pressed = false;        // press event between the previous and this frame
        bool Released = false;       // release event between the previous and this frame
        bool Simulated = false;      // held by Input::SimulateKey: the platform poll does not overwrite Live

        void OnEvent(bool pressed)
        {
            Live = pressed;
            (pressed ? PendingPress : PendingRelease) = true;
        }

        void BeginFrame()
        {
            WasDown = Down;
            Down = Live;
            Pressed = PendingPress;
            Released = PendingRelease;
            PendingPress = PendingRelease = false;
        }
    };

    struct GamepadState
    {
        bool Connected = false;
        std::array<bool, GamepadButtonCount> Buttons{};
        std::array<float, GamepadAxisCount> Axes{};
    };

    struct GamepadSlot
    {
        GamepadState Live, Current, Previous;
        bool Simulated = false;
    };

    struct InputState
    {
        std::array<ButtonState, InputKeyCount> Keys{};
        std::array<ButtonState, MouseButtonCount> Mouse{};

        glm::vec2 MouseLive{ 0.0f }, MousePosition{ 0.0f }, MousePrevious{ 0.0f };
        bool HasMousePosition = false;
        glm::vec2 PendingScroll{ 0.0f }, Scroll{ 0.0f };

        std::array<GamepadSlot, Input::MaxGamepads> Gamepads{};

        std::vector<TextInputEvent> PendingText, Text; // since the last Update() / this frame
        std::string Clipboard;                          // without a window
    };

    InputState& GetInputState();

    // Implemented by the platform backend: writes the live keyboard / mouse / gamepad state
    // (skipping simulated gamepad slots). Does nothing when there is no window.
    void PollPlatformInput(InputState& state);

}
