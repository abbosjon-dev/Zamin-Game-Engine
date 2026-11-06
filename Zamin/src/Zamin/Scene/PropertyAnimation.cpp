#include "Zamin/Scene/PropertyAnimation.h"

#include "Zamin/Asset/AssetImportLog.h"
#include "Zamin/Core/Log.h"
#include "Zamin/Project/Project.h"
#include "Zamin/Scene/Components.h"
#include "Zamin/Scene/Entity.h"
#include "Zamin/Scene/Scene.h"
#include "Zamin/Scripting/ScriptEngine.h"
#include "Zamin/Scripting/ScriptableEntity.h"
#include "Zamin/Utils/FileSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <unordered_map>

namespace Zamin {

    namespace fs = std::filesystem;
    using json = nlohmann::json;

    // ------------------------------------------------------------------ names
    struct PropertyInfo
    {
        AnimatedProperty Property;
        const char* Key;
        const char* Name;
        int Components;
        bool Color;
    };

    static constexpr std::array<PropertyInfo, (size_t)AnimatedProperty::Count> s_Properties = { {
        { AnimatedProperty::Position, "Transform.Position", "Joylashuv", 3, false },
        { AnimatedProperty::Rotation, "Transform.Rotation", "Burilish (gradus)", 3, false },
        { AnimatedProperty::Scale, "Transform.Scale", "Masshtab", 3, false },
        { AnimatedProperty::SpriteColor, "SpriteRenderer.Color", "Sprite rangi", 4, true },
        { AnimatedProperty::CircleColor, "CircleRenderer.Color", "Doira rangi", 4, true },
        { AnimatedProperty::TextColor, "Text.Color", "Matn rangi", 4, true },
        { AnimatedProperty::MeshColor, "MeshRenderer.Color", "Mesh rangi", 4, true },
        { AnimatedProperty::LightColor, "Light.Color", "Yorug'lik rangi", 3, true },
        { AnimatedProperty::LightIntensity, "Light.Intensity", "Yorug'lik kuchi", 1, false },
        { AnimatedProperty::UIPosition, "UITransform.Position", "UI joylashuvi", 2, false },
        { AnimatedProperty::UISize, "UITransform.Size", "UI o'lchami", 2, false },
        { AnimatedProperty::UIScale, "UITransform.Scale", "UI masshtabi", 2, false },
        { AnimatedProperty::UIAlpha, "UITransform.Alpha", "UI shaffofligi (alpha)", 1, false },
        { AnimatedProperty::UIImageColor, "UIImage.Color", "UI rasm rangi", 4, true },
        { AnimatedProperty::ScriptFloat, "Script.Float", "Skript maydoni", 1, false },
    } };

    static const PropertyInfo& InfoOf(AnimatedProperty property)
    {
        return s_Properties[std::min((size_t)property, s_Properties.size() - 1)];
    }

    const char* PropertyAnimation::PropertyToKey(AnimatedProperty property) { return InfoOf(property).Key; }
    const char* PropertyAnimation::GetPropertyName(AnimatedProperty property) { return InfoOf(property).Name; }
    int PropertyAnimation::GetComponentCount(AnimatedProperty property) { return InfoOf(property).Components; }
    bool PropertyAnimation::IsColor(AnimatedProperty property) { return InfoOf(property).Color; }

    std::optional<AnimatedProperty> PropertyAnimation::PropertyFromKey(std::string_view key)
    {
        for (const PropertyInfo& info : s_Properties)
            if (key == info.Key)
                return info.Property;
        return std::nullopt;
    }

    const char* PropertyAnimation::InterpolationToString(KeyInterpolation interpolation)
    {
        switch (interpolation)
        {
            case KeyInterpolation::Step:   return "Step";
            case KeyInterpolation::Linear: return "Linear";
            case KeyInterpolation::Bezier: return "Bezier";
        }
        return "Linear";
    }

    std::optional<KeyInterpolation> PropertyAnimation::InterpolationFromString(std::string_view text)
    {
        if (text == "Step") return KeyInterpolation::Step;
        if (text == "Linear") return KeyInterpolation::Linear;
        if (text == "Bezier") return KeyInterpolation::Bezier;
        return std::nullopt;
    }

    const char* PropertyAnimation::TangentModeToString(KeyTangentMode mode)
    {
        switch (mode)
        {
            case KeyTangentMode::Auto: return "Auto";
            case KeyTangentMode::Flat: return "Flat";
            case KeyTangentMode::Free: return "Free";
        }
        return "Auto";
    }

    std::optional<KeyTangentMode> PropertyAnimation::TangentModeFromString(std::string_view text)
    {
        if (text == "Auto") return KeyTangentMode::Auto;
        if (text == "Flat") return KeyTangentMode::Flat;
        if (text == "Free") return KeyTangentMode::Free;
        return std::nullopt;
    }

    const char* PropertyAnimation::WrapToString(AnimationWrap wrap)
    {
        switch (wrap)
        {
            case AnimationWrap::Once:     return "Once";
            case AnimationWrap::Loop:     return "Loop";
            case AnimationWrap::PingPong: return "PingPong";
        }
        return "Loop";
    }

