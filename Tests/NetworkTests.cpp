// The networking layer (Network/, docs/Networking.md): the codec's bounds (and a fuzz of every decoder), the
// reliable channel under a hostile link conditioner, fragmentation, the handshake's rejections, rate limits,
// reconnect with session resume, and the session on top: snapshot replication with delta compression and
// interest, prediction and reconciliation, RPCs (C++ and graph) with their validation, replicated graph variables
// and the graph format 7 migration. Everything runs in-process over a LoopbackNetwork on a manual clock.
#include "TestAssertions.h"
#include "Zamin/Network/Net.h"
#include "Zamin/Network/NetCodec.h"
#include "Zamin/Network/NetConnection.h"
#include "Zamin/Network/NetSession.h"
#include "Zamin/Network/NetSocket.h"
#include "Zamin/Network/NetTransport.h"
#include "Zamin/Project/Project.h"
#include "Zamin/Scene/Components.h"
#include "Zamin/Scene/Scene.h"
#include "Zamin/Scripting/ScriptEngine.h"
#include "Zamin/Scripting/ScriptableEntity.h"
#include "Zamin/Scripting/Visual/VisualGraph.h"
#include "Zamin/Scripting/Visual/VisualGraphBytecode.h"
#include "Zamin/Scripting/Visual/VisualGraphHost.h"
#include "Zamin/Utils/FileSystem.h"

#include <filesystem>
#include <memory>
#include <random>

using namespace Zamin;
using namespace Zamin::Net;

namespace {
    // A manual clock for the transport (Net::Now) for the length of a test.
    struct Clock
    {
        double T = 1000.0;
        Clock() { SetTimeSource([this] { return T; }); }
        ~Clock() { SetTimeSource({}); }
    };

    TransportSettings Settings(uint32_t maxClients = 8)
    {
        TransportSettings s;
        s.ProjectId = 0x5A4D;
        s.MaxClients = maxClients;
        s.TimeoutSeconds = 2.0;
        s.HandshakeSeconds = 1.0;
        s.ResumeSeconds = 5.0;
        return s;
    }

    std::vector<uint8_t> Bytes(size_t size, uint32_t seed)
    {
        std::vector<uint8_t> v(size);
        for (size_t i = 0; i < size; ++i) v[i] = uint8_t((i * 131 + seed * 17) ^ (seed >> 3));
        // The message's id in its first bytes (order checks).
        for (size_t i = 0; i < 4 && i < size; ++i) v[i] = uint8_t(seed >> (8 * i));
        return v;
    }
    uint32_t IdOf(const std::vector<uint8_t>& v) { return v.size() < 4 ? 0 : uint32_t(v[0]) | uint32_t(v[1]) << 8 | uint32_t(v[2]) << 16 | uint32_t(v[3]) << 24; }

    // A server and clients on one loopback network, run frame by frame.
    struct Link
    {
        Clock clock;
        std::shared_ptr<LoopbackNetwork> net = LoopbackNetwork::Create();
        ServerTransport server;
        std::vector<std::unique_ptr<ClientTransport>> clients;
        std::vector<TransportEvent> serverEvents;
        std::vector<std::vector<TransportEvent>> clientEvents;
        explicit Link(const TransportSettings& s = Settings(), const ConditionerSettings& c = {})
        {
            std::unique_ptr<Socket> socket = net->Open(7000);
            if (c.Enabled) socket = std::make_unique<LinkConditioner>(std::move(socket), c);
            server.Start(std::move(socket), s);
        }
        ClientTransport& Add(const TransportSettings& s = Settings(), const ConditionerSettings& c = {}, uint32_t resumeId = 0, uint64_t token = 0)
        {
            std::unique_ptr<Socket> socket = net->Open();
            if (c.Enabled) socket = std::make_unique<LinkConditioner>(std::move(socket), c);
            clients.push_back(std::make_unique<ClientTransport>());
            clientEvents.emplace_back();
            clients.back()->Connect(std::move(socket), Address::Loopback(7000), s, resumeId, token);
            return *clients.back();
        }
        void Step(int frames = 1, double dt = 1.0 / 60.0)
        {
            for (int f = 0; f < frames; ++f)
            {
                clock.T += dt;
                for (size_t i = 0; i < clients.size(); ++i)
                {
                    clients[i]->Update(clock.T);
                    for (TransportEvent e; clients[i]->Poll(e);) clientEvents[i].push_back(std::move(e));
                    clients[i]->Flush(clock.T);
                }
                server.Update(clock.T);
                for (TransportEvent e; server.Poll(e);) serverEvents.push_back(std::move(e));
                server.Flush(clock.T);
            }
        }
        size_t Count(TransportEvent::Kind kind) const
        {
            return size_t(std::count_if(serverEvents.begin(), serverEvents.end(), [&](const TransportEvent& e) { return e.Type == kind; }));
        }
    };
}

// ------------------------------------------------------------------ codec
TEST(NetCodecRoundTripsAndRefusesWhatIsOutOfBounds)
{
    Writer w;
    w.U8(7);
    w.U16(0xBEEF);
    w.U32(0xDEADBEEF);
    w.U64(0x0123456789ABCDEFull);
    w.VarU(0);
    w.VarU(127);
    w.VarU(128);
    w.VarU(UINT64_MAX);
    w.VarI(-1);
    w.VarI(INT64_MIN);
    w.F32(1.5f);
    w.F64(-2.25);
    w.String("hello");
    Reader r(w.Data());
    CHECK(r.U8() == 7 && r.U16() == 0xBEEF && r.U32() == 0xDEADBEEF && r.U64() == 0x0123456789ABCDEFull);
    CHECK(r.VarU() == 0 && r.VarU() == 127 && r.VarU() == 128 && r.VarU() == UINT64_MAX);
    CHECK(r.VarI() == -1 && r.VarI() == INT64_MIN && r.F32() == 1.5f && r.F64() == -2.25 && r.String(16) == "hello" && r.AtEnd());
    // Short buffers, over-long strings and varints, non-finite floats: the reader fails and stays failed.
    Reader shortRead(w.Data().data(), 2);
    CHECK(shortRead.U8() == 7 && shortRead.U16() == 0 && !shortRead.Ok() && shortRead.U8() == 0);
    Writer s;
    s.String("too long");
    Reader limited(s.Data());
    CHECK(limited.String(4).empty() && !limited.Ok());
    const uint8_t overlong[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01 };
    Reader v(overlong, sizeof(overlong));
    v.VarU();
    CHECK(!v.Ok());
    Writer big;
    big.VarU(1ull << 40);
    Reader narrow(big.Data());
    narrow.VarU(32);
    CHECK(!narrow.Ok());
    Writer nan;
    nan.F32(std::numeric_limits<float>::quiet_NaN());
    Reader nanRead(nan.Data());
    nanRead.F32();
    CHECK(!nanRead.Ok());
    // Script values: every kind round-trips; limits hold both ways.
    nlohmann::json value = { { "n", 3 }, { "d", 2.5 }, { "s", "x" }, { "b", true }, { "z", nullptr }, { "a", { 1, 2, { { "k", -4 } } } } };
    Writer vw;
    CHECK(EncodeValue(vw, value));
    Reader vr(vw.Data());
    nlohmann::json back;
    CHECK(DecodeValue(vr, back) && back == value && vr.AtEnd());
    nlohmann::json deep = 1;
    for (int i = 0; i < 12; ++i) deep = nlohmann::json::array({ deep });
    Writer dw;
    std::string why;
    CHECK(!EncodeValue(dw, deep, &why) && dw.Size() == 0 && why.find("deep") != std::string::npos);
    CHECK(!EncodeValue(dw, std::numeric_limits<double>::infinity(), &why));
    // A hand-made value nested too deeply / a repeated key / an unknown tag fail to decode.
    std::vector<uint8_t> nested(40, uint8_t(6));
    for (size_t i = 1; i < nested.size(); i += 2) nested[i] = 1;
    Reader nr(nested);
    nlohmann::json out;
    CHECK(!DecodeValue(nr, out));
    const uint8_t dup[] = { 7, 2, 1, 'k', 0, 1, 'k', 0 };
    Reader dr(dup, sizeof(dup));
    CHECK(!DecodeValue(dr, out));
    const uint8_t tag[] = { 42 };
    Reader tr(tag, 1);
    CHECK(!DecodeValue(tr, out));
}

