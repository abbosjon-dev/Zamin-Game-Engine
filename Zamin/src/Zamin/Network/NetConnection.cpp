#include "Zamin/Network/NetConnection.h"

#include "Zamin/Network/NetCodec.h"
#include "Zamin/Network/NetSocket.h"

#include <algorithm>

namespace Zamin::Net {

    namespace {
        constexpr size_t RingSize = 1024;
        constexpr size_t ReceiveWindow = 1024;
        constexpr uint16_t MaxFragments = 512;
        constexpr size_t MaxAssemblies = 32;
        constexpr double AssemblyTimeout = 1.0;
        constexpr uint8_t ChunkReliable = 1, ChunkFragment = 2;
        constexpr uint8_t HeaderHasAck = 1;

        size_t VarUSize(uint64_t v)
        {
            size_t n = 1;
            while (v >= 0x80)
            {
                v >>= 7;
                ++n;
            }
            return n;
        }
    }

    Connection::Connection(uint64_t key, double now) : m_Key(key), m_LastReceived(now), m_Sent(RingSize), m_Received(RingSize), m_RateStart(now) {}

    bool Connection::Send(const std::vector<uint8_t>& message, bool reliable) { return Send(message.data(), message.size(), reliable); }

    bool Connection::Send(const uint8_t* data, size_t size, bool reliable)
    {
        if (!data || size == 0 || size > MaxMessageSize) return false;
        const size_t parts = (size + FragmentSize - 1) / FragmentSize;
        if (reliable)
        {
            if (m_Pending.size() + parts > MaxPendingReliable) return false;
            for (size_t i = 0; i < parts; ++i)
            {
                Pending p;
                p.Id = m_NextReliableId++;
                p.Fragment = parts > 1;
                p.Index = uint16_t(i);
                p.Count = uint16_t(parts);
                const size_t begin = i * FragmentSize, end = std::min(size, begin + FragmentSize);
                p.Data.assign(data + begin, data + end);
                m_Pending.push_back(std::move(p));
            }
            return true;
        }
        const uint16_t group = parts > 1 ? m_NextGroup++ : 0;
        for (size_t i = 0; i < parts; ++i)
        {
            Unreliable u;
            u.Fragment = parts > 1;
            u.Group = group;
            u.Index = uint16_t(i);
            u.Count = uint16_t(parts);
            const size_t begin = i * FragmentSize, end = std::min(size, begin + FragmentSize);
            u.Data.assign(data + begin, data + end);
            m_Outgoing.push_back(std::move(u));
        }
        return true;
    }

    double Connection::ResendDelay() const { return m_Rtt < 0 ? 0.2 : std::clamp(m_Rtt * 1.5 + 0.03, 0.05, 1.0); }

    void Connection::Count(double now, bool sent, size_t bytes)
    {
        if (sent)
        {
            ++m_Stats.PacketsSent;
            m_Stats.BytesSent += bytes;
            m_RateSent += bytes;
        }
        else
        {
            ++m_Stats.PacketsReceived;
            m_Stats.BytesReceived += bytes;
            m_RateReceived += bytes;
        }
        const double elapsed = now - m_RateStart;
        if (elapsed >= 1.0)
        {
            m_Stats.SentBytesPerSecond = double(m_RateSent) / elapsed;
            m_Stats.ReceivedBytesPerSecond = double(m_RateReceived) / elapsed;
            m_RateSent = m_RateReceived = 0;
            m_RateStart = now;
        }
    }

