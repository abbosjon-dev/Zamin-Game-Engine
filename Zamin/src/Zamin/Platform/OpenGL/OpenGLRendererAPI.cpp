#include "Zamin/Renderer/RendererAPI.h"

#include <glad/glad.h>

#include <algorithm>
#include <cmath>

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

        void DrawIndexedInstanced(const Ref<VertexArray>& vertexArray, uint32_t indexCount, uint32_t instanceCount) override
        {
            vertexArray->Bind();
            uint32_t count = indexCount ? indexCount : vertexArray->GetIndexBuffer()->GetCount();
            glDrawElementsInstanced(GL_TRIANGLES, (GLsizei)count, GL_UNSIGNED_INT, nullptr, (GLsizei)instanceCount);
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
            // Core profiles only guarantee width 1.0 – clamp to the supported range. A
            // forward-compatible context (macOS, our GLFW window) rejects any width above 1.0
            // with GL_INVALID_VALUE although the range query reports more.
            static GLfloat s_MaxWidth = -1.0f;
            if (s_MaxWidth < 0.0f)
            {
                GLfloat range[2] = { 1.0f, 1.0f };
                glGetFloatv(GL_ALIASED_LINE_WIDTH_RANGE, range);
                GLint flags = 0;
                glGetIntegerv(GL_CONTEXT_FLAGS, &flags);
                s_MaxWidth = (flags & GL_CONTEXT_FLAG_FORWARD_COMPATIBLE_BIT) ? 1.0f : std::max(1.0f, range[1]);
            }
            glLineWidth(std::clamp(width, 1.0f, s_MaxWidth));
        }

        void SetDepthTest(bool enabled) override { enabled ? glEnable(GL_DEPTH_TEST) : glDisable(GL_DEPTH_TEST); }
        void SetDepthWrite(bool enabled) override { glDepthMask(enabled ? GL_TRUE : GL_FALSE); }
        void SetBlend(bool enabled) override { enabled ? glEnable(GL_BLEND) : glDisable(GL_BLEND); }

        void SetBlendMode(BlendMode mode) override
        {
            glBlendEquation(mode == BlendMode::Max ? GL_MAX : GL_FUNC_ADD);
            switch (mode)
            {
                case BlendMode::None:     glDisable(GL_BLEND); break;
                case BlendMode::Alpha:    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); break;
                case BlendMode::Premultiplied: glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); break;
                case BlendMode::Additive: glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE); break;
                case BlendMode::Max:      glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE); break; // the factors do not apply to GL_MAX
            }
        }

        void SetCullMode(CullMode mode) override
        {
            if (mode == CullMode::None) { glDisable(GL_CULL_FACE); return; }
            glEnable(GL_CULL_FACE);
            glCullFace(mode == CullMode::Back ? GL_BACK : GL_FRONT);
        }

        void SetWireframe(bool enabled, bool pullForward) override
        {
            glPolygonMode(GL_FRONT_AND_BACK, enabled ? GL_LINE : GL_FILL);
            if (enabled && pullForward)
            {
                glEnable(GL_POLYGON_OFFSET_LINE);
                glPolygonOffset(-1.0f, -1.0f);
            }
            else
                glDisable(GL_POLYGON_OFFSET_LINE);
        }

        void SetColorWrite(uint32_t attachment, bool enabled) override
        {
            GLboolean on = enabled ? GL_TRUE : GL_FALSE;
            glColorMaski(attachment, on, on, on, on);
        }

        void SetColorWriteMask(uint32_t attachment, bool color, bool alpha) override
        {
            GLboolean rgb = color ? GL_TRUE : GL_FALSE;
            glColorMaski(attachment, rgb, rgb, rgb, alpha ? GL_TRUE : GL_FALSE);
        }

        void ClearDepth() override { glClear(GL_DEPTH_BUFFER_BIT); }

        void SetScissor(bool enabled, const glm::vec2& min01, const glm::vec2& max01) override
        {
            if (!enabled)
            {
                glDisable(GL_SCISSOR_TEST);
                return;
            }
            GLint viewport[4] = { 0, 0, 0, 0 };
            glGetIntegerv(GL_VIEWPORT, viewport);
            const float w = (float)viewport[2], h = (float)viewport[3];
            const int x0 = viewport[0] + (int)std::floor(std::clamp(min01.x, 0.0f, 1.0f) * w);
            const int y0 = viewport[1] + (int)std::floor(std::clamp(min01.y, 0.0f, 1.0f) * h);
            const int x1 = viewport[0] + (int)std::ceil(std::clamp(max01.x, 0.0f, 1.0f) * w);
            const int y1 = viewport[1] + (int)std::ceil(std::clamp(max01.y, 0.0f, 1.0f) * h);
            glEnable(GL_SCISSOR_TEST);
            glScissor(x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0));
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