TEST(NetDecodersSurviveRandomAndMutatedInput)
{
    // 20000 random and mutated buffers through every decoder: nothing reads out of bounds (run under ASan in the
    // sanitizer job), crashes or loops; valid input still decodes after the storm.
    std::mt19937 random(1234);
    Writer good;
    EncodeValue(good, nlohmann::json{ { "list", { 1, 2.5, "three", { { "x", false } } } } });
    Clock clock;
    Connection conn(0x1234, clock.T);
    std::vector<Message> delivered;
    size_t decoded = 0;
    for (int i = 0; i < 20000; ++i)
    {
        std::vector<uint8_t> data;
        if (i % 2)
        {
            data = good.Data();
            for (int m = 0; m < 1 + int(random() % 4); ++m) data[random() % data.size()] = uint8_t(random());
            if (random() % 3 == 0) data.resize(random() % data.size());
        }
        else
        {
            data.resize(random() % 1300);
            for (auto& b : data) b = uint8_t(random());
            if (data.size() > 12 && i % 4 == 0)
            {
                // Plausible data packets: the right magic, type and key, random after them.
                data[0] = 0x5A;
                data[1] = 0x4D;
                data[2] = uint8_t(PacketType::Data);
                for (int k = 0; k < 8; ++k) data[3 + size_t(k)] = uint8_t(0x1234ull >> (8 * k));
            }
        }
        Reader r(data);
        nlohmann::json value;
        if (DecodeValue(r, value)) ++decoded;
        conn.ReadPacket(clock.T, data.data(), data.size(), delivered);
        CHECK(delivered.size() < 100000);
        delivered.clear();
    }
    Reader r(good.Data());
    nlohmann::json value;
    CHECK(DecodeValue(r, value) && value["list"][2] == "three");
    CHECK(conn.GetStats().MalformedPackets > 1000);
    // A server fed random datagrams from strangers answers none of them with state, and still serves.
    Link link;
    auto stranger = link.net->Open(7100);
    for (int i = 0; i < 3000; ++i)
    {
        std::vector<uint8_t> data(random() % 400);
        for (auto& b : data) b = uint8_t(random());
        if (data.size() > 3 && i % 2)
        {
            data[0] = 0x5A;
            data[1] = 0x4D;
            data[2] = uint8_t(1 + random() % 7);
        }
        stranger->Send(Address::Loopback(7000), data.data(), data.size());
        if (i % 100 == 0) link.Step();
    }
    link.Step();
    CHECK(link.server.GetClientCount() == 0 && link.server.GetCounters().BadDatagrams > 1000);
    link.Add();
    link.Step(30);
    CHECK(link.server.GetClientCount() == 1 && link.clients[0]->IsConnected());
}

// ------------------------------------------------------------------ reliability
TEST(NetReliableMessagesArriveExactlyOnceInOrderUnderLossReorderAndDuplication)
{
    ConditionerSettings bad;
    bad.Enabled = true;
    bad.LatencyMs = 40;
    bad.JitterMs = 15;
    bad.LossPercent = 20;
    bad.DuplicatePercent = 10;
    bad.ReorderPercent = 10;
    bad.Seed = 7;
    ConditionerSettings bad2 = bad;
    bad2.Seed = 99;
    Link link(Settings(), bad);
    auto& client = link.Add(Settings(), bad2);
    link.Step(120);
    CHECK(client.IsConnected() && link.server.GetClientCount() == 1);
    const uint32_t id = client.GetClientId();
    // 600 reliable messages each way, sizes 4 B .. 3.5 KB (fragmented), queued a few a frame, and unreliable ones
    // between them.
    std::vector<uint32_t> toServer, toClient;
    uint32_t sent = 0, unreliableSent = 0;
    for (int frame = 0; frame < 2400; ++frame)
    {
        if (sent < 600 && frame % 2 == 0)
            for (int k = 0; k < 3 && sent < 600; ++k, ++sent)
            {
                const size_t size = 4 + (sent * 97) % (sent % 10 == 0 ? 3500 : 300);
                CHECK(client.Send(Bytes(size, sent + 1), true));
                CHECK(link.server.Send(id, Bytes(size, sent + 1), true));
            }
        if (frame % 5 == 0 && unreliableSent < 200)
        {
            client.Send(Bytes(16, 900000 + unreliableSent), false);
            ++unreliableSent;
        }
        link.Step();
        for (auto& e : link.serverEvents)
            if (e.Type == TransportEvent::Kind::Message && e.Payload.Reliable) toServer.push_back(IdOf(e.Payload.Data));
        link.serverEvents.clear();
        for (auto& e : link.clientEvents[0])
            if (e.Type == TransportEvent::Kind::Message && e.Payload.Reliable) toClient.push_back(IdOf(e.Payload.Data));
        link.clientEvents[0].clear();
    }
    std::vector<uint32_t> expected(600);
    for (uint32_t i = 0; i < 600; ++i) expected[i] = i + 1;
    CHECK(toServer == expected);
    CHECK(toClient == expected);
    const auto stats = link.server.GetConnection(id)->GetStats();
    CHECK(stats.ReliableResent > 50 && stats.DuplicatePackets > 10 && stats.LossPercent > 5 && stats.RttMs > 50);
    CHECK(client.GetConnection()->AllReliableAcked() && link.server.GetConnection(id)->AllReliableAcked());
}

