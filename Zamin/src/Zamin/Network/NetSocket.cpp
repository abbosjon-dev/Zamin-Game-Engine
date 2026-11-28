#include "Zamin/Network/NetSocket.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstring>
#include <format>

namespace Zamin::Net {

    namespace {
        std::function<double()>& TimeSource()
        {
            static std::function<double()> source;
            return source;
        }
    }

    double Now()
    {
        if (auto& source = TimeSource()) return source();
        using Clock = std::chrono::steady_clock;
        static const Clock::time_point start = Clock::now();
        return std::chrono::duration<double>(Clock::now() - start).count();
    }

    void SetTimeSource(std::function<double()> source) { TimeSource() = std::move(source); }

    // ------------------------------------------------------------------ Address
    std::string Address::ToString() const
    {
        return std::format("{}.{}.{}.{}:{}", (Ip >> 24) & 255, (Ip >> 16) & 255, (Ip >> 8) & 255, Ip & 255, Port);
    }

    bool Address::Parse(std::string_view text, Address& out, std::string* error)
    {
        auto fail = [&](std::string why) {
            if (error) *error = std::move(why);
            return false;
        };
        const size_t colon = text.rfind(':');
        if (colon == std::string_view::npos || colon == 0 || colon + 1 >= text.size()) return fail("expected host:port");
        const std::string_view host = text.substr(0, colon), portText = text.substr(colon + 1);
        unsigned port = 0;
        if (auto [ptr, ec] = std::from_chars(portText.data(), portText.data() + portText.size(), port);
            ec != std::errc() || ptr != portText.data() + portText.size() || port == 0 || port > 65535)
            return fail("the port must be 1..65535");
        if (host.size() > 253) return fail("the host name is too long");
        const std::string name(host);
        in_addr parsed{};
        if (inet_pton(AF_INET, name.c_str(), &parsed) == 1)
        {
            out = { ntohl(parsed.s_addr), uint16_t(port) };
            return true;
        }
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        addrinfo* result = nullptr;
        if (getaddrinfo(name.c_str(), nullptr, &hints, &result) != 0 || !result) return fail("the host could not be resolved: " + name);
        const auto* in = reinterpret_cast<const sockaddr_in*>(result->ai_addr);
        out = { ntohl(in->sin_addr.s_addr), uint16_t(port) };
        freeaddrinfo(result);
        return true;
    }

