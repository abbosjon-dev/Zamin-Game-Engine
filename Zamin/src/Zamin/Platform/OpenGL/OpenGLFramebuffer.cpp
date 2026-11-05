#include "Zamin/Renderer/Framebuffer.h"

#include <glad/glad.h>

namespace Zamin {

    static constexpr uint32_t s_MaxFramebufferSize = 8192;

    static bool IsDepthFormat(FramebufferTextureFormat format)
    {
        return format == FramebufferTextureFormat::DEPTH24STENCIL8 || format == FramebufferTextureFormat::DEPTH32F;
    }

    class OpenGLFramebuffer : public Framebuffer
    {
    public:
        explicit OpenGLFramebuffer(const FramebufferSpecification& spec) : m_Specification(spec)
        {
            for (auto s : m_Specification.Attachments.Attachments)
            {
                if (!IsDepthFormat(s.TextureFormat))
                    m_ColorAttachmentSpecs.emplace_back(s);
                else
                    m_DepthAttachmentSpec = s;
            }
            Invalidate();
        }

        ~OpenGLFramebuffer() override { Release(); }

        void Bind() override
        {
            glBindFramebuffer(GL_FRAMEBUFFER, m_RendererID);
            glViewport(0, 0, (GLsizei)m_Specification.Width, (GLsizei)m_Specification.Height);
        }

        void Unbind() override { glBindFramebuffer(GL_FRAMEBUFFER, 0); }

        void Resize(uint32_t width, uint32_t height) override
        {
            if (width == 0 || height == 0 || width > s_MaxFramebufferSize || height > s_MaxFramebufferSize)
            {
                ZM_CORE_WARN("Framebuffer o'lchami noto'g'ri: {}x{}", width, height);
                return;
            }
            m_Specification.Width = width;
            m_Specification.Height = height;
            Invalidate();
        }

        int ReadPixel(uint32_t attachmentIndex, int x, int y) override
        {
            if (attachmentIndex >= m_ColorAttachments.size())
                return -1;
            glBindFramebuffer(GL_READ_FRAMEBUFFER, m_RendererID);
            glReadBuffer(GL_COLOR_ATTACHMENT0 + attachmentIndex);
            int pixelData = -1;
            glReadPixels(x, y, 1, 1, GL_RED_INTEGER, GL_INT, &pixelData);
            return pixelData;
        }

        std::vector<uint8_t> ReadColorPixels(uint32_t attachmentIndex) override
        {
            std::vector<uint8_t> pixels;
            if (attachmentIndex >= m_ColorAttachments.size() ||
                m_ColorAttachmentSpecs[attachmentIndex].TextureFormat != FramebufferTextureFormat::RGBA8)
                return pixels;
            pixels.resize((size_t)m_Specification.Width * m_Specification.Height * 4);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, m_RendererID);
            glReadBuffer(GL_COLOR_ATTACHMENT0 + attachmentIndex);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, (GLsizei)m_Specification.Width, (GLsizei)m_Specification.Height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
            return pixels;
        }

        void ClearAttachment(uint32_t attachmentIndex, int value) override
        {
            if (attachmentIndex >= m_ColorAttachments.size())
                return;
            if (m_ColorAttachmentSpecs[attachmentIndex].TextureFormat == FramebufferTextureFormat::RED_INTEGER)
            {
                GLint v[4] = { value, value, value, value };
                glClearBufferiv(GL_COLOR, (GLint)attachmentIndex, v);
            }
            else
            {
                GLfloat v[4] = { (float)value, (float)value, (float)value, (float)value };
                glClearBufferfv(GL_COLOR, (GLint)attachmentIndex, v);
            }
        }

        uint32_t GetColorAttachmentRendererID(uint32_t index) const override
        {
            return index < m_ColorAttachments.size() ? m_ColorAttachments[index] : 0;
        }
        uint32_t GetDepthAttachmentRendererID() const override { return m_DepthAttachment; }
        const FramebufferSpecification& GetSpecification() const override { return m_Specification; }