    bool Connection::WritePacket(double now, std::vector<uint8_t>& out)
    {
        Writer w;
        w.Data().reserve(MaxPacketSize);
        w.U16(PacketMagic);
        w.U8(uint8_t(PacketType::Data));
        w.U64(m_Key);
        w.U16(m_LocalSeq);
        uint32_t bits = 0;
        if (m_HaveRemote)
            for (uint16_t i = 0; i < 32; ++i)
            {
                const uint16_t seq = uint16_t(m_RemoteSeq - 1 - i);
                const auto& slot = m_Received[seq % RingSize];
                if (slot.Valid && slot.Seq == seq) bits |= 1u << i;
            }
        w.U16(m_HaveRemote ? m_RemoteSeq : 0);
        w.U32(bits);
        w.U8(m_HaveRemote ? HeaderHasAck : 0);

        SentPacket record;
        record.Seq = m_LocalSeq;
        bool included = false;
        auto room = [&](size_t chunk) { return w.Size() + chunk <= MaxPacketSize; };
        const double delay = ResendDelay();
        for (auto& p : m_Pending)
        {
            if (p.Acked || (p.LastSent >= 0 && now - p.LastSent < delay)) continue;
            const size_t chunk = 1 + 2 + (p.Fragment ? 4 : 0) + VarUSize(p.Data.size()) + p.Data.size();
            if (!room(chunk)) break; // keep the order: the rest goes in the next packet
            w.U8(uint8_t(ChunkReliable | (p.Fragment ? ChunkFragment : 0)));
            w.U16(p.Id);
            if (p.Fragment)
            {
                w.U16(p.Index);
                w.U16(p.Count);
            }
            w.VarU(p.Data.size());
            w.Bytes(p.Data.data(), p.Data.size());
            if (p.LastSent >= 0) ++m_Stats.ReliableResent;
            else ++m_Stats.ReliableSent;
            p.LastSent = now;
            record.Reliable.push_back(p.Id);
            included = true;
        }
        while (!m_Outgoing.empty())
        {
            const auto& u = m_Outgoing.front();
            const size_t chunk = 1 + (u.Fragment ? 6 : 0) + VarUSize(u.Data.size()) + u.Data.size();
            if (!room(chunk)) break;
            w.U8(u.Fragment ? ChunkFragment : 0);
            if (u.Fragment)
            {
                w.U16(u.Group);
                w.U16(u.Index);
                w.U16(u.Count);
            }
            w.VarU(u.Data.size());
            w.Bytes(u.Data.data(), u.Data.size());
            m_Outgoing.pop_front();
            included = true;
        }
        if (!included)
        {
            // Nothing to say: an ack the peer is owed (soon after its packets came), else a keep-alive now and then.
            const bool ack = m_AckOwed && (m_LastSent < 0 || now - m_LastSent >= 0.02);
            const bool keepAlive = m_LastSent < 0 || now - m_LastSent >= 0.25;
            if (!ack && !keepAlive) return false;
        }
        record.Valid = true;
        record.Time = now;
        m_Sent[m_LocalSeq % RingSize] = std::move(record);
        ++m_LocalSeq;
        m_AckOwed = false;
        m_LastSent = now;
        Count(now, true, w.Size());
        out = std::move(w.Data());
        return true;
    }

    void Connection::OnAck(double now, uint16_t seq)
    {
        auto& slot = m_Sent[seq % RingSize];
        if (!slot.Valid || slot.Seq != seq || slot.Acked) return;
        slot.Acked = true;
        if (!m_Pending.empty())
            for (uint16_t id : slot.Reliable)
            {
                const uint16_t offset = uint16_t(id - m_Pending.front().Id);
                if (offset < m_Pending.size() && m_Pending[offset].Id == id) m_Pending[offset].Acked = true;
            }
        while (!m_Pending.empty() && m_Pending.front().Acked) m_Pending.pop_front();
        const double sample = now - slot.Time;
        if (sample >= 0) m_Rtt = m_Rtt < 0 ? sample : m_Rtt * 0.9 + sample * 0.1;
    }

