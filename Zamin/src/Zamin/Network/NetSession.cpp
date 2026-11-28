#include "Zamin/Network/NetSession.h"

#include "Zamin/Core/Input.h"
#include "Zamin/Core/Log.h"
#include "Zamin/Core/Profiler.h"
#include "Zamin/Network/NetCodec.h"
#include "Zamin/Project/Project.h"
#include "Zamin/Scene/Components.h"
#include "Zamin/Scene/PrefabCache.h"
#include "Zamin/Scene/Scene.h"
#include "Zamin/Scene/SceneSerializer.h"
#include "Zamin/Scripting/EventBus.h"
#include "Zamin/Scripting/ScriptEngine.h"
#include "Zamin/Scripting/ScriptableEntity.h"
#include "Zamin/Scripting/Visual/VisualGraphHost.h"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>

namespace Zamin::Net {

    using json = nlohmann::json;

    namespace {
        // Session messages (the first byte of a transport message).
        enum class Msg : uint8_t
        {
            Welcome = 1,   ///< server -> client, reliable: the client id, the tick rate, the inputs it had processed
            Snapshot = 2,  ///< server -> client, unreliable: entity states (delta against the acked snapshot)
            Rpc = 3,       ///< server -> client: a Client or Multicast RPC
            ClientTick = 10, ///< client -> server, unreliable: the newest snapshot it decoded and its recent inputs
            ServerRpc = 11,  ///< client -> server: a Server RPC
        };
        constexpr uint8_t RpcGraph = 1, RpcMulticast = 2;
        constexpr size_t HistorySize = 32;     // server: snapshots kept per client as delta baselines
        constexpr size_t ReceivedSize = 64;    // client: decoded snapshots kept as baselines
        constexpr size_t MaxInputsPerMessage = 16;
        constexpr size_t MaxPendingInputs = 128;
        constexpr size_t MaxFields = 64;
        constexpr uint32_t MaxMalformed = 32;
        constexpr double PendingRpcSeconds = 1.0;
        const ValueLimits FieldLimits{ 6, 1024, 256, 2048 };

        // A move input on the wire: two signed 16-bit fractions of 1.
        std::pair<int16_t, int16_t> Quantize(glm::vec2 v)
        {
            auto q = [](float f) { return int16_t(std::lround(std::clamp(f, -1.0f, 1.0f) * 32767.0f)); };
            return { q(v.x), q(v.y) };
        }
        glm::vec2 Dequantize(int16_t x, int16_t y) { return { float(x) / 32767.0f, float(y) / 32767.0f }; }

        double Seconds()
        {
            return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }

        // ---------------------------------------------------------------- script fields as values
        bool Replicable(const ScriptFieldInfo& f) { return f.IsReplicated && f.Type != ScriptFieldType::Curve && f.Type != ScriptFieldType::None; }

        json ToJson(const ScriptFieldValue& v)
        {
            auto vec = [&](int n) {
                float f[4]{};
                std::memcpy(f, v.Data, sizeof(float) * size_t(n));
                json a = json::array();
                for (int i = 0; i < n; ++i) a.push_back(double(f[i]));
                return a;
            };
            switch (v.Type)
            {
                case ScriptFieldType::Float: return double(v.Get<float>());
                case ScriptFieldType::Int: return int64_t(v.Get<int>());
                case ScriptFieldType::Bool: return v.Get<bool>();
                case ScriptFieldType::Vec2: return vec(2);
                case ScriptFieldType::Vec3: return vec(3);
                case ScriptFieldType::Vec4: return vec(4);
                case ScriptFieldType::String: return v.StringValue;
                case ScriptFieldType::Entity: return v.Get<EntityRef>().ID;
                case ScriptFieldType::Enum: return v.GetEnum();
                default: return nullptr;
            }
        }

        bool FromJson(ScriptFieldType type, const json& j, ScriptFieldValue& out)
        {
            auto number = [&](double& d) {
                if (!j.is_number()) return false;
                d = j.get<double>();
                return std::isfinite(d) && std::abs(d) <= 3.0e38;
            };
            auto vec = [&](int n) {
                if (!j.is_array() || j.size() != size_t(n)) return false;
                float f[4]{};
                for (int i = 0; i < n; ++i)
                {
                    if (!j[size_t(i)].is_number()) return false;
                    const double d = j[size_t(i)].get<double>();
                    if (!std::isfinite(d) || std::abs(d) > 3.0e38) return false;
                    f[i] = float(d);
                }
                out.Type = type;
                std::memcpy(out.Data, f, sizeof(float) * size_t(n));
                return true;
            };
            double d = 0;
            switch (type)
            {
                case ScriptFieldType::Float:
                    if (!number(d)) return false;
                    out.Set(float(d));
                    return true;
                case ScriptFieldType::Int:
                    if (!j.is_number_integer() || j.get<int64_t>() < INT32_MIN || j.get<int64_t>() > INT32_MAX) return false;
                    out.Set(int(j.get<int64_t>()));
                    return true;
                case ScriptFieldType::Bool:
                    if (!j.is_boolean()) return false;
                    out.Set(j.get<bool>());
                    return true;
                case ScriptFieldType::Vec2: return vec(2);
                case ScriptFieldType::Vec3: return vec(3);
                case ScriptFieldType::Vec4: return vec(4);
                case ScriptFieldType::String:
                    if (!j.is_string() || j.get_ref<const std::string&>().size() > 1024) return false;
                    out.Set(j.get<std::string>());
                    return true;
                case ScriptFieldType::Entity:
                    if (!j.is_number_unsigned() && !(j.is_number_integer() && j.get<int64_t>() >= 0)) return false;
                    out.Set(EntityRef{ j.get<uint64_t>() });
                    return true;
                case ScriptFieldType::Enum:
                    if (!j.is_number_integer()) return false;
                    out.SetEnum(j.get<int64_t>());
                    return true;
                default: return false;
            }
        }

        VisualGraphHost* GraphHost(Entity e)
        {
            auto* nsc = e ? e.TryGetComponent<NativeScriptComponent>() : nullptr;
            return nsc && nsc->ClassName == "Zamin.VisualGraph" ? dynamic_cast<VisualGraphHost*>(nsc->Instance) : nullptr;
        }

        // The replicated members of the entity's script: names, and (when `values`) their values now.
        std::vector<std::string> FieldNamesOf(Entity e, FieldValues* values)
        {
            std::vector<std::string> names;
            auto* nsc = e.TryGetComponent<NativeScriptComponent>();
            if (!nsc) return names;
            if (nsc->ClassName == "Zamin.VisualGraph")
            {
                auto* host = GraphHost(e);
                if (!host) return names;
                const auto& variables = host->VM.Variables();
                for (const auto& [name, variable] : host->VM.Loaded().Variables)
                    if (variable.Replicated && names.size() < MaxFields)
                    {
                        names.push_back(name);
                        if (values)
                        {
                            auto it = variables.find(name);
                            values->push_back(it == variables.end() ? json() : it->second);
                        }
                    }
                return names;
            }
            const auto* info = ScriptRegistry::Find(nsc->ClassName);
            if (!info) return names;
            for (const auto& field : info->Fields)
            {
                if (!Replicable(field) || names.size() >= MaxFields) continue;
                names.push_back(field.Name);
                if (!values) continue;
                ScriptFieldValue v;
                if (nsc->Instance) field.Load(nsc->Instance, v);
                else if (auto it = nsc->Fields.find(field.Name); it != nsc->Fields.end()) v = it->second;
                values->push_back(ToJson(v));
            }
            return names;
        }

        // Writes a replicated value into the entity's script (client); false when it cannot be written yet.
        bool WriteField(Entity e, const std::string& name, const json& value)
        {
            auto* nsc = e.TryGetComponent<NativeScriptComponent>();
            if (!nsc) return false;
            if (nsc->ClassName == "Zamin.VisualGraph")
            {
                auto* host = GraphHost(e);
                if (!host) return false;
                // (Event.Rep runs only for a value that differs from what the variable holds.)
                if (auto now = host->VM.Variables().find(name); now != host->VM.Variables().end() && now->second == value) return true;
                if (!host->VM.SetVariable(name, value)) return false;
                host->VM.Event("Rep." + name);
                return true;
            }
            const auto* info = ScriptRegistry::Find(nsc->ClassName);
            if (!info) return false;
            for (const auto& field : info->Fields)
            {
                if (field.Name != name || !Replicable(field)) continue;
                ScriptFieldValue v;
                if (!FromJson(field.Type, value, v)) return false;
                if (!nsc->Instance)
                {
                    nsc->Fields[field.Name] = v;
                    return true;
                }
                ScriptFieldValue current;
                field.Load(nsc->Instance, current);
                if (ToJson(current) == value) return true;
                if (!field.Store(nsc->Instance, v)) return false;
                if (field.RepNotify) field.RepNotify(nsc->Instance);
                return true;
            }
            return false;
        }

        bool ValidPrefabPath(const std::string& path)
        {
            if (path.empty() || path.size() > 256 || !path.ends_with(".zprefab") || path.find("..") != std::string::npos || path.front() == '/' ||
                path.find('\\') != std::string::npos)
                return false;
            return std::none_of(path.begin(), path.end(), [](char c) { return c == 0; });
        }

