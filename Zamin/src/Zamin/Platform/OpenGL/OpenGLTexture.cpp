#include "Zamin/Renderer/Texture.h"

#include <glad/glad.h>
#include <stb_image.h>

#include <unordered_map>

namespace Zamin {

    static void FormatToGL(ImageFormat format, GLenum& internalFormat, GLenum& dataFormat, GLenum& type)
    {
        type = GL_UNSIGNED_BYTE;
        switch (format)
        {
            case ImageFormat::R8:      internalFormat = GL_R8;      dataFormat = GL_RED;  break;
            case ImageFormat::RGB8:    internalFormat = GL_RGB8;    dataFormat = GL_RGB;  break;
            case ImageFormat::RGBA32F: internalFormat = GL_RGBA32F; dataFormat = GL_RGBA; type = GL_FLOAT; break;
            case ImageFormat::RGBA8:
            default:                   internalFormat = GL_RGBA8;   dataFormat = GL_RGBA; break;
        }
    }

    static uint32_t BytesPerPixel(ImageFormat format)
    {
        switch (format)
        {
            case ImageFormat::R8: return 1;
            case ImageFormat::RGB8: return 3;
            case ImageFormat::RGBA32F: return 16;
            default: return 4;
        }
    }

    class OpenGLTexture2D : public Texture2D
    {
    public:
        explicit OpenGLTexture2D(const TextureSpecification& spec) : m_Specification(spec)
        {
            Allocate();
            m_Loaded = true;
        }

        OpenGLTexture2D(const std::string& path, TextureFilter filter) : m_Path(path), m_FromFile(true)
        {
            DecodedImage image;
            if (!Decode(path, image))
            {
                ZM_CORE_ERROR("Rasm yuklanmadi: '{}' ({})", path, image.Error);
                return;
            }
            m_Specification.Width = image.Width;
            m_Specification.Height = image.Height;
            m_Specification.Filter = filter;
            m_Specification.GenerateMips = filter != TextureFilter::Nearest;
            m_Specification.Format = image.Format;

            Allocate();
            SetData(image.Pixels, (uint32_t)(image.Width * image.Height * BytesPerPixel(image.Format)));
            m_Loaded = true;
        }

        OpenGLTexture2D(const void* encoded, size_t size, const std::string& name) : m_Path(name)
        {
            int width, height, channels;
            stbi_set_flip_vertically_on_load(1); // same convention as file textures (glTF V is flipped in the mesh)
            stbi_uc* data = stbi_load_from_memory((const stbi_uc*)encoded, (int)size, &width, &height, &channels, 4);
            if (!data)
            {
                ZM_CORE_ERROR("Rasm (xotiradan) yuklanmadi: '{}' ({})", name, stbi_failure_reason());
                return;
            }
            m_Specification.Width = (uint32_t)width;
            m_Specification.Height = (uint32_t)height;
            m_Specification.Format = ImageFormat::RGBA8;
            Allocate();
            SetData(data, (uint32_t)(width * height * 4));
            stbi_image_free(data);
            m_Loaded = true;
        }

        ~OpenGLTexture2D() override { if (m_RendererID) glDeleteTextures(1, &m_RendererID); }

        bool Reload(std::string* error) override
        {
            if (!m_FromFile)
            {
                if (error) *error = "fayldan yuklanmagan";
                return false;
            }
            DecodedImage image;
            if (!Decode(m_Path, image))
            {
                if (error)
                    *error = image.Error;
                else
                    ZM_CORE_ERROR("Rasm qayta yuklanmadi: '{}' ({}) - avvalgisi qoldi", m_Path, image.Error);
                return false;
            }
            bool resized = !m_RendererID || image.Width != m_Specification.Width || image.Height != m_Specification.Height ||
                           image.Format != m_Specification.Format;
            m_Specification.Width = image.Width;
            m_Specification.Height = image.Height;
            m_Specification.Format = image.Format;
            if (!m_RendererID)
                Allocate();       // the first load had failed
            else if (resized)
                DefineStorage();  // same texture object, new size / format
            SetData(image.Pixels, (uint32_t)(image.Width * image.Height * BytesPerPixel(image.Format)));
            m_Loaded = true;
            return true;
        }

        const TextureSpecification& GetSpecification() const override { return m_Specification; }
        uint32_t GetWidth() const override { return m_Specification.Width; }
        uint32_t GetHeight() const override { return m_Specification.Height; }
        uint32_t GetRendererID() const override { return m_RendererID; }
        const std::string& GetPath() const override { return m_Path; }
        bool IsLoaded() const override { return m_Loaded; }

        void SetData(const void* data, uint32_t size) override
        {
            uint32_t expected = m_Specification.Width * m_Specification.Height * BytesPerPixel(m_Specification.Format);
            if (size != expected)
            {
                ZM_CORE_ERROR("Texture::SetData: hajm noto'g'ri ({} != {})", size, expected);
                return;
            }
            GLenum internalFormat, dataFormat, type;
            FormatToGL(m_Specification.Format, internalFormat, dataFormat, type);
            glBindTexture(GL_TEXTURE_2D, m_RendererID);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, (GLsizei)m_Specification.Width, (GLsizei)m_Specification.Height, dataFormat, type, data);
            if (m_Specification.GenerateMips)
                glGenerateMipmap(GL_TEXTURE_2D);
        }

        void Bind(uint32_t slot) const override
        {
            glActiveTexture(GL_TEXTURE0 + slot);
            glBindTexture(GL_TEXTURE_2D, m_RendererID);
        }

