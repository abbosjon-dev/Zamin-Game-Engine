// ZaminBench --net: the replication session under load, headless, in one process (Network/NetSession.h). A server
// scene with N networked entities moving every tick (a replicated field changing now and then) and C clients, each in
// its own scene, over an in-process LoopbackNetwork on a manual clock (no sockets, no waiting: the numbers are CPU
// time). Every client owns one entity (its "player"), so distance-based interest decides what it gets.
//
//   ZaminBench --net [--clients C] [--entities N] [--seconds S] [--radius R] [--budget B] [--json <file>]
//
// Reports the server's work a tick (snapshot building, delta encoding, interest: the session's own timer) and the
// whole server frame, a client frame, bytes sent per client a second, entities per client snapshot, and the
// invariants (exit code 3 when one fails): every client connected, got snapshots every tick, sees fewer entities
// than exist (interest works) and has every entity of its snapshot at the server's position of that tick.
#include "Zamin/Core/Base.h"
#include "Zamin/Network/NetSession.h"
#include "Zamin/Network/NetSocket.h"
#include "Zamin/Scene/Components.h"
#include "Zamin/Scene/Scene.h"
#include "Zamin/Scripting/ScriptEngine.h"
#include "Zamin/Scripting/ScriptableEntity.h"
#include "Zamin/Utils/FileSystem.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <format>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

using namespace Zamin;

namespace {
    class BenchActor : public ScriptableEntity
    {
    public:
        int Health = 100;
    };

    double Ms(std::chrono::steady_clock::duration d) { return std::chrono::duration<double, std::milli>(d).count(); }

    double Percentile(std::vector<double> v, double p)
    {
        if (v.empty()) return 0;
        std::sort(v.begin(), v.end());
        return v[std::min(v.size() - 1, size_t(p * double(v.size() - 1) + 0.5))];
    }

    // The same scene on every side: N scene objects on a 200 x 200 m field (the server moves them).
    std::unique_ptr<Scene> MakeScene(int entities)
    {
        auto scene = std::make_unique<Scene>();
        for (int i = 0; i < entities; ++i)
        {
            Entity e = scene->CreateEntityWithUUID(UUID(uint64_t(100000 + i)), "Actor");
            e.AddComponent<NetworkIdentityComponent>();
            e.AddComponent<NetworkTransformComponent>();
            e.AddComponent<NativeScriptComponent>().ClassName = "BenchActor";
            const float a = float(i) * 2.399963f, r = 98.0f * std::sqrt(float(i) / float(std::max(1, entities)));
            e.GetComponent<TransformComponent>().Translation = { r * std::cos(a), r * std::sin(a), 0.0f };
        }
        scene->OnRuntimeStart();
        return scene;
    }
}