TEST(NetFragmentationReassemblesUpToTheCapAndRefusesMore)
{
    Link link;
    auto& client = link.Add();
    link.Step(10);
    const uint32_t id = client.GetClientId();
    // 200 KB reliable and 20 KB unreliable messages arrive whole; one above the cap is refused at once.
    const auto large = Bytes(200 * 1024, 5), medium = Bytes(20 * 1024, 6);
    CHECK(link.server.Send(id, large, true));
    CHECK(link.server.Send(id, medium, false));
    CHECK(!link.server.Send(id, Bytes(Connection::MaxMessageSize + 1, 7), true));
    CHECK(!client.Send(std::vector<uint8_t>(), true));
    link.Step(30);
    std::vector<std::vector<uint8_t>> got;
    for (auto& e : link.clientEvents[0])
        if (e.Type == TransportEvent::Kind::Message) got.push_back(e.Payload.Data);
    CHECK(got.size() == 2 && std::count(got.begin(), got.end(), large) == 1 && std::count(got.begin(), got.end(), medium) == 1);
    // An unreliable fragment group with a lost piece is dropped after a second (never delivered half).
    Connection a(1, link.clock.T), b(1, link.clock.T);
    a.Send(Bytes(5000, 8), false);
    std::vector<uint8_t> packet;
    std::vector<Message> delivered;
    int n = 0;
    while (a.WritePacket(link.clock.T, packet))
        if (n++ != 2) b.ReadPacket(link.clock.T, packet.data(), packet.size(), delivered);
    CHECK(delivered.empty());
    a.WritePacket(link.clock.T + 2.0, packet);
    b.ReadPacket(link.clock.T + 2.0, packet.data(), packet.size(), delivered);
    CHECK(delivered.empty() && b.GetStats().FragmentsExpired == 1);
}

// ------------------------------------------------------------------ handshake
TEST(NetHandshakeRejectsWrongVersionProjectFullServerAndForgedChallenges)
{
    Link link(Settings(2));
    auto other = Settings();
    other.ProtocolVersion = 2;
    auto& wrongVersion = link.Add(other);
    other = Settings();
    other.ProjectId = 99;
    auto& wrongProject = link.Add(other);
    link.Step(20);
    CHECK(!wrongVersion.IsConnected() && wrongVersion.GetLastReason() == DisconnectReason::WrongVersion);
    CHECK(!wrongProject.IsConnected() && wrongProject.GetLastReason() == DisconnectReason::WrongProject);
    auto& a = link.Add();
    auto& b = link.Add();
    link.Step(20);
    auto& c = link.Add();
    link.Step(20);
    CHECK(a.IsConnected() && b.IsConnected() && !c.IsConnected() && c.GetLastReason() == DisconnectReason::ServerFull);
    CHECK(link.server.GetCounters().Denied[size_t(DisconnectReason::ServerFull)] >= 1);
    // A challenge response with a made-up token gets no answer and makes no state.
    auto spoof = link.net->Open(7200);
    Writer w;
    w.U16(PacketMagic);
    w.U8(uint8_t(PacketType::ChallengeResponse));
    w.U64(1);
    w.U64(0xBADC0FFEE);
    w.U16(TransportVersion);
    w.U16(1);
    w.U64(Settings().ProjectId);
    w.U32(0);
    w.U64(0);
    const uint64_t bad = link.server.GetCounters().BadDatagrams;
    spoof->Send(Address::Loopback(7000), w.Data().data(), w.Size());
    link.Step();
    CHECK(link.server.GetCounters().BadDatagrams == bad + 1 && link.server.GetClientCount() == 2 && link.net->Pending(7200) == 0);
    // A connect request shorter than the padded size is ignored (no amplification).
    Writer small;
    small.U16(PacketMagic);
    small.U8(uint8_t(PacketType::ConnectRequest));
    small.U16(TransportVersion);
    spoof->Send(Address::Loopback(7000), small.Data().data(), small.Size());
    link.Step();
    CHECK(link.net->Pending(7200) == 0);
    // No server at all: the client gives up after HandshakeSeconds.
    ClientTransport lonely;
    lonely.Connect(link.net->Open(), Address::Loopback(7999), Settings());
    for (int i = 0; i < 90; ++i)
    {
        link.clock.T += 1.0 / 60.0;
        lonely.Update(link.clock.T);
    }
    TransportEvent e;
    CHECK(!lonely.IsConnected() && lonely.Poll(e) && e.Type == TransportEvent::Kind::Disconnected && e.Reason == DisconnectReason::HandshakeFailed);
    // A graceful goodbye frees the slot at once.
    a.Disconnect();
    link.Step(2);
    CHECK(link.server.GetClientCount() == 1 && link.Count(TransportEvent::Kind::Disconnected) == 1);
}

TEST(NetRateLimitsDropAndThenKickAFloodingClient)
{
    auto s = Settings();
    s.MaxPacketsPerSecond = 100;
    s.KickAfterSeconds = 1.0;
    s.TimeoutSeconds = 10;
    Link link(s);
    auto& client = link.Add(s);
    link.Step(10);
    CHECK(client.IsConnected());
    const uint32_t id = client.GetClientId();
    // 1000 small unreliable messages a second: each its own packet (Flush after each).
    for (int frame = 0; frame < 150 && link.server.HasClient(id); ++frame)
    {
        for (int k = 0; k < 17; ++k)
        {
            client.Send(Bytes(8, uint32_t(k)), false);
            client.Flush(link.clock.T);
        }
        link.Step();
    }
    CHECK(!link.server.HasClient(id) && link.server.GetCounters().RateLimited > 100);
    CHECK(std::any_of(link.serverEvents.begin(), link.serverEvents.end(), [](const TransportEvent& e) { return e.Reason == DisconnectReason::RateLimited; }));
}

