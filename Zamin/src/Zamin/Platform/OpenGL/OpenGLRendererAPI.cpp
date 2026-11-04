#include "Zamin/Renderer/RendererAPI.h"

#include <glad/glad.h>

#include <algorithm>

namespace Zamin {

#ifndef NDEBUG
    static void APIENTRY OpenGLMessageCallback(GLenum, GLenum, GLuint, GLenum severity, GLsizei, const GLchar* message, const void*)
    {
        switch (severity)
        {
            case GL_DEBUG_SEVERITY_HIGH:   ZM_CORE_ERROR("OpenGL: {}", message); return;
            case GL_DEBUG_SEVERITY_MEDIUM: ZM_CORE_WARN("OpenGL: {}", message); return;
            default: return;
        }
    }
#endif

    class OpenGLRendererAPI : public RendererAPI
    {
    public:
        void Init() override
        {
#ifndef NDEBUG
            if (GLAD_GL_KHR_debug || GLAD_GL_VERSION_4_3)
            {
                glEnable(GL_DEBUG_OUTPUT);
                glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
                glDebugMessageCallback(OpenGLMessageCallback, nullptr);
            }
#endif
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LEQUAL);
            glEnable(GL_LINE_SMOOTH);
        }

        void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override { glViewport(x, y, width, height); }
        void SetClearColor(const glm::vec4& c) override { glClearColor(c.r, c.g, c.b, c.a); }
        void Clear() override { glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); }

        void DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t indexCount) override
        {
            vertexArray->Bind();
            uint32_t count = indexCount ? indexCount : vertexArray->GetIndexBuffer()->GetCount();
            glDrawElements(GL_TRIANGLES, (GLsizei)count, GL_UNSIGNED_INT, nullptr);
        }

        void DrawLines(const Ref<VertexArray>& vertexArray, uint32_t vertexCount) override
        {
            vertexArray->Bind();
            glDrawArrays(GL_LINES, 0, (GLsizei)vertexCount);
        }

        void DrawArrays(uint32_t vertexCount) override
        {
            // Core profile requires a bound VAO even for attribute-less draws.
            static GLuint emptyVAO = 0;
            if (!emptyVAO)
                glGenVertexArrays(1, &emptyVAO);
            glBindVertexArray(emptyVAO);
            glDrawArrays(GL_TRIANGLES, 0, (GLsizei)vertexCount);
        }

        void SetLineWidth(float width) override
        {
            // Core profiles only guarantee width 1.0 – clamp to the supported range.
            GLfloat range[2] = { 1.0f, 1.0f };
            glGetFloatv(GL_ALIASED_LINE_WIDTH_RANGE, range);
            glLineWidth(std::clamp(width, range[0], range[1]));
        }

        void SetDepthTest(bool enabled) override { enabled ? glEnable(GL_DEPTH_TEST) : glDisable(GL_DEPTH_TEST); }
        void SetDepthWrite(bool enabled) override { glDepthMask(enabled ? GL_TRUE : GL_FALSE); }
        void SetBlend(bool enabled) override { enabled ? glEnable(GL_BLEND) : glDisable(GL_BLEND); }

        void SetCullMode(CullMode mode) override
        {
            if (mode == CullMode::None) { glDisable(GL_CULL_FACE); return; }
            glEnable(GL_CULL_FACE);
            glCullFace(mode == CullMode::Back ? GL_BACK : GL_FRONT);
        }

        void BindTextureUnit(uint32_t unit, uint32_t rendererID) override
        {
            glActiveTexture(GL_TEXTURE0 + unit);
            glBindTexture(GL_TEXTURE_2D, rendererID);
        }

        void SaveRenderTarget() override
        {
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &m_SavedFramebuffer);
            glGetIntegerv(GL_VIEWPORT, m_SavedViewport);
        }

        void RestoreRenderTarget() override
        {
            glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)m_SavedFramebuffer);
            glViewport(m_SavedViewport[0], m_SavedViewport[1], m_SavedViewport[2], m_SavedViewport[3]);
        }

        std::vector<uint8_t> ReadDefaultFramebuffer(uint32_t width, uint32_t height) override
        {
            std::vector<uint8_t> pixels((size_t)width * height * 4);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glReadBuffer(GL_BACK);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, (GLsizei)width, (GLsizei)height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            return pixels;
        }

    private:
        GLint m_SavedFramebuffer = 0;
        GLint m_SavedViewport[4] = { 0, 0, 0, 0 };
    };

    Scope<RendererAPI> RendererAPI::Create() { return CreateScope<OpenGLRendererAPI>(); }

}
