#pragma once

/// @file
/// @brief The connection-oriented transport over a datagram socket: a server that accepts clients through a
/// handshake (transport, game protocol and project checks, a stateless challenge token against spoofed source
/// addresses, a padded request so that a reply is never larger than what asked for it), keeps a Connection per
/// client, times them out, rate-limits what each one sends, caps the number of clients and lets a client that lost
/// its connection resume its session (the same client id) with the token the server gave it; and the client side.
/// Both are polled (Update / Poll / Flush) from one thread and never block. Nothing is encrypted or authenticated
/// beyond the challenge and the session keys: see docs/Networking.md.

#include "Zamin/Network/NetConnection.h"
#include "Zamin/Network/NetSocket.h"

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Zamin::Net {

    /// The version of the packet layer itself (Connection, handshake packets).
    constexpr uint16_t TransportVersion = 1;

    enum class DisconnectReason : uint8_t
    {
        None = 0,
        Graceful,       ///< the other side said goodbye
        Timeout,        ///< nothing arrived for TimeoutSeconds (a resumable loss)
        WrongVersion,   ///< another transport or game protocol version
        WrongProject,   ///< another project (a different game)
        ServerFull,     ///< MaxClients reached
        Kicked,         ///< the server removed the client
        RateLimited,    ///< it sent more than its rate limits for too long
        Malformed,      ///< it kept sending packets that do not decode
        HandshakeFailed,///< no answer to the handshake in time
        Replaced,       ///< the same session resumed from another address
        Shutdown,       ///< the server stopped
    };
    const char* DisconnectReasonName(DisconnectReason reason);

    struct TransportSettings
    {
        uint16_t ProtocolVersion = 1;   ///< the game's network protocol (both sides must match)
        uint64_t ProjectId = 0;         ///< which game (both sides must match)
        double TimeoutSeconds = 10.0;
        double HandshakeSeconds = 5.0;  ///< the client gives up connecting after this
        uint32_t MaxClients = 16;
        double ResumeSeconds = 30.0;    ///< a timed-out client may resume its session this long
        /// Per connection, what the server takes from a client (token buckets with one second of burst): more is
        /// dropped, and a client over its limit for KickAfterSeconds is disconnected (RateLimited).
        double MaxPacketsPerSecond = 300;
        double MaxBytesPerSecond = 256 * 1024;
        double KickAfterSeconds = 3.0;
        /// Malformed data packets a client may send in a row before it is disconnected (Malformed).
        uint32_t MaxMalformed = 64;
    };

    struct TransportEvent
    {
        /// Expired (server): a client that left resumably (Disconnected with Resumable) did not come back within
        /// ResumeSeconds; its session is gone for good.
        enum class Kind { Connected, Disconnected, Message, Expired };
        Kind Type = Kind::Message;
        uint32_t Client = 0;     ///< server: which client; client: its own id
        bool Resumed = false;    ///< Connected: an earlier session of this client came back
        bool Resumable = false;  ///< Disconnected (server): the client may resume its session (a timeout, a restart)
        DisconnectReason Reason = DisconnectReason::None;
        Message Payload;
    };

    /// Counters of what the server refused.
    struct ServerCounters
    {
        uint64_t Denied[16]{};          ///< by DisconnectReason
        uint64_t BadDatagrams = 0;      ///< not our magic, too short, unknown type, a bad challenge
        uint64_t RateLimited = 0;       ///< packets dropped by the rate limits
        uint64_t Resumed = 0;
    };

    struct ClientInfo
    {
        uint32_t Id = 0;
        Address Peer;
        double ConnectedAt = 0;
        ConnectionStats Stats;
        uint64_t RateLimited = 0;
    };

    class ServerTransport
    {
    public:
        ~ServerTransport();
        bool Start(std::unique_ptr<Socket> socket, const TransportSettings& settings);
        /// Every client gets a goodbye (Shutdown); the socket closes.
        void Stop();
        bool IsRunning() const { return m_Socket != nullptr; }
        /// Reads every datagram that has arrived, answers handshakes, times out silent clients.
        void Update(double now);
        bool Poll(TransportEvent& out);
        /// False for an unknown client or a message Connection::Send refuses.
        bool Send(uint32_t client, const std::vector<uint8_t>& message, bool reliable);
        /// Sends what every connection has to send now.
        void Flush(double now);
        void Kick(uint32_t client, DisconnectReason reason = DisconnectReason::Kicked);
        std::vector<ClientInfo> GetClients() const;
        bool HasClient(uint32_t client) const { return m_Peers.contains(client); }
        const Connection* GetConnection(uint32_t client) const;
        size_t GetClientCount() const { return m_Peers.size(); }
        const ServerCounters& GetCounters() const { return m_Counters; }
        const TransportSettings& GetSettings() const { return m_Settings; }
        Socket* GetSocket() { return m_Socket.get(); }
        Address GetLocalAddress() const { return m_Socket ? m_Socket->GetLocalAddress() : Address(); }

    private:
        struct Peer
        {
            uint32_t Id = 0;
            Address Addr;
            uint64_t Nonce = 0, ResumeToken = 0;
            std::unique_ptr<Connection> Conn;
            double ConnectedAt = 0, LastRefill = 0;
            double PacketTokens = 0, ByteTokens = 0;
            double OverLimitSince = -1, LastDropped = -1;
            uint64_t RateLimited = 0, MalformedSeen = 0;
            uint32_t MalformedInRow = 0;
        };
        struct Dormant
        {
            uint64_t Token = 0;
            double Expires = 0;
        };
        void HandleDatagram(double now, const Address& from, const std::vector<uint8_t>& data);
        uint64_t Challenge(const Address& from, uint64_t nonce, int64_t bucket) const;
        void Deny(const Address& to, uint64_t nonce, DisconnectReason reason);
        void SendAccept(const Peer& peer, bool resumed);
        void Remove(double now, uint32_t id, DisconnectReason reason, bool notifyPeer, bool keepForResume);
        bool Admit(double now, Peer& peer, size_t bytes);

        std::unique_ptr<Socket> m_Socket;
        TransportSettings m_Settings;
        uint64_t m_Secret = 0, m_Counter = 0;
        std::map<uint32_t, Peer> m_Peers;
        std::map<Address, uint32_t> m_ByAddress;
        std::map<uint32_t, Dormant> m_Dormant;
        std::deque<TransportEvent> m_Events;
        uint32_t m_NextId = 1;
        ServerCounters m_Counters;
        std::vector<uint8_t> m_Buffer, m_Packet;
        std::vector<Message> m_Delivered;
    };

    class ClientTransport
    {
    public:
        enum class State { Disconnected, Requesting, Responding, Connected };
        ~ClientTransport();
        /// Starts connecting (Requesting). resumeId/resumeToken: a session to resume (ClientTransport of an earlier
        /// run: GetClientId / GetResumeToken), 0 for a new one.
        bool Connect(std::unique_ptr<Socket> socket, const Address& server, const TransportSettings& settings, uint32_t resumeId = 0, uint64_t resumeToken = 0);
        /// A goodbye to the server (if connected); the socket closes.
        void Disconnect();
        /// After a lost connection (Disconnected, the socket kept): connects again on the same socket, resuming the
        /// session it had (GetClientId / GetResumeToken). False without a socket.
        bool Reconnect();
        void Update(double now);
        bool Poll(TransportEvent& out);
        bool Send(const std::vector<uint8_t>& message, bool reliable);
        void Flush(double now);

        State GetState() const { return m_State; }
        bool IsConnected() const { return m_State == State::Connected; }
        uint32_t GetClientId() const { return m_ClientId; }
        uint64_t GetResumeToken() const { return m_ResumeToken; }
        DisconnectReason GetLastReason() const { return m_LastReason; }
        const Connection* GetConnection() const { return m_Conn.get(); }
        const Address& GetServer() const { return m_Server; }
        Socket* GetSocket() { return m_Socket.get(); }

    private:
        void Fail(DisconnectReason reason);
        void SendRequest();
        void SendResponse();

        std::unique_ptr<Socket> m_Socket;
        std::unique_ptr<Connection> m_Conn;
        TransportSettings m_Settings;
        Address m_Server;
        State m_State = State::Disconnected;
        uint64_t m_Nonce = 0, m_Challenge = 0, m_ResumeToken = 0;
        uint32_t m_ClientId = 0, m_ResumeId = 0;
        double m_Started = 0, m_LastHandshake = -1;
        DisconnectReason m_LastReason = DisconnectReason::None;
        std::deque<TransportEvent> m_Events;
        std::vector<uint8_t> m_Buffer, m_Packet;
        std::vector<Message> m_Delivered;
    };

}
