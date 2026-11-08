#pragma once

#include "Zamin/Renderer/RendererAPI.h"

namespace Zamin {

    class RenderCommand
    {
    public:
        static void Init() { s_API = RendererAPI::Create(); s_API->Init(); }
        static void Shutdown() { s_API.reset(); }

        static void SetViewport(uint32_t x, uint32_t y, uint32_t w, uint32_t h) { s_API->SetViewport(x, y, w, h); }
        static glm::ivec4 GetViewport() { return s_API ? s_API->GetViewport() : glm::ivec4(0); }
        static void SetClearColor(const glm::vec4& color) { s_API->SetClearColor(color); }
        static void Clear() { s_API->Clear(); }

        static void DrawIndexed(const Ref<VertexArray>& va, uint32_t count = 0) { s_API->DrawIndexed(va, count); }
        static void DrawIndexedInstanced(const Ref<VertexArray>& va, uint32_t count, uint32_t instances) { s_API->DrawIndexedInstanced(va, count, instances); }
        static void DrawLines(const Ref<VertexArray>& va, uint32_t vertexCount) { s_API->DrawLines(va, vertexCount); }
        static void DrawArrays(uint32_t vertexCount) { s_API->DrawArrays(vertexCount); }

        static void SetLineWidth(float width) { s_API->SetLineWidth(width); }
        static void SetDepthTest(bool enabled) { s_API->SetDepthTest(enabled); }
        static void SetDepthWrite(bool enabled) { s_API->SetDepthWrite(enabled); }
        static void SetBlend(bool enabled) { s_API->SetBlend(enabled); }
        static void SetBlendMode(BlendMode mode) { s_API->SetBlendMode(mode); }
        static void SetCullMode(CullMode mode) { s_API->SetCullMode(mode); }
        static void SetWireframe(bool enabled, bool pullForward = false) { s_API->SetWireframe(enabled, pullForward); }
        static void SetColorWrite(uint32_t attachment, bool enabled) { s_API->SetColorWrite(attachment, enabled); }
        static void SetColorWriteMask(uint32_t attachment, bool color, bool alpha) { s_API->SetColorWriteMask(attachment, color, alpha); }
        static void ClearDepth() { s_API->ClearDepth(); }
        static void SetScissor(bool enabled, const glm::vec2& min01 = glm::vec2(0.0f), const glm::vec2& max01 = glm::vec2(1.0f))
        {
            s_API->SetScissor(enabled, min01, max01);
        }

        static void BindTextureUnit(uint32_t unit, uint32_t id) { s_API->BindTextureUnit(unit, id); }
        static void SaveRenderTarget() { s_API->SaveRenderTarget(); }
        static void RestoreRenderTarget() { s_API->RestoreRenderTarget(); }
        static bool IsDrawingToWindow() { return s_API->IsDrawingToWindow(); }
        static void SetFramebufferSRGB(bool enabled) { s_API->SetFramebufferSRGB(enabled); }

        static std::vector<uint8_t> ReadDefaultFramebuffer(uint32_t w, uint32_t h) { return s_API->ReadDefaultFramebuffer(w, h); }
        static uint32_t PopErrors(std::string* names = nullptr) { return s_API ? s_API->PopErrors(names) : 0; }

    private:
        static inline Scope<RendererAPI> s_API;
    };

}