TEST(NetReconnectResumesTheSessionAndExpiresItLater)
{
    Link link;
    auto& client = link.Add();
    link.Step(10);
    const uint32_t id = client.GetClientId();
    const uint64_t token = client.GetResumeToken();
    CHECK(id && token);
    // The cable is pulled: both sides time out; the server keeps the session for ResumeSeconds.
    const uint16_t port = client.GetSocket()->GetLocalAddress().Port;
    link.net->SetBlocked(port, true);
    link.Step(150);
    CHECK(!client.IsConnected() && client.GetLastReason() == DisconnectReason::Timeout && link.server.GetClientCount() == 0);
    auto lost = std::find_if(link.serverEvents.begin(), link.serverEvents.end(), [](const TransportEvent& e) { return e.Type == TransportEvent::Kind::Disconnected; });
    CHECK(lost != link.serverEvents.end() && lost->Resumable && lost->Reason == DisconnectReason::Timeout);
    link.net->SetBlocked(port, false);
    CHECK(client.Reconnect());
    link.Step(20);
    CHECK(client.IsConnected() && client.GetClientId() == id && link.server.GetCounters().Resumed == 1);
    auto back = std::find_if(link.serverEvents.begin(), link.serverEvents.end(), [](const TransportEvent& e) { return e.Type == TransportEvent::Kind::Connected && e.Resumed; });
    CHECK(back != link.serverEvents.end() && back->Client == id);
    // A new process (another socket) resumes with the saved id and token while the old connection still lives:
    // the old one is replaced, without a departure event.
    const uint64_t token2 = client.GetResumeToken();
    link.serverEvents.clear();
    auto& restarted = link.Add(Settings(), {}, id, token2);
    link.Step(20);
    CHECK(restarted.IsConnected() && restarted.GetClientId() == id && link.server.GetClientCount() == 1);
    CHECK(link.Count(TransportEvent::Kind::Disconnected) == 0);
    link.Step(5);
    CHECK(!client.IsConnected() && client.GetLastReason() == DisconnectReason::Replaced);
    // A wrong token is a new client; a session that does not come back within ResumeSeconds expires.
    auto& stranger = link.Add(Settings(), {}, id, 12345);
    link.Step(20);
    CHECK(stranger.IsConnected() && stranger.GetClientId() != id);
    link.net->SetBlocked(restarted.GetSocket()->GetLocalAddress().Port, true);
    link.serverEvents.clear();
    link.Step(150 + 6 * 60);
    CHECK(link.Count(TransportEvent::Kind::Expired) == 1);
}

// ------------------------------------------------------------------ the session
namespace {
    class NetProbe : public ScriptableEntity
    {
    public:
        int Score = 0;
        std::string Label = "none";
        int Secret = 0;
        int Notified = 0, Pings = 0, Flashes = 0, Notices = 0;
        uint32_t LastSender = 0;
        glm::vec3 LastFlash{ 0 };
        void OnScore() { ++Notified; }
        void AddScore(int amount)
        {
            Score += amount;
            LastSender = RpcSender();
        }
        void Ping() { ++Pings; LastSender = RpcSender(); }
        void Flash(glm::vec3 at)
        {
            ++Flashes;
            LastFlash = at;
        }
        void Notice(const std::string& text)
        {
            ++Notices;
            Label = text;
        }
    };
    std::unique_ptr<ScriptRegistrar> RegisterNetProbe()
    {
        return std::make_unique<ScriptRegistrar>("NetProbe", []() -> ScriptableEntity* { return new NetProbe; },
            std::vector<ScriptMember>{ ZM_REPLICATED_NOTIFY(NetProbe, Score, OnScore), ZM_REPLICATED(NetProbe, Label), ZM_FIELD(NetProbe, Secret),
                                       ZM_SERVER_RPC(NetProbe, AddScore, "Amount"), ZM_SERVER_RPC(NetProbe, Ping).AnyClient(),
                                       ZM_MULTICAST_RPC(NetProbe, Flash, "At"), ZM_CLIENT_RPC(NetProbe, Notice, "Text") });
    }
    NetProbe* Probe(Entity e)
    {
        auto* nsc = e ? e.TryGetComponent<NativeScriptComponent>() : nullptr;
        return nsc ? dynamic_cast<NetProbe*>(nsc->Instance) : nullptr;
    }

    // A temporary project with the player prefab (predicted movement, the probe script), a pickup prefab and the
    // graphs of the graph tests; one server scene and client scenes with the same scene objects.
    struct World
    {
        Clock clock;
        std::filesystem::path Root = std::filesystem::temp_directory_path() / ("zamin-net-" + std::to_string(uint64_t(UUID())));
        Ref<Project> Previous = Project::GetActive();
        std::unique_ptr<ScriptRegistrar> Registrar = RegisterNetProbe();
        std::shared_ptr<LoopbackNetwork> Net = LoopbackNetwork::Create();
        std::unique_ptr<Scene> ServerScene;
        std::vector<std::unique_ptr<Scene>> ClientScenes;
        Session Server;
        std::vector<std::unique_ptr<Session>> Clients;
        std::map<Session*, glm::vec2> Inputs;
        SessionSettings Settings;
        static constexpr uint64_t Board = 4242, Far = 4343;

        World()
        {
            std::filesystem::create_directories(Root / "Assets/Prefabs");
            std::filesystem::create_directories(Root / "Assets/Scripts");
            Project::New();
            CHECK(Project::SaveActive(Root / "Net.zproject"));
            Scene author;
            auto player = author.CreateEntity("Player");
            player.AddComponent<NetworkIdentityComponent>();
            auto& nt = player.AddComponent<NetworkTransformComponent>();
            nt.Mode = NetworkTransformComponent::SyncMode::Predict;
            nt.MoveSpeed = 4.0f;
            nt.ClampToBounds = true;
            nt.BoundsMin = { -20, -20 };
            nt.BoundsMax = { 20, 20 };
            player.AddComponent<NativeScriptComponent>().ClassName = "NetProbe";
            CHECK(author.SavePrefab(player, Root / "Assets/Prefabs/Player.zprefab"));
            auto pickup = author.CreateEntity("Pickup");
            pickup.AddComponent<NetworkIdentityComponent>();
            pickup.AddComponent<NetworkTransformComponent>();
            CHECK(author.SavePrefab(pickup, Root / "Assets/Prefabs/Pickup.zprefab"));
            Settings.Transport = ::Settings();
            Settings.TickRate = 30;
            Settings.RelevanceRadius = 15.0f;
            ServerScene = Make();
            CHECK(Server.StartServer(*ServerScene, Net->Open(7300), Settings));
        }
        ~World()
        {
            Clients.clear();
            Server.Stop();
            for (auto& s : ClientScenes) s->OnRuntimeStop();
            ServerScene->OnRuntimeStop();
            Project::SetActive(Previous);
            std::error_code ec;
            std::filesystem::remove_all(Root, ec);
        }
        // The scene both sides load: a scoreboard (a scene object with the probe) and a far-away marker.
        std::unique_ptr<Scene> Make()
        {
            auto scene = std::make_unique<Scene>();
            auto board = scene->CreateEntityWithUUID(UUID(Board), "Board");
            board.AddComponent<NetworkIdentityComponent>();
            board.AddComponent<NativeScriptComponent>().ClassName = "NetProbe";
            auto far = scene->CreateEntityWithUUID(UUID(Far), "Far");
            far.AddComponent<NetworkIdentityComponent>();
            far.GetComponent<TransformComponent>().Translation = { 500, 0, 0 };
            scene->OnRuntimeStart();
            return scene;
        }
        Session& AddClient(const ConditionerSettings& c = {}, uint32_t resumeId = 0, uint64_t token = 0)
        {
            ClientScenes.push_back(Make());
            Clients.push_back(std::make_unique<Session>());
            std::unique_ptr<Socket> socket = Net->Open();
            if (c.Enabled) socket = std::make_unique<LinkConditioner>(std::move(socket), c);
            Session* s = Clients.back().get();
            s->SetInputSource([this, s](Entity) { return Inputs[s]; });
            CHECK(s->StartClient(*ClientScenes.back(), std::move(socket), Address::Loopback(7300), Settings, resumeId, token));
            return *s;
        }
        void Step(int frames = 1, float dt = 1.0f / 60.0f)
        {
            for (int f = 0; f < frames; ++f)
            {
                clock.T += dt;
                for (auto& s : ClientScenes) s->UpdateRuntime(dt);
                ServerScene->UpdateRuntime(dt);
            }
        }
        Entity Player(uint32_t client) { return Server.Spawn("Prefabs/Player.zprefab", { float(client) * 2.0f, 0, 0 }, client); }
    };
    glm::vec3 At(Entity e) { return e ? e.GetComponent<TransformComponent>().Translation : glm::vec3(-9999); }
}

