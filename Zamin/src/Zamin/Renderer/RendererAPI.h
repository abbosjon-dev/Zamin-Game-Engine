#pragma once

#include "Zamin/Core/Base.h"
#include "Zamin/Renderer/VertexArray.h"

#include <glm/glm.hpp>
#include <vector>

namespace Zamin {

    enum class CullMode { None, Back, Front };
    enum class BlendMode { None, Alpha, Additive };

    // Low-level graphics API abstraction. Today only OpenGL is implemented;
    // Vulkan / Metal / GLES backends can be added behind this interface.
    class RendererAPI
    {
    public:
        enum class API { None = 0, OpenGL = 1 };

        virtual ~RendererAPI() = default;

        virtual void Init() = 0;
        virtual void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) = 0;
        virtual void SetClearColor(const glm::vec4& color) = 0;
        virtual void Clear() = 0;

        virtual void DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t indexCount = 0) = 0;
        virtual void DrawLines(const Ref<VertexArray>& vertexArray, uint32_t vertexCount) = 0;
        virtual void DrawArrays(uint32_t vertexCount) = 0;

        virtual void SetLineWidth(float width) = 0;
        virtual void SetDepthTest(bool enabled) = 0;
        virtual void SetDepthWrite(bool enabled) = 0;
        virtual void SetBlend(bool enabled) = 0;
        virtual void SetBlendMode(BlendMode mode) = 0;
        virtual void SetCullMode(CullMode mode) = 0;

        // Remember / restore the currently bound render target + viewport
        // (used by passes such as shadow mapping that render off-screen).
        virtual void BindTextureUnit(uint32_t unit, uint32_t rendererID) = 0;
        virtual void SaveRenderTarget() = 0;
        virtual void RestoreRenderTarget() = 0;

        virtual std::vector<uint8_t> ReadDefaultFramebuffer(uint32_t width, uint32_t height) = 0;

        static API GetAPI() { return s_API; }
        static Scope<RendererAPI> Create();

    private:
        static inline API s_API = API::OpenGL;
    };

}