    std::optional<AnimationWrap> PropertyAnimation::WrapFromString(std::string_view text)
    {
        if (text == "Once") return AnimationWrap::Once;
        if (text == "Loop") return AnimationWrap::Loop;
        if (text == "PingPong") return AnimationWrap::PingPong;
        return std::nullopt;
    }

    // ------------------------------------------------------------------ curves
    glm::vec4 PropertyAnimation::Interpolate(const AnimationKey& a, const AnimationKey& b, float t)
    {
        const float dt = b.Time - a.Time;
        if (dt <= 0.0f)
            return b.Value;
        if (a.Interpolation == KeyInterpolation::Step)
            return a.Value;
        const float s = std::clamp(t / dt, 0.0f, 1.0f);
        if (a.Interpolation == KeyInterpolation::Linear)
            return glm::mix(a.Value, b.Value, s);
        // Cubic Hermite with slopes = the Bezier with handles a third of the way in.
        const float s2 = s * s, s3 = s2 * s;
        const float h00 = 2.0f * s3 - 3.0f * s2 + 1.0f;
        const float h10 = s3 - 2.0f * s2 + s;
        const float h01 = -2.0f * s3 + 3.0f * s2;
        const float h11 = s3 - s2;
        return h00 * a.Value + h10 * dt * a.OutTangent + h01 * b.Value + h11 * dt * b.InTangent;
    }

    int AnimationTrack::GetComponentCount() const { return PropertyAnimation::GetComponentCount(Property); }

    glm::vec4 AnimationTrack::Evaluate(float time) const
    {
        if (Keys.empty())
            return glm::vec4(0.0f);
        if (Keys.size() == 1 || time <= Keys.front().Time)
            return Keys.front().Value;
        if (time >= Keys.back().Time)
            return Keys.back().Value;
        auto it = std::upper_bound(Keys.begin(), Keys.end(), time, [](float t, const AnimationKey& k) { return t < k.Time; });
        const AnimationKey& b = *it;
        const AnimationKey& a = *(it - 1);
        return PropertyAnimation::Interpolate(a, b, time - a.Time);
    }

    float AnimationTrack::EvaluateComponent(int component, float time) const
    {
        return Evaluate(time)[std::clamp(component, 0, 3)];
    }

    void AnimationTrack::Sort()
    {
        std::stable_sort(Keys.begin(), Keys.end(), [](const AnimationKey& a, const AnimationKey& b) { return a.Time < b.Time; });
        UpdateTangents();
    }

    void AnimationTrack::UpdateTangents()
    {
        const size_t n = Keys.size();
        for (size_t i = 0; i < n; i++)
        {
            AnimationKey& k = Keys[i];
            if (k.TangentMode == KeyTangentMode::Free)
                continue;
            if (k.TangentMode == KeyTangentMode::Flat || i == 0 || i + 1 == n)
            {
                k.InTangent = k.OutTangent = glm::vec4(0.0f);
                continue;
            }
            const AnimationKey& p = Keys[i - 1];
            const AnimationKey& q = Keys[i + 1];
            const float dl = k.Time - p.Time, dr = q.Time - k.Time;
            glm::vec4 slope(0.0f);
            if (dl > 0.0f && dr > 0.0f)
            {
                for (int c = 0; c < 4; c++)
                {
                    const float left = (k.Value[c] - p.Value[c]) / dl;
                    const float right = (q.Value[c] - k.Value[c]) / dr;
                    if (left * right <= 0.0f)
                        continue; // a peak or a plateau: level, so the curve does not overshoot
                    float s = (q.Value[c] - p.Value[c]) / (dl + dr);
                    const float limit = 3.0f * std::min(std::abs(left), std::abs(right));
                    slope[c] = std::clamp(s, -limit, limit);
                }
            }
            k.InTangent = k.OutTangent = slope;
        }
    }

    int AnimationTrack::FindKey(float time, float tolerance) const
    {
        for (size_t i = 0; i < Keys.size(); i++)
            if (std::abs(Keys[i].Time - time) <= tolerance)
                return (int)i;
        return -1;
    }

    int AnimationTrack::SetKey(float time, const glm::vec4& value, float tolerance)
    {
        int index = FindKey(time, tolerance);
        if (index >= 0)
        {
            Keys[index].Value = value;
            UpdateTangents();
            return index;
        }
        auto it = std::upper_bound(Keys.begin(), Keys.end(), time, [](float t, const AnimationKey& k) { return t < k.Time; });
        AnimationKey key;
        key.Time = time;
        key.Value = value;
        if (it != Keys.begin())
            key.Interpolation = (it - 1)->Interpolation;
        else if (it != Keys.end())
            key.Interpolation = it->Interpolation;
        index = (int)(it - Keys.begin());
        Keys.insert(it, key);
        UpdateTangents();
        return index;
    }

    AnimationTrack* PropertyClip::FindTrack(const std::string& path, AnimatedProperty property, const std::string& field)
    {
        for (AnimationTrack& t : Tracks)
            if (t.SameTarget(path, property, field))
                return &t;
        return nullptr;
    }

    const AnimationTrack* PropertyClip::FindTrack(const std::string& path, AnimatedProperty property, const std::string& field) const
    {
        for (const AnimationTrack& t : Tracks)
            if (t.SameTarget(path, property, field))
                return &t;
        return nullptr;
    }

