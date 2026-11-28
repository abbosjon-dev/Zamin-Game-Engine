#include "VisualGraph.h"
#include "Zamin/Utils/FileSystem.h"
#include <algorithm>
#include <fstream>
#include <stdexcept>
// Graph inheritance: a child names a parent .zgraph (Graph::Parent). Loading puts the parent chain's members into the
// child's maps (marked in Inherited, never saved with the child) and keeps the chain as one resolved graph
// (Inherited.Base). Resolved() is what the VM runs: the parent's nodes under new IDs plus the child's own nodes, with
// the overridden parent functions renamed "<Parent>::<Name>" and the overridden parent event handlers run only
// through the child's Parent.Event nodes.
namespace Zamin::Visual {
    namespace {
        std::string NameOf(const Node& n)
        {
            auto it = n.Inputs.find("Name");
            if (it != n.Inputs.end() && it->second.is_string()) return it->second.get<std::string>();
            if (n.Kind == "Event.Custom") return "MyEvent";
            if (n.Kind == "Parent.Event") return "Start";
            return n.Kind.starts_with("Function.") || n.Kind == "Parent.Call" ? "MyFunction" : "";
        }
        Value ReadJson(const std::filesystem::path& path) { return ReadGraphJson(path); }
    }
    std::string Graph::EventKey(const Node& n)
    {
        if (n.Kind == "Event.Custom") return "Custom." + NameOf(n);
        if (n.Kind == "Event.Rpc") return "Rpc." + NameOf(n);
        if (n.Kind == "Event.Rep") return "Rep." + NameOf(n);
        if (n.Kind.starts_with("Event.")) return n.Kind.substr(6);
        return {};
    }
    bool Graph::IsInherited(const std::string& kind, const std::string& name) const
    {
        if (kind == "Variable") return Inherited.Variables.contains(name);
        if (kind == "Function") return Inherited.Functions.contains(name);
        if (kind == "Enum") return Inherited.Enums.contains(name);
        if (kind == "Struct") return Inherited.Structs.contains(name);
        if (kind == "Macro") return Inherited.Macros.contains(name);
        if (kind == "RPC") return Inherited.Rpcs.contains(name);
        if (kind == "Event") return Inherited.Events.contains(name);
        return false;
    }
    void Graph::Record(const char* kind, const std::string& from, const std::string& to)
    {
        Renames.push_back({ kind, from, to });
        if (Renames.size() > MaxRenames) Renames.erase(Renames.begin());
    }
    void Graph::ClearInherited()
    {
        for (const auto& name : Inherited.Variables) Variables.erase(name);
        for (const auto& name : Inherited.Functions) Functions.erase(name);
        for (const auto& name : Inherited.Enums) Enums.erase(name);
        for (const auto& name : Inherited.Structs) Structs.erase(name);
        for (const auto& name : Inherited.Macros) Macros.erase(name);
        for (const auto& name : Inherited.Rpcs) Rpcs.erase(name);
        Inherited = {};
    }
    void Graph::LoadParent(const std::filesystem::path& assets, const std::filesystem::path& self)
    {
        std::vector<std::filesystem::path> chain;
        std::error_code ec;
        if (!self.empty()) chain.push_back(std::filesystem::weakly_canonical(self, ec));
        LoadParentChain(assets, std::move(chain));
    }
    void Graph::LoadParentChain(const std::filesystem::path& assets, std::vector<std::filesystem::path> chain)
    {
        ClearInherited();
        if (Parent.empty()) return;
        auto error = [&](std::string message) { Inherited.Errors.push_back({ 0, std::move(message) }); };
        const auto path = FileSystem::SafeJoin(assets, Parent);
        std::error_code ec;
        if (path.empty() || !std::filesystem::is_regular_file(path, ec))
        {
            error("Parent graph not found: " + Parent);
            return;
        }
        const auto canonical = std::filesystem::weakly_canonical(path, ec);
        if (auto loop = std::find(chain.begin(), chain.end(), canonical); loop != chain.end())
        {
            std::string cycle;
            const auto root = std::filesystem::weakly_canonical(assets, ec);
            for (auto it = loop; it != chain.end(); ++it) cycle += it->lexically_relative(root).generic_string() + " -> ";
            error("Graph inheritance cycle: " + cycle + Parent);
            return;
        }
        if (chain.size() > MaxInheritanceDepth)
        {
            error("Graph inheritance is deeper than " + std::to_string(MaxInheritanceDepth) + " levels");
            return;
        }
        Graph parent;
        try
        {
            parent = Parse(ReadJson(path));
        }
        catch (const std::exception& e)
        {
            error("Parent graph " + Parent + " could not be loaded: " + e.what());
            return;
        }
        chain.push_back(canonical);
        parent.LoadParentChain(assets, std::move(chain));
        InheritFrom(parent, Parent);
    }
    void Graph::InheritFrom(const Graph& parent, const std::string& parentPath)
    {
        ClearInherited();
        for (const auto& e : parent.Inherited.Errors) Inherited.Errors.push_back({ 0, "In parent " + parentPath + ": " + e.Message });
        auto base = std::make_shared<Graph>(parent.Resolved());
        Inherited.Chain = { parentPath };
        Inherited.Chain.insert(Inherited.Chain.end(), parent.Inherited.Chain.begin(), parent.Inherited.Chain.end());
        // A member renamed in the parent chain is renamed here too: the nodes, types and overrides that still use
        // the old name follow (Graph::Rename* on a stand-in definition), unless the old name is still the parent's
        // or the new one is this graph's own.
        for (const auto& r : base->Renames)
        {
            const size_t log = Renames.size();
            if (r.Kind == "Variable" && !base->Variables.contains(r.From) && !Variables.contains(r.From) && !Variables.contains(r.To))
            {
                Variables[r.From] = {};
                RenameVariable(r.From, r.To);
                Variables.erase(r.To);
            }
            if (r.Kind == "Function" && !base->Functions.contains(r.From) && !Functions.contains(r.To))
            {
                // An override of the renamed function is renamed with it.
                const bool own = Functions.contains(r.From);
                if (!own) Functions[r.From] = {};
                RenameFunction(r.From, r.To);
                if (!own) Functions.erase(r.To);
            }
            if (r.Kind == "Enum" && !base->Enums.contains(r.From) && !Enums.contains(r.From) && !Enums.contains(r.To))
            {
                Enums[r.From] = {};
                RenameEnum(r.From, r.To);
                Enums.erase(r.To);
            }
            if (r.Kind == "Struct" && !base->Structs.contains(r.From) && !Structs.contains(r.From) && !Structs.contains(r.To))
            {
                Structs[r.From] = {};
                RenameStruct(r.From, r.To);
                Structs.erase(r.To);
            }
            if (r.Kind == "Macro" && !base->Macros.contains(r.From) && !Macros.contains(r.From) && !Macros.contains(r.To))
            {
                Macros[r.From] = {};
                RenameMacro(r.From, r.To);
                Macros.erase(r.To);
            }
            Renames.resize(log); // following a parent's rename is not a rename of this graph's own
        }
        // The parent's members: a name this graph defines itself stays its own (an override for a function, a
        // conflict that Validate reports for the others). Hidden parent versions ("P::F") stay in the base.
        auto inherit = [](auto& mine, const auto& theirs, std::set<std::string>& marks) {
            for (const auto& [name, definition] : theirs)
                if (name.find("::") == std::string::npos && !mine.contains(name))
                {
                    mine[name] = definition;
                    marks.insert(name);
                }
        };
        inherit(Variables, base->Variables, Inherited.Variables);
        inherit(Functions, base->Functions, Inherited.Functions);
        inherit(Enums, base->Enums, Inherited.Enums);
        inherit(Structs, base->Structs, Inherited.Structs);
        inherit(Macros, base->Macros, Inherited.Macros);
        inherit(Rpcs, base->Rpcs, Inherited.Rpcs);
        // The parent chain's native method signatures (for its nodes in the resolved graph; never saved here: a
        // graph saves the ones its own nodes use).
        for (const auto& [key, signature] : base->Natives) Natives.try_emplace(key, signature);
        for (const auto& n : base->Nodes)
            if (n.Macro.empty() && !base->Inherited.OverriddenEvents.contains(n.ID))
                if (auto key = EventKey(n); !key.empty()) Inherited.Events.insert(key);
        Inherited.Base = std::move(base);
    }
    std::set<std::string> Graph::FunctionOverrides() const
    {
        std::set<std::string> overrides;
        if (!Inherited.Base) return overrides;
        for (const auto& [name, signature] : Functions)
            if (!Inherited.Functions.contains(name) && Inherited.Base->Functions.contains(name) && name.find("::") == std::string::npos) overrides.insert(name);
        return overrides;
    }
    std::set<std::string> Graph::EventOverrides() const
    {
        std::set<std::string> overrides;
        for (const auto& n : Nodes)
            if (n.Macro.empty())
                if (auto key = EventKey(n); !key.empty() && Inherited.Events.contains(key)) overrides.insert(key);
        return overrides;
    }
    std::vector<Diagnostic> Graph::ValidateInheritance() const
    {
        auto d = Inherited.Errors;
        auto error = [&](uint32_t id, std::string message) { d.push_back({ id, std::move(message) }); };
        if (Parent.size() > 512) error(0, "Parent path is too long");
        if (Renames.size() > MaxRenames) error(0, "Rename log exceeds " + std::to_string(MaxRenames));
        const auto functions = FunctionOverrides();
        const auto events = EventOverrides();
        for (const auto& n : Nodes)
        {
            if (!n.Kind.starts_with("Parent.")) continue;
            const auto name = NameOf(n);
            if (Parent.empty()) error(n.ID, n.Kind + " needs a parent graph");
            else if (!Inherited.Base) continue; // the missing parent is reported already
            else if (n.Kind == "Parent.Call" && !functions.contains(name)) error(n.ID, "Parent.Call: " + name + " does not override a parent function");
            else if (n.Kind == "Parent.Event" && !events.contains(name)) error(n.ID, "Parent.Event: " + name + " is not a parent event this graph handles");
            if (n.Kind == "Parent.Event" && !n.Macro.empty()) error(n.ID, "A macro cannot hold Parent.Event");
        }
        if (!Inherited.Base) return d;
        const auto& base = *Inherited.Base;
        for (const auto& name : functions)
        {
            const auto& mine = Functions.at(name);
            const auto& theirs = base.Functions.at(name);
            if (mine.Inputs != theirs.Inputs || mine.Outputs != theirs.Outputs) error(0, "Override of " + name + " must keep the parent's signature");
        }
        auto conflicts = [&](const auto& mine, const auto& theirs, const std::set<std::string>& marks, const char* what) {
            for (const auto& [name, definition] : mine)
                if (!marks.contains(name) && theirs.contains(name)) error(0, std::string(what) + " " + name + " is already defined in parent " + Parent);
        };
        conflicts(Variables, base.Variables, Inherited.Variables, "Variable");
        conflicts(Enums, base.Enums, Inherited.Enums, "Enum");
        conflicts(Structs, base.Structs, Inherited.Structs, "Struct");
        conflicts(Macros, base.Macros, Inherited.Macros, "Macro");
        conflicts(Rpcs, base.Rpcs, Inherited.Rpcs, "RPC");
        return d;
    }
    std::vector<Diagnostic> Graph::Validate() const
    {
        auto d = ValidateInheritance();
        if (!Inherited.Base)
        {
            auto found = ValidateGraph();
            d.insert(d.end(), found.begin(), found.end());
            return d;
        }
        // The child with its parent chain, as the VM runs it; a finding in a parent's node names the parent file.
        const auto resolved = Resolved();
        std::set<std::pair<uint32_t, std::string>> seen;
        for (auto found : resolved.ValidateGraph())
        {
            auto origin = resolved.Inherited.Origins.find(found.Source ? found.Source : found.Node);
            if (origin == resolved.Inherited.Origins.end()) origin = resolved.Inherited.Origins.find(found.Node);
            if (origin != resolved.Inherited.Origins.end())
            {
                found.Message = "In parent " + origin->second.first + " (node " + std::to_string(origin->second.second) + "): " + found.Message;
                if (resolved.Inherited.Origins.contains(found.Node)) found = { 0, found.Message, 0 };
            }
            if (seen.insert({ found.Node, found.Message }).second) d.push_back(std::move(found));
        }
        return d;
    }
    Graph Graph::Resolved() const
    {
        Graph r = *this;
        if (!Inherited.Base) return r;
        const Graph& base = *Inherited.Base;
        r = Graph{};
        r.SourceVersion = SourceVersion;
        r.Parent = Parent;
        r.Renames = base.Renames;
        r.Renames.insert(r.Renames.end(), Renames.begin(), Renames.end());
        // The parent's nodes take IDs this graph does not use (above its own when they fit).
        std::set<uint32_t> own;
        uint64_t next = 0;
        for (const auto& n : Nodes)
        {
            own.insert(n.ID);
            next = std::max<uint64_t>(next, n.ID);
        }
        if (next + base.Nodes.size() >= UINT32_MAX) next = 0;
        std::map<uint32_t, uint32_t> remap;
        for (const auto& n : base.Nodes)
        {
            do ++next;
            while (own.contains(uint32_t(next)));
            remap[n.ID] = uint32_t(next);
        }
        auto id = [&](uint32_t old) {
            auto it = remap.find(old);
            return it == remap.end() ? old : it->second;
        };
        for (auto n : base.Nodes)
        {
            auto origin = base.Inherited.Origins.find(n.ID);
            r.Inherited.Origins[id(n.ID)] = origin != base.Inherited.Origins.end() ? origin->second : std::pair{ Parent, n.ID };
            n.ID = id(n.ID);
            r.Nodes.push_back(std::move(n));
        }
        for (auto l : base.Links)
        {
            l.From = id(l.From);
            l.To = id(l.To);
            r.Links.push_back(std::move(l));
        }
        for (auto overridden : base.Inherited.OverriddenEvents) r.Inherited.OverriddenEvents.insert(id(overridden));
        for (const auto& [node, handlers] : base.Inherited.ParentEvents)
            for (auto handler : handlers) r.Inherited.ParentEvents[id(node)].push_back(id(handler));
        r.Variables = base.Variables;
        r.Functions = base.Functions;
        r.Enums = base.Enums;
        r.Structs = base.Structs;
        r.Macros = base.Macros;
        r.Natives = base.Natives;
        for (const auto& [key, signature] : Natives) r.Natives[key] = signature;
        r.Rpcs = base.Rpcs;
        // An overridden function: the parent's version moves to "<Parent>::<Name>" (its entry and returns follow;
        // calls keep the name and so run the override).
        std::map<std::string, std::string> hidden;
        const auto stem = std::filesystem::path(Parent).stem().string();
        for (const auto& name : FunctionOverrides())
        {
            std::string moved = stem + "::" + name;
            for (int i = 2; r.Functions.contains(moved); ++i) moved = stem + std::to_string(i) + "::" + name;
            r.Functions[moved] = r.Functions.at(name);
            r.Functions.erase(name);
            for (auto& n : r.Nodes)
                if ((n.Kind == "Function.Entry" || n.Kind == "Function.Return") && NameOf(n) == name) n.Inputs["Name"] = moved;
            hidden[name] = moved;
        }
        // This graph's own members (a name it shares with the parent is its own: an override or a reported conflict).
        auto own_members = [](auto& into, const auto& from, const std::set<std::string>& marks) {
            for (const auto& [name, definition] : from)
                if (!marks.contains(name)) into[name] = definition;
        };
        own_members(r.Variables, Variables, Inherited.Variables);
        own_members(r.Functions, Functions, Inherited.Functions);
        own_members(r.Enums, Enums, Inherited.Enums);
        own_members(r.Structs, Structs, Inherited.Structs);
        own_members(r.Macros, Macros, Inherited.Macros);
        own_members(r.Rpcs, Rpcs, Inherited.Rpcs);
        // An overridden event: the parent's live handlers of it run only through this graph's Parent.Event nodes.
        const auto events = EventOverrides();
        std::map<std::string, std::vector<uint32_t>> handlers;
        for (const auto& n : r.Nodes)
            if (n.Macro.empty() && !r.Inherited.OverriddenEvents.contains(n.ID))
                if (auto key = EventKey(n); !key.empty() && events.contains(key)) handlers[key].push_back(n.ID);
        for (const auto& [key, ids] : handlers) r.Inherited.OverriddenEvents.insert(ids.begin(), ids.end());
        for (auto n : Nodes)
        {
            if (n.Kind == "Parent.Call")
                if (auto moved = hidden.find(NameOf(n)); moved != hidden.end())
                {
                    n.Kind = "Function.Call";
                    n.Inputs["Name"] = moved->second;
                }
            if (n.Kind == "Parent.Event")
                if (auto found = handlers.find(NameOf(n)); found != handlers.end()) r.Inherited.ParentEvents[n.ID] = found->second;
            r.Nodes.push_back(std::move(n));
        }
        r.Links.insert(r.Links.end(), Links.begin(), Links.end());
        return r;
    }
}
