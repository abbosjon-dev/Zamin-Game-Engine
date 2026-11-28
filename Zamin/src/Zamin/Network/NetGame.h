#pragma once

/// @file
/// @brief Starting a networked game the way the Player and the editor do (docs/Networking.md): the session
/// settings of a project (its id from the name, its protocol from the game's version), a UDP socket (optionally
/// through a link conditioner), a client's saved session (resume after a restart), a bot that plays, and the
/// session's report (state and statistics as JSON).

#include "Zamin/Network/NetSession.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <memory>
#include <string>

namespace Zamin {
    struct ProjectConfig;
}

namespace Zamin::Net {

    constexpr uint16_t DefaultPort = 7777;

    /// A project's session settings: ProjectId from its name, ProtocolVersion from its version (another build of
    /// the game is refused), the rest the defaults.
    SessionSettings SettingsFor(const ProjectConfig& config);

    struct LaunchOptions
    {
        bool Server = false, Host = false;
        uint16_t Port = DefaultPort;  ///< server: the port to bind (0: any free one)
        std::string Connect;          ///< client: host:port
        std::string Conditioner;      ///< ConditionerSettings::Parse ("" / "off": none)
        std::filesystem::path SessionFile; ///< client: read to resume, written once connected
        uint32_t MaxClients = 0, TickRate = 0; ///< 0: the settings'
    };

    /// Starts the session on the running scene as the options say; false with a reason (bad address, port taken,
    /// bad conditioner text).
    bool Launch(Session& session, Scene& scene, const SessionSettings& settings, const LaunchOptions& options, std::string* error = nullptr);

    /// A client's session file: "<client id> <resume token>"; Save writes it whenever the session is connected
    /// (call it every frame - it writes only on a change).
    bool LoadSessionFile(const std::filesystem::path& file, uint32_t& client, uint64_t& token);
    void SaveSessionFile(Session& session, const std::filesystem::path& file);

    /// A player that plays by itself (tests, --net-bot): moves its own predicted entity on a circle of its own
    /// (by client id) and taps `action` (a key bound to it) every `period` seconds.
    class Bot
    {
    public:
        void Attach(Session& session, std::string action = "Collect", double period = 0.5);
        /// Once a frame before the scene's update (presses / releases the action's key).
        void Update(float dt);

    private:
        Session* m_Session = nullptr;
        std::string m_Action;
        double m_Period = 0.5, m_Time = 0, m_Next = 0.3;
        bool m_Down = false;
    };

    /// The session as JSON: role, client id, tick, statistics, the connection(s) (RTT, loss, bandwidth) and every
    /// networked entity (network id, owner, name, position, replicated values).
    nlohmann::json Report(const Session& session);

}
