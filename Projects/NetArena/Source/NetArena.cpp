// Net Arena - the networking sample of Zamin Engine (docs/Networking.md, docs/Samples.md).
//
// A 2D arena for 2+ players. The server is the authority: the ArenaRules graph spawns a player for every client that
// connects (Net.Spawn), the ArenaDirector keeps coins on the field (Net::Spawn), and a player's Collect is a server RPC
// the server checks (the coin must be within reach of where the server has the player) before it adds to the
// replicated Score and multicasts a Burst effect to everyone. Movement is client-side predicted
// (NetworkTransformComponent Predict); the others are interpolated.
//
//   ZaminPlayer Projects/NetArena --server                  a dedicated server on port 7777 (no window)
//   ZaminPlayer Projects/NetArena --connect 127.0.0.1:7777  a player (more than one: start it again)
//   Editor: Play with 2+ clients (the Network panel)
// Without a network (Play alone, the sample tests) the game is its own server: one local player, ZAMIN_DEMO=1 lets a
// bot play.
#include <Zamin/Zamin.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <format>
#include <limits>
#include <map>

using namespace Zamin;

namespace {
    glm::vec4 ColorFor(uint32_t client)
    {
        static const glm::vec4 palette[] = { { 0.35f, 0.85f, 1.0f, 1.0f }, { 1.0f, 0.45f, 0.4f, 1.0f },  { 0.5f, 1.0f, 0.45f, 1.0f },
                                             { 1.0f, 0.7f, 0.25f, 1.0f },  { 0.85f, 0.5f, 1.0f, 1.0f }, { 0.95f, 0.95f, 0.95f, 1.0f } };
        return palette[client % 6];
    }

    // The coins on this side (both sides: replicated entities named "Coin").
    std::vector<Entity> Coins(Scene& scene)
    {
        std::vector<Entity> out;
        auto& registry = scene.GetRegistry();
        for (auto e : registry.view<NetworkIdentityComponent, TagComponent>())
            if (registry.get<TagComponent>(e).Tag == "Coin") out.push_back({ e, &scene });
        return out;
    }

    bool BotMode()
    {
        static const bool demo = std::getenv("ZAMIN_DEMO") != nullptr;
        return demo || GameState::GetBool("Net.Bot", false);
    }
}

// A player: moved by its owner (predicted), its score replicated; Collect is a server RPC, Burst a multicast one.
class NetPlayer : public ScriptableEntity
{
public:
    int Score = 0;
    glm::vec4 Tint{ 1.0f };

    void OnStart() override
    {
        if (Net::IsServer(*this)) Tint = ColorFor(Net::OwnerOf(*this));
        if (Net::IsOwner(*this))
            ZM_INFO("NETARENA my player is ready (client {}{})", Net::LocalClient(*this), Net::SessionOf(*this) ? "" : ", no network");
    }

    void OnDestroy() override
    {
        if (auto* session = Net::SessionOf(*this); session && m_Steering) session->SetInputSource({});
    }

    void OnUpdate(Timestep dt) override
    {
        if (auto* sprite = TryGetComponent<SpriteRendererComponent>()) sprite->Color = Tint;
        if (!Net::IsOwner(*this)) return;
        m_Cooldown -= dt;
        auto* session = Net::SessionOf(*this);
        bool collect = Input::IsActionJustPressed("Collect");
        if (BotMode())
        {
            // The bot: towards the nearest coin, Collect once it is within reach.
            glm::vec2 move{ 0.0f };
            const glm::vec3 at = Transform().Translation;
            float best = std::numeric_limits<float>::max();
            for (Entity coin : Coins(GetScene()))
            {
                const glm::vec3 d = coin.GetComponent<TransformComponent>().Translation - at;
                const float distance = glm::length(glm::vec2(d));
                if (distance < best)
                {
                    best = distance;
                    move = distance > 0.05f ? glm::vec2(d) / distance : glm::vec2(0.0f);
                }
            }
            m_BotMove = move;
            if (session && !m_Steering)
            {
                session->SetInputSource([this](Entity) { return m_BotMove; });
                m_Steering = true;
            }
            collect = best < 0.9f && m_Cooldown <= 0.0f;
        }
        if (!session)
        {
            // Alone: this side is the server and moves its own player itself.
            const auto& nt = GetComponent<NetworkTransformComponent>();
            const glm::vec2 input = BotMode() ? m_BotMove : Input::GetVector(nt.MoveAction);
            Transform().Translation = Net::Session::Move(nt, Transform().Translation, input, dt);
        }
        if (collect && m_Cooldown <= 0.0f)
        {
            m_Cooldown = 0.35f;
            Net::CallRpc(*this, "Collect");
        }
    }

    // Server RPC (its owner calls it): the nearest coin within reach of where the server has this player.
    void Collect()
    {
        if (!Net::IsServer(*this)) return;
        const glm::vec3 at = Transform().Translation;
        Entity nearest;
        float best = 1.5f;
        for (Entity coin : Coins(GetScene()))
            if (const float d = glm::length(glm::vec2(coin.GetComponent<TransformComponent>().Translation - at)); d <= best)
            {
                best = d;
                nearest = coin;
            }
        if (!nearest)
        {
            ++m_Misses;
            return;
        }
        const glm::vec3 where = nearest.GetComponent<TransformComponent>().Translation;
        Net::Despawn(*this, nearest);
        ++Score;
        ZM_INFO("NETARENA collect by client {}: score {}", Net::OwnerOf(*this), Score);
        if (!Net::SessionOf(*this) && BotMode()) ZM_INFO("NETARENA demo score={}", Score);
        Net::CallRpc(*this, "Burst", where);
    }

