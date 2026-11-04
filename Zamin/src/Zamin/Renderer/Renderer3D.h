#pragma once

#include "Zamin/Renderer/Mesh.h"
#include "Zamin/Renderer/Material.h"

#include <glm/glm.hpp>
#include <vector>

namespace Zamin {

    struct DirectionalLight
    {
        glm::vec3 Direction{ -0.4f, -1.0f, -0.3f }; // direction the light travels
        glm::vec3 Color{ 1.0f };
        float Intensity = 3.0f;
        bool CastShadows = true;
    };

    struct PointLight
    {
        glm::vec3 Position{ 0.0f };
        glm::vec3 Color{ 1.0f };
        float Intensity = 5.0f;
        float Radius = 10.0f;
    };

    struct SceneEnvironment
    {
        bool DrawSky = true;
        glm::vec3 SkyTopColor{ 0.32f, 0.52f, 0.85f };
        glm::vec3 SkyHorizonColor{ 0.78f, 0.86f, 0.95f };
        glm::vec3 GroundColor{ 0.36f, 0.33f, 0.30f };
        float AmbientIntensity = 0.6f;
        float Exposure = 1.0f;

        bool HasSun = false;
        DirectionalLight Sun;
        std::vector<PointLight> PointLights;
        float ShadowDistance = 40.0f;
    };

    // Forward PBR mesh renderer with directional shadow map and gradient sky.
    class Renderer3D
    {
    public:
        static constexpr uint32_t MaxPointLights = 16;

        static void Init();
        static void Shutdown();

        static void BeginScene(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& cameraPosition,
                               const SceneEnvironment& environment);
        static void SubmitMesh(const Ref<Mesh>& mesh, const glm::mat4& transform, const Material& material,
                               int entityID = -1, bool castShadows = true);
        static void EndScene();

        // Editor-only infinite grid. plane: 0 = XZ (3D), 1 = XY (2D)
        static void DrawGrid(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& cameraPosition, int plane = 0);

        static uint32_t GetShadowMapRendererID();
    };

}
