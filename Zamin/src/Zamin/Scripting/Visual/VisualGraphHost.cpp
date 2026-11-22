#include "VisualGraphHost.h"
#include "Zamin/Project/Project.h"
#include "Zamin/Scene/SceneManager.h"
#include "Zamin/Scene/WorldStreaming.h"
#include "Zamin/Utils/FileSystem.h"
#include "Zamin/Core/Log.h"
#include "Zamin/Core/Display.h"
#include "Zamin/Audio/Audio.h"
#include "Zamin/Audio/AudioMixer.h"
#include "Zamin/Localization/Localization.h"
#include "Zamin/Math/Math.h"
#include "Zamin/Physics/PhysicsWorld2D.h"
#include "Zamin/Physics/PhysicsWorld3D.h"
#include <stdexcept>
#include <cmath>
#include <limits>
#include <charconv>
#include <algorithm>
#include <cstdlib>
namespace Zamin {
    Visual::Value DispatchAnimatorGraph(Entity target, const std::string& kind, const std::map<std::string, Visual::Value>& args);

    ScriptClassInfo MakeVisualGraphClassInfo()
    {
        return { "Zamin.VisualGraph", []() -> ScriptableEntity* { return new VisualGraphHost; }, { ZM_FIELD(VisualGraphHost, Graph), ZM_FIELD(VisualGraphHost, Overrides) } };
    }
    void VisualGraphHost::OnCreate()
    {
        m_Gameplay.Reset();
        try
        {
            auto root = Project::GetAssetDirectory();
            auto path = FileSystem::SafeJoin(root, Graph);
            if (Graph.empty() || path.empty() || !FileSystem::IsSubPathOnDisk(path, root)) throw std::runtime_error("Graph must be inside Assets");
            auto overrides = Overrides.empty() ? Visual::Value::object() : Visual::Value::parse(Overrides);
            if (VM.Load(Visual::Graph::Load(path), [this](const auto& k, const auto& args) { return Bridge(k, args); }, overrides)) Emit("Create");
            else Emit("Invalid");
        }
        catch (const std::exception& e)
        {
            Visual::Graph invalid;
            invalid.Nodes.push_back({ 1, std::string("Load failed: ") + e.what() });
            VM.Load(std::move(invalid));
            m_Reported = true;
            ZM_CORE_ERROR("Visual graph '{}': {}", Graph, e.what());
        }
    }
    void VisualGraphHost::Emit(const std::string& event, Visual::Value payload)
    {
        if (VM.Paused && (event == "Update" || event == "LateUpdate")) return;
        VM.Event(event, std::move(payload));
        if ((VM.Paused || !VM.Error().empty()) && GetEntity()) m_Gameplay.Stop(GetScene());
        if (VM.Error().empty()) Overrides = Visual::Value(VM.Variables()).dump();
        if (!VM.Error().empty() && !m_Reported)
        {
            ZM_CORE_ERROR("Visual graph '{}' entity '{}': {}", Graph, GetName(), VM.Error());
            m_Reported = true;
        }
    }
    void VisualGraphHost::OnDisable()
    {
        Emit("Disable");
        m_Gameplay.Stop(GetScene());
    }
    void VisualGraphHost::OnDestroy()
    {
        Emit("Destroy");
        m_Gameplay.Stop(GetScene());
    }
    void VisualGraphHost::OnUpdate(Timestep dt)
    {
        m_Gameplay.BeginFrame(GetScene());
        if (VM.Paused || !VM.Error().empty()) m_Gameplay.Stop(GetScene());
        const auto link = GetNavTraversalLink();
        auto* nav = GetEntity().TryGetComponent<NavAgentComponent>();
        if (link && nav && (m_TraversalLink != nav->TraversalTicket || m_TraversalRevision != nav->AtlasRevision))
        {
            m_TraversalLink = nav->TraversalTicket;
            m_TraversalRevision = nav->AtlasRevision;
            Emit("NavTraversalRequested", { { "Link", std::to_string(link) }, { "Revision", std::to_string(nav->AtlasRevision) }, { "Ticket", std::to_string(nav->TraversalTicket) }, { "Entry", { nav->TraversalEntry.x, nav->TraversalEntry.y, nav->TraversalEntry.z } }, { "Exit", { nav->TraversalExit.x, nav->TraversalExit.y, nav->TraversalExit.z } } });
        }
        VM.Tick((float)dt);
        Emit("Update", { { "Delta", (float)dt } });
    }
    Visual::Value VisualGraphHost::Bridge(const std::string& k, const std::map<std::string, Visual::Value>& a)
    {
        using V = Visual::Value;
        auto s = [&](const char* n) { return a.at(n).get<std::string>(); };
        auto scalar = [](const V& v) {double number=v.get<double>();if(!std::isfinite(number)||std::abs(number)>std::numeric_limits<float>::max())throw std::runtime_error("Engine float overflow");return (float)number; };
        auto f = [&](const char* n) { return scalar(a.at(n)); };
        auto vec = [&](const char* n) {auto&v=a.at(n);return glm::vec3(scalar(v.at(0)),scalar(v.at(1)),v.size()>2?scalar(v.at(2)):0); };
        auto value = [](const auto& v) -> V {if constexpr(requires{v.z;})return V::array({v.x,v.y,v.z});else return V::array({v.x,v.y}); };
        if (k == "Self") return (uint64_t)GetEntity().GetUUID();
        if (k == "FindEntity")
        {
            auto e = FindEntity(s("Name"));
            return e ? (uint64_t)e.GetUUID() : 0ull;
        }
        if (k == "Axis") return Input::GetAxis(s("Name"));
        if (k == "ActionPressed") return Input::IsActionPressed(s("Name"));
        if (k == "ActionJustPressed") return Input::IsActionJustPressed(s("Name"));
        if (k == "Print")
        {
            ZM_CORE_INFO("[Graph {}] {}", GetName(), s("Text"));
            return nullptr;
        }
        if (k == "PlaySound")
        {
            PlaySound(s("Path"), f("Volume"), f("Pitch"));
            return nullptr;
        }
        if (k == "SpawnPrefab")
        {
            auto e = Instantiate(s("Path"), vec("Position"));
            return e ? (uint64_t)e.GetUUID() : 0ull;
        }
        if (k == "LoadScene")
        {
            // FadeOut 0 and FadeIn -1 (the defaults, and every graph written before the pins) switch at once.
            const float out = f("FadeOut"), in = f("FadeIn");
            if (out < 0 || out > 10 || (in < 0 && in != -1) || in > 10) throw std::runtime_error("LoadScene fades must be 0..10 s (FadeIn -1: as FadeOut)");
            auto& c = a.at("Color");
            const glm::vec4 color{ scalar(c.at(0)), scalar(c.at(1)), scalar(c.at(2)), scalar(c.at(3)) };
            for (int i = 0; i < 4; ++i)
                if (color[i] < 0 || color[i] > 1) throw std::runtime_error("LoadScene colour must be 0..1");
            const auto transition = SceneTransition::Fade(out, in, color);
            if (transition.IsNone()) LoadScene(s("Path"));
            else LoadScene(s("Path"), transition);
            return nullptr;
        }
        if (k == "QuitGame")
        {
            QuitGame();
            return nullptr;
        }
        if (k == "App.DemoMode")
        {
            static const bool demo = [] { const char* v = std::getenv("ZAMIN_DEMO"); return v && *v && std::string_view(v) != "0"; }();
            return demo;
        }
        if (k == "Audio.SetGroupVolume" || k == "Audio.GroupVolume")
        {
            const auto group = s("Group");
            if (group.empty() || group.size() > 64) throw std::runtime_error("Audio group name must be 1..64 characters");
            if (k == "Audio.GroupVolume") return Audio::GetGroupVolume(group);
            const float volume = f("Volume");
            if (volume < 0 || volume > AudioSettings::MaxVolume) throw std::runtime_error("Audio group volume must be 0..2");
            return V{ { "Success", Audio::SetGroupVolume(group, volume) } };
        }
        if (k == "Localization.SetLanguage") return V{ { "Success", Localization::SetLanguage(s("Code")) } };
        if (k == "Localization.Language") return Localization::GetLanguage();
        if (k == "Localization.Text")
        {
            const auto key = s("Key");
            if (key.size() > 128) throw std::runtime_error("Localization key limit 128 bytes");
            const auto* text = key.empty() ? nullptr : Localization::Find(key);
            return text ? *text : s("Fallback");
        }
        if (k == "Display.SetFullscreen")
        {
            Display::SetFullscreen(a.at("Fullscreen").get<bool>());
            return nullptr;
        }
        if (k == "Display.IsFullscreen") return Display::IsFullscreen();
        if (k == "Display.SetUIScale" || k == "Display.UIScale")
        {
            // The UI scale setting: the scene manager's (every scene, and the ones loaded later), else this scene's.
            Scene* scene = &GetScene();
            SceneManager* manager = scene->GetSceneManager();
            if (k == "Display.UIScale") return double(manager ? manager->GetUIScaleMultiplier() : scene->GetUIScaleMultiplier());
            const float scale = f("Scale");
            if (!std::isfinite(scale) || scale < Scene::MinUIScaleMultiplier || scale > Scene::MaxUIScaleMultiplier)
                throw std::runtime_error("Display.SetUIScale: Scale must be 0.5..2");
            if (manager) manager->SetUIScaleMultiplier(scale);
            else scene->SetUIScaleMultiplier(scale);
            return V{ { "Success", true } };
        }
        if (k == "ShakeCamera")
        {
            const float intensity = f("Intensity"), duration = f("Duration"), frequency = f("Frequency");
            if (intensity < 0 || intensity > 1 || duration < 0 || duration > 5 || frequency < .1f || frequency > 100) throw std::runtime_error("ShakeCamera: Intensity 0..1, Duration 0..5 s, Frequency .1..100");
            return V{ { "Success", ShakeCamera(intensity, duration, frequency) } };
        }
        if (k == "SaveGameState")
        {
            GameState::Save();
            return nullptr;
        }
        if (k == "LoadGameState")
        {
            GameState::Load();
            return nullptr;
        }
        if (k.starts_with("ReadState."))
        {
            auto key = s("Key");
            if (k.ends_with("Number")) return GameState::GetFloat(key, a.at("Default").get<float>());
            if (k.ends_with("Bool")) return GameState::GetBool(key, a.at("Default").get<bool>());
            return GameState::GetString(key, a.at("Default").get<std::string>());
        }
        if (k.starts_with("WriteState."))
        {
            auto key = s("Key");
            if (k.ends_with("Number")) GameState::SetFloat(key, f("Value"));
            else if (k.ends_with("Bool")) GameState::SetBool(key, a.at("Value").get<bool>());
            else GameState::SetString(key, s("Value"));
            GameState::SetPersistent(key, a.at("Persistent").get<bool>());
            return nullptr;
        }
        if (k == "Raycast2D" || k == "Raycast3D")
        {
            auto hit = k == "Raycast2D" ? GetScene().Raycast2D(glm::vec2(vec("Origin")), glm::vec2(vec("Direction")), f("Distance")) : GetScene().Raycast3D(vec("Origin"), vec("Direction"), f("Distance"));
            return V{ { "Hit", hit.Hit }, { "Entity", (uint64_t)hit.EntityID }, { "Point", value(hit.Point) }, { "Normal", value(hit.Normal) }, { "Distance", hit.Distance } };
        }

// An omitted Target means Self. An explicitly linked zero UUID stays invalid.
        Entity target = GetEntity();
        auto it = a.find("Target");
        if (it != a.end())
        {
            uint64_t id = it->second.get<uint64_t>();
            target = id == UINT64_MAX ? GetEntity() : (id ? Resolve(EntityRef{ id }) : Entity{});
        }
        if (k == "EntityValid") return (bool)target;
        if (!target) throw std::runtime_error("Target entity no longer exists");
        if (k.starts_with("ReadField.") || k.starts_with("WriteField."))
        {
            auto* script = target.TryGetComponent<NativeScriptComponent>();
            if (!script || script->ClassName == "Zamin.VisualGraph") throw std::runtime_error("Target needs a native C++ script");
            auto* info = ScriptRegistry::Find(script->ClassName);
            if (!info) throw std::runtime_error("Target script class is not registered");
            const ScriptFieldInfo* field = nullptr;
            for (auto& entry : info->Fields)
                if (entry.Name == s("Name"))
                {
                    field = &entry;
                    break;
                }
            if (!field) throw std::runtime_error("Reflected C++ field not found");
            const auto expected = k.substr(k.find('.') + 1);
            const auto ft = field->Type;
            std::string actual = ft == ScriptFieldType::Float || ft == ScriptFieldType::Int ? "Number" : ScriptFieldTypeToString(ft);
            if (actual != expected) throw std::runtime_error("C++ field type differs from node");
            const bool live = script->Instance && script->Instance->GetScriptClassName() == script->ClassName;
            ScriptFieldValue fieldValue;
            if (k.starts_with("WriteField."))
            {
                auto& v = a.at("Value");
                switch (ft)
                {
                    case ScriptFieldType::Float:
                    {
                        double n = v.get<double>();
                        if (std::abs(n) > std::numeric_limits<float>::max()) throw std::runtime_error("Float overflow");
                        fieldValue.Set((float)n);
                        break;
                    }
                    case ScriptFieldType::Int:
                    {
                        double n = v.get<double>();
                        if (n < std::numeric_limits<int>::min() || n > std::numeric_limits<int>::max() || std::floor(n) != n) throw std::runtime_error("Integer overflow or fractional value");
                        fieldValue.Set((int)n);
                        break;
                    }
                    case ScriptFieldType::Bool:   fieldValue.Set(v.get<bool>()); break;
                    case ScriptFieldType::String: fieldValue.Set(v.get<std::string>()); break;
                    case ScriptFieldType::Entity: fieldValue.Set(EntityRef{ v.get<uint64_t>() }); break;
                    case ScriptFieldType::Vec2:   fieldValue.Set(glm::vec2(vec("Value"))); break;
                    case ScriptFieldType::Vec3:   fieldValue.Set(vec("Value")); break;
                    case ScriptFieldType::Vec4:   fieldValue.Set(glm::vec4(scalar(v[0]), scalar(v[1]), scalar(v[2]), scalar(v[3]))); break;
                    default:                      throw std::runtime_error("Unsupported reflected field type");
                }
                field->ClampToRange(fieldValue);
                if (live && !field->Store(script->Instance, fieldValue)) throw std::runtime_error("C++ field rejected value");
                script->Fields[field->Name] = fieldValue;
                return nullptr;
            }
            if (live) field->Load(script->Instance, fieldValue);
            else
            {
                auto found = script->Fields.find(field->Name);
                if (found == script->Fields.end()) throw std::runtime_error("C++ field has no saved value and no live instance");
                fieldValue = found->second;
            }
            switch (ft)
            {
                case ScriptFieldType::Float:  return fieldValue.Get<float>();
                case ScriptFieldType::Int:    return fieldValue.Get<int>();
                case ScriptFieldType::Bool:   return fieldValue.Get<bool>();
                case ScriptFieldType::String: return fieldValue.StringValue;
                case ScriptFieldType::Entity: return fieldValue.Get<EntityRef>().ID;
                case ScriptFieldType::Vec2:   return value(fieldValue.Get<glm::vec2>());
                case ScriptFieldType::Vec3:   return value(fieldValue.Get<glm::vec3>());
                case ScriptFieldType::Vec4:
                {
                    auto v = fieldValue.Get<glm::vec4>();
                    return V::array({ v.x, v.y, v.z, v.w });
                }
                default: throw std::runtime_error("Unsupported reflected field type");
            }
        }
        if (k.starts_with("Gameplay.")) return m_Gameplay.Dispatch(GetEntity(), target, k, a);
        VisualGraphHost proxy;
        proxy.m_Entity = target;
        auto& t = target.GetComponent<TransformComponent>();
        if (k == "GetPosition") return value(t.Translation);
        if (k == "GetRotation") return value(t.Rotation);
        if (k == "GetScale") return value(t.Scale);
        if (k == "GetVelocity2D") return value(proxy.GetVelocity());
        if (k == "GetVelocity3D") return value(proxy.GetVelocity3D());
        if (k == "SetPosition" || k == "Translate" || k == "SetRotation")
        {
            if (k == "SetRotation") t.Rotation = vec("Vector");
            else t.Translation = vec("Vector") + (k == "Translate" ? t.Translation : glm::vec3(0));
            glm::vec3 worldPosition, worldRotation, worldScale;
            if (!Math::DecomposeTransform(target.GetScene()->GetWorldTransform(target), worldPosition, worldRotation, worldScale)) throw std::runtime_error("Invalid world transform");
            if (auto* body = target.TryGetComponent<Rigidbody2DComponent>(); body && body->RuntimeBody)
            {
                PhysicsWorld2D::SetTransform((b2Body*)body->RuntimeBody, glm::vec2(worldPosition), worldRotation.z);
                target.GetScene()->SetAwake2D(target, true);
            }
            if (auto* body = target.TryGetComponent<Rigidbody3DComponent>(); body && body->RuntimeBodyID != 0xffffffff)
            {
                if (auto* world = target.GetScene()->GetPhysicsWorld3D()) world->SetTransform(body->RuntimeBodyID, worldPosition, glm::quat(worldRotation));
            }
        }
        else if (k == "SetScale") t.Scale = vec("Vector");
        else if (k == "DestroyEntity") Destroy(target);
        else if (k == "SetActive") SetActive(target, a.at("Active").get<bool>());
        else if (k == "HasTag") return target.HasTag(s("Tag"));
        else if (k == "AddTag") target.AddTag(s("Tag"));
        else if (k == "RemoveTag") target.RemoveTag(s("Tag"));
        else if (k == "SetVelocity2D") proxy.SetVelocity(glm::vec2(vec("Vector")));
        else if (k == "ApplyForce2D") proxy.ApplyForce(glm::vec2(vec("Vector")));
        else if (k == "ApplyImpulse2D") proxy.ApplyImpulse(glm::vec2(vec("Vector")));
        else if (k == "SetVelocity3D") proxy.SetVelocity3D(vec("Vector"));
        else if (k == "ApplyForce3D") proxy.ApplyForce3D(vec("Vector"));
        else if (k == "ApplyImpulse3D") proxy.ApplyImpulse3D(vec("Vector"));
        else if (k == "ApplyTorque3D") proxy.ApplyTorque3D(vec("Vector"));
        else if (k == "SetWorldDestination")
        {
            auto* nav = target.TryGetComponent<NavAgentComponent>();
            if (!nav) return false;
            const auto position = vec("Position");
            nav->WorldNavigation = true;
            return GetScene().SetNavDestination(target, position);
        }
        else if (k == "NavRecoveryState")
        {
            const auto* nav = target.TryGetComponent<NavAgentComponent>();
            return V{ { "Phase", nav ? NavRecovery::Name(nav->Recovery.GetPhase()) : "Idle" }, { "Retries", nav ? nav->Recovery.GetRetries() : 0 }, { "StalledSeconds", nav ? nav->Recovery.GetStalledSeconds() : 0 }, { "Cooldown", nav ? nav->Recovery.GetCooldown() : 0 }, { "Exhausted", nav && nav->Recovery.IsExhausted() } };
        }
        else if (k == "RetryNavigation")
        {
            auto* nav = target.TryGetComponent<NavAgentComponent>();
            if (!nav || !nav->WorldNavigation || !nav->HasDestination) return false;
            nav->Recovery.Reset();
            nav->RepathRequested = true;
            nav->Arrived = false;
            nav->PendingTraversalLink = nav->TraversalTicket = 0;
            return true;
        }
        else if (k == "NavSteeringState")
        {
            const auto* nav = target.TryGetComponent<NavAgentComponent>();
            return V{ { "Preferred", nav ? value(nav->PreferredVelocity) : V::array({ 0, 0, 0 }) }, { "Velocity", nav ? value(nav->Velocity) : V::array({ 0, 0, 0 }) }, { "Neighbors", nav ? nav->CrowdNeighbors : 0 }, { "Blocked", nav && nav->CrowdBlocked }, { "Limited", nav && nav->CrowdLimited } };
        }
        else if (k == "NavQueryPending")
        {
            auto* manager = GetScene().GetSceneManager();
            auto* world = manager ? manager->GetWorldStreaming() : nullptr;
            auto* nav = target.TryGetComponent<NavAgentComponent>();
            return world && world->IsAnchor(&GetScene()) && nav && nav->WorldNavigation && nav->HasDestination && world->HasNavigationQuery(target.GetUUID());
        }
        else if (k == "NavTraversalState")
        {
            auto* nav = target.TryGetComponent<NavAgentComponent>();
            const bool pending = GetScene().GetNavTraversalLink(target) != 0;
            return V{ { "Pending", pending }, { "Link", pending ? std::to_string(nav->PendingTraversalLink) : "0" }, { "Revision", nav ? std::to_string(nav->AtlasRevision) : "0" }, { "Ticket", pending ? std::to_string(nav->TraversalTicket) : "0" }, { "Entry", nav ? value(nav->TraversalEntry) : V::array({ 0, 0, 0 }) }, { "Exit", nav ? value(nav->TraversalExit) : V::array({ 0, 0, 0 }) }, { "Status", nav ? NavPathStatusName(nav->PathStatus) : "None" } };
        }
        else if (k == "CompleteNavTraversal")
        {
            auto token = [&](const char* name) { const auto text = s(name); uint64_t id = 0; const auto result = std::from_chars(text.data(), text.data()+text.size(), id); return result.ec == std::errc{} && result.ptr == text.data()+text.size() ? id : uint64_t(0); };
            return GetScene().CompleteNavTraversal(target, token("Link"), token("Revision"), token("Ticket"));
        }
        else if (k == "CancelNavTraversal") return GetScene().CancelNavTraversal(target);
        else if (k == "SetDestination") proxy.SetDestination(vec("Position"));
        else if (k == "StopMoving") proxy.StopMoving(a.at("Immediate").get<bool>());
        else if (k == "IsMoving") return proxy.IsMoving();
        else if (k == "HasArrived") return proxy.HasArrived();
        else if (k == "RemainingDistance") return proxy.GetRemainingDistance();
        else if (k == "PlayAudio") proxy.PlayAudio();
        else if (k == "StopAudio") proxy.StopAudio();
        else if (k == "StopAnimation") proxy.StopAnimation();
        else if (k == "EmitParticles")
        {
            float count = f("Count");
            if (count < 0 || count > 10000 || std::floor(count) != count) throw std::runtime_error("Particle count must be integer 0..10000");
            proxy.EmitParticles((int)count);
        }
        else if (k.starts_with("Animator.")) return DispatchAnimatorGraph(target, k, a);
        else if (k == "PlayAnimation") proxy.PlayAnimation(s("Clip"), f("Speed"));
        else if (k == "PlaySpriteClip") return V{ { "Success", proxy.PlaySpriteClip(s("Clip"), a.at("Restart").get<bool>()) } };
        else if (k == "FadeAudio")
        {
            const float volume = f("Volume"), seconds = f("Seconds");
            if (volume < 0 || volume > AudioSettings::MaxVolume || seconds < 0 || seconds > 30) throw std::runtime_error("FadeAudio: Volume 0..2, Seconds 0..30");
            return V{ { "Success", FadeAudio(target, volume, seconds) } };
        }
        else if (k == "UIValue")
        {
            if (auto* c = target.TryGetComponent<UISliderComponent>()) return V{ { "Found", true }, { "Value", c->Value }, { "On", c->Value > c->MinValue } };
            if (auto* c = target.TryGetComponent<UIToggleComponent>()) return V{ { "Found", true }, { "Value", c->IsOn ? 1.0 : 0.0 }, { "On", c->IsOn } };
            if (auto* c = target.TryGetComponent<UIProgressBarComponent>()) return V{ { "Found", true }, { "Value", c->Value }, { "On", c->Value > 0 } };
            return V{ { "Found", false }, { "Value", 0.0 }, { "On", false } };
        }
        else if (k == "SetUIValue")
        {
            // Notify (default off) raises ValueChanged/ToggleChanged as a user change would: a settings panel
            // shows stored values without re-applying them.
            const float value = f("Value");
            const bool notify = a.at("Notify").get<bool>();
            if (target.HasComponent<UISliderComponent>()) return V{ { "Success", GetScene().SetSliderValue(target, value, notify) } };
            if (target.HasComponent<UIToggleComponent>()) return V{ { "Success", GetScene().SetToggle(target, value != 0, notify) } };
            if (auto* c = target.TryGetComponent<UIProgressBarComponent>())
            {
                c->Value = std::clamp(value, 0.0f, 1.0f);
                return V{ { "Success", true } };
            }
            return V{ { "Success", false } };
        }
        else if (k == "SendMessage") SendMessage(target, s("Name"), s("Text"));
        else if (k == "SetUIText")
        {
            auto* c = target.TryGetComponent<TextComponent>();
            if (!c) throw std::runtime_error("Text component required");
            c->Text = s("Text");
        }
        else if (k == "SetSpriteColor")
        {
            auto* c = target.TryGetComponent<SpriteRendererComponent>();
            if (!c) throw std::runtime_error("Sprite component required");
            auto& v = a.at("Color");
            c->Color = { scalar(v[0]), scalar(v[1]), scalar(v[2]), scalar(v[3]) };
        }
        else throw std::runtime_error("Unsupported engine operation: " + k);
        return nullptr;
    }
}
