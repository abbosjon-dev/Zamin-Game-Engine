#pragma once

#include "Zamin/Core/Timestep.h"
#include "Zamin/Core/UUID.h"
#include "Zamin/Renderer/EditorCamera.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <string_view>
#include <unordered_map>
#include <vector>

class b2World;

namespace Zamin {

    class Entity;
    class ContactListener2D;

    struct SceneSettings
    {
        // Environment / lighting
        bool DrawSky = true;
        glm::vec3 SkyTopColor{ 0.32f, 0.52f, 0.85f };
        glm::vec3 SkyHorizonColor{ 0.78f, 0.86f, 0.95f };
        glm::vec3 GroundColor{ 0.36f, 0.33f, 0.30f };
        float AmbientIntensity = 0.6f;
        float Exposure = 1.0f;
        float ShadowDistance = 40.0f;

        // Physics
        glm::vec2 Gravity{ 0.0f, -9.8f };
        int VelocityIterations = 6;
        int PositionIterations = 2;
    };

    struct SceneRenderOptions
    {
        bool ShowGrid = false;
        int GridPlane = 0;       // 0 = XZ, 1 = XY
    };

    class Scene
    {
    public:
        Scene();
        ~Scene();

        static Ref<Scene> Copy(const Ref<Scene>& other);

        Entity CreateEntity(const std::string& name = std::string());
        Entity CreateEntityWithUUID(UUID uuid, const std::string& name = std::string());
        void DestroyEntity(Entity entity);
        Entity DuplicateEntity(Entity entity);

        void OnRuntimeStart();
        void OnRuntimeStop();
        void OnSimulationStart();
        void OnSimulationStop();

        void OnUpdateRuntime(Timestep ts);
        void OnUpdateSimulation(Timestep ts, const EditorCamera& camera, const SceneRenderOptions& options = {});
        void OnUpdateEditor(Timestep ts, const EditorCamera& camera, const SceneRenderOptions& options = {});
        void OnViewportResize(uint32_t width, uint32_t height);

        Entity FindEntityByName(std::string_view name);
        Entity GetEntityByUUID(UUID uuid);
        Entity GetPrimaryCameraEntity();
        // Background color used when the sky is disabled (primary camera's clear color).
        glm::vec4 GetClearColor();

        bool IsRunning() const { return m_IsRunning; }
        bool IsPaused() const { return m_IsPaused; }
        void SetPaused(bool paused) { m_IsPaused = paused; }
        void Step(int frames = 1) { m_StepFrames = frames; }

        SceneSettings& GetSettings() { return m_Settings; }
        const SceneSettings& GetSettings() const { return m_Settings; }

        const std::string& GetName() const { return m_Name; }
        void SetName(const std::string& name) { m_Name = name; }

        float GetTime() const { return m_Time; }

        template<typename... Components>
        auto GetAllEntitiesWith() { return m_Registry.view<Components...>(); }

        entt::registry& GetRegistry() { return m_Registry; }

        // --- used by ScriptableEntity
        Entity Instantiate(Entity prefab);
        void DestroyEntityDeferred(Entity entity);

        // Destroys all live script instances (needed before hot-reloading the script module).
        void DestroyScriptInstances();

        // Reload GPU assets (textures, meshes, fonts) referenced by components.
        void ResolveAssets();

    private:
        template<typename T>
        void OnComponentAdded(Entity entity, T& component);

        void OnPhysics2DStart();
        void OnPhysics2DStop();
        void CreatePhysicsBody(Entity entity);
        void StepPhysics(Timestep ts);
        void StartScripts();
        void StopScripts();
        void CreateScriptInstance(Entity entity);
        void FlushDestroyQueue();

        void RenderScene(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& cameraPosition,
                         const SceneRenderOptions& options);

    private:
        entt::registry m_Registry;
        uint32_t m_ViewportWidth = 0, m_ViewportHeight = 0;
        bool m_IsRunning = false;
        bool m_IsPaused = false;
        bool m_ScriptsRunning = false;
        int m_StepFrames = 0;
        float m_Time = 0.0f;
        float m_PhysicsAccumulator = 0.0f;

        std::string m_Name = "Untitled";
        SceneSettings m_Settings;

        b2World* m_PhysicsWorld = nullptr;
        ContactListener2D* m_ContactListener = nullptr;

        std::unordered_map<UUID, entt::entity> m_EntityMap;
        std::vector<UUID> m_DestroyQueue;

        friend class Entity;
        friend class SceneSerializer;
        friend class SceneHierarchyPanel;
        friend class ContactListener2D;
    };

}
