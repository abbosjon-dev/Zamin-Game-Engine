#include "VisualGraph.h"
#include <algorithm>
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
            Need(Matches(pin.Kind, value), "Function argument type mismatch");
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
        for (const auto& pin : signature.Outputs) Need(values.contains(pin.Name) && Matches(pin.Kind, values.at(pin.Name)), "Function return type/value mismatch");
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
                    if (pin.Name.empty() || pin.Name.size() > 128 || pin.Name == "Name" || pin.Name == "In" || pin.Name == "Then" || !names.insert(pin.Name).second || pin.Kind == Type::Exec || !Matches(pin.Kind, pin.Default)) error(0, "Invalid/duplicate/reserved function pin in " + name);
                }
            }
            for (const auto& [key, var] : signature.Locals)
            {
                bytes += key.size() + ValueBytes(var.Default);
                if (key.empty() || key.size() > 128 || var.Kind == Type::Exec || !Matches(var.Kind, var.Default)) error(0, "Invalid local variable in " + name);
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
                            if (type != locals.at(key).Kind) error(n.ID, "Local variable type differs from node");
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
}