TEST(NetSessionReplicatesSpawnsFieldsAndTransformsWithDeltaCompression)
{
    World w;
    auto& a = w.AddClient();
    auto& b = w.AddClient();
    w.Step(20);
    CHECK(a.IsConnected() && b.IsConnected() && a.GetLocalClient() != b.GetLocalClient());
    Entity pa = w.Player(a.GetLocalClient()), pb = w.Player(b.GetLocalClient());
    CHECK(pa && pb && Probe(pa));
    w.Step(30);
    // Both clients have both players (the prefab spawned there) at the server's positions, with the owners.
    for (auto* s : { &a, &b })
    {
        CHECK(s->FindEntity(pa.GetComponent<NetworkIdentityComponent>().NetId) && s->FindEntity(pb.GetComponent<NetworkIdentityComponent>().NetId));
        Entity mine = s->OwnedEntity(s->GetLocalClient());
        CHECK(mine && s->IsOwner(mine) && glm::length(At(mine) - At(s == &a ? pa : pb)) < 1e-4f);
    }
    // Replicated fields follow the server's (with the notify), the plain field does not.
    Probe(pa)->Score = 7;
    Probe(pa)->Label = "seven";
    Probe(pa)->Secret = 99;
    Entity board = w.ServerScene->GetEntityByUUID(UUID(World::Board));
    Probe(board)->Score = 3;
    w.Step(10);
    Entity remote = b.FindEntity(pa.GetComponent<NetworkIdentityComponent>().NetId);
    CHECK(Probe(remote) && Probe(remote)->Score == 7 && Probe(remote)->Label == "seven" && Probe(remote)->Secret == 0 && Probe(remote)->Notified >= 1);
    CHECK(Probe(w.ClientScenes[0]->GetEntityByUUID(UUID(World::Board)))->Score == 3);
    // Delta compression: with nothing changing, a snapshot is a few bytes (no entity in it).
    w.Step(60);
    const auto before = w.Server.GetStats();
    w.Step(60);
    const auto idle = w.Server.GetStats();
    const double bytesPerSnapshot = double(idle.SnapshotBytes - before.SnapshotBytes) / double(idle.SnapshotsSent - before.SnapshotsSent);
    CHECK(idle.SnapshotsSent - before.SnapshotsSent == 60 && bytesPerSnapshot < 12);
    CHECK(idle.ReplicatedEntities == 0);
    // One moving entity: only its position goes (about 12 bytes of state plus the headers).
    for (int i = 0; i < 60; ++i)
    {
        board.GetComponent<TransformComponent>().Translation.y += 0.05f;
        w.Step();
    }
    const auto moving = w.Server.GetStats();
    const double movingBytes = double(moving.SnapshotBytes - idle.SnapshotBytes) / double(moving.SnapshotsSent - idle.SnapshotsSent);
    CHECK(movingBytes > 12 && movingBytes < 30);
    // Clients show it interpolated (about InterpolationDelay behind), and settle on the server's position.
    w.Step(30);
    CHECK(std::abs(At(w.ClientScenes[1]->GetEntityByUUID(UUID(World::Board))).y - At(board).y) < 1e-4f);
    // Despawn reaches every client.
    const uint32_t gone = pb.GetComponent<NetworkIdentityComponent>().NetId;
    CHECK(w.Server.Despawn(pb));
    w.Step(20);
    CHECK(!a.FindEntity(gone) && !b.FindEntity(gone));
}

TEST(NetInterestManagementSpawnsAndDespawnsByDistance)
{
    World w;
    auto& a = w.AddClient();
    w.Step(20);
    Entity player = w.Player(a.GetLocalClient());
    Entity pickup = w.Server.Spawn("Prefabs/Pickup.zprefab", { 30, 0, 0 });
    CHECK(player && pickup);
    w.Step(20);
    const uint32_t id = pickup.GetComponent<NetworkIdentityComponent>().NetId;
    // Out of the 15 m radius: not on the client; a scene object out of interest is hidden (Far at 500 m), one that
    // is always relevant is not.
    const uint32_t far = w.ServerScene->GetEntityByUUID(UUID(World::Far)).GetComponent<NetworkIdentityComponent>().NetId;
    CHECK(!a.FindEntity(id) && !a.FindEntity(far) && !w.ClientScenes[0]->GetEntityByUUID(UUID(World::Far)).IsActive());
    w.ServerScene->GetEntityByUUID(UUID(World::Far)).GetComponent<NetworkIdentityComponent>().AlwaysRelevant = true;
    w.Step(10);
    CHECK(a.FindEntity(far) && w.ClientScenes[0]->GetEntityByUUID(UUID(World::Far)).IsActive() && At(a.FindEntity(far)).x == 500);
    pickup.GetComponent<TransformComponent>().Translation.x = 10;
    w.Step(10);
    CHECK(a.FindEntity(id) && std::abs(At(a.FindEntity(id)).x - 10) < 1e-4f);
    // Just past the radius it stays (hysteresis), well past it it despawns.
    pickup.GetComponent<TransformComponent>().Translation.x = 15.8f;
    w.Step(10);
    CHECK(a.FindEntity(id));
    pickup.GetComponent<TransformComponent>().Translation.x = 25;
    w.Step(10);
    CHECK(!a.FindEntity(id));
    CHECK(w.Server.GetClients().size() == 1 && w.Server.GetClients()[0].Relevant == 3);
}