    // Multicast RPC (the server calls it, every client the player is relevant to runs it too): a local effect.
    void Burst(glm::vec3 at)
    {
        ++s_Bursts;
        ZM_INFO("NETARENA burst #{} at {:.1f},{:.1f}", s_Bursts, at.x, at.y);
        Instantiate("Prefabs/Spark.zprefab", at);
    }

    static inline int s_Bursts = 0;

private:
    float m_Cooldown = 0.0f;
    glm::vec2 m_BotMove{ 0.0f };
    bool m_Steering = false;
    int m_Misses = 0;
};

// The burst: grows and fades, then goes (local to each side, not networked).
class NetSpark : public ScriptableEntity
{
public:
    float Lifetime = 0.45f;

    void OnUpdate(Timestep dt) override
    {
        m_Age += dt;
        const float t = std::clamp(m_Age / std::max(Lifetime, 0.01f), 0.0f, 1.0f);
        Transform().Scale = glm::vec3(0.4f + 1.6f * t, 0.4f + 1.6f * t, 1.0f);
        if (auto* circle = TryGetComponent<CircleRendererComponent>()) circle->Color.a = 1.0f - t;
        if (m_Age >= Lifetime) Destroy();
    }

private:
    float m_Age = 0.0f;
};

// The arena: the server keeps Coins coins on the field (new ones where a pseudo-random sequence says); every side
// shows the scoreboard and its own status (role, RTT, bandwidth). Alone, it spawns the local player.
class ArenaDirector : public ScriptableEntity
{
public:
    int Coins = 6;
    EntityRef Scoreboard, Status;

    void OnStart() override
    {
        if (!Net::SessionOf(*this))
        {
            Net::Spawn(*this, "Prefabs/Player.zprefab", { 0.0f, 0.0f, 0.0f });
            ZM_INFO("NETARENA no network: playing alone");
        }
    }

    void OnUpdate(Timestep dt) override
    {
        if (Net::IsServer(*this))
        {
            int have = int(::Coins(GetScene()).size());
            for (; have < Coins; ++have)
            {
                m_Seed = m_Seed * 1103515245u + 12345u;
                const float x = float((m_Seed >> 8) % 2000) / 2000.0f * 20.0f - 10.0f;
                m_Seed = m_Seed * 1103515245u + 12345u;
                const float y = float((m_Seed >> 8) % 2000) / 2000.0f * 10.0f - 5.0f;
                Net::Spawn(*this, "Prefabs/Coin.zprefab", { x, y, 0.0f });
            }
        }
        m_Refresh -= dt;
        if (m_Refresh > 0.0f) return;
        m_Refresh = 0.25f;
        // The scoreboard: every player this side has, by owner.
        std::map<uint32_t, int> scores;
        auto& registry = GetScene().GetRegistry();
        for (auto e : registry.view<NativeScriptComponent, NetworkIdentityComponent>())
            if (auto* player = dynamic_cast<NetPlayer*>(registry.get<NativeScriptComponent>(e).Instance))
                scores[registry.get<NetworkIdentityComponent>(e).Owner] = player->Score;
        std::string board;
        for (const auto& [owner, score] : scores) board += std::format("{}Player {}: {}", board.empty() ? "" : "\n", owner, score);
        if (Entity e = Resolve(Scoreboard); e && e.HasComponent<TextComponent>()) e.GetComponent<TextComponent>().Text = board.empty() ? "Waiting for players" : board;
        std::string status = "Offline";
        if (auto* session = Net::SessionOf(*this))
        {
            const auto stats = session->GetStats();
            if (session->IsServer())
                status = std::format("{} | {} clients | tick {}", session->IsHost() ? "Host" : "Server", session->GetClients().size(), stats.Tick);
            else if (const auto* conn = session->GetServerConnection(); conn && session->IsConnected())
            {
                const auto c = conn->GetStats();
                status = std::format("Client {} | RTT {:.0f} ms | loss {:.1f}% | {:.1f} KB/s", session->GetLocalClient(), c.RttMs, c.LossPercent,
                                     c.ReceivedBytesPerSecond / 1024.0);
            }
            else status = session->IsReconnecting() ? "Reconnecting..." : "Connecting...";
        }
        if (Entity e = Resolve(Status); e && e.HasComponent<TextComponent>()) e.GetComponent<TextComponent>().Text = status;
    }

private:
    uint32_t m_Seed = 20261128u;
    float m_Refresh = 0.0f;
};

ZM_REGISTER_SCRIPT(NetPlayer,
    ZM_REPLICATED(NetPlayer, Score).Tooltip("Coins collected (the server's count, replicated)"),
    ZM_REPLICATED(NetPlayer, Tint).Tooltip("The player's colour (the server picks it by owner)"),
    ZM_SERVER_RPC(NetPlayer, Collect).Tooltip("Picks up the nearest coin within reach (the server checks)"),
    ZM_MULTICAST_RPC(NetPlayer, Burst, "At").Unreliable().Tooltip("The burst effect where a coin was taken"))

ZM_REGISTER_SCRIPT(NetSpark, ZM_FIELD_RANGE(NetSpark, Lifetime, 0.05f, 5.0f))

ZM_REGISTER_SCRIPT(ArenaDirector,
    ZM_FIELD_RANGE(ArenaDirector, Coins, 1, 64).Tooltip("Coins the server keeps on the field"),
    ZM_FIELD(ArenaDirector, Scoreboard),
    ZM_FIELD(ArenaDirector, Status))
