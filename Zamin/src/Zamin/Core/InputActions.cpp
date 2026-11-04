#include "Zamin/Core/InputActions.h"

#include <algorithm>
#include <array>
#include <format>

namespace Zamin {

    namespace {

        struct KeyName { Key Code; const char* Name; };

        // UI order: letters, digits, arrows/navigation, modifiers, function keys, punctuation, keypad.
        constexpr KeyName s_KeyNames[] = {
            { Key::A, "A" }, { Key::B, "B" }, { Key::C, "C" }, { Key::D, "D" }, { Key::E, "E" }, { Key::F, "F" },
            { Key::G, "G" }, { Key::H, "H" }, { Key::I, "I" }, { Key::J, "J" }, { Key::K, "K" }, { Key::L, "L" },
            { Key::M, "M" }, { Key::N, "N" }, { Key::O, "O" }, { Key::P, "P" }, { Key::Q, "Q" }, { Key::R, "R" },
            { Key::S, "S" }, { Key::T, "T" }, { Key::U, "U" }, { Key::V, "V" }, { Key::W, "W" }, { Key::X, "X" },
            { Key::Y, "Y" }, { Key::Z, "Z" },
            { Key::D0, "0" }, { Key::D1, "1" }, { Key::D2, "2" }, { Key::D3, "3" }, { Key::D4, "4" },
            { Key::D5, "5" }, { Key::D6, "6" }, { Key::D7, "7" }, { Key::D8, "8" }, { Key::D9, "9" },
            { Key::Space, "Space" }, { Key::Enter, "Enter" }, { Key::Escape, "Escape" }, { Key::Tab, "Tab" },
            { Key::Backspace, "Backspace" },
            { Key::Left, "Left" }, { Key::Right, "Right" }, { Key::Up, "Up" }, { Key::Down, "Down" },
            { Key::Insert, "Insert" }, { Key::Delete, "Delete" }, { Key::Home, "Home" }, { Key::End, "End" },
            { Key::PageUp, "PageUp" }, { Key::PageDown, "PageDown" },
            { Key::LeftShift, "LeftShift" }, { Key::RightShift, "RightShift" },
            { Key::LeftControl, "LeftControl" }, { Key::RightControl, "RightControl" },
            { Key::LeftAlt, "LeftAlt" }, { Key::RightAlt, "RightAlt" },
            { Key::LeftSuper, "LeftSuper" }, { Key::RightSuper, "RightSuper" }, { Key::Menu, "Menu" },
            { Key::CapsLock, "CapsLock" }, { Key::ScrollLock, "ScrollLock" }, { Key::NumLock, "NumLock" },
            { Key::PrintScreen, "PrintScreen" }, { Key::Pause, "Pause" },
            { Key::F1, "F1" }, { Key::F2, "F2" }, { Key::F3, "F3" }, { Key::F4, "F4" }, { Key::F5, "F5" },
            { Key::F6, "F6" }, { Key::F7, "F7" }, { Key::F8, "F8" }, { Key::F9, "F9" }, { Key::F10, "F10" },
            { Key::F11, "F11" }, { Key::F12, "F12" },
            { Key::Apostrophe, "Apostrophe" }, { Key::Comma, "Comma" }, { Key::Minus, "Minus" },
            { Key::Period, "Period" }, { Key::Slash, "Slash" }, { Key::Semicolon, "Semicolon" },
            { Key::Equal, "Equal" }, { Key::LeftBracket, "LeftBracket" }, { Key::Backslash, "Backslash" },
            { Key::RightBracket, "RightBracket" }, { Key::GraveAccent, "GraveAccent" },
            { Key::KP0, "KP0" }, { Key::KP1, "KP1" }, { Key::KP2, "KP2" }, { Key::KP3, "KP3" }, { Key::KP4, "KP4" },
            { Key::KP5, "KP5" }, { Key::KP6, "KP6" }, { Key::KP7, "KP7" }, { Key::KP8, "KP8" }, { Key::KP9, "KP9" },
            { Key::KPDecimal, "KPDecimal" }, { Key::KPDivide, "KPDivide" }, { Key::KPMultiply, "KPMultiply" },
            { Key::KPSubtract, "KPSubtract" }, { Key::KPAdd, "KPAdd" }, { Key::KPEnter, "KPEnter" },
            { Key::KPEqual, "KPEqual" },
        };