    void Connection::Classify(uint16_t latestAck)
    {
        // Packets 33 or more behind the latest ack can no longer be acked: their fate is known.
        const uint16_t known = uint16_t(latestAck - 33);
        if (!m_HaveLossChecked)
        {
            m_LossChecked = known;
            m_HaveLossChecked = true;
            return;
        }
        if (!SequenceNewer(known, m_LossChecked)) return;
        size_t steps = 0;
        while (m_LossChecked != known && steps++ < RingSize)
        {
            ++m_LossChecked;
            const auto& slot = m_Sent[m_LossChecked % RingSize];
            if (!slot.Valid || slot.Seq != m_LossChecked) continue;
            m_Fates.push_back(!slot.Acked);
            if (m_Fates.size() > 256) m_Fates.pop_front();
        }
        m_LossChecked = known;
    }

    void Connection::Deliver(InReliable&& chunk, std::vector<Message>& delivered)
    {
        if (!chunk.Fragment)
        {
            if (m_AssemblyCount) ++m_Stats.MalformedPackets; // a message inside a fragmented one: never written
            m_ReliableAssembly.clear();
            m_AssemblyCount = 0;
            ++m_Stats.ReliableDelivered;
            delivered.push_back({ std::move(chunk.Data), true });
            return;
        }
        if (chunk.Index == 0)
        {
            m_ReliableAssembly.clear();
            m_AssemblyCount = chunk.Count;
            m_AssemblyNext = 0;
        }
        if (!m_AssemblyCount || chunk.Count != m_AssemblyCount || chunk.Index != m_AssemblyNext ||
            m_ReliableAssembly.size() + chunk.Data.size() > MaxMessageSize)
        {
            ++m_Stats.MalformedPackets;
            m_ReliableAssembly.clear();
            m_AssemblyCount = 0;
            return;
        }
        m_ReliableAssembly.insert(m_ReliableAssembly.end(), chunk.Data.begin(), chunk.Data.end());
        ++m_AssemblyNext;
        if (m_AssemblyNext == m_AssemblyCount)
        {
            ++m_Stats.ReliableDelivered;
            delivered.push_back({ std::move(m_ReliableAssembly), true });
            m_ReliableAssembly = {};
            m_AssemblyCount = 0;
        }
    }

