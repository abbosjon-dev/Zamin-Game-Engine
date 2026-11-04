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

#include <map>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class b2World;

namespace Zamin {

    class Entity;
    class ContactListener2D;
    class PhysicsWorld3D;
    class EventBus;
    class ScriptableEntity;
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
        GridSettings Grid;       // cell size, colors, axes (editor viewport preferences)
        // Editor scene view: entities hidden with the hierarchy's eye button (and their children)
        // are not drawn and their lights do not count. Runtime rendering never sets this.
        bool SkipEditorHidden = false;
        // Editor picking pass (a click on a locked entity goes through it): locked entities and
        // their subtrees are not drawn either, so the entity ID buffer shows what is behind them.
        bool SkipEditorLocked = false;
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
        // Draws the scene from the editor camera without updating anything (an extra pass in
        // the same frame, e.g. the editor's click-through picking).
        void OnRenderEditor(const EditorCamera& camera, const SceneRenderOptions& options = {});
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

        // ---- hierarchy order (what the editor's hierarchy shows; saved with the scene)
        // Entities without a parent, in scene order.
        std::vector<Entity> GetRootEntities();
        // Every entity, depth-first: roots in scene order, children in their sibling order.
        std::vector<Entity> GetEntitiesInHierarchyOrder();
        // Position among the siblings (the parent's children, or the root entities); -1 if invalid.
        int GetSiblingIndex(Entity entity);
        // Moves the entity among its siblings so that it ends up at `index` (negative or past
        // the end = last).
        bool SetSiblingIndex(Entity entity, int index);
        // Gives `entity` the parent of `sibling` and places it right before / after `sibling`.
        // Fails (nothing changes) when that would make the entity its own ancestor.
        bool MoveEntityNextTo(Entity entity, Entity sibling, bool after, bool keepWorldTransform = true);

        // ---- several entities at once (editor multi-selection)
        // The given entities without duplicates and without those inside another listed
        // entity's subtree, in hierarchy order.
        std::vector<Entity> GetTopLevelEntities(const std::vector<Entity>& entities);
        // Duplicates every top-level subtree once; each copy is placed right after its original.
        // EntityRef fields pointing anywhere inside the duplicated set point to the copies.
        // Returns the copied top-level entities (in hierarchy order of the originals).
        std::vector<Entity> DuplicateEntities(const std::vector<Entity>& entities);
        // Destroys the entities with their descendants. Returns how many top-level ones were destroyed.
        size_t DestroyEntities(const std::vector<Entity>& entities);
        // ---- editor-only flags (EditorMetadataComponent: the hierarchy's eye / lock buttons)
        // inherited: also true when an ancestor has the flag (a hidden / locked parent hides /
        // locks its whole subtree); false: only the entity's own flag.
        bool IsHiddenInEditor(Entity entity, bool inherited = true);
        bool IsLockedInEditor(Entity entity, bool inherited = true);
        // Set the entity's own flag (the component is removed again when nothing is left in it).
        void SetHiddenInEditor(Entity entity, bool hidden);
        void SetLockedInEditor(Entity entity, bool locked);
        // Entities with their own flag set.
        size_t CountHiddenInEditor();
        size_t CountLockedInEditor();
        // Hidden entities and every descendant of one (what the editor's scene view skips).
        std::unordered_set<entt::entity> CollectHiddenInEditor();
        // Locked entities and every descendant of one (what viewport clicks go through).
        std::unordered_set<entt::entity> CollectLockedInEditor();

        glm::mat4 GetWorldTransform(Entity entity);
        // Sets local transform so that the entity ends up at `world`.
        void SetWorldTransform(Entity entity, const glm::mat4& world);
        glm::vec3 GetWorldPosition(Entity entity);