    AnimationTrack& PropertyClip::GetOrAddTrack(const std::string& path, AnimatedProperty property, const std::string& field)
    {
        if (AnimationTrack* t = FindTrack(path, property, field))
            return *t;
        AnimationTrack track;
        track.Path = path;
        track.Property = property;
        if (property == AnimatedProperty::ScriptFloat)
            track.Field = field;
        Tracks.push_back(std::move(track));
        return Tracks.back();
    }

    void PropertyClip::SortEvents()
    {
        std::stable_sort(Events.begin(), Events.end(), [](const AnimationEvent& a, const AnimationEvent& b) { return a.Time < b.Time; });
    }

    float PropertyClip::GetLastKeyTime() const
    {
        float last = 0.0f;
        for (const AnimationTrack& t : Tracks)
            if (!t.Keys.empty())
                last = std::max(last, t.Keys.back().Time);
        return last;
    }

    bool PropertyClip::operator==(const PropertyClip& other) const
    {
        return PropertyClipSerializer::SerializeToString(*this) == PropertyClipSerializer::SerializeToString(other);
    }

    // ------------------------------------------------------------------ files
    static json ValueToJson(const glm::vec4& v, int count)
    {
        json a = json::array();
        for (int i = 0; i < count; i++)
            a.push_back(std::isfinite(v[i]) ? v[i] : 0.0f);
        return a;
    }

    static glm::vec4 ValueFromJson(const json& j, int count)
    {
        glm::vec4 v(0.0f);
        if (j.is_number())
            v.x = j.get<float>();
        else if (j.is_array())
            for (int i = 0; i < count && i < (int)j.size(); i++)
                if (j[i].is_number())
                    v[i] = j[i].get<float>();
        for (int i = 0; i < 4; i++)
            if (!std::isfinite(v[i]))
                v[i] = 0.0f;
        return v;
    }

    std::string PropertyClipSerializer::SerializeToString(const PropertyClip& clip)
    {
        json tracks = json::array();
        for (const AnimationTrack& t : clip.Tracks)
        {
            const int count = t.GetComponentCount();
            json keys = json::array();
            for (const AnimationKey& k : t.Keys)
            {
                json key = { { "Time", k.Time }, { "Value", ValueToJson(k.Value, count) },
                             { "Interpolation", PropertyAnimation::InterpolationToString(k.Interpolation) } };
                if (k.TangentMode != KeyTangentMode::Auto)
                    key["Tangent"] = PropertyAnimation::TangentModeToString(k.TangentMode);
                if (k.TangentMode == KeyTangentMode::Free)
                {
                    key["In"] = ValueToJson(k.InTangent, count);
                    key["Out"] = ValueToJson(k.OutTangent, count);
                }
                keys.push_back(std::move(key));
            }
            json track = { { "Path", t.Path }, { "Property", PropertyAnimation::PropertyToKey(t.Property) }, { "Keys", std::move(keys) } };
            if (t.Property == AnimatedProperty::ScriptFloat)
                track["Field"] = t.Field;
            tracks.push_back(std::move(track));
        }
        json events = json::array();
        for (const AnimationEvent& e : clip.Events)
        {
            json ev = { { "Time", e.Time }, { "Name", e.Name } };
            if (!e.Parameter.empty())
                ev["Parameter"] = e.Parameter;
            events.push_back(std::move(ev));
        }
        json j = { { "Version", PropertyClip::CurrentVersion }, { "Duration", clip.Duration }, { "FrameRate", clip.FrameRate },
                   { "Wrap", PropertyAnimation::WrapToString(clip.Wrap) }, { "Tracks", std::move(tracks) }, { "Events", std::move(events) } };
        return j.dump(2);
    }