        // An RPC resolved on the entity's script.
        struct RpcTarget
        {
            ScriptRpc Kind = ScriptRpc::None;
            bool Reliable = true, AnyClient = false;
            const ScriptMethodInfo* Method = nullptr;     // C++
            const Visual::RpcDefinition* Graph = nullptr; // graph
            VisualGraphHost* Host = nullptr;
            ScriptableEntity* Instance = nullptr;
        };

        bool Resolve(Entity e, const std::string& name, bool graph, RpcTarget& out, std::string& error)
        {
            auto* nsc = e ? e.TryGetComponent<NativeScriptComponent>() : nullptr;
            if (!nsc)
            {
                error = "the entity has no script";
                return false;
            }
            if (graph)
            {
                auto* host = GraphHost(e);
                if (!host)
                {
                    error = "the entity has no running graph";
                    return false;
                }
                auto it = host->VM.Loaded().Rpcs.find(name);
                if (it == host->VM.Loaded().Rpcs.end())
                {
                    error = "the graph defines no RPC " + name;
                    return false;
                }
                out.Graph = &it->second;
                out.Host = host;
                out.Kind = it->second.Kind == Visual::RpcDefinition::Target::Server ? ScriptRpc::Server
                           : it->second.Kind == Visual::RpcDefinition::Target::Client ? ScriptRpc::Client
                                                                                     : ScriptRpc::Multicast;
                out.Reliable = it->second.Reliable;
                out.AnyClient = it->second.AnyClient;
                return true;
            }
            const auto* info = ScriptRegistry::Find(nsc->ClassName);
            const auto* method = info ? info->FindMethod(name) : nullptr;
            if (!method || method->Rpc == ScriptRpc::None)
            {
                error = "no RPC " + nsc->ClassName + "." + name + " (ZM_SERVER_RPC / ZM_CLIENT_RPC / ZM_MULTICAST_RPC)";
                return false;
            }
            out.Method = method;
            out.Instance = nsc->Instance;
            out.Kind = method->Rpc;
            out.Reliable = method->RpcReliable;
            out.AnyClient = method->RpcAnyClient;
            return true;
        }

        bool CheckArgs(const RpcTarget& t, const json& args, std::string& error)
        {
            if (!args.is_array())
            {
                error = "the arguments are not a list";
                return false;
            }
            if (t.Graph)
            {
                if (args.size() != t.Graph->Params.size())
                {
                    error = std::format("expected {} arguments, got {}", t.Graph->Params.size(), args.size());
                    return false;
                }
                for (size_t i = 0; i < args.size(); ++i)
                    if (!t.Host->VM.Loaded().Fits(t.Graph->Params[i], args[i]))
                    {
                        error = "argument " + t.Graph->Params[i].Name + " is not a " + Visual::TypeName(t.Graph->Params[i].Kind);
                        return false;
                    }
                return true;
            }
            if (args.size() != t.Method->Parameters.size())
            {
                error = std::format("expected {} arguments, got {}", t.Method->Parameters.size(), args.size());
                return false;
            }
            for (size_t i = 0; i < args.size(); ++i)
                if (!Session::Fits(t.Method->Parameters[i].Type, args[i]))
                {
                    error = "argument " + t.Method->Parameters[i].Name + " is not a " + ScriptValueTypeName(t.Method->Parameters[i].Type);
                    return false;
                }
            return true;
        }

        uint32_t s_RpcSender = 0;

        bool Run(const RpcTarget& t, const std::string& name, const json& args, uint32_t sender, std::string& error)
        {
            const uint32_t previous = s_RpcSender;
            s_RpcSender = sender;
            bool ok = true;
            if (t.Graph)
            {
                json payload = json::object();
                payload["Sender"] = sender;
                for (size_t i = 0; i < args.size(); ++i) payload[t.Graph->Params[i].Name] = args[i];
                t.Host->VM.Event("Rpc." + name, std::move(payload));
                if (!t.Host->VM.Error().empty())
                {
                    error = t.Host->VM.Error();
                    ok = false;
                }
            }
            else if (!t.Instance)
            {
                error = "the script has no live instance";
                ok = false;
            }
            else
            {
                try
                {
                    t.Method->Invoke(t.Instance, args.empty() ? nullptr : &args[0]);
                }
                catch (const std::exception& e)
                {
                    error = e.what();
                    ok = false;
                }
            }
            s_RpcSender = previous;
            return ok;
        }

        void Broadcast(Scene& scene, const char* name, uint32_t client)
        {
            ScriptMessage message;
            message.Name = name;
            message.Payload = client;
            scene.Broadcast(message);
        }
    }

    uint32_t RpcSender() { return s_RpcSender; }

    namespace {
        // "NETSTATE tick=T id:owner:x,y,z:{fields}" (StateLogEvery): the same text on both sides for the same state.
        template<typename World>
        std::string StateLine(uint32_t tick, const World& world)
        {
            std::string line = std::format("NETSTATE tick={}", tick);
            for (const auto& [netId, st] : world)
            {
                json fields = json::object();
                for (size_t i = 0; st->Names && i < st->Names->size() && i < st->Fields.size(); ++i) fields[(*st->Names)[i]] = st->Fields[i];
                line += std::format(" {}:{}:{:.3f},{:.3f},{:.3f}:{}", netId, st->Owner, st->Position.x, st->Position.y, st->Position.z, fields.dump());
            }
            return line;
        }
    }

    // ===================================================================== state
    struct Session::EntityState
    {
        uint32_t NetId = 0, Owner = 0;
        uint64_t SceneId = 0;  ///< a scene object's UUID (0: spawned from Prefab)
        std::string Prefab;
        std::shared_ptr<const std::vector<std::string>> Names;
        glm::vec3 Position{ 0.0f }, Rotation{ 0.0f }, Scale{ 1.0f };
        FieldValues Fields;
        bool operator==(const EntityState& o) const
        {
            return NetId == o.NetId && Owner == o.Owner && SceneId == o.SceneId && Prefab == o.Prefab && (Names == o.Names || (Names && o.Names && *Names == *o.Names)) &&
                   Position == o.Position && Rotation == o.Rotation && Scale == o.Scale && Fields == o.Fields;
        }
    };
    using StatePtr = std::shared_ptr<const Session::EntityState>;
    using WorldState = std::map<uint32_t, StatePtr>;

    struct Session::Impl
    {
        std::unique_ptr<ServerTransport> Server;
        std::unique_ptr<ClientTransport> Client;
        uint32_t Tick = 0;
        double Accumulator = 0;
        std::map<uint32_t, UUID> Entities; // network id -> the entity here
        SessionStats Stats;

        // Server.
        struct Remote
        {
            uint32_t Id = 0;
            bool Connected = true;
            bool HasAck = false;
            uint32_t AckedTick = 0;
            std::deque<std::pair<uint32_t, WorldState>> History;
            std::map<uint32_t, glm::vec2> Inputs;
            uint32_t LastInput = 0;
            double RpcTokens = 0, RpcRefill = 0;
            uint64_t RpcsRejected = 0, InputsRejected = 0;
            uint32_t Malformed = 0;
            size_t Rotate = 0, Relevant = 0;
        };
        std::map<uint32_t, Remote> Clients;
        WorldState Current;
        uint32_t NextNetId = 1;
        bool Ticked = false;
        std::set<uint64_t> DestroyedSceneObjects;
        std::set<UUID> Unreplicable; // warned about once
        double TickWork = 0;
        uint64_t TickCount = 0;

        // Client.
        std::deque<std::pair<uint32_t, WorldState>> Received;
        uint32_t AppliedTick = 0, NewestTick = 0;
        bool HaveApplied = false;
        double ServerOffset = 0;
        bool HaveOffset = false;
        uint32_t TickRate = 30;
        struct Sample
        {
            double Time;
            glm::vec3 Position, Rotation, Scale;
        };
        struct Local
        {
            UUID Uuid = 0;
            std::deque<Sample> Samples;
            FieldValues Applied;
            std::vector<bool> Written;
            bool SceneObject = false, Hidden = false; // a scene object out of this client's interest is hidden, not destroyed
        };
        std::set<uint32_t> Gone; // removed because the server destroyed them (not just out of interest)
        std::map<uint32_t, Local> Locals;
        struct Input
        {
            uint32_t Seq;
            glm::vec2 Move;
        };
        std::deque<Input> Pending;
        uint32_t InputSeq = 0;
        bool SendTick = false;
        struct QueuedRpc
        {
            uint32_t NetId;
            uint8_t Flags;
            std::string Name;
            json Args;
            double Until;
        };
        std::deque<QueuedRpc> QueuedRpcs;
        double ReconnectUntil = 0;
        bool Welcomed = false;
    };

    // ===================================================================== helpers
    Session::Session() : m_Impl(std::make_unique<Impl>()) {}
    Session::~Session() { Stop(); }

    Session* Session::Of(const Scene& scene)
    {
        auto& registry = const_cast<Scene&>(scene).GetRegistry();
        auto* link = registry.ctx().find<SceneLink>();
        return link ? link->Owner : nullptr;
    }

    bool IsServer(const Scene& scene)
    {
        auto* s = Session::Of(scene);
        return !s || s->IsServer();
    }
    bool IsClient(const Scene& scene)
    {
        auto* s = Session::Of(scene);
        return s && s->IsClient();
    }
    bool IsOwner(Entity entity)
    {
        auto* s = entity ? Session::Of(*entity.GetScene()) : nullptr;
        return !s || s->IsOwner(entity);
    }

