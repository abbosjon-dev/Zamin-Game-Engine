#include "Zamin/Scene/SceneSerializer.h"
#include "Zamin/Scene/Entity.h"
#include "Zamin/Utils/FileSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>

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

    static json SerializeEntity(Entity entity, bool includeParent = true)
    {
        json e;
        e["Entity"] = (uint64_t)entity.GetUUID();
        e["Tag"] = entity.GetName();
        if (includeParent)
            if (auto* rel = entity.TryGetComponent<RelationshipComponent>(); rel && (uint64_t)rel->Parent)
                e["Parent"] = (uint64_t)rel->Parent;

        {
            auto& t = entity.GetComponent<TransformComponent>();
            e["Transform"] = { { "Translation", ToJson(t.Translation) }, { "Rotation", ToJson(t.Rotation) }, { "Scale", ToJson(t.Scale) } };
        }
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
        }
        if (auto* c = entity.TryGetComponent<SpriteRendererComponent>())
            e["SpriteRenderer"] = { { "Color", ToJson(c->Color) }, { "Texture", c->TexturePath }, { "Tiling", c->TilingFactor },
                                    { "SortingOrder", c->SortingOrder }, { "PixelArt", c->PixelArt }, { "FlipX", c->FlipX }, { "FlipY", c->FlipY } };
        if (auto* c = entity.TryGetComponent<SpriteAnimationComponent>())
            e["SpriteAnimation"] = { { "Columns", c->Columns }, { "Rows", c->Rows }, { "StartFrame", c->StartFrame },
                                     { "FrameCount", c->FrameCount }, { "FPS", c->FPS }, { "Loop", c->Loop }, { "Playing", c->Playing } };
        if (auto* c = entity.TryGetComponent<CircleRendererComponent>())
            e["CircleRenderer"] = { { "Color", ToJson(c->Color) }, { "Thickness", c->Thickness }, { "Fade", c->Fade }, { "SortingOrder", c->SortingOrder } };
        if (auto* c = entity.TryGetComponent<TextComponent>())
            e["Text"] = { { "Text", c->Text }, { "Color", ToJson(c->Color) }, { "Kerning", c->Kerning }, { "LineSpacing", c->LineSpacing },
                          { "Centered", c->Centered }, { "Font", c->FontPath } };
        if (auto* c = entity.TryGetComponent<MeshRendererComponent>())
            e["MeshRenderer"] = {
                { "Mesh", PrimitiveMeshToString(c->Type) }, { "Model", c->ModelPath }, { "CastShadows", c->CastShadows },
                { "Albedo", ToJson(c->Mat.Albedo) }, { "AlbedoMap", c->AlbedoPath }, { "Tiling", c->Mat.Tiling },
                { "NormalMap", c->NormalPath }, { "NormalStrength", c->Mat.NormalStrength },
                { "MetalRoughMap", c->MetalRoughPath }, { "EmissiveMap", c->EmissivePath },
                { "Metallic", c->Mat.Metallic }, { "Roughness", c->Mat.Roughness },
                { "Emission", ToJson(c->Mat.Emission) }, { "EmissionStrength", c->Mat.EmissionStrength },
            };
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
        if (auto* c = entity.TryGetComponent<Rigidbody2DComponent>())
            e["Rigidbody2D"] = { { "BodyType", BodyTypeToString(c->Type) }, { "FixedRotation", c->FixedRotation },
                                 { "GravityScale", c->GravityScale }, { "LinearDamping", c->LinearDamping },
                                 { "AngularDamping", c->AngularDamping }, { "Bullet", c->Bullet } };
        if (auto* c = entity.TryGetComponent<BoxCollider2DComponent>())
            e["BoxCollider2D"] = { { "Offset", ToJson(c->Offset) }, { "Size", ToJson(c->Size) }, { "Density", c->Density },
                                   { "Friction", c->Friction }, { "Restitution", c->Restitution },
                                   { "RestitutionThreshold", c->RestitutionThreshold }, { "IsSensor", c->IsSensor } };
        if (auto* c = entity.TryGetComponent<CircleCollider2DComponent>())
            e["CircleCollider2D"] = { { "Offset", ToJson(c->Offset) }, { "Radius", c->Radius }, { "Density", c->Density },
                                      { "Friction", c->Friction }, { "Restitution", c->Restitution },
                                      { "RestitutionThreshold", c->RestitutionThreshold }, { "IsSensor", c->IsSensor } };
        if (auto* c = entity.TryGetComponent<Rigidbody3DComponent>())
            e["Rigidbody3D"] = { { "BodyType", BodyType3DToString(c->Type) }, { "Mass", c->Mass }, { "LinearDamping", c->LinearDamping },
                                 { "AngularDamping", c->AngularDamping }, { "GravityFactor", c->GravityFactor }, { "LockRotation", c->LockRotation } };
        if (auto* c = entity.TryGetComponent<BoxCollider3DComponent>())
            e["BoxCollider3D"] = { { "HalfExtents", ToJson(c->HalfExtents) }, { "Offset", ToJson(c->Offset) }, { "Friction", c->Friction },
                                   { "Restitution", c->Restitution }, { "IsSensor", c->IsSensor } };
        if (auto* c = entity.TryGetComponent<SphereCollider3DComponent>())
            e["SphereCollider3D"] = { { "Radius", c->Radius }, { "Offset", ToJson(c->Offset) }, { "Friction", c->Friction },
                                      { "Restitution", c->Restitution }, { "IsSensor", c->IsSensor } };
        if (auto* c = entity.TryGetComponent<CapsuleCollider3DComponent>())
            e["CapsuleCollider3D"] = { { "Radius", c->Radius }, { "HalfHeight", c->HalfHeight }, { "Offset", ToJson(c->Offset) },
                                       { "Friction", c->Friction }, { "Restitution", c->Restitution }, { "IsSensor", c->IsSensor } };
        if (auto* c = entity.TryGetComponent<CharacterController3DComponent>())
            e["CharacterController3D"] = { { "Radius", c->Radius }, { "Height", c->Height }, { "MaxSlope", c->MaxSlope },
                                           { "StepHeight", c->StepHeight }, { "Mass", c->Mass }, { "GravityScale", c->GravityScale },
                                           { "StickToFloor", c->StickToFloor } };
        if (auto* c = entity.TryGetComponent<TilemapComponent>())
        {
            c->EnsureSize();
            e["Tilemap"] = { { "Tileset", c->TilesetPath }, { "PixelArt", c->PixelArt }, { "TilesetColumns", c->TilesetColumns },
                             { "TilesetRows", c->TilesetRows }, { "Width", c->Width }, { "Height", c->Height }, { "TileSize", c->TileSize },
                             { "Tiles", c->Tiles }, { "Color", ToJson(c->Color) }, { "SortingOrder", c->SortingOrder },
                             { "Collision", c->Collision }, { "Friction", c->Friction } };
        }
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
        if (auto* c = entity.TryGetComponent<UITransformComponent>())
            e["UITransform"] = { { "Anchor", (int)c->Anchor }, { "Position", ToJson(c->Position) }, { "Size", ToJson(c->Size) }, { "FontSize", c->FontSize }, { "Order", c->Order } };
        if (auto* c = entity.TryGetComponent<UIButtonComponent>())
            e["UIButton"] = { { "Normal", ToJson(c->NormalColor) }, { "Hover", ToJson(c->HoverColor) }, { "Pressed", ToJson(c->PressedColor) },
                              { "Interactable", c->Interactable } };
        if (auto* c = entity.TryGetComponent<AudioSourceComponent>())
            e["AudioSource"] = { { "Path", c->Path }, { "Volume", c->Volume }, { "Pitch", c->Pitch }, { "Loop", c->Loop }, { "PlayOnStart", c->PlayOnStart },
                                 { "Spatial", c->Spatial }, { "MinDistance", c->MinDistance }, { "MaxDistance", c->MaxDistance } };
        if (auto* c = entity.TryGetComponent<NativeScriptComponent>())
        {
            json fields = json::object();
            for (const auto& [name, value] : c->Fields)
                fields[name] = SerializeFieldValue(value);
            e["NativeScript"] = { { "Class", c->ClassName }, { "Fields", fields } };
        }
        return e;
    }

    static void DeserializeEntityComponents(const json& e, Entity entity)
    {
        if (e.contains("Transform"))
        {
            const json& j = e["Transform"];
            auto& t = entity.GetComponent<TransformComponent>();
            t.Translation = Vec(j, "Translation", glm::vec3(0.0f));
            t.Rotation = Vec(j, "Rotation", glm::vec3(0.0f));
            t.Scale = Vec(j, "Scale", glm::vec3(1.0f));
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
        }
        if (e.contains("SpriteRenderer"))
        {
            const json& j = e["SpriteRenderer"];
            auto& c = entity.AddComponent<SpriteRendererComponent>();
            c.Color = Vec(j, "Color", glm::vec4(1.0f));
            c.TexturePath = j.value("Texture", "");
            c.TilingFactor = j.value("Tiling", 1.0f);
            c.SortingOrder = j.value("SortingOrder", 0);
            c.PixelArt = j.value("PixelArt", false);
            c.FlipX = j.value("FlipX", false);
            c.FlipY = j.value("FlipY", false);
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
        }
        if (e.contains("CircleRenderer"))
        {
            const json& j = e["CircleRenderer"];
            auto& c = entity.AddComponent<CircleRendererComponent>();
            c.Color = Vec(j, "Color", glm::vec4(1.0f));
            c.Thickness = j.value("Thickness", 1.0f);
            c.Fade = j.value("Fade", 0.005f);
            c.SortingOrder = j.value("SortingOrder", 0);
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
            c.FontPath = j.value("Font", "");
        }
        if (e.contains("MeshRenderer"))
        {
            const json& j = e["MeshRenderer"];
            auto& c = entity.AddComponent<MeshRendererComponent>();
            c.Type = PrimitiveMeshFromString(j.value("Mesh", "Cube"));
            c.ModelPath = j.value("Model", "");
            c.CastShadows = j.value("CastShadows", true);
            c.Mat.Albedo = Vec(j, "Albedo", glm::vec4(1.0f));
            c.AlbedoPath = j.value("AlbedoMap", "");
            c.NormalPath = j.value("NormalMap", "");
            c.Mat.NormalStrength = j.value("NormalStrength", 1.0f);
            c.MetalRoughPath = j.value("MetalRoughMap", "");
            c.EmissivePath = j.value("EmissiveMap", "");
            c.Mat.Tiling = j.value("Tiling", 1.0f);
            c.Mat.Metallic = j.value("Metallic", 0.0f);
            c.Mat.Roughness = j.value("Roughness", 0.5f);
            c.Mat.Emission = Vec(j, "Emission", glm::vec3(0.0f));
            c.Mat.EmissionStrength = j.value("EmissionStrength", 0.0f);
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
        }
        auto readCollider = [](const json& j, auto& c) {
            c.Offset = Vec(j, "Offset", glm::vec2(0.0f));
            c.Density = j.value("Density", 1.0f);
            c.Friction = j.value("Friction", 0.5f);
            c.Restitution = j.value("Restitution", 0.0f);
            c.RestitutionThreshold = j.value("RestitutionThreshold", 0.5f);
            c.IsSensor = j.value("IsSensor", false);
        };
        if (e.contains("BoxCollider2D"))
        {
            auto& c = entity.AddComponent<BoxCollider2DComponent>();
            readCollider(e["BoxCollider2D"], c);
            c.Size = Vec(e["BoxCollider2D"], "Size", glm::vec2(0.5f));
        }
        if (e.contains("CircleCollider2D"))
        {
            auto& c = entity.AddComponent<CircleCollider2DComponent>();
            readCollider(e["CircleCollider2D"], c);
            c.Radius = e["CircleCollider2D"].value("Radius", 0.5f);
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
        }
        auto read3D = [](const json& j, auto& c) {
            c.Offset = Vec(j, "Offset", glm::vec3(0.0f));
            c.Friction = j.value("Friction", 0.5f);
            c.Restitution = j.value("Restitution", 0.0f);
            c.IsSensor = j.value("IsSensor", false);
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
        }
        if (e.contains("Tilemap"))
        {
            const json& j = e["Tilemap"];
            auto& c = entity.AddComponent<TilemapComponent>();
            c.TilesetPath = j.value("Tileset", "");
            c.PixelArt = j.value("PixelArt", true);
            c.TilesetColumns = j.value("TilesetColumns", 8);
            c.TilesetRows = j.value("TilesetRows", 4);
            c.Width = j.value("Width", 32);
            c.Height = j.value("Height", 12);
            c.TileSize = j.value("TileSize", 1.0f);
            if (j.contains("Tiles") && j["Tiles"].is_array()) c.Tiles = j["Tiles"].get<std::vector<int>>();
            c.EnsureSize();
            c.Color = Vec(j, "Color", glm::vec4(1.0f));
            c.SortingOrder = j.value("SortingOrder", -1);
            c.Collision = j.value("Collision", true);
            c.Friction = j.value("Friction", 0.6f);
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
            c.TexturePath = j.value("Texture", "");
            c.SortingOrder = j.value("SortingOrder", 0);
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
            c.Path = j.value("Path", "");
            c.Volume = j.value("Volume", 1.0f);
            c.Pitch = j.value("Pitch", 1.0f);
            c.Loop = j.value("Loop", false);
            c.PlayOnStart = j.value("PlayOnStart", true);
            c.Spatial = j.value("Spatial", false);
            c.MinDistance = j.value("MinDistance", 1.0f);
            c.MaxDistance = j.value("MaxDistance", 30.0f);
        }
        if (e.contains("NativeScript"))
        {
            const json& j = e["NativeScript"];
            auto& c = entity.AddComponent<NativeScriptComponent>();
            c.ClassName = j.value("Class", "");
            if (j.contains("Fields") && j["Fields"].is_object())
                for (auto it = j["Fields"].begin(); it != j["Fields"].end(); ++it)
                {
                    ScriptFieldValue v;
                    if (DeserializeFieldValue(it.value(), v))
                        c.Fields[it.key()] = v;
                }
        }
        
    }

    std::string SceneSerializer::SerializeToString()
    {
        const auto& s = m_Scene->GetSettings();
        json root;
        root["Scene"] = m_Scene->GetName();
        root["Version"] = 1;
        root["Settings"] = {
            { "DrawSky", s.DrawSky }, { "SkyTop", ToJson(s.SkyTopColor) }, { "SkyHorizon", ToJson(s.SkyHorizonColor) },
            { "Ground", ToJson(s.GroundColor) }, { "Ambient", s.AmbientIntensity }, { "Exposure", s.Exposure },
            { "ShadowDistance", s.ShadowDistance }, { "Gravity", ToJson(s.Gravity) },
            { "Environment", { { "Map", s.EnvironmentMap }, { "Intensity", s.EnvironmentIntensity }, { "Rotation", s.EnvironmentRotation } } },
            { "Fog", { { "Enabled", s.FogEnabled }, { "Color", ToJson(s.FogColor) }, { "Density", s.FogDensity }, { "HeightFalloff", s.FogHeightFalloff } } }, { "Gravity3D", ToJson(s.Gravity3D) },
            { "Post", {
                { "Tonemap", (int)s.Post.Tonemap }, { "Bloom", s.Post.Bloom },
                { "SSAO", s.Post.SSAO }, { "SSAORadius", s.Post.SSAORadius }, { "SSAOIntensity", s.Post.SSAOIntensity }, { "SSAOBias", s.Post.SSAOBias }, { "BloomThreshold", s.Post.BloomThreshold }, { "BloomKnee", s.Post.BloomKnee },
                { "BloomIntensity", s.Post.BloomIntensity }, { "BloomRadius", s.Post.BloomRadius }, { "Vignette", s.Post.Vignette },
                { "Saturation", s.Post.Saturation }, { "Contrast", s.Post.Contrast }, { "Brightness", s.Post.Brightness },
                { "Tint", ToJson(s.Post.Tint) } } },
        };

        json entities = json::array();
        for (Entity e : m_Scene->GetEntitiesInOrder()) // creation order => stable diffs
            entities.push_back(SerializeEntity(e));
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
        std::string text = FileSystem::ReadTextFile(filepath);
        if (text.empty())
            return false;
        if (!DeserializeFromString(text))
        {
            ZM_CORE_ERROR("Sahna o'qilmadi: '{}'", filepath.string());
            return false;
        }
        if (m_Scene->GetName().empty() || m_Scene->GetName() == "Untitled")
            m_Scene->SetName(filepath.stem().string());
        return true;
    }

    bool SceneSerializer::DeserializeFromString(const std::string& text)
    {
        json root;
        try { root = json::parse(text); }
        catch (const std::exception& e)
        {
            ZM_CORE_ERROR("JSON xatosi: {}", e.what());
            return false;
        }
        if (!root.contains("Scene"))
            return false;

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
                s.AmbientIntensity = j.value("Ambient", s.AmbientIntensity);
                s.Exposure = j.value("Exposure", s.Exposure);
                s.ShadowDistance = j.value("ShadowDistance", s.ShadowDistance);
                s.Gravity = Vec(j, "Gravity", s.Gravity);
                s.Gravity3D = Vec(j, "Gravity3D", s.Gravity3D);
                if (j.contains("Environment"))
                {
                    const json& en = j["Environment"];
                    s.EnvironmentMap = en.value("Map", "");
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
                    s.Post.Vignette = p.value("Vignette", 0.0f);
                    s.Post.Saturation = p.value("Saturation", 1.0f);
                    s.Post.Contrast = p.value("Contrast", 1.0f);
                    s.Post.Brightness = p.value("Brightness", 0.0f);
                    s.Post.Tint = Vec(p, "Tint", glm::vec3(1.0f));
                }
            }

            std::vector<std::pair<Entity, uint64_t>> parents;
            for (const json& e : root.value("Entities", json::array()))
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
        }
        catch (const std::exception& ex)
        {
            ZM_CORE_ERROR("Sahna formati xato: {}", ex.what());
            return false;
        }
        return true;
    }

}

