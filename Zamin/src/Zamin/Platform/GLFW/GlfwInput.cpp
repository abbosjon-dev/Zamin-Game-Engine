#include "Zamin/Core/Input.h"
#include "Zamin/Core/Application.h"

#include <GLFW/glfw3.h>

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

}
