#pragma once

#include "Zamin/Core/Base.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Zamin {

    class Entity;

    // ------------------------------------------------------------------ keys and curves
    // How the value goes from a key to the next one.
    enum class KeyInterpolation : uint8_t { Step = 0, Linear, Bezier };
    // Where a Bezier key's tangents (slopes) come from: Auto - smooth through the neighbours, without
    // overshooting at peaks and level at the first / last key; Flat - level (eases in and out);
    // Free - set by hand (the curve view's handles).
    enum class KeyTangentMode : uint8_t { Auto = 0, Flat, Free };

    // What happens at the end of a clip: Once stops on its last value, Loop starts over, PingPong
    // plays it backwards and forwards again.
    enum class AnimationWrap : uint8_t { Once = 0, Loop, PingPong };

    // What a track animates on its target entity.
    enum class AnimatedProperty : uint8_t
    {
        Position = 0, Rotation, Scale,           // Transform (local; rotation in degrees)
        SpriteColor, CircleColor, TextColor, MeshColor,
        LightColor, LightIntensity,              // the first light of the entity: point, spot, directional or 2D
        UIPosition, UISize, UIScale, UIAlpha, UIImageColor,
        ScriptFloat,                             // a float field of the entity's script (AnimationTrack::Field)
        Count
    };

    struct AnimationKey
    {
        float Time = 0.0f;                    // seconds
        glm::vec4 Value{ 0.0f };              // the property's components (x, y, z, w; unused ones are 0)
        KeyInterpolation Interpolation = KeyInterpolation::Linear; // from this key to the next
        KeyTangentMode TangentMode = KeyTangentMode::Auto;
        // Bezier: slopes (value per second) arriving at / leaving the key. The curve between keys a
        // and b is the cubic Bezier through a.Value, a.Value + a.OutTangent * dt / 3,
        // b.Value - b.InTangent * dt / 3 and b.Value (dt = b.Time - a.Time).
        glm::vec4 InTangent{ 0.0f };
        glm::vec4 OutTangent{ 0.0f };

        bool operator==(const AnimationKey&) const = default;
    };

    // The keys of one property of one entity.
    struct AnimationTrack
    {
        // The target relative to the player's entity: names of children separated by '/'
        // ("Door/Hinge": its child "Door", that one's child "Hinge"); "" = the entity itself.
        std::string Path;
        AnimatedProperty Property = AnimatedProperty::Position;
        std::string Field;                    // ScriptFloat: the script field
        std::vector<AnimationKey> Keys;       // by time

        int GetComponentCount() const;
        glm::vec4 Evaluate(float time) const;
        float EvaluateComponent(int component, float time) const;
        // Keys back in time order, then the automatic tangents (UpdateTangents).
        void Sort();
        // Computes the tangents of the Auto and Flat keys (Free ones are left as they are).
        void UpdateTangents();
        // The key at `time` (within `tolerance` seconds), -1 when there is none.
        int FindKey(float time, float tolerance = 1e-4f) const;
        // Sets the value of the key at `time` (made when missing; a new key takes the interpolation
        // of the key before it). Returns its index; the tangents are updated.
        int SetKey(float time, const glm::vec4& value, float tolerance = 1e-4f);
        bool SameTarget(const std::string& path, AnimatedProperty property, const std::string& field) const
        {
            return Path == path && Property == property && (property != AnimatedProperty::ScriptFloat || Field == field);
        }
    };

    // A moment of a clip that tells the entity's script (OnAnimationEvent) and the event bus.
    struct AnimationEvent
    {
        float Time = 0.0f;                    // seconds (Animator events: 0..1 of the clip)
        std::string Name;
        std::string Parameter;                // optional text handed over with it
    };

    // A property animation clip: the content of a .zanim file (the Animation window edits it).
    struct PropertyClip
    {
        static constexpr int CurrentVersion = 1;
        static constexpr const char* Extension = ".zanim";

        float Duration = 1.0f;                // seconds
        float FrameRate = 30.0f;              // the timeline's frames (snapping), not the playback rate
        AnimationWrap Wrap = AnimationWrap::Loop;
        std::vector<AnimationTrack> Tracks;
        std::vector<AnimationEvent> Events;   // by time

        AnimationTrack* FindTrack(const std::string& path, AnimatedProperty property, const std::string& field = {});
        const AnimationTrack* FindTrack(const std::string& path, AnimatedProperty property, const std::string& field = {}) const;
        // The track of that property (added when missing).
        AnimationTrack& GetOrAddTrack(const std::string& path, AnimatedProperty property, const std::string& field = {});
        void SortEvents();
        // The time of the last key (0 without keys).
        float GetLastKeyTime() const;
        bool operator==(const PropertyClip& other) const;
    };

    // Names of the properties, interpolations and wrap modes in files and in the editor.
    class PropertyAnimation
    {
    public:
        static const char* PropertyToKey(AnimatedProperty property);     // "Transform.Position" ...
        static std::optional<AnimatedProperty> PropertyFromKey(std::string_view key);
        static const char* GetPropertyName(AnimatedProperty property);   // shown in the editor (Uzbek)
        static int GetComponentCount(AnimatedProperty property);
        // Whether the components are a colour (r, g, b[, a]).
        static bool IsColor(AnimatedProperty property);
        static const char* InterpolationToString(KeyInterpolation interpolation);
        static std::optional<KeyInterpolation> InterpolationFromString(std::string_view text);
        static const char* TangentModeToString(KeyTangentMode mode);
        static std::optional<KeyTangentMode> TangentModeFromString(std::string_view text);
        static const char* WrapToString(AnimationWrap wrap);
        static std::optional<AnimationWrap> WrapFromString(std::string_view text);

        // The value between two keys `t` seconds after `a` (b follows a).
        static glm::vec4 Interpolate(const AnimationKey& a, const AnimationKey& b, float t);
        // The value of keys sorted by time at `time` (held before the first / after the last; 0
        // without keys). Every curve uses it: tracks, particle curves.
        static glm::vec4 Evaluate(const std::vector<AnimationKey>& keys, float time);
        // The tangents of the Auto and Flat keys of keys sorted by time (Free ones stay).
        static void UpdateTangents(std::vector<AnimationKey>& keys);
    };

    class PropertyClipSerializer
    {
    public:
        static std::string SerializeToString(const PropertyClip& clip);
        static bool DeserializeFromString(const std::string& text, PropertyClip& out, std::string* error = nullptr);
        static bool Serialize(const PropertyClip& clip, const std::filesystem::path& path);
        static bool Deserialize(const std::filesystem::path& path, PropertyClip& out, std::string* error = nullptr);
    };

    // A .zanim file's clip, shared by every player that uses it (PropertyClipLibrary); edits in the
    // Animation window change this object, so everything playing it sees them at once.
    class PropertyClipAsset
    {
    public:
        PropertyClipAsset() = default;
        PropertyClipAsset(PropertyClip clip, std::filesystem::path path) : m_Clip(std::move(clip)), m_Path(std::move(path)) {}

        const PropertyClip& GetClip() const { return m_Clip; }
        void SetClip(PropertyClip clip);
        // The clip's name: its file name without the extension.
        std::string GetName() const { return m_Path.stem().string(); }
        const std::filesystem::path& GetPath() const { return m_Path; }
        void SetPath(const std::filesystem::path& path) { m_Path = path; }
        // Changes with every change of the clip (SetClip, Reload).
        uint64_t GetRevision() const { return m_Revision; }
        bool Reload(std::string* error = nullptr);
        bool Save() const;

    private:
        PropertyClip m_Clip;
        std::filesystem::path m_Path;
        uint64_t m_Revision = 1;
    };

    // Caches clips by file.
    class PropertyClipLibrary
    {
    public:
        // The clip of a .zanim file (absolute, or relative to the asset folder); nullptr when it
        // cannot be read (reported once, AssetImportLog).
        static Ref<PropertyClipAsset> Load(const std::filesystem::path& path);
        static Ref<PropertyClipAsset> Find(const std::filesystem::path& path);
        static bool IsFileLoaded(const std::filesystem::path& path);
        // The file changed on disk: its cached clip is read again in place. Returns 1 when the
        // clip changed.
        static size_t ReloadFile(const std::filesystem::path& path, bool* failed = nullptr);
        static void OnFileMoved(const std::filesystem::path& from, const std::filesystem::path& to);
        static void Clear();
    };

    // ------------------------------------------------------------------ playback
    // Where a clip is being played.
    struct ClipCursor
    {
        float Time = 0.0f;       // 0..duration
        bool Forward = true;     // the direction it moves in (ping-pong's way back, a negative speed)
        bool Finished = false;   // Once: reached its end
        bool AtStart = true;     // nothing played since it started: events right at Time fire too
    };

    // A stretch of a clip that was played (To < From: backwards). Events in it fire.
    struct ClipSpan
    {
        float From = 0.0f, To = 0.0f;
        bool IncludeFrom = false;
        bool Contains(float time) const
        {
            if (To >= From)
                return (IncludeFrom ? time >= From : time > From) && time <= To;
            return (IncludeFrom ? time <= From : time < From) && time >= To;
        }
    };

    // Moves the cursor `delta` seconds (negative: against its direction) through a clip
    // `duration` long; the stretches played go to `spans` (when given), in order. A step of a whole
    // cycle or more (a hitch) plays every moment once: one cycle's spans, then the rest silently.
    void AdvanceClip(ClipCursor& cursor, float delta, float duration, AnimationWrap wrap, std::vector<ClipSpan>* spans);

    // The events (index: their time via `timeOf`) the spans pass, in the order they are passed.
    void CollectEvents(const std::vector<ClipSpan>& spans, size_t count, const std::function<float(size_t)>& timeOf,
                       std::vector<size_t>& out);

    // ------------------------------------------------------------------ entity properties
    class AnimationTargets
    {
    public:
        // The entity a track path names below `root` ("" = root itself); invalid when missing.
        static Entity Resolve(Entity root, std::string_view path);
        // The path of `target` relative to `root` ("" for root itself); nullopt when target is not
        // in root's subtree.
        static std::optional<std::string> MakePath(Entity root, Entity target);
        // Whether the entity has what the property animates.
        static bool Has(Entity entity, AnimatedProperty property, const std::string& field = {});
        // The property's current value (false when the entity does not have it).
        static bool Read(Entity entity, AnimatedProperty property, const std::string& field, glm::vec4& out);
        static bool Write(Entity entity, AnimatedProperty property, const std::string& field, const glm::vec4& value);
        // The properties the entity can have animated (scripts: its float fields), for the editor.
        struct Candidate
        {
            AnimatedProperty Property;
            std::string Field;
        };
        static std::vector<Candidate> GetCandidates(Entity entity);
    };

    // Applies every track of `clip` at `time` to the entities below `root`. Returns how many
    // tracks found their target.
    int ApplyPropertyClip(Entity root, const PropertyClip& clip, float time);

}