    bool PropertyClipSerializer::DeserializeFromString(const std::string& text, PropertyClip& out, std::string* error)
    {
        json j = json::parse(text, nullptr, false);
        if (j.is_discarded() || !j.is_object())
        {
            if (error) *error = "JSON o'qilmadi";
            return false;
        }
        if (j.value("Version", 1) > PropertyClip::CurrentVersion)
        {
            if (error) *error = std::format("yangiroq versiya ({})", j.value("Version", 1));
            return false;
        }
        PropertyClip clip;
        auto number = [&](const char* key, float fallback) {
            auto it = j.find(key);
            return it != j.end() && it->is_number() && std::isfinite(it->get<float>()) ? it->get<float>() : fallback;
        };
        clip.Duration = std::max(0.0f, number("Duration", 1.0f));
        clip.FrameRate = std::clamp(number("FrameRate", 30.0f), 1.0f, 240.0f);
        if (auto it = j.find("Wrap"); it != j.end() && it->is_string())
            clip.Wrap = PropertyAnimation::WrapFromString(it->get<std::string>()).value_or(AnimationWrap::Loop);
        if (auto tracks = j.find("Tracks"); tracks != j.end() && tracks->is_array())
            for (const json& t : *tracks)
            {
                if (!t.is_object())
                    continue;
                auto property = PropertyAnimation::PropertyFromKey(t.value("Property", std::string()));
                if (!property)
                {
                    ZM_CORE_WARN("Animatsiya klipi: noma'lum xususiyat '{}' o'tkazib yuborildi", t.value("Property", std::string()));
                    continue;
                }
                AnimationTrack track;
                track.Path = t.value("Path", std::string());
                track.Property = *property;
                if (*property == AnimatedProperty::ScriptFloat)
                    track.Field = t.value("Field", std::string());
                const int count = track.GetComponentCount();
                if (auto keys = t.find("Keys"); keys != t.end() && keys->is_array())
                    for (const json& k : *keys)
                    {
                        if (!k.is_object())
                            continue;
                        AnimationKey key;
                        auto time = k.find("Time");
                        key.Time = time != k.end() && time->is_number() && std::isfinite(time->get<float>()) ? std::max(0.0f, time->get<float>()) : 0.0f;
                        if (auto v = k.find("Value"); v != k.end())
                            key.Value = ValueFromJson(*v, count);
                        if (auto it = k.find("Interpolation"); it != k.end() && it->is_string())
                            key.Interpolation = PropertyAnimation::InterpolationFromString(it->get<std::string>()).value_or(KeyInterpolation::Linear);
                        if (auto it = k.find("Tangent"); it != k.end() && it->is_string())
                            key.TangentMode = PropertyAnimation::TangentModeFromString(it->get<std::string>()).value_or(KeyTangentMode::Auto);
                        if (key.TangentMode == KeyTangentMode::Free)
                        {
                            if (auto it = k.find("In"); it != k.end()) key.InTangent = ValueFromJson(*it, count);
                            if (auto it = k.find("Out"); it != k.end()) key.OutTangent = ValueFromJson(*it, count);
                        }
                        track.Keys.push_back(key);
                    }
                track.Sort();
                clip.Tracks.push_back(std::move(track));
            }
        if (auto events = j.find("Events"); events != j.end() && events->is_array())
            for (const json& e : *events)
            {
                if (!e.is_object())
                    continue;
                AnimationEvent ev;
                auto time = e.find("Time");
                ev.Time = time != e.end() && time->is_number() && std::isfinite(time->get<float>()) ? std::max(0.0f, time->get<float>()) : 0.0f;
                ev.Name = e.value("Name", std::string());
                ev.Parameter = e.value("Parameter", std::string());
                clip.Events.push_back(std::move(ev));
            }
        clip.SortEvents();
        out = std::move(clip);
        return true;
    }

    bool PropertyClipSerializer::Serialize(const PropertyClip& clip, const fs::path& path)
    {
        std::error_code ec;
        if (path.has_parent_path())
            fs::create_directories(path.parent_path(), ec);
        return FileSystem::WriteTextFile(path, SerializeToString(clip) + "\n");
    }

    bool PropertyClipSerializer::Deserialize(const fs::path& path, PropertyClip& out, std::string* error)
    {
        std::error_code ec;
        if (!fs::exists(path, ec))
        {
            if (error) *error = "fayl topilmadi";
            return false;
        }
        return DeserializeFromString(FileSystem::ReadTextFile(path), out, error);
    }

    // ------------------------------------------------------------------ assets
    static uint64_t s_ClipRevision = 1;

    void PropertyClipAsset::SetClip(PropertyClip clip)
    {
        m_Clip = std::move(clip);
        m_Revision = ++s_ClipRevision;
    }

    bool PropertyClipAsset::Reload(std::string* error)
    {
        PropertyClip clip;
        if (!PropertyClipSerializer::Deserialize(m_Path, clip, error))
            return false;
        if (!(clip == m_Clip))
            SetClip(std::move(clip));
        return true;
    }

    bool PropertyClipAsset::Save() const
    {
        return !m_Path.empty() && PropertyClipSerializer::Serialize(m_Clip, m_Path);
    }

    static std::unordered_map<std::string, Ref<PropertyClipAsset>> s_ClipCache;

    static std::string ClipKey(const fs::path& path)
    {
        fs::path full = Project::GetAssetFileSystemPath(path);
        std::error_code ec;
        fs::path canonical = fs::weakly_canonical(full, ec);
        return (ec ? fs::absolute(full, ec).lexically_normal() : canonical).generic_string();
    }

    Ref<PropertyClipAsset> PropertyClipLibrary::Load(const fs::path& path)
    {
        if (path.empty())
            return nullptr;
        std::string key = ClipKey(path);
        if (auto it = s_ClipCache.find(key); it != s_ClipCache.end())
            return it->second;
        if (!AssetImportLog::ShouldRetry(key))
            return nullptr;
        PropertyClip clip;
        std::string error;
        if (!PropertyClipSerializer::Deserialize(key, clip, &error))
        {
            AssetImportLog::Report(key, error);
            return nullptr;
        }
        AssetImportLog::Clear(key);
        Ref<PropertyClipAsset> asset = CreateRef<PropertyClipAsset>(std::move(clip), fs::path(key));
        s_ClipCache[key] = asset;
        return asset;
    }

    Ref<PropertyClipAsset> PropertyClipLibrary::Find(const fs::path& path)
    {
        auto it = s_ClipCache.find(ClipKey(path));
        return it != s_ClipCache.end() ? it->second : nullptr;
    }

    bool PropertyClipLibrary::IsFileLoaded(const fs::path& path) { return Find(path) != nullptr; }

