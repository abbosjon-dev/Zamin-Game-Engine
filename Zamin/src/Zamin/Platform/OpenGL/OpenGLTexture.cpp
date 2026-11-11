#include "Zamin/Renderer/Texture.h"
#include "Zamin/Core/MemoryStats.h"
#include "Zamin/Asset/AssetImportLog.h"
#include "Zamin/Asset/AssetImporter.h"
#include "Zamin/Asset/AsyncLoader.h"

#include <glad/glad.h>
#include <stb_image.h>

#include "Zamin/Core/JobSystem.h"

#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

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

    // ---- sampling quality (the quality level): every texture read from an image takes it.
    class OpenGLTexture2D;
    namespace {
        struct SamplingQuality
        {
            int Anisotropy = 1;
            int SkippedMips = 0;
            std::unordered_set<OpenGLTexture2D*> ImageTextures; // alive, made from an image (file / memory)
            std::mutex Mutex; // ImageTextures: the main thread adds and walks them, the last owner (any thread) removes one
        };

        // Never destroyed: textures of static caches leave it as the program ends.
        SamplingQuality& Sampling()
        {
            static SamplingQuality* s_Sampling = new SamplingQuality();
            return *s_Sampling;
        }

        // The GPU's most anisotropic samples (1: no support), asked once.
        float MaxSupportedAnisotropy()
        {
            static float s_Max = -1.0f;
            if (s_Max < 0.0f)
            {
                s_Max = 1.0f;
                if (GLAD_GL_VERSION_4_6 || GLAD_GL_ARB_texture_filter_anisotropic || GLAD_GL_EXT_texture_filter_anisotropic)
                {
                    GLfloat value = 1.0f;
                    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &value);
                    s_Max = std::max(1.0f, (float)value);
                }
            }
            return s_Max;
        }

        // Mip levels of a texture of this size (level 0 included).
        int MipLevelCount(uint32_t width, uint32_t height)
        {
            int levels = 1;
            for (uint32_t size = std::max(width, height); size > 1; size >>= 1)
                levels++;
            return levels;
        }
    }

    // One pixel of this colour (made on first use, main thread).
    static GLuint MakePixelTexture(const uint8_t (&pixel)[4])
    {
        GLuint id = 0;
        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        return id;
    }

    // What a texture still loading binds: one transparent pixel.
    static GLuint PlaceholderTexture()
    {
        static GLuint s_Placeholder = MakePixelTexture({ 255, 255, 255, 0 });
        return s_Placeholder;
    }

    // What a texture whose file could not be read binds: one white pixel - it draws like no texture
    // (the colour alone), not black.
    static GLuint MissingTexture()
    {
        static GLuint s_Missing = MakePixelTexture({ 255, 255, 255, 255 });
        return s_Missing;
    }

    // A texture object freed: at once on the main thread; one whose last owner is a job system worker
    // (a model read in the background and dropped) is freed by the main thread's AsyncLoader::Update -
    // a worker has no GL context.
    static int s_DeferredDeleteOwner = 0;
    static void DeleteTextureObject(GLuint id)
    {
        if (!JobSystem::IsWorkerThread())
        {
            glDeleteTextures(1, &id);
            return;
        }
        AsyncLoader::QueueUpload(&s_DeferredDeleteOwner, [id] { glDeleteTextures(1, &id); });
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
            // Before the first upload: the quality level's sampling from the start (and on reloads).
            m_FromImage = true;
            ApplySettings(settings);
            auto image = std::make_unique<GLDecodedImage>();
            // Decoded ahead on a loading thread (an async scene load): only the upload is left.
            if (!TakePredecoded(path, *image) && !Decode(path, *image))
            {
                AssetImportLog::Report(path, image->Error); // once per file version
                return;
            }
            if (AsyncLoader::IsDeferringGPU()) // a worker (a model loaded in the background): the main thread uploads
            {
                Defer(std::move(image)); // (queued by Create, QueueDeferredUpload)
                return;
            }
            Upload(*image);
            AssetImportLog::Clear(path);
        }

        // TextureLibrary::Load on a worker (DeferGPU: a model read in the background): the file decoded
        // now, uploaded by the main thread - which reads the file's import settings then too: the asset
        // registry is the main thread's, a worker never looks into it. Its upload is queued by the
        // library once the texture is in the cache (a copy that lost the race to another thread is
        // dropped by the worker with nothing of the GPU's made).
        struct OnWorker {};
        OpenGLTexture2D(OnWorker, const std::string& path, bool pixelArt) : m_Path(path), m_FromFile(true), m_SettingsLater(pixelArt ? 1 : 0)
        {
            m_FromImage = true;
            auto image = std::make_unique<GLDecodedImage>();
            if (!TakePredecoded(path, *image) && !Decode(path, *image))
            {
                AssetImportLog::Report(path, image->Error); // once per file version
                return;
            }
            Defer(std::move(image));
        }

        // A file decoded by a worker later (TextureLibrary::LoadAsync): nothing read yet.
        struct LoadLater {};
        OpenGLTexture2D(LoadLater, const std::string& path, const TextureImportSettings& settings) : m_Path(path), m_FromFile(true)
        {
            m_FromImage = true;
            ApplySettings(settings);
            m_Pending = true;
            m_Loaded = true; // (until the worker finds it unreadable)
        }

        OpenGLTexture2D(const void* encoded, size_t size, const std::string& name) : m_Path(name)
        {
            m_FromImage = true;
            int width, height, channels;
            stbi_set_flip_vertically_on_load_thread(1); // same convention as file textures (glTF V is flipped in the mesh)
            auto image = std::make_unique<GLDecodedImage>();
            image->Pixels = stbi_load_from_memory((const stbi_uc*)encoded, (int)size, &width, &height, &channels, 4);
            if (!image->Pixels)
            {
                ZM_CORE_ERROR("Rasm (xotiradan) yuklanmadi: '{}' ({})", name, stbi_failure_reason());
                return;
            }
            image->Width = (uint32_t)width;
            image->Height = (uint32_t)height;
            image->Format = ImageFormat::RGBA8;
            if (AsyncLoader::IsDeferringGPU())
            {
                Defer(std::move(image));
                return;
            }
            Upload(*image);
        }

        // The last owner may be a worker (a model read in the background that it dropped): a queued
        // upload holds its texture (QueueDeferredUpload), so it is never freed while one runs, and its
        // GL object goes through DeleteTextureObject.
        ~OpenGLTexture2D() override
        {
            if (m_Pending)
                AsyncLoader::CancelUpload(this);
            if (m_InSampling)
            {
                std::lock_guard lock(Sampling().Mutex);
                Sampling().ImageTextures.erase(this);
            }
            if (m_RendererID)
                DeleteTextureObject(m_RendererID);
        }

        // A texture a worker made (Defer, not uploaded yet): its upload queued for AsyncLoader::Update.
        // The queue holds the texture until the upload has run (main thread).
        static void QueueDeferredUpload(const Ref<OpenGLTexture2D>& texture)
        {
            if (texture->m_Pending && texture->m_PendingImage && !texture->m_UploadQueued)
            {
                texture->m_UploadQueued = true;
                AsyncLoader::QueueUpload(texture.get(), [texture] {
                    if (texture.use_count() > 1) // (nothing but the queue holds it any more: not uploaded, freed here)
                        texture->FinishLoading();
                });
            }
        }

        bool IsReady() const override { return !m_Pending; }

        // LoadAsync's worker: the file's pixels (any thread; the main thread finishes with FinishLoading).
        void DecodeLater()
        {
            auto image = std::make_unique<GLDecodedImage>();
            if (!TakePredecoded(m_Path, *image) && !Decode(m_Path, *image))
            {
                m_PendingError = image->Error.empty() ? std::string("o'qilmadi") : image->Error;
                return;
            }
            m_PendingImage = std::move(image);
        }

        bool FinishLoading() override
        {
            if (!m_Pending)
                return m_Loaded;
            AsyncLoader::CancelUpload(this);
            m_Pending = false;
            ResolveSettingsLater();
            std::unique_ptr<GLDecodedImage> image = std::move(m_PendingImage);
            if (!image || !image->Pixels)
            {
                m_Loaded = false;
                if (m_FromFile)
                    AssetImportLog::Report(m_Path, m_PendingError);
                return false;
            }
            Upload(*image);
            if (m_FromFile)
                AssetImportLog::Clear(m_Path);
            return true;
        }

        // The quality level changed: anisotropy and the skipped mip levels again (binds the texture).
        void ApplyQualitySampling()
        {
            if (!m_RendererID || !m_FromImage)
                return;
            glBindTexture(GL_TEXTURE_2D, m_RendererID);
            ApplyQualityParameters();
        }

        bool Reimport(const TextureImportSettings& settings, std::string* error) override
        {
            if (!m_FromFile)
            {
                if (error) *error = "fayldan yuklanmagan";
                return false;
            }
            bool changed = !(settings == GetImportSettings());
            m_SettingsLater = -1; // (the settings given here win over the lookup a worker's texture would make at its upload)
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
            if (m_Pending)
            {
                // A LoadAsync decode may be under way on a worker (it writes the pending pixels): waited
                // for first, then finished (read again below).
                WaitForAsyncLoad(this);
                FinishLoading();
            }
            ResolveSettingsLater();
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
            AddToSampling();
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

        // The decoded pixels into a new GPU texture (main thread).
        void Upload(const GLDecodedImage& image)
        {
            AddToSampling(); // the quality level's sampling from the start (and on reloads)
            m_Specification.Width = image.Width;
            m_Specification.Height = image.Height;
            m_Specification.Format = image.Format;
            Allocate();
            SetData(image.Pixels, (uint32_t)(image.Width * image.Height * BytesPerPixel(image.Format)));
            m_Loaded = true;
        }

        // Made by a worker: the pixels kept for the main thread's AsyncLoader::Update (the upload is
        // queued by whoever holds the texture: QueueDeferredUpload).
        void Defer(std::unique_ptr<GLDecodedImage> image)
        {
            m_PendingImage = std::move(image);
            m_Pending = true;
            m_Loaded = true;
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
            {
                // The chain is made from the base level: the full picture, whatever the quality skips.
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
                glGenerateMipmap(GL_TEXTURE_2D);
                if (m_FromImage)
                    ApplyQualityParameters();
            }
        }

        void Bind(uint32_t slot) const override
        {
            glActiveTexture(GL_TEXTURE0 + slot);
            glBindTexture(GL_TEXTURE_2D, m_RendererID ? m_RendererID : (m_Pending ? PlaceholderTexture() : MissingTexture()));
        }

        bool operator==(const Texture& other) const override { return m_RendererID == other.GetRendererID(); }

    private:
        // Main thread: in the quality level's list (SetSamplingQuality walks it).
        void AddToSampling()
        {
            if (!m_FromImage || m_InSampling)
                return;
            std::lock_guard lock(Sampling().Mutex);
            Sampling().ImageTextures.insert(this);
            m_InSampling = true;
        }

        // Main thread: the import settings of a texture a worker made (OnWorker), read now.
        void ResolveSettingsLater()
        {
            if (m_SettingsLater < 0)
                return;
            ApplySettings(TextureLibrary::GetLoadSettings(m_Path, m_SettingsLater == 1));
            m_SettingsLater = -1;
        }

        // A LoadAsync load of this texture under way: its decode waited for and its finish run (main thread).
        static void WaitForAsyncLoad(const Texture2D* texture);

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
            // The memory statistics: the base level, a third more with its mip chain.
            const uint64_t base = (uint64_t)m_Specification.Width * m_Specification.Height * BytesPerPixel(m_Specification.Format);
            m_Memory.Set(m_Specification.GenerateMips ? base + base / 3 : base);
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
            if (m_FromImage)
                ApplyQualityParameters();
        }

        // Anisotropic filtering and the first mip level sampled (the bound texture): only textures
        // with mipmaps (a pixel-art copy without them stays crisp and full size).
        void ApplyQualityParameters()
        {
            const SamplingQuality& q = Sampling();
            const bool mips = m_Specification.GenerateMips;
            const int maxLevel = MipLevelCount(m_Specification.Width, m_Specification.Height) - 1;
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, mips ? std::clamp(q.SkippedMips, 0, maxLevel) : 0);
            if (MaxSupportedAnisotropy() > 1.0f)
            {
                const bool linear = m_Specification.Filter == TextureFilter::Linear;
                const float samples = mips && linear ? std::min((float)q.Anisotropy, MaxSupportedAnisotropy()) : 1.0f;
                glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, std::max(1.0f, samples));
            }
        }

    private:
        TextureSpecification m_Specification;
        std::string m_Path;
        uint32_t m_RendererID = 0;
        // (atomic: a worker reading the cache asks whether a texture the main thread is finishing is loaded)
        std::atomic<bool> m_Loaded{ false };
        bool m_FromFile = false; // m_Path is a file (not the name of an image decoded from memory)
        bool m_FromImage = false; // read from an image (a file or memory): takes the quality level's sampling
        bool m_InSampling = false; // in Sampling().ImageTextures (from its first upload on: main thread)
        std::atomic<bool> m_Pending{ false }; // still loading (IsReady false)
        bool m_UploadQueued = false;          // QueueDeferredUpload done
        int8_t m_SettingsLater = -1;          // OnWorker: its import settings read at the upload (1: the pixel-art copy)
        std::unique_ptr<GLDecodedImage> m_PendingImage; // its pixels, decoded, not uploaded yet
        std::string m_PendingError;
        TrackedMemory m_Memory{ MemoryCategory::Textures };
        friend class TextureLibrary;
    };

    void Texture2D::SetSamplingQuality(int anisotropy, int skippedMips)
    {
        SamplingQuality& q = Sampling();
        anisotropy = std::clamp(anisotropy, 1, 16);
        skippedMips = std::clamp(skippedMips, 0, 8);
        if (q.Anisotropy == anisotropy && q.SkippedMips == skippedMips)
            return;
        q.Anisotropy = anisotropy;
        q.SkippedMips = skippedMips;
        {
            std::lock_guard lock(q.Mutex);
            if (q.ImageTextures.empty())
                return;
        }
        GLint bound = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound);
        std::lock_guard lock(q.Mutex);
        for (OpenGLTexture2D* texture : q.ImageTextures)
            texture->ApplyQualitySampling();
        glBindTexture(GL_TEXTURE_2D, (GLuint)bound);
    }

    int Texture2D::GetAnisotropy() { return Sampling().Anisotropy; }
    int Texture2D::GetSkippedMips() { return Sampling().SkippedMips; }

    Ref<Texture2D> Texture2D::Create(const TextureSpecification& specification) { return CreateRef<OpenGLTexture2D>(specification); }
    Ref<Texture2D> Texture2D::Create(const std::string& path, TextureFilter filter)
    {
        TextureImportSettings settings;
        settings.Filter = filter;
        settings.Mipmaps = filter != TextureFilter::Nearest;
        return Create(path, settings);
    }
    Ref<Texture2D> Texture2D::Create(const std::string& path, const TextureImportSettings& settings)
    {
        auto t = CreateRef<OpenGLTexture2D>(path, settings);
        OpenGLTexture2D::QueueDeferredUpload(t); // (made by a worker)
        return t;
    }
    Ref<Texture2D> Texture2D::CreateFromMemory(const void* data, size_t size, const std::string& name)
    {
        auto t = CreateRef<OpenGLTexture2D>(data, size, name);
        if (!t->IsLoaded())
            return nullptr;
        OpenGLTexture2D::QueueDeferredUpload(t); // (made by a worker: a model's embedded image)
        return t;
    }

    // ---------------------------------------------------------------- TextureLibrary
    struct CachedTexture
    {
        Ref<Texture2D> Texture;
        uint64_t Serial = 0;  // when it was loaded (s_LoadSerial)
        uint64_t LastUse = 0;
    };
    static std::unordered_map<std::string, CachedTexture> s_TextureCache;
    static uint64_t s_LoadSerial = 0, s_CacheUse = 0;
    // The cache may be asked from a worker (a model loaded in the background reads its textures).
    static std::recursive_mutex s_TextureCacheMutex;
    // LoadAsync's textures still loading: their AsyncLoader loads (main thread).
    static std::unordered_map<const Texture2D*, uint64_t> s_AsyncLoads;
    // LoadAsync's textures that turned out unreadable (GetFailedLoadSerial).
    static uint64_t s_FailedLoadSerial = 0;

    void OpenGLTexture2D::WaitForAsyncLoad(const Texture2D* texture)
    {
        if (JobSystem::IsWorkerThread() || AsyncLoader::IsDeferringGPU())
            return;
        if (auto it = s_AsyncLoads.find(texture); it != s_AsyncLoads.end())
            AsyncLoader::Wait(it->second); // its decode, then its finish (the upload)
    }

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

    // A texture of the cache that is still loading, finished now (main thread) - a Load must give a ready one.
    static void FinishNow(const Ref<Texture2D>& texture)
    {
        // (the thread first: a worker never reads what the main thread is finishing)
        if (!texture || AsyncLoader::IsDeferringGPU() || JobSystem::IsWorkerThread() || texture->IsReady())
            return;
        if (auto it = s_AsyncLoads.find(texture.get()); it != s_AsyncLoads.end())
        {
            const uint64_t id = it->second;
            AsyncLoader::Wait(id); // its decode, then its finish (the upload)
        }
        texture->FinishLoading();
    }

    Ref<Texture2D> TextureLibrary::Load(const std::filesystem::path& path, TextureFilter filter)
    {
        std::string file = CanonicalFile(path);
        bool pixelArt = filter == TextureFilter::Nearest;
        std::string key = pixelArt ? file + std::string(s_PixelArtSuffix) : file;
        Ref<Texture2D> cached;
        {
            std::lock_guard lock(s_TextureCacheMutex);
            if (auto it = s_TextureCache.find(key); it != s_TextureCache.end())
            {
                it->second.LastUse = ++s_CacheUse;
                cached = it->second.Texture;
            }
        }
        if (cached)
        {
            FinishNow(cached); // (outside the lock: it waits for a worker)
            return cached->IsLoaded() ? cached : nullptr;
        }
        // A file that failed and did not change since is not decoded again (tilemaps and particles
        // ask for their texture every frame while they have none).
        if (!AssetImportLog::ShouldRetry(file))
            return nullptr;

        // Decoded outside the lock (a worker may be decoding another file meanwhile). On a worker (a model
        // read in the background) the import settings are read by the main thread at the upload.
        const bool worker = AsyncLoader::IsDeferringGPU();
        Ref<OpenGLTexture2D> texture = worker ? CreateRef<OpenGLTexture2D>(OpenGLTexture2D::OnWorker{}, file, pixelArt)
                                              : CreateRef<OpenGLTexture2D>(file, GetLoadSettings(file, pixelArt));
        if (!texture->IsLoaded())
            return nullptr;
        std::lock_guard lock(s_TextureCacheMutex);
        auto [it, added] = s_TextureCache.try_emplace(key, CachedTexture{ texture, s_LoadSerial + 1, ++s_CacheUse });
        if (!added)
            return it->second.Texture; // another thread's copy of the file came first (ours made nothing on the GPU)
        ++s_LoadSerial;
        OpenGLTexture2D::QueueDeferredUpload(texture); // (a worker's: the main thread uploads it)
        return texture;
    }

    Ref<Texture2D> TextureLibrary::LoadAsync(const std::filesystem::path& path, TextureFilter filter)
    {
        if (!AsyncLoader::IsEnabled() || AsyncLoader::IsDeferringGPU() || JobSystem::IsWorkerThread())
            return Load(path, filter);
        std::string file = CanonicalFile(path);
        bool pixelArt = filter == TextureFilter::Nearest;
        std::string key = pixelArt ? file + std::string(s_PixelArtSuffix) : file;
        std::lock_guard lock(s_TextureCacheMutex);
        if (auto it = s_TextureCache.find(key); it != s_TextureCache.end())
        {
            it->second.LastUse = ++s_CacheUse;
            return it->second.Texture->IsLoaded() ? it->second.Texture : nullptr;
        }
        if (!AssetImportLog::ShouldRetry(file))
            return nullptr;
        auto texture = CreateRef<OpenGLTexture2D>(OpenGLTexture2D::LoadLater{}, file, GetLoadSettings(file, pixelArt));
        s_TextureCache[key] = { texture, ++s_LoadSerial, ++s_CacheUse };
        const Texture2D* raw = texture.get();
        const uint64_t id = AsyncLoader::Enqueue(
            file, [texture] { texture->DecodeLater(); },
            [texture, key, raw] {
                s_AsyncLoads.erase(raw);
                if (!texture->FinishLoading())
                {
                    // Unreadable: out of the cache, like a Load that failed (reported once per file version);
                    // what took the texture meanwhile (sprites, tilemaps) lets go of it (GetFailedLoadSerial).
                    s_FailedLoadSerial++;
                    std::lock_guard lock(s_TextureCacheMutex);
                    if (auto it = s_TextureCache.find(key); it != s_TextureCache.end() && it->second.Texture.get() == raw)
                        s_TextureCache.erase(it);
                }
            });
        if (AsyncLoader::IsPending(id))
            s_AsyncLoads[raw] = id;
        return texture;
    }

    size_t TextureLibrary::GetLoadingCount() { return s_AsyncLoads.size(); }
    uint64_t TextureLibrary::GetFailedLoadSerial() { return s_FailedLoadSerial; }

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
        // The file's textures first, then read again outside the lock: one still loading in the background
        // is waited for, and the loads finished meanwhile change the cache.
        std::vector<std::pair<Ref<Texture2D>, bool>> textures; // (texture, the pixel-art copy)
        {
            std::lock_guard lock(s_TextureCacheMutex);
            for (const auto& [key, cached] : s_TextureCache)
                if (cached.Texture->GetPath() == file)
                    textures.emplace_back(cached.Texture, key.ends_with(s_PixelArtSuffix));
        }
        for (const auto& [texture, pixelArt] : textures)
        {
            FinishNow(texture);
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
        std::lock_guard lock(s_TextureCacheMutex);
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
        std::vector<Ref<Texture2D>> released; // freed outside the lock
        {
            std::lock_guard lock(s_TextureCacheMutex);
            std::erase_if(s_TextureCache, [&](const auto& item) {
                if (item.second.Serial <= since)
                    return false;
                // Only the cache has it - and its own upload, for one a worker made that is still queued.
                const auto* texture = static_cast<const OpenGLTexture2D*>(item.second.Texture.get());
                const long queued = texture->m_Pending && texture->m_UploadQueued ? 1 : 0;
                if (item.second.Texture.use_count() != 1 + queued)
                    return false;
                released.push_back(item.second.Texture);
                return true;
            });
        }
        for (const Ref<Texture2D>& texture : released)
            AsyncLoader::CancelUpload(texture.get()); // (not uploaded for nothing)
        return released.size();
    }

    void TextureLibrary::Clear()
    {
        AsyncLoader::WaitAll(); // (the loads' finishes look the cache up)
        std::lock_guard lock(s_TextureCacheMutex);
        s_TextureCache.clear();
        s_AsyncLoads.clear();
    }

    CacheResidencyStats TextureLibrary::GetResidencyStats()
    {
        std::lock_guard lock(s_TextureCacheMutex);
        CacheResidencyStats stats;
        stats.Entries = s_TextureCache.size();
        for (const auto& [key, entry] : s_TextureCache)
        {
            (void)key;
            const bool loading = !entry.Texture->IsReady();
            stats.Loading += loading;
            stats.Pinned += loading || entry.Texture.use_count() > 1;
        }
        return stats;
    }
    size_t TextureLibrary::TrimUnused(size_t targetEntries, size_t maxEvictions)
    {
        if (!maxEvictions) return 0;
        std::vector<Ref<Texture2D>> released;
        {
            std::lock_guard lock(s_TextureCacheMutex);
            std::vector<std::string> keys;
            if (s_TextureCache.size() <= targetEntries) return 0;
            for (const auto& [key, entry] : s_TextureCache)
                if (entry.Texture.use_count() == 1 && entry.Texture->IsReady()) keys.push_back(key);
            std::sort(keys.begin(), keys.end(), [](const auto& a, const auto& b) {
                const auto x = s_TextureCache.at(a).LastUse, y = s_TextureCache.at(b).LastUse;
                return x != y ? x < y : a < b;
            });
            for (const auto& key : keys)
            {
                if (s_TextureCache.size() <= targetEntries || released.size() >= maxEvictions) break;
                released.push_back(std::move(s_TextureCache.at(key).Texture));
                s_TextureCache.erase(key);
            }
        }
        // GL destruction and upload cancellation never happen under the cache lock.
        return released.size();
    }
}
