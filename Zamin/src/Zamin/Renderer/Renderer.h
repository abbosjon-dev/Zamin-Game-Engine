#pragma once

#include "Zamin/Renderer/RenderCommand.h"

#include <filesystem>

namespace Zamin {

    struct RenderStatistics
    {
        uint32_t DrawCalls = 0;
        uint32_t QuadCount = 0;
        uint32_t CircleCount = 0;
        uint32_t LineCount = 0;
        uint32_t MeshCount = 0;
        uint32_t TriangleCount = 0;
        uint32_t TransparentCount = 0; // meshes drawn in the sorted transparent pass (also in MeshCount)
        uint32_t CulledMeshCount = 0;  // meshes skipped: outside the camera's view (frustum culling)
        uint32_t CulledShadowCasterCount = 0; // shadow caster draws skipped: outside a cascade's volume
    };

    class Renderer
    {
    public:
        static void Init();
        static void Shutdown();
        static void OnWindowResize(uint32_t width, uint32_t height);

        static RendererAPI::API GetAPI() { return RendererAPI::GetAPI(); }

        // Render scale (SceneSettings::RenderScale): the 3D scene picture is drawn at this many
        // times the view's size, then scaled to it with linear filtering (below 1: faster and
        // softer, above 1: supersampled).
        static constexpr float MinRenderScale = 0.5f;
        static constexpr float MaxRenderScale = 2.0f;
        static constexpr uint32_t MaxRenderSize = 8192; // the largest framebuffer side
        // The size a width x height view is drawn at with `renderScale` (clamped to the range
        // above): each side rounded, at least 1 pixel; the scale is lowered as needed so that
        // neither side exceeds MaxRenderSize (the aspect ratio is kept).
        static glm::uvec2 GetRenderResolution(uint32_t width, uint32_t height, float renderScale);

        static RenderStatistics& GetStats() { return s_Stats; }
        static void ResetStats() { s_Stats = {}; }

        // Directory containing engine resources (fonts, icons ...).
        static std::filesystem::path GetResourcesDirectory();

    private:
        static inline RenderStatistics s_Stats;
    };

}
