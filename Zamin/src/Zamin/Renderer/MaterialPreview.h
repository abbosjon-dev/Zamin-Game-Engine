#pragma once

#include "Zamin/Renderer/Environment.h"
#include "Zamin/Renderer/Framebuffer.h"
#include "Zamin/Renderer/Material.h"

#include <cstdint>
#include <vector>

namespace Zamin {

    // Draws a material on a sphere (or another primitive) into an offscreen picture: the
    // inspector's material view and material thumbnails. Lit by a sun and the sky - the
    // procedural gradient, or an HDRI environment map (image based lighting) - tone mapped (ACES).
    // Needs the renderer (GPU); draw calls are not counted in the scene's statistics.
    class MaterialPreview
    {
    public:
        enum class Shape : uint8_t { Sphere = 0, Cube, Plane, Cylinder };

        struct Settings
        {
            Shape PreviewShape = Shape::Sphere;
            float Yaw = 0.5f;                  // object turned around Y (radians)
            float Pitch = 0.2f;                // and around X
            float Zoom = 1.0f;                 // camera distance factor (< 1: closer)
            Ref<EnvironmentMap> Environment;   // null: the procedural sky gradient
            float EnvironmentIntensity = 1.0f;
            float EnvironmentRotation = 0.0f;
            bool Background = true;            // the sky behind the object (false: a dark backdrop)
            bool Sun = true;                   // a key light from the upper left
        };

        // Renders into its framebuffer (resized to width x height when needed). Returns the color
        // texture (RGBA8, bottom row first - flip V to show it), 0 when nothing could be drawn.
        uint32_t Render(const Material& material, uint32_t width, uint32_t height, const Settings& settings);
        uint32_t GetTextureID() const;
        uint32_t GetWidth() const;
        uint32_t GetHeight() const;
        // The last picture as RGBA8, top row first (empty before the first Render).
        std::vector<uint8_t> ReadPixels() const;

        static const char* GetShapeName(Shape shape); // shown in the editor

    private:
        Ref<Framebuffer> m_Target, m_Final;
    };

}
