#include "Zamin/Scene/SceneSerializer.h"
#include "Zamin/Asset/AssetImportLog.h"
#include "Zamin/Asset/AssetRegistry.h"
#include "Zamin/Physics/CollisionShapes3D.h"
#include "Zamin/Project/Project.h"
#include "Zamin/Renderer/MaterialAsset.h"
#include "Zamin/Renderer/Renderer.h"
#include "Zamin/Scene/ComponentRegistry.h"
#include "Zamin/Scene/Entity.h"
#include "Zamin/Utils/FileSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

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

    private:
        static inline thread_local AssetReferenceScope* s_Current = nullptr;
        AssetReferenceScope* m_Previous = nullptr;
        AssetRegistry* m_Registry = nullptr;
        std::vector<SceneSerializer::AssetReferenceRepair>* m_Repairs = nullptr;
    };

    static std::string UUIDKey(std::string_view key) { return std::string(key) + "UUID"; }

    // Adds "<key>UUID" for the asset path stored under `key` of `object` (when known).
    static void WriteAssetHandle(json& object, std::string_view key, const std::string& path)
    {
        AssetReferenceScope* scope = AssetReferenceScope::Get();
        AssetRegistry* registry = scope ? scope->GetRegistry() : nullptr;
        if (path.empty() || !registry)
            return;
        // Only a UUID stored in a .zmeta file means something in the next session.
        if (const AssetMetadata* meta = registry->Find(std::filesystem::path(path)); meta && meta->HasMetaFile)
            object[UUIDKey(key)] = meta->Handle.ToString();
    }

    // The asset path stored under `key`, or where its UUID is now when that file is gone.
    static std::string ReadAssetPath(const json& object, std::string_view key)
    {
        std::string path = object.value(std::string(key), std::string());
        auto handleIt = object.find(UUIDKey(key));
        if (path.empty() || handleIt == object.end() || !handleIt->is_string())
            return path;
        AssetReferenceScope* scope = AssetReferenceScope::Get();
        AssetRegistry* registry = scope ? scope->GetRegistry() : nullptr;
        if (!registry || registry->Find(std::filesystem::path(path)))
            return path; // no project, or the file is still there
        std::error_code ec;
        if (std::filesystem::exists(registry->ToFileSystemPath(path), ec))
            return path; // there, just not a registered asset
        std::optional<AssetHandle> handle = AssetHandle::FromString(handleIt->get<std::string>());
        std::string moved = handle ? registry->GetPath(*handle) : std::string();
        if (moved.empty() || moved == path)
            return path; // gone for good: the path stays (the editor shows what is missing)
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
                          { "Centered", c->Centered }, { "Font", c->FontPath } };
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
                states.push_back({ { "Name", st.Name }, { "Clip", st.Clip }, { "Speed", st.Speed }, { "Loop", st.Loop }, { "SpeedParameter", st.SpeedParameter } });
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
        if (auto* c = entity.TryGetComponent<ParticleSystemComponent>())
            e["ParticleSystem"] = {
                { "Playing", c->Playing }, { "Looping", c->Looping }, { "PlayInEditor", c->PlayInEditor },
                { "EmissionRate", c->EmissionRate }, { "Burst", c->Burst }, { "MaxParticles", c->MaxParticles },
                { "Shape", (int)c->Shape }, { "ShapeRadius", c->ShapeRadius }, { "ShapeExtents", ToJson(c->ShapeExtents) },
                { "LifetimeMin", c->LifetimeMin }, { "LifetimeMax", c->LifetimeMax }, { "SpeedMin", c->SpeedMin }, { "SpeedMax", c->SpeedMax },
                { "Direction", ToJson(c->Direction) }, { "Spread", c->Spread }, { "Gravity", ToJson(c->Gravity) }, { "Drag", c->Drag },
                { "AngularVelocityMax", c->AngularVelocityMax },
                { "StartColor", ToJson(c->StartColor) }, { "EndColor", ToJson(c->EndColor) },
                { "StartSize", c->StartSize }, { "EndSize", c->EndSize },
                { "RenderAsCircle", c->RenderAsCircle }, { "Additive", c->Additive }, { "WorldSpace", c->WorldSpace },
                { "Billboard", c->Billboard }, { "Texture", c->TexturePath }, { "SortingOrder", c->SortingOrder },
            };
        if (auto* c = entity.TryGetComponent<ParticleSystemComponent>())
        {
            WriteAssetHandle(e["ParticleSystem"], "Texture", c->TexturePath);
            WriteSortingLayer(e["ParticleSystem"], c->SortingLayer);
        }
        if (auto* c = entity.TryGetComponent<UITransformComponent>())
            e["UITransform"] = { { "Anchor", (int)c->Anchor }, { "Position", ToJson(c->Position) }, { "Size", ToJson(c->Size) }, { "FontSize", c->FontSize }, { "Order", c->Order } };
        if (auto* c = entity.TryGetComponent<UIButtonComponent>())
            e["UIButton"] = { { "Normal", ToJson(c->NormalColor) }, { "Hover", ToJson(c->HoverColor) }, { "Pressed", ToJson(c->PressedColor) },
                              { "Interactable", c->Interactable } };
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
            WriteAssetHandle(a, "Path", c->Path);
        }
        if (auto* c = entity.TryGetComponent<AudioListenerComponent>())
            e["AudioListener"] = { { "Enabled", c->Enabled }, { "Priority", c->Priority }, { "TrackVelocity", c->TrackVelocity } };
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
            c.Centered = j.value("Centered", false);
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
                c.States.push_back({ st.value("Name", "State"), st.value("Clip", ""), st.value("Speed", 1.0f), st.value("Loop", true),
                                     st.value("SpeedParameter", "") });
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
        if (e.contains("ParticleSystem"))
        {
            const json& j = e["ParticleSystem"];
            auto& c = entity.AddComponent<ParticleSystemComponent>();
            c.Playing = j.value("Playing", true);
            c.Looping = j.value("Looping", true);
            c.PlayInEditor = j.value("PlayInEditor", true);
            c.EmissionRate = j.value("EmissionRate", 30.0f);
            c.Burst = j.value("Burst", 0);
            c.MaxParticles = j.value("MaxParticles", 1000);
            c.Shape = (ParticleSystemComponent::EmitterShape)j.value("Shape", 0);
            c.ShapeRadius = j.value("ShapeRadius", 0.5f);
            c.ShapeExtents = Vec(j, "ShapeExtents", glm::vec3(0.5f));
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
            c.WorldSpace = j.value("WorldSpace", true);
            c.Billboard = j.value("Billboard", true);
            c.TexturePath = ReadAssetPath(j, "Texture");
            c.SortingOrder = j.value("SortingOrder", 0);
            c.SortingLayer = ReadSortingLayer(j);
        }
        if (e.contains("UITransform"))
        {
            const json& j = e["UITransform"];
            auto& c = entity.AddComponent<UITransformComponent>();
            c.Anchor = (UITransformComponent::AnchorPoint)j.value("Anchor", 0);
            c.Position = Vec(j, "Position", c.Position);
            c.Size = Vec(j, "Size", c.Size);
            c.FontSize = j.value("FontSize", 0.0f);
            c.Order = j.value("Order", 0);
        }
        if (e.contains("UIButton"))
        {
            const json& j = e["UIButton"];
            auto& c = entity.AddComponent<UIButtonComponent>();
            c.NormalColor = Vec(j, "Normal", c.NormalColor);
            c.HoverColor = Vec(j, "Hover", c.HoverColor);
            c.PressedColor = Vec(j, "Pressed", c.PressedColor);
            c.Interactable = j.value("Interactable", true);
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
        }
        if (e.contains("AudioListener"))
        {
            const json& j = e["AudioListener"];
            auto& c = entity.AddComponent<AudioListenerComponent>();
            c.Enabled = j.value("Enabled", true);
            c.Priority = j.value("Priority", 0);
            c.TrackVelocity = j.value("TrackVelocity", false);
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
            if (std::find(children.begin(), children.end(), child) != children.end() &&
                std::find(ordered.begin(), ordered.end(), child) == ordered.end())
                ordered.push_back(child);
        }
        for (UUID child : children) // anything not listed keeps the file order, after the listed ones
            if (std::find(ordered.begin(), ordered.end(), child) == ordered.end())
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

    std::string SceneSerializer::SerializeToString()
    {
        const auto& s = m_Scene->GetSettings();
        AssetReferenceScope assetReferences;
        json root;
        root["Scene"] = m_Scene->GetName();
        root["Version"] = 1;
        root["Settings"] = {
            { "DrawSky", s.DrawSky }, { "SkyTop", ToJson(s.SkyTopColor) }, { "SkyHorizon", ToJson(s.SkyHorizonColor) },
            { "Ground", ToJson(s.GroundColor) }, { "Ambient", s.AmbientIntensity }, { "Exposure", s.Exposure },
            { "Sky", { { "Curve", s.Sky.Curve }, { "GroundFade", s.Sky.GroundFade }, { "SunDisc", s.Sky.SunDisc },
                       { "SunSize", s.Sky.SunSize }, { "SunIntensity", s.Sky.SunIntensity }, { "SunGlow", s.Sky.SunGlow } } },
            { "ShadowDistance", s.ShadowDistance }, { "RenderScale", s.RenderScale }, { "Gravity", ToJson(s.Gravity) },
            { "Environment", { { "Map", s.EnvironmentMap }, { "Intensity", s.EnvironmentIntensity }, { "Rotation", s.EnvironmentRotation } } },
            { "Fog", { { "Enabled", s.FogEnabled }, { "Color", ToJson(s.FogColor) }, { "Density", s.FogDensity }, { "HeightFalloff", s.FogHeightFalloff } } }, { "Gravity3D", ToJson(s.Gravity3D) },
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
        // Physics step / Box2D settings only when changed (older files stay as they were).
        if (s.PhysicsStep != SceneSettings::PhysicsStepSettings())
            root["Settings"]["PhysicsStep"] = { { "Rate", s.PhysicsStep.StepRate }, { "MaxStepsPerFrame", s.PhysicsStep.MaxStepsPerFrame } };
        if (s.Physics2D != SceneSettings::Physics2DSettings())
            root["Settings"]["Physics2D"] = { { "VelocityIterations", s.Physics2D.VelocityIterations },
                                              { "PositionIterations", s.Physics2D.PositionIterations },
                                              { "SubSteps", s.Physics2D.SubSteps }, { "AllowSleep", s.Physics2D.AllowSleep },
                                              { "TimeToSleep", s.Physics2D.TimeToSleep } };
        if (s.Physics3D != SceneSettings::Physics3DSettings())
            root["Settings"]["Physics3D"] = { { "AllowSleep", s.Physics3D.AllowSleep }, { "TimeToSleep", s.Physics3D.TimeToSleep } };

        json entities = json::array();
        std::vector<Entity> ordered = m_Scene->GetEntitiesInOrder(); // creation order => stable diffs
        for (Entity e : ordered)
            entities.push_back(SerializeEntity(e));
        WriteChildOrder(entities, ordered);
        root["Entities"] = entities;
        return root.dump(2);
    }

    bool SceneSerializer::Serialize(const std::filesystem::path& filepath)
    {
        if (m_Scene->GetName().empty() || m_Scene->GetName() == "Untitled")
            m_Scene->SetName(filepath.stem().string());
        return FileSystem::WriteTextFile(filepath, SerializeToString());
    }

    bool SceneSerializer::Deserialize(const std::filesystem::path& filepath)
    {
        m_LastError = AssetImportLog::CheckReadable(filepath);
        std::string text = m_LastError.empty() ? FileSystem::ReadTextFile(filepath) : std::string();
        if (text.empty() && m_LastError.empty())
            m_LastError = "fayl o'qilmadi";
        bool ok = false;
        if (m_LastError.empty())
        {
            m_QuietErrors = true;
            ok = DeserializeFromString(text);
            m_QuietErrors = false;
        }
        if (!ok)
        {
            AssetImportLog::Report(filepath, m_LastError);
            return false;
        }
        AssetImportLog::Clear(filepath);
        if (m_Scene->GetName().empty() || m_Scene->GetName() == "Untitled")
            m_Scene->SetName(filepath.stem().string());
        return true;
    }

    bool SceneSerializer::DeserializeFromString(const std::string& text)
    {
        m_RepairedReferences.clear();
        m_LastError.clear();
        AssetReferenceScope assetReferences(&m_RepairedReferences);
        auto fail = [&](std::string reason) {
            if (!m_QuietErrors)
                ZM_CORE_ERROR("{}", reason);
            m_LastError = std::move(reason);
            return false;
        };
        json root;
        try { root = json::parse(text); }
        catch (const std::exception& e)
        {
            return fail(std::format("JSON xatosi: {}", e.what()));
        }
        if (!root.is_object() || !root.contains("Scene"))
            return fail("sahna fayli emas (\"Scene\" kaliti yo'q)");

        try
        {
            m_Scene->SetName(root.value("Scene", "Untitled"));
            if (root.contains("Settings"))
            {
                const json& j = root["Settings"];
                auto& s = m_Scene->GetSettings();
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
                s.Gravity = Vec(j, "Gravity", s.Gravity);
                s.Gravity3D = Vec(j, "Gravity3D", s.Gravity3D);
                // Older scenes have none: 60 steps a second, Box2D's 6 / 2 iterations, one sub-step,
                // sleeping after 0.5 s. Out-of-range values are clamped.
                s.PhysicsStep = {};
                if (auto ps = j.find("PhysicsStep"); ps != j.end() && ps->is_object())
                {
                    s.PhysicsStep.StepRate = ps->value("Rate", s.PhysicsStep.StepRate);
                    s.PhysicsStep.MaxStepsPerFrame = ps->value("MaxStepsPerFrame", s.PhysicsStep.MaxStepsPerFrame);
                }
                s.PhysicsStep.Sanitize();
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

            std::vector<std::pair<Entity, uint64_t>> parents;
            const json entityList = root.value("Entities", json::array());
            for (const json& e : entityList)
            {
                uint64_t uuid = e.value("Entity", (uint64_t)0);
                Entity entity = m_Scene->CreateEntityWithUUID(uuid ? UUID(uuid) : UUID(), e.value("Tag", "Entity"));
                DeserializeEntityComponents(e, entity);
                parents.emplace_back(entity, e.value("Parent", (uint64_t)0));
            }
            for (auto& [entity, parent] : parents)
            {
                if (!parent) continue;
                Entity p = m_Scene->GetEntityByUUID(parent);
                if (!p) continue;
                entity.GetComponent<RelationshipComponent>().Parent = parent;
                p.GetComponent<RelationshipComponent>().Children.push_back(entity.GetUUID());
            }
            for (size_t i = 0; i < parents.size() && i < entityList.size(); i++)
                ApplyChildOrder(parents[i].first, entityList[i], nullptr);
            m_Scene->InvalidateActiveCache(); // flags and parents were written directly
        }
        catch (const std::exception& ex)
        {
            return fail(std::format("Sahna formati xato: {}", ex.what()));
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

}