        constexpr auto s_AllKeys = [] {
            std::array<Key, std::size(s_KeyNames)> keys{};
            for (size_t i = 0; i < keys.size(); i++)
                keys[i] = s_KeyNames[i].Code;
            return keys;
        }();

        constexpr const char* s_MouseNames[MouseButtonCount] = {
            "Left", "Right", "Middle", "Button3", "Button4", "Button5", "Button6", "Button7"
        };

        constexpr const char* s_GamepadButtonNames[GamepadButtonCount] = {
            "A", "B", "X", "Y", "LeftBumper", "RightBumper", "Back", "Start", "Guide",
            "LeftThumb", "RightThumb", "DPadUp", "DPadRight", "DPadDown", "DPadLeft"
        };

        constexpr const char* s_GamepadAxisNames[GamepadAxisCount] = {
            "LeftX", "LeftY", "RightX", "RightY", "LeftTrigger", "RightTrigger"
        };

        template<size_t N>
        int IndexOf(const char* const (&names)[N], std::string_view name)
        {
            for (size_t i = 0; i < N; i++)
                if (name == names[i])
                    return (int)i;
            return -1;
        }

        template<typename T>
        std::string MakeUniqueName(const std::vector<T>& items, std::string_view base)
        {
            auto taken = [&](const std::string& n) {
                return std::any_of(items.begin(), items.end(), [&](const T& item) { return item.Name == n; });
            };
            std::string name(base);
            for (int i = 2; taken(name); i++)
                name = std::format("{} {}", base, i);
            return name;
        }

    }

    // ------------------------------------------------------------------ names
    const char* KeyToString(Key key)
    {
        for (const auto& k : s_KeyNames)
            if (k.Code == key)
                return k.Name;
        return "";
    }

    bool KeyFromString(std::string_view name, Key& out)
    {
        for (const auto& k : s_KeyNames)
            if (name == k.Name)
            {
                out = k.Code;
                return true;
            }
        return false;
    }

    std::span<const Key> GetAllKeys() { return s_AllKeys; }

    const char* MouseButtonToString(MouseButton button)
    {
        int i = (int)button;
        return i >= 0 && i < MouseButtonCount ? s_MouseNames[i] : "";
    }

    bool MouseButtonFromString(std::string_view name, MouseButton& out)
    {
        int i = IndexOf(s_MouseNames, name);
        if (i < 0) return false;
        out = (MouseButton)i;
        return true;
    }

    const char* GamepadButtonToString(GamepadButton button)
    {
        int i = (int)button;
        return i >= 0 && i < GamepadButtonCount ? s_GamepadButtonNames[i] : "";
    }

    bool GamepadButtonFromString(std::string_view name, GamepadButton& out)
    {
        int i = IndexOf(s_GamepadButtonNames, name);
        if (i < 0) return false;
        out = (GamepadButton)i;
        return true;
    }

    const char* GamepadAxisToString(GamepadAxis axis)
    {
        int i = (int)axis;
        return i >= 0 && i < GamepadAxisCount ? s_GamepadAxisNames[i] : "";
    }

    bool GamepadAxisFromString(std::string_view name, GamepadAxis& out)
    {
        int i = IndexOf(s_GamepadAxisNames, name);
        if (i < 0) return false;
        out = (GamepadAxis)i;
        return true;
    }

    const char* InputSourceToString(InputSource source)
    {
        switch (source)
        {
            case InputSource::Key:           return "Key";
            case InputSource::Mouse:         return "Mouse";
            case InputSource::GamepadButton: return "Gamepad";
            case InputSource::GamepadAxis:   return "GamepadAxis";
        }
        return "Key";
    }

    std::string InputBindingToString(const InputBinding& b)
    {
        switch (b.Source)
        {
            case InputSource::Key:           return std::format("Key:{}", KeyToString((Key)b.Code));
            case InputSource::Mouse:         return std::format("Mouse:{}", MouseButtonToString((MouseButton)b.Code));
            case InputSource::GamepadButton: return std::format("Gamepad:{}", GamepadButtonToString((GamepadButton)b.Code));
            case InputSource::GamepadAxis:   return std::format("GamepadAxis:{}{}", GamepadAxisToString((GamepadAxis)b.Code), b.Direction < 0 ? '-' : '+');
        }
        return {};
    }

