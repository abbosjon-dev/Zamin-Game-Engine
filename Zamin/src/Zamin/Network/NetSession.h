#pragma once

/// @file
/// @brief The networked game on top of the transport (docs/Networking.md): one Session per Scene, the server
/// authoritative. The server runs the game at a fixed tick, gives every entity with a NetworkIdentityComponent a
/// network id and an owner, and sends each client a snapshot every tick: the transforms and replicated fields
/// (ZM_REPLICATED, graph variables marked Replicated) of the entities relevant to it (distance-based interest),
/// delta-compressed against the last snapshot that client acknowledged; entities that enter a client's view are
/// spawned there (their prefab, or the scene's own entity), the ones that leave it or are destroyed despawn.
/// Clients show the other entities interpolated between snapshots, and move the entities they own whose
/// NetworkTransform says Predict at once from their input (client-side prediction) and reconcile with the server's
/// state (replaying the inputs it has not processed yet). RPCs (C++: ZM_SERVER_RPC ..., graphs: Graph::Rpcs) go to
/// the server (from the entity's owner only, unless AnyClient), to the owner's client or to every client the
/// entity is relevant to; their arguments are checked against the declared types on receipt.
///
/// The scene drives it: Scene::UpdateRuntime calls BeforeUpdate (receive, apply snapshots, predict, server ticks
/// with the clients' inputs) and AfterUpdate (snapshots, inputs and acks out) of the session attached to it.

#include "Zamin/Network/NetTransport.h"
#include "Zamin/Scene/Entity.h"
#include "Zamin/Scripting/ScriptMethod.h"

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace Zamin {
    class Scene;
    class ScriptableEntity;
    struct NetworkTransformComponent;
}

namespace Zamin::Net {

    /// The version of the session's own messages (snapshots, inputs, RPCs): part of the handshake check together
    /// with the game's ProtocolVersion.
    constexpr uint16_t SessionProtocol = 1;

    enum class Role { None, Server, Client };

    struct SessionSettings
    {
        TransportSettings Transport;
        uint32_t TickRate = 30;           ///< server ticks (and snapshots) a second, 1..120
        float RelevanceRadius = 40.0f;    ///< an entity farther than this from a client's own entity is not sent to it
        size_t SnapshotBudget = 900;      ///< bytes of entity data in one snapshot (the rest follows in the next ones)
        bool Host = false;                ///< a server that is also a player: the host plays as client 0
        double MaxRpcsPerSecond = 60;     ///< per client, with one second of burst; more are dropped (counted)
        bool DespawnOwnedOnLeave = true;  ///< a client that left for good: the entities it owned are destroyed
        double ReconnectSeconds = 20.0;   ///< a client that lost its connection tries to resume this long
        /// The input vector (an InputVector of the project's input map) a predicted entity is moved with, unless
        /// the entity names its own (NetworkTransformComponent::MoveAction).
        std::string MoveAction = "Move";
    };

    /// One line of the session's own log (the Network panel, the dedicated server's console).
    struct SessionLogEntry
    {
        double Time = 0;
        std::string Text;
    };

    struct SessionStats
    {
        uint32_t Tick = 0;                ///< the server's tick (client: the latest snapshot's)
        size_t NetworkedEntities = 0;     ///< with a network id on this side
        size_t ReplicatedEntities = 0;    ///< server: entity states sent in the last tick's snapshots (all clients)
        uint64_t SnapshotsSent = 0, SnapshotsReceived = 0, SnapshotsDropped = 0, SnapshotBytes = 0;
        uint64_t RpcsSent = 0, RpcsReceived = 0, RpcsRejected = 0;
        uint64_t InputsReceived = 0, InputsRejected = 0, InputsMissed = 0, MalformedMessages = 0;
        uint64_t Corrections = 0;         ///< client: reconciliations that moved the predicted entity
        double LastCorrection = 0;        ///< client: how far the last one moved it
        double TickMs = 0, AverageTickMs = 0, MaxTickMs = 0; ///< server: the session's work in a tick
        double InterpolationDelay = 0;    ///< client: seconds the remote entities are shown behind the server
    };

    /// A client as the server sees it.
    struct SessionClient
    {
        uint32_t Id = 0;
        Address Peer;
        bool Connected = false;           ///< false: lost, may still resume
        ConnectionStats Stats;
        size_t Relevant = 0;              ///< entities in its last snapshot
        uint32_t AckedTick = 0;
        uint64_t RpcsRejected = 0, InputsRejected = 0;
    };

    /// A replicated script field or graph variable of an entity: its name and value.
    using FieldValues = std::vector<nlohmann::json>;

    class Session
    {
    public:
        Session();
        ~Session();
        Session(const Session&) = delete;
        Session& operator=(const Session&) = delete;

        /// The session attached to the scene (nullptr: none - a game without networking).
        static Session* Of(const Scene& scene);

        /// Starts serving the scene (it must be running: OnRuntimeStart). The socket: UdpSocket::Open, a
        /// LoopbackNetwork's, optionally inside a LinkConditioner. Entities of the scene with a Network Identity
        /// become scene objects (both sides have them; matched by UUID).
        bool StartServer(Scene& scene, std::unique_ptr<Socket> socket, const SessionSettings& settings, std::string* error = nullptr);
        /// Connects the scene to a server (the same project and scene). resumeId / resumeToken: a session of an
        /// earlier run to resume (GetResumeToken), 0 for a new one.
        bool StartClient(Scene& scene, std::unique_ptr<Socket> socket, const Address& server, const SessionSettings& settings, uint32_t resumeId = 0,
                         uint64_t resumeToken = 0, std::string* error = nullptr);
        /// Says goodbye (clients) or stops serving (every client gets Shutdown); the scene is detached.
        void Stop();