    size_t PropertyClipLibrary::ReloadFile(const fs::path& path, bool* failed)
    {
        auto it = s_ClipCache.find(ClipKey(path));
        if (it == s_ClipCache.end())
            return 0;
        std::string error;
        uint64_t revision = it->second->GetRevision();
        if (!it->second->Reload(&error))
        {
            AssetImportLog::Report(it->first, error, true);
            if (failed)
                *failed = true;
            return 0;
        }
        AssetImportLog::Clear(it->first);
        return it->second->GetRevision() != revision ? 1 : 0;
    }

    void PropertyClipLibrary::OnFileMoved(const fs::path& from, const fs::path& to)
    {
        std::string fromKey = ClipKey(from), toKey = ClipKey(to);
        if (fromKey == toKey)
            return;
        std::string prefix = fromKey + "/";
        std::vector<std::pair<std::string, Ref<PropertyClipAsset>>> moved;
        for (auto it = s_ClipCache.begin(); it != s_ClipCache.end();)
        {
            if (it->first == fromKey || it->first.starts_with(prefix))
            {
                moved.emplace_back(toKey + it->first.substr(fromKey.size()), it->second);
                it = s_ClipCache.erase(it);
            }
            else
                ++it;
        }
        for (auto& [key, asset] : moved)
        {
            asset->SetPath(fs::path(key));
            s_ClipCache[key] = asset;
        }
    }

    void PropertyClipLibrary::Clear() { s_ClipCache.clear(); }

    // ------------------------------------------------------------------ playback
    // Moves `amount` (>= 0) seconds in the cursor's direction (reversed: against it); returns
    // whether the place it stopped at counts as not played yet (a loop jumped back to its start).
    static bool WalkClip(ClipCursor& c, float amount, bool reversed, float duration, AnimationWrap wrap, bool include,
                         std::vector<ClipSpan>* spans)
    {
        for (int guard = 0; amount > 0.0f && guard < 64 && !c.Finished; guard++)
        {
            const bool forward = c.Forward != reversed;
            const float room = forward ? duration - c.Time : c.Time;
            if (amount < room)
            {
                const float to = forward ? c.Time + amount : c.Time - amount;
                if (spans) spans->push_back({ c.Time, to, include });
                c.Time = to;
                return false;
            }
            const float edge = forward ? duration : 0.0f;
            if (spans) spans->push_back({ c.Time, edge, include });
            amount -= room;
            c.Time = edge;
            switch (wrap)
            {
                case AnimationWrap::Once:
                    c.Finished = true;
                    return false;
                case AnimationWrap::Loop:
                    c.Time = forward ? 0.0f : duration; // jumps: the moment it lands on plays next
                    include = true;
                    break;
                case AnimationWrap::PingPong:
                    c.Forward = !c.Forward;            // turns: the edge has been played
                    include = false;
                    break;
            }
        }
        return include;
    }

    void AdvanceClip(ClipCursor& cursor, float delta, float duration, AnimationWrap wrap, std::vector<ClipSpan>* spans)
    {
        if (delta == 0.0f || !std::isfinite(delta) || cursor.Finished)
            return;
        if (duration <= 0.0f)
        {
            // A single moment: it plays once (Once), or every step (looping).
            if (spans && (cursor.AtStart || wrap != AnimationWrap::Once))
                spans->push_back({ 0.0f, 0.0f, true });
            cursor.Time = 0.0f;
            cursor.AtStart = false;
            cursor.Finished = wrap == AnimationWrap::Once;
            return;
        }
        cursor.Time = std::clamp(cursor.Time, 0.0f, duration);
        const bool reversed = delta < 0.0f;
        float amount = std::abs(delta);
        bool include = cursor.AtStart;
        const float cycle = wrap == AnimationWrap::PingPong ? 2.0f * duration : duration;
        if (wrap != AnimationWrap::Once && amount >= cycle)
        {
            // A hitch longer than the clip: every moment plays once, the rest moves silently.
            const float rest = std::fmod(amount, cycle);
            include = WalkClip(cursor, cycle, reversed, duration, wrap, include, spans);
            if (rest > 0.0f)
                include = WalkClip(cursor, rest, reversed, duration, wrap, include, nullptr);
        }
        else
            include = WalkClip(cursor, amount, reversed, duration, wrap, include, spans);
        cursor.AtStart = include;
    }

    void CollectEvents(const std::vector<ClipSpan>& spans, size_t count, const std::function<float(size_t)>& timeOf, std::vector<size_t>& out)
    {
        if (count == 0)
            return;
        std::vector<size_t> hit;
        for (const ClipSpan& span : spans)
        {
            hit.clear();
            for (size_t i = 0; i < count; i++)
                if (span.Contains(timeOf(i)))
                    hit.push_back(i);
            const bool forward = span.To >= span.From;
            std::stable_sort(hit.begin(), hit.end(), [&](size_t a, size_t b) {
                return forward ? timeOf(a) < timeOf(b) : timeOf(a) > timeOf(b);
            });
            out.insert(out.end(), hit.begin(), hit.end());
        }
    }