    bool InputBindingFromString(std::string_view text, InputBinding& out)
    {
        size_t colon = text.find(':');
        if (colon == std::string_view::npos) return false;
        std::string_view source = text.substr(0, colon), name = text.substr(colon + 1);

        if (source == "Key")
        {
            Key k;
            if (!KeyFromString(name, k)) return false;
            out = InputBinding::FromKey(k);
            return true;
        }
        if (source == "Mouse")
        {
            MouseButton m;
            if (!MouseButtonFromString(name, m)) return false;
            out = InputBinding::FromMouse(m);
            return true;
        }
        if (source == "Gamepad")
        {
            GamepadButton g;
            if (!GamepadButtonFromString(name, g)) return false;
            out = InputBinding::FromGamepadButton(g);
            return true;
        }
        if (source == "GamepadAxis")
        {
            int direction = 1;
            if (!name.empty() && (name.back() == '+' || name.back() == '-'))
            {
                direction = name.back() == '-' ? -1 : 1;
                name.remove_suffix(1);
            }
            GamepadAxis a;
            if (!GamepadAxisFromString(name, a)) return false;
            out = InputBinding::FromGamepadAxis(a, direction);
            return true;
        }
        return false;
    }

    std::string InputBindingDisplayName(const InputBinding& b)
    {
        switch (b.Source)
        {
            case InputSource::Key:           return KeyToString((Key)b.Code);
            case InputSource::Mouse:         return std::format("Sichqoncha {}", MouseButtonToString((MouseButton)b.Code));
            case InputSource::GamepadButton: return std::format("Gamepad {}", GamepadButtonToString((GamepadButton)b.Code));
            case InputSource::GamepadAxis:   return std::format("Gamepad {} {}", GamepadAxisToString((GamepadAxis)b.Code), b.Direction < 0 ? '-' : '+');
        }
        return {};
    }

    // ------------------------------------------------------------------ map
    InputActionMap InputActionMap::CreateDefault()
    {
        using B = InputBinding;
        InputActionMap map;
        map.Actions = {
            { "Jump",     { B::FromKey(Key::Space), B::FromGamepadButton(GamepadButton::A) } },
            { "Fire",     { B::FromMouse(MouseButton::Left), B::FromKey(Key::LeftControl), B::FromGamepadAxis(GamepadAxis::RightTrigger, 1) } },
            { "Interact", { B::FromKey(Key::E), B::FromGamepadButton(GamepadButton::X) } },
            { "Sprint",   { B::FromKey(Key::LeftShift), B::FromGamepadButton(GamepadButton::LeftThumb) } },
            { "Pause",    { B::FromKey(Key::Escape), B::FromGamepadButton(GamepadButton::Start) } },
        };

        InputAxis horizontal;
        horizontal.Name = "Horizontal";
        horizontal.Negative = { B::FromKey(Key::A), B::FromKey(Key::Left), B::FromGamepadButton(GamepadButton::DPadLeft) };
        horizontal.Positive = { B::FromKey(Key::D), B::FromKey(Key::Right), B::FromGamepadButton(GamepadButton::DPadRight) };
        horizontal.GamepadAxisCode = (int)GamepadAxis::LeftX;

        InputAxis vertical;
        vertical.Name = "Vertical";
        vertical.Negative = { B::FromKey(Key::S), B::FromKey(Key::Down), B::FromGamepadButton(GamepadButton::DPadDown) };
        vertical.Positive = { B::FromKey(Key::W), B::FromKey(Key::Up), B::FromGamepadButton(GamepadButton::DPadUp) };
        vertical.GamepadAxisCode = (int)GamepadAxis::LeftY;
        vertical.Invert = true; // stick up is -1

        map.Axes = { std::move(horizontal), std::move(vertical) };
        return map;
    }

    InputAction* InputActionMap::FindAction(std::string_view name)
    {
        for (auto& a : Actions)
            if (a.Name == name)
                return &a;
        return nullptr;
    }

    const InputAction* InputActionMap::FindAction(std::string_view name) const
    {
        return const_cast<InputActionMap*>(this)->FindAction(name);
    }

    InputAxis* InputActionMap::FindAxis(std::string_view name)
    {
        for (auto& a : Axes)
            if (a.Name == name)
                return &a;
        return nullptr;
    }

    const InputAxis* InputActionMap::FindAxis(std::string_view name) const
    {
        return const_cast<InputActionMap*>(this)->FindAxis(name);
    }

    std::string InputActionMap::MakeUniqueActionName(std::string_view base) const { return MakeUniqueName(Actions, base); }
    std::string InputActionMap::MakeUniqueAxisName(std::string_view base) const { return MakeUniqueName(Axes, base); }

}
