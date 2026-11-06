#include "Zamin/Core/Input.h"
#include "Zamin/Core/InputState.h"
#include "Zamin/Core/Log.h"
#include "Zamin/Project/Project.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_set>

namespace Zamin {

    namespace Internal {

        InputState& GetInputState()
        {
            static InputState s_State;
            return s_State;
        }

    }

    namespace {

        using Internal::ButtonState;
        using Internal::GamepadState;
        using Internal::InputState;

        InputState& State() { return Internal::GetInputState(); }

        const ButtonState* KeyState(Key key)
        {
            int code = (int)key;
            return code >= Internal::InputFirstKey && code < Internal::InputKeyCount ? &State().Keys[code] : nullptr;
        }

        const ButtonState* MouseState(MouseButton button)
        {
            int code = (int)button;
            return code >= 0 && code < MouseButtonCount ? &State().Mouse[code] : nullptr;
        }

        const Internal::GamepadSlot* GamepadSlot(int id)
        {
            return id >= 0 && id < Input::MaxGamepads ? &State().Gamepads[id] : nullptr;
        }

        bool JustPressed(const ButtonState& b) { return b.Pressed || (b.Down && !b.WasDown); }
        bool JustReleased(const ButtonState& b) { return b.Released || (!b.Down && b.WasDown); }

        bool IsTrigger(int axis) { return axis == (int)GamepadAxis::LeftTrigger || axis == (int)GamepadAxis::RightTrigger; }

        // Triggers report -1 (released) .. 1 (pressed); everything else uses them as 0..1.
        float AxisValue01(const GamepadState& pad, int axis)
        {
            float v = pad.Axes[axis];
            return IsTrigger(axis) ? (v + 1.0f) * 0.5f : v;
        }

        // One physical control of a binding during the last two frames. A gamepad binding
        // yields one sample per connected gamepad.
        struct Sample
        {
            bool Down = false, WasDown = false, Pressed = false, Released = false;

            bool JustPressed() const { return Pressed || (Down && !WasDown); }
            bool JustReleased() const { return Released || (!Down && WasDown); }
            // Held through the whole frame transition (keeps an action active without a new press).
            bool HeldAcross() const { return Down && WasDown && !Released; }
        };

        Sample FromButton(const ButtonState& b) { return { b.Down, b.WasDown, b.Pressed, b.Released }; }

        template<typename F>
        void ForEachSample(const InputBinding& binding, F&& fn)
        {
            switch (binding.Source)
            {
                case InputSource::Key:
                    if (const ButtonState* b = KeyState((Key)binding.Code))
                        fn(FromButton(*b));
                    return;
                case InputSource::Mouse:
                    if (const ButtonState* b = MouseState((MouseButton)binding.Code))
                        fn(FromButton(*b));
                    return;
                case InputSource::GamepadButton:
                    if (binding.Code < 0 || binding.Code >= GamepadButtonCount)
                        return;
                    for (const auto& slot : State().Gamepads)
                        if (slot.Current.Connected || slot.Previous.Connected)
                            fn(Sample{ slot.Current.Connected && slot.Current.Buttons[binding.Code],
                                       slot.Previous.Connected && slot.Previous.Buttons[binding.Code] });
                    return;
                case InputSource::GamepadAxis:
                {
                    if (binding.Code < 0 || binding.Code >= GamepadAxisCount)
                        return;
                    // An axis half acts as a button past the middle of its range.
                    auto active = [&](const GamepadState& pad) {
                        return pad.Connected && AxisValue01(pad, binding.Code) * (float)binding.Direction > 0.5f;
                    };
                    for (const auto& slot : State().Gamepads)
                        if (slot.Current.Connected || slot.Previous.Connected)
                            fn(Sample{ active(slot.Current), active(slot.Previous) });
                    return;
                }
            }
        }

        bool AnyDown(const std::vector<InputBinding>& bindings)
        {
            bool down = false;
            for (const auto& b : bindings)
                ForEachSample(b, [&](const Sample& s) { down |= s.Down; });
            return down;
        }

        // Each unknown name is reported once (scripts usually query every frame).
        void WarnUnknown(const char* kind, std::string_view name)
        {
            static std::unordered_set<std::string> s_Reported;
            if (s_Reported.emplace(name).second)
                ZM_CORE_WARN("Input: '{}' nomli {} topilmadi (Loyiha sozlamalari > Input)", name, kind);
        }

        const InputAction* LookupAction(std::string_view name)
        {
            const InputAction* action = Project::GetInputActionMap().FindAction(name);
            if (!action)
                WarnUnknown("harakat", name);
            return action;
        }

        const InputAxis* LookupAxis(std::string_view name)
        {
            const InputAxis* axis = Project::GetInputActionMap().FindAxis(name);
            if (!axis)
                WarnUnknown("o'q", name);
            return axis;
        }

    }

    // ------------------------------------------------------------------ keyboard / mouse
    bool Input::IsKeyPressed(Key key)
    {
        const ButtonState* b = KeyState(key);
        return b && b->Down;
    }