    // ------------------------------------------------------------------ entity properties
    Entity AnimationTargets::Resolve(Entity root, std::string_view path)
    {
        if (!root || path.empty())
            return root;
        Scene* scene = root.GetScene();
        Entity current = root;
        size_t start = 0;
        while (start <= path.size())
        {
            size_t end = path.find('/', start);
            if (end == std::string_view::npos)
                end = path.size();
            std::string_view name = path.substr(start, end - start);
            if (!name.empty())
            {
                Entity next;
                for (UUID id : current.GetComponent<RelationshipComponent>().Children)
                    if (Entity c = scene->GetEntityByUUID(id); c && c.GetName() == name)
                    {
                        next = c;
                        break;
                    }
                if (!next)
                    return {};
                current = next;
            }
            start = end + 1;
        }
        return current;
    }

    std::optional<std::string> AnimationTargets::MakePath(Entity root, Entity target)
    {
        if (!root || !target)
            return std::nullopt;
        Scene* scene = root.GetScene();
        std::vector<std::string> names;
        Entity e = target;
        for (int guard = 0; e && e != root && guard < 256; guard++)
        {
            names.push_back(e.GetName());
            e = scene->GetParent(e);
        }
        if (e != root)
            return std::nullopt;
        std::string path;
        for (auto it = names.rbegin(); it != names.rend(); ++it)
        {
            if (!path.empty())
                path += '/';
            path += *it;
        }
        return path;
    }

    static const ScriptFieldInfo* FindFloatField(const NativeScriptComponent& nsc, const std::string& field)
    {
        const ScriptClassInfo* cls = ScriptRegistry::Find(nsc.ClassName);
        if (!cls)
            return nullptr;
        for (const ScriptFieldInfo& f : cls->Fields)
            if (f.Name == field && f.Type == ScriptFieldType::Float)
                return &f;
        return nullptr;
    }

    // The light the Light properties animate: point, spot, directional or 2D (the first one there).
    template<typename F>
    static bool WithLight(Entity e, F&& f)
    {
        if (auto* l = e.TryGetComponent<PointLightComponent>()) { f(l->Color, l->Intensity); return true; }
        if (auto* l = e.TryGetComponent<SpotLightComponent>()) { f(l->Color, l->Intensity); return true; }
        if (auto* l = e.TryGetComponent<DirectionalLightComponent>()) { f(l->Color, l->Intensity); return true; }
        if (auto* l = e.TryGetComponent<Light2DComponent>()) { f(l->Color, l->Intensity); return true; }
        return false;
    }

    bool AnimationTargets::Has(Entity e, AnimatedProperty property, const std::string& field)
    {
        if (!e)
            return false;
        switch (property)
        {
            case AnimatedProperty::Position:
            case AnimatedProperty::Rotation:
            case AnimatedProperty::Scale:          return e.HasComponent<TransformComponent>();
            case AnimatedProperty::SpriteColor:    return e.HasComponent<SpriteRendererComponent>();
            case AnimatedProperty::CircleColor:    return e.HasComponent<CircleRendererComponent>();
            case AnimatedProperty::TextColor:      return e.HasComponent<TextComponent>();
            case AnimatedProperty::MeshColor:      return e.HasComponent<MeshRendererComponent>();
            case AnimatedProperty::LightColor:
            case AnimatedProperty::LightIntensity: return WithLight(e, [](glm::vec3&, float&) {});
            case AnimatedProperty::UIPosition:
            case AnimatedProperty::UISize:
            case AnimatedProperty::UIScale:
            case AnimatedProperty::UIAlpha:        return e.HasComponent<UITransformComponent>();
            case AnimatedProperty::UIImageColor:   return e.HasComponent<UIImageComponent>();
            case AnimatedProperty::ScriptFloat:
            {
                const auto* nsc = e.TryGetComponent<NativeScriptComponent>();
                if (!nsc || field.empty())
                    return false;
                if (FindFloatField(*nsc, field))
                    return true;
                auto it = nsc->Fields.find(field);
                return it != nsc->Fields.end() && it->second.Type == ScriptFieldType::Float;
            }
            case AnimatedProperty::Count: break;
        }
        return false;
    }

