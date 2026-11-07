#include "Zamin/Renderer/GraphicsContext.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <format>
#include <string>

namespace Zamin {

    static GraphicsContextInfo s_ContextInfo;

    // The driver's messages (a debug context): errors and warnings into the log, the rest is left out.
    static void APIENTRY OpenGLMessageCallback(GLenum, GLenum, GLuint, GLenum severity, GLsizei, const GLchar* message, const void*)
    {
        switch (severity)
        {
            case GL_DEBUG_SEVERITY_HIGH:   ZM_CORE_ERROR("OpenGL: {}", message); return;
            case GL_DEBUG_SEVERITY_MEDIUM: ZM_CORE_WARN("OpenGL: {}", message); return;
            default: return;
        }
    }

    const GraphicsContextInfo& GraphicsContext::GetInfo() { return s_ContextInfo; }

    class OpenGLContext : public GraphicsContext
    {
    public:
        OpenGLContext(GLFWwindow* window, const GraphicsSettings& requested, bool fellBack)
            : m_Window(window), m_Requested(requested), m_FellBack(fellBack) {}

        void Init() override
        {
            glfwMakeContextCurrent(m_Window);
            int status = gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
            if (!status)
            {
                ZM_CORE_CRITICAL("OpenGL funksiyalari yuklanmadi (glad)");
                std::exit(1);
            }
            GraphicsContextInfo info;
            info.Valid = true;
            auto text = [](GLenum name) {
                const GLubyte* s = glGetString(name);
                return s ? std::string((const char*)s) : std::string();
            };
            info.Vendor = text(GL_VENDOR);
            info.Renderer = text(GL_RENDERER);
            info.Version = text(GL_VERSION);
            info.ShadingLanguage = text(GL_SHADING_LANGUAGE_VERSION);
            glGetIntegerv(GL_MAJOR_VERSION, &info.Major);
            glGetIntegerv(GL_MINOR_VERSION, &info.Minor);
            info.RequestedMajor = m_Requested.GLMajor;
            info.RequestedMinor = m_Requested.GLMinor;
            info.RequestedSamples = m_Requested.MSAASamples;
            info.RequestedSRGB = m_Requested.SRGBFramebuffer;
            info.FellBack = m_FellBack;
            GLint profile = 0, flags = 0;
            glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &profile);
            info.CoreProfile = (profile & GL_CONTEXT_CORE_PROFILE_BIT) != 0;
            glGetIntegerv(GL_CONTEXT_FLAGS, &flags);
            info.DebugContext = (flags & GL_CONTEXT_FLAG_DEBUG_BIT) != 0;
            glGetIntegerv(GL_MAX_TEXTURE_SIZE, &info.MaxTextureSize);
            glGetIntegerv(GL_MAX_SAMPLES, &info.MaxSamples);
            if (GLAD_GL_ARB_texture_filter_anisotropic || GLAD_GL_EXT_texture_filter_anisotropic || GLAD_GL_VERSION_4_6)
                glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &info.MaxAnisotropy);
            // The window's own framebuffer: samples and whether it can encode to sRGB.
            GLint samples = 0;
            glGetIntegerv(GL_SAMPLES, &samples);
            info.Samples = samples > 1 ? samples : 0;
            if (info.Samples > 0)
                glEnable(GL_MULTISAMPLE);
            // A debug context: its messages go to the log (synchronous: the message comes from the call that caused it).
            if ((info.DebugContext || m_Requested.WantsDebugContext()) && (GLAD_GL_KHR_debug || GLAD_GL_VERSION_4_3))
            {
                glEnable(GL_DEBUG_OUTPUT);
                glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
                glDebugMessageCallback(OpenGLMessageCallback, nullptr);
                info.DebugOutput = true;
            }
            if (m_Requested.SRGBFramebuffer)
            {
                GLint encoding = GL_LINEAR;
                glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_BACK_LEFT, GL_FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING, &encoding);
                // Cleared from the error queue (a driver without the query): no sRGB then.
                while (glGetError() != GL_NO_ERROR) {}
                info.SRGB = encoding == GL_SRGB;
                if (!info.SRGB)
                    ZM_CORE_WARN("sRGB framebuffer so'raldi, lekin oyna buferi sRGB emas - gamma shaderda qo'llanadi");
            }
            s_ContextInfo = info;

            ZM_CORE_INFO("OpenGL: {} | {} | {}", info.Vendor, info.Renderer, info.Version);
            ZM_CORE_INFO("Grafik kontekst: {} (so'ralgan: {}.{}{}{}{})", info.Summary(), info.RequestedMajor, info.RequestedMinor,
                         m_Requested.WantsDebugContext() ? ", debug" : "",
                         info.RequestedSamples > 0 ? std::format(", MSAA {}x", info.RequestedSamples) : std::string(),
                         info.RequestedSRGB ? ", sRGB" : "");
            if (info.RequestedSamples > 0 && info.Samples != info.RequestedSamples)
                ZM_CORE_WARN("MSAA {}x so'ralgan edi, oyna buferi {}", info.RequestedSamples,
                             info.Samples > 0 ? std::format("{}x", info.Samples) : std::string("multisample'siz"));
        }

        void SwapBuffers() override { glfwSwapBuffers(m_Window); }

    private:
        GLFWwindow* m_Window;
        GraphicsSettings m_Requested;
        bool m_FellBack = false;
    };

    Scope<GraphicsContext> GraphicsContext::Create(void* window, const GraphicsSettings& requested, bool fellBack)
    {
        return CreateScope<OpenGLContext>(static_cast<GLFWwindow*>(window), requested, fellBack);
    }

}
