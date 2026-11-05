#pragma once

#include "Zamin/Core/Base.h"

#include <filesystem>
#include <string>

namespace Zamin {

    enum class ImageFormat { None = 0, R8, RGB8, RGBA8, RGBA32F };
    enum class TextureFilter { Linear, Nearest };
    enum class TextureWrap { Repeat, ClampToEdge };

    struct TextureSpecification
    {
        uint32_t Width = 1;
        uint32_t Height = 1;
        ImageFormat Format = ImageFormat::RGBA8;
        TextureFilter Filter = TextureFilter::Linear;
        TextureWrap Wrap = TextureWrap::Repeat;
        bool GenerateMips = true;
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
        // Decode an encoded image (png/jpg/...) from memory, e.g. textures embedded in .glb files.
        static Ref<Texture2D> CreateFromMemory(const void* data, size_t size, const std::string& debugName = {});

        // Reads the file again into this texture (same GPU object, so everything that holds it
        // shows the new pixels; the size may change). False - and the old pixels stay - when the
        // file cannot be read or the texture was not made from a file.
        virtual bool Reload() = 0;
    };

    // Caches textures by absolute path so the same file is only uploaded once.
    class TextureLibrary
    {
    public:
        static Ref<Texture2D> Load(const std::filesystem::path& path, TextureFilter filter = TextureFilter::Linear);
        // Re-reads a file into every cached texture made from it (each filter variant), in place.
        // Returns how many were updated; `failed` is set when one could not be read.
        static size_t Reload(const std::filesystem::path& path, bool* failed = nullptr);
        // Whether a texture of this file is in the cache.
        static bool IsLoaded(const std::filesystem::path& path);
        static void Clear();
    };

}
