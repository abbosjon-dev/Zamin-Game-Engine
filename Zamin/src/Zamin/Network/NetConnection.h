#pragma once

/// @file
/// @brief One end of a connection: packet sequence numbers and acks (the latest received sequence plus a 32-bit
/// field of the ones before it), round-trip time and loss estimates, a reliable-ordered channel (message ids,
/// resends until acked, in-order delivery exactly once), an unreliable channel, fragmentation of large messages
/// (both channels, up to MaxMessageSize) and traffic statistics. It knows nothing of sockets: the transport feeds
/// it the data packets of its peer and sends what it writes.

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <vector>

namespace Zamin::Net {

    /// Every datagram starts with this (and the packet type): anything else is dropped at once.
    constexpr uint16_t PacketMagic = 0x4D5A; // "ZM"
    enum class PacketType : uint8_t
    {
        ConnectRequest = 1, ///< client -> server: versions, project, nonce, resume token (padded, see Transport)
        Challenge,          ///< server -> client: a stateless token the client must echo
        ChallengeResponse,  ///< client -> server: the token back with the request's data
        Accept,             ///< server -> client: the client id and the session's key and resume token
        Deny,               ///< server -> client: why not (DisconnectReason)
        Data,               ///< either way: acks and messages (Connection)
        Disconnect,         ///< either way: a graceful goodbye (sent three times)
    };

    /// A delivered message.
    struct Message
    {
        std::vector<uint8_t> Data;
        bool Reliable = false;
    };

    /// Traffic and quality of one connection.
    struct ConnectionStats
    {
        double RttMs = 0;              ///< smoothed round-trip time
        double LossPercent = 0;        ///< of our packets over the last ~256 whose fate is known
        double SentBytesPerSecond = 0, ReceivedBytesPerSecond = 0; ///< the last full second
        uint64_t PacketsSent = 0, PacketsReceived = 0, BytesSent = 0, BytesReceived = 0;
        uint64_t ReliableSent = 0, ReliableResent = 0, ReliableDelivered = 0, UnreliableDelivered = 0;
        uint64_t DuplicatePackets = 0, StalePackets = 0, MalformedPackets = 0, FragmentsExpired = 0;
        size_t PendingReliable = 0;    ///< reliable messages (fragments) not acked yet
    };

    class Connection
    {
    public:
        /// The largest message either channel takes (bytes): larger ones are refused by Send.
        static constexpr size_t MaxMessageSize = 256 * 1024;
        /// A fragment's payload (a message above this is split).
        static constexpr size_t FragmentSize = 1000;
        /// Reliable messages (fragments counted one each) that may wait for their ack at once.
        static constexpr size_t MaxPendingReliable = 1024;

        /// key: the session key every data packet of this connection carries (packets with another are not ours).
        Connection(uint64_t key, double now);

        /// Queues a message. False when it is empty, larger than MaxMessageSize, or the reliable queue is full.
        bool Send(const std::vector<uint8_t>& message, bool reliable);
        bool Send(const uint8_t* data, size_t size, bool reliable);
        /// The next data packet to send now (header and messages) into `out`; false when there is nothing to send
        /// (no message, no ack owed, no keep-alive due). Call it until it returns false.
        bool WritePacket(double now, std::vector<uint8_t>& out);
        /// A data packet from the peer (the whole datagram). Delivered messages are appended (reliable ones in
        /// order, exactly once). False for a malformed packet, another session's key, a duplicate or a stale one.
        bool ReadPacket(double now, const uint8_t* data, size_t size, std::vector<Message>& delivered);

        uint64_t GetKey() const { return m_Key; }
        double LastReceived() const { return m_LastReceived; }
        double LastSent() const { return m_LastSent; }
        ConnectionStats GetStats() const;
        /// Every reliable message queued so far has been acked by the peer.
        bool AllReliableAcked() const { return m_Pending.empty(); }

    private:
        struct Pending
        {
            uint16_t Id = 0;
            bool Fragment = false;
            uint16_t Index = 0, Count = 0;
            std::vector<uint8_t> Data;
            double LastSent = -1;
            bool Acked = false;
        };
        struct Unreliable
        {
            bool Fragment = false;
            uint16_t Group = 0, Index = 0, Count = 0;
            std::vector<uint8_t> Data;
        };
        struct SentPacket
        {
            uint16_t Seq = 0;
            bool Valid = false, Acked = false;
            double Time = 0;
            std::vector<uint16_t> Reliable;
        };
        struct Received
        {
            uint16_t Seq = 0;
            bool Valid = false;
        };
        struct InReliable
        {
            bool Fragment = false;
            uint16_t Index = 0, Count = 0;
            std::vector<uint8_t> Data;
        };
        struct Assembly
        {
            uint16_t Count = 0, Have = 0;
            double Started = 0;
            std::vector<std::vector<uint8_t>> Parts;
        };

        void OnAck(double now, uint16_t seq);
        void Classify(uint16_t latestAck);
        void Deliver(InReliable&& chunk, std::vector<Message>& delivered);
        void Count(double now, bool sent, size_t bytes);
        double ResendDelay() const;

        uint64_t m_Key;
        uint16_t m_LocalSeq = 0;          // next packet sequence
        uint16_t m_RemoteSeq = 0;         // latest received sequence
        bool m_HaveRemote = false;
        bool m_AckOwed = false;
        double m_LastReceived, m_LastSent = -1;
        std::vector<SentPacket> m_Sent;   // ring by seq
        std::vector<Received> m_Received; // ring by seq
        uint16_t m_LossChecked = 0;
        bool m_HaveLossChecked = false;
        std::deque<bool> m_Fates;         // last ~256 classified packets: true = lost
        // Reliable channel.
        uint16_t m_NextReliableId = 0;
        std::deque<Pending> m_Pending;
        uint16_t m_NextDeliver = 0;
        std::map<uint16_t, InReliable> m_InBuffer;
        std::vector<uint8_t> m_ReliableAssembly;
        uint16_t m_AssemblyNext = 0, m_AssemblyCount = 0;
        // Unreliable channel.
        std::deque<Unreliable> m_Outgoing;
        uint16_t m_NextGroup = 0;
        std::map<uint16_t, Assembly> m_Assemblies;
        // Stats.
        ConnectionStats m_Stats;
        double m_Rtt = -1;
        double m_RateStart = 0;
        uint64_t m_RateSent = 0, m_RateReceived = 0;
    };

    /// 16-bit sequence comparison with wraparound: a is newer than b.
    inline bool SequenceNewer(uint16_t a, uint16_t b) { return a != b && uint16_t(a - b) < 32768; }

}
