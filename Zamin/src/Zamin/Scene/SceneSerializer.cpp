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
            default: return false;
        }
        return true;
    }

    static json SerializeEntity(Entity entity)
    {
        json e;
        e["Entity"] = (uint64_t)entity.GetUUID();
        e["Tag"] = entity.GetName();

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
            e["SpriteRenderer"] = { { "Color", ToJson(c->Color) }, { "Texture", c->TexturePath }, { "Tiling", c->TilingFactor }, { "SortingOrder", c->SortingOrder } };
        if (auto* c = entity.TryGetComponent<CircleRendererComponent>())
            e["CircleRenderer"] = { { "Color", ToJson(c->Color) }, { "Thickness", c->Thickness }, { "Fade", c->Fade }, { "SortingOrder", c->SortingOrder } };
        if (auto* c = entity.TryGetComponent<TextComponent>())
            e["Text"] = { { "Text", c->Text }, { "Color", ToJson(c->Color) }, { "Kerning", c->Kerning }, { "LineSpacing", c->LineSpacing },
                          { "Centered", c->Centered }, { "Font", c->FontPath } };
        if (auto* c = entity.TryGetComponent<MeshRendererComponent>())
            e["MeshRenderer"] = {
                { "Mesh", PrimitiveMeshToString(c->Type) }, { "Model", c->ModelPath }, { "CastShadows", c->CastShadows },
                { "Albedo", ToJson(c->Mat.Albedo) }, { "AlbedoMap", c->AlbedoPath }, { "Tiling", c->Mat.Tiling },
                { "Metallic", c->Mat.Metallic }, { "Roughness", c->Mat.Roughness },
                { "Emission", ToJson(c->Mat.Emission) }, { "EmissionStrength", c->Mat.EmissionStrength },
            };
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
        if (auto* c = entity.TryGetComponent<AudioSourceComponent>())
            e["AudioSource"] = { { "Path", c->Path }, { "Volume", c->Volume }, { "Pitch", c->Pitch }, { "Loop", c->Loop }, { "PlayOnStart", c->PlayOnStart } };
        if (auto* c = entity.TryGetComponent<NativeScriptComponent>())
        {
            json fields = json::object();
            for (const auto& [name, value] : c->Fields)
                fields[name] = SerializeFieldValue(value);
            e["NativeScript"] = { { "Class", c->ClassName }, { "Fields", fields } };
        }
        return e;
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
        };

        // Keep creation order for stable diffs.
        std::vector<entt::entity> ordered;
        for (auto e : m_Scene->m_Registry.view<IDComponent>())
            ordered.push_back(e);
        std::reverse(ordered.begin(), ordered.end());

        json entities = json::array();
        for (auto e : ordered)
            entities.push_back(SerializeEntity({ e, m_Scene.get() }));
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
            }

            for (const json& e : root.value("Entities", json::array()))
            {
                uint64_t uuid = e.value("Entity", (uint64_t)0);
                Entity entity = m_Scene->CreateEntityWithUUID(uuid ? UUID(uuid) : UUID(), e.value("Tag", "Entity"));

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
                    c.Mat.Tiling = j.value("Tiling", 1.0f);
                    c.Mat.Metallic = j.value("Metallic", 0.0f);
                    c.Mat.Roughness = j.value("Roughness", 0.5f);
                    c.Mat.Emission = Vec(j, "Emission", glm::vec3(0.0f));
                    c.Mat.EmissionStrength = j.value("EmissionStrength", 0.0f);
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
                if (e.contains("AudioSource"))
                {
                    const json& j = e["AudioSource"];
                    auto& c = entity.AddComponent<AudioSourceComponent>();
                    c.Path = j.value("Path", "");
                    c.Volume = j.value("Volume", 1.0f);
                    c.Pitch = j.value("Pitch", 1.0f);
                    c.Loop = j.value("Loop", false);
                    c.PlayOnStart = j.value("PlayOnStart", true);
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
        }
        catch (const std::exception& ex)
        {
            ZM_CORE_ERROR("Sahna formati xato: {}", ex.what());
            return false;
        }
        return true;
    }

}
