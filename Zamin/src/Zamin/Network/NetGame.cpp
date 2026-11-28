#include "Zamin/Network/NetGame.h"

#include "Zamin/Core/GameState.h"
#include "Zamin/Core/Input.h"
#include "Zamin/Network/NetCodec.h"
#include "Zamin/Project/Project.h"
#include "Zamin/Scene/Components.h"
#include "Zamin/Scene/Scene.h"
#include "Zamin/Utils/FileSystem.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <format>
#include <fstream>
#include <sstream>

namespace Zamin::Net {

    SessionSettings SettingsFor(const ProjectConfig& config)
    {
        SessionSettings s;
        s.Transport.ProjectId = Hash64(config.Name);
        s.Transport.ProtocolVersion = uint16_t(1 + Hash64(config.Player.Version) % 65000);
        return s;
    }

    bool Launch(Session& session, Scene& scene, const SessionSettings& base, const LaunchOptions& options, std::string* error)
    {
        SessionSettings settings = base;
        settings.Host = options.Host;
        // ZAMIN_NET_STATE_LOG=N (tests): the NETSTATE lines every N ticks.
        if (const char* every = std::getenv("ZAMIN_NET_STATE_LOG")) settings.StateLogEvery = uint32_t(std::clamp(std::atoi(every), 0, 100000));
        if (options.MaxClients) settings.Transport.MaxClients = options.MaxClients;
        if (options.TickRate) settings.TickRate = options.TickRate;
        ConditionerSettings conditioner;
        if (!options.Conditioner.empty() && !ConditionerSettings::Parse(options.Conditioner, conditioner, error)) return false;
        auto wrap = [&](std::unique_ptr<Socket> socket) -> std::unique_ptr<Socket> {
            // Always through a conditioner (off unless asked): the editor's Network panel turns it on later.
            return std::make_unique<LinkConditioner>(std::move(socket), conditioner);
        };
        if (options.Server)
        {
            auto socket = UdpSocket::Open(options.Port, false, error);
            if (!socket) return false;
            return session.StartServer(scene, wrap(std::move(socket)), settings, error);
        }
        Address server;
        if (!Address::Parse(options.Connect, server, error)) return false;
        auto socket = UdpSocket::Open(0, false, error);
        if (!socket) return false;
        uint32_t id = 0;
        uint64_t token = 0;
        if (!options.SessionFile.empty() && LoadSessionFile(options.SessionFile, id, token))
            ZM_CORE_INFO("[Network] Resuming session {} from {}", id, options.SessionFile.string());
        return session.StartClient(scene, wrap(std::move(socket)), server, settings, id, token, error);
    }

    bool LoadSessionFile(const std::filesystem::path& file, uint32_t& client, uint64_t& token)
    {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(file, ec)) return false;
        std::istringstream in(FileSystem::ReadTextFile(file));
        uint64_t id = 0;
        if (!(in >> id >> token) || id == 0 || id > UINT32_MAX || token == 0) return false;
        client = uint32_t(id);
        return true;
    }

    void SaveSessionFile(Session& session, const std::filesystem::path& file)
    {
        if (file.empty() || !session.IsClient() || !session.IsConnected()) return;
        const std::string text = std::format("{} {}\n", session.GetLocalClient(), session.GetResumeToken());
        std::error_code ec;
        if (std::filesystem::is_regular_file(file, ec) && FileSystem::ReadTextFile(file) == text) return;
        FileSystem::WriteTextFile(file, text);
    }

    void Bot::Attach(Session& session, std::string action, double period)
    {
        m_Session = &session;
        m_Action = std::move(action);
        // A game that steers its own bot (NetArena) reads this; the circle below is the fallback.
        GameState::SetBool("Net.Bot", true);
        m_Period = period;
        session.SetInputSource([this, &session](Entity) {
            // A circle of its own per client (radius and phase), changing direction now and then.
            const double id = double(session.GetLocalClient());
            const double angle = m_Time * (0.8 + 0.15 * id) + id * 1.7;
            return glm::vec2(float(std::cos(angle)), float(std::sin(angle)));
        });
    }

    void Bot::Update(float dt)
    {
        if (!m_Session) return;
        m_Time += dt;
        const InputAction* action = Project::GetActive() ? Project::GetInputActionMap().FindAction(m_Action) : nullptr;
        Key key = Key::Space;
        if (action)
            for (const auto& binding : action->Bindings)
                if (binding.Source == InputSource::Key)
                {
                    key = Key(binding.Code);
                    break;
                }
        if (m_Down)
        {
            Input::SimulateKey(key, false);
            m_Down = false;
        }
        else if (m_Time >= m_Next && m_Session->IsConnected())
        {
            Input::SimulateKey(key, true);
            m_Down = true;
            m_Next = m_Time + m_Period;
        }
    }

    nlohmann::json Report(const Session& session)
    {
        using json = nlohmann::json;
        json r;
        r["Role"] = session.IsServer() ? (session.IsHost() ? "host" : "server") : session.IsClient() ? "client" : "none";
        r["Client"] = session.GetLocalClient();
        r["Connected"] = session.IsConnected();
        const SessionStats st = session.GetStats();
        r["Tick"] = st.Tick;
        r["Stats"] = { { "NetworkedEntities", st.NetworkedEntities }, { "SnapshotsSent", st.SnapshotsSent }, { "SnapshotsReceived", st.SnapshotsReceived },
                       { "SnapshotsDropped", st.SnapshotsDropped }, { "SnapshotBytes", st.SnapshotBytes }, { "RpcsSent", st.RpcsSent }, { "RpcsReceived", st.RpcsReceived },
                       { "RpcsRejected", st.RpcsRejected }, { "InputsReceived", st.InputsReceived }, { "InputsMissed", st.InputsMissed }, { "Corrections", st.Corrections },
                       { "TickMs", st.TickMs }, { "AverageTickMs", st.AverageTickMs }, { "MaxTickMs", st.MaxTickMs } };
        auto connection = [](const ConnectionStats& c) {
            return json{ { "RttMs", c.RttMs }, { "LossPercent", c.LossPercent }, { "SentBytesPerSecond", c.SentBytesPerSecond },
                         { "ReceivedBytesPerSecond", c.ReceivedBytesPerSecond }, { "BytesSent", c.BytesSent }, { "BytesReceived", c.BytesReceived },
                         { "PacketsSent", c.PacketsSent }, { "PacketsReceived", c.PacketsReceived }, { "ReliableResent", c.ReliableResent } };
        };
        if (const auto* conn = session.GetServerConnection()) r["Connection"] = connection(conn->GetStats());
        r["Clients"] = json::array();
        for (const auto& c : session.GetClients())
        {
            json entry = connection(c.Stats);
            entry["Id"] = c.Id;
            entry["Connected"] = c.Connected;
            entry["Relevant"] = c.Relevant;
            r["Clients"].push_back(std::move(entry));
        }
        r["Entities"] = session.DescribeEntities();
        return r;
    }

}