        bool operator==(const Texture& other) const override { return m_RendererID == other.GetRendererID(); }

    private:
        struct DecodedImage
        {
            stbi_uc* Pixels = nullptr;
            uint32_t Width = 0, Height = 0;
            ImageFormat Format = ImageFormat::RGBA8;
            std::string Error;

            DecodedImage() = default;
            DecodedImage(const DecodedImage&) = delete;
            DecodedImage& operator=(const DecodedImage&) = delete;
            ~DecodedImage() { if (Pixels) stbi_image_free(Pixels); }
        };

        static bool Decode(const std::string& path, DecodedImage& image)
        {
            int width, height, channels;
            stbi_set_flip_vertically_on_load(1);
            image.Pixels = stbi_load(path.c_str(), &width, &height, &channels, 0);
            if (image.Pixels && channels == 2) // grey + alpha: expand to RGBA
            {
                stbi_image_free(image.Pixels);
                image.Pixels = stbi_load(path.c_str(), &width, &height, &channels, 4);
                channels = 4;
            }
            if (!image.Pixels)
            {
                const char* reason = stbi_failure_reason();
                image.Error = reason ? reason : "noma'lum xato";
                return false;
            }
            image.Width = (uint32_t)width;
            image.Height = (uint32_t)height;
            image.Format = channels == 3 ? ImageFormat::RGB8 : channels == 1 ? ImageFormat::R8 : ImageFormat::RGBA8;
            return true;
        }

        void Allocate()
        {
            glGenTextures(1, &m_RendererID);
            DefineStorage();
        }

        // Size, format and sampling of the bound texture object (kept when it is defined again).
        void DefineStorage()
        {
            GLenum internalFormat, dataFormat, type;
            FormatToGL(m_Specification.Format, internalFormat, dataFormat, type);

            glBindTexture(GL_TEXTURE_2D, m_RendererID);
            glTexImage2D(GL_TEXTURE_2D, 0, (GLint)internalFormat, (GLsizei)m_Specification.Width, (GLsizei)m_Specification.Height,
                         0, dataFormat, type, nullptr);

            bool nearest = m_Specification.Filter == TextureFilter::Nearest;
            GLint minFilter = nearest ? (m_Specification.GenerateMips ? GL_NEAREST_MIPMAP_NEAREST : GL_NEAREST)
                                      : (m_Specification.GenerateMips ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, nearest ? GL_NEAREST : GL_LINEAR);
            GLint wrap = m_Specification.Wrap == TextureWrap::Repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);

            // Sample single-channel textures (e.g. font atlases) as white + alpha.
            GLint swizzle[] = { GL_ONE, GL_ONE, GL_ONE, GL_RED };
            GLint identity[] = { GL_RED, GL_GREEN, GL_BLUE, GL_ALPHA };
            glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, m_Specification.Format == ImageFormat::R8 ? swizzle : identity);
        }

    private:
        TextureSpecification m_Specification;
        std::string m_Path;
        uint32_t m_RendererID = 0;
        bool m_Loaded = false;
        bool m_FromFile = false; // m_Path is a file (not the name of an image decoded from memory)
    };

    Ref<Texture2D> Texture2D::Create(const TextureSpecification& specification) { return CreateRef<OpenGLTexture2D>(specification); }
    Ref<Texture2D> Texture2D::Create(const std::string& path, TextureFilter filter) { return CreateRef<OpenGLTexture2D>(path, filter); }
    Ref<Texture2D> Texture2D::CreateFromMemory(const void* data, size_t size, const std::string& name)
    {
        auto t = CreateRef<OpenGLTexture2D>(data, size, name);
        return t->IsLoaded() ? t : nullptr;
    }

    // ---------------------------------------------------------------- TextureLibrary
    static std::unordered_map<std::string, Ref<Texture2D>> s_TextureCache;

    static std::string CanonicalFile(const std::filesystem::path& path);

    Ref<Texture2D> TextureLibrary::Load(const std::filesystem::path& path, TextureFilter filter)
    {
        std::string file = CanonicalFile(path);
        std::string key = file + (filter == TextureFilter::Nearest ? "#nearest" : "");
        auto it = s_TextureCache.find(key);
        if (it != s_TextureCache.end())
            return it->second;

        Ref<Texture2D> texture = Texture2D::Create(file, filter);
        if (!texture->IsLoaded())
            return nullptr;
        s_TextureCache[key] = texture;
        return texture;
    }

    static std::string CanonicalFile(const std::filesystem::path& path)
    {
        std::error_code ec;
        std::filesystem::path abs = std::filesystem::weakly_canonical(path, ec);
        return (ec ? path : abs).string();
    }

    size_t TextureLibrary::Reload(const std::filesystem::path& path, bool* failed)
    {
        std::string file = CanonicalFile(path);
        size_t reloaded = 0;
        std::string error;
        for (auto& [key, texture] : s_TextureCache)
        {
            if (texture->GetPath() != file)
                continue;
            if (error.empty() && texture->Reload(&error)) // a file that failed once is not read again
                reloaded++;
            else if (failed)
                *failed = true;
        }
        if (!error.empty())
            ZM_CORE_ERROR("Rasm qayta yuklanmadi: '{}' ({}) - avvalgisi qoldi", file, error);
        return reloaded;
    }

    bool TextureLibrary::IsLoaded(const std::filesystem::path& path)
    {
        std::string file = CanonicalFile(path);
        for (const auto& [key, texture] : s_TextureCache)
            if (texture->GetPath() == file)
                return true;
        return false;
    }

    void TextureLibrary::Clear() { s_TextureCache.clear(); }

}

