#pragma once

#include "Zamin/Core/Timestep.h"
#include "Zamin/Core/UUID.h"
#include "Zamin/Renderer/EditorCamera.h"
#include "Zamin/Physics/PhysicsTypes.h"
#include "Zamin/Physics/Polygon2D.h"
#include "Zamin/Renderer/PostProcess.h"
#include "Zamin/Renderer/MaterialAsset.h"
#include "Zamin/Renderer/Renderer3D.h"
#include "Zamin/Renderer/SpriteSheet.h"
#include "Zamin/Renderer/Lighting2D.h"
#include "Zamin/Renderer/Animation.h"
#include "Zamin/Scene/PropertyAnimation.h"
#include "Zamin/Scene/SortingLayers.h"
#include "Zamin/Scene/PixelPerfect.h"
#include "Zamin/UI/UIAnimation.h"
#include "Zamin/UI/UILayout.h"
#include "Zamin/UI/UIWidgets.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <map>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Zamin {

    class Entity;
    class AssetRegistry;
    struct TilemapComponent;
    class PhysicsWorld2D;
    class PhysicsWorld3D;
    struct PhysicsJoint2DRef;
    struct PhysicsJoint3DRef;
    struct ConvexHull3D;
    struct CollisionMesh3D;
    class EventBus;
    class SceneManager;
    class ScriptableEntity;
    struct ScriptMessage;
    struct SoundInfo;
    struct AudioSourceComponent;
    struct AmbientZoneComponent;
    struct AudioFilterSettings;
    struct UITransformComponent;

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

        // 2D lighting: the light Lit sprites and tilemaps get everywhere (linear colour x intensity),
        // the Light 2D lights add to it. White at 1: a Lit renderer without lights looks unlit.
        struct Lighting2DSettings
        {
            glm::vec3 AmbientColor{ 1.0f };
            float AmbientIntensity = 1.0f;
            bool operator==(const Lighting2DSettings&) const = default;
        } Lighting2D;

        // Physics
        glm::vec2 Gravity{ 0.0f, -9.8f };
        glm::vec3 Gravity3D{ 0.0f, -9.81f, 0.0f };

        // Both worlds (2D and 3D) advance in fixed steps of 1 / StepRate seconds: a frame takes as
        // many as its time holds, at most MaxStepsPerFrame - a slower frame drops the rest (the game
        // slows down instead of falling further and further behind).
        struct PhysicsStepSettings
        {
            static constexpr int MinRate = 20, MaxRate = 240, MaxSteps = 20;
            int StepRate = 60;
            int MaxStepsPerFrame = 5;
            float GetFixedStep() const { return 1.0f / (float)StepRate; }
            void Sanitize();
            bool operator==(const PhysicsStepSettings&) const = default;
        } PhysicsStep;

        // 2D physics (Box2D).
        struct Physics2DSettings
        {
            static constexpr int MaxIterations = 100, MaxSubSteps = 16;
            // The solver's passes per step: more - stiffer stacks and joints, more time.
            int VelocityIterations = 6;
            int PositionIterations = 2;
            // Box2D steps per fixed step (each 1 / (StepRate x SubSteps) s): fast bodies, long joint
            // chains and ragdolls hold together better. Effectors push before each of them and joints
            // are checked for breaking after each.
            int SubSteps = 1;
            // Sleeping: a body that keeps still for TimeToSleep seconds together with everything
            // touching it / joined to it is not simulated until something wakes it (Box2D's own time
            // is 0.5 s). Off: every body is simulated all the time (Rigidbody2D's Sleeping mode
            // decides for one body).
            bool AllowSleep = true;
            float TimeToSleep = 0.5f;
            void Sanitize();
            bool operator==(const Physics2DSettings&) const = default;
        } Physics2D;

        // 3D physics (Jolt).
        struct Physics3DSettings
        {
            // Sleeping: a body that keeps still for TimeToSleep seconds together with everything
            // touching it / joined to it is not simulated until something wakes it (Jolt's own time is
            // 0.5 s). Off: every body is simulated all the time (Rigidbody 3D's Sleeping mode decides
            // for one body).
            bool AllowSleep = true;
            float TimeToSleep = 0.5f;
            void Sanitize();
            bool operator==(const Physics3DSettings&) const = default;
        } Physics3D;
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
        // Editor tile painting: layer TilePreviewLayer of the tilemap TilePreviewEntity is drawn with
        // these tiles instead of its own (what the tool would leave; nothing in the scene changes).
        entt::entity TilePreviewEntity = entt::null;
        int TilePreviewLayer = 0;
        const std::vector<int>* TilePreviewTiles = nullptr;
        // Pixel-perfect camera (SnapSprites): sprites and tilemaps are drawn at the nearest multiple
        // of this many world units (0: where they are).
        float PixelSnap = 0.0f;
        // Screen-space UI is left out of the scene picture: the host draws it over the finished one
        // (Scene::RenderDeferredUI) - a pixel-perfect camera's low-resolution picture gets crisp UI.
        bool DeferUI = false;
        // Drawn over another scene's picture through that scene's camera (SceneManager: additively
        // loaded scenes): no sky - only what this scene has is added to the picture.
        bool Overlay = false;
    };

    // What a scene says about itself ("Metadata" in its file): shown by the editor (the inspector of
    // the scene file, the Scenes panel, the content browser's tooltip) and readable by a game (a level
    // select: SceneSerializer::ReadSceneInfo). The thumbnail is a picture next to the scene file
    // (GetThumbnailPath), drawn by the editor whenever the scene is saved.
    struct SceneMetadata
    {
        std::string Description;
        std::string Author;
        std::vector<std::string> Tags;   // free words ("daraja", "menyu"); unique, none empty

        bool IsEmpty() const { return Description.empty() && Author.empty() && Tags.empty(); }
        bool HasTag(std::string_view tag) const { return std::find(Tags.begin(), Tags.end(), tag) != Tags.end(); }
        // Adds a tag (trimmed; false when empty or already there).
        bool AddTag(std::string_view tag);
        bool RemoveTag(std::string_view tag);
        bool operator==(const SceneMetadata&) const = default;

        // The scene's thumbnail: ".Level1.zscene.png" next to "Level1.zscene" - a dot file, so it is
        // no asset of its own (no .zmeta, not listed in the content browser, which shows it as the
        // scene's picture); it moves, is copied and deleted with its scene.
        static std::filesystem::path GetThumbnailPath(const std::filesystem::path& scenePath);
        static bool IsThumbnailFile(const std::filesystem::path& path);
        // The size thumbnails are drawn at (16:9).
        static constexpr uint32_t ThumbnailWidth = 320, ThumbnailHeight = 180;
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
        // Rewrites EntityRef script fields (and Camera2DFollow targets) of `entities` using an old->new UUID map
        // (used when a subtree is duplicated / pasted / instantiated with fresh IDs, so
        // references inside the copied subtree point to the copies).
        static void RemapEntityReferences(const std::vector<Entity>& entities, const std::unordered_map<uint64_t, uint64_t>& oldToNew);

        void OnRuntimeStart();
        // How long the last OnRuntimeStart took in its heavy parts (the scene load profiler's
        // Physics / Scripts stages): making the 2D and 3D physics worlds with their bodies, and the
        // scripts' OnCreate / OnEnable.
        struct RuntimeStartTiming
        {
            double PhysicsMilliseconds = 0.0;
            double ScriptsMilliseconds = 0.0;
            double TotalMilliseconds = 0.0;
        };
        const RuntimeStartTiming& GetRuntimeStartTiming() const { return m_RuntimeStartTiming; }
        // Stops the scripts, physics and the scene's sounds; the music (AudioEngine::PlayMusic)
        // goes on, it belongs to the game (the editor stops it when play mode ends).
        void OnRuntimeStop();
        void OnSimulationStart();
        void OnSimulationStop();

        void OnUpdateRuntime(Timestep ts);
        // OnUpdateRuntime in its two halves: the game's frame (scripts, physics, animation, audio)
        // and drawing it through the primary camera. SceneManager runs several scenes' frames, then
        // draws them all through one camera.
        void UpdateRuntime(Timestep ts);
        void RenderRuntime();
        // The view the primary camera draws the game with this frame (false without one).
        struct RuntimeView
        {
            glm::mat4 View{ 1.0f };
            glm::mat4 Projection{ 1.0f };       // drawn with (pixel perfect: the whole render target)
            glm::mat4 ViewProjection{ 1.0f };   // what GetLastProjection reports (pixel perfect: the view's part)
            glm::vec3 Position{ 0.0f };
            float PixelSnap = 0.0f;
            bool DeferUI = false;               // pixel perfect upscaling: the host draws the UI (RenderDeferredUI)
        };
        bool GetRuntimeView(RuntimeView& out);
        // Draws the game through `view` (another scene's camera; `overlay`: no sky, see
        // SceneRenderOptions::Overlay).
        void RenderRuntime(const RuntimeView& view, bool overlay);
        void OnUpdateSimulation(Timestep ts, const EditorCamera& camera, const SceneRenderOptions& options = {});
        void OnUpdateEditor(Timestep ts, const EditorCamera& camera, const SceneRenderOptions& options = {});
        // What OnUpdateEditor animates before it draws: sprite / tint animations, particles and
        // Animators as the editor shows them (their previews, the Animation window's scrubbing).
        void UpdateEditorAnimation(Timestep ts);
        // Draws the scene from the editor camera without updating anything (an extra pass in
        // the same frame, e.g. the editor's click-through picking).
        void OnRenderEditor(const EditorCamera& camera, const SceneRenderOptions& options = {});
        void OnViewportResize(uint32_t width, uint32_t height);

        // First entity with this name, in scene order (the order of GetEntitiesInOrder()).
        // Indexed: a hash lookup instead of a scan over all entities - O(1) for a unique name, O(k)
        // for k entities with it. The index is made at the first lookup and then kept up to date
        // entity by entity: creating / destroying entities and Entity::SetName at once (no rebuild
        // when a game spawns and destroys every frame), reordering the hierarchy at the next lookup
        // of a name; a direct write to TagComponent::Tag is picked up on the next frame (one pass
        // over the names, without hashing) or after InvalidateNameCache().
        Entity FindEntityByName(std::string_view name);
        // Every entity with this name, in scene order.
        std::vector<Entity> FindEntitiesByName(std::string_view name);
        // How many entities have this name (O(1) after the index's frame check).
        size_t CountEntitiesByName(std::string_view name);
        // The next lookup checks every name (after direct writes to TagComponent::Tag).
        void InvalidateNameCache() { m_NameIndex.Check = true; }
        struct NameCacheStats
        {
            uint32_t Rebuilds = 0;     // full builds of the index (the first lookup)
            uint32_t Validations = 0;  // once-per-frame passes looking for direct writes
            uint32_t Updates = 0;      // entities re-listed (a rename, a direct write)
        };
        const NameCacheStats& GetNameCacheStats() const { return m_NameIndex.Stats; }
        // The gameplay tags' index (FindEntitiesWithTag ...): the same rules, for TagsComponent::Tags
        // (Entity::AddTag / RemoveTag at once, direct edits of the list - added or removed tags - on
        // the next frame or after InvalidateTagIndex()).
        const NameCacheStats& GetTagIndexStats() const { return m_TagIndex.Stats; }
        void InvalidateTagIndex() { m_TagIndex.Check = true; }

        // ---- animation work of the last Animator update (the editor's statistics, profiling)
        struct AnimationStats
        {
            uint32_t Animators = 0;      // updated (active in the hierarchy)
            uint32_t Posed = 0;          // skeletons / blend shapes posed
            uint32_t Unchanged = 0;      // made from the same inputs as last frame: the last pose kept
            uint32_t Culled = 0;         // off screen: not posed (CullPose) or not updated at all (CullCompletely)
            uint32_t CachedSamples = 0;  // clip poses read from pose caches (CachePoses)
            uint32_t ClipSamples = 0;    // clip poses sampled from the clips' keys
            size_t CacheBytes = 0;       // the memory of every pose cache (all models)
            float Milliseconds = 0.0f;   // the whole update
        };
        const AnimationStats& GetAnimationStats() const { return m_AnimationStats; }
        // The animators (with a model) a camera of this view x projection sees are marked as seen
        // after the last update; RenderScene does it for each view it draws (the game's camera, the
        // editor's). An Animator with Culling on that no view saw is off screen in the next update -
        // as long as the scene was drawn at all since the last one (a scene nobody draws, a headless
        // run: nothing is culled).
        void MarkVisibleAnimators(const glm::mat4& viewProjection);

        Entity GetEntityByUUID(UUID uuid);
        // Entities in creation order (stable, used by UI & serialization).
        std::vector<Entity> GetEntitiesInOrder();

        // ---- gameplay tags (TagsComponent), results in scene order. Indexed like the names: O(k)
        // for the k entities with the tag (CountEntitiesWithTag O(1)), see GetTagIndexStats.
        std::vector<Entity> FindEntitiesWithTag(std::string_view tag);
        Entity FindEntityWithTag(std::string_view tag);
        size_t CountEntitiesWithTag(std::string_view tag);
        // Every tag used in this scene (sorted, unique).
        std::vector<std::string> GetAllTags();

        // ---- by component type (the registry's pool of T: O(k) for the k entities that have one)
        // Every entity with a T, in scene order.
        template<typename T> std::vector<Entity> FindEntitiesWith();
        // The first one in scene order (null: none).
        template<typename T> Entity FindEntityWith();
        template<typename T> size_t CountEntitiesWith() { return m_Registry.storage<T>().size(); }
        // Every T, in scene order: a component (pointers into the pool: valid until components of
        // that type are added / removed) - or, for a script class (a ScriptableEntity subclass), the
        // running instances of that class and of classes derived from it.
        //   for (auto* light : scene.FindObjectsOfType<PointLightComponent>()) light->Intensity *= 0.5f;
        //   for (auto* enemy : scene.FindObjectsOfType<EnemyAI>()) enemy->Alert();
        template<typename T> std::vector<T*> FindObjectsOfType();
        template<typename T> T* FindObjectOfType();

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
        // The space the entity's Transform is in: its bone's (BoneAttachmentComponent), else its
        // parent's world transform, else the world (identity).
        glm::mat4 GetParentWorldTransform(Entity entity);
        // ---- bone attachments (BoneAttachmentComponent)
        // The model entity an attachment follows: its Target, else its nearest ancestor with a
        // skinned Mesh Renderer (an invalid entity when there is none).
        Entity GetBoneAttachmentTarget(Entity entity);
        // Where the attachment's bone is in its target's own space (the model's space, the pose its
        // Animator gives it; scale left out unless InheritBoneScale). False when the entity is not
        // attached or its target / bone is missing - it then follows its parent as usual.
        bool GetBoneAttachmentLocal(Entity entity, Entity& target, glm::mat4& boneInTarget);
        glm::vec3 GetWorldPosition(Entity entity);

        // ---- bounds (editor framing: F = selection, Shift+F = whole scene)
        // World-space box around what the entity draws: mesh / model, sprite, circle, text,
        // tilemap. Without any of these it is just the entity's position. False for screen-space
        // UI entities (they are not in the world).
        bool GetEntityBounds(Entity entity, AABB& out);
        // The box around what the entity draws in its own space (before its world transform; a
        // model's bounds in its bind pose). False when it draws nothing of the above or is UI.
        bool GetEntityLocalBounds(Entity entity, AABB& out);

        // ---- 2D draw order (sorting layers, see SortKey2D)
        // The sorting layer of the entity's 2D renderer (sprite, circle, text, tilemap, particles,
        // in that order); false without one.
        static bool GetSortingLayer(Entity entity, uint32_t& layer);
        // Y-sorted layers: the entity whose world position is the sort point of `entity` drawn on
        // `layer` - its topmost ancestor reached through parents that all draw on that layer, else
        // the entity itself (a group entity without a renderer breaks the chain).
        Entity GetSortRoot(Entity entity, uint32_t layer);
        // The key the renderer sorts the entity's renderer on `layer` with (`view`: the camera's
        // view matrix, for the depth; parallax offsets not counted).
        SortKey2D GetSortKey2D(Entity entity, uint32_t layer, int order, const glm::mat4& view = glm::mat4(1.0f));
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
        // The closest 2D collider (not a trigger) along the ray whose collision layer is in
        // `layerMask` (bits of CollisionLayers2D, e.g. Project::GetCollisionLayers2D().MakeMask({ "Ground" })).
        RaycastHit Raycast2D(const glm::vec2& origin, const glm::vec2& direction, float maxDistance, uint16_t layerMask = 0xFFFF);
        // Every entity the ray goes through (its first hit along the ray), the closest first; on a
        // layer in `layerMask`, triggers only with includeTriggers. A ray that starts inside a
        // collider does not hit it.
        std::vector<RaycastHit> Raycast2DAll(const glm::vec2& origin, const glm::vec2& direction, float maxDistance,
                                             uint16_t layerMask = 0xFFFF, bool includeTriggers = false);
        // The entities whose 2D colliders overlap a circle / a box (half extents, turned by
        // `angleDegrees` counter-clockwise) / contain a point (edge colliders have no inside), on a
        // layer in `layerMask`; triggers only with includeTriggers. Each entity once, the one whose
        // body is nearest to the centre first. Empty outside play / simulate.
        std::vector<Entity> OverlapCircle2D(const glm::vec2& center, float radius, uint16_t layerMask = 0xFFFF, bool includeTriggers = false);
        std::vector<Entity> OverlapBox2D(const glm::vec2& center, const glm::vec2& halfExtents, float angleDegrees = 0.0f,
                                         uint16_t layerMask = 0xFFFF, bool includeTriggers = false);
        std::vector<Entity> OverlapPoint2D(const glm::vec2& point, uint16_t layerMask = 0xFFFF, bool includeTriggers = false);
        // The convex pieces a polygon collider's outline is made of at this scale (the entity's world
        // scale; what its body gets, and what the editor draws).
        static Polygon2D::Decomposition DecomposePolygonCollider(const struct PolygonCollider2DComponent& collider, const glm::vec2& scale);
        // ---- 3D queries. layerMask: the collision layers that count (CollisionLayers2D bits - the same
        // layers as 2D, e.g. Project::GetCollisionLayers2D().MakeMask({ "Enemy" })); triggers only with
        // includeTriggers. A ray / swept shape that starts inside a collider does not hit it (the
        // caster's own body); mesh colliders' triangles are hit from their front side. The hits'
        // EntityID: Scene::GetEntityByUUID. Nothing outside play / simulate.
        // The closest 3D collider (not a trigger) along the ray.
        RaycastHit Raycast3D(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, uint16_t layerMask = 0xFFFF);
        // Every entity the ray goes through (its first hit), the closest first.
        std::vector<RaycastHit> Raycast3DAll(const glm::vec3& origin, const glm::vec3& direction, float maxDistance,
                                             uint16_t layerMask = 0xFFFF, bool includeTriggers = false);
        // The entities whose 3D colliders (or character controllers) overlap a sphere / a box (half
        // extents, turned by `rotation`) / a capsule between the centres of its two end spheres. Each
        // entity once, the one whose body is nearest to the centre first. A shape wholly inside a mesh
        // collider does not touch it (only its triangles count).
        std::vector<Entity> OverlapSphere3D(const glm::vec3& center, float radius, uint16_t layerMask = 0xFFFF, bool includeTriggers = false);
        std::vector<Entity> OverlapBox3D(const glm::vec3& center, const glm::vec3& halfExtents, const glm::quat& rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                                         uint16_t layerMask = 0xFFFF, bool includeTriggers = false);
        std::vector<Entity> OverlapCapsule3D(const glm::vec3& point0, const glm::vec3& point1, float radius, uint16_t layerMask = 0xFFFF,
                                             bool includeTriggers = false);
        // Shape casts: the shape swept from where it is along `direction` (normalized here) for
        // `maxDistance`. The hit: Distance - how far the shape went until it touched (the shape is then
        // at origin + direction * Distance), Point - where it touches, Normal - the surface there.
        // The *All variants: every entity on the way (its first touch), the closest first.
        RaycastHit SphereCast3D(const glm::vec3& origin, float radius, const glm::vec3& direction, float maxDistance,
                                uint16_t layerMask = 0xFFFF, bool includeTriggers = false);
        std::vector<RaycastHit> SphereCast3DAll(const glm::vec3& origin, float radius, const glm::vec3& direction, float maxDistance,
                                                uint16_t layerMask = 0xFFFF, bool includeTriggers = false);
        RaycastHit BoxCast3D(const glm::vec3& center, const glm::vec3& halfExtents, const glm::vec3& direction, const glm::quat& rotation,
                             float maxDistance, uint16_t layerMask = 0xFFFF, bool includeTriggers = false);
        std::vector<RaycastHit> BoxCast3DAll(const glm::vec3& center, const glm::vec3& halfExtents, const glm::vec3& direction,
                                             const glm::quat& rotation, float maxDistance, uint16_t layerMask = 0xFFFF, bool includeTriggers = false);
        RaycastHit CapsuleCast3D(const glm::vec3& point0, const glm::vec3& point1, float radius, const glm::vec3& direction, float maxDistance,
                                 uint16_t layerMask = 0xFFFF, bool includeTriggers = false);
        std::vector<RaycastHit> CapsuleCast3DAll(const glm::vec3& point0, const glm::vec3& point1, float radius, const glm::vec3& direction,
                                                 float maxDistance, uint16_t layerMask = 0xFFFF, bool includeTriggers = false);
        // The hull a Convex Hull Collider 3D of `entity` makes (unscaled, in the entity's frame): of its
        // ModelPath, else of the entity's Mesh Renderer (model or primitive). Null - with the reason in
        // `error` - without a source or when the mesh has no volume.
        static Ref<const ConvexHull3D> GetConvexHullCollider(Entity entity, std::string* error = nullptr);
        // The triangles a Mesh Collider 3D of `entity` is made of (unscaled, in the entity's frame): of
        // its ModelPath, else of the Mesh Renderer's model or primitive. Null - with the reason - without
        // a source or triangles.
        static Ref<const CollisionMesh3D> GetMeshCollider(Entity entity, std::string* error = nullptr);
        // The running worlds (null outside play / simulate).
        PhysicsWorld2D* GetPhysicsWorld2D() { return m_PhysicsWorld.get(); }
        // ---- 2D sleeping (Rigidbody2DComponent::Sleeping, SceneSettings::Physics2D)
        // The entity's running 2D body is dynamic / kinematic and sleeps now.
        bool IsSleeping2D(Entity entity);
        // Wakes the entity's 2D body / puts it to sleep (its velocities stop; it wakes again when
        // something touches it). False without a running dynamic / kinematic body.
        bool SetAwake2D(Entity entity, bool awake);
        // A force on the entity's dynamic 2D body's centre (ScriptableEntity::ApplyForce; wakes it). It
        // acts for the time of the frame it is applied in: through every fixed step and sub-step that
        // simulates that time (Box2D itself forgets a force after one b2World::Step), so a force applied
        // every frame acts the same at any frame rate, step rate or sub-step count. False without a
        // running dynamic body.
        bool ApplyForce2D(Entity entity, const glm::vec2& force);
        // The running 2D world's bodies, sleeping ones, joints, contacts, steps (zeros without one).
        PhysicsStats2D GetPhysics2DStats() const;
        // Box2D steps the last frame took (fixed steps x sub-steps).
        int GetPhysics2DStepsLastFrame() const { return m_Physics2DStepsLastFrame; }
        PhysicsWorld3D* GetPhysicsWorld3D() { return m_PhysicsWorld3D.get(); }
        // ---- 3D sleeping (Rigidbody3DComponent::Sleeping, SceneSettings::Physics3D)
        // The entity's running 3D body is dynamic / kinematic and sleeps now.
        bool IsSleeping3D(Entity entity);
        // Wakes the entity's 3D body / puts it to sleep (its velocities stop; it wakes again when
        // something touches it). False without a running dynamic / kinematic body.
        bool SetAwake3D(Entity entity, bool awake);
        // The running 3D world's bodies, sleeping ones, joints, characters, contacts (zeros without one).
        PhysicsStats3D GetPhysics3DStats() const;

        // ---- viewport input (set by the host each frame; pixels, origin top-left of the game view)
        void SetViewportMouse(const glm::vec2& position, bool leftButtonDown) { m_MousePosition = position; m_MouseDown = leftButtonDown; }
        // The mouse wheel over the game view this frame (notches; y > 0: away from the user) - the UI's
        // scroll views use it. The host sets it each frame (it is used up by the next UI update).
        void SetViewportScroll(const glm::vec2& wheel) { m_ViewportScroll = wheel; }
        const glm::vec2& GetViewportMouse() const { return m_MousePosition; }
        glm::vec2 GetViewportSize() const { return { (float)m_ViewportWidth, (float)m_ViewportHeight }; }
        // ---- screen-space UI (UITransformComponent). UI units are view pixels unless a UI Canvas
        // scales them (UICanvasComponent: then px = units x GetUIScale); origin bottom-left of the view.
        // The font size (UI units) of a UI element's Text: the UI Transform's FontSize, else the
        // rectangle's height (a toggle's label: 0.55 of it).
        static float GetUITextSize(const UITransformComponent& t, const UIRect& rect, bool toggleLabel = false);
        // The entity's UI rectangle in the current view, in its UI units (empty without a UITransform):
        // laid out in its UI parent's rectangle (else the view's) - or placed by the parent's layout group.
        UIRect GetUIRect(Entity entity);
        // The same in view pixels.
        UIRect GetUIScreenRect(Entity entity);
        // Where it is drawn (UI units): its rectangle moved / scaled by its look and its UI
        // ancestors' (UITransform Scale, VisualOffset - what tweens animate). Clicks follow it.
        UIRect GetUIVisualRect(Entity entity);
        // Its alpha times its UI ancestors' (what it is drawn with).
        float GetUIAlpha(Entity entity);
        // A point in the entity's layout units <-> view pixels (origin bottom-left): through its
        // canvas scale and its look (the editor's rect gizmo draws / drags the layout rectangle there).
        glm::vec2 UILayoutToView(Entity entity, const glm::vec2& point);
        glm::vec2 UIViewToLayout(Entity entity, const glm::vec2& pixel);
        // The topmost UI element whose rectangle (as drawn) holds `point` (view px, bottom-left) -
        // any that draws something (image, sprite, circle, text) or is a widget, not only those
        // that take the mouse (the editor selects UI in the viewport with it). Inactive and faded
        // out ones are skipped; `accept` filters the rest (the editor: hidden / locked ones).
        Entity PickUIRect(const glm::vec2& point, const std::function<bool(Entity)>& accept = {});
        // View pixels per UI unit of the entity's UI (its outermost UI Canvas's scale; 1 without one).
        float GetUIScale(Entity entity);
        // The rectangle the entity is laid out in (UI units): its UI parent's, else the whole view.
        UIRect GetUIParentRect(Entity entity);
        // A layout group (UILayoutGroupComponent of its UI parent) places the entity.
        bool IsUILayoutDriven(Entity entity);
        // Moves / resizes it so that it gets `rect` (UI units; its anchors and pivot stay).
        void SetUIRect(Entity entity, const UIRect& rect);
        // New anchors / pivot; keepRect: Position and Size change so that it stays where it is.
        void SetUIAnchors(Entity entity, const glm::vec2& anchorMin, const glm::vec2& anchorMax, bool keepRect = true);
        void SetUIPivot(Entity entity, const glm::vec2& pivot, bool keepRect = true);
        // The UI entities in the order they are drawn (bottom first): the hierarchy depth-first (a
        // parent below its children), siblings by UITransform Order, then hierarchy order (roots: by
        // Order, then creation). `skip`: entities to leave out (their children are still listed).
        std::vector<entt::entity> GetUIDrawOrder(const std::unordered_set<entt::entity>* skip = nullptr);
        // The topmost UI element that takes the mouse at `point` (view px, origin bottom-left): an
        // interactable button / slider / toggle / input field, or an image that is a raycast target
        // (then what is below it is not reached). Invalid when the point is over none of them.
        // Inactive entities are skipped.
        Entity PickUI(const glm::vec2& point);
        // The UI element under the mouse in the last UI update (what PickUI found then).
        Entity GetHoveredUI();
        // ---- UI animations (UIAnimation.h): tweens on the UI's own time - they run while the game
        // runs, also while it is paused. A tween of a property replaces the one already running on
        // it for that entity. Returns its id (0: not started - no UI Transform / picture).
        //   scene.TweenUI(panel, UITweenProperty::Alpha, glm::vec4(0.0f), 0.3f);
        //   scene.TweenUI(button, UITweenProperty::Scale, glm::vec4(1.1f), 0.5f, { .Easing = Ease::InOutQuad, .Loop = UITweenLoop::PingPong });
        uint64_t TweenUI(Entity entity, UITweenProperty property, const glm::vec4& to, float duration, UITweenOptions options = {});
        // Stops its tweens (only those of `property` when given); `complete`: they jump to their end
        // first (their OnComplete is not called).
        void CancelUITweens(Entity entity, bool complete = false);
        void CancelUITweens(Entity entity, UITweenProperty property, bool complete = false);
        bool CancelUITween(uint64_t id);
        bool IsUITweening(Entity entity) const;
        size_t GetUITweenCount() const { return m_UITweens.size(); }
        // A property's value now (vec4 as TweenUI takes it); `ok` false: the entity has none.
        glm::vec4 GetUITweenValue(Entity entity, UITweenProperty property, bool* ok = nullptr);
        // Switches the entity on; its UIAnimation's show plays (as it does when it becomes active).
        // Stops a hide that is playing.
        void ShowUI(Entity entity);
        // Plays its UIAnimation's hide, then switches it off (at once: no hide preset, not running,
        // `animate` false). Its look comes back to rest for the next show.
        void HideUI(Entity entity, bool animate = true);
        bool IsUIHiding(Entity entity);
        // A quick bump: drawn `amount` bigger, springing back over `duration` seconds.
        void PunchUIScale(Entity entity, float amount = 0.15f, float duration = 0.35f);

        // ---- keyboard / gamepad navigation of the UI widgets (buttons, sliders, toggles, input
        // fields that are interactable and active). While one is selected: arrows / D-pad / left
        // stick move the selection (UINavigationComponent: how; else to the nearest widget in that
        // direction - a horizontal slider takes left / right for its value), Tab / Shift+Tab go to
        // the next / previous one, Enter / gamepad A submits it (clicks a button, switches a toggle,
        // starts editing an input field), Escape / gamepad B cancels (OnCancel). Nothing is selected
        // until code selects a widget (or a UINavigation SelectOnEnable does), Tab is pressed or the
        // mouse presses a widget; a press elsewhere selects nothing.
        Entity GetSelectedUI();
        // `highlight`: the selected widget shows its outline (keyboard / gamepad use; a mouse press
        // selects without it). An invalid entity (or one that is not a widget) selects nothing.
        void SetSelectedUI(Entity widget, bool highlight = true);
        bool IsUISelectionHighlighted() const { return m_UISelectionHighlight && m_SelectedUI != entt::null; }
        // A widget that navigation can select (UINavigation Mode None: only a click selects it).
        bool IsUISelectable(Entity entity);
        // Where a move from `from` in `direction` goes (invalid: nowhere), as a key press would.
        Entity FindUINavigationTarget(Entity from, UINavDirection direction);
        // What the keys do, from code (only while the game runs): move the selection (false: it
        // stayed), submit / cancel the selected widget (false: nothing selected).
        bool NavigateUI(UINavDirection direction);
        bool SubmitUI();
        bool CancelUI();
        // Keys and gamepads move the selection (on by default).
        void SetUINavigationEnabled(bool enabled) { m_UINavigationEnabled = enabled; }
        bool IsUINavigationEnabled() const { return m_UINavigationEnabled; }
        // ---- UI widgets from code. `notify`: the entity's script callback and the event bus hear of
        // it as when the player changes it. False when the entity has no such widget or nothing changed.
        bool SetSliderValue(Entity slider, float value, bool notify = true); // clamped (rounded: WholeNumbers)
        bool SetToggle(Entity toggle, bool isOn, bool notify = true);        // switched on: the rest of its group off
        bool SetInputText(Entity field, const std::string& text, bool notify = true); // filtered by its content type
        // Starts editing the input field (keyboard input goes to it, the caret at the end); an invalid
        // entity ends the editing of the one being edited.
        void FocusInputField(Entity field);
        Entity GetFocusedInputField();
        bool IsTextInputFocused() const { return m_FocusedInput != entt::null; }
        // Whether the keyboard reaches the UI (typing into input fields). The editor turns it off
        // while its game view does not have the focus. On by default.
        void SetUIKeyboardEnabled(bool enabled) { m_UIKeyboardEnabled = enabled; }
        bool IsUIKeyboardEnabled() const { return m_UIKeyboardEnabled; }
        // ---- masks (UIMaskComponent): the rectangle (view px) the masks of the entity's UI ancestors
        // let it be drawn / found in; false when no mask clips it.
        bool GetUIClipRect(Entity entity, UIRect& clip);
        // ---- scroll views (UIScrollViewComponent). Positions are normalized: x 0 = the content's
        // left end shown .. 1 = its right end, y 1 = its top .. 0 = its bottom.
        glm::vec2 GetScrollPosition(Entity scrollView);
        // Moves the content there (clamped to 0..1) and stops its motion; `notify`: OnScrollChanged
        // and "UIScrollChanged" hear of it. False without a scroll view with a content.
        bool SetScrollPosition(Entity scrollView, const glm::vec2& normalized, bool notify = true);
        // Moves the content by `delta` UI units (+y up: the content goes up, the view shows what is
        // further down); kept in its bounds unless the movement is Unrestricted.
        bool ScrollBy(Entity scrollView, const glm::vec2& delta, bool notify = true);
        // Scrolls every scroll view the entity is in (innermost first) so that all of it is shown
        // (`margin` UI units around it) - smoothly while the game runs and `animate`, else at once.
        // Keyboard / gamepad navigation does this for the widget it selects. False: nothing to scroll.
        bool ScrollIntoView(Entity entity, bool animate = true, float margin = 8.0f);
        // The view of the scroll view (UI units): its Viewport's rectangle, else its own without its scrollbars.
        UIRect GetScrollViewRect(Entity scrollView);
        // The content the scroll view moves (invalid: none set or not found).
        Entity GetScrollContent(Entity scrollView);

        // Projection and view matrix used by the most recent render (post-processing needs the
        // projection for SSAO, debug drawing both).
        const glm::mat4& GetLastProjection() const { return m_LastProjection; }
        const glm::mat4& GetLastView() const { return m_LastView; }
        // Unprojects a viewport pixel through the primary camera onto the plane z = depthZ.
        glm::vec3 ScreenToWorld(const glm::vec2& screen, float depthZ = 0.0f);

        // ---- pixel-perfect cameras (CameraComponent::PixelPerfect), for the viewport size
        // The projection a camera entity shows the viewport with: its pixel-perfect one (what the
        // view shows of the render target) when it has one, else its own.
        glm::mat4 GetCameraProjection(Entity camera);
        // The pixel-perfect view of the primary camera; false when it has none (or no camera).
        bool GetPixelPerfectView(PixelPerfectView& out);
        // The size the game's scene picture is drawn at for a width x height view: a pixel-perfect
        // camera's render target (upscaling: the art's resolution; otherwise the view's own size),
        // else the render scale's (Renderer::GetRenderResolution).
        glm::uvec2 GetRuntimeRenderResolution(uint32_t width, uint32_t height);
        // The last game frame left its screen-space UI out (a pixel-perfect camera that upscales):
        // the host draws it with RenderDeferredUI over the finished (post-processed) picture, at the
        // view's resolution, into the bound target (display colours, not tonemapped).
        bool HasDeferredUI() const { return m_UIDeferred; }
        void RenderDeferredUI();

        // ---- particles (nothing is emitted for an entity that is not active in the hierarchy)
        void EmitParticles(Entity entity, int count);
        // Runs the particle systems `seconds` ahead (steps of `step`, at most 600), as the scene view
        // plays them (PlayInEditor ones): a picture of a scene that was just loaded or copied - a
        // thumbnail - shows its effects under way instead of empty. Nothing else moves.
        void SimulateParticles(float seconds, float step = 1.0f / 30.0f);
        // Where the entity's particles live in the world (their positions -> world): the emitter
        // (Local), the Custom space's entity, else the identity (World, or the Custom entity gone).
        glm::mat4 GetParticleSpace(Entity entity, const struct ParticleSystemComponent& ps);
        // Where the entity's emitter is in the world: its world transform, moved to the camera when
        // its system follows one (FollowCamera, as placed in the last update).
        glm::mat4 GetParticleEmitterTransform(Entity entity, const struct ParticleSystemComponent& ps);
        // The point FollowCamera systems follow: set (the scene view's camera - OnUpdateEditor /
        // OnUpdateSimulation set it every frame), else the primary camera's position; none
        // without either.
        void SetParticleViewer(std::optional<glm::vec3> position) { m_ParticleViewer = position; }
        std::optional<glm::vec3> GetParticleViewer();

        // ---- audio sources (AudioSourceComponent; only while the game runs)
        // Starts the source's sound from the beginning (a sound it is already playing is
        // stopped first). False when the scene is not running, the entity is not active in the
        // hierarchy, it has no source / path, or the sound could not be started (no device).
        // `fadeInSeconds` < 0: the source's FadeIn.
        bool PlayAudio(Entity entity, float fadeInSeconds = -1.0f);
        // Stops it; with a fade (`fadeOutSeconds` < 0: the source's FadeOut) the sound fades out by
        // itself while the source is free at once (IsAudioPlaying false, PlayAudio starts anew).
        void StopAudio(Entity entity, float fadeOutSeconds = -1.0f);
        // The playing sound's fade envelope goes to `volume` (0..1, x the source's Volume) over
        // `seconds`. False when it plays nothing. GetAudioFade: the envelope now (1: nothing plays).
        bool FadeAudio(Entity entity, float volume, float seconds);
        float GetAudioFade(Entity entity);
        // The source's filter (also of the sound playing now; a changed cutoff glides there).
        // SetAudioLowPass / SetAudioHighPass: cutoff in Hz, <= 0 switches that filter off.
        void SetAudioFilter(Entity entity, const AudioFilterSettings& filter);
        void SetAudioLowPass(Entity entity, float cutoffHz);
        void SetAudioHighPass(Entity entity, float cutoffHz);
        // Heard now (false while paused).
        bool IsAudioPlaying(Entity entity);
        // Pause keeps the sound where it is (its voice too); Resume goes on from there (refused
        // while the entity is inactive). False when the source plays nothing.
        bool PauseAudio(Entity entity);
        bool ResumeAudio(Entity entity);
        bool IsAudioPaused(Entity entity);
        // The source's Volume / Pitch (also of the sound playing now; its random variation stays).
        void SetAudioVolume(Entity entity, float volume);
        void SetAudioPitch(Entity entity, float pitch);
        // Seconds into the playing sound / its length (0 when it plays nothing). SetAudioTime jumps
        // there (clamped to the length); false when it plays nothing.
        float GetAudioTime(Entity entity);
        bool SetAudioTime(Entity entity, float seconds);
        float GetAudioLength(Entity entity);
        // The playing sound's live state (the inspector). False when it plays nothing.
        bool GetAudioInfo(Entity entity, SoundInfo& info);
        // The Audio Listener that listens: enabled, active in the hierarchy, the highest Priority
        // (ties: the one created first). Empty when there is none.
        Entity GetActiveAudioListener();
        // Where the 3D sounds are heard from: the active Audio Listener, else the primary camera.
        Entity GetAudioListenerEntity();
        // ---- sounds that end (OnSoundFinished)
        // `handle` is `owner`'s sound (a script's PlaySound): when it ends by itself, the script of
        // `owner` gets OnSoundFinished. Audio Sources' sounds are tracked by PlayAudio itself
        // (fromAudioSource). 0 / an invalid owner: nothing.
        void TrackSound(uint32_t handle, Entity owner, bool fromAudioSource = false);
        // ---- ambient zones (AmbientZoneComponent)
        // How far `point` is outside the zone's volume (0 inside), in world units, for the zone on
        // an entity with the world transform `world`.
        static float GetAmbientZoneDistance(const AmbientZoneComponent& zone, const glm::mat4& world, const glm::vec3& point);
        // How much of the zone is heard at `point`: 1 inside, falling smoothly to 0 at BlendDistance
        // outside (before the FadeTime smoothing).
        static float GetAmbientZoneWeight(const AmbientZoneComponent& zone, const glm::mat4& world, const glm::vec3& point);
        // The zone's level heard now, 0..1 (0: no zone, not running).
        float GetAmbientZoneLevel(Entity entity);

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
        // Calls `call` for every created script instance (OnCreate has returned, not being destroyed),
        // in scene order - a snapshot: scripts the calls create are not visited, destroyed ones are
        // skipped (SceneManager's scene events).
        void ForEachRunningScript(const std::function<void(ScriptableEntity&)>& call);

        // ---- publish / subscribe events shared by all scripts (cleared when play mode stops)
        // SceneManager: every scene it runs shares one bus (a persistent manager hears the level's events).
        EventBus& GetEventBus() { return *m_EventBus; }

        // ---- running together with other scenes (SceneManager: additive scenes, DontDestroyOnLoad)
        // The manager this scene runs in (null: on its own - the editor's edit scene, tests).
        SceneManager* GetSceneManager() const { return m_SceneManager; }
        // Moves `entity` with all its descendants into `target` while both run: the same UUIDs and
        // components (runtime state included - a playing sound plays on, particles and animators go
        // on), the running script instances go along (their member values, timers, tweens and event
        // bus subscriptions stay; no OnDisable / OnDestroy / OnCreate), physics bodies are made again
        // in the target's worlds where the entities are (at rest). The entity becomes a root of
        // `target` (its world transform kept). Returns it in `target` (invalid when nothing moved).
        // Not from inside a callback of a moved script: SceneManager::DontDestroyOnLoad defers it to
        // the end of the frame.
        static Entity MoveEntityToScene(Entity entity, Scene& target);
        // The audio clips set to Preload that this scene's sources and zones play (SceneManager keeps
        // the clips of every loaded scene preloaded).
        std::vector<std::filesystem::path> CollectAudioPreloads();

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

        // ---- camera shake (CameraComponent::Shake)
        // Shakes the view of the primary camera (or of `camera`): `intensity` of trauma (0..1, added
        // up to 1) that fades out over `duration` seconds (an earlier, longer shake keeps going);
        // `frequency`: how jittery (noise samples per second). Only the picture moves - the camera's
        // transform, a Camera2DFollow on it, physics and scripts are not affected. Runs while the
        // game runs (paused: it holds). False without such a camera.
        bool ShakeCamera(float intensity, float duration = 0.4f, float frequency = 20.0f);
        bool ShakeCamera(Entity camera, float intensity, float duration = 0.4f, float frequency = 20.0f);
        void StopCameraShake(); // every camera
        // What a camera entity sees through: its world transform with its shake (the game view,
        // the editor's play-mode overlays).
        glm::mat4 GetCameraViewTransform(Entity camera);

        // ---- tint animation (TintAnimationComponent)
        // What the entity's 2D renderers and Light 2D colours are multiplied by now (white without one).
        static glm::vec4 GetTint(Entity entity);
        // ---- one-way platforms (OneWay tiles of a tileset, BoxCollider2D::OneWay)
        // The entity's 2D body falls through every one-way surface for `seconds` (and for as long as
        // it is still inside one): "down + jump" in a platformer. False without a running 2D world.
        bool DropThroughPlatforms(Entity entity, float seconds = 0.35f);

        bool IsRunning() const { return m_IsRunning; }
        // Between OnSimulationStart and OnSimulationStop (the editor's Simulate: physics and
        // animations without scripts).
        bool IsSimulating() const { return m_IsSimulating; }
        bool IsPaused() const { return m_IsPaused; }
        void SetPaused(bool paused) { m_IsPaused = paused; }
        // ---- game time (a pause menu, slow motion): the game's clock runs TimeScale times as fast as
        // real time - OnUpdate / OnLateUpdate get the scaled time, timers, physics, animations and
        // particles follow it. At 0 the game is paused: nothing of it moves and only the scripts that
        // asked for it (ScriptableEntity::SetUpdateWhenPaused) get OnUpdate / OnLateUpdate (with 0).
        // The UI (widgets, UI tweens, navigation) always runs on real time. A pause from the UI (its
        // click / submit handlers) takes effect in the same frame; a resume from it in the next one, so
        // the press that closed a pause menu is not also a press for the game. Back to 1 when play
        // starts and stops. Not the editor's pause (SetPaused), which stops everything.
        void SetTimeScale(float scale) { m_TimeScale = std::max(0.0f, scale); }
        float GetTimeScale() const { return m_TimeScale; }
        // Time scale 0 (paused) and back to the scale it had before.
        void SetGamePaused(bool paused);
        bool IsGamePaused() const { return m_TimeScale <= 0.0f; }
        // The frame's real time (seconds, not scaled) and the real time since play started.
        float GetUnscaledDeltaTime() const { return m_UnscaledDeltaTime; }
        float GetUnscaledTime() const { return m_UnscaledTime; }
        // ---- quitting: the game asks to end (its menu's Quit). The player closes its window; the
        // editor stops Play. Cleared when play starts.
        void RequestQuit() { m_QuitRequested = true; }
        bool IsQuitRequested() const { return m_QuitRequested; }
        void ClearQuitRequest() { m_QuitRequested = false; }
        void Step(int frames = 1) { m_StepFrames = frames; }

        SceneSettings& GetSettings() { return m_Settings; }
        const SceneSettings& GetSettings() const { return m_Settings; }

        const std::string& GetName() const { return m_Name; }
        void SetName(const std::string& name) { m_Name = name; }
        // Description, author, tags (saved with the scene; Scene::Copy keeps them).
        SceneMetadata& GetMetadata() { return m_Metadata; }
        const SceneMetadata& GetMetadata() const { return m_Metadata; }

        float GetTime() const { return m_Time; }
        // The clock of animated tiles (seconds): runs while the game runs (not while paused), while
        // simulating and in the editor's scene view.
        double GetAnimationTime() const { return m_AnimationTime; }
        void SetAnimationTime(double time) { m_AnimationTime = time; }

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
        // A new 3D world with the project's collision layers.
        void MakePhysicsWorld3D();
        // The 3D queries with their debug drawing.
        std::vector<Entity> Overlap3D(const PhysicsQueryShape3D& shape, const glm::vec3& center, const glm::quat& rotation, uint16_t layerMask,
                                      bool includeTriggers);
        std::vector<RaycastHit> ShapeCast3D(const PhysicsQueryShape3D& shape, const glm::vec3& origin, const glm::quat& rotation,
                                            const glm::vec3& direction, float maxDistance, uint16_t layerMask, bool includeTriggers, bool all);
        void OnPhysics3DStop();
        // The Create* functions do nothing for an entity that is not active in the hierarchy.
        void CreatePhysicsBody(Entity entity);
        void CreatePhysicsBody3D(Entity entity);
        void CreateTilemapBody(Entity entity);
        void CreateCharacter3D(Entity entity);
        // A physics material asset changed: the running colliders that use one get its values.
        void ApplyPhysicsMaterials();
        // Every body / character the entity should have in the running worlds (none when inactive);
        // sleeping bodies where a new body appears are woken (switched on again / spawned).
        void CreatePhysicsBodies(Entity entity);
        // Removes the entity's 2D / tilemap / 3D bodies and its character from the worlds (bodies
        // resting on them wake up). The 2D joints on them go too (made again when both bodies are back).
        void DestroyPhysicsBodies(Entity entity);
        // ---- 2D joints (DistanceJoint2D ... WheelJoint2D)
        // Makes the joints whose bodies both exist (and that are not broken), makes again the ones
        // whose ConnectedBody changed, and passes the live settings (limits, motors, springs) on.
        void SyncJoints2D();
        // The joints a destroyed body took with it: their components let go of them.
        void ForgetJoints2D(const std::vector<PhysicsJoint2DRef>& joints);
        // Warns about joints (of `entities`, else all) whose connected body is not in the scene.
        void ReportMissingJointBodies(const std::vector<Entity>* entities);
        // Before each physics step of `dt`: the area / buoyancy effectors push the bodies inside them.
        void ApplyEffectors2D(float dt);
        // Before each physics step: the average of the scripts' forces (ApplyForce2D) not simulated yet.
        void ApplyScriptForces2D();
        // After a physics step of `dt`: the joints pulled harder than their BreakForce / BreakTorque break.
        void CheckJointBreaks2D(float dt);
        // The read-outs of the joints (reaction force / torque, angle, translation ...).
        void UpdateJointStates2D(float dt);
        // OnJointBreak for the scripts of the joints that broke since the last call.
        void DispatchJointBreaks2D();
        // A joint component removed (its joint goes) / replaced (a pasted copy: the old joint goes).
        template<typename T>
        void OnJoint2DDestroyed(entt::registry& registry, entt::entity entity);
        template<typename T>
        void OnJoint2DReplaced(entt::registry& registry, entt::entity entity);
        // ---- 3D joints (FixedJoint3D ... SliderJoint3D): the same as the 2D ones, in the 3D world
        void SyncJoints3D();
        void ForgetJoints3D(const std::vector<PhysicsJoint3DRef>& joints);
        void CheckJointBreaks3D(float dt);
        void UpdateJointStates3D(float dt);
        void DispatchJointBreaks3D();
        template<typename T>
        void OnJoint3DDestroyed(entt::registry& registry, entt::entity entity);
        template<typename T>
        void OnJoint3DReplaced(entt::registry& registry, entt::entity entity);
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
        // Camera2DFollowComponent: the followers move after their targets (game time `dt`).
        void UpdateCameraFollow(float dt);
        void UpdateAudio(float ts);
        // The entity's Audio Source (nullptr: none, or not this scene's entity).
        AudioSourceComponent* GetAudioSource(Entity entity);
        // The listener's place / direction (and velocity) for the audio engine.
        void UpdateAudioListener(float ts);
        // Ambient zones follow the listener: their levels, their sounds started / stopped.
        void UpdateAmbientZones(float ts);
        // The sounds that ended by themselves (AudioEngine::TakeFinishedSounds) to their entities'
        // scripts (OnSoundFinished) and the event bus.
        void DispatchFinishedSounds();
        void UpdateAnimators(Timestep ts, bool editor);
        // The mesh a morphed submesh is drawn with: the model's shared one at its own weights, else
        // the entity's copy with the weights in effect (its Animator's, else its Mesh Renderer's).
        Ref<Mesh> GetMorphedMesh(entt::entity entity, struct MeshRendererComponent& mr, size_t submesh);
        // Sprite animations: time, their clips (.zsanim: frames, texture, events); the editor
        // shows the first clip's frames.
        void UpdateSpriteAnimations(Timestep ts, bool editor);
        // Property animation players (.zanim): the values of the playing clips and their events.
        void UpdateAnimationPlayers(Timestep ts, bool editor);
        struct PendingAnimationEvent
        {
            UUID Entity;
            AnimationEvent Event;
            std::string Clip;
        };
        // The Animator events of `clip` passed by playing it `delta` seconds from `localTime`.
        // anyClipEvents: also the events for every clip (Clip ""; the base animation, not layers).
        void CollectAnimatorEvents(Entity entity, const struct AnimatorComponent& anim, const AnimationClip& clip, float localTime, float delta,
                                   bool loop, bool& atStart, bool anyClipEvents, std::vector<PendingAnimationEvent>& out);
        void ApplyRootMotion(Entity entity, struct AnimatorComponent& anim, const RootMotion& motion, float ts);
        // Tells the entity's script (OnAnimationEvent) and the event bus ("AnimationEvent").
        void FireAnimationEvent(Entity entity, const AnimationEvent& event, const std::string& clip, bool skeletal);
    public:
        // A step of the entity's FootstepsComponent now (its animation event): a random sound,
        // scaled by the speed. Returns the sound's handle (0: none - too slow, too soon after the
        // last step, no sounds, no audio device). The Footsteps component calls it on its event.
        uint32_t PlayFootstep(Entity entity, struct FootstepsComponent& steps);
    private:
        // TintAnimationComponent: the playing ones advance (editor: only the inspector's previews).
        void UpdateTintAnimations(Timestep ts, bool editor);
    public:
        // The 2D lights (Light 2D) as the renderer gets them for a view centred at `viewCenter`
        // (no culling, parallax not counted): for tests and tools.
        Lighting2DData CollectLights2D(const glm::vec2& viewCenter);
    private:
        void DispatchContactEvents(const std::vector<ContactEvent>& events);
        Entity DuplicateEntityRecursive(Entity entity, UUID newParent);
        Entity InstantiateCopy(Entity prefab, const glm::vec3* position);
        void StepPhysics(Timestep ts);
        // DebugDraw: the 2D / 3D contact points with their normals (ShowContacts, ShowContactDetails).
        void DrawPhysicsContacts();
        // DebugDraw: the running 2D / 3D joints (ShowJoints).
        void DrawPhysicsJoints();
        // DebugDraw: the velocities of the characters and dynamic bodies (ShowVelocities / ShowBodyVelocities).
        void DrawPhysicsVelocities();
        void StartScripts();
        void StopScripts();
        void CreateScriptInstance(Entity entity);
        // Delivers OnEnable / OnDisable until the instance matches NativeScriptComponent::Enabled.
        void SyncScriptEnabled(Entity entity);
        // OnStart for every enabled script that has not started yet (before the frame's OnUpdate).
        void StartPendingScripts();
        // A frame while the game is paused (time scale 0): the scripts that run while it is.
        void UpdatePausedScripts();
        // OnCollisionEnd / OnTriggerEnd for everything still touching `entity` (it is being destroyed).
        void EndContactsOf(Entity entity);
        void DeliverContact(Entity self, Entity other, bool begin, bool sensor);
        void FlushDestroyQueue();
        void UpdateParticles(Timestep ts, bool editor);
        void UpdateUI(Timestep ts);
        // The widgets' runtime state from the start: nothing hovered, pressed or edited, toggles and
        // progress bars drawn as they are set (play starts / stops).
        void ResetUIRuntime();
        // ---- UI widgets (SceneUI.cpp)
        // What the scripts hear of in a UI update, in the order it happened.
        enum class UIChange
        {
            Slider, Toggle, TextChanged, TextSubmitted, TextEndEdit,
            PointerEnter, PointerExit, PointerDown, PointerUp, PointerClick, Click, Select, Deselect, Cancel, Scroll
        };
        struct UIChangeEvent
        {
            entt::entity Entity = entt::null;
            UIChange Kind = UIChange::Slider;
            float Value = 0.0f;
            bool On = false;
            std::string Text;
            entt::entity Target = entt::null; // pointer events: the element under the mouse
            glm::vec2 Screen{ 0.0f };         // pointer events: the mouse (view px, bottom-left); Scroll: the normalized position
        };
        // The laid-out rectangle of a UI entity (UI units) and its canvas scale.
        struct UILayoutNode
        {
            UIRect Rect;
            UIRect Parent;      // the rectangle it was laid out in
            float Scale = 1.0f;
            bool Driven = false; // placed by its parent's layout group
            // The look (with its UI ancestors'): drawn point = layout point x VisScale + VisOffset.
            glm::vec2 VisScale{ 1.0f };
            glm::vec2 VisOffset{ 0.0f };
            float Alpha = 1.0f;
            UIRect Visual;       // Rect as drawn
            // The same without the widgets' hover / press feedback: where the mouse finds it (a
            // button that shrinks while pressed does not slip away from under the mouse).
            glm::vec2 HitScale{ 1.0f };
            glm::vec2 HitOffset{ 0.0f };
            UIRect HitRect;
            // Masks (UIMaskComponent) of its UI ancestors: it is drawn / found only inside VisClip /
            // HitClip (view px) while Clipped; what its own UI children get (its mask on top).
            bool Clipped = false;
            UIRect VisClip, HitClip;
            bool ChildClipped = false;
            UIRect ChildVisClip, ChildHitClip;
            // A scroll view: the space its UI children are laid out in (its rectangle without the
            // scrollbars it shows).
            UIRect ChildArea;
            bool HasLook() const { return VisScale != glm::vec2(1.0f) || VisOffset != glm::vec2(0.0f); }
            // A view point (UI units) in the layout's units (the look - without feedback - undone).
            glm::vec2 ToLayout(const glm::vec2& p) const
            {
                glm::vec2 s = HitScale;
                for (int i = 0; i < 2; i++)
                    if (std::abs(s[i]) < 1e-6f)
                        s[i] = s[i] < 0.0f ? -1e-6f : 1e-6f;
                return (p - HitOffset) / s;
            }
        };
        // Lays out every UI entity for the current view (m_UILayout).
        void LayoutUI();
        void LayoutUINode(entt::entity e, const UIRect& parent, const UIRect& rect, float scale, bool driven,
                          std::unordered_map<entt::entity, UILayoutSizes>& sizes, const UILayoutNode* parentLook = nullptr);
        // UI animations: shows of elements that became active, hover / press feedback; the tweens.
        void UpdateUIAnimations(float dt);
        void UpdateUITweens(float dt);
        // Elements that became active since the last look: their show starts (before they are drawn).
        void StartPendingUIShows();
        void StartUIShow(entt::entity e);
        // Writes a tween's value into its property.
        bool SetUITweenValue(entt::entity e, UITweenProperty property, const glm::vec4& value);
        struct UITween
        {
            uint64_t ID = 0;
            entt::entity Entity = entt::null;
            UITweenProperty Property = UITweenProperty::Scale;
            glm::vec4 From{ 0.0f }, To{ 0.0f };
            bool FromSet = false;
            float Duration = 0.0f, Delay = 0.0f, Elapsed = 0.0f;
            Ease Easing = Ease::OutQuad;
            UITweenLoop Loop = UITweenLoop::None;
            int Loops = -1;
            bool Done = false;
            std::function<void()> OnComplete;
        };
        // The entity's rectangle as the parent's layout group sees it (own size, min / preferred / flexible).
        UILayoutItem GetUILayoutItem(entt::entity child, const glm::vec2& parentSize, std::unordered_map<entt::entity, UILayoutSizes>& sizes);
        UILayoutSizes GetUILayoutGroupSizes(entt::entity group, const glm::vec2& groupSize, std::unordered_map<entt::entity, UILayoutSizes>& sizes);
        // The rectangle of a child laid out by its own UI Transform in `parent` (a group fitting its content resized).
        UIRect ComputeOwnUIRect(entt::entity e, const UIRect& parent, std::unordered_map<entt::entity, UILayoutSizes>& sizes);
        // The children (with a UI Transform) of a UI entity, in hierarchy order.
        void GetUIChildren(entt::entity e, std::vector<entt::entity>& out);
        entt::entity GetUIParent(entt::entity e);
        // The layout of a UI entity in this frame (laid out again when it is not known yet).
        const UILayoutNode& GetUINode(entt::entity e);
        // PickUI over the current layout.
        entt::entity PickUILaidOut(const glm::vec2& point);
        bool IsUIWidget(entt::entity e);
        void SelectUI(entt::entity e, bool highlight, std::vector<UIChangeEvent>& changes);
        // Clicks / switches / starts editing the widget (the keyboard's Submit).
        bool SubmitWidget(entt::entity e, std::vector<UIChangeEvent>& changes);
        bool MoveUISelection(UINavDirection direction, std::vector<UIChangeEvent>& changes);
        bool StepSlider(entt::entity e, UINavDirection direction, std::vector<UIChangeEvent>& changes);
        void TabUISelection(bool backwards, std::vector<UIChangeEvent>& changes);
        // The keys / gamepad buttons of navigation in this frame (`editing`: an input field had the
        // keyboard when the frame began).
        void ProcessUINavigation(float dt, bool editing, std::vector<UIChangeEvent>& changes);
        // The script callback and the event bus message of a change (only while the game runs).
        void DeliverUIChange(const UIChangeEvent& change);
        void DeliverUIChanges(std::vector<UIChangeEvent>& changes);
        // A toggle switched (its group follows); the changes for the callbacks are appended.
        bool ApplyToggle(entt::entity toggle, bool isOn, std::vector<UIChangeEvent>* changes);
        void BeginInputEdit(entt::entity field);
        // Ends the editing of the focused field (`changes`: its OnTextEndEdit).
        void EndInputEdit(std::vector<UIChangeEvent>* changes);
        void ProcessTextInput(entt::entity field, std::vector<UIChangeEvent>& changes);
        // Keeps the focused field's caret in view (its Scroll), for its rectangle.
        void ScrollInputField(entt::entity field);
        // ---- scroll views (SceneUI.cpp)
        entt::entity GetScrollContentEntity(entt::entity scrollView);
        // The view rectangle (UI units) of a laid-out scroll view.
        UIRect GetScrollViewRectLaidOut(entt::entity scrollView);
        // Its scrollbars (UI units of its layout): which are shown, their tracks and handles.
        struct ScrollbarGeometry
        {
            bool Show[2] = { false, false };     // 0 horizontal, 1 vertical
            UIRect Track[2], Handle[2];
            float HandleLength[2] = { 0.0f, 0.0f };
        };
        ScrollbarGeometry GetScrollbars(entt::entity scrollView);
        // The scrollbar of the scroll view at a view point (px): 0 horizontal, 1 vertical, -1 none.
        int ScrollbarAt(entt::entity scrollView, const glm::vec2& point);
        // Moves the content by `delta` (UI units) and lays the UI out again.
        void MoveScrollContent(entt::entity scrollView, const glm::vec2& delta);
        // Wheel, drags, scrollbars, inertia, the spring back, revealing the selection; the changes
        // of position for the callbacks.
        void UpdateUIScrollViews(float dt, const glm::vec2& mouse, bool pressedNow, bool released, entt::entity hovered,
                                 std::vector<UIChangeEvent>& changes);
        // Scrolls the scroll views around the entity to show it (see ScrollIntoView).
        bool RevealInScrollViews(entt::entity e, bool animate, float margin);
        // Whether the masks around a laid-out element leave none of it to be seen.
        bool IsUIClippedAway(entt::entity e);
        bool HasScrollViewAncestor(entt::entity e);

        void RenderUI(const std::unordered_set<entt::entity>* skip);
        // The Light 2D lights the view sees (culled by `frustum` when given; the nearest to
        // `viewCenter` first, at most Light2DMath::MaxLights) and the scene's 2D ambient light.
        Lighting2DData GatherLights2D(const std::function<const glm::mat4&(entt::entity)>& world, const glm::vec2& viewCenter,
                                      const Frustum* frustum, const std::unordered_set<entt::entity>& skipped);
        // CollectLights with the set of entities to leave out given (RenderScene has it already).
        LightSet GatherLights(const glm::vec3& viewPosition, const std::unordered_set<entt::entity>& skipped);
        // Entities by their scene order (the order of the ID pool, as the hierarchy lists them).
        void SortInSceneOrder(std::vector<entt::entity>& entities);
        // The main directional light of these (in scene order): the first that casts shadows, else the first.
        entt::entity PickSun(const std::vector<entt::entity>& directionalLights);

        // ---- the name / tag indices (FindEntityByName, FindEntitiesWithTag)
        struct StringHash
        {
            using is_transparent = void;
            size_t operator()(std::string_view s) const { return std::hash<std::string_view>{}(s); }
        };
        // The entities listed under one name / tag; sorted in scene order when it is read (Order:
        // the m_SceneOrderSerial it was sorted at).
        struct IndexedEntities
        {
            std::vector<entt::entity> Entities;
            uint64_t Order = 0;
        };
        struct EntityIndex
        {
            std::unordered_map<std::string, IndexedEntities, StringHash, std::equal_to<>> Lists;
            bool Built = false;
            bool Check = false;              // the next lookup checks every entity
            uint64_t CheckedFrame = ~0ull;   // m_FrameIndex of the last check
            NameCacheStats Stats;
        };
        // What each entity is listed under, by its entt index (entt::to_entity).
        struct NameSlot
        {
            std::string Name;
            bool Listed = false;
        };
        struct TagSlot
        {
            std::vector<std::string> Tags;
            bool Listed = false;
        };
        void OnNameConstructed(entt::registry&, entt::entity e);
        void OnNameUpdated(entt::registry&, entt::entity e);
        void OnNameDestroyed(entt::registry&, entt::entity e);
        void OnTagsChanged(entt::registry&, entt::entity e);
        void OnTagsDestroyed(entt::registry&, entt::entity e);
        void IndexInsert(EntityIndex& index, std::string_view key, entt::entity e);
        void IndexErase(EntityIndex& index, std::string_view key, entt::entity e);
        void ListName(entt::entity e);
        void UnlistName(entt::entity e);
        void ListTags(entt::entity e);
        void UnlistTags(entt::entity e);
        void EnsureNameIndex();
        void EnsureTagIndex();
        // The list under `key` in scene order (null: none); stale hits are re-listed first.
        const std::vector<entt::entity>* LookupName(std::string_view name);
        const std::vector<entt::entity>* LookupTag(std::string_view tag, bool sorted = true);
        void SortIndexed(IndexedEntities& list);
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
        // Whether its outline (OutlineThickness) can be drawn: a texture drawn as one quad that shows
        // the whole picture - Simple without repeating, or Sliced / Tiled when the picture comes out
        // as one stretched piece (no borders, or exactly one tile). Several pieces get the flash only.
        static bool CanDrawOutline(const struct SpriteRendererComponent& sprite, const struct SpriteAnimationComponent* animation,
                                   const AssetRegistry* registry, const glm::vec2* uiSize = nullptr);
        // A particle system's particles (and their trails) into the open Renderer2D scene (flushed
        // around them: no depth writes, its blend mode): billboards as GPU instances (one draw call,
        // ParticleRenderer; while instancing is on), flat ones in the 2D batch; alpha-blended ones
        // back to front (Sorting). `space` is the particles' space in the world (GetParticleSpace;
        // not used for World), `billboard` the camera's rotation, `viewer` its position (trails and
        // billboards face it; orthographic: its view direction) (SceneParticles.cpp).
        static void DrawParticleSystem(struct ParticleSystemComponent& ps, const glm::mat4& space, const glm::mat4& billboard, int entityID,
                                       const AssetRegistry* registry, const glm::vec3& viewer = glm::vec3(0.0f), bool perspective = false);
        // The box around a system's particles at their largest (any rotation, facing the camera or
        // not); `space` as for DrawParticleSystem.
        static AABB GetParticleBounds(const struct ParticleSystemComponent& ps, const glm::mat4& space);
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
        RuntimeStartTiming m_RuntimeStartTiming;
        bool m_IsRunning = false;
        bool m_IsSimulating = false;
        bool m_IsPaused = false;
        float m_TimeScale = 1.0f;
        float m_TimeScaleBeforePause = 1.0f;
        float m_UnscaledDeltaTime = 0.0f;
        float m_UnscaledTime = 0.0f;
        bool m_QuitRequested = false;
        bool m_ScriptsRunning = false;
        int m_StepFrames = 0;
        float m_Time = 0.0f;
        double m_AnimationTime = 0.0;
        float m_PhysicsAccumulator = 0.0f;
        int m_Physics2DStepsLastFrame = 0;
        // Script forces on 2D bodies (ApplyForce2D): this frame's sum, and the earlier frames' forces x
        // their time since Box2D last stepped (m_ScriptForceTime2D seconds of frames); the steps apply
        // Impulse / m_ScriptForceTime2D.
        struct ScriptForce2D
        {
            glm::vec2 Frame{ 0.0f };
            glm::vec2 Impulse{ 0.0f };
        };
        std::unordered_map<entt::entity, ScriptForce2D> m_ScriptForces2D;
        float m_ScriptForceTime2D = 0.0f;
        uint64_t m_PhysicsMaterialRevision = 0; // PhysicsMaterialLibrary::GetRevision() the colliders have

        std::string m_Name = "Untitled";
        SceneSettings m_Settings;
        SceneMetadata m_Metadata;

        std::unique_ptr<PhysicsWorld2D> m_PhysicsWorld;
        std::unique_ptr<PhysicsWorld3D> m_PhysicsWorld3D;
        std::filesystem::path m_PendingSceneLoad;
        glm::vec2 m_MousePosition{ -1.0f };
        glm::mat4 m_LastProjection{ 1.0f };
        glm::mat4 m_LastView{ 1.0f };
        Ref<class EnvironmentMap> m_EnvMap;
        std::string m_EnvMapPath;
        bool m_MouseDown = false, m_MouseWasDown = false;
        bool m_UIDeferred = false;
        // The UI widget the mouse button went down on (button, toggle, slider, input field): a click
        // is a release over the same one, a slider follows the mouse while it is held.
        entt::entity m_PressedUI = entt::null;
        entt::entity m_FocusedInput = entt::null; // the input field being edited
        bool m_UIKeyboardEnabled = true;
        float m_UITime = 0.0f;                    // seconds of UI updates (double clicks)
        float m_LastUIClickTime = -1.0f;
        entt::entity m_LastUIClickEntity = entt::null;
        std::unordered_map<entt::entity, UILayoutNode> m_UILayout;
        std::vector<UITween> m_UITweens;
        uint64_t m_NextUITweenID = 1;
        bool m_TickingUITweens = false;
        bool m_UIAnimationsStarted = false; // the first UI update of the run has been (ShowAtStart)
        std::vector<entt::entity> m_UIHoverChain;   // the hovered element and its UI ancestors (deepest first)
        entt::entity m_HoveredUI = entt::null;
        entt::entity m_SelectedUI = entt::null;     // keyboard / gamepad navigation
        bool m_UISelectionHighlight = false;
        bool m_UINavigationEnabled = true;
        int m_UINavHeld = -1;                       // the gamepad direction held (-1 none), for repeats
        float m_UINavRepeat = 0.0f;                 // seconds until it repeats
        entt::entity m_UISubmitFlash = entt::null;  // a button submitted by key looks pressed for a moment
        float m_UISubmitFlashTime = 0.0f;
        glm::vec2 m_ViewportScroll{ 0.0f };         // the wheel this frame (SetViewportScroll)
        entt::entity m_ScrollPress = entt::null;    // the scroll view a press went down in (it may become a drag)
        glm::vec2 m_ScrollPressPoint{ 0.0f };       // view px
        glm::vec2 m_ScrollPressLocal{ 0.0f };       // the press in the scroll view's UI units
        glm::vec2 m_ScrollContentStart{ 0.0f };     // the content's Position when the drag began
        bool m_ScrollDragging = false;              // m_ScrollPress drags its content
        float m_ScrollbarGrab = 0.0f;               // where on the handle a dragged scrollbar was taken

        std::unordered_map<UUID, entt::entity> m_EntityMap;
        std::vector<UUID> m_DestroyQueue;

        // FindEntityByName / FindEntitiesWithTag indices (see EntityIndex).
        EntityIndex m_NameIndex, m_TagIndex;
        std::vector<NameSlot> m_NameSlots;
        std::vector<TagSlot> m_TagSlots;
        uint64_t m_SceneOrderSerial = 1; // grows when the scene order changes (MoveInSceneOrder)
        uint64_t m_FrameIndex = 0;
        std::optional<glm::vec3> m_ParticleViewer; // SetParticleViewer
        AnimationStats m_AnimationStats;
        uint64_t m_AnimationUpdate = 0;           // UpdateAnimators calls
        uint64_t m_AnimationViewUpdate = ~0ull;   // the update after which a view was last drawn

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
        // Joints that broke in the steps of this frame (delivered as OnJointBreak after the contacts).
        struct JointBreak2DRecord
        {
            UUID Entity = 0;
            Joint2DType Type = Joint2DType::Distance;
            UUID Connected = 0;
            float Force = 0.0f, Torque = 0.0f;
        };
        std::vector<JointBreak2DRecord> m_JointBreaks2D;
        struct JointBreak3DRecord
        {
            UUID Entity = 0;
            Joint3DType Type = Joint3DType::Fixed;
            UUID Connected = 0;
            float Force = 0.0f, Torque = 0.0f;
        };
        std::vector<JointBreak3DRecord> m_JointBreaks3D;
        // Sounds of the game's entities (a script's PlaySound, an Audio Source's PlayAudio) -> the
        // entity whose script is told when they end. Recorded when they start: whatever happens to
        // the source afterwards (restarted, stopped), a sound that already ended is still its.
        struct SoundOwner
        {
            UUID Entity = 0;
            bool FromAudioSource = false;
        };
        std::unordered_map<uint32_t, SoundOwner> m_SoundOwners;
        std::unordered_set<uint64_t> m_ReportedMissingJointBodies; // connected bodies already warned about
        bool m_ScriptsPendingStart = false;

        std::shared_ptr<EventBus> m_EventBus;
        bool m_SharedEventBus = false;        // the manager's bus: not cleared when this scene stops
        // SceneManager (see GetSceneManager): another of its scenes did this frame's global work
        // (DebugDraw's frame) / this scene places the audio listener.
        SceneManager* m_SceneManager = nullptr;
        bool m_FrameFollower = false;
        bool m_PlacesAudioListener = true;
        // The sounds this scene's entities play (sources, zones, scripts' one-shots) stop with it.
        void StopOwnSounds();
        std::vector<ScriptableEntity*> m_RetiredScripts;
        int m_MessageDepth = 0;
        bool m_MessageDepthWarned = false;

        friend class Entity;
        friend class SceneSerializer;
        friend class SceneHierarchyPanel;
        friend class SceneManager;
    };

}