    bool Input::IsKeyJustPressed(Key key)
    {
        const ButtonState* b = KeyState(key);
        return b && JustPressed(*b);
    }

    bool Input::IsKeyJustReleased(Key key)
    {
        const ButtonState* b = KeyState(key);
        return b && JustReleased(*b);
    }

    bool Input::IsMouseButtonPressed(MouseButton button)
    {
        const ButtonState* b = MouseState(button);
        return b && b->Down;
    }

    bool Input::IsMouseButtonJustPressed(MouseButton button)
    {
        const ButtonState* b = MouseState(button);
        return b && JustPressed(*b);
    }

    bool Input::IsMouseButtonJustReleased(MouseButton button)
    {
        const ButtonState* b = MouseState(button);
        return b && JustReleased(*b);
    }

    glm::vec2 Input::GetMousePosition() { return State().MousePosition; }
    glm::vec2 Input::GetMouseDelta() { return State().MousePosition - State().MousePrevious; }
    glm::vec2 Input::GetMouseScrollDelta() { return State().Scroll; }

    // ------------------------------------------------------------------ gamepads
    bool Input::IsGamepadConnected(int id)
    {
        const auto* slot = GamepadSlot(id);
        return slot && slot->Current.Connected;
    }

    bool Input::IsGamepadButtonPressed(GamepadButton button, int id)
    {
        const auto* slot = GamepadSlot(id);
        int b = (int)button;
        return slot && b < GamepadButtonCount && slot->Current.Connected && slot->Current.Buttons[b];
    }

    bool Input::IsGamepadButtonJustPressed(GamepadButton button, int id)
    {
        const auto* slot = GamepadSlot(id);
        int b = (int)button;
        if (!slot || b >= GamepadButtonCount) return false;
        return slot->Current.Connected && slot->Current.Buttons[b] && !(slot->Previous.Connected && slot->Previous.Buttons[b]);
    }

    bool Input::IsGamepadButtonJustReleased(GamepadButton button, int id)
    {
        const auto* slot = GamepadSlot(id);
        int b = (int)button;
        if (!slot || b >= GamepadButtonCount) return false;
        return slot->Previous.Connected && slot->Previous.Buttons[b] && !(slot->Current.Connected && slot->Current.Buttons[b]);
    }

    float Input::GetGamepadAxis(GamepadAxis axis, int id, float deadzone)
    {
        const auto* slot = GamepadSlot(id);
        int a = (int)axis;
        if (!slot || a >= GamepadAxisCount || !slot->Current.Connected)
            return 0.0f;
        float v = slot->Current.Axes[a];
        if (!IsTrigger(a) && std::abs(v) < deadzone)
            return 0.0f;
        return v;
    }

    // ------------------------------------------------------------------ actions / axes
    bool Input::IsActionPressed(const InputAction& action) { return AnyDown(action.Bindings); }

    bool Input::IsActionJustPressed(const InputAction& action)
    {
        // A new press counts only when no other binding kept the action active.
        bool pressed = false, held = false;
        for (const auto& b : action.Bindings)
            ForEachSample(b, [&](const Sample& s) {
                pressed |= s.JustPressed();
                held |= s.HeldAcross();
            });
        return pressed && !held;
    }

    bool Input::IsActionJustReleased(const InputAction& action)
    {
        // Released when the last binding let go (a quick tap counts, too).
        bool released = false, down = false;
        for (const auto& b : action.Bindings)
            ForEachSample(b, [&](const Sample& s) {
                released |= s.JustReleased();
                down |= s.Down;
            });
        return released && !down;
    }

    float Input::ApplyDeadzone(float value, float deadzone)
    {
        deadzone = std::clamp(deadzone, 0.0f, 0.99f);
        float magnitude = std::abs(value);
        if (magnitude <= deadzone)
            return 0.0f;
        float scaled = std::min(1.0f, (magnitude - deadzone) / (1.0f - deadzone));
        return value < 0.0f ? -scaled : scaled;
    }

    float Input::GetAxis(const InputAxis& axis)
    {
        float digital = (AnyDown(axis.Positive) ? 1.0f : 0.0f) - (AnyDown(axis.Negative) ? 1.0f : 0.0f);

        float analog = 0.0f;
        if (axis.GamepadAxisCode >= 0 && axis.GamepadAxisCode < GamepadAxisCount)
            for (const auto& slot : State().Gamepads)
            {
                if (!slot.Current.Connected)
                    continue;
                float v = ApplyDeadzone(AxisValue01(slot.Current, axis.GamepadAxisCode), axis.Deadzone);
                if (axis.Invert)
                    v = -v;
                if (std::abs(v) > std::abs(analog))
                    analog = v;
            }
        return std::abs(analog) > std::abs(digital) ? analog : digital;
    }

    bool Input::IsActionPressed(std::string_view action)
    {
        const InputAction* a = LookupAction(action);
        return a && IsActionPressed(*a);
    }