        // ---- bounds (editor framing: F = selection, Shift+F = whole scene)
        // World-space box around what the entity draws: mesh / model, sprite, circle, text,
        // tilemap. Without any of these it is just the entity's position. False for screen-space
        // UI entities (they are not in the world).
        bool GetEntityBounds(Entity entity, AABB& out);
        // Union over `entities` (with their descendants when includeChildren). False when none counted.
        bool ComputeBounds(const std::vector<Entity>& entities, AABB& out, bool includeChildren = true);
        // The whole scene as the editor's scene view shows it (Shift+F): entities hidden in the
        // editor (and their subtrees) are left out, and so are the ones that draw nothing
        // (lights, cameras, empty groups) unless nothing draws at all - then their positions
        // are framed. False when there is nothing to frame.
        bool ComputeSceneBounds(AABB& out);

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

        // ---- script enabled state (NativeScriptComponent::Enabled)
        // Sets the flag; a live instance gets OnEnable / OnDisable immediately.
        void SetScriptEnabled(Entity entity, bool enabled);
        bool IsScriptEnabled(Entity entity);

        // Reload GPU assets (textures, meshes, fonts) referenced by components.
        void ResolveAssets();
        // The same for one entity (e.g. after pasting a component onto it).
        void ResolveEntityAssets(Entity entity);

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
        // Delivers OnEnable / OnDisable until the instance matches NativeScriptComponent::Enabled.
        void SyncScriptEnabled(Entity entity);
        // OnStart for every enabled script that has not started yet (before the frame's OnUpdate).
        void StartPendingScripts();
        // OnCollisionEnd / OnTriggerEnd for everything still touching `entity` (it is being destroyed).
        void EndContactsOf(Entity entity);
        void DeliverContact(Entity self, Entity other, bool begin, bool sensor);
        void FlushDestroyQueue();
        void UpdateParticles(Timestep ts, bool editor);
        void UpdateUI();

        void RenderUI(const std::unordered_set<entt::entity>* skip);

        void OnNameChanged(entt::registry&, entt::entity) { m_NameCacheDirty = true; }
        void RebuildNameCache();
        bool IsNameCacheValid();
        const std::vector<entt::entity>* LookupName(std::string_view name);
        void SortInSceneOrder(std::vector<Entity>& entities);
        // Moves the entity in the scene order (IDComponent pool) to just before `before`
        // (the end when invalid); every other entity keeps its relative order.
        void MoveInSceneOrder(Entity entity, Entity before);
        // DestroyEntity's work for one entity and its subtree (children first).
        void DestroyEntityRecursive(Entity entity);
        // The entities DestroyEntityRecursive(entity) removes, in the order it removes them.
        void CollectDestroyOrder(Entity entity, std::vector<entt::entity>& order, std::unordered_set<entt::entity>& visited);
        // Moves the entities (in the order they will be destroyed) to the end of the scene
        // order without changing the order of the others: then removing them from the pools
        // (swap-and-pop) disturbs nothing.
        void PrepareDestroyOrder(const std::vector<entt::entity>& order);
        bool DeliverMessage(Entity target, const ScriptMessage& message);
        // OnDisable / OnDestroy for the entity's running script (if any), then it is retired.
        void DestroyScriptInstance(Entity entity);
        // on_destroy<NativeScriptComponent>: a component removed while its instance is still
        // live (raw registry access) is detached without callbacks, so it neither leaks nor
        // keeps receiving events.
        void OnScriptComponentDestroyed(entt::registry& registry, entt::entity entity);
        // Destroyed instances are deleted here, outside any script callback: a script may
        // destroy its own entity / component and keep running until its callback returns.
        void DeleteRetiredScripts();
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

        // Begin/end pairs that were dispatched to scripts: (lower id, higher id, sensor) ->
        // number of physics contacts. Scripts hear one Begin/End per entity pair even when
        // several fixtures / shapes (or a character and its inner body) touch.
        std::map<std::tuple<uint64_t, uint64_t, bool>, int> m_ActiveContacts;
        bool m_ScriptsPendingStart = false;

        std::unique_ptr<EventBus> m_EventBus;
        std::vector<ScriptableEntity*> m_RetiredScripts;
        int m_MessageDepth = 0;
        bool m_MessageDepthWarned = false;

        friend class Entity;
        friend class SceneSerializer;
        friend class SceneHierarchyPanel;
        friend class ContactListener2D;
    };

}
