#include "Zamin/Scene/PixelPerfect.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace Zamin {

    void PixelPerfect::Sanitize(PixelPerfectSettings& s)
    {
        s.PixelsPerUnit = std::isfinite(s.PixelsPerUnit) ? std::clamp(s.PixelsPerUnit, MinPixelsPerUnit, MaxPixelsPerUnit) : 16.0f;
        s.ReferenceResolution = glm::clamp(s.ReferenceResolution, glm::ivec2(1), glm::ivec2(8192));
    }

    int PixelPerfect::GetZoom(const PixelPerfectSettings& settings, uint32_t width, uint32_t height)
    {
        PixelPerfectSettings s = settings;
        Sanitize(s);
        if (width == 0 || height == 0)
            return 1;
        int zx = (int)width / s.ReferenceResolution.x, zy = (int)height / s.ReferenceResolution.y;
        return std::clamp(std::min(zx, zy), 1, MaxZoom);
    }

    PixelPerfectView PixelPerfect::Compute(const PixelPerfectSettings& settings, uint32_t width, uint32_t height)
    {
        PixelPerfectSettings s = settings;
        Sanitize(s);
        PixelPerfectView v;
        v.Zoom = GetZoom(s, width, height);
        v.ViewSize = { std::max(1u, width), std::max(1u, height) };
        v.Upscale = s.UpscaleRenderTarget && v.Zoom > 1;
        const float artPerUnit = s.PixelsPerUnit;              // art pixels per world unit
        const float screenPerUnit = artPerUnit * (float)v.Zoom; // screen pixels per world unit
        v.VisibleWorldSize = glm::vec2(v.ViewSize) / screenPerUnit;
        if (v.Upscale)
        {
            // The art's resolution, rounded up: the centred picture covers the whole view.
            v.RenderSize = { (v.ViewSize.x + (uint32_t)v.Zoom - 1) / (uint32_t)v.Zoom, (v.ViewSize.y + (uint32_t)v.Zoom - 1) / (uint32_t)v.Zoom };
            v.RenderWorldSize = glm::vec2(v.RenderSize) / artPerUnit;
            v.SnapUnit = 1.0f / artPerUnit;
        }
        else
        {
            v.RenderSize = v.ViewSize;
            v.RenderWorldSize = v.VisibleWorldSize;
            v.SnapUnit = 1.0f / screenPerUnit;
        }
        return v;
    }

    glm::mat4 PixelPerfect::GetProjection(const PixelPerfectView& view, float nearClip, float farClip, bool renderTarget)
    {
        glm::vec2 half = (renderTarget ? view.RenderWorldSize : view.VisibleWorldSize) * 0.5f;
        return glm::ortho(-half.x, half.x, -half.y, half.y, nearClip, farClip);
    }

    glm::vec2 PixelPerfect::Snap(const glm::vec2& p, float unit)
    {
        if (!(unit > 0.0f) || !std::isfinite(unit))
            return p;
        return { std::round(p.x / unit) * unit, std::round(p.y / unit) * unit };
    }

    glm::ivec2 PixelPerfect::GetUpscaleOffset(const PixelPerfectView& view)
    {
        if (!view.Upscale)
            return { 0, 0 };
        glm::ivec2 covered = glm::ivec2(view.RenderSize) * view.Zoom;
        return (covered - glm::ivec2(view.ViewSize)) / 2;
    }

    glm::ivec2 PixelPerfect::ViewToRenderPixel(const glm::ivec2& viewPixel, const PixelPerfectView& view)
    {
        if (!view.Upscale || view.Zoom <= 1)
            return viewPixel;
        glm::ivec2 p = viewPixel + GetUpscaleOffset(view);
        auto floorDiv = [](int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); };
        return { floorDiv(p.x, view.Zoom), floorDiv(p.y, view.Zoom) };
    }

}
