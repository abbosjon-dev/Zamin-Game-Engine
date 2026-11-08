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

    // The two kinds of controls a player uses (on-screen hints: Input::GetActiveDevice).
    enum class InputDevice : uint8_t { KeyboardMouse = 0, Gamepad };

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

    // How a composite 2D axis treats its value (after the radial deadzone).
    enum class InputVectorMode : uint8_t
    {
        Clamp = 0,  // longer than 1 (keyboard diagonals) is shortened to 1; a half tilted stick stays half
        Normalize,  // any direction has length 1 (digital feel); nothing pressed stays (0, 0)
        Raw,        // the two axes as they are (diagonals up to ~1.41)
    };
    constexpr int InputVectorModeCount = 3;

    // Named 2D composite axis ("Move"): two of the map's axes as X and Y, read together with
    // Input::GetVector("Move"). The radial deadzone acts on the combined length (a stick drifting a
    // little in any direction reads 0), then the length is rescaled to start at 0.
    struct InputVector
    {
        std::string Name;
        std::string XAxis = "Horizontal";
        std::string YAxis = "Vertical";
        InputVectorMode Mode = InputVectorMode::Clamp;
        float Deadzone = 0.0f; // 0 .. 0.95 of the length

        bool operator==(const InputVector&) const = default;
    };

    // One physical control bound more than once in the map (Project settings > Input warns about it):
    // `Users` names each place, e.g. "Jump", "Horizontal (+)"; the same list twice is listed twice.
    struct InputBindingConflict
    {
        InputBinding Binding;
        std::vector<std::string> Users;
    };

    // The project's input configuration (stored in the .zproject file).
    struct InputActionMap
    {
        std::vector<InputAction> Actions;
        std::vector<InputAxis> Axes;
        std::vector<InputVector> Vectors;

        // Jump, Fire, Interact, Sprint, Pause + Horizontal / Vertical axes + the Move vector.
        static InputActionMap CreateDefault();

        InputAction* FindAction(std::string_view name);
        const InputAction* FindAction(std::string_view name) const;
        InputAxis* FindAxis(std::string_view name);
        const InputAxis* FindAxis(std::string_view name) const;
        InputVector* FindVector(std::string_view name);
        const InputVector* FindVector(std::string_view name) const;
        bool IsEmpty() const { return Actions.empty() && Axes.empty() && Vectors.empty(); }

        // Returns `base` if unused, otherwise base + " 2", " 3", ... (for new entries in the editor).
        std::string MakeUniqueActionName(std::string_view base) const;
        std::string MakeUniqueAxisName(std::string_view base) const;
        std::string MakeUniqueVectorName(std::string_view base) const;

        // Every control bound in more than one place (two actions, an action and an axis side, both
        // sides of an axis, or twice in one list), in the order they are first found. A shared control
        // can be intended (Gamepad A for Jump and a menu's Submit); the editor only warns.
        std::vector<InputBindingConflict> FindConflicts() const;
        // The places that use this control ("Jump", "Horizontal (-)" ...).
        std::vector<std::string> FindUsers(const InputBinding& binding) const;
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
    const char* InputVectorModeToString(InputVectorMode mode);   // "Clamp", "Normalize", "Raw"
    bool InputVectorModeFromString(std::string_view name, InputVectorMode& out);

    // "Key:Space", "Mouse:Left", "Gamepad:A", "GamepadAxis:LeftX+" / "GamepadAxis:LeftY-"
    std::string InputBindingToString(const InputBinding& binding);
    bool InputBindingFromString(std::string_view text, InputBinding& out);
    // Short label for the UI: "Space", "Sichqoncha Left", "Gamepad A", "LeftX +"
    std::string InputBindingDisplayName(const InputBinding& binding);

    // ---- on-screen hints ("[Jump] - sakrash" -> "Space - sakrash" / "A - sakrash")
    const char* InputDeviceToString(InputDevice device);       // "KeyboardMouse", "Gamepad"
    // Whether the binding is a control of that device (keys and mouse buttons / gamepad buttons and axes).
    bool IsInputBindingOfDevice(const InputBinding& binding, InputDevice device);
    // The name a hint shows for one control: "Space", "Esc", "Shift", "LMB", "A", "LB", "Start", "D-pad Up",
    // "Left stick", "RT" ... A game renames any of them in its language files with "input.<id>" keys, ids:
    // "key.<Key>" (key.Escape, key.Left), "mouse.<Button>" (mouse.Left), "pad.<Button>" (pad.Start,
    // pad.DPadUp), "stick.Left" / "stick.Right" (a stick's X or Y axis), "pad.LeftTrigger" / "pad.RightTrigger";
    // and for pairs: "key.Arrows" (Left + Right, Up + Down or all four), "pad.DPad" (two or four D-pad buttons).
    std::string InputHintName(const InputBinding& binding);
    // The controls of one device bound to an action, an axis or a vector of `map` (looked up in that order)
    // as hint text: an action lists its bindings ("Space / W"); an axis pairs its sides ("A, D / Arrows") and
    // names its stick ("Left stick / D-pad"); a vector joins its two axes ("WASD / Arrows", "Left stick").
    // Repeated names are listed once. Empty when nothing of that device is bound (or no such name).
    std::string DescribeInputBindings(const InputActionMap& map, std::string_view name, InputDevice device,
                                      std::string_view separator = " / ");
    // `text` with every "[Name]" of an action / axis / vector of `map` replaced by DescribeInputBindings
    // (localized hint lines); other brackets are left as they are. A name nothing of that device is bound to
    // reads "-", or the text after a bar: "[Look|Sichqoncha]". "[kb:text]" / "[pad:text]" show their text only
    // for the keyboard and mouse / only for a gamepad: "[kb:Sichqoncha / ][Aim] - nishon".
    std::string ExpandInputBindings(const InputActionMap& map, std::string_view text, InputDevice device);

}
