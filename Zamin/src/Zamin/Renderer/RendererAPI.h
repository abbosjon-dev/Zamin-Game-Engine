#pragma once

#include "Zamin/Core/Base.h"
#include "Zamin/Renderer/VertexArray.h"

#include <glm/glm.hpp>
#include <vector>

namespace Zamin {

    enum class CullMode { None, Back, Front };
    // Alpha: src * a + dst * (1 - a); Premultiplied: src + dst * (1 - a) (the color is already
    // multiplied by its alpha); Additive: src * a + dst; Max: the larger of src and dst per channel.
    enum class BlendMode { None, Alpha, Premultiplied, Additive, Max };

    // Low-level graphics API abstraction. Today only OpenGL is implemented;
    // Vulkan / Metal / GLES backends can be added behind this interface.
    class RendererAPI
    {
    public:
        enum class API { None = 0, OpenGL = 1 };

        virtual ~RendererAPI() = default;

        virtual void Init() = 0;
        virtual void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) = 0;
        // The current viewport: x, y, width, height (pixels of the bound target).
        virtual glm::ivec4 GetViewport() const = 0;
        virtual void SetClearColor(const glm::vec4& color) = 0;
        virtual void Clear() = 0;

        virtual void DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t indexCount = 0) = 0;
        // `instanceCount` copies of the indexed triangles (per-instance attributes advance per copy).
        virtual void DrawIndexedInstanced(const Ref<VertexArray>& vertexArray, uint32_t indexCount, uint32_t instanceCount) = 0;
        virtual void DrawLines(const Ref<VertexArray>& vertexArray, uint32_t vertexCount) = 0;
        virtual void DrawArrays(uint32_t vertexCount) = 0;

        virtual void SetLineWidth(float width) = 0;
        virtual void SetDepthTest(bool enabled) = 0;
        virtual void SetDepthWrite(bool enabled) = 0;
        virtual void SetBlend(bool enabled) = 0;
        virtual void SetBlendMode(BlendMode mode) = 0;
        virtual void SetCullMode(CullMode mode) = 0;
        // Triangles drawn as their edges (true) or filled. `pullForward`: the edges are moved a
        // little towards the camera, so they win the depth test against the same surface drawn
        // filled (wireframe over a shaded picture).
        virtual void SetWireframe(bool enabled, bool pullForward = false) = 0;
        // Whether draws write into the bound framebuffer's colour attachment `attachment` (all
        // channels); the other attachments keep their own setting.
        virtual void SetColorWrite(uint32_t attachment, bool enabled) = 0;
        // The same with the colour (RGB) and the alpha channel apart, e.g. a layer drawn over a
        // finished picture that keeps its alpha (opaque) whatever the layer's own transparency.
        virtual void SetColorWriteMask(uint32_t attachment, bool color, bool alpha) = 0;
        // Clears only the depth of the bound framebuffer (depth writes must be on).
        virtual void ClearDepth() = 0;
        // Draws (and clears) only inside a rectangle of the current viewport: min01 / max01 in 0..1 of
        // it, origin bottom-left (UI clipping). Off: everywhere again.
        virtual void SetScissor(bool enabled, const glm::vec2& min01, const glm::vec2& max01) = 0;

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
