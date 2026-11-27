#include "VisualGraph.h"
#include <algorithm>
// Inline macros: Graph::Expand replaces every Macro.Instance by a copy of its macro's body, wired in place of the
// instance's pins. The VM only ever sees the expanded graph; diagnostics and runtime errors of a copied node point
// back to the instance (Expansion::Origins).
namespace Zamin::Visual {
    namespace {
        std::string MacroName(const Node& n)
        {
            auto it = n.Inputs.find("Name");
            return it != n.Inputs.end() && it->second.is_string() ? it->second.get<std::string>() : "";
        }
        const Pin* PinOf(const std::vector<Pin>& pins, const std::string& name)
        {
            auto it = std::find_if(pins.begin(), pins.end(), [&](const Pin& p) { return p.Name == name; });
            return it == pins.end() ? nullptr : &*it;
        }
        // A whole expansion is bounded: copies stop at four times the graph node limit and at 16 nested macros.
        constexpr size_t MaxExpandedNodes = 4 * 4096;
        constexpr int MaxNesting = 16;
    }
    Graph::Expansion Graph::Expand() const
    {
        Expansion ex;
        auto error = [&](uint32_t id, std::string message) { ex.Errors.push_back({ id, std::move(message) }); };
        uint64_t next = 0;
        std::map<std::string, std::vector<const Node*>> bodies;
        std::map<uint32_t, std::string> owner;
        for (const auto& n : Nodes)
        {
            next = std::max<uint64_t>(next, n.ID);
            owner[n.ID] = n.Macro;
            if (n.Macro.empty()) ex.Nodes.push_back(n);
            else bodies[n.Macro].push_back(&n);
        }
        std::map<std::string, std::vector<Link>> bodyLinks;
        for (const auto& l : Links)
        {
            if (!owner.contains(l.From) || !owner.contains(l.To) || owner.at(l.From) != owner.at(l.To)) continue;
            if (owner.at(l.From).empty()) ex.Links.push_back(l);
            else bodyLinks[owner.at(l.From)].push_back(l);
        }
        std::map<uint32_t, size_t> index;
        std::vector<uint32_t> work;
        std::map<uint32_t, int> depth;
        for (size_t i = 0; i < ex.Nodes.size(); ++i)
        {
            index[ex.Nodes[i].ID] = i;
            if (ex.Nodes[i].Kind == "Macro.Instance") work.push_back(ex.Nodes[i].ID);
        }
        std::set<uint32_t> removed;
        // Every outside wire of an instance of the graph itself is checked against the macro's pins here: after the
        // expansion an unused pin leaves no trace to check.
        auto checkWires = [&](const Node& instance) {
            const auto def = Describe(instance);
            std::set<std::string> inputs, execOutputs;
            for (const auto& [key, value] : instance.Inputs)
            {
                const auto* pin = PinOf(def.Inputs, key);
                if (!pin || pin->Kind == Type::Exec || !Fits(*pin, value)) error(instance.ID, "Invalid input: " + key);
            }
            for (const auto& l : ex.Links)
            {
                if (l.To != instance.ID && l.From != instance.ID) continue;
                const bool into = l.To == instance.ID;
                const auto* pin = PinOf(into ? def.Inputs : def.Outputs, into ? l.Input : l.Output);
                if (!pin || pin->Name == "Name")
                {
                    error(instance.ID, "Pin " + (into ? l.Input : l.Output) + " is not in macro " + MacroName(instance));
                    continue;
                }
                const auto other = index.find(into ? l.From : l.To);
                if (other == index.end()) continue;
                const auto otherDef = Describe(ex.Nodes[other->second]);
                const auto* otherPin = PinOf(into ? otherDef.Outputs : otherDef.Inputs, into ? l.Output : l.Input);
                if (!otherPin || !SameType(*pin, *otherPin)) error(instance.ID, "Link pin types differ or pin missing");
                else if (into && pin->Kind != Type::Exec && !inputs.insert(l.Input).second) error(instance.ID, "Data input already linked");
                else if (!into && pin->Kind == Type::Exec && !execOutputs.insert(l.Output).second) error(instance.ID, "Execution output already linked; use Sequence");
            }
        };
        while (!work.empty())
        {
            const uint32_t id = work.back();
            work.pop_back();
            const Node instance = ex.Nodes[index.at(id)];
            const auto origin = ex.Origins.find(id);
            const uint32_t top = origin == ex.Origins.end() ? id : origin->second.first;
            removed.insert(id);
            const auto name = MacroName(instance);
            const auto macro = Macros.find(name);
            if (macro == Macros.end())
            {
                error(top, "Unknown macro: " + name);
                continue;
            }
            if (depth[id] >= MaxNesting)
            {
                error(top, "Macro nesting exceeds 16");
                continue;
            }
            if (origin == ex.Origins.end()) checkWires(instance);
            std::map<std::string, std::vector<Link>> in, out;
            for (const auto& l : ex.Links)
            {
                if (l.From == id && l.To == id) error(top, "A macro instance cannot be wired to itself");
                else if (l.To == id) in[l.Input].push_back(l);
                else if (l.From == id) out[l.Output].push_back(l);
            }
            std::erase_if(ex.Links, [&](const Link& l) { return l.From == id || l.To == id; });
            // The body's copy: new IDs for every node but the entry and exit, which become the instance's wires.
            uint32_t entry = 0, exit = 0;
            const Node* exitNode = nullptr;
            std::map<uint32_t, uint32_t> copy;
            const auto& body = bodies[name];
            if (ex.Nodes.size() + body.size() > MaxExpandedNodes || next + body.size() > UINT32_MAX)
            {
                error(top, "Expanded macros exceed the node limit");
                break;
            }
            for (const auto* n : body)
            {
                if (n->Kind == "Macro.Entry")
                {
                    entry = n->ID;
                    continue;
                }
                if (n->Kind == "Macro.Exit")
                {
                    exit = n->ID;
                    exitNode = n;
                    continue;
                }
                Node clone = *n;
                clone.ID = uint32_t(++next);
                clone.Macro.clear();
                copy[n->ID] = clone.ID;
                ex.Origins[clone.ID] = { top, n->ID };
                depth[clone.ID] = depth[id] + 1;
                index[clone.ID] = ex.Nodes.size();
                if (clone.Kind == "Macro.Instance") work.push_back(clone.ID);
                ex.Nodes.push_back(std::move(clone));
            }
            // The value of an input pin that has no wire: the instance's literal or the pin's default.
            auto literal = [&](const Pin& pin, const Node* holder) -> Value {
                if (holder)
                    if (auto it = holder->Inputs.find(pin.Name); it != holder->Inputs.end()) return it->second;
                return pin.Default;
            };
            auto setLiteral = [&](uint32_t node, const std::string& input, const Value& value) {
                if (auto at = index.find(node); at != index.end()) ex.Nodes[at->second].Inputs[input] = value;
            };
            std::set<std::string> fedOutputs;
            for (const auto& l : bodyLinks[name])
            {
                const bool fromEntry = l.From == entry, toExit = l.To == exit;
                if (toExit) fedOutputs.insert(l.Input);
                if (!fromEntry && !toExit)
                {
                    if (copy.contains(l.From) && copy.contains(l.To)) ex.Links.push_back({ copy.at(l.From), l.Output, copy.at(l.To), l.Input });
                    continue;
                }
                const Pin* input = fromEntry ? PinOf(macro->second.Inputs, l.Output) : nullptr;
                if (fromEntry && !input) continue;
                // The outside ends of this inside wire: what feeds the instance's input, what the output feeds.
                std::vector<std::pair<uint32_t, std::string>> sources, targets;
                if (fromEntry)
                    for (const auto& outside : in[l.Output]) sources.push_back({ outside.From, outside.Output });
                else sources.push_back({ copy.contains(l.From) ? copy.at(l.From) : 0, l.Output });
                if (toExit)
                    for (const auto& outside : out[l.Input]) targets.push_back({ outside.To, outside.Input });
                else targets.push_back({ copy.contains(l.To) ? copy.at(l.To) : 0, l.Input });
                for (const auto& [to, toPin] : targets)
                {
                    if (!to) continue;
                    if (fromEntry && input->Kind != Type::Exec && sources.empty()) setLiteral(to, toPin, literal(*input, &instance));
                    for (const auto& [from, fromPin] : sources)
                        if (from) ex.Links.push_back({ from, fromPin, to, toPin });
                }
            }
            // A data output the body leaves unwired gives the exit's literal (or the pin's default) to its users.
            for (const auto& pin : macro->second.Outputs)
                if (pin.Kind != Type::Exec && !fedOutputs.contains(pin.Name))
                    for (const auto& outside : out[pin.Name]) setLiteral(outside.To, outside.Input, literal(pin, exitNode));
        }
        std::erase_if(ex.Nodes, [&](const Node& n) { return removed.contains(n.ID); });
        std::set<uint32_t> kept;
        for (const auto& n : ex.Nodes) kept.insert(n.ID);
        std::erase_if(ex.Links, [&](const Link& l) { return !kept.contains(l.From) || !kept.contains(l.To); });
        return ex;
    }
    uint32_t Machine::Origin(uint32_t id, uint32_t* source) const
    {
        auto it = m_Origins.find(id);
        if (source) *source = it == m_Origins.end() ? 0 : it->second.second;
        return it == m_Origins.end() ? id : it->second.first;
    }
}
