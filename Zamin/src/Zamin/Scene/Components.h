#pragma once

#include "Zamin/Audio/AudioMixer.h"
#include "Zamin/Core/UUID.h"
#include "Zamin/Renderer/Texture.h"
#include "Zamin/Renderer/Mesh.h"
#include "Zamin/Renderer/Material.h"
#include "Zamin/Renderer/MaterialAsset.h"
#include "Zamin/Renderer/Model.h"
#include "Zamin/Renderer/Font.h"
#include "Zamin/Renderer/SpriteSheet.h"
#include "Zamin/Scene/Camera2D.h"
#include "Zamin/Scene/PixelPerfect.h"
#include "Zamin/Scene/PropertyAnimation.h"
#include "Zamin/Scene/TintAnimation.h"
#include "Zamin/Scene/SceneCamera.h"
#include "Zamin/Scripting/ScriptField.h"
#include "Zamin/UI/TextEdit.h"
#include "Zamin/UI/TextLayout.h"
#include "Zamin/UI/UIAnimation.h"
#include "Zamin/UI/UILayout.h"
#include "Zamin/UI/UIScroll.h"
#include "Zamin/UI/UIWidgets.h"

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
    // 2D renderers draw back to front by sorting layer (Project settings > Sorting layers: the
    // layer's position in that list; its ID is stored here, 0 = "Default"), then by SortingOrder
    // inside the layer, then by the distance from the camera.
    struct SpriteRendererComponent
    {
        glm::vec4 Color{ 1.0f, 1.0f, 1.0f, 1.0f };
        Ref<Texture2D> Texture;
        std::string TexturePath; // relative to project assets
        // A named frame of the texture's sprite sheet (Sprite Editor, kept in the texture's .zmeta);
        // "" = the whole texture. A name the sheet does not have draws the whole texture as well.
        std::string SpriteName;
        float TilingFactor = 1.0f;     // Simple mode with the whole texture: repeats it N times
        uint32_t SortingLayer = 0;     // SortingLayerList ID
        int SortingOrder = 0;          // order inside the sorting layer
        bool PixelArt = false;   // nearest filtering (crisp pixels)
        bool FlipX = false;
        bool FlipY = false;
        // Lit by the 2D lights (Light2DComponent) and the scene's 2D ambient light; off: drawn as it is.
        bool Lit = false;
        // Simple: the picture on the unit quad (sized by the transform's scale). Sliced (9-slice) /
        // Tiled: the picture fills Size (local units, the scale applies on top) keeping its borders:
        // corners are SliceBorder px / PixelsPerUnit big. All four borders 0 = the frame's border
        // (Sprite Editor). Screen-space UI sprites: Size is the UI rect and 1 px = 1 unit.
        SpriteDrawMode DrawMode = SpriteDrawMode::Simple;
        glm::vec2 Size{ 1.0f };
        glm::vec4 SliceBorder{ 0.0f };  // px: left, right, top, bottom
        float PixelsPerUnit = 100.0f;

        // Effects (drawn by the sprite shader). Hit flash: FlashAmount 0..1 mixes the picture toward
        // FlashColor (Flash() plays one that fades out). Outline: a border OutlineThickness texture
        // pixels wide around the opaque pixels, in OutlineColor (sprites drawn as one piece: Simple,
        // or Sliced / Tiled without borders and repeats - see Scene::CanDrawOutline; the others and
        // repeated textures get the flash only).
        glm::vec4 FlashColor{ 1.0f };
        float FlashAmount = 0.0f;
        glm::vec4 OutlineColor{ 1.0f, 1.0f, 1.0f, 1.0f };
        float OutlineThickness = 0.0f;
        // runtime: the flash Flash() started (cleared when play starts / stops)
        glm::vec4 FlashPlayingColor{ 1.0f };
        float FlashTime = 0.0f, FlashDuration = 0.0f;

        SpriteRendererComponent() = default;
        SpriteRendererComponent(const glm::vec4& color) : Color(color) {}

        bool HasSliceBorder() const { return SliceBorder.x > 0.0f || SliceBorder.y > 0.0f || SliceBorder.z > 0.0f || SliceBorder.w > 0.0f; }

        // Starts a flash: the sprite turns `color` (its alpha: how strongly) at once and fades back
        // over `duration` seconds (the game's time: it waits while paused).
        void Flash(const glm::vec4& color = glm::vec4(1.0f), float duration = 0.15f)
        {
            FlashPlayingColor = color;
            FlashDuration = std::max(0.0f, duration);
            FlashTime = FlashDuration;
        }
        bool IsFlashing() const { return FlashTime > 0.0f && FlashDuration > 0.0f; }
        void StopFlash() { FlashTime = FlashDuration = 0.0f; }
        // Advances the playing flash.
        void UpdateFlash(float dt) { if (FlashTime > 0.0f) FlashTime = std::max(0.0f, FlashTime - dt); }
        // What is drawn now: the playing flash (fading out) while it is stronger, else the constant one.
        float GetFlashAmount() const
        {
            float constant = std::clamp(FlashAmount, 0.0f, 1.0f) * FlashColor.a;
            float playing = IsFlashing() ? std::clamp(FlashTime / FlashDuration, 0.0f, 1.0f) * FlashPlayingColor.a : 0.0f;
            return std::max(constant, playing);
        }
        glm::vec3 GetFlashColor() const
        {
            float constant = std::clamp(FlashAmount, 0.0f, 1.0f) * FlashColor.a;
            float playing = IsFlashing() ? std::clamp(FlashTime / FlashDuration, 0.0f, 1.0f) * FlashPlayingColor.a : 0.0f;
            return playing > constant ? glm::vec3(FlashPlayingColor) : glm::vec3(FlashColor);
        }
        bool HasEffects() const { return GetFlashAmount() > 0.0f || (OutlineThickness > 0.0f && OutlineColor.a > 0.0f); }
    };

    // Plays frames on the SpriteRenderer texture: cells of a grid (Columns x Rows, numbered left to
    // right, top to bottom) or, when Frames is set, named frames of the texture's sprite sheet
    // (StartFrame / FrameCount then pick from that list).
    struct SpriteAnimationComponent
    {
        int Columns = 4;
        int Rows = 1;
        int StartFrame = 0;
        int FrameCount = 4;
        float FPS = 10.0f;
        bool Loop = true;
        bool Playing = true;
        std::vector<std::string> Frames; // sprite sheet frame names ("" list = the grid)

        // runtime
        float Time = 0.0f;

        void Play(int startFrame, int frameCount, float fps = 10.0f, bool loop = true)
        {
            if (StartFrame == startFrame && FrameCount == frameCount && Playing) return;
            StartFrame = startFrame; FrameCount = frameCount; FPS = fps; Loop = loop; Playing = true; Time = 0.0f;
        }
        // Plays a list of sprite sheet frames (e.g. SpriteSheet::FindSequence("run")).
        void PlayFrames(const std::vector<std::string>& frames, float fps = 10.0f, bool loop = true)
        {
            if (Frames == frames && StartFrame == 0 && FrameCount == (int)frames.size() && Playing) return;
            Frames = frames; StartFrame = 0; FrameCount = std::max(1, (int)frames.size());
            FPS = fps; Loop = loop; Playing = true; Time = 0.0f;
        }
        bool UsesFrameList() const { return !Frames.empty(); }
        // The frame name shown now ("" for a grid animation).
        const std::string& GetCurrentFrameName() const
        {
            static const std::string s_None;
            if (Frames.empty()) return s_None;
            return Frames[(size_t)std::clamp(GetCurrentFrame(), 0, (int)Frames.size() - 1)];
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

    // Animates the colour of the entity's 2D renderer - Sprite Renderer, Circle Renderer, Text or
    // Tilemap - and of its Light 2D: what is drawn is the renderer's own colour times the gradient's
    // colour (Keys, 0..1 of Duration) at the animation's time; the renderer's Color itself does not
    // change. Mode: Once (start -> end), Loop, PingPong (there and back); Cycles > 0 stops it after
    // that many cycles. StartOffset seconds are skipped at the start (copies out of step). The
    // editor shows the colour the game starts with (the inspector can preview it playing).
    struct TintAnimationComponent
    {
        std::vector<TintKey> Keys{ TintKey{ 0.0f, glm::vec4(1.0f) }, TintKey{ 1.0f, glm::vec4(1.0f, 0.35f, 0.3f, 1.0f) } };
        float Duration = 1.0f;
        TintPlayMode Mode = TintPlayMode::PingPong;
        Ease Easing = Ease::InOutQuad;
        int Cycles = 0;            // Loop / PingPong: 0 = endless
        float StartOffset = 0.0f;  // seconds
        bool PlayOnStart = true;

        // runtime (reset when play starts / stops and for spawned copies)
        bool Playing = false;
        bool EditorPreview = false; // the inspector's preview in the editor's scene view
        float Elapsed = 0.0f;

        void Play(bool restart = true) { if (restart) Elapsed = 0.0f; Playing = true; }
        void Stop() { Playing = false; }
        // Back to the start colour (and stopped).
        void Reset() { Playing = false; Elapsed = 0.0f; }
        void ResetRuntime() { Playing = PlayOnStart; EditorPreview = false; Elapsed = 0.0f; }
        void Update(float dt)
        {
            Elapsed += dt;
            if (Playing && TintAnimation::IsFinished(Mode, Duration, Cycles, Elapsed + StartOffset))
                Playing = false; // stays on its last colour
        }
        bool IsFinished() const { return TintAnimation::IsFinished(Mode, Duration, Cycles, Elapsed + StartOffset); }
        float GetPhase() const { return TintAnimation::GetPhase(Mode, Easing, Duration, Cycles, Elapsed + StartOffset); }
        // The colour the renderer's colour is multiplied by now.
        glm::vec4 GetColor() const { return TintAnimation::EvaluateGradient(Keys, GetPhase()); }
    };

    struct CircleRendererComponent
    {
        glm::vec4 Color{ 1.0f, 1.0f, 1.0f, 1.0f };
        float Thickness = 1.0f;
        float Fade = 0.005f;
        uint32_t SortingLayer = 0;
        int SortingOrder = 0;
    };

    // Text. With a UI Transform it is placed in the UI rectangle (its font size: the UI Transform's
    // FontSize, else the rectangle's height); in the world it is placed around the entity's origin
    // (1 unit = the font size; MaxWidth: the box it wraps / is cut in).
    struct TextComponent
    {
        std::string Text = "Matn";
        glm::vec4 Color{ 1.0f };
        float Kerning = 0.0f;       // em added after every character
        float LineSpacing = 0.0f;   // em added to every line's height
        TextAlignH AlignH = TextAlignH::Left;
        TextAlignV AlignV = TextAlignV::Middle;
        bool WordWrap = false;      // lines break between words at the rectangle's width (world: MaxWidth)
        TextOverflow Overflow = TextOverflow::Overflow; // what does not fit the rectangle (world: MaxWidth)
        float MaxWidth = 0.0f;      // world-space text: the box's width in text units (0: none)
        std::string FontPath; // empty = default engine font
        Ref<Font> FontAsset;
        uint32_t SortingLayer = 0;
        int SortingOrder = 1000; // text draws over the sprites of its layer unless told otherwise

        TextLayoutSettings GetLayoutSettings() const { return { AlignH, AlignV, WordWrap, Overflow, Kerning, LineSpacing }; }
        // Laid out in a UI rectangle, `px` high.
        TextLayoutResult Layout(const UIRect& rect, float px) const
        {
            return TextLayout::Layout(Text, TextMetrics::Of(FontAsset.get()), px, rect, GetLayoutSettings());
        }
        // In the world: around the origin, in text units (1 = the font size); MaxWidth wide when set
        // (starting at the origin, centred on it or ending there - AlignH).
        TextLayoutResult LayoutWorld() const
        {
            UIRect box;
            if (MaxWidth > 0.0f)
            {
                const float x0 = AlignH == TextAlignH::Left ? 0.0f : AlignH == TextAlignH::Center ? -MaxWidth * 0.5f : -MaxWidth;
                box = { { x0, 0.0f }, { x0 + MaxWidth, 0.0f } };
            }
            return TextLayout::Layout(Text, TextMetrics::Of(FontAsset.get()), 1.0f, box, GetLayoutSettings());
        }
    };

    struct MeshRendererComponent
    {
        PrimitiveMesh Type = PrimitiveMesh::Cube;
        std::string ModelPath;    // .gltf / .glb / .obj (when Type == Model)
        Ref<Mesh> MeshAsset;      // runtime cache (primitives)
        Ref<Model> ModelAsset;    // runtime cache (models)
        // Material asset (.zmat, relative to assets). When set, the primitive and every submesh of
        // the model are drawn with it and the inline material below is not used.
        std::string MaterialPath;
        // Models: a material asset per submesh (index = submesh, "" = MaterialPath or, without
        // one, the model's own material with the inline tint).
        std::vector<std::string> SubmeshMaterials;
        Ref<MaterialAsset> MaterialRef;                     // runtime cache of MaterialPath
        std::vector<Ref<MaterialAsset>> SubmeshMaterialRefs; // runtime cache of SubmeshMaterials
        // Runtime only (never saved; cleared when play starts and stops): this entity's own copies
        // made by scripts (ScriptableEntity::GetMaterial), so their edits leave the shared asset
        // and every other entity alone. MaterialInstance takes the place of the main material -
        // the material asset, or without one the inline material (models: the tint of every part);
        // SubmeshMaterialInstances[i] takes the place of part i's whole material.
        Ref<MaterialAsset> MaterialInstance;
        std::vector<Ref<MaterialAsset>> SubmeshMaterialInstances;
        Material Mat;             // inline material: primitives: the material; models: tint/overrides
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

    // A moment of a clip that tells the entity's script (ScriptableEntity::OnAnimationEvent) and the
    // scene's event bus ("AnimationEvent") - footsteps, hits, sounds. Time is 0..1 of the clip.
    struct AnimatorEvent
    {
        std::string Clip;       // animation name ("" = every clip)
        float Time = 0.0f;      // normalized: 0 = its start, 1 = its end
        std::string Name;
        std::string Parameter;  // optional text handed over with it
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

        // Events of the clips (in play mode).
        std::vector<AnimatorEvent> Events;
        bool EventsAtStart = true; // runtime: the clip just started - events right at its start fire too

        // ---- root motion: the root bone's walk (XZ) and turn (yaw) moves the entity instead of
        // the mesh drifting away from it; the pose keeps the root in place (its height stays).
        // With a Character Controller 3D it walks the character (its MoveVelocity), else the
        // transform. The editor's preview stays in place.
        bool ApplyRootMotion = false;
        std::string RootMotionBone;                // joint name ("" = the first animated root joint)
        glm::vec3 RootMotionVelocity{ 0.0f };      // runtime: the last frame's root motion (world units / s)
        float RootMotionTurnRate = 0.0f;           // runtime: radians / s

        // Switch clip with a smooth crossfade (fade < 0 = DefaultFade, 0 = instant).
        void Play(const std::string& clip, bool loop = true, float speed = 1.0f, float fade = -1.0f)
        {
            if (Clip == clip && Playing) { Speed = speed; Loop = loop; return; }
            if (fade < 0.0f) fade = DefaultFade;
            PreviousClip = Clip; PreviousTime = Time; PreviousSpeed = Speed; PreviousLoop = Loop;
            FadeDuration = fade; FadeElapsed = 0.0f;
            Clip = clip; Loop = loop; Speed = speed; Playing = true; Time = 0.0f;
            EventsAtStart = true;
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

    // How an Animation Player plays its clips: as the clip says, or Once / Loop / PingPong.
    enum class AnimationPlayerWrap : uint8_t { Clip = 0, Once, Loop, PingPong };

    // Plays property animation clips (.zanim, made in the Animation window) on the entity and its
    // children: transforms, colours, lights, UI and script float fields, with events at moments of
    // the clip (ScriptableEntity::OnAnimationEvent, the event bus' "AnimationEvent"). The first clip
    // starts with the game (PlayOnStart); scripts play the others by name (the file name without
    // .zanim) or path:
    //   GetComponent<AnimationPlayerComponent>().Play("DoorOpen");      // forwards from the start
    //   GetComponent<AnimationPlayerComponent>().Play("DoorOpen", -1.0f); // backwards from the end
    // A player that is not playing writes nothing, so scripts may change the same values then.
    struct AnimationPlayerComponent
    {
        std::vector<std::string> Clips;           // .zanim files, relative to Assets
        float Speed = 1.0f;                       // times the speed each Play() gives
        bool PlayOnStart = true;                  // the first clip
        AnimationPlayerWrap Wrap = AnimationPlayerWrap::Clip;

        // runtime (reset when play starts / stops and for spawned copies)
        std::vector<Ref<PropertyClipAsset>> ClipAssets; // the loaded Clips (ResolveClips)
        std::vector<std::string> LoadedClips;     // the paths ClipAssets were looked up for
        int Current = -1;                         // index into Clips (-1: none)
        ClipCursor Cursor;
        float PlaySpeed = 1.0f;                   // the speed the last Play() was given
        bool Playing = false;
        bool Started = false;                     // PlayOnStart was looked at
        bool PoseDirty = false;                   // SetTime / Play / Stop: the pose is written on the next update

        // Loads the clips that changed (Clips edited) - Play and the scene call it.
        void ResolveClips();
        // The clip with this name (file name without .zanim) or path; -1 when there is none.
        int FindClip(std::string_view nameOrPath) const;
        // Plays a clip ("" = the current one, else the first) from its start (speed < 0: backwards
        // from its end). False when there is no such clip.
        bool Play(std::string_view clip = {}, float speed = 1.0f);
        bool Play(int index, float speed = 1.0f);
        // Stops where it is (the values stay); Resume goes on from there.
        void Stop() { Playing = false; }
        void Resume() { if (Current >= 0) { Playing = true; Cursor.Finished = false; } }
        bool IsPlaying() const { return Playing; }
        // Moves to `seconds` into the current clip (the pose is written on the next update, also
        // while stopped).
        void SetTime(float seconds);
        float GetTime() const { return Cursor.Time; }
        float GetDuration() const;
        float GetNormalizedTime() const { float d = GetDuration(); return d > 0.0f ? Cursor.Time / d : 0.0f; }
        const PropertyClip* GetCurrentClip() const;
        std::string GetCurrentClipName() const;
        AnimationWrap GetWrap() const;
        void ResetRuntime()
        {
            Current = -1; Cursor = {}; PlaySpeed = 1.0f; Playing = false; Started = false; PoseDirty = false;
        }
    };

    struct CameraComponent
    {
        SceneCamera Camera;
        bool Primary = true;
        bool FixedAspectRatio = false;
        glm::vec4 ClearColor{ 0.08f, 0.09f, 0.11f, 1.0f };
        // Scene::ShakeCamera / ScriptableEntity::ShakeCamera shake the view of the camera the game
        // is seen through; its strength settings are saved, the trauma is runtime only.
        CameraShake Shake;
        // Orthographic cameras: whole screen pixels per texture pixel (see PixelPerfectSettings);
        // the game view (play) only - the editor's scene view is not changed.
        PixelPerfectSettings PixelPerfect;
        bool IsPixelPerfect() const { return PixelPerfect.Enabled && Camera.GetProjectionType() == SceneCamera::ProjectionType::Orthographic; }
    };

    // ------------------------------------------------------------------ 2D camera follow
    // Moves its entity (usually the camera) after Target while the game runs: once per frame,
    // after physics and the scripts' OnLateUpdate; only x / y change (z stays). See Camera2DFollow.
    // - Offset: added to the target's position (look a little above the player);
    // - Damping: seconds to catch up about 63% of the way, per axis (0 = locked to the target);
    // - DeadZone: a box (full width / height, units) around the view's centre inside which the
    //   target moves without the camera moving;
    // - LookAhead: how far (units, per axis) the view runs ahead in the direction the target last
    //   moved, eased in with LookAheadDamping seconds;
    // - Bounds: the camera's whole view (at the target's depth) stays inside this world rectangle.
    struct Camera2DFollowComponent
    {
        EntityRef Target;
        glm::vec2 Offset{ 0.0f, 1.0f };
        glm::vec2 Damping{ 0.2f, 0.3f };
        glm::vec2 DeadZone{ 1.0f, 1.5f };
        glm::vec2 LookAhead{ 1.5f, 0.0f };
        float LookAheadDamping = 0.6f;
        float LookAheadMinSpeed = 0.5f;   // units / s the target must move before the look-ahead turns
        bool UseBounds = false;
        glm::vec2 BoundsMin{ -20.0f, -10.0f };
        glm::vec2 BoundsMax{ 20.0f, 10.0f };
        bool SnapOnStart = true;          // play starts with the camera on the target

        // runtime (cleared when play starts / stops)
        bool Initialized = false;
        bool SnapRequested = false;
        glm::vec2 LastTarget{ 0.0f };
        glm::vec2 LookAheadDirection{ 0.0f }; // -1, 0 or 1 per axis
        glm::vec2 LookAheadOffset{ 0.0f };

        // Jumps onto the target on the next update (after a teleport / respawn).
        void Snap() { SnapRequested = true; }
        void ResetRuntime()
        {
            Initialized = SnapRequested = false;
            LastTarget = LookAheadDirection = LookAheadOffset = glm::vec2(0.0f);
        }
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

    // A cone of light from the entity's position along its forward axis (-Z, like the directional
    // light). Angles are measured from the axis in degrees: full brightness inside InnerAngle,
    // fading smoothly to nothing at OuterAngle. Range attenuates like a point light's radius.
    struct SpotLightComponent
    {
        glm::vec3 Color{ 1.0f };
        float Intensity = 20.0f;
        float Range = 15.0f;
        float InnerAngle = 20.0f;
        float OuterAngle = 30.0f;

        static constexpr float MaxAngle = 89.0f;
        // The angles the renderer uses: outer in (0, MaxAngle], inner in [0, outer].
        float GetOuterAngle() const { return std::clamp(OuterAngle, 0.1f, MaxAngle); }
        float GetInnerAngle() const { return std::clamp(InnerAngle, 0.0f, GetOuterAngle()); }
    };

    // ------------------------------------------------------------------ 2D lights
    // A light for 2D scenes. It lights the Lit sprites and tilemaps (their "Lit" flag) within Radius
    // of it: fully inside InnerRadius, then fading out ((1 - t)^Falloff: 1 linear, 2 soft, 4 a bright
    // core). Spot: a cone SpotAngle degrees wide along the entity's +X axis (its rotation turns it),
    // the outer SpotSoftness of it fading. Lights add up on top of the scene's 2D ambient light
    // (Scene settings > 2D lighting); renderers that are not Lit are drawn as before. Flicker
    // makes the intensity waver (torches, fire).
    enum class Light2DType { Point = 0, Spot };
    struct Light2DComponent
    {
        Light2DType Type = Light2DType::Point;
        glm::vec3 Color{ 1.0f, 0.85f, 0.6f };
        float Intensity = 1.0f;
        float Radius = 4.0f;
        float InnerRadius = 0.0f;
        float Falloff = 2.0f;
        float SpotAngle = 60.0f;    // degrees, the whole cone
        float SpotSoftness = 0.3f;  // 0..1 of the cone's half angle that fades
        float Flicker = 0.0f;       // 0..1
        float FlickerSpeed = 8.0f;  // noise samples per second
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
        // Its collision layer (0..15, Project settings > Physics layers - the same layers and matrix
        // as 2D physics): which bodies it walks into / stands on.
        int Layer = 0;
        // Walking into a dynamic body pushes it along (towards the character's speed where they touch)
        // with at most PushForce newtons: 100 N moves a crate of a few dozen kg, a heavy one needs
        // more. PushBodies off: dynamic bodies are walls to it (they still push the character).
        bool PushBodies = true;
        float PushForce = 100.0f; // N

        // control (set by scripts)
        glm::vec3 MoveVelocity{ 0.0f }; // horizontal, world space
        float VerticalVelocity = 0.0f;

        // runtime (read by scripts)
        bool Grounded = false;
        // How fast it really moved in the last physics step (after collisions, stairs, sticking
        // to the floor): the change of its position / the step.
        glm::vec3 Velocity{ 0.0f };
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
    // One layer of a tilemap: its own grid of tiles over the tilemap's Width x Height. Layers draw
    // in list order (the first at the back) unless their SortingOrder says otherwise.
    struct TilemapLayer
    {
        std::string Name = "Asosiy";
        std::vector<int> Tiles;   // Width * Height, row-major, y = 0 is the bottom row, -1 = empty
        bool Visible = true;      // drawn (editor and game)
        bool Collision = true;    // its tiles are solid (when the tilemap's Collision is on)
        int SortingOrder = 0;     // added to the tilemap's SortingOrder (e.g. +2: in front of the sprites)
        float Opacity = 1.0f;     // multiplies the tilemap colour's alpha

        bool operator==(const TilemapLayer&) const = default;
    };

    // Grid of tiles from a tileset texture. The entity position is the bottom-left corner. Scene
    // files from before layers existed load as one layer (layer 0).
    struct TilemapComponent
    {
        static constexpr int MaxLayers = 32;

        std::string TilesetPath;
        Ref<Texture2D> Tileset;
        bool PixelArt = true;
        int TilesetColumns = 8;
        int TilesetRows = 4;

        int Width = 32;
        int Height = 12;
        float TileSize = 1.0f;
        std::vector<TilemapLayer> Layers = std::vector<TilemapLayer>(1); // at least one
        glm::vec4 Color{ 1.0f };
        uint32_t SortingLayer = 0;
        int SortingOrder = -1;
        bool Lit = false;        // lit by the 2D lights (Light2DComponent) and the scene's 2D ambient

        bool Collision = true;   // the tiles of the layers with Collision are solid (static 2D physics)
        float Friction = 0.6f;
        float Restitution = 0.0f;
        std::string MaterialPath; // physics material asset (.zphysmat), "" = Friction / Restitution above
        int CollisionLayer = 0;   // the 2D collision layer of the tiles (Project settings > Physics layers)

        void* RuntimeBody = nullptr; // b2Body*

        // Width / Height at least 1, at least one layer, every layer's grid Width * Height big.
        void EnsureSize()
        {
            Width = std::max(1, Width); Height = std::max(1, Height);
            if (Layers.empty()) Layers.emplace_back();
            for (TilemapLayer& layer : Layers)
                if ((int)layer.Tiles.size() != Width * Height) layer.Tiles.resize((size_t)Width * Height, -1);
        }
        int GetLayerCount() const { return (int)Layers.size(); }
        bool IsValidLayer(int layer) const { return layer >= 0 && layer < (int)Layers.size(); }
        TilemapLayer* GetLayer(int layer) { return IsValidLayer(layer) ? &Layers[(size_t)layer] : nullptr; }
        const TilemapLayer* GetLayer(int layer) const { return IsValidLayer(layer) ? &Layers[(size_t)layer] : nullptr; }
        bool Contains(int x, int y) const { return x >= 0 && y >= 0 && x < Width && y < Height; }
        // The tile at (x, y) of a layer: -1 for empty cells, cells outside the map and unknown layers.
        int Get(int x, int y, int layer = 0) const
        {
            const TilemapLayer* l = GetLayer(layer);
            if (!l || !Contains(x, y) || (int)l->Tiles.size() != Width * Height) return -1;
            return l->Tiles[(size_t)y * Width + x];
        }
        void Set(int x, int y, int tile, int layer = 0)
        {
            EnsureSize();
            if (!IsValidLayer(layer) || !Contains(x, y)) return;
            Layers[(size_t)layer].Tiles[(size_t)y * Width + x] = tile < 0 ? -1 : tile;
        }
        // A tile on any layer with Collision (what the physics body is built from).
        bool IsSolid(int x, int y) const
        {
            for (int i = 0; i < (int)Layers.size(); i++)
                if (Layers[(size_t)i].Collision && Get(x, y, i) >= 0) return true;
            return false;
        }
        // Resize keeping existing tiles anchored at the bottom-left (every layer).
        void Resize(int w, int h)
        {
            w = std::max(1, w); h = std::max(1, h);
            if (Layers.empty()) Layers.emplace_back();
            for (int i = 0; i < (int)Layers.size(); i++)
            {
                std::vector<int> t((size_t)w * h, -1);
                for (int y = 0; y < std::min(h, Height); y++)
                    for (int x = 0; x < std::min(w, Width); x++)
                        t[(size_t)y * w + x] = Get(x, y, i);
                Layers[(size_t)i].Tiles = std::move(t);
            }
            Width = w; Height = h;
        }

        // ---- layers
        int FindLayer(std::string_view name) const
        {
            for (int i = 0; i < (int)Layers.size(); i++)
                if (Layers[(size_t)i].Name == name) return i;
            return -1;
        }
        // `base`, or "<base> <n>" with the first n no layer uses.
        std::string MakeUniqueLayerName(std::string_view base) const
        {
            std::string name(base.empty() ? std::string_view("Qatlam") : base);
            if (FindLayer(name) < 0) return name;
            for (int n = 2;; n++)
            {
                std::string candidate = name + " " + std::to_string(n);
                if (FindLayer(candidate) < 0) return candidate;
            }
        }
        // A new empty layer on top (drawn last), without collision. Returns its index, -1 when the
        // tilemap has MaxLayers already.
        int AddLayer(std::string_view name = "Qatlam")
        {
            EnsureSize();
            if ((int)Layers.size() >= MaxLayers) return -1;
            TilemapLayer layer;
            layer.Name = MakeUniqueLayerName(name);
            layer.Collision = false;
            layer.Tiles.assign((size_t)Width * Height, -1);
            Layers.push_back(std::move(layer));
            return (int)Layers.size() - 1;
        }
        // Every layer but the last one left can be removed.
        bool RemoveLayer(int layer)
        {
            if (!IsValidLayer(layer) || Layers.size() <= 1) return false;
            Layers.erase(Layers.begin() + layer);
            return true;
        }
        // Moves the layer at `from` to position `to` (drawing order).
        bool MoveLayer(int from, int to)
        {
            if (!IsValidLayer(from) || !IsValidLayer(to) || from == to) return false;
            TilemapLayer moved = std::move(Layers[(size_t)from]);
            Layers.erase(Layers.begin() + from);
            Layers.insert(Layers.begin() + to, std::move(moved));
            return true;
        }
        // Number of non-empty cells of a layer.
        size_t CountTiles(int layer) const
        {
            const TilemapLayer* l = GetLayer(layer);
            return l ? (size_t)std::count_if(l->Tiles.begin(), l->Tiles.end(), [](int t) { return t >= 0; }) : 0;
        }
    };

    // ------------------------------------------------------------------ parallax (2D)
    // Moves the entity (and its children) with the game camera when the scene is drawn: Factor 0 =
    // an ordinary world object, 1 = fixed on the screen (infinitely far), 0.5 = half as fast as the
    // camera (a far background); negative values move against the camera (a near foreground). It is
    // where it was placed while the camera is at its position. RepeatX / RepeatY draw its sprite
    // again every Spacing units (0 = the sprite's own size) so it fills the view endlessly.
    // Only the game camera moves it (the editor's scene view shows it as placed, unless View >
    // Parallax preview is on); the transform itself never changes, so physics and scripts are not
    // affected.
    struct ParallaxComponent
    {
        glm::vec2 Factor{ 0.5f, 0.5f };
        bool RepeatX = false;
        bool RepeatY = false;
        glm::vec2 Spacing{ 0.0f };
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
        uint32_t SortingLayer = 0;
        int SortingOrder = 0;

        // runtime
        std::vector<Particle> Particles;
        float EmitAccumulator = 0.0f;
        bool BurstDone = false;
        uint32_t RandomState = 0x9E3779B9u;

        void Clear() { Particles.clear(); EmitAccumulator = 0.0f; BurstDone = false; }
    };

    // ------------------------------------------------------------------ screen-space UI
    // Entities with a UITransform are drawn in screen space on top of the scene, instead of in the
    // world (Sprite / Circle / Text renderers and the UI widgets). Its rectangle is laid out in its
    // parent's - the UI Transform of the parent entity, else the whole screen (RectTransform model):
    // - AnchorMin / AnchorMax: 0..1 of the parent's rectangle (0,0 bottom-left, 1,1 top-right).
    //   Equal ones pin it to that point of the parent; different ones stretch it with the parent
    //   along that axis (a bar across the top: AnchorMin 0,1 AnchorMax 1,1);
    // - Pivot: 0..1 of its own rectangle - the point Position places (and that it grows around);
    // - Position: px from the anchor point (stretched: the point between the anchors at the pivot)
    //   to the pivot, +x right, +y up;
    // - Size: px. Along a stretched axis it is added to the anchors' span (0: exactly between
    //   them, -40: 20 px in from each edge with the pivot in the middle) - Left / Right / Top /
    //   Bottom offsets: GetOffsetMin / GetOffsetMax, SetOffsets.
    // UI units are screen pixels unless a UI Canvas scales them (UICanvasComponent). A layout group
    // on the parent (UILayoutGroupComponent) places it instead (its anchors and position then do not
    // count). Scene::GetUIRect gives the rectangle the frame uses.
    struct UITransformComponent
    {
        // The 9 point presets (both anchors at that point of the parent).
        enum class AnchorPoint { TopLeft = 0, Top, TopRight, Left, Center, Right, BottomLeft, Bottom, BottomRight };
        glm::vec2 AnchorMin{ 0.0f, 1.0f };
        glm::vec2 AnchorMax{ 0.0f, 1.0f };
        glm::vec2 Pivot{ 0.5f, 0.5f };
        glm::vec2 Position{ 100.0f, -60.0f };
        glm::vec2 Size{ 200.0f, 60.0f };
        float FontSize = 0.0f;                // text height in UI units (0 = the rect's height)
        int Order = 0;                        // drawn above its lower-Order siblings (children are drawn above their parent)
        // The look, not the layout (UI tweens animate it): drawn Scale times as big around its pivot,
        // VisualOffset UI units away and Alpha opaque - its UI children with it. Clicks follow what is
        // drawn; at Alpha 0 it takes none.
        glm::vec2 Scale{ 1.0f };
        glm::vec2 VisualOffset{ 0.0f };
        float Alpha = 1.0f;

        // Both anchors at a preset point (Position and Size stay: it moves with the new anchor).
        void SetAnchor(AnchorPoint point)
        {
            AnchorMin = AnchorMax = UILayout::PresetPoint((int)point);
        }
        // The preset both anchors are at (-1: they are not at one of the 9 points).
        int GetAnchorPreset() const
        {
            for (int i = 0; i < 9; i++)
                if (AnchorMin == UILayout::PresetPoint(i) && AnchorMax == AnchorMin)
                    return i;
            return -1;
        }
        bool IsStretchedX() const { return AnchorMin.x != AnchorMax.x; }
        bool IsStretchedY() const { return AnchorMin.y != AnchorMax.y; }
        // The corners' offsets from the anchors: OffsetMin = (left, bottom) from AnchorMin,
        // OffsetMax = (right, top) from AnchorMax (inward edges: left > 0, right < 0).
        glm::vec2 GetOffsetMin() const { return Position - Size * Pivot; }
        glm::vec2 GetOffsetMax() const { return Position + Size * (1.0f - Pivot); }
        void SetOffsets(const glm::vec2& offsetMin, const glm::vec2& offsetMax)
        {
            Size = offsetMax - offsetMin;
            Position = offsetMin + Size * Pivot;
        }
        // Stretched over the whole parent, `inset` px in from its edges (left, right, top, bottom).
        void SetStretch(const glm::vec4& inset = glm::vec4(0.0f))
        {
            AnchorMin = { 0.0f, 0.0f };
            AnchorMax = { 1.0f, 1.0f };
            SetOffsets({ inset.x, inset.w }, { -inset.y, -inset.z });
        }
        // The rectangle inside `parent` (UI units).
        UIRect ComputeRect(const UIRect& parent) const { return UILayout::ComputeRect(parent, AnchorMin, AnchorMax, Pivot, Position, Size); }
        // Position / Size so that it has `rect` inside `parent`.
        void SetRect(const UIRect& parent, const UIRect& rect) { UILayout::FromRect(parent, AnchorMin, AnchorMax, Pivot, rect, Position, Size); }
    };

    // A canvas: the UI below it (this entity's subtree - UI Transforms, also under plain entities)
    // is scaled for the screen. ScaleWithScreenSize: made for ReferenceResolution, it grows and
    // shrinks with the screen (MatchWidthOrHeight: 0 follows the width, 1 the height, 0.5 both
    // halfway - in log space; Expand: always fits; Shrink: always fills). ConstantPixelSize: 1 UI
    // unit = ScaleFactor px. Only the outermost canvas of a subtree counts.
    struct UICanvasComponent
    {
        UIScaleMode ScaleMode = UIScaleMode::ScaleWithScreenSize;
        float ScaleFactor = 1.0f;                       // ConstantPixelSize
        glm::vec2 ReferenceResolution{ 1920.0f, 1080.0f };
        UIScreenMatchMode MatchMode = UIScreenMatchMode::MatchWidthOrHeight;
        float MatchWidthOrHeight = 0.5f;

        // runtime: the scale of the last layout (screen px per UI unit)
        float CurrentScale = 1.0f;

        float ComputeScale(const glm::vec2& screen) const
        {
            return UILayout::CanvasScale(screen, ScaleMode, ScaleFactor, ReferenceResolution, MatchMode, MatchWidthOrHeight);
        }
    };

    // Arranges the entity's UI children - in a row (Horizontal), a column (Vertical) or a grid -
    // inside its rectangle, in hierarchy order (inactive children and those whose layout element
    // ignores the layout are left out). Their anchors and Position no longer count; their size
    // too when the group controls it (see UILayoutGroupSettings). FitWidth / FitHeight: the
    // group's own rectangle takes its content's preferred size (a list that grows with its items).
    struct UILayoutGroupComponent : UILayoutGroupSettings
    {
    };

    // What a child of a layout group asks for (-1: not set). Min: the least it is shrunk to;
    // Preferred: its size when there is room (not set: its own size); Flexible: its share of
    // the space left over (relative to its siblings' - 1 and 2: the second gets twice as much).
    // These count where the group controls the children's size. IgnoreLayout: the group leaves it
    // where its own UI Transform puts it.
    struct UILayoutElementComponent
    {
        bool IgnoreLayout = false;
        glm::vec2 MinSize{ -1.0f };
        glm::vec2 PreferredSize{ -1.0f };
        glm::vec2 FlexibleSize{ -1.0f };
    };

    // How keyboard / gamepad navigation leaves this widget (button, slider, toggle, input field):
    // Automatic - to the nearest widget in the pressed direction; Horizontal / Vertical - the same,
    // only left / right or up / down; Explicit - to the widgets set for each direction (empty: none);
    // None - it is not reached by navigation (a click still selects it). Widgets without this
    // component navigate Automatically. SelectOnEnable: it is selected when it becomes active while
    // the game runs (also at the start) - a menu's first button.
    struct UINavigationComponent
    {
        enum class Mode { Automatic = 0, Horizontal, Vertical, Explicit, None };
        Mode NavigationMode = Mode::Automatic;
        EntityRef Up, Down, Left, Right;     // Explicit
        bool SelectOnEnable = false;
        bool ShowHighlight = true;           // the outline around it while selected by keyboard / gamepad
        glm::vec4 HighlightColor{ 1.0f, 0.80f, 0.28f, 1.0f };

        // runtime
        bool WasActive = false;              // SelectOnEnable notices the switch on
    };

    // Animations of a UI element (tweens of its UI Transform's look - see UIAnimation.h):
    // - Show: played when it becomes active while the game runs (at the start too, unless
    //   ShowAtStart is off) - it comes in from its preset's look (faded out, smaller, off to a side);
    // - Hide: Scene::HideUI / ScriptableEntity::HideUI play it, then switch the entity off;
    // - feedback: a button / toggle / slider / input field on the entity grows to HoverScale under
    //   the mouse (or while selected by the keyboard) and shrinks to PressScale while pressed (1: off).
    struct UIAnimationComponent
    {
        UIAnimPreset Show = UIAnimPreset::Pop;
        UIAnimPreset Hide = UIAnimPreset::Fade;
        bool ShowAtStart = true;
        float Duration = 0.25f;                 // seconds (show and hide)
        float ShowDelay = 0.0f;                 // seconds before the show starts (staggered menus)
        Ease ShowEase = Ease::OutBack;
        Ease HideEase = Ease::InQuad;
        float SlideDistance = 160.0f;           // UI units (Slide presets)
        float HoverScale = 1.0f;
        float PressScale = 1.0f;
        float FeedbackDuration = 0.08f;         // seconds to the new feedback scale

        // runtime
        bool WasActive = false;                 // the show notices the switch on
        bool Hiding = false;                    // a hide plays: the entity goes off when it ends
        bool RestSaved = false;                 // RestLook holds the look to come back to
        UIAnimMath::Look RestLook;
        float FeedbackScale = 1.0f, FeedbackFrom = 1.0f, FeedbackTo = 1.0f, FeedbackTime = 1.0f;

        void ResetRuntime()
        {
            WasActive = Hiding = RestSaved = false;
            RestLook = {};
            FeedbackScale = FeedbackFrom = FeedbackTo = FeedbackTime = 1.0f;
        }
    };

    // A clickable rectangle: tinted by its state colours (Normal / Hover / Pressed), OnClick() on
    // the entity's script, "UIClick" on the scene's event bus (sender: the button) and - with a
    // ClickMessage - that script message to ClickTarget (empty: every running script, Broadcast).
    // Enter / gamepad A clicks it while it is selected (keyboard / gamepad navigation).
    struct UIButtonComponent
    {
        glm::vec4 NormalColor{ 0.16f, 0.62f, 0.50f, 1.0f };
        glm::vec4 HoverColor{ 0.22f, 0.74f, 0.60f, 1.0f };
        glm::vec4 PressedColor{ 0.10f, 0.45f, 0.36f, 1.0f };
        bool Interactable = true;
        std::string ClickMessage;            // "" = none
        EntityRef ClickTarget;               // empty: Broadcast

        // runtime state (read by scripts)
        bool Hovered = false;
        bool Pressed = false;
        bool Clicked = false; // true for one frame
    };

    // A picture in the UI rect: a texture or one of its sprite sheet frames ("" = a plain colour),
    // tinted by Color and by a UI Button's state colours. Type: Simple (stretched, or fitted with
    // PreserveAspect), Sliced (9-slice: corners keep their size - border px x BorderScale), Tiled, or
    // Filled (FillAmount 0..1 of it: horizontal, vertical or radial - bars, cooldowns, timers).
    // RaycastTarget: the mouse stops at it - UI widgets drawn below it are not hovered or clicked.
    struct UIImageComponent
    {
        glm::vec4 Color{ 1.0f };
        Ref<Texture2D> Texture;
        std::string TexturePath;       // relative to project assets
        std::string SpriteName;        // a named frame of the texture's sprite sheet ("" = the whole texture)
        UIImageType Type = UIImageType::Simple;
        bool PreserveAspect = false;   // Simple / Filled
        glm::vec4 SliceBorder{ 0.0f }; // Sliced / Tiled: px left, right, top, bottom (all 0 = the frame's border)
        float BorderScale = 1.0f;      // Sliced / Tiled: corner size on screen = border px x this
        UIFillMethod FillMethod = UIFillMethod::Horizontal;
        int FillOrigin = 0;            // Horizontal: 0 left, 1 right; Vertical: 0 bottom, 1 top; Radial: 0 top, 1 right, 2 bottom, 3 left
        float FillAmount = 1.0f;
        bool FillClockwise = true;     // Radial 360
        bool PixelArt = false;         // nearest filtering (crisp pixels)
        bool RaycastTarget = true;

        bool HasSliceBorder() const { return SliceBorder.x > 0.0f || SliceBorder.y > 0.0f || SliceBorder.z > 0.0f || SliceBorder.w > 0.0f; }
    };

    // A value picked by dragging a handle along a track or clicking the track: MinValue..MaxValue
    // (whole numbers only with WholeNumbers; MinValue > MaxValue runs backwards). Drawn over the
    // entity's own image / sprite: the track, its filled part up to the handle, the handle. A change
    // by the user calls OnValueChanged(value) on the entity's script and emits "UISliderChanged"
    // (payload float, sender: the slider) on the scene's event bus; Scene::SetSliderValue sets it from code.
    struct UISliderComponent
    {
        float MinValue = 0.0f;
        float MaxValue = 1.0f;
        float Value = 0.5f;
        bool WholeNumbers = false;
        UIDirection Direction = UIDirection::LeftToRight;
        bool Interactable = true;
        float TrackThickness = 8.0f;   // px (0 = the whole rect)
        float HandleSize = 0.0f;       // px across (0 = the rect's thickness)
        bool Rounded = true;           // round track ends and a round handle (else square)
        glm::vec4 TrackColor{ 0.12f, 0.13f, 0.16f, 1.0f };
        glm::vec4 FillColor{ 0.16f, 0.62f, 0.50f, 1.0f };
        glm::vec4 HandleColor{ 0.92f, 0.94f, 0.96f, 1.0f };

        // runtime state (read by scripts)
        bool Hovered = false;
        bool Dragging = false;
        bool ValueChanged = false;     // true for one frame: the user changed the value

        float GetNormalizedValue() const { return UIMath::Normalize(Value, MinValue, MaxValue); }
        // Value set from 0..1 of the range (rounded with WholeNumbers). No notification.
        void SetNormalizedValue(float t) { Value = UIMath::Denormalize(t, MinValue, MaxValue, WholeNumbers); }
    };

    // An on / off switch: a check box (or a sliding switch) at the left of the rect with the
    // entity's Text as its label right of it; a click switches it. Toggles of the same Group are
    // radio buttons: one on at a time (AllowSwitchOff: the one that is on can be clicked off). A
    // change calls OnToggleChanged(isOn) on the entity's script and emits "UIToggleChanged"
    // (payload bool); Scene::SetToggle sets it from code (the group follows).
    struct UIToggleComponent
    {
        bool IsOn = false;
        bool Interactable = true;
        UIToggleStyle Style = UIToggleStyle::Checkbox;
        UIToggleMark Mark = UIToggleMark::Check;
        float BoxSize = 0.0f;          // px (0 = the rect's height)
        float LabelSpacing = 10.0f;    // px between the box and the label
        std::string Group;             // "" = on its own
        bool AllowSwitchOff = false;
        glm::vec4 BoxColor{ 0.12f, 0.13f, 0.16f, 1.0f };
        glm::vec4 OnColor{ 0.16f, 0.62f, 0.50f, 1.0f };   // the box (switch: the track) when on
        glm::vec4 MarkColor{ 1.0f };                       // the check mark / the switch's knob

        // runtime state (read by scripts)
        bool Hovered = false;
        bool Pressed = false;
        bool ValueChanged = false;     // true for one frame: it was switched
        float Transition = 0.0f;       // 0..1, follows IsOn (the switch's knob slides)
    };

    // A bar filled to Value (0..1) in Direction over its background (drawn over the entity's own
    // image / sprite). UseLowColor: the fill blends from LowColor (empty) to FillColor (full) - a
    // health bar going red. SmoothSpeed > 0: the bar follows Value at that much per second.
    struct UIProgressBarComponent
    {
        float Value = 0.5f;
        UIDirection Direction = UIDirection::LeftToRight;
        glm::vec4 BackgroundColor{ 0.12f, 0.13f, 0.16f, 1.0f };
        glm::vec4 FillColor{ 0.16f, 0.62f, 0.50f, 1.0f };
        bool UseLowColor = false;
        glm::vec4 LowColor{ 0.86f, 0.24f, 0.20f, 1.0f };
        float Padding = 3.0f;          // px between the background's edge and the fill
        float SmoothSpeed = 0.0f;      // per second (0 = at once)
        bool ShowPercentage = false;   // "75%" in the middle
        glm::vec4 TextColor{ 1.0f };

        // runtime: what is drawn (follows Value while the game runs)
        float DisplayValue = 0.5f;

        glm::vec4 GetFillColor(float t) const { return UseLowColor ? glm::mix(LowColor, FillColor, std::clamp(t, 0.0f, 1.0f)) : FillColor; }
    };

    // A single line of text the player types into: click it (or Scene::FocusInputField) to edit -
    // caret, selection (drag, Shift + arrows, double click: everything), Ctrl+A / C / X / V, Home /
    // End, Ctrl + arrows by words; Enter submits, Escape cancels (the text it had before), a click
    // elsewhere ends editing. UTF-8 throughout (oʻ, gʻ, sh, Cyrillic). ContentType filters what can
    // be typed (Integer, Decimal, Alphanumeric) or hides it (Password); CharacterLimit counts
    // characters. Script callbacks: OnTextChanged(text), OnTextSubmit(text), OnTextEndEdit(text)
    // (event bus: "UITextChanged", "UITextSubmitted", "UITextEndEdit", payload std::string).
    struct UIInputFieldComponent
    {
        std::string Text;
        std::string Placeholder = "Matn kiriting...";
        UIContentType ContentType = UIContentType::Standard;
        int CharacterLimit = 0;        // characters (0 = no limit)
        bool Interactable = true;
        bool ReadOnly = false;         // can be selected and copied, not changed
        bool SelectAllOnFocus = false;
        std::string FontPath;          // "" = the engine's font
        Ref<Font> FontAsset;
        float Padding = 10.0f;         // px left / right of the text
        glm::vec4 BackgroundColor{ 0.09f, 0.10f, 0.12f, 1.0f };
        glm::vec4 OutlineColor{ 0.28f, 0.30f, 0.35f, 1.0f };
        glm::vec4 FocusColor{ 0.16f, 0.62f, 0.50f, 1.0f };  // the outline while editing
        glm::vec4 TextColor{ 0.95f, 0.96f, 0.98f, 1.0f };
        glm::vec4 PlaceholderColor{ 0.50f, 0.53f, 0.58f, 1.0f };
        glm::vec4 CaretColor{ 1.0f };
        glm::vec4 SelectionColor{ 0.22f, 0.46f, 0.86f, 0.55f };
        float CaretBlinkRate = 0.53f;  // seconds on / off (0 = always on)

        // runtime state (read by scripts)
        bool Focused = false;
        bool Hovered = false;
        bool TextChanged = false;      // true for one frame
        bool Submitted = false;        // true for one frame (Enter)
        size_t Caret = 0, Anchor = 0;  // bytes into Text (Anchor: the selection's other end)
        float BlinkTime = 0.0f;
        float Scroll = 0.0f;           // px the text is moved left (a long text keeps the caret in view)
        std::string TextBeforeEdit;    // Escape brings it back
        bool Selecting = false;        // the mouse drags a selection

        void ResetRuntime()
        {
            Focused = Hovered = TextChanged = Submitted = Selecting = false;
            Caret = Anchor = 0;
            BlinkTime = Scroll = 0.0f;
            TextBeforeEdit.clear();
        }
    };

    // A mask: the entity's UI subtree (its UI children and theirs) is drawn - and found by the mouse
    // - only inside its rectangle as drawn, Padding px in from its edges (left, right, top, bottom;
    // negative: out). A mask inside a mask shows only what both let through (a list in a scrolled
    // panel). ShowGraphic off: the mask's own picture (image, sprite, button colour) is not drawn -
    // it only clips. Rectangles only (the scissor test): a round picture does not round the clip.
    // On a scroll view it clips to the view: its Viewport's rectangle (as drawn), else its own without
    // its scrollbars (they stay out).
    struct UIMaskComponent
    {
        bool ShowGraphic = true;
        glm::vec4 Padding{ 0.0f };
    };

    // A scroll view: its Content (a UI entity of its subtree - a list whose layout group fits its
    // height, a big map) moves inside the view (the Viewport's rectangle, else the scroll view's
    // own without its scrollbars) by the mouse wheel, by dragging it (also when the press starts
    // on a button in it: the button is then not clicked), by the scrollbars it draws along its
    // right / bottom edge (drag the handle, click the track) and by keyboard / gamepad navigation
    // (the widget selected is scrolled into view). Its UI children are laid out in the space
    // left of the scrollbars. A UI Mask on it (or on the Viewport) hides what is outside the view.
    // Movement: Elastic - pulled past an edge it resists and springs back in Elasticity seconds;
    // Clamped - it stops at the edges; Unrestricted. Inertia: it glides on after a drag, keeping
    // DecelerationRate of its speed per second. The content's UI Transform Position is what moves
    // (it must not be placed by a layout group). A change calls OnScrollChanged(position) on its
    // script and emits "UIScrollChanged" (payload glm::vec2: x 0 left .. 1 right, y 1 top .. 0 bottom);
    // Scene::SetScrollPosition / ScrollBy / ScrollIntoView move it from code.
    struct UIScrollViewComponent
    {
        EntityRef Content;
        EntityRef Viewport;                  // empty: the scroll view itself
        bool Horizontal = false;
        bool Vertical = true;
        UIScrollMovement Movement = UIScrollMovement::Elastic;
        float Elasticity = 0.1f;             // seconds it springs back in
        bool Inertia = true;
        float DecelerationRate = 0.135f;     // the part of the speed kept after a second
        float ScrollSensitivity = 40.0f;     // UI units per notch of the mouse wheel
        bool Draggable = true;               // the content can be dragged with the mouse
        UIScrollbarVisibility Scrollbars = UIScrollbarVisibility::AutoHide;
        float ScrollbarWidth = 10.0f;        // UI units
        float ScrollbarInset = 3.0f;         // UI units between a bar and the edges
        glm::vec4 TrackColor{ 0.0f, 0.0f, 0.0f, 0.28f };
        glm::vec4 HandleColor{ 0.78f, 0.80f, 0.86f, 0.85f };

        // runtime state (read by scripts)
        glm::vec2 NormalizedPosition{ 0.0f, 1.0f };
        glm::vec2 Velocity{ 0.0f };          // UI units per second (inertia, the spring)
        bool Dragging = false;               // its content or a scrollbar is dragged
        bool ScrollChanged = false;          // true for one frame: it moved
        bool ShowHorizontalBar = false;      // the scrollbars shown now (the layout leaves room for them)
        bool ShowVerticalBar = false;
        int HoveredBar = -1;                 // the scrollbar under the mouse: 0 horizontal, 1 vertical, -1 none
        int DraggedBar = -1;
        bool Revealing = false;              // moving to show the selected widget
        glm::vec2 RevealTarget{ 0.0f };      // the content's Position it moves to
        bool PositionKnown = false;          // NormalizedPosition was measured (the first frame tells nothing)

        void ResetRuntime()
        {
            NormalizedPosition = { 0.0f, 1.0f };
            Velocity = RevealTarget = glm::vec2(0.0f);
            Dragging = ScrollChanged = Revealing = PositionKnown = false;
            HoveredBar = DraggedBar = -1;
        }
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
        // The collision layer of its colliders (0..15, Project settings > Physics layers): which other
        // layers it touches is the project's layer matrix. A collider may pick its own (Layer >= 0).
        int Layer = 0;
        // Sleeping: a body that keeps still for a while (scene settings: Physics 2D > time to sleep),
        // together with everything touching it, is not simulated until something hits it, a joint
        // pulls it, a script moves it or wakes it (ScriptableEntity::WakeUp). StartAsleep: it starts
        // like that (a tower that waits to be knocked over); NeverSleep: it is always simulated.
        enum class SleepMode { StartAwake = 0, StartAsleep, NeverSleep };
        SleepMode Sleeping = SleepMode::StartAwake;

        void* RuntimeBody = nullptr; // b2Body*
    };

    // What every 2D collider has besides its shape. The values are the collider's own unless
    // MaterialPath names a physics material asset (.zphysmat): then its friction, bounciness,
    // density and combine modes are used (and follow the file when it changes).
    // Layer: -1 = the Rigidbody 2D's layer, else this collider's own collision layer.
    // BoxCollider2D, CircleCollider2D, PolygonCollider2D and EdgeCollider2D have these fields:
    //   float Density, Friction, Restitution, RestitutionThreshold; bool IsSensor;
    //   std::string MaterialPath; int Layer;

    struct BoxCollider2DComponent
    {
        glm::vec2 Offset = { 0.0f, 0.0f };
        glm::vec2 Size = { 0.5f, 0.5f }; // half extents (multiplied by transform scale)

        float Density = 1.0f;
        float Friction = 0.5f;
        float Restitution = 0.0f;
        float RestitutionThreshold = 0.5f;
        bool IsSensor = false;
        std::string MaterialPath; // physics material asset (.zphysmat), "" = the values above
        int Layer = -1;           // -1: the Rigidbody 2D's layer
        // A one-way platform: bodies jump up through it from below and land on its top (its up is the
        // entity's +Y). Scripts can drop through with ScriptableEntity::DropThroughPlatforms.
        // (PlatformEffector2DComponent does the same with more settings.)
        bool OneWay = false;

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
        std::string MaterialPath;
        int Layer = -1;

        void* RuntimeFixture = nullptr;
    };

    // Any polygon (points in the entity's frame, multiplied by its scale like a box's size, plus
    // Offset). A concave or long outline is split into convex pieces of at most 8 corners (ear
    // clipping, then the pieces merged back while they stay convex); an outline that crosses itself
    // uses its convex hull. The editor moves / adds / removes the points in the viewport.
    struct PolygonCollider2DComponent
    {
        glm::vec2 Offset = { 0.0f, 0.0f };
        std::vector<glm::vec2> Points = { { -0.5f, -0.5f }, { 0.5f, -0.5f }, { 0.5f, 0.2f }, { 0.0f, 0.5f }, { -0.5f, 0.2f } };

        float Density = 1.0f;
        float Friction = 0.5f;
        float Restitution = 0.0f;
        float RestitutionThreshold = 0.5f;
        bool IsSensor = false;
        std::string MaterialPath;
        int Layer = -1;

        void* RuntimeFixture = nullptr; // the first piece's b2Fixture*
        int RuntimePieces = 0;          // how many convex pieces the body got
    };

    // A line through the points (an open chain), or a closed loop: ground, slopes, walls. Edges
    // have no area (no mass); on a dynamic body they collide but do not weigh anything. Two-sided by
    // default; OneSided makes a smooth chain that only stops bodies coming from the left of the
    // points' direction (points left to right: from above; a counter-clockwise loop: from inside) -
    // bodies sliding along it do not catch on the corners between its segments.
    struct EdgeCollider2DComponent
    {
        glm::vec2 Offset = { 0.0f, 0.0f };
        std::vector<glm::vec2> Points = { { -1.0f, 0.0f }, { 1.0f, 0.0f } };
        bool Loop = false;
        bool OneSided = false;

        float Density = 1.0f; // unused by edges (kept for the material / inspector)
        float Friction = 0.5f;
        float Restitution = 0.0f;
        float RestitutionThreshold = 0.5f;
        bool IsSensor = false;
        std::string MaterialPath;
        int Layer = -1;

        void* RuntimeFixture = nullptr; // the first segment's / the chain's b2Fixture*
    };

    // Turns the entity's colliders (its Rigidbody 2D's, or a tilemap's tiles) into platforms. OneWay:
    // bodies pass through from below / the sides and land on the surface - collisions only from
    // within SurfaceArc (degrees, around the surface's up: the entity's +Y turned by RotationOffset).
    // 180 is the classic one-way platform (BoxCollider2D::OneWay), smaller arcs leave out the
    // corners, larger ones also hold bodies at the sides. Side contacts (within SideArc of the
    // surface's left / right) get no friction / bounce unless UseSideFriction / UseSideBounce, so a
    // player pushing against the side does not stick to it. Scripts drop through with
    // ScriptableEntity::DropThroughPlatforms.
    struct PlatformEffector2DComponent
    {
        bool OneWay = true;
        float SurfaceArc = 180.0f;    // degrees, 1..360
        float RotationOffset = 0.0f;  // degrees: the surface's up from the entity's +Y
        bool UseSideFriction = false;
        bool UseSideBounce = false;
        float SideArc = 1.0f;         // degrees, 0..180
    };

    // Pushes the dynamic bodies inside the entity's colliders (usually triggers on a static Rigidbody
    // 2D): wind, a jet of air, a current. Every physics step each body overlapping one of them (whose
    // collider's layer is in ColliderMask) gets ForceMagnitude along ForceAngle and is slowed down by
    // Drag / AngularDrag. Solid colliders push the bodies that touch them.
    struct AreaEffector2DComponent
    {
        float ForceAngle = 90.0f;      // degrees (0 = +X, 90 = up)
        bool UseGlobalAngle = false;   // the angle in the world, else turned with the entity
        float ForceMagnitude = 10.0f;  // N (ScaleWithMass: N per kg, i.e. an acceleration)
        float ForceVariation = 0.0f;   // a random amount up to this much added / taken each step (gusts)
        bool ScaleWithMass = false;    // every body gets the same acceleration, light or heavy
        float Drag = 0.0f;             // 1/s: how quickly the bodies' velocity dies away inside
        float AngularDrag = 0.0f;
        uint16_t ColliderMask = 0xFFFF; // the collision layers (bits) it acts on

        int AffectedBodies = 0;        // runtime: bodies pushed in the last physics step
    };

    // A fluid (water) in the entity's colliders (triggers on a static Rigidbody 2D) up to its surface:
    // SurfaceLevel world units above the entity's position, level (at right angles to gravity). A
    // body inside gets the weight of the fluid it displaces upwards - Density x its collider area
    // below the surface x gravity, at the centre of that part, so a collider less dense than the
    // fluid floats and turns upright - and is slowed by the drags and carried by the flow in
    // proportion to how much of it is under.
    struct BuoyancyEffector2DComponent
    {
        float SurfaceLevel = 0.5f;     // world units above the entity's position
        float Density = 2.0f;          // the fluid's (colliders' Density: 1 by default)
        float LinearDrag = 1.5f;       // 1/s, for a fully submerged body
        float AngularDrag = 1.0f;
        float FlowAngle = 0.0f;        // degrees in the world (0 = +X)
        float FlowMagnitude = 0.0f;    // N on a fully submerged body (a current)
        float FlowVariation = 0.0f;
        uint16_t ColliderMask = 0xFFFF;

        int AffectedBodies = 0;        // runtime: bodies in the fluid in the last physics step
        float SubmergedArea = 0.0f;    // runtime: their area under the surface
    };

    // ------------------------------------------------------------------ joints 2D
    // What every 2D joint has. A joint ties the entity's Rigidbody 2D to the connected body (another
    // entity's Rigidbody 2D or Tilemap) or, without one, to a fixed point in the world. It is made
    // while the game runs / simulates once both bodies exist, goes when either of them leaves the
    // world (inactive, destroyed) and comes back with them. Points are in the bodies' own frames
    // (body units: not scaled, like the colliders' offsets); angles in degrees.
    struct Joint2DComponentBase
    {
        EntityRef ConnectedBody;           // empty: a fixed point in the world
        glm::vec2 Anchor{ 0.0f };          // on this body
        glm::vec2 ConnectedAnchor{ 0.0f }; // on the connected body, or the world point
        // The connected anchor is where Anchor is when the joint is made (written back then).
        bool AutoConfigureConnectedAnchor = true;
        bool CollideConnected = false;     // the two bodies still collide with each other
        // The joint breaks (goes for good, OnJointBreak) when its reaction force / torque gets
        // larger than this. 0 = never.
        float BreakForce = 0.0f;
        float BreakTorque = 0.0f;

        // runtime state (scripts may read it; Broken = false makes a broken joint again)
        void* RuntimeJoint = nullptr;  // b2Joint*
        uint64_t RuntimeConnected = 0; // the ConnectedBody it was made with (changed: made again)
        bool Broken = false;
        float ReactionForce = 0.0f;    // after the last physics step
        float ReactionTorque = 0.0f;

        void ResetRuntime()
        {
            RuntimeJoint = nullptr;
            RuntimeConnected = 0;
            Broken = false;
            ReactionForce = ReactionTorque = 0.0f;
        }
    };

    // Keeps the two anchors at a distance: a rigid rod, a rope (MaxDistanceOnly) or a spring (Frequency).
    // Its connected anchor is not auto-configured by default (both ends at one point would be no
    // distance): the connected body's origin, or the world point.
    struct DistanceJoint2DComponent : Joint2DComponentBase
    {
        DistanceJoint2DComponent() { AutoConfigureConnectedAnchor = false; }

        bool AutoConfigureDistance = true; // Distance = the anchors' distance when the joint is made
        float Distance = 1.0f;
        bool MaxDistanceOnly = false;      // a rope: closer is allowed, further is not
        float Frequency = 0.0f;            // spring oscillations per second (0 = rigid)
        float DampingRatio = 0.0f;         // 0..1 (1 = no oscillation)

        float CurrentDistance = 0.0f;      // runtime: after the last step
    };

    // A hinge: the bodies share the anchor point and turn around it, optionally within limits
    // (relative to the pose the joint was made in) and driven by a motor.
    struct RevoluteJoint2DComponent : Joint2DComponentBase
    {
        bool EnableLimit = false;
        float LowerAngle = -45.0f; // degrees: this body's rotation relative to the connected body
        float UpperAngle = 45.0f;
        bool EnableMotor = false;
        float MotorSpeed = 0.0f;      // degrees per second (positive: counter-clockwise)
        float MaxMotorTorque = 10.0f;

        float JointAngle = 0.0f;      // runtime: degrees from the start pose
        float JointSpeed = 0.0f;      // runtime: degrees per second
    };

    // A slider: the body moves only along Axis relative to the connected body, without turning.
    struct PrismaticJoint2DComponent : Joint2DComponentBase
    {
        glm::vec2 Axis{ 1.0f, 0.0f }; // in this body's frame (when the joint is made)
        bool EnableLimit = false;
        float LowerTranslation = -1.0f; // units along the axis from the start position
        float UpperTranslation = 1.0f;
        bool EnableMotor = false;
        float MotorSpeed = 0.0f;        // units per second
        float MaxMotorForce = 10.0f;

        float JointTranslation = 0.0f;  // runtime
        float JointSpeed = 0.0f;        // runtime: units per second
    };

    // Glues the bodies together (rigid, or springy around the angle with Frequency).
    struct WeldJoint2DComponent : Joint2DComponentBase
    {
        float Frequency = 0.0f;    // angular spring oscillations per second (0 = rigid)
        float DampingRatio = 0.0f;
    };

    // A wheel: this body (the wheel) turns freely around the anchor and is held on the connected
    // body (the chassis) by a suspension spring along Axis; the motor drives the wheel.
    struct WheelJoint2DComponent : Joint2DComponentBase
    {
        glm::vec2 Axis{ 0.0f, 1.0f }; // the suspension direction, in this body's frame (when made)
        float Frequency = 4.0f;       // suspension oscillations per second (0 = no spring: slides freely)
        float DampingRatio = 0.7f;
        bool EnableLimit = false;     // suspension travel
        float LowerTranslation = -0.25f;
        float UpperTranslation = 0.25f;
        bool EnableMotor = false;
        float MotorSpeed = 0.0f;      // degrees per second (positive: counter-clockwise)
        float MaxMotorTorque = 10.0f;

        float JointTranslation = 0.0f; // runtime: suspension travel
        float JointSpeed = 0.0f;       // runtime: the wheel's spin, degrees per second
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
        // The collision layer of its colliders (0..15, Project settings > Physics layers - shared with
        // 2D physics): which other layers it touches is the project's layer matrix. Raycasts and the
        // other queries take a mask of layers. May change while the game runs.
        int Layer = 0;
        // Continuous collision detection (dynamic bodies): the body is swept along its path every step,
        // so a fast one (a bullet, a thrown ball) stops at a thin wall instead of passing through it
        // between two steps. Costs more: for the fast and small ones. May change while the game runs.
        bool ContinuousCollision = false;
        // Sleeping: a body that keeps still for a while (scene settings: Physics 3D > time to sleep),
        // together with everything touching it, is not simulated until something hits it, a joint
        // pulls it, a script moves or wakes it (ScriptableEntity::WakeUp3D). StartAsleep: it starts
        // like that (a stack that waits to be knocked over); NeverSleep: it is always simulated.
        enum class SleepMode { StartAwake = 0, StartAsleep, NeverSleep };
        SleepMode Sleeping = SleepMode::StartAwake;

        uint32_t RuntimeBodyID = 0xffffffff; // JPH::BodyID
    };

    // What every 3D collider has besides its shape. The friction / bounciness are the collider's own
    // unless MaterialPath names a physics material asset (.zphysmat, the same files as 2D colliders'):
    // then its friction, bounciness, bounce threshold and combine modes are used (and follow the file
    // when it changes; its density is not - a 3D body's mass is its Rigidbody 3D's). IsSensor: a
    // trigger - OnTriggerBegin / OnTriggerEnd instead of a collision; a body may have both trigger and
    // solid colliders. BoxCollider3D ... MeshCollider3D have these fields:
    //   glm::vec3 Offset; float Friction, Restitution; bool IsSensor; std::string MaterialPath;

    struct BoxCollider3DComponent
    {
        glm::vec3 HalfExtents = { 0.5f, 0.5f, 0.5f }; // multiplied by transform scale
        glm::vec3 Offset = { 0.0f, 0.0f, 0.0f };
        float Friction = 0.5f;
        float Restitution = 0.0f;
        bool IsSensor = false;
        std::string MaterialPath; // physics material asset (.zphysmat), "" = the values above
    };

    struct SphereCollider3DComponent
    {
        float Radius = 0.5f;
        glm::vec3 Offset = { 0.0f, 0.0f, 0.0f };
        float Friction = 0.5f;
        float Restitution = 0.0f;
        bool IsSensor = false;
        std::string MaterialPath; // physics material asset (.zphysmat), "" = the values above
    };

    struct CapsuleCollider3DComponent
    {
        float Radius = 0.5f;
        float HalfHeight = 0.5f; // cylinder part, along Y
        glm::vec3 Offset = { 0.0f, 0.0f, 0.0f };
        float Friction = 0.5f;
        float Restitution = 0.0f;
        bool IsSensor = false;
        std::string MaterialPath; // physics material asset (.zphysmat), "" = the values above
    };

    // A convex collider shaped like a mesh: the convex hull of the vertices of the entity's Mesh
    // Renderer (its model or primitive) - or of the model ModelPath names - with at most MaxVertices
    // corners (a mesh with more gets a simpler hull through its outermost points: cheaper, a little
    // smaller). Hollows and holes are filled in (a cup is a solid block); for those use several
    // colliders. Multiplied by the transform scale like the other 3D colliders. Hulls are made once per
    // model asset and limit (CollisionShapes3D) and follow the model file when it changes.
    struct ConvexHullCollider3DComponent
    {
        std::string ModelPath;   // .gltf / .glb / .obj (relative to assets); "" = the Mesh Renderer's
        int MaxVertices = 32;    // 4..256
        glm::vec3 Offset = { 0.0f, 0.0f, 0.0f };
        float Friction = 0.5f;
        float Restitution = 0.0f;
        bool IsSensor = false;
        std::string MaterialPath; // physics material asset (.zphysmat), "" = the values above
    };

    // A collider that is the mesh itself: every triangle of the entity's Mesh Renderer (its model or
    // primitive) - or of the model ModelPath names - so stairs, arches, tunnels and rough ground
    // collide where they are drawn. For static and kinematic Rigidbody 3D bodies only: a dynamic body
    // leaves it out (use Convex Hull Collider 3D or primitive colliders for those). Triangles are
    // solid from their front side (as they are drawn). Multiplied by the transform scale like the
    // other 3D colliders. Each model's triangles and their tree are made once (CollisionShapes3D) and
    // follow the model file when it changes.
    struct MeshCollider3DComponent
    {
        std::string ModelPath;   // .gltf / .glb / .obj (relative to assets); "" = the Mesh Renderer's
        glm::vec3 Offset = { 0.0f, 0.0f, 0.0f };
        float Friction = 0.5f;
        float Restitution = 0.0f;
        bool IsSensor = false;
        std::string MaterialPath; // physics material asset (.zphysmat), "" = the values above
    };

    // ------------------------------------------------------------------ joints 3D
    // What every 3D joint has (Jolt constraints). Like the 2D joints: a joint ties the entity's
    // Rigidbody 3D to the connected body (another entity's Rigidbody 3D) or, without one, to a fixed
    // point in the world. It is made while the game runs / simulates once both bodies exist, in the
    // pose they have then (a hinge's angle 0, a slider's position 0), goes when either of them leaves
    // the world (inactive, destroyed) and comes back with them. Points are in the entities' own frames
    // (scaled with them, like the 3D colliders' offsets), axes in this entity's frame, angles in
    // degrees.
    struct Joint3DComponentBase
    {
        EntityRef ConnectedBody;           // empty: a fixed point in the world
        glm::vec3 Anchor{ 0.0f };          // on this body
        glm::vec3 ConnectedAnchor{ 0.0f }; // on the connected body, or the world point
        // The connected anchor is where Anchor is when the joint is made (written back then).
        bool AutoConfigureConnectedAnchor = true;
        bool CollideConnected = false;     // the two bodies still collide with each other
        // The joint breaks (goes for good, OnJointBreak3D) when its reaction force / torque gets larger
        // than this. 0 = never.
        float BreakForce = 0.0f;
        float BreakTorque = 0.0f;

        // runtime state (scripts may read it; Broken = false makes a broken joint again)
        uint32_t RuntimeJoint = 0;     // PhysicsWorld3D joint handle (0 = none)
        uint64_t RuntimeConnected = 0; // the ConnectedBody it was made with (changed: made again)
        bool Broken = false;
        float ReactionForce = 0.0f;    // after the last physics step
        float ReactionTorque = 0.0f;

        void ResetRuntime()
        {
            RuntimeJoint = 0;
            RuntimeConnected = 0;
            Broken = false;
            ReactionForce = ReactionTorque = 0.0f;
        }
    };

    // Glues the bodies together: they move as one (until it breaks).
    struct FixedJoint3DComponent : Joint3DComponentBase
    {
    };

    // A hinge (door, wheel, elbow): the bodies share the anchor point and this body turns only around
    // Axis, optionally within limits (relative to the pose the joint was made in), driven by a motor
    // or pulled towards an angle by a spring.
    struct HingeJoint3DComponent : Joint3DComponentBase
    {
        glm::vec3 Axis{ 0.0f, 1.0f, 0.0f }; // in this body's frame (when the joint is made)
        bool EnableLimit = false;
        float LowerAngle = -45.0f;    // -180..0 degrees (0 is the start pose)
        float UpperAngle = 45.0f;     // 0..180
        bool EnableMotor = false;
        float MotorSpeed = 0.0f;      // degrees per second (positive: counter-clockwise around Axis)
        float MaxMotorTorque = 10.0f; // N*m
        // A spring towards SpringTargetAngle (while the motor is off): a door that swings shut.
        bool EnableSpring = false;
        float SpringTargetAngle = 0.0f;
        float SpringFrequency = 2.0f; // oscillations per second
        float SpringDamping = 0.5f;   // 0..1 (1 = no oscillation)

        float JointAngle = 0.0f;      // runtime: degrees from the start pose
        float JointSpeed = 0.0f;      // runtime: degrees per second
    };

    // Keeps the two anchors between MinDistance and MaxDistance apart: a rigid rod (equal), a rope
    // (MaxDistanceOnly: closer is allowed) or a range; Frequency makes the ends soft (a spring).
    // Its connected anchor is not auto-configured by default (both ends at one point would be no
    // distance): the connected body's origin, or the world point.
    struct DistanceJoint3DComponent : Joint3DComponentBase
    {
        DistanceJoint3DComponent() { AutoConfigureConnectedAnchor = false; }

        // MaxDistance (and MinDistance unless MaxDistanceOnly) = the anchors' distance when the joint is made.
        bool AutoConfigureDistance = true;
        float MinDistance = 1.0f;
        float MaxDistance = 1.0f;
        bool MaxDistanceOnly = false;  // a rope: MinDistance is 0
        float Frequency = 0.0f;        // spring oscillations per second (0 = rigid)
        float DampingRatio = 0.0f;     // 0..1

        float CurrentDistance = 0.0f;  // runtime: after the last step
    };

    // A slider: the body moves only along Axis relative to the connected body, without turning;
    // optionally within limits (around the start position) and driven by a motor.
    struct SliderJoint3DComponent : Joint3DComponentBase
    {
        glm::vec3 Axis{ 1.0f, 0.0f, 0.0f }; // in this body's frame (when the joint is made)
        bool EnableLimit = false;
        float LowerTranslation = -1.0f; // <= 0: units along the axis from the start position
        float UpperTranslation = 1.0f;  // >= 0
        bool EnableMotor = false;
        float MotorSpeed = 0.0f;        // units per second
        float MaxMotorForce = 10.0f;    // N

        float JointTranslation = 0.0f;  // runtime
        float JointSpeed = 0.0f;        // runtime: units per second
    };

    // A ball-and-socket joint (shoulder, hip, neck, a lamp on a chain): the bodies share the anchor
    // point; this body may swing its Axis within a cone of SwingLimit degrees around where the axis was
    // when the joint was made, and twist around it between TwistLower and TwistUpper. FrictionTorque
    // resists turning (a ragdoll's limbs do not flail).
    struct ConeJoint3DComponent : Joint3DComponentBase
    {
        glm::vec3 Axis{ 0.0f, 1.0f, 0.0f }; // the twist axis in this body's frame (along a limb)
        float SwingLimit = 45.0f;    // 0..180 degrees: the cone's half angle (180 = free)
        float TwistLower = -30.0f;   // -180..0 degrees
        float TwistUpper = 30.0f;    // 0..180
        float FrictionTorque = 0.0f; // N*m

        float SwingAngle = 0.0f;     // runtime: degrees between the axis now and where it started
        float TwistAngle = 0.0f;     // runtime: degrees of twist around the axis
    };

    // ------------------------------------------------------------------ audio
    struct AudioSourceComponent
    {
        std::string Path;     // relative to assets (.wav / .mp3 / .flac / .ogg)
        float Volume = 1.0f;
        float Pitch = 1.0f;
        bool Loop = false;
        bool PlayOnStart = true;
        bool Spatial = false;      // 3D sound: volume/pan depend on the listener (Audio Listener / primary camera)
        float MinDistance = 1.0f;
        float MaxDistance = 30.0f;
        // The mixer group it plays in (Audio Mixer window; an unknown name plays in SFX).
        std::string Output = "SFX";
        // Streamed from disk while it plays instead of decoded into memory first (long music /
        // ambience). Files larger than the project's threshold always stream.
        bool Stream = false;

        // Random variation, picked anew each time it starts: Volume and Pitch are multiplied by a
        // value between x (min) and y (max), e.g. pitch 0.9..1.1 for footsteps. 1..1 = none.
        glm::vec2 VolumeRandom{ 1.0f, 1.0f };
        glm::vec2 PitchRandom{ 1.0f, 1.0f };

        // Voice limits: with the project's maximum of sounds playing (Audio Mixer > Settings) a less
        // important sound (lower Priority, 0..255) gives its place to this one; among equals the
        // steal policy decides. MaxInstances: this file at most so many times at once (0 = no limit).
        int Priority = 128;
        int MaxInstances = 0;
        AudioStealPolicy Steal = AudioStealPolicy::Oldest;

        // 3D sound: how strongly its pitch follows the Doppler effect while it (or the listener)
        // moves - x the project's Doppler factor; 0 = off. Its velocity comes from its movement.
        float DopplerFactor = 1.0f;

        // Fades, in seconds (0 = none): the sound starts silent and fades in whenever it plays
        // (PlayOnStart, PlayAudio); it fades out when it is stopped (StopAudio, the entity switched
        // off or destroyed) instead of being cut. Scripts may pass their own times.
        float FadeIn = 0.0f;
        float FadeOut = 0.0f;
        // Low-pass / high-pass on this source alone (e.g. a radio behind a wall); its mixer group
        // may filter on top. Changes while it plays glide there.
        AudioFilterSettings Filter;
        // 3D sound: a directional cone around the entity's forward (-Z) - full volume in front,
        // OuterGain behind (a loudspeaker, a TV, someone talking).
        AudioConeSettings Cone;

        // Runtime.
        uint32_t RuntimeHandle = 0;
        glm::vec3 Velocity{ 0.0f };     // units per second (measured while a 3D sound plays)
        glm::vec3 LastPosition{ 0.0f };
        bool HasLastPosition = false;

        void ResetRuntime()
        {
            RuntimeHandle = 0;
            Velocity = glm::vec3(0.0f);
            LastPosition = glm::vec3(0.0f);
            HasLastPosition = false;
        }
    };

    // Where the 3D sounds are heard from (instead of the primary camera), e.g. the player's head
    // in a third-person game. Of several, the enabled one active in the hierarchy with the
    // highest Priority listens (ties: the one created first); without one, the primary camera.
    struct AudioListenerComponent
    {
        bool Enabled = true;
        int Priority = 0;
        // The listener's velocity is measured from how it moves, for the Doppler effect of moving
        // sounds (off: a still listener).
        bool TrackVelocity = false;

        // Runtime.
        glm::vec3 Velocity{ 0.0f };     // units per second (TrackVelocity)
        glm::vec3 LastPosition{ 0.0f };
        bool HasLastPosition = false;

        void ResetRuntime()
        {
            Velocity = glm::vec3(0.0f);
            LastPosition = glm::vec3(0.0f);
            HasLastPosition = false;
        }
    };

    // A looping sound heard everywhere inside a volume - a forest, a cave, rain, a crowd, a river
    // bank. Inside it plays without a position (the same in both ears); as the listener (Audio
    // Listener / primary camera) comes up to it, it fades in over BlendDistance outside the edge,
    // and FadeTime smooths changes (a teleport, the zone switched on or off). The volume is a box
    // (Size in the entity's space: its scale and rotation apply) or a sphere (Radius x the largest
    // scale). The sound starts when it can first be heard and stops when it fades out.
    struct AmbientZoneComponent
    {
        enum class ZoneShape { Box = 0, Sphere };

        std::string Path;            // relative to assets; loops
        float Volume = 1.0f;
        float Pitch = 1.0f;
        std::string Output = "SFX";  // the mixer group (an unknown name plays in SFX)
        bool Stream = false;         // streamed from disk (long ambience)
        int Priority = 128;          // the voice limit (AudioSourceComponent::Priority)
        ZoneShape Shape = ZoneShape::Box;
        glm::vec3 Size{ 10.0f, 4.0f, 10.0f }; // box, full size in local units
        float Radius = 5.0f;         // sphere, local units
        float BlendDistance = 4.0f;  // outside the edge: full at the edge, silent this far out
        float FadeTime = 0.5f;       // seconds the level takes at most from silent to full (0: at once)

        // Runtime.
        uint32_t RuntimeHandle = 0;
        float Target = 0.0f;         // where the listener is: 1 inside, 0 beyond the blend
        float Level = 0.0f;          // what is heard now (Target, smoothed over FadeTime)
        float Distance = 0.0f;       // the listener's distance outside the volume (0: inside)
        float RetryIn = 0.0f;        // the voice limit refused it: tried again after this

        void ResetRuntime()
        {
            RuntimeHandle = 0;
            Target = Level = Distance = RetryIn = 0.0f;
        }
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

    // The 2D joints (each derives from Joint2DComponentBase).
    using Joint2DComponents = ComponentGroup<DistanceJoint2DComponent, RevoluteJoint2DComponent, PrismaticJoint2DComponent,
        WeldJoint2DComponent, WheelJoint2DComponent>;

    // The 3D joints (each derives from Joint3DComponentBase).
    using Joint3DComponents =
        ComponentGroup<FixedJoint3DComponent, HingeJoint3DComponent, DistanceJoint3DComponent, SliderJoint3DComponent, ConeJoint3DComponent>;

    // Every component that is copied/serialized with an entity (IDComponent & TagComponent are handled separately).
    using AllComponents = ComponentGroup<RelationshipComponent, TagsComponent, TransformComponent, SpriteRendererComponent,
        SpriteAnimationComponent, TintAnimationComponent, CircleRendererComponent, TextComponent, MeshRendererComponent, AnimatorComponent, AnimationPlayerComponent, CameraComponent,
        DirectionalLightComponent, PointLightComponent, SpotLightComponent, Light2DComponent, Rigidbody2DComponent, BoxCollider2DComponent,
        CircleCollider2DComponent, PolygonCollider2DComponent, EdgeCollider2DComponent, PlatformEffector2DComponent, AreaEffector2DComponent, BuoyancyEffector2DComponent, DistanceJoint2DComponent, RevoluteJoint2DComponent, PrismaticJoint2DComponent, WeldJoint2DComponent,
        WheelJoint2DComponent, Rigidbody3DComponent, BoxCollider3DComponent, SphereCollider3DComponent,
        CapsuleCollider3DComponent, ConvexHullCollider3DComponent, MeshCollider3DComponent, FixedJoint3DComponent, HingeJoint3DComponent, DistanceJoint3DComponent,
        SliderJoint3DComponent, ConeJoint3DComponent, CharacterController3DComponent, TilemapComponent, ParallaxComponent, Camera2DFollowComponent, ParticleSystemComponent, UITransformComponent, UICanvasComponent,
        UILayoutGroupComponent, UILayoutElementComponent, UINavigationComponent, UIAnimationComponent, UIButtonComponent,
        UIImageComponent, UISliderComponent, UIToggleComponent, UIProgressBarComponent, UIInputFieldComponent, UIMaskComponent,
        UIScrollViewComponent, AudioSourceComponent, AudioListenerComponent, AmbientZoneComponent, NativeScriptComponent, EditorMetadataComponent>;

}