TEST(NetPredictionAndReconciliationConvergeUnderLatencyAndLoss)
{
    World w;
    ConditionerSettings c;
    c.Enabled = true;
    c.LatencyMs = 80;
    c.JitterMs = 20;
    c.LossPercent = 10;
    c.Seed = 3;
    auto& a = w.AddClient(c);
    auto& b = w.AddClient();
    w.Step(60);
    CHECK(a.IsConnected());
    Entity server = w.Player(a.GetLocalClient());
    w.Step(60);
    Entity mine = a.OwnedEntity(a.GetLocalClient());
    CHECK(mine);
    // The client moves at once: after one frame of input it is ahead of the server (which has not seen it yet).
    w.Inputs[&a] = { 1, 0 };
    const float start = At(mine).x;
    w.Step(4);
    CHECK(At(mine).x > start + 0.1f && At(server).x < At(mine).x);
    // A zig-zag for 3 seconds: the client never waits for the server, and both end where the inputs say.
    for (int i = 0; i < 180; ++i)
    {
        w.Inputs[&a] = { (i / 30) % 2 ? -1.0f : 1.0f, i % 45 < 20 ? 0.5f : -0.5f };
        w.Step();
    }
    w.Inputs[&a] = { 0, 0 };
    w.Step(90);
    const float error = glm::length(At(mine) - At(server));
    CHECK(error < 1e-3f);
    // The other client sees the same place (interpolated), the clamp holds on both sides.
    Entity seen = b.FindEntity(server.GetComponent<NetworkIdentityComponent>().NetId);
    CHECK(glm::length(At(seen) - At(server)) < 1e-3f);
    w.Inputs[&a] = { 1, 0 };
    w.Step(400);
    w.Inputs[&a] = { 0, 0 };
    w.Step(60);
    CHECK(std::abs(At(server).x - 20) < 1e-4f && std::abs(At(mine).x - 20) < 1e-3f);
    // Lost inputs were caught up by the redundancy; the server never moved faster than one input a tick.
    CHECK(w.Server.GetStats().InputsReceived > 300);
    // A cheating input (a diagonal of length 1.41) is refused, not applied.
    Writer cheat;
    cheat.U8(10);
    cheat.VarU(0);
    cheat.VarU(1);
    cheat.VarU(1000000);
    cheat.U16(32767); // (1, 1): longer than 1
    cheat.U16(32767);
    const uint64_t rejected = w.Server.GetStats().InputsRejected;
    // (sent on b's own connection: its transport is reachable through a raw ClientTransport of its own)
    ClientTransport raw;
    raw.Connect(w.Net->Open(), Address::Loopback(7300), [&] { auto t = w.Settings.Transport; t.ProtocolVersion = uint16_t(t.ProtocolVersion * 31u + SessionProtocol); return t; }());
    for (int i = 0; i < 20; ++i)
    {
        w.clock.T += 1.0 / 60.0;
        raw.Update(w.clock.T);
        if (raw.IsConnected()) raw.Send(cheat.Data(), false);
        raw.Flush(w.clock.T);
        w.ServerScene->UpdateRuntime(1.0f / 60.0f);
    }
    CHECK(w.Server.GetStats().InputsRejected > rejected);
}

TEST(NetRpcsRunWhereTheyShouldAndInvalidCallsAreRejected)
{
    World w;
    auto& a = w.AddClient();
    auto& b = w.AddClient();
    w.Step(20);
    Entity pa = w.Player(a.GetLocalClient()), pb = w.Player(b.GetLocalClient());
    w.Step(30);
    Entity mineA = a.OwnedEntity(a.GetLocalClient());
    Entity theirsOnA = a.FindEntity(pb.GetComponent<NetworkIdentityComponent>().NetId);
    // A server RPC from the owner runs on the server with its sender.
    CHECK(a.Call(mineA, "AddScore", { 5 }, false));
    w.Step(10);
    CHECK(Probe(pa)->Score == 5 && Probe(pa)->LastSender == a.GetLocalClient());
    // ... and the replicated result comes back to both clients.
    CHECK(Probe(mineA)->Score == 5 && Probe(b.FindEntity(pa.GetComponent<NetworkIdentityComponent>().NetId))->Score == 5);
    // Not the owner: refused on the client already; a forged message is refused on the server.
    std::string why;
    CHECK(!a.Call(theirsOnA, "AddScore", { 5 }, false, &why) && why.find("own") != std::string::npos);
    // Wrong argument types / count, unknown and non-RPC methods: refused before sending.
    CHECK(!a.Call(mineA, "AddScore", { "five" }, false, &why) && why.find("Amount") != std::string::npos);
    CHECK(!a.Call(mineA, "AddScore", { 1.5 }, false, &why));
    CHECK(!a.Call(mineA, "AddScore", nlohmann::json::array(), false, &why));
    CHECK(!a.Call(mineA, "Missing", nlohmann::json::array(), false, &why));
    CHECK(!a.Call(mineA, "Flash", { { 1, 2, 3 } }, false, &why) && why.find("server") != std::string::npos);
    // A client that skips its own checks: raw messages with wrong types / someone else's entity are rejected.
    auto forge = [&](uint32_t netId, const char* name, const nlohmann::json& args) {
        Writer m;
        m.U8(11);
        m.VarU(netId);
        m.U8(0);
        m.String(name);
        EncodeValue(m, args);
        return m.Data();
    };
    ClientTransport raw;
    auto t = w.Settings.Transport;
    t.ProtocolVersion = uint16_t(t.ProtocolVersion * 31u + SessionProtocol);
    raw.Connect(w.Net->Open(), Address::Loopback(7300), t);
    const uint64_t rejected = w.Server.GetStats().RpcsRejected;
    for (int i = 0; i < 30; ++i)
    {
        w.clock.T += 1.0 / 60.0;
        raw.Update(w.clock.T);
        if (raw.IsConnected() && i == 20)
        {
            raw.Send(forge(pa.GetComponent<NetworkIdentityComponent>().NetId, "AddScore", { 100 }), true); // not its own
            raw.Send(forge(pa.GetComponent<NetworkIdentityComponent>().NetId, "Flash", { { 1, 2, 3 } }), true); // not a server RPC
            raw.Send(forge(999999, "AddScore", { 1 }), true);
        }
        raw.Flush(w.clock.T);
        w.ServerScene->UpdateRuntime(1.0f / 60.0f);
    }
    CHECK(Probe(pa)->Score == 5 && w.Server.GetStats().RpcsRejected == rejected + 3);
    // AnyClient: anyone may call it, on any entity.
    CHECK(b.Call(b.FindEntity(pa.GetComponent<NetworkIdentityComponent>().NetId), "Ping", nlohmann::json::array(), false));
    w.Step(10);
    CHECK(Probe(pa)->Pings == 1 && Probe(pa)->LastSender == b.GetLocalClient());
    // Multicast runs on the server and both clients; a client RPC on the owner's client only.
    CHECK(w.Server.Call(pa, "Flash", { { 1, 2, 3 } }, false));
    CHECK(w.Server.Call(pa, "Notice", { "hi" }, false));
    w.Step(10);
    CHECK(Probe(pa)->Flashes == 1 && Probe(mineA)->Flashes == 1 && Probe(b.FindEntity(pa.GetComponent<NetworkIdentityComponent>().NetId))->Flashes == 1);
    CHECK(Probe(mineA)->LastFlash == glm::vec3(1, 2, 3));
    CHECK(Probe(mineA)->Notices == 1 && Probe(b.FindEntity(pa.GetComponent<NetworkIdentityComponent>().NetId))->Notices == 0 && Probe(pa)->Notices == 0);
    // Rate limit: a burst far above MaxRpcsPerSecond is cut down.
    for (int i = 0; i < 200; ++i) a.Call(mineA, "AddScore", { 1 }, false);
    w.Step(20);
    CHECK(Probe(pa)->Score > 5 + 50 && Probe(pa)->Score < 5 + 80);
    // Without a session every RPC runs at once.
    Scene alone;
    auto e = alone.CreateEntity("Solo");
    e.AddComponent<NativeScriptComponent>().ClassName = "NetProbe";
    alone.OnRuntimeStart();
    CHECK(Net::Call(e, "AddScore", { 2 }, false) && Net::Call(e, "Flash", { { 0, 0, 1 } }, false) && Probe(e)->Score == 2 && Probe(e)->Flashes == 1);
    CHECK(Net::IsServer(alone) && !Net::IsClient(alone) && Net::IsOwner(e));
    alone.OnRuntimeStop();
}

