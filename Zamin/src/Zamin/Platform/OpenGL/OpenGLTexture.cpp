#include "Zamin/Renderer/Texture.h"
#include "Zamin/Asset/AssetImportLog.h"
#include "Zamin/Asset/AssetImporter.h"

#include <glad/glad.h>
#include <stb_image.h>

#include <memory>
#include <mutex>
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

    struct GLDecodedImage
    {
        stbi_uc* Pixels = nullptr;
        uint32_t Width = 0, Height = 0;
        ImageFormat Format = ImageFormat::RGBA8;
        std::string Error;

        GLDecodedImage() = default;
        GLDecodedImage(const GLDecodedImage&) = delete;
        GLDecodedImage& operator=(const GLDecodedImage&) = delete;
        ~GLDecodedImage() { if (Pixels) stbi_image_free(Pixels); }
    };

    static bool Decode(const std::string& path, GLDecodedImage& image)
    {
        if (std::string unreadable = AssetImportLog::CheckReadable(path); !unreadable.empty())
        {
            image.Error = std::move(unreadable);
            return false;
        }
        int width, height, channels;
        stbi_set_flip_vertically_on_load_thread(1); // per thread (see Image::Load)
        image.Pixels = stbi_load(path.c_str(), &width, &height, &channels, 0);
        if (image.Pixels && channels == 2) // grey + alpha: expand to RGBA
        {
            stbi_image_free(image.Pixels);
            image.Pixels = stbi_load(path.c_str(), &width, &height, &channels, 4);
            channels = 4;
        }
        if (!image.Pixels)
        {
            image.Error = AssetImportLog::DescribeImageError(stbi_failure_reason());
            return false;
        }
        image.Width = (uint32_t)width;
        image.Height = (uint32_t)height;
        image.Format = channels == 3 ? ImageFormat::RGB8 : channels == 1 ? ImageFormat::R8 : ImageFormat::RGBA8;
        return true;
    }

    // Pixels decoded ahead of their upload (TextureLibrary::Predecode, any thread), by canonical file.
    static std::mutex s_PredecodeMutex;
    static std::unordered_map<std::string, std::unique_ptr<GLDecodedImage>> s_Predecoded;

    // The file's predecoded pixels move into `image` (false when there are none).
    static bool TakePredecoded(const std::string& path, GLDecodedImage& image)
    {
        std::lock_guard lock(s_PredecodeMutex);
        auto it = s_Predecoded.find(path);
        if (it == s_Predecoded.end())
            return false;
        GLDecodedImage& ready = *it->second;
        image.Pixels = ready.Pixels;
        image.Width = ready.Width;
        image.Height = ready.Height;
        image.Format = ready.Format;
        ready.Pixels = nullptr;
        s_Predecoded.erase(it);
        return true;
    }

    class OpenGLTexture2D : public Texture2D
    {
    public:
        explicit OpenGLTexture2D(const TextureSpecification& spec) : m_Specification(spec)
        {
            Allocate();
            m_Loaded = true;
        }

        OpenGLTexture2D(const std::string& path, const TextureImportSettings& settings) : m_Path(path), m_FromFile(true)
        {
            ApplySettings(settings);
            GLDecodedImage image;
            // Decoded ahead on a loading thread (an async scene load): only the upload is left.
            if (!TakePredecoded(path, image) && !Decode(path, image))
            {
                AssetImportLog::Report(path, image.Error); // once per file version
                return;
            }
            m_Specification.Width = image.Width;
            m_Specification.Height = image.Height;
            m_Specification.Format = image.Format;

            Allocate();
            SetData(image.Pixels, (uint32_t)(image.Width * image.Height * BytesPerPixel(image.Format)));
            m_Loaded = true;
            AssetImportLog::Clear(path);
        }

        OpenGLTexture2D(const void* encoded, size_t size, const std::string& name) : m_Path(name)
        {
            int width, height, channels;
            stbi_set_flip_vertically_on_load_thread(1); // same convention as file textures (glTF V is flipped in the mesh)
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

        bool Reimport(const TextureImportSettings& settings, std::string* error) override
        {
            if (!m_FromFile)
            {
                if (error) *error = "fayldan yuklanmagan";
                return false;
            }
            bool changed = !(settings == GetImportSettings());
            ApplySettings(settings);
            if (Reload(error))
                return true;
            if (changed && m_RendererID)
                ApplySampling(); // the old pixels with the new sampling
            return false;
        }

        bool Reload(std::string* error) override
        {
            if (!m_FromFile)
            {
                if (error) *error = "fayldan yuklanmagan";
                return false;
            }
            GLDecodedImage image;
            if (!Decode(m_Path, image))
            {
                if (error)
                    *error = image.Error;
                else
                    AssetImportLog::Report(m_Path, image.Error, true);
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
            else
                ApplySampling();  // import settings may have changed
            SetData(image.Pixels, (uint32_t)(image.Width * image.Height * BytesPerPixel(image.Format)));
            m_Loaded = true;
            AssetImportLog::Clear(m_Path);
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
        void ApplySettings(const TextureImportSettings& settings)
        {
            m_Specification.Filter = settings.Filter;
            m_Specification.Wrap = settings.Wrap;
            m_Specification.GenerateMips = settings.Mipmaps;
            m_Specification.SRGB = settings.SRGB;
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
            ApplySampling();
        }

        // Filtering, wrapping and channel swizzle of the texture object (binds it).
        void ApplySampling()
        {
            glBindTexture(GL_TEXTURE_2D, m_RendererID);
            bool nearest = m_Specification.Filter == TextureFilter::Nearest;
            GLint minFilter = nearest ? (m_Specification.GenerateMips ? GL_NEAREST_MIPMAP_NEAREST : GL_NEAREST)
                                      : (m_Specification.GenerateMips ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, nearest ? GL_NEAREST : GL_LINEAR);
            GLint wrap = m_Specification.Wrap == TextureWrap::ClampToEdge ? GL_CLAMP_TO_EDGE
                       : m_Specification.Wrap == TextureWrap::MirroredRepeat ? GL_MIRRORED_REPEAT : GL_REPEAT;
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
    Ref<Texture2D> Texture2D::Create(const std::string& path, TextureFilter filter)
    {
        TextureImportSettings settings;
        settings.Filter = filter;
        settings.Mipmaps = filter != TextureFilter::Nearest;
        return CreateRef<OpenGLTexture2D>(path, settings);
    }
    Ref<Texture2D> Texture2D::Create(const std::string& path, const TextureImportSettings& settings) { return CreateRef<OpenGLTexture2D>(path, settings); }
    Ref<Texture2D> Texture2D::CreateFromMemory(const void* data, size_t size, const std::string& name)
    {
        auto t = CreateRef<OpenGLTexture2D>(data, size, name);
        return t->IsLoaded() ? t : nullptr;
    }

    // ---------------------------------------------------------------- TextureLibrary
    struct CachedTexture
    {
        Ref<Texture2D> Texture;
        uint64_t Serial = 0;  // when it was loaded (s_LoadSerial)
    };
    static std::unordered_map<std::string, CachedTexture> s_TextureCache;
    static uint64_t s_LoadSerial = 0;

    static std::string CanonicalFile(const std::filesystem::path& path);

    static constexpr std::string_view s_PixelArtSuffix = "#nearest";

    TextureImportSettings TextureLibrary::GetLoadSettings(const std::filesystem::path& path, bool pixelArt)
    {
        TextureImportSettings settings = AssetImporter::GetTextureSettings(path);
        if (pixelArt)
        {
            settings.Filter = TextureFilter::Nearest;
            settings.Mipmaps = false;
        }
        return settings;
    }

    Ref<Texture2D> TextureLibrary::Load(const std::filesystem::path& path, TextureFilter filter)
    {
        std::string file = CanonicalFile(path);
        bool pixelArt = filter == TextureFilter::Nearest;
        std::string key = pixelArt ? file + std::string(s_PixelArtSuffix) : file;
        auto it = s_TextureCache.find(key);
        if (it != s_TextureCache.end())
            return it->second.Texture;
        // A file that failed and did not change since is not decoded again (tilemaps and particles
        // ask for their texture every frame while they have none).
        if (!AssetImportLog::ShouldRetry(file))
            return nullptr;

        Ref<Texture2D> texture = Texture2D::Create(file, GetLoadSettings(file, pixelArt));
        if (!texture->IsLoaded())
            return nullptr;
        s_TextureCache[key] = { texture, ++s_LoadSerial };
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
        for (auto& [key, cached] : s_TextureCache)
        {
            const Ref<Texture2D>& texture = cached.Texture;
            if (texture->GetPath() != file)
                continue;
            bool pixelArt = key.ends_with(s_PixelArtSuffix);
            if (error.empty() && texture->Reimport(GetLoadSettings(file, pixelArt), &error)) // a file that failed once is not read again
                reloaded++;
            else if (failed)
                *failed = true;
        }
        if (!error.empty())
            AssetImportLog::Report(file, error, true);
        return reloaded;
    }

    bool TextureLibrary::IsLoaded(const std::filesystem::path& path)
    {
        std::string file = CanonicalFile(path);
        for (const auto& [key, cached] : s_TextureCache)
            if (cached.Texture->GetPath() == file)
                return true;
        return false;
    }

    uint64_t TextureLibrary::GetLoadSerial() { return s_LoadSerial; }

    bool TextureLibrary::Predecode(const std::filesystem::path& path)
    {
        const std::string file = CanonicalFile(path);
        {
            std::lock_guard lock(s_PredecodeMutex);
            if (s_Predecoded.contains(file))
                return true;
        }
        auto image = std::make_unique<GLDecodedImage>();
        if (!Decode(file, *image))
            return false; // the upload decodes it again and reports why it cannot be read
        std::lock_guard lock(s_PredecodeMutex);
        s_Predecoded.try_emplace(file, std::move(image));
        return true;
    }

    void TextureLibrary::DropPredecoded(const std::vector<std::filesystem::path>& paths)
    {
        std::vector<std::string> files;
        files.reserve(paths.size());
        for (const auto& path : paths)
            files.push_back(CanonicalFile(path));
        std::lock_guard lock(s_PredecodeMutex);
        for (const std::string& file : files)
            s_Predecoded.erase(file);
    }

    size_t TextureLibrary::GetPredecodedCount()
    {
        std::lock_guard lock(s_PredecodeMutex);
        return s_Predecoded.size();
    }

    size_t TextureLibrary::ReleaseUnused(uint64_t since)
    {
        return std::erase_if(s_TextureCache, [&](const auto& item) {
            return item.second.Serial > since && item.second.Texture.use_count() == 1; // only the cache has it
        });
    }

    void TextureLibrary::Clear() { s_TextureCache.clear(); }

}

