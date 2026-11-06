#pragma once

#include "Zamin/Core/Base.h"

#include <filesystem>
#include <vector>
#include <string>

namespace Zamin {

    enum class ImageFormat { None = 0, R8, RGB8, RGBA8, RGBA32F };
    enum class TextureFilter { Linear, Nearest };
    enum class TextureWrap { Repeat, ClampToEdge, MirroredRepeat };

    struct TextureSpecification
    {
        uint32_t Width = 1;
        uint32_t Height = 1;
        ImageFormat Format = ImageFormat::RGBA8;
        TextureFilter Filter = TextureFilter::Linear;
        TextureWrap Wrap = TextureWrap::Repeat;
        bool GenerateMips = true;
        // The texels are sRGB-encoded colors (photos, sprites, albedo / emissive maps): shaders
        // turn them into linear light. false = linear data (masks, lookup tables) used as stored.
        bool SRGB = true;
    };

    // How a texture file is imported: the "Import" object of its .zmeta file (see AssetImporter).
    struct TextureImportSettings
    {
        TextureFilter Filter = TextureFilter::Linear;
        TextureWrap Wrap = TextureWrap::Repeat;
        bool Mipmaps = true;
        bool SRGB = true;

        bool operator==(const TextureImportSettings&) const = default;
    };

    class Texture
    {
    public:
        virtual ~Texture() = default;

        virtual const TextureSpecification& GetSpecification() const = 0;
        virtual uint32_t GetWidth() const = 0;
        virtual uint32_t GetHeight() const = 0;
        virtual uint32_t GetRendererID() const = 0;
        virtual const std::string& GetPath() const = 0;

        virtual void SetData(const void* data, uint32_t size) = 0;
        virtual void Bind(uint32_t slot = 0) const = 0;
        virtual bool IsLoaded() const = 0;

        virtual bool operator==(const Texture& other) const = 0;
    };

    class Texture2D : public Texture
    {
    public:
        static Ref<Texture2D> Create(const TextureSpecification& specification);
        static Ref<Texture2D> Create(const std::string& path, TextureFilter filter = TextureFilter::Linear);
        static Ref<Texture2D> Create(const std::string& path, const TextureImportSettings& settings);
        // Decode an encoded image (png/jpg/...) from memory, e.g. textures embedded in .glb files.
        static Ref<Texture2D> CreateFromMemory(const void* data, size_t size, const std::string& debugName = {});

        // Reads the file again into this texture (same GPU object, so everything that holds it
        // shows the new pixels; the size may change). False - and the old pixels stay - when the
        // file cannot be read or the texture was not made from a file: the reason goes to `error`,
        // or to the log without one.
        virtual bool Reload(std::string* error = nullptr) = 0;
        // Applies new import settings (sampling, mipmaps, color space) and reads the file again,
        // in place like Reload. On failure the old pixels stay but the new settings are kept.
        virtual bool Reimport(const TextureImportSettings& settings, std::string* error = nullptr) = 0;

        // The settings this texture was imported with (from its specification).
        TextureImportSettings GetImportSettings() const
        {
            const TextureSpecification& spec = GetSpecification();
            return { spec.Filter, spec.Wrap, spec.GenerateMips, spec.SRGB };
        }
    };

    // Caches textures by absolute path so the same file is only uploaded once. Files are imported
    // with their .zmeta import settings (AssetImporter::GetTextureSettings).
    class TextureLibrary
    {
    public:
        // filter = Linear: exactly as the import settings say. Nearest: the crisp-pixel copy that
        // pixel-art sprites and tilemaps ask for (nearest filtering, no mipmaps; wrap and color
        // space still from the import settings).
        static Ref<Texture2D> Load(const std::filesystem::path& path, TextureFilter filter = TextureFilter::Linear);
        // Settings a file is loaded with (its import settings, adjusted for the pixel-art copy).
        static TextureImportSettings GetLoadSettings(const std::filesystem::path& path, bool pixelArt = false);
        // Re-reads a file into every cached texture made from it (each filter variant), in place,
        // with its current import settings. Returns how many were updated; `failed` is set when
        // one could not be read.
        static size_t Reload(const std::filesystem::path& path, bool* failed = nullptr);
        // Whether a texture of this file is in the cache.
        static bool IsLoaded(const std::filesystem::path& path);
        // Grows with every texture the cache takes in. ReleaseUnused(since) drops the ones taken
        // in after GetLoadSerial() returned `since` that nothing else holds any more (loaded for
        // a moment only - a model drawn into a thumbnail); returns how many.
        static uint64_t GetLoadSerial();
        // ---- decoding ahead (SceneManager's async scene loading): Predecode reads and decodes the
        // file's pixels on the calling thread - any thread, nothing of the GPU - and keeps them until
        // the next Load of the file only uploads them. DropPredecoded lets go of pixels no Load took.
        static bool Predecode(const std::filesystem::path& path);
        static void DropPredecoded(const std::vector<std::filesystem::path>& paths);
        static size_t GetPredecodedCount();
        static size_t ReleaseUnused(uint64_t since);
        static void Clear();
    };

}
