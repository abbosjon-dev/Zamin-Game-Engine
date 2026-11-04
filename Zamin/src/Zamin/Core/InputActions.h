#pragma once

#include "Zamin/Core/KeyCodes.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Zamin {

    // Physical input a binding listens to.
    enum class InputSource : uint8_t { Key = 0, Mouse, GamepadButton, GamepadAxis };

    // One physical control: a key, a mouse button, a gamepad button, or one half of a
    // gamepad axis (Direction +1 / -1). A gamepad axis acts as a button when
    // value * Direction > 0.5 (triggers are mapped from -1..1 to 0..1 first).
    struct InputBinding
    {
        InputSource Source = InputSource::Key;
        int Code = 0;       // Key / MouseButton / GamepadButton / GamepadAxis value
        int Direction = 1;  // GamepadAxis only

        static InputBinding FromKey(Key key) { return { InputSource::Key, (int)key, 1 }; }
        static InputBinding FromMouse(MouseButton button) { return { InputSource::Mouse, (int)button, 1 }; }
        static InputBinding FromGamepadButton(GamepadButton button) { return { InputSource::GamepadButton, (int)button, 1 }; }
        static InputBinding FromGamepadAxis(GamepadAxis axis, int direction) { return { InputSource::GamepadAxis, (int)axis, direction < 0 ? -1 : 1 }; }

        bool operator==(const InputBinding& other) const
        {
            return Source == other.Source && Code == other.Code && (Source != InputSource::GamepadAxis || Direction == other.Direction);
        }
    };

    // Named button-like action ("Jump"): active while any of its bindings is.
    struct InputAction
    {
        std::string Name;
        std::vector<InputBinding> Bindings;
    };

    // Named axis in -1..1 ("Horizontal"): negative / positive buttons plus an
    // optional analog gamepad axis with a deadzone.
    struct InputAxis
    {
        std::string Name;
        std::vector<InputBinding> Negative;
        std::vector<InputBinding> Positive;
        int GamepadAxisCode = -1;   // GamepadAxis value, -1 = none
        bool Invert = false;        // e.g. stick Y (up is -1 on gamepads)
        float Deadzone = 0.15f;
    };

    // The project's input configuration (stored in the .zproject file).
    struct InputActionMap
    {
        std::vector<InputAction> Actions;
        std::vector<InputAxis> Axes;

        // Jump, Fire, Interact, Sprint, Pause + Horizontal / Vertical axes.
        static InputActionMap CreateDefault();

        InputAction* FindAction(std::string_view name);
        const InputAction* FindAction(std::string_view name) const;
        InputAxis* FindAxis(std::string_view name);
        const InputAxis* FindAxis(std::string_view name) const;
        bool IsEmpty() const { return Actions.empty() && Axes.empty(); }

        // Returns `base` if unused, otherwise base + " 2", " 3", ... (for new entries in the editor).
        std::string MakeUniqueActionName(std::string_view base) const;
        std::string MakeUniqueAxisName(std::string_view base) const;
    };

    // ---- names (stable identifiers used by the project file and the editor)
    const char* KeyToString(Key key);                 // "Space", "A", "LeftShift" ... ("" if unknown)
    bool KeyFromString(std::string_view name, Key& out);
    std::span<const Key> GetAllKeys();                // every named key, in a UI-friendly order

    const char* MouseButtonToString(MouseButton button);
    bool MouseButtonFromString(std::string_view name, MouseButton& out);
    inline constexpr int MouseButtonCount = 8;

    const char* GamepadButtonToString(GamepadButton button);
    bool GamepadButtonFromString(std::string_view name, GamepadButton& out);
    inline constexpr int GamepadButtonCount = 15;

    const char* GamepadAxisToString(GamepadAxis axis);
    bool GamepadAxisFromString(std::string_view name, GamepadAxis& out);
    inline constexpr int GamepadAxisCount = 6;

    const char* InputSourceToString(InputSource source);

    // "Key:Space", "Mouse:Left", "Gamepad:A", "GamepadAxis:LeftX+" / "GamepadAxis:LeftY-"
    std::string InputBindingToString(const InputBinding& binding);
    bool InputBindingFromString(std::string_view text, InputBinding& out);
    // Short label for the UI: "Space", "Sichqoncha Left", "Gamepad A", "LeftX +"
    std::string InputBindingDisplayName(const InputBinding& binding);

}