    bool AnimationTargets::Read(Entity e, AnimatedProperty property, const std::string& field, glm::vec4& out)
    {
        if (!e)
            return false;
        out = glm::vec4(0.0f);
        switch (property)
        {
            case AnimatedProperty::Position: { const auto& t = e.GetComponent<TransformComponent>(); out = glm::vec4(t.Translation, 0.0f); return true; }
            case AnimatedProperty::Rotation: { const auto& t = e.GetComponent<TransformComponent>(); out = glm::vec4(glm::degrees(t.Rotation), 0.0f); return true; }
            case AnimatedProperty::Scale:    { const auto& t = e.GetComponent<TransformComponent>(); out = glm::vec4(t.Scale, 0.0f); return true; }
            case AnimatedProperty::SpriteColor:
                if (auto* c = e.TryGetComponent<SpriteRendererComponent>()) { out = c->Color; return true; }
                return false;
            case AnimatedProperty::CircleColor:
                if (auto* c = e.TryGetComponent<CircleRendererComponent>()) { out = c->Color; return true; }
                return false;
            case AnimatedProperty::TextColor:
                if (auto* c = e.TryGetComponent<TextComponent>()) { out = c->Color; return true; }
                return false;
            case AnimatedProperty::MeshColor:
                if (auto* c = e.TryGetComponent<MeshRendererComponent>())
                {
                    if (c->MaterialInstance) out = c->MaterialInstance->GetColor();
                    else if (c->MaterialRef) out = c->MaterialRef->GetColor();
                    else out = c->Mat.Albedo;
                    return true;
                }
                return false;
            case AnimatedProperty::LightColor:
                return WithLight(e, [&](glm::vec3& color, float&) { out = glm::vec4(color, 0.0f); });
            case AnimatedProperty::LightIntensity:
                return WithLight(e, [&](glm::vec3&, float& intensity) { out.x = intensity; });
            case AnimatedProperty::UIPosition:
                if (auto* c = e.TryGetComponent<UITransformComponent>()) { out = glm::vec4(c->Position, 0.0f, 0.0f); return true; }
                return false;
            case AnimatedProperty::UISize:
                if (auto* c = e.TryGetComponent<UITransformComponent>()) { out = glm::vec4(c->Size, 0.0f, 0.0f); return true; }
                return false;
            case AnimatedProperty::UIScale:
                if (auto* c = e.TryGetComponent<UITransformComponent>()) { out = glm::vec4(c->Scale, 0.0f, 0.0f); return true; }
                return false;
            case AnimatedProperty::UIAlpha:
                if (auto* c = e.TryGetComponent<UITransformComponent>()) { out.x = c->Alpha; return true; }
                return false;
            case AnimatedProperty::UIImageColor:
                if (auto* c = e.TryGetComponent<UIImageComponent>()) { out = c->Color; return true; }
                return false;
            case AnimatedProperty::ScriptFloat:
            {
                auto* nsc = e.TryGetComponent<NativeScriptComponent>();
                if (!nsc || field.empty())
                    return false;
                const ScriptFieldInfo* info = FindFloatField(*nsc, field);
                if (nsc->Instance && info)
                {
                    ScriptFieldValue v;
                    info->Load(nsc->Instance, v);
                    out.x = v.Get<float>();
                    return true;
                }
                if (auto it = nsc->Fields.find(field); it != nsc->Fields.end() && it->second.Type == ScriptFieldType::Float)
                {
                    out.x = it->second.Get<float>();
                    return true;
                }
                if (!info)
                    return false;
                // Not set in the inspector: the class's own default.
                if (ScriptableEntity* temp = ScriptRegistry::Instantiate(nsc->ClassName))
                {
                    ScriptFieldValue v;
                    info->Load(temp, v);
                    out.x = v.Get<float>();
                    delete temp;
                }
                return true;
            }
            case AnimatedProperty::Count: break;
        }
        return false;
    }

    bool AnimationTargets::Write(Entity e, AnimatedProperty property, const std::string& field, const glm::vec4& v)
    {
        if (!e)
            return false;
        switch (property)
        {
            case AnimatedProperty::Position: e.GetComponent<TransformComponent>().Translation = glm::vec3(v); return true;
            case AnimatedProperty::Rotation: e.GetComponent<TransformComponent>().Rotation = glm::radians(glm::vec3(v)); return true;
            case AnimatedProperty::Scale:    e.GetComponent<TransformComponent>().Scale = glm::vec3(v); return true;
            case AnimatedProperty::SpriteColor:
                if (auto* c = e.TryGetComponent<SpriteRendererComponent>()) { c->Color = v; return true; }
                return false;
            case AnimatedProperty::CircleColor:
                if (auto* c = e.TryGetComponent<CircleRendererComponent>()) { c->Color = v; return true; }
                return false;
            case AnimatedProperty::TextColor:
                if (auto* c = e.TryGetComponent<TextComponent>()) { c->Color = v; return true; }
                return false;
            case AnimatedProperty::MeshColor:
                if (auto* c = e.TryGetComponent<MeshRendererComponent>())
                {
                    // A material asset is shared: the colour goes to this renderer's own copy.
                    if (c->MaterialInstance || c->MaterialRef)
                    {
                        if (Ref<MaterialAsset> instance = Scene::GetMaterialInstance(*c); instance && instance->GetColor() != v)
                            instance->SetColor(v);
                    }
                    else
                        c->Mat.Albedo = v;
                    return true;
                }
                return false;
            case AnimatedProperty::LightColor:
                return WithLight(e, [&](glm::vec3& color, float&) { color = glm::max(glm::vec3(v), glm::vec3(0.0f)); });
            case AnimatedProperty::LightIntensity:
                return WithLight(e, [&](glm::vec3&, float& intensity) { intensity = std::max(0.0f, v.x); });
            case AnimatedProperty::UIPosition:
                if (auto* c = e.TryGetComponent<UITransformComponent>()) { c->Position = glm::vec2(v); return true; }
                return false;
            case AnimatedProperty::UISize:
                if (auto* c = e.TryGetComponent<UITransformComponent>()) { c->Size = glm::vec2(v); return true; }
                return false;
            case AnimatedProperty::UIScale:
                if (auto* c = e.TryGetComponent<UITransformComponent>()) { c->Scale = glm::vec2(v); return true; }
                return false;
            case AnimatedProperty::UIAlpha:
                if (auto* c = e.TryGetComponent<UITransformComponent>()) { c->Alpha = std::clamp(v.x, 0.0f, 1.0f); return true; }
                return false;
            case AnimatedProperty::UIImageColor:
                if (auto* c = e.TryGetComponent<UIImageComponent>()) { c->Color = v; return true; }
                return false;
            case AnimatedProperty::ScriptFloat:
            {
                auto* nsc = e.TryGetComponent<NativeScriptComponent>();
                if (!nsc || field.empty())
                    return false;
                ScriptFieldValue value;
                value.Set<float>(v.x);
                if (nsc->Instance)
                {
                    const ScriptFieldInfo* info = FindFloatField(*nsc, field);
                    return info && info->Store(nsc->Instance, value);
                }
                nsc->Fields[field] = value; // edit mode: the value the script starts with
                return true;
            }
            case AnimatedProperty::Count: break;
        }
        return false;
    }

