#pragma once

#include "Zamin/AI/Formation.h"
#include "Zamin/AI/NavGrid.h"
#include "Zamin/AI/NavPathfinder.h"
#include "Zamin/AI/NavRecovery.h"
#include "Zamin/Audio/AudioMixer.h"
#include "Zamin/Core/Input.h"
#include "Zamin/Core/UUID.h"
#include "Zamin/Localization/Localization.h"
#include "Zamin/Math/Spline.h"
#include "Zamin/Renderer/Texture.h"
#include "Zamin/Renderer/Mesh.h"
#include "Zamin/Renderer/Material.h"
#include "Zamin/Renderer/MaterialAsset.h"
#include "Zamin/Renderer/Model.h"
#include "Zamin/Renderer/Font.h"
#include "Zamin/Renderer/SpriteSheet.h"
#include "Zamin/Scene/Terrain.h"
#include "Zamin/Scene/Foliage.h"
#include "Zamin/Scene/Camera2D.h"
#include "Zamin/Scene/ParticleSystem.h"
#include "Zamin/Scene/PixelPerfect.h"
#include "Zamin/Scene/PropertyAnimation.h"
#include "Zamin/Scene/SpriteClip.h"
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

#include <array>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Zamin {

    class ScriptableEntity;

    /// The entity's persistent ID (Entity::GetUUID): saved with the scene, kept by play mode copies;
    /// entity references (EntityRef, the hierarchy) point at it.
    struct IDComponent
    {
        UUID ID;
        IDComponent() = default;
        IDComponent(UUID id) : ID(id) {}
    };

    /// Display name and the entity's own active flag. An entity takes part in the game only
    /// when it and every ancestor are active (Scene::IsActiveInHierarchy); change the flag with
    /// Scene::SetActive / Entity::SetActive so the running scene reacts at once (a direct write
    /// is noticed on the next frame). Unlike EditorMetadataComponent::Hidden this is game state:
    /// it is saved with the scene and kept by copies, duplicates and prefabs.
    struct TagComponent
    {
        std::string Tag;
        bool Active = true;
        TagComponent() = default;
        TagComponent(const std::string& tag) : Tag(tag) {}
    };

    /// Gameplay tags ("Enemy", "Pickup", ...). Not to be confused with TagComponent,
    /// which holds the entity's display name. Queried with Scene::FindEntitiesWithTag,
    /// Entity::HasTag and the matching ScriptableEntity helpers.
    struct TagsComponent
    {
        std::vector<std::string> Tags;

        bool Has(std::string_view tag) const
        {
            return std::find(Tags.begin(), Tags.end(), tag) != Tags.end();
        }
        /// Returns false for empty or duplicate tags.
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

    /// Parent/child hierarchy. Transform of a child is relative to its parent.
    struct RelationshipComponent
    {
        UUID Parent = 0;              ///< 0 = root
        std::vector<UUID> Children;
    };

    struct TransformComponent
    {
        glm::vec3 Translation = { 0.0f, 0.0f, 0.0f };
        glm::vec3 Rotation = { 0.0f, 0.0f, 0.0f }; ///< euler angles, radians
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
    /// 2D renderers draw back to front by sorting layer (Project settings > Sorting layers: the
    /// layer's position in that list; its ID is stored here, 0 = "Default"), then by SortingOrder
    /// inside the layer, then by the distance from the camera.
    struct SpriteRendererComponent
    {
        glm::vec4 Color{ 1.0f, 1.0f, 1.0f, 1.0f };
        Ref<Texture2D> Texture;
        std::string TexturePath; ///< relative to project assets
        /// A named frame of the texture's sprite sheet (Sprite Editor, kept in the texture's .zmeta);
        /// "" = the whole texture. A name the sheet does not have draws the whole texture as well.
        std::string SpriteName;
        float TilingFactor = 1.0f;     ///< Simple mode with the whole texture: repeats it N times
        uint32_t SortingLayer = 0;     ///< SortingLayerList ID
        int SortingOrder = 0;          ///< order inside the sorting layer
        bool PixelArt = false;   ///< nearest filtering (crisp pixels)
        bool FlipX = false;
        bool FlipY = false;
        /// Lit by the 2D lights (Light2DComponent) and the scene's 2D ambient light; off: drawn as it is.
        bool Lit = false;
        /// Simple: the picture on the unit quad (sized by the transform's scale). Sliced (9-slice) /
        /// Tiled: the picture fills Size (local units, the scale applies on top) keeping its borders:
        /// corners are SliceBorder px / PixelsPerUnit big. All four borders 0 = the frame's border
        /// (Sprite Editor). Screen-space UI sprites: Size is the UI rect and 1 px = 1 unit.
        SpriteDrawMode DrawMode = SpriteDrawMode::Simple;
        glm::vec2 Size{ 1.0f };
        glm::vec4 SliceBorder{ 0.0f };  ///< px: left, right, top, bottom
        float PixelsPerUnit = 100.0f;

        /// Effects (drawn by the sprite shader). Hit flash: FlashAmount 0..1 mixes the picture toward
        /// FlashColor (Flash() plays one that fades out). Outline: a border OutlineThickness texture
        /// pixels wide around the opaque pixels, in OutlineColor (sprites drawn as one piece: Simple,
        /// or Sliced / Tiled without borders and repeats - see Scene::CanDrawOutline; the others and
        /// repeated textures get the flash only).
        glm::vec4 FlashColor{ 1.0f };
        float FlashAmount = 0.0f;
        glm::vec4 OutlineColor{ 1.0f, 1.0f, 1.0f, 1.0f };
        float OutlineThickness = 0.0f;
        /// runtime: the flash Flash() started (cleared when play starts / stops)
        glm::vec4 FlashPlayingColor{ 1.0f };
        float FlashTime = 0.0f, FlashDuration = 0.0f;

        SpriteRendererComponent() = default;
        SpriteRendererComponent(const glm::vec4& color) : Color(color) {}

        bool HasSliceBorder() const { return SliceBorder.x > 0.0f || SliceBorder.y > 0.0f || SliceBorder.z > 0.0f || SliceBorder.w > 0.0f; }

        /// Starts a flash: the sprite turns `color` (its alpha: how strongly) at once and fades back
        /// over `duration` seconds (the game's time: it waits while paused).
        void Flash(const glm::vec4& color = glm::vec4(1.0f), float duration = 0.15f)
        {
            FlashPlayingColor = color;
            FlashDuration = std::max(0.0f, duration);
            FlashTime = FlashDuration;
        }
        bool IsFlashing() const { return FlashTime > 0.0f && FlashDuration > 0.0f; }
        void StopFlash() { FlashTime = FlashDuration = 0.0f; }
        /// Advances the playing flash.
        void UpdateFlash(float dt) { if (FlashTime > 0.0f) FlashTime = std::max(0.0f, FlashTime - dt); }
        /// What is drawn now: the playing flash (fading out) while it is stronger, else the constant one.
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

    /// Plays frames on the SpriteRenderer texture: cells of a grid (Columns x Rows, numbered left to
    /// right, top to bottom) or, when Frames is set, named frames of the texture's sprite sheet
    /// (StartFrame / FrameCount then pick from that list).
    /// Clips: sprite animation clips (.zsanim assets) the entity can play by name (PlayClip("run"):
    /// the file name without .zsanim). A clip sets the frames, FPS and loop above (and the
    /// renderer's texture when it names one) and fires its events at its frames; the first clip
    /// plays when the game starts (PlayOnStart) and is what the editor shows.
    struct SpriteAnimationComponent
    {
        int Columns = 4;
        int Rows = 1;
        int StartFrame = 0;
        int FrameCount = 4;
        float FPS = 10.0f;
        bool Loop = true;
        bool Playing = true;
        std::vector<std::string> Frames; ///< sprite sheet frame names ("" list = the grid)
        std::vector<std::string> Clips;  ///< .zsanim files, relative to Assets
        bool PlayOnStart = true;         ///< the first clip

        /// runtime (reset when play starts / stops and for spawned copies)
        float Time = 0.0f;
        int CurrentClip = -1;            ///< index into Clips (-1: the frames above, no clip)
        std::vector<Ref<SpriteClipAsset>> ClipAssets; ///< the loaded Clips (ResolveClips)
        std::vector<std::string> LoadedClips;          ///< the paths ClipAssets were looked up for
        uint64_t AppliedRevision = 0;    ///< the clip revision the fields hold
        int LastFrame = -1;              ///< the frame counter (Time x FPS) last seen: events (-1: none yet)
        bool Started = false;            ///< PlayOnStart was looked at

        void Play(int startFrame, int frameCount, float fps = 10.0f, bool loop = true)
        {
            if (CurrentClip < 0 && StartFrame == startFrame && FrameCount == frameCount && Playing) return;
            StartFrame = startFrame; FrameCount = frameCount; FPS = fps; Loop = loop; Playing = true; Time = 0.0f;
            CurrentClip = -1; LastFrame = -1;
        }
        /// Plays a list of sprite sheet frames (e.g. SpriteSheet::FindSequence("run")).
        void PlayFrames(const std::vector<std::string>& frames, float fps = 10.0f, bool loop = true)
        {
            if (CurrentClip < 0 && Frames == frames && StartFrame == 0 && FrameCount == (int)frames.size() && Playing) return;
            Frames = frames; StartFrame = 0; FrameCount = std::max(1, (int)frames.size());
            FPS = fps; Loop = loop; Playing = true; Time = 0.0f;
            CurrentClip = -1; LastFrame = -1;
        }

        /// @name Clips
        /// Clips (.zsanim)
        /// Loads the clips that changed (Clips edited) - PlayClip and the scene call it.
        /// @{
        void ResolveClips();
        /// The clip with this name (file name without .zsanim) or path; -1 when there is none.
        int FindClip(std::string_view nameOrPath) const;
        /// Plays a clip from its first frame (already playing it: goes on, unless restart). False
        /// when there is no such clip or it cannot be read.
        bool PlayClip(std::string_view nameOrPath, bool restart = false);
        bool PlayClip(int index, bool restart = false);
        /// The frames, rate and loop of a clip into the fields above (what is drawn).
        void ApplyClip(const SpriteClip& clip);
        const SpriteClipAsset* GetCurrentClipAsset() const
        {
            return CurrentClip >= 0 && CurrentClip < (int)ClipAssets.size() ? ClipAssets[(size_t)CurrentClip].get() : nullptr;
        }
        std::string GetCurrentClipName() const { const auto* a = GetCurrentClipAsset(); return a ? a->GetName() : std::string(); }
        void ResetRuntime()
        {
            Time = 0.0f; CurrentClip = -1; AppliedRevision = 0; LastFrame = -1; Started = false;
        }
        bool UsesFrameList() const { return !Frames.empty(); }
        /// The frame name shown now ("" for a grid animation).
        const std::string& GetCurrentFrameName() const
        {
            static const std::string s_None;
            if (Frames.empty()) return s_None;
            return Frames[(size_t)std::clamp(GetCurrentFrame(), 0, (int)Frames.size() - 1)];
        }
        /// @}

        int GetCurrentFrame() const
        {
            int count = FrameCount > 0 ? FrameCount : 1;
            int f = (int)(Time * FPS);
            f = Loop ? f % count : (f < count ? f : count - 1);
            return StartFrame + f;
        }

        bool IsFinished() const { return !Loop && (int)(Time * FPS) >= FrameCount; }
    };

    /// Animates the colour of the entity's 2D renderer - Sprite Renderer, Circle Renderer, Text or
    /// Tilemap - and of its Light 2D: what is drawn is the renderer's own colour times the gradient's
    /// colour (Keys, 0..1 of Duration) at the animation's time; the renderer's Color itself does not
    /// change. Mode: Once (start -> end), Loop, PingPong (there and back); Cycles > 0 stops it after
    /// that many cycles. StartOffset seconds are skipped at the start (copies out of step). The
    /// editor shows the colour the game starts with (the inspector can preview it playing).
    struct TintAnimationComponent
    {
        std::vector<TintKey> Keys{ TintKey{ 0.0f, glm::vec4(1.0f) }, TintKey{ 1.0f, glm::vec4(1.0f, 0.35f, 0.3f, 1.0f) } };
        float Duration = 1.0f;
        TintPlayMode Mode = TintPlayMode::PingPong;
        Ease Easing = Ease::InOutQuad;
        int Cycles = 0;            ///< Loop / PingPong: 0 = endless
        float StartOffset = 0.0f;  ///< seconds
        bool PlayOnStart = true;

        /// runtime (reset when play starts / stops and for spawned copies)
        bool Playing = false;
        bool EditorPreview = false; ///< the inspector's preview in the editor's scene view
        float Elapsed = 0.0f;

        void Play(bool restart = true) { if (restart) Elapsed = 0.0f; Playing = true; }
        void Stop() { Playing = false; }
        /// Back to the start colour (and stopped).
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
        /// The colour the renderer's colour is multiplied by now.
        glm::vec4 GetColor() const { return TintAnimation::EvaluateGradient(Keys, GetPhase()); }
    };

    /// A circle or ring filling the unit quad of the entity's transform (Renderer2D::DrawCircle):
    /// Thickness 1 a disc, less a ring; Fade softens the edge. Drawn in the 2D order (SortingLayer, SortingOrder).
    struct CircleRendererComponent
    {
        glm::vec4 Color{ 1.0f, 1.0f, 1.0f, 1.0f };
        float Thickness = 1.0f;
        float Fade = 0.005f;
        uint32_t SortingLayer = 0;
        int SortingOrder = 0;
    };

    /// Text. With a UI Transform it is placed in the UI rectangle (its font size: the UI Transform's
    /// FontSize, else the rectangle's height); in the world it is placed around the entity's origin
    /// (1 unit = the font size; MaxWidth: the box it wraps / is cut in).
    struct TextComponent
    {
        std::string Text = "Matn";
        /// A key of the project's language files ("menu.start"): the text shown is that string in the
        /// current language and follows a language change by itself (Localization); Text is shown when
        /// no language has the key. "" = Text as it is.
        std::string LocalizationKey;
        /// Control hints: every "[Name]" of an input action / axis / vector of the project's map is shown as the
        /// controls bound to it, of the device the player uses - "[Jump] - sakrash" reads "Space - sakrash" with
        /// the keyboard and "A - sakrash" with a gamepad, and changes when the player switches (Input::ExpandBindings).
        bool InputHints = false;
        glm::vec4 Color{ 1.0f };
        float Kerning = 0.0f;       ///< em added after every character
        float LineSpacing = 0.0f;   ///< em added to every line's height
        TextAlignH AlignH = TextAlignH::Left;
        TextAlignV AlignV = TextAlignV::Middle;
        bool WordWrap = false;      ///< lines break between words at the rectangle's width (world: MaxWidth)
        TextOverflow Overflow = TextOverflow::Overflow; ///< what does not fit the rectangle (world: MaxWidth)
        float MaxWidth = 0.0f;      ///< world-space text: the box's width in text units (0: none)
        std::string FontPath; ///< empty = default engine font
        Ref<Font> FontAsset;
        uint32_t SortingLayer = 0;
        int SortingOrder = 1000; ///< text draws over the sprites of its layer unless told otherwise

        /// Runtime: the localized string for LocalizationKey and / or Text with its control hints
        /// (UpdateLocalized), never saved.
        std::string LocalizedText;
        std::string LocalizedFor;   ///< the key it was looked up for
        uint64_t LocalizedVersion = 0;
        bool HasLocalizedText = false;
        uint64_t HintsVersion = 0;  ///< Input::GetActiveDeviceVersion the hints were expanded for (0: none)
        std::string HintsSource;    ///< Text the hints were expanded from (when HintsFromText)
        bool HintsFromText = false; ///< the hints were expanded from Text (no language has the key), not the language's string

        /// What is drawn: the localized string when the key has one, else Text (with InputHints expanded).
        const std::string& GetDisplayText() const { return HasLocalizedText ? LocalizedText : Text; }
        /// Looks the key up again when the language (Localization::GetVersion) or the key changed, and
        /// expands the control hints again when the device in use changed. True when the shown text may
        /// have changed.
        bool UpdateLocalized()
        {
            if (LocalizationKey.empty() && !InputHints)
            {
                if (!HasLocalizedText && LocalizedFor.empty() && HintsVersion == 0)
                    return false;
                ResetLocalized();
                return true;
            }
            const uint64_t version = Localization::GetVersion();
            const uint64_t hints = InputHints ? Input::GetActiveDeviceVersion() : 0;
            if (version == LocalizedVersion && LocalizedFor == LocalizationKey && hints == HintsVersion &&
                (!InputHints || !HintsFromText || HintsSource == Text))
                return false;
            LocalizedVersion = version;
            LocalizedFor = LocalizationKey;
            HintsVersion = hints;
            const std::string* text = LocalizationKey.empty() ? nullptr : Localization::Find(LocalizationKey);
            if (InputHints)
            {
                // From the language's string, else from Text (watched: a script may change it, also from "").
                HintsFromText = text == nullptr;
                HintsSource = text ? std::string() : Text;
                LocalizedText = Input::ExpandBindings(text ? *text : Text);
                HasLocalizedText = true;
                return true;
            }
            HintsSource.clear();
            HintsFromText = false;
            HasLocalizedText = text != nullptr;
            LocalizedText = text ? *text : std::string();
            return true;
        }
        void ResetLocalized()
        {
            LocalizedText.clear();
            LocalizedFor.clear();
            LocalizedVersion = 0;
            HasLocalizedText = false;
            HintsVersion = 0;
            HintsSource.clear();
            HintsFromText = false;
        }

        TextLayoutSettings GetLayoutSettings() const { return { AlignH, AlignV, WordWrap, Overflow, Kerning, LineSpacing }; }
        /// Laid out in a UI rectangle, `px` high.
        TextLayoutResult Layout(const UIRect& rect, float px) const
        {
            return TextLayout::Layout(GetDisplayText(), TextMetrics::Of(FontAsset.get()), px, rect, GetLayoutSettings());
        }
        /// In the world: around the origin, in text units (1 = the font size); MaxWidth wide when set
        /// (starting at the origin, centred on it or ending there - AlignH).
        TextLayoutResult LayoutWorld() const
        {
            UIRect box;
            if (MaxWidth > 0.0f)
            {
                const float x0 = AlignH == TextAlignH::Left ? 0.0f : AlignH == TextAlignH::Center ? -MaxWidth * 0.5f : -MaxWidth;
                box = { { x0, 0.0f }, { x0 + MaxWidth, 0.0f } };
            }
            return TextLayout::Layout(GetDisplayText(), TextMetrics::Of(FontAsset.get()), 1.0f, box, GetLayoutSettings());
        }
        /// LayoutWorld kept until the text, its font (or the font's glyphs / kerning) or the layout settings
        /// change: a text drawn every frame is laid out once (task 482).
        const TextLayoutResult& GetWorldLayout() const
        {
            const std::string& text = GetDisplayText();
            const Font* font = FontAsset.get();
            const uint64_t revision = font ? font->GetRevision() : 0;
            const TextLayoutSettings settings = GetLayoutSettings();
            if (!WorldLayoutValid || WorldLayoutFont != font || WorldLayoutRevision != revision || WorldLayoutWidth != MaxWidth ||
                !(WorldLayoutSettings == settings) || WorldLayoutText != text)
            {
                WorldLayout = LayoutWorld();
                WorldLayoutText = text;
                WorldLayoutFont = font;
                WorldLayoutRevision = revision;
                WorldLayoutWidth = MaxWidth;
                WorldLayoutSettings = settings;
                WorldLayoutValid = true;
            }
            return WorldLayout;
        }

        /// Runtime: GetWorldLayout's cache, never saved.
        mutable TextLayoutResult WorldLayout;
        mutable std::string WorldLayoutText;
        mutable const Font* WorldLayoutFont = nullptr;
        mutable uint64_t WorldLayoutRevision = 0;
        mutable float WorldLayoutWidth = 0.0f;
        mutable TextLayoutSettings WorldLayoutSettings;
        mutable bool WorldLayoutValid = false;
    };

    /// A 3D mesh drawn by Renderer3D: a primitive (Type) or a model file (ModelPath with Type Model),
    /// with a material asset (MaterialPath, per submesh SubmeshMaterials) or the inline material (Mat).
    struct MeshRendererComponent
    {
        PrimitiveMesh Type = PrimitiveMesh::Cube;
        std::string ModelPath;    ///< .gltf / .glb / .obj (when Type == Model)
        Ref<Mesh> MeshAsset;      ///< runtime cache (primitives)
        Ref<Model> ModelAsset;    ///< runtime cache (models)
        /// Material asset (.zmat, relative to assets). When set, the primitive and every submesh of
        /// the model are drawn with it and the inline material below is not used.
        std::string MaterialPath;
        /// Models: a material asset per submesh (index = submesh, "" = MaterialPath or, without
        /// one, the model's own material with the inline tint).
        std::vector<std::string> SubmeshMaterials;
        Ref<MaterialAsset> MaterialRef;                     ///< runtime cache of MaterialPath
        std::vector<Ref<MaterialAsset>> SubmeshMaterialRefs; ///< runtime cache of SubmeshMaterials
        /// Runtime only (never saved; cleared when play starts and stops): this entity's own copies
        /// made by scripts (ScriptableEntity::GetMaterial), so their edits leave the shared asset
        /// and every other entity alone. MaterialInstance takes the place of the main material -
        /// the material asset, or without one the inline material (models: the tint of every part);
        /// SubmeshMaterialInstances[i] takes the place of part i's whole material.
        Ref<MaterialAsset> MaterialInstance;
        std::vector<Ref<MaterialAsset>> SubmeshMaterialInstances;
        Material Mat;             ///< inline material: primitives: the material; models: tint/overrides
        std::string AlbedoPath;   ///< texture paths relative to assets
        std::string NormalPath;
        std::string MetalRoughPath;
        std::string EmissivePath;
        bool CastShadows = true;

        /// @name Morph targets
        /// Morph targets (glTF blend shapes): one weight per target of the model, every morph
        /// group's after each other (Model::GetMorphGroups). Empty = the model's own weights. Saved
        /// with the scene; an Animator whose clips animate weights takes their place while it plays
        /// (the targets its clip does not animate keep these). Scripts:
        /// @code
        ///   GetComponent<MeshRendererComponent>().SetMorphWeight("Smile", 0.8f);
        /// @endcode
        /// @{
        std::vector<float> MorphWeights;
        /// runtime (never saved): this entity's copies of the morphed submeshes (by index; null:
        /// the model's shared mesh), the weights they show and what they were made from
        std::vector<Ref<Mesh>> MorphMeshes;
        std::vector<std::vector<float>> MorphMeshWeights;
        std::vector<const Mesh*> MorphMeshSources;
        uint64_t MorphMeshOwner = 0;
        /// @}

        uint32_t GetMorphTargetCount() const { return ModelAsset ? ModelAsset->GetMorphWeightCount() : 0u; }
        /// The weight of target `index` / `name` (or "Group/Target"): this entity's, else the model's own.
        float GetMorphWeight(uint32_t index) const
        {
            if (index < MorphWeights.size()) return MorphWeights[index];
            if (!ModelAsset || index >= ModelAsset->GetMorphWeightCount()) return 0.0f;
            for (const auto& g : ModelAsset->GetMorphGroups())
                if (index >= g.Offset && index < g.Offset + g.GetCount())
                    return index - g.Offset < g.DefaultWeights.size() ? g.DefaultWeights[index - g.Offset] : 0.0f;
            return 0.0f;
        }
        float GetMorphWeight(const std::string& name) const
        {
            const int index = ModelAsset ? ModelAsset->FindMorphTarget(name) : -1;
            return index >= 0 ? GetMorphWeight((uint32_t)index) : 0.0f;
        }
        /// Sets one weight (the others start at the model's own). False when the model has no such target.
        bool SetMorphWeight(uint32_t index, float weight)
        {
            if (!ModelAsset || index >= ModelAsset->GetMorphWeightCount()) return false;
            if (MorphWeights.size() != ModelAsset->GetMorphWeightCount()) MorphWeights = ModelAsset->GetDefaultMorphWeights();
            MorphWeights[index] = weight;
            return true;
        }
        bool SetMorphWeight(const std::string& name, float weight)
        {
            const int index = ModelAsset ? ModelAsset->FindMorphTarget(name) : -1;
            return index >= 0 && SetMorphWeight((uint32_t)index, weight);
        }
        /// Back to the model's own weights.
        void ResetMorphWeights() { MorphWeights.clear(); }
        void ReleaseMorphMeshes()
        {
            MorphMeshes.clear(); MorphMeshWeights.clear(); MorphMeshSources.clear(); MorphMeshOwner = 0;
        }
    };

    // ---- animation state machine data
    struct AnimatorParameter
    {
        enum class Type { Float = 0, Bool, Trigger };
        std::string Name;
        Type Kind = Type::Float;
        float Value = 0.0f; ///< bools/triggers: 0 or 1
    };

    struct AnimatorCondition
    {
        enum class Op { Greater = 0, Less, IsTrue, IsFalse, Triggered };
        std::string Parameter;
        Op Mode = Op::Greater;
        float Threshold = 0.0f;
    };

    /// One clip of a 1D blend tree: it plays alone where the tree's parameter equals Threshold and
    /// shares the weight with its neighbour between two thresholds.
    struct AnimatorBlendMotion
    {
        std::string Clip;
        float Threshold = 0.0f;
        float Speed = 1.0f;         ///< this clip's own rate inside the tree
        glm::vec2 Position{ 0.0f }; ///< sample position in a 2D blend field
    };

    /// What a state plays: one clip, or a 1D blend tree - clips mixed by a float parameter (idle /
    /// walk / run by the character's speed).
    enum class AnimatorStateType : uint8_t
    {
        Clip = 0,
        BlendTree1D,
        BlendTree2D
    };

    struct AnimatorState
    {
        std::string Name = "State";
        std::string Clip;
        float Speed = 1.0f;
        bool Loop = true;
        std::string SpeedParameter; ///< optional: multiplies Speed by a float parameter
        /// @name 1D blend tree
        /// 1D blend tree (Type BlendTree1D): BlendParameter's value picks the motions by their
        /// thresholds. They play in step - one normalized time, so the feet stay in phase while a
        /// walk turns into a run - and a cycle lasts the weighted mix of their lengths.
        /// @{
        AnimatorStateType Type = AnimatorStateType::Clip;
        std::string BlendParameter;
        std::vector<AnimatorBlendMotion> Motions;
        glm::vec2 GraphPosition{ 0.0f }; ///< the node in the editor's state graph (canvas units)
        /// @}

        std::string BlendParameterY; ///< second float axis for BlendTree2D
        float Duration = 0.0f; ///< optional model-free state clock in seconds; 0 keeps legacy clip timing
        bool IsBlendTree() const { return Type == AnimatorStateType::BlendTree1D || Type == AnimatorStateType::BlendTree2D; }
    };

    /// How a layer's pose goes onto the animation below it: Override - replaces it by the layer's
    /// weight; Additive - what the clip changes from its first frame is added on top (a nod, breathing).
    enum class AnimatorLayerBlend : uint8_t { Override = 0, Additive };

    /// What an Animator does while its model is off screen - in none of the views the scene was drawn
    /// in last frame (its mesh's posed bounds outside every camera's view):
    /// - AlwaysAnimate: everything as usual;
    /// - CullPose: time, the state machine, events and root motion go on, the skeleton is not posed
    ///   (nobody sees it) - it is posed again the frame after it comes into view;
    /// - CullCompletely: nothing advances until it is seen again (it waits where it was).
    enum class AnimatorCulling : uint8_t { AlwaysAnimate = 0, CullPose, CullCompletely };

    /// An Animator layer: a clip of its own played over the base animation (the state machine or
    /// Clip) on the bones of its mask only - e.g. the upper body (mask "Spine") waving while the
    /// legs walk. Scripts: AnimatorComponent::PlayLayer / SetLayerWeight.
    struct AnimatorLayer
    {
        std::string Name = "Layer";
        std::string Clip;                       ///< "" = nothing: the layer does not change the pose
        float Speed = 1.0f;
        bool Loop = true;
        float Weight = 1.0f;                    ///< 0..1 (SetLayerWeight fades to a new one)
        std::string WeightParameter;            ///< optional: a float parameter (0..1) multiplies Weight
        AnimatorLayerBlend Blend = AnimatorLayerBlend::Override;
        std::vector<std::string> MaskBones;     ///< these bones and every bone below them (none: the whole body)

        /// runtime (reset when play starts)
        float Time = 0.0f;
        std::string PreviousClip;               ///< crossfading from it into Clip
        float PreviousTime = 0.0f;
        float PreviousSpeed = 1.0f;
        bool PreviousLoop = true;
        float FadeDuration = 0.0f;
        float FadeElapsed = 0.0f;
        float CurrentWeight = -1.0f;            ///< the weight in effect (-1: not started - Weight)
        float WeightFadeSpeed = 0.0f;           ///< per second toward Weight (0: at once)
        bool EventsAtStart = true;
        float EffectiveWeight = 0.0f;           ///< the last pose's weight (parameter and fade-in included; inspector)

        bool IsFading() const { return FadeDuration > 0.0f && FadeElapsed < FadeDuration; }
        void ResetRuntime()
        {
            Time = 0.0f;
            PreviousClip.clear();
            PreviousTime = 0.0f;
            FadeDuration = FadeElapsed = 0.0f;
            CurrentWeight = -1.0f;
            WeightFadeSpeed = 0.0f;
            EventsAtStart = true;
            EffectiveWeight = 0.0f;
        }
    };

    /// A moment of a clip that tells the entity's script (ScriptableEntity::OnAnimationEvent) and the
    /// scene's event bus ("AnimationEvent") - footsteps, hits, sounds. Time is 0..1 of the clip.
    struct AnimatorEvent
    {
        std::string Clip;       ///< animation name ("" = every clip)
        float Time = 0.0f;      ///< normalized: 0 = its start, 1 = its end
        std::string Name;
        std::string Parameter;  ///< optional text handed over with it
    };

    struct AnimatorTransition
    {
        int From = -1;          ///< -1 = any state
        int To = 0;
        float Fade = 0.2f;
        bool HasExitTime = false;
        float ExitTime = 1.0f;  ///< normalized (1 = end of the clip)
        std::vector<AnimatorCondition> Conditions;
        int Priority = 0; ///< larger wins; equal priority keeps authoring order
        bool InterruptDuringFade = true; ///< legacy behaviour; false waits for the current fade
    };

    /// Plays skeletal animations of the entity's (skinned glTF) MeshRenderer model.
    /// Either drive it directly with Play(), or define States/Transitions/Parameters
    /// (state machine) and only set parameters from scripts.
    struct AnimatorComponent
    {
        std::string Clip;          ///< animation name ("" = first clip)
        float Speed = 1.0f;
        bool Loop = true;
        bool Playing = true;
        bool PlayInEditor = true;
        float DefaultFade = 0.2f;  ///< seconds used by Play() when no fade is given

        /// runtime
        float Time = 0.0f;         ///< seconds into Clip; a blend tree (BlendTree >= 0): its normalized time (1 = one cycle)
        int BlendTree = -1;        ///< the state whose blend tree plays instead of Clip (state machine)
        std::vector<glm::mat4> JointMatrices;
        std::vector<float> BlendWeights; ///< the playing blend tree's weights, one per motion (inspector)
        /// crossfade state: blending from the previous clip / blend tree into the current one
        std::string PreviousClip;
        int PreviousBlendTree = -1;
        int PreviousState = -1;    ///< the state the transition came from (-1: none / Play())
        float PreviousTime = 0.0f;
        float PreviousSpeed = 1.0f;
        bool PreviousLoop = true;
        float FadeDuration = 0.0f;
        float FadeElapsed = 0.0f;

        /// Morph target weights the clips animate (runtime): the model's flat list - the Mesh
        /// Renderer's weights where the clips have none. Empty: the clips animate none.
        std::vector<float> MorphWeights;

        /// @name Editor preview
        /// Editor preview (never saved): the Animation window's scrubbing shows this clip at
        /// this time in the scene view instead of what the Animator plays (edit mode only).
        /// @{
        struct EditorPreviewState
        {
            bool Active = false;
            std::string Clip;
            float Time = 0.0f;
        };
        EditorPreviewState EditorPreview;
        /// @}

        /// Events of the clips (in play mode).
        std::vector<AnimatorEvent> Events;
        bool EventsAtStart = true; ///< runtime: the clip just started - events right at its start fire too

        /// @name Root motion
        /// Root motion: the root bone's walk (XZ) and turn (yaw) moves the entity instead of
        /// the mesh drifting away from it; the pose keeps the root in place (its height stays).
        /// With a Character Controller 3D it walks the character (its MoveVelocity), else the
        /// transform. The editor's preview stays in place.
        /// @{
        bool ApplyRootMotion = false;
        std::string RootMotionBone;                ///< joint name ("" = the first animated root joint)
        /// @}

        /// @name Performance
        /// @{
        AnimatorCulling Culling = AnimatorCulling::AlwaysAnimate;
        /// Clips are read from poses cached at PoseCacheRate frames per second (made once per clip
        /// of the model and rate, shared by all its animators): cheaper for clips with many keys and bones,
        /// a little less exact between the cached frames (step keys stay exact).
        bool CachePoses = false;
        float PoseCacheRate = 30.0f;
        /// runtime: the update the model was last seen after (Scene::MarkVisibleAnimators), whether
        /// the last update found it off screen, and what its last pose was made from - an update
        /// with the same inputs (paused, a finished clip, speed 0) keeps that pose.
        uint64_t VisibleFrame = 0;
        bool Culled = false;
        std::vector<uint32_t> PoseSignature;
        glm::vec3 RootMotionVelocity{ 0.0f };      ///< runtime: the last frame's root motion (world units / s)
        float RootMotionTurnRate = 0.0f;           ///< runtime: radians / s
        /// @}

        /// Switch clip with a smooth crossfade (fade < 0 = DefaultFade, 0 = instant).
        void Play(const std::string& clip, bool loop = true, float speed = 1.0f, float fade = -1.0f)
        {
            if (BlendTree < 0 && Clip == clip && Playing) { Speed = speed; Loop = loop; return; }
            BeginCrossfade(fade);
            Clip = clip; BlendTree = -1; Loop = loop; Speed = speed; Playing = true; Time = 0.0f;
            EventsAtStart = true;
        }
        /// Plays state `state`'s blend tree (the state machine does this), crossfading like Play.
        void PlayBlendTree(int state, bool loop = true, float speed = 1.0f, float fade = -1.0f)
        {
            if (BlendTree == state && Playing) { Speed = speed; Loop = loop; return; }
            BeginCrossfade(fade);
            BlendTree = state; Loop = loop; Speed = speed; Playing = true; Time = 0.0f;
            EventsAtStart = true;
        }
        /// What plays now becomes the crossfade's "previous" (fade < 0 = DefaultFade, 0 = none).
        void BeginCrossfade(float fade)
        {
            if (fade < 0.0f) fade = DefaultFade;
            PreviousClip = Clip; PreviousBlendTree = BlendTree; PreviousTime = Time; PreviousSpeed = Speed; PreviousLoop = Loop;
            FadeDuration = fade; FadeElapsed = 0.0f;
        }
        bool IsFading() const { return FadeDuration > 0.0f && FadeElapsed < FadeDuration; }
        /// 0..1 of the crossfade (1 when there is none).
        float GetFadeProgress() const { return IsFading() ? FadeElapsed / FadeDuration : 1.0f; }
        /// How far the current clip / blend tree is: 1 = one cycle. Loops keep counting (2.5: half of
        /// the third cycle). A clip the model does not have counts as finished (1).
        float GetNormalizedTime(const Model* model) const
        {
            if (BlendTree >= 0)
                return Time;
            if (!model && CurrentState >= 0 && CurrentState < (int)States.size() && States[CurrentState].Duration > 0.0f)
                return Time / States[CurrentState].Duration;
            const int index = !model ? -1 : Clip.empty() ? (model->GetAnimations().empty() ? -1 : 0) : model->FindAnimation(Clip);
            if (index < 0)
                return 1.0f;
            const float duration = model->GetAnimations()[(size_t)index].Duration;
            return duration > 0.0f ? Time / duration : 1.0f;
        }

        /// @name Layers
        /// Layers: clips over the base animation on parts of the body (avatar masks)
        /// @{
        std::vector<AnimatorLayer> Layers;
        AnimatorLayer* FindLayer(const std::string& name)
        {
            for (auto& l : Layers) if (l.Name == name) return &l;
            return nullptr;
        }
        /// Plays a clip on a layer, crossfading from what the layer played (fade < 0 = DefaultFade;
        /// from nothing: the layer fades in). False when there is no such layer.
        bool PlayLayer(const std::string& layer, const std::string& clip, bool loop = true, float speed = 1.0f, float fade = -1.0f)
        {
            AnimatorLayer* l = FindLayer(layer);
            if (!l) return false;
            if (l->Clip == clip) { l->Speed = speed; l->Loop = loop; return true; }
            if (fade < 0.0f) fade = DefaultFade;
            l->PreviousClip = l->Clip; l->PreviousTime = l->Time; l->PreviousSpeed = l->Speed; l->PreviousLoop = l->Loop;
            l->FadeDuration = fade; l->FadeElapsed = 0.0f;
            l->Clip = clip; l->Loop = loop; l->Speed = speed; l->Time = 0.0f; l->EventsAtStart = true;
            return true;
        }
        /// The layer's weight goes to `weight` (0..1) over `fade` seconds (0: at once).
        bool SetLayerWeight(const std::string& layer, float weight, float fade = 0.0f)
        {
            AnimatorLayer* l = FindLayer(layer);
            if (!l) return false;
            weight = std::clamp(weight, 0.0f, 1.0f);
            if (l->CurrentWeight < 0.0f) l->CurrentWeight = l->Weight;
            l->WeightFadeSpeed = fade > 0.0f ? std::abs(weight - l->CurrentWeight) / fade : 0.0f;
            if (fade <= 0.0f) l->CurrentWeight = weight;
            l->Weight = weight;
            return true;
        }
        float GetLayerWeight(const std::string& layer)
        {
            const AnimatorLayer* l = FindLayer(layer);
            return !l ? 0.0f : l->CurrentWeight < 0.0f ? l->Weight : l->CurrentWeight;
        }
        /// @}

        /// @name State machine
        /// @{
        std::vector<AnimatorParameter> Parameters;
        std::vector<AnimatorState> States;
        std::vector<AnimatorTransition> Transitions;
        int DefaultState = 0;
        int CurrentState = -1; ///< runtime
        bool StateMachinePaused = false; ///< runtime direct clip mode; SetState resumes the controller
        glm::vec2 AnyStateGraphPosition{ 0.0f }; ///< the "any state" node of the editor's graph
        /// @}

        AnimatorParameter* FindParameter(const std::string& name)
        {
            for (auto& p : Parameters) if (p.Name == name) return &p;
            return nullptr;
        }
        bool SetFloat(const std::string& name, float v);
        bool SetBool(const std::string& name, bool v);
        bool SetTrigger(const std::string& name);
        bool ResetTrigger(const std::string& name);
        bool SetFloatDamped(const std::string& name, float target, float dampingSeconds, float delta);
        int SelectTransition(float normalizedTime) const;
        void ConsumeTransition(int index);
        void ComputeStateWeights(const AnimatorState& state, std::vector<float>& weights) const;
        std::vector<std::string> ValidateController(const Model* model = nullptr) const;
        float GetFloat(const std::string& name) { auto* p = FindParameter(name); return p ? p->Value : 0.0f; }
        bool GetBool(const std::string& name) { auto* p = FindParameter(name); return p && p->Value != 0.0f; }
        const char* GetCurrentStateName() const
        {
            return CurrentState >= 0 && CurrentState < (int)States.size() ? States[CurrentState].Name.c_str() : "";
        }
        /// The state machine enters this state now (crossfading over `fade` seconds; < 0: DefaultFade).
        bool SetState(const std::string& name, float fade = -1.0f)
        {
            for (int i = 0; i < (int)States.size(); i++)
                if (States[(size_t)i].Name == name)
                {
                    EnterState(i, fade);
                    return true;
                }
            return false;
        }
        void EnterState(int index, float fade = -1.0f);
        /// The blend tree playing now (nullptr: a clip).
        const AnimatorState* GetBlendTreeState() const
        {
            return BlendTree >= 0 && BlendTree < (int)States.size() && States[(size_t)BlendTree].IsBlendTree() ? &States[(size_t)BlendTree] : nullptr;
        }
        bool UsesStateMachine() const { return !States.empty() && !StateMachinePaused; }

        /// Play starts / stops (and spawned copies): events from the start again, no root motion
        /// yet, the layers at their own weights.
        void ResetRuntime()
        {
            EventsAtStart = true;
            StateMachinePaused = false;
            EditorPreview = {};
            RootMotionVelocity = glm::vec3(0.0f);
            RootMotionTurnRate = 0.0f;
            VisibleFrame = 0;
            Culled = false;
            PoseSignature.clear();
            for (auto& l : Layers)
                l.ResetRuntime();
        }
    };

    /// How an Animation Player plays its clips: as the clip says, or Once / Loop / PingPong.
    enum class AnimationPlayerWrap : uint8_t { Clip = 0, Once, Loop, PingPong };

    /// Plays property animation clips (.zanim, made in the Animation window) on the entity and its
    /// children: transforms, colours, lights, UI and script float fields, with events at moments of
    /// the clip (ScriptableEntity::OnAnimationEvent, the event bus' "AnimationEvent"). The first clip
    /// starts with the game (PlayOnStart); scripts play the others by name (the file name without
    /// .zanim) or path:
    /// @code
    ///   GetComponent<AnimationPlayerComponent>().Play("DoorOpen");      // forwards from the start
    ///   GetComponent<AnimationPlayerComponent>().Play("DoorOpen", -1.0f); // backwards from the end
    /// @endcode
    /// A player that is not playing writes nothing, so scripts may change the same values then.
    struct AnimationPlayerComponent
    {
        std::vector<std::string> Clips;           ///< .zanim files, relative to Assets
        float Speed = 1.0f;                       ///< times the speed each Play() gives
        bool PlayOnStart = true;                  ///< the first clip
        AnimationPlayerWrap Wrap = AnimationPlayerWrap::Clip;

        /// runtime (reset when play starts / stops and for spawned copies)
        std::vector<Ref<PropertyClipAsset>> ClipAssets; ///< the loaded Clips (ResolveClips)
        std::vector<std::string> LoadedClips;     ///< the paths ClipAssets were looked up for
        int Current = -1;                         ///< index into Clips (-1: none)
        ClipCursor Cursor;
        float PlaySpeed = 1.0f;                   ///< the speed the last Play() was given
        bool Playing = false;
        bool Started = false;                     ///< PlayOnStart was looked at
        bool PoseDirty = false;                   ///< SetTime / Play / Stop: the pose is written on the next update

        /// Loads the clips that changed (Clips edited) - Play and the scene call it.
        void ResolveClips();
        /// The clip with this name (file name without .zanim) or path; -1 when there is none.
        int FindClip(std::string_view nameOrPath) const;
        /// Plays a clip ("" = the current one, else the first) from its start (speed < 0: backwards
        /// from its end). False when there is no such clip.
        bool Play(std::string_view clip = {}, float speed = 1.0f);
        bool Play(int index, float speed = 1.0f);
        /// Plays a clip from `seconds` into it (clamped) in the direction of `speed`: a door that
        /// starts closing while half open turns round where it is. The events right at `seconds`
        /// do not fire (it is there already).
        bool PlayFrom(std::string_view clip, float seconds, float speed = 1.0f);
        /// Stops where it is (the values stay); Resume goes on from there.
        void Stop() { Playing = false; }
        void Resume() { if (Current >= 0) { Playing = true; Cursor.Finished = false; } }
        bool IsPlaying() const { return Playing; }
        /// Moves to `seconds` into the current clip (the pose is written on the next update, also
        /// while stopped).
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

    /// A part of the scene kept in a scene file of its own (a level's rooms, a shared lighting set-up,
    /// a piece several levels use): at runtime the scene manager loads that file additively next to
    /// the scene this entity is in - its own entities, scripts and physics world - with its root
    /// entities placed by this entity's world transform (an offset applied once, when it is loaded:
    /// moving this entity later does not move them). Unloading the scene unloads its sub-scenes.
    /// The editor draws the sub-scene in place, read-only (open its file to edit it).
    struct SubSceneComponent
    {
        std::string ScenePath;        ///< .zscene, relative to the assets
        bool LoadOnStart = true;      ///< with the scene (else SceneManager::LoadSubScene / the script's LoadSubScene)
        bool ApplyTransform = true;   ///< the roots placed by this entity's transform (else where their file has them)
        bool ShowInEditor = true;     ///< drawn in place in the editor's scene view
    };

    /// Puts the entity on a bone of a skinned model - a sword in the hand, a hat on the head, a
    /// lantern on the belt. The bone, as the model's Animator poses it (the editor's preview too),
    /// takes the place of the entity's parent: its Transform is the offset in the bone's space, so
    /// the gizmo moves it relative to the bone and the offset is what is saved. Target: the entity
    /// whose Mesh Renderer has the skinned model (empty: the nearest ancestor that has one).
    struct BoneAttachmentComponent
    {
        EntityRef Target;
        std::string Bone;                 ///< joint name of the model's skeleton
        /// false: only the bone's position and turn are followed - its scale (an import's
        /// centimetres, a squash) is left out; the model entity's own scale still counts.
        bool InheritBoneScale = false;

        /// runtime cache: the joint Bone was found at (checked by name on every use)
        int CachedJoint = -1;
    };

    /// A camera (SceneCamera: perspective or orthographic). The game is seen through the primary one
    /// (Primary, Scene::GetPrimaryCameraEntity); without FixedAspectRatio its aspect follows the
    /// viewport. ClearColor is the background when the scene draws no sky.
    struct CameraComponent
    {
        SceneCamera Camera;
        bool Primary = true;
        bool FixedAspectRatio = false;
        glm::vec4 ClearColor{ 0.08f, 0.09f, 0.11f, 1.0f };
        /// Scene::ShakeCamera / ScriptableEntity::ShakeCamera shake the view of the camera the game
        /// is seen through; its strength settings are saved, the trauma is runtime only.
        CameraShake Shake;
        /// Orthographic cameras: whole screen pixels per texture pixel (see PixelPerfectSettings);
        /// the game view (play) only - the editor's scene view is not changed.
        PixelPerfectSettings PixelPerfect;
        bool IsPixelPerfect() const { return PixelPerfect.Enabled && Camera.GetProjectionType() == SceneCamera::ProjectionType::Orthographic; }
    };

    // ------------------------------------------------------------------ 2D camera follow
    /// Moves its entity (usually the camera) after Target while the game runs: once per frame,
    /// after physics and the scripts' OnLateUpdate; only x / y change (z stays). See Camera2DFollow.
    /// - Offset: added to the target's position (look a little above the player);
    /// - Damping: seconds to catch up about 63% of the way, per axis (0 = locked to the target);
    /// - DeadZone: a box (full width / height, units) around the view's centre inside which the
    ///   target moves without the camera moving;
    /// - LookAhead: how far (units, per axis) the view runs ahead in the direction the target last
    ///   moved, eased in with LookAheadDamping seconds;
    /// - Bounds: the camera's whole view (at the target's depth) stays inside this world rectangle.
    struct Camera2DFollowComponent
    {
        EntityRef Target;
        glm::vec2 Offset{ 0.0f, 1.0f };
        glm::vec2 Damping{ 0.2f, 0.3f };
        glm::vec2 DeadZone{ 1.0f, 1.5f };
        glm::vec2 LookAhead{ 1.5f, 0.0f };
        float LookAheadDamping = 0.6f;
        float LookAheadMinSpeed = 0.5f;   ///< units / s the target must move before the look-ahead turns
        bool UseBounds = false;
        glm::vec2 BoundsMin{ -20.0f, -10.0f };
        glm::vec2 BoundsMax{ 20.0f, 10.0f };
        bool SnapOnStart = true;          ///< play starts with the camera on the target

        /// runtime (cleared when play starts / stops)
        bool Initialized = false;
        bool SnapRequested = false;
        glm::vec2 LastTarget{ 0.0f };
        glm::vec2 LookAheadDirection{ 0.0f }; ///< -1, 0 or 1 per axis
        glm::vec2 LookAheadOffset{ 0.0f };

        /// Jumps onto the target on the next update (after a teleport / respawn).
        void Snap() { SnapRequested = true; }
        void ResetRuntime()
        {
            Initialized = SnapRequested = false;
            LastTarget = LookAheadDirection = LookAheadOffset = glm::vec2(0.0f);
        }
    };

    /// Light from a direction (the entity's forward, -Z), the same everywhere: the sun. The first active
    /// one that casts shadows (else the first) is the sun with cascaded shadows; up to three more light
    /// the scene without shadows.
    struct DirectionalLightComponent
    {
        glm::vec3 Color{ 1.0f, 0.96f, 0.9f };
        float Intensity = 3.0f;
        bool CastShadows = true;
    };

    /// Light from the entity's position in every direction, fading out to nothing at Radius.
    struct PointLightComponent
    {
        glm::vec3 Color{ 1.0f };
        float Intensity = 5.0f;
        float Radius = 10.0f;
    };

    /// A cone of light from the entity's position along its forward axis (-Z, like the directional
    /// light). Angles are measured from the axis in degrees: full brightness inside InnerAngle,
    /// fading smoothly to nothing at OuterAngle. Range attenuates like a point light's radius.
    struct SpotLightComponent
    {
        glm::vec3 Color{ 1.0f };
        float Intensity = 20.0f;
        float Range = 15.0f;
        float InnerAngle = 20.0f;
        float OuterAngle = 30.0f;

        static constexpr float MaxAngle = 89.0f;
        /// The angles the renderer uses: outer in (0, MaxAngle], inner in [0, outer].
        float GetOuterAngle() const { return std::clamp(OuterAngle, 0.1f, MaxAngle); }
        float GetInnerAngle() const { return std::clamp(InnerAngle, 0.0f, GetOuterAngle()); }
    };

    // ------------------------------------------------------------------ 2D lights
    /// A light for 2D scenes. It lights the Lit sprites and tilemaps (their "Lit" flag) within Radius
    /// of it: fully inside InnerRadius, then fading out ((1 - t)^Falloff: 1 linear, 2 soft, 4 a bright
    /// core). Spot: a cone SpotAngle degrees wide along the entity's +X axis (its rotation turns it),
    /// the outer SpotSoftness of it fading. Lights add up on top of the scene's 2D ambient light
    /// (Scene settings > 2D lighting); renderers that are not Lit are drawn as before. Flicker
    /// makes the intensity waver (torches, fire).
    enum class Light2DType { Point = 0, Spot };
    /// A 2D light (see Light2DType above: point or spot) on the Lit sprites and tilemaps.
    struct Light2DComponent
    {
        Light2DType Type = Light2DType::Point;
        glm::vec3 Color{ 1.0f, 0.85f, 0.6f };
        float Intensity = 1.0f;
        float Radius = 4.0f;
        float InnerRadius = 0.0f;
        float Falloff = 2.0f;
        float SpotAngle = 60.0f;    ///< degrees, the whole cone
        float SpotSoftness = 0.3f;  ///< 0..1 of the cone's half angle that fades
        float Flicker = 0.0f;       ///< 0..1
        float FlickerSpeed = 8.0f;  ///< noise samples per second
    };

    /// Capsule character (Jolt CharacterVirtual): walks on slopes/stairs, collides with the world.
    /// Scripts set the horizontal velocity with Move() and call Jump(); gravity is applied automatically.
    struct CharacterController3DComponent
    {
        float Radius = 0.35f;
        float Height = 1.8f;
        float MaxSlope = 50.0f;     ///< degrees
        float StepHeight = 0.35f;
        float Mass = 70.0f;
        float GravityScale = 1.0f;
        bool StickToFloor = true;
        /// Its collision layer (0..15, Project settings > Physics layers - the same layers and matrix
        /// as 2D physics): which bodies it walks into / stands on.
        int Layer = 0;
        /// Walking into a dynamic body pushes it along (towards the character's speed where they touch)
        /// with at most PushForce newtons: 100 N moves a crate of a few dozen kg, a heavy one needs
        /// more. PushBodies off: dynamic bodies are walls to it (they still push the character).
        bool PushBodies = true;
        float PushForce = 100.0f; ///< N

        /// control (set by scripts)
        glm::vec3 MoveVelocity{ 0.0f }; ///< horizontal, world space
        float VerticalVelocity = 0.0f;

        /// runtime (read by scripts)
        bool Grounded = false;
        /// How fast it really moved in the last physics step (after collisions, stairs, sticking
        /// to the floor): the change of its position / the step.
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
    /// One layer of a tilemap: its own grid of tiles over the tilemap's Width x Height. Layers draw
    /// in list order (the first at the back) unless their SortingOrder says otherwise.
    struct TilemapLayer
    {
        std::string Name = "Asosiy";
        std::vector<int> Tiles;   ///< Width * Height, row-major, y = 0 is the bottom row, -1 = empty
        bool Visible = true;      ///< drawn (editor and game)
        bool Collision = true;    ///< its tiles are solid (when the tilemap's Collision is on)
        int SortingOrder = 0;     ///< added to the tilemap's SortingOrder (e.g. +2: in front of the sprites)
        float Opacity = 1.0f;     ///< multiplies the tilemap colour's alpha

        bool operator==(const TilemapLayer&) const = default;
    };

    /// Grid of tiles from a tileset texture. The entity position is the bottom-left corner. Scene
    /// files from before layers existed load as one layer (layer 0).
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
        std::vector<TilemapLayer> Layers = std::vector<TilemapLayer>(1); ///< at least one
        glm::vec4 Color{ 1.0f };
        uint32_t SortingLayer = 0;
        int SortingOrder = -1;
        bool Lit = false;        ///< lit by the 2D lights (Light2DComponent) and the scene's 2D ambient

        bool Collision = true;   ///< the tiles of the layers with Collision are solid (static 2D physics)
        float Friction = 0.6f;
        float Restitution = 0.0f;
        std::string MaterialPath; ///< physics material asset (.zphysmat), "" = Friction / Restitution above
        int CollisionLayer = 0;   ///< the 2D collision layer of the tiles (Project settings > Physics layers)

        void* RuntimeBody = nullptr; ///< b2Body*

        /// Width / Height at least 1, at least one layer, every layer's grid Width * Height big.
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
        /// The tile at (x, y) of a layer: -1 for empty cells, cells outside the map and unknown layers.
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
        /// A tile on any layer with Collision (what the physics body is built from).
        bool IsSolid(int x, int y) const
        {
            for (int i = 0; i < (int)Layers.size(); i++)
                if (Layers[(size_t)i].Collision && Get(x, y, i) >= 0) return true;
            return false;
        }
        /// Resize keeping existing tiles anchored at the bottom-left (every layer).
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

        /// @name Layers
        /// @{
        int FindLayer(std::string_view name) const
        {
            for (int i = 0; i < (int)Layers.size(); i++)
                if (Layers[(size_t)i].Name == name) return i;
            return -1;
        }
        /// `base`, or "<base> <n>" with the first n no layer uses.
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
        /// A new empty layer on top (drawn last), without collision. Returns its index, -1 when the
        /// tilemap has MaxLayers already.
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
        /// Every layer but the last one left can be removed.
        bool RemoveLayer(int layer)
        {
            if (!IsValidLayer(layer) || Layers.size() <= 1) return false;
            Layers.erase(Layers.begin() + layer);
            return true;
        }
        /// Moves the layer at `from` to position `to` (drawing order).
        bool MoveLayer(int from, int to)
        {
            if (!IsValidLayer(from) || !IsValidLayer(to) || from == to) return false;
            TilemapLayer moved = std::move(Layers[(size_t)from]);
            Layers.erase(Layers.begin() + from);
            Layers.insert(Layers.begin() + to, std::move(moved));
            return true;
        }
        /// Number of non-empty cells of a layer.
        size_t CountTiles(int layer) const
        {
            const TilemapLayer* l = GetLayer(layer);
            return l ? (size_t)std::count_if(l->Tiles.begin(), l->Tiles.end(), [](int t) { return t >= 0; }) : 0;
        }
        /// @}
    };

    // ------------------------------------------------------------------ parallax (2D)
    /// Moves the entity (and its children) with the game camera when the scene is drawn: Factor 0 =
    /// an ordinary world object, 1 = fixed on the screen (infinitely far), 0.5 = half as fast as the
    /// camera (a far background); negative values move against the camera (a near foreground). It is
    /// where it was placed while the camera is at its position. RepeatX / RepeatY draw its sprite
    /// again every Spacing units (0 = the sprite's own size) so it fills the view endlessly.
    /// Only the game camera moves it (the editor's scene view shows it as placed, unless View >
    /// Parallax preview is on); the transform itself never changes, so physics and scripts are not
    /// affected.
    struct ParallaxComponent
    {
        glm::vec2 Factor{ 0.5f, 0.5f };
        bool RepeatX = false;
        bool RepeatY = false;
        glm::vec2 Spacing{ 0.0f };
    };

    // ------------------------------------------------------------------ splines
    /// A path through points in the entity's own space (Math/Spline.h): Catmull-Rom (smooth through
    /// every point), Bezier (shaped by two handles per point) or straight lines; open or closed.
    /// Spline Followers move along it, scripts sample it (Scene::GetSplinePosition / GetSplineTangent /
    /// GetSplineClosestDistance). The editor draws it and edits its points in the viewport
    /// (Inspector: "Nuqtalarni tahrirlash").
    struct SplineComponent
    {
        Zamin::Spline Path = DefaultPath();
        glm::vec4 Color{ 0.35f, 0.85f, 1.0f, 1.0f }; ///< the editor's line

        /// The path's length table (its own units), rebuilt when the path has changed.
        const SplineLengthTable& GetLengthTable() const;
        float GetLength() const { return GetLengthTable().GetLength(); }
        /// Distance along the path -> its parameter (closed paths wrap, open ones clamp).
        float GetParameterAtDistance(float distance) const;
        /// Local-space point / unit direction `distance` along the path.
        glm::vec3 GetPositionAtDistance(float distance) const { return Path.Evaluate(GetParameterAtDistance(distance)); }
        glm::vec3 GetTangentAtDistance(float distance) const { return Path.EvaluateTangent(GetParameterAtDistance(distance)); }

        /// A gentle S of four points along X.
        static Zamin::Spline DefaultPath();

    private:
        mutable SplineLengthTable m_Table;
        mutable Zamin::Spline m_TableSource;
        mutable bool m_TableBuilt = false;
    };

    /// Moves its entity along a Spline component's path at Speed (world units / s): once to the end,
    /// round and round (Loop; an open path starts over), or back and forth (PingPong). Rotation turns
    /// it to face along the path (Path3D: its -Z forward, Y up; Path2D: its +X along the path, about
    /// Z). Offset shifts it from the path in the path's own frame (x right, y up, z along it). A
    /// kinematic body is moved by velocity (it carries what stands on it), anything else by its
    /// transform. Scripts: Play() / Pause() / Restart(), Speed, Distance.
    struct SplineFollowerComponent
    {
        enum class FollowMode { Once = 0, Loop, PingPong };
        enum class RotationMode { None = 0, Path3D, Path2D };

        EntityRef Path;                  ///< the entity with the Spline component (not set: the nearest ancestor with one)
        float Speed = 2.0f;              ///< world units / s (negative: backwards)
        FollowMode Mode = FollowMode::Loop;
        float StartPosition = 0.0f;      ///< where it starts: 0..1 of the path's length
        RotationMode Rotation = RotationMode::None;
        glm::vec3 Offset{ 0.0f };
        bool PlayOnStart = true;

        /// runtime (cleared when play starts / stops)
        float Distance = 0.0f;           ///< along the path (the path's own units)
        float Direction = 1.0f;          ///< 1, or -1 on a ping-pong's way back
        bool Playing = false;            ///< the first update sets it from PlayOnStart (unless Play / Pause came first)
        bool Finished = false;           ///< Once: reached the end
        bool Initialized = false;        ///< placed at StartPosition
        bool PlayStateSet = false;       ///< Play / Pause / Restart called

        void Play() { Playing = PlayStateSet = true; Finished = false; }
        void Pause() { Playing = false; PlayStateSet = true; }
        /// Back to StartPosition, playing.
        void Restart() { Initialized = false; Play(); }
        void ResetRuntime()
        {
            Distance = 0.0f;
            Direction = 1.0f;
            Playing = Finished = Initialized = PlayStateSet = false;
        }
    };

    // ------------------------------------------------------------------ navigation (AI/NavGrid.h)
    /// What every grid's agents share: how their paths may step and how the grid is made.
    struct NavGridPathSettings
    {
        bool Diagonals = true;   ///< 8 neighbours (else 4)
        bool CutCorners = false; ///< diagonal steps past one blocked side cell (graze corners)
        /// Baked when the game starts (else: Scene::BakeNavGrid / a script); a grid switched off then, or
        /// spawned later, when it is switched on / spawned.
        bool BakeOnStart = true;
        /// The editor saves the bake into the .znav next to the scene file (AI/NavBakeFile.h) and the
        /// game starts with it while what it was baked from is unchanged (else it bakes): for big
        /// grids that take long to bake. Off: baked every time (always up to date).
        bool CacheBake = false;
    };

    /// A path-finding grid over a 2D level (the world's XY plane), baked from it: the cells under
    /// colliding tiles (the tileset's tile collision: Solid; OneWay too with OneWayBlocks) and under
    /// static 2D colliders (Rigidbody 2D Static; triggers left out) are blocked, cells nearer than
    /// AgentRadius to those too. The area is the sources' bounds (AutoBounds) or Size around the
    /// entity's position + Offset; the grid is axis aligned (the entity's rotation / scale do not
    /// turn it). Not saved in the scene: baked when the game starts (BakeOnStart), again while it runs
    /// when the sources change (AutoRebuild: tiles painted / dug, static colliders moved) or when a
    /// script asks (Scene::BakeNavGrid). Nav Agents walk on it.
    struct NavGrid2DComponent
    {
        static constexpr float MinCellSize = 0.05f, MaxCellSize = 100.0f;

        bool AutoBounds = true;
        glm::vec2 Offset{ 0.0f };
        glm::vec2 Size{ 20.0f, 20.0f };
        float CellSize = 0.5f;
        bool UseTilemaps = true;
        bool OneWayBlocks = false;
        EntityRef Tilemap;                ///< not set: every tilemap of the scene
        bool UseColliders = true;
        uint16_t LayerMask = 0xFFFF;      ///< the collision layers of tilemaps / colliders that block
        float AgentRadius = 0.25f;
        NavGridPathSettings Paths;
        bool AutoRebuild = false;

        /// runtime (not saved; cleared when play starts / stops)
        NavGrid Grid;                     ///< empty until baked
        bool Baked = false;
        uint64_t SourceHash = 0;          ///< of what it was baked from
        float RebuildTimer = 0.0f;
        uint32_t BakeCount = 0;
        float BakeMilliseconds = 0.0f;
        bool FromBakeFile = false;        ///< the last bake came from the scene's .znav file
        bool StartBakePending = false;    ///< BakeOnStart: not baked yet (switched off since play started)

        void ResetRuntime()
        {
            Grid.Clear();
            Baked = false;
            SourceHash = 0;
            RebuildTimer = 0.0f;
            BakeCount = 0;
            BakeMilliseconds = 0.0f;
            FromBakeFile = false;
            StartBakePending = false;
        }
    };

    /// A path-finding grid over a 3D level (cells on the world's XZ plane, one floor height each),
    /// baked by casting a ray down through each cell's centre onto the static 3D colliders (Rigidbody 3D
    /// Static on a layer of LayerMask) of the box Size around the entity's position + Offset: the first
    /// surface hit is the cell's floor - walkable when it is no steeper than MaxSlope and nothing is in
    /// the way above it up to AgentHeight (an overlap box from StepHeight up: walls, crates, low
    /// ceilings). Neighbouring floors more than StepHeight apart do not connect; cells nearer than
    /// AgentRadius to a blocked cell or such a ledge are blocked. One level (the topmost floor), not a
    /// navmesh. Baked when the game starts (BakeOnStart), by the inspector's button or Scene::BakeNavGrid.
    struct NavGrid3DComponent
    {
        static constexpr float MinCellSize = 0.05f, MaxCellSize = 100.0f;

        glm::vec3 Offset{ 0.0f };
        glm::vec3 Size{ 20.0f, 10.0f, 20.0f };
        float CellSize = 0.5f;
        float AgentRadius = 0.4f;
        float AgentHeight = 1.8f;
        float MaxSlope = 45.0f;           ///< degrees
        float StepHeight = 0.4f;
        uint16_t LayerMask = 0xFFFF;
        NavGridPathSettings Paths;

        /// runtime (not saved; cleared when play starts / stops)
        NavGrid Grid;
        bool Baked = false;
        uint32_t BakeCount = 0;
        float BakeMilliseconds = 0.0f;
        int BakeBodies = 0;               ///< static bodies the last bake saw
        bool FromBakeFile = false;        ///< the last bake came from the scene's .znav file
        bool StartBakePending = false;    ///< BakeOnStart: not baked yet (switched off since play started)

        void ResetRuntime()
        {
            Grid.Clear();
            Baked = false;
            BakeCount = 0;
            BakeMilliseconds = 0.0f;
            BakeBodies = 0;
            FromBakeFile = false;
            StartBakePending = false;
        }
    };

    /// Walks its entity along paths found on a NavGrid 2D / 3D: SetDestination (or Target: an entity
    /// it keeps going after) finds the path, the agent then follows it - towards the point LookAhead
    /// ahead along it, speeding up / braking with Acceleration, turning at AngularSpeed (3D: its -Z
    /// forward; 2D: its +X) - and stops StoppingDistance from the end. It moves a Character Controller
    /// 3D (by its move velocity), a Rigidbody 2D / 3D (dynamic: by velocity; kinematic: by its
    /// transform), or else the transform itself (3D: on the grid's floor + BaseOffset). The path is
    /// found again every RepathInterval seconds while moving and whenever its grid changes.
    /// Scripts: SetDestination(p), Stop(), IsMoving(), RemainingDistance() (ScriptableEntity::
    /// SetDestination finds the path at once). Stop() and SetDestination() pause going after Target
    /// (it is kept): ResumeTarget() - or another Target - goes after it again.
    struct NavAgentComponent
    {
        bool WorldNavigation = false;    ///< opt in: streamed sparse atlas, explicit traversal handshake
        NavRecovery::Settings RecoverySettings;
        NavRecovery Recovery; // runtime only
        bool CrowdAvoidance = false;     ///< world mode: predictive velocity sampling, not a physics guarantee
        float CrowdNeighborDistance = 4, CrowdTimeHorizon = 1.5f, CrowdPriority = 1, CrowdHeight = 1.8f;
        /// Runtime diagnostics; Radius is the personal-space radius (the grid already includes static clearance).
        glm::vec3 PreferredVelocity{ 0 };
        uint32_t CrowdNeighbors = 0;
        bool CrowdLimited = false, CrowdBlocked = false;
        uint32_t WorldMaxExpanded = 16384; ///< per-query expansion limit (1..1048576)
        EntityRef Grid;                   ///< the NavGrid entity (not set: the grid whose area holds the agent)
        EntityRef Target;                 ///< set: it keeps going after this entity
        float Speed = 3.5f;               ///< units / s
        float Acceleration = 12.0f;       ///< units / s^2 (0: changes speed at once)
        float AngularSpeed = 540.0f;      ///< degrees / s (0: does not turn)
        float StoppingDistance = 0.1f;
        float Radius = 0.3f;              ///< how near a corner counts as reached
        float LookAhead = 0.5f;           ///< units ahead along the path it steers towards
        float RepathInterval = 0.5f;      ///< s (0: only on SetDestination / grid changes)
        float BaseOffset = 0.0f;          ///< 3D, moved by transform: its origin above the floor
        bool AutoBraking = true;          ///< slows down to stop at the end (else full speed to it)
        bool SmoothPath = true;           ///< string pulling (else through cell centres)
        bool SplinePath = false;          ///< rounded corners (Catmull-Rom over the string-pulled path)
        bool UpdateRotation = true;
        /// Local avoidance: three physics rays ("feelers": straight ahead along where it goes and
        /// FeelerAngle to each side) push it round what they hit - other agents, crates, moving bodies
        /// the grid does not know - blended with the path at AvoidanceWeight. Static bodies only with
        /// AvoidStatic (the grid already keeps the path clear of them). Its own body, its children and
        /// its Target are never avoided.
        bool Avoidance = false;
        float FeelerLength = 1.5f;        ///< units ahead (the side ones 3/4 of it)
        float FeelerAngle = 35.0f;        ///< degrees
        float FeelerHeight = 0.5f;        ///< 3D: above the agent's origin
        float AvoidanceWeight = 1.0f;     ///< 0..: how hard it turns away (1: up to its full speed)
        uint16_t AvoidanceMask = 0xFFFF;  ///< the collision layers the feelers see
        bool AvoidStatic = false;

        /// runtime (read by scripts; cleared when play starts / stops)
        glm::vec3 Destination{ 0.0f };
        bool HasDestination = false;
        bool Arrived = false;             ///< reached the end of its path (Partial: as near as it gets)
        bool RepathRequested = false;
        std::vector<glm::vec3> Path;      ///< world corners, from where it was when the path was found
        int PathIndex = 0;                ///< the corner it is heading to
        NavPathStatus PathStatus = NavPathStatus::None;
        glm::vec3 Velocity{ 0.0f };       ///< what it moves with now (planar)
        float Remaining = 0.0f;           ///< along the path to its end
        float RepathTimer = 0.0f;
        uint32_t GridRevision = 0;
        uint32_t CarveRevision = 0;       ///< the grid's carving (Nav Obstacles) its path was checked against
        std::vector<uint64_t> PathLinks;  ///< incoming traversal ID for each world path point
        uint64_t AtlasRevision = 0, PendingTraversalLink = 0, TraversalTicket = 0;
        glm::vec3 TraversalEntry{ 0 }, TraversalExit{ 0 };
        uint32_t TraversalCompletions = 0;
        uint32_t PathRequests = 0;        ///< paths found since play started
        uint32_t BlockedRepaths = 0;      ///< of them: because an obstacle blocked the path
        /// Avoidance (debug): the feelers of the last update (where each starts / ends, which hit) and
        /// the push they gave.
        glm::vec3 FeelerOrigin{ 0.0f };
        std::array<glm::vec3, 3> FeelerEnds{};
        uint8_t FeelerHits = 0;           ///< bit i: feeler i hit (0 ahead, 1 left, 2 right)
        bool FeelersActive = false;
        glm::vec3 AvoidancePush{ 0.0f };
        float LastDeltaTime = 0.0f;
        int AvoidSide = 0;                ///< the way it goes round what is ahead (+1 left, -1 right, 0 free)
        float AvoidClearTime = 0.0f;      ///< s since the feelers last hit something
        uint64_t PausedTarget = 0;        ///< the Target that Stop / SetDestination paused (not followed while set)

        /// Goes there (the path is found at the next navigation update, this frame when called from
        /// OnUpdate) instead of after its Target (paused: ResumeTarget).
        void SetDestination(const glm::vec3& destination)
        {
            Recovery.Reset();
            Destination = destination;
            HasDestination = true;
            Arrived = false;
            RepathRequested = true;
            PausedTarget = Target.ID;
            PendingTraversalLink = TraversalTicket = 0;
        }
        /// No destination any more: it brakes to a stop (at once with `immediate`). Target is kept but
        /// not followed until ResumeTarget (or another Target is set).
        void Stop(bool immediate = false)
        {
            Recovery.Reset();
            HasDestination = Arrived = RepathRequested = false;
            PreferredVelocity = glm::vec3(0);
            CrowdNeighbors = 0;
            CrowdLimited = CrowdBlocked = false;
            Path.clear();
            PathIndex = 0;
            PathLinks.clear();
            PendingTraversalLink = TraversalTicket = 0;
            Remaining = 0.0f;
            PausedTarget = Target.ID;
            if (immediate)
                Velocity = glm::vec3(0.0f);
        }
        /// Goes after its Target again (after Stop / SetDestination paused it).
        void ResumeTarget()
        {
            PausedTarget = 0;
            if (Target)
            {
                HasDestination = RepathRequested = true;
                Arrived = false;
            }
        }
        /// It has a Target it goes after now (not paused).
        bool IsFollowingTarget() const { return Target && Target.ID != PausedTarget; }
        /// Moving now, walking a path, or about to (a destination whose path is not found yet).
        bool IsMoving() const
        {
            return !(WorldNavigation && RecoverySettings.Enabled && Recovery.IsExhausted()) && !PendingTraversalLink && (glm::dot(Velocity, Velocity) > 1e-4f || (HasDestination && !Arrived && (RepathRequested || Path.size() >= 2)));
        }
        float RemainingDistance() const { return Remaining; }
        bool HasPath() const { return Path.size() >= 2; }
        bool IsPathPartial() const { return PathStatus == NavPathStatus::Partial; }
        void ResetRuntime()
        {
            Recovery.Reset();
            PathLinks.clear();
            AtlasRevision = PendingTraversalLink = TraversalTicket = 0;
            TraversalEntry = TraversalExit = glm::vec3(0);
            TraversalCompletions = 0;
            Destination = glm::vec3(0.0f);
            HasDestination = Arrived = RepathRequested = false;
            Path.clear();
            PathIndex = 0;
            PathStatus = NavPathStatus::None;
            Velocity = glm::vec3(0.0f);
            Remaining = RepathTimer = 0.0f;
            GridRevision = CarveRevision = PathRequests = BlockedRepaths = 0;
            PreferredVelocity = glm::vec3(0);
            CrowdNeighbors = 0;
            CrowdLimited = CrowdBlocked = false;
            FeelerOrigin = AvoidancePush = glm::vec3(0.0f);
            FeelerEnds = {};
            FeelerHits = 0;
            FeelersActive = false;
            LastDeltaTime = AvoidClearTime = 0.0f;
            AvoidSide = 0;
            PausedTarget = 0;
        }
    };

    /// A moving or temporary obstacle for Nav Agents (a door, a crate pushed around, a parked cart):
    /// while the game runs it carves the cells under its shape out of the baked NavGrid 2D / 3D it
    /// overlaps (Grid: only that one's), widened by the grid's AgentRadius - a box (Size: its whole
    /// size in the entity's frame) or a circle (Radius; 3D: an upright cylinder Size.y tall) around
    /// Center. 2D grids see it on the XY plane, 3D ones on XZ (only floors it stands on: a 3D grid's
    /// cells under it between their step and agent heights). It carves again - only the cells that
    /// changed - when it moved or turned more than MoveThreshold, and lets go of them when it is
    /// removed, made inactive or destroyed. Agents whose path it blocks find a new one at once.
    /// CarveOnlyStationary: only after it has stood still for StationaryTime (while it moves the
    /// agents' Avoidance has to deal with it).
    struct NavObstacleComponent
    {
        enum class ShapeType : uint8_t { Box = 0, Circle };

        ShapeType Shape = ShapeType::Box;
        glm::vec3 Center{ 0.0f };
        glm::vec3 Size{ 1.0f, 1.0f, 1.0f };
        float Radius = 0.5f;
        EntityRef Grid;                   ///< not set: every baked grid it overlaps
        float MoveThreshold = 0.1f;       ///< units
        bool CarveOnlyStationary = false;
        float StationaryTime = 0.5f;      ///< s

        /// runtime (cleared when play starts / stops)
        struct Carving
        {
            uint64_t Grid = 0;            ///< the grid entity's UUID
            int Dimension = 2;            ///< which of its grids (2D / 3D)
            uint32_t Serial = 0;          ///< NavGrid::GetSerial when carved (rebaked since: gone)
            std::vector<uint32_t> Cells;  ///< sorted NavGrid indices
        };
        std::vector<Carving> Carvings;
        std::vector<glm::vec3> CarvedPoints; ///< the shape's corners when it last carved
        std::vector<glm::vec3> LastPoints;   ///< and last frame (standing still)
        float StillTime = 0.0f;
        uint64_t GridSignature = 0;       ///< the baked grids it carved into (one rebaked: carve again)
        uint32_t CarveCount = 0;          ///< times it carved (again)

        size_t GetCarvedCellCount() const
        {
            size_t n = 0;
            for (const Carving& c : Carvings)
                n += c.Cells.size();
            return n;
        }
        bool IsCarving() const { return !Carvings.empty(); }
        void ResetRuntime()
        {
            Carvings.clear();
            CarvedPoints.clear();
            LastPoints.clear();
            StillTime = 0.0f;
            GridSignature = 0;
            CarveCount = 0;
        }
    };

    /// Eyes for AI (line of sight and field of view): the entity sees a target that is within
    /// ViewDistance inside the FieldOfView cone around its forward (3D: its -Z, from EyeHeight above
    /// its origin; 2D: its +X turned by its rotation) - or nearer than NearDistance in any direction -
    /// when a physics ray from the eye reaches one of the target's points (the centre and edges of its
    /// collider / character / mesh) with no collider of BlockingMask's layers in between. Targets: the
    /// Target entity, else every active entity tagged TargetTag. Checked every CheckInterval seconds
    /// while the game runs; scripts read CanSeeTarget() / GetSeenTarget() / GetLastSeenPosition() or get
    /// OnTargetSpotted / OnTargetLost, and Scene::CanSee checks any entity at once.
    struct SightSensorComponent
    {
        float ViewDistance = 10.0f;
        float FieldOfView = 110.0f;       ///< degrees, the whole cone
        float NearDistance = 1.0f;        ///< seen all round this near (0: only the cone)
        float EyeHeight = 1.6f;           ///< 3D: above the origin
        EntityRef Target;                 ///< not set: the entities tagged TargetTag
        std::string TargetTag = "Player";
        uint16_t BlockingMask = 0xFFFF;   ///< the collision layers whose colliders block the view
        float CheckInterval = 0.1f;       ///< s (0: every frame)
        float MemoryTime = 3.0f;          ///< s: how long it remembers where it last saw the target

        /// runtime (read by scripts; cleared when play starts / stops)
        bool CanSee = false;
        UUID SeenTarget = 0;              ///< the nearest target seen (0: none)
        glm::vec3 LastSeenPosition{ 0.0f };
        bool HasSeen = false;             ///< saw something since play started
        float TimeSinceSeen = 0.0f;       ///< s since it last saw one (0 while it sees)
        float CheckTimer = 0.0f;
        uint32_t Checks = 0;
        int VisibleCount = 0;             ///< targets seen at the last check

        /// Lost sight of the target less than MemoryTime ago (or sees it).
        bool RemembersTarget() const { return CanSee || (HasSeen && TimeSinceSeen <= MemoryTime); }
        void ResetRuntime()
        {
            CanSee = HasSeen = false;
            SeenTarget = 0;
            LastSeenPosition = glm::vec3(0.0f);
            TimeSinceSeen = CheckTimer = 0.0f;
            Checks = 0;
            VisibleCount = 0;
        }
    };

    /// A round for AI to walk (a guard, an animal, a cart): waypoints - its own Points (world
    /// positions, or offsets from where it starts with RelativeToStart), the children of Route, or
    /// the points of Route's Spline component (SplineSpacing > 0: points that far apart along its
    /// curve). It goes to them one after another - Loop (from the last back to the first), PingPong
    /// (back and forth), Random (any other point) or Once (stops at the last) - and waits WaitTime
    /// (+/- WaitVariance) seconds at each (a point's own Wait when it is >= 0). With a Nav Agent it
    /// walks there on the nav grid (the agent's SetDestination: its speed, avoidance ...); without
    /// one it moves straight at Speed by its transform. Scripts: PausePatrol() / ResumePatrol() (a
    /// chase, then back to the round: the point it was heading to, or the nearest), OnPatrolPointReached.
    struct PatrolPathComponent
    {
        enum class PointSource : uint8_t { Points = 0, Children, Spline };
        enum class PatrolMode : uint8_t { Loop = 0, PingPong, Random, Once };
        struct Waypoint
        {
            glm::vec3 Position{ 0.0f };
            float Wait = -1.0f;           ///< s (< 0: the component's WaitTime)
            bool operator==(const Waypoint&) const = default;
        };
        static constexpr size_t MaxWaypoints = 1024;

        PointSource Source = PointSource::Points;
        std::vector<Waypoint> Points;
        bool RelativeToStart = false;     ///< Points: offsets from where it is when the game starts
        EntityRef Route;                  ///< Children / Spline: the entity (not set: the nearest ancestor with a Spline)
        float SplineSpacing = 0.0f;       ///< Spline: 0 - its own points; > 0 - points this far apart (world units) along its curve
        PatrolMode Mode = PatrolMode::Loop;
        float WaitTime = 1.0f;            ///< s at each point
        float WaitVariance = 0.0f;        ///< s: the wait is WaitTime +/- up to this much
        float ArriveDistance = 0.25f;     ///< how near a point counts as reached (planar)
        float Speed = 2.0f;               ///< without a Nav Agent: units / s
        bool FaceMovement = true;         ///< without a Nav Agent: turns the way it goes (3D: -Z forward; 2D: +X)
        bool StartAtNearest = true;       ///< begins at the nearest point (else the first)
        bool PlayOnStart = true;          ///< patrols from the start (else: ResumePatrol)

        /// runtime (read by scripts; cleared when play starts / stops)
        int Current = -1;                 ///< the point it is heading to / waiting at (-1: not started)
        int Direction = 1;                ///< PingPong: 1 forwards, -1 back
        bool Started = false;             ///< the first update set it up (start position, first point)
        bool Patrolling = false;          ///< walking the round (PausePatrol: false)
        bool Waiting = false;             ///< at Current, waiting
        bool Finished = false;            ///< Once: at the last point
        bool DestinationIssued = false;   ///< the agent was sent to Current
        bool ResumeNearest = false;       ///< ResumePatrol(true): to the nearest point first
        bool PatrolStateSet = false;      ///< PausePatrol / ResumePatrol called (PlayOnStart then does not decide)
        float WaitTimer = 0.0f;           ///< s left to wait
        float StuckTimer = 0.0f;          ///< s the agent has not got nearer (an unreachable point is skipped)
        float BestDistance = 0.0f;
        glm::vec3 StartPosition{ 0.0f };  ///< where it was when the game started (RelativeToStart)
        glm::vec3 Target{ 0.0f };         ///< the world position of Current
        glm::vec3 IssuedTarget{ 0.0f };   ///< where the Nav Agent was sent
        uint32_t Arrivals = 0;            ///< points reached since play started
        uint32_t Laps = 0;                ///< rounds done (Loop: back at the first point; PingPong: at an end)
        uint32_t Skipped = 0;             ///< points given up (no path)
        uint32_t RandomState = 0x9E3779B9u;

        void PausePatrol()
        {
            Patrolling = false;
            PatrolStateSet = true;
        }
        /// Back to the round: to the point it was heading to (nearest: to the nearest point).
        void ResumePatrol(bool nearest = false)
        {
            Patrolling = PatrolStateSet = true;
            Finished = false;
            Waiting = false;
            DestinationIssued = false;
            ResumeNearest = nearest;
        }
        bool IsPatrolling() const { return Patrolling && !Finished; }
        void ResetRuntime()
        {
            Current = -1;
            Direction = 1;
            Started = Patrolling = Waiting = Finished = DestinationIssued = ResumeNearest = PatrolStateSet = false;
            WaitTimer = StuckTimer = BestDistance = 0.0f;
            StartPosition = Target = IssuedTarget = glm::vec3(0.0f);
            Arrivals = Laps = Skipped = 0;
            RandomState = 0x9E3779B9u;
        }
    };

    /// A group that moves together (AI/Formation.h), on its leader: the Members (and the active
    /// entities tagged MemberTag) keep to slots round it - Line (side by side with it), Column (in a
    /// file behind it), Wedge (a V behind it, WedgeAngle open) or Circle (a ring round it that does
    /// not turn with it; its members face outwards) - Spacing apart, turned the way the leader goes
    /// (standing: the way it went last; at the start: the way it faces). Members with a Nav Agent walk
    /// to their slots on its grid (sent again every UpdateInterval while the slots move: give them a
    /// little more speed than the leader's); without one they move straight at Speed. With KeepOnGrid
    /// a slot in a blocked cell moves to the nearest walkable one, and one behind a wall comes in
    /// towards the leader. Each member takes the slot that makes the least walking in all
    /// (AssignNearest; else the list's order) - again when the shape, the members or the heading
    /// change a lot. A member in its slot (SlotTolerance) turns the way the formation faces.
    /// Scripts: SetFormationShape, AddFormationMember / RemoveFormationMember, IsFormationInPlace.
    struct FormationComponent
    {
        static constexpr size_t MaxMembers = 64;

        FormationShape Shape = FormationShape::Wedge;
        float Spacing = 1.5f;             ///< between neighbouring members (units)
        float WedgeAngle = 90.0f;         ///< Wedge: degrees, how wide the V opens
        std::vector<EntityRef> Members;
        std::string MemberTag;            ///< not empty: also every active entity with this tag
        bool AssignNearest = true;        ///< slots by the least walking (else in the list's order)
        bool FaceHeading = true;          ///< members in their slot turn the way the formation faces
        bool KeepOnGrid = true;           ///< slots kept on the nav grid's walkable cells, in the leader's sight
        float UpdateInterval = 0.25f;     ///< s: how often Nav Agent members are sent to their moved slots
        float SlotTolerance = 0.35f;      ///< how near its slot a member counts as in place (planar)
        float Speed = 3.0f;               ///< members without a Nav Agent: units / s

        /// runtime (read by scripts; cleared when play starts / stops)
        std::vector<UUID> SlotMembers;    ///< the member of each slot
        std::vector<glm::vec3> Slots;     ///< where the slots are (world), as of the last update
        std::vector<glm::vec3> Issued;    ///< where each slot's Nav Agent member was last sent
        glm::vec2 Heading{ 0.0f };        ///< where the formation faces, on the plane (unit; 0: not yet)
        glm::vec2 AssignedHeading{ 0.0f }; ///< the heading when the slots were last handed out
        glm::vec3 LastLeaderPosition{ 0.0f };
        FormationShape AssignedShape = FormationShape::Wedge;
        uint64_t MemberSignature = 0;     ///< of the members the slots were handed out to
        bool Started = false;
        bool LeaderMoving = false;
        float Timer = 0.0f;               ///< s until Nav Agent members are sent again
        int InPlace = 0;                  ///< members in their slot at the last update
        uint32_t Assignments = 0;         ///< times the slots were handed out

        bool IsInPlace() const { return !SlotMembers.empty() && InPlace >= (int)SlotMembers.size(); }
        void ResetRuntime()
        {
            SlotMembers.clear();
            Slots.clear();
            Issued.clear();
            Heading = AssignedHeading = glm::vec2(0.0f);
            LastLeaderPosition = glm::vec3(0.0f);
            AssignedShape = FormationShape::Wedge;
            MemberSignature = 0;
            Started = LeaderMoving = false;
            Timer = 0.0f;
            InPlace = 0;
            Assignments = 0;
        }
    };

    // ------------------------------------------------------------------ particles
    /// A particle emitter: the settings of a system simulated by the scene (ParticleSystem.h:
    /// ParticleSimulation) and drawn as camera-facing quads / circles. The optional modules
    /// (ColorOverLifetime, SizeOverLifetime, VelocityOverLifetime, LimitVelocity, Noise, Collision,
    /// SubEmitters, TextureSheet, Trails) do nothing until Enabled; without them a particle goes from
    /// StartColor / StartSize to EndColor / EndSize over its life.
    struct ParticleSystemComponent
    {
        /// Point; Circle (local XY disc); Sphere; Box (half size ShapeExtents); Cone (around
        /// Direction: from a disc of ShapeRadius, ConeAngle wide at its rim); Edge (a line of
        /// EdgeLength along local X); Mesh (the surface of a mesh: MeshSource).
        enum class EmitterShape { Point = 0, Circle, Sphere, Box, Cone, Edge, Mesh };
        enum class ConeEmitFrom { Base = 0, Volume };
        /// The Mesh shape's mesh: a primitive (MeshPrimitive), a model file (MeshModelPath) or what
        /// the entity's own Mesh Renderer shows.
        enum class MeshShapeSource { Primitive = 0, Model, Renderer };
        using MeshEmitMode = ParticleMeshMode;
        /// Where the particles live: World - they stay where they were born when the emitter
        /// moves; Local - they move (turn, scale) with the emitter; Custom - with another entity
        /// (CustomSpace: a ship whose turret emits; not set or gone: World).
        enum class SimulationSpace { Local = 0, World, Custom };
        /// What happens when a non-looping system has finished (its Duration over, its last
        /// particle dead, the particles of its sub-emitter templates among its children too) while
        /// the game runs: nothing, the entity switched off, or destroyed.
        /// Scripts hear of it either way (OnParticlesFinished, the bus' "ParticlesFinished").
        enum class StopAction { None = 0, Disable, Destroy };
        /// The order the particles are drawn in (the last one on top): Auto - back to front by
        /// the camera's distance when alpha-blended (not Additive), else as they were born;
        /// ByDistance - back to front always; OldestOnTop / YoungestOnTop - by age.
        enum class SortMode { Auto = 0, None, ByDistance, OldestOnTop, YoungestOnTop };

        /// A burst of the emission: Count particles (with that chance) Time seconds into each
        /// cycle, repeated Cycles times (0: until the cycle ends) every Interval seconds.
        struct EmissionBurst
        {
            float Time = 0.0f;
            int Count = 30;
            int Cycles = 1;
            float Interval = 0.5f;
            float Probability = 1.0f;
            bool operator==(const EmissionBurst&) const = default;
        };

        /// Plays when the scene starts (saved; the scene file's "Playing"). Playing below is the
        /// state it is in now: Play / Stop / Pause and the editor's preview controls change only that.
        bool PlayOnStart = true;
        bool Playing = true;
        bool Looping = true;
        bool PlayInEditor = true;
        /// One cycle of the emission (seconds): the bursts' times are in it and it starts over
        /// while Looping; a non-looping system emits for one cycle, then finishes (StopActionType).
        float Duration = 5.0f;
        float StartDelay = 0.0f;     ///< seconds before it starts emitting
        /// Looping: it starts as if it had already run a whole cycle (a smoking chimney is full of
        /// smoke at once). Sub-emitters do not fire for the prewarm.
        bool Prewarm = false;
        float SimulationSpeed = 1.0f; ///< its time runs this many times as fast (0.5: slow motion)
        StopAction StopActionType = StopAction::None;
        float EmissionRate = 30.0f;  ///< particles / second
        int Burst = 0;               ///< particles emitted once when it starts playing (not every cycle)
        std::vector<EmissionBurst> Bursts; ///< every cycle
        /// The Emission module: off - the system emits nothing itself (rate, start burst, bursts);
        /// particles still come from scripts (EmitParticles) and as a sub-emitter's template.
        bool EmissionEnabled = true;
        int MaxParticles = 1000;

        EmitterShape Shape = EmitterShape::Point;
        bool ShapeEnabled = true;          ///< the Shape module: off - from the centre along Direction (as Point)
        float ShapeRadius = 0.5f;          ///< Circle, Sphere, Cone (its base)
        glm::vec3 ShapeExtents{ 0.5f };    ///< Box
        /// Circle / Sphere / Cone: how much of the radius emits, from the rim inwards (1: all of
        /// it, 0: only the rim / the surface).
        float ShapeThickness = 1.0f;
        float ConeAngle = 25.0f;           ///< degrees from the axis at the rim of the base (0: a straight beam)
        float ConeLength = 1.0f;           ///< Volume: how far into the cone particles start
        ConeEmitFrom ConeFrom = ConeEmitFrom::Base;
        bool ConeFlat = false;             ///< 2D: the base is a line across the axis, everything in the XY plane
        float EdgeLength = 1.0f;
        /// Circle / Sphere / Box: particles fly away from the centre (Spread around that) instead
        /// of along Direction.
        bool AlignToShape = false;
        /// Mesh: from a random point of its triangles (by area) / edges / vertices, in the mesh's
        /// own space (the emitter's transform places and scales it, as a Mesh Renderer draws it);
        /// MeshAlongNormal: away from the surface (Spread around its normal) instead of along
        /// Direction; MeshNormalOffset: that far out along the normal.
        MeshShapeSource MeshSource = MeshShapeSource::Primitive;
        PrimitiveMesh MeshPrimitive = PrimitiveMesh::Sphere;
        std::string MeshModelPath;
        MeshEmitMode MeshMode = MeshEmitMode::Triangle;
        bool MeshAlongNormal = true;
        float MeshNormalOffset = 0.0f;

        float LifetimeMin = 0.8f, LifetimeMax = 1.6f;
        float SpeedMin = 1.0f, SpeedMax = 3.0f;
        glm::vec3 Direction{ 0.0f, 1.0f, 0.0f }; ///< also the cone's axis
        float Spread = 25.0f;        ///< degrees (not for Cone: its angle spreads)
        glm::vec3 Gravity{ 0.0f, 0.0f, 0.0f };
        float Drag = 0.0f;
        float AngularVelocityMax = 0.0f; ///< radians / second

        glm::vec4 StartColor{ 1.0f, 0.85f, 0.35f, 1.0f };
        glm::vec4 EndColor{ 1.0f, 0.25f, 0.05f, 0.0f };
        float StartSize = 0.35f, EndSize = 0.05f;

        /// @name Modules
        /// @{
        /// The colour over a particle's life: a gradient of any number of stops (instead of Start /
        /// End colour); each particle somewhere between two gradients when RandomBetweenTwo.
        struct ColorOverLifetimeModule
        {
            bool Enabled = false;
            ParticleGradient Gradient = ParticleGradient::TwoStops({ 1.0f, 0.85f, 0.35f, 1.0f }, { 1.0f, 0.25f, 0.05f, 0.0f });
            bool RandomBetweenTwo = false;
            ParticleGradient GradientB = ParticleGradient::TwoStops({ 1.0f, 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, 0.0f });
            bool operator==(const ColorOverLifetimeModule&) const = default;
        } ColorOverLifetime;
        /// @}

        /// The size over a particle's life: StartSize times the curve (instead of Start / End size).
        struct SizeOverLifetimeModule
        {
            bool Enabled = false;
            ParticleCurve Curve = ParticleCurve::Linear(1.0f, 0.0f);
            bool RandomBetweenTwo = false;
            ParticleCurve CurveB = ParticleCurve::Linear(1.0f, 0.5f);
            bool operator==(const SizeOverLifetimeModule&) const = default;
        } SizeOverLifetime;

        /// Motion added over a particle's life (it does not change the particle's own velocity):
        /// Linear (units / s) times LinearCurve, in the emitter's axes (World: the world's);
        /// Orbital (radians / s around the emitter's X, Y, Z axes through its centre); Radial
        /// (units / s away from the centre, negative: towards it). The particle's own velocity is
        /// multiplied by SpeedModifier.
        struct VelocityOverLifetimeModule
        {
            bool Enabled = false;
            glm::vec3 Linear{ 0.0f };
            ParticleCurve LinearCurve = ParticleCurve::Constant(1.0f);
            bool World = false;
            glm::vec3 Orbital{ 0.0f };
            float Radial = 0.0f;
            ParticleCurve SpeedModifier = ParticleCurve::Constant(1.0f);
            bool operator==(const VelocityOverLifetimeModule&) const = default;
        } VelocityOverLifetime;

        /// A speed limit over a particle's life (MaxSpeed times MaxSpeedCurve): the speed above it
        /// goes by Dampen every 1/60 s (1: at once).
        struct LimitVelocityModule
        {
            bool Enabled = false;
            float MaxSpeed = 2.0f;
            ParticleCurve MaxSpeedCurve = ParticleCurve::Constant(1.0f);
            float Dampen = 0.5f;
            bool operator==(const LimitVelocityModule&) const = default;
        } LimitVelocity;

        /// Turbulence: a smooth noise field (ParticleSimulation::NoiseField) moves the particles -
        /// up to Strength units / s per axis (0: not along it; 2D: Z 0) times StrengthCurve over
        /// their life. Frequency: how many swirls per unit; ScrollSpeed: how fast the field drifts
        /// and changes; Octaves: finer layers on top (each OctaveMultiplier as strong); Seed: another
        /// field of the same kind.
        struct NoiseModule
        {
            bool Enabled = false;
            glm::vec3 Strength{ 1.0f };
            ParticleCurve StrengthCurve = ParticleCurve::Constant(1.0f);
            float Frequency = 0.5f;
            float ScrollSpeed = 0.25f;
            int Octaves = 1;
            float OctaveMultiplier = 0.5f;
            uint32_t Seed = 0;
            bool operator==(const NoiseModule&) const = default;
        } Noise;

        /// Collisions: with a plane (world space: PlaneNormal, PlaneOffset along it - a ground at
        /// height Y for the up normal; PlaneEntity set: through that entity, its local Y the normal)
        /// or with the physics world's colliders (World3D / World2D: raycasts, only while physics
        /// runs - play / simulate; CollidesWith: their layers). A particle is a sphere of its
        /// size / 2 times RadiusScale. On a hit it bounces (Bounce: the part of its speed into the
        /// surface it keeps), loses Dampen of its speed along the surface (while resting: per 1/60 s),
        /// LifetimeLoss of its whole lifetime (1: dies) and dies when slower than MinKillSpeed.
        struct CollisionModule
        {
            enum class CollisionType { Plane = 0, World3D, World2D };
            bool Enabled = false;
            CollisionType Type = CollisionType::Plane;
            glm::vec3 PlaneNormal{ 0.0f, 1.0f, 0.0f };
            float PlaneOffset = 0.0f;
            EntityRef PlaneEntity;
            float Bounce = 0.5f;
            float Dampen = 0.1f;
            float LifetimeLoss = 0.0f;
            float MinKillSpeed = 0.0f;
            float RadiusScale = 1.0f;
            uint16_t CollidesWith = 0xFFFF;
            bool operator==(const CollisionModule&) const = default;
        } Collision;

        /// Sub-emitters: when a particle is born, dies or hits something (Collision module) - or all
        /// the time while it lives (Lifetime: Count per second) - the Emitter entity's Particle
        /// System makes Count new particles there (with that chance), from its own shape, speeds,
        /// colours..., plus InheritVelocity of the particle's velocity; InheritColor / InheritSize:
        /// multiplied by the particle's colour (RGB) / size. The template system usually emits
        /// nothing itself (Emission 0, no Burst, or not Playing). Firework: rockets that die into a
        /// burst; rain drops that splash on the ground.
        struct SubEmitter
        {
            enum class TriggerType { Birth = 0, Death, Collision, Lifetime };
            TriggerType Trigger = TriggerType::Death;
            EntityRef Emitter;
            int Count = 10;
            float Probability = 1.0f;
            float InheritVelocity = 0.0f;
            bool InheritColor = false;
            bool InheritSize = false;
            bool operator==(const SubEmitter&) const = default;
        };
        struct SubEmittersModule
        {
            bool Enabled = false;
            std::vector<SubEmitter> Emitters;
            bool operator==(const SubEmittersModule&) const = default;
        } SubEmitters;

        /// Flipbook: the texture is a sheet of frames - a grid of Columns x Rows cells (left to
        /// right, top to bottom; at most FrameCount, 0: all) or the named frames of its sprite
        /// sheet (.zmeta; SpritePrefix: only the numbered "<prefix>_<n>" ones in order). A particle
        /// goes through them Cycles times over its life, or at FrameRate frames / s; starting at
        /// StartFrame (or a random one). RandomRow (grid): each particle plays one random row.
        struct TextureSheetModule
        {
            enum class FrameSource { Grid = 0, Sprites };
            enum class TimeMode { Lifetime = 0, FrameRate };
            bool Enabled = false;
            FrameSource Source = FrameSource::Grid;
            int Columns = 4, Rows = 4;
            int FrameCount = 0;
            std::string SpritePrefix;
            TimeMode Mode = TimeMode::Lifetime;
            float Cycles = 1.0f;
            float FrameRate = 12.0f;
            int StartFrame = 0;
            bool RandomStartFrame = false;
            bool RandomRow = false;
            bool operator==(const TextureSheetModule&) const = default;
        } TextureSheet;

        /// Trails: a ribbon behind a particle (Ratio of them) through the places it was over the
        /// last Lifetime seconds - a new point after every MinVertexDistance it moves, at most
        /// MaxPoints. Width (times the particle's size with WidthFromSize) times WidthCurve, and
        /// Gradient (times the particle's colour with InheritColor), from the particle (0) to the
        /// trail's end (1). It goes when the particle dies.
        struct TrailsModule
        {
            bool Enabled = false;
            float Ratio = 1.0f;
            float Lifetime = 0.4f;
            float MinVertexDistance = 0.1f;
            int MaxPoints = 32;
            float Width = 1.0f;
            bool WidthFromSize = true;
            ParticleCurve WidthCurve = ParticleCurve::Linear(1.0f, 0.0f);
            ParticleGradient Gradient = ParticleGradient::TwoStops({ 1.0f, 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, 0.0f });
            bool InheritColor = true;
            bool operator==(const TrailsModule&) const = default;
        } Trails;

        /// Weather around the camera (rain, snow, dust in the air): the emitter is where the camera
        /// is - the primary camera while the game runs, the scene view's in the editor - plus Offset
        /// (world axes); its own rotation and scale stay. Wrap (World space): a particle farther
        /// from the emitter than WrapExtents along a world axis (0: not along it) goes over to the
        /// other side of that box, so the camera never walks out of the rain. NearFade (perspective
        /// cameras): particles nearer the camera than this fade out (gone at a quarter of it), so a
        /// drop passing by the lens is no streak across the screen; 0: off.
        struct FollowCameraModule
        {
            bool Enabled = false;
            glm::vec3 Offset{ 0.0f, 6.0f, 0.0f };
            bool Wrap = true;
            glm::vec3 WrapExtents{ 10.0f, 0.0f, 10.0f };
            float NearFade = 4.0f;
            bool operator==(const FollowCameraModule&) const = default;
        } FollowCamera;

        /// @name The Renderer module
        /// @{
        bool RendererEnabled = true;       ///< off: simulated, not drawn (a rocket that only carries its sub-emitters)
        bool RenderAsCircle = true;
        bool Additive = true;
        /// Stretched along the velocity (rain streaks, sparks): StartSize wide, size x StretchLength
        /// + speed x StretchSpeed long - as the camera sees the motion (looking along it: shorter).
        bool Stretched = false;
        float StretchLength = 1.0f;
        float StretchSpeed = 0.05f;
        SimulationSpace Space = SimulationSpace::World;
        EntityRef CustomSpace;       ///< Space Custom: the entity the particles move with
        bool Billboard = true;       ///< face the camera (3D)
        SortMode Sorting = SortMode::Auto;
        std::string TexturePath;
        Ref<Texture2D> Texture;
        uint32_t SortingLayer = 0;
        int SortingOrder = 0;
        /// @}

        /// runtime
        std::vector<Particle> Particles;
        float EmitAccumulator = 0.0f;
        bool BurstDone = false;
        uint32_t RandomState = 0x9E3779B9u;
        float Time = 0.0f;                    ///< seconds simulated since it started (noise, trails)
        float PlayTime = 0.0f;                ///< seconds it has played (emitted) since it started, StartDelay included
        bool Prewarmed = false;
        bool Finished = false;                ///< a non-looping system that played its cycle and emptied (once)
        /// Finished, not yet reported: the scene waits for the particles of its sub-emitter templates
        /// among its children (a rocket's last burst) before the callbacks and the stop action.
        bool FinishPending = false;
        bool Paused = false;                  ///< frozen as it is (the editor's pause; not saved)
        bool FollowActive = false;            ///< FollowCamera: placed at a camera in the last update,
        glm::vec3 FollowPosition{ 0.0f };     ///< at this point (world)
        std::vector<ParticleEvent> Events;    ///< for the sub-emitters (the scene takes them every frame)
        ParticleTrailPool TrailPool;
        Ref<const ParticleEmitterMesh> MeshRuntime; ///< the Mesh shape's mesh (the scene finds it)
        std::string MeshRuntimeKey;                 ///< what it was found for

        /// Particles in the emitter's own space (Local): they move with it.
        bool IsLocalSpace() const { return Space == SimulationSpace::Local; }

        /// @name Playback
        /// Playback (the inspector's and the scene view's controls, scripts)
        /// Emitting again - from where it was paused, or from the start when it had stopped / finished.
        /// @{
        void Play()
        {
            if (Finished || (!Playing && !Paused))
                Clear();
            Playing = true;
            Paused = false;
        }
        /// Frozen as it is: the particles neither move nor age, nothing is emitted.
        void Pause() { Paused = true; }
        /// No more emission; `clear`: the living particles go at once (else they live out their life).
        void Stop(bool clear = true)
        {
            Playing = false;
            Paused = false;
            if (clear)
                Clear();
        }
        /// From the start (Burst, bursts, prewarm, delay again).
        void Restart()
        {
            Clear();
            Playing = true;
        }
        bool IsEmitting() const { return Playing && !Paused && !Finished; }
        /// As a scene starts it (a copy for the game, a new instance): empty, playing when PlayOnStart.
        void ResetRuntime()
        {
            Clear();
            Playing = PlayOnStart;
        }
        /// @}

        void Clear()
        {
            Particles.clear();
            EmitAccumulator = 0.0f;
            BurstDone = false;
            Time = 0.0f;
            PlayTime = 0.0f;
            Prewarmed = false;
            Finished = false;
            FinishPending = false;
            Paused = false;
            FollowActive = false;
            Events.clear();
            TrailPool.Reset(0);
            MeshRuntime = nullptr;
            MeshRuntimeKey.clear();
        }
    };

    // ------------------------------------------------------------------ screen-space UI
    /// Entities with a UITransform are drawn in screen space on top of the scene, instead of in the
    /// world (Sprite / Circle / Text renderers and the UI widgets). Its rectangle is laid out in its
    /// parent's - the UI Transform of the parent entity, else the whole screen (RectTransform model):
    /// - AnchorMin / AnchorMax: 0..1 of the parent's rectangle (0,0 bottom-left, 1,1 top-right).
    ///   Equal ones pin it to that point of the parent; different ones stretch it with the parent
    ///   along that axis (a bar across the top: AnchorMin 0,1 AnchorMax 1,1);
    /// - Pivot: 0..1 of its own rectangle - the point Position places (and that it grows around);
    /// - Position: px from the anchor point (stretched: the point between the anchors at the pivot)
    ///   to the pivot, +x right, +y up;
    /// - Size: px. Along a stretched axis it is added to the anchors' span (0: exactly between
    ///   them, -40: 20 px in from each edge with the pivot in the middle) - Left / Right / Top /
    ///   Bottom offsets: GetOffsetMin / GetOffsetMax, SetOffsets.
    /// UI units are screen pixels unless a UI Canvas scales them (UICanvasComponent). A layout group
    /// on the parent (UILayoutGroupComponent) places it instead (its anchors and position then do not
    /// count). Scene::GetUIRect gives the rectangle the frame uses.
    struct UITransformComponent
    {
        /// The 9 point presets (both anchors at that point of the parent).
        enum class AnchorPoint { TopLeft = 0, Top, TopRight, Left, Center, Right, BottomLeft, Bottom, BottomRight };
        glm::vec2 AnchorMin{ 0.0f, 1.0f };
        glm::vec2 AnchorMax{ 0.0f, 1.0f };
        glm::vec2 Pivot{ 0.5f, 0.5f };
        glm::vec2 Position{ 100.0f, -60.0f };
        glm::vec2 Size{ 200.0f, 60.0f };
        float FontSize = 0.0f;                ///< text height in UI units (0 = the rect's height)
        int Order = 0;                        ///< drawn above its lower-Order siblings (children are drawn above their parent)
        /// The look, not the layout (UI tweens animate it): drawn Scale times as big around its pivot,
        /// VisualOffset UI units away and Alpha opaque - its UI children with it. Clicks follow what is
        /// drawn; at Alpha 0 it takes none.
        glm::vec2 Scale{ 1.0f };
        glm::vec2 VisualOffset{ 0.0f };
        float Alpha = 1.0f;

        /// Both anchors at a preset point (Position and Size stay: it moves with the new anchor).
        void SetAnchor(AnchorPoint point)
        {
            AnchorMin = AnchorMax = UILayout::PresetPoint((int)point);
        }
        /// The preset both anchors are at (-1: they are not at one of the 9 points).
        int GetAnchorPreset() const
        {
            for (int i = 0; i < 9; i++)
                if (AnchorMin == UILayout::PresetPoint(i) && AnchorMax == AnchorMin)
                    return i;
            return -1;
        }
        bool IsStretchedX() const { return AnchorMin.x != AnchorMax.x; }
        bool IsStretchedY() const { return AnchorMin.y != AnchorMax.y; }
        /// The corners' offsets from the anchors: OffsetMin = (left, bottom) from AnchorMin,
        /// OffsetMax = (right, top) from AnchorMax (inward edges: left > 0, right < 0).
        glm::vec2 GetOffsetMin() const { return Position - Size * Pivot; }
        glm::vec2 GetOffsetMax() const { return Position + Size * (1.0f - Pivot); }
        void SetOffsets(const glm::vec2& offsetMin, const glm::vec2& offsetMax)
        {
            Size = offsetMax - offsetMin;
            Position = offsetMin + Size * Pivot;
        }
        /// Stretched over the whole parent, `inset` px in from its edges (left, right, top, bottom).
        void SetStretch(const glm::vec4& inset = glm::vec4(0.0f))
        {
            AnchorMin = { 0.0f, 0.0f };
            AnchorMax = { 1.0f, 1.0f };
            SetOffsets({ inset.x, inset.w }, { -inset.y, -inset.z });
        }
        /// The rectangle inside `parent` (UI units).
        UIRect ComputeRect(const UIRect& parent) const { return UILayout::ComputeRect(parent, AnchorMin, AnchorMax, Pivot, Position, Size); }
        /// Position / Size so that it has `rect` inside `parent`.
        void SetRect(const UIRect& parent, const UIRect& rect) { UILayout::FromRect(parent, AnchorMin, AnchorMax, Pivot, rect, Position, Size); }
    };

    /// A canvas: the UI below it (this entity's subtree - UI Transforms, also under plain entities)
    /// is scaled for the screen. ScaleWithScreenSize: made for ReferenceResolution, it grows and
    /// shrinks with the screen (MatchWidthOrHeight: 0 follows the width, 1 the height, 0.5 both
    /// halfway - in log space; Expand: always fits; Shrink: always fills). ConstantPixelSize: 1 UI
    /// unit = ScaleFactor px. Only the outermost canvas of a subtree counts.
    struct UICanvasComponent
    {
        UIScaleMode ScaleMode = UIScaleMode::ScaleWithScreenSize;
        float ScaleFactor = 1.0f;                       ///< ConstantPixelSize
        glm::vec2 ReferenceResolution{ 1920.0f, 1080.0f };
        UIScreenMatchMode MatchMode = UIScreenMatchMode::MatchWidthOrHeight;
        float MatchWidthOrHeight = 0.5f;

        /// runtime: the scale of the last layout (screen px per UI unit)
        float CurrentScale = 1.0f;

        float ComputeScale(const glm::vec2& screen) const
        {
            return UILayout::CanvasScale(screen, ScaleMode, ScaleFactor, ReferenceResolution, MatchMode, MatchWidthOrHeight);
        }
    };

    /// Arranges the entity's UI children - in a row (Horizontal), a column (Vertical) or a grid -
    /// inside its rectangle, in hierarchy order (inactive children and those whose layout element
    /// ignores the layout are left out). Their anchors and Position no longer count; their size
    /// too when the group controls it (see UILayoutGroupSettings). FitWidth / FitHeight: the
    /// group's own rectangle takes its content's preferred size (a list that grows with its items).
    struct UILayoutGroupComponent : UILayoutGroupSettings
    {
    };

    /// What a child of a layout group asks for (-1: not set). Min: the least it is shrunk to;
    /// Preferred: its size when there is room (not set: its own size); Flexible: its share of
    /// the space left over (relative to its siblings' - 1 and 2: the second gets twice as much).
    /// These count where the group controls the children's size. IgnoreLayout: the group leaves it
    /// where its own UI Transform puts it.
    struct UILayoutElementComponent
    {
        bool IgnoreLayout = false;
        glm::vec2 MinSize{ -1.0f };
        glm::vec2 PreferredSize{ -1.0f };
        glm::vec2 FlexibleSize{ -1.0f };
    };

    /// How keyboard / gamepad navigation leaves this widget (button, slider, toggle, input field):
    /// Automatic - to the nearest widget in the pressed direction; Horizontal / Vertical - the same,
    /// only left / right or up / down; Explicit - to the widgets set for each direction (empty: none);
    /// None - it is not reached by navigation (a click still selects it). Widgets without this
    /// component navigate Automatically. SelectOnEnable: it is selected when it becomes active while
    /// the game runs (also at the start) - a menu's first button.
    struct UINavigationComponent
    {
        enum class Mode { Automatic = 0, Horizontal, Vertical, Explicit, None };
        Mode NavigationMode = Mode::Automatic;
        EntityRef Up, Down, Left, Right;     ///< Explicit
        bool SelectOnEnable = false;
        bool ShowHighlight = true;           ///< the outline around it while selected by keyboard / gamepad
        glm::vec4 HighlightColor{ 1.0f, 0.80f, 0.28f, 1.0f };

        /// runtime
        bool WasActive = false;              ///< SelectOnEnable notices the switch on
    };

    /// Animations of a UI element (tweens of its UI Transform's look - see UIAnimation.h):
    /// - Show: played when it becomes active while the game runs (at the start too, unless
    ///   ShowAtStart is off) - it comes in from its preset's look (faded out, smaller, off to a side);
    /// - Hide: Scene::HideUI / ScriptableEntity::HideUI play it, then switch the entity off;
    /// - feedback: a button / toggle / slider / input field on the entity grows to HoverScale under
    ///   the mouse (or while selected by the keyboard) and shrinks to PressScale while pressed (1: off).
    struct UIAnimationComponent
    {
        UIAnimPreset Show = UIAnimPreset::Pop;
        UIAnimPreset Hide = UIAnimPreset::Fade;
        bool ShowAtStart = true;
        float Duration = 0.25f;                 ///< seconds (show and hide)
        float ShowDelay = 0.0f;                 ///< seconds before the show starts (staggered menus)
        Ease ShowEase = Ease::OutBack;
        Ease HideEase = Ease::InQuad;
        float SlideDistance = 160.0f;           ///< UI units (Slide presets)
        float HoverScale = 1.0f;
        float PressScale = 1.0f;
        float FeedbackDuration = 0.08f;         ///< seconds to the new feedback scale

        /// runtime
        bool WasActive = false;                 ///< the show notices the switch on
        bool Hiding = false;                    ///< a hide plays: the entity goes off when it ends
        bool RestSaved = false;                 ///< RestLook holds the look to come back to
        UIAnimMath::Look RestLook;
        float FeedbackScale = 1.0f, FeedbackFrom = 1.0f, FeedbackTo = 1.0f, FeedbackTime = 1.0f;

        void ResetRuntime()
        {
            WasActive = Hiding = RestSaved = false;
            RestLook = {};
            FeedbackScale = FeedbackFrom = FeedbackTo = FeedbackTime = 1.0f;
        }
    };

    /// A clickable rectangle: tinted by its state colours (Normal / Hover / Pressed), OnClick() on
    /// the entity's script, "UIClick" on the scene's event bus (sender: the button) and - with a
    /// ClickMessage - that script message to ClickTarget (empty: every running script, Broadcast).
    /// Enter / gamepad A clicks it while it is selected (keyboard / gamepad navigation).
    struct UIButtonComponent
    {
        glm::vec4 NormalColor{ 0.16f, 0.62f, 0.50f, 1.0f };
        glm::vec4 HoverColor{ 0.22f, 0.74f, 0.60f, 1.0f };
        glm::vec4 PressedColor{ 0.10f, 0.45f, 0.36f, 1.0f };
        bool Interactable = true;
        std::string ClickMessage;            ///< "" = none
        EntityRef ClickTarget;               ///< empty: Broadcast

        /// runtime state (read by scripts)
        bool Hovered = false;
        bool Pressed = false;
        bool Clicked = false; ///< true for one frame
    };

    /// A picture in the UI rect: a texture or one of its sprite sheet frames ("" = a plain colour),
    /// tinted by Color and by a UI Button's state colours. Type: Simple (stretched, or fitted with
    /// PreserveAspect), Sliced (9-slice: corners keep their size - border px x BorderScale), Tiled, or
    /// Filled (FillAmount 0..1 of it: horizontal, vertical or radial - bars, cooldowns, timers).
    /// RaycastTarget: the mouse stops at it - UI widgets drawn below it are not hovered or clicked.
    struct UIImageComponent
    {
        glm::vec4 Color{ 1.0f };
        Ref<Texture2D> Texture;
        std::string TexturePath;       ///< relative to project assets
        std::string SpriteName;        ///< a named frame of the texture's sprite sheet ("" = the whole texture)
        UIImageType Type = UIImageType::Simple;
        bool PreserveAspect = false;   ///< Simple / Filled
        glm::vec4 SliceBorder{ 0.0f }; ///< Sliced / Tiled: px left, right, top, bottom (all 0 = the frame's border)
        float BorderScale = 1.0f;      ///< Sliced / Tiled: corner size on screen = border px x this
        UIFillMethod FillMethod = UIFillMethod::Horizontal;
        int FillOrigin = 0;            ///< Horizontal: 0 left, 1 right; Vertical: 0 bottom, 1 top; Radial: 0 top, 1 right, 2 bottom, 3 left
        float FillAmount = 1.0f;
        bool FillClockwise = true;     ///< Radial 360
        bool PixelArt = false;         ///< nearest filtering (crisp pixels)
        bool RaycastTarget = true;

        bool HasSliceBorder() const { return SliceBorder.x > 0.0f || SliceBorder.y > 0.0f || SliceBorder.z > 0.0f || SliceBorder.w > 0.0f; }
    };

    /// A value picked by dragging a handle along a track or clicking the track: MinValue..MaxValue
    /// (whole numbers only with WholeNumbers; MinValue > MaxValue runs backwards). Drawn over the
    /// entity's own image / sprite: the track, its filled part up to the handle, the handle. A change
    /// by the user calls OnValueChanged(value) on the entity's script and emits "UISliderChanged"
    /// (payload float, sender: the slider) on the scene's event bus; Scene::SetSliderValue sets it from code.
    struct UISliderComponent
    {
        float MinValue = 0.0f;
        float MaxValue = 1.0f;
        float Value = 0.5f;
        bool WholeNumbers = false;
        UIDirection Direction = UIDirection::LeftToRight;
        bool Interactable = true;
        float TrackThickness = 8.0f;   ///< px (0 = the whole rect)
        float HandleSize = 0.0f;       ///< px across (0 = the rect's thickness)
        bool Rounded = true;           ///< round track ends and a round handle (else square)
        glm::vec4 TrackColor{ 0.12f, 0.13f, 0.16f, 1.0f };
        glm::vec4 FillColor{ 0.16f, 0.62f, 0.50f, 1.0f };
        glm::vec4 HandleColor{ 0.92f, 0.94f, 0.96f, 1.0f };

        /// runtime state (read by scripts)
        bool Hovered = false;
        bool Dragging = false;
        bool ValueChanged = false;     ///< true for one frame: the user changed the value

        float GetNormalizedValue() const { return UIMath::Normalize(Value, MinValue, MaxValue); }
        /// Value set from 0..1 of the range (rounded with WholeNumbers). No notification.
        void SetNormalizedValue(float t) { Value = UIMath::Denormalize(t, MinValue, MaxValue, WholeNumbers); }
    };

    /// An on / off switch: a check box (or a sliding switch) at the left of the rect with the
    /// entity's Text as its label right of it; a click switches it. Toggles of the same Group are
    /// radio buttons: one on at a time (AllowSwitchOff: the one that is on can be clicked off). A
    /// change calls OnToggleChanged(isOn) on the entity's script and emits "UIToggleChanged"
    /// (payload bool); Scene::SetToggle sets it from code (the group follows).
    struct UIToggleComponent
    {
        bool IsOn = false;
        bool Interactable = true;
        UIToggleStyle Style = UIToggleStyle::Checkbox;
        UIToggleMark Mark = UIToggleMark::Check;
        float BoxSize = 0.0f;          ///< px (0 = the rect's height)
        float LabelSpacing = 10.0f;    ///< px between the box and the label
        std::string Group;             ///< "" = on its own
        bool AllowSwitchOff = false;
        glm::vec4 BoxColor{ 0.12f, 0.13f, 0.16f, 1.0f };
        glm::vec4 OnColor{ 0.16f, 0.62f, 0.50f, 1.0f };   ///< the box (switch: the track) when on
        glm::vec4 MarkColor{ 1.0f };                       ///< the check mark / the switch's knob

        /// runtime state (read by scripts)
        bool Hovered = false;
        bool Pressed = false;
        bool ValueChanged = false;     ///< true for one frame: it was switched
        float Transition = 0.0f;       ///< 0..1, follows IsOn (the switch's knob slides)
    };

    /// A bar filled to Value (0..1) in Direction over its background (drawn over the entity's own
    /// image / sprite). UseLowColor: the fill blends from LowColor (empty) to FillColor (full) - a
    /// health bar going red. SmoothSpeed > 0: the bar follows Value at that much per second.
    struct UIProgressBarComponent
    {
        float Value = 0.5f;
        UIDirection Direction = UIDirection::LeftToRight;
        glm::vec4 BackgroundColor{ 0.12f, 0.13f, 0.16f, 1.0f };
        glm::vec4 FillColor{ 0.16f, 0.62f, 0.50f, 1.0f };
        bool UseLowColor = false;
        glm::vec4 LowColor{ 0.86f, 0.24f, 0.20f, 1.0f };
        float Padding = 3.0f;          ///< px between the background's edge and the fill
        float SmoothSpeed = 0.0f;      ///< per second (0 = at once)
        bool ShowPercentage = false;   ///< "75%" in the middle
        glm::vec4 TextColor{ 1.0f };

        /// runtime: what is drawn (follows Value while the game runs)
        float DisplayValue = 0.5f;

        glm::vec4 GetFillColor(float t) const { return UseLowColor ? glm::mix(LowColor, FillColor, std::clamp(t, 0.0f, 1.0f)) : FillColor; }
    };

    /// A single line of text the player types into: click it (or Scene::FocusInputField) to edit -
    /// caret, selection (drag, Shift + arrows, double click: everything), Ctrl+A / C / X / V, Home /
    /// End, Ctrl + arrows by words; Enter submits, Escape cancels (the text it had before), a click
    /// elsewhere ends editing. UTF-8 throughout (oʻ, gʻ, sh, Cyrillic). ContentType filters what can
    /// be typed (Integer, Decimal, Alphanumeric) or hides it (Password); CharacterLimit counts
    /// characters. Script callbacks: OnTextChanged(text), OnTextSubmit(text), OnTextEndEdit(text)
    /// (event bus: "UITextChanged", "UITextSubmitted", "UITextEndEdit", payload std::string).
    struct UIInputFieldComponent
    {
        std::string Text;
        std::string Placeholder = "Matn kiriting...";
        UIContentType ContentType = UIContentType::Standard;
        int CharacterLimit = 0;        ///< characters (0 = no limit)
        bool Interactable = true;
        bool ReadOnly = false;         ///< can be selected and copied, not changed
        bool SelectAllOnFocus = false;
        std::string FontPath;          ///< "" = the engine's font
        Ref<Font> FontAsset;
        float Padding = 10.0f;         ///< px left / right of the text
        glm::vec4 BackgroundColor{ 0.09f, 0.10f, 0.12f, 1.0f };
        glm::vec4 OutlineColor{ 0.28f, 0.30f, 0.35f, 1.0f };
        glm::vec4 FocusColor{ 0.16f, 0.62f, 0.50f, 1.0f };  ///< the outline while editing
        glm::vec4 TextColor{ 0.95f, 0.96f, 0.98f, 1.0f };
        glm::vec4 PlaceholderColor{ 0.50f, 0.53f, 0.58f, 1.0f };
        glm::vec4 CaretColor{ 1.0f };
        glm::vec4 SelectionColor{ 0.22f, 0.46f, 0.86f, 0.55f };
        float CaretBlinkRate = 0.53f;  ///< seconds on / off (0 = always on)

        /// runtime state (read by scripts)
        bool Focused = false;
        bool Hovered = false;
        bool TextChanged = false;      ///< true for one frame
        bool Submitted = false;        ///< true for one frame (Enter)
        size_t Caret = 0, Anchor = 0;  ///< bytes into Text (Anchor: the selection's other end)
        float BlinkTime = 0.0f;
        float Scroll = 0.0f;           ///< px the text is moved left (a long text keeps the caret in view)
        std::string TextBeforeEdit;    ///< Escape brings it back
        bool Selecting = false;        ///< the mouse drags a selection

        void ResetRuntime()
        {
            Focused = Hovered = TextChanged = Submitted = Selecting = false;
            Caret = Anchor = 0;
            BlinkTime = Scroll = 0.0f;
            TextBeforeEdit.clear();
        }
    };

    /// A mask: the entity's UI subtree (its UI children and theirs) is drawn - and found by the mouse
    /// - only inside its rectangle as drawn, Padding px in from its edges (left, right, top, bottom;
    /// negative: out). A mask inside a mask shows only what both let through (a list in a scrolled
    /// panel). ShowGraphic off: the mask's own picture (image, sprite, button colour) is not drawn -
    /// it only clips. Rectangles only (the scissor test): a round picture does not round the clip.
    /// On a scroll view it clips to the view: its Viewport's rectangle (as drawn), else its own without
    /// its scrollbars (they stay out).
    struct UIMaskComponent
    {
        bool ShowGraphic = true;
        glm::vec4 Padding{ 0.0f };
    };

    /// A scroll view: its Content (a UI entity of its subtree - a list whose layout group fits its
    /// height, a big map) moves inside the view (the Viewport's rectangle, else the scroll view's
    /// own without its scrollbars) by the mouse wheel, by dragging it (also when the press starts
    /// on a button in it: the button is then not clicked), by the scrollbars it draws along its
    /// right / bottom edge (drag the handle, click the track) and by keyboard / gamepad navigation
    /// (the widget selected is scrolled into view). Its UI children are laid out in the space
    /// left of the scrollbars. A UI Mask on it (or on the Viewport) hides what is outside the view.
    /// Movement: Elastic - pulled past an edge it resists and springs back in Elasticity seconds;
    /// Clamped - it stops at the edges; Unrestricted. Inertia: it glides on after a drag, keeping
    /// DecelerationRate of its speed per second. The content's UI Transform Position is what moves
    /// (it must not be placed by a layout group). A change calls OnScrollChanged(position) on its
    /// script and emits "UIScrollChanged" (payload glm::vec2: x 0 left .. 1 right, y 1 top .. 0 bottom);
    /// Scene::SetScrollPosition / ScrollBy / ScrollIntoView move it from code.
    struct UIScrollViewComponent
    {
        EntityRef Content;
        EntityRef Viewport;                  ///< empty: the scroll view itself
        bool Horizontal = false;
        bool Vertical = true;
        UIScrollMovement Movement = UIScrollMovement::Elastic;
        float Elasticity = 0.1f;             ///< seconds it springs back in
        bool Inertia = true;
        float DecelerationRate = 0.135f;     ///< the part of the speed kept after a second
        float ScrollSensitivity = 40.0f;     ///< UI units per notch of the mouse wheel
        bool Draggable = true;               ///< the content can be dragged with the mouse
        UIScrollbarVisibility Scrollbars = UIScrollbarVisibility::AutoHide;
        float ScrollbarWidth = 10.0f;        ///< UI units
        float ScrollbarInset = 3.0f;         ///< UI units between a bar and the edges
        glm::vec4 TrackColor{ 0.0f, 0.0f, 0.0f, 0.28f };
        glm::vec4 HandleColor{ 0.78f, 0.80f, 0.86f, 0.85f };

        /// runtime state (read by scripts)
        glm::vec2 NormalizedPosition{ 0.0f, 1.0f };
        glm::vec2 Velocity{ 0.0f };          ///< UI units per second (inertia, the spring)
        bool Dragging = false;               ///< its content or a scrollbar is dragged
        bool ScrollChanged = false;          ///< true for one frame: it moved
        bool ShowHorizontalBar = false;      ///< the scrollbars shown now (the layout leaves room for them)
        bool ShowVerticalBar = false;
        int HoveredBar = -1;                 ///< the scrollbar under the mouse: 0 horizontal, 1 vertical, -1 none
        int DraggedBar = -1;
        bool Revealing = false;              ///< moving to show the selected widget
        glm::vec2 RevealTarget{ 0.0f };      ///< the content's Position it moves to
        bool PositionKnown = false;          ///< NormalizedPosition was measured (the first frame tells nothing)

        void ResetRuntime()
        {
            NormalizedPosition = { 0.0f, 1.0f };
            Velocity = RevealTarget = glm::vec2(0.0f);
            Dragging = ScrollChanged = Revealing = PositionKnown = false;
            HoveredBar = DraggedBar = -1;
        }
    };

    // ------------------------------------------------------------------ physics 2D
    /// A 2D physics body (Box2D), made at play start for an active entity: Static, Dynamic or Kinematic.
    /// Its colliders (BoxCollider2D, CircleCollider2D ...) give it its shape; without one it touches nothing.
    struct Rigidbody2DComponent
    {
        enum class BodyType { Static = 0, Dynamic, Kinematic };
        BodyType Type = BodyType::Dynamic;
        bool FixedRotation = false;
        float GravityScale = 1.0f;
        float LinearDamping = 0.0f;
        float AngularDamping = 0.01f;
        bool Bullet = false;
        /// The collision layer of its colliders (0..15, Project settings > Physics layers): which other
        /// layers it touches is the project's layer matrix. A collider may pick its own (Layer >= 0).
        int Layer = 0;
        /// Sleeping: a body that keeps still for a while (scene settings: Physics 2D > time to sleep),
        /// together with everything touching it, is not simulated until something hits it, a joint
        /// pulls it, a script moves it or wakes it (ScriptableEntity::WakeUp). StartAsleep: it starts
        /// like that (a tower that waits to be knocked over); NeverSleep: it is always simulated.
        enum class SleepMode { StartAwake = 0, StartAsleep, NeverSleep };
        SleepMode Sleeping = SleepMode::StartAwake;

        void* RuntimeBody = nullptr; ///< b2Body*
    };

    // What every 2D collider has besides its shape. The values are the collider's own unless
    // MaterialPath names a physics material asset (.zphysmat): then its friction, bounciness,
    // density and combine modes are used (and follow the file when it changes).
    // Layer: -1 = the Rigidbody 2D's layer, else this collider's own collision layer.
    // BoxCollider2D, CircleCollider2D, PolygonCollider2D and EdgeCollider2D have these fields:
    //   float Density, Friction, Restitution, RestitutionThreshold; bool IsSensor;
    //   std::string MaterialPath; int Layer;

    /// A box shape for the entity's Rigidbody 2D (Size: half extents, times the transform's scale).
    struct BoxCollider2DComponent
    {
        glm::vec2 Offset = { 0.0f, 0.0f };
        glm::vec2 Size = { 0.5f, 0.5f }; ///< half extents (multiplied by transform scale)

        float Density = 1.0f;
        float Friction = 0.5f;
        float Restitution = 0.0f;
        float RestitutionThreshold = 0.5f;
        bool IsSensor = false;
        std::string MaterialPath; ///< physics material asset (.zphysmat), "" = the values above
        int Layer = -1;           ///< -1: the Rigidbody 2D's layer
        /// A one-way platform: bodies jump up through it from below and land on its top (its up is the
        /// entity's +Y). Scripts can drop through with ScriptableEntity::DropThroughPlatforms.
        /// (PlatformEffector2DComponent does the same with more settings.)
        bool OneWay = false;

        void* RuntimeFixture = nullptr;
    };

    /// A circle shape for the entity's Rigidbody 2D (Radius times the transform's x scale).
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

    /// Any polygon (points in the entity's frame, multiplied by its scale like a box's size, plus
    /// Offset). A concave or long outline is split into convex pieces of at most 8 corners (ear
    /// clipping, then the pieces merged back while they stay convex); an outline that crosses itself
    /// uses its convex hull. The editor moves / adds / removes the points in the viewport.
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

        void* RuntimeFixture = nullptr; ///< the first piece's b2Fixture*
        int RuntimePieces = 0;          ///< how many convex pieces the body got
    };

    /// A line through the points (an open chain), or a closed loop: ground, slopes, walls. Edges
    /// have no area (no mass); on a dynamic body they collide but do not weigh anything. Two-sided by
    /// default; OneSided makes a smooth chain that only stops bodies coming from the left of the
    /// points' direction (points left to right: from above; a counter-clockwise loop: from inside) -
    /// bodies sliding along it do not catch on the corners between its segments.
    struct EdgeCollider2DComponent
    {
        glm::vec2 Offset = { 0.0f, 0.0f };
        std::vector<glm::vec2> Points = { { -1.0f, 0.0f }, { 1.0f, 0.0f } };
        bool Loop = false;
        bool OneSided = false;

        float Density = 1.0f; ///< unused by edges (kept for the material / inspector)
        float Friction = 0.5f;
        float Restitution = 0.0f;
        float RestitutionThreshold = 0.5f;
        bool IsSensor = false;
        std::string MaterialPath;
        int Layer = -1;

        void* RuntimeFixture = nullptr; ///< the first segment's / the chain's b2Fixture*
    };

    /// Turns the entity's colliders (its Rigidbody 2D's, or a tilemap's tiles) into platforms. OneWay:
    /// bodies pass through from below / the sides and land on the surface - collisions only from
    /// within SurfaceArc (degrees, around the surface's up: the entity's +Y turned by RotationOffset).
    /// 180 is the classic one-way platform (BoxCollider2D::OneWay), smaller arcs leave out the
    /// corners, larger ones also hold bodies at the sides. Side contacts (within SideArc of the
    /// surface's left / right) get no friction / bounce unless UseSideFriction / UseSideBounce, so a
    /// player pushing against the side does not stick to it. Scripts drop through with
    /// ScriptableEntity::DropThroughPlatforms.
    struct PlatformEffector2DComponent
    {
        bool OneWay = true;
        float SurfaceArc = 180.0f;    ///< degrees, 1..360
        float RotationOffset = 0.0f;  ///< degrees: the surface's up from the entity's +Y
        bool UseSideFriction = false;
        bool UseSideBounce = false;
        float SideArc = 1.0f;         ///< degrees, 0..180
    };

    /// Pushes the dynamic bodies inside the entity's colliders (usually triggers on a static Rigidbody
    /// 2D): wind, a jet of air, a current. Every physics step each body overlapping one of them (whose
    /// collider's layer is in ColliderMask) gets ForceMagnitude along ForceAngle and is slowed down by
    /// Drag / AngularDrag. Solid colliders push the bodies that touch them.
    struct AreaEffector2DComponent
    {
        float ForceAngle = 90.0f;      ///< degrees (0 = +X, 90 = up)
        bool UseGlobalAngle = false;   ///< the angle in the world, else turned with the entity
        float ForceMagnitude = 10.0f;  ///< N (ScaleWithMass: N per kg, i.e. an acceleration)
        float ForceVariation = 0.0f;   ///< a random amount up to this much added / taken each step (gusts)
        bool ScaleWithMass = false;    ///< every body gets the same acceleration, light or heavy
        float Drag = 0.0f;             ///< 1/s: how quickly the bodies' velocity dies away inside
        float AngularDrag = 0.0f;
        uint16_t ColliderMask = 0xFFFF; ///< the collision layers (bits) it acts on

        int AffectedBodies = 0;        ///< runtime: bodies pushed in the last physics step
    };

    /// A fluid (water) in the entity's colliders (triggers on a static Rigidbody 2D) up to its surface:
    /// SurfaceLevel world units above the entity's position, level (at right angles to gravity). A
    /// body inside gets the weight of the fluid it displaces upwards - Density x its collider area
    /// below the surface x gravity, at the centre of that part, so a collider less dense than the
    /// fluid floats and turns upright - and is slowed by the drags and carried by the flow in
    /// proportion to how much of it is under.
    struct BuoyancyEffector2DComponent
    {
        float SurfaceLevel = 0.5f;     ///< world units above the entity's position
        float Density = 2.0f;          ///< the fluid's (colliders' Density: 1 by default)
        float LinearDrag = 1.5f;       ///< 1/s, for a fully submerged body
        float AngularDrag = 1.0f;
        float FlowAngle = 0.0f;        ///< degrees in the world (0 = +X)
        float FlowMagnitude = 0.0f;    ///< N on a fully submerged body (a current)
        float FlowVariation = 0.0f;
        uint16_t ColliderMask = 0xFFFF;

        int AffectedBodies = 0;        ///< runtime: bodies in the fluid in the last physics step
        float SubmergedArea = 0.0f;    ///< runtime: their area under the surface
    };

    // ------------------------------------------------------------------ joints 2D
    /// What every 2D joint has. A joint ties the entity's Rigidbody 2D to the connected body (another
    /// entity's Rigidbody 2D or Tilemap) or, without one, to a fixed point in the world. It is made
    /// while the game runs / simulates once both bodies exist, goes when either of them leaves the
    /// world (inactive, destroyed) and comes back with them. Points are in the bodies' own frames
    /// (body units: not scaled, like the colliders' offsets); angles in degrees.
    struct Joint2DComponentBase
    {
        EntityRef ConnectedBody;           ///< empty: a fixed point in the world
        glm::vec2 Anchor{ 0.0f };          ///< on this body
        glm::vec2 ConnectedAnchor{ 0.0f }; ///< on the connected body, or the world point
        /// The connected anchor is where Anchor is when the joint is made (written back then).
        bool AutoConfigureConnectedAnchor = true;
        bool CollideConnected = false;     ///< the two bodies still collide with each other
        /// The joint breaks (goes for good, OnJointBreak) when its reaction force / torque gets
        /// larger than this. 0 = never.
        float BreakForce = 0.0f;
        float BreakTorque = 0.0f;

        /// runtime state (scripts may read it; Broken = false makes a broken joint again)
        void* RuntimeJoint = nullptr;  ///< b2Joint*
        uint64_t RuntimeConnected = 0; ///< the ConnectedBody it was made with (changed: made again)
        bool Broken = false;
        float ReactionForce = 0.0f;    ///< after the last physics step
        float ReactionTorque = 0.0f;

        void ResetRuntime()
        {
            RuntimeJoint = nullptr;
            RuntimeConnected = 0;
            Broken = false;
            ReactionForce = ReactionTorque = 0.0f;
        }
    };

    /// Keeps the two anchors at a distance: a rigid rod, a rope (MaxDistanceOnly) or a spring (Frequency).
    /// Its connected anchor is not auto-configured by default (both ends at one point would be no
    /// distance): the connected body's origin, or the world point.
    struct DistanceJoint2DComponent : Joint2DComponentBase
    {
        DistanceJoint2DComponent() { AutoConfigureConnectedAnchor = false; }

        bool AutoConfigureDistance = true; ///< Distance = the anchors' distance when the joint is made
        float Distance = 1.0f;
        bool MaxDistanceOnly = false;      ///< a rope: closer is allowed, further is not
        float Frequency = 0.0f;            ///< spring oscillations per second (0 = rigid)
        float DampingRatio = 0.0f;         ///< 0..1 (1 = no oscillation)

        float CurrentDistance = 0.0f;      ///< runtime: after the last step
    };

    /// A hinge: the bodies share the anchor point and turn around it, optionally within limits
    /// (relative to the pose the joint was made in) and driven by a motor.
    struct RevoluteJoint2DComponent : Joint2DComponentBase
    {
        bool EnableLimit = false;
        float LowerAngle = -45.0f; ///< degrees: this body's rotation relative to the connected body
        float UpperAngle = 45.0f;
        bool EnableMotor = false;
        float MotorSpeed = 0.0f;      ///< degrees per second (positive: counter-clockwise)
        float MaxMotorTorque = 10.0f;

        float JointAngle = 0.0f;      ///< runtime: degrees from the start pose
        float JointSpeed = 0.0f;      ///< runtime: degrees per second
    };

    /// A slider: the body moves only along Axis relative to the connected body, without turning.
    struct PrismaticJoint2DComponent : Joint2DComponentBase
    {
        glm::vec2 Axis{ 1.0f, 0.0f }; ///< in this body's frame (when the joint is made)
        bool EnableLimit = false;
        float LowerTranslation = -1.0f; ///< units along the axis from the start position
        float UpperTranslation = 1.0f;
        bool EnableMotor = false;
        float MotorSpeed = 0.0f;        ///< units per second
        float MaxMotorForce = 10.0f;

        float JointTranslation = 0.0f;  ///< runtime
        float JointSpeed = 0.0f;        ///< runtime: units per second
    };

    /// Glues the bodies together (rigid, or springy around the angle with Frequency).
    struct WeldJoint2DComponent : Joint2DComponentBase
    {
        float Frequency = 0.0f;    ///< angular spring oscillations per second (0 = rigid)
        float DampingRatio = 0.0f;
    };

    /// A wheel: this body (the wheel) turns freely around the anchor and is held on the connected
    /// body (the chassis) by a suspension spring along Axis; the motor drives the wheel.
    struct WheelJoint2DComponent : Joint2DComponentBase
    {
        glm::vec2 Axis{ 0.0f, 1.0f }; ///< the suspension direction, in this body's frame (when made)
        float Frequency = 4.0f;       ///< suspension oscillations per second (0 = no spring: slides freely)
        float DampingRatio = 0.7f;
        bool EnableLimit = false;     ///< suspension travel
        float LowerTranslation = -0.25f;
        float UpperTranslation = 0.25f;
        bool EnableMotor = false;
        float MotorSpeed = 0.0f;      ///< degrees per second (positive: counter-clockwise)
        float MaxMotorTorque = 10.0f;

        float JointTranslation = 0.0f; ///< runtime: suspension travel
        float JointSpeed = 0.0f;       ///< runtime: the wheel's spin, degrees per second
    };

    // ------------------------------------------------------------------ physics 3D (Jolt)
    /// A 3D physics body (Jolt), made at play start for an active entity with a 3D collider: Static,
    /// Dynamic or Kinematic.
    struct Rigidbody3DComponent
    {
        enum class BodyType { Static = 0, Dynamic, Kinematic };
        BodyType Type = BodyType::Dynamic;
        float Mass = 1.0f;
        float LinearDamping = 0.05f;
        float AngularDamping = 0.05f;
        float GravityFactor = 1.0f;
        bool LockRotation = false; ///< keep upright (characters)
        /// The collision layer of its colliders (0..15, Project settings > Physics layers - shared with
        /// 2D physics): which other layers it touches is the project's layer matrix. Raycasts and the
        /// other queries take a mask of layers. May change while the game runs.
        int Layer = 0;
        /// Continuous collision detection (dynamic bodies): the body is swept along its path every step,
        /// so a fast one (a bullet, a thrown ball) stops at a thin wall instead of passing through it
        /// between two steps. Costs more: for the fast and small ones. May change while the game runs.
        bool ContinuousCollision = false;
        /// Sleeping: a body that keeps still for a while (scene settings: Physics 3D > time to sleep),
        /// together with everything touching it, is not simulated until something hits it, a joint
        /// pulls it, a script moves or wakes it (ScriptableEntity::WakeUp3D). StartAsleep: it starts
        /// like that (a stack that waits to be knocked over); NeverSleep: it is always simulated.
        enum class SleepMode { StartAwake = 0, StartAsleep, NeverSleep };
        SleepMode Sleeping = SleepMode::StartAwake;

        uint32_t RuntimeBodyID = 0xffffffff; ///< JPH::BodyID
    };

    // What every 3D collider has besides its shape. The friction / bounciness are the collider's own
    // unless MaterialPath names a physics material asset (.zphysmat, the same files as 2D colliders'):
    // then its friction, bounciness, bounce threshold and combine modes are used (and follow the file
    // when it changes; its density is not - a 3D body's mass is its Rigidbody 3D's). IsSensor: a
    // trigger - OnTriggerBegin / OnTriggerEnd instead of a collision; a body may have both trigger and
    // solid colliders. BoxCollider3D ... MeshCollider3D have these fields:
    //   glm::vec3 Offset; float Friction, Restitution; bool IsSensor; std::string MaterialPath;

    /// A box shape for the entity's Rigidbody 3D.
    struct BoxCollider3DComponent
    {
        glm::vec3 HalfExtents = { 0.5f, 0.5f, 0.5f }; ///< multiplied by transform scale
        glm::vec3 Offset = { 0.0f, 0.0f, 0.0f };
        float Friction = 0.5f;
        float Restitution = 0.0f;
        bool IsSensor = false;
        std::string MaterialPath; ///< physics material asset (.zphysmat), "" = the values above
    };

    /// A sphere shape for the entity's Rigidbody 3D.
    struct SphereCollider3DComponent
    {
        float Radius = 0.5f;
        glm::vec3 Offset = { 0.0f, 0.0f, 0.0f };
        float Friction = 0.5f;
        float Restitution = 0.0f;
        bool IsSensor = false;
        std::string MaterialPath; ///< physics material asset (.zphysmat), "" = the values above
    };

    /// A capsule shape along Y for the entity's Rigidbody 3D (Radius, HalfHeight of its cylinder).
    struct CapsuleCollider3DComponent
    {
        float Radius = 0.5f;
        float HalfHeight = 0.5f; ///< cylinder part, along Y
        glm::vec3 Offset = { 0.0f, 0.0f, 0.0f };
        float Friction = 0.5f;
        float Restitution = 0.0f;
        bool IsSensor = false;
        std::string MaterialPath; ///< physics material asset (.zphysmat), "" = the values above
    };

    /// A convex collider shaped like a mesh: the convex hull of the vertices of the entity's Mesh
    /// Renderer (its model or primitive) - or of the model ModelPath names - with at most MaxVertices
    /// corners (a mesh with more gets a simpler hull through its outermost points: cheaper, a little
    /// smaller). Hollows and holes are filled in (a cup is a solid block); for those use several
    /// colliders. Multiplied by the transform scale like the other 3D colliders. Hulls are made once per
    /// model asset and limit (CollisionShapes3D) and follow the model file when it changes.
    struct ConvexHullCollider3DComponent
    {
        std::string ModelPath;   ///< .gltf / .glb / .obj (relative to assets); "" = the Mesh Renderer's
        int MaxVertices = 32;    ///< 4..256
        glm::vec3 Offset = { 0.0f, 0.0f, 0.0f };
        float Friction = 0.5f;
        float Restitution = 0.0f;
        bool IsSensor = false;
        std::string MaterialPath; ///< physics material asset (.zphysmat), "" = the values above
    };

    /// A collider that is the mesh itself: every triangle of the entity's Mesh Renderer (its model or
    /// primitive) - or of the model ModelPath names - so stairs, arches, tunnels and rough ground
    /// collide where they are drawn. For static and kinematic Rigidbody 3D bodies only: a dynamic body
    /// leaves it out (use Convex Hull Collider 3D or primitive colliders for those). Triangles are
    /// solid from their front side (as they are drawn). Multiplied by the transform scale like the
    /// other 3D colliders. Each model's triangles and their tree are made once (CollisionShapes3D) and
    /// follow the model file when it changes.
    struct MeshCollider3DComponent
    {
        std::string ModelPath;   ///< .gltf / .glb / .obj (relative to assets); "" = the Mesh Renderer's
        glm::vec3 Offset = { 0.0f, 0.0f, 0.0f };
        float Friction = 0.5f;
        float Restitution = 0.0f;
        bool IsSensor = false;
        std::string MaterialPath; ///< physics material asset (.zphysmat), "" = the values above
    };

    // ------------------------------------------------------------------ joints 3D
    /// What every 3D joint has (Jolt constraints). Like the 2D joints: a joint ties the entity's
    /// Rigidbody 3D to the connected body (another entity's Rigidbody 3D) or, without one, to a fixed
    /// point in the world. It is made while the game runs / simulates once both bodies exist, in the
    /// pose they have then (a hinge's angle 0, a slider's position 0), goes when either of them leaves
    /// the world (inactive, destroyed) and comes back with them. Points are in the entities' own frames
    /// (scaled with them, like the 3D colliders' offsets), axes in this entity's frame, angles in
    /// degrees.
    struct Joint3DComponentBase
    {
        EntityRef ConnectedBody;           ///< empty: a fixed point in the world
        glm::vec3 Anchor{ 0.0f };          ///< on this body
        glm::vec3 ConnectedAnchor{ 0.0f }; ///< on the connected body, or the world point
        /// The connected anchor is where Anchor is when the joint is made (written back then).
        bool AutoConfigureConnectedAnchor = true;
        bool CollideConnected = false;     ///< the two bodies still collide with each other
        /// The joint breaks (goes for good, OnJointBreak3D) when its reaction force / torque gets larger
        /// than this. 0 = never.
        float BreakForce = 0.0f;
        float BreakTorque = 0.0f;

        /// runtime state (scripts may read it; Broken = false makes a broken joint again)
        uint32_t RuntimeJoint = 0;     ///< PhysicsWorld3D joint handle (0 = none)
        uint64_t RuntimeConnected = 0; ///< the ConnectedBody it was made with (changed: made again)
        bool Broken = false;
        float ReactionForce = 0.0f;    ///< after the last physics step
        float ReactionTorque = 0.0f;

        void ResetRuntime()
        {
            RuntimeJoint = 0;
            RuntimeConnected = 0;
            Broken = false;
            ReactionForce = ReactionTorque = 0.0f;
        }
    };

    /// Glues the bodies together: they move as one (until it breaks).
    struct FixedJoint3DComponent : Joint3DComponentBase
    {
    };

    /// A hinge (door, wheel, elbow): the bodies share the anchor point and this body turns only around
    /// Axis, optionally within limits (relative to the pose the joint was made in), driven by a motor
    /// or pulled towards an angle by a spring.
    struct HingeJoint3DComponent : Joint3DComponentBase
    {
        glm::vec3 Axis{ 0.0f, 1.0f, 0.0f }; ///< in this body's frame (when the joint is made)
        bool EnableLimit = false;
        float LowerAngle = -45.0f;    ///< -180..0 degrees (0 is the start pose)
        float UpperAngle = 45.0f;     ///< 0..180
        bool EnableMotor = false;
        float MotorSpeed = 0.0f;      ///< degrees per second (positive: counter-clockwise around Axis)
        float MaxMotorTorque = 10.0f; ///< N*m
        /// A spring towards SpringTargetAngle (while the motor is off): a door that swings shut.
        bool EnableSpring = false;
        float SpringTargetAngle = 0.0f;
        float SpringFrequency = 2.0f; ///< oscillations per second
        float SpringDamping = 0.5f;   ///< 0..1 (1 = no oscillation)

        float JointAngle = 0.0f;      ///< runtime: degrees from the start pose
        float JointSpeed = 0.0f;      ///< runtime: degrees per second
    };

    /// Keeps the two anchors between MinDistance and MaxDistance apart: a rigid rod (equal), a rope
    /// (MaxDistanceOnly: closer is allowed) or a range; Frequency makes the ends soft (a spring).
    /// Its connected anchor is not auto-configured by default (both ends at one point would be no
    /// distance): the connected body's origin, or the world point.
    struct DistanceJoint3DComponent : Joint3DComponentBase
    {
        DistanceJoint3DComponent() { AutoConfigureConnectedAnchor = false; }

        /// MaxDistance (and MinDistance unless MaxDistanceOnly) = the anchors' distance when the joint is made.
        bool AutoConfigureDistance = true;
        float MinDistance = 1.0f;
        float MaxDistance = 1.0f;
        bool MaxDistanceOnly = false;  ///< a rope: MinDistance is 0
        float Frequency = 0.0f;        ///< spring oscillations per second (0 = rigid)
        float DampingRatio = 0.0f;     ///< 0..1

        float CurrentDistance = 0.0f;  ///< runtime: after the last step
    };

    /// A slider: the body moves only along Axis relative to the connected body, without turning;
    /// optionally within limits (around the start position) and driven by a motor.
    struct SliderJoint3DComponent : Joint3DComponentBase
    {
        glm::vec3 Axis{ 1.0f, 0.0f, 0.0f }; ///< in this body's frame (when the joint is made)
        bool EnableLimit = false;
        float LowerTranslation = -1.0f; ///< <= 0: units along the axis from the start position
        float UpperTranslation = 1.0f;  ///< >= 0
        bool EnableMotor = false;
        float MotorSpeed = 0.0f;        ///< units per second
        float MaxMotorForce = 10.0f;    ///< N

        float JointTranslation = 0.0f;  ///< runtime
        float JointSpeed = 0.0f;        ///< runtime: units per second
    };

    /// A ball-and-socket joint (shoulder, hip, neck, a lamp on a chain): the bodies share the anchor
    /// point; this body may swing its Axis within a cone of SwingLimit degrees around where the axis was
    /// when the joint was made, and twist around it between TwistLower and TwistUpper. FrictionTorque
    /// resists turning (a ragdoll's limbs do not flail).
    struct ConeJoint3DComponent : Joint3DComponentBase
    {
        glm::vec3 Axis{ 0.0f, 1.0f, 0.0f }; ///< the twist axis in this body's frame (along a limb)
        float SwingLimit = 45.0f;    ///< 0..180 degrees: the cone's half angle (180 = free)
        float TwistLower = -30.0f;   ///< -180..0 degrees
        float TwistUpper = 30.0f;    ///< 0..180
        float FrictionTorque = 0.0f; ///< N*m

        float SwingAngle = 0.0f;     ///< runtime: degrees between the axis now and where it started
        float TwistAngle = 0.0f;     ///< runtime: degrees of twist around the axis
    };

    // ------------------------------------------------------------------ audio
    /// A sound the entity plays: a file of the assets (Path), started at play start (PlayOnStart) on an
    /// active entity, routed to a mixer group; Spatial makes it a 3D sound heard from the listener.
    struct AudioSourceComponent
    {
        std::string Path;     ///< relative to assets (.wav / .mp3 / .flac; .ogg has no decoder)
        float Volume = 1.0f;
        float Pitch = 1.0f;
        bool Loop = false;
        bool PlayOnStart = true;
        bool Spatial = false;      ///< 3D sound: volume/pan depend on the listener (Audio Listener / primary camera)
        float MinDistance = 1.0f;
        float MaxDistance = 30.0f;
        /// The mixer group it plays in (Audio Mixer window; an unknown name plays in SFX).
        std::string Output = "SFX";
        /// Streamed from disk while it plays instead of decoded into memory first (long music /
        /// ambience). Files larger than the project's threshold always stream.
        bool Stream = false;

        /// Random variation, picked anew each time it starts: Volume and Pitch are multiplied by a
        /// value between x (min) and y (max), e.g. pitch 0.9..1.1 for footsteps. 1..1 = none.
        glm::vec2 VolumeRandom{ 1.0f, 1.0f };
        glm::vec2 PitchRandom{ 1.0f, 1.0f };

        /// Voice limits: with the project's maximum of sounds playing (Audio Mixer > Settings) a less
        /// important sound (lower Priority, 0..255) gives its place to this one; among equals the
        /// steal policy decides. MaxInstances: this file at most so many times at once (0 = no limit).
        int Priority = 128;
        int MaxInstances = 0;
        AudioStealPolicy Steal = AudioStealPolicy::Oldest;

        /// 3D sound: how strongly its pitch follows the Doppler effect while it (or the listener)
        /// moves - x the project's Doppler factor; 0 = off. Its velocity comes from its movement.
        float DopplerFactor = 1.0f;

        /// Fades, in seconds (0 = none): the sound starts silent and fades in whenever it plays
        /// (PlayOnStart, PlayAudio); it fades out when it is stopped (StopAudio, the entity switched
        /// off or destroyed) instead of being cut. Scripts may pass their own times.
        float FadeIn = 0.0f;
        float FadeOut = 0.0f;
        /// Low-pass / high-pass on this source alone (e.g. a radio behind a wall); its mixer group
        /// may filter on top. Changes while it plays glide there.
        AudioFilterSettings Filter;
        /// 3D sound: a directional cone around the entity's forward (-Z) - full volume in front,
        /// OuterGain behind (a loudspeaker, a TV, someone talking).
        AudioConeSettings Cone;

        /// Runtime.
        uint32_t RuntimeHandle = 0;
        glm::vec3 Velocity{ 0.0f };     ///< units per second (measured while a 3D sound plays)
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

    /// Where the 3D sounds are heard from (instead of the primary camera), e.g. the player's head
    /// in a third-person game. Of several, the enabled one active in the hierarchy with the
    /// highest Priority listens (ties: the one created first); without one, the primary camera.
    struct AudioListenerComponent
    {
        bool Enabled = true;
        int Priority = 0;
        /// The listener's velocity is measured from how it moves, for the Doppler effect of moving
        /// sounds (off: a still listener).
        bool TrackVelocity = false;

        /// Runtime.
        glm::vec3 Velocity{ 0.0f };     ///< units per second (TrackVelocity)
        glm::vec3 LastPosition{ 0.0f };
        bool HasLastPosition = false;

        void ResetRuntime()
        {
            Velocity = glm::vec3(0.0f);
            LastPosition = glm::vec3(0.0f);
            HasLastPosition = false;
        }
    };

    /// A looping sound heard everywhere inside a volume - a forest, a cave, rain, a crowd, a river
    /// bank. Inside it plays without a position (the same in both ears); as the listener (Audio
    /// Listener / primary camera) comes up to it, it fades in over BlendDistance outside the edge,
    /// and FadeTime smooths changes (a teleport, the zone switched on or off). The volume is a box
    /// (Size in the entity's space: its scale and rotation apply) or a sphere (Radius x the largest
    /// scale). The sound starts when it can first be heard and stops when it fades out.
    struct AmbientZoneComponent
    {
        enum class ZoneShape { Box = 0, Sphere };

        std::string Path;            ///< relative to assets; loops
        float Volume = 1.0f;
        float Pitch = 1.0f;
        std::string Output = "SFX";  ///< the mixer group (an unknown name plays in SFX)
        bool Stream = false;         ///< streamed from disk (long ambience)
        int Priority = 128;          ///< the voice limit (AudioSourceComponent::Priority)
        ZoneShape Shape = ZoneShape::Box;
        glm::vec3 Size{ 10.0f, 4.0f, 10.0f }; ///< box, full size in local units
        float Radius = 5.0f;         ///< sphere, local units
        float BlendDistance = 4.0f;  ///< outside the edge: full at the edge, silent this far out
        float FadeTime = 0.5f;       ///< seconds the level takes at most from silent to full (0: at once)

        /// Runtime.
        uint32_t RuntimeHandle = 0;
        float Target = 0.0f;         ///< where the listener is: 1 inside, 0 beyond the blend
        float Level = 0.0f;          ///< what is heard now (Target, smoothed over FadeTime)
        float Distance = 0.0f;       ///< the listener's distance outside the volume (0: inside)
        float RetryIn = 0.0f;        ///< the voice limit refused it: tried again after this

        void ResetRuntime()
        {
            RuntimeHandle = 0;
            Target = Level = Distance = RetryIn = 0.0f;
        }
    };

    // ------------------------------------------------------------------ scripting (C++)
    /// Footstep sounds played by animation events: every event named EventName (an Animator clip's
    /// event, an Animation Player's, a sprite clip's) plays one of Sounds at random - never the
    /// same one twice in a row - louder and higher the faster the character goes (its speed: the
    /// Animator float parameter SpeedParameter, compared with ReferenceSpeed), with a little random
    /// pitch / volume on top so no two steps sound alike. Steps closer than MinInterval play once
    /// (both clips of a crossfade / blend tree fire theirs).
    struct FootstepsComponent
    {
        std::vector<std::string> Sounds;          ///< audio files, relative to Assets
        std::string EventName = "Footstep";
        float Volume = 0.8f;
        glm::vec2 VolumeRandom{ 0.85f, 1.0f };    ///< x a random value in [x, y] per step
        glm::vec2 PitchRandom{ 0.92f, 1.08f };
        /// Speed scaling ("" or a parameter the Animator does not have: none).
        std::string SpeedParameter = "Speed";
        float ReferenceSpeed = 3.0f;              ///< at this speed: Volume and pitch 1 (walking)
        float SpeedPitch = 0.12f;                 ///< pitch x (1 + SpeedPitch x (speed / reference - 1))
        float SpeedVolume = 0.35f;                ///< volume x (1 + SpeedVolume x (speed / reference - 1))
        float MinSpeed = 0.3f;                    ///< slower than this: silent (blending into idle)
        float MinInterval = 0.12f;                ///< seconds
        bool Spatial = true;                      ///< 3D sound at the entity
        float MinDistance = 1.0f;
        float MaxDistance = 25.0f;
        std::string Output = "SFX";               ///< mixer group

        /// runtime (reset when play starts)
        int LastSound = -1;
        double LastStepTime = -1.0e9;
        uint32_t RandomState = 0x9E3779B9u;
        uint32_t StepCount = 0;

        /// What speed `speed` does to a step: its volume and pitch multipliers (before the random
        /// variation). False when it is too slow to be heard.
        bool GetSpeedScale(float speed, float& volume, float& pitch) const
        {
            volume = pitch = 1.0f;
            if (!std::isfinite(speed)) speed = ReferenceSpeed;
            speed = std::abs(speed);
            if (speed < MinSpeed) return false;
            const float r = ReferenceSpeed > 1e-4f ? speed / ReferenceSpeed - 1.0f : 0.0f;
            volume = std::clamp(1.0f + SpeedVolume * r, 0.2f, 1.6f);
            pitch = std::clamp(1.0f + SpeedPitch * r, 0.6f, 1.6f);
            return true;
        }
        /// The next sound to play: a random one of Sounds, not the last one again (when there are
        /// more). -1 without sounds.
        int PickSound()
        {
            const int n = (int)Sounds.size();
            if (n == 0) return -1;
            RandomState ^= RandomState << 13; RandomState ^= RandomState >> 17; RandomState ^= RandomState << 5;
            int index = (int)(RandomState % (uint32_t)n);
            if (n > 1 && index == LastSound)
                index = (index + 1 + (int)((RandomState >> 8) % (uint32_t)(n - 1))) % n;
            LastSound = index;
            return index;
        }
        void ResetRuntime() { LastSound = -1; LastStepTime = -1.0e9; StepCount = 0; }
    };

    /// Puts a C++ script on the entity: ClassName is a class registered with ZM_REGISTER_SCRIPT, Fields
    /// the values its ZM_FIELD members get. Play mode creates the instance (Instance) and runs it.
    struct NativeScriptComponent
    {
        std::string ClassName;  ///< registered with ZM_REGISTER_SCRIPT
        std::unordered_map<std::string, ScriptFieldValue> Fields; ///< inspector-edited values
        /// A disabled script is created (OnCreate) but gets no OnEnable / OnStart / OnUpdate /
        /// OnLateUpdate / timers until it is enabled. At runtime prefer
        /// ScriptableEntity::SetEnabled (callbacks run at once); a direct write is picked up
        /// at the start of the next frame.
        bool Enabled = true;
        ScriptableEntity* Instance = nullptr;

        /// A copy is a new entity's component: it gets its own instance at runtime.
        NativeScriptComponent() = default;
        NativeScriptComponent(const NativeScriptComponent& other)
            : ClassName(other.ClassName), Fields(other.Fields), Enabled(other.Enabled), Instance(nullptr) {}
        /// Assigning onto an existing component changes its data, not the script the entity
        /// is running: a live instance stays (and is destroyed by the scene as usual), it is
        /// never dropped here. Entity::AddOrReplaceComponent destroys it first.
        NativeScriptComponent& operator=(const NativeScriptComponent& other)
        {
            ClassName = other.ClassName;
            Fields = other.Fields;
            Enabled = other.Enabled;
            return *this;
        }
        /// A move is the same component changing places (entt fills the hole left by a removed
        /// component with the last one, sorting swaps): the live instance moves along with it.
        /// entt only move-assigns into elements it has just moved out of; the one other case,
        /// a replace with a fresh value (registry.replace), brings no instance and keeps ours.
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
    /// What the editor remembers about an entity: the hierarchy's eye / lock buttons and the
    /// inspector's component order. Saved with the scene (only when something is set), copied
    /// with the entity (duplicate, copy / paste, play-mode copy; prefab instances and script
    /// Instantiate() clones start without Hidden / Locked); the game ignores it. Hidden and
    /// Locked apply to the whole subtree
    /// (Scene::IsHiddenInEditor / IsLockedInEditor look at the ancestors too):
    /// - Hidden: not drawn in the editor's scene view (edit + simulate) - no mesh, sprite, text,
    ///   light, icon or collider outline - so it cannot be clicked there either. Play mode and
    ///   the player draw it as usual: it is a view filter, not a gameplay state.
    /// - Locked: drawn, but clicks in the viewport go through it and the gizmo does not move it;
    ///   it can still be selected and edited in the hierarchy / inspector.
    struct EditorMetadataComponent
    {
        bool Hidden = false;
        bool Locked = false;
        /// Inspector order of the component headers (scene-file keys, see ComponentKey<T>());
        /// empty = the default order. Transform always stays on top.
        std::vector<std::string> ComponentOrder;
        /// The prefab (relative to Assets) this entity is the root of an instance of: set when the
        /// editor places a prefab; copies of the instance keep it, a prefab saved from it does not.
        /// The scene statistics count the instances by it. Empty = no prefab instance.
        std::string Prefab;

        bool IsDefault() const { return !Hidden && !Locked && ComponentOrder.empty() && Prefab.empty(); }
    };

    /// Editor-only: how an entity of a prefab instance is linked to its prefab file (PrefabUtility,
    /// Scene/Prefab.h). The instance's OWNER is the nearest ancestor-or-self whose
    /// EditorMetadataComponent::Prefab is set (the instance root); every entity of the instance
    /// that came from the prefab carries the ID it has in that .zprefab ("Entity"), so the editor
    /// can tell what the prefab says about it (Apply / Revert / sync after the prefab changed).
    /// Kept in its own component (not in EditorMetadataComponent): the editor's hidden / locked
    /// fast path looks at whether any entity has that one. Saved under the entity's "Editor" key;
    /// the player does not read it (SceneSerializer::SetReadPrefabLinks(false)) and no runtime
    /// system looks at it. An entity of the instance without one (or with PrefabEntity 0) was
    /// added to the instance in the scene.
    struct PrefabLinkComponent
    {
        uint64_t PrefabEntity = 0;                   ///< "Entity" ID in the owner's .zprefab; 0 = not linked
        /// What the instance changed on this entity, as override paths (Scene/PrefabOverrides.h):
        /// "Tag", "Active", "Parent", "ChildOrder", "SpriteRenderer" (added), "SpriteRenderer/Color",
        /// "NativeScript/Fields/Speed" ... sorted, each once. The prefab's later changes reach every
        /// other value.
        std::vector<std::string> Overrides;
        std::vector<std::string> RemovedComponents;  ///< keys of prefab components this entity no longer has
        /// @name The owner root only
        /// The owner root only (the prefab's path itself is EditorMetadataComponent::Prefab)
        /// @{
        UUID PrefabUUID = 0;                         ///< the prefab's .zmeta UUID (the path found again after a move)
        uint64_t PrefabHash = 0;                     ///< PrefabDatabase::ResolvedHash at the last sync (0: unknown)
        std::vector<uint64_t> RemovedEntities;       ///< prefab IDs deleted from this instance (the top-most ones)
        /// @}

        bool IsDefault() const
        {
            return !PrefabEntity && Overrides.empty() && RemovedComponents.empty() && !(uint64_t)PrefabUUID && !PrefabHash &&
                   RemovedEntities.empty();
        }
        bool HasOverrides() const { return !Overrides.empty() || !RemovedComponents.empty() || !RemovedEntities.empty(); }
    };

    template<typename... Component>
    struct ComponentGroup {};

    /// The 2D joints (each derives from Joint2DComponentBase).
    using Joint2DComponents = ComponentGroup<DistanceJoint2DComponent, RevoluteJoint2DComponent, PrismaticJoint2DComponent,
        WeldJoint2DComponent, WheelJoint2DComponent>;

    /// The 3D joints (each derives from Joint3DComponentBase).
    using Joint3DComponents =
        ComponentGroup<FixedJoint3DComponent, HingeJoint3DComponent, DistanceJoint3DComponent, SliderJoint3DComponent, ConeJoint3DComponent>;

    /// Every component that is copied/serialized with an entity (IDComponent & TagComponent are handled separately).
    using AllComponents = ComponentGroup<RelationshipComponent, TagsComponent, TransformComponent, SpriteRendererComponent,
                                         SpriteAnimationComponent, TintAnimationComponent, CircleRendererComponent, TextComponent, MeshRendererComponent, TerrainComponent, FoliageComponent, AnimatorComponent, AnimationPlayerComponent, CameraComponent,
                                         DirectionalLightComponent, PointLightComponent, SpotLightComponent, Light2DComponent, Rigidbody2DComponent, BoxCollider2DComponent,
                                         CircleCollider2DComponent, PolygonCollider2DComponent, EdgeCollider2DComponent, PlatformEffector2DComponent, AreaEffector2DComponent, BuoyancyEffector2DComponent, DistanceJoint2DComponent, RevoluteJoint2DComponent, PrismaticJoint2DComponent, WeldJoint2DComponent,
                                         WheelJoint2DComponent, Rigidbody3DComponent, BoxCollider3DComponent, SphereCollider3DComponent,
                                         CapsuleCollider3DComponent, ConvexHullCollider3DComponent, MeshCollider3DComponent, FixedJoint3DComponent, HingeJoint3DComponent, DistanceJoint3DComponent,
                                         SliderJoint3DComponent, ConeJoint3DComponent, CharacterController3DComponent, TilemapComponent, ParallaxComponent, Camera2DFollowComponent, SplineComponent, SplineFollowerComponent, NavGrid2DComponent, NavGrid3DComponent, NavAgentComponent, NavObstacleComponent, SightSensorComponent, PatrolPathComponent, FormationComponent, ParticleSystemComponent, UITransformComponent, UICanvasComponent,
                                         UILayoutGroupComponent, UILayoutElementComponent, UINavigationComponent, UIAnimationComponent, UIButtonComponent,
                                         UIImageComponent, UISliderComponent, UIToggleComponent, UIProgressBarComponent, UIInputFieldComponent, UIMaskComponent,
                                         UIScrollViewComponent, AudioSourceComponent, AudioListenerComponent, AmbientZoneComponent, FootstepsComponent,
                                         BoneAttachmentComponent, SubSceneComponent, NativeScriptComponent, EditorMetadataComponent, PrefabLinkComponent>;
}