TEST(NetClientReconnectsAndResumesItsPlayer)
{
    World w;
    auto& a = w.AddClient();
    w.Step(20);
    Entity player = w.Player(a.GetLocalClient());
    const UUID playerId = player.GetUUID();
    Probe(player)->Score = 11;
    w.Step(30);
    const uint32_t id = a.GetLocalClient();
    // The connection drops for 3 s (longer than the timeout): the client reconnects by itself.
    const uint16_t port = a.GetLocalAddress().Port;
    w.Net->SetBlocked(port, true);
    w.Step(180);
    CHECK(!a.IsConnected());
    w.Net->SetBlocked(port, false);
    w.Step(120);
    CHECK(a.IsConnected() && a.GetLocalClient() == id && player && Probe(player)->Score == 11);
    CHECK(a.OwnedEntity(id) && Probe(a.OwnedEntity(id))->Score == 11);
    // A new process resumes with the saved token: the same client id and player; leaving for good despawns it.
    // (killed: its goodbye never leaves - the port is blocked while it stops)
    const uint64_t token = a.GetResumeToken();
    w.Net->SetBlocked(port, true);
    w.Clients.front()->Stop();
    w.Net->SetBlocked(port, false);
    auto& again = w.AddClient({}, id, token);
    w.Step(40);
    CHECK(again.IsConnected() && again.GetLocalClient() == id);
    w.Step(30);
    CHECK(again.OwnedEntity(id) && Probe(again.OwnedEntity(id))->Score == 11);
    again.Stop();
    w.Step(10);
    CHECK(!w.ServerScene->GetEntityByUUID(playerId));
}

// ------------------------------------------------------------------ graphs
namespace {
    Visual::Node N(uint32_t id, const char* kind, const char* name)
    {
        Visual::Node n;
        n.ID = id;
        n.Kind = kind;
        n.Inputs["Name"] = name;
        return n;
    }
    Visual::Graph NetGraph()
    {
        using namespace Visual;
        Graph g;
        g.Variables["Total"] = { Type::Number, 0.0 };
        g.Variables["Total"].Replicated = true;
        g.Variables["Reps"] = { Type::Number, 0.0 };
        g.Variables["Heard"] = { Type::String, std::string() };
        g.Variables["Joined"] = { Type::Number, 0.0 };
        RpcDefinition add;
        add.Kind = RpcDefinition::Target::Server;
        add.AnyClient = true;
        add.Params = { { "Amount", Type::Number, 0.0 } };
        g.Rpcs["Add"] = add;
        RpcDefinition shout;
        shout.Kind = RpcDefinition::Target::Multicast;
        shout.Params = { { "Text", Type::String, std::string() } };
        g.Rpcs["Shout"] = shout;
        auto node = [&](uint32_t id, const char* kind, Value inputs = Value::object()) {
            Node n;
            n.ID = id;
            n.Kind = kind;
            for (auto& [k, v] : inputs.items()) n.Inputs[k] = v;
            g.Nodes.push_back(n);
        };
        auto link = [&](uint32_t from, const char* output, uint32_t to, const char* input) { g.Links.push_back({ from, output, to, input }); };
        // Event.Rpc Add -> Total += Amount -> Multicast Shout("added")
        node(1, "Event.Rpc", { { "Name", "Add" } });
        node(2, "Get.Number", { { "Name", "Total" } });
        node(3, "Add");
        node(4, "Set.Number", { { "Name", "Total" } });
        node(5, "Net.CallMulticastRpc", { { "Name", "Shout" }, { "Text", "added" } });
        link(1, "Then", 4, "In");
        link(2, "Value", 3, "A");
        link(1, "Amount", 3, "B");
        link(3, "Value", 4, "Value");
        link(4, "Then", 5, "In");
        // Event.Rpc Shout -> Heard = Text
        node(6, "Event.Rpc", { { "Name", "Shout" } });
        node(7, "Set.String", { { "Name", "Heard" } });
        link(6, "Then", 7, "In");
        link(6, "Text", 7, "Value");
        // Event.Rep Total -> Reps += 1
        node(8, "Event.Rep", { { "Name", "Total" } });
        node(9, "Get.Number", { { "Name", "Reps" } });
        node(10, "Add", { { "B", 1.0 } });
        node(11, "Set.Number", { { "Name", "Reps" } });
        link(8, "Then", 11, "In");
        link(9, "Value", 10, "A");
        link(10, "Value", 11, "Value");
        // Event.ClientConnected -> Joined = Client; on a client a key press calls the server RPC.
        node(12, "Event.ClientConnected");
        node(13, "Set.Number", { { "Name", "Joined" } });
        link(12, "Then", 13, "In");
        link(12, "Client", 13, "Value");
        node(14, "Event.Custom", { { "Name", "Press" } });
        node(15, "Net.CallServerRpc", { { "Name", "Add" }, { "Amount", 4.0 } });
        link(14, "Then", 15, "In");
        return g;
    }
}

