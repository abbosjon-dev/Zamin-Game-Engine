#pragma once

#include <glm/glm.hpp>

#include <cstdint>

namespace Zamin {

    // Pixel-perfect mode of an orthographic camera (CameraComponent::PixelPerfect): every texture
    // pixel (PixelsPerUnit of them per world unit) becomes the same whole number of screen pixels.
    // - The zoom is the largest whole number at which ReferenceResolution (art pixels) still fits
    //   in the view (at least 1); the camera then shows view size / zoom art pixels - a bigger
    //   window shows more of the world, never blurrier pixels. The camera's orthographic size is
    //   not used.
    // - UpscaleRenderTarget: the scene is drawn at the art's resolution (view size / zoom) and blown
    //   up with sharp (nearest) pixels - rotated sprites and lines stay on the pixel grid too.
    // - SnapCamera: the view moves in whole pixels (render-target pixels) - no shimmering.
    // - SnapSprites: sprites and tilemaps are drawn at whole pixels as well (their transforms stay).
    struct PixelPerfectSettings
    {
        bool Enabled = false;
        float PixelsPerUnit = 16.0f;
        glm::ivec2 ReferenceResolution{ 320, 180 };
        bool UpscaleRenderTarget = true;
        bool SnapCamera = true;
        bool SnapSprites = false;

        bool operator==(const PixelPerfectSettings&) const = default;
    };

    // What a pixel-perfect camera shows in a view of a given size.
    struct PixelPerfectView
    {
        int Zoom = 1;                       // screen pixels per art pixel
        glm::uvec2 ViewSize{ 0 };           // the view (screen pixels)
        glm::uvec2 RenderSize{ 0 };         // the render target: the view, or the art's size when upscaling
        glm::vec2 VisibleWorldSize{ 0.0f }; // world units the view shows (width, height)
        glm::vec2 RenderWorldSize{ 0.0f };  // world units the render target covers (>= the visible)
        float SnapUnit = 0.0f;              // world units of one render-target pixel
        bool Upscale = false;
    };

    class PixelPerfect
    {
    public:
        static constexpr float MinPixelsPerUnit = 1.0f;
        static constexpr float MaxPixelsPerUnit = 1024.0f;
        static constexpr int MaxZoom = 64;

        // The whole-number zoom for a view of w x h pixels (1 when the reference does not fit).
        static int GetZoom(const PixelPerfectSettings& settings, uint32_t width, uint32_t height);
        static PixelPerfectView Compute(const PixelPerfectSettings& settings, uint32_t width, uint32_t height);
        // The orthographic projection over the render target (`renderTarget`), or over the part of
        // it the view shows (what the screen <-> world mapping, overlays and bounds use).
        static glm::mat4 GetProjection(const PixelPerfectView& view, float nearClip, float farClip, bool renderTarget);
        // `p` on the grid of `unit` world units (nearest; a unit of 0 leaves it as it is).
        static glm::vec2 Snap(const glm::vec2& p, float unit);
        // Upscaling: the render-target pixel a view pixel shows (the picture is centred: the
        // render target may be up to one art pixel bigger than the view / zoom).
        static glm::ivec2 ViewToRenderPixel(const glm::ivec2& viewPixel, const PixelPerfectView& view);
        // The view pixel (of the picture's centring) the render target's pixel (0, 0) starts at.
        static glm::ivec2 GetUpscaleOffset(const PixelPerfectView& view);
        // Values out of range back into range.
        static void Sanitize(PixelPerfectSettings& settings);
    };

}
