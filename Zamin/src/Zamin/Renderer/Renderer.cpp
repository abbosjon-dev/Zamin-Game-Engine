#include "Zamin/Renderer/Renderer.h"
#include "Zamin/Renderer/Renderer2D.h"
#include "Zamin/Renderer/Renderer3D.h"
#include "Zamin/Renderer/Font.h"
#include "Zamin/Renderer/Mesh.h"
#include "Zamin/Renderer/Texture.h"
#include "Zamin/Renderer/PostProcess.h"
#include "Zamin/Utils/FileSystem.h"

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
        MeshLibrary::Clear();
        TextureLibrary::Clear();
        PostProcessor::Shutdown();
        Renderer3D::Shutdown();
        Renderer2D::Shutdown();
        RenderCommand::Shutdown();
    }

    void Renderer::OnWindowResize(uint32_t width, uint32_t height)
    {
        RenderCommand::SetViewport(0, 0, width, height);
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