    std::vector<AnimationTargets::Candidate> AnimationTargets::GetCandidates(Entity e)
    {
        std::vector<Candidate> out;
        if (!e)
            return out;
        for (int p = 0; p < (int)AnimatedProperty::ScriptFloat; p++)
            if (Has(e, (AnimatedProperty)p))
                out.push_back({ (AnimatedProperty)p, {} });
        if (const auto* nsc = e.TryGetComponent<NativeScriptComponent>())
            if (const ScriptClassInfo* cls = ScriptRegistry::Find(nsc->ClassName))
                for (const ScriptFieldInfo& f : cls->Fields)
                    if (f.Type == ScriptFieldType::Float)
                        out.push_back({ AnimatedProperty::ScriptFloat, f.Name });
        return out;
    }

    int ApplyPropertyClip(Entity root, const PropertyClip& clip, float time)
    {
        int applied = 0;
        for (const AnimationTrack& track : clip.Tracks)
        {
            if (track.Keys.empty())
                continue;
            Entity target = AnimationTargets::Resolve(root, track.Path);
            if (target && AnimationTargets::Write(target, track.Property, track.Field, track.Evaluate(time)))
                applied++;
        }
        return applied;
    }


    // ------------------------------------------------------------------ AnimationPlayerComponent
    void AnimationPlayerComponent::ResolveClips()
    {
        ClipAssets.resize(Clips.size());
        LoadedClips.resize(Clips.size());
        for (size_t i = 0; i < Clips.size(); i++)
        {
            // Looked up already (loaded, or failed until the path changes): kept.
            if (LoadedClips[i] == Clips[i])
                continue;
            LoadedClips[i] = Clips[i];
            ClipAssets[i] = Clips[i].empty() ? nullptr : PropertyClipLibrary::Load(Clips[i]);
        }
        if (Current >= (int)Clips.size())
        {
            Current = -1;
            Playing = false;
        }
    }

    int AnimationPlayerComponent::FindClip(std::string_view nameOrPath) const
    {
        for (size_t i = 0; i < Clips.size(); i++)
        {
            if (Clips[i] == nameOrPath)
                return (int)i;
            if (fs::path(Clips[i]).stem().string() == nameOrPath)
                return (int)i;
        }
        return -1;
    }

    bool AnimationPlayerComponent::Play(std::string_view clip, float speed)
    {
        int index = clip.empty() ? (Current >= 0 ? Current : 0) : FindClip(clip);
        return Play(index, speed);
    }

    bool AnimationPlayerComponent::Play(int index, float speed)
    {
        ResolveClips();
        if (index < 0 || index >= (int)Clips.size() || !ClipAssets[index])
            return false;
        Current = index;
        PlaySpeed = std::isfinite(speed) ? speed : 1.0f;
        Cursor = {};
        // Backwards: from the end (the cursor moves forwards, the speed turns it round).
        if (PlaySpeed * Speed < 0.0f)
            Cursor.Time = GetDuration();
        Playing = true;
        Started = true;
        PoseDirty = true;
        return true;
    }

    void AnimationPlayerComponent::SetTime(float seconds)
    {
        const float duration = GetDuration();
        Cursor.Time = std::clamp(std::isfinite(seconds) ? seconds : 0.0f, 0.0f, duration);
        Cursor.Finished = false;
        Cursor.AtStart = true;
        PoseDirty = true;
    }

    const PropertyClip* AnimationPlayerComponent::GetCurrentClip() const
    {
        if (Current < 0 || Current >= (int)ClipAssets.size() || !ClipAssets[Current])
            return nullptr;
        return &ClipAssets[Current]->GetClip();
    }

    float AnimationPlayerComponent::GetDuration() const
    {
        const PropertyClip* clip = GetCurrentClip();
        return clip ? clip->Duration : 0.0f;
    }

    std::string AnimationPlayerComponent::GetCurrentClipName() const
    {
        return Current >= 0 && Current < (int)Clips.size() ? fs::path(Clips[Current]).stem().string() : std::string();
    }

    AnimationWrap AnimationPlayerComponent::GetWrap() const
    {
        switch (Wrap)
        {
            case AnimationPlayerWrap::Once:     return AnimationWrap::Once;
            case AnimationPlayerWrap::Loop:     return AnimationWrap::Loop;
            case AnimationPlayerWrap::PingPong: return AnimationWrap::PingPong;
            case AnimationPlayerWrap::Clip:     break;
        }
        const PropertyClip* clip = GetCurrentClip();
        return clip ? clip->Wrap : AnimationWrap::Loop;
    }

}
