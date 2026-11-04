#include "Zamin/Core/Input.h"
#include "Zamin/Core/Application.h"

#include <GLFW/glfw3.h>

#include <cmath>

namespace Zamin {

    static GLFWwindow* NativeWindow()
    {
        return static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());
    }

    bool Input::IsKeyPressed(Key key)
    {
        if (!Application::Exists()) return false;
        int state = glfwGetKey(NativeWindow(), static_cast<int32_t>(key));
        return state == GLFW_PRESS || state == GLFW_REPEAT;
    }

    bool Input::IsMouseButtonPressed(MouseButton button)
    {
        if (!Application::Exists()) return false;
        return glfwGetMouseButton(NativeWindow(), static_cast<int32_t>(button)) == GLFW_PRESS;
    }

    glm::vec2 Input::GetMousePosition()
    {
        if (!Application::Exists()) return { 0.0f, 0.0f };
        double x, y;
        glfwGetCursorPos(NativeWindow(), &x, &y);
        return { (float)x, (float)y };
    }


    bool Input::IsGamepadConnected(int id)
    {
        return glfwJoystickIsGamepad(GLFW_JOYSTICK_1 + id) == GLFW_TRUE;
    }

    bool Input::IsGamepadButtonPressed(GamepadButton button, int id)
    {
        GLFWgamepadstate state;
        if (!glfwGetGamepadState(GLFW_JOYSTICK_1 + id, &state)) return false;
        return state.buttons[(int)button] == GLFW_PRESS;
    }

    float Input::GetGamepadAxis(GamepadAxis axis, int id, float deadzone)
    {
        GLFWgamepadstate state;
        if (!glfwGetGamepadState(GLFW_JOYSTICK_1 + id, &state)) return 0.0f;
        float v = state.axes[(int)axis];
        if (axis != GamepadAxis::LeftTrigger && axis != GamepadAxis::RightTrigger && std::abs(v) < deadzone)
            return 0.0f;
        return v;
    }

    const char* Input::GetGamepadName(int id)
    {
        const char* name = glfwGetGamepadName(GLFW_JOYSTICK_1 + id);
        return name ? name : "";
    }

}