    private:
        void Release()
        {
            if (!m_RendererID) return;
            glDeleteFramebuffers(1, &m_RendererID);
            if (!m_ColorAttachments.empty())
                glDeleteTextures((GLsizei)m_ColorAttachments.size(), m_ColorAttachments.data());
            if (m_DepthAttachment)
                glDeleteTextures(1, &m_DepthAttachment);
            m_ColorAttachments.clear();
            m_DepthAttachment = 0;
            m_RendererID = 0;
        }

        void Invalidate()
        {
            Release();

            glGenFramebuffers(1, &m_RendererID);
            glBindFramebuffer(GL_FRAMEBUFFER, m_RendererID);

            const GLsizei w = (GLsizei)m_Specification.Width, h = (GLsizei)m_Specification.Height;

            m_ColorAttachments.resize(m_ColorAttachmentSpecs.size());
            if (!m_ColorAttachments.empty())
                glGenTextures((GLsizei)m_ColorAttachments.size(), m_ColorAttachments.data());

            for (size_t i = 0; i < m_ColorAttachments.size(); i++)
            {
                glBindTexture(GL_TEXTURE_2D, m_ColorAttachments[i]);
                GLint filter = GL_LINEAR;
                switch (m_ColorAttachmentSpecs[i].TextureFormat)
                {
                    case FramebufferTextureFormat::RGBA8:
                        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
                        break;
                    case FramebufferTextureFormat::RGBA16F:
                        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
                        break;
                    case FramebufferTextureFormat::RED_INTEGER:
                        glTexImage2D(GL_TEXTURE_2D, 0, GL_R32I, w, h, 0, GL_RED_INTEGER, GL_INT, nullptr);
                        filter = GL_NEAREST;
                        break;
                    default: break;
                }
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + (GLenum)i, GL_TEXTURE_2D, m_ColorAttachments[i], 0);
            }

            if (m_DepthAttachmentSpec.TextureFormat != FramebufferTextureFormat::None)
            {
                glGenTextures(1, &m_DepthAttachment);
                glBindTexture(GL_TEXTURE_2D, m_DepthAttachment);
                if (m_DepthAttachmentSpec.TextureFormat == FramebufferTextureFormat::DEPTH32F)
                {
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, w, h, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
                    float border[] = { 1.0f, 1.0f, 1.0f, 1.0f };
                    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
                    // Hardware PCF via sampler2DShadow.
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
                    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_DepthAttachment, 0);
                }
                else
                {
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, w, h, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, nullptr);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, m_DepthAttachment, 0);
                }
            }

            if (m_ColorAttachments.size() > 1)
            {
                GLenum buffers[8];
                for (size_t i = 0; i < m_ColorAttachments.size() && i < 8; i++)
                    buffers[i] = GL_COLOR_ATTACHMENT0 + (GLenum)i;
                glDrawBuffers((GLsizei)std::min<size_t>(m_ColorAttachments.size(), 8), buffers);
            }
            else if (m_ColorAttachments.empty())
            {
                glDrawBuffer(GL_NONE);
                glReadBuffer(GL_NONE);
            }

            GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
            if (status != GL_FRAMEBUFFER_COMPLETE)
                ZM_CORE_ERROR("Framebuffer to'liq emas! (status 0x{:X})", status);

            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }

    private:
        uint32_t m_RendererID = 0;
        FramebufferSpecification m_Specification;
        std::vector<FramebufferTextureSpecification> m_ColorAttachmentSpecs;
        FramebufferTextureSpecification m_DepthAttachmentSpec;
        std::vector<uint32_t> m_ColorAttachments;
        uint32_t m_DepthAttachment = 0;
    };

    Ref<Framebuffer> Framebuffer::Create(const FramebufferSpecification& spec) { return CreateRef<OpenGLFramebuffer>(spec); }

}