    bool Call(Entity entity, const std::string& name, const json& args, bool graph, std::string* error)
    {
        if (auto* s = entity ? Session::Of(*entity.GetScene()) : nullptr) return s->Call(entity, name, args, graph, error);
        RpcTarget t;
        std::string why;
        if (Resolve(entity, name, graph, t, why) && CheckArgs(t, args, why) && Run(t, name, args, 0, why)) return true;
        if (error) *error = why;
        return false;
    }

    void SceneBeforeUpdate(Scene& scene, float dt)
    {
        if (auto* s = Session::Of(scene)) s->BeforeUpdate(dt);
    }
    void SceneAfterUpdate(Scene& scene, float dt)
    {
        if (auto* s = Session::Of(scene)) s->AfterUpdate(dt);
    }
    void SceneDestroyed(Scene& scene)
    {
        if (auto* s = Session::Of(scene)) s->OnSceneDestroyed();
    }

    void Session::Log(std::string text)
    {
        ZM_CORE_INFO("[Network] {}", text);
        if (m_LogSink) m_LogSink(text);
        m_Log.push_back({ Now(), std::move(text) });
        while (m_Log.size() > 200) m_Log.pop_front();
    }

    bool Session::Attach(Scene& scene, std::string* error)
    {
        if (Session::Of(scene) && Session::Of(scene) != this)
        {
            if (error) *error = "the scene already has a network session";
            return false;
        }
        if (!scene.IsRunning())
        {
            if (error) *error = "the scene is not running (OnRuntimeStart)";
            return false;
        }
        m_Scene = &scene;
        scene.GetRegistry().ctx().insert_or_assign(SceneLink{ this });
        return true;
    }

    void Session::Detach()
    {
        if (m_Scene)
        {
            auto& ctx = m_Scene->GetRegistry().ctx();
            if (auto* link = ctx.find<SceneLink>(); link && link->Owner == this) ctx.erase<SceneLink>();
        }
        m_Scene = nullptr;
    }

    void Session::OnSceneDestroyed()
    {
        m_Scene = nullptr;
        Stop();
    }

    uint64_t Session::GetResumeToken() const { return m_Impl->Client ? m_Impl->Client->GetResumeToken() : 0; }

    Address Session::GetLocalAddress() const
    {
        if (m_Impl->Server) return m_Impl->Server->GetLocalAddress();
        if (m_Impl->Client && m_Impl->Client->GetSocket()) return m_Impl->Client->GetSocket()->GetLocalAddress();
        return {};
    }

    bool Session::IsConnected() const
    {
        if (m_Role == Role::Server) return m_Impl->Server && m_Impl->Server->IsRunning();
        return m_Role == Role::Client && m_Impl->Client && m_Impl->Client->IsConnected();
    }

    glm::vec3 Session::Move(const NetworkTransformComponent& t, glm::vec3 p, glm::vec2 input, float dt)
    {
        if (!std::isfinite(input.x) || !std::isfinite(input.y)) input = glm::vec2(0.0f);
        if (glm::length(input) > 1.0f) input = glm::normalize(input);
        const glm::vec2 step = input * t.MoveSpeed * dt;
        glm::vec2 plane = t.Plane == NetworkTransformComponent::MovePlane::XZ ? glm::vec2(p.x, p.z) : glm::vec2(p.x, p.y);
        plane += t.Plane == NetworkTransformComponent::MovePlane::XZ ? glm::vec2(step.x, -step.y) : step;
        if (t.ClampToBounds) plane = glm::clamp(plane, glm::min(t.BoundsMin, t.BoundsMax), glm::max(t.BoundsMin, t.BoundsMax));
        if (t.Plane == NetworkTransformComponent::MovePlane::XZ)
        {
            p.x = plane.x;
            p.z = plane.y;
        }
        else
        {
            p.x = plane.x;
            p.y = plane.y;
        }
        return p;
    }

    bool Session::Fits(const ScriptValueType& type, const json& v)
    {
        if (type.Array)
        {
            if (!v.is_array() || v.size() > 1024) return false;
            ScriptValueType element = type;
            element.Array = false;
            return std::all_of(v.begin(), v.end(), [&](const json& item) { return Fits(element, item); });
        }
        auto finite = [](const json& n) { return n.is_number() && std::isfinite(n.get<double>()) && std::abs(n.get<double>()) <= 3.0e38; };
        auto vector = [&](size_t n) { return v.is_array() && v.size() == n && std::all_of(v.begin(), v.end(), finite); };
        switch (type.Kind)
        {
            case ScriptValueKind::Void: return v.is_null();
            case ScriptValueKind::Bool: return v.is_boolean();
            case ScriptValueKind::Number:
                if (!finite(v)) return false;
                return !type.Integer || (std::floor(v.get<double>()) == v.get<double>() && std::abs(v.get<double>()) <= 9007199254740992.0);
            case ScriptValueKind::String: return v.is_string() && v.get_ref<const std::string&>().size() <= 65536;
            case ScriptValueKind::Vec2: return vector(2);
            case ScriptValueKind::Vec3: return vector(3);
            case ScriptValueKind::Vec4: return vector(4);
            case ScriptValueKind::Entity: return v.is_number_unsigned() || (v.is_number_integer() && v.get<int64_t>() >= 0);
            case ScriptValueKind::Enum:
            {
                if (!v.is_number_integer()) return false;
                const auto* info = ScriptRegistry::FindEnum(type.Named);
                return !info || std::any_of(info->Options.begin(), info->Options.end(), [&](const ScriptEnumOption& o) { return o.Value == v.get<int64_t>(); });
            }
            case ScriptValueKind::Struct:
            {
                const auto* info = ScriptRegistry::FindStruct(type.Named);
                if (!info || !v.is_object() || v.size() != info->Fields.size()) return false;
                for (const auto& field : info->Fields)
                {
                    auto it = v.find(field.Name);
                    if (it == v.end() || !Fits(field.Type, *it)) return false;
                }
                return true;
            }
        }
        return false;
    }