namespace Zamin {

    std::string SceneSerializer::SerializeEntities(Scene& scene, const std::vector<Entity>& entities)
    {
        std::unordered_map<uint64_t, bool> inSet;
        for (Entity e : entities) inSet[(uint64_t)e.GetUUID()] = true;
        json list = json::array();
        for (Entity e : entities)
        {
            json j = SerializeEntity(e, true);
            if (j.contains("Parent") && !inSet.count(j["Parent"].get<uint64_t>()))
                j.erase("Parent");
            list.push_back(j);
        }
        json root;
        root["ZaminEntities"] = 1;
        root["Entities"] = list;
        return root.dump(2);
    }

    std::vector<Entity> SceneSerializer::DeserializeEntities(Scene& scene, const std::string& text, bool generateNewUUIDs)
    {
        std::vector<Entity> created;
        json root;
        try { root = json::parse(text); }
        catch (...) { return created; }
        if (!root.contains("Entities") || !root["Entities"].is_array())
            return created;

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
        }
        catch (const std::exception& ex)
        {
            ZM_CORE_ERROR("Entity ma'lumotlari xato: {}", ex.what());
        }
        // Roots first
        std::stable_partition(created.begin(), created.end(), [](Entity e) {
            return !(uint64_t)e.GetComponent<RelationshipComponent>().Parent;
        });
        return created;
    }

}
