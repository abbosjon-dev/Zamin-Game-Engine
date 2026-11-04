#include "Zamin/Renderer/GraphicsContext.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

namespace Zamin {

    class OpenGLContext : public GraphicsContext
    {
    public:
        explicit OpenGLContext(GLFWwindow* window) : m_Window(window) {}

        void Init() override
        {
            glfwMakeContextCurrent(m_Window);
            int status = gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
            if (!status)
            {
                ZM_CORE_CRITICAL("OpenGL funksiyalari yuklanmadi (glad)");
                std::exit(1);
            }
            ZM_CORE_INFO("OpenGL: {} | {} | {}", (const char*)glGetString(GL_VENDOR),
                         (const char*)glGetString(GL_RENDERER), (const char*)glGetString(GL_VERSION));
        }

        void SwapBuffers() override { glfwSwapBuffers(m_Window); }

    private:
        GLFWwindow* m_Window;
    };

    Scope<GraphicsContext> GraphicsContext::Create(void* window)
    {
        return CreateScope<OpenGLContext>(static_cast<GLFWwindow*>(window));
    }

}
