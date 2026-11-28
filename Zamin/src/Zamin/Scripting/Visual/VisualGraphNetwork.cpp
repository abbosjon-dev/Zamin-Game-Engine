#include "VisualGraph.h"
#include <algorithm>
#include <set>
// Networking in graphs (format 7, Network/NetSession.h): graph-defined RPCs (Graph::Rpcs; an Event.Rpc node handles
// one, the Net.Call*Rpc nodes call it, both take their parameter pins from the definition), replicated variables
// (Variable::Replicated; an Event.Rep node named after one runs on a client when a new value arrived) and the role,
// ownership, connection and spawn nodes, which the host (VisualGraphHost) answers.
namespace Zamin::Visual {
    namespace {
        std::string LiteralName(const Node& n)
        {
            auto it = n.Inputs.find("Name");
            return it != n.Inputs.end() && it->second.is_string() ? it->second.get<std::string>() : std::string();
        }
    }
    const char* RpcTargetName(RpcDefinition::Target target)
    {
        switch (target)
        {
            case RpcDefinition::Target::Server: return "Server";
            case RpcDefinition::Target::Client: return "Client";
            case RpcDefinition::Target::Multicast: return "Multicast";
        }
        return "Server";
    }
    const char* RpcCallKind(RpcDefinition::Target target)
    {
        switch (target)
        {
            case RpcDefinition::Target::Server: return "Net.CallServerRpc";
            case RpcDefinition::Target::Client: return "Net.CallClientRpc";
            case RpcDefinition::Target::Multicast: return "Net.CallMulticastRpc";
        }
        return "Net.CallServerRpc";
    }
    bool IsNetworkNode(const std::string& kind)
    {
        return kind.starts_with("Net.") || kind == "Event.Rpc" || kind == "Event.Rep" || kind == "Event.ClientConnected" || kind == "Event.ClientDisconnected";
    }
    Definition Graph::DescribeRpc(const Node& n, Definition result) const
    {
        auto rpc = Rpcs.find(LiteralName(n));
        if (rpc == Rpcs.end()) return result;
        if (n.Kind == "Event.Rpc") result.Outputs.insert(result.Outputs.end(), rpc->second.Params.begin(), rpc->second.Params.end());
        else result.Inputs.insert(result.Inputs.end(), rpc->second.Params.begin(), rpc->second.Params.end());
        return result;
    }
    std::vector<Diagnostic> Graph::ValidateNetwork() const
    {
        std::vector<Diagnostic> d;
        auto error = [&](uint32_t n, std::string s) { d.push_back({ n, std::move(s) }); };
        if (Rpcs.size() > MaxRpcs) error(0, "Network RPC limit exceeds " + std::to_string(MaxRpcs));
        for (const auto& [name, rpc] : Rpcs)
        {
            if (name.empty() || name.size() > 64) error(0, "Invalid RPC name: " + name);
            if (rpc.Params.size() > MaxRpcParams) error(0, "RPC " + name + " has more than " + std::to_string(MaxRpcParams) + " parameters");
            if (rpc.Kind != RpcDefinition::Target::Server && rpc.AnyClient) error(0, "RPC " + name + ": AnyClient is for server RPCs only");
            std::set<std::string> names;
            for (const auto& pin : rpc.Params)
                if (pin.Name.empty() || pin.Name.size() > 64 || pin.Name == "In" || pin.Name == "Then" || pin.Name == "Name" || pin.Name == "Sender" || !names.insert(pin.Name).second || pin.Kind == Type::Exec ||
                    pin.Kind >= Type::Count || !Fits(pin, pin.Default))
                    error(0, "Invalid/duplicate/reserved parameter in RPC " + name);
        }
        std::set<std::string> handled;
        for (const auto& n : Nodes)
        {
            if (n.Kind == "Event.Rpc" || n.Kind.ends_with("Rpc"))
            {
                if (!n.Kind.starts_with("Net.Call") && n.Kind != "Event.Rpc") continue;
                const auto name = LiteralName(n);
                auto rpc = Rpcs.find(name);
                if (rpc == Rpcs.end())
                {
                    error(n.ID, "Unknown RPC (define it in the graph's RPCs): " + name);
                    continue;
                }
                if (n.Kind == "Event.Rpc")
                {
                    // An inherited handler may be overridden like any event; two of this graph's own are an error.
                    if (!Inherited.Origins.contains(n.ID) && !handled.insert(name).second) error(n.ID, "RPC " + name + " has more than one Event.Rpc handler");
                }
                else if (n.Kind != RpcCallKind(rpc->second.Kind))
                    error(n.ID, std::string("RPC ") + name + " is a " + RpcTargetName(rpc->second.Kind) + " RPC: use " + RpcCallKind(rpc->second.Kind));
            }
            if (n.Kind == "Event.Rep")
            {
                const auto name = LiteralName(n);
                auto variable = Variables.find(name);
                if (variable == Variables.end() || !variable->second.Replicated) error(n.ID, "Event.Rep names no replicated variable: " + name);
            }
        }
        size_t replicated = 0;
        for (const auto& [name, variable] : Variables)
            if (variable.Replicated && ++replicated == 65) error(0, "More than 64 replicated variables");
        return d;
    }
    bool Machine::SetVariable(const std::string& name, Value value)
    {
        auto definition = m_Graph.Variables.find(name);
        auto stored = m_Variables.find(name);
        if (definition == m_Graph.Variables.end() || stored == m_Variables.end() || !m_Graph.Fits(definition->second.Kind, definition->second.Enum, value)) return false;
        const size_t before = m_VariableBytes;
        Value previous = std::move(stored->second);
        m_VariableBytes = m_VariableBytes - ValueBytes(previous) + ValueBytes(value);
        stored->second = std::move(value);
        try
        {
            CheckStorage();
        }
        catch (const std::exception&)
        {
            stored->second = std::move(previous);
            m_VariableBytes = before;
            return false;
        }
        return true;
    }
}