    // ------------------------------------------------------------------ UDP
    std::unique_ptr<UdpSocket> UdpSocket::Open(uint16_t port, bool loopbackOnly, std::string* error)
    {
        auto fail = [&](const char* what) -> std::unique_ptr<UdpSocket> {
            if (error) *error = std::format("{}: {}", what, std::strerror(errno));
            return nullptr;
        };
        const int fd = ::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (fd < 0) return fail("socket");
        std::unique_ptr<UdpSocket> s(new UdpSocket());
        s->m_Fd = fd;
        const int yes = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        // Room for a burst of snapshots to every client.
        const int buffer = 1 << 20;
        ::setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &buffer, sizeof(buffer));
        ::setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &buffer, sizeof(buffer));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = htonl(loopbackOnly ? INADDR_LOOPBACK : INADDR_ANY);
        if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) return fail(std::format("bind to port {}", port).c_str());
        socklen_t length = sizeof(addr);
        if (::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &length) != 0) return fail("getsockname");
        s->m_Local = { loopbackOnly ? 0x7F000001u : 0u, ntohs(addr.sin_port) };
        return s;
    }

    UdpSocket::~UdpSocket()
    {
        if (m_Fd >= 0) ::close(m_Fd);
    }

    bool UdpSocket::Send(const Address& to, const uint8_t* data, size_t size)
    {
        if (m_Fd < 0 || size > MaxPacketSize || !to.IsValid()) return false;
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(to.Port);
        addr.sin_addr.s_addr = htonl(to.Ip);
        return ::sendto(m_Fd, data, size, MSG_NOSIGNAL, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == ssize_t(size);
    }

    bool UdpSocket::Receive(Address& from, std::vector<uint8_t>& data)
    {
        if (m_Fd < 0) return false;
        uint8_t buffer[MaxPacketSize + 1];
        for (;;)
        {
            sockaddr_in addr{};
            socklen_t length = sizeof(addr);
            // MSG_TRUNC: the real size of a datagram larger than the buffer, which is dropped.
            const ssize_t got = ::recvfrom(m_Fd, buffer, sizeof(buffer), MSG_TRUNC, reinterpret_cast<sockaddr*>(&addr), &length);
            if (got < 0) return false; // EAGAIN: nothing more (or an ICMP error for an earlier send)
            if (size_t(got) > MaxPacketSize || length < sizeof(sockaddr_in)) continue;
            from = { ntohl(addr.sin_addr.s_addr), ntohs(addr.sin_port) };
            data.assign(buffer, buffer + got);
            return true;
        }
    }

    // ------------------------------------------------------------------ loopback
    class LoopbackSocket final : public Socket
    {
    public:
        LoopbackSocket(std::shared_ptr<LoopbackNetwork> network, uint16_t port) : m_Network(std::move(network)), m_Port(port) {}
        ~LoopbackSocket() override
        {
            auto& port = m_Network->m_Ports[m_Port];
            port.Open = false;
            port.Queue.clear();
        }
        bool Send(const Address& to, const uint8_t* data, size_t size) override
        {
            if (size > MaxPacketSize) return false;
            if (m_Network->m_Ports[m_Port].Blocked) return true; // gone into the void
            auto found = m_Network->m_Ports.find(to.Port);
            if (found == m_Network->m_Ports.end() || !found->second.Open || found->second.Blocked) return true;
            if (found->second.Queue.size() >= 8192) return false; // a full receive buffer drops
            found->second.Queue.push_back({ GetLocalAddress(), std::vector<uint8_t>(data, data + size) });
            return true;
        }
        bool Receive(Address& from, std::vector<uint8_t>& data) override
        {
            auto& port = m_Network->m_Ports[m_Port];
            if (port.Queue.empty()) return false;
            from = port.Queue.front().From;
            data = std::move(port.Queue.front().Data);
            port.Queue.pop_front();
            return true;
        }
        Address GetLocalAddress() const override { return Address::Loopback(m_Port); }

    private:
        std::shared_ptr<LoopbackNetwork> m_Network;
        uint16_t m_Port;
    };

    std::unique_ptr<Socket> LoopbackNetwork::Open(uint16_t port)
    {
        if (port == 0)
        {
            for (int tries = 0; tries < 65535; ++tries)
            {
                const uint16_t candidate = m_Next++;
                if (m_Next == 0) m_Next = 40000;
                if (candidate != 0 && !m_Ports[candidate].Open)
                {
                    port = candidate;
                    break;
                }
            }
            if (port == 0) return nullptr;
        }
        auto& slot = m_Ports[port];
        if (slot.Open) return nullptr;
        slot.Open = true;
        slot.Queue.clear();
        return std::make_unique<LoopbackSocket>(shared_from_this(), port);
    }

    size_t LoopbackNetwork::Pending(uint16_t port) const
    {
        auto found = m_Ports.find(port);
        return found == m_Ports.end() ? 0 : found->second.Queue.size();
    }

    void LoopbackNetwork::SetBlocked(uint16_t port, bool blocked) { m_Ports[port].Blocked = blocked; }

    // ------------------------------------------------------------------ conditioner
    std::string ConditionerSettings::Describe() const
    {
        if (!Enabled) return "off";
        std::string text = std::format("{:.0f} ms +-{:.0f} ms, {:.1f}% loss", LatencyMs, JitterMs, LossPercent);
        if (DuplicatePercent > 0) text += std::format(", {:.1f}% duplicated", DuplicatePercent);
        if (ReorderPercent > 0) text += std::format(", {:.1f}% reordered", ReorderPercent);
        return text;
    }

    bool ConditionerSettings::Parse(std::string_view text, ConditionerSettings& out, std::string* error)
    {
        ConditionerSettings s;
        s.Enabled = true;
        if (text == "off" || text.empty())
        {
            out = {};
            return true;
        }
        while (!text.empty())
        {
            const size_t comma = text.find(',');
            const std::string_view item = text.substr(0, comma);
            text = comma == std::string_view::npos ? std::string_view() : text.substr(comma + 1);
            const size_t eq = item.find('=');
            if (eq == std::string_view::npos)
            {
                if (error) *error = "expected name=value: " + std::string(item);
                return false;
            }
            const std::string_view key = item.substr(0, eq), value = item.substr(eq + 1);
            float v = 0;
            if (auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), v); ec != std::errc() || ptr != value.data() + value.size() || v < 0 ||
                                                                                               v > (key == "seed" ? 2.0e9f : key == "latency" || key == "jitter" ? 5000.0f : 100.0f))
            {
                if (error) *error = "a value out of range: " + std::string(item);
                return false;
            }
            if (key == "latency") s.LatencyMs = v;
            else if (key == "jitter") s.JitterMs = v;
            else if (key == "loss") s.LossPercent = v;
            else if (key == "dup" || key == "duplicate") s.DuplicatePercent = v;
            else if (key == "reorder") s.ReorderPercent = v;
            else if (key == "seed") s.Seed = uint32_t(v);
            else
            {
                if (error) *error = "unknown setting: " + std::string(key) + " (latency, jitter, loss, dup, reorder, seed)";
                return false;
            }
        }
        out = s;
        return true;
    }

    LinkConditioner::LinkConditioner(std::unique_ptr<Socket> inner, const ConditionerSettings& settings)
        : m_Inner(std::move(inner)), m_Settings(settings), m_Random(settings.Seed)
    {
    }

    void LinkConditioner::SetSettings(const ConditionerSettings& settings)
    {
        if (settings.Seed != m_Settings.Seed) m_Random.seed(settings.Seed);
        m_Settings = settings;
    }

    void LinkConditioner::Hold(const Address& to, const uint8_t* data, size_t size, double delaySeconds)
    {
        m_Queue.push_back({ Now() + std::max(0.0, delaySeconds), m_Order++, to, std::vector<uint8_t>(data, data + size) });
    }

    bool LinkConditioner::Send(const Address& to, const uint8_t* data, size_t size)
    {
        Pump();
        if (!m_Settings.Enabled) return m_Inner->Send(to, data, size);
        std::uniform_real_distribution<float> percent(0.0f, 100.0f), unit(-1.0f, 1.0f);
        if (percent(m_Random) < m_Settings.LossPercent)
        {
            ++m_Dropped;
            return true;
        }
        auto delay = [&] {
            double ms = m_Settings.LatencyMs + m_Settings.JitterMs * unit(m_Random);
            if (percent(m_Random) < m_Settings.ReorderPercent) ms += std::max(20.0f, m_Settings.LatencyMs);
            return std::max(0.0, ms) / 1000.0;
        };
        Hold(to, data, size, delay());
        if (percent(m_Random) < m_Settings.DuplicatePercent)
        {
            ++m_Duplicated;
            Hold(to, data, size, delay());
        }
        Pump();
        return true;
    }

    bool LinkConditioner::Receive(Address& from, std::vector<uint8_t>& data)
    {
        Pump();
        return m_Inner->Receive(from, data);
    }

    void LinkConditioner::Pump()
    {
        if (m_Queue.empty()) return;
        const double now = Now();
        // Due packets in due order (equal times keep the send order).
        std::sort(m_Queue.begin(), m_Queue.end(), [](const Held& a, const Held& b) { return a.Due != b.Due ? a.Due < b.Due : a.Order < b.Order; });
        size_t sent = 0;
        while (sent < m_Queue.size() && m_Queue[sent].Due <= now)
        {
            m_Inner->Send(m_Queue[sent].To, m_Queue[sent].Data.data(), m_Queue[sent].Data.size());
            ++sent;
        }
        m_Queue.erase(m_Queue.begin(), m_Queue.begin() + ptrdiff_t(sent));
    }

}