    // ===================================================================== start / stop
    bool Session::StartServer(Scene& scene, std::unique_ptr<Socket> socket, const SessionSettings& settings, std::string* error)
    {
        Stop();
        if (!socket)
        {
            if (error) *error = "no socket";
            return false;
        }
        m_Settings = settings;
        m_Settings.TickRate = std::clamp(m_Settings.TickRate, 1u, 120u);
        m_Settings.Transport.ProtocolVersion = uint16_t(m_Settings.Transport.ProtocolVersion * 31u + SessionProtocol);
        if (!Attach(scene, error)) return false;
        m_Conditioner = dynamic_cast<LinkConditioner*>(socket.get());
        *m_Impl = Impl();
        m_Impl->Server = std::make_unique<ServerTransport>();
        const Address local = socket->GetLocalAddress();
        m_Impl->Server->Start(std::move(socket), m_Settings.Transport);
        m_Impl->TickRate = m_Settings.TickRate;
        m_Role = Role::Server;
        m_LocalClient = 0;
        // The scene's own networked entities: both sides have them (matched by UUID).
        auto& registry = scene.GetRegistry();
        std::vector<std::pair<UUID, entt::entity>> found;
        for (auto e : registry.view<NetworkIdentityComponent>()) found.push_back({ registry.get<IDComponent>(e).ID, e });
        std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return uint64_t(a.first) < uint64_t(b.first); });
        for (auto& [uuid, e] : found)
        {
            auto& id = registry.get<NetworkIdentityComponent>(e);
            id.NetId = m_Impl->NextNetId++;
            id.Owner = 0;
            id.SceneObject = true;
            m_Impl->Entities[id.NetId] = uuid;
        }
        Log(std::format("Server started on port {} ({} Hz, up to {} clients, {} scene objects{})", local.Port, m_Settings.TickRate, m_Settings.Transport.MaxClients,
                        found.size(), m_Settings.Host ? ", host plays as client 0" : ""));
        if (m_Settings.Host) Broadcast(scene, "Net.ClientConnected", 0);
        return true;
    }

    bool Session::StartClient(Scene& scene, std::unique_ptr<Socket> socket, const Address& server, const SessionSettings& settings, uint32_t resumeId,
                              uint64_t resumeToken, std::string* error)
    {
        Stop();
        if (!socket || !server.IsValid())
        {
            if (error) *error = "no socket or server address";
            return false;
        }
        m_Settings = settings;
        m_Settings.Host = false;
        m_Settings.Transport.ProtocolVersion = uint16_t(m_Settings.Transport.ProtocolVersion * 31u + SessionProtocol);
        if (!Attach(scene, error)) return false;
        m_Conditioner = dynamic_cast<LinkConditioner*>(socket.get());
        *m_Impl = Impl();
        m_Impl->Client = std::make_unique<ClientTransport>();
        m_Impl->Client->Connect(std::move(socket), server, m_Settings.Transport, resumeId, resumeToken);
        m_Role = Role::Client;
        m_LocalClient = 0;
        m_Reconnecting = false;
        auto& registry = scene.GetRegistry();
        for (auto e : registry.view<NetworkIdentityComponent>())
        {
            auto& id = registry.get<NetworkIdentityComponent>(e);
            id.SceneObject = true;
            id.NetId = 0; // the server's state names it
        }
        Log(std::format("Connecting to {}{}", server.ToString(), resumeId ? std::format(" (resuming client {})", resumeId) : std::string()));
        return true;
    }

    void Session::Stop()
    {
        if (m_Role == Role::None && !m_Scene) return;
        if (m_Impl->Server)
        {
            m_Impl->Server->Stop();
            Log("Server stopped");
        }
        if (m_Impl->Client)
        {
            m_Impl->Client->Disconnect();
            Log("Disconnected");
        }
        m_Impl->Server.reset();
        m_Impl->Client.reset();
        m_Conditioner = nullptr;
        m_InputSource = {}; // (it may be a game module's lambda: the module may be unloaded next)
        m_Role = Role::None;
        Detach();
    }

    // ===================================================================== entities
    Entity Session::FindEntity(uint32_t netId) const
    {
        if (!m_Scene || !netId) return {};
        if (m_Role == Role::Client)
        {
            auto it = m_Impl->Locals.find(netId);
            return it == m_Impl->Locals.end() || it->second.Hidden ? Entity() : m_Scene->GetEntityByUUID(it->second.Uuid);
        }
        auto it = m_Impl->Entities.find(netId);
        return it == m_Impl->Entities.end() ? Entity() : m_Scene->GetEntityByUUID(it->second);
    }

    Entity Session::OwnedEntity(uint32_t client) const
    {
        if (!m_Scene) return {};
        Entity best;
        uint32_t bestId = UINT32_MAX;
        auto& registry = m_Scene->GetRegistry();
        for (auto e : registry.view<NetworkIdentityComponent>())
        {
            const auto& id = registry.get<NetworkIdentityComponent>(e);
            if (id.NetId && id.Owner == client && id.NetId < bestId && (client != 0 || !id.SceneObject))
            {
                bestId = id.NetId;
                best = { e, m_Scene };
            }
        }
        return best;
    }

    uint32_t Session::OwnerOf(Entity e) const
    {
        auto* id = e ? e.TryGetComponent<NetworkIdentityComponent>() : nullptr;
        return id ? id->Owner : 0;
    }

    bool Session::IsOwner(Entity e) const
    {
        auto* id = e ? e.TryGetComponent<NetworkIdentityComponent>() : nullptr;
        if (m_Role != Role::Client) return !id || id->Owner == 0;
        return id && id->NetId && id->Owner != 0 && id->Owner == m_LocalClient;
    }

    namespace {
        Entity Instantiate(Scene& scene, const std::string& prefab, const glm::vec3& position, uint32_t netId, uint32_t owner)
        {
            SceneSerializer::EntitySetReadOptions options;
            options.KeepPrefabLinks = false;
            std::vector<Entity> entities = PrefabCache::Instantiate(scene, Project::GetAssetFileSystemPath(prefab), options);
            if (entities.empty()) return {};
            Entity root = entities.front();
            root.GetComponent<TransformComponent>().Translation = position;
            auto& id = root.HasComponent<NetworkIdentityComponent>() ? root.GetComponent<NetworkIdentityComponent>() : root.AddComponent<NetworkIdentityComponent>();
            id.NetId = netId;
            id.Owner = owner;
            id.Prefab = prefab;
            id.SceneObject = false;
            scene.ResolveAssets();
            scene.OnEntitiesSpawned(entities);
            return root;
        }
    }

    Entity Session::Spawn(const std::string& prefab, const glm::vec3& position, uint32_t owner)
    {
        if (m_Role != Role::Server || !m_Scene) return {};
        if (!ValidPrefabPath(prefab))
        {
            Log("Spawn: not a prefab path inside the assets: " + prefab);
            return {};
        }
        const uint32_t netId = m_Impl->NextNetId++;
        Entity e = Instantiate(*m_Scene, prefab, position, netId, owner);
        if (!e)
        {
            Log("Spawn: the prefab could not be read: " + prefab);
            return {};
        }
        m_Impl->Entities[netId] = e.GetUUID();
        return e;
    }

    bool Session::Despawn(Entity e)
    {
        if (m_Role != Role::Server || !m_Scene || !e) return false;
        auto* id = e.TryGetComponent<NetworkIdentityComponent>();
        if (!id || !id->NetId) return false;
        m_Scene->DestroyEntityDeferred(e);
        return true;
    }

    // ===================================================================== RPCs
    bool Session::Call(Entity entity, const std::string& name, const json& args, bool graph, std::string* error)
    {
        std::string why;
        auto fail = [&](std::string text) {
            if (error) *error = text;
            return false;
        };
        RpcTarget t;
        if (!Resolve(entity, name, graph, t, why) || !CheckArgs(t, args, why)) return fail(why);
        auto* id = entity.TryGetComponent<NetworkIdentityComponent>();
        const uint32_t netId = id ? id->NetId : 0;
        auto encode = [&](Msg type, uint8_t flags, std::vector<uint8_t>& out) {
            Writer w;
            w.U8(uint8_t(type));
            w.VarU(netId);
            w.U8(flags);
            w.String(name);
            std::string e;
            if (!EncodeValue(w, args, &e)) return fail("the arguments cannot be sent: " + e);
            out = std::move(w.Data());
            return true;
        };
        if (t.Kind == ScriptRpc::Server)
        {
            if (m_Role != Role::Client) return Run(t, name, args, m_LocalClient, why) || fail(why);
            if (!netId) return fail("the entity is not networked yet");
            if (!t.AnyClient && !IsOwner(entity)) return fail("a client calls server RPCs on the entities it owns only");
            std::vector<uint8_t> bytes;
            if (!encode(Msg::ServerRpc, graph ? RpcGraph : 0, bytes)) return false;
            if (!m_Impl->Client->Send(bytes, t.Reliable)) return fail("not connected");
            ++m_Impl->Stats.RpcsSent;
            return true;
        }
        if (m_Role == Role::Client) return fail(std::string(t.Kind == ScriptRpc::Client ? "Client" : "Multicast") + " RPCs are called by the server");
        if (m_Role == Role::None) return Run(t, name, args, 0, why) || fail(why);
        if (!netId) return fail("the entity is not networked yet");
        std::vector<uint8_t> bytes;
        if (!encode(Msg::Rpc, uint8_t((graph ? RpcGraph : 0) | (t.Kind == ScriptRpc::Multicast ? RpcMulticast : 0)), bytes)) return false;
        if (t.Kind == ScriptRpc::Client)
        {
            const uint32_t owner = id->Owner;
            if (owner == 0) return Run(t, name, args, 0, why) || fail(why); // the server's own (the host's player)
            if (!m_Impl->Server->Send(owner, bytes, t.Reliable)) return fail("the owner is not connected");
            ++m_Impl->Stats.RpcsSent;
            return true;
        }
        // Multicast: here, then every client that has (or is about to get) the entity.
        bool ok = Run(t, name, args, 0, why);
        for (auto& [client, remote] : m_Impl->Clients)
        {
            if (!remote.Connected) continue;
            const bool relevant = remote.History.empty() || remote.History.back().second.contains(netId);
            if (relevant && m_Impl->Server->Send(client, bytes, t.Reliable)) ++m_Impl->Stats.RpcsSent;
        }
        return ok || fail(why);
    }

    // ===================================================================== update
    void Session::BeforeUpdate(float dt)
    {
        if (!m_Scene || m_Role == Role::None) return;
        ZM_PROFILE_SCOPE("Network");
        Impl& s = *m_Impl;
        const double now = Now();
        const double tickDt = 1.0 / double(s.TickRate);
        if (!std::isfinite(dt) || dt < 0) dt = 0;
        auto& registry = m_Scene->GetRegistry();

        if (m_Role == Role::Server)
        {
            const double start = Seconds();
            s.Server->Update(now);
            TransportEvent e;
            while (s.Server->Poll(e))
            {
                if (e.Type == TransportEvent::Kind::Connected)
                {
                    auto [it, fresh] = s.Clients.try_emplace(e.Client);
                    auto& r = it->second;
                    r.Id = e.Client;
                    r.Connected = true;
                    r.HasAck = false;
                    r.History.clear();
                    r.Inputs.clear();
                    r.RpcTokens = m_Settings.MaxRpcsPerSecond;
                    r.RpcRefill = now;
                    Writer w;
                    w.U8(uint8_t(Msg::Welcome));
                    w.VarU(e.Client);
                    w.VarU(s.TickRate);
                    w.VarU(r.LastInput);
                    w.VarU(s.DestroyedSceneObjects.size());
                    for (uint64_t uuid : s.DestroyedSceneObjects) w.U64(uuid);
                    s.Server->Send(e.Client, w.Data(), true);
                    Log(std::format("Client {} {}", e.Client, fresh ? "connected" : "resumed its session"));
                    if (fresh) Broadcast(*m_Scene, "Net.ClientConnected", e.Client);
                }
                else if (e.Type == TransportEvent::Kind::Disconnected || e.Type == TransportEvent::Kind::Expired)
                {
                    auto it = s.Clients.find(e.Client);
                    if (it == s.Clients.end()) continue;
                    if (e.Type == TransportEvent::Kind::Disconnected && e.Resumable)
                    {
                        it->second.Connected = false;
                        Log(std::format("Client {} lost ({}); it may resume for {:.0f} s", e.Client, DisconnectReasonName(e.Reason), m_Settings.Transport.ResumeSeconds));
                        continue;
                    }
                    Log(std::format("Client {} left ({})", e.Client, e.Type == TransportEvent::Kind::Expired ? "did not come back" : DisconnectReasonName(e.Reason)));
                    s.Clients.erase(it);
                    if (m_Settings.DespawnOwnedOnLeave)
                        for (auto ent : registry.view<NetworkIdentityComponent>())
                            if (auto& id = registry.get<NetworkIdentityComponent>(ent); id.Owner == e.Client && id.NetId) m_Scene->DestroyEntityDeferred({ ent, m_Scene });
                    Broadcast(*m_Scene, "Net.ClientDisconnected", e.Client);
                }
                else if (e.Type == TransportEvent::Kind::Message)
                {
                    auto it = s.Clients.find(e.Client);
                    if (it == s.Clients.end()) continue;
                    auto& r = it->second;
                    Reader in(e.Payload.Data);
                    const uint8_t type = in.U8();
                    bool bad = !in.Ok();
                    if (type == uint8_t(Msg::ClientTick))
                    {
                        const uint32_t ack = uint32_t(in.VarU(32));
                        const size_t count = in.Count(MaxInputsPerMessage);
                        const uint32_t first = count ? uint32_t(in.VarU(32)) : 0;
                        std::vector<std::pair<uint32_t, glm::vec2>> inputs;
                        for (size_t i = 0; i < count && in.Ok(); ++i)
                        {
                            const int16_t x = int16_t(in.U16()), y = int16_t(in.U16());
                            inputs.push_back({ first + uint32_t(i), Dequantize(x, y) });
                        }
                        bad = bad || !in.AtEnd();
                        if (!bad)
                        {
                            if (ack && ack <= s.Tick && (!r.HasAck || ack > r.AckedTick) &&
                                std::any_of(r.History.begin(), r.History.end(), [&](const auto& h) { return h.first == ack; }))
                            {
                                r.AckedTick = ack;
                                r.HasAck = true;
                            }
                            for (auto& [seq, v] : inputs)
                            {
                                if (glm::length(v) > 1.001f)
                                {
                                    ++r.InputsRejected;
                                    ++s.Stats.InputsRejected;
                                    continue;
                                }
                                if (seq <= r.LastInput || seq > r.LastInput + MaxPendingInputs) continue;
                                if (r.Inputs.emplace(seq, v).second) ++s.Stats.InputsReceived;
                            }
                            while (r.Inputs.size() > 64) r.Inputs.erase(r.Inputs.begin());
                        }
                    }
                    else if (type == uint8_t(Msg::ServerRpc))
                    {
                        const uint32_t netId = uint32_t(in.VarU(32));
                        const uint8_t flags = in.U8();
                        const std::string name = in.String(64);
                        json args;
                        bad = bad || !in.Ok() || (flags & ~RpcGraph) || !DecodeValue(in, args) || !in.AtEnd() || !args.is_array();
                        if (!bad)
                        {
                            // One second of burst at the per-client rate.
                            r.RpcTokens = std::min(m_Settings.MaxRpcsPerSecond, r.RpcTokens + (now - r.RpcRefill) * m_Settings.MaxRpcsPerSecond);
                            r.RpcRefill = now;
                            std::string why;
                            RpcTarget t;
                            Entity target = FindEntity(netId);
                            auto* id = target ? target.TryGetComponent<NetworkIdentityComponent>() : nullptr;
                            if (r.RpcTokens < 1.0) why = "rate limit";
                            else if (!id) why = "no such entity";
                            else if (!Resolve(target, name, flags & RpcGraph, t, why)) {}
                            else if (t.Kind != ScriptRpc::Server) why = "not a server RPC";
                            else if (!t.AnyClient && id->Owner != e.Client) why = "the client does not own the entity";
                            else if (!CheckArgs(t, args, why)) {}
                            if (r.RpcTokens >= 1.0) r.RpcTokens -= 1.0;
                            if (why.empty())
                            {
                                ++s.Stats.RpcsReceived;
                                if (!Run(t, name, args, e.Client, why)) Log(std::format("RPC {} from client {} failed: {}", name, e.Client, why));
                            }
                            else
                            {
                                ++s.Stats.RpcsRejected;
                                if (++r.RpcsRejected <= 8) Log(std::format("RPC {} from client {} rejected: {}", name, e.Client, why));
                            }
                        }
                    }
                    else bad = true;
                    if (bad)
                    {
                        ++s.Stats.MalformedMessages;
                        if (++r.Malformed > MaxMalformed)
                        {
                            Log(std::format("Client {} kicked: malformed messages", e.Client));
                            s.Server->Kick(e.Client, DisconnectReason::Malformed);
                        }
                    }
                }
            }
            // Fixed ticks: the clients' inputs move the entities they own.
            s.Accumulator = std::min(s.Accumulator + dt, tickDt * 5);
            while (s.Accumulator >= tickDt - 1e-9)
            {
                s.Accumulator -= tickDt;
                ++s.Tick;
                s.Ticked = true;
                std::map<uint32_t, std::vector<entt::entity>> predicted;
                for (auto ent : registry.view<NetworkIdentityComponent, NetworkTransformComponent>())
                    if (registry.get<NetworkTransformComponent>(ent).Mode == NetworkTransformComponent::SyncMode::Predict && registry.get<NetworkIdentityComponent>(ent).NetId)
                        predicted[registry.get<NetworkIdentityComponent>(ent).Owner].push_back(ent);
                auto move = [&](const std::vector<entt::entity>& list, glm::vec2 input) {
                    for (auto ent : list)
                    {
                        auto& t = registry.get<TransformComponent>(ent);
                        t.Translation = Move(registry.get<NetworkTransformComponent>(ent), t.Translation, input, float(tickDt));
                    }
                };
                for (auto& [client, r] : s.Clients)
                {
                    auto list = predicted.find(client);
                    if (r.Inputs.empty())
                    {
                        if (list != predicted.end() && r.Connected) ++s.Stats.InputsMissed;
                        continue;
                    }
                    // Exactly one input a tick (a client cannot move faster by sending more); a backlog is cut short.
                    while (r.Inputs.size() > 6) r.Inputs.erase(r.Inputs.begin());
                    auto next = r.Inputs.begin();
                    r.LastInput = next->first;
                    const glm::vec2 input = next->second;
                    r.Inputs.erase(next);
                    if (list != predicted.end()) move(list->second, input);
                }
                if (m_Settings.Host)
                    if (auto list = predicted.find(0); list != predicted.end() && !list->second.empty())
                    {
                        Entity first{ list->second.front(), m_Scene };
                        const auto& nt = first.GetComponent<NetworkTransformComponent>();
                        move(list->second, m_InputSource ? m_InputSource(first) : Input::GetVector(nt.MoveAction.empty() ? m_Settings.MoveAction : nt.MoveAction));
                    }
            }
            s.TickWork += Seconds() - start;
            return;
        }

        // ---- client
        s.Client->Update(now);
        TransportEvent e;
        while (s.Client->Poll(e))
        {
            if (e.Type == TransportEvent::Kind::Connected)
            {
                m_LocalClient = e.Client;
                m_Reconnecting = false;
                s.Received.clear(); // the server starts this connection without baselines
                s.Welcomed = false;
                Log(std::format("Connected to {} as client {}{}", s.Client->GetServer().ToString(), e.Client, e.Resumed ? " (session resumed)" : ""));
            }
            else if (e.Type == TransportEvent::Kind::Disconnected)
            {
                m_LastReason = e.Reason;
                const bool retry = e.Reason == DisconnectReason::Timeout || (m_Reconnecting && e.Reason == DisconnectReason::HandshakeFailed);
                if (retry && !m_Reconnecting) s.ReconnectUntil = now + m_Settings.ReconnectSeconds;
                if (retry && now < s.ReconnectUntil && s.Client->Reconnect())
                {
                    if (!m_Reconnecting) Log(std::format("Connection lost ({}); reconnecting", DisconnectReasonName(e.Reason)));
                    m_Reconnecting = true;
                }
                else
                {
                    m_Reconnecting = false;
                    Log(std::format("Disconnected: {}", DisconnectReasonName(e.Reason)));
                }
            }
            else if (e.Type == TransportEvent::Kind::Message)
            {
                Reader in(e.Payload.Data);
                const uint8_t type = in.U8();
                if (type == uint8_t(Msg::Welcome))
                {
                    const uint32_t id = uint32_t(in.VarU(32));
                    const uint32_t rate = uint32_t(in.VarU(8));
                    const uint32_t lastInput = uint32_t(in.VarU(32));
                    const size_t destroyed = in.Count(65536);
                    std::vector<uint64_t> gone;
                    for (size_t i = 0; i < destroyed && in.Ok(); ++i) gone.push_back(in.U64());
                    if (!in.AtEnd() || rate == 0 || rate > 120 || id != m_LocalClient)
                    {
                        ++s.Stats.MalformedMessages;
                        continue;
                    }
                    s.TickRate = rate;
                    s.InputSeq = std::max(s.InputSeq, lastInput);
                    s.Welcomed = true;
                    for (uint64_t uuid : gone)
                        if (Entity ent = m_Scene->GetEntityByUUID(UUID(uuid)); ent && ent.HasComponent<NetworkIdentityComponent>()) m_Scene->DestroyEntityDeferred(ent);
                }
                else if (type == uint8_t(Msg::Snapshot))
                {
                    const uint32_t tick = uint32_t(in.VarU(32)), base = uint32_t(in.VarU(32)), lastInput = uint32_t(in.VarU(32));
                    const WorldState* baseline = nullptr;
                    static const WorldState empty;
                    bool drop = !in.Ok();
                    if (!drop && base)
                    {
                        auto found = std::find_if(s.Received.begin(), s.Received.end(), [&](const auto& r) { return r.first == base; });
                        if (found == s.Received.end()) drop = true;
                        else baseline = &found->second;
                    }
                    if (!drop && std::any_of(s.Received.begin(), s.Received.end(), [&](const auto& r) { return r.first == tick; })) drop = true;
                    if (drop || base >= tick)
                    {
                        ++s.Stats.SnapshotsDropped;
                        continue;
                    }
                    WorldState state = baseline ? *baseline : empty;
                    const size_t removed = in.Count(65536);
                    for (size_t i = 0; i < removed && in.Ok(); ++i)
                    {
                        const uint64_t code = in.VarU(33); // network id << 1 | destroyed on the server
                        if (code & 1) s.Gone.insert(uint32_t(code >> 1));
                        state.erase(uint32_t(code >> 1));
                    }
                    const size_t entries = in.Count(65536);
                    for (size_t i = 0; i < entries && in.Ok(); ++i)
                    {
                        const uint32_t netId = uint32_t(in.VarU(32));
                        const uint8_t flags = in.U8();
                        auto existing = state.find(netId);
                        if (!netId || (flags & ~63) || (!(flags & 32) && existing == state.end()))
                        {
                            in.Fail();
                            break;
                        }
                        auto next = std::make_shared<EntityState>(existing != state.end() && !(flags & 32) ? *existing->second : EntityState());
                        next->NetId = netId;
                        if (flags & 32)
                        {
                            const uint8_t kind = in.U8();
                            if (kind == 0) next->SceneId = in.U64();
                            else if (kind == 1)
                            {
                                next->Prefab = in.String(256);
                                if (!ValidPrefabPath(next->Prefab)) in.Fail();
                            }
                            else in.Fail();
                            auto names = std::make_shared<std::vector<std::string>>(in.Count(MaxFields));
                            for (auto& n : *names) n = in.String(64);
                            next->Names = names;
                            next->Fields.assign(names->size(), json());
                        }
                        if (flags & 16) next->Owner = uint32_t(in.VarU(32));
                        if (flags & 1) next->Position = { in.F32(), in.F32(), in.F32() };
                        if (flags & 2) next->Rotation = { in.F32(), in.F32(), in.F32() };
                        if (flags & 4) next->Scale = { in.F32(), in.F32(), in.F32() };
                        if (flags & 8)
                        {
                            const size_t changed = in.Count(MaxFields);
                            for (size_t f = 0; f < changed && in.Ok(); ++f)
                            {
                                const size_t index = size_t(in.VarU(16));
                                json value;
                                if (index >= next->Fields.size() || !DecodeValue(in, value, FieldLimits)) in.Fail();
                                else next->Fields[index] = std::move(value);
                            }
                        }
                        if (in.Ok()) state[netId] = std::move(next);
                    }
                    if (!in.AtEnd())
                    {
                        ++s.Stats.MalformedMessages;
                        ++s.Stats.SnapshotsDropped;
                        continue;
                    }
                    ++s.Stats.SnapshotsReceived;
                    s.Stats.SnapshotBytes += e.Payload.Data.size();
                    s.NewestTick = std::max(s.NewestTick, tick);
                    // The server's clock: our time minus the snapshot's tick time, smoothed.
                    const double sample = now - double(tick) / double(s.TickRate);
                    if (!s.HaveOffset || std::abs(sample - s.ServerOffset) > 0.5) s.ServerOffset = sample;
                    else s.ServerOffset += (sample - s.ServerOffset) * (sample < s.ServerOffset ? 0.2 : 0.02);
                    s.HaveOffset = true;
                    // Interpolation samples of everything in it.
                    for (const auto& [netId, st] : state)
                    {
                        auto& local = s.Locals[netId];
                        const double time = double(tick) / double(s.TickRate);
                        if (!local.Samples.empty() && local.Samples.back().Time >= time)
                        {
                            // An older snapshot (reordered): slot it in.
                            auto at = std::find_if(local.Samples.begin(), local.Samples.end(), [&](const Impl::Sample& x) { return x.Time >= time; });
                            if (at != local.Samples.end() && at->Time != time) local.Samples.insert(at, { time, st->Position, st->Rotation, st->Scale });
                        }
                        else local.Samples.push_back({ time, st->Position, st->Rotation, st->Scale });
                        while (local.Samples.size() > 32) local.Samples.pop_front();
                    }
                    if (m_Settings.StateLogEvery && tick % m_Settings.StateLogEvery == 0) ZM_CORE_INFO("{}", StateLine(tick, state));
                    s.Received.push_back({ tick, std::move(state) });
                    std::sort(s.Received.begin(), s.Received.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
                    while (s.Received.size() > ReceivedSize) s.Received.pop_front();
                    if (s.HaveApplied && tick <= s.AppliedTick) continue;
                    s.HaveApplied = true;
                    s.AppliedTick = tick;
                    s.Stats.Tick = tick;
                    const WorldState& applied = std::find_if(s.Received.begin(), s.Received.end(), [&](const auto& r) { return r.first == tick; })->second;
                    // Spawn / update / despawn to match the server's state.
                    for (const auto& [netId, st] : applied)
                    {
                        auto& local = s.Locals[netId];
                        Entity ent = local.Uuid ? m_Scene->GetEntityByUUID(local.Uuid) : Entity();
                        if (!ent)
                        {
                            if (st->SceneId)
                            {
                                ent = m_Scene->GetEntityByUUID(UUID(st->SceneId));
                                if (ent && !ent.HasComponent<NetworkIdentityComponent>()) ent = {};
                            }
                            else ent = Instantiate(*m_Scene, st->Prefab, st->Position, netId, st->Owner);
                            if (!ent)
                            {
                                local.Uuid = 0;
                                continue;
                            }
                            local.Uuid = ent.GetUUID();
                            local.SceneObject = st->SceneId != 0;
                            local.Applied.assign(st->Fields.size(), json());
                            local.Written.assign(st->Fields.size(), false);
                            auto& t = ent.GetComponent<TransformComponent>();
                            t.Translation = st->Position;
                            t.Rotation = st->Rotation;
                            t.Scale = st->Scale;
                        }
                        if (local.Hidden)
                        {
                            ent.SetActive(true); // back in this client's interest
                            local.Hidden = false;
                        }
                        auto& id = ent.GetComponent<NetworkIdentityComponent>();
                        id.NetId = netId;
                        id.Owner = st->Owner;
                        auto* nt = ent.TryGetComponent<NetworkTransformComponent>();
                        if (nt && nt->Mode == NetworkTransformComponent::SyncMode::Predict && st->Owner == m_LocalClient && m_LocalClient)
                        {
                            // Reconciliation: the server's position after the inputs it processed, then ours it has not.
                            while (!s.Pending.empty() && s.Pending.front().Seq <= lastInput) s.Pending.pop_front();
                            glm::vec3 p = st->Position;
                            for (const auto& input : s.Pending) p = Move(*nt, p, input.Move, float(tickDt));
                            auto& t = ent.GetComponent<TransformComponent>();
                            const double error = glm::length(p - t.Translation);
                            if (error > 1e-3)
                            {
                                ++s.Stats.Corrections;
                                s.Stats.LastCorrection = error;
                            }
                            t.Translation = p;
                            t.Rotation = st->Rotation;
                            t.Scale = st->Scale;
                        }
                    }
                    for (auto it = s.Locals.begin(); it != s.Locals.end();)
                    {
                        if (applied.contains(it->first))
                        {
                            ++it;
                            continue;
                        }
                        Entity ent = it->second.Uuid ? m_Scene->GetEntityByUUID(it->second.Uuid) : Entity();
                        if (ent && it->second.SceneObject && !s.Gone.contains(it->first))
                        {
                            // Out of interest: the scene's own entity waits, hidden, until it is relevant again.
                            if (!it->second.Hidden) ent.SetActive(false);
                            it->second.Hidden = true;
                            it->second.Samples.clear();
                            ++it;
                            continue;
                        }
                        if (ent) m_Scene->DestroyEntityDeferred(ent);
                        s.Gone.erase(it->first);
                        it = s.Locals.erase(it);
                    }
                }
                else if (type == uint8_t(Msg::Rpc))
                {
                    Impl::QueuedRpc q;
                    q.NetId = uint32_t(in.VarU(32));
                    q.Flags = in.U8();
                    q.Name = in.String(64);
                    if (!in.Ok() || (q.Flags & ~(RpcGraph | RpcMulticast)) || !DecodeValue(in, q.Args) || !in.AtEnd() || !q.Args.is_array())
                    {
                        ++s.Stats.MalformedMessages;
                        continue;
                    }
                    q.Until = now + PendingRpcSeconds;
                    s.QueuedRpcs.push_back(std::move(q));
                    while (s.QueuedRpcs.size() > 256) s.QueuedRpcs.pop_front();
                }
                else ++s.Stats.MalformedMessages;
            }
        }
        // Server RPCs for entities this client has (an RPC may come before the snapshot that spawns its entity).
        for (auto it = s.QueuedRpcs.begin(); it != s.QueuedRpcs.end();)
        {
            Entity target = FindEntity(it->NetId);
            if (!target)
            {
                it = now > it->Until ? s.QueuedRpcs.erase(it) : std::next(it);
                continue;
            }
            std::string why;
            RpcTarget t;
            const ScriptRpc expected = it->Flags & RpcMulticast ? ScriptRpc::Multicast : ScriptRpc::Client;
            if (Resolve(target, it->Name, it->Flags & RpcGraph, t, why) && (t.Kind == expected || (why = "not that kind of RPC", false)) && CheckArgs(t, it->Args, why))
            {
                ++s.Stats.RpcsReceived;
                if (!Run(t, it->Name, it->Args, 0, why)) Log(std::format("RPC {} failed: {}", it->Name, why));
            }
            else
            {
                ++s.Stats.RpcsRejected;
                Log(std::format("RPC {} from the server rejected: {}", it->Name, why));
            }
            it = s.QueuedRpcs.erase(it);
        }
        // Replicated fields: written once the script can take them (a just spawned entity's script may not run yet).
        for (const auto& [tickId, state] : s.Received)
        {
            if (tickId != s.AppliedTick) continue;
            for (const auto& [netId, st] : state)
            {
                auto local = s.Locals.find(netId);
                if (local == s.Locals.end() || !local->second.Uuid || !st->Names) continue;
                Entity ent = m_Scene->GetEntityByUUID(local->second.Uuid);
                if (!ent) continue;
                auto& l = local->second;
                l.Applied.resize(st->Fields.size());
                l.Written.resize(st->Fields.size(), false);
                for (size_t i = 0; i < st->Fields.size() && i < st->Names->size(); ++i)
                    if (!st->Fields[i].is_null() && (!l.Written[i] || l.Applied[i] != st->Fields[i]) && WriteField(ent, (*st->Names)[i], st->Fields[i]))
                    {
                        l.Applied[i] = st->Fields[i];
                        l.Written[i] = true;
                    }
            }
        }
        if (!s.Client->IsConnected()) return;
        // Prediction: the owned predicted entities move from this client's input at the server's tick rate.
        s.Accumulator = std::min(s.Accumulator + dt, tickDt * 5);
        std::vector<Entity> own;
        for (auto ent : registry.view<NetworkIdentityComponent, NetworkTransformComponent>())
        {
            const auto& id = registry.get<NetworkIdentityComponent>(ent);
            if (id.NetId && id.Owner == m_LocalClient && m_LocalClient && registry.get<NetworkTransformComponent>(ent).Mode == NetworkTransformComponent::SyncMode::Predict)
                own.push_back({ ent, m_Scene });
        }
        while (s.Accumulator >= tickDt - 1e-9)
        {
            s.Accumulator -= tickDt;
            s.SendTick = true;
            if (own.empty() || !s.Welcomed) continue;
            const auto& nt = own.front().GetComponent<NetworkTransformComponent>();
            glm::vec2 input = m_InputSource ? m_InputSource(own.front()) : Input::GetVector(nt.MoveAction.empty() ? m_Settings.MoveAction : nt.MoveAction);
            if (!std::isfinite(input.x) || !std::isfinite(input.y)) input = glm::vec2(0.0f);
            if (glm::length(input) > 1.0f) input = glm::normalize(input);
            // Both sides move with exactly the value the server will get.
            input = Dequantize(Quantize(input).first, Quantize(input).second);
            s.Pending.push_back({ ++s.InputSeq, input });
            while (s.Pending.size() > MaxPendingInputs) s.Pending.pop_front();
            for (auto& ent : own)
            {
                auto& t = ent.GetComponent<TransformComponent>();
                t.Translation = Move(ent.GetComponent<NetworkTransformComponent>(), t.Translation, input, float(tickDt));
            }
        }
        // Interpolation: the others' entities as they were InterpolationDelay ago on the server.
        const double serverNow = now - s.ServerOffset;
        for (auto& [netId, local] : s.Locals)
        {
            if (!local.Uuid || local.Samples.empty()) continue;
            Entity ent = m_Scene->GetEntityByUUID(local.Uuid);
            if (!ent) continue;
            auto* nt = ent.TryGetComponent<NetworkTransformComponent>();
            const auto& id = ent.GetComponent<NetworkIdentityComponent>();
            if (nt && nt->Mode == NetworkTransformComponent::SyncMode::Predict && id.Owner == m_LocalClient && m_LocalClient) continue;
            const double delay = nt ? nt->InterpolationDelay : 0.1;
            s.Stats.InterpolationDelay = delay;
            const double at = serverNow - delay;
            auto& t = ent.GetComponent<TransformComponent>();
            const auto& samples = local.Samples;
            if (at <= samples.front().Time || samples.size() == 1)
            {
                const auto& x = at <= samples.front().Time ? samples.front() : samples.back();
                t.Translation = x.Position;
                t.Rotation = x.Rotation;
                t.Scale = x.Scale;
                continue;
            }
            if (at >= samples.back().Time)
            {
                t.Translation = samples.back().Position;
                t.Rotation = samples.back().Rotation;
                t.Scale = samples.back().Scale;
                continue;
            }
            auto next = std::find_if(samples.begin(), samples.end(), [&](const Impl::Sample& x) { return x.Time > at; });
            const auto& b = *next;
            const auto& a = *std::prev(next);
            const float k = float((at - a.Time) / std::max(1e-9, b.Time - a.Time));
            t.Translation = glm::mix(a.Position, b.Position, k);
            t.Scale = glm::mix(a.Scale, b.Scale, k);
            t.Rotation = glm::eulerAngles(glm::slerp(glm::quat(a.Rotation), glm::quat(b.Rotation), k));
        }
    }

    void Session::AfterUpdate(float)
    {
        if (!m_Scene || m_Role == Role::None) return;
        ZM_PROFILE_SCOPE("Network");
        Impl& s = *m_Impl;
        const double now = Now();
        auto& registry = m_Scene->GetRegistry();
        if (m_Role == Role::Client)
        {
            if (s.SendTick && s.Client->IsConnected())
            {
                s.SendTick = false;
                Writer w;
                w.U8(uint8_t(Msg::ClientTick));
                w.VarU(s.Received.empty() ? 0 : s.Received.back().first);
                // The inputs the server has not acknowledged yet (consecutive: the first's number, then the values).
                const size_t count = std::min(s.Pending.size(), MaxInputsPerMessage);
                w.VarU(count);
                if (count) w.VarU(s.Pending[s.Pending.size() - count].Seq);
                for (size_t i = s.Pending.size() - count; i < s.Pending.size(); ++i)
                {
                    const auto [x, y] = Quantize(s.Pending[i].Move);
                    w.U16(uint16_t(x));
                    w.U16(uint16_t(y));
                }
                s.Client->Send(w.Data(), false);
            }
            s.Client->Flush(now);
            s.Stats.NetworkedEntities = s.Locals.size();
            return;
        }
        if (!s.Ticked)
        {
            s.Server->Flush(now);
            return;
        }
        s.Ticked = false;
        const double start = Seconds();
        // The world as it is after this tick: every networked entity's state (unchanged ones keep their pointer).
        WorldState world;
        std::map<uint32_t, std::pair<glm::vec3, const NetworkIdentityComponent*>> where;
        for (auto it = s.Entities.begin(); it != s.Entities.end();)
        {
            Entity ent = m_Scene->GetEntityByUUID(it->second);
            auto* id = ent ? ent.TryGetComponent<NetworkIdentityComponent>() : nullptr;
            if (!id || id->NetId != it->first)
            {
                if (auto cur = s.Current.find(it->first); cur != s.Current.end() && cur->second->SceneId) s.DestroyedSceneObjects.insert(cur->second->SceneId);
                it = s.Entities.erase(it);
                continue;
            }
            ++it;
        }
        // Entities that got a Network Identity some other way than Spawn: replicated when they came from a prefab.
        for (auto ent : registry.view<NetworkIdentityComponent>())
        {
            auto& id = registry.get<NetworkIdentityComponent>(ent);
            if (id.NetId) continue;
            const UUID uuid = registry.get<IDComponent>(ent).ID;
            if (id.Prefab.empty() || !ValidPrefabPath(id.Prefab))
            {
                if (s.Unreplicable.insert(uuid).second)
                    Log(std::format("'{}' has a Network Identity but was not spawned with Net::Spawn: not replicated", registry.get<TagComponent>(ent).Tag));
                continue;
            }
            id.NetId = s.NextNetId++;
            s.Entities[id.NetId] = uuid;
        }
        for (const auto& [netId, uuid] : s.Entities)
        {
            Entity ent = m_Scene->GetEntityByUUID(uuid);
            const auto& id = ent.GetComponent<NetworkIdentityComponent>();
            const auto& t = ent.GetComponent<TransformComponent>();
            auto st = std::make_shared<EntityState>();
            st->NetId = netId;
            st->Owner = id.Owner;
            st->SceneId = id.SceneObject ? uint64_t(uuid) : 0;
            st->Prefab = id.SceneObject ? std::string() : id.Prefab;
            st->Position = t.Translation;
            st->Rotation = t.Rotation;
            st->Scale = t.Scale;
            auto names = FieldNamesOf(ent, &st->Fields);
            auto previous = s.Current.find(netId);
            if (previous != s.Current.end() && previous->second->Names && *previous->second->Names == names) st->Names = previous->second->Names;
            else st->Names = std::make_shared<const std::vector<std::string>>(std::move(names));
            world[netId] = previous != s.Current.end() && *previous->second == *st ? previous->second : StatePtr(st);
            where[netId] = { t.Translation, &id };
        }
        s.Current = world;
        s.Stats.NetworkedEntities = world.size();
        std::map<uint32_t, glm::vec3> views; // client -> the position of its first entity (where is ordered by network id)
        for (const auto& [netId, info] : where)
            if (info.second->Owner) views.try_emplace(info.second->Owner, info.first);
        if (m_Settings.StateLogEvery && s.Tick % m_Settings.StateLogEvery == 0) ZM_CORE_INFO("{}", StateLine(s.Tick, world));
        size_t replicated = 0;
        for (auto& [client, r] : s.Clients)
        {
            if (!r.Connected) continue;
            const WorldState* baseline = nullptr;
            uint32_t baseTick = 0;
            if (r.HasAck)
                for (const auto& h : r.History)
                    if (h.first == r.AckedTick)
                    {
                        baseline = &h.second;
                        baseTick = h.first;
                    }
            // Interest: what is near the client's own entity (its first: the lowest network id), or everything while
            // it has none.
            auto view = views.find(client);
            const bool hasView = view != views.end();
            const glm::vec3 viewAt = hasView ? view->second : glm::vec3(0.0f);
            std::vector<uint32_t> relevant;
            for (const auto& [netId, info] : where)
            {
                const auto* id = info.second;
                bool keep = !hasView || id->AlwaysRelevant || id->Owner == client;
                if (!keep)
                {
                    float radius = id->RelevanceRadius > 0 ? id->RelevanceRadius : m_Settings.RelevanceRadius;
                    if (baseline && baseline->contains(netId)) radius *= 1.1f; // hysteresis: no flicker at the edge
                    keep = glm::length(info.first - viewAt) <= radius;
                }
                if (keep) relevant.push_back(netId);
            }
            WorldState record;
            std::vector<uint32_t> removed;
            if (baseline)
                for (const auto& [netId, st] : *baseline)
                {
                    if (std::binary_search(relevant.begin(), relevant.end(), netId)) record[netId] = st;
                    else removed.push_back(netId);
                }
            // The owned entities first, then the rest round-robin from where the last snapshot stopped.
            std::vector<uint32_t> order;
            for (uint32_t netId : relevant)
                if (where[netId].second->Owner == client) order.push_back(netId);
            const size_t n = relevant.size();
            for (size_t i = 0; i < n; ++i)
            {
                const uint32_t netId = relevant[(r.Rotate + i) % n];
                if (where[netId].second->Owner != client) order.push_back(netId);
            }
            Writer entries;
            size_t count = 0, examined = 0;
            bool full = false;
            for (uint32_t netId : order)
            {
                ++examined;
                const StatePtr& cur = world[netId];
                const EntityState* base = nullptr;
                if (baseline)
                    if (auto b = baseline->find(netId); b != baseline->end()) base = b->second.get();
                if (base == cur.get()) continue;
                Writer w;
                w.VarU(netId);
                uint8_t flags = 0;
                if (!base) flags = 32 | 16 | 1 | 2 | 4 | (cur->Fields.empty() ? 0 : 8);
                else
                {
                    if (base->Owner != cur->Owner) flags |= 16;
                    if (base->Position != cur->Position) flags |= 1;
                    if (base->Rotation != cur->Rotation) flags |= 2;
                    if (base->Scale != cur->Scale) flags |= 4;
                    if (base->Fields != cur->Fields) flags |= 8;
                    if (base->SceneId != cur->SceneId || base->Prefab != cur->Prefab || base->Fields.size() != cur->Fields.size()) flags = 32 | 16 | 1 | 2 | 4 | 8;
                }
                w.U8(flags);
                if (flags & 32)
                {
                    if (cur->SceneId)
                    {
                        w.U8(0);
                        w.U64(cur->SceneId);
                    }
                    else
                    {
                        w.U8(1);
                        w.String(cur->Prefab);
                    }
                    w.VarU(cur->Names->size());
                    for (const auto& name : *cur->Names) w.String(name);
                }
                if (flags & 16) w.VarU(cur->Owner);
                auto vec = [&](const glm::vec3& v) {
                    w.F32(v.x);
                    w.F32(v.y);
                    w.F32(v.z);
                };
                if (flags & 1) vec(cur->Position);
                if (flags & 2) vec(cur->Rotation);
                if (flags & 4) vec(cur->Scale);
                if (flags & 8)
                {
                    Writer f;
                    size_t changed = 0;
                    for (size_t i = 0; i < cur->Fields.size(); ++i)
                        if ((flags & 32) || !base || i >= base->Fields.size() || base->Fields[i] != cur->Fields[i])
                        {
                            Writer one;
                            one.VarU(i);
                            if (!EncodeValue(one, cur->Fields[i], nullptr, FieldLimits)) continue; // (too big: not sent)
                            f.Bytes(one.Data().data(), one.Size());
                            ++changed;
                        }
                    w.VarU(changed);
                    w.Bytes(f.Data().data(), f.Size());
                }
                if (count > 0 && entries.Size() + w.Size() > m_Settings.SnapshotBudget)
                {
                    full = true;
                    break;
                }
                entries.Bytes(w.Data().data(), w.Size());
                ++count;
                record[netId] = cur;
            }
            if (n) r.Rotate = full ? (r.Rotate + examined) % n : 0;
            Writer msg;
            msg.U8(uint8_t(Msg::Snapshot));
            msg.VarU(s.Tick);
            msg.VarU(baseTick);
            msg.VarU(r.LastInput);
            msg.VarU(removed.size());
            for (uint32_t netId : removed) msg.VarU((uint64_t(netId) << 1) | (world.contains(netId) ? 0u : 1u));
            msg.VarU(count);
            msg.Bytes(entries.Data().data(), entries.Size());
            if (s.Server->Send(client, msg.Data(), false))
            {
                ++s.Stats.SnapshotsSent;
                s.Stats.SnapshotBytes += msg.Size();
            }
            replicated += count;
            r.Relevant = record.size();
            r.History.push_back({ s.Tick, std::move(record) });
            while (r.History.size() > HistorySize) r.History.pop_front();
            if (r.HasAck && (r.History.front().first > r.AckedTick)) r.HasAck = false; // the baseline fell out: full again
        }
        s.Server->Flush(now);
        s.Stats.ReplicatedEntities = replicated;
        s.TickWork += Seconds() - start;
        const double ms = s.TickWork * 1000.0;
        s.TickWork = 0;
        ++s.TickCount;
        s.Stats.TickMs = ms;
        s.Stats.MaxTickMs = std::max(s.Stats.MaxTickMs, ms);
        s.Stats.AverageTickMs += (ms - s.Stats.AverageTickMs) / double(std::min<uint64_t>(s.TickCount, 100));
        (void)now;
    }

    // ===================================================================== monitoring
    SessionStats Session::GetStats() const
    {
        SessionStats st = m_Impl->Stats;
        if (m_Role == Role::Server) st.Tick = m_Impl->Tick;
        return st;
    }

    std::vector<SessionClient> Session::GetClients() const
    {
        std::vector<SessionClient> out;
        if (!m_Impl->Server) return out;
        for (const auto& [id, r] : m_Impl->Clients)
        {
            SessionClient c;
            c.Id = id;
            c.Connected = r.Connected && m_Impl->Server->HasClient(id);
            if (const auto* conn = m_Impl->Server->GetConnection(id)) c.Stats = conn->GetStats();
            for (const auto& info : m_Impl->Server->GetClients())
                if (info.Id == id) c.Peer = info.Peer;
            c.Relevant = r.Relevant;
            c.AckedTick = r.AckedTick;
            c.RpcsRejected = r.RpcsRejected;
            c.InputsRejected = r.InputsRejected;
            out.push_back(c);
        }
        return out;
    }

    json Session::DescribeEntities() const
    {
        json out = json::array();
        if (!m_Scene) return out;
        auto& registry = m_Scene->GetRegistry();
        std::vector<std::pair<uint32_t, entt::entity>> list;
        for (auto e : registry.view<NetworkIdentityComponent>())
            if (registry.get<NetworkIdentityComponent>(e).NetId) list.push_back({ registry.get<NetworkIdentityComponent>(e).NetId, e });
        std::sort(list.begin(), list.end());
        for (const auto& [netId, e] : list)
        {
            Entity ent{ e, m_Scene };
            const auto& t = ent.GetComponent<TransformComponent>();
            FieldValues values;
            const auto names = FieldNamesOf(ent, &values);
            json fields = json::object();
            for (size_t i = 0; i < names.size(); ++i) fields[names[i]] = values[i];
            out.push_back({ { "NetId", netId }, { "Owner", ent.GetComponent<NetworkIdentityComponent>().Owner }, { "Name", ent.GetName() },
                            { "Position", { t.Translation.x, t.Translation.y, t.Translation.z } }, { "Fields", std::move(fields) } });
        }
        return out;
    }

    const Connection* Session::GetServerConnection() const { return m_Impl->Client ? m_Impl->Client->GetConnection() : nullptr; }

}
