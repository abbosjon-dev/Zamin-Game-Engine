#include "VisualGraph.h"
#include <algorithm>
#include <functional>
#include <stdexcept>
namespace Zamin::Visual {
    namespace {
        void Need(bool value, const char* text)
        {
            if (!value) throw std::runtime_error(text);
        }
        std::string Name(const Node& node)
        {
            auto it = node.Inputs.find("Name");
            return it != node.Inputs.end() && it->second.is_string() ? it->second.get<std::string>() : (node.Kind == "Function" || node.Kind == "Call" || node.Kind.starts_with("Function.")) ? "MyFunction"
                                                                                                   : node.Kind == "Event.Custom"                                                              ? "MyEvent"
                                                                                                                                                                                              : "";
        }
        struct Coverage
        {
            uint32_t Entry = 0;
            bool Reachable = false; // a Return of the function can be reached from its entry
            bool AllPaths = true;   // every execution path from the entry ends in a Return
        };
        // Static return-path analysis of a typed function. A loop's Body may run zero times, so only Completed
        // counts; a Sequence returns when one of its outputs returns on every path; any other node with several
        // execution outputs (Branch, Enum.Switch ...) needs all of them. A wire back into a node being visited
        // (a loop) and Break are not reported.
        Coverage ReturnCoverage(const Graph& graph, const std::string& function)
        {
            Coverage coverage;
            std::map<uint32_t, const Node*> nodes;
            for (const auto& n : graph.Nodes)
            {
                nodes[n.ID] = &n;
                if (n.Kind == "Function.Entry" && Name(n) == function) coverage.Entry = n.ID;
            }
            if (!coverage.Entry) return coverage;
            std::map<std::pair<uint32_t, std::string>, uint32_t> next;
            for (const auto& link : graph.Links)
            {
                auto to = nodes.find(link.To);
                if (to == nodes.end() || !nodes.contains(link.From)) continue;
                const auto def = graph.Describe(*to->second);
                auto pin = std::find_if(def.Inputs.begin(), def.Inputs.end(), [&](const Pin& p) { return p.Name == link.Input; });
                if (pin != def.Inputs.end() && pin->Kind == Type::Exec) next[{ link.From, link.Output }] = link.To;
            }
            std::map<uint32_t, bool> memo;
            std::set<uint32_t> visiting;
            std::function<bool(uint32_t, int)> returns = [&](uint32_t id, int depth) -> bool {
                if (auto known = memo.find(id); known != memo.end()) return known->second;
                if (depth > 4096 || visiting.contains(id)) return true;
                const auto& n = *nodes.at(id);
                if (n.Kind == "Function.Return")
                {
                    coverage.Reachable |= Name(n) == function;
                    return memo[id] = true;
                }
                if (n.Kind == "Break") return memo[id] = true;
                visiting.insert(id);
                auto follow = [&](const std::string& pin) {
                    auto it = next.find({ id, pin });
                    return it != next.end() && returns(it->second, depth + 1);
                };
                std::vector<std::string> outputs;
                for (const auto& pin : graph.Describe(n).Outputs)
                    if (pin.Kind == Type::Exec) outputs.push_back(pin.Name);
                bool result = false;
                if (n.Kind == "For" || n.Kind == "While" || n.Kind.starts_with("ForEach."))
                {
                    follow("Body");
                    result = follow("Completed");
                }
                else if (n.Kind == "Sequence")
                    for (const auto& pin : outputs) result |= follow(pin);
                else
                {
                    result = !outputs.empty();
                    for (const auto& pin : outputs) result &= follow(pin);
                }
                visiting.erase(id);
                return memo[id] = result;
            };
            coverage.AllPaths = returns(coverage.Entry, 0);
            return coverage;
        }
        size_t ResultBytes(const std::map<uint32_t, Value>& results)
        {
            size_t n = 0;
            for (const auto& [id, v] : results) n += 64 + ValueBytes(v);
            return n;
        }
    }
    Value Machine::DebugFrame() const
    {
        const Task* task = m_Active;
        if (!task && !m_Tasks.empty()) task = &m_Tasks.front();
        if (!task || task->Calls.empty()) return Value::object();
        const auto& call = task->Calls.back();
        return { { "Function", call.Name }, { "Caller", call.Caller }, { "Arguments", call.Arguments }, { "Locals", call.Locals } };
    }
    size_t Machine::RetainedBytes() const
    {
        size_t bytes = 0;
        for (const auto& [name, value] : m_Variables) bytes += 64 + name.size() + ValueBytes(value);
        auto taskBytes = [&](const Task& t) {
            size_t count = sizeof(Task) + t.Stack.size() * sizeof(Frame) + ValueBytes(t.Event) + ResultBytes(t.Results);
            for (const auto& call : t.Calls)
            {
                count += sizeof(Invocation) + call.Name.size() + ResultBytes(call.ParentResults);
                for (const auto& [key, v] : call.Arguments) count += 64 + key.size() + ValueBytes(v);
                for (const auto& [key, v] : call.Locals) count += 64 + key.size() + ValueBytes(v);
            }
            return count;
        };
        for (const auto& task : m_Tasks) bytes += taskBytes(task);
        if (m_Active) bytes += taskBytes(*m_Active);
        return bytes;
    }
    void Machine::CheckStorage() const { Need(RetainedBytes() <= MaxRetainedBytes, "Retained graph data exceeds 8 MiB"); }
    void Machine::CaptureCallStack(const Task& task)
    {
        m_CallStack.clear();
        for (const auto& call : task.Calls)
        {
            if (!m_CallStack.empty()) m_CallStack += " -> ";
            m_CallStack += call.Name + " (#" + std::to_string(call.Caller) + ")";
        }
    }
    void Machine::TypedCall(Task& task, const Node& node)
    {
        Need(task.Calls.size() < 32, "Typed function call depth exceeds 32");
        auto name = Name(node);
        const auto& signature = m_Graph.Functions.at(name);
        Invocation call;
        call.Name = name;
        call.Caller = node.ID;
        call.Entry = m_TypedEntries.at(name);
        std::set<uint32_t> active;
        for (const auto& pin : signature.Inputs)
        {
            auto value = Input(node, pin.Name, task, active);
            Need(m_Graph.Fits(pin, value), "Function argument type mismatch");
            call.Arguments[pin.Name] = std::move(value);
        }
        for (const auto& [key, var] : signature.Locals) call.Locals[key] = var.Default;
        Push(task, node.ID, "Then");
        call.ReturnDepth = task.Stack.size();
        call.ParentResults = std::move(task.Results);
        task.Results.clear();
        task.Calls.push_back(std::move(call));
        Need(task.Stack.size() < 1023, "Function execution stack limit");
        task.Stack.push_back({ 0, -1, 0, true });
        task.Stack.push_back({ task.Calls.back().Entry });
        CaptureCallStack(task);
        CheckStorage();
    }
    void Machine::ReturnFunction(Task& task, Value values)
    {
        Need(!task.Calls.empty(), "Return outside a typed function");
        const auto& signature = m_Graph.Functions.at(task.Calls.back().Name);
        for (const auto& pin : signature.Outputs) Need(values.contains(pin.Name) && m_Graph.Fits(pin, values.at(pin.Name)), "Function return type/value mismatch");
        auto call = std::move(task.Calls.back());
        task.Calls.pop_back();
        task.Stack.resize(call.ReturnDepth);
        task.Results = std::move(call.ParentResults);
        task.Results[call.Caller] = std::move(values);
        CaptureCallStack(task);
        CheckStorage();
    }
    std::vector<Diagnostic> Graph::ValidateFunctions() const
    {
        std::vector<Diagnostic> errors;
        auto error = [&](uint32_t id, std::string message) { errors.push_back({ id, std::move(message) }); };
        if (Functions.size() > 64)
        {
            error(0, "Function definition limit exceeds 64");
            return errors;
        }
        std::map<std::string, int> entries;
        std::map<uint32_t, const Node*> nodes;
        std::map<uint32_t, Definition> defs;
        std::map<uint32_t, std::vector<uint32_t>> execution, consumers;
        std::map<uint32_t, std::set<std::string>> owners;
        for (const auto& [name, signature] : Functions)
        {
            if (name.empty() || name.size() > 128) error(0, "Invalid function name");
            if (signature.Inputs.size() > 32 || signature.Outputs.size() > 32 || signature.Locals.size() > 32) error(0, "Function parameter/local limit exceeds 32");
            size_t bytes = 0;
            for (const auto* pins : { &signature.Inputs, &signature.Outputs })
            {
                std::set<std::string> names;
                for (const auto& pin : *pins)
                {
                    bytes += pin.Name.size() + ValueBytes(pin.Default);
                    if (pin.Name.empty() || pin.Name.size() > 128 || pin.Name == "Name" || pin.Name == "In" || pin.Name == "Then" || !names.insert(pin.Name).second || pin.Kind == Type::Exec || !Fits(pin, pin.Default)) error(0, "Invalid/duplicate/reserved function pin in " + name);
                }
            }
            for (const auto& [key, var] : signature.Locals)
            {
                bytes += key.size() + ValueBytes(var.Default);
                if (key.empty() || key.size() > 128 || var.Kind == Type::Exec || !Fits(var.Kind, var.Enum, var.Default)) error(0, "Invalid local variable in " + name);
            }
            if (bytes > 65536) error(0, "Function defaults exceed 64 KiB in " + name);
        }
        for (const auto& n : Nodes)
        {
            nodes[n.ID] = &n;
            defs[n.ID] = Describe(n);
            if (n.Kind.starts_with("Function."))
            {
                if (!Functions.contains(Name(n))) error(n.ID, "Typed function not found: " + Name(n));
                if (n.Kind == "Function.Entry") ++entries[Name(n)];
            }
            if (n.Kind == "Function" && Functions.contains(Name(n))) error(n.ID, "Legacy/typed function name collision");
            if (n.Kind == "Event.Custom")
            {
                const auto name = n.Inputs.contains("Name") ? Name(n) : "MyEvent";
                if (name.empty() || name.size() > 128) error(n.ID, "Invalid custom event name");
            }
        }
        for (const auto& [name, sig] : Functions)
            if (entries[name] != 1) error(0, "Typed function needs exactly one entry: " + name);
            else if (!sig.Outputs.empty())
            {
                const auto coverage = ReturnCoverage(*this, name);
                if (!coverage.Reachable) error(coverage.Entry, "Function " + name + " has results but no reachable Return");
            }
        for (const auto& link : Links)
        {
            if (!nodes.contains(link.From) || !nodes.contains(link.To)) continue;
            const auto& inputs = defs.at(link.To).Inputs;
            auto it = std::find_if(inputs.begin(), inputs.end(), [&](const auto& pin) { return pin.Name == link.Input; });
            if (it != inputs.end() && it->Kind == Type::Exec) execution[link.From].push_back(link.To);
            else consumers[link.From].push_back(link.To);
        }
        for (const auto& root : Nodes)
            if (root.Kind.starts_with("Event.") || root.Kind == "Function" || root.Kind == "Function.Entry")
            {
                std::string scope = root.Kind == "Function.Entry" ? Name(root) : "@" + std::to_string(root.ID);
                // Event/legacy scopes must never alias a user-authored function name.
                if (root.Kind != "Function.Entry")
                    while (Functions.contains(scope)) scope = "@" + scope;
                std::vector<uint32_t> todo{ root.ID };
                std::set<uint32_t> seen;
                while (!todo.empty())
                {
                    auto id = todo.back();
                    todo.pop_back();
                    if (!seen.insert(id).second) continue;
                    owners[id].insert(scope);
                    for (auto next : execution[id]) todo.push_back(next);
                }
            }
        std::map<uint32_t, std::set<std::string>> uses;
        std::set<uint32_t> active;
        std::function<std::set<std::string>(uint32_t, int)> scopeFor = [&](uint32_t id, int depth) {
            if (uses.contains(id)) return uses[id];
            if (depth > 128 || !active.insert(id).second) return std::set<std::string>{};
            auto scope = owners[id];
            for (auto consumer : consumers[id])
            {
                auto more = scopeFor(consumer, depth + 1);
                scope.insert(more.begin(), more.end());
            }
            active.erase(id);
            uses[id] = scope;
            return scope;
        };
        for (const auto& n : Nodes)
        {
            if (n.Kind == "Function.Entry")
                for (auto consumer : consumers[n.ID])
                    for (const auto& scope : scopeFor(consumer, 0))
                        if (scope != Name(n)) error(consumer, "Function parameter escapes its invocation");
            if (n.Kind.starts_with("Local.") || n.Kind == "Function.Return")
            {
                auto scopes = scopeFor(n.ID, 0);
                if (scopes.empty()) error(n.ID, "Local/return node is outside a typed function");
                for (const auto& scope : scopes)
                {
                    if (!Functions.contains(scope))
                    {
                        error(n.ID, "Local/return node escapes typed function scope");
                        continue;
                    }
                    if (n.Kind == "Function.Return")
                    {
                        if (Name(n) != scope) error(n.ID, "Return belongs to another function");
                    }
                    else
                    {
                        const auto& locals = Functions.at(scope).Locals;
                        const auto key = Name(n);
                        if (!locals.contains(key)) error(n.ID, "Unknown local: " + key);
                        else
                        {
                            const auto& def = defs.at(n.ID);
                            auto type = n.Kind.starts_with("Local.Get.") ? def.Outputs.back().Kind : def.Inputs.back().Kind;
                            const auto& pin = n.Kind.starts_with("Local.Get.") ? def.Outputs.back() : def.Inputs.back();
                            if (type != locals.at(key).Kind || (IsNamed(type) && pin.Enum != locals.at(key).Enum)) error(n.ID, "Local variable type differs from node");
                        }
                    }
                }
            }
            if (!defs.at(n.ID).Pure && n.Kind != "Function.Entry" && !n.Kind.starts_with("Event."))
                for (auto consumer : consumers[n.ID])
                    for (const auto& scope : scopeFor(consumer, 0))
                        if (Functions.contains(scope) && !owners[n.ID].contains(scope)) error(consumer, "Pass execution results through a function argument");
            if (n.Kind.starts_with("Event."))
                for (auto consumer : consumers[n.ID])
                    for (const auto& scope : scopeFor(consumer, 0))
                        if (Functions.contains(scope)) error(consumer, "Pass event data through a function argument");
        }
        return errors;
    }
    std::vector<Diagnostic> Graph::Warnings() const
    {
        std::vector<Diagnostic> warnings;
        if (!Macros.empty())
        {
            // Return paths are followed through the expanded macros; a finding in a copy names the instance.
            auto expansion = Expand();
            Graph expanded = *this;
            expanded.Nodes = std::move(expansion.Nodes);
            expanded.Links = std::move(expansion.Links);
            expanded.Macros.clear();
            warnings = expanded.Warnings();
            for (auto& w : warnings)
                if (auto origin = expansion.Origins.find(w.Node); origin != expansion.Origins.end()) w = { origin->second.first, w.Message, origin->second.second };
            return warnings;
        }
        for (const auto& [name, signature] : Functions)
        {
            if (signature.Outputs.empty()) continue;
            const auto coverage = ReturnCoverage(*this, name);
            if (coverage.Entry && coverage.Reachable && !coverage.AllPaths) warnings.push_back({ coverage.Entry, "Some execution paths of " + name + " end without Return" });
        }
        return warnings;
    }
    std::set<uint32_t> Graph::FunctionNodes(const std::string& name) const
    {
        std::set<uint32_t> result;
        std::map<uint32_t, const Node*> nodes;
        uint32_t entry = 0;
        for (const auto& n : Nodes)
        {
            nodes[n.ID] = &n;
            if (n.Kind == "Function.Entry" && Name(n) == name) entry = n.ID;
        }
        if (!entry) return result;
        std::map<uint32_t, std::vector<uint32_t>> execution, sources;
        for (const auto& link : Links)
        {
            auto to = nodes.find(link.To);
            if (to == nodes.end() || !nodes.contains(link.From)) continue;
            const auto def = Describe(*to->second);
            auto pin = std::find_if(def.Inputs.begin(), def.Inputs.end(), [&](const Pin& p) { return p.Name == link.Input; });
            if (pin != def.Inputs.end() && pin->Kind == Type::Exec) execution[link.From].push_back(link.To);
            else sources[link.To].push_back(link.From);
        }
        std::vector<uint32_t> todo{ entry };
        while (!todo.empty())
        {
            const auto id = todo.back();
            todo.pop_back();
            if (!result.insert(id).second) continue;
            for (auto to : execution[id]) todo.push_back(to);
        }
        // Data sources of the body (pure helpers, locals), not other entries or events.
        todo.assign(result.begin(), result.end());
        while (!todo.empty())
        {
            const auto id = todo.back();
            todo.pop_back();
            for (auto from : sources[id])
            {
                const auto& kind = nodes.at(from)->Kind;
                if (kind.starts_with("Event.") || (kind == "Function.Entry" && from != entry)) continue;
                if (result.insert(from).second) todo.push_back(from);
            }
        }
        return result;
    }
    bool Graph::CollapseToFunction(const std::set<uint32_t>& selected, const std::string& requested, uint32_t& call, std::string& error)
    {
        call = 0;
        error.clear();
        try
        {
            Need(!selected.empty(), "Select the nodes to collapse");
            Need(Functions.size() < 64, "Function definition limit exceeds 64");
            Graph next = *this;
            std::map<uint32_t, Definition> defs;
            float minX = 1e30f, minY = 1e30f, maxX = -1e30f, graphMaxY = -1e30f;
            uint64_t id = 0;
            for (const auto& n : Nodes)
            {
                defs[n.ID] = Describe(n);
                id = std::max<uint64_t>(id, n.ID);
                graphMaxY = std::max(graphMaxY, n.Y + 65 + 24 * float(std::max(defs[n.ID].Inputs.size(), defs[n.ID].Outputs.size())));
                if (!selected.contains(n.ID)) continue;
                Need(n.Macro.empty(), "Nodes inside a macro cannot be collapsed into a function");
                Need(!n.Kind.starts_with("Event.") && n.Kind != "Function" && n.Kind != "Function.Entry" && n.Kind != "Function.Return" && !n.Kind.starts_with("Local."),
                     "Events, function entries/returns and locals cannot be collapsed");
                minX = std::min(minX, n.X);
                minY = std::min(minY, n.Y);
                maxX = std::max(maxX, n.X);
            }
            for (auto selectedID : selected) Need(defs.contains(selectedID), "A selected node does not exist");
            Need(id + 3 <= UINT32_MAX && Nodes.size() + 3 <= 4096, "Node count/ID limit reached");
            std::string name = requested;
            if (name.empty() || Functions.contains(name))
            {
                const std::string base = name.empty() ? "Collapsed" : name;
                name = base;
                for (int i = 2; Functions.contains(name); ++i) name = base + std::to_string(i);
            }
            Need(name.size() <= 128, "Invalid function name");
            auto isExec = [&](const Link& l) {
                const auto& inputs = defs.at(l.To).Inputs;
                auto pin = std::find_if(inputs.begin(), inputs.end(), [&](const Pin& p) { return p.Name == l.Input; });
                return pin != inputs.end() && pin->Kind == Type::Exec;
            };
            auto outputPin = [&](const Link& l) {
                const auto& outputs = defs.at(l.From).Outputs;
                auto pin = std::find_if(outputs.begin(), outputs.end(), [&](const Pin& p) { return p.Name == l.Output; });
                Need(pin != outputs.end(), "A wire references a missing pin");
                return *pin;
            };
            std::vector<Link> kept, entries, exits, dataIn, dataOut;
            for (const auto& l : Links)
            {
                if (!defs.contains(l.From) || !defs.contains(l.To)) continue;
                const bool from = selected.contains(l.From), to = selected.contains(l.To);
                if (from == to) kept.push_back(l);
                else if (isExec(l)) (to ? entries : exits).push_back(l);
                else (to ? dataIn : dataOut).push_back(l);
            }
            Need(!entries.empty(), "The selection needs one incoming execution wire");
            for (const auto& l : entries) Need(l.To == entries[0].To && l.Input == entries[0].Input, "The selection needs one execution entry");
            Need(exits.size() <= 1, "The selection needs at most one execution exit");
            for (const auto& l : dataOut) Need(!defs.at(l.From).Pure, "A selected pure node also feeds unselected nodes; select its users too or leave it out");
            FunctionSignature signature;
            std::set<std::string> taken{ "Name", "In", "Then" };
            auto pinFor = [&](const Pin& source, std::vector<Pin>& pins) {
                std::string pinName = source.Name;
                for (int i = 2; taken.contains(pinName); ++i) pinName = source.Name + std::to_string(i);
                taken.insert(pinName);
                Value initial = Fits(source, source.Default) ? source.Default : DefaultFor(source.Kind, source.Enum);
                pins.push_back({ pinName, source.Kind, initial, source.Enum });
                return pinName;
            };
            std::map<std::pair<uint32_t, std::string>, std::string> parameters, results;
            for (const auto& l : dataIn)
                if (!parameters.contains({ l.From, l.Output })) parameters[{ l.From, l.Output }] = pinFor(outputPin(l), signature.Inputs);
            taken = { "Name", "In", "Then" };
            for (const auto& l : dataOut)
                if (!results.contains({ l.From, l.Output })) results[{ l.From, l.Output }] = pinFor(outputPin(l), signature.Outputs);
            Need(signature.Inputs.size() <= 32 && signature.Outputs.size() <= 32, "Function parameter/local limit exceeds 32");
            Need(signature.Outputs.empty() || !exits.empty(), "A function with results needs the selection's execution exit");
            const uint32_t entry = uint32_t(id + 1), callID = uint32_t(id + 2), ret = uint32_t(id + 3);
            const bool needsReturn = !exits.empty();
            // The body moves below the graph, between its entry and return; the call takes its place.
            const float dx = 280, dy = graphMaxY + 200 - minY;
            for (auto& n : next.Nodes)
                if (selected.contains(n.ID))
                {
                    n.X += dx;
                    n.Y += dy;
                }
            next.Functions[name] = signature;
            next.Nodes.push_back({ entry, "Function.Entry", minX, minY + dy, { { "Name", name } } });
            next.Nodes.push_back({ callID, "Function.Call", minX, minY, { { "Name", name } } });
            if (needsReturn) next.Nodes.push_back({ ret, "Function.Return", maxX + 2 * dx, minY + dy, { { "Name", name } } });
            next.Links = kept;
            for (const auto& l : entries) next.Links.push_back({ l.From, l.Output, callID, "In" });
            next.Links.push_back({ entry, "Then", entries[0].To, entries[0].Input });
            for (const auto& l : exits)
            {
                next.Links.push_back({ l.From, l.Output, ret, "In" });
                next.Links.push_back({ callID, "Then", l.To, l.Input });
            }
            for (const auto& l : dataIn) next.Links.push_back({ entry, parameters.at({ l.From, l.Output }), l.To, l.Input });
            for (const auto& [source, pin] : parameters) next.Links.push_back({ source.first, source.second, callID, pin });
            for (const auto& l : dataOut) next.Links.push_back({ callID, results.at({ l.From, l.Output }), l.To, l.Input });
            for (const auto& [source, pin] : results) next.Links.push_back({ source.first, source.second, ret, pin });
            // Accept only when the collapse adds no diagnostic the graph did not already have.
            const auto before = Validate(), after = next.Validate();
            for (const auto& d : after)
                Need(std::any_of(before.begin(), before.end(), [&](const Diagnostic& old) { return old.Message == d.Message; }), d.Message.c_str());
            *this = std::move(next);
            call = callID;
            return true;
        }
        catch (const std::exception& e)
        {
            error = e.what();
            return false;
        }
    }
}
