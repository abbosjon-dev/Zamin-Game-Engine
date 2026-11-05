#pragma once

#include "Zamin/Core/Base.h"

#include <initializer_list>
#include <vector>

namespace Zamin {

    enum class FramebufferTextureFormat
    {
        None = 0,
        RGBA8,          // color
        RGBA16F,        // HDR color
        RED_INTEGER,    // entity ID (mouse picking)
        DEPTH24STENCIL8,
        DEPTH32F,       // sampleable depth (shadow maps)
        Depth = DEPTH24STENCIL8
    };

    struct FramebufferTextureSpecification
    {
        FramebufferTextureSpecification() = default;
        FramebufferTextureSpecification(FramebufferTextureFormat format) : TextureFormat(format) {}
        FramebufferTextureFormat TextureFormat = FramebufferTextureFormat::None;
    };

    struct FramebufferAttachmentSpecification
    {
        FramebufferAttachmentSpecification() = default;
        FramebufferAttachmentSpecification(std::initializer_list<FramebufferTextureSpecification> attachments)
            : Attachments(attachments) {}
        std::vector<FramebufferTextureSpecification> Attachments;
    };

    struct FramebufferSpecification
    {
        uint32_t Width = 0, Height = 0;
        FramebufferAttachmentSpecification Attachments;
        uint32_t Samples = 1;
    };

    class Framebuffer
    {
    public:
        virtual ~Framebuffer() = default;

        virtual void Bind() = 0;
        virtual void Unbind() = 0;
        virtual void Resize(uint32_t width, uint32_t height) = 0;

        virtual int ReadPixel(uint32_t attachmentIndex, int x, int y) = 0;
        // Every pixel of an 8-bit color attachment as RGBA8, bottom row first (empty for others).
        virtual std::vector<uint8_t> ReadColorPixels(uint32_t attachmentIndex = 0) = 0;
        virtual void ClearAttachment(uint32_t attachmentIndex, int value) = 0;

        virtual uint32_t GetColorAttachmentRendererID(uint32_t index = 0) const = 0;
        virtual uint32_t GetDepthAttachmentRendererID() const = 0;
        virtual const FramebufferSpecification& GetSpecification() const = 0;

        static Ref<Framebuffer> Create(const FramebufferSpecification& spec);
    };

}
