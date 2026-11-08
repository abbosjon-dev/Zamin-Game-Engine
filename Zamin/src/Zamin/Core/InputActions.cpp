#include "Zamin/Core/InputActions.h"
#include "Zamin/Localization/Localization.h"

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

    // ------------------------------------------------------------------ hints
    namespace {

        // A hint label: the game's "input.<id>" string when its language files have one, else `fallback`.
        std::string HintLabel(std::string_view id, std::string_view fallback)
        {
            if (const std::string* text = Localization::Find(std::format("input.{}", id)))
                return *text;
            return std::string(fallback);
        }

        const char* DefaultKeyHint(Key key)
        {
            switch (key)
            {
                case Key::Escape:       return "Esc";
                case Key::Backspace:    return "Backspace";
                case Key::LeftShift:
                case Key::RightShift:   return "Shift";
                case Key::LeftControl:
                case Key::RightControl: return "Ctrl";
                case Key::LeftAlt:
                case Key::RightAlt:     return "Alt";
                case Key::PageUp:       return "PgUp";
                case Key::PageDown:     return "PgDn";
                default:                return KeyToString(key);
            }
        }

        const char* DefaultPadHint(GamepadButton button)
        {
            switch (button)
            {
                case GamepadButton::LeftBumper:  return "LB";
                case GamepadButton::RightBumper: return "RB";
                case GamepadButton::LeftThumb:   return "L3";
                case GamepadButton::RightThumb:  return "R3";
                case GamepadButton::DPadUp:      return "D-pad Up";
                case GamepadButton::DPadRight:   return "D-pad Right";
                case GamepadButton::DPadDown:    return "D-pad Down";
                case GamepadButton::DPadLeft:    return "D-pad Left";
                default:                         return GamepadButtonToString(button);
            }
        }

        bool IsKey(const InputBinding& b, Key key) { return b.Source == InputSource::Key && b.Code == (int)key; }
        bool IsDPad(const InputBinding& b)
        {
            return b.Source == InputSource::GamepadButton && b.Code >= (int)GamepadButton::DPadUp && b.Code <= (int)GamepadButton::DPadLeft;
        }
        bool IsArrow(const InputBinding& b)
        {
            return IsKey(b, Key::Left) || IsKey(b, Key::Right) || IsKey(b, Key::Up) || IsKey(b, Key::Down);
        }

        void AddUnique(std::vector<std::string>& items, std::string item)
        {
            if (!item.empty() && std::find(items.begin(), items.end(), item) == items.end())
                items.push_back(std::move(item));
        }

        std::string Join(const std::vector<std::string>& items, std::string_view separator)
        {
            std::string out;
            for (const std::string& item : items)
            {
                if (!out.empty())
                    out += separator;
                out += item;
            }
            return out;
        }

        std::vector<InputBinding> OfDevice(const std::vector<InputBinding>& bindings, InputDevice device)
        {
            std::vector<InputBinding> out;
            for (const InputBinding& b : bindings)
                if (IsInputBindingOfDevice(b, device))
                    out.push_back(b);
            return out;
        }

        // A stick's name for an analog axis (both axes of a stick share it); "" for none.
        std::string StickHint(int axisCode)
        {
            if (axisCode < 0 || axisCode >= GamepadAxisCount)
                return {};
            return InputHintName(InputBinding::FromGamepadAxis((GamepadAxis)axisCode, 1));
        }

        // An axis' controls of one device: its sides paired up ("A, D"), arrows and D-pad pairs by one
        // name, its stick.
        void DescribeAxis(const InputAxis& axis, InputDevice device, std::vector<std::string>& out)
        {
            const std::vector<InputBinding> negative = OfDevice(axis.Negative, device), positive = OfDevice(axis.Positive, device);
            if (device == InputDevice::Gamepad)
                AddUnique(out, StickHint(axis.GamepadAxisCode));
            const size_t pairs = std::min(negative.size(), positive.size());
            for (size_t i = 0; i < pairs; i++)
            {
                const InputBinding& n = negative[i];
                const InputBinding& p = positive[i];
                if (IsArrow(n) && IsArrow(p))
                    AddUnique(out, HintLabel("key.Arrows", "Arrows"));
                else if (IsDPad(n) && IsDPad(p))
                    AddUnique(out, HintLabel("pad.DPad", "D-pad"));
                else if (n.Source == InputSource::GamepadAxis && p.Source == InputSource::GamepadAxis && n.Code == p.Code)
                    AddUnique(out, StickHint(n.Code)); // both halves of one stick axis
                else
                    AddUnique(out, std::format("{}, {}", InputHintName(n), InputHintName(p)));
            }
            // Unpaired controls of a lopsided axis.
            for (size_t i = pairs; i < negative.size(); i++)
                AddUnique(out, InputHintName(negative[i]));
            for (size_t i = pairs; i < positive.size(); i++)
                AddUnique(out, InputHintName(positive[i]));
        }

        // A vector: four letter keys as one word ("WASD"), arrows and D-pads by one name, else both axes.
        void DescribeVector(const InputActionMap& map, const InputVector& vector, InputDevice device, std::vector<std::string>& out)
        {
            const InputAxis* x = map.FindAxis(vector.XAxis);
            const InputAxis* y = map.FindAxis(vector.YAxis);
            if (!x || !y)
            {
                if (x) DescribeAxis(*x, device, out);
                if (y) DescribeAxis(*y, device, out);
                return;
            }
            if (device == InputDevice::Gamepad)
            {
                AddUnique(out, StickHint(x->GamepadAxisCode));
                AddUnique(out, StickHint(y->GamepadAxisCode));
            }
            const std::vector<InputBinding> left = OfDevice(x->Negative, device), right = OfDevice(x->Positive, device);
            const std::vector<InputBinding> down = OfDevice(y->Negative, device), up = OfDevice(y->Positive, device);
            const size_t sets = std::min({ left.size(), right.size(), down.size(), up.size() });
            for (size_t i = 0; i < sets; i++)
            {
                const InputBinding* four[] = { &up[i], &left[i], &down[i], &right[i] };
                if (std::all_of(std::begin(four), std::end(four), [](const InputBinding* b) { return IsArrow(*b); }))
                    AddUnique(out, HintLabel("key.Arrows", "Arrows"));
                else if (std::all_of(std::begin(four), std::end(four), [](const InputBinding* b) { return IsDPad(*b); }))
                    AddUnique(out, HintLabel("pad.DPad", "D-pad"));
                else
                {
                    std::string word, listed;
                    bool letters = true;
                    for (const InputBinding* b : four)
                    {
                        const std::string name = InputHintName(*b);
                        letters = letters && b->Source == InputSource::Key && name.size() == 1;
                        word += name;
                        listed += (listed.empty() ? "" : ", ") + name;
                    }
                    AddUnique(out, letters ? word : listed);
                }
            }
            if (sets == 0)
            {
                DescribeAxis(*x, device, out);
                DescribeAxis(*y, device, out);
            }
        }

    }

    const char* InputDeviceToString(InputDevice device)
    {
        return device == InputDevice::Gamepad ? "Gamepad" : "KeyboardMouse";
    }

    bool IsInputBindingOfDevice(const InputBinding& binding, InputDevice device)
    {
        const bool pad = binding.Source == InputSource::GamepadButton || binding.Source == InputSource::GamepadAxis;
        return pad == (device == InputDevice::Gamepad);
    }

    std::string InputHintName(const InputBinding& b)
    {
        switch (b.Source)
        {
            case InputSource::Key:
            {
                const char* name = KeyToString((Key)b.Code);
                return HintLabel(std::format("key.{}", name), DefaultKeyHint((Key)b.Code));
            }
            case InputSource::Mouse:
            {
                static constexpr const char* s_Short[] = { "LMB", "RMB", "MMB" };
                const char* name = MouseButtonToString((MouseButton)b.Code);
                const std::string fallback = b.Code >= 0 && b.Code < 3 ? s_Short[b.Code] : std::format("Mouse {}", b.Code + 1);
                return HintLabel(std::format("mouse.{}", name), fallback);
            }
            case InputSource::GamepadButton:
            {
                const GamepadButton button = (GamepadButton)b.Code;
                return HintLabel(std::format("pad.{}", GamepadButtonToString(button)), DefaultPadHint(button));
            }
            case InputSource::GamepadAxis:
            {
                const GamepadAxis axis = (GamepadAxis)b.Code;
                if (axis == GamepadAxis::LeftTrigger)
                    return HintLabel("pad.LeftTrigger", "LT");
                if (axis == GamepadAxis::RightTrigger)
                    return HintLabel("pad.RightTrigger", "RT");
                const bool left = axis == GamepadAxis::LeftX || axis == GamepadAxis::LeftY;
                return left ? HintLabel("stick.Left", "Left stick") : HintLabel("stick.Right", "Right stick");
            }
        }
        return {};
    }

    std::string DescribeInputBindings(const InputActionMap& map, std::string_view name, InputDevice device, std::string_view separator)
    {
        std::vector<std::string> items;
        if (const InputAction* action = map.FindAction(name))
        {
            for (const InputBinding& b : OfDevice(action->Bindings, device))
                AddUnique(items, InputHintName(b));
        }
        else if (const InputAxis* axis = map.FindAxis(name))
            DescribeAxis(*axis, device, items);
        else if (const InputVector* vector = map.FindVector(name))
            DescribeVector(map, *vector, device, items);
        return Join(items, separator);
    }

    std::string ExpandInputBindings(const InputActionMap& map, std::string_view text, InputDevice device)
    {
        std::string out;
        out.reserve(text.size() + 16);
        size_t at = 0;
        while (at < text.size())
        {
            const size_t open = text.find('[', at);
            const size_t close = open == std::string_view::npos ? open : text.find(']', open + 1);
            if (close == std::string_view::npos)
                break;
            std::string_view name = text.substr(open + 1, close - open - 1);
            out.append(text.substr(at, open - at));
            at = close + 1;
            // "[kb:text]" / "[pad:text]": text for one device only.
            if (name.starts_with("kb:") || name.starts_with("pad:"))
            {
                const bool pad = name.starts_with("pad:");
                if (pad == (device == InputDevice::Gamepad))
                    out.append(name.substr(pad ? 4 : 3));
                continue;
            }
            // "[Name|fallback]": the fallback when nothing of the device is bound.
            std::string_view fallback = "-";
            if (const size_t bar = name.find('|'); bar != std::string_view::npos)
            {
                fallback = name.substr(bar + 1);
                name = name.substr(0, bar);
            }
            if (!name.empty() && (map.FindAction(name) || map.FindAxis(name) || map.FindVector(name)))
            {
                const std::string controls = DescribeInputBindings(map, name, device);
                out += controls.empty() ? std::string(fallback) : controls;
            }
            else
                out.append(text.substr(open, close - open + 1)); // not a name of the map: as it was
        }
        out.append(text.substr(std::min(at, text.size())));
        return out;
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
        map.Vectors = { InputVector{ "Move", "Horizontal", "Vertical", InputVectorMode::Clamp, 0.0f } };
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

    InputVector* InputActionMap::FindVector(std::string_view name)
    {
        for (auto& v : Vectors)
            if (v.Name == name)
                return &v;
        return nullptr;
    }

    const InputVector* InputActionMap::FindVector(std::string_view name) const
    {
        return const_cast<InputActionMap*>(this)->FindVector(name);
    }

    std::string InputActionMap::MakeUniqueActionName(std::string_view base) const { return MakeUniqueName(Actions, base); }
    std::string InputActionMap::MakeUniqueAxisName(std::string_view base) const { return MakeUniqueName(Axes, base); }
    std::string InputActionMap::MakeUniqueVectorName(std::string_view base) const { return MakeUniqueName(Vectors, base); }

    std::vector<InputBindingConflict> InputActionMap::FindConflicts() const
    {
        // Every place each control is bound, in map order.
        std::vector<InputBindingConflict> uses;
        auto add = [&](const InputBinding& binding, const std::string& user) {
            for (InputBindingConflict& u : uses)
                if (u.Binding == binding)
                {
                    u.Users.push_back(user);
                    return;
                }
            uses.push_back({ binding, { user } });
        };
        for (const InputAction& action : Actions)
            for (const InputBinding& b : action.Bindings)
                add(b, action.Name);
        for (const InputAxis& axis : Axes)
        {
            for (const InputBinding& b : axis.Negative)
                add(b, axis.Name + " (-)");
            for (const InputBinding& b : axis.Positive)
                add(b, axis.Name + " (+)");
        }
        std::vector<InputBindingConflict> conflicts;
        for (InputBindingConflict& u : uses)
            if (u.Users.size() > 1)
                conflicts.push_back(std::move(u));
        return conflicts;
    }

    std::vector<std::string> InputActionMap::FindUsers(const InputBinding& binding) const
    {
        std::vector<std::string> users;
        for (const InputAction& action : Actions)
            for (const InputBinding& b : action.Bindings)
                if (b == binding)
                    users.push_back(action.Name);
        for (const InputAxis& axis : Axes)
        {
            for (const InputBinding& b : axis.Negative)
                if (b == binding)
                    users.push_back(axis.Name + " (-)");
            for (const InputBinding& b : axis.Positive)
                if (b == binding)
                    users.push_back(axis.Name + " (+)");
        }
        return users;
    }

    const char* InputVectorModeToString(InputVectorMode mode)
    {
        switch (mode)
        {
            case InputVectorMode::Clamp:     return "Clamp";
            case InputVectorMode::Normalize: return "Normalize";
            case InputVectorMode::Raw:       return "Raw";
        }
        return "Clamp";
    }

    bool InputVectorModeFromString(std::string_view name, InputVectorMode& out)
    {
        for (int i = 0; i < InputVectorModeCount; i++)
            if (name == InputVectorModeToString((InputVectorMode)i))
            {
                out = (InputVectorMode)i;
                return true;
            }
        return false;
    }

}
