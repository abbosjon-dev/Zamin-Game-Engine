#include "Zamin/Core/InputState.h"
#include "Zamin/Core/Application.h"

#include <GLFW/glfw3.h>

namespace Zamin {

    namespace Internal {

        void PollPlatformInput(InputState& state)
        {
            if (!Application::Exists())
                return;
            auto* window = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());
            if (!window)
                return;

            // Keys / buttons only change while GLFW processes events, so this matches the
            // event stream; polling also covers presses that happened before the window opened.
            static_assert(InputKeyCount == GLFW_KEY_LAST + 1);
            for (int key = InputFirstKey; key < InputKeyCount; key++)
            {
                int s = glfwGetKey(window, key);
                state.Keys[key].Live = s == GLFW_PRESS || s == GLFW_REPEAT;
            }
            for (int b = 0; b < MouseButtonCount; b++)
                state.Mouse[b].Live = glfwGetMouseButton(window, b) == GLFW_PRESS;

            double x, y;
            glfwGetCursorPos(window, &x, &y);
            state.MouseLive = { (float)x, (float)y };

            for (int id = 0; id < Input::MaxGamepads; id++)
            {
                auto& slot = state.Gamepads[id];
                if (slot.Simulated)
                    continue;
                GLFWgamepadstate gs;
                slot.Live = {};
                if (!glfwGetGamepadState(GLFW_JOYSTICK_1 + id, &gs))
                    continue;
                slot.Live.Connected = true;
                for (int b = 0; b < GamepadButtonCount; b++)
                    slot.Live.Buttons[b] = gs.buttons[b] == GLFW_PRESS;
                for (int a = 0; a < GamepadAxisCount; a++)
                    slot.Live.Axes[a] = gs.axes[a];
            }
        }

    }

    static GLFWwindow* NativeWindow()
    {
        return Application::Exists() ? static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow()) : nullptr;
    }

    std::string Input::GetClipboardText()
    {
        if (GLFWwindow* window = NativeWindow())
        {
            const char* text = glfwGetClipboardString(window);
            return text ? std::string(text) : std::string();
        }
        return Internal::GetInputState().Clipboard;
    }

    void Input::SetClipboardText(std::string_view text)
    {
        if (GLFWwindow* window = NativeWindow())
            glfwSetClipboardString(window, std::string(text).c_str());
        else
            Internal::GetInputState().Clipboard = std::string(text);
    }

    const char* Input::GetGamepadName(int id)
    {
        if (id < 0 || id >= MaxGamepads)
            return "";
        const auto& slot = Internal::GetInputState().Gamepads[id];
        if (slot.Simulated)
            return slot.Current.Connected ? "Simulyatsiya" : "";
        if (!Application::Exists())
            return "";
        const char* name = glfwGetGamepadName(GLFW_JOYSTICK_1 + id);
        return name ? name : "";
    }

}
