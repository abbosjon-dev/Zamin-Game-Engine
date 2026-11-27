#include "VisualGraph.h"
#include <algorithm>
// Native method calls (format 6): Native.Call (execution) and Native.Pure (data) nodes name a reflected C++ method
// or free function in their literal Method; the graph keeps the signature it was built with (Graph::Natives), so
// the node's pins and wires do not depend on the C++ module being loaded.
namespace Zamin::Visual {
    std::string NativeKey(const Node& n)
    {
        if (n.Kind != "Native.Call" && n.Kind != "Native.Pure") return {};
        auto it = n.Inputs.find("Method");
        return it != n.Inputs.end() && it->second.is_string() ? it->second.get<std::string>() : "";
    }
    bool NativeSignature::Matches(const NativeSignature& other) const
    {
        auto same = [](const std::vector<Pin>& a, const std::vector<Pin>& b) {
            return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](const Pin& x, const Pin& y) { return x.Name == y.Name && SameType(x, y); });
        };
        return Static == other.Static && Pure == other.Pure && same(Inputs, other.Inputs) && same(Outputs, other.Outputs);
    }
    std::string NativeSignature::Text() const
    {
        auto pin = [](const Pin& p) { return p.Name + ": " + TypeName(p.Kind) + (p.Enum.empty() ? "" : " " + p.Enum); };
        std::string text = std::string(Static ? "static " : "") + (Pure ? "pure " : "") + "(";
        for (size_t i = 0; i < Inputs.size(); ++i) text += (i ? ", " : "") + pin(Inputs[i]);
        text += ")";
        if (!Outputs.empty()) text += " -> " + pin(Outputs.front());
        return text;
    }
    std::set<std::string> Graph::NativesUsed() const
    {
        std::set<std::string> used;
        for (const auto& n : Nodes)
            if (auto key = NativeKey(n); !key.empty()) used.insert(key);
        return used;
    }
    size_t Graph::SetNative(const std::string& key, NativeSignature signature)
    {
        Natives[key] = std::move(signature);
        return PruneInvalidPins();
    }
    Definition Graph::DescribeNative(const Node& n, Definition result) const
    {
        auto signature = Natives.find(NativeKey(n));
        if (signature == Natives.end()) return result;
        // An instance method runs on the script of its Target entity (an omitted Target means Self, as for the
        // reflected fields).
        if (!signature->second.Static) result.Inputs.push_back({ "Target", Type::Entity, UINT64_MAX });
        result.Inputs.insert(result.Inputs.end(), signature->second.Inputs.begin(), signature->second.Inputs.end());
        result.Outputs.insert(result.Outputs.end(), signature->second.Outputs.begin(), signature->second.Outputs.end());
        return result;
    }
    std::vector<Diagnostic> Graph::ValidateNatives() const
    {
        std::vector<Diagnostic> d;
        auto error = [&](uint32_t n, std::string s) { d.push_back({ n, std::move(s) }); };
        if (Natives.size() > MaxNatives) error(0, "Native method signature limit exceeds " + std::to_string(MaxNatives));
        for (const auto& [key, signature] : Natives)
        {
            const auto dot = key.rfind('.');
            if (key.size() > 128 || dot == std::string::npos || dot == 0 || dot + 1 == key.size()) error(0, "Invalid native method name: " + key);
            if (signature.Inputs.size() > 32 || signature.Outputs.size() > 1) error(0, "Native method " + key + " has more than 32 parameters or more than one result");
            if (signature.Pure && signature.Outputs.empty()) error(0, "Pure native method " + key + " has no result");
            if (signature.Tooltip.size() > 1024) error(0, "Native method " + key + " tooltip exceeds 1 KiB");
            for (const auto* pins : { &signature.Inputs, &signature.Outputs })
                for (std::set<std::string> names; const auto& pin : *pins)
                    if (pin.Name.empty() || pin.Name.size() > 64 || pin.Name == "In" || pin.Name == "Then" || pin.Name == "Method" || pin.Name == "Target" || !names.insert(pin.Name).second || pin.Kind == Type::Exec || pin.Kind >= Type::Count || !Fits(pin, pin.Default))
                        error(0, "Invalid/duplicate/reserved pin in native method " + key);
        }
        for (const auto& n : Nodes)
        {
            if (n.Kind != "Native.Call" && n.Kind != "Native.Pure") continue;
            const auto key = NativeKey(n);
            auto signature = Natives.find(key);
            if (signature == Natives.end()) error(n.ID, "Unknown native method (no signature in the graph): " + key);
            else if (signature->second.Pure != (n.Kind == "Native.Pure")) error(n.ID, "Native method " + key + (signature->second.Pure ? " is pure: use Native.Pure" : " is not pure: use Native.Call"));
        }
        return d;
    }
}