TEST(NetGraphRpcsReplicatedVariablesAndConnectionEvents)
{
    World w;
    CHECK(NetGraph().Save(w.Root / "Assets/Scripts/Net.zgraph"));
    auto diagnostics = Visual::Graph::Load(w.Root / "Assets/Scripts/Net.zgraph", w.Root / "Assets").Validate();
    CHECK(diagnostics.empty());
    // A graph on a scene object of a fresh server and two clients.
    w.Clients.clear();
    w.Server.Stop();
    auto attach = [](Scene& scene) {
        auto e = scene.CreateEntityWithUUID(UUID(5151), "Rules");
        e.AddComponent<NetworkIdentityComponent>();
        auto& nsc = e.AddComponent<NativeScriptComponent>();
        nsc.ClassName = "Zamin.VisualGraph";
        nsc.Fields["Graph"].Set(std::string("Scripts/Net.zgraph"));
        return e;
    };
    w.ServerScene->OnRuntimeStop();
    w.ServerScene = std::make_unique<Scene>();
    Entity rules = attach(*w.ServerScene);
    w.ServerScene->OnRuntimeStart();
    CHECK(w.Server.StartServer(*w.ServerScene, w.Net->Open(7300), w.Settings));
    std::vector<Entity> clientRules;
    for (int i = 0; i < 2; ++i)
    {
        w.ClientScenes.push_back(std::make_unique<Scene>());
        clientRules.push_back(attach(*w.ClientScenes.back()));
        w.ClientScenes.back()->OnRuntimeStart();
        w.Clients.push_back(std::make_unique<Session>());
        CHECK(w.Clients.back()->StartClient(*w.ClientScenes.back(), w.Net->Open(), Address::Loopback(7300), w.Settings));
    }
    w.Step(30);
    auto vm = [](Entity e) -> Visual::Machine& { return dynamic_cast<VisualGraphHost*>(e.GetComponent<NativeScriptComponent>().Instance)->VM; };
    CHECK(vm(rules).Error().empty() && vm(rules).Variables().at("Joined") == double(w.Clients[1]->GetLocalClient()));
    // A client's graph calls the server RPC (AnyClient): the server adds, multicasts; the replicated Total and
    // its Event.Rep reach both clients.
    vm(clientRules[0]).Event("Custom.Press");
    w.Step(20);
    CHECK(vm(rules).Variables().at("Total") == 4.0 && vm(rules).Variables().at("Heard") == "added");
    for (auto& e : clientRules)
    {
        CHECK(vm(e).Error().empty() && vm(e).Variables().at("Total") == 4.0 && vm(e).Variables().at("Heard") == "added");
        CHECK(vm(e).Variables().at("Reps") == 1.0);
    }
    // The node library has the networking nodes.
    for (const char* kind : { "Net.IsServer", "Net.IsClient", "Net.IsOwner", "Net.Spawn", "Net.Despawn", "Event.ClientDisconnected", "Net.CallClientRpc" })
        CHECK(Visual::Catalog().contains(kind) && Visual::Catalog().at(kind).Category == "Networking");
    // A server RPC call with a wrong argument type is refused by the session (the graph checks its pins too).
    std::string why;
    CHECK(!w.Clients[0]->Call(clientRules[0], "Add", { "x" }, true, &why) && why.find("Amount") != std::string::npos);
    CHECK(!w.Clients[0]->Call(clientRules[0], "Shout", { "x" }, true, &why));
}

TEST(NetGraphFormat7ValidationAndMigration)
{
    using namespace Visual;
    // Format 6 files (no RPCs) load unchanged and are written back as format 7.
    Graph old;
    old.Variables["Score"] = { Type::Number, 2.0 };
    Node start;
    start.ID = 1;
    start.Kind = "Event.Start";
    old.Nodes.push_back(start);
    auto j = old.Serialize();
    j["Version"] = 6;
    Graph loaded = Graph::Parse(j);
    CHECK(loaded.SourceVersion == 6 && loaded.Variables.at("Score").Default == 2.0 && !loaded.Variables.at("Score").Replicated);
    CHECK(loaded.Serialize()["Version"] == 7 && !loaded.Serialize().contains("Rpcs"));
    // Networking in an older file is refused.
    auto net = NetGraph().Serialize();
    CHECK(net["Version"] == 7 && net["Rpcs"].size() == 2 && net["Variables"]["Total"]["Replicated"] == true);
    auto older = net;
    older["Version"] = 6;
    bool refused = false;
    try
    {
        Graph::Parse(older);
    }
    catch (const std::exception&)
    {
        refused = true;
    }
    CHECK(refused);
    Graph back = Graph::Parse(net);
    CHECK(back.Rpcs == NetGraph().Rpcs && back.Variables.at("Total").Replicated);
    // Validation: an unknown RPC, the wrong call node for its kind, a duplicate handler, Event.Rep of a plain
    // variable, a reserved parameter name.
    Graph bad = NetGraph();
    bad.Nodes.push_back(N(50, "Net.CallServerRpc", "Nope"));
    bad.Nodes.push_back(N(51, "Net.CallServerRpc", "Shout"));
    bad.Nodes.push_back(N(52, "Event.Rpc", "Add"));
    bad.Nodes.push_back(N(53, "Event.Rep", "Reps"));
    bad.Rpcs["Bad"].Params = { { "Sender", Type::Number, 0.0 } };
    std::string all;
    for (const auto& d : bad.Validate()) all += d.Message + "\n";
    CHECK(all.find("Unknown RPC") != std::string::npos && all.find("use Net.CallMulticastRpc") != std::string::npos && all.find("more than one Event.Rpc") != std::string::npos &&
          all.find("no replicated variable") != std::string::npos && all.find("reserved parameter in RPC Bad") != std::string::npos);
    // The RPC's parameters are the nodes' pins.
    Node call = N(60, "Net.CallMulticastRpc", "Shout");
    auto pins = NetGraph().Describe(call);
    CHECK(pins.Inputs.size() == 3 && pins.Inputs.back().Name == "Text" && pins.Inputs.back().Kind == Type::String);
    Node handler = N(61, "Event.Rpc", "Add");
    pins = NetGraph().Describe(handler);
    CHECK(pins.Outputs.size() == 3 && pins.Outputs[1].Name == "Sender" && pins.Outputs[2].Name == "Amount");
    // The bytecode cook keeps RPCs and the replicated flag.
    auto compiled = Bytecode::Compile(NetGraph());
    CHECK(compiled.Ok);
    Program program;
    std::string error;
    CHECK(Bytecode::Read(compiled.Bytes.data(), compiled.Bytes.size(), program, error));
    CHECK(program.Runnable.Rpcs == NetGraph().Rpcs && program.Runnable.Variables.at("Total").Replicated && !program.Runnable.Variables.at("Reps").Replicated);
}
