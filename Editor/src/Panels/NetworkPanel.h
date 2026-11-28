#pragma once
#include <filesystem>
// The editor's networking (docs/Networking.md): Play with clients - the editor is the host (a server that plays as
// client 0) and the extra players are ZaminPlayer processes that connect to it - and the Network panel: the session's
// role, tick and cost, every client's RTT, loss, bandwidth and replicated entities, RPC counts, the link conditioner
// (applied to the host's packets at once, and to the client processes when they start) and the session's log.
namespace Zamin {
    class Scene;
    bool* NetworkPanelOpen();
    // Play started (the runtime scene is running): with "Play with clients" on, the host session starts on the scene and
    // the client processes are launched (the same project and scene).
    void NetworkPlayStarted(Scene& scene, const std::filesystem::path& scenePath);
    // Play stopped: the session ends, the client processes are stopped.
    void NetworkPlayStopped();
    void DrawNetworkPanel();
}
