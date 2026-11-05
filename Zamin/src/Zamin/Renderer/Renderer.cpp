#include "Zamin/Renderer/Renderer.h"
#include "Zamin/Debug/DebugDraw.h"
#include "Zamin/Renderer/Renderer2D.h"
#include "Zamin/Renderer/Renderer3D.h"
#include "Zamin/Renderer/Font.h"
#include "Zamin/Renderer/Mesh.h"
#include "Zamin/Renderer/Model.h"
#include "Zamin/Renderer/MaterialAsset.h"
#include "Zamin/Renderer/Environment.h"
#include "Zamin/Renderer/Texture.h"
#include "Zamin/Renderer/PostProcess.h"
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
        PostProcessor::Init();
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
        PostProcessor::Shutdown();
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
        float scale = std::isfinite(renderScale) ? std::clamp(renderScale, MinRenderScale, MaxRenderScale) : 1.0f;
        scale = std::min({ scale, (float)MaxRenderSize / (float)width, (float)MaxRenderSize / (float)height });
        auto side = [&](uint32_t size) {
            return (uint32_t)std::clamp(std::lround((double)size * scale), 1l, (long)MaxRenderSize);
        };
        return { side(width), side(height) };
    }

    std::string RenderStatistics::Format() const
    {
        return std::format("draw call {}, uchburchak {} | mesh: {} chizildi, {} kesildi, {} instanced ({} chaqiruv), {} shaffof | "
                           "soya: {} chizildi, {} kesildi, {} instanced ({} chaqiruv) | 2D: {} chizildi, {} kesildi | tile bo'laklari: "
                           "{} chizildi, {} kesildi | quad {}, doira {}, chiziq {}",
                           DrawCalls, TriangleCount, MeshCount, CulledMeshCount, InstancedMeshCount, InstancedDrawCount, TransparentCount,
                           ShadowCasterCount, CulledShadowCasterCount, InstancedShadowCasterCount, InstancedShadowDrawCount, SpriteCount,
                           CulledSpriteCount, TileChunkCount, CulledTileChunkCount, QuadCount, CircleCount, LineCount);
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
