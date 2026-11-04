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
    class EventBus;
    struct ScriptMessage;

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
        std::string EnvironmentMap;      // .hdr / .png / .jpg equirect panorama (empty = gradient sky)
        float EnvironmentIntensity = 1.0f;
        float EnvironmentRotation = 0.0f; // radians
        bool FogEnabled = false;
        glm::vec3 FogColor{ 0.7f, 0.78f, 0.86f };
        float FogDensity = 0.015f;
        float FogHeightFalloff = 0.05f;

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
        // Rewrites EntityRef script fields of `entities` using an old->new UUID map
        // (used when a subtree is duplicated / pasted / instantiated with fresh IDs, so
        // references inside the copied subtree point to the copies).
        static void RemapEntityReferences(const std::vector<Entity>& entities, const std::unordered_map<uint64_t, uint64_t>& oldToNew);

        void OnRuntimeStart();
        void OnRuntimeStop();
        void OnSimulationStart();
        void OnSimulationStop();

        void OnUpdateRuntime(Timestep ts);
        void OnUpdateSimulation(Timestep ts, const EditorCamera& camera, const SceneRenderOptions& options = {});
        void OnUpdateEditor(Timestep ts, const EditorCamera& camera, const SceneRenderOptions& options = {});
        void OnViewportResize(uint32_t width, uint32_t height);

        // First entity with this name, in scene order (the order of GetEntitiesInOrder()).
        // Cached: a hash lookup instead of a scan over all entities. Creating / destroying
        // entities and Entity::SetName update the cache at once; a direct write to
        // TagComponent::Tag is picked up on the next frame (or after InvalidateNameCache()).
        Entity FindEntityByName(std::string_view name);
        // Every entity with this name, in scene order.
        std::vector<Entity> FindEntitiesByName(std::string_view name);
        void InvalidateNameCache() { m_NameCacheDirty = true; }
        struct NameCacheStats
        {
            uint32_t Rebuilds = 0;     // full rebuilds of the name -> entity map
            uint32_t Validations = 0;  // once-per-frame consistency checks
        };
        const NameCacheStats& GetNameCacheStats() const { return m_NameCacheStats; }

        Entity GetEntityByUUID(UUID uuid);
        // Entities in creation order (stable, used by UI & serialization).
        std::vector<Entity> GetEntitiesInOrder();

        // ---- gameplay tags (TagsComponent), results in scene order
        std::vector<Entity> FindEntitiesWithTag(std::string_view tag);
        Entity FindEntityWithTag(std::string_view tag);
        size_t CountEntitiesWithTag(std::string_view tag);
        // Every tag used in this scene (sorted, unique).
        std::vector<std::string> GetAllTags();

        // ---- hierarchy
        // Make `child` a child of `parent` (or a root when parent is invalid).
        // keepWorldTransform: the child keeps its current world position/rotation/scale.
        bool SetParent(Entity child, Entity parent, bool keepWorldTransform = true);
        Entity GetParent(Entity entity);
        // Direct children, in order.
        std::vector<Entity> GetChildren(Entity entity);
        size_t GetChildCount(Entity entity);
        Entity GetChild(Entity entity, size_t index);
        // Child with this name (recursive: searches the whole subtree depth-first).
        Entity FindChild(Entity parent, std::string_view name, bool recursive = true);
        // All descendants, depth-first pre-order (a parent comes before its children).
        std::vector<Entity> GetDescendants(Entity entity);
        // Top-most ancestor (the entity itself when it has no parent).
        Entity GetRoot(Entity entity);
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
        // Projection used by the most recent render (post-processing needs it for SSAO).
        const glm::mat4& GetLastProjection() const { return m_LastProjection; }
        // Unprojects a viewport pixel through the primary camera onto the plane z = depthZ.
        glm::vec3 ScreenToWorld(const glm::vec2& screen, float depthZ = 0.0f);

        // ---- particles
        void EmitParticles(Entity entity, int count);

        // ---- script messages (delivered immediately to live script instances:
        // handlers added with ScriptableEntity::AddMessageHandler, then OnMessage)
        // Returns true when `target` has a running script.
        bool SendMessage(Entity target, const ScriptMessage& message);
        // `root` (optional) and its descendants, depth-first. Returns the number of scripts reached.
        int SendMessageToChildren(Entity root, const ScriptMessage& message, bool includeSelf = true);
        // `entity` (optional) and its ancestors, nearest first.
        int SendMessageUpwards(Entity entity, const ScriptMessage& message, bool includeSelf = true);
        // Every running script in the scene (scene order), the sender included.
        int Broadcast(const ScriptMessage& message);
        static constexpr int MaxMessageDepth = 32;

        // ---- publish / subscribe events shared by all scripts (cleared when play mode stops)
        EventBus& GetEventBus() { return *m_EventBus; }

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
        void CreateCharacter3D(Entity entity);
        void StepCharacters(float dt);
        void UpdateAudio();
        void UpdateAnimators(Timestep ts, bool editor);
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

        void OnNameChanged(entt::registry&, entt::entity) { m_NameCacheDirty = true; }
        void RebuildNameCache();
        bool IsNameCacheValid();
        const std::vector<entt::entity>* LookupName(std::string_view name);
        void SortInSceneOrder(std::vector<Entity>& entities);
        bool DeliverMessage(Entity target, const ScriptMessage& message);
        void DestroyScriptInstance(Entity entity);
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
        glm::mat4 m_LastProjection{ 1.0f };
        Ref<class EnvironmentMap> m_EnvMap;
        std::string m_EnvMapPath;
        bool m_MouseDown = false, m_MouseWasDown = false;
        entt::entity m_PressedButton = entt::null;

        std::unordered_map<UUID, entt::entity> m_EntityMap;
        std::vector<UUID> m_DestroyQueue;

        // FindEntityByName cache: name -> entities in scene order.
        struct StringHash
        {
            using is_transparent = void;
            size_t operator()(std::string_view s) const { return std::hash<std::string_view>{}(s); }
        };
        std::unordered_map<std::string, std::vector<entt::entity>, StringHash, std::equal_to<>> m_NameCache;
        bool m_NameCacheDirty = true;
        uint64_t m_FrameIndex = 0;
        uint64_t m_NameCacheFrame = ~0ull; // frame of the last validation / rebuild
        NameCacheStats m_NameCacheStats;

        std::unique_ptr<EventBus> m_EventBus;
        int m_MessageDepth = 0;
        bool m_MessageDepthWarned = false;

        friend class Entity;
        friend class SceneSerializer;
        friend class SceneHierarchyPanel;
        friend class ContactListener2D;
    };

}