        Role GetRole() const { return m_Role; }
        bool IsServer() const { return m_Role == Role::Server; }
        bool IsClient() const { return m_Role == Role::Client; }
        bool IsHost() const { return m_Role == Role::Server && m_Settings.Host; }
        /// Client: connected (or resumed) to the server. Server: serving.
        bool IsConnected() const;
        /// The local player's client id: a client's own (0 before it is connected), the host's 0.
        uint32_t GetLocalClient() const { return m_LocalClient; }
        /// Client: trying to resume a lost connection.
        bool IsReconnecting() const { return m_Reconnecting; }
        uint64_t GetResumeToken() const;
        DisconnectReason GetLastDisconnectReason() const { return m_LastReason; }
        Scene* GetScene() const { return m_Scene; }
        const SessionSettings& GetSettings() const { return m_Settings; }
        Address GetLocalAddress() const;

        /// Scene::UpdateRuntime calls these (real time, not the game's time scale).
        void BeforeUpdate(float dt);
        void AfterUpdate(float dt);

        /// @name Entities
        /// @{
        /// Server: makes an entity of the prefab at `position`, owned by `owner` (0: the server), and replicates
        /// it. The prefab's root gets a Network Identity when it has none. An invalid entity on a client or for a
        /// prefab that cannot be read.
        Entity Spawn(const std::string& prefab, const glm::vec3& position, uint32_t owner = 0);
        /// Server: destroys a networked entity (at the end of the frame); it despawns on the clients.
        bool Despawn(Entity entity);
        /// The entity with this network id on this side (invalid: none).
        Entity FindEntity(uint32_t netId) const;
        /// The first entity the client owns (its player): interest is measured from it.
        Entity OwnedEntity(uint32_t client) const;
        /// Who owns the entity (0: the server). IsOwner: the entity is this side's own - a client's, or on the
        /// server (and the host) one the server owns (Owner 0). Authority is IsServer: the server decides for all.
        uint32_t OwnerOf(Entity entity) const;
        bool IsOwner(Entity entity) const;
        /// @}

        /// @name RPCs
        /// @{
        /// Calls an RPC of the entity's script: a C++ method registered with ZM_SERVER_RPC / ZM_CLIENT_RPC /
        /// ZM_MULTICAST_RPC (graph false) or an RPC the entity's graph defines (graph true); args are its
        /// parameters in order. Server RPCs are called by a client (on the host and without a session they run
        /// at once), Client and Multicast RPCs by the server (Multicast runs on the server too). False with a
        /// reason when the RPC is unknown, of another kind, the arguments do not fit, or this side may not call it.
        bool Call(Entity entity, const std::string& name, const nlohmann::json& args, bool graph, std::string* error = nullptr);
        /// @}

        /// @name Monitoring
        /// @{
        SessionStats GetStats() const;
        std::vector<SessionClient> GetClients() const;
        const Connection* GetServerConnection() const;
        const std::deque<SessionLogEntry>& GetLog() const { return m_Log; }
        /// The link conditioner the session's socket goes through (nullptr: it was not started in one).
        LinkConditioner* GetConditioner() const { return m_Conditioner; }
        /// @}

        /// Replaces where a predicted entity's input comes from (tests, bots): the move vector for this tick.
        void SetInputSource(std::function<glm::vec2(Entity)> source) { m_InputSource = std::move(source); }
        /// Lines the session logs also go here (the dedicated server prints them).
        void SetLogSink(std::function<void(const std::string&)> sink) { m_LogSink = std::move(sink); }
        /// Called after the scene was destroyed under the session (it is detached).
        void OnSceneDestroyed();

        /// The movement a predicted NetworkTransform makes in one tick (both sides run exactly this).
        static glm::vec3 Move(const NetworkTransformComponent& transform, glm::vec3 position, glm::vec2 input, float dt);
        /// Whether a value fits a reflected parameter type (RPC arguments are checked with it on receipt).
        static bool Fits(const ScriptValueType& type, const nlohmann::json& value);

        struct EntityState;
        struct Impl;

    private:
        void Log(std::string text);
        bool Attach(Scene& scene, std::string* error);
        void Detach();

        Role m_Role = Role::None;
        Scene* m_Scene = nullptr;
        SessionSettings m_Settings;
        uint32_t m_LocalClient = 0;
        bool m_Reconnecting = false;
        DisconnectReason m_LastReason = DisconnectReason::None;
        LinkConditioner* m_Conditioner = nullptr;
        std::function<glm::vec2(Entity)> m_InputSource;
        std::function<void(const std::string&)> m_LogSink;
        std::deque<SessionLogEntry> m_Log;
        std::unique_ptr<Impl> m_Impl;
    };

    /// The scene's link to its session (in the registry's context; Session::Of reads it).
    struct SceneLink
    {
        Session* Owner = nullptr;
    };

    /// @name For scripts (Zamin/Network/Net.h has the C++ helpers)
    /// Without a session a game is its own server: IsServer true, IsClient false, every entity its own.
    /// @{
    bool IsServer(const Scene& scene);
    bool IsClient(const Scene& scene);
    bool IsOwner(Entity entity);
    /// Session::Call on the entity's scene's session; without one the RPC runs here at once (the game is its own
    /// server): every kind, after the same argument checks.
    bool Call(Entity entity, const std::string& name, const nlohmann::json& args, bool graph, std::string* error = nullptr);
    /// The client that called the RPC running now (0: the server / the host, or no RPC running).
    uint32_t RpcSender();
    /// @}

    /// Scene::UpdateRuntime / ~Scene call these.
    void SceneBeforeUpdate(Scene& scene, float dt);
    void SceneAfterUpdate(Scene& scene, float dt);
    void SceneDestroyed(Scene& scene);

}
