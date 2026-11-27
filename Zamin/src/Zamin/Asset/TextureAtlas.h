#pragma once

/// @file
/// @brief Texture atlas packing: sprites and tiling terrain layers into pages with padding/bleed (CPU only).

#include <glm/glm.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Zamin {

    // What fills the padding around an image in its page: nothing (transparent), its edge pixels repeated
    // (sprites: linear filtering at the edge reads the sprite's own colour) or the opposite edge (a tiling
    // texture: a terrain layer sampled with repeat keeps its seamless wrap at every mip level).
    enum class AtlasBleed : uint8_t { None = 0, Clamp, Wrap };

    // One image to pack: RGBA8, rows top first (as image editors and SpriteFrame measure).
    struct AtlasImage
    {
        std::string Name;
        uint32_t Width = 0, Height = 0;
        std::vector<uint8_t> Pixels; // Width * Height * 4
        AtlasBleed Bleed = AtlasBleed::Clamp;
    };

    struct AtlasSettings
    {
        uint32_t MaxPageSize = 2048;  // px, square pages at most this big (a power of two)
        uint32_t Padding = 2;         // px of bleed on every side of an image (raised to 2^MipLevels)
        // Mip-safe levels: placements and padded cells are aligned to 2^MipLevels px, so the 2x2 box mips of
        // a page down to that level never mix two images, and bilinear sampling there still reads bleed.
        uint32_t MipLevels = 0;
        bool PowerOfTwo = true;       // pages shrink to the smallest power-of-two size that holds them
        uint32_t MaxPages = 16;

        bool operator==(const AtlasSettings&) const = default;
        // The padding actually used (at least 2^MipLevels so the last mip level keeps a bleed texel).
        uint32_t EffectivePadding() const;
        uint32_t Alignment() const { return 1u << (MipLevels > 8 ? 8 : MipLevels); }
    };

    struct AtlasPlacement
    {
        std::string Name;
        uint32_t Page = 0;
        uint32_t X = 0, Y = 0;           // the image's top-left pixel in its page (rows top first)
        uint32_t Width = 0, Height = 0;
        // Texture coordinates of the image itself in OpenGL convention (v = 0 at the bottom row).
        glm::vec2 UVMin{ 0.0f }, UVMax{ 1.0f };
        bool operator==(const AtlasPlacement&) const = default;
    };

    struct AtlasPage
    {
        uint32_t Width = 0, Height = 0;
        std::vector<uint8_t> Pixels; // RGBA8, rows top first
    };

    struct AtlasResult
    {
        std::vector<AtlasPage> Pages;
        std::vector<AtlasPlacement> Placements; // one per input image, in input order
        uint64_t ImagePixels = 0;               // the pixels of the images themselves
        std::string Error;
        // The share of the pages' pixels the images cover (0..1).
        double Occupancy() const;
        // Hash of the pages' pixels and placements (deterministic packing: the same inputs give the same hash).
        uint64_t Hash() const;
    };

    class AtlasPacker
    {
    public:
        // Packs the images into as few pages as fit (MaxRects, best short side fit). Deterministic: the order
        // is by height, width and name, never by input order or memory addresses. False with Error when an
        // image is larger than a page or more than MaxPages are needed (Pages / Placements then empty).
        static bool Pack(std::span<const AtlasImage> images, const AtlasSettings& settings, AtlasResult& out);
        // The 2x2 box mip chain of a page (levels 1..n, RGBA8): what a GPU would make of it (tests, cooking).
        static std::vector<std::vector<uint8_t>> BuildMips(const AtlasPage& page, uint32_t levels);
    };

    // 64-bit FNV-1a, chainable (cook cache keys, atlas source hashes).
    uint64_t HashBytes(const void* data, size_t size, uint64_t seed = 14695981039346656037ull);

}
