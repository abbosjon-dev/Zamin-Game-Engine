#pragma once

#include "Zamin/Renderer/RenderCommand.h"

#include <filesystem>
#include <string>

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
        uint32_t InstancedMeshCount = 0;  // meshes drawn as instances (also in MeshCount)
        uint32_t InstancedDrawCount = 0;  // the instanced draw calls they took (also in DrawCalls)
        uint32_t ShadowCasterCount = 0;   // shadow caster draws into the cascades (summed over the cascades)
        uint32_t CulledShadowCasterCount = 0; // shadow caster draws skipped: outside a cascade's volume
        uint32_t InstancedShadowCasterCount = 0; // shadow caster draws done as instances (also in ShadowCasterCount)
        uint32_t InstancedShadowDrawCount = 0;   // the instanced draw calls they took (also in DrawCalls)
        uint32_t SpriteCount = 0;         // 2D objects drawn: sprites, circles, texts, particle systems
        uint32_t CulledSpriteCount = 0;   // 2D objects skipped: outside the camera's view
        uint32_t TileChunkCount = 0;      // tilemap chunks drawn (blocks of TilemapChunkSize x TilemapChunkSize tiles)
        uint32_t CulledTileChunkCount = 0; // tilemap chunks skipped: outside the camera's view
        uint32_t ParticleCount = 0;       // particles drawn (every system)
        uint32_t InstancedParticleCount = 0;     // of them drawn as GPU instances (ParticleRenderer)
        uint32_t InstancedParticleDrawCount = 0; // the instanced draw calls they took (also in DrawCalls)

        // One line for logs (ZAMIN_EDITOR_STATS / ZAMIN_PLAYER_STATS): what was drawn, culled
        // and instanced.
        std::string Format() const;
        // A count short enough for an overlay: 950, 12.3K, 1.24M, 2.1B.
        static std::string FormatCount(uint64_t value);
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
