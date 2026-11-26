#include "VisualGraph.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace Zamin::Visual {
    bool IsArray(Type type) { return type >= Type::ArrayBool && type <= Type::ArrayEntity; }
    Type ElementType(Type type) { return IsArray(type) ? Type(int(type) - int(Type::ArrayBool) + int(Type::Bool)) : Type::Exec; }
    Value DefaultValue(Type type)
    {
        if (IsArray(type)) return Value::array();
        if (type == Type::Exec) return nullptr;
        if (type == Type::Bool) return false;
        if (type == Type::Number) return 0.;
        if (type == Type::String) return "";
        if (type == Type::Entity) return 0ull;
        if (type == Type::Enum) return 0;
        return Value(std::vector<double>(type == Type::Vec2 ? 2 : type == Type::Vec3 ? 3
                                                                                     : 4,
                                         0));
    }
    namespace {
        size_t Bytes(const Value& value, int depth)
        {
            constexpr size_t cap = 16 * 1024 * 1024;
            if (depth > 8) return cap;
            size_t bytes = sizeof(Value);
            if (value.is_string()) return bytes + std::min(cap, value.get_ref<const std::string&>().size());
            if (value.is_structured())
                for (auto it = value.begin(); it != value.end(); ++it)
                {
                    bytes += Bytes(it.value(), depth + 1) + (value.is_object() ? it.key().size() + 32 : 0);
                    if (bytes >= cap) return cap;
                }
            return bytes;
        }
        std::string Name(const Node& n)
        {
            auto it = n.Inputs.find("Name");
            return it != n.Inputs.end() && it->second.is_string() ? it->second.get<std::string>() : (n.Kind == "Function" || n.Kind == "Call" || n.Kind.starts_with("Function.")) ? "MyFunction"
                                                                                                : n.Kind == "Event.Custom"                                                        ? "MyEvent"
                                                                                                                                                                                  : "";
        }
        bool Typed(const std::string& kind) { return kind == "Function.Entry" || kind == "Function.Call" || kind == "Function.Return"; }
        // The enum an enum node works with: its Enum literal; for Get/Set.Enum without one the variable's enum,
        // for an Enum local the enum of the locals with that name (when they agree).
        std::string EnumOf(const Graph& g, const Node& n)
        {
            auto it = n.Inputs.find("Enum");
            if (it != n.Inputs.end() && it->second.is_string() && !it->second.get_ref<const std::string&>().empty()) return it->second.get<std::string>();
            const auto name = Name(n);
            if (n.Kind == "Get.Enum" || n.Kind == "Set.Enum")
            {
                auto v = g.Variables.find(name);
                return v != g.Variables.end() && v->second.Kind == Type::Enum ? v->second.Enum : "";
            }
            std::string found;
            if (n.Kind == "Local.Get.Enum" || n.Kind == "Local.Set.Enum")
                for (const auto& [function, signature] : g.Functions)
                    if (auto local = signature.Locals.find(name); local != signature.Locals.end() && local->second.Kind == Type::Enum)
                    {
                        if (!found.empty() && found != local->second.Enum) return "";
                        found = local->second.Enum;
                    }
            return found;
        }
        const Pin* PinNamed(const std::vector<Pin>& pins, const std::string& name)
        {
            for (const auto& pin : pins)
                if (pin.Name == name) return &pin;
            return nullptr;
        }
    }
    size_t ValueBytes(const Value& value) { return Bytes(value, 0); }
    const EnumEntry* EnumDefinition::Find(int64_t value) const
    {
        for (const auto& entry : Entries)
            if (entry.Value == value) return &entry;
        return nullptr;
    }
    const EnumEntry* EnumDefinition::Find(const std::string& name) const
    {
        for (const auto& entry : Entries)
            if (entry.Name == name) return &entry;
        return nullptr;
    }
    bool Graph::Fits(Type type, const std::string& enumName, const Value& value) const
    {
        if (!Matches(type, value)) return false;
        if (type != Type::Enum) return true;
        auto it = Enums.find(enumName);
        return it != Enums.end() && it->second.Find(value.get<int64_t>());
    }
    size_t Graph::EnumUses(const std::string& name) const
    {
        size_t uses = 0;
        for (const auto& n : Nodes)
        {
            const auto def = Describe(n);
            bool used = false;
            for (const auto* pins : { &def.Inputs, &def.Outputs })
                for (const auto& pin : *pins) used |= pin.Kind == Type::Enum && pin.Enum == name;
            uses += used;
        }
        for (const auto& [key, v] : Variables) uses += v.Kind == Type::Enum && v.Enum == name;
        for (const auto& [key, signature] : Functions)
        {
            for (const auto* pins : { &signature.Inputs, &signature.Outputs })
                for (const auto& pin : *pins) uses += pin.Kind == Type::Enum && pin.Enum == name;
            for (const auto& [local, v] : signature.Locals) uses += v.Kind == Type::Enum && v.Enum == name;
        }
        return uses;
    }
    bool Graph::RenameEnum(const std::string& from, const std::string& to)
    {
        if (!Enums.contains(from) || to.empty() || to.size() > 64 || Enums.contains(to)) return false;
        auto entry = Enums.extract(from);
        entry.key() = to;
        Enums.insert(std::move(entry));
        for (auto& n : Nodes)
            if (auto it = n.Inputs.find("Enum"); it != n.Inputs.end() && it->second == Value(from)) it->second = to;
        auto retype = [&](std::string& name) { if (name == from) name = to; };
        for (auto& [key, v] : Variables) retype(v.Enum);
        for (auto& [key, signature] : Functions)
        {
            for (auto* pins : { &signature.Inputs, &signature.Outputs })
                for (auto& pin : *pins) retype(pin.Enum);
            for (auto& [local, v] : signature.Locals) retype(v.Enum);
        }
        return true;
    }
    Definition Graph::Describe(const Node& n) const
    {
        auto found = Catalog().find(n.Kind);
        if (found == Catalog().end()) return { n.Kind, "Unknown", {}, {}, false };
        auto result = found->second;
        if (!Typed(n.Kind))
        {
            const bool enumPins = std::any_of(result.Inputs.begin(), result.Inputs.end(), [](const Pin& p) { return p.Kind == Type::Enum; }) ||
                                  std::any_of(result.Outputs.begin(), result.Outputs.end(), [](const Pin& p) { return p.Kind == Type::Enum; }) || n.Kind == "Enum.Switch";
            if (!enumPins) return result;
            const auto name = EnumOf(*this, n);
            const auto definition = Enums.find(name);
            for (auto* pins : { &result.Inputs, &result.Outputs })
                for (auto& pin : *pins)
                    if (pin.Kind == Type::Enum)
                    {
                        pin.Enum = name;
                        if (definition != Enums.end() && !definition->second.Entries.empty()) pin.Default = definition->second.Entries.front().Value;
                    }
            if (n.Kind == "Enum.Switch" && definition != Enums.end())
                for (const auto& entry : definition->second.Entries) result.Outputs.push_back({ entry.Name, Type::Exec, nullptr });
            return result;
        }
        auto signature = Functions.find(Name(n));
        if (signature == Functions.end()) return result;
        if (n.Kind == "Function.Entry") result.Outputs.insert(result.Outputs.end(), signature->second.Inputs.begin(), signature->second.Inputs.end());
        if (n.Kind == "Function.Call")
        {
            result.Inputs.insert(result.Inputs.end(), signature->second.Inputs.begin(), signature->second.Inputs.end());
            result.Outputs.insert(result.Outputs.end(), signature->second.Outputs.begin(), signature->second.Outputs.end());
        }
        if (n.Kind == "Function.Return") result.Inputs.insert(result.Inputs.end(), signature->second.Outputs.begin(), signature->second.Outputs.end());
        return result;
    }
    bool Graph::RenameVariable(const std::string& from, const std::string& to)
    {
        if (!Variables.contains(from) || to.empty() || to.size() > 128 || Variables.contains(to)) return false;
        auto entry = Variables.extract(from);
        entry.key() = to;
        Variables.insert(std::move(entry));
        for (auto& n : Nodes)
            if ((n.Kind.starts_with("Get.") || n.Kind.starts_with("Set.")) && Name(n) == from) n.Inputs["Name"] = to;
        return true;
    }
    bool Graph::RenameFunction(const std::string& from, const std::string& to)
    {
        if (!Functions.contains(from) || to.empty() || to.size() > 128 || Functions.contains(to)) return false;
        auto entry = Functions.extract(from);
        entry.key() = to;
        Functions.insert(std::move(entry));
        for (auto& n : Nodes)
            if (Typed(n.Kind) && Name(n) == from) n.Inputs["Name"] = to;
        return true;
    }
    size_t Graph::PruneInvalidPins()
    {
        size_t removed = 0;
        std::map<uint32_t, Definition> definitions;
        for (auto& n : Nodes)
        {
            const auto def = Describe(n);
            definitions[n.ID] = def;
            removed += std::erase_if(n.Inputs, [&](const auto& input) { const auto* pin = PinNamed(def.Inputs, input.first); return !pin || !Fits(*pin, input.second); });
        }
        removed += std::erase_if(Links, [&](const auto& l) {
            if (!definitions.contains(l.From) || !definitions.contains(l.To)) return true;
            const auto* from = PinNamed(definitions.at(l.From).Outputs, l.Output);
            const auto* to = PinNamed(definitions.at(l.To).Inputs, l.Input);
            return !from || !to || !SameType(*from, *to);
        });
        return removed;
    }
    Value Graph::CopyNodes(const std::set<uint32_t>& selected) const
    {
        Graph fragment;
        fragment.Variables = Variables;
        fragment.Enums = Enums;
        for (const auto& n : Nodes)
            if (selected.contains(n.ID))
            {
                fragment.Nodes.push_back(n);
                if (Typed(n.Kind) && Functions.contains(Name(n))) fragment.Functions[Name(n)] = Functions.at(Name(n));
            }
        for (const auto& l : Links)
            if (selected.contains(l.From) && selected.contains(l.To)) fragment.Links.push_back(l);
        return fragment.Serialize();
    }
    bool Graph::PasteNodes(const Value& value, float x, float y, std::set<uint32_t>& inserted, std::string& error)
    {
        inserted.clear();
        error.clear();
        try
        {
            if (!std::isfinite(x) || !std::isfinite(y) || ValueBytes(value) > 4 * 1024 * 1024) throw std::runtime_error("Invalid clipboard size/offset");
            Graph fragment = Parse(value), next = *this;
            auto unique = [](std::string base, const auto& names) { std::string name = base; for (uint32_t i = 1; names.contains(name); ++i) name = base + "_copy" + std::to_string(i); return name; };
            auto variableNames = next.Variables;
            variableNames.insert(fragment.Variables.begin(), fragment.Variables.end());
            auto functionNames = next.Functions;
            functionNames.insert(fragment.Functions.begin(), fragment.Functions.end());
            std::map<std::string, std::string> variableRenames, functionRenames;
            // Enums keep their names: an equal definition is shared, a different one with the same name is rejected.
            for (const auto& [name, definition] : fragment.Enums)
            {
                auto existing = next.Enums.find(name);
                if (existing == next.Enums.end()) next.Enums[name] = definition;
                else if (existing->second != definition) throw std::runtime_error("Enum " + name + " differs in this graph");
            }
            for (const auto& [name, var] : fragment.Variables)
            {
                std::string target = name;
                if (next.Variables.contains(name) && (next.Variables.at(name).Kind != var.Kind || next.Variables.at(name).Default != var.Default || next.Variables.at(name).Enum != var.Enum)) target = unique(name, variableNames);
                variableNames[target] = var;
                if (!next.Variables.contains(target)) next.Variables[target] = var;
                variableRenames[name] = target;
            }
            for (const auto& [name, signature] : fragment.Functions)
            {
                const bool includesEntry = std::any_of(fragment.Nodes.begin(), fragment.Nodes.end(), [&](const auto& n) { return n.Kind == "Function.Entry" && Name(n) == name; });
                std::string target = includesEntry && next.Functions.contains(name) ? unique(name, functionNames) : name;
                if (!includesEntry && (!next.Functions.contains(name) || next.Serialize().at("Functions").at(name) != fragment.Serialize().at("Functions").at(name)))
                    throw std::runtime_error("Copy the function entry and body with the call");
                functionNames[target] = signature;
                if (!next.Functions.contains(target)) next.Functions[target] = signature;
                functionRenames[name] = target;
            }
            // Apply mappings once to original names: a renamed Foo must not become Foo_copy1's mapping.
            for (auto& n : fragment.Nodes)
            {
                const auto name = Name(n);
                if ((n.Kind.starts_with("Get.") || n.Kind.starts_with("Set.")) && variableRenames.contains(name)) n.Inputs["Name"] = variableRenames.at(name);
                if (Typed(n.Kind) && functionRenames.contains(name)) n.Inputs["Name"] = functionRenames.at(name);
            }
            uint64_t id = 0;
            for (const auto& n : next.Nodes) id = std::max(id, uint64_t(n.ID));
            std::map<uint32_t, uint32_t> ids;
            for (auto n : fragment.Nodes)
            {
                if (++id > UINT32_MAX) throw std::runtime_error("Node ID space exhausted");
                ids[n.ID] = uint32_t(id);
                n.ID = uint32_t(id);
                n.X += x;
                n.Y += y;
                next.Nodes.push_back(std::move(n));
            }
            for (auto l : fragment.Links)
            {
                l.From = ids.at(l.From);
                l.To = ids.at(l.To);
                next.Links.push_back(std::move(l));
            }
            const auto diagnostics = next.Validate();
            if (!diagnostics.empty()) throw std::runtime_error(diagnostics.front().Message);
            for (const auto& [old, added] : ids) inserted.insert(added);
            *this = std::move(next);
            return true;
        }
        catch (const std::exception& e)
        {
            error = e.what();
            inserted.clear();
            return false;
        }
    }
}