    bool Input::IsActionJustPressed(std::string_view action)
    {
        const InputAction* a = LookupAction(action);
        return a && IsActionJustPressed(*a);
    }

    bool Input::IsActionJustReleased(std::string_view action)
    {
        const InputAction* a = LookupAction(action);
        return a && IsActionJustReleased(*a);
    }

    float Input::GetAxis(std::string_view axis)
    {
        const InputAxis* a = LookupAxis(axis);
        return a ? GetAxis(*a) : 0.0f;
    }

    glm::vec2 Input::GetAxis2D(std::string_view horizontal, std::string_view vertical)
    {
        glm::vec2 v(GetAxis(horizontal), GetAxis(vertical));
        float length = glm::length(v);
        return length > 1.0f ? v / length : v;
    }

    // ------------------------------------------------------------------ frame state
    void Input::Update()
    {
        InputState& s = State();
        Internal::PollPlatformInput(s);

        for (auto& key : s.Keys)
            key.BeginFrame();
        for (auto& button : s.Mouse)
            button.BeginFrame();

        s.MousePrevious = s.HasMousePosition ? s.MousePosition : s.MouseLive;
        s.MousePosition = s.MouseLive;
        s.HasMousePosition = true;
        s.Scroll = s.PendingScroll;
        s.PendingScroll = glm::vec2(0.0f);
        s.Text.swap(s.PendingText);
        s.PendingText.clear();

        for (auto& slot : s.Gamepads)
        {
            slot.Previous = slot.Current;
            slot.Current = slot.Live;
        }
    }

    void Input::ResetState()
    {
        State() = InputState{};
    }

    static bool KeyLive(Key key)
    {
        int code = (int)key;
        return code >= Internal::InputFirstKey && code < Internal::InputKeyCount && State().Keys[code].Live;
    }

    static TextInputEvent ModifiedTextEvent()
    {
        TextInputEvent e;
        e.Control = KeyLive(Key::LeftControl) || KeyLive(Key::RightControl);
        e.Shift = KeyLive(Key::LeftShift) || KeyLive(Key::RightShift);
        e.Alt = KeyLive(Key::LeftAlt) || KeyLive(Key::RightAlt);
        e.Super = KeyLive(Key::LeftSuper) || KeyLive(Key::RightSuper);
        return e;
    }

    void Input::OnKeyEvent(Key key, bool pressed, bool repeat)
    {
        int code = (int)key;
        if (code < Internal::InputFirstKey || code >= Internal::InputKeyCount)
            return;
        if (!repeat)
            State().Keys[code].OnEvent(pressed);
        if (pressed)
        {
            TextInputEvent e = ModifiedTextEvent();
            e.KeyCode = key;
            State().PendingText.push_back(e);
        }
    }

    void Input::OnCharEvent(uint32_t codepoint)
    {
        if (codepoint == 0)
            return;
        TextInputEvent e = ModifiedTextEvent();
        e.Codepoint = codepoint;
        State().PendingText.push_back(e);
    }

    const std::vector<TextInputEvent>& Input::GetTextInput() { return State().Text; }

    void Input::OnMouseButtonEvent(MouseButton button, bool pressed)
    {
        int code = (int)button;
        if (code >= 0 && code < MouseButtonCount)
            State().Mouse[code].OnEvent(pressed);
    }

    void Input::OnMouseMovedEvent(float x, float y) { State().MouseLive = { x, y }; }
    void Input::OnMouseScrolledEvent(float xOffset, float yOffset) { State().PendingScroll += glm::vec2(xOffset, yOffset); }

    // Marks a slot as simulated and connects it with every control at rest.
    static Internal::GamepadState& SimulatedPad(int id)
    {
        auto& slot = State().Gamepads[id];
        slot.Simulated = true;
        if (!slot.Live.Connected)
        {
            slot.Live = {};
            slot.Live.Connected = true;
            slot.Live.Axes[(int)GamepadAxis::LeftTrigger] = -1.0f; // released triggers read -1
            slot.Live.Axes[(int)GamepadAxis::RightTrigger] = -1.0f;
        }
        return slot.Live;
    }

    void Input::SimulateGamepadConnected(int id, bool connected)
    {
        if (id < 0 || id >= MaxGamepads) return;
        if (connected)
            SimulatedPad(id);
        else
        {
            auto& slot = State().Gamepads[id];
            slot.Simulated = true;
            slot.Live = {};
        }
    }

    void Input::SimulateGamepadButton(int id, GamepadButton button, bool pressed)
    {
        if (id < 0 || id >= MaxGamepads || (int)button >= GamepadButtonCount) return;
        SimulatedPad(id).Buttons[(int)button] = pressed;
    }

    void Input::SimulateGamepadAxis(int id, GamepadAxis axis, float value)
    {
        if (id < 0 || id >= MaxGamepads || (int)axis >= GamepadAxisCount) return;
        SimulatedPad(id).Axes[(int)axis] = std::clamp(value, -1.0f, 1.0f);
    }

}
