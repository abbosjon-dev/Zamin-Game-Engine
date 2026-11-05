#pragma once

#include "Zamin/Core/Timestep.h"
#include "Zamin/Core/UUID.h"
#include "Zamin/Renderer/EditorCamera.h"
#include "Zamin/Physics/PhysicsTypes.h"
#include "Zamin/Renderer/PostProcess.h"
#include "Zamin/Renderer/MaterialAsset.h"
#include "Zamin/Renderer/Renderer3D.h"
#include "Zamin/Renderer/SpriteSheet.h"

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
    class AssetRegistry;
    struct TilemapComponent;
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
        // The gradient's blend and the sun disc (scenes without an HDRI).
        SkySettings Sky;
        float AmbientIntensity = 0.6f;
        float Exposure = 1.0f;
        float ShadowDistance = 40.0f;
        // Sun shadow quality: atlas size, filter (PCF), cascades, depth / normal bias.
        ShadowSettings Shadows;
        std::string EnvironmentMap;      // .hdr / .png / .jpg equirect panorama (empty = gradient sky)
        float EnvironmentIntensity = 1.0f;
        float EnvironmentRotation = 0.0f; // radians
        bool FogEnabled = false;
        glm::vec3 FogColor{ 0.7f, 0.78f, 0.86f };
        float FogDensity = 0.015f;
        float FogHeightFalloff = 0.05f;

        // Render scale: the scene picture is drawn at this many times the view's size and scaled
        // to it (Renderer::MinRenderScale..MaxRenderScale; below 1 faster and softer, above 1
        // supersampled). The editor viewport and the player both use it.
        float RenderScale = 1.0f;

        // Post-processing
        PostProcessSettings Post;

        // Physics
        glm::vec2 Gravity{ 0.0f, -9.8f };
        glm::vec3 Gravity3D{ 0.0f, -9.81f, 0.0f };
        int VelocityIterations = 6;
        int PositionIterations = 2;
    };

    // Tilemaps are drawn (and frustum culled) in square chunks of this many tiles per side.
    constexpr int TilemapChunkSize = 16;
    // A block of tiles: columns [X0, X1), rows [Y0, Y1).
    struct TilemapChunk
    {
        int X0 = 0, Y0 = 0, X1 = 0, Y1 = 0;
    };
    // The chunks of `tilemap` (drawn with the world transform `world`) whose world bounds the
    // frustum can see, row by row from the bottom (every chunk without a frustum). `culled`: add
    // the number of the others. No GPU needed.
    std::vector<TilemapChunk> GetVisibleTilemapChunks(const TilemapComponent& tilemap, const glm::mat4& world, const Frustum* frustum,
                                                      uint32_t* culled = nullptr);

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
        // Editor: a material dragged over a mesh is shown on it before it is dropped (nothing in
        // the scene changes). MaterialPreviewSubmesh < 0: every part, else only that part.
        entt::entity MaterialPreviewEntity = entt::null;
        Ref<MaterialAsset> MaterialPreview;
        int MaterialPreviewSubmesh = -1;
        // Editor view modes (the viewport toolbar): debug views of the 3D surfaces, wireframe.
        RenderDebugView DebugView = RenderDebugView::Lit;
        WireframeMode Wireframe = WireframeMode::Off;
        // Parallax layers (ParallaxComponent) move with the rendering camera. The game camera
        // always does this (OnUpdateRuntime); the editor's scene view only as a preview.
        bool Parallax = false;
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

        // ---- active state (TagComponent::Active): game state, unlike the editor-only Hidden flag
        // An entity is active in the hierarchy when it and all of its ancestors are active. An
        // inactive one is not drawn (meshes, sprites, circles, text, tilemaps, particles, UI,
        // lights), has no physics body / character (removed, and created again at its current
        // transform when it comes back), and its script gets OnDisable, then no OnUpdate /
        // OnLateUpdate / timers until it is active again (OnEnable). Besides that, while inactive:
        // - particle systems stop: live particles are cleared and nothing is emitted (also by
        //   EmitParticles); back on, the system starts over (its Burst fires again);
        // - audio sources stop (the sound is released); back on, a PlayOnStart source plays again
        //   from the beginning, and PlayAudio refuses an inactive entity;
        // - animators (skeletal and sprite) are not updated: time, pose and state machine stay
        //   where they were and continue from there;
        // - UI buttons cannot be hovered, pressed or clicked (a press in progress is cancelled);
        // - cameras do not count: GetPrimaryCameraEntity skips them (also for the audio listener
        //   and ScreenToWorld);
        // - lights do not count: no light, no shadow, no sun / point / spot light slot (CollectLights);
        // - a tilemap has no collision body, a character controller is not in the 3D world (no
        //   gravity, Grounded false, Jump refused); bodies resting on a removed body wake up and fall.
        // The flag is saved with the scene and kept by Scene::Copy, duplicates, Instantiate() and
        // prefabs.
        // Sets the entity's own flag; while playing / simulating, physics, scripts, sounds, particles
        // and UI buttons follow at once (also while paused): the OnDisable / OnEnable calls of the
        // whole subtree have run when it returns. Animators, cameras and drawing look at the flag
        // every frame. Scripts use ScriptableEntity::SetActive, the editor the inspector's checkbox.
        void SetActive(Entity entity, bool active);
        // The entity's own flag (its ancestors may still switch it off).
        bool IsActive(Entity entity);
        // The entity and every ancestor are active. Cached (a hash lookup): SetActive, SetParent,
        // duplicating / instantiating and loading keep it current; a direct write to
        // TagComponent::Active is picked up on the next frame (or after InvalidateActiveCache()).
        bool IsActiveInHierarchy(Entity entity);
        void InvalidateActiveCache() { m_ActiveCacheDirty = true; }
        // Entities that are not active in the hierarchy (own flag off, or below such an entity).
        size_t CountInactiveInHierarchy();
        // Everything RenderScene leaves out with these options: entities that are not active in
        // the hierarchy always; the editor-hidden / editor-locked subtrees when the options say so.
        std::unordered_set<entt::entity> CollectRenderSkipped(const SceneRenderOptions& options = {});

        // ---- lights (what the 3D renderer lights the scene with)
        // Only lights on entities that are active in the hierarchy count (with the options' editor
        // flags, the subtrees RenderScene skips are left out as well): an inactive light does not
        // light anything, casts no shadow, and takes neither the sun's place nor one of the
        // Renderer3D::MaxLocalLights slots - the next active light gets it.
        struct LightSet
        {
            // The sun: the main directional light - in scene order (as the hierarchy / scene file
            // list them) the first one that counts with CastShadows on, else the first one that
            // counts; null: no directional light. It is the only one that casts shadows, and the
            // sky's sun disc follows it.
            entt::entity Sun = entt::null;
            // Every directional light that lights the scene, in scene order, the sun among them: at
            // most Renderer3D::MaxDirectionalLights (the sun and the first others in scene order).
            std::vector<entt::entity> DirectionalLights;
            // Point and spot lights that light the scene, at most Renderer3D::MaxLocalLights
            // together: each in pool order when they all fit, else the most important ones to the
            // view position (Renderer3D::SelectLights: strength weighted by how near the viewer is
            // to the lit range), the most important first.
            std::vector<entt::entity> PointLights;
            std::vector<entt::entity> SpotLights;
            uint32_t InactiveLights = 0;     // directional, point and spot lights left out (not active in the hierarchy)
            uint32_t DroppedDirectionalLights = 0; // directional lights that count beyond the limit (the last ones in scene order)
            uint32_t DroppedLocalLights = 0; // point + spot lights that count beyond the limit (the least important ones)

            size_t GetLocalLightCount() const { return PointLights.size() + SpotLights.size(); }
        };
        LightSet CollectLights(const glm::vec3& viewPosition, const SceneRenderOptions& options = {});
        // The main directional light (LightSet::Sun: the first active one in scene order that casts
        // shadows, else the first active one), or an invalid entity.
        Entity GetSunEntity();

        glm::mat4 GetWorldTransform(Entity entity);
        // Sets local transform so that the entity ends up at `world`.
        void SetWorldTransform(Entity entity, const glm::mat4& world);
        glm::vec3 GetWorldPosition(Entity entity);

        // ---- bounds (editor framing: F = selection, Shift+F = whole scene)
        // World-space box around what the entity draws: mesh / model, sprite, circle, text,
        // tilemap. Without any of these it is just the entity's position. False for screen-space
        // UI entities (they are not in the world).
        bool GetEntityBounds(Entity entity, AABB& out);
        // The box around what the entity draws in its own space (before its world transform; a
        // model's bounds in its bind pose). False when it draws nothing of the above or is UI.
        bool GetEntityLocalBounds(Entity entity, AABB& out);
        // Union over `entities` (with their descendants when includeChildren). False when none counted.
        bool ComputeBounds(const std::vector<Entity>& entities, AABB& out, bool includeChildren = true);
        // The whole scene as the editor's scene view shows it (Shift+F): entities hidden in the
        // editor or inactive (and their subtrees) are left out, and so are the ones that draw nothing
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
        // Projection and view matrix used by the most recent render (post-processing needs the
        // projection for SSAO, debug drawing both).
        const glm::mat4& GetLastProjection() const { return m_LastProjection; }
        const glm::mat4& GetLastView() const { return m_LastView; }
        // Unprojects a viewport pixel through the primary camera onto the plane z = depthZ.
        glm::vec3 ScreenToWorld(const glm::vec2& screen, float depthZ = 0.0f);

        // ---- particles (nothing is emitted for an entity that is not active in the hierarchy)
        void EmitParticles(Entity entity, int count);

        // ---- audio sources (AudioSourceComponent; only while the game runs)
        // Starts the source's sound from the beginning (a sound it is already playing is
        // stopped first). False when the scene is not running, the entity is not active in the
        // hierarchy, it has no source / path, or the sound could not be started (no device).
        bool PlayAudio(Entity entity);
        void StopAudio(Entity entity);
        bool IsAudioPlaying(Entity entity);

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
        // The camera the game is seen through: a CameraComponent with Primary set on an entity
        // that is active in the hierarchy (with several, the first one in the camera pool).
        // includeInactive: inactive entities count too (the editor uses it to tell "no primary
        // camera" from "the primary camera is switched off").
        Entity GetPrimaryCameraEntity(bool includeInactive = false);
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
        // Copies `prefab` with its subtree (fresh IDs) next to it; while running, the copies get
        // their bodies, sounds and scripts at once (OnEntitiesSpawned). Every copy keeps the
        // active flag of the entity it was copied from: a switched-off template gives switched-off
        // clones (SetActive(true) brings them in, also right after this call). A template that is
        // switched off only by an ancestor (kept in an inactive "templates" group) gives a root
        // copy - where the template is in the world - so the clone is as active as the template
        // itself is.
        Entity Instantiate(Entity prefab);
        // The same with the copy's position set before its bodies are created: local to the
        // template's parent, or the world position when the copy is a root (which keeps the
        // world rotation and scale the template has in its group).
        Entity Instantiate(Entity prefab, const glm::vec3& position);
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
        // The Create* functions do nothing for an entity that is not active in the hierarchy.
        void CreatePhysicsBody(Entity entity);
        void CreatePhysicsBody3D(Entity entity);
        void CreateTilemapBody(Entity entity);
        void CreateCharacter3D(Entity entity);
        // Every body / character the entity should have in the running worlds (none when inactive);
        // sleeping bodies where a new body appears are woken (switched on again / spawned).
        void CreatePhysicsBodies(Entity entity);
        // Removes the entity's 2D / tilemap / 3D bodies and its character from the worlds (bodies
        // resting on them wake up).
        void DestroyPhysicsBodies(Entity entity);
        // ---- active state cache (IsActiveInHierarchy)
        void EnsureActiveCache();
        void RebuildActiveCache();
        // Runtime / simulation: physics and scripts follow the entities whose active-in-hierarchy
        // state changed since the last call (no-op in edit mode). Switched off: contacts end,
        // OnDisable, bodies removed; switched on: bodies created, OnEnable - each step for the
        // whole batch, parent first. Idempotent per entity, so callbacks may nest it.
        void ApplyActiveChanges();
        // What else follows the active state while the game runs / simulates: an entity that
        // left the game stops its sound, clears its particles and drops its UI button state;
        // one that (re)joined it starts its PlayOnStart sound.
        void StopEntityEffects(Entity entity);
        void StartEntityEffects(Entity entity);
        // NativeScriptComponent::Enabled and active in the hierarchy.
        bool ShouldScriptBeEnabled(Entity entity, const struct NativeScriptComponent& nsc);
        void StepCharacters(float dt);
        void UpdateAudio();
        void UpdateAnimators(Timestep ts, bool editor);
        void DispatchContactEvents(const std::vector<ContactEvent>& events);
        Entity DuplicateEntityRecursive(Entity entity, UUID newParent);
        Entity InstantiateCopy(Entity prefab, const glm::vec3* position);
        void StepPhysics(Timestep ts);
        // DebugDraw: the 2D / 3D contact points with their normals (ShowContacts).
        void DrawPhysicsContacts();
        // DebugDraw: the velocities of the characters and dynamic bodies (ShowVelocities / ShowBodyVelocities).
        void DrawPhysicsVelocities();
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
        // CollectLights with the set of entities to leave out given (RenderScene has it already).
        LightSet GatherLights(const glm::vec3& viewPosition, const std::unordered_set<entt::entity>& skipped);
        // Entities by their scene order (the order of the ID pool, as the hierarchy lists them).
        void SortInSceneOrder(std::vector<entt::entity>& entities);
        // The main directional light of these (in scene order): the first that casts shadows, else the first.
        entt::entity PickSun(const std::vector<entt::entity>& directionalLights);

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
        // Looks up the mesh / model, the material assets and the inline material's textures.
        static void ResolveMeshRenderer(struct MeshRendererComponent& mr);
        // Only the material assets (MaterialPath, SubmeshMaterials) and inline textures: no GPU mesh.
        static void ResolveMeshMaterials(struct MeshRendererComponent& mr);
        // What a mesh renderer draws with. A primitive (modelMaterial = null): its material asset,
        // or the inline material. Submesh `submesh` of a model: the slot's material asset, then the
        // component's material asset, else the model's own material tinted / overridden by the
        // inline material (albedo multiplied, inline maps replace the model's, emission added; an
        // inline Unlit shader / Cutout or Transparent blend mode replaces the model's, an inline
        // double-sided flag makes every part double-sided).
        static Material ComposeMeshMaterial(const struct MeshRendererComponent& mr, const Material* modelMaterial, size_t submesh = 0);
        // The material asset submesh `submesh` (or the primitive) is drawn with (null = none). The
        // shared one: an instance a script made (GetMaterialInstance) is not counted.
        static Ref<MaterialAsset> GetMeshMaterialAsset(const struct MeshRendererComponent& mr, size_t submesh = 0);
        // The inline material as material asset data: its values and texture paths.
        static MaterialData GetInlineMaterialData(const struct MeshRendererComponent& mr);
        // Sets the material asset (path relative to Assets, "" = the inline material) of the whole
        // renderer (submesh < 0) or of one part of a model (its slot in SubmeshMaterials; "" clears
        // the slot), looks it up and drops the renderer's material instances. False when the path
        // names a file that is not a .zmat.
        static bool SetMeshMaterial(struct MeshRendererComponent& mr, const std::string& assetPath, int submesh = -1);

        // ---- material instances (runtime, MeshRendererComponent::MaterialInstance)
        // The renderer's own copy of its main material, made on the first call - of the material
        // asset, or of the inline material without one (a model's inline material is the tint of
        // all its parts) - and the same object after that. Edits of it change only this renderer.
        static Ref<MaterialAsset> GetMaterialInstance(struct MeshRendererComponent& mr);
        // Part `submesh` of a model: a copy of the whole material that part is drawn with now
        // (primitives: the main instance). nullptr for a part the loaded model does not have.
        static Ref<MaterialAsset> GetMaterialInstance(struct MeshRendererComponent& mr, size_t submesh);
        static bool HasMaterialInstances(const struct MeshRendererComponent& mr);
        // Back to the shared materials.
        static void ClearMaterialInstances(struct MeshRendererComponent& mr);

        // ---- sprites (2D)
        // The part of its texture a sprite shows now: the animation's frame (a grid cell, or a
        // named frame of the texture's sprite sheet), else the renderer's SpriteName frame, else
        // the whole texture. Frames come from `registry` (the texture's .zmeta); without one, or
        // for a name the sheet does not have, the whole texture.
        static SpriteRegion GetSpriteRegion(const struct SpriteRendererComponent& sprite, const struct SpriteAnimationComponent* animation,
                                            const AssetRegistry* registry);
        // How it is laid out: draw mode, size, pivot, 9-slice borders (the renderer's, else the
        // frame's). `uiSize`: a screen-space UI sprite that many px big (1 px = 1 unit).
        static SpriteGeometry GetSpriteGeometry(const struct SpriteRendererComponent& sprite, const SpriteRegion& region,
                                                const glm::vec2* uiSize = nullptr);
        // Draws it into the open Renderer2D batch (FlipX / FlipY are part of `transform`).
        static void DrawSprite(const glm::mat4& transform, const struct SpriteRendererComponent& sprite,
                               const struct SpriteAnimationComponent* animation, const glm::vec4& color, int entityID,
                               const AssetRegistry* registry, const glm::vec2* uiSize = nullptr);
        // The rectangle a sprite covers in its own space as a transform of the unit quad (identity
        // for a Simple sprite of a whole texture or a centred frame).
        static glm::mat4 GetSpriteRectTransform(const struct SpriteRendererComponent& sprite, const struct SpriteAnimationComponent* animation,
                                                const AssetRegistry* registry);
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
        glm::mat4 m_LastView{ 1.0f };
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

        // Entities not active in the hierarchy: as a set and depth-first (a parent before its
        // children, subtrees in scene order). Rebuilt when dirty and once per frame.
        std::unordered_set<entt::entity> m_InactiveInHierarchy;
        std::vector<entt::entity> m_InactiveOrder;
        bool m_ActiveCacheDirty = true;
        uint64_t m_ActiveCacheFrame = ~0ull;
        // The inactive entities the running physics / scripts were last brought in line with.
        std::vector<entt::entity> m_AppliedInactive;

        // Begin/end pairs that were dispatched to scripts: (lower id, higher id, sensor) ->
        // number of physics contacts. Scripts hear one Begin/End per entity pair even when
        // several fixtures / shapes (or a character and its inner body) touch.
        std::map<std::tuple<uint64_t, uint64_t, bool>, int> m_ActiveContacts;
        // While a batch of contact events is dispatched: entities whose bodies a callback
        // destroyed (switched off, maybe on again). The rest of the batch is about the old bodies.
        int m_ContactDispatchDepth = 0;
        std::unordered_set<uint64_t> m_ContactBodiesReplaced;
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
