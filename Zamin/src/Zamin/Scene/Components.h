#pragma once

#include "Zamin/Core/UUID.h"
#include "Zamin/Renderer/Texture.h"
#include "Zamin/Renderer/Mesh.h"
#include "Zamin/Renderer/Material.h"
#include "Zamin/Renderer/Model.h"
#include "Zamin/Renderer/Font.h"
#include "Zamin/Scene/SceneCamera.h"
#include "Zamin/Scripting/ScriptField.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtx/quaternion.hpp>

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Zamin {

    class ScriptableEntity;

    struct IDComponent
    {
        UUID ID;
        IDComponent() = default;
        IDComponent(UUID id) : ID(id) {}
    };

    // Display name and the entity's own active flag. An entity takes part in the game only
    // when it and every ancestor are active (Scene::IsActiveInHierarchy); change the flag with
    // Scene::SetActive / Entity::SetActive so the running scene reacts at once (a direct write
    // is noticed on the next frame). Unlike EditorMetadataComponent::Hidden this is game state:
    // it is saved with the scene and kept by copies, duplicates and prefabs.
    struct TagComponent
    {
        std::string Tag;
        bool Active = true;
        TagComponent() = default;
        TagComponent(const std::string& tag) : Tag(tag) {}
    };

    // Gameplay tags ("Enemy", "Pickup", ...). Not to be confused with TagComponent,
    // which holds the entity's display name. Queried with Scene::FindEntitiesWithTag,
    // Entity::HasTag and the matching ScriptableEntity helpers.
    struct TagsComponent
    {
        std::vector<std::string> Tags;

        bool Has(std::string_view tag) const
        {
            return std::find(Tags.begin(), Tags.end(), tag) != Tags.end();
        }
        // Returns false for empty or duplicate tags.
        bool Add(std::string_view tag)
        {
            if (tag.empty() || Has(tag)) return false;
            Tags.emplace_back(tag);
            return true;
        }
        bool Remove(std::string_view tag)
        {
            auto it = std::find(Tags.begin(), Tags.end(), tag);
            if (it == Tags.end()) return false;
            Tags.erase(it);
            return true;
        }
    };

    // Parent/child hierarchy. Transform of a child is relative to its parent.
    struct RelationshipComponent
    {
        UUID Parent = 0;              // 0 = root
        std::vector<UUID> Children;
    };

    struct TransformComponent
    {
        glm::vec3 Translation = { 0.0f, 0.0f, 0.0f };
        glm::vec3 Rotation = { 0.0f, 0.0f, 0.0f }; // euler angles, radians
        glm::vec3 Scale = { 1.0f, 1.0f, 1.0f };

        TransformComponent() = default;
        TransformComponent(const glm::vec3& translation) : Translation(translation) {}

        glm::mat4 GetTransform() const
        {
            glm::mat4 rotation = glm::toMat4(glm::quat(Rotation));
            return glm::translate(glm::mat4(1.0f), Translation) * rotation * glm::scale(glm::mat4(1.0f), Scale);
        }

        glm::vec3 GetForward() const { return glm::rotate(glm::quat(Rotation), glm::vec3(0, 0, -1)); }
        glm::vec3 GetRight() const { return glm::rotate(glm::quat(Rotation), glm::vec3(1, 0, 0)); }
        glm::vec3 GetUp() const { return glm::rotate(glm::quat(Rotation), glm::vec3(0, 1, 0)); }
    };

    // ------------------------------------------------------------------ rendering
    struct SpriteRendererComponent
    {
        glm::vec4 Color{ 1.0f, 1.0f, 1.0f, 1.0f };
        Ref<Texture2D> Texture;
        std::string TexturePath; // relative to project assets
        float TilingFactor = 1.0f;
        int SortingOrder = 0;
        bool PixelArt = false;   // nearest filtering (crisp pixels)
        bool FlipX = false;
        bool FlipY = false;

        SpriteRendererComponent() = default;
        SpriteRendererComponent(const glm::vec4& color) : Color(color) {}
    };

    // Plays frames from a sprite sheet (grid) on the SpriteRenderer texture.
    struct SpriteAnimationComponent
    {
        int Columns = 4;
        int Rows = 1;
        int StartFrame = 0;
        int FrameCount = 4;
        float FPS = 10.0f;
        bool Loop = true;
        bool Playing = true;

        // runtime
        float Time = 0.0f;

        void Play(int startFrame, int frameCount, float fps = 10.0f, bool loop = true)
        {
            if (StartFrame == startFrame && FrameCount == frameCount && Playing) return;
            StartFrame = startFrame; FrameCount = frameCount; FPS = fps; Loop = loop; Playing = true; Time = 0.0f;
        }

        int GetCurrentFrame() const
        {
            int count = FrameCount > 0 ? FrameCount : 1;
            int f = (int)(Time * FPS);
            f = Loop ? f % count : (f < count ? f : count - 1);
            return StartFrame + f;
        }

        bool IsFinished() const { return !Loop && (int)(Time * FPS) >= FrameCount; }
    };

    struct CircleRendererComponent
    {
        glm::vec4 Color{ 1.0f, 1.0f, 1.0f, 1.0f };
        float Thickness = 1.0f;
        float Fade = 0.005f;
        int SortingOrder = 0;
    };

    struct TextComponent
    {
        std::string Text = "Matn";
        glm::vec4 Color{ 1.0f };
        float Kerning = 0.0f;
        float LineSpacing = 0.0f;
        bool Centered = false;
        std::string FontPath; // empty = default engine font
        Ref<Font> FontAsset;
    };

    struct MeshRendererComponent
    {
        PrimitiveMesh Type = PrimitiveMesh::Cube;
        std::string ModelPath;    // .gltf / .glb / .obj (when Type == Model)
        Ref<Mesh> MeshAsset;      // runtime cache (primitives)
        Ref<Model> ModelAsset;    // runtime cache (models)
        Material Mat;             // primitives: the material; models: tint/overrides
        std::string AlbedoPath;   // texture paths relative to assets
        std::string NormalPath;
        std::string MetalRoughPath;
        std::string EmissivePath;
        bool CastShadows = true;
    };

    // ---- animation state machine data
    struct AnimatorParameter
    {
        enum class Type { Float = 0, Bool, Trigger };
        std::string Name;
        Type Kind = Type::Float;
        float Value = 0.0f; // bools/triggers: 0 or 1
    };

    struct AnimatorCondition
    {
        enum class Op { Greater = 0, Less, IsTrue, IsFalse, Triggered };
        std::string Parameter;
        Op Mode = Op::Greater;
        float Threshold = 0.0f;
    };

    struct AnimatorState
    {
        std::string Name = "State";
        std::string Clip;
        float Speed = 1.0f;
        bool Loop = true;
        std::string SpeedParameter; // optional: multiplies Speed by a float parameter
    };

    struct AnimatorTransition
    {
        int From = -1;          // -1 = any state
        int To = 0;
        float Fade = 0.2f;
        bool HasExitTime = false;
        float ExitTime = 1.0f;  // normalized (1 = end of the clip)
        std::vector<AnimatorCondition> Conditions;
    };

    // Plays skeletal animations of the entity's (skinned glTF) MeshRenderer model.
    // Either drive it directly with Play(), or define States/Transitions/Parameters
    // (state machine) and only set parameters from scripts.
    struct AnimatorComponent
    {
        std::string Clip;          // animation name ("" = first clip)
        float Speed = 1.0f;
        bool Loop = true;
        bool Playing = true;
        bool PlayInEditor = true;
        float DefaultFade = 0.2f;  // seconds used by Play() when no fade is given

        // runtime
        float Time = 0.0f;
        std::vector<glm::mat4> JointMatrices;
        // crossfade state: blending from the previous clip into `Clip`
        std::string PreviousClip;
        float PreviousTime = 0.0f;
        float PreviousSpeed = 1.0f;
        bool PreviousLoop = true;
        float FadeDuration = 0.0f;
        float FadeElapsed = 0.0f;

        // Switch clip with a smooth crossfade (fade < 0 = DefaultFade, 0 = instant).
        void Play(const std::string& clip, bool loop = true, float speed = 1.0f, float fade = -1.0f)
        {
            if (Clip == clip && Playing) { Speed = speed; Loop = loop; return; }
            if (fade < 0.0f) fade = DefaultFade;
            PreviousClip = Clip; PreviousTime = Time; PreviousSpeed = Speed; PreviousLoop = Loop;
            FadeDuration = fade; FadeElapsed = 0.0f;
            Clip = clip; Loop = loop; Speed = speed; Playing = true; Time = 0.0f;
        }
        bool IsFading() const { return FadeDuration > 0.0f && FadeElapsed < FadeDuration; }

        // ---- state machine
        std::vector<AnimatorParameter> Parameters;
        std::vector<AnimatorState> States;
        std::vector<AnimatorTransition> Transitions;
        int DefaultState = 0;
        int CurrentState = -1; // runtime

        AnimatorParameter* FindParameter(const std::string& name)
        {
            for (auto& p : Parameters) if (p.Name == name) return &p;
            return nullptr;
        }
        void SetFloat(const std::string& name, float v) { if (auto* p = FindParameter(name)) p->Value = v; }
        void SetBool(const std::string& name, bool v) { if (auto* p = FindParameter(name)) p->Value = v ? 1.0f : 0.0f; }
        void SetTrigger(const std::string& name) { if (auto* p = FindParameter(name)) p->Value = 1.0f; }
        float GetFloat(const std::string& name) { auto* p = FindParameter(name); return p ? p->Value : 0.0f; }
        bool GetBool(const std::string& name) { auto* p = FindParameter(name); return p && p->Value != 0.0f; }
        const char* GetCurrentStateName() const
        {
            return CurrentState >= 0 && CurrentState < (int)States.size() ? States[CurrentState].Name.c_str() : "";
        }
        bool UsesStateMachine() const { return !States.empty(); }
    };

    struct CameraComponent
    {
        SceneCamera Camera;
        bool Primary = true;
        bool FixedAspectRatio = false;
        glm::vec4 ClearColor{ 0.08f, 0.09f, 0.11f, 1.0f };
    };

    struct DirectionalLightComponent
    {
        glm::vec3 Color{ 1.0f, 0.96f, 0.9f };
        float Intensity = 3.0f;
        bool CastShadows = true;
    };

    struct PointLightComponent
    {
        glm::vec3 Color{ 1.0f };
        float Intensity = 5.0f;
        float Radius = 10.0f;
    };

    // Capsule character (Jolt CharacterVirtual): walks on slopes/stairs, collides with the world.
    // Scripts set the horizontal velocity with Move() and call Jump(); gravity is applied automatically.
    struct CharacterController3DComponent
    {
        float Radius = 0.35f;
        float Height = 1.8f;
        float MaxSlope = 50.0f;     // degrees
        float StepHeight = 0.35f;
        float Mass = 70.0f;
        float GravityScale = 1.0f;
        bool StickToFloor = true;

        // control (set by scripts)
        glm::vec3 MoveVelocity{ 0.0f }; // horizontal, world space
        float VerticalVelocity = 0.0f;

        // runtime (read by scripts)
        bool Grounded = false;
        uint32_t RuntimeID = 0xffffffff;

        void Move(const glm::vec3& velocity) { MoveVelocity = { velocity.x, 0.0f, velocity.z }; }
        bool Jump(float speed)
        {
            if (!Grounded) return false;
            VerticalVelocity = speed;
            Grounded = false;
            return true;
        }
    };

    // ------------------------------------------------------------------ tilemap (2D)
    // Grid of tiles from a tileset texture. The entity position is the bottom-left corner.
    struct TilemapComponent
    {
        std::string TilesetPath;
        Ref<Texture2D> Tileset;
        bool PixelArt = true;
        int TilesetColumns = 8;
        int TilesetRows = 4;

        int Width = 32;
        int Height = 12;
        float TileSize = 1.0f;
        std::vector<int> Tiles; // Width * Height, row-major, y = 0 is the bottom row, -1 = empty
        glm::vec4 Color{ 1.0f };
        int SortingOrder = -1;

        bool Collision = true;   // every non-empty tile is solid (static 2D physics)
        float Friction = 0.6f;

        void* RuntimeBody = nullptr; // b2Body*

        void EnsureSize()
        {
            Width = std::max(1, Width); Height = std::max(1, Height);
            if ((int)Tiles.size() != Width * Height) Tiles.resize((size_t)Width * Height, -1);
        }
        int Get(int x, int y) const
        {
            if (x < 0 || y < 0 || x >= Width || y >= Height || (int)Tiles.size() != Width * Height) return -1;
            return Tiles[(size_t)y * Width + x];
        }
        void Set(int x, int y, int tile)
        {
            EnsureSize();
            if (x < 0 || y < 0 || x >= Width || y >= Height) return;
            Tiles[(size_t)y * Width + x] = tile;
        }
        // Resize keeping existing tiles anchored at the bottom-left.
        void Resize(int w, int h)
        {
            w = std::max(1, w); h = std::max(1, h);
            std::vector<int> t((size_t)w * h, -1);
            for (int y = 0; y < std::min(h, Height); y++)
                for (int x = 0; x < std::min(w, Width); x++)
                    t[(size_t)y * w + x] = Get(x, y);
            Width = w; Height = h; Tiles = std::move(t);
        }
    };

    // ------------------------------------------------------------------ particles
    struct Particle
    {
        glm::vec3 Position{ 0.0f };
        glm::vec3 Velocity{ 0.0f };
        float Rotation = 0.0f;
        float AngularVelocity = 0.0f;
        float Life = 0.0f;      // remaining
        float Lifetime = 1.0f;  // total
    };

    struct ParticleSystemComponent
    {
        enum class EmitterShape { Point = 0, Circle, Sphere, Box };

        bool Playing = true;
        bool Looping = true;
        bool PlayInEditor = true;
        float EmissionRate = 30.0f;  // particles / second
        int Burst = 0;               // particles emitted once at start
        int MaxParticles = 1000;

        EmitterShape Shape = EmitterShape::Point;
        float ShapeRadius = 0.5f;
        glm::vec3 ShapeExtents{ 0.5f };

        float LifetimeMin = 0.8f, LifetimeMax = 1.6f;
        float SpeedMin = 1.0f, SpeedMax = 3.0f;
        glm::vec3 Direction{ 0.0f, 1.0f, 0.0f };
        float Spread = 25.0f;        // degrees
        glm::vec3 Gravity{ 0.0f, 0.0f, 0.0f };
        float Drag = 0.0f;
        float AngularVelocityMax = 0.0f; // radians / second

        glm::vec4 StartColor{ 1.0f, 0.85f, 0.35f, 1.0f };
        glm::vec4 EndColor{ 1.0f, 0.25f, 0.05f, 0.0f };
        float StartSize = 0.35f, EndSize = 0.05f;

        bool RenderAsCircle = true;
        bool Additive = true;
        bool WorldSpace = true;      // particles stay in the world when the emitter moves
        bool Billboard = true;       // face the camera (3D)
        std::string TexturePath;
        Ref<Texture2D> Texture;
        int SortingOrder = 0;

        // runtime
        std::vector<Particle> Particles;
        float EmitAccumulator = 0.0f;
        bool BurstDone = false;
        uint32_t RandomState = 0x9E3779B9u;

        void Clear() { Particles.clear(); EmitAccumulator = 0.0f; BurstDone = false; }
    };

    // ------------------------------------------------------------------ screen-space UI
    // Entities with a UITransform are drawn in screen space (pixels) on top of the scene,
    // instead of in the world. Works with Sprite/Circle/Text renderers.
    struct UITransformComponent
    {
        enum class AnchorPoint { TopLeft = 0, Top, TopRight, Left, Center, Right, BottomLeft, Bottom, BottomRight };
        AnchorPoint Anchor = AnchorPoint::TopLeft;
        glm::vec2 Position{ 100.0f, -60.0f }; // pixels from the anchor (+x right, +y up)
        glm::vec2 Size{ 200.0f, 60.0f };      // pixels
        float FontSize = 0.0f;                // text height in pixels (0 = Size.y)
        int Order = 0;
    };

    struct UIButtonComponent
    {
        glm::vec4 NormalColor{ 0.16f, 0.62f, 0.50f, 1.0f };
        glm::vec4 HoverColor{ 0.22f, 0.74f, 0.60f, 1.0f };
        glm::vec4 PressedColor{ 0.10f, 0.45f, 0.36f, 1.0f };
        bool Interactable = true;

        // runtime state (read by scripts)
        bool Hovered = false;
        bool Pressed = false;
        bool Clicked = false; // true for one frame
    };

    // ------------------------------------------------------------------ physics 2D
    struct Rigidbody2DComponent
    {
        enum class BodyType { Static = 0, Dynamic, Kinematic };
        BodyType Type = BodyType::Dynamic;
        bool FixedRotation = false;
        float GravityScale = 1.0f;
        float LinearDamping = 0.0f;
        float AngularDamping = 0.01f;
        bool Bullet = false;

        void* RuntimeBody = nullptr; // b2Body*
    };

    struct BoxCollider2DComponent
    {
        glm::vec2 Offset = { 0.0f, 0.0f };
        glm::vec2 Size = { 0.5f, 0.5f }; // half extents (multiplied by transform scale)

        float Density = 1.0f;
        float Friction = 0.5f;
        float Restitution = 0.0f;
        float RestitutionThreshold = 0.5f;
        bool IsSensor = false;

        void* RuntimeFixture = nullptr;
    };

    struct CircleCollider2DComponent
    {
        glm::vec2 Offset = { 0.0f, 0.0f };
        float Radius = 0.5f;

        float Density = 1.0f;
        float Friction = 0.5f;
        float Restitution = 0.0f;
        float RestitutionThreshold = 0.5f;
        bool IsSensor = false;

        void* RuntimeFixture = nullptr;
    };

    // ------------------------------------------------------------------ physics 3D (Jolt)
    struct Rigidbody3DComponent
    {
        enum class BodyType { Static = 0, Dynamic, Kinematic };
        BodyType Type = BodyType::Dynamic;
        float Mass = 1.0f;
        float LinearDamping = 0.05f;
        float AngularDamping = 0.05f;
        float GravityFactor = 1.0f;
        bool LockRotation = false; // keep upright (characters)

        uint32_t RuntimeBodyID = 0xffffffff; // JPH::BodyID
    };

    struct BoxCollider3DComponent
    {
        glm::vec3 HalfExtents = { 0.5f, 0.5f, 0.5f }; // multiplied by transform scale
        glm::vec3 Offset = { 0.0f, 0.0f, 0.0f };
        float Friction = 0.5f;
        float Restitution = 0.0f;
        bool IsSensor = false;
    };

    struct SphereCollider3DComponent
    {
        float Radius = 0.5f;
        glm::vec3 Offset = { 0.0f, 0.0f, 0.0f };
        float Friction = 0.5f;
        float Restitution = 0.0f;
        bool IsSensor = false;
    };

    struct CapsuleCollider3DComponent
    {
        float Radius = 0.5f;
        float HalfHeight = 0.5f; // cylinder part, along Y
        glm::vec3 Offset = { 0.0f, 0.0f, 0.0f };
        float Friction = 0.5f;
        float Restitution = 0.0f;
        bool IsSensor = false;
    };

    // ------------------------------------------------------------------ audio
    struct AudioSourceComponent
    {
        std::string Path;     // relative to assets (.wav / .mp3 / .flac / .ogg)
        float Volume = 1.0f;
        float Pitch = 1.0f;
        bool Loop = false;
        bool PlayOnStart = true;
        bool Spatial = false;      // 3D sound: volume/pan depend on the listener (primary camera)
        float MinDistance = 1.0f;
        float MaxDistance = 30.0f;

        uint32_t RuntimeHandle = 0;
    };

    // ------------------------------------------------------------------ scripting (C++)
    struct NativeScriptComponent
    {
        std::string ClassName;  // registered with ZM_REGISTER_SCRIPT
        std::unordered_map<std::string, ScriptFieldValue> Fields; // inspector-edited values
        // A disabled script is created (OnCreate) but gets no OnEnable / OnStart / OnUpdate /
        // OnLateUpdate / timers until it is enabled. At runtime prefer
        // ScriptableEntity::SetEnabled (callbacks run at once); a direct write is picked up
        // at the start of the next frame.
        bool Enabled = true;
        ScriptableEntity* Instance = nullptr;

        // A copy is a new entity's component: it gets its own instance at runtime.
        NativeScriptComponent() = default;
        NativeScriptComponent(const NativeScriptComponent& other)
            : ClassName(other.ClassName), Fields(other.Fields), Enabled(other.Enabled), Instance(nullptr) {}
        // Assigning onto an existing component changes its data, not the script the entity
        // is running: a live instance stays (and is destroyed by the scene as usual), it is
        // never dropped here. Entity::AddOrReplaceComponent destroys it first.
        NativeScriptComponent& operator=(const NativeScriptComponent& other)
        {
            ClassName = other.ClassName;
            Fields = other.Fields;
            Enabled = other.Enabled;
            return *this;
        }
        // A move is the same component changing places (entt fills the hole left by a removed
        // component with the last one, sorting swaps): the live instance moves along with it.
        // entt only move-assigns into elements it has just moved out of; the one other case,
        // a replace with a fresh value (registry.replace), brings no instance and keeps ours.
        NativeScriptComponent(NativeScriptComponent&& other) noexcept
            : ClassName(std::move(other.ClassName)), Fields(std::move(other.Fields)), Enabled(other.Enabled), Instance(other.Instance)
        {
            other.Instance = nullptr;
        }
        NativeScriptComponent& operator=(NativeScriptComponent&& other) noexcept
        {
            if (this != &other)
            {
                ClassName = std::move(other.ClassName);
                Fields = std::move(other.Fields);
                Enabled = other.Enabled;
                if (other.Instance)
                {
                    Instance = other.Instance;
                    other.Instance = nullptr;
                }
            }
            return *this;
        }
    };

    // ------------------------------------------------------------------ editor-only data
    // What the editor remembers about an entity: the hierarchy's eye / lock buttons and the
    // inspector's component order. Saved with the scene (only when something is set), copied
    // with the entity (duplicate, copy / paste, play-mode copy; prefab instances and script
    // Instantiate() clones start without Hidden / Locked); the game ignores it. Hidden and
    // Locked apply to the whole subtree
    // (Scene::IsHiddenInEditor / IsLockedInEditor look at the ancestors too):
    // - Hidden: not drawn in the editor's scene view (edit + simulate) - no mesh, sprite, text,
    //   light, icon or collider outline - so it cannot be clicked there either. Play mode and
    //   the player draw it as usual: it is a view filter, not a gameplay state.
    // - Locked: drawn, but clicks in the viewport go through it and the gizmo does not move it;
    //   it can still be selected and edited in the hierarchy / inspector.
    struct EditorMetadataComponent
    {
        bool Hidden = false;
        bool Locked = false;
        // Inspector order of the component headers (scene-file keys, see ComponentKey<T>());
        // empty = the default order. Transform always stays on top.
        std::vector<std::string> ComponentOrder;

        bool IsDefault() const { return !Hidden && !Locked && ComponentOrder.empty(); }
    };

    template<typename... Component>
    struct ComponentGroup {};

    // Every component that is copied/serialized with an entity (IDComponent & TagComponent are handled separately).
    using AllComponents = ComponentGroup<RelationshipComponent, TagsComponent, TransformComponent, SpriteRendererComponent,
        SpriteAnimationComponent, CircleRendererComponent, TextComponent, MeshRendererComponent, AnimatorComponent, CameraComponent,
        DirectionalLightComponent, PointLightComponent, Rigidbody2DComponent, BoxCollider2DComponent,
        CircleCollider2DComponent, Rigidbody3DComponent, BoxCollider3DComponent, SphereCollider3DComponent,
        CapsuleCollider3DComponent, CharacterController3DComponent, TilemapComponent, ParticleSystemComponent, UITransformComponent, UIButtonComponent,
        AudioSourceComponent, NativeScriptComponent, EditorMetadataComponent>;

}
