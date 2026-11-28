#pragma once

/// @file
/// @brief Datagram sockets for the network layer: UDP over POSIX sockets (non-blocking), an in-process loopback
/// network for tests and tools, and a link conditioner that adds latency, jitter, loss, duplication and reordering
/// to any socket. Everything here is polled from one thread; nothing blocks.

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace Zamin::Net {

    /// The largest datagram the layer sends or accepts (bytes): below a typical internet path MTU.
    constexpr size_t MaxPacketSize = 1200;

    /// Seconds on a steady clock. Tests replace it (SetTimeSource) to run time forward without waiting.
    double Now();
    void SetTimeSource(std::function<double()> source); ///< empty: the real steady clock again

    /// An IPv4 address and port (host byte order).
    struct Address
    {
        uint32_t Ip = 0;
        uint16_t Port = 0;

        bool IsValid() const { return Port != 0; }
        bool operator==(const Address&) const = default;
        bool operator<(const Address& o) const { return Ip != o.Ip ? Ip < o.Ip : Port < o.Port; }
        /// "127.0.0.1:7777"
        std::string ToString() const;
        static Address Loopback(uint16_t port) { return { 0x7F000001u, port }; }
        /// "host:port" (a dotted IPv4 address or a name resolved through getaddrinfo, e.g. localhost); false with a
        /// reason otherwise.
        static bool Parse(std::string_view text, Address& out, std::string* error = nullptr);
    };

    /// A non-blocking datagram endpoint.
    class Socket
    {
    public:
        virtual ~Socket() = default;
        /// Sends one datagram (at most MaxPacketSize bytes); false when it could not be handed to the network.
        virtual bool Send(const Address& to, const uint8_t* data, size_t size) = 0;
        /// The next datagram that has arrived, if any (never waits). Oversized datagrams are dropped.
        virtual bool Receive(Address& from, std::vector<uint8_t>& data) = 0;
        virtual Address GetLocalAddress() const = 0;
    };

    /// UDP over a POSIX socket (O_NONBLOCK, SO_REUSEADDR).
    class UdpSocket final : public Socket
    {
    public:
        /// Binds to `port` (0: any free port) on every interface, or only 127.0.0.1 with loopbackOnly. Null with a
        /// reason when the port is taken or the socket cannot be made.
        static std::unique_ptr<UdpSocket> Open(uint16_t port, bool loopbackOnly = false, std::string* error = nullptr);
        ~UdpSocket() override;
        bool Send(const Address& to, const uint8_t* data, size_t size) override;
        bool Receive(Address& from, std::vector<uint8_t>& data) override;
        Address GetLocalAddress() const override { return m_Local; }

    private:
        UdpSocket() = default;
        int m_Fd = -1;
        Address m_Local;
    };

    /// An in-process network: sockets opened on it reach each other by port (address 127.0.0.1), with no system
    /// sockets. One thread uses it (tests, the benchmark, in-process clients).
    class LoopbackNetwork : public std::enable_shared_from_this<LoopbackNetwork>
    {
    public:
        static std::shared_ptr<LoopbackNetwork> Create() { return std::shared_ptr<LoopbackNetwork>(new LoopbackNetwork()); }
        /// A socket on `port` (0: the next free one); null when the port is taken.
        std::unique_ptr<Socket> Open(uint16_t port = 0);
        /// Datagrams waiting for a port (tests).
        size_t Pending(uint16_t port) const;
        /// Every datagram from then on is dropped while blocked (a cable pulled out).
        void SetBlocked(uint16_t port, bool blocked);

    private:
        LoopbackNetwork() = default;
        friend class LoopbackSocket;
        struct Datagram
        {
            Address From;
            std::vector<uint8_t> Data;
        };
        struct Port
        {
            std::deque<Datagram> Queue;
            bool Open = false, Blocked = false;
        };
        std::map<uint16_t, Port> m_Ports;
        uint16_t m_Next = 40000;
    };

    /// What a link conditioner does to the packets that go through it.
    struct ConditionerSettings
    {
        bool Enabled = false;
        float LatencyMs = 0.0f;       ///< one-way delay added to every packet
        float JitterMs = 0.0f;        ///< +- uniformly random on top of the latency
        float LossPercent = 0.0f;     ///< packets dropped (0..100)
        float DuplicatePercent = 0.0f;///< packets sent twice (the copy a little later)
        float ReorderPercent = 0.0f;  ///< packets held back for an extra latency (arrive after later ones)
        uint32_t Seed = 1;            ///< the random sequence (tests are repeatable)
        bool operator==(const ConditionerSettings&) const = default;
        /// "100ms+-20 5% loss" or "off".
        std::string Describe() const;
        /// "latency=100,jitter=20,loss=5,dup=1,reorder=2" (any subset, any order); false with a reason otherwise.
        static bool Parse(std::string_view text, ConditionerSettings& out, std::string* error = nullptr);
    };

    /// Wraps a socket: what is sent through it is delayed / dropped / duplicated / reordered as the settings say
    /// (the outgoing direction; condition both ends for both directions). Receive passes straight through.
    class LinkConditioner final : public Socket
    {
    public:
        LinkConditioner(std::unique_ptr<Socket> inner, const ConditionerSettings& settings);
        bool Send(const Address& to, const uint8_t* data, size_t size) override;
        bool Receive(Address& from, std::vector<uint8_t>& data) override;
        Address GetLocalAddress() const override { return m_Inner->GetLocalAddress(); }
        void SetSettings(const ConditionerSettings& settings);
        const ConditionerSettings& GetSettings() const { return m_Settings; }
        /// Hands the packets whose time has come to the inner socket (Send and Receive call it too).
        void Pump();
        size_t Delayed() const { return m_Queue.size(); }
        uint64_t Dropped() const { return m_Dropped; }
        uint64_t Duplicated() const { return m_Duplicated; }
        Socket& Inner() { return *m_Inner; }

    private:
        struct Held
        {
            double Due = 0;
            uint64_t Order = 0;
            Address To;
            std::vector<uint8_t> Data;
        };
        void Hold(const Address& to, const uint8_t* data, size_t size, double delaySeconds);
        std::unique_ptr<Socket> m_Inner;
        ConditionerSettings m_Settings;
        std::mt19937 m_Random;
        std::vector<Held> m_Queue;
        uint64_t m_Order = 0, m_Dropped = 0, m_Duplicated = 0;
    };

}
