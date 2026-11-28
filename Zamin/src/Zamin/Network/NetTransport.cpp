#include "Zamin/Network/NetTransport.h"

#include "Zamin/Network/NetCodec.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace Zamin::Net {

    namespace {
        // A connect request is padded to this size: the challenge it asks for is far smaller, so the server can
        // never be used to amplify traffic towards a spoofed address.
        constexpr size_t RequestSize = 256;
        constexpr double HandshakeRetry = 0.25;
        constexpr double ChallengeBucket = 5.0; // seconds a challenge token stays valid (plus the bucket before)

        uint64_t RandomU64()
        {
            static std::random_device device;
            return (uint64_t(device()) << 32) ^ uint64_t(device()) ^ Mix64(uint64_t(Now() * 1e9));
        }

        void Header(Writer& w, PacketType type)
        {
            w.U16(PacketMagic);
            w.U8(uint8_t(type));
        }
    }

    const char* DisconnectReasonName(DisconnectReason reason)
    {
        switch (reason)
        {
            case DisconnectReason::None: return "none";
            case DisconnectReason::Graceful: return "disconnected";
            case DisconnectReason::Timeout: return "timed out";
            case DisconnectReason::WrongVersion: return "wrong protocol version";
            case DisconnectReason::WrongProject: return "wrong project";
            case DisconnectReason::ServerFull: return "server full";
            case DisconnectReason::Kicked: return "kicked";
            case DisconnectReason::RateLimited: return "rate limited";
            case DisconnectReason::Malformed: return "malformed packets";
            case DisconnectReason::HandshakeFailed: return "no answer from the server";
            case DisconnectReason::Replaced: return "replaced by a resumed session";
            case DisconnectReason::Shutdown: return "server shut down";
        }
        return "unknown";
    }

    // =================================================================== server
    ServerTransport::~ServerTransport() { Stop(); }

    bool ServerTransport::Start(std::unique_ptr<Socket> socket, const TransportSettings& settings)
    {
        Stop();
        if (!socket) return false;
        m_Socket = std::move(socket);
        m_Settings = settings;
        m_Secret = RandomU64();
        m_Events.clear();
        m_Dormant.clear();
        return true;
    }

    void ServerTransport::Stop()
    {
        if (!m_Socket) return;
        std::vector<uint32_t> ids;
        for (const auto& [id, peer] : m_Peers) ids.push_back(id);
        for (uint32_t id : ids) Remove(Now(), id, DisconnectReason::Shutdown, true, false);
        m_Dormant.clear();
        m_Socket.reset();
    }

    uint64_t ServerTransport::Challenge(const Address& from, uint64_t nonce, int64_t bucket) const
    {
        return Mix64(m_Secret ^ Mix64((uint64_t(from.Ip) << 16) ^ from.Port) ^ Mix64(nonce) ^ Mix64(uint64_t(bucket) * 0x9E3779B97F4A7C15ull));
    }

    void ServerTransport::Deny(const Address& to, uint64_t nonce, DisconnectReason reason)
    {
        Writer w;
        Header(w, PacketType::Deny);
        w.U64(nonce);
        w.U8(uint8_t(reason));
        m_Socket->Send(to, w.Data().data(), w.Size());
        ++m_Counters.Denied[size_t(reason) & 15];
    }

    void ServerTransport::SendAccept(const Peer& peer, bool resumed)
    {
        Writer w;
        Header(w, PacketType::Accept);
        w.U64(peer.Nonce);
        w.U32(peer.Id);
        w.U64(peer.Conn->GetKey());
        w.U64(peer.ResumeToken);
        w.U8(resumed ? 1 : 0);
        m_Socket->Send(peer.Addr, w.Data().data(), w.Size());
    }

    void ServerTransport::Remove(double now, uint32_t id, DisconnectReason reason, bool notifyPeer, bool keepForResume)
    {
        auto found = m_Peers.find(id);
        if (found == m_Peers.end()) return;
        Peer& peer = found->second;
        if (notifyPeer && m_Socket)
        {
            Writer w;
            Header(w, PacketType::Disconnect);
            w.U64(peer.Conn->GetKey());
            w.U8(uint8_t(reason));
            for (int i = 0; i < 3; ++i) m_Socket->Send(peer.Addr, w.Data().data(), w.Size());
        }
        if (keepForResume) m_Dormant[id] = { peer.ResumeToken, now + m_Settings.ResumeSeconds };
        if (auto byAddress = m_ByAddress.find(peer.Addr); byAddress != m_ByAddress.end() && byAddress->second == id) m_ByAddress.erase(byAddress);
        m_Peers.erase(found);
        TransportEvent e;
        e.Type = TransportEvent::Kind::Disconnected;
        e.Client = id;
        e.Reason = reason;
        e.Resumable = keepForResume;
        m_Events.push_back(std::move(e));
    }

    void ServerTransport::Kick(uint32_t client, DisconnectReason reason) { Remove(Now(), client, reason, true, false); }

    bool ServerTransport::Admit(double now, Peer& peer, size_t bytes)
    {
        // Token buckets: one second of burst at the configured rates.
        const double dt = std::max(0.0, now - peer.LastRefill);
        peer.LastRefill = now;
        peer.PacketTokens = std::min(m_Settings.MaxPacketsPerSecond, peer.PacketTokens + dt * m_Settings.MaxPacketsPerSecond);
        peer.ByteTokens = std::min(m_Settings.MaxBytesPerSecond, peer.ByteTokens + dt * m_Settings.MaxBytesPerSecond);
        if (peer.PacketTokens >= 1.0 && peer.ByteTokens >= double(bytes))
        {
            peer.PacketTokens -= 1.0;
            peer.ByteTokens -= double(bytes);
            // Over the limit until a whole second passes without a dropped packet.
            if (peer.OverLimitSince >= 0 && now - peer.LastDropped > 1.0) peer.OverLimitSince = -1;
            return true;
        }
        ++peer.RateLimited;
        ++m_Counters.RateLimited;
        peer.LastDropped = now;
        if (peer.OverLimitSince < 0) peer.OverLimitSince = now;
        return false;
    }

    void ServerTransport::HandleDatagram(double now, const Address& from, const std::vector<uint8_t>& data)
    {
        Reader r(data);
        const uint16_t magic = r.U16();
        const uint8_t type = r.U8();
        if (!r.Ok() || magic != PacketMagic)
        {
            ++m_Counters.BadDatagrams;
            return;
        }
        if (type == uint8_t(PacketType::Data))
        {
            auto byAddress = m_ByAddress.find(from);
            if (byAddress == m_ByAddress.end()) return; // not connected (or a forged source)
            Peer& peer = m_Peers.at(byAddress->second);
            if (!Admit(now, peer, data.size())) return;
            m_Delivered.clear();
            const uint64_t malformedBefore = peer.Conn->GetStats().MalformedPackets;
            if (!peer.Conn->ReadPacket(now, data.data(), data.size(), m_Delivered))
            {
                if (peer.Conn->GetStats().MalformedPackets > malformedBefore && ++peer.MalformedInRow > m_Settings.MaxMalformed)
                    Remove(now, peer.Id, DisconnectReason::Malformed, true, false);
                return;
            }
            peer.MalformedInRow = 0;
            for (auto& message : m_Delivered)
            {
                TransportEvent e;
                e.Type = TransportEvent::Kind::Message;
                e.Client = peer.Id;
                e.Payload = std::move(message);
                m_Events.push_back(std::move(e));
            }
            return;
        }
        if (type == uint8_t(PacketType::ConnectRequest))
        {
            const uint16_t transport = r.U16(), protocol = r.U16();
            const uint64_t project = r.U64(), nonce = r.U64();
            if (!r.Ok() || data.size() < RequestSize)
            {
                ++m_Counters.BadDatagrams;
                return;
            }
            if (transport != TransportVersion || protocol != m_Settings.ProtocolVersion) return Deny(from, nonce, DisconnectReason::WrongVersion);
            if (project != m_Settings.ProjectId) return Deny(from, nonce, DisconnectReason::WrongProject);
            // A client already connected from here (its accept was lost): accept again, no new state.
            if (auto byAddress = m_ByAddress.find(from); byAddress != m_ByAddress.end() && m_Peers.at(byAddress->second).Nonce == nonce)
                return SendAccept(m_Peers.at(byAddress->second), false);
            Writer w;
            Header(w, PacketType::Challenge);
            w.U64(nonce);
            w.U64(Challenge(from, nonce, int64_t(std::floor(now / ChallengeBucket))));
            m_Socket->Send(from, w.Data().data(), w.Size());
            return;
        }
        if (type == uint8_t(PacketType::ChallengeResponse))
        {
            const uint64_t nonce = r.U64(), token = r.U64();
            const uint16_t transport = r.U16(), protocol = r.U16();
            const uint64_t project = r.U64();
            const uint32_t resumeId = r.U32();
            const uint64_t resumeToken = r.U64();
            if (!r.Ok())
            {
                ++m_Counters.BadDatagrams;
                return;
            }
            const int64_t bucket = int64_t(std::floor(now / ChallengeBucket));
            if (token != Challenge(from, nonce, bucket) && token != Challenge(from, nonce, bucket - 1))
            {
                ++m_Counters.BadDatagrams; // a forged / stale token: no state, no answer
                return;
            }
            if (transport != TransportVersion || protocol != m_Settings.ProtocolVersion) return Deny(from, nonce, DisconnectReason::WrongVersion);
            if (project != m_Settings.ProjectId) return Deny(from, nonce, DisconnectReason::WrongProject);
            if (auto byAddress = m_ByAddress.find(from); byAddress != m_ByAddress.end())
            {
                Peer& existing = m_Peers.at(byAddress->second);
                if (existing.Nonce == nonce) return SendAccept(existing, false); // the accept was lost
                Remove(now, existing.Id, DisconnectReason::Replaced, false, true);  // a new process on the same port (it may resume)
            }
            // Resume: a dormant session, or a live one (the client came back from another address before its old
            // connection timed out) with the token the server gave it.
            uint32_t id = 0;
            bool resumed = false;
            if (resumeId && resumeToken)
            {
                if (auto dormant = m_Dormant.find(resumeId); dormant != m_Dormant.end() && dormant->second.Token == resumeToken && dormant->second.Expires >= now)
                {
                    id = resumeId;
                    resumed = true;
                    m_Dormant.erase(dormant);
                }
                else if (auto live = m_Peers.find(resumeId); live != m_Peers.end() && live->second.ResumeToken == resumeToken)
                {
                    Remove(now, resumeId, DisconnectReason::Replaced, true, false);
                    // The Disconnected event of the replaced connection is not a departure of the client: drop it.
                    if (!m_Events.empty() && m_Events.back().Type == TransportEvent::Kind::Disconnected && m_Events.back().Client == resumeId) m_Events.pop_back();
                    id = resumeId;
                    resumed = true;
                }
            }
            if (!id)
            {
                if (m_Peers.size() >= m_Settings.MaxClients) return Deny(from, nonce, DisconnectReason::ServerFull);
                do id = m_NextId++;
                while (m_Peers.contains(id) || m_Dormant.contains(id) || id == 0);
            }
            else if (m_Peers.size() >= m_Settings.MaxClients)
                return Deny(from, nonce, DisconnectReason::ServerFull);
            Peer peer;
            peer.Id = id;
            peer.Addr = from;
            peer.Nonce = nonce;
            peer.ResumeToken = RandomU64() | 1;
            peer.Conn = std::make_unique<Connection>(Mix64(m_Secret ^ Mix64(nonce) ^ (uint64_t(id) << 32) ^ ++m_Counter), now);
            peer.ConnectedAt = now;
            peer.LastRefill = now;
            peer.PacketTokens = m_Settings.MaxPacketsPerSecond;
            peer.ByteTokens = m_Settings.MaxBytesPerSecond;
            m_ByAddress[from] = id;
            auto& stored = m_Peers[id] = std::move(peer);
            SendAccept(stored, resumed);
            if (resumed) ++m_Counters.Resumed;
            TransportEvent e;
            e.Type = TransportEvent::Kind::Connected;
            e.Client = id;
            e.Resumed = resumed;
            m_Events.push_back(std::move(e));
            return;
        }
        if (type == uint8_t(PacketType::Disconnect))
        {
            const uint64_t key = r.U64();
            auto byAddress = m_ByAddress.find(from);
            if (r.Ok() && byAddress != m_ByAddress.end() && m_Peers.at(byAddress->second).Conn->GetKey() == key)
                Remove(now, byAddress->second, DisconnectReason::Graceful, false, false);
            return;
        }
        ++m_Counters.BadDatagrams;
    }

    void ServerTransport::Update(double now)
    {
        if (!m_Socket) return;
        Address from;
        for (int i = 0; i < 100000 && m_Socket->Receive(from, m_Buffer); ++i) HandleDatagram(now, from, m_Buffer);
        std::vector<std::pair<uint32_t, DisconnectReason>> gone;
        for (const auto& [id, peer] : m_Peers)
        {
            if (now - peer.Conn->LastReceived() > m_Settings.TimeoutSeconds) gone.push_back({ id, DisconnectReason::Timeout });
            else if (peer.OverLimitSince >= 0 && now - peer.OverLimitSince > m_Settings.KickAfterSeconds) gone.push_back({ id, DisconnectReason::RateLimited });
        }
        for (auto [id, reason] : gone) Remove(now, id, reason, reason != DisconnectReason::Timeout, reason == DisconnectReason::Timeout);
        for (auto it = m_Dormant.begin(); it != m_Dormant.end();)
        {
            if (it->second.Expires >= now)
            {
                ++it;
                continue;
            }
            TransportEvent e;
            e.Type = TransportEvent::Kind::Expired;
            e.Client = it->first;
            m_Events.push_back(std::move(e));
            it = m_Dormant.erase(it);
        }
    }

    bool ServerTransport::Poll(TransportEvent& out)
    {
        if (m_Events.empty()) return false;
        out = std::move(m_Events.front());
        m_Events.pop_front();
        return true;
    }

    bool ServerTransport::Send(uint32_t client, const std::vector<uint8_t>& message, bool reliable)
    {
        auto found = m_Peers.find(client);
        return found != m_Peers.end() && found->second.Conn->Send(message, reliable);
    }

    void ServerTransport::Flush(double now)
    {
        if (!m_Socket) return;
        for (auto& [id, peer] : m_Peers)
            for (int i = 0; i < 512 && peer.Conn->WritePacket(now, m_Packet); ++i) m_Socket->Send(peer.Addr, m_Packet.data(), m_Packet.size());
    }

    std::vector<ClientInfo> ServerTransport::GetClients() const
    {
        std::vector<ClientInfo> out;
        for (const auto& [id, peer] : m_Peers) out.push_back({ id, peer.Addr, peer.ConnectedAt, peer.Conn->GetStats(), peer.RateLimited });
        return out;
    }

    const Connection* ServerTransport::GetConnection(uint32_t client) const
    {
        auto found = m_Peers.find(client);
        return found == m_Peers.end() ? nullptr : found->second.Conn.get();
    }

    // =================================================================== client
    ClientTransport::~ClientTransport() { Disconnect(); }

    bool ClientTransport::Connect(std::unique_ptr<Socket> socket, const Address& server, const TransportSettings& settings, uint32_t resumeId, uint64_t resumeToken)
    {
        Disconnect();
        if (!socket || !server.IsValid()) return false;
        m_Socket = std::move(socket);
        m_Server = server;
        m_Settings = settings;
        m_Nonce = RandomU64();
        m_ResumeId = resumeId;
        m_ResumeToken = resumeToken;
        m_ClientId = 0;
        m_State = State::Requesting;
        m_Started = Now();
        m_LastHandshake = -1;
        m_LastReason = DisconnectReason::None;
        m_Conn.reset();
        return true;
    }

    void ClientTransport::Disconnect()
    {
        if (m_State == State::Connected && m_Socket && m_Conn)
        {
            Writer w;
            Header(w, PacketType::Disconnect);
            w.U64(m_Conn->GetKey());
            w.U8(uint8_t(DisconnectReason::Graceful));
            for (int i = 0; i < 3; ++i) m_Socket->Send(m_Server, w.Data().data(), w.Size());
        }
        m_State = State::Disconnected;
        m_Conn.reset();
        m_Socket.reset();
    }

    bool ClientTransport::Reconnect()
    {
        if (!m_Socket || !m_Server.IsValid()) return false;
        m_Nonce = RandomU64();
        if (m_ClientId) m_ResumeId = m_ClientId;
        m_State = State::Requesting;
        m_Started = Now();
        m_LastHandshake = -1;
        m_Conn.reset();
        return true;
    }

    void ClientTransport::Fail(DisconnectReason reason)
    {
        const bool was = m_State != State::Disconnected;
        m_State = State::Disconnected;
        m_LastReason = reason;
        m_Conn.reset();
        if (!was) return;
        TransportEvent e;
        e.Type = TransportEvent::Kind::Disconnected;
        e.Client = m_ClientId;
        e.Reason = reason;
        m_Events.push_back(std::move(e));
    }

    void ClientTransport::SendRequest()
    {
        Writer w;
        Header(w, PacketType::ConnectRequest);
        w.U16(TransportVersion);
        w.U16(m_Settings.ProtocolVersion);
        w.U64(m_Settings.ProjectId);
        w.U64(m_Nonce);
        w.Data().resize(RequestSize, 0);
        m_Socket->Send(m_Server, w.Data().data(), w.Size());
    }

    void ClientTransport::SendResponse()
    {
        Writer w;
        Header(w, PacketType::ChallengeResponse);
        w.U64(m_Nonce);
        w.U64(m_Challenge);
        w.U16(TransportVersion);
        w.U16(m_Settings.ProtocolVersion);
        w.U64(m_Settings.ProjectId);
        w.U32(m_ResumeId);
        w.U64(m_ResumeToken);
        m_Socket->Send(m_Server, w.Data().data(), w.Size());
    }

    void ClientTransport::Update(double now)
    {
        if (!m_Socket || m_State == State::Disconnected) return;
        Address from;
        for (int i = 0; i < 100000 && m_State != State::Disconnected && m_Socket->Receive(from, m_Buffer); ++i)
        {
            if (from != m_Server) continue; // only the server talks to us
            Reader r(m_Buffer);
            const uint16_t magic = r.U16();
            const uint8_t type = r.U8();
            if (!r.Ok() || magic != PacketMagic) continue;
            if (type == uint8_t(PacketType::Data))
            {
                if (m_State != State::Connected || !m_Conn) continue;
                m_Delivered.clear();
                if (!m_Conn->ReadPacket(now, m_Buffer.data(), m_Buffer.size(), m_Delivered)) continue;
                for (auto& message : m_Delivered)
                {
                    TransportEvent e;
                    e.Type = TransportEvent::Kind::Message;
                    e.Client = m_ClientId;
                    e.Payload = std::move(message);
                    m_Events.push_back(std::move(e));
                }
                continue;
            }
            const uint64_t nonce = r.U64();
            if (!r.Ok() || nonce != m_Nonce)
            {
                // A Disconnect carries the session key instead of the nonce.
                if (type == uint8_t(PacketType::Disconnect) && r.Ok() && m_Conn && nonce == m_Conn->GetKey())
                {
                    const uint8_t reason = r.U8();
                    Fail(r.Ok() && reason <= uint8_t(DisconnectReason::Shutdown) ? DisconnectReason(reason) : DisconnectReason::Graceful);
                }
                continue;
            }
            if (type == uint8_t(PacketType::Challenge) && m_State == State::Requesting)
            {
                m_Challenge = r.U64();
                if (!r.Ok()) continue;
                m_State = State::Responding;
                SendResponse();
                m_LastHandshake = now;
            }
            else if (type == uint8_t(PacketType::Accept) && m_State != State::Connected)
            {
                const uint32_t id = r.U32();
                const uint64_t key = r.U64(), resume = r.U64();
                const uint8_t resumed = r.U8();
                if (!r.Ok() || id == 0) continue;
                m_ClientId = id;
                m_ResumeToken = resume;
                m_ResumeId = id;
                m_Conn = std::make_unique<Connection>(key, now);
                m_State = State::Connected;
                TransportEvent e;
                e.Type = TransportEvent::Kind::Connected;
                e.Client = id;
                e.Resumed = resumed != 0;
                m_Events.push_back(std::move(e));
            }
            else if (type == uint8_t(PacketType::Deny) && m_State != State::Connected)
            {
                const uint8_t reason = r.U8();
                Fail(r.Ok() && reason <= uint8_t(DisconnectReason::Shutdown) ? DisconnectReason(reason) : DisconnectReason::HandshakeFailed);
            }
        }
        if (m_State == State::Requesting || m_State == State::Responding)
        {
            if (now - m_Started > m_Settings.HandshakeSeconds) return Fail(DisconnectReason::HandshakeFailed);
            if (m_LastHandshake < 0 || now - m_LastHandshake >= HandshakeRetry)
            {
                if (m_State == State::Requesting) SendRequest();
                else SendResponse();
                m_LastHandshake = now;
            }
        }
        else if (m_State == State::Connected && now - m_Conn->LastReceived() > m_Settings.TimeoutSeconds)
            Fail(DisconnectReason::Timeout);
    }

    bool ClientTransport::Poll(TransportEvent& out)
    {
        if (m_Events.empty()) return false;
        out = std::move(m_Events.front());
        m_Events.pop_front();
        return true;
    }

    bool ClientTransport::Send(const std::vector<uint8_t>& message, bool reliable) { return m_State == State::Connected && m_Conn && m_Conn->Send(message, reliable); }

    void ClientTransport::Flush(double now)
    {
        if (m_State != State::Connected || !m_Conn || !m_Socket) return;
        for (int i = 0; i < 512 && m_Conn->WritePacket(now, m_Packet); ++i) m_Socket->Send(m_Server, m_Packet.data(), m_Packet.size());
    }

}
