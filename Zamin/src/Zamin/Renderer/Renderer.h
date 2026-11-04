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
    };

    class Renderer
    {
    public:
        static void Init();
        static void Shutdown();
        static void OnWindowResize(uint32_t width, uint32_t height);

        static RendererAPI::API GetAPI() { return RendererAPI::GetAPI(); }

        static RenderStatistics& GetStats() { return s_Stats; }
        static void ResetStats() { s_Stats = {}; }

        // Directory containing engine resources (fonts, icons ...).
        static std::filesystem::path GetResourcesDirectory();

    private:
        static inline RenderStatistics s_Stats;
    };

}
