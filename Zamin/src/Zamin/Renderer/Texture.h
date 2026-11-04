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
    };

    // Caches textures by absolute path so the same file is only uploaded once.
    class TextureLibrary
    {
    public:
        static Ref<Texture2D> Load(const std::filesystem::path& path, TextureFilter filter = TextureFilter::Linear);
        static void Clear();
    };

}
