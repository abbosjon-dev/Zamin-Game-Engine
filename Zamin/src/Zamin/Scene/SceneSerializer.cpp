#include "Zamin/Scene/SceneSerializer.h"
#include "Zamin/Asset/AssetImportLog.h"
#include "Zamin/Asset/AssetRegistry.h"
#include "Zamin/Physics/CollisionShapes3D.h"
#include "Zamin/Project/AssetFiles.h"
#include "Zamin/Project/Project.h"
#include "Zamin/Renderer/MaterialAsset.h"
#include "Zamin/Renderer/Renderer.h"
#include "Zamin/Scene/ComponentRegistry.h"
#include "Zamin/Scene/Entity.h"
#include "Zamin/Scene/ParticlePresets.h"
#include "Zamin/Scene/SceneMigrationJson.h"
#include "Zamin/Utils/FileSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace Zamin {

    using json = nlohmann::json;

    static json ToJson(const glm::vec2& v) { return json::array({ v.x, v.y }); }
    static json ToJson(const glm::vec3& v) { return json::array({ v.x, v.y, v.z }); }
    static json ToJson(const glm::vec4& v) { return json::array({ v.x, v.y, v.z, v.w }); }

    template<typename V>
    static V Vec(const json& j, const char* key, const V& fallback)
    {
        if (!j.contains(key) || !j[key].is_array() || j[key].size() < (size_t)V::length())
            return fallback;
        V v;
        for (int i = 0; i < V::length(); i++)
            v[i] = j[key][i].get<float>();
        return v;
    }

    // ------------------------------------------------------------------ asset references
    // Components refer to assets by path ("Texture": "Textures/coin.png"). Next to each path the
    // asset's UUID is written when the registry knows the file ("TextureUUID": "3f2a..."), so a
    // reference survives the file being renamed or moved outside the editor: when the path is
    // gone on load but the UUID belongs to an asset elsewhere, that asset's path is used.
    // One scope per (de)serialization: the registry of the active project, the repairs made.
    class AssetReferenceScope
    {
    public:
        explicit AssetReferenceScope(std::vector<SceneSerializer::AssetReferenceRepair>* repairs = nullptr)
            : m_Previous(s_Current), m_Repairs(repairs)
        {
            m_Registry = Project::GetActive() ? Project::GetAssetRegistry() : nullptr;
            s_Current = this;
        }
        ~AssetReferenceScope() { s_Current = m_Previous; }
        AssetReferenceScope(const AssetReferenceScope&) = delete;
        AssetReferenceScope& operator=(const AssetReferenceScope&) = delete;

        static AssetReferenceScope* Get() { return s_Current; }
        AssetRegistry* GetRegistry() const { return m_Registry; }
        void AddRepair(const std::string& from, const std::string& to)
        {
            if (m_Repairs)
                m_Repairs->push_back({ from, to });
        }

        // Scene loads: missing asset files are reported here, with the entity being read.
        void SetReport(SceneLoadReport* report) { m_Report = report; }
        SceneLoadReport* GetReport() const { return m_Report; }
        void SetEntity(const json* source, std::string name, uint64_t id)
        {
            m_EntitySource = source;
            m_EntityName = std::move(name);
            m_EntityID = id;
        }
        void ReportMissingAsset(const json& object, std::string_view key, const std::string& path);

        // What the registry said about a path during this (de)serialization: a large scene names the
        // same few assets thousands of times (one registry / file system lookup per path).
        struct PathInfo
        {
            bool Known = false;   // a registered asset (or the file is there)
            std::string Handle;   // its UUID text when it has a .zmeta ("" otherwise)
        };
        const PathInfo& LookUp(const std::string& path)
        {
            auto it = m_Paths.find(path);
            if (it != m_Paths.end())
                return it->second;
            PathInfo info;
            if (m_Registry)
            {
                const AssetMetadata* meta = m_Registry->Find(std::filesystem::path(path));
                if (meta && meta->HasMetaFile)
                    info.Handle = meta->Handle.ToString();
                std::error_code ec;
                info.Known = meta || std::filesystem::exists(m_Registry->ToFileSystemPath(path), ec);
            }
            return m_Paths.emplace(path, std::move(info)).first->second;
        }

    private:
        static inline thread_local AssetReferenceScope* s_Current = nullptr;
        std::unordered_map<std::string, PathInfo> m_Paths;
        std::unordered_set<std::string> m_ReportedMissing; // "<entity ID>/<component>/<path>"
        AssetReferenceScope* m_Previous = nullptr;
        AssetRegistry* m_Registry = nullptr;
        std::vector<SceneSerializer::AssetReferenceRepair>* m_Repairs = nullptr;
        SceneLoadReport* m_Report = nullptr;
        const json* m_EntitySource = nullptr; // the entity being read ("Settings" without one)
        std::string m_EntityName;
        uint64_t m_EntityID = 0;
    };

    static bool ContainsNode(const json& j, const json* node)
    {
        if (&j == node)
            return true;
        if (j.is_object() || j.is_array())
            for (const json& child : j)
                if (ContainsNode(child, node))
                    return true;
        return false;
    }

    void AssetReferenceScope::ReportMissingAsset(const json& object, std::string_view key, const std::string& path)
    {
        if (!m_Report)
            return;
        // The component the reference belongs to: the entity's member that holds `object`.
        std::string component = m_EntitySource ? std::string() : std::string("Settings");
        if (m_EntitySource && m_EntitySource->is_object())
            for (auto it = m_EntitySource->begin(); it != m_EntitySource->end() && component.empty(); ++it)
                if (ContainsNode(it.value(), &object))
                    component = it.key();
        if (!m_ReportedMissing.insert(std::format("{}/{}/{}", m_EntityID, component, path)).second)
            return; // once per entity and component
        SceneLoadIssue issue;
        issue.Type = SceneLoadIssue::Kind::MissingAsset;
        issue.Level = SceneLoadIssue::Severity::Warning;
        issue.Entity = m_EntityName;
        issue.EntityID = m_EntityID;
        issue.Component = component;
        issue.Asset = path;
        issue.Message = m_EntitySource
            ? std::format("'{}' obyekti ({}.{}): asset topilmadi '{}'", m_EntityName, component, key, path)
            : std::format("Sahna sozlamalari ({}): asset topilmadi '{}'", key, path);
        m_Report->Add(std::move(issue));
    }

    static std::string UUIDKey(std::string_view key) { return std::string(key) + "UUID"; }

    // Adds "<key>UUID" for the asset path stored under `key` of `object` (when known).
    static void WriteAssetHandle(json& object, std::string_view key, const std::string& path)
    {
        AssetReferenceScope* scope = AssetReferenceScope::Get();
        if (path.empty() || !scope || !scope->GetRegistry())
            return;
        // Only a UUID stored in a .zmeta file means something in the next session.
        if (const auto& info = scope->LookUp(path); !info.Handle.empty())
            object[UUIDKey(key)] = info.Handle;
    }

    // The asset path stored under `key`, or where its UUID is now when that file is gone.
    // A scene load (with a report) notes asset files that are not there.
    static std::string ReadAssetPath(const json& object, std::string_view key)
    {
        auto pathIt = object.find(key);
        std::string path = pathIt != object.end() && pathIt->is_string() ? pathIt->get<std::string>() : std::string();
        if (path.empty())
            return path;
        AssetReferenceScope* scope = AssetReferenceScope::Get();
        AssetRegistry* registry = scope ? scope->GetRegistry() : nullptr;
        if (!registry || scope->LookUp(path).Known)
            return path; // no project (paths mean nothing here), or the file is still there (registered or not)
        auto handleIt = object.find(UUIDKey(key));
        std::optional<AssetHandle> handle = handleIt != object.end() && handleIt->is_string()
            ? AssetHandle::FromString(handleIt->get<std::string>()) : std::nullopt;
        std::string moved = handle ? registry->GetPath(*handle) : std::string();
        if (moved.empty() || moved == path)
        {
            // Gone for good: the path stays (the editor shows what is missing).
            scope->ReportMissingAsset(object, key, path);
            return path;
        }
        ZM_CORE_INFO("Asset havolasi UUID orqali tiklandi: '{}' -> '{}'", path, moved);
        scope->AddRepair(path, moved);
        return moved;
    }

    // ------------------------------------------------------------------ 2D sorting
    // The sorting layer of a 2D renderer: its ID and, for people reading the file and for a scene
    // opened in another project, its name. Nothing is written for the Default layer.
    static void WriteSortingLayer(json& j, uint32_t id)
    {
        if (id == SortingLayerList::DefaultID)
            return;
        j["SortingLayer"] = id;
        if (const SortingLayer* layer = Project::GetSortingLayers().Find(id))
            j["SortingLayerName"] = layer->Name;
    }

    static uint32_t ReadSortingLayer(const json& j)
    {
        uint32_t id = SortingLayerList::DefaultID;
        if (auto it = j.find("SortingLayer"); it != j.end() && it->is_number_unsigned())
            id = it->get<uint32_t>();
        std::string name;
        if (auto it = j.find("SortingLayerName"); it != j.end() && it->is_string())
            name = it->get<std::string>();
        if (id == SortingLayerList::DefaultID && name.empty())
            return id;
        return Project::GetSortingLayers().Resolve(id, name);
    }

    static const char* SpriteDrawModeToString(SpriteDrawMode mode)
    {
        switch (mode)
        {
            case SpriteDrawMode::Simple: return "Simple";
            case SpriteDrawMode::Sliced: return "Sliced";
            case SpriteDrawMode::Tiled:  return "Tiled";
        }
        return "Simple";
    }

    static SpriteDrawMode SpriteDrawModeFromString(const std::string& s)
    {
        if (s == "Sliced") return SpriteDrawMode::Sliced;
        if (s == "Tiled")  return SpriteDrawMode::Tiled;
        return SpriteDrawMode::Simple;
    }

    // ------------------------------------------------------------------ UI widget enums (by name)
    static const char* const s_UIImageTypes[] = { "Simple", "Sliced", "Tiled", "Filled" };
    static const char* const s_UIFillMethods[] = { "Horizontal", "Vertical", "Radial360" };
    static const char* const s_UIDirections[] = { "LeftToRight", "RightToLeft", "BottomToTop", "TopToBottom" };
    static const char* const s_UIToggleStyles[] = { "Checkbox", "Switch" };
    static const char* const s_UIToggleMarks[] = { "Check", "Square", "Dot" };
    static const char* const s_SplineFollowModes[] = { "Once", "Loop", "PingPong" };
    static const char* const s_SplineRotations[] = { "None", "Path3D", "Path2D" };
    static const char* const s_UIContentTypes[] = { "Standard", "Integer", "Decimal", "Alphanumeric", "Password" };
    static const char* const s_UIScaleModes[] = { "ConstantPixelSize", "ScaleWithScreenSize" };
    static const char* const s_UIScreenMatchModes[] = { "MatchWidthOrHeight", "Expand", "Shrink" };
    static const char* const s_UILayoutTypes[] = { "Horizontal", "Vertical", "Grid" };
    static const char* const s_UIGridConstraints[] = { "Flexible", "FixedColumnCount", "FixedRowCount" };
    static const char* const s_UINavigationModes[] = { "Automatic", "Horizontal", "Vertical", "Explicit", "None" };
    static const char* const s_TextAlignH[] = { "Left", "Center", "Right" };
    static const char* const s_TextAlignV[] = { "Top", "Middle", "Bottom", "Baseline" };
    static const char* const s_TextOverflows[] = { "Overflow", "Truncate", "Ellipsis" };
    static const char* const s_UIAnimPresets[] = { "None", "Fade", "Pop", "SlideLeft", "SlideRight", "SlideUp", "SlideDown" };
    static const char* const s_UIScrollMovements[] = { "Unrestricted", "Elastic", "Clamped" };
    static const char* const s_UIScrollbarVisibilities[] = { "Permanent", "AutoHide", "Hidden" };

    static const char* const s_ConeEmitFrom[] = { "Base", "Volume" };
    static const char* const s_SheetSources[] = { "Grid", "Sprites" };
    static const char* const s_SheetTimeModes[] = { "Lifetime", "FrameRate" };
    static const char* const s_MeshShapeSources[] = { "Primitive", "Model", "Renderer" };
    static const char* const s_MeshEmitModes[] = { "Triangle", "Edge", "Vertex" };
    static const char* const s_CollisionTypes[] = { "Plane", "World3D", "World2D" };
    static const char* const s_SubEmitterTriggers[] = { "Birth", "Death", "Collision", "Lifetime" };
    static const char* const s_ParticleSpaces[] = { "Local", "World", "Custom" };
    static const char* const s_ParticleStopActions[] = { "None", "Disable", "Destroy" };
    static const char* const s_ParticleSortModes[] = { "Auto", "None", "ByDistance", "OldestOnTop", "YoungestOnTop" };

    template<typename E, size_t N>
    static const char* EnumName(E value, const char* const (&names)[N])
    {
        const int i = (int)value;
        return i >= 0 && i < (int)N ? names[i] : names[0];
    }

    // The enum stored under `key` by name (or by number); `fallback` when missing / unknown.
    template<typename E, size_t N>
    static E EnumFromJson(const json& j, const char* key, const char* const (&names)[N], E fallback)
    {
        auto it = j.find(key);
        if (it == j.end())
            return fallback;
        if (it->is_string())
        {
            const std::string name = it->get<std::string>();
            for (size_t i = 0; i < N; i++)
                if (name == names[i])
                    return (E)i;
            return fallback;
        }
        if (it->is_number_integer())
        {
            const int i = it->get<int>();
            return i >= 0 && i < (int)N ? (E)i : fallback;
        }
        return fallback;
    }

    // ------------------------------------------------------------------ particle curves / gradients
    static float Number(const json& j, const char* key, float fallback)
    {
        auto it = j.find(key);
        return it != j.end() && it->is_number() && std::isfinite(it->get<float>()) ? it->get<float>() : fallback;
    }

    // [{ "Time", "Value", "Interpolation"[, "Tangent"[, "In", "Out"]] }] - the keys of the animation clips, one value.
    static json CurveKeysToJson(const std::vector<AnimationKey>& keys)
    {
        json out = json::array();
        for (const AnimationKey& k : keys)
        {
            json key = { { "Time", k.Time }, { "Value", k.Value.x }, { "Interpolation", PropertyAnimation::InterpolationToString(k.Interpolation) } };
            if (k.TangentMode != KeyTangentMode::Auto)
                key["Tangent"] = PropertyAnimation::TangentModeToString(k.TangentMode);
            if (k.TangentMode == KeyTangentMode::Free)
            {
                key["In"] = k.InTangent.x;
                key["Out"] = k.OutTangent.x;
            }
            out.push_back(std::move(key));
        }
        return out;
    }

    // The keys of an array (sorted, tangents updated); times clamped into [minTime, maxTime].
    static std::vector<AnimationKey> CurveKeysFromJson(const json& array, float minTime, float maxTime, float defaultValue)
    {
        std::vector<AnimationKey> keys;
        for (const json& k : array)
        {
            if (!k.is_object())
                continue;
            AnimationKey key;
            key.Time = std::clamp(Number(k, "Time", 0.0f), minTime, maxTime);
            key.Value.x = Number(k, "Value", defaultValue);
            if (auto i = k.find("Interpolation"); i != k.end() && i->is_string())
                key.Interpolation = PropertyAnimation::InterpolationFromString(i->get<std::string>()).value_or(KeyInterpolation::Linear);
            if (auto i = k.find("Tangent"); i != k.end() && i->is_string())
                key.TangentMode = PropertyAnimation::TangentModeFromString(i->get<std::string>()).value_or(KeyTangentMode::Auto);
            if (key.TangentMode == KeyTangentMode::Free)
            {
                key.InTangent.x = Number(k, "In", 0.0f);
                key.OutTangent.x = Number(k, "Out", 0.0f);
            }
            keys.push_back(key);
        }
        CurveKeys::Sort(keys);
        return keys;
    }

    static json ParticleCurveToJson(const ParticleCurve& curve)
    {
        return CurveKeysToJson(curve.Keys);
    }

    static ParticleCurve ParticleCurveFromJson(const json& j, const char* name, const ParticleCurve& fallback)
    {
        auto it = j.find(name);
        if (it == j.end() || !it->is_array())
            return fallback;
        ParticleCurve curve;
        curve.Keys = CurveKeysFromJson(*it, 0.0f, 1.0f, 1.0f);
        return curve;
    }

    // { "Keys": [...], "PreWrap", "PostWrap" } (the wraps only when not Once).
    static json AnimationCurveToJson(const AnimationCurve& curve)
    {
        json j = { { "Keys", CurveKeysToJson(curve.Keys) } };
        if (curve.PreWrap != AnimationWrap::Once)
            j["PreWrap"] = AnimationCurve::WrapToString(curve.PreWrap);
        if (curve.PostWrap != AnimationWrap::Once)
            j["PostWrap"] = AnimationCurve::WrapToString(curve.PostWrap);
        return j;
    }

    static AnimationCurve AnimationCurveFromJson(const json& j)
    {
        AnimationCurve curve;
        if (!j.is_object())
            return curve;
        if (auto it = j.find("Keys"); it != j.end() && it->is_array())
            curve.Keys = CurveKeysFromJson(*it, -1e9f, 1e9f, 0.0f);
        auto wrap = [&](const char* key) {
            auto it = j.find(key);
            return it != j.end() && it->is_string() ? AnimationCurve::WrapFromString(it->get<std::string>()).value_or(AnimationWrap::Once)
                                                    : AnimationWrap::Once;
        };
        curve.PreWrap = wrap("PreWrap");
        curve.PostWrap = wrap("PostWrap");
        return curve;
    }

    // ------------------------------------------------------------------ splines
    // { "Type", "Closed", "Alpha", "Color", "Points": [[x, y, z], ...][, "Handles": [[in xyz, out xyz], ...]] }
    // (the Bezier handles when the path is a Bezier or they were changed from the default).
    static json SplineToJson(const SplineComponent& c)
    {
        const Spline& path = c.Path;
        json points = json::array();
        bool customHandles = path.Type == SplineType::Bezier;
        const SplinePoint defaults;
        for (const SplinePoint& p : path.Points)
        {
            points.push_back(ToJson(p.Position));
            customHandles |= p.InTangent != defaults.InTangent || p.OutTangent != defaults.OutTangent;
        }
        json j = { { "Type", SplineTypeToString(path.Type) }, { "Closed", path.Closed }, { "Alpha", path.Alpha },
                   { "Color", ToJson(c.Color) }, { "Points", std::move(points) } };
        if (customHandles)
        {
            json handles = json::array();
            for (const SplinePoint& p : path.Points)
                handles.push_back(json::array({ p.InTangent.x, p.InTangent.y, p.InTangent.z, p.OutTangent.x, p.OutTangent.y, p.OutTangent.z }));
            j["Handles"] = std::move(handles);
        }
        return j;
    }

    // An array of finite numbers (at least `count`) into out[0..count).
    static bool Numbers(const json& j, float* out, int count)
    {
        if (!j.is_array() || j.size() < (size_t)count)
            return false;
        for (int i = 0; i < count; i++)
        {
            if (!j[(size_t)i].is_number())
                return false;
            const float v = j[(size_t)i].get<float>();
            if (!std::isfinite(v))
                return false;
            out[i] = v;
        }
        return true;
    }

    static void SplineFromJson(const json& j, SplineComponent& c)
    {
        Spline& path = c.Path;
        if (auto it = j.find("Type"); it != j.end() && it->is_string())
            path.Type = SplineTypeFromString(it->get<std::string>()).value_or(SplineType::CatmullRom);
        path.Closed = j.value("Closed", false);
        path.Alpha = std::clamp(Number(j, "Alpha", 0.5f), 0.0f, 1.0f);
        if (auto it = j.find("Color"); it != j.end())
            Numbers(*it, &c.Color.x, 4);
        if (auto it = j.find("Points"); it != j.end() && it->is_array())
        {
            path.Points.clear();
            for (const json& p : *it)
            {
                SplinePoint point;
                if (Numbers(p, &point.Position.x, 3))
                    path.Points.push_back(point);
            }
            if (auto h = j.find("Handles"); h != j.end() && h->is_array())
                for (size_t i = 0; i < std::min(h->size(), path.Points.size()); i++)
                {
                    float v[6];
                    if (Numbers((*h)[i], v, 6))
                    {
                        path.Points[i].InTangent = { v[0], v[1], v[2] };
                        path.Points[i].OutTangent = { v[3], v[4], v[5] };
                    }
                }
        }
    }

    // { "Fixed", "Stops": [[time, r, g, b, a], ...] }
    static json ParticleGradientToJson(const ParticleGradient& gradient)
    {
        json stops = json::array();
        for (const auto& stop : gradient.Stops)
            stops.push_back(json::array({ stop.Time, stop.Color.r, stop.Color.g, stop.Color.b, stop.Color.a }));
        json j = { { "Stops", std::move(stops) } };
        if (gradient.Fixed)
            j["Fixed"] = true;
        return j;
    }

    static ParticleGradient ParticleGradientFromJson(const json& j, const char* name, const ParticleGradient& fallback)
    {
        auto it = j.find(name);
        if (it == j.end() || !it->is_object() || !it->contains("Stops") || !(*it)["Stops"].is_array())
            return fallback;
        ParticleGradient gradient;
        gradient.Fixed = it->value("Fixed", false);
        for (const json& stop : (*it)["Stops"])
        {
            if (!stop.is_array() || stop.size() < 5)
                continue;
            float v[5];
            bool valid = true;
            for (int i = 0; i < 5; i++)
            {
                valid &= stop[i].is_number() && std::isfinite(stop[i].get<float>());
                v[i] = valid ? stop[i].get<float>() : 0.0f;
            }
            if (valid)
                gradient.Stops.push_back({ std::clamp(v[0], 0.0f, 1.0f), glm::vec4(v[1], v[2], v[3], v[4]) });
        }
        if (gradient.Stops.empty())
            return fallback;
        gradient.Sort();
        return gradient;
    }

    // The main settings newer than the first particle systems, when they are not at their defaults
    // (older scenes stay as they were written).
    static void WriteParticleMain(json& j, const ParticleSystemComponent& c)
    {
        using PS = ParticleSystemComponent;
        const PS d;
        if (c.Duration != d.Duration) j["Duration"] = c.Duration;
        if (c.StartDelay != d.StartDelay) j["StartDelay"] = c.StartDelay;
        if (c.Prewarm) j["Prewarm"] = true;
        if (c.SimulationSpeed != d.SimulationSpeed) j["SimulationSpeed"] = c.SimulationSpeed;
        if (c.StopActionType != d.StopActionType) j["StopAction"] = EnumName(c.StopActionType, s_ParticleStopActions);
        if (c.Sorting != d.Sorting) j["Sorting"] = EnumName(c.Sorting, s_ParticleSortModes);
        if (c.Space == PS::SimulationSpace::Custom)
        {
            j["Space"] = EnumName(c.Space, s_ParticleSpaces);
            j["CustomSpace"] = c.CustomSpace.ID;
        }
        if (!c.Bursts.empty())
        {
            json list = json::array();
            for (const auto& b : c.Bursts)
                list.push_back({ { "Time", b.Time }, { "Count", b.Count }, { "Cycles", b.Cycles }, { "Interval", b.Interval },
                                 { "Probability", b.Probability } });
            j["Bursts"] = std::move(list);
        }
    }

    static void ReadParticleMain(const json& j, ParticleSystemComponent& c)
    {
        using PS = ParticleSystemComponent;
        const PS d;
        c.Duration = std::max(0.01f, Number(j, "Duration", d.Duration));
        c.StartDelay = std::max(0.0f, Number(j, "StartDelay", d.StartDelay));
        c.Prewarm = j.value("Prewarm", false);
        c.SimulationSpeed = std::max(0.0f, Number(j, "SimulationSpeed", d.SimulationSpeed));
        c.StopActionType = EnumFromJson(j, "StopAction", s_ParticleStopActions, d.StopActionType);
        c.Sorting = EnumFromJson(j, "Sorting", s_ParticleSortModes, d.Sorting);
        // Older scenes: WorldSpace on / off.
        c.Space = j.value("WorldSpace", true) ? PS::SimulationSpace::World : PS::SimulationSpace::Local;
        c.Space = EnumFromJson(j, "Space", s_ParticleSpaces, c.Space);
        c.CustomSpace = EntityRef();
        if (auto it = j.find("CustomSpace"); it != j.end() && it->is_number_unsigned())
            c.CustomSpace.ID = it->get<uint64_t>();
        c.Bursts.clear();
        if (auto list = j.find("Bursts"); list != j.end() && list->is_array())
            for (const json& item : *list)
            {
                if (!item.is_object())
                    continue;
                PS::EmissionBurst b;
                b.Time = std::max(0.0f, Number(item, "Time", b.Time));
                b.Count = std::clamp(item.value("Count", b.Count), 0, 100000);
                b.Cycles = std::clamp(item.value("Cycles", b.Cycles), 0, 100000);
                b.Interval = std::max(0.0f, Number(item, "Interval", b.Interval));
                b.Probability = std::clamp(Number(item, "Probability", b.Probability), 0.0f, 1.0f);
                c.Bursts.push_back(b);
            }
    }

    // The modules of a particle system that are on or not at their defaults (the others are left out).
    static void WriteParticleModules(json& j, const ParticleSystemComponent& c)
    {
        using PS = ParticleSystemComponent;
        if (const auto& m = c.ColorOverLifetime; m.Enabled || !(m == PS::ColorOverLifetimeModule{}))
            j["ColorOverLifetime"] = { { "Enabled", m.Enabled }, { "Gradient", ParticleGradientToJson(m.Gradient) },
                                       { "RandomBetweenTwo", m.RandomBetweenTwo }, { "GradientB", ParticleGradientToJson(m.GradientB) } };
        if (const auto& m = c.SizeOverLifetime; m.Enabled || !(m == PS::SizeOverLifetimeModule{}))
            j["SizeOverLifetime"] = { { "Enabled", m.Enabled }, { "Curve", ParticleCurveToJson(m.Curve) },
                                      { "RandomBetweenTwo", m.RandomBetweenTwo }, { "CurveB", ParticleCurveToJson(m.CurveB) } };
        if (const auto& m = c.VelocityOverLifetime; m.Enabled || !(m == PS::VelocityOverLifetimeModule{}))
            j["VelocityOverLifetime"] = { { "Enabled", m.Enabled }, { "Linear", ToJson(m.Linear) }, { "LinearCurve", ParticleCurveToJson(m.LinearCurve) },
                                          { "World", m.World }, { "Orbital", ToJson(m.Orbital) }, { "Radial", m.Radial },
                                          { "SpeedModifier", ParticleCurveToJson(m.SpeedModifier) } };
        if (const auto& m = c.LimitVelocity; m.Enabled || !(m == PS::LimitVelocityModule{}))
            j["LimitVelocity"] = { { "Enabled", m.Enabled }, { "MaxSpeed", m.MaxSpeed }, { "MaxSpeedCurve", ParticleCurveToJson(m.MaxSpeedCurve) },
                                   { "Dampen", m.Dampen } };
        if (const auto& m = c.Noise; m.Enabled || !(m == PS::NoiseModule{}))
            j["Noise"] = { { "Enabled", m.Enabled }, { "Strength", ToJson(m.Strength) }, { "StrengthCurve", ParticleCurveToJson(m.StrengthCurve) },
                           { "Frequency", m.Frequency }, { "ScrollSpeed", m.ScrollSpeed }, { "Octaves", m.Octaves },
                           { "OctaveMultiplier", m.OctaveMultiplier }, { "Seed", m.Seed } };
        if (const auto& m = c.Collision; m.Enabled || !(m == PS::CollisionModule{}))
            j["Collision"] = { { "Enabled", m.Enabled }, { "Type", EnumName(m.Type, s_CollisionTypes) }, { "PlaneNormal", ToJson(m.PlaneNormal) },
                               { "PlaneOffset", m.PlaneOffset }, { "PlaneEntity", m.PlaneEntity.ID }, { "Bounce", m.Bounce }, { "Dampen", m.Dampen },
                               { "LifetimeLoss", m.LifetimeLoss }, { "MinKillSpeed", m.MinKillSpeed }, { "RadiusScale", m.RadiusScale },
                               { "CollidesWith", m.CollidesWith } };
        if (const auto& m = c.SubEmitters; m.Enabled || !m.Emitters.empty())
        {
            json list = json::array();
            for (const auto& sub : m.Emitters)
                list.push_back({ { "Trigger", EnumName(sub.Trigger, s_SubEmitterTriggers) }, { "Emitter", sub.Emitter.ID }, { "Count", sub.Count },
                                 { "Probability", sub.Probability }, { "InheritVelocity", sub.InheritVelocity },
                                 { "InheritColor", sub.InheritColor }, { "InheritSize", sub.InheritSize } });
            j["SubEmitters"] = { { "Enabled", m.Enabled }, { "Emitters", std::move(list) } };
        }
        if (const auto& m = c.Trails; m.Enabled || !(m == PS::TrailsModule{}))
            j["Trails"] = { { "Enabled", m.Enabled }, { "Ratio", m.Ratio }, { "Lifetime", m.Lifetime }, { "MinVertexDistance", m.MinVertexDistance },
                            { "MaxPoints", m.MaxPoints }, { "Width", m.Width }, { "WidthFromSize", m.WidthFromSize },
                            { "WidthCurve", ParticleCurveToJson(m.WidthCurve) }, { "Gradient", ParticleGradientToJson(m.Gradient) },
                            { "InheritColor", m.InheritColor } };
        if (const auto& m = c.FollowCamera; m.Enabled || !(m == PS::FollowCameraModule{}))
        {
            j["FollowCamera"] = { { "Enabled", m.Enabled }, { "Offset", ToJson(m.Offset) }, { "Wrap", m.Wrap }, { "WrapExtents", ToJson(m.WrapExtents) } };
            if (m.NearFade != PS::FollowCameraModule{}.NearFade)
                j["FollowCamera"]["NearFade"] = m.NearFade;
        }
        if (const auto& m = c.TextureSheet; m.Enabled || !(m == PS::TextureSheetModule{}))
            j["TextureSheet"] = { { "Enabled", m.Enabled }, { "Source", EnumName(m.Source, s_SheetSources) }, { "Columns", m.Columns },
                                  { "Rows", m.Rows }, { "FrameCount", m.FrameCount }, { "SpritePrefix", m.SpritePrefix },
                                  { "Mode", EnumName(m.Mode, s_SheetTimeModes) }, { "Cycles", m.Cycles }, { "FrameRate", m.FrameRate },
                                  { "StartFrame", m.StartFrame }, { "RandomStartFrame", m.RandomStartFrame }, { "RandomRow", m.RandomRow } };
    }

    static void ReadParticleModules(const json& j, ParticleSystemComponent& c)
    {
        using PS = ParticleSystemComponent;
        if (auto it = j.find("ColorOverLifetime"); it != j.end() && it->is_object())
        {
            auto& m = c.ColorOverLifetime;
            const PS::ColorOverLifetimeModule d;
            m.Enabled = it->value("Enabled", false);
            m.Gradient = ParticleGradientFromJson(*it, "Gradient", d.Gradient);
            m.RandomBetweenTwo = it->value("RandomBetweenTwo", false);
            m.GradientB = ParticleGradientFromJson(*it, "GradientB", d.GradientB);
        }
        if (auto it = j.find("SizeOverLifetime"); it != j.end() && it->is_object())
        {
            auto& m = c.SizeOverLifetime;
            const PS::SizeOverLifetimeModule d;
            m.Enabled = it->value("Enabled", false);
            m.Curve = ParticleCurveFromJson(*it, "Curve", d.Curve);
            m.RandomBetweenTwo = it->value("RandomBetweenTwo", false);
            m.CurveB = ParticleCurveFromJson(*it, "CurveB", d.CurveB);
        }
        if (auto it = j.find("VelocityOverLifetime"); it != j.end() && it->is_object())
        {
            auto& m = c.VelocityOverLifetime;
            const PS::VelocityOverLifetimeModule d;
            m.Enabled = it->value("Enabled", false);
            m.Linear = Vec(*it, "Linear", d.Linear);
            m.LinearCurve = ParticleCurveFromJson(*it, "LinearCurve", d.LinearCurve);
            m.World = it->value("World", false);
            m.Orbital = Vec(*it, "Orbital", d.Orbital);
            m.Radial = Number(*it, "Radial", 0.0f);
            m.SpeedModifier = ParticleCurveFromJson(*it, "SpeedModifier", d.SpeedModifier);
        }
        if (auto it = j.find("LimitVelocity"); it != j.end() && it->is_object())
        {
            auto& m = c.LimitVelocity;
            const PS::LimitVelocityModule d;
            m.Enabled = it->value("Enabled", false);
            m.MaxSpeed = std::max(0.0f, Number(*it, "MaxSpeed", d.MaxSpeed));
            m.MaxSpeedCurve = ParticleCurveFromJson(*it, "MaxSpeedCurve", d.MaxSpeedCurve);
            m.Dampen = std::clamp(Number(*it, "Dampen", d.Dampen), 0.0f, 1.0f);
        }
        if (auto it = j.find("TextureSheet"); it != j.end() && it->is_object())
        {
            auto& m = c.TextureSheet;
            const PS::TextureSheetModule d;
            m.Enabled = it->value("Enabled", false);
            m.Source = EnumFromJson(*it, "Source", s_SheetSources, d.Source);
            m.Columns = std::clamp(it->value("Columns", d.Columns), 1, 256);
            m.Rows = std::clamp(it->value("Rows", d.Rows), 1, 256);
            m.FrameCount = std::max(0, it->value("FrameCount", 0));
            m.SpritePrefix = it->value("SpritePrefix", std::string());
            m.Mode = EnumFromJson(*it, "Mode", s_SheetTimeModes, d.Mode);
            m.Cycles = std::max(0.0f, Number(*it, "Cycles", d.Cycles));
            m.FrameRate = std::max(0.0f, Number(*it, "FrameRate", d.FrameRate));
            m.StartFrame = std::max(0, it->value("StartFrame", 0));
            m.RandomStartFrame = it->value("RandomStartFrame", false);
            m.RandomRow = it->value("RandomRow", false);
        }
        auto entityRef = [](const json& o, const char* key) {
            EntityRef ref;
            if (auto i = o.find(key); i != o.end() && i->is_number_unsigned())
                ref.ID = i->get<uint64_t>();
            return ref;
        };
        if (auto it = j.find("Noise"); it != j.end() && it->is_object())
        {
            auto& m = c.Noise;
            const PS::NoiseModule d;
            m.Enabled = it->value("Enabled", false);
            m.Strength = Vec(*it, "Strength", d.Strength);
            m.StrengthCurve = ParticleCurveFromJson(*it, "StrengthCurve", d.StrengthCurve);
            m.Frequency = std::max(0.0f, Number(*it, "Frequency", d.Frequency));
            m.ScrollSpeed = Number(*it, "ScrollSpeed", d.ScrollSpeed);
            m.Octaves = std::clamp(it->value("Octaves", d.Octaves), 1, 4);
            m.OctaveMultiplier = std::clamp(Number(*it, "OctaveMultiplier", d.OctaveMultiplier), 0.0f, 1.0f);
            m.Seed = it->contains("Seed") && (*it)["Seed"].is_number_unsigned() ? (*it)["Seed"].get<uint32_t>() : 0u;
        }
        if (auto it = j.find("Collision"); it != j.end() && it->is_object())
        {
            auto& m = c.Collision;
            const PS::CollisionModule d;
            m.Enabled = it->value("Enabled", false);
            m.Type = EnumFromJson(*it, "Type", s_CollisionTypes, d.Type);
            m.PlaneNormal = Vec(*it, "PlaneNormal", d.PlaneNormal);
            m.PlaneOffset = Number(*it, "PlaneOffset", d.PlaneOffset);
            m.PlaneEntity = entityRef(*it, "PlaneEntity");
            m.Bounce = std::max(0.0f, Number(*it, "Bounce", d.Bounce));
            m.Dampen = std::clamp(Number(*it, "Dampen", d.Dampen), 0.0f, 1.0f);
            m.LifetimeLoss = std::clamp(Number(*it, "LifetimeLoss", d.LifetimeLoss), 0.0f, 1.0f);
            m.MinKillSpeed = std::max(0.0f, Number(*it, "MinKillSpeed", d.MinKillSpeed));
            m.RadiusScale = std::max(0.0f, Number(*it, "RadiusScale", d.RadiusScale));
            m.CollidesWith = (uint16_t)std::clamp(it->value("CollidesWith", (int)d.CollidesWith), 0, 0xFFFF);
        }
        if (auto it = j.find("SubEmitters"); it != j.end() && it->is_object())
        {
            auto& m = c.SubEmitters;
            m.Enabled = it->value("Enabled", false);
            m.Emitters.clear();
            if (auto list = it->find("Emitters"); list != it->end() && list->is_array())
                for (const json& item : *list)
                {
                    if (!item.is_object())
                        continue;
                    PS::SubEmitter sub;
                    sub.Trigger = EnumFromJson(item, "Trigger", s_SubEmitterTriggers, sub.Trigger);
                    sub.Emitter = entityRef(item, "Emitter");
                    sub.Count = std::clamp(item.value("Count", sub.Count), 0, 100000);
                    sub.Probability = std::clamp(Number(item, "Probability", sub.Probability), 0.0f, 1.0f);
                    sub.InheritVelocity = Number(item, "InheritVelocity", sub.InheritVelocity);
                    sub.InheritColor = item.value("InheritColor", false);
                    sub.InheritSize = item.value("InheritSize", false);
                    m.Emitters.push_back(sub);
                }
        }
        if (auto it = j.find("FollowCamera"); it != j.end() && it->is_object())
        {
            auto& m = c.FollowCamera;
            const PS::FollowCameraModule d;
            m.Enabled = it->value("Enabled", false);
            m.Offset = Vec(*it, "Offset", d.Offset);
            m.Wrap = it->value("Wrap", d.Wrap);
            m.WrapExtents = glm::max(Vec(*it, "WrapExtents", d.WrapExtents), glm::vec3(0.0f));
            m.NearFade = std::max(0.0f, Number(*it, "NearFade", d.NearFade));
        }
        if (auto it = j.find("Trails"); it != j.end() && it->is_object())
        {
            auto& m = c.Trails;
            const PS::TrailsModule d;
            m.Enabled = it->value("Enabled", false);
            m.Ratio = std::clamp(Number(*it, "Ratio", d.Ratio), 0.0f, 1.0f);
            m.Lifetime = std::max(0.0f, Number(*it, "Lifetime", d.Lifetime));
            m.MinVertexDistance = std::max(0.0f, Number(*it, "MinVertexDistance", d.MinVertexDistance));
            m.MaxPoints = std::clamp(it->value("MaxPoints", d.MaxPoints), 2, 256);
            m.Width = std::max(0.0f, Number(*it, "Width", d.Width));
            m.WidthFromSize = it->value("WidthFromSize", d.WidthFromSize);
            m.WidthCurve = ParticleCurveFromJson(*it, "WidthCurve", d.WidthCurve);
            m.Gradient = ParticleGradientFromJson(*it, "Gradient", d.Gradient);
            m.InheritColor = it->value("InheritColor", d.InheritColor);
        }
    }

    // A particle system's settings (scenes, prefabs, .zparticle presets): the first systems' fields
    // always, the newer ones only when they are used or changed (older files stay as they were).
    static json ParticleSystemToJson(const ParticleSystemComponent& c)
    {
        using PS = ParticleSystemComponent;
        json j = {
            { "Playing", c.PlayOnStart }, { "Looping", c.Looping }, { "PlayInEditor", c.PlayInEditor },
            { "EmissionRate", c.EmissionRate }, { "Burst", c.Burst }, { "MaxParticles", c.MaxParticles },
            { "Shape", (int)c.Shape }, { "ShapeRadius", c.ShapeRadius }, { "ShapeExtents", ToJson(c.ShapeExtents) },
            { "LifetimeMin", c.LifetimeMin }, { "LifetimeMax", c.LifetimeMax }, { "SpeedMin", c.SpeedMin }, { "SpeedMax", c.SpeedMax },
            { "Direction", ToJson(c.Direction) }, { "Spread", c.Spread }, { "Gravity", ToJson(c.Gravity) }, { "Drag", c.Drag },
            { "AngularVelocityMax", c.AngularVelocityMax },
            { "StartColor", ToJson(c.StartColor) }, { "EndColor", ToJson(c.EndColor) },
            { "StartSize", c.StartSize }, { "EndSize", c.EndSize },
            { "RenderAsCircle", c.RenderAsCircle }, { "Additive", c.Additive },
            { "WorldSpace", c.Space == PS::SimulationSpace::World },
            { "Billboard", c.Billboard }, { "Texture", c.TexturePath }, { "SortingOrder", c.SortingOrder },
        };
        WriteAssetHandle(j, "Texture", c.TexturePath);
        WriteSortingLayer(j, c.SortingLayer);
        // The newer shape settings only when they are used or changed.
        const PS d;
        if (c.ShapeThickness != d.ShapeThickness) j["ShapeThickness"] = c.ShapeThickness;
        if (c.AlignToShape) j["AlignToShape"] = true;
        if (c.Shape == PS::EmitterShape::Cone || c.ConeAngle != d.ConeAngle || c.ConeLength != d.ConeLength || c.ConeFrom != d.ConeFrom || c.ConeFlat)
        {
            j["ConeAngle"] = c.ConeAngle;
            j["ConeLength"] = c.ConeLength;
            j["ConeFrom"] = EnumName(c.ConeFrom, s_ConeEmitFrom);
            j["ConeFlat"] = c.ConeFlat;
        }
        if (c.Shape == PS::EmitterShape::Edge || c.EdgeLength != d.EdgeLength)
            j["EdgeLength"] = c.EdgeLength;
        if (c.Shape == PS::EmitterShape::Mesh || c.MeshSource != d.MeshSource || c.MeshPrimitive != d.MeshPrimitive || !c.MeshModelPath.empty() ||
            c.MeshMode != d.MeshMode || c.MeshAlongNormal != d.MeshAlongNormal || c.MeshNormalOffset != d.MeshNormalOffset)
        {
            j["MeshSource"] = EnumName(c.MeshSource, s_MeshShapeSources);
            j["MeshPrimitive"] = PrimitiveMeshToString(c.MeshPrimitive);
            j["MeshModel"] = c.MeshModelPath;
            WriteAssetHandle(j, "MeshModel", c.MeshModelPath);
            j["MeshMode"] = EnumName(c.MeshMode, s_MeshEmitModes);
            j["MeshAlongNormal"] = c.MeshAlongNormal;
            j["MeshNormalOffset"] = c.MeshNormalOffset;
        }
        // The modules' on / off switches of the first systems (always on then).
        if (!c.EmissionEnabled) j["EmissionEnabled"] = false;
        if (!c.ShapeEnabled) j["ShapeEnabled"] = false;
        if (!c.RendererEnabled) j["RendererEnabled"] = false;
        if (c.Stretched || c.StretchLength != d.StretchLength || c.StretchSpeed != d.StretchSpeed)
        {
            j["Stretched"] = c.Stretched;
            j["StretchLength"] = c.StretchLength;
            j["StretchSpeed"] = c.StretchSpeed;
        }
        WriteParticleMain(j, c);
        WriteParticleModules(j, c);
        return j;
    }

    static void ParticleSystemFromJson(const json& j, ParticleSystemComponent& c)
    {
        using PS = ParticleSystemComponent;
        c.PlayOnStart = j.value("Playing", true); // plays on start (the key of the first systems)
        c.Playing = c.PlayOnStart;
        c.Looping = j.value("Looping", true);
        c.PlayInEditor = j.value("PlayInEditor", true);
        c.EmissionRate = j.value("EmissionRate", 30.0f);
        c.Burst = j.value("Burst", 0);
        c.MaxParticles = j.value("MaxParticles", 1000);
        c.Shape = (PS::EmitterShape)std::clamp(j.value("Shape", 0), 0, (int)PS::EmitterShape::Mesh);
        c.ShapeRadius = j.value("ShapeRadius", 0.5f);
        c.ShapeExtents = Vec(j, "ShapeExtents", glm::vec3(0.5f));
        c.ShapeThickness = std::clamp(Number(j, "ShapeThickness", 1.0f), 0.0f, 1.0f);
        c.AlignToShape = j.value("AlignToShape", false);
        c.ConeAngle = std::clamp(Number(j, "ConeAngle", c.ConeAngle), 0.0f, 89.0f);
        c.ConeLength = std::max(0.0f, Number(j, "ConeLength", c.ConeLength));
        c.ConeFrom = EnumFromJson(j, "ConeFrom", s_ConeEmitFrom, c.ConeFrom);
        c.ConeFlat = j.value("ConeFlat", false);
        c.EdgeLength = std::max(0.0f, Number(j, "EdgeLength", c.EdgeLength));
        c.MeshSource = EnumFromJson(j, "MeshSource", s_MeshShapeSources, c.MeshSource);
        if (auto it = j.find("MeshPrimitive"); it != j.end() && it->is_string())
            c.MeshPrimitive = PrimitiveMeshFromString(it->get<std::string>());
        c.MeshModelPath = ReadAssetPath(j, "MeshModel");
        c.MeshMode = EnumFromJson(j, "MeshMode", s_MeshEmitModes, c.MeshMode);
        c.MeshAlongNormal = j.value("MeshAlongNormal", true);
        c.MeshNormalOffset = Number(j, "MeshNormalOffset", 0.0f);
        c.LifetimeMin = j.value("LifetimeMin", 0.8f);
        c.LifetimeMax = j.value("LifetimeMax", 1.6f);
        c.SpeedMin = j.value("SpeedMin", 1.0f);
        c.SpeedMax = j.value("SpeedMax", 3.0f);
        c.Direction = Vec(j, "Direction", glm::vec3(0, 1, 0));
        c.Spread = j.value("Spread", 25.0f);
        c.Gravity = Vec(j, "Gravity", glm::vec3(0.0f));
        c.Drag = j.value("Drag", 0.0f);
        c.AngularVelocityMax = j.value("AngularVelocityMax", 0.0f);
        c.StartColor = Vec(j, "StartColor", c.StartColor);
        c.EndColor = Vec(j, "EndColor", c.EndColor);
        c.StartSize = j.value("StartSize", 0.35f);
        c.EndSize = j.value("EndSize", 0.05f);
        c.RenderAsCircle = j.value("RenderAsCircle", true);
        c.Additive = j.value("Additive", true);
        c.Billboard = j.value("Billboard", true);
        c.TexturePath = ReadAssetPath(j, "Texture");
        c.SortingOrder = j.value("SortingOrder", 0);
        c.SortingLayer = ReadSortingLayer(j);
        const PS d;
        c.EmissionEnabled = j.value("EmissionEnabled", true);
        c.ShapeEnabled = j.value("ShapeEnabled", true);
        c.RendererEnabled = j.value("RendererEnabled", true);
        c.Stretched = j.value("Stretched", false);
        c.StretchLength = std::max(0.0f, Number(j, "StretchLength", d.StretchLength));
        c.StretchSpeed = std::max(0.0f, Number(j, "StretchSpeed", d.StretchSpeed));
        ReadParticleMain(j, c);
        ReadParticleModules(j, c);
    }

    static const char* BodyTypeToString(Rigidbody2DComponent::BodyType t)
    {
        switch (t)
        {
            case Rigidbody2DComponent::BodyType::Static:    return "Static";
            case Rigidbody2DComponent::BodyType::Dynamic:   return "Dynamic";
            case Rigidbody2DComponent::BodyType::Kinematic: return "Kinematic";
        }
        return "Static";
    }

    static Rigidbody2DComponent::BodyType BodyTypeFromString(const std::string& s)
    {
        if (s == "Dynamic")   return Rigidbody2DComponent::BodyType::Dynamic;
        if (s == "Kinematic") return Rigidbody2DComponent::BodyType::Kinematic;
        return Rigidbody2DComponent::BodyType::Static;
    }

    static json SerializeFieldValue(const ScriptFieldValue& v)
    {
        json j;
        j["Type"] = ScriptFieldTypeToString(v.Type);
        switch (v.Type)
        {
            case ScriptFieldType::Float:  j["Value"] = v.Get<float>(); break;
            case ScriptFieldType::Int:    j["Value"] = v.Get<int>(); break;
            case ScriptFieldType::Bool:   j["Value"] = v.Get<bool>(); break;
            case ScriptFieldType::Vec2:   j["Value"] = ToJson(v.Get<glm::vec2>()); break;
            case ScriptFieldType::Vec3:   j["Value"] = ToJson(v.Get<glm::vec3>()); break;
            case ScriptFieldType::Vec4:   j["Value"] = ToJson(v.Get<glm::vec4>()); break;
            case ScriptFieldType::String: j["Value"] = v.StringValue; break;
            case ScriptFieldType::Entity: j["Value"] = v.Get<EntityRef>().ID; break;
            case ScriptFieldType::Enum:   j["Value"] = v.GetEnum(); break;
            case ScriptFieldType::Curve:  j["Value"] = AnimationCurveToJson(v.CurveValue); break;
            default: break;
        }
        return j;
    }

    static bool DeserializeFieldValue(const json& j, ScriptFieldValue& v)
    {
        ScriptFieldType type = ScriptFieldTypeFromString(j.value("Type", ""));
        json wrapper = { { "Value", j.contains("Value") ? j["Value"] : json() } };
        switch (type)
        {
            case ScriptFieldType::Float:  v.Set(j.value("Value", 0.0f)); break;
            case ScriptFieldType::Int:    v.Set(j.value("Value", 0)); break;
            case ScriptFieldType::Bool:   v.Set(j.value("Value", false)); break;
            case ScriptFieldType::Vec2:   v.Set(Vec<glm::vec2>(wrapper, "Value", glm::vec2(0.0f))); break;
            case ScriptFieldType::Vec3:   v.Set(Vec<glm::vec3>(wrapper, "Value", glm::vec3(0.0f))); break;
            case ScriptFieldType::Vec4:   v.Set(Vec<glm::vec4>(wrapper, "Value", glm::vec4(0.0f))); break;
            case ScriptFieldType::String: v.Set(j.value("Value", std::string())); break;
            case ScriptFieldType::Entity: v.Set(EntityRef{ j.value("Value", (uint64_t)0) }); break;
            case ScriptFieldType::Curve:  v.Set(AnimationCurveFromJson(j.contains("Value") ? j["Value"] : json())); break;
            case ScriptFieldType::Enum:
            {
                const json& value = j.contains("Value") ? j["Value"] : json();
                v.SetEnum(value.is_number_integer() ? value.get<int64_t>()
                          : value.is_number() ? (int64_t)value.get<double>() : 0);
                break;
            }
            default: return false;
        }
        return true;
    }

    static const char* BodyType3DToString(Rigidbody3DComponent::BodyType t)
    {
        switch (t)
        {
            case Rigidbody3DComponent::BodyType::Static:    return "Static";
            case Rigidbody3DComponent::BodyType::Dynamic:   return "Dynamic";
            case Rigidbody3DComponent::BodyType::Kinematic: return "Kinematic";
        }
        return "Static";
    }

    static Rigidbody3DComponent::BodyType BodyType3DFromString(const std::string& s)
    {
        if (s == "Dynamic")   return Rigidbody3DComponent::BodyType::Dynamic;
        if (s == "Kinematic") return Rigidbody3DComponent::BodyType::Kinematic;
        return Rigidbody3DComponent::BodyType::Static;
    }

    // includeEditorFlags: false for prefabs (an instance should not start hidden / locked).
    static json SerializeEntity(Entity entity, bool includeParent = true, bool includeEditorFlags = true)
    {
        json e;
        e["Entity"] = (uint64_t)entity.GetUUID();
        e["Tag"] = entity.GetName();
        // Game state (kept by prefabs too); only written when off, so older files stay the same.
        if (!entity.IsActive())
            e["Active"] = false;
        if (includeParent)
            if (auto* rel = entity.TryGetComponent<RelationshipComponent>(); rel && (uint64_t)rel->Parent)
                e["Parent"] = (uint64_t)rel->Parent;

        {
            auto& t = entity.GetComponent<TransformComponent>();
            e["Transform"] = { { "Translation", ToJson(t.Translation) }, { "Rotation", ToJson(t.Rotation) }, { "Scale", ToJson(t.Scale) } };
        }
        if (auto* c = entity.TryGetComponent<TagsComponent>())
            e["Tags"] = c->Tags; // gameplay tags ("Tag" above is the display name)
        if (auto* c = entity.TryGetComponent<CameraComponent>())
        {
            auto& cam = c->Camera;
            e["Camera"] = {
                { "ProjectionType", (int)cam.GetProjectionType() },
                { "PerspectiveFOV", cam.GetPerspectiveVerticalFOV() },
                { "PerspectiveNear", cam.GetPerspectiveNearClip() },
                { "PerspectiveFar", cam.GetPerspectiveFarClip() },
                { "OrthographicSize", cam.GetOrthographicSize() },
                { "OrthographicNear", cam.GetOrthographicNearClip() },
                { "OrthographicFar", cam.GetOrthographicFarClip() },
                { "Primary", c->Primary },
                { "FixedAspectRatio", c->FixedAspectRatio },
                { "ClearColor", ToJson(c->ClearColor) },
            };
            const CameraShake defaults;
            if (c->Shake.MaxOffset != defaults.MaxOffset || c->Shake.MaxAngle != defaults.MaxAngle || c->Shake.Exponent != defaults.Exponent)
                e["Camera"]["Shake"] = { { "MaxOffset", ToJson(c->Shake.MaxOffset) }, { "MaxAngle", c->Shake.MaxAngle },
                                         { "Exponent", c->Shake.Exponent } };
            // Only when used (older files stay the same).
            if (c->PixelPerfect != PixelPerfectSettings())
            {
                const PixelPerfectSettings& pp = c->PixelPerfect;
                e["Camera"]["PixelPerfect"] = { { "Enabled", pp.Enabled }, { "PixelsPerUnit", pp.PixelsPerUnit },
                                                { "ReferenceResolution", { pp.ReferenceResolution.x, pp.ReferenceResolution.y } },
                                                { "Upscale", pp.UpscaleRenderTarget }, { "SnapCamera", pp.SnapCamera },
                                                { "SnapSprites", pp.SnapSprites } };
            }
        }
        if (auto* c = entity.TryGetComponent<SpriteRendererComponent>())
        {
            json& j = e["SpriteRenderer"];
            j = { { "Color", ToJson(c->Color) }, { "Texture", c->TexturePath }, { "Tiling", c->TilingFactor },
                  { "SortingOrder", c->SortingOrder }, { "PixelArt", c->PixelArt }, { "FlipX", c->FlipX }, { "FlipY", c->FlipY } };
            WriteAssetHandle(j, "Texture", c->TexturePath);
            // Newer settings only when used (older files stay unchanged).
            if (!c->SpriteName.empty())
                j["Sprite"] = c->SpriteName;
            if (c->DrawMode != SpriteDrawMode::Simple)
                j["DrawMode"] = SpriteDrawModeToString(c->DrawMode);
            if (c->Size != glm::vec2(1.0f))
                j["Size"] = ToJson(c->Size);
            if (c->HasSliceBorder())
                j["SliceBorder"] = ToJson(c->SliceBorder);
            if (c->PixelsPerUnit != 100.0f)
                j["PixelsPerUnit"] = c->PixelsPerUnit;
            if (c->FlashAmount > 0.0f || c->FlashColor != glm::vec4(1.0f))
                j["Flash"] = { { "Color", ToJson(c->FlashColor) }, { "Amount", c->FlashAmount } };
            if (c->OutlineThickness > 0.0f || c->OutlineColor != glm::vec4(1.0f))
                j["Outline"] = { { "Color", ToJson(c->OutlineColor) }, { "Thickness", c->OutlineThickness } };
            if (c->Lit)
                j["Lit"] = true;
            WriteSortingLayer(j, c->SortingLayer);
        }
        if (auto* c = entity.TryGetComponent<SpriteAnimationComponent>())
        {
            e["SpriteAnimation"] = { { "Columns", c->Columns }, { "Rows", c->Rows }, { "StartFrame", c->StartFrame },
                                     { "FrameCount", c->FrameCount }, { "FPS", c->FPS }, { "Loop", c->Loop }, { "Playing", c->Playing } };
            if (!c->Frames.empty())
                e["SpriteAnimation"]["Frames"] = c->Frames;
            if (!c->Clips.empty())
            {
                json clips = json::array();
                for (const std::string& clip : c->Clips)
                {
                    json j = { { "Path", clip } };
                    WriteAssetHandle(j, "Path", clip);
                    clips.push_back(std::move(j));
                }
                e["SpriteAnimation"]["Clips"] = std::move(clips);
                e["SpriteAnimation"]["PlayOnStart"] = c->PlayOnStart;
            }
        }
        if (auto* c = entity.TryGetComponent<TintAnimationComponent>())
        {
            json keys = json::array();
            for (const TintKey& k : c->Keys)
                keys.push_back({ { "Time", k.Time }, { "Color", ToJson(k.Color) } });
            e["TintAnimation"] = { { "Keys", std::move(keys) }, { "Duration", c->Duration }, { "Mode", TintAnimation::ModeToString(c->Mode) },
                                   { "Ease", EaseToString(c->Easing) }, { "Cycles", c->Cycles }, { "StartOffset", c->StartOffset },
                                   { "PlayOnStart", c->PlayOnStart } };
        }
        if (auto* c = entity.TryGetComponent<CircleRendererComponent>())
        {
            e["CircleRenderer"] = { { "Color", ToJson(c->Color) }, { "Thickness", c->Thickness }, { "Fade", c->Fade }, { "SortingOrder", c->SortingOrder } };
            WriteSortingLayer(e["CircleRenderer"], c->SortingLayer);
        }
        if (auto* c = entity.TryGetComponent<TextComponent>())
        {
            e["Text"] = { { "Text", c->Text }, { "Color", ToJson(c->Color) }, { "Kerning", c->Kerning }, { "LineSpacing", c->LineSpacing },
                          { "AlignH", EnumName(c->AlignH, s_TextAlignH) }, { "AlignV", EnumName(c->AlignV, s_TextAlignV) },
                          { "Font", c->FontPath } };
            if (c->WordWrap)
                e["Text"]["WordWrap"] = true;
            if (c->Overflow != TextOverflow::Overflow)
                e["Text"]["Overflow"] = EnumName(c->Overflow, s_TextOverflows);
            if (c->MaxWidth > 0.0f)
                e["Text"]["MaxWidth"] = c->MaxWidth;
            if (!c->LocalizationKey.empty())
                e["Text"]["LocalizationKey"] = c->LocalizationKey;
            WriteAssetHandle(e["Text"], "Font", c->FontPath);
            if (c->SortingOrder != 1000)
                e["Text"]["SortingOrder"] = c->SortingOrder;
            WriteSortingLayer(e["Text"], c->SortingLayer);
        }
        if (auto* c = entity.TryGetComponent<MeshRendererComponent>())
        {
            json& j = e["MeshRenderer"];
            j = {
                { "Mesh", PrimitiveMeshToString(c->Type) }, { "Model", c->ModelPath }, { "CastShadows", c->CastShadows },
                { "Material", c->MaterialPath },
                { "Albedo", ToJson(c->Mat.Albedo) }, { "AlbedoMap", c->AlbedoPath },
                // the same on both axes: one number, as before tiling became a vec2
                { "Tiling", c->Mat.Tiling.x == c->Mat.Tiling.y ? json(c->Mat.Tiling.x) : ToJson(c->Mat.Tiling) },
                { "Offset", ToJson(c->Mat.Offset) },
                { "NormalMap", c->NormalPath }, { "NormalStrength", c->Mat.NormalStrength },
                { "MetalRoughMap", c->MetalRoughPath }, { "EmissiveMap", c->EmissivePath },
                { "Metallic", c->Mat.Metallic }, { "Roughness", c->Mat.Roughness },
                { "Emission", ToJson(c->Mat.Emission) }, { "EmissionStrength", c->Mat.EmissionStrength },
                { "Shader", MaterialSerializer::ShaderToString(c->Mat.Shader) },
                { "BlendMode", MaterialSerializer::BlendModeToString(c->Mat.BlendMode) },
                { "AlphaCutoff", c->Mat.AlphaCutoff }, { "DoubleSided", c->Mat.DoubleSided },
                { "PreserveSpecular", c->Mat.PreserveSpecular }, { "SortPriority", c->Mat.SortPriority },
            };
            WriteAssetHandle(j, "Material", c->MaterialPath);
            if (std::any_of(c->SubmeshMaterials.begin(), c->SubmeshMaterials.end(), [](const std::string& p) { return !p.empty(); }))
            {
                json slots = json::array();
                for (const std::string& path : c->SubmeshMaterials)
                {
                    json slot = { { "Material", path } };
                    WriteAssetHandle(slot, "Material", path);
                    slots.push_back(std::move(slot));
                }
                j["SubmeshMaterials"] = std::move(slots);
            }
            if (!c->MorphWeights.empty())
                j["MorphWeights"] = c->MorphWeights;
            WriteAssetHandle(j, "Model", c->ModelPath);
            WriteAssetHandle(j, "AlbedoMap", c->AlbedoPath);
            WriteAssetHandle(j, "NormalMap", c->NormalPath);
            WriteAssetHandle(j, "MetalRoughMap", c->MetalRoughPath);
            WriteAssetHandle(j, "EmissiveMap", c->EmissivePath);
        }
        if (auto* c = entity.TryGetComponent<AnimatorComponent>())
        {
            json params = json::array(), states = json::array(), transitions = json::array();
            for (const auto& p : c->Parameters)
                params.push_back({ { "Name", p.Name }, { "Type", (int)p.Kind }, { "Value", p.Value } });
            for (const auto& st : c->States)
            {
                json js = { { "Name", st.Name }, { "Clip", st.Clip }, { "Speed", st.Speed }, { "Loop", st.Loop }, { "SpeedParameter", st.SpeedParameter } };
                if (st.IsBlendTree())
                {
                    js["Type"] = "BlendTree1D";
                    js["BlendParameter"] = st.BlendParameter;
                    json motions = json::array();
                    for (const auto& m : st.Motions)
                        motions.push_back({ { "Clip", m.Clip }, { "Threshold", m.Threshold }, { "Speed", m.Speed } });
                    js["Motions"] = std::move(motions);
                }
                js["Graph"] = ToJson(st.GraphPosition);
                states.push_back(std::move(js));
            }
            for (const auto& t : c->Transitions)
            {
                json conds = json::array();
                for (const auto& cd : t.Conditions)
                    conds.push_back({ { "Parameter", cd.Parameter }, { "Op", (int)cd.Mode }, { "Threshold", cd.Threshold } });
                transitions.push_back({ { "From", t.From }, { "To", t.To }, { "Fade", t.Fade }, { "HasExitTime", t.HasExitTime },
                                        { "ExitTime", t.ExitTime }, { "Conditions", conds } });
            }
            e["Animator"] = { { "Clip", c->Clip }, { "Speed", c->Speed }, { "Loop", c->Loop }, { "Playing", c->Playing },
                              { "PlayInEditor", c->PlayInEditor }, { "DefaultFade", c->DefaultFade }, { "DefaultState", c->DefaultState },
                              { "Parameters", params }, { "States", states }, { "Transitions", transitions } };
            if (!c->Events.empty())
            {
                json events = json::array();
                for (const AnimatorEvent& ev : c->Events)
                {
                    json j = { { "Clip", ev.Clip }, { "Time", ev.Time }, { "Name", ev.Name } };
                    if (!ev.Parameter.empty())
                        j["Parameter"] = ev.Parameter;
                    events.push_back(std::move(j));
                }
                e["Animator"]["Events"] = std::move(events);
            }
            if (!c->States.empty() || c->AnyStateGraphPosition != glm::vec2(0.0f))
                e["Animator"]["AnyStateGraph"] = ToJson(c->AnyStateGraphPosition);
            if (!c->Layers.empty())
            {
                json layers = json::array();
                for (const AnimatorLayer& l : c->Layers)
                {
                    json jl = { { "Name", l.Name }, { "Clip", l.Clip }, { "Speed", l.Speed }, { "Loop", l.Loop }, { "Weight", l.Weight },
                                { "Blend", l.Blend == AnimatorLayerBlend::Additive ? "Additive" : "Override" }, { "Mask", l.MaskBones } };
                    if (!l.WeightParameter.empty())
                        jl["WeightParameter"] = l.WeightParameter;
                    layers.push_back(std::move(jl));
                }
                e["Animator"]["Layers"] = std::move(layers);
            }
            if (c->ApplyRootMotion)
                e["Animator"]["ApplyRootMotion"] = true;
            if (!c->RootMotionBone.empty())
                e["Animator"]["RootMotionBone"] = c->RootMotionBone;
            if (c->Culling != AnimatorCulling::AlwaysAnimate)
                e["Animator"]["Culling"] = c->Culling == AnimatorCulling::CullPose ? "Pose" : "Complete";
            if (c->CachePoses)
                e["Animator"]["CachePoses"] = true;
            if (c->PoseCacheRate != 30.0f)
                e["Animator"]["PoseCacheRate"] = c->PoseCacheRate;
        }
        if (auto* c = entity.TryGetComponent<AnimationPlayerComponent>())
        {
            json clips = json::array();
            for (const std::string& clip : c->Clips)
            {
                json j = { { "Path", clip } };
                WriteAssetHandle(j, "Path", clip);
                clips.push_back(std::move(j));
            }
            static const char* s_Wraps[] = { "Clip", "Once", "Loop", "PingPong" };
            e["AnimationPlayer"] = { { "Clips", std::move(clips) }, { "Speed", c->Speed }, { "PlayOnStart", c->PlayOnStart },
                                     { "Wrap", s_Wraps[std::clamp((int)c->Wrap, 0, 3)] } };
        }
        if (auto* c = entity.TryGetComponent<BoneAttachmentComponent>())
            e["BoneAttachment"] = { { "Target", c->Target.ID }, { "Bone", c->Bone }, { "InheritBoneScale", c->InheritBoneScale } };
        if (auto* c = entity.TryGetComponent<SubSceneComponent>())
        {
            // "Path", not "Scene": that is a name key (the scene's own name), and asset tools pass names by.
            json& j = e["SubScene"];
            j = { { "Path", c->ScenePath }, { "LoadOnStart", c->LoadOnStart }, { "ApplyTransform", c->ApplyTransform },
                  { "ShowInEditor", c->ShowInEditor } };
            WriteAssetHandle(j, "Path", c->ScenePath);
        }
        if (auto* c = entity.TryGetComponent<FootstepsComponent>())
        {
            json sounds = json::array();
            for (const std::string& sound : c->Sounds)
            {
                json j = { { "Path", sound } };
                WriteAssetHandle(j, "Path", sound);
                sounds.push_back(std::move(j));
            }
            e["Footsteps"] = { { "Sounds", std::move(sounds) }, { "Event", c->EventName }, { "Volume", c->Volume },
                               { "VolumeRandom", ToJson(c->VolumeRandom) }, { "PitchRandom", ToJson(c->PitchRandom) },
                               { "SpeedParameter", c->SpeedParameter }, { "ReferenceSpeed", c->ReferenceSpeed },
                               { "SpeedPitch", c->SpeedPitch }, { "SpeedVolume", c->SpeedVolume }, { "MinSpeed", c->MinSpeed },
                               { "MinInterval", c->MinInterval }, { "Spatial", c->Spatial }, { "MinDistance", c->MinDistance },
                               { "MaxDistance", c->MaxDistance }, { "Output", c->Output } };
        }
        if (auto* c = entity.TryGetComponent<DirectionalLightComponent>())
            e["DirectionalLight"] = { { "Color", ToJson(c->Color) }, { "Intensity", c->Intensity }, { "CastShadows", c->CastShadows } };
        if (auto* c = entity.TryGetComponent<PointLightComponent>())
            e["PointLight"] = { { "Color", ToJson(c->Color) }, { "Intensity", c->Intensity }, { "Radius", c->Radius } };
        if (auto* c = entity.TryGetComponent<SpotLightComponent>())
            e["SpotLight"] = { { "Color", ToJson(c->Color) }, { "Intensity", c->Intensity }, { "Range", c->Range },
                               { "InnerAngle", c->InnerAngle }, { "OuterAngle", c->OuterAngle } };
        if (auto* c = entity.TryGetComponent<Light2DComponent>())
            e["Light2D"] = { { "Type", c->Type == Light2DType::Spot ? "Spot" : "Point" }, { "Color", ToJson(c->Color) },
                             { "Intensity", c->Intensity }, { "Radius", c->Radius }, { "InnerRadius", c->InnerRadius },
                             { "Falloff", c->Falloff }, { "SpotAngle", c->SpotAngle }, { "SpotSoftness", c->SpotSoftness },
                             { "Flicker", c->Flicker }, { "FlickerSpeed", c->FlickerSpeed } };
        if (auto* c = entity.TryGetComponent<Rigidbody2DComponent>())
        {
            e["Rigidbody2D"] = { { "BodyType", BodyTypeToString(c->Type) }, { "FixedRotation", c->FixedRotation },
                                 { "GravityScale", c->GravityScale }, { "LinearDamping", c->LinearDamping },
                                 { "AngularDamping", c->AngularDamping }, { "Bullet", c->Bullet } };
            if (c->Layer != 0)
                e["Rigidbody2D"]["Layer"] = c->Layer;
            if (c->Sleeping != Rigidbody2DComponent::SleepMode::StartAwake)
                e["Rigidbody2D"]["Sleeping"] = c->Sleeping == Rigidbody2DComponent::SleepMode::StartAsleep ? "StartAsleep" : "NeverSleep";
        }
        // The common collider values; the material and an own layer only when set (older files stay as they were).
        auto writeCollider = [](const auto& c) {
            json j = { { "Offset", ToJson(c.Offset) }, { "Density", c.Density }, { "Friction", c.Friction }, { "Restitution", c.Restitution },
                       { "RestitutionThreshold", c.RestitutionThreshold }, { "IsSensor", c.IsSensor } };
            if (!c.MaterialPath.empty())
            {
                j["Material"] = c.MaterialPath;
                WriteAssetHandle(j, "Material", c.MaterialPath);
            }
            if (c.Layer >= 0)
                j["Layer"] = c.Layer;
            return j;
        };
        auto writePoints = [](const std::vector<glm::vec2>& points) {
            json list = json::array();
            for (const glm::vec2& p : points)
                list.push_back(ToJson(p));
            return list;
        };
        if (auto* c = entity.TryGetComponent<BoxCollider2DComponent>())
        {
            e["BoxCollider2D"] = writeCollider(*c);
            e["BoxCollider2D"]["Size"] = ToJson(c->Size);
            if (c->OneWay)
                e["BoxCollider2D"]["OneWay"] = true;
        }
        if (auto* c = entity.TryGetComponent<CircleCollider2DComponent>())
        {
            e["CircleCollider2D"] = writeCollider(*c);
            e["CircleCollider2D"]["Radius"] = c->Radius;
        }
        if (auto* c = entity.TryGetComponent<PolygonCollider2DComponent>())
        {
            e["PolygonCollider2D"] = writeCollider(*c);
            e["PolygonCollider2D"]["Points"] = writePoints(c->Points);
        }
        if (auto* c = entity.TryGetComponent<EdgeCollider2DComponent>())
        {
            json j = writeCollider(*c);
            j["Points"] = writePoints(c->Points);
            j["Loop"] = c->Loop;
            j["OneSided"] = c->OneSided;
            e["EdgeCollider2D"] = std::move(j);
        }
        if (auto* c = entity.TryGetComponent<PlatformEffector2DComponent>())
            e["PlatformEffector2D"] = { { "OneWay", c->OneWay }, { "SurfaceArc", c->SurfaceArc }, { "RotationOffset", c->RotationOffset },
                                        { "UseSideFriction", c->UseSideFriction }, { "UseSideBounce", c->UseSideBounce },
                                        { "SideArc", c->SideArc } };
        if (auto* c = entity.TryGetComponent<AreaEffector2DComponent>())
            e["AreaEffector2D"] = { { "ForceAngle", c->ForceAngle }, { "UseGlobalAngle", c->UseGlobalAngle }, { "ForceMagnitude", c->ForceMagnitude },
                                    { "ForceVariation", c->ForceVariation }, { "ScaleWithMass", c->ScaleWithMass }, { "Drag", c->Drag },
                                    { "AngularDrag", c->AngularDrag }, { "ColliderMask", c->ColliderMask } };
        if (auto* c = entity.TryGetComponent<BuoyancyEffector2DComponent>())
            e["BuoyancyEffector2D"] = { { "SurfaceLevel", c->SurfaceLevel }, { "Density", c->Density }, { "LinearDrag", c->LinearDrag },
                                        { "AngularDrag", c->AngularDrag }, { "FlowAngle", c->FlowAngle }, { "FlowMagnitude", c->FlowMagnitude },
                                        { "FlowVariation", c->FlowVariation }, { "ColliderMask", c->ColliderMask } };
        // Joints: the connected body by its UUID (0 = a fixed point in the world).
        auto writeJoint = [](const Joint2DComponentBase& c) {
            return json{ { "ConnectedBody", c.ConnectedBody.ID }, { "Anchor", ToJson(c.Anchor) }, { "ConnectedAnchor", ToJson(c.ConnectedAnchor) },
                         { "AutoConfigureConnectedAnchor", c.AutoConfigureConnectedAnchor }, { "CollideConnected", c.CollideConnected },
                         { "BreakForce", c.BreakForce }, { "BreakTorque", c.BreakTorque } };
        };
        if (auto* c = entity.TryGetComponent<DistanceJoint2DComponent>())
        {
            json j = writeJoint(*c);
            j["AutoConfigureDistance"] = c->AutoConfigureDistance;
            j["Distance"] = c->Distance;
            j["MaxDistanceOnly"] = c->MaxDistanceOnly;
            j["Frequency"] = c->Frequency;
            j["DampingRatio"] = c->DampingRatio;
            e["DistanceJoint2D"] = std::move(j);
        }
        if (auto* c = entity.TryGetComponent<RevoluteJoint2DComponent>())
        {
            json j = writeJoint(*c);
            j["EnableLimit"] = c->EnableLimit;
            j["LowerAngle"] = c->LowerAngle;
            j["UpperAngle"] = c->UpperAngle;
            j["EnableMotor"] = c->EnableMotor;
            j["MotorSpeed"] = c->MotorSpeed;
            j["MaxMotorTorque"] = c->MaxMotorTorque;
            e["RevoluteJoint2D"] = std::move(j);
        }
        if (auto* c = entity.TryGetComponent<PrismaticJoint2DComponent>())
        {
            json j = writeJoint(*c);
            j["Axis"] = ToJson(c->Axis);
            j["EnableLimit"] = c->EnableLimit;
            j["LowerTranslation"] = c->LowerTranslation;
            j["UpperTranslation"] = c->UpperTranslation;
            j["EnableMotor"] = c->EnableMotor;
            j["MotorSpeed"] = c->MotorSpeed;
            j["MaxMotorForce"] = c->MaxMotorForce;
            e["PrismaticJoint2D"] = std::move(j);
        }
        if (auto* c = entity.TryGetComponent<WeldJoint2DComponent>())
        {
            json j = writeJoint(*c);
            j["Frequency"] = c->Frequency;
            j["DampingRatio"] = c->DampingRatio;
            e["WeldJoint2D"] = std::move(j);
        }
        if (auto* c = entity.TryGetComponent<WheelJoint2DComponent>())
        {
            json j = writeJoint(*c);
            j["Axis"] = ToJson(c->Axis);
            j["Frequency"] = c->Frequency;
            j["DampingRatio"] = c->DampingRatio;
            j["EnableLimit"] = c->EnableLimit;
            j["LowerTranslation"] = c->LowerTranslation;
            j["UpperTranslation"] = c->UpperTranslation;
            j["EnableMotor"] = c->EnableMotor;
            j["MotorSpeed"] = c->MotorSpeed;
            j["MaxMotorTorque"] = c->MaxMotorTorque;
            e["WheelJoint2D"] = std::move(j);
        }
        if (auto* c = entity.TryGetComponent<Rigidbody3DComponent>())
            e["Rigidbody3D"] = { { "BodyType", BodyType3DToString(c->Type) }, { "Mass", c->Mass }, { "LinearDamping", c->LinearDamping },
                                 { "AngularDamping", c->AngularDamping }, { "GravityFactor", c->GravityFactor }, { "LockRotation", c->LockRotation } };
        if (auto* c = entity.TryGetComponent<Rigidbody3DComponent>())
        {
            // Left out when off / the defaults (older files stay as they were).
            json& j = e["Rigidbody3D"];
            if (c->Layer != 0)
                j["Layer"] = c->Layer;
            if (c->ContinuousCollision)
                j["ContinuousCollision"] = true;
            if (c->Sleeping != Rigidbody3DComponent::SleepMode::StartAwake)
                j["Sleeping"] = c->Sleeping == Rigidbody3DComponent::SleepMode::StartAsleep ? "StartAsleep" : "NeverSleep";
        }
        // A 3D collider's physics material: its path (and the asset's UUID) when it has one.
        auto writeMaterial3D = [](json& j, const std::string& material) {
            if (material.empty())
                return;
            j["Material"] = material;
            WriteAssetHandle(j, "Material", material);
        };
        if (auto* c = entity.TryGetComponent<BoxCollider3DComponent>())
        {
            e["BoxCollider3D"] = { { "HalfExtents", ToJson(c->HalfExtents) }, { "Offset", ToJson(c->Offset) }, { "Friction", c->Friction },
                                   { "Restitution", c->Restitution }, { "IsSensor", c->IsSensor } };
            writeMaterial3D(e["BoxCollider3D"], c->MaterialPath);
        }
        if (auto* c = entity.TryGetComponent<SphereCollider3DComponent>())
        {
            e["SphereCollider3D"] = { { "Radius", c->Radius }, { "Offset", ToJson(c->Offset) }, { "Friction", c->Friction },
                                      { "Restitution", c->Restitution }, { "IsSensor", c->IsSensor } };
            writeMaterial3D(e["SphereCollider3D"], c->MaterialPath);
        }
        if (auto* c = entity.TryGetComponent<CapsuleCollider3DComponent>())
        {
            e["CapsuleCollider3D"] = { { "Radius", c->Radius }, { "HalfHeight", c->HalfHeight }, { "Offset", ToJson(c->Offset) },
                                       { "Friction", c->Friction }, { "Restitution", c->Restitution }, { "IsSensor", c->IsSensor } };
            writeMaterial3D(e["CapsuleCollider3D"], c->MaterialPath);
        }
        if (auto* c = entity.TryGetComponent<ConvexHullCollider3DComponent>())
        {
            json& j = e["ConvexHullCollider3D"];
            j = { { "Model", c->ModelPath }, { "MaxVertices", c->MaxVertices }, { "Offset", ToJson(c->Offset) }, { "Friction", c->Friction },
                  { "Restitution", c->Restitution }, { "IsSensor", c->IsSensor } };
            WriteAssetHandle(j, "Model", c->ModelPath);
            writeMaterial3D(j, c->MaterialPath);
        }
        if (auto* c = entity.TryGetComponent<MeshCollider3DComponent>())
        {
            json& j = e["MeshCollider3D"];
            j = { { "Model", c->ModelPath }, { "Offset", ToJson(c->Offset) }, { "Friction", c->Friction }, { "Restitution", c->Restitution },
                  { "IsSensor", c->IsSensor } };
            WriteAssetHandle(j, "Model", c->ModelPath);
            writeMaterial3D(j, c->MaterialPath);
        }
        // 3D joints: the connected body by its UUID (0 = a fixed point in the world).
        auto writeJoint3D = [](const Joint3DComponentBase& c) {
            return json{ { "ConnectedBody", c.ConnectedBody.ID }, { "Anchor", ToJson(c.Anchor) }, { "ConnectedAnchor", ToJson(c.ConnectedAnchor) },
                         { "AutoConfigureConnectedAnchor", c.AutoConfigureConnectedAnchor }, { "CollideConnected", c.CollideConnected },
                         { "BreakForce", c.BreakForce }, { "BreakTorque", c.BreakTorque } };
        };
        if (auto* c = entity.TryGetComponent<FixedJoint3DComponent>())
            e["FixedJoint3D"] = writeJoint3D(*c);
        if (auto* c = entity.TryGetComponent<HingeJoint3DComponent>())
        {
            json j = writeJoint3D(*c);
            j["Axis"] = ToJson(c->Axis);
            j["EnableLimit"] = c->EnableLimit;
            j["LowerAngle"] = c->LowerAngle;
            j["UpperAngle"] = c->UpperAngle;
            j["EnableMotor"] = c->EnableMotor;
            j["MotorSpeed"] = c->MotorSpeed;
            j["MaxMotorTorque"] = c->MaxMotorTorque;
            j["EnableSpring"] = c->EnableSpring;
            j["SpringTargetAngle"] = c->SpringTargetAngle;
            j["SpringFrequency"] = c->SpringFrequency;
            j["SpringDamping"] = c->SpringDamping;
            e["HingeJoint3D"] = std::move(j);
        }
        if (auto* c = entity.TryGetComponent<DistanceJoint3DComponent>())
        {
            json j = writeJoint3D(*c);
            j["AutoConfigureDistance"] = c->AutoConfigureDistance;
            j["MinDistance"] = c->MinDistance;
            j["MaxDistance"] = c->MaxDistance;
            j["MaxDistanceOnly"] = c->MaxDistanceOnly;
            j["Frequency"] = c->Frequency;
            j["DampingRatio"] = c->DampingRatio;
            e["DistanceJoint3D"] = std::move(j);
        }
        if (auto* c = entity.TryGetComponent<SliderJoint3DComponent>())
        {
            json j = writeJoint3D(*c);
            j["Axis"] = ToJson(c->Axis);
            j["EnableLimit"] = c->EnableLimit;
            j["LowerTranslation"] = c->LowerTranslation;
            j["UpperTranslation"] = c->UpperTranslation;
            j["EnableMotor"] = c->EnableMotor;
            j["MotorSpeed"] = c->MotorSpeed;
            j["MaxMotorForce"] = c->MaxMotorForce;
            e["SliderJoint3D"] = std::move(j);
        }
        if (auto* c = entity.TryGetComponent<ConeJoint3DComponent>())
        {
            json j = writeJoint3D(*c);
            j["Axis"] = ToJson(c->Axis);
            j["SwingLimit"] = c->SwingLimit;
            j["TwistLower"] = c->TwistLower;
            j["TwistUpper"] = c->TwistUpper;
            j["FrictionTorque"] = c->FrictionTorque;
            e["ConeJoint3D"] = std::move(j);
        }
        if (auto* c = entity.TryGetComponent<CharacterController3DComponent>())
            e["CharacterController3D"] = { { "Radius", c->Radius }, { "Height", c->Height }, { "MaxSlope", c->MaxSlope },
                                           { "StepHeight", c->StepHeight }, { "Mass", c->Mass }, { "GravityScale", c->GravityScale },
                                           { "StickToFloor", c->StickToFloor } };
        if (auto* c = entity.TryGetComponent<CharacterController3DComponent>())
        {
            json& j = e["CharacterController3D"];
            if (c->Layer != 0)
                j["Layer"] = c->Layer;
            if (!c->PushBodies)
                j["PushBodies"] = false;
            if (c->PushForce != CharacterController3DComponent().PushForce)
                j["PushForce"] = c->PushForce;
        }
        if (auto* c = entity.TryGetComponent<TilemapComponent>())
        {
            c->EnsureSize();
            json layers = json::array();
            for (const TilemapLayer& layer : c->Layers)
                layers.push_back({ { "Name", layer.Name }, { "Visible", layer.Visible }, { "Collision", layer.Collision },
                                   { "SortingOrder", layer.SortingOrder }, { "Opacity", layer.Opacity }, { "Tiles", layer.Tiles } });
            e["Tilemap"] = { { "Tileset", c->TilesetPath }, { "PixelArt", c->PixelArt }, { "TilesetColumns", c->TilesetColumns },
                             { "TilesetRows", c->TilesetRows }, { "Width", c->Width }, { "Height", c->Height }, { "TileSize", c->TileSize },
                             { "Layers", std::move(layers) }, { "Color", ToJson(c->Color) }, { "SortingOrder", c->SortingOrder },
                             { "Collision", c->Collision }, { "Friction", c->Friction } };
            WriteAssetHandle(e["Tilemap"], "Tileset", c->TilesetPath);
            if (c->Restitution != 0.0f)
                e["Tilemap"]["Restitution"] = c->Restitution;
            if (!c->MaterialPath.empty())
            {
                e["Tilemap"]["Material"] = c->MaterialPath;
                WriteAssetHandle(e["Tilemap"], "Material", c->MaterialPath);
            }
            if (c->CollisionLayer != 0)
                e["Tilemap"]["CollisionLayer"] = c->CollisionLayer;
            if (c->Lit)
                e["Tilemap"]["Lit"] = true;
            WriteSortingLayer(e["Tilemap"], c->SortingLayer);
        }
        if (auto* c = entity.TryGetComponent<ParallaxComponent>())
            e["Parallax"] = { { "Factor", ToJson(c->Factor) }, { "RepeatX", c->RepeatX }, { "RepeatY", c->RepeatY },
                              { "Spacing", ToJson(c->Spacing) } };
        if (auto* c = entity.TryGetComponent<Camera2DFollowComponent>())
            e["Camera2DFollow"] = { { "Target", c->Target.ID }, { "Offset", ToJson(c->Offset) }, { "Damping", ToJson(c->Damping) },
                                    { "DeadZone", ToJson(c->DeadZone) }, { "LookAhead", ToJson(c->LookAhead) },
                                    { "LookAheadDamping", c->LookAheadDamping }, { "LookAheadMinSpeed", c->LookAheadMinSpeed },
                                    { "UseBounds", c->UseBounds }, { "BoundsMin", ToJson(c->BoundsMin) }, { "BoundsMax", ToJson(c->BoundsMax) },
                                    { "SnapOnStart", c->SnapOnStart } };
        if (auto* c = entity.TryGetComponent<SplineComponent>())
            e["Spline"] = SplineToJson(*c);
        if (auto* c = entity.TryGetComponent<SplineFollowerComponent>())
            e["SplineFollower"] = { { "Path", c->Path.ID }, { "Speed", c->Speed }, { "Mode", EnumName(c->Mode, s_SplineFollowModes) },
                                    { "StartPosition", c->StartPosition }, { "Rotation", EnumName(c->Rotation, s_SplineRotations) },
                                    { "Offset", ToJson(c->Offset) }, { "PlayOnStart", c->PlayOnStart } };
        if (auto* c = entity.TryGetComponent<ParticleSystemComponent>())
            e["ParticleSystem"] = ParticleSystemToJson(*c);
        if (auto* c = entity.TryGetComponent<UITransformComponent>())
        {
            e["UITransform"] = { { "AnchorMin", ToJson(c->AnchorMin) }, { "AnchorMax", ToJson(c->AnchorMax) }, { "Pivot", ToJson(c->Pivot) },
                                 { "Position", ToJson(c->Position) }, { "Size", ToJson(c->Size) }, { "FontSize", c->FontSize }, { "Order", c->Order } };
            // The look only when it is not the plain one.
            if (c->Scale != glm::vec2(1.0f))
                e["UITransform"]["Scale"] = ToJson(c->Scale);
            if (c->VisualOffset != glm::vec2(0.0f))
                e["UITransform"]["VisualOffset"] = ToJson(c->VisualOffset);
            if (c->Alpha != 1.0f)
                e["UITransform"]["Alpha"] = c->Alpha;
        }
        if (auto* c = entity.TryGetComponent<UIAnimationComponent>())
            e["UIAnimation"] = { { "Show", EnumName(c->Show, s_UIAnimPresets) }, { "Hide", EnumName(c->Hide, s_UIAnimPresets) },
                                 { "ShowAtStart", c->ShowAtStart }, { "Duration", c->Duration }, { "ShowDelay", c->ShowDelay },
                                 { "ShowEase", EaseToString(c->ShowEase) }, { "HideEase", EaseToString(c->HideEase) },
                                 { "SlideDistance", c->SlideDistance }, { "HoverScale", c->HoverScale }, { "PressScale", c->PressScale },
                                 { "FeedbackDuration", c->FeedbackDuration } };
        if (auto* c = entity.TryGetComponent<UICanvasComponent>())
            e["UICanvas"] = { { "ScaleMode", EnumName(c->ScaleMode, s_UIScaleModes) }, { "ScaleFactor", c->ScaleFactor },
                              { "ReferenceResolution", ToJson(c->ReferenceResolution) }, { "MatchMode", EnumName(c->MatchMode, s_UIScreenMatchModes) },
                              { "Match", c->MatchWidthOrHeight } };
        if (auto* c = entity.TryGetComponent<UILayoutGroupComponent>())
        {
            json& j = e["UILayoutGroup"];
            j = { { "Type", EnumName(c->Type, s_UILayoutTypes) }, { "Padding", ToJson(c->Padding) }, { "Alignment", c->ChildAlignment },
                  { "FitWidth", c->FitWidth }, { "FitHeight", c->FitHeight } };
            if (c->Type == UILayoutType::Grid)
            {
                j["CellSize"] = ToJson(c->CellSize);
                j["GridSpacing"] = ToJson(c->GridSpacing);
                j["StartCorner"] = c->StartCorner;
                j["StartAxisVertical"] = c->StartAxisVertical;
                j["Constraint"] = EnumName(c->Constraint, s_UIGridConstraints);
                j["ConstraintCount"] = c->ConstraintCount;
            }
            else
            {
                j["Spacing"] = c->Spacing;
                j["Reverse"] = c->ReverseArrangement;
                j["ControlWidth"] = c->ControlChildWidth;
                j["ControlHeight"] = c->ControlChildHeight;
                j["ExpandWidth"] = c->ChildForceExpandWidth;
                j["ExpandHeight"] = c->ChildForceExpandHeight;
            }
        }
        if (auto* c = entity.TryGetComponent<UILayoutElementComponent>())
            e["UILayoutElement"] = { { "IgnoreLayout", c->IgnoreLayout }, { "Min", ToJson(c->MinSize) }, { "Preferred", ToJson(c->PreferredSize) },
                                     { "Flexible", ToJson(c->FlexibleSize) } };
        if (auto* c = entity.TryGetComponent<UINavigationComponent>())
            e["UINavigation"] = { { "Mode", EnumName(c->NavigationMode, s_UINavigationModes) }, { "Up", c->Up.ID }, { "Down", c->Down.ID },
                                  { "Left", c->Left.ID }, { "Right", c->Right.ID }, { "SelectOnEnable", c->SelectOnEnable },
                                  { "ShowHighlight", c->ShowHighlight }, { "HighlightColor", ToJson(c->HighlightColor) } };
        if (auto* c = entity.TryGetComponent<UIButtonComponent>())
        {
            e["UIButton"] = { { "Normal", ToJson(c->NormalColor) }, { "Hover", ToJson(c->HoverColor) }, { "Pressed", ToJson(c->PressedColor) },
                              { "Interactable", c->Interactable } };
            if (!c->ClickMessage.empty())
            {
                e["UIButton"]["ClickMessage"] = c->ClickMessage;
                e["UIButton"]["ClickTarget"] = c->ClickTarget.ID;
            }
        }
        if (auto* c = entity.TryGetComponent<UIImageComponent>())
        {
            json& j = e["UIImage"];
            j = { { "Color", ToJson(c->Color) }, { "Texture", c->TexturePath }, { "Type", EnumName(c->Type, s_UIImageTypes) },
                  { "PreserveAspect", c->PreserveAspect }, { "PixelArt", c->PixelArt }, { "RaycastTarget", c->RaycastTarget } };
            WriteAssetHandle(j, "Texture", c->TexturePath);
            if (!c->SpriteName.empty())
                j["Sprite"] = c->SpriteName;
            if (c->HasSliceBorder())
                j["SliceBorder"] = ToJson(c->SliceBorder);
            if (c->BorderScale != 1.0f)
                j["BorderScale"] = c->BorderScale;
            if (c->Type == UIImageType::Filled)
                j["Fill"] = { { "Method", EnumName(c->FillMethod, s_UIFillMethods) }, { "Origin", c->FillOrigin },
                              { "Amount", c->FillAmount }, { "Clockwise", c->FillClockwise } };
        }
        if (auto* c = entity.TryGetComponent<UISliderComponent>())
            e["UISlider"] = { { "Min", c->MinValue }, { "Max", c->MaxValue }, { "Value", c->Value }, { "WholeNumbers", c->WholeNumbers },
                              { "Direction", EnumName(c->Direction, s_UIDirections) }, { "Interactable", c->Interactable },
                              { "TrackThickness", c->TrackThickness }, { "HandleSize", c->HandleSize }, { "Rounded", c->Rounded },
                              { "TrackColor", ToJson(c->TrackColor) }, { "FillColor", ToJson(c->FillColor) },
                              { "HandleColor", ToJson(c->HandleColor) } };
        if (auto* c = entity.TryGetComponent<UIToggleComponent>())
            e["UIToggle"] = { { "IsOn", c->IsOn }, { "Interactable", c->Interactable }, { "Style", EnumName(c->Style, s_UIToggleStyles) },
                              { "Mark", EnumName(c->Mark, s_UIToggleMarks) }, { "BoxSize", c->BoxSize }, { "LabelSpacing", c->LabelSpacing },
                              { "Group", c->Group }, { "AllowSwitchOff", c->AllowSwitchOff }, { "BoxColor", ToJson(c->BoxColor) },
                              { "OnColor", ToJson(c->OnColor) }, { "MarkColor", ToJson(c->MarkColor) } };
        if (auto* c = entity.TryGetComponent<UIProgressBarComponent>())
            e["UIProgressBar"] = { { "Value", c->Value }, { "Direction", EnumName(c->Direction, s_UIDirections) },
                                   { "BackgroundColor", ToJson(c->BackgroundColor) }, { "FillColor", ToJson(c->FillColor) },
                                   { "UseLowColor", c->UseLowColor }, { "LowColor", ToJson(c->LowColor) }, { "Padding", c->Padding },
                                   { "SmoothSpeed", c->SmoothSpeed }, { "ShowPercentage", c->ShowPercentage },
                                   { "TextColor", ToJson(c->TextColor) } };
        if (auto* c = entity.TryGetComponent<UIInputFieldComponent>())
        {
            json& j = e["UIInputField"];
            j = { { "Text", c->Text }, { "Placeholder", c->Placeholder }, { "ContentType", EnumName(c->ContentType, s_UIContentTypes) },
                  { "CharacterLimit", c->CharacterLimit }, { "Interactable", c->Interactable }, { "ReadOnly", c->ReadOnly },
                  { "SelectAllOnFocus", c->SelectAllOnFocus }, { "Font", c->FontPath }, { "Padding", c->Padding },
                  { "BackgroundColor", ToJson(c->BackgroundColor) }, { "OutlineColor", ToJson(c->OutlineColor) },
                  { "FocusColor", ToJson(c->FocusColor) }, { "TextColor", ToJson(c->TextColor) },
                  { "PlaceholderColor", ToJson(c->PlaceholderColor) }, { "CaretColor", ToJson(c->CaretColor) },
                  { "SelectionColor", ToJson(c->SelectionColor) }, { "CaretBlinkRate", c->CaretBlinkRate } };
            WriteAssetHandle(j, "Font", c->FontPath);
        }
        if (auto* c = entity.TryGetComponent<UIMaskComponent>())
            e["UIMask"] = { { "ShowGraphic", c->ShowGraphic }, { "Padding", ToJson(c->Padding) } };
        if (auto* c = entity.TryGetComponent<UIScrollViewComponent>())
            e["UIScrollView"] = { { "Content", c->Content.ID }, { "Viewport", c->Viewport.ID }, { "Horizontal", c->Horizontal },
                                  { "Vertical", c->Vertical }, { "Movement", EnumName(c->Movement, s_UIScrollMovements) },
                                  { "Elasticity", c->Elasticity }, { "Inertia", c->Inertia }, { "DecelerationRate", c->DecelerationRate },
                                  { "ScrollSensitivity", c->ScrollSensitivity }, { "Draggable", c->Draggable },
                                  { "Scrollbars", EnumName(c->Scrollbars, s_UIScrollbarVisibilities) }, { "ScrollbarWidth", c->ScrollbarWidth },
                                  { "ScrollbarInset", c->ScrollbarInset }, { "TrackColor", ToJson(c->TrackColor) },
                                  { "HandleColor", ToJson(c->HandleColor) } };
        if (auto* c = entity.TryGetComponent<AudioSourceComponent>())
        {
            e["AudioSource"] = { { "Path", c->Path }, { "Volume", c->Volume }, { "Pitch", c->Pitch }, { "Loop", c->Loop }, { "PlayOnStart", c->PlayOnStart },
                                 { "Spatial", c->Spatial }, { "MinDistance", c->MinDistance }, { "MaxDistance", c->MaxDistance },
                                 { "Output", c->Output } };
            json& a = e["AudioSource"];
            // Only what differs from the defaults: older files stay the same.
            if (c->Stream)
                a["Stream"] = true;
            if (c->VolumeRandom != glm::vec2(1.0f))
                a["VolumeRandom"] = ToJson(c->VolumeRandom);
            if (c->PitchRandom != glm::vec2(1.0f))
                a["PitchRandom"] = ToJson(c->PitchRandom);
            if (c->Priority != 128)
                a["Priority"] = c->Priority;
            if (c->MaxInstances != 0)
                a["MaxInstances"] = c->MaxInstances;
            if (c->Steal != AudioStealPolicy::Oldest)
                a["Steal"] = c->Steal == AudioStealPolicy::Quietest ? "Quietest" : "None";
            if (c->DopplerFactor != 1.0f)
                a["Doppler"] = c->DopplerFactor;
            if (c->FadeIn > 0.0f)
                a["FadeIn"] = c->FadeIn;
            if (c->FadeOut > 0.0f)
                a["FadeOut"] = c->FadeOut;
            if (!c->Filter.IsDefault())
                a["Filter"] = { { "LowPass", c->Filter.LowPass }, { "LowPassCutoff", c->Filter.LowPassCutoff },
                                { "HighPass", c->Filter.HighPass }, { "HighPassCutoff", c->Filter.HighPassCutoff } };
            if (!c->Cone.IsDefault())
                a["Cone"] = { { "Enabled", c->Cone.Enabled }, { "InnerAngle", c->Cone.InnerAngle }, { "OuterAngle", c->Cone.OuterAngle },
                              { "OuterGain", c->Cone.OuterGain } };
            WriteAssetHandle(a, "Path", c->Path);
        }
        if (auto* c = entity.TryGetComponent<AudioListenerComponent>())
            e["AudioListener"] = { { "Enabled", c->Enabled }, { "Priority", c->Priority }, { "TrackVelocity", c->TrackVelocity } };
        if (auto* c = entity.TryGetComponent<AmbientZoneComponent>())
        {
            json& z = e["AmbientZone"];
            z = { { "Path", c->Path }, { "Volume", c->Volume }, { "Pitch", c->Pitch }, { "Output", c->Output },
                  { "Shape", c->Shape == AmbientZoneComponent::ZoneShape::Sphere ? "Sphere" : "Box" }, { "BlendDistance", c->BlendDistance },
                  { "FadeTime", c->FadeTime } };
            // Both sizes: the other shape's is kept too (switched back, it has its size again).
            z["Size"] = ToJson(c->Size);
            z["Radius"] = c->Radius;
            WriteAssetHandle(z, "Path", c->Path);
            if (c->Stream)
                z["Stream"] = true;
            if (c->Priority != 128)
                z["Priority"] = c->Priority;
        }
        if (auto* c = entity.TryGetComponent<NativeScriptComponent>())
        {
            json fields = json::object();
            for (const auto& [name, value] : c->Fields)
                fields[name] = SerializeFieldValue(value);
            e["NativeScript"] = { { "Class", c->ClassName }, { "Fields", fields } };
            if (!c->Enabled)
                e["NativeScript"]["Enabled"] = false; // written only when off: older files stay unchanged
        }
        if (auto* c = entity.TryGetComponent<EditorMetadataComponent>())
        {
            // Editor-only data, written only when something is set (other files stay unchanged).
            json m = json::object();
            if (c->Hidden && includeEditorFlags) m["Hidden"] = true;
            if (c->Locked && includeEditorFlags) m["Locked"] = true;
            if (!c->ComponentOrder.empty()) m["ComponentOrder"] = c->ComponentOrder;
            if (!c->Prefab.empty() && includeEditorFlags) m["Prefab"] = c->Prefab;
            if (!m.empty())
                e["Editor"] = std::move(m);
        }
        return e;
    }

    static void DeserializeEntityComponents(const json& e, Entity entity)
    {
        // Missing = active. A hand-edited 0 / 1 is accepted too; anything else keeps the default.
        if (auto it = e.find("Active"); it != e.end())
        {
            if (it->is_boolean())
                entity.GetComponent<TagComponent>().Active = it->get<bool>();
            else if (it->is_number())
                entity.GetComponent<TagComponent>().Active = it->get<double>() != 0.0;
        }
        if (e.contains("Transform"))
        {
            const json& j = e["Transform"];
            auto& t = entity.GetComponent<TransformComponent>();
            t.Translation = Vec(j, "Translation", glm::vec3(0.0f));
            t.Rotation = Vec(j, "Rotation", glm::vec3(0.0f));
            t.Scale = Vec(j, "Scale", glm::vec3(1.0f));
        }
        if (e.contains("Tags") && e["Tags"].is_array())
        {
            auto& c = entity.AddOrReplaceComponent<TagsComponent>();
            c.Tags.clear();
            for (const json& tag : e["Tags"])
                if (tag.is_string())
                    c.Add(tag.get<std::string>()); // skips empty / duplicate entries
        }
        if (e.contains("Camera"))
        {
            const json& j = e["Camera"];
            auto& c = entity.AddComponent<CameraComponent>();
            c.Camera.SetPerspectiveVerticalFOV(j.value("PerspectiveFOV", 0.7854f));
            c.Camera.SetPerspectiveNearClip(j.value("PerspectiveNear", 0.1f));
            c.Camera.SetPerspectiveFarClip(j.value("PerspectiveFar", 1000.0f));
            c.Camera.SetOrthographicSize(j.value("OrthographicSize", 10.0f));
            c.Camera.SetOrthographicNearClip(j.value("OrthographicNear", -100.0f));
            c.Camera.SetOrthographicFarClip(j.value("OrthographicFar", 100.0f));
            c.Camera.SetProjectionType((SceneCamera::ProjectionType)j.value("ProjectionType", 0));
            c.Primary = j.value("Primary", true);
            c.FixedAspectRatio = j.value("FixedAspectRatio", false);
            c.ClearColor = Vec(j, "ClearColor", c.ClearColor);
            if (j.contains("Shake") && j["Shake"].is_object())
            {
                const json& shake = j["Shake"];
                c.Shake.MaxOffset = glm::max(Vec(shake, "MaxOffset", c.Shake.MaxOffset), glm::vec2(0.0f));
                c.Shake.MaxAngle = std::max(0.0f, shake.value("MaxAngle", c.Shake.MaxAngle));
                c.Shake.Exponent = std::clamp(shake.value("Exponent", c.Shake.Exponent), 0.1f, 8.0f);
            }
            if (j.contains("PixelPerfect") && j["PixelPerfect"].is_object())
            {
                const json& pj = j["PixelPerfect"];
                PixelPerfectSettings& pp = c.PixelPerfect;
                pp.Enabled = pj.value("Enabled", false);
                pp.PixelsPerUnit = pj.value("PixelsPerUnit", pp.PixelsPerUnit);
                if (auto ref = pj.find("ReferenceResolution"); ref != pj.end() && ref->is_array() && ref->size() == 2 &&
                    (*ref)[0].is_number() && (*ref)[1].is_number())
                    pp.ReferenceResolution = { (*ref)[0].get<int>(), (*ref)[1].get<int>() };
                pp.UpscaleRenderTarget = pj.value("Upscale", pp.UpscaleRenderTarget);
                pp.SnapCamera = pj.value("SnapCamera", pp.SnapCamera);
                pp.SnapSprites = pj.value("SnapSprites", pp.SnapSprites);
                PixelPerfect::Sanitize(pp);
            }
        }
        if (e.contains("SpriteRenderer"))
        {
            const json& j = e["SpriteRenderer"];
            auto& c = entity.AddComponent<SpriteRendererComponent>();
            c.Color = Vec(j, "Color", glm::vec4(1.0f));
            c.TexturePath = ReadAssetPath(j, "Texture");
            c.TilingFactor = j.value("Tiling", 1.0f);
            c.SortingOrder = j.value("SortingOrder", 0);
            c.PixelArt = j.value("PixelArt", false);
            c.FlipX = j.value("FlipX", false);
            c.FlipY = j.value("FlipY", false);
            c.SpriteName = j.value("Sprite", std::string());
            c.DrawMode = SpriteDrawModeFromString(j.value("DrawMode", std::string("Simple")));
            c.Size = glm::max(Vec(j, "Size", glm::vec2(1.0f)), glm::vec2(0.0f));
            c.SliceBorder = glm::max(Vec(j, "SliceBorder", glm::vec4(0.0f)), glm::vec4(0.0f));
            c.PixelsPerUnit = std::max(0.001f, j.value("PixelsPerUnit", 100.0f));
            if (auto flash = j.find("Flash"); flash != j.end() && flash->is_object())
            {
                c.FlashColor = Vec(*flash, "Color", glm::vec4(1.0f));
                c.FlashAmount = std::clamp(flash->value("Amount", 0.0f), 0.0f, 1.0f);
            }
            if (auto outline = j.find("Outline"); outline != j.end() && outline->is_object())
            {
                c.OutlineColor = Vec(*outline, "Color", glm::vec4(1.0f));
                c.OutlineThickness = std::clamp(outline->value("Thickness", 0.0f), 0.0f, 64.0f);
            }
            c.Lit = j.value("Lit", false);
            c.SortingLayer = ReadSortingLayer(j);
        }
        if (e.contains("SpriteAnimation"))
        {
            const json& j = e["SpriteAnimation"];
            auto& c = entity.AddComponent<SpriteAnimationComponent>();
            c.Columns = j.value("Columns", 4);
            c.Rows = j.value("Rows", 1);
            c.StartFrame = j.value("StartFrame", 0);
            c.FrameCount = j.value("FrameCount", 4);
            c.FPS = j.value("FPS", 10.0f);
            c.Loop = j.value("Loop", true);
            c.Playing = j.value("Playing", true);
            if (auto it = j.find("Frames"); it != j.end() && it->is_array())
                for (const json& name : *it)
                    if (name.is_string())
                        c.Frames.push_back(name.get<std::string>());
            if (auto clips = j.find("Clips"); clips != j.end() && clips->is_array())
                for (const json& clip : *clips)
                {
                    if (clip.is_string())
                        c.Clips.push_back(clip.get<std::string>());
                    else if (clip.is_object())
                        c.Clips.push_back(ReadAssetPath(clip, "Path"));
                }
            c.PlayOnStart = j.value("PlayOnStart", true);
        }
        if (e.contains("TintAnimation"))
        {
            const json& j = e["TintAnimation"];
            auto& c = entity.AddComponent<TintAnimationComponent>();
            if (auto keys = j.find("Keys"); keys != j.end() && keys->is_array())
            {
                c.Keys.clear();
                for (const json& k : *keys)
                    if (k.is_object())
                        c.Keys.push_back({ k.value("Time", 0.0f), Vec(k, "Color", glm::vec4(1.0f)) });
                TintAnimation::SortKeys(c.Keys);
            }
            c.Duration = std::max(0.0f, j.value("Duration", c.Duration));
            if (auto mode = j.find("Mode"); mode != j.end() && mode->is_string())
                c.Mode = TintAnimation::ModeFromString(mode->get<std::string>()).value_or(TintPlayMode::PingPong);
            if (auto ease = j.find("Ease"); ease != j.end() && ease->is_string())
                c.Easing = EaseFromString(ease->get<std::string>()).value_or(Ease::InOutQuad);
            c.Cycles = std::max(0, j.value("Cycles", 0));
            c.StartOffset = std::max(0.0f, j.value("StartOffset", 0.0f));
            c.PlayOnStart = j.value("PlayOnStart", true);
        }
        if (e.contains("CircleRenderer"))
        {
            const json& j = e["CircleRenderer"];
            auto& c = entity.AddComponent<CircleRendererComponent>();
            c.Color = Vec(j, "Color", glm::vec4(1.0f));
            c.Thickness = j.value("Thickness", 1.0f);
            c.Fade = j.value("Fade", 0.005f);
            c.SortingOrder = j.value("SortingOrder", 0);
            c.SortingLayer = ReadSortingLayer(j);
        }
        if (e.contains("Text"))
        {
            const json& j = e["Text"];
            auto& c = entity.AddComponent<TextComponent>();
            c.Text = j.value("Text", "");
            c.Color = Vec(j, "Color", glm::vec4(1.0f));
            c.Kerning = j.value("Kerning", 0.0f);
            c.LineSpacing = j.value("LineSpacing", 0.0f);
            if (j.contains("AlignH") || j.contains("AlignV"))
            {
                c.AlignH = EnumFromJson(j, "AlignH", s_TextAlignH, TextAlignH::Left);
                c.AlignV = EnumFromJson(j, "AlignV", s_TextAlignV, TextAlignV::Middle);
            }
            else
            {
                // Before alignment: "Centered" centred it both ways; otherwise UI text sat at the
                // left, vertically centred, and world text started on its first baseline.
                const bool centered = j.value("Centered", false);
                c.AlignH = centered ? TextAlignH::Center : TextAlignH::Left;
                c.AlignV = centered || e.contains("UITransform") ? TextAlignV::Middle : TextAlignV::Baseline;
            }
            c.WordWrap = j.value("WordWrap", false);
            c.Overflow = EnumFromJson(j, "Overflow", s_TextOverflows, TextOverflow::Overflow);
            c.MaxWidth = j.value("MaxWidth", 0.0f);
            if (auto key = j.find("LocalizationKey"); key != j.end() && key->is_string())
                c.LocalizationKey = key->get<std::string>();
            c.FontPath = ReadAssetPath(j, "Font");
            c.SortingOrder = j.value("SortingOrder", 1000);
            c.SortingLayer = ReadSortingLayer(j);
        }
        if (e.contains("MeshRenderer"))
        {
            const json& j = e["MeshRenderer"];
            auto& c = entity.AddComponent<MeshRendererComponent>();
            c.Type = PrimitiveMeshFromString(j.value("Mesh", "Cube"));
            c.ModelPath = ReadAssetPath(j, "Model");
            c.CastShadows = j.value("CastShadows", true);
            c.Mat.Albedo = Vec(j, "Albedo", glm::vec4(1.0f));
            c.AlbedoPath = ReadAssetPath(j, "AlbedoMap");
            c.NormalPath = ReadAssetPath(j, "NormalMap");
            c.Mat.NormalStrength = j.value("NormalStrength", 1.0f);
            c.MetalRoughPath = ReadAssetPath(j, "MetalRoughMap");
            c.EmissivePath = ReadAssetPath(j, "EmissiveMap");
            c.MaterialPath = ReadAssetPath(j, "Material");
            if (auto slots = j.find("SubmeshMaterials"); slots != j.end() && slots->is_array())
                for (const json& slot : *slots)
                    c.SubmeshMaterials.push_back(slot.is_object() ? ReadAssetPath(slot, "Material") : slot.is_string() ? slot.get<std::string>() : std::string());
            if (auto weights = j.find("MorphWeights"); weights != j.end() && weights->is_array())
                for (const json& w : *weights)
                    c.MorphWeights.push_back(w.is_number() ? w.get<float>() : 0.0f);
            // A single number before tiling became a vec2 (the same on both axes).
            if (auto tiling = j.find("Tiling"); tiling != j.end() && tiling->is_number())
                c.Mat.Tiling = glm::vec2(tiling->get<float>());
            else
                c.Mat.Tiling = Vec(j, "Tiling", glm::vec2(1.0f));
            c.Mat.Offset = Vec(j, "Offset", glm::vec2(0.0f));
            c.Mat.Metallic = j.value("Metallic", 0.0f);
            c.Mat.Roughness = j.value("Roughness", 0.5f);
            c.Mat.Emission = Vec(j, "Emission", glm::vec3(0.0f));
            c.Mat.EmissionStrength = j.value("EmissionStrength", 0.0f);
            if (auto it = j.find("Shader"); it != j.end() && it->is_string())
                c.Mat.Shader = MaterialSerializer::ShaderFromString(it->get<std::string>()).value_or(MaterialShader::Lit);
            if (auto it = j.find("BlendMode"); it != j.end() && it->is_string())
                c.Mat.BlendMode = MaterialSerializer::BlendModeFromString(it->get<std::string>()).value_or(MaterialBlendMode::Opaque);
            else if (c.Mat.Albedo.a < 1.0f)
            {
                // Scenes from before blend modes: a color with alpha below 1 was drawn see-through
                // (the whole surface faded) - it stays that way.
                c.Mat.BlendMode = MaterialBlendMode::Transparent;
                c.Mat.PreserveSpecular = false;
            }
            c.Mat.AlphaCutoff = std::clamp(j.value("AlphaCutoff", 0.5f), 0.0f, 1.0f);
            c.Mat.DoubleSided = j.value("DoubleSided", false);
            c.Mat.PreserveSpecular = j.value("PreserveSpecular", c.Mat.PreserveSpecular);
            if (auto it = j.find("SortPriority"); it != j.end() && it->is_number())
            {
                double priority = it->get<double>();
                c.Mat.SortPriority = std::isfinite(priority) ? (int)std::lround(std::clamp(priority, (double)MinMaterialSortPriority,
                                                                                           (double)MaxMaterialSortPriority))
                                                             : 0;
            }
        }
        if (e.contains("Animator"))
        {
            const json& j = e["Animator"];
            auto& c = entity.AddComponent<AnimatorComponent>();
            c.Clip = j.value("Clip", "");
            c.Speed = j.value("Speed", 1.0f);
            c.Loop = j.value("Loop", true);
            c.Playing = j.value("Playing", true);
            c.PlayInEditor = j.value("PlayInEditor", true);
            c.DefaultFade = j.value("DefaultFade", 0.2f);
            c.DefaultState = j.value("DefaultState", 0);
            for (const json& p : j.value("Parameters", json::array()))
                c.Parameters.push_back({ p.value("Name", ""), (AnimatorParameter::Type)p.value("Type", 0), p.value("Value", 0.0f) });
            for (const json& st : j.value("States", json::array()))
            {
                if (!st.is_object())
                    continue;
                AnimatorState state{ st.value("Name", "State"), st.value("Clip", ""), st.value("Speed", 1.0f), st.value("Loop", true),
                                     st.value("SpeedParameter", "") };
                if (st.value("Type", std::string()) == "BlendTree1D")
                {
                    state.Type = AnimatorStateType::BlendTree1D;
                    state.BlendParameter = st.value("BlendParameter", std::string());
                    if (auto motions = st.find("Motions"); motions != st.end() && motions->is_array())
                        for (const json& m : *motions)
                            if (m.is_object())
                            {
                                AnimatorBlendMotion motion{ m.value("Clip", std::string()), m.value("Threshold", 0.0f), m.value("Speed", 1.0f) };
                                if (!std::isfinite(motion.Threshold)) motion.Threshold = 0.0f;
                                if (!std::isfinite(motion.Speed)) motion.Speed = 1.0f;
                                state.Motions.push_back(std::move(motion));
                            }
                }
                state.GraphPosition = Vec(st, "Graph", glm::vec2(0.0f));
                c.States.push_back(std::move(state));
            }
            c.AnyStateGraphPosition = Vec(j, "AnyStateGraph", glm::vec2(0.0f));
            if (auto layers = j.find("Layers"); layers != j.end() && layers->is_array())
                for (const json& jl : *layers)
                {
                    if (!jl.is_object())
                        continue;
                    AnimatorLayer l;
                    l.Name = jl.value("Name", std::string("Layer"));
                    l.Clip = jl.value("Clip", std::string());
                    l.Speed = jl.value("Speed", 1.0f);
                    if (!std::isfinite(l.Speed)) l.Speed = 1.0f;
                    l.Loop = jl.value("Loop", true);
                    l.Weight = std::clamp(jl.value("Weight", 1.0f), 0.0f, 1.0f);
                    if (!std::isfinite(l.Weight)) l.Weight = 1.0f;
                    l.WeightParameter = jl.value("WeightParameter", std::string());
                    l.Blend = jl.value("Blend", std::string()) == "Additive" ? AnimatorLayerBlend::Additive : AnimatorLayerBlend::Override;
                    if (auto mask = jl.find("Mask"); mask != jl.end() && mask->is_array())
                        for (const json& bone : *mask)
                            if (bone.is_string())
                                l.MaskBones.push_back(bone.get<std::string>());
                    c.Layers.push_back(std::move(l));
                }
            for (const json& t : j.value("Transitions", json::array()))
            {
                AnimatorTransition tr;
                tr.From = t.value("From", -1);
                tr.To = t.value("To", 0);
                tr.Fade = t.value("Fade", 0.2f);
                tr.HasExitTime = t.value("HasExitTime", false);
                tr.ExitTime = t.value("ExitTime", 1.0f);
                for (const json& cd : t.value("Conditions", json::array()))
                    tr.Conditions.push_back({ cd.value("Parameter", ""), (AnimatorCondition::Op)cd.value("Op", 0), cd.value("Threshold", 0.0f) });
                c.Transitions.push_back(std::move(tr));
            }
            if (auto events = j.find("Events"); events != j.end() && events->is_array())
                for (const json& ev : *events)
                    if (ev.is_object())
                        c.Events.push_back({ ev.value("Clip", std::string()), std::clamp(ev.value("Time", 0.0f), 0.0f, 1.0f),
                                             ev.value("Name", std::string()), ev.value("Parameter", std::string()) });
            c.ApplyRootMotion = j.value("ApplyRootMotion", false);
            c.RootMotionBone = j.value("RootMotionBone", std::string());
            const std::string culling = j.value("Culling", std::string());
            c.Culling = culling == "Pose" ? AnimatorCulling::CullPose : culling == "Complete" ? AnimatorCulling::CullCompletely
                                                                                           : AnimatorCulling::AlwaysAnimate;
            c.CachePoses = j.value("CachePoses", false);
            c.PoseCacheRate = j.value("PoseCacheRate", 30.0f);
            if (!std::isfinite(c.PoseCacheRate) || c.PoseCacheRate < 1.0f)
                c.PoseCacheRate = 30.0f;
        }
        if (e.contains("AnimationPlayer"))
        {
            const json& j = e["AnimationPlayer"];
            auto& c = entity.AddComponent<AnimationPlayerComponent>();
            if (auto clips = j.find("Clips"); clips != j.end() && clips->is_array())
                for (const json& clip : *clips)
                {
                    if (clip.is_string())
                        c.Clips.push_back(clip.get<std::string>());
                    else if (clip.is_object())
                        c.Clips.push_back(ReadAssetPath(clip, "Path"));
                }
            c.Speed = j.value("Speed", 1.0f);
            if (!std::isfinite(c.Speed))
                c.Speed = 1.0f;
            c.PlayOnStart = j.value("PlayOnStart", true);
            const std::string wrap = j.value("Wrap", std::string("Clip"));
            c.Wrap = wrap == "Once" ? AnimationPlayerWrap::Once : wrap == "Loop" ? AnimationPlayerWrap::Loop
                   : wrap == "PingPong" ? AnimationPlayerWrap::PingPong : AnimationPlayerWrap::Clip;
        }
        if (e.contains("BoneAttachment"))
        {
            const json& j = e["BoneAttachment"];
            auto& c = entity.AddComponent<BoneAttachmentComponent>();
            c.Target.ID = j.contains("Target") && j["Target"].is_number_unsigned() ? j["Target"].get<uint64_t>() : 0;
            c.Bone = j.value("Bone", std::string());
            c.InheritBoneScale = j.value("InheritBoneScale", false);
        }
        if (auto sub = e.find("SubScene"); sub != e.end() && sub->is_object())
        {
            const json& j = *sub;
            auto& c = entity.AddComponent<SubSceneComponent>();
            // Scene files come here migrated (v3); entity snippets (prefabs, the clipboard) of an
            // engine before it may still have "Scene".
            c.ScenePath = ReadAssetPath(j, !j.contains("Path") && j.contains("Scene") ? "Scene" : "Path");
            c.LoadOnStart = j.value("LoadOnStart", true);
            c.ApplyTransform = j.value("ApplyTransform", true);
            c.ShowInEditor = j.value("ShowInEditor", true);
        }
        if (e.contains("Footsteps"))
        {
            const json& j = e["Footsteps"];
            auto& c = entity.AddComponent<FootstepsComponent>();
            if (auto sounds = j.find("Sounds"); sounds != j.end() && sounds->is_array())
                for (const json& sound : *sounds)
                {
                    if (sound.is_string())
                        c.Sounds.push_back(sound.get<std::string>());
                    else if (sound.is_object())
                        c.Sounds.push_back(ReadAssetPath(sound, "Path"));
                }
            auto number = [&](const char* key, float fallback) {
                const float v = j.value(key, fallback);
                return std::isfinite(v) ? v : fallback;
            };
            c.EventName = j.value("Event", std::string("Footstep"));
            c.Volume = std::max(0.0f, number("Volume", 0.8f));
            c.VolumeRandom = Vec(j, "VolumeRandom", glm::vec2(0.85f, 1.0f));
            c.PitchRandom = Vec(j, "PitchRandom", glm::vec2(0.92f, 1.08f));
            c.SpeedParameter = j.value("SpeedParameter", std::string("Speed"));
            c.ReferenceSpeed = std::max(0.0f, number("ReferenceSpeed", 3.0f));
            c.SpeedPitch = number("SpeedPitch", 0.12f);
            c.SpeedVolume = number("SpeedVolume", 0.35f);
            c.MinSpeed = std::max(0.0f, number("MinSpeed", 0.3f));
            c.MinInterval = std::max(0.0f, number("MinInterval", 0.12f));
            c.Spatial = j.value("Spatial", true);
            c.MinDistance = std::max(0.0f, number("MinDistance", 1.0f));
            c.MaxDistance = std::max(c.MinDistance, number("MaxDistance", 25.0f));
            c.Output = j.value("Output", std::string("SFX"));
        }
        if (e.contains("DirectionalLight"))
        {
            const json& j = e["DirectionalLight"];
            auto& c = entity.AddComponent<DirectionalLightComponent>();
            c.Color = Vec(j, "Color", c.Color);
            c.Intensity = j.value("Intensity", c.Intensity);
            c.CastShadows = j.value("CastShadows", true);
        }
        if (e.contains("PointLight"))
        {
            const json& j = e["PointLight"];
            auto& c = entity.AddComponent<PointLightComponent>();
            c.Color = Vec(j, "Color", c.Color);
            c.Intensity = j.value("Intensity", c.Intensity);
            c.Radius = j.value("Radius", c.Radius);
        }
        if (e.contains("SpotLight"))
        {
            const json& j = e["SpotLight"];
            auto& c = entity.AddComponent<SpotLightComponent>();
            c.Color = Vec(j, "Color", c.Color);
            c.Intensity = j.value("Intensity", c.Intensity);
            c.Range = j.value("Range", c.Range);
            c.OuterAngle = std::clamp(j.value("OuterAngle", c.OuterAngle), 0.1f, SpotLightComponent::MaxAngle);
            c.InnerAngle = std::clamp(j.value("InnerAngle", c.InnerAngle), 0.0f, c.OuterAngle);
        }
        if (e.contains("Light2D"))
        {
            const json& j = e["Light2D"];
            auto& c = entity.AddComponent<Light2DComponent>();
            c.Type = j.value("Type", std::string("Point")) == "Spot" ? Light2DType::Spot : Light2DType::Point;
            c.Color = glm::max(Vec(j, "Color", c.Color), glm::vec3(0.0f));
            c.Intensity = std::max(0.0f, j.value("Intensity", c.Intensity));
            c.Radius = std::max(0.0f, j.value("Radius", c.Radius));
            c.InnerRadius = std::clamp(j.value("InnerRadius", c.InnerRadius), 0.0f, c.Radius);
            c.Falloff = std::clamp(j.value("Falloff", c.Falloff), 0.1f, 8.0f);
            c.SpotAngle = std::clamp(j.value("SpotAngle", c.SpotAngle), 1.0f, 360.0f);
            c.SpotSoftness = std::clamp(j.value("SpotSoftness", c.SpotSoftness), 0.0f, 1.0f);
            c.Flicker = std::clamp(j.value("Flicker", c.Flicker), 0.0f, 1.0f);
            c.FlickerSpeed = std::max(0.0f, j.value("FlickerSpeed", c.FlickerSpeed));
        }
        if (e.contains("Rigidbody2D"))
        {
            const json& j = e["Rigidbody2D"];
            auto& c = entity.AddComponent<Rigidbody2DComponent>();
            c.Type = BodyTypeFromString(j.value("BodyType", "Static"));
            c.FixedRotation = j.value("FixedRotation", false);
            c.GravityScale = j.value("GravityScale", 1.0f);
            c.LinearDamping = j.value("LinearDamping", 0.0f);
            c.AngularDamping = j.value("AngularDamping", 0.01f);
            c.Bullet = j.value("Bullet", false);
            c.Layer = CollisionLayers2D::Clamp(j.value("Layer", 0));
            const std::string sleeping = j.value("Sleeping", std::string("StartAwake"));
            c.Sleeping = sleeping == "StartAsleep" ? Rigidbody2DComponent::SleepMode::StartAsleep
                       : sleeping == "NeverSleep" ? Rigidbody2DComponent::SleepMode::NeverSleep
                       : Rigidbody2DComponent::SleepMode::StartAwake;
        }
        auto readCollider = [](const json& j, auto& c) {
            c.Offset = Vec(j, "Offset", glm::vec2(0.0f));
            c.Density = j.value("Density", 1.0f);
            c.Friction = j.value("Friction", 0.5f);
            c.Restitution = j.value("Restitution", 0.0f);
            c.RestitutionThreshold = j.value("RestitutionThreshold", 0.5f);
            c.IsSensor = j.value("IsSensor", false);
            c.MaterialPath = ReadAssetPath(j, "Material");
            const int layer = j.value("Layer", -1);
            c.Layer = layer < 0 ? -1 : CollisionLayers2D::Clamp(layer);
        };
        auto readPoints = [](const json& j, std::vector<glm::vec2>& out) {
            auto it = j.find("Points");
            if (it == j.end() || !it->is_array())
                return;
            out.clear();
            for (const json& p : *it)
                if (p.is_array() && p.size() >= 2 && p[0].is_number() && p[1].is_number())
                    out.push_back({ p[0].get<float>(), p[1].get<float>() });
        };
        if (e.contains("BoxCollider2D"))
        {
            auto& c = entity.AddComponent<BoxCollider2DComponent>();
            readCollider(e["BoxCollider2D"], c);
            c.Size = Vec(e["BoxCollider2D"], "Size", glm::vec2(0.5f));
            c.OneWay = e["BoxCollider2D"].value("OneWay", false);
        }
        if (e.contains("CircleCollider2D"))
        {
            auto& c = entity.AddComponent<CircleCollider2DComponent>();
            readCollider(e["CircleCollider2D"], c);
            c.Radius = e["CircleCollider2D"].value("Radius", 0.5f);
        }
        if (e.contains("PolygonCollider2D"))
        {
            auto& c = entity.AddComponent<PolygonCollider2DComponent>();
            readCollider(e["PolygonCollider2D"], c);
            readPoints(e["PolygonCollider2D"], c.Points);
        }
        if (e.contains("EdgeCollider2D"))
        {
            const json& j = e["EdgeCollider2D"];
            auto& c = entity.AddComponent<EdgeCollider2DComponent>();
            readCollider(j, c);
            readPoints(j, c.Points);
            c.Loop = j.value("Loop", false);
            c.OneSided = j.value("OneSided", false);
        }
        if (e.contains("PlatformEffector2D"))
        {
            const json& j = e["PlatformEffector2D"];
            auto& c = entity.AddComponent<PlatformEffector2DComponent>();
            c.OneWay = j.value("OneWay", c.OneWay);
            c.SurfaceArc = std::clamp(j.value("SurfaceArc", c.SurfaceArc), 0.0f, 360.0f);
            c.RotationOffset = j.value("RotationOffset", c.RotationOffset);
            c.UseSideFriction = j.value("UseSideFriction", c.UseSideFriction);
            c.UseSideBounce = j.value("UseSideBounce", c.UseSideBounce);
            c.SideArc = std::clamp(j.value("SideArc", c.SideArc), 0.0f, 180.0f);
        }
        // Effectors: drags and densities are not negative; the layer mask is 16 bits.
        auto readMask = [](const json& j, uint16_t fallback) {
            return j.contains("ColliderMask") && j["ColliderMask"].is_number_integer() ? (uint16_t)(j["ColliderMask"].get<int64_t>() & 0xFFFF) : fallback;
        };
        if (e.contains("AreaEffector2D"))
        {
            const json& j = e["AreaEffector2D"];
            auto& c = entity.AddComponent<AreaEffector2DComponent>();
            c.ForceAngle = j.value("ForceAngle", c.ForceAngle);
            c.UseGlobalAngle = j.value("UseGlobalAngle", c.UseGlobalAngle);
            c.ForceMagnitude = j.value("ForceMagnitude", c.ForceMagnitude);
            c.ForceVariation = std::max(0.0f, j.value("ForceVariation", c.ForceVariation));
            c.ScaleWithMass = j.value("ScaleWithMass", c.ScaleWithMass);
            c.Drag = std::max(0.0f, j.value("Drag", c.Drag));
            c.AngularDrag = std::max(0.0f, j.value("AngularDrag", c.AngularDrag));
            c.ColliderMask = readMask(j, c.ColliderMask);
        }
        if (e.contains("BuoyancyEffector2D"))
        {
            const json& j = e["BuoyancyEffector2D"];
            auto& c = entity.AddComponent<BuoyancyEffector2DComponent>();
            c.SurfaceLevel = j.value("SurfaceLevel", c.SurfaceLevel);
            c.Density = std::max(0.0f, j.value("Density", c.Density));
            c.LinearDrag = std::max(0.0f, j.value("LinearDrag", c.LinearDrag));
            c.AngularDrag = std::max(0.0f, j.value("AngularDrag", c.AngularDrag));
            c.FlowAngle = j.value("FlowAngle", c.FlowAngle);
            c.FlowMagnitude = j.value("FlowMagnitude", c.FlowMagnitude);
            c.FlowVariation = std::max(0.0f, j.value("FlowVariation", c.FlowVariation));
            c.ColliderMask = readMask(j, c.ColliderMask);
        }
        // Missing values keep the defaults; limits are put in order, forces / frequencies are not negative.
        auto readJoint = [](const json& j, Joint2DComponentBase& c) {
            c.ConnectedBody.ID = j.contains("ConnectedBody") && j["ConnectedBody"].is_number_unsigned() ? j["ConnectedBody"].get<uint64_t>() : 0;
            c.Anchor = Vec(j, "Anchor", c.Anchor);
            c.ConnectedAnchor = Vec(j, "ConnectedAnchor", c.ConnectedAnchor);
            c.AutoConfigureConnectedAnchor = j.value("AutoConfigureConnectedAnchor", c.AutoConfigureConnectedAnchor);
            c.CollideConnected = j.value("CollideConnected", c.CollideConnected);
            c.BreakForce = std::max(0.0f, j.value("BreakForce", c.BreakForce));
            c.BreakTorque = std::max(0.0f, j.value("BreakTorque", c.BreakTorque));
        };
        auto readRange = [](const json& j, const char* lowerKey, const char* upperKey, float& lower, float& upper) {
            lower = j.value(lowerKey, lower);
            upper = j.value(upperKey, upper);
            if (lower > upper)
                std::swap(lower, upper);
        };
        if (e.contains("DistanceJoint2D"))
        {
            const json& j = e["DistanceJoint2D"];
            auto& c = entity.AddComponent<DistanceJoint2DComponent>();
            readJoint(j, c);
            c.AutoConfigureDistance = j.value("AutoConfigureDistance", c.AutoConfigureDistance);
            c.Distance = std::max(0.0f, j.value("Distance", c.Distance));
            c.MaxDistanceOnly = j.value("MaxDistanceOnly", c.MaxDistanceOnly);
            c.Frequency = std::max(0.0f, j.value("Frequency", c.Frequency));
            c.DampingRatio = std::max(0.0f, j.value("DampingRatio", c.DampingRatio));
        }
        if (e.contains("RevoluteJoint2D"))
        {
            const json& j = e["RevoluteJoint2D"];
            auto& c = entity.AddComponent<RevoluteJoint2DComponent>();
            readJoint(j, c);
            c.EnableLimit = j.value("EnableLimit", c.EnableLimit);
            readRange(j, "LowerAngle", "UpperAngle", c.LowerAngle, c.UpperAngle);
            c.EnableMotor = j.value("EnableMotor", c.EnableMotor);
            c.MotorSpeed = j.value("MotorSpeed", c.MotorSpeed);
            c.MaxMotorTorque = std::max(0.0f, j.value("MaxMotorTorque", c.MaxMotorTorque));
        }
        if (e.contains("PrismaticJoint2D"))
        {
            const json& j = e["PrismaticJoint2D"];
            auto& c = entity.AddComponent<PrismaticJoint2DComponent>();
            readJoint(j, c);
            c.Axis = Vec(j, "Axis", c.Axis);
            c.EnableLimit = j.value("EnableLimit", c.EnableLimit);
            readRange(j, "LowerTranslation", "UpperTranslation", c.LowerTranslation, c.UpperTranslation);
            c.EnableMotor = j.value("EnableMotor", c.EnableMotor);
            c.MotorSpeed = j.value("MotorSpeed", c.MotorSpeed);
            c.MaxMotorForce = std::max(0.0f, j.value("MaxMotorForce", c.MaxMotorForce));
        }
        if (e.contains("WeldJoint2D"))
        {
            const json& j = e["WeldJoint2D"];
            auto& c = entity.AddComponent<WeldJoint2DComponent>();
            readJoint(j, c);
            c.Frequency = std::max(0.0f, j.value("Frequency", c.Frequency));
            c.DampingRatio = std::max(0.0f, j.value("DampingRatio", c.DampingRatio));
        }
        if (e.contains("WheelJoint2D"))
        {
            const json& j = e["WheelJoint2D"];
            auto& c = entity.AddComponent<WheelJoint2DComponent>();
            readJoint(j, c);
            c.Axis = Vec(j, "Axis", c.Axis);
            c.Frequency = std::max(0.0f, j.value("Frequency", c.Frequency));
            c.DampingRatio = std::max(0.0f, j.value("DampingRatio", c.DampingRatio));
            c.EnableLimit = j.value("EnableLimit", c.EnableLimit);
            readRange(j, "LowerTranslation", "UpperTranslation", c.LowerTranslation, c.UpperTranslation);
            c.EnableMotor = j.value("EnableMotor", c.EnableMotor);
            c.MotorSpeed = j.value("MotorSpeed", c.MotorSpeed);
            c.MaxMotorTorque = std::max(0.0f, j.value("MaxMotorTorque", c.MaxMotorTorque));
        }
        if (e.contains("Rigidbody3D"))
        {
            const json& j = e["Rigidbody3D"];
            auto& c = entity.AddComponent<Rigidbody3DComponent>();
            c.Type = BodyType3DFromString(j.value("BodyType", "Dynamic"));
            c.Mass = j.value("Mass", 1.0f);
            c.LinearDamping = j.value("LinearDamping", 0.05f);
            c.AngularDamping = j.value("AngularDamping", 0.05f);
            c.GravityFactor = j.value("GravityFactor", 1.0f);
            c.LockRotation = j.value("LockRotation", false);
            c.Layer = CollisionLayers2D::Clamp(j.value("Layer", 0));
            c.ContinuousCollision = j.value("ContinuousCollision", false);
            const std::string sleeping = j.value("Sleeping", std::string("StartAwake"));
            c.Sleeping = sleeping == "StartAsleep" ? Rigidbody3DComponent::SleepMode::StartAsleep
                       : sleeping == "NeverSleep" ? Rigidbody3DComponent::SleepMode::NeverSleep
                       : Rigidbody3DComponent::SleepMode::StartAwake;
        }
        auto read3D = [](const json& j, auto& c) {
            c.Offset = Vec(j, "Offset", glm::vec3(0.0f));
            c.Friction = j.value("Friction", 0.5f);
            c.Restitution = j.value("Restitution", 0.0f);
            c.IsSensor = j.value("IsSensor", false);
            c.MaterialPath = ReadAssetPath(j, "Material");
        };
        if (e.contains("BoxCollider3D"))
        {
            auto& c = entity.AddComponent<BoxCollider3DComponent>();
            read3D(e["BoxCollider3D"], c);
            c.HalfExtents = Vec(e["BoxCollider3D"], "HalfExtents", glm::vec3(0.5f));
        }
        if (e.contains("SphereCollider3D"))
        {
            auto& c = entity.AddComponent<SphereCollider3DComponent>();
            read3D(e["SphereCollider3D"], c);
            c.Radius = e["SphereCollider3D"].value("Radius", 0.5f);
        }
        if (e.contains("CapsuleCollider3D"))
        {
            auto& c = entity.AddComponent<CapsuleCollider3DComponent>();
            read3D(e["CapsuleCollider3D"], c);
            c.Radius = e["CapsuleCollider3D"].value("Radius", 0.5f);
            c.HalfHeight = e["CapsuleCollider3D"].value("HalfHeight", 0.5f);
        }
        if (e.contains("ConvexHullCollider3D"))
        {
            const json& j = e["ConvexHullCollider3D"];
            auto& c = entity.AddComponent<ConvexHullCollider3DComponent>();
            read3D(j, c);
            c.ModelPath = ReadAssetPath(j, "Model");
            c.MaxVertices = std::clamp(j.value("MaxVertices", c.MaxVertices), CollisionShapes3D::MinHullVertices, CollisionShapes3D::MaxHullVertices);
        }
        if (e.contains("MeshCollider3D"))
        {
            const json& j = e["MeshCollider3D"];
            auto& c = entity.AddComponent<MeshCollider3DComponent>();
            read3D(j, c);
            c.ModelPath = ReadAssetPath(j, "Model");
        }
        // The same rules as the 2D joints; hinge / slider limits are kept around the start pose.
        auto readJoint3D = [](const json& j, Joint3DComponentBase& c) {
            c.ConnectedBody.ID = j.contains("ConnectedBody") && j["ConnectedBody"].is_number_unsigned() ? j["ConnectedBody"].get<uint64_t>() : 0;
            c.Anchor = Vec(j, "Anchor", c.Anchor);
            c.ConnectedAnchor = Vec(j, "ConnectedAnchor", c.ConnectedAnchor);
            c.AutoConfigureConnectedAnchor = j.value("AutoConfigureConnectedAnchor", c.AutoConfigureConnectedAnchor);
            c.CollideConnected = j.value("CollideConnected", c.CollideConnected);
            c.BreakForce = std::max(0.0f, j.value("BreakForce", c.BreakForce));
            c.BreakTorque = std::max(0.0f, j.value("BreakTorque", c.BreakTorque));
        };
        if (e.contains("FixedJoint3D"))
            readJoint3D(e["FixedJoint3D"], entity.AddComponent<FixedJoint3DComponent>());
        if (e.contains("HingeJoint3D"))
        {
            const json& j = e["HingeJoint3D"];
            auto& c = entity.AddComponent<HingeJoint3DComponent>();
            readJoint3D(j, c);
            c.Axis = Vec(j, "Axis", c.Axis);
            c.EnableLimit = j.value("EnableLimit", c.EnableLimit);
            readRange(j, "LowerAngle", "UpperAngle", c.LowerAngle, c.UpperAngle);
            c.LowerAngle = std::clamp(c.LowerAngle, -180.0f, 0.0f);
            c.UpperAngle = std::clamp(c.UpperAngle, 0.0f, 180.0f);
            c.EnableMotor = j.value("EnableMotor", c.EnableMotor);
            c.MotorSpeed = j.value("MotorSpeed", c.MotorSpeed);
            c.MaxMotorTorque = std::max(0.0f, j.value("MaxMotorTorque", c.MaxMotorTorque));
            c.EnableSpring = j.value("EnableSpring", c.EnableSpring);
            c.SpringTargetAngle = std::clamp(j.value("SpringTargetAngle", c.SpringTargetAngle), -180.0f, 180.0f);
            c.SpringFrequency = std::max(0.0f, j.value("SpringFrequency", c.SpringFrequency));
            c.SpringDamping = std::max(0.0f, j.value("SpringDamping", c.SpringDamping));
        }
        if (e.contains("DistanceJoint3D"))
        {
            const json& j = e["DistanceJoint3D"];
            auto& c = entity.AddComponent<DistanceJoint3DComponent>();
            readJoint3D(j, c);
            c.AutoConfigureDistance = j.value("AutoConfigureDistance", c.AutoConfigureDistance);
            readRange(j, "MinDistance", "MaxDistance", c.MinDistance, c.MaxDistance);
            c.MinDistance = std::max(0.0f, c.MinDistance);
            c.MaxDistance = std::max(c.MinDistance, c.MaxDistance);
            c.MaxDistanceOnly = j.value("MaxDistanceOnly", c.MaxDistanceOnly);
            c.Frequency = std::max(0.0f, j.value("Frequency", c.Frequency));
            c.DampingRatio = std::max(0.0f, j.value("DampingRatio", c.DampingRatio));
        }
        if (e.contains("SliderJoint3D"))
        {
            const json& j = e["SliderJoint3D"];
            auto& c = entity.AddComponent<SliderJoint3DComponent>();
            readJoint3D(j, c);
            c.Axis = Vec(j, "Axis", c.Axis);
            c.EnableLimit = j.value("EnableLimit", c.EnableLimit);
            readRange(j, "LowerTranslation", "UpperTranslation", c.LowerTranslation, c.UpperTranslation);
            c.LowerTranslation = std::min(0.0f, c.LowerTranslation);
            c.UpperTranslation = std::max(0.0f, c.UpperTranslation);
            c.EnableMotor = j.value("EnableMotor", c.EnableMotor);
            c.MotorSpeed = j.value("MotorSpeed", c.MotorSpeed);
            c.MaxMotorForce = std::max(0.0f, j.value("MaxMotorForce", c.MaxMotorForce));
        }
        if (e.contains("ConeJoint3D"))
        {
            const json& j = e["ConeJoint3D"];
            auto& c = entity.AddComponent<ConeJoint3DComponent>();
            readJoint3D(j, c);
            c.Axis = Vec(j, "Axis", c.Axis);
            c.SwingLimit = std::clamp(j.value("SwingLimit", c.SwingLimit), 0.0f, 180.0f);
            readRange(j, "TwistLower", "TwistUpper", c.TwistLower, c.TwistUpper);
            c.TwistLower = std::clamp(c.TwistLower, -180.0f, 0.0f);
            c.TwistUpper = std::clamp(c.TwistUpper, 0.0f, 180.0f);
            c.FrictionTorque = std::max(0.0f, j.value("FrictionTorque", c.FrictionTorque));
        }
        if (e.contains("CharacterController3D"))
        {
            const json& j = e["CharacterController3D"];
            auto& c = entity.AddComponent<CharacterController3DComponent>();
            c.Radius = j.value("Radius", 0.35f);
            c.Height = j.value("Height", 1.8f);
            c.MaxSlope = j.value("MaxSlope", 50.0f);
            c.StepHeight = j.value("StepHeight", 0.35f);
            c.Mass = j.value("Mass", 70.0f);
            c.GravityScale = j.value("GravityScale", 1.0f);
            c.StickToFloor = j.value("StickToFloor", true);
            c.Layer = CollisionLayers2D::Clamp(j.value("Layer", 0));
            c.PushBodies = j.value("PushBodies", true);
            c.PushForce = std::max(0.0f, j.value("PushForce", c.PushForce));
        }
        if (e.contains("Tilemap"))
        {
            const json& j = e["Tilemap"];
            auto& c = entity.AddComponent<TilemapComponent>();
            c.TilesetPath = ReadAssetPath(j, "Tileset");
            c.PixelArt = j.value("PixelArt", true);
            c.TilesetColumns = j.value("TilesetColumns", 8);
            c.TilesetRows = j.value("TilesetRows", 4);
            c.Width = j.value("Width", 32);
            c.Height = j.value("Height", 12);
            c.TileSize = j.value("TileSize", 1.0f);
            // Each layer: { Name, Visible, Collision, SortingOrder, Opacity, Tiles }. Files from before
            // layers existed have one "Tiles" array: it becomes layer 0 (solid, as it always was).
            auto readTiles = [&](const json& tiles, std::vector<int>& out) {
                out.clear();
                if (!tiles.is_array())
                    return;
                out.reserve(tiles.size());
                for (const json& t : tiles)
                    out.push_back(t.is_number_integer() ? std::max(-1, t.get<int>()) : -1);
            };
            c.Layers.clear();
            if (j.contains("Layers") && j["Layers"].is_array())
            {
                for (const json& l : j["Layers"])
                {
                    if (!l.is_object() || (int)c.Layers.size() >= TilemapComponent::MaxLayers)
                        continue;
                    TilemapLayer layer;
                    layer.Name = l.value("Name", std::string());
                    if (layer.Name.empty() || c.FindLayer(layer.Name) >= 0)
                        layer.Name = c.MakeUniqueLayerName(layer.Name.empty() ? "Qatlam" : layer.Name);
                    layer.Visible = l.value("Visible", true);
                    layer.Collision = l.value("Collision", true);
                    layer.SortingOrder = l.value("SortingOrder", 0);
                    layer.Opacity = std::clamp(l.value("Opacity", 1.0f), 0.0f, 1.0f);
                    if (l.contains("Tiles"))
                        readTiles(l["Tiles"], layer.Tiles);
                    c.Layers.push_back(std::move(layer));
                }
            }
            else
            {
                c.Layers.emplace_back();
                if (j.contains("Tiles"))
                    readTiles(j["Tiles"], c.Layers.back().Tiles);
            }
            c.EnsureSize(); // a layer of another size (edited by hand) is cut / padded
            c.Color = Vec(j, "Color", glm::vec4(1.0f));
            c.SortingOrder = j.value("SortingOrder", -1);
            c.SortingLayer = ReadSortingLayer(j);
            c.Lit = j.value("Lit", false);
            c.Collision = j.value("Collision", true);
            c.Friction = j.value("Friction", 0.6f);
            c.Restitution = std::max(0.0f, j.value("Restitution", 0.0f));
            c.MaterialPath = ReadAssetPath(j, "Material");
            c.CollisionLayer = CollisionLayers2D::Clamp(j.value("CollisionLayer", 0));
        }
        if (e.contains("Parallax"))
        {
            const json& j = e["Parallax"];
            auto& c = entity.AddComponent<ParallaxComponent>();
            c.Factor = Vec(j, "Factor", glm::vec2(0.5f));
            c.RepeatX = j.value("RepeatX", false);
            c.RepeatY = j.value("RepeatY", false);
            c.Spacing = glm::max(Vec(j, "Spacing", glm::vec2(0.0f)), glm::vec2(0.0f));
        }
        if (e.contains("Camera2DFollow"))
        {
            const json& j = e["Camera2DFollow"];
            auto& c = entity.AddComponent<Camera2DFollowComponent>();
            c.Target.ID = j.contains("Target") && j["Target"].is_number_unsigned() ? j["Target"].get<uint64_t>() : 0;
            c.Offset = Vec(j, "Offset", c.Offset);
            c.Damping = glm::max(Vec(j, "Damping", c.Damping), glm::vec2(0.0f));
            c.DeadZone = glm::max(Vec(j, "DeadZone", c.DeadZone), glm::vec2(0.0f));
            c.LookAhead = Vec(j, "LookAhead", c.LookAhead);
            c.LookAheadDamping = std::max(0.0f, j.value("LookAheadDamping", c.LookAheadDamping));
            c.LookAheadMinSpeed = std::max(0.0f, j.value("LookAheadMinSpeed", c.LookAheadMinSpeed));
            c.UseBounds = j.value("UseBounds", false);
            c.BoundsMin = Vec(j, "BoundsMin", c.BoundsMin);
            c.BoundsMax = Vec(j, "BoundsMax", c.BoundsMax);
            c.SnapOnStart = j.value("SnapOnStart", true);
        }
        if (auto it = e.find("Spline"); it != e.end() && it->is_object())
            SplineFromJson(*it, entity.AddComponent<SplineComponent>());
        if (auto it = e.find("SplineFollower"); it != e.end() && it->is_object())
        {
            const json& j = *it;
            auto& c = entity.AddComponent<SplineFollowerComponent>();
            c.Path.ID = j.contains("Path") && j["Path"].is_number_unsigned() ? j["Path"].get<uint64_t>() : 0;
            c.Speed = std::clamp(Number(j, "Speed", c.Speed), -1e4f, 1e4f);
            c.Mode = EnumFromJson(j, "Mode", s_SplineFollowModes, c.Mode);
            c.StartPosition = std::clamp(Number(j, "StartPosition", 0.0f), 0.0f, 1.0f);
            c.Rotation = EnumFromJson(j, "Rotation", s_SplineRotations, c.Rotation);
            if (auto o = j.find("Offset"); o != j.end())
                Numbers(*o, &c.Offset.x, 3);
            c.PlayOnStart = j.value("PlayOnStart", true);
        }
        if (e.contains("ParticleSystem"))
            ParticleSystemFromJson(e["ParticleSystem"], entity.AddComponent<ParticleSystemComponent>());
        if (e.contains("UITransform"))
        {
            const json& j = e["UITransform"];
            auto& c = entity.AddComponent<UITransformComponent>();
            if (j.contains("AnchorMin") || j.contains("AnchorMax"))
            {
                c.AnchorMin = glm::clamp(Vec(j, "AnchorMin", c.AnchorMin), glm::vec2(0.0f), glm::vec2(1.0f));
                c.AnchorMax = glm::clamp(Vec(j, "AnchorMax", c.AnchorMin), glm::vec2(0.0f), glm::vec2(1.0f));
                c.Pivot = Vec(j, "Pivot", c.Pivot);
            }
            else
            {
                // Older scenes: one of 9 anchor points, the position of the rect's centre from it.
                c.SetAnchor((UITransformComponent::AnchorPoint)std::clamp(j.value("Anchor", 0), 0, 8));
                c.Pivot = { 0.5f, 0.5f };
            }
            c.Position = Vec(j, "Position", c.Position);
            c.Size = Vec(j, "Size", c.Size);
            c.FontSize = j.value("FontSize", 0.0f);
            c.Order = j.value("Order", 0);
            c.Scale = Vec(j, "Scale", glm::vec2(1.0f));
            c.VisualOffset = Vec(j, "VisualOffset", glm::vec2(0.0f));
            c.Alpha = std::clamp(j.value("Alpha", 1.0f), 0.0f, 1.0f);
        }
        if (e.contains("UIAnimation"))
        {
            const json& j = e["UIAnimation"];
            auto& c = entity.AddComponent<UIAnimationComponent>();
            auto ease = [&](const char* key, Ease fallback) {
                auto it = j.find(key);
                return it != j.end() && it->is_string() ? EaseFromString(it->get<std::string>()).value_or(fallback) : fallback;
            };
            c.Show = EnumFromJson(j, "Show", s_UIAnimPresets, UIAnimPreset::Pop);
            c.Hide = EnumFromJson(j, "Hide", s_UIAnimPresets, UIAnimPreset::Fade);
            c.ShowAtStart = j.value("ShowAtStart", true);
            c.Duration = std::max(0.0f, j.value("Duration", 0.25f));
            c.ShowDelay = std::max(0.0f, j.value("ShowDelay", 0.0f));
            c.ShowEase = ease("ShowEase", Ease::OutBack);
            c.HideEase = ease("HideEase", Ease::InQuad);
            c.SlideDistance = j.value("SlideDistance", 160.0f);
            c.HoverScale = j.value("HoverScale", 1.0f);
            c.PressScale = j.value("PressScale", 1.0f);
            c.FeedbackDuration = std::max(0.0f, j.value("FeedbackDuration", 0.08f));
        }
        if (e.contains("UICanvas"))
        {
            const json& j = e["UICanvas"];
            auto& c = entity.AddComponent<UICanvasComponent>();
            c.ScaleMode = EnumFromJson(j, "ScaleMode", s_UIScaleModes, UIScaleMode::ScaleWithScreenSize);
            c.ScaleFactor = std::max(0.01f, j.value("ScaleFactor", 1.0f));
            c.ReferenceResolution = glm::max(Vec(j, "ReferenceResolution", c.ReferenceResolution), glm::vec2(1.0f));
            c.MatchMode = EnumFromJson(j, "MatchMode", s_UIScreenMatchModes, UIScreenMatchMode::MatchWidthOrHeight);
            c.MatchWidthOrHeight = std::clamp(j.value("Match", 0.5f), 0.0f, 1.0f);
        }
        if (e.contains("UILayoutGroup"))
        {
            const json& j = e["UILayoutGroup"];
            auto& c = entity.AddComponent<UILayoutGroupComponent>();
            c.Type = EnumFromJson(j, "Type", s_UILayoutTypes, UILayoutType::Vertical);
            c.Padding = Vec(j, "Padding", c.Padding);
            c.ChildAlignment = std::clamp(j.value("Alignment", 0), 0, 8);
            c.FitWidth = j.value("FitWidth", false);
            c.FitHeight = j.value("FitHeight", false);
            c.Spacing = j.value("Spacing", c.Spacing);
            c.ReverseArrangement = j.value("Reverse", false);
            c.ControlChildWidth = j.value("ControlWidth", false);
            c.ControlChildHeight = j.value("ControlHeight", false);
            c.ChildForceExpandWidth = j.value("ExpandWidth", false);
            c.ChildForceExpandHeight = j.value("ExpandHeight", false);
            c.CellSize = glm::max(Vec(j, "CellSize", c.CellSize), glm::vec2(0.0f));
            c.GridSpacing = Vec(j, "GridSpacing", c.GridSpacing);
            c.StartCorner = std::clamp(j.value("StartCorner", 0), 0, 3);
            c.StartAxisVertical = j.value("StartAxisVertical", false);
            c.Constraint = EnumFromJson(j, "Constraint", s_UIGridConstraints, UIGridConstraint::Flexible);
            c.ConstraintCount = std::max(1, j.value("ConstraintCount", 2));
        }
        if (e.contains("UILayoutElement"))
        {
            const json& j = e["UILayoutElement"];
            auto& c = entity.AddComponent<UILayoutElementComponent>();
            c.IgnoreLayout = j.value("IgnoreLayout", false);
            c.MinSize = Vec(j, "Min", c.MinSize);
            c.PreferredSize = Vec(j, "Preferred", c.PreferredSize);
            c.FlexibleSize = Vec(j, "Flexible", c.FlexibleSize);
        }
        if (e.contains("UINavigation"))
        {
            const json& j = e["UINavigation"];
            auto& c = entity.AddComponent<UINavigationComponent>();
            auto ref = [&](const char* key) { return EntityRef{ j.contains(key) && j[key].is_number_unsigned() ? j[key].get<uint64_t>() : 0 }; };
            c.NavigationMode = EnumFromJson(j, "Mode", s_UINavigationModes, UINavigationComponent::Mode::Automatic);
            c.Up = ref("Up");
            c.Down = ref("Down");
            c.Left = ref("Left");
            c.Right = ref("Right");
            c.SelectOnEnable = j.value("SelectOnEnable", false);
            c.ShowHighlight = j.value("ShowHighlight", true);
            c.HighlightColor = Vec(j, "HighlightColor", c.HighlightColor);
        }
        if (e.contains("UIButton"))
        {
            const json& j = e["UIButton"];
            auto& c = entity.AddComponent<UIButtonComponent>();
            c.NormalColor = Vec(j, "Normal", c.NormalColor);
            c.HoverColor = Vec(j, "Hover", c.HoverColor);
            c.PressedColor = Vec(j, "Pressed", c.PressedColor);
            c.Interactable = j.value("Interactable", true);
            c.ClickMessage = j.value("ClickMessage", std::string());
            c.ClickTarget.ID = j.contains("ClickTarget") && j["ClickTarget"].is_number_unsigned() ? j["ClickTarget"].get<uint64_t>() : 0;
        }
        if (e.contains("UIImage"))
        {
            const json& j = e["UIImage"];
            auto& c = entity.AddComponent<UIImageComponent>();
            c.Color = Vec(j, "Color", c.Color);
            c.TexturePath = ReadAssetPath(j, "Texture");
            c.SpriteName = j.value("Sprite", std::string());
            c.Type = EnumFromJson(j, "Type", s_UIImageTypes, UIImageType::Simple);
            c.PreserveAspect = j.value("PreserveAspect", false);
            c.PixelArt = j.value("PixelArt", false);
            c.RaycastTarget = j.value("RaycastTarget", true);
            c.SliceBorder = glm::max(Vec(j, "SliceBorder", glm::vec4(0.0f)), glm::vec4(0.0f));
            c.BorderScale = std::max(0.01f, j.value("BorderScale", 1.0f));
            if (auto fill = j.find("Fill"); fill != j.end() && fill->is_object())
            {
                c.FillMethod = EnumFromJson(*fill, "Method", s_UIFillMethods, UIFillMethod::Horizontal);
                c.FillOrigin = std::clamp(fill->value("Origin", 0), 0, 3);
                c.FillAmount = std::clamp(fill->value("Amount", 1.0f), 0.0f, 1.0f);
                c.FillClockwise = fill->value("Clockwise", true);
            }
        }
        if (e.contains("UISlider"))
        {
            const json& j = e["UISlider"];
            auto& c = entity.AddComponent<UISliderComponent>();
            c.MinValue = j.value("Min", 0.0f);
            c.MaxValue = j.value("Max", 1.0f);
            c.WholeNumbers = j.value("WholeNumbers", false);
            c.Value = UIMath::ClampValue(j.value("Value", 0.5f), c.MinValue, c.MaxValue, c.WholeNumbers);
            c.Direction = EnumFromJson(j, "Direction", s_UIDirections, UIDirection::LeftToRight);
            c.Interactable = j.value("Interactable", true);
            c.TrackThickness = std::max(0.0f, j.value("TrackThickness", 8.0f));
            c.HandleSize = std::max(0.0f, j.value("HandleSize", 0.0f));
            c.Rounded = j.value("Rounded", true);
            c.TrackColor = Vec(j, "TrackColor", c.TrackColor);
            c.FillColor = Vec(j, "FillColor", c.FillColor);
            c.HandleColor = Vec(j, "HandleColor", c.HandleColor);
        }
        if (e.contains("UIToggle"))
        {
            const json& j = e["UIToggle"];
            auto& c = entity.AddComponent<UIToggleComponent>();
            c.IsOn = j.value("IsOn", false);
            c.Interactable = j.value("Interactable", true);
            c.Style = EnumFromJson(j, "Style", s_UIToggleStyles, UIToggleStyle::Checkbox);
            c.Mark = EnumFromJson(j, "Mark", s_UIToggleMarks, UIToggleMark::Check);
            c.BoxSize = std::max(0.0f, j.value("BoxSize", 0.0f));
            c.LabelSpacing = std::max(0.0f, j.value("LabelSpacing", 10.0f));
            c.Group = j.value("Group", std::string());
            c.AllowSwitchOff = j.value("AllowSwitchOff", false);
            c.BoxColor = Vec(j, "BoxColor", c.BoxColor);
            c.OnColor = Vec(j, "OnColor", c.OnColor);
            c.MarkColor = Vec(j, "MarkColor", c.MarkColor);
            c.Transition = c.IsOn ? 1.0f : 0.0f;
        }
        if (e.contains("UIProgressBar"))
        {
            const json& j = e["UIProgressBar"];
            auto& c = entity.AddComponent<UIProgressBarComponent>();
            c.Value = std::clamp(j.value("Value", 0.5f), 0.0f, 1.0f);
            c.Direction = EnumFromJson(j, "Direction", s_UIDirections, UIDirection::LeftToRight);
            c.BackgroundColor = Vec(j, "BackgroundColor", c.BackgroundColor);
            c.FillColor = Vec(j, "FillColor", c.FillColor);
            c.UseLowColor = j.value("UseLowColor", false);
            c.LowColor = Vec(j, "LowColor", c.LowColor);
            c.Padding = std::max(0.0f, j.value("Padding", 3.0f));
            c.SmoothSpeed = std::max(0.0f, j.value("SmoothSpeed", 0.0f));
            c.ShowPercentage = j.value("ShowPercentage", false);
            c.TextColor = Vec(j, "TextColor", c.TextColor);
            c.DisplayValue = c.Value;
        }
        if (e.contains("UIInputField"))
        {
            const json& j = e["UIInputField"];
            auto& c = entity.AddComponent<UIInputFieldComponent>();
            c.Text = j.value("Text", std::string());
            c.Placeholder = j.value("Placeholder", std::string());
            c.ContentType = EnumFromJson(j, "ContentType", s_UIContentTypes, UIContentType::Standard);
            c.CharacterLimit = std::max(0, j.value("CharacterLimit", 0));
            c.Interactable = j.value("Interactable", true);
            c.ReadOnly = j.value("ReadOnly", false);
            c.SelectAllOnFocus = j.value("SelectAllOnFocus", false);
            c.FontPath = ReadAssetPath(j, "Font");
            c.Padding = std::max(0.0f, j.value("Padding", 10.0f));
            c.BackgroundColor = Vec(j, "BackgroundColor", c.BackgroundColor);
            c.OutlineColor = Vec(j, "OutlineColor", c.OutlineColor);
            c.FocusColor = Vec(j, "FocusColor", c.FocusColor);
            c.TextColor = Vec(j, "TextColor", c.TextColor);
            c.PlaceholderColor = Vec(j, "PlaceholderColor", c.PlaceholderColor);
            c.CaretColor = Vec(j, "CaretColor", c.CaretColor);
            c.SelectionColor = Vec(j, "SelectionColor", c.SelectionColor);
            c.CaretBlinkRate = std::max(0.0f, j.value("CaretBlinkRate", 0.53f));
        }
        if (e.contains("UIMask"))
        {
            const json& j = e["UIMask"];
            auto& c = entity.AddComponent<UIMaskComponent>();
            c.ShowGraphic = j.value("ShowGraphic", true);
            c.Padding = Vec(j, "Padding", c.Padding);
        }
        if (e.contains("UIScrollView"))
        {
            const json& j = e["UIScrollView"];
            auto& c = entity.AddComponent<UIScrollViewComponent>();
            auto ref = [&](const char* key) { return EntityRef{ j.contains(key) && j[key].is_number_unsigned() ? j[key].get<uint64_t>() : 0 }; };
            c.Content = ref("Content");
            c.Viewport = ref("Viewport");
            c.Horizontal = j.value("Horizontal", false);
            c.Vertical = j.value("Vertical", true);
            c.Movement = EnumFromJson(j, "Movement", s_UIScrollMovements, UIScrollMovement::Elastic);
            c.Elasticity = std::max(0.0f, j.value("Elasticity", 0.1f));
            c.Inertia = j.value("Inertia", true);
            c.DecelerationRate = std::clamp(j.value("DecelerationRate", 0.135f), 0.0f, 1.0f);
            c.ScrollSensitivity = j.value("ScrollSensitivity", 40.0f);
            c.Draggable = j.value("Draggable", true);
            c.Scrollbars = EnumFromJson(j, "Scrollbars", s_UIScrollbarVisibilities, UIScrollbarVisibility::AutoHide);
            c.ScrollbarWidth = std::max(0.0f, j.value("ScrollbarWidth", 10.0f));
            c.ScrollbarInset = std::max(0.0f, j.value("ScrollbarInset", 3.0f));
            c.TrackColor = Vec(j, "TrackColor", c.TrackColor);
            c.HandleColor = Vec(j, "HandleColor", c.HandleColor);
        }
        if (e.contains("AudioSource"))
        {
            const json& j = e["AudioSource"];
            auto& c = entity.AddComponent<AudioSourceComponent>();
            c.Path = ReadAssetPath(j, "Path");
            c.Volume = j.value("Volume", 1.0f);
            c.Pitch = j.value("Pitch", 1.0f);
            c.Loop = j.value("Loop", false);
            c.PlayOnStart = j.value("PlayOnStart", true);
            c.Spatial = j.value("Spatial", false);
            c.MinDistance = j.value("MinDistance", 1.0f);
            c.MaxDistance = j.value("MaxDistance", 30.0f);
            c.Output = j.value("Output", std::string("SFX"));
            if (c.Output.empty())
                c.Output = "SFX";
            c.Stream = j.value("Stream", false);
            auto range = [](glm::vec2 v, float floor) {
                for (int i = 0; i < 2; i++)
                    v[i] = std::isfinite(v[i]) ? std::max(floor, v[i]) : 1.0f;
                return glm::vec2(std::min(v.x, v.y), std::max(v.x, v.y));
            };
            c.VolumeRandom = range(Vec(j, "VolumeRandom", glm::vec2(1.0f)), 0.0f);
            c.PitchRandom = range(Vec(j, "PitchRandom", glm::vec2(1.0f)), 0.01f);
            c.Priority = std::clamp(j.value("Priority", 128), 0, 255);
            c.MaxInstances = std::max(0, j.value("MaxInstances", 0));
            std::string steal = j.contains("Steal") && j["Steal"].is_string() ? j["Steal"].get<std::string>() : std::string();
            c.Steal = steal == "Quietest" ? AudioStealPolicy::Quietest : steal == "None" ? AudioStealPolicy::None : AudioStealPolicy::Oldest;
            c.DopplerFactor = std::clamp(j.value("Doppler", 1.0f), 0.0f, AudioSettings::MaxDopplerFactor);
            auto seconds = [](float v) { return std::isfinite(v) ? std::clamp(v, 0.0f, 600.0f) : 0.0f; };
            c.FadeIn = seconds(j.value("FadeIn", 0.0f));
            c.FadeOut = seconds(j.value("FadeOut", 0.0f));
            if (j.contains("Filter") && j["Filter"].is_object())
            {
                const json& f = j["Filter"];
                c.Filter.LowPass = f.value("LowPass", false);
                c.Filter.LowPassCutoff = f.value("LowPassCutoff", c.Filter.LowPassCutoff);
                c.Filter.HighPass = f.value("HighPass", false);
                c.Filter.HighPassCutoff = f.value("HighPassCutoff", c.Filter.HighPassCutoff);
                c.Filter.Sanitize();
            }
            if (j.contains("Cone") && j["Cone"].is_object())
            {
                const json& cone = j["Cone"];
                c.Cone.Enabled = cone.value("Enabled", false);
                c.Cone.InnerAngle = cone.value("InnerAngle", c.Cone.InnerAngle);
                c.Cone.OuterAngle = cone.value("OuterAngle", c.Cone.OuterAngle);
                c.Cone.OuterGain = cone.value("OuterGain", c.Cone.OuterGain);
                c.Cone.Sanitize();
            }
        }
        if (e.contains("AudioListener"))
        {
            const json& j = e["AudioListener"];
            auto& c = entity.AddComponent<AudioListenerComponent>();
            c.Enabled = j.value("Enabled", true);
            c.Priority = j.value("Priority", 0);
            c.TrackVelocity = j.value("TrackVelocity", false);
        }
        if (e.contains("AmbientZone") && e["AmbientZone"].is_object())
        {
            const json& j = e["AmbientZone"];
            auto& c = entity.AddComponent<AmbientZoneComponent>();
            auto finite = [](float v, float lo, float hi, float fallback) { return std::isfinite(v) ? std::clamp(v, lo, hi) : fallback; };
            c.Path = ReadAssetPath(j, "Path");
            c.Volume = finite(j.value("Volume", 1.0f), 0.0f, AudioSettings::MaxVolume, 1.0f);
            c.Pitch = finite(j.value("Pitch", 1.0f), 0.01f, 10.0f, 1.0f);
            c.Output = j.value("Output", std::string(AudioSettings::SFX));
            c.Stream = j.value("Stream", false);
            c.Priority = std::clamp(j.value("Priority", 128), 0, 255);
            c.Shape = j.value("Shape", std::string("Box")) == "Sphere" ? AmbientZoneComponent::ZoneShape::Sphere : AmbientZoneComponent::ZoneShape::Box;
            glm::vec3 size = Vec(j, "Size", c.Size);
            for (int i = 0; i < 3; i++)
                c.Size[i] = finite(size[i], 0.0f, 1e6f, 10.0f);
            c.Radius = finite(j.value("Radius", c.Radius), 0.0f, 1e6f, 5.0f);
            c.BlendDistance = finite(j.value("BlendDistance", c.BlendDistance), 0.0f, 1e6f, 4.0f);
            c.FadeTime = finite(j.value("FadeTime", c.FadeTime), 0.0f, 600.0f, 0.5f);
        }
        if (e.contains("NativeScript"))
        {
            const json& j = e["NativeScript"];
            auto& c = entity.AddComponent<NativeScriptComponent>();
            c.ClassName = j.value("Class", "");
            c.Enabled = j.value("Enabled", true);
            if (j.contains("Fields") && j["Fields"].is_object())
                for (auto it = j["Fields"].begin(); it != j["Fields"].end(); ++it)
                {
                    ScriptFieldValue v;
                    if (DeserializeFieldValue(it.value(), v))
                        c.Fields[it.key()] = v;
                }
        }
        if (auto it = e.find("Editor"); it != e.end() && it->is_object())
        {
            EditorMetadataComponent m;
            auto flag = [&](const char* key) { auto f = it->find(key); return f != it->end() && f->is_boolean() && f->get<bool>(); };
            m.Hidden = flag("Hidden");
            m.Locked = flag("Locked");
            if (auto order = it->find("ComponentOrder"); order != it->end() && order->is_array())
                for (const json& key : *order)
                    if (key.is_string() && std::find(m.ComponentOrder.begin(), m.ComponentOrder.end(), key.get<std::string>()) == m.ComponentOrder.end())
                        m.ComponentOrder.push_back(key.get<std::string>());
            if (auto prefab = it->find("Prefab"); prefab != it->end() && prefab->is_string())
                m.Prefab = prefab->get<std::string>();
            if (!m.IsDefault())
                entity.AddOrReplaceComponent<EditorMetadataComponent>(std::move(m));
        }
    }

    // Children lists are rebuilt from the "Parent" links in file order. When a parent's
    // children are in another order (reordered in the hierarchy) it is stored explicitly.
    static void WriteChildOrder(json& list, const std::vector<Entity>& entities)
    {
        std::unordered_map<uint64_t, size_t> position;
        for (size_t i = 0; i < entities.size(); i++)
            position[(uint64_t)entities[i].GetUUID()] = i;
        for (size_t i = 0; i < entities.size(); i++)
        {
            const auto& children = entities[i].GetComponent<RelationshipComponent>().Children;
            if (children.size() < 2) continue;
            std::vector<uint64_t> order;
            bool fileOrder = true;
            size_t last = 0;
            for (UUID child : children)
            {
                auto it = position.find((uint64_t)child);
                if (it == position.end()) continue; // not part of this file (copy / prefab subset)
                if (!order.empty() && it->second < last) fileOrder = false;
                last = it->second;
                order.push_back((uint64_t)child);
            }
            if (!fileOrder)
                list[i]["ChildOrder"] = order;
        }
    }

    // `idMap` maps the file's IDs to the created entities' IDs (nullptr: unchanged).
    static void ApplyChildOrder(Entity parent, const json& e, const std::unordered_map<uint64_t, UUID>* idMap)
    {
        auto it = e.find("ChildOrder");
        if (it == e.end() || !it->is_array()) return;
        auto& children = parent.GetComponent<RelationshipComponent>().Children;
        std::vector<UUID> ordered;
        ordered.reserve(children.size());
        // Membership in sets (a parent can have thousands of children).
        std::unordered_set<uint64_t> present, placed;
        present.reserve(children.size());
        for (UUID child : children)
            present.insert((uint64_t)child);
        for (const json& id : *it)
        {
            if (!id.is_number_integer()) continue;
            UUID child = UUID(id.get<uint64_t>());
            if (idMap)
            {
                auto mapped = idMap->find((uint64_t)child);
                if (mapped == idMap->end()) continue;
                child = mapped->second;
            }
            if (present.contains((uint64_t)child) && placed.insert((uint64_t)child).second)
                ordered.push_back(child);
        }
        for (UUID child : children) // anything not listed keeps the file order, after the listed ones
            if (placed.insert((uint64_t)child).second)
                ordered.push_back(child);
        children = std::move(ordered);
    }

    std::string SceneSerializer::SerializeTransform(const TransformComponent& t)
    {
        json j;
        j["ZaminTransform"] = 1;
        j["Translation"] = ToJson(t.Translation);
        j["Rotation"] = ToJson(t.Rotation);
        j["Scale"] = ToJson(t.Scale);
        return j.dump();
    }

    bool SceneSerializer::DeserializeTransform(const std::string& text, TransformComponent& out)
    {
        if (text.find("\"ZaminTransform\"") == std::string::npos)
            return false;
        try
        {
            json j = json::parse(text);
            if (!j.is_object() || !j.contains("ZaminTransform"))
                return false;
            TransformComponent t;
            t.Translation = Vec(j, "Translation", glm::vec3(0.0f));
            t.Rotation = Vec(j, "Rotation", glm::vec3(0.0f));
            t.Scale = Vec(j, "Scale", glm::vec3(1.0f));
            out = t;
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    // ------------------------------------------------------------------ component clipboard
    std::string SceneSerializer::SerializeComponent(Entity entity, std::string_view key)
    {
        if (!entity || !ComponentRegistry::Has(entity, key))
            return {};
        AssetReferenceScope assetReferences;
        json e = SerializeEntity(entity, false, false);
        auto it = e.find(std::string(key));
        if (it == e.end())
            return {};
        json j;
        j["ZaminComponent"] = std::string(key);
        j["Data"] = *it;
        return j.dump(2);
    }

    void SceneSerializer::CollectAssetPaths(const std::vector<Entity>& entities,
                                            const std::function<void(size_t, std::string_view, const std::string&)>& visit)
    {
        AssetReferenceScope assetReferences;
        std::function<void(const json&, std::string_view, size_t, std::string_view)> walk =
            [&](const json& node, std::string_view key, size_t index, std::string_view component) {
                if (node.is_object())
                {
                    for (auto it = node.begin(); it != node.end(); ++it)
                    {
                        const std::string& k = it.key();
                        const json& v = it.value();
                        if (!v.is_string())
                            walk(v, k, index, component);
                        else if (!(k.size() > 4 && k.ends_with("UUID")) && AssetFiles::IsAssetPathValue(k, v.get_ref<const std::string&>()))
                            visit(index, component, v.get_ref<const std::string&>());
                    }
                }
                else if (node.is_array())
                {
                    for (const json& item : node)
                    {
                        if (item.is_string())
                        {
                            if (AssetFiles::IsAssetPathValue(key, item.get_ref<const std::string&>()))
                                visit(index, component, item.get_ref<const std::string&>());
                        }
                        else
                            walk(item, key, index, component);
                    }
                }
            };
        for (size_t i = 0; i < entities.size(); i++)
        {
            if (!entities[i])
                continue;
            json e = SerializeEntity(entities[i], false, false);
            // The entity's own keys (name, UUID, parent, active) are no components; "Editor" is the
            // editor's data.
            for (auto it = e.begin(); it != e.end(); ++it)
                if ((it->is_object() || it->is_array()) && it.key() != "Editor")
                    walk(it.value(), it.key(), i, it.key());
        }
    }

    // The copied component's key and data, or false for any other text.
    static bool ParseComponentText(const std::string& text, std::string& key, json& data)
    {
        if (text.find("\"ZaminComponent\"") == std::string::npos) // cheap test for clipboard text
            return false;
        json j = json::parse(text, nullptr, false);
        if (j.is_discarded() || !j.is_object())
            return false;
        auto k = j.find("ZaminComponent");
        auto d = j.find("Data");
        // Data is an object for most components (an array for Tags).
        if (k == j.end() || !k->is_string() || d == j.end() || d->is_null() || !ComponentRegistry::Find(k->get<std::string>()))
            return false;
        key = k->get<std::string>();
        data = *d;
        return true;
    }

    std::string SceneSerializer::GetSerializedComponentKey(const std::string& text)
    {
        std::string key;
        json data;
        return ParseComponentText(text, key, data) ? key : std::string();
    }

    bool SceneSerializer::DeserializeComponent(const std::string& text, Entity target)
    {
        std::string key;
        json data;
        if (!target || !ParseComponentText(text, key, data))
            return false;
        // Read into a scratch entity first: a broken value fails without touching the target.
        Scene scratch;
        Entity temp = scratch.CreateEntity("Clipboard");
        AssetReferenceScope assetReferences;
        try
        {
            json e = json::object();
            e[key] = std::move(data);
            DeserializeEntityComponents(e, temp);
        }
        catch (const std::exception& ex)
        {
            ZM_CORE_ERROR("Komponent ma'lumotlari xato: {}", ex.what());
            return false;
        }
        const ComponentTypeInfo* type = ComponentRegistry::Find(key);
        if (!type || !type->Has(temp))
            return false;
        type->Copy(temp, target);
        return true;
    }

    // The scene as the JSON tree of its file (the text and the binary cache are both made from it).
    static json SceneToJson(Scene& scene)
    {
        const auto& s = scene.GetSettings();
        AssetReferenceScope assetReferences;
        json root;
        root["Scene"] = scene.GetName();
        root["Version"] = SceneMigration::CurrentVersion;
        root["Settings"] = {
            { "DrawSky", s.DrawSky }, { "SkyTop", ToJson(s.SkyTopColor) }, { "SkyHorizon", ToJson(s.SkyHorizonColor) },
            { "Ground", ToJson(s.GroundColor) }, { "Ambient", s.AmbientIntensity }, { "Exposure", s.Exposure },
            { "Sky", { { "Curve", s.Sky.Curve }, { "GroundFade", s.Sky.GroundFade }, { "SunDisc", s.Sky.SunDisc },
                       { "SunSize", s.Sky.SunSize }, { "SunIntensity", s.Sky.SunIntensity }, { "SunGlow", s.Sky.SunGlow } } },
            { "ShadowDistance", s.ShadowDistance }, { "RenderScale", s.RenderScale },
            { "Environment", { { "Map", s.EnvironmentMap }, { "Intensity", s.EnvironmentIntensity }, { "Rotation", s.EnvironmentRotation } } },
            { "Fog", { { "Enabled", s.FogEnabled }, { "Color", ToJson(s.FogColor) }, { "Density", s.FogDensity }, { "HeightFalloff", s.FogHeightFalloff } } },
            { "Shadows", { { "AtlasSize", s.Shadows.AtlasSize }, { "Filter", (int)s.Shadows.Filter }, { "Cascades", s.Shadows.CascadeCount },
                           { "CascadeSplit", s.Shadows.CascadeSplit }, { "DepthBias", s.Shadows.DepthBias }, { "NormalBias", s.Shadows.NormalBias } } },
            { "Post", {
                { "Tonemap", (int)s.Post.Tonemap }, { "Bloom", s.Post.Bloom },
                { "SSAO", s.Post.SSAO }, { "SSAORadius", s.Post.SSAORadius }, { "SSAOIntensity", s.Post.SSAOIntensity }, { "SSAOBias", s.Post.SSAOBias }, { "BloomThreshold", s.Post.BloomThreshold }, { "BloomKnee", s.Post.BloomKnee },
                { "BloomIntensity", s.Post.BloomIntensity }, { "BloomRadius", s.Post.BloomRadius },
                { "BloomTint", ToJson(s.Post.BloomTint) }, { "BloomDirt", s.Post.BloomDirt }, { "Vignette", s.Post.Vignette },
                { "ChromaticAberration", s.Post.ChromaticAberration }, { "FilmGrain", s.Post.FilmGrain },
                { "FilmGrainSize", s.Post.FilmGrainSize },
                { "Saturation", s.Post.Saturation }, { "Contrast", s.Post.Contrast }, { "Brightness", s.Post.Brightness },
                { "Tint", ToJson(s.Post.Tint) }, { "FXAA", s.Post.FXAA }, { "FXAASubpixel", s.Post.FXAASubpixel },
                { "AutoExposure", s.Post.AutoExposure }, { "AutoExposureCompensation", s.Post.AutoExposureCompensation },
                { "AutoExposureMinEV", s.Post.AutoExposureMinEV }, { "AutoExposureMaxEV", s.Post.AutoExposureMaxEV },
                { "AutoExposureSpeed", s.Post.AutoExposureSpeed } } },
        };
        WriteAssetHandle(root["Settings"]["Environment"], "Map", s.EnvironmentMap);
        if (s.Lighting2D != SceneSettings::Lighting2DSettings())
            root["Settings"]["Lighting2D"] = { { "AmbientColor", ToJson(s.Lighting2D.AmbientColor) },
                                               { "AmbientIntensity", s.Lighting2D.AmbientIntensity } };
        // Gravity and the physics step only when the scene has its own (else it follows the project:
        // Project settings > Physics / Time). A value written straight (a script, a test) that is not the
        // project's counts as the scene's own.
        const PhysicsDefaults& physics = Project::GetPhysicsDefaults();
        if (!s.UseProjectGravity || !SameGravity(s.Gravity, physics.Gravity2D))
            root["Settings"]["Gravity"] = ToJson(s.Gravity);
        if (!s.UseProjectGravity3D || !SameGravity(s.Gravity3D, physics.Gravity3D))
            root["Settings"]["Gravity3D"] = ToJson(s.Gravity3D);
        {
            const TimeSettings& time = Project::GetTimeSettings();
            SceneSettings::PhysicsStepSettings projectStep;
            projectStep.StepRate = time.FixedStepRate;
            projectStep.MaxStepsPerFrame = time.MaxStepsPerFrame;
            projectStep.Sanitize();
            if (!s.UseProjectPhysicsStep || s.PhysicsStep != projectStep)
                root["Settings"]["PhysicsStep"] = { { "Rate", s.PhysicsStep.StepRate }, { "MaxStepsPerFrame", s.PhysicsStep.MaxStepsPerFrame } };
        }
        if (s.Physics2D != SceneSettings::Physics2DSettings())
            root["Settings"]["Physics2D"] = { { "VelocityIterations", s.Physics2D.VelocityIterations },
                                              { "PositionIterations", s.Physics2D.PositionIterations },
                                              { "SubSteps", s.Physics2D.SubSteps }, { "AllowSleep", s.Physics2D.AllowSleep },
                                              { "TimeToSleep", s.Physics2D.TimeToSleep } };
        if (s.Physics3D != SceneSettings::Physics3DSettings())
            root["Settings"]["Physics3D"] = { { "AllowSleep", s.Physics3D.AllowSleep }, { "TimeToSleep", s.Physics3D.TimeToSleep } };

        // Metadata only when there is some (written by the editor's scene inspector).
        if (const SceneMetadata& meta = scene.GetMetadata(); !meta.IsEmpty())
        {
            json& j = root["Metadata"];
            j = json::object();
            if (!meta.Description.empty())
                j["Description"] = meta.Description;
            if (!meta.Author.empty())
                j["Author"] = meta.Author;
            if (!meta.Tags.empty())
                j["Tags"] = meta.Tags;
        }

        // The same scene always gives the same text (whatever order its entities were made in): sorted
        // by UUID, with the hierarchy's order written out (the roots here, the children with their parents).
        json entities = json::array();
        std::vector<Entity> ordered;
        {
            // Sorted on the IDs read once (not looked up again in every comparison).
            std::vector<std::pair<uint64_t, Entity>> keyed;
            const std::vector<Entity> all = scene.GetEntitiesInOrder();
            keyed.reserve(all.size());
            for (Entity e : all)
                keyed.emplace_back((uint64_t)e.GetUUID(), e);
            std::sort(keyed.begin(), keyed.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
            ordered.reserve(keyed.size());
            for (const auto& [id, e] : keyed)
                ordered.push_back(e);
        }
        entities.get_ref<json::array_t&>().reserve(ordered.size());
        for (Entity e : ordered)
            entities.push_back(SerializeEntity(e));
        WriteChildOrder(entities, ordered);
        json rootOrder = json::array();
        for (Entity e : scene.GetRootEntities())
            rootOrder.push_back((uint64_t)e.GetUUID());
        root["RootOrder"] = std::move(rootOrder);
        root["Entities"] = std::move(entities);
        return root;
    }

    // The JSON writer's output into one growing buffer (the library's string output appends a
    // character at a time through the string's own members).
    class TextOutput final : public nlohmann::detail::output_adapter_protocol<char>
    {
    public:
        explicit TextOutput(size_t reserve) { m_Data.reserve(reserve); }
        void write_character(char c) override
        {
            if (m_Size == m_Data.size())
                Grow(1);
            m_Data[m_Size++] = c;
        }
        void write_characters(const char* s, std::size_t length) override
        {
            if (m_Size + length > m_Data.size())
                Grow(length);
            std::memcpy(m_Data.data() + m_Size, s, length);
            m_Size += length;
        }
        std::string Take() const { return std::string(m_Data.data(), m_Size); }

    private:
        void Grow(size_t more) { m_Data.resize(std::max(m_Data.size() * 2, m_Size + more + 4096)); }
        std::vector<char> m_Data;
        size_t m_Size = 0;
    };

    // `root` as indented text (what json::dump(2) gives).
    static std::string DumpIndented(const json& root, size_t sizeHint)
    {
        auto output = std::make_shared<TextOutput>(sizeHint);
        nlohmann::detail::serializer<json> writer(output, ' ', nlohmann::detail::error_handler_t::strict);
        writer.dump(root, true, false, 2);
        return output->Take();
    }

    std::string SceneSerializer::SerializeToString()
    {
        // About 900 bytes of text per entity (sprites, bodies ...): one allocation for most scenes.
        return DumpIndented(SceneToJson(*m_Scene), 4096 + 1024 * m_Scene->GetRegistry().view<IDComponent>().size());
    }

    // ------------------------------------------------------------------ binary cache (.zsceneb)
    // "ZMSB", the cache format's version, the size of the scene text it was made from (0: not made
    // from a file), then the scene's JSON tree as MessagePack.
    static constexpr uint8_t s_BinaryMagic[4] = { 'Z', 'M', 'S', 'B' };
    static constexpr uint8_t s_BinaryFormat = 1;
    static constexpr size_t s_BinaryHeaderSize = 4 + 1 + 8;
    static std::atomic<bool> s_UseBinaryCache = true;

    static std::vector<uint8_t> MakeBinary(const json& root, uint64_t sourceSize)
    {
        std::vector<uint8_t> bytes;
        bytes.reserve(s_BinaryHeaderSize);
        bytes.insert(bytes.end(), std::begin(s_BinaryMagic), std::end(s_BinaryMagic));
        bytes.push_back(s_BinaryFormat);
        for (int i = 0; i < 8; i++)
            bytes.push_back((uint8_t)(sourceSize >> (8 * i)));
        json::to_msgpack(root, bytes);
        return bytes;
    }

    bool SceneSerializer::IsBinary(const void* data, size_t size)
    {
        return data && size >= s_BinaryHeaderSize && std::memcmp(data, s_BinaryMagic, 4) == 0;
    }

    // The size of the text a cache was made from (0: unknown), and whether the header is one this
    // engine reads.
    static bool ReadBinaryHeader(const uint8_t* data, size_t size, uint64_t& sourceSize)
    {
        if (!SceneSerializer::IsBinary(data, size) || data[4] != s_BinaryFormat)
            return false;
        sourceSize = 0;
        for (int i = 0; i < 8; i++)
            sourceSize |= (uint64_t)data[5 + i] << (8 * i);
        return true;
    }

    // The cache's MessagePack back into a JSON tree. The library's reader takes a string a byte at a
    // time; this one copies strings whole and appends an object's keys at its end (they were written
    // sorted), so the cache is read about twice as fast as the text. Throws std::runtime_error on
    // data that is not well-formed (sizes are checked against what is left: nothing is over-allocated).
    class MessagePackReader
    {
    public:
        MessagePackReader(const uint8_t* begin, const uint8_t* end) : m_At(begin), m_End(end) {}

        void Read(json& out, int depth = 0)
        {
            if (depth > 512)
                Fail("juda chuqur ichma-ich tuzilma");
            const uint8_t b = Byte();
            if (b <= 0x7f) { out = (uint64_t)b; return; }
            if (b >= 0xe0) { out = (int64_t)(int8_t)b; return; }
            if ((b & 0xf0) == 0x80) return ReadMap(out, b & 0x0f, depth);
            if ((b & 0xf0) == 0x90) return ReadArray(out, b & 0x0f, depth);
            if ((b & 0xe0) == 0xa0) return ReadString(out, b & 0x1f);
            switch (b)
            {
                case 0xc0: out = nullptr; return;
                case 0xc2: out = false; return;
                case 0xc3: out = true; return;
                case 0xc4: return ReadBinary(out, Unsigned(1));
                case 0xc5: return ReadBinary(out, Unsigned(2));
                case 0xc6: return ReadBinary(out, Unsigned(4));
                case 0xca:
                {
                    const uint32_t bits = (uint32_t)Unsigned(4);
                    float value;
                    std::memcpy(&value, &bits, sizeof(value));
                    out = (double)value;
                    return;
                }
                case 0xcb:
                {
                    const uint64_t bits = Unsigned(8);
                    double value;
                    std::memcpy(&value, &bits, sizeof(value));
                    out = value;
                    return;
                }
                case 0xcc: out = Unsigned(1); return;
                case 0xcd: out = Unsigned(2); return;
                case 0xce: out = Unsigned(4); return;
                case 0xcf: out = Unsigned(8); return;
                case 0xd0: out = (int64_t)(int8_t)Unsigned(1); return;
                case 0xd1: out = (int64_t)(int16_t)Unsigned(2); return;
                case 0xd2: out = (int64_t)(int32_t)Unsigned(4); return;
                case 0xd3: out = (int64_t)Unsigned(8); return;
                case 0xd9: return ReadString(out, Unsigned(1));
                case 0xda: return ReadString(out, Unsigned(2));
                case 0xdb: return ReadString(out, Unsigned(4));
                case 0xdc: return ReadArray(out, Unsigned(2), depth);
                case 0xdd: return ReadArray(out, Unsigned(4), depth);
                case 0xde: return ReadMap(out, Unsigned(2), depth);
                case 0xdf: return ReadMap(out, Unsigned(4), depth);
                default: break;
            }
            Fail(std::format("noma'lum MessagePack turi 0x{:02x}", b));
        }
        bool AtEnd() const { return m_At == m_End; }

    private:
        [[noreturn]] void Fail(const std::string& why) const { throw std::runtime_error(why); }
        size_t Left() const { return (size_t)(m_End - m_At); }
        uint8_t Byte()
        {
            if (m_At >= m_End)
                Fail("ma'lumot kutilmaganda tugadi");
            return *m_At++;
        }
        uint64_t Unsigned(int bytes) // big-endian
        {
            if (Left() < (size_t)bytes)
                Fail("ma'lumot kutilmaganda tugadi");
            uint64_t value = 0;
            for (int i = 0; i < bytes; i++)
                value = (value << 8) | m_At[i];
            m_At += bytes;
            return value;
        }
        std::string Text(uint64_t length)
        {
            if (length > Left())
                Fail("satr uzunligi ma'lumotdan katta");
            std::string text((const char*)m_At, (size_t)length);
            m_At += length;
            return text;
        }
        void ReadString(json& out, uint64_t length) { out = Text(length); }
        void ReadBinary(json& out, uint64_t length)
        {
            if (length > Left())
                Fail("binar uzunligi ma'lumotdan katta");
            out = json::binary(std::vector<uint8_t>(m_At, m_At + length));
            m_At += length;
        }
        void ReadArray(json& out, uint64_t count, int depth)
        {
            if (count > Left())
                Fail("ro'yxat uzunligi ma'lumotdan katta");
            out = json::array();
            json::array_t& items = out.get_ref<json::array_t&>();
            items.resize((size_t)count);
            for (json& item : items)
                Read(item, depth + 1);
        }
        void ReadMap(json& out, uint64_t count, int depth)
        {
            if (count > Left() / 2)
                Fail("obyekt kalitlari soni ma'lumotdan katta");
            out = json::object();
            json::object_t& members = out.get_ref<json::object_t&>();
            for (uint64_t i = 0; i < count; i++)
            {
                const uint8_t b = Byte();
                uint64_t length = 0;
                if ((b & 0xe0) == 0xa0)
                    length = b & 0x1f;
                else if (b == 0xd9)
                    length = Unsigned(1);
                else if (b == 0xda)
                    length = Unsigned(2);
                else if (b == 0xdb)
                    length = Unsigned(4);
                else
                    Fail("obyekt kaliti satr emas");
                // Written in sorted order: each key goes at the end in constant time.
                json& value = members.emplace_hint(members.end(), Text(length), nullptr)->second;
                Read(value, depth + 1);
            }
        }

        const uint8_t* m_At;
        const uint8_t* m_End;
    };

    static json ParseMessagePack(const uint8_t* begin, const uint8_t* end)
    {
        MessagePackReader reader(begin, end);
        json root;
        reader.Read(root);
        if (!reader.AtEnd())
            throw std::runtime_error("ma'lumot oxirida ortiqcha baytlar bor");
        return root;
    }

    std::vector<uint8_t> SceneSerializer::SerializeToBinary()
    {
        return MakeBinary(SceneToJson(*m_Scene), 0);
    }

    std::filesystem::path SceneSerializer::GetBinaryCachePath(const std::filesystem::path& sceneFile)
    {
        std::filesystem::path cache = sceneFile;
        cache.replace_extension(BinaryExtension);
        return cache;
    }

    bool SceneSerializer::WriteBinaryCache(const std::filesystem::path& sceneFile, std::string* error)
    {
        auto fail = [&](std::string reason) {
            if (error)
                *error = std::move(reason);
            return false;
        };
        std::string text = AssetImportLog::CheckReadable(sceneFile);
        if (!text.empty())
            return fail(text);
        text = FileSystem::ReadTextFile(sceneFile);
        if (text.empty())
            return fail("fayl o'qilmadi");
        json root = json::parse(text, nullptr, false);
        if (root.is_discarded() || !root.is_object() || !root.contains("Scene"))
            return fail("sahna fayli emas yoki JSON xato");
        // Stored already migrated: the player reads the current format straight away.
        SceneMigrationJson::Migrate(root);
        const std::vector<uint8_t> bytes = MakeBinary(root, text.size());
        if (!FileSystem::WriteBinaryFile(GetBinaryCachePath(sceneFile), bytes.data(), bytes.size()))
            return fail("kesh yozilmadi");
        return true;
    }

    void SceneSerializer::SetUseBinaryCache(bool use) { s_UseBinaryCache = use; }
    bool SceneSerializer::GetUseBinaryCache() { return s_UseBinaryCache; }

    // The cache next to a scene file when it can stand in for it: made from a text of the file's
    // current size and not older than it (else the text is read).
    static bool FindValidBinaryCache(const std::filesystem::path& sceneFile, std::filesystem::path& cache)
    {
        if (!s_UseBinaryCache || sceneFile.extension() == SceneSerializer::BinaryExtension)
            return false;
        cache = SceneSerializer::GetBinaryCachePath(sceneFile);
        std::error_code ec;
        if (!std::filesystem::is_regular_file(cache, ec))
            return false;
        const bool hasText = std::filesystem::is_regular_file(sceneFile, ec);
        if (!hasText)
            return true; // only the cache was shipped
        std::ifstream in(cache, std::ios::binary);
        uint8_t header[s_BinaryHeaderSize] = {};
        uint64_t sourceSize = 0;
        if (!in.read((char*)header, sizeof(header)) || !ReadBinaryHeader(header, sizeof(header), sourceSize))
            return false;
        const auto textSize = std::filesystem::file_size(sceneFile, ec);
        if (ec || sourceSize != (uint64_t)textSize)
            return false;
        const auto textTime = std::filesystem::last_write_time(sceneFile, ec);
        const auto cacheTime = std::filesystem::last_write_time(cache, ec);
        return !ec && cacheTime >= textTime;
    }

    bool SceneSerializer::Serialize(const std::filesystem::path& filepath)
    {
        if (m_Scene->GetName().empty() || m_Scene->GetName() == "Untitled")
            m_Scene->SetName(filepath.stem().string());
        return FileSystem::WriteTextFile(filepath, SerializeToString());
    }

    static SceneLoadIssue MakeUnreadableIssue(const std::string& reason);
    static std::string SceneDisplayPath(const std::filesystem::path& file);

    bool SceneSerializer::Deserialize(const std::filesystem::path& filepath)
    {
        m_Report.Clear();
        m_Report.File = filepath.generic_string();
        // A binary cache next to the file (an exported game's) is read instead when it is current.
        std::filesystem::path cache;
        bool fromCache = FindValidBinaryCache(filepath, cache);
        bool ok = false;
        size_t bytes = 0;
        double readMs = 0.0;
        for (int attempt = 0; attempt < 2 && !ok; attempt++)
        {
            const std::filesystem::path& source = fromCache ? cache : filepath;
            m_Report.Clear();
            m_Report.File = filepath.generic_string();
            const auto readStart = std::chrono::steady_clock::now();
            m_LastError = AssetImportLog::CheckReadable(source);
            std::string data = m_LastError.empty() ? FileSystem::ReadTextFile(source) : std::string();
            readMs = ElapsedMilliseconds(readStart);
            bytes = data.size();
            if (data.empty() && m_LastError.empty())
                m_LastError = "fayl o'qilmadi";
            if (m_LastError.empty())
            {
                m_QuietErrors = true;
                ok = IsBinary(data.data(), data.size()) ? DeserializeFromMemory((const uint8_t*)data.data(), data.size())
                                                        : DeserializeFromString(data);
                m_QuietErrors = false;
            }
            else
            {
                m_Timing = {};
                m_Report.Add(MakeUnreadableIssue(m_LastError));
            }
            if (ok || !fromCache)
                break;
            // A cache that cannot be read (nothing was made from it): the text instead.
            ZM_CORE_WARN("Sahnaning binar keshi o'qilmadi ({}): matn fayli o'qiladi - {}", m_LastError, cache.filename().string());
            fromCache = false;
        }
        m_Timing[SceneLoadTiming::Stage::Read] = readMs;
        m_Timing.FileBytes = bytes;
        m_Timing.Failed = !ok;
        if (!ok)
        {
            // Logged once with the file's path (and the line the parser stopped at).
            if (AssetImportLog::Report(filepath, m_LastError))
                if (const SceneLoadIssue* main = m_Report.GetMain(); main && !main->SourceLine.empty())
                    ZM_CORE_ERROR("    {}", main->SourceLine);
            return false;
        }
        AssetImportLog::Clear(filepath);
        if (m_Scene->GetName().empty() || m_Scene->GetName() == "Untitled")
        {
            std::filesystem::path stem = filepath.stem();
            m_Scene->SetName(stem.string());
        }
        SceneMigration::LogMigration(SceneDisplayPath(filepath), m_Migration);
        m_Report.Log(); // what was left out (a clean load says nothing)
        return true;
    }

    // ------------------------------------------------------------------ scene files in steps
    // "Metadata" (absent: none). What is not text is left out.
    static SceneMetadata ReadSceneMetadata(const json& root)
    {
        SceneMetadata meta;
        auto it = root.find("Metadata");
        if (it == root.end() || !it->is_object())
            return meta;
        const json& j = *it;
        if (auto d = j.find("Description"); d != j.end() && d->is_string())
            meta.Description = d->get<std::string>();
        if (auto a = j.find("Author"); a != j.end() && a->is_string())
            meta.Author = a->get<std::string>();
        if (auto tags = j.find("Tags"); tags != j.end() && tags->is_array())
            for (const json& tag : *tags)
                if (tag.is_string())
                    meta.AddTag(tag.get_ref<const std::string&>());
        return meta;
    }

    // The scene's name, metadata and settings ("Scene", "Metadata", "Settings").
    static void DeserializeSceneSettings(const json& root, Scene& scene)
    {
        scene.SetName(root.value("Scene", "Untitled"));
        scene.GetMetadata() = ReadSceneMetadata(root);
        if (root.contains("Settings"))
        {
            const json& j = root["Settings"];
            auto& s = scene.GetSettings();
            s.DrawSky = j.value("DrawSky", s.DrawSky);
            s.SkyTopColor = Vec(j, "SkyTop", s.SkyTopColor);
            s.SkyHorizonColor = Vec(j, "SkyHorizon", s.SkyHorizonColor);
            s.GroundColor = Vec(j, "Ground", s.GroundColor);
            // Older scenes have none: the original gradient and sun disc.
            s.Sky = {};
            if (j.contains("Sky") && j["Sky"].is_object())
            {
                const json& sky = j["Sky"];
                s.Sky.Curve = sky.value("Curve", s.Sky.Curve);
                s.Sky.GroundFade = sky.value("GroundFade", s.Sky.GroundFade);
                s.Sky.SunDisc = sky.value("SunDisc", s.Sky.SunDisc);
                s.Sky.SunSize = sky.value("SunSize", s.Sky.SunSize);
                s.Sky.SunIntensity = sky.value("SunIntensity", s.Sky.SunIntensity);
                s.Sky.SunGlow = sky.value("SunGlow", s.Sky.SunGlow);
                s.Sky.Sanitize();
            }
            s.AmbientIntensity = j.value("Ambient", s.AmbientIntensity);
            s.Exposure = j.value("Exposure", s.Exposure);
            s.ShadowDistance = j.value("ShadowDistance", s.ShadowDistance);
            // Older scenes have none: 1 (the picture at the view's own size).
            float renderScale = j.value("RenderScale", 1.0f);
            s.RenderScale = std::isfinite(renderScale) ? std::clamp(renderScale, Renderer::MinRenderScale, Renderer::MaxRenderScale) : 1.0f;
            // Gravity: the scene's own when the file has it, else the project's (Project settings > Physics).
            // (Older files always had it: the migration to v4 took the engine's value out of them.)
            s.UseProjectGravity = !j.contains("Gravity");
            s.UseProjectGravity3D = !j.contains("Gravity3D");
            s.Gravity = Vec(j, "Gravity", s.Gravity);
            s.Gravity3D = Vec(j, "Gravity3D", s.Gravity3D);
            // The physics step: the scene's own when it has the section (older files wrote it only when it
            // was changed), else the project's (Project settings > Time: 60 steps a second, at most 5 a
            // frame without one). Box2D: 6 / 2 iterations, one sub-step, sleeping after 0.5 s. Out-of-range
            // values are clamped.
            s.PhysicsStep = {};
            s.UseProjectPhysicsStep = true;
            if (auto ps = j.find("PhysicsStep"); ps != j.end() && ps->is_object())
            {
                s.PhysicsStep.StepRate = ps->value("Rate", s.PhysicsStep.StepRate);
                s.PhysicsStep.MaxStepsPerFrame = ps->value("MaxStepsPerFrame", s.PhysicsStep.MaxStepsPerFrame);
                s.UseProjectPhysicsStep = false;
            }
            s.PhysicsStep.Sanitize();
            s.ApplyProjectDefaults();
            s.Physics2D = {};
            if (auto p2 = j.find("Physics2D"); p2 != j.end() && p2->is_object())
            {
                s.Physics2D.VelocityIterations = p2->value("VelocityIterations", s.Physics2D.VelocityIterations);
                s.Physics2D.PositionIterations = p2->value("PositionIterations", s.Physics2D.PositionIterations);
                s.Physics2D.SubSteps = p2->value("SubSteps", s.Physics2D.SubSteps);
                s.Physics2D.AllowSleep = p2->value("AllowSleep", s.Physics2D.AllowSleep);
                s.Physics2D.TimeToSleep = p2->value("TimeToSleep", s.Physics2D.TimeToSleep);
            }
            s.Physics2D.Sanitize();
            s.Physics3D = {};
            if (auto p3 = j.find("Physics3D"); p3 != j.end() && p3->is_object())
            {
                s.Physics3D.AllowSleep = p3->value("AllowSleep", s.Physics3D.AllowSleep);
                s.Physics3D.TimeToSleep = p3->value("TimeToSleep", s.Physics3D.TimeToSleep);
            }
            s.Physics3D.Sanitize();
            // Older scenes have none: white at 1 (Lit renderers look unlit).
            s.Lighting2D = {};
            if (auto l2d = j.find("Lighting2D"); l2d != j.end() && l2d->is_object())
            {
                s.Lighting2D.AmbientColor = glm::max(Vec(*l2d, "AmbientColor", glm::vec3(1.0f)), glm::vec3(0.0f));
                s.Lighting2D.AmbientIntensity = std::max(0.0f, l2d->value("AmbientIntensity", 1.0f));
            }
            if (j.contains("Environment"))
            {
                const json& en = j["Environment"];
                s.EnvironmentMap = ReadAssetPath(en, "Map");
                s.EnvironmentIntensity = en.value("Intensity", 1.0f);
                s.EnvironmentRotation = en.value("Rotation", 0.0f);
            }
            if (j.contains("Fog"))
            {
                const json& f = j["Fog"];
                s.FogEnabled = f.value("Enabled", false);
                s.FogColor = Vec(f, "Color", s.FogColor);
                s.FogDensity = f.value("Density", 0.015f);
                s.FogHeightFalloff = f.value("HeightFalloff", 0.05f);
            }
            // Older scenes have none: the renderer's original fixed quality.
            s.Shadows = {};
            if (j.contains("Shadows") && j["Shadows"].is_object())
            {
                const json& sh = j["Shadows"];
                s.Shadows.AtlasSize = sh.value("AtlasSize", s.Shadows.AtlasSize);
                int filter = sh.value("Filter", (int)s.Shadows.Filter);
                s.Shadows.Filter = filter >= 0 && filter < ShadowFilterCount ? (ShadowFilter)filter : ShadowFilter::PCF3x3;
                s.Shadows.CascadeCount = sh.value("Cascades", s.Shadows.CascadeCount);
                s.Shadows.CascadeSplit = sh.value("CascadeSplit", s.Shadows.CascadeSplit);
                s.Shadows.DepthBias = sh.value("DepthBias", s.Shadows.DepthBias);
                s.Shadows.NormalBias = sh.value("NormalBias", s.Shadows.NormalBias);
                s.Shadows.Sanitize();
            }
            if (j.contains("Post"))
            {
                const json& p = j["Post"];
                s.Post.Tonemap = (TonemapMode)p.value("Tonemap", 1);
                s.Post.Bloom = p.value("Bloom", false);
                s.Post.SSAO = p.value("SSAO", false);
                s.Post.SSAORadius = p.value("SSAORadius", 0.6f);
                s.Post.SSAOIntensity = p.value("SSAOIntensity", 1.0f);
                s.Post.SSAOBias = p.value("SSAOBias", 0.03f);
                s.Post.BloomThreshold = p.value("BloomThreshold", 1.0f);
                s.Post.BloomKnee = p.value("BloomKnee", 0.5f);
                s.Post.BloomIntensity = p.value("BloomIntensity", 0.6f);
                s.Post.BloomRadius = p.value("BloomRadius", 1.0f);
                s.Post.BloomTint = Vec(p, "BloomTint", glm::vec3(1.0f));
                s.Post.BloomDirt = p.value("BloomDirt", 0.0f);
                s.Post.ChromaticAberration = p.value("ChromaticAberration", 0.0f);
                s.Post.FilmGrain = p.value("FilmGrain", 0.0f);
                s.Post.FilmGrainSize = p.value("FilmGrainSize", 1.0f);
                s.Post.SanitizeLensEffects();
                s.Post.Vignette = p.value("Vignette", 0.0f);
                s.Post.Saturation = p.value("Saturation", 1.0f);
                s.Post.Contrast = p.value("Contrast", 1.0f);
                s.Post.Brightness = p.value("Brightness", 0.0f);
                s.Post.Tint = Vec(p, "Tint", glm::vec3(1.0f));
                s.Post.FXAA = p.value("FXAA", false);
                s.Post.FXAASubpixel = std::clamp(p.value("FXAASubpixel", 0.75f), 0.0f, 1.0f);
                s.Post.AutoExposure = p.value("AutoExposure", false);
                s.Post.AutoExposureCompensation = p.value("AutoExposureCompensation", 0.0f);
                s.Post.AutoExposureMinEV = p.value("AutoExposureMinEV", -4.0f);
                s.Post.AutoExposureMaxEV = p.value("AutoExposureMaxEV", 4.0f);
                s.Post.AutoExposureSpeed = p.value("AutoExposureSpeed", 2.0f);
                s.Post.SanitizeAutoExposure();
            }
        }
    }

    // ------------------------------------------------------------------ load reports
    const char* SceneLoadIssue::KindName(Kind kind)
    {
        switch (kind)
        {
            case Kind::FileMissing: return "Fayl topilmadi";
            case Kind::Unreadable: return "Fayl o'qilmadi";
            case Kind::ParseError: return "JSON xatosi";
            case Kind::NotAScene: return "Sahna fayli emas";
            case Kind::BadEntity: return "Obyekt xato";
            case Kind::BadComponent: return "Komponent xato";
            case Kind::UnknownComponent: return "Noma'lum komponent";
            case Kind::MissingAsset: return "Asset topilmadi";
            case Kind::DuplicateID: return "Takroriy ID";
            case Kind::BadHierarchy: return "Ierarxiya xato";
            case Kind::NewerVersion: return "Yangiroq format";
        }
        return "?";
    }

    bool SceneLoadReport::Failed() const
    {
        return std::any_of(Issues.begin(), Issues.end(), [](const SceneLoadIssue& i) { return i.Level == SceneLoadIssue::Severity::Fatal; });
    }

    size_t SceneLoadReport::Count(SceneLoadIssue::Severity level) const
    {
        return (size_t)std::count_if(Issues.begin(), Issues.end(), [&](const SceneLoadIssue& i) { return i.Level == level; });
    }

    const SceneLoadIssue* SceneLoadReport::GetMain() const
    {
        const SceneLoadIssue* main = nullptr;
        for (const SceneLoadIssue& issue : Issues)
            if (!main || issue.Level > main->Level)
                main = &issue;
        return main;
    }

    std::string SceneLoadReport::Summary() const
    {
        if (Failed())
            return GetMain()->Message;
        const size_t errors = Count(SceneLoadIssue::Severity::Error), warnings = Count(SceneLoadIssue::Severity::Warning);
        if (errors && warnings)
            return std::format("{} ta xato, {} ta ogohlantirish", errors, warnings);
        if (errors)
            return std::format("{} ta xato", errors);
        if (warnings)
            return std::format("{} ta ogohlantirish", warnings);
        return "muammo yo'q";
    }

    std::string SceneLoadReport::Format(size_t maxLines) const
    {
        std::string text;
        size_t shown = 0;
        for (const SceneLoadIssue& issue : Issues)
        {
            if (shown == maxLines)
            {
                text += std::format("... va yana {} ta\n", Issues.size() - shown);
                break;
            }
            const char* level = issue.Level == SceneLoadIssue::Severity::Warning ? "ogohlantirish" : "xato";
            text += std::format("[{}] {}\n", level, issue.Message);
            if (!issue.SourceLine.empty())
                text += std::format("    {}\n", issue.SourceLine);
            shown++;
        }
        return text;
    }

    void SceneLoadReport::Log() const
    {
        if (Issues.empty())
            return;
        const std::string name = File.empty() ? std::string("(matn)") : std::filesystem::path(File).filename().string();
        if (Failed())
            ZM_CORE_ERROR("Sahna yuklanmadi: {} - {}", name, Summary());
        else if (Count(SceneLoadIssue::Severity::Error))
            ZM_CORE_ERROR("Sahna '{}' qisman yuklandi ({}): yaroqli obyektlar qoldi", name, Summary());
        else
            ZM_CORE_WARN("Sahna '{}' yuklandi, {}", name, Summary());
        constexpr size_t MaxLogged = 20;
        size_t logged = 0;
        for (const SceneLoadIssue& issue : Issues)
        {
            if (issue.Level == SceneLoadIssue::Severity::Fatal)
            {
                if (!issue.SourceLine.empty())
                    ZM_CORE_ERROR("    {}", issue.SourceLine);
                continue;
            }
            if (logged++ == MaxLogged)
            {
                ZM_CORE_WARN("  ... va yana {} ta muammo", Issues.size() - MaxLogged);
                break;
            }
            if (issue.Level == SceneLoadIssue::Severity::Error)
                ZM_CORE_ERROR("  - {}", issue.Message);
            else
                ZM_CORE_WARN("  - {}", issue.Message);
        }
    }

    void SceneLoadReport::Add(SceneLoadIssue issue) { Issues.push_back(std::move(issue)); }

    // An exception's text without the JSON library's "[json.exception.type_error.302] " prefix.
    static std::string DescribeException(const std::exception& ex)
    {
        std::string what = ex.what();
        if (size_t bracket = what.find("] "); what.starts_with("[json.exception") && bracket != std::string::npos)
            what = what.substr(bracket + 2);
        return what;
    }

    static SceneLoadIssue MakeIssue(SceneLoadIssue::Kind kind, SceneLoadIssue::Severity level, std::string message)
    {
        SceneLoadIssue issue;
        issue.Type = kind;
        issue.Level = level;
        issue.Message = std::move(message);
        return issue;
    }

    // A file that cannot be read at all (AssetImportLog::CheckReadable's reason).
    static SceneLoadIssue MakeUnreadableIssue(const std::string& reason)
    {
        return MakeIssue(reason == "fayl topilmadi" ? SceneLoadIssue::Kind::FileMissing : SceneLoadIssue::Kind::Unreadable,
                         SceneLoadIssue::Severity::Fatal, reason);
    }

    // Where in `text` the parser stopped (1-based line / column), that line, and the parser's reason.
    static SceneLoadIssue MakeParseIssue(const std::string& text, const std::exception& ex)
    {
        SceneLoadIssue issue = MakeIssue(SceneLoadIssue::Kind::ParseError, SceneLoadIssue::Severity::Fatal, {});
        std::string reason = ex.what();
        // "[json.exception.parse_error.101] parse error at line 3, column 5: syntax error ..." -> "syntax error ..."
        if (size_t bracket = reason.find("] "); reason.starts_with("[json.exception") && bracket != std::string::npos)
            reason = reason.substr(bracket + 2);
        if (size_t colon = reason.find(": "); reason.starts_with("parse error") && colon != std::string::npos)
            reason = reason.substr(colon + 2);
        if (const auto* parse = dynamic_cast<const json::parse_error*>(&ex); parse && parse->byte > 0)
        {
            // `byte` counts from 1 (past the end: the input ended).
            const size_t at = std::min<size_t>((size_t)parse->byte - 1, text.size());
            const size_t lineStart = at == 0 ? 0 : (text.rfind('\n', at - 1) == std::string::npos ? 0 : text.rfind('\n', at - 1) + 1);
            issue.Line = 1 + (int)std::count(text.begin(), text.begin() + (ptrdiff_t)lineStart, '\n');
            issue.Column = 1 + (int)(at - lineStart);
            size_t lineEnd = text.find('\n', lineStart);
            std::string line = text.substr(lineStart, (lineEnd == std::string::npos ? text.size() : lineEnd) - lineStart);
            std::replace(line.begin(), line.end(), '\t', ' ');
            while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
                line.pop_back();
            if (line.size() > 160)
                line = line.substr(0, 160) + "...";
            issue.SourceLine = std::move(line);
            issue.Message = std::format("JSON xatosi {}-qator, {}-ustunda: {}", issue.Line, issue.Column, reason);
        }
        else
            issue.Message = std::format("JSON xatosi: {}", reason);
        return issue;
    }

    static SceneLoadIssue MakeNotASceneIssue()
    {
        return MakeIssue(SceneLoadIssue::Kind::NotAScene, SceneLoadIssue::Severity::Fatal, "sahna fayli emas (\"Scene\" kaliti yo'q)");
    }

    // A scene file as the log names it: relative to the assets when it is one of the project's.
    static std::string SceneDisplayPath(const std::filesystem::path& file)
    {
        if (Project::GetActive())
        {
            std::string relative = Project::GetAssetRelativePath(file);
            if (!relative.empty() && !relative.starts_with("..") && std::filesystem::path(relative).is_relative())
                return relative;
        }
        return file.generic_string();
    }

    // The parsed file in the current format (SceneMigration); a newer file is reported (read as far
    // as it is understood).
    static SceneMigrationResult MigrateSceneRoot(json& root, SceneLoadReport* report)
    {
        SceneMigrationResult migration = SceneMigrationJson::Migrate(root);
        if (migration.IsNewer() && report)
            report->Add(MakeIssue(SceneLoadIssue::Kind::NewerVersion, SceneLoadIssue::Severity::Warning,
                                  std::format("Fayl yangiroq formatda (v{}, bu dvigatel v{} gacha biladi): tanilmagan ma'lumotlar o'qilmadi "
                                              "va saqlanganda yo'qoladi", migration.FileVersion, SceneMigration::CurrentVersion)));
        return migration;
    }

    static void AddEntityIssue(SceneLoadReport* report, SceneLoadIssue::Kind kind, SceneLoadIssue::Severity level, std::string message,
                               const std::string& entity, uint64_t id, std::string component = {})
    {
        if (!report)
            return;
        SceneLoadIssue issue = MakeIssue(kind, level, std::move(message));
        issue.Entity = entity;
        issue.EntityID = id;
        issue.Component = std::move(component);
        report->Add(std::move(issue));
    }

    // Keys of an entity in a scene file: its own ones and the components'.
    static bool IsKnownEntityKey(std::string_view key)
    {
        static const std::unordered_set<std::string_view> s_Keys = []() {
            std::unordered_set<std::string_view> keys{ "Entity", "Tag", "Active", "Parent", "ChildOrder", "Editor" };
            for (const ComponentTypeInfo& type : ComponentRegistry::GetTypes())
                keys.insert(type.Key);
            return keys;
        }();
        return s_Keys.contains(key);
    }

    // An entity whose components threw while they were read (a value of the wrong type ...): it starts
    // over bare, then each component is read on its own - the broken ones are left out, the rest stay.
    static void DeserializeComponentsOneByOne(const json& e, Entity entity, SceneLoadReport* report)
    {
        for (const ComponentTypeInfo& type : ComponentRegistry::GetTypes())
            if (type.Key != "Transform" && type.Has(entity)) // every entity keeps its transform
                type.Remove(entity);
        if (entity.HasComponent<EditorMetadataComponent>())
            entity.RemoveComponent<EditorMetadataComponent>();
        entity.GetComponent<TransformComponent>() = TransformComponent();
        entity.GetComponent<TagComponent>().Active = true;

        std::vector<std::string_view> keys{ "Active" };
        for (const ComponentTypeInfo& type : ComponentRegistry::GetTypes())
            keys.push_back(type.Key);
        keys.push_back("Editor");
        AssetReferenceScope* scope = AssetReferenceScope::Get();
        const std::string name = entity.GetName();
        const uint64_t id = (uint64_t)entity.GetUUID();
        for (std::string_view key : keys)
        {
            auto it = e.find(key);
            if (it == e.end())
                continue;
            json single = json::object();
            single[std::string(key)] = *it;
            if (scope)
                scope->SetEntity(&single, name, id);
            try
            {
                DeserializeEntityComponents(single, entity);
            }
            catch (const std::exception& ex)
            {
                if (key == "Transform")
                    entity.GetComponent<TransformComponent>() = TransformComponent();
                else if (key == "Editor")
                {
                    if (entity.HasComponent<EditorMetadataComponent>())
                        entity.RemoveComponent<EditorMetadataComponent>();
                }
                else if (key != "Active")
                    ComponentRegistry::Remove(entity, key);
                AddEntityIssue(report, SceneLoadIssue::Kind::BadComponent, SceneLoadIssue::Severity::Error,
                               std::format("'{}' obyektining '{}' komponenti o'qilmadi ({}): usiz yuklandi", name, key, DescribeException(ex)),
                               name, id, std::string(key));
            }
        }
        if (scope)
            scope->SetEntity(&e, name, id);
    }

    // An entity of a scene file as it is made: where it came from and its parent's ID in the file.
    struct LoadedEntity
    {
        Entity Handle;
        uint64_t Parent = 0;
        const json* Source = nullptr;
    };

    static uint64_t ReadEntityID(const json& e, const char* key)
    {
        auto it = e.find(key);
        if (it == e.end())
            return 0;
        if (it->is_number_unsigned())
            return it->get<uint64_t>();
        if (it->is_number_integer() && it->get<int64_t>() > 0)
            return (uint64_t)it->get<int64_t>();
        return 0;
    }

    // One entry of a scene file's "Entities" (`index`: its place in the file). Whatever of it can be
    // read is made (see SceneLoadIssue); false when it is not an entity at all.
    static bool DeserializeSceneEntity(const json& e, size_t index, Scene& scene, std::vector<LoadedEntity>& loaded, SceneLoadReport* report)
    {
        if (!e.is_object())
        {
            if (report)
            {
                report->EntitiesSkipped++;
                AddEntityIssue(report, SceneLoadIssue::Kind::BadEntity, SceneLoadIssue::Severity::Error,
                               std::format("\"Entities\" ro'yxatining {}-yozuvi obyekt emas ({}): o'tkazib yuborildi", index + 1, e.type_name()), {}, 0);
            }
            return false;
        }
        std::string name = "Entity";
        if (auto tag = e.find("Tag"); tag != e.end() && tag->is_string() && !tag->get_ref<const std::string&>().empty())
            name = tag->get<std::string>();
        uint64_t uuid = ReadEntityID(e, "Entity");
        if (!uuid && e.contains("Entity"))
            AddEntityIssue(report, SceneLoadIssue::Kind::BadEntity, SceneLoadIssue::Severity::Warning,
                           std::format("'{}' obyektining ID'si xato: yangi ID berildi", name), name, 0);
        if (uuid && scene.GetEntityByUUID(uuid))
        {
            AddEntityIssue(report, SceneLoadIssue::Kind::DuplicateID, SceneLoadIssue::Severity::Warning,
                           std::format("'{}' obyektining ID'si ({}) takrorlangan: yangi ID berildi", name, uuid), name, uuid);
            uuid = 0;
        }
        Entity entity = scene.CreateEntityWithUUID(uuid ? UUID(uuid) : UUID(), name);
        const uint64_t id = (uint64_t)entity.GetUUID();
        AssetReferenceScope* scope = AssetReferenceScope::Get();
        if (scope)
            scope->SetEntity(&e, name, id);
        if (report)
            for (auto it = e.begin(); it != e.end(); ++it)
                if (!IsKnownEntityKey(it.key()))
                    AddEntityIssue(report, SceneLoadIssue::Kind::UnknownComponent, SceneLoadIssue::Severity::Warning,
                                   std::format("'{}' obyektida noma'lum komponent '{}': o'tkazib yuborildi (saqlanganda yo'qoladi)", name, it.key()),
                                   name, id, it.key());
        try
        {
            DeserializeEntityComponents(e, entity);
        }
        catch (const std::exception&)
        {
            DeserializeComponentsOneByOne(e, entity, report);
        }
        if (scope)
            scope->SetEntity(nullptr, {}, 0);
        const uint64_t parent = ReadEntityID(e, "Parent");
        if (!parent && e.contains("Parent"))
            AddEntityIssue(report, SceneLoadIssue::Kind::BadHierarchy, SceneLoadIssue::Severity::Warning,
                           std::format("'{}' obyektining ota obyekti (\"Parent\") xato: ildiz qilib yuklandi", name), name, id);
        loaded.push_back({ entity, parent, &e });
        if (report)
            report->EntitiesLoaded++;
        return true;
    }

    // The order the entries of "Entities" are made in. A file with "RootOrder" (sorted by UUID): the
    // hierarchy's - the roots in that order, each followed by its descendants depth first in their
    // children's order ("ChildOrder", else file order) - so the scene's order is the one the editor
    // showed. Older files (no "RootOrder"): file order, which was the scene's order.
    static std::vector<size_t> ComputeLoadOrder(const json& root, const json& entities)
    {
        const size_t count = entities.size();
        std::vector<size_t> order;
        order.reserve(count);
        auto rootOrder = root.find("RootOrder");
        if (rootOrder == root.end() || !rootOrder->is_array())
        {
            for (size_t i = 0; i < count; i++)
                order.push_back(i);
            return order;
        }
        std::unordered_map<uint64_t, size_t> index;
        index.reserve(count);
        for (size_t i = 0; i < count; i++)
            if (entities[i].is_object())
                if (uint64_t id = ReadEntityID(entities[i], "Entity"))
                    index.emplace(id, i); // the first of a duplicate ID
        std::vector<std::vector<size_t>> children(count);
        std::vector<char> isChild(count, 0), visited(count, 0);
        for (size_t i = 0; i < count; i++)
        {
            if (!entities[i].is_object())
                continue;
            auto parent = index.find(ReadEntityID(entities[i], "Parent"));
            if (parent != index.end() && parent->second != i)
            {
                children[parent->second].push_back(i);
                isChild[i] = 1;
            }
        }
        for (size_t i = 0; i < count; i++)
        {
            if (children[i].size() < 2)
                continue;
            auto listed = entities[i].find("ChildOrder");
            if (listed == entities[i].end() || !listed->is_array())
                continue;
            std::vector<size_t> ordered;
            ordered.reserve(children[i].size());
            for (const json& id : *listed)
            {
                if (!id.is_number_unsigned())
                    continue;
                auto child = index.find(id.get<uint64_t>());
                if (child != index.end() && std::find(children[i].begin(), children[i].end(), child->second) != children[i].end() &&
                    std::find(ordered.begin(), ordered.end(), child->second) == ordered.end())
                    ordered.push_back(child->second);
            }
            for (size_t child : children[i]) // the unlisted ones after the listed, in file order
                if (std::find(ordered.begin(), ordered.end(), child) == ordered.end())
                    ordered.push_back(child);
            children[i] = std::move(ordered);
        }
        std::vector<size_t> stack;
        auto visit = [&](size_t first) {
            stack.push_back(first);
            while (!stack.empty())
            {
                const size_t i = stack.back();
                stack.pop_back();
                if (visited[i])
                    continue;
                visited[i] = 1;
                order.push_back(i);
                for (auto it = children[i].rbegin(); it != children[i].rend(); ++it)
                    if (!visited[*it])
                        stack.push_back(*it);
            }
        };
        for (const json& id : *rootOrder)
            if (id.is_number_unsigned())
                if (auto it = index.find(id.get<uint64_t>()); it != index.end() && !isChild[it->second] && !visited[it->second])
                    visit(it->second);
        for (size_t i = 0; i < count; i++) // roots not listed, in file order
            if (!isChild[i] && !visited[i])
                visit(i);
        for (size_t i = 0; i < count; i++) // a parent loop: in file order
            if (!visited[i])
                visit(i);
        return order;
    }

    // Parents and children of the entities made (in the order they were made). A parent that is not
    // in the scene, or one that would close a loop, leaves the entity a root.
    static void LinkSceneHierarchy(Scene& scene, std::vector<LoadedEntity>& loaded, SceneLoadReport* report)
    {
        for (LoadedEntity& item : loaded)
        {
            if (!item.Parent || !item.Handle)
                continue;
            Entity p = scene.GetEntityByUUID(item.Parent);
            const std::string& name = item.Handle.GetName();
            const UUID self = item.Handle.GetUUID();
            if (!p || p == item.Handle)
            {
                AddEntityIssue(report, SceneLoadIssue::Kind::BadHierarchy, SceneLoadIssue::Severity::Warning,
                               std::format("'{}' obyektining ota obyekti ({}) sahnada yo'q: ildiz qilib yuklandi", name, item.Parent),
                               name, (uint64_t)self);
                continue;
            }
            bool loop = false;
            UUID up = p.GetComponent<RelationshipComponent>().Parent;
            for (size_t steps = 0; (uint64_t)up && steps <= loaded.size(); steps++)
            {
                if (up == self)
                {
                    loop = true;
                    break;
                }
                Entity ancestor = scene.GetEntityByUUID(up);
                if (!ancestor)
                    break;
                up = ancestor.GetComponent<RelationshipComponent>().Parent;
            }
            if (loop)
            {
                AddEntityIssue(report, SceneLoadIssue::Kind::BadHierarchy, SceneLoadIssue::Severity::Warning,
                               std::format("'{}' obyekti o'z avlodining farzandi bo'lib qolardi (halqa): ildiz qilib yuklandi", name),
                               name, (uint64_t)self);
                continue;
            }
            item.Handle.GetComponent<RelationshipComponent>().Parent = item.Parent;
            p.GetComponent<RelationshipComponent>().Children.push_back(self);
        }
        for (LoadedEntity& item : loaded)
            if (item.Handle && item.Source)
                ApplyChildOrder(item.Handle, *item.Source, nullptr);
        scene.InvalidateActiveCache(); // flags and parents were written directly
    }

    static const json& SceneEntityList(const json& root, SceneLoadReport* report)
    {
        static const json s_NoEntities = json::array();
        auto list = root.find("Entities");
        if (list == root.end())
            return s_NoEntities;
        if (!list->is_array())
        {
            if (report)
                report->Add(MakeIssue(SceneLoadIssue::Kind::BadEntity, SceneLoadIssue::Severity::Error,
                                      std::format("\"Entities\" ro'yxat emas ({}): obyektlar yuklanmadi", list->type_name())));
            return s_NoEntities;
        }
        return *list;
    }

    // The settings; what cannot be read keeps its default (reported).
    static void DeserializeSceneSettingsChecked(const json& root, Scene& scene, SceneLoadReport* report)
    {
        try
        {
            DeserializeSceneSettings(root, scene);
        }
        catch (const std::exception& ex)
        {
            if (report)
            {
                SceneLoadIssue issue = MakeIssue(SceneLoadIssue::Kind::BadComponent, SceneLoadIssue::Severity::Error,
                                                 std::format("Sahna sozlamalari to'liq o'qilmadi ({}): qolganlari standart", DescribeException(ex)));
                issue.Component = "Settings";
                report->Add(std::move(issue));
            }
        }
    }

    // A parsed scene file into `scene`: the migration, the settings, the entities in their load
    // order, then the hierarchy (what cannot be read is reported and left out).
    static void BuildSceneFromRoot(json& root, Scene& scene, SceneLoadReport& report, SceneMigrationResult& migration, SceneLoadTiming& timing)
    {
        auto stage = std::chrono::steady_clock::now();
        auto lap = [&](SceneLoadTiming::Stage s) {
            const auto now = std::chrono::steady_clock::now();
            timing[s] += std::chrono::duration<double, std::milli>(now - stage).count();
            stage = now;
        };
        migration = MigrateSceneRoot(root, &report);
        lap(SceneLoadTiming::Stage::Migrate);
        DeserializeSceneSettingsChecked(root, scene, &report);
        const json& entityList = SceneEntityList(root, &report);
        std::vector<LoadedEntity> loaded;
        loaded.reserve(entityList.size());
        for (size_t i : ComputeLoadOrder(root, entityList))
            DeserializeSceneEntity(entityList[i], i, scene, loaded, &report);
        LinkSceneHierarchy(scene, loaded, &report);
        lap(SceneLoadTiming::Stage::Deserialize);
        timing.Entities = loaded.size();
        timing.SceneName = scene.GetName();
    }

    bool SceneSerializer::DeserializeFromString(const std::string& text)
    {
        m_RepairedReferences.clear();
        m_LastError.clear();
        m_Report.Clear();
        m_Migration = {};
        m_Timing = {};
        m_Timing.FileBytes = text.size();
        AssetReferenceScope assetReferences(&m_RepairedReferences);
        assetReferences.SetReport(&m_Report);
        auto fail = [&](SceneLoadIssue issue) {
            m_LastError = issue.Message;
            m_Report.Add(std::move(issue));
            m_Timing.Failed = true;
            if (!m_QuietErrors)
                ZM_CORE_ERROR("{}", m_LastError);
            return false;
        };
        const auto parseStart = std::chrono::steady_clock::now();
        json root;
        try { root = json::parse(text); }
        catch (const std::exception& e)
        {
            m_Timing[SceneLoadTiming::Stage::Parse] = ElapsedMilliseconds(parseStart);
            return fail(MakeParseIssue(text, e));
        }
        m_Timing[SceneLoadTiming::Stage::Parse] = ElapsedMilliseconds(parseStart);
        if (!root.is_object() || !root.contains("Scene"))
            return fail(MakeNotASceneIssue());
        BuildSceneFromRoot(root, *m_Scene, m_Report, m_Migration, m_Timing);
        const auto freeStart = std::chrono::steady_clock::now();
        root = json(); // the parsed tree is let go (part of reading it)
        m_Timing[SceneLoadTiming::Stage::Parse] += ElapsedMilliseconds(freeStart);
        return true;
    }

    bool SceneSerializer::DeserializeFromBinary(const std::vector<uint8_t>& bytes)
    {
        return DeserializeFromMemory(bytes.data(), bytes.size());
    }

    bool SceneSerializer::DeserializeFromMemory(const uint8_t* data, size_t size)
    {
        m_RepairedReferences.clear();
        m_LastError.clear();
        m_Report.Clear();
        m_Migration = {};
        m_Timing = {};
        m_Timing.FileBytes = size;
        m_Timing.Binary = true;
        AssetReferenceScope assetReferences(&m_RepairedReferences);
        assetReferences.SetReport(&m_Report);
        auto fail = [&](SceneLoadIssue issue) {
            m_LastError = issue.Message;
            m_Report.Add(std::move(issue));
            m_Timing.Failed = true;
            if (!m_QuietErrors)
                ZM_CORE_ERROR("{}", m_LastError);
            return false;
        };
        uint64_t sourceSize = 0;
        if (!ReadBinaryHeader(data, size, sourceSize))
            return fail(MakeIssue(SceneLoadIssue::Kind::Unreadable, SceneLoadIssue::Severity::Fatal,
                                  "Binar sahna keshi emas yoki boshqa versiyadagi kesh"));
        const auto parseStart = std::chrono::steady_clock::now();
        json root;
        try { root = ParseMessagePack(data + s_BinaryHeaderSize, data + size); }
        catch (const std::exception& e)
        {
            m_Timing[SceneLoadTiming::Stage::Parse] = ElapsedMilliseconds(parseStart);
            return fail(MakeIssue(SceneLoadIssue::Kind::ParseError, SceneLoadIssue::Severity::Fatal,
                                  std::format("Binar sahna keshi buzilgan: {}", DescribeException(e))));
        }
        m_Timing[SceneLoadTiming::Stage::Parse] = ElapsedMilliseconds(parseStart);
        if (!root.is_object() || !root.contains("Scene"))
            return fail(MakeNotASceneIssue());
        BuildSceneFromRoot(root, *m_Scene, m_Report, m_Migration, m_Timing);
        const auto freeStart = std::chrono::steady_clock::now();
        root = json();
        m_Timing[SceneLoadTiming::Stage::Parse] += ElapsedMilliseconds(freeStart);
        return true;
    }

    // ------------------------------------------------------------------ scene file info
    // A sub-scene's file as a scene file names it (migrated: "Path"); where its UUID is now when
    // that path is gone (moved while the file was not open) - quietly: the file is only looked at.
    static std::string SubScenePathOf(const json& sub)
    {
        auto pathIt = sub.find("Path");
        std::string path = pathIt != sub.end() && pathIt->is_string() ? pathIt->get<std::string>() : std::string();
        if (path.empty() || !Project::GetActive())
            return path;
        AssetRegistry* registry = Project::GetAssetRegistry();
        std::error_code ec;
        if (!registry || std::filesystem::exists(Project::GetAssetFileSystemPath(path), ec))
            return path;
        auto handleIt = sub.find(UUIDKey("Path"));
        if (handleIt == sub.end() || !handleIt->is_string())
            return path;
        const std::optional<AssetHandle> handle = AssetHandle::FromString(handleIt->get<std::string>());
        const std::string moved = handle ? registry->GetPath(*handle) : std::string();
        return moved.empty() ? path : moved;
    }

    bool SceneSerializer::ReadSceneInfoFromString(const std::string& text, SceneFileInfo& out)
    {
        const std::string name = out.Name;
        out = {};
        out.Name = name;
        json root = json::parse(text, nullptr, false);
        if (root.is_discarded())
        {
            out.Error = "JSON o'qilmadi";
            return false;
        }
        if (!root.is_object() || !root.contains("Scene"))
        {
            out.Error = "sahna fayli emas";
            return false;
        }
        out.Version = SceneMigrationJson::ReadVersion(root);
        SceneMigrationJson::Migrate(root); // read as the current format (string IDs of a v0 file ...)
        if (auto scene = root.find("Scene"); scene->is_string() && !scene->get_ref<const std::string&>().empty())
            out.Name = scene->get<std::string>();
        out.Metadata = ReadSceneMetadata(root);
        if (auto list = root.find("Entities"); list != root.end() && list->is_array())
            for (const json& e : *list)
            {
                if (!e.is_object())
                    continue;
                out.EntityCount++;
                if (!ReadEntityID(e, "Parent"))
                    out.RootCount++;
                if (auto sub = e.find("SubScene"); sub != e.end() && sub->is_object())
                {
                    std::string path = SubScenePathOf(*sub);
                    if (!path.empty() && std::find(out.SubScenes.begin(), out.SubScenes.end(), path) == out.SubScenes.end())
                        out.SubScenes.push_back(std::move(path));
                }
            }
        return true;
    }

    bool SceneSerializer::ReadSceneInfo(const std::filesystem::path& file, SceneFileInfo& out)
    {
        out = {};
        out.Name = file.stem().string();
        std::string error = AssetImportLog::CheckReadable(file);
        std::string text = error.empty() ? FileSystem::ReadTextFile(file) : std::string();
        if (error.empty() && text.empty())
            error = "fayl o'qilmadi";
        if (!error.empty())
        {
            out.Error = std::move(error);
            return false;
        }
        const bool ok = ReadSceneInfoFromString(text, out);
        std::error_code ec;
        if (std::filesystem::path thumbnail = SceneMetadata::GetThumbnailPath(file); std::filesystem::is_regular_file(thumbnail, ec))
            out.Thumbnail = thumbnail;
        return ok;
    }

    // ------------------------------------------------------------------ SceneLoadJob
    struct SceneLoadJob::Data
    {
        json Root;
        std::filesystem::path File;
        std::string Error;
        enum class Phase { Empty = 0, Parsed, Settings, Entities, Hierarchy, Built } Step = Phase::Empty;
        size_t Next = 0;
        std::vector<size_t> Order;           // the entries of "Entities" in the order they are made
        std::vector<LoadedEntity> Loaded;
        std::vector<SceneSerializer::AssetReferenceRepair> Repairs;
        SceneLoadReport Report;
        SceneMigrationResult Migration;
        SceneLoadTiming Timing;

        const json& Entities() const
        {
            static const json s_None = json::array();
            auto it = Root.find("Entities");
            return it != Root.end() && it->is_array() ? *it : s_None;
        }
        void Fail(SceneLoadIssue issue)
        {
            Error = issue.Message;
            Report.Add(std::move(issue));
        }
    };

    SceneLoadJob::SceneLoadJob() : m_Data(std::make_unique<Data>()) {}
    SceneLoadJob::~SceneLoadJob() = default;

    bool SceneLoadJob::Parse(const std::filesystem::path& path)
    {
        m_Data->File = path;
        std::filesystem::path cache;
        bool fromCache = FindValidBinaryCache(path, cache);
        for (int attempt = 0; attempt < 2; attempt++)
        {
            const std::filesystem::path& source = fromCache ? cache : path;
            m_Data->Report.Clear();
            m_Data->Report.File = path.generic_string();
            const auto readStart = std::chrono::steady_clock::now();
            std::string error = AssetImportLog::CheckReadable(source);
            std::string data = error.empty() ? FileSystem::ReadTextFile(source) : std::string();
            const double readMs = ElapsedMilliseconds(readStart);
            if (data.empty() && error.empty())
                error = "fayl o'qilmadi";
            if (!error.empty())
            {
                m_Data->Timing = {};
                m_Data->Timing[SceneLoadTiming::Stage::Read] = readMs;
                m_Data->Timing.Failed = true;
                m_Data->Fail(MakeUnreadableIssue(error));
                if (!fromCache)
                    return false;
            }
            else
            {
                const bool parsed = SceneSerializer::IsBinary(data.data(), data.size())
                    ? ParseMemory((const uint8_t*)data.data(), data.size())
                    : ParseString(data);
                m_Data->Timing[SceneLoadTiming::Stage::Read] = readMs;
                if (parsed)
                {
                    SceneMigration::LogMigration(SceneDisplayPath(path), m_Data->Migration);
                    return true;
                }
                if (!fromCache)
                    return false;
            }
            ZM_CORE_WARN("Sahnaning binar keshi o'qilmadi ({}): matn fayli o'qiladi - {}", m_Data->Error, cache.filename().string());
            fromCache = false;
        }
        return false;
    }

    bool SceneLoadJob::ParseString(const std::string& text)
    {
        m_Data->Error.clear();
        m_Data->Timing = {};
        m_Data->Timing.FileBytes = text.size();
        std::erase_if(m_Data->Report.Issues, [](const SceneLoadIssue& i) { return i.Level == SceneLoadIssue::Severity::Fatal; });
        const auto parseStart = std::chrono::steady_clock::now();
        try { m_Data->Root = json::parse(text); }
        catch (const std::exception& e)
        {
            m_Data->Timing[SceneLoadTiming::Stage::Parse] = ElapsedMilliseconds(parseStart);
            m_Data->Timing.Failed = true;
            m_Data->Fail(MakeParseIssue(text, e));
            return false;
        }
        m_Data->Timing[SceneLoadTiming::Stage::Parse] = ElapsedMilliseconds(parseStart);
        return AcceptParsed();
    }

    bool SceneLoadJob::ParseBinary(const std::vector<uint8_t>& bytes)
    {
        return ParseMemory(bytes.data(), bytes.size());
    }

    bool SceneLoadJob::ParseMemory(const uint8_t* data, size_t size)
    {
        m_Data->Error.clear();
        m_Data->Timing = {};
        m_Data->Timing.FileBytes = size;
        m_Data->Timing.Binary = true;
        std::erase_if(m_Data->Report.Issues, [](const SceneLoadIssue& i) { return i.Level == SceneLoadIssue::Severity::Fatal; });
        uint64_t sourceSize = 0;
        if (!ReadBinaryHeader(data, size, sourceSize))
        {
            m_Data->Timing.Failed = true;
            m_Data->Fail(MakeIssue(SceneLoadIssue::Kind::Unreadable, SceneLoadIssue::Severity::Fatal,
                                   "Binar sahna keshi emas yoki boshqa versiyadagi kesh"));
            return false;
        }
        const auto parseStart = std::chrono::steady_clock::now();
        try { m_Data->Root = ParseMessagePack(data + s_BinaryHeaderSize, data + size); }
        catch (const std::exception& e)
        {
            m_Data->Timing[SceneLoadTiming::Stage::Parse] = ElapsedMilliseconds(parseStart);
            m_Data->Timing.Failed = true;
            m_Data->Fail(MakeIssue(SceneLoadIssue::Kind::ParseError, SceneLoadIssue::Severity::Fatal,
                                   std::format("Binar sahna keshi buzilgan: {}", DescribeException(e))));
            return false;
        }
        m_Data->Timing[SceneLoadTiming::Stage::Parse] = ElapsedMilliseconds(parseStart);
        return AcceptParsed();
    }

    bool SceneLoadJob::AcceptParsed()
    {
        if (!m_Data->Root.is_object() || !m_Data->Root.contains("Scene"))
        {
            m_Data->Timing.Failed = true;
            m_Data->Fail(MakeNotASceneIssue());
            return false;
        }
        const auto migrateStart = std::chrono::steady_clock::now();
        std::erase_if(m_Data->Report.Issues, [](const SceneLoadIssue& i) { return i.Type == SceneLoadIssue::Kind::NewerVersion; });
        m_Data->Migration = MigrateSceneRoot(m_Data->Root, &m_Data->Report);
        // The order the entities are made in (any thread: only the parsed file is read).
        m_Data->Order = ComputeLoadOrder(m_Data->Root, m_Data->Entities());
        m_Data->Timing[SceneLoadTiming::Stage::Migrate] = ElapsedMilliseconds(migrateStart);
        m_Data->Timing.Entities = m_Data->Entities().size();
        m_Data->Step = Data::Phase::Parsed;
        return true;
    }

    static bool IsImageFileName(std::string_view s)
    {
        const size_t dot = s.find_last_of('.');
        if (dot == std::string_view::npos || s.size() - dot > 5)
            return false;
        std::string ext(s.substr(dot));
        for (char& c : ext)
            c = (char)std::tolower((unsigned char)c);
        return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp" || ext == ".psd" || ext == ".gif";
    }

    static void CollectImageStrings(const json& j, std::vector<std::string>& out)
    {
        if (j.is_string())
        {
            const std::string& s = j.get_ref<const std::string&>();
            if (IsImageFileName(s) && std::find(out.begin(), out.end(), s) == out.end())
                out.push_back(s);
        }
        else if (j.is_object() || j.is_array())
            for (const json& child : j)
                CollectImageStrings(child, out);
    }

    std::vector<std::string> SceneLoadJob::CollectImagePaths() const
    {
        std::vector<std::string> images;
        if (m_Data->Step != Data::Phase::Empty)
            CollectImageStrings(m_Data->Entities(), images);
        return images;
    }

    size_t SceneLoadJob::GetEntityCount() const { return m_Data->Step == Data::Phase::Empty ? 0 : m_Data->Entities().size(); }
    const std::string& SceneLoadJob::GetError() const { return m_Data->Error; }
    bool SceneLoadJob::IsBuilt() const { return m_Data->Step == Data::Phase::Built; }
    const std::vector<SceneSerializer::AssetReferenceRepair>& SceneLoadJob::GetRepairedReferences() const { return m_Data->Repairs; }
    const SceneLoadReport& SceneLoadJob::GetReport() const { return m_Data->Report; }
    const SceneMigrationResult& SceneLoadJob::GetMigration() const { return m_Data->Migration; }
    const SceneLoadTiming& SceneLoadJob::GetTiming() const { return m_Data->Timing; }

    float SceneLoadJob::GetBuildProgress() const
    {
        switch (m_Data->Step)
        {
            case Data::Phase::Empty:
            case Data::Phase::Parsed:
            case Data::Phase::Settings: return 0.0f;
            case Data::Phase::Entities:
            {
                const size_t count = m_Data->Order.size();
                return count ? 0.95f * (float)m_Data->Next / (float)count : 0.95f;
            }
            case Data::Phase::Hierarchy: return 0.95f;
            case Data::Phase::Built: return 1.0f;
        }
        return 0.0f;
    }

    bool SceneLoadJob::Build(Scene& scene, double budgetMs)
    {
        Data& d = *m_Data;
        if (d.Step == Data::Phase::Empty || d.Step == Data::Phase::Built || !d.Error.empty())
            return true;
        const auto start = std::chrono::steady_clock::now();
        auto elapsed = [&]() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count(); };
        // Every slice adds to the job's Deserialize time (however it returns).
        struct SliceClock
        {
            Data& D;
            std::chrono::steady_clock::time_point Start;
            ~SliceClock() { D.Timing[SceneLoadTiming::Stage::Deserialize] += ElapsedMilliseconds(Start); }
        } sliceClock{ d, start };
        AssetReferenceScope assetReferences(&d.Repairs);
        assetReferences.SetReport(&d.Report);
        try
        {
            if (d.Step == Data::Phase::Parsed)
                d.Step = Data::Phase::Settings;
            if (d.Step == Data::Phase::Settings)
            {
                DeserializeSceneSettingsChecked(d.Root, scene, &d.Report);
                SceneEntityList(d.Root, &d.Report); // "Entities" that is not a list is reported
                d.Loaded.reserve(d.Entities().size());
                d.Step = Data::Phase::Entities;
            }
            if (d.Step == Data::Phase::Entities)
            {
                const json& list = d.Entities();
                while (d.Next < d.Order.size())
                {
                    const size_t index = d.Order[d.Next];
                    DeserializeSceneEntity(list[index], index, scene, d.Loaded, &d.Report);
                    d.Next++;
                    // The clock is looked at every few entities (it costs more than a plain entity).
                    if ((d.Next & 7) == 0 && elapsed() >= budgetMs)
                        return false;
                }
                d.Step = Data::Phase::Hierarchy;
                if (elapsed() >= budgetMs)
                    return false;
            }
            if (d.Step == Data::Phase::Hierarchy)
            {
                LinkSceneHierarchy(scene, d.Loaded, &d.Report);
                if (scene.GetName().empty() || scene.GetName() == "Untitled")
                    scene.SetName(d.File.empty() ? std::string("Untitled") : d.File.stem().string());
                d.Timing.Entities = d.Loaded.size();
                d.Timing.SceneName = scene.GetName();
                d.Loaded.clear();
                d.Order.clear();
                d.Root = json(); // the parsed file is not needed any more
                d.Step = Data::Phase::Built;
            }
        }
        catch (const std::exception& ex)
        {
            d.Timing.Failed = true;
            d.Fail(MakeIssue(SceneLoadIssue::Kind::BadEntity, SceneLoadIssue::Severity::Fatal, std::format("Sahna formati xato: {}", ex.what())));
        }
        return true;
    }

}

namespace Zamin {

    std::string SceneSerializer::SerializeEntities(Scene& scene, const std::vector<Entity>& entities, bool includeEditorFlags)
    {
        std::unordered_map<uint64_t, bool> inSet;
        for (Entity e : entities) inSet[(uint64_t)e.GetUUID()] = true;
        AssetReferenceScope assetReferences;
        json list = json::array();
        for (Entity e : entities)
        {
            json j = SerializeEntity(e, true, includeEditorFlags);
            if (j.contains("Parent") && !inSet.count(j["Parent"].get<uint64_t>()))
                j.erase("Parent");
            list.push_back(j);
        }
        WriteChildOrder(list, entities);
        json root;
        root["ZaminEntities"] = 1;
        root["Entities"] = list;
        return root.dump(2);
    }

    std::vector<Entity> SceneSerializer::DeserializeEntities(Scene& scene, const std::string& text, bool generateNewUUIDs,
                                                             std::vector<AssetReferenceRepair>* repairs, std::string* error)
    {
        std::vector<Entity> created;
        AssetReferenceScope assetReferences(repairs);
        if (error)
            error->clear();
        json root;
        try { root = json::parse(text); }
        catch (const std::exception& e)
        {
            if (error)
                *error = std::format("JSON xatosi: {}", e.what());
            return created;
        }
        if (!root.is_object() || !root.contains("Entities") || !root["Entities"].is_array())
        {
            if (error)
                *error = "obyektlar ro'yxati (\"Entities\") yo'q";
            return created;
        }

        try
        {
            std::unordered_map<uint64_t, UUID> remap;
            std::vector<std::pair<Entity, uint64_t>> parents;
            for (const json& e : root["Entities"])
            {
                uint64_t old = e.value("Entity", (uint64_t)0);
                UUID id = generateNewUUIDs || !old || scene.GetEntityByUUID(old) ? UUID() : UUID(old);
                remap[old] = id;
                Entity entity = scene.CreateEntityWithUUID(id, e.value("Tag", "Entity"));
                DeserializeEntityComponents(e, entity);
                parents.emplace_back(entity, e.value("Parent", (uint64_t)0));
                created.push_back(entity);
            }
            for (auto& [entity, oldParent] : parents)
            {
                if (!oldParent) continue;
                auto it = remap.find(oldParent);
                if (it == remap.end()) continue;
                Entity p = scene.GetEntityByUUID(it->second);
                if (!p) continue;
                entity.GetComponent<RelationshipComponent>().Parent = p.GetUUID();
                p.GetComponent<RelationshipComponent>().Children.push_back(entity.GetUUID());
            }
            {
                size_t i = 0;
                for (const json& e : root["Entities"])
                    if (i < created.size())
                        ApplyChildOrder(created[i++], e, &remap);
            }

            // EntityRef fields that point inside the pasted set follow the new IDs.
            std::unordered_map<uint64_t, uint64_t> refs;
            for (const auto& [old, id] : remap)
                if (old && old != (uint64_t)id)
                    refs[old] = (uint64_t)id;
            if (!refs.empty())
                Scene::RemapEntityReferences(created, refs);
        }
        catch (const std::exception& ex)
        {
            if (error)
                *error = std::format("Entity ma'lumotlari xato: {}", ex.what());
            else
                ZM_CORE_ERROR("Entity ma'lumotlari xato: {}", ex.what());
        }
        scene.InvalidateActiveCache(); // flags and parents were written directly
        // Roots first
        std::stable_partition(created.begin(), created.end(), [](Entity e) {
            return !(uint64_t)e.GetComponent<RelationshipComponent>().Parent;
        });
        return created;
    }

    // ------------------------------------------------------------------ particle presets (.zparticle)
    // { "ZaminParticlePreset": 1, "Name", "System": {...}, "Templates": [{ "Name", "System" }] } - the
    // systems as scenes write them; a sub-emitter's "Emitter" is 1 + its template's index.
    std::string ParticlePresets::ToString(const ParticlePreset& preset)
    {
        AssetReferenceScope scope;
        json templates = json::array();
        for (const auto& t : preset.Templates)
        {
            ParticleSystemComponent system = t.System;
            system.SubEmitters = {}; // one level
            templates.push_back({ { "Name", t.Name }, { "System", ParticleSystemToJson(system) } });
        }
        json root = { { "ZaminParticlePreset", FileVersion }, { "Name", preset.Name }, { "System", ParticleSystemToJson(preset.System) } };
        if (!templates.empty())
            root["Templates"] = std::move(templates);
        return root.dump(2);
    }

    bool ParticlePresets::FromString(const std::string& text, ParticlePreset& out, std::string* error)
    {
        auto fail = [&](const std::string& why) {
            if (error)
                *error = why;
            return false;
        };
        json root = json::parse(text, nullptr, false);
        if (root.is_discarded() || !root.is_object())
            return fail("JSON o'qilmadi");
        auto system = root.find("System");
        if (!root.contains("ZaminParticlePreset") || system == root.end() || !system->is_object())
            return fail("Bu particle preset fayli emas");
        if (auto version = root["ZaminParticlePreset"]; version.is_number_integer() && version.get<int>() > FileVersion)
            ZM_CORE_WARN("Particle preset yangiroq versiyada ({}): ba'zi sozlamalar o'qilmasligi mumkin", version.get<int>());
        AssetReferenceScope scope;
        ParticlePreset preset;
        try
        {
            preset.Name = root.value("Name", std::string());
            ParticleSystemFromJson(*system, preset.System);
            if (auto list = root.find("Templates"); list != root.end() && list->is_array())
                for (const json& item : *list)
                {
                    ParticlePreset::Template t;
                    if (item.is_object())
                    {
                        t.Name = item.value("Name", std::string());
                        if (auto s = item.find("System"); s != item.end() && s->is_object())
                            ParticleSystemFromJson(*s, t.System);
                    }
                    t.System.SubEmitters = {};
                    t.System.CustomSpace = EntityRef();
                    t.System.Collision.PlaneEntity = EntityRef();
                    preset.Templates.push_back(std::move(t)); // a broken one keeps the others' places
                }
        }
        catch (const std::exception& ex)
        {
            return fail(std::format("Particle preset xato: {}", ex.what()));
        }
        // Entity references of another scene mean nothing here; sub-emitters: template numbers.
        preset.System.CustomSpace = EntityRef();
        preset.System.Collision.PlaneEntity = EntityRef();
        for (auto& sub : preset.System.SubEmitters.Emitters)
            if (sub.Emitter.ID > preset.Templates.size())
                sub.Emitter.ID = 0;
        out = std::move(preset);
        return true;
    }

}
