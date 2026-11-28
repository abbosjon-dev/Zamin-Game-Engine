#pragma once

/// @file
/// @brief Networking for C++ scripts (docs/Networking.md): the role of this process, ownership, RPC calls and
/// networked spawns. Without a network session a game is its own server (IsServer true, RPCs run at once).
///
/// @code
///   ZM_REGISTER_SCRIPT(Player, ZM_REPLICATED(Player, Score),
///       ZM_SERVER_RPC(Player, Collect, "Coin"), ZM_MULTICAST_RPC(Player, Sparkle, "At").Unreliable())
///   void OnUpdate(Timestep) override
///   {
///       if (Net::IsOwner(*this) && Input::IsActionJustPressed("Collect")) Net::CallRpc(*this, "Collect", coin);
///   }
///   void Collect(EntityRef coin) { if (Net::IsServer(*this)) { ++Score; Net::CallRpc(*this, "Sparkle", at); } }
/// @endcode

#include "Zamin/Core/Log.h"
#include "Zamin/Network/NetSession.h"
#include "Zamin/Scene/Components.h"
#include "Zamin/Scripting/ScriptableEntity.h"

#include <string>
#include <type_traits>

namespace Zamin::Net {

    inline Session* SessionOf(const ScriptableEntity& self) { return Session::Of(self.GetScene()); }
    inline bool IsServer(const ScriptableEntity& self) { return IsServer(self.GetScene()); }
    inline bool IsClient(const ScriptableEntity& self) { return IsClient(self.GetScene()); }
    /// This side's own entity (a client's; on the server the ones the server owns).
    inline bool IsOwner(const ScriptableEntity& self) { return IsOwner(self.GetEntity()); }
    /// The local player's client id (0: the host, a server, no session).
    inline uint32_t LocalClient(const ScriptableEntity& self)
    {
        auto* s = SessionOf(self);
        return s ? s->GetLocalClient() : 0;
    }
    /// The owner of the script's entity (0: the server).
    inline uint32_t OwnerOf(const ScriptableEntity& self)
    {
        auto* id = self.GetEntity().TryGetComponent<NetworkIdentityComponent>();
        return id ? id->Owner : 0;
    }

    /// Calls an RPC of this script's class (ZM_SERVER_RPC / ZM_CLIENT_RPC / ZM_MULTICAST_RPC) with typed arguments;
    /// false (logged) when it cannot be called from here or the arguments do not fit.
    template<typename... Args>
    bool CallRpc(ScriptableEntity& self, const std::string& method, const Args&... args)
    {
        nlohmann::json list = nlohmann::json::array();
        (list.push_back(ScriptReflection::Marshal<std::remove_cvref_t<Args>>::To(args)), ...);
        std::string error;
        if (Call(self.GetEntity(), method, list, false, &error)) return true;
        ZM_CORE_WARN("RPC {}: {}", method, error);
        return false;
    }

    /// Server: spawns a networked prefab (owner 0: the server's); without a session a plain Instantiate. Invalid on
    /// a client.
    inline Entity Spawn(ScriptableEntity& self, const std::string& prefab, const glm::vec3& position, uint32_t owner = 0)
    {
        if (auto* s = SessionOf(self)) return s->Spawn(prefab, position, owner);
        return self.Instantiate(prefab, position);
    }
    /// Server: destroys a networked entity on every side; without a session a plain Destroy.
    inline void Despawn(ScriptableEntity& self, Entity entity)
    {
        auto* s = SessionOf(self);
        if (!s) self.Destroy(entity);
        else if (s->IsServer()) s->Despawn(entity);
    }

}