    bool Connection::ReadPacket(double now, const uint8_t* data, size_t size, std::vector<Message>& delivered)
    {
        Reader r(data, size);
        const uint16_t magic = r.U16();
        const uint8_t type = r.U8();
        const uint64_t key = r.U64();
        const uint16_t seq = r.U16(), ack = r.U16();
        const uint32_t bits = r.U32();
        const uint8_t flags = r.U8();
        if (!r.Ok() || magic != PacketMagic || type != uint8_t(PacketType::Data) || (flags & ~HeaderHasAck) || size > MaxPacketSize)
        {
            ++m_Stats.MalformedPackets;
            return false;
        }
        if (key != m_Key) return false; // another session's packet (or a forged one): not ours
        if (m_HaveRemote && !SequenceNewer(seq, m_RemoteSeq) && uint16_t(m_RemoteSeq - seq) >= RingSize)
        {
            ++m_Stats.StalePackets;
            return false;
        }
        auto& slot = m_Received[seq % RingSize];
        if (slot.Valid && slot.Seq == seq)
        {
            ++m_Stats.DuplicatePackets;
            return false;
        }
        // The whole packet is checked before any of it is used.
        struct Chunk
        {
            bool Reliable, Fragment;
            uint16_t Id, Group, Index, Count;
            const uint8_t* Bytes;
            size_t Size;
        };
        Chunk chunks[64];
        size_t count = 0;
        while (r.Ok() && !r.AtEnd())
        {
            Chunk c{};
            const uint8_t chunkFlags = r.U8();
            if (chunkFlags & ~(ChunkReliable | ChunkFragment) || count == 64)
            {
                r.Fail();
                break;
            }
            c.Reliable = chunkFlags & ChunkReliable;
            c.Fragment = chunkFlags & ChunkFragment;
            if (c.Reliable) c.Id = r.U16();
            if (c.Fragment)
            {
                if (!c.Reliable) c.Group = r.U16();
                c.Index = r.U16();
                c.Count = r.U16();
                if (c.Count < 2 || c.Count > MaxFragments || c.Index >= c.Count) r.Fail();
            }
            c.Size = size_t(r.VarU(32));
            if (c.Size == 0 || c.Size > FragmentSize) r.Fail();
            c.Bytes = r.Cursor();
            r.Skip(c.Size);
            if (r.Ok()) chunks[count++] = c;
        }
        if (!r.Ok())
        {
            ++m_Stats.MalformedPackets;
            return false;
        }
        slot = { seq, true };
        if (!m_HaveRemote || SequenceNewer(seq, m_RemoteSeq)) m_RemoteSeq = seq;
        m_HaveRemote = true;
        m_AckOwed = true;
        m_LastReceived = now;
        Count(now, false, size);
        if (flags & HeaderHasAck)
        {
            OnAck(now, ack);
            for (uint16_t i = 0; i < 32; ++i)
                if (bits & (1u << i)) OnAck(now, uint16_t(ack - 1 - i));
            Classify(ack);
        }
        for (size_t i = 0; i < count; ++i)
        {
            const Chunk& c = chunks[i];
            if (c.Reliable)
            {
                const uint16_t ahead = uint16_t(c.Id - m_NextDeliver);
                if (ahead >= ReceiveWindow) continue; // already delivered (a resend), or far outside the window
                m_InBuffer.try_emplace(c.Id, InReliable{ c.Fragment, c.Index, c.Count, std::vector<uint8_t>(c.Bytes, c.Bytes + c.Size) });
                continue;
            }
            if (!c.Fragment)
            {
                ++m_Stats.UnreliableDelivered;
                delivered.push_back({ std::vector<uint8_t>(c.Bytes, c.Bytes + c.Size), false });
                continue;
            }
            auto found = m_Assemblies.find(c.Group);
            if (found == m_Assemblies.end())
            {
                if (m_Assemblies.size() >= MaxAssemblies)
                {
                    auto oldest = std::min_element(m_Assemblies.begin(), m_Assemblies.end(), [](const auto& a, const auto& b) { return a.second.Started < b.second.Started; });
                    m_Assemblies.erase(oldest);
                    ++m_Stats.FragmentsExpired;
                }
                Assembly a;
                a.Count = c.Count;
                a.Started = now;
                a.Parts.resize(c.Count);
                found = m_Assemblies.emplace(c.Group, std::move(a)).first;
            }
            Assembly& a = found->second;
            if (a.Count != c.Count || !a.Parts[c.Index].empty()) continue;
            a.Parts[c.Index].assign(c.Bytes, c.Bytes + c.Size);
            if (++a.Have == a.Count)
            {
                std::vector<uint8_t> whole;
                for (auto& part : a.Parts) whole.insert(whole.end(), part.begin(), part.end());
                m_Assemblies.erase(found);
                ++m_Stats.UnreliableDelivered;
                delivered.push_back({ std::move(whole), false });
            }
        }
        for (auto it = m_InBuffer.find(m_NextDeliver); it != m_InBuffer.end(); it = m_InBuffer.find(m_NextDeliver))
        {
            Deliver(std::move(it->second), delivered);
            m_InBuffer.erase(it);
            ++m_NextDeliver;
        }
        for (auto it = m_Assemblies.begin(); it != m_Assemblies.end();)
            if (now - it->second.Started > AssemblyTimeout)
            {
                ++m_Stats.FragmentsExpired;
                it = m_Assemblies.erase(it);
            }
            else ++it;
        return true;
    }

    ConnectionStats Connection::GetStats() const
    {
        ConnectionStats s = m_Stats;
        s.RttMs = m_Rtt < 0 ? 0 : m_Rtt * 1000.0;
        if (!m_Fates.empty()) s.LossPercent = 100.0 * double(std::count(m_Fates.begin(), m_Fates.end(), true)) / double(m_Fates.size());
        s.PendingReliable = m_Pending.size();
        return s;
    }

}
