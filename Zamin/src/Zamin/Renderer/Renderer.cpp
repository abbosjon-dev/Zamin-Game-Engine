#include "Zamin/Renderer/Renderer.h"
#include "Zamin/Renderer/ShaderCache.h"
#include "Zamin/Renderer/DisplayPass.h"
#include "Zamin/Renderer/Quality.h"
#include "Zamin/Debug/DebugDraw.h"
#include "Zamin/Renderer/Renderer2D.h"
#include "Zamin/Renderer/Renderer3D.h"
#include "Zamin/Renderer/ParticleRenderer.h"
#include "Zamin/Renderer/Font.h"
#include "Zamin/Renderer/Mesh.h"
#include "Zamin/Renderer/Model.h"
#include "Zamin/Renderer/MaterialAsset.h"
#include "Zamin/Renderer/Environment.h"
#include "Zamin/Renderer/Texture.h"
#include "Zamin/Renderer/PostProcess.h"
#include "Zamin/Renderer/SelectionOutline.h"
#include "Zamin/Utils/FileSystem.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace Zamin {

    void Renderer::Init()
    {
        RenderCommand::Init();
        Renderer2D::Init();
        Renderer3D::Init();
        ParticleRenderer::Init();
        PostProcessor::Init();
        const ShaderCache::Stats& shaders = ShaderCache::GetStats();
        ZM_CORE_INFO("Shader'lar: {} ta keshdan ({:.1f} ms), {} ta kompilyatsiya ({:.1f} ms){}", shaders.Hits, shaders.LoadMilliseconds,
                     shaders.Misses, shaders.CompileMilliseconds,
                     ShaderCache::IsAvailable()                ? std::string()
                     : ShaderCache::GetDirectory().empty()     ? std::string(" - binary kesh o'chiq")
                                                               : std::string(" - drayver program binary'ni qo'llamaydi"));
    }

    void Renderer::Shutdown()
    {
        Font::ClearCache();
        Model::ClearCache();
        MaterialLibrary::Clear();
        EnvironmentMap::ClearCache();
        MeshLibrary::Clear();
        TextureLibrary::Clear();
        DebugDraw::Shutdown();
        SelectionOutline::Shutdown();
        PostProcessor::Shutdown();
        DisplayPass::Shutdown();
        ParticleRenderer::Shutdown();
        Renderer3D::Shutdown();
        Renderer2D::Shutdown();
        RenderCommand::Shutdown();
    }

    void Renderer::OnWindowResize(uint32_t width, uint32_t height)
    {
        RenderCommand::SetViewport(0, 0, width, height);
    }

    glm::uvec2 Renderer::GetRenderResolution(uint32_t width, uint32_t height, float renderScale)
    {
        if (width == 0 || height == 0)
            return { width, height };
        // The scene's render scale x the quality level's.
        renderScale = Quality::GetActive().ApplyRenderScale(renderScale);
        float scale = std::isfinite(renderScale) ? std::clamp(renderScale, MinRenderScale, MaxRenderScale) : 1.0f;
        scale = std::min({ scale, (float)MaxRenderSize / (float)width, (float)MaxRenderSize / (float)height });
        auto side = [&](uint32_t size) {
            return (uint32_t)std::clamp(std::lround((double)size * scale), 1l, (long)MaxRenderSize);
        };
        return { side(width), side(height) };
    }

    std::string RenderStatistics::FormatCount(uint64_t value)
    {
        if (value < 1000)
            return std::to_string(value);
        static const char* suffixes[] = { "K", "M", "B", "T" };
        double v = (double)value;
        int unit = -1;
        while (v >= 999.5 && unit < 3)
        {
            v /= 1000.0;
            unit++;
        }
        // Three significant digits: 1.24M, 12.3K, 456K.
        std::string text = v < 9.995 ? std::format("{:.2f}", v) : v < 99.95 ? std::format("{:.1f}", v) : std::format("{:.0f}", v);
        return text + suffixes[unit];
    }

    std::string RenderStatistics::Format() const
    {
        return std::format("draw call {}, uchburchak {} | mesh: {} chizildi, {} kesildi, {} instanced ({} chaqiruv), {} shaffof | "
                           "soya: {} chizildi, {} kesildi ({} ko'rinmas soya), {} instanced ({} chaqiruv) | 2D: {} chizildi, {} kesildi | tile bo'laklari: "
                           "{} chizildi, {} kesildi | quad {}, doira {}, chiziq {} | particle {} ({} instanced, {} chaqiruv) | "
                           "2D batch bo'linishi: tekstura {}, tur {}, holat {}; tartibi o'zgargan {} | uniform bufer {} ({} bayt)",
                           DrawCalls, TriangleCount, MeshCount, CulledMeshCount, InstancedMeshCount, InstancedDrawCount, TransparentCount,
                           ShadowCasterCount, CulledShadowCasterCount, CulledShadowByViewCount, InstancedShadowCasterCount, InstancedShadowDrawCount, SpriteCount,
                           CulledSpriteCount, TileChunkCount, CulledTileChunkCount, QuadCount, CircleCount, LineCount, ParticleCount,
                           InstancedParticleCount, InstancedParticleDrawCount, Flush2DTextures, Flush2DKind, Flush2DState, Reordered2DCount,
                           UniformBufferUploads, UniformBufferBytes);
    }

    std::filesystem::path Renderer::GetResourcesDirectory()
    {
        static std::filesystem::path dir = [] {
            std::filesystem::path candidates[] = {
                FileSystem::GetExecutableDirectory() / "Resources",
                FileSystem::GetExecutableDirectory().parent_path() / "Resources",
                std::filesystem::current_path() / "Resources",
            };
            for (auto& c : candidates)
                if (std::filesystem::exists(c))
                    return c;
            ZM_CORE_WARN("'Resources' papkasi topilmadi");
            return candidates[0];
        }();
        return dir;
    }

}
