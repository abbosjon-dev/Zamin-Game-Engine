#pragma once

#include "Zamin/Core/Timestep.h"
#include "Zamin/Core/UUID.h"
#include "Zamin/Renderer/EditorCamera.h"
#include "Zamin/Physics/PhysicsTypes.h"
#include "Zamin/Renderer/PostProcess.h"

#include <filesystem>
#include <memory>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <string_view>
#include <unordered_map>
#include <vector>

class b2World;

namespace Zamin {

    class Entity;
    class ContactListener2D;
    class PhysicsWorld3D;

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

        // Post-processing
        PostProcessSettings Post;

        // Physics
        glm::vec2 Gravity{ 0.0f, -9.8f };
        glm::vec3 Gravity3D{ 0.0f, -9.81f, 0.0f };
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
        // Entities in creation order (stable, used by UI & serialization).
        std::vector<Entity> GetEntitiesInOrder();

        // ---- hierarchy
        // Make `child` a child of `parent` (or a root when parent is invalid).
        // keepWorldTransform: the child keeps its current world position/rotation/scale.
        bool SetParent(Entity child, Entity parent, bool keepWorldTransform = true);
        Entity GetParent(Entity entity);
        bool IsAncestorOf(Entity ancestor, Entity entity);
        glm::mat4 GetWorldTransform(Entity entity);
        // Sets local transform so that the entity ends up at `world`.
        void SetWorldTransform(Entity entity, const glm::mat4& world);
        glm::vec3 GetWorldPosition(Entity entity);

        // ---- prefabs (.zprefab)
        Entity InstantiatePrefab(const std::filesystem::path& path);
        bool SavePrefab(Entity root, const std::filesystem::path& path);

        // ---- physics queries
        RaycastHit Raycast2D(const glm::vec2& origin, const glm::vec2& direction, float maxDistance);
        RaycastHit Raycast3D(const glm::vec3& origin, const glm::vec3& direction, float maxDistance);
        PhysicsWorld3D* GetPhysicsWorld3D() { return m_PhysicsWorld3D.get(); }

        // ---- viewport input (set by the host each frame; pixels, origin top-left of the game view)
        void SetViewportMouse(const glm::vec2& position, bool leftButtonDown) { m_MousePosition = position; m_MouseDown = leftButtonDown; }
        const glm::vec2& GetViewportMouse() const { return m_MousePosition; }
        glm::vec2 GetViewportSize() const { return { (float)m_ViewportWidth, (float)m_ViewportHeight }; }
        // Unprojects a viewport pixel through the primary camera onto the plane z = depthZ.
        glm::vec3 ScreenToWorld(const glm::vec2& screen, float depthZ = 0.0f);

        // ---- particles
        void EmitParticles(Entity entity, int count);

        // ---- scene switching (handled by the host: editor / player)
        void RequestLoadScene(const std::filesystem::path& path) { m_PendingSceneLoad = path; }
        const std::filesystem::path& GetPendingSceneLoad() const { return m_PendingSceneLoad; }
        void ClearPendingSceneLoad() { m_PendingSceneLoad.clear(); }
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
        void OnEntitiesSpawned(const std::vector<Entity>& entities); // creates runtime state
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
        void OnPhysics3DStart();
        void OnPhysics3DStop();
        void CreatePhysicsBody(Entity entity);
        void CreatePhysicsBody3D(Entity entity);
        void CreateTilemapBody(Entity entity);
        void UpdateAudio();
        void DispatchContactEvents(const std::vector<ContactEvent>& events);
        Entity DuplicateEntityRecursive(Entity entity, UUID newParent);
        void StepPhysics(Timestep ts);
        void StartScripts();
        void StopScripts();
        void CreateScriptInstance(Entity entity);
        void FlushDestroyQueue();
        void UpdateParticles(Timestep ts, bool editor);
        void UpdateUI();

        void RenderUI();
    public:
        static void ResolveMeshRenderer(struct MeshRendererComponent& mr);
    private:
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
        std::unique_ptr<PhysicsWorld3D> m_PhysicsWorld3D;
        std::filesystem::path m_PendingSceneLoad;
        glm::vec2 m_MousePosition{ -1.0f };
        bool m_MouseDown = false, m_MouseWasDown = false;
        entt::entity m_PressedButton = entt::null;

        std::unordered_map<UUID, entt::entity> m_EntityMap;
        std::vector<UUID> m_DestroyQueue;

        friend class Entity;
        friend class SceneSerializer;
        friend class SceneHierarchyPanel;
        friend class ContactListener2D;
    };

}
