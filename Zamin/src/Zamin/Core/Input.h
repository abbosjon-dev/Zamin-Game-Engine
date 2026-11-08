#pragma once

#include "Zamin/Core/KeyCodes.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Zamin {

    struct InputAction;
    struct InputAxis;
    struct InputVector;
    enum class InputDevice : uint8_t;

    /// One step of typing (Input::GetTextInput): a typed character, or a key press for editing -
    /// Backspace, arrows, Enter, Ctrl+C ... (the keyboard's auto-repeat included).
    struct TextInputEvent
    {
        uint32_t Codepoint = 0; ///< the typed character (Unicode); 0: a key press
        Key KeyCode{};          ///< the pressed key (when Codepoint is 0)
        bool Control = false, Shift = false, Alt = false, Super = false; ///< modifiers held at that moment

        bool IsCharacter() const { return Codepoint != 0; }
    };

    /// Keyboard, mouse, gamepad and named actions / axes.
    ///
    /// The state is captured once per frame by Input::Update() (called by Application::Run
    /// before the layers update), so every query inside a frame sees the same snapshot and
    /// "just pressed / just released" stay true for exactly one frame. A press and release
    /// that both happen between two frames (a very quick tap) still reports JustPressed and
    /// JustReleased in the next frame.
    class Input
    {
    public:
        /// @name Keyboard
        /// @{
        static bool IsKeyPressed(Key key);         ///< held down this frame
        static bool IsKeyJustPressed(Key key);     ///< went down since the previous frame
        static bool IsKeyJustReleased(Key key);    ///< went up since the previous frame
        /// Any keyboard key (modifier keys alone - Shift, Ctrl, Alt, Super - do not count), mouse button
        /// or gamepad button went down since the previous frame ("press any key").
        static bool IsAnyKeyJustPressed();
        /// @}

        /// @name Mouse
        /// @{
        static bool IsMouseButtonPressed(MouseButton button);
        static bool IsMouseButtonJustPressed(MouseButton button);
        static bool IsMouseButtonJustReleased(MouseButton button);
        static glm::vec2 GetMousePosition();       ///< window pixels, origin top-left
        static float GetMouseX() { return GetMousePosition().x; }
        static float GetMouseY() { return GetMousePosition().y; }
        static glm::vec2 GetMouseDelta();          ///< movement since the previous frame (pixels)
        /// The next frame's mouse delta is 0 (the cursor was moved by the system: locked / released).
        static void ResetMouseDelta();
        static glm::vec2 GetMouseScrollDelta();    ///< wheel movement since the previous frame (y: up > 0)
        /// @}

        /// @name Gamepads
        /// Gamepads (Xbox / PlayStation layout). id = 0..MaxGamepads-1
        /// @{
        static constexpr int MaxGamepads = 16;
        static bool IsGamepadConnected(int id = 0);
        static bool IsGamepadButtonPressed(GamepadButton button, int id = 0);
        static bool IsGamepadButtonJustPressed(GamepadButton button, int id = 0);
        static bool IsGamepadButtonJustReleased(GamepadButton button, int id = 0);
        /// Returns -1..1 (triggers: -1 released .. 1 pressed). Small values (< deadzone) return 0.
        static float GetGamepadAxis(GamepadAxis axis, int id = 0, float deadzone = 0.15f);
        static const char* GetGamepadName(int id = 0);
        /// @}

        /// @name Named actions / axes
        /// Named actions / axes (Project settings > Input, stored in the .zproject).
        /// Gamepad bindings listen to every connected gamepad. Unknown names return
        /// false / 0 and log a warning once.
        /// @{
        static bool IsActionPressed(std::string_view action);
        static bool IsActionJustPressed(std::string_view action);   ///< the action became active this frame
        static bool IsActionJustReleased(std::string_view action);  ///< the action stopped being active this frame
        /// -1..1: negative / positive buttons (-1 / +1) combined with the analog gamepad axis
        /// (deadzone applied and rescaled); the input with the larger magnitude wins.
        static float GetAxis(std::string_view axis);
        /// Two axes as a vector whose length is clamped to 1 (diagonals are not faster).
        static glm::vec2 GetAxis2D(std::string_view horizontal = "Horizontal", std::string_view vertical = "Vertical");
        /// A composite 2D axis of the map ("Move": its X and Y axes, radial deadzone and mode).
        static glm::vec2 GetVector(std::string_view vector);
        /// @}

        /// @name Hints of the device in use
        /// On-screen hints follow the device the player uses: the last one that did something - a key,
        /// a mouse button, the wheel or a mouse move of a few pixels (KeyboardMouse), a gamepad button or a
        /// stick / trigger pushed past half way (Gamepad). It starts as KeyboardMouse.
        /// @{
        static InputDevice GetActiveDevice();
        /// Sets it (tests, a game's own "controller hints" setting); the next input changes it again.
        static void SetActiveDevice(InputDevice device);
        /// Counts the device changes (a text that shows hints redraws when it changed).
        static uint64_t GetActiveDeviceVersion();
        /// The controls bound to an action, axis or vector of the project's map, for a hint: "Space / W",
        /// "A, D / Arrows", "Left stick" (DescribeInputBindings in InputActions.h; a game's language files
        /// rename controls with "input.<id>" keys).
        static std::string GetBindingText(std::string_view name, InputDevice device);
        static std::string GetBindingText(std::string_view name);   ///< of the active device
        /// `text` with every "[Name]" of the map's actions / axes / vectors replaced by its controls:
        /// "[Jump] - sakrash" -> "Space - sakrash" (keyboard) / "A - sakrash" (gamepad).
        static std::string ExpandBindings(std::string_view text, InputDevice device);
        static std::string ExpandBindings(std::string_view text);  ///< for the active device
        /// @}

        /// The same queries for a specific action / axis definition (editor preview, tools, tests).
        static bool IsActionPressed(const InputAction& action);
        static bool IsActionJustPressed(const InputAction& action);
        static bool IsActionJustReleased(const InputAction& action);
        static float GetAxis(const InputAxis& axis);
        /// Its axes are looked up in the project's map (an unknown one reads 0).
        static glm::vec2 GetVector(const InputVector& vector);
        /// The composite's rules on a raw (x, y) value: radial deadzone, then the mode.
        static glm::vec2 ShapeVector(const InputVector& vector, glm::vec2 value);

        /// 0 inside the deadzone, then rescaled so the output still covers 0..1 (sign kept).
        static float ApplyDeadzone(float value, float deadzone);

        /// @name Text input
        /// Text input (UI input fields): the characters typed and the key presses (auto-repeat
        /// included) since the previous frame, in the order they happened.
        /// @{
        static const std::vector<TextInputEvent>& GetTextInput();
        /// @}
        /// @name Clipboard
        /// Clipboard (the system's; without a window a clipboard of the process)
        /// @{
        static std::string GetClipboardText();
        static void SetClipboardText(std::string_view text);
        /// @}

        /// @name Frame state
        /// @{
        /// Captures the current device state (previous frame <- current frame). Called once
        /// per frame by the application; tests call it to advance simulated input.
        static void Update();
        /// Forgets every pressed key / button, simulated gamepad and pending event.
        static void ResetState();
        /// @}

        /// Device events (fed by the application from window events). Without a window they
        /// are the only input source, which makes them usable to simulate input headlessly.
        /// `repeat`: the keyboard's auto-repeat of a held key - only text input sees it.
        static void OnKeyEvent(Key key, bool pressed, bool repeat = false);
        /// A typed character (Unicode codepoint; the window's character callback).
        static void OnCharEvent(uint32_t codepoint);
        static void OnMouseButtonEvent(MouseButton button, bool pressed);
        static void OnMouseMovedEvent(float x, float y);
        static void OnMouseScrolledEvent(float xOffset, float yOffset);

        /// Simulated gamepad (tests / automation): the slot stops following the real device
        /// until ResetState(). Values become visible after the next Update().
        static void SimulateGamepadConnected(int id, bool connected);
        static void SimulateGamepadButton(int id, GamepadButton button, bool pressed);
        static void SimulateGamepadAxis(int id, GamepadAxis axis, float value);
        /// Simulated key (tests / automation: the player's ZAMIN_PLAYER_INPUT): pressed, it stays held - the
        /// window's keyboard poll leaves it alone - until released here (or ResetState()); a press and a release
        /// are events like a real key's (JustPressed / JustReleased, UI navigation, text input). Visible after
        /// the next Update().
        static void SimulateKey(Key key, bool pressed);
        static bool IsKeySimulated(Key key);
    };

}