int RunNetBench(int argc, char** argv)
{
    int clients = 64, entities = 1000;
    double seconds = 5.0;
    float radius = 30.0f;
    size_t budget = 900;
    std::string json;
    for (int i = 1; i < argc; i++)
    {
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (!std::strcmp(argv[i], "--net")) continue;
        else if (!std::strcmp(argv[i], "--clients")) clients = std::clamp(std::atoi(next()), 1, 256);
        else if (!std::strcmp(argv[i], "--entities")) entities = std::clamp(std::atoi(next()), 1, 100000);
        else if (!std::strcmp(argv[i], "--seconds")) seconds = std::clamp(std::atof(next()), 0.5, 600.0);
        else if (!std::strcmp(argv[i], "--radius")) radius = float(std::clamp(std::atof(next()), 1.0, 10000.0));
        else if (!std::strcmp(argv[i], "--budget")) budget = size_t(std::clamp(std::atoi(next()), 200, 200000));
        else if (!std::strcmp(argv[i], "--json")) json = next();
        else
        {
            std::printf("ZaminBench --net [--clients C] [--entities N] [--seconds S] [--radius R] [--budget B] [--json <file>]\n");
            return 1;
        }
    }
    if (entities < clients) entities = clients;
#if defined(NDEBUG)
    const char* build = "Release";
#else
    const char* build = "Debug";
#endif
    ScriptRegistrar registrar("BenchActor", []() -> ScriptableEntity* { return new BenchActor; },
                              std::vector<ScriptMember>{ ZM_REPLICATED(BenchActor, Health) });
    double clock = 1000.0;
    Net::SetTimeSource([&] { return clock; });
    auto network = Net::LoopbackNetwork::Create();
    Net::SessionSettings settings;
    settings.Transport.ProjectId = 77;
    settings.Transport.MaxClients = uint32_t(clients);
    settings.Transport.TimeoutSeconds = 60;
    settings.Transport.MaxPacketsPerSecond = 10000;
    settings.Transport.MaxBytesPerSecond = 64 * 1024 * 1024;
    settings.TickRate = 30;
    settings.RelevanceRadius = radius;
    settings.SnapshotBudget = budget;
    const float dt = 1.0f / float(settings.TickRate);

    std::printf("Network replication (%s build): %d clients, %d entities, interest radius %.0f m, %zu B snapshot budget, %.1f s at %u Hz\n", build, clients,
                entities, radius, budget, seconds, settings.TickRate);
    auto server = MakeScene(entities);
    Net::Session host;
    if (!host.StartServer(*server, network->Open(7777), settings)) return 3;
    std::vector<std::unique_ptr<Scene>> scenes;
    std::vector<std::unique_ptr<Net::Session>> sessions;
    for (int c = 0; c < clients; ++c)
    {
        scenes.push_back(MakeScene(entities));
        sessions.push_back(std::make_unique<Net::Session>());
        if (!sessions.back()->StartClient(*scenes.back(), network->Open(), Net::Address::Loopback(7777), settings)) return 3;
    }
    auto step = [&](std::vector<double>* serverMs, std::vector<double>* clientMs) {
        clock += dt;
        // The server moves every entity a little and changes a field now and then.
        auto& registry = server->GetRegistry();
        int k = 0;
        for (auto e : registry.view<NetworkIdentityComponent, TransformComponent>())
        {
            auto& t = registry.get<TransformComponent>(e).Translation;
            const float a = 0.02f * float(1 + (k % 5));
            t = { t.x * std::cos(a) - t.y * std::sin(a), t.x * std::sin(a) + t.y * std::cos(a), 0.0f };
            if (k % 97 == int(clock * 30.0) % 97)
                if (auto* actor = dynamic_cast<BenchActor*>(registry.get<NativeScriptComponent>(e).Instance)) actor->Health = (actor->Health + 7) % 100;
            ++k;
        }
        const auto s0 = std::chrono::steady_clock::now();
        server->UpdateRuntime(dt);
        const auto s1 = std::chrono::steady_clock::now();
        for (auto& scene : scenes) scene->UpdateRuntime(dt);
        const auto s2 = std::chrono::steady_clock::now();
        if (serverMs) serverMs->push_back(Ms(s1 - s0));
        if (clientMs) clientMs->push_back(Ms(s2 - s1) / double(clients));
    };
    // Connect, then give every client one entity of its own (the view interest is measured from).
    for (int i = 0; i < 60; ++i) step(nullptr, nullptr);
    int connected = 0;
    for (auto& s : sessions) connected += s->IsConnected() ? 1 : 0;
    {
        auto& registry = server->GetRegistry();
        for (auto e : registry.view<NetworkIdentityComponent>())
        {
            auto& id = registry.get<NetworkIdentityComponent>(e);
            if (id.NetId <= uint32_t(clients)) id.Owner = id.NetId; // client ids and the first network ids both start at 1
        }
    }
    for (int i = 0; i < 60; ++i) step(nullptr, nullptr);
    // The measured run.
    const auto before = host.GetStats();
    std::vector<uint64_t> sentBefore;
    for (const auto& c : host.GetClients()) sentBefore.push_back(c.Stats.BytesSent);
    std::vector<double> serverMs, clientMs, tickMs;
    const int frames = int(seconds * settings.TickRate);
    for (int i = 0; i < frames; ++i)
    {
        step(&serverMs, &clientMs);
        tickMs.push_back(host.GetStats().TickMs);
    }
    const auto after = host.GetStats();
    const auto infos = host.GetClients();
    double bytes = 0, relevant = 0;
    for (size_t i = 0; i < infos.size(); ++i)
    {
        bytes += double(infos[i].Stats.BytesSent - (i < sentBefore.size() ? sentBefore[i] : 0));
        relevant += double(infos[i].Relevant);
    }
    const double perClientBps = bytes / double(std::max<size_t>(1, infos.size())) / seconds;
    relevant /= double(std::max<size_t>(1, infos.size()));
    const uint64_t snapshots = after.SnapshotsSent - before.SnapshotsSent;
    // Correctness: a client shows the entities interpolated (InterpolationDelay behind the server). With the world
    // standing still for a few ticks, every entity a client has must be exactly where the server has it.
    bool positionsOk = true;
    size_t checked = 0;
    {
        auto& registry = server->GetRegistry();
        std::vector<glm::vec3> serverAt;
        for (int i = 0; i < 20; ++i)
        {
            clock += dt;
            server->UpdateRuntime(dt);
            for (auto& scene : scenes) scene->UpdateRuntime(dt);
        }
        for (int c = 0; c < std::min(clients, 4); ++c)
            for (auto e : scenes[size_t(c)]->GetRegistry().view<NetworkIdentityComponent>())
            {
                const auto& id = scenes[size_t(c)]->GetRegistry().get<NetworkIdentityComponent>(e);
                if (!id.NetId || !sessions[size_t(c)]->FindEntity(id.NetId)) continue; // (hidden: out of its interest)
                Entity theirs = host.FindEntity(id.NetId);
                const glm::vec3 a = scenes[size_t(c)]->GetRegistry().get<TransformComponent>(e).Translation;
                const glm::vec3 b = registry.get<TransformComponent>(theirs).Translation;
                ++checked;
                if (glm::length(a - b) > 1e-3f) positionsOk = false;
            }
    }
    const bool allConnected = connected == clients;
    const bool interest = relevant > 0 && relevant < double(entities);
    const bool snapshotsOk = snapshots >= uint64_t(frames) * uint64_t(clients) * 9 / 10;
    std::printf("\n  server frame        %8.3f ms avg  %8.3f ms p95  (the scene's update with the session)\n", std::accumulate(serverMs.begin(), serverMs.end(), 0.0) / double(serverMs.size()),
                Percentile(serverMs, 0.95));
    std::printf("  session tick        %8.3f ms avg  %8.3f ms p95  (snapshots, deltas, interest, inputs)\n", std::accumulate(tickMs.begin(), tickMs.end(), 0.0) / double(tickMs.size()),
                Percentile(tickMs, 0.95));
    std::printf("  client frame        %8.3f ms avg  (decode, apply, interpolate; per client)\n", std::accumulate(clientMs.begin(), clientMs.end(), 0.0) / double(clientMs.size()));
    std::printf("  sent per client     %8.2f KB/s   (%.0f B a snapshot)\n", perClientBps / 1024.0, perClientBps / double(settings.TickRate));
    std::printf("  entities per client %8.1f of %d   (interest)\n", relevant, entities);
    std::printf("  snapshots           %8llu sent   (%d frames x %d clients)\n", (unsigned long long)snapshots, frames, clients);
    std::printf("  invariants: connected %d/%d %s, interest %s, snapshots every tick %s, positions match (%zu checked) %s\n", connected, clients, allConnected ? "ok" : "FAIL",
                interest ? "ok" : "FAIL", snapshotsOk ? "ok" : "FAIL", checked, positionsOk ? "ok" : "FAIL");
    if (!json.empty())
    {
        const double serverAvg = std::accumulate(serverMs.begin(), serverMs.end(), 0.0) / double(serverMs.size());
        const double tickAvg = std::accumulate(tickMs.begin(), tickMs.end(), 0.0) / double(tickMs.size());
        const double clientAvg = std::accumulate(clientMs.begin(), clientMs.end(), 0.0) / double(clientMs.size());
        const std::string text = std::format(
            "{{\n  \"Build\": \"{}\",\n  \"Clients\": {},\n  \"Entities\": {},\n  \"Seconds\": {},\n  \"RelevanceRadius\": {},\n  \"SnapshotBudget\": {},\n"
            "  \"Results\": {{\n"
            "    \"ServerFrameMs\": {{ \"Ms\": {:.4f} }},\n    \"ServerFrameP95Ms\": {{ \"Ms\": {:.4f} }},\n    \"SessionTickMs\": {{ \"Ms\": {:.4f} }},\n"
            "    \"SessionTickP95Ms\": {{ \"Ms\": {:.4f} }},\n    \"ClientFrameMs\": {{ \"Ms\": {:.4f} }},\n"
            "    \"BytesPerClientPerSecond\": {{ \"Value\": {:.0f}, \"Unit\": \"B\" }},\n    \"EntitiesPerClient\": {{ \"Value\": {:.1f}, \"Unit\": \"entities\" }}\n  }},\n"
            "  \"Invariants\": {{ \"Connected\": {}, \"Interest\": {}, \"SnapshotsEveryTick\": {}, \"PositionsMatch\": {} }}\n}}\n",
            build, clients, entities, seconds, radius, budget, serverAvg, Percentile(serverMs, 0.95), tickAvg, Percentile(tickMs, 0.95), clientAvg, perClientBps, relevant,
            allConnected, interest, snapshotsOk, positionsOk);
        FileSystem::WriteTextFile(json, text);
        std::printf("\nJSON: %s\n", json.c_str());
    }
    for (auto& s : sessions) s->Stop();
    host.Stop();
    for (auto& scene : scenes) scene->OnRuntimeStop();
    server->OnRuntimeStop();
    Net::SetTimeSource({});
    return allConnected && interest && snapshotsOk && positionsOk ? 0 : 3;
}
