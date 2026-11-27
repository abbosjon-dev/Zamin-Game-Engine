#include "VisualGraph.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <stdexcept>
namespace Zamin::Visual {
    bool IsArray(Type type) { return type >= Type::ArrayBool && type <= Type::ArrayStruct; }
    bool IsNamed(Type type) { return type == Type::Enum || type == Type::ArrayEnum || type == Type::Struct || type == Type::ArrayStruct; }
    // The array types follow the order of their element types (ArrayBool..ArrayStruct <-> Bool..Struct).
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
        if (type == Type::Struct) return Value::object();
        if (type == Type::Curve) return { { "Keys", Value::array() } };
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
        // Parent.Call takes its pins from the signature of the function it names, like a call.
        bool Typed(const std::string& kind) { return kind == "Function.Entry" || kind == "Function.Call" || kind == "Function.Return" || kind == "Parent.Call"; }
        // The named type (enum or struct) a node works with: its Enum/Struct literal; for a variable Get/Set without
        // one the variable's type, for a local Get/Set the type of the locals with that name (when they agree).
        std::string NamedOf(const Graph& g, const Node& n, const char* literal)
        {
            auto it = n.Inputs.find(literal);
            if (it != n.Inputs.end() && it->second.is_string() && !it->second.get_ref<const std::string&>().empty()) return it->second.get<std::string>();
            const Type kind = n.Kind.ends_with(".ArrayStruct") ? Type::ArrayStruct : n.Kind.ends_with(".Struct") ? Type::Struct
                                                                                 : n.Kind.ends_with(".ArrayEnum") ? Type::ArrayEnum
                                                                                 : n.Kind.ends_with(".Enum")      ? Type::Enum
                                                                                                                  : Type::Exec;
            if (kind == Type::Exec) return "";
            const auto name = Name(n);
            if (n.Kind.starts_with("Get.") || n.Kind.starts_with("Set."))
            {
                auto v = g.Variables.find(name);
                return v != g.Variables.end() && v->second.Kind == kind ? v->second.Enum : "";
            }
            std::string found;
            if (n.Kind.starts_with("Local.Get.") || n.Kind.starts_with("Local.Set."))
                for (const auto& [function, signature] : g.Functions)
                    if (auto local = signature.Locals.find(name); local != signature.Locals.end() && local->second.Kind == kind)
                    {
                        if (!found.empty() && found != local->second.Enum) return "";
                        found = local->second.Enum;
                    }
            return found;
        }
        std::string EnumOf(const Graph& g, const Node& n) { return NamedOf(g, n, "Enum"); }
        bool StructKind(Type type) { return type == Type::Struct || type == Type::ArrayStruct; }
        bool EnumKind(Type type) { return type == Type::Enum || type == Type::ArrayEnum; }
        // Calls visit(structName, value) for every stored struct value: variable defaults, signature and macro pin
        // defaults, locals, struct field defaults and node literals of Struct pins, each element of ArrayStruct ones,
        // and the struct values nested in those (inner values before the value that holds them).
        template<typename Visit>
        void VisitStructValues(Graph& g, Visit visit)
        {
            std::function<void(Type, const std::string&, Value&, int)> one = [&](Type kind, const std::string& name, Value& value, int depth) {
                if (depth > 16) return;
                if (kind == Type::ArrayStruct && value.is_array())
                    for (auto& item : value) one(Type::Struct, name, item, depth + 1);
                if (kind != Type::Struct) return;
                if (auto definition = g.Structs.find(name); definition != g.Structs.end() && value.is_object())
                    for (const auto& field : definition->second.Fields)
                        if (auto member = value.find(field.Name); member != value.end() && StructKind(field.Kind)) one(field.Kind, field.Enum, *member, depth + 1);
                visit(name, value);
            };
            for (auto& [key, v] : g.Variables) one(v.Kind, v.Enum, v.Default, 0);
            for (auto& [key, signature] : g.Functions)
            {
                for (auto* pins : { &signature.Inputs, &signature.Outputs })
                    for (auto& pin : *pins) one(pin.Kind, pin.Enum, pin.Default, 0);
                for (auto& [local, v] : signature.Locals) one(v.Kind, v.Enum, v.Default, 0);
            }
            for (auto& [key, macro] : g.Macros)
                for (auto* pins : { &macro.Inputs, &macro.Outputs })
                    for (auto& pin : *pins) one(pin.Kind, pin.Enum, pin.Default, 0);
            // Only the values change (never the definitions' field lists), so the definitions can be read meanwhile.
            for (auto& [key, definition] : g.Structs)
                for (auto& field : definition.Fields) one(field.Kind, field.Enum, field.Default, 0);
            for (auto& n : g.Nodes)
            {
                const auto def = g.Describe(n);
                for (const auto& pin : def.Inputs)
                    if (auto it = n.Inputs.find(pin.Name); it != n.Inputs.end() && StructKind(pin.Kind)) one(pin.Kind, pin.Enum, it->second, 0);
            }
        }
        const Pin* PinNamed(const std::vector<Pin>& pins, const std::string& name)
        {
            for (const auto& pin : pins)
                if (pin.Name == name) return &pin;
            return nullptr;
        }
    }
    size_t ValueBytes(const Value& value) { return Bytes(value, 0); }
    const Pin* StructDefinition::Find(const std::string& name) const { return PinNamed(Fields, name); }
    Value Graph::DefaultFor(Type type, const std::string& named) const
    {
        if (type == Type::Enum)
            if (auto it = Enums.find(named); it != Enums.end() && !it->second.Entries.empty()) return it->second.Entries.front().Value;
        if (type == Type::Struct)
            if (auto it = Structs.find(named); it != Structs.end())
            {
                Value value = Value::object();
                for (const auto& field : it->second.Fields) value[field.Name] = field.Default;
                return value;
            }
        return DefaultValue(type);
    }
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
        if (type == Type::ArrayStruct || type == Type::ArrayEnum) return std::all_of(value.begin(), value.end(), [&](const Value& item) { return Fits(ElementType(type), enumName, item); });
        if (type == Type::Struct)
        {
            // A nested struct value is checked field by field too; the value's own JSON depth bounds the recursion.
            auto it = Structs.find(enumName);
            if (it == Structs.end() || value.size() != it->second.Fields.size()) return false;
            return std::all_of(it->second.Fields.begin(), it->second.Fields.end(), [&](const Pin& field) {
                auto member = value.find(field.Name);
                return member != value.end() && Fits(field, *member);
            });
        }
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
                for (const auto& pin : *pins) used |= EnumKind(pin.Kind) && pin.Enum == name;
            uses += used;
        }
        for (const auto& [key, v] : Variables) uses += EnumKind(v.Kind) && v.Enum == name;
        for (const auto& [key, signature] : Functions)
        {
            for (const auto* pins : { &signature.Inputs, &signature.Outputs })
                for (const auto& pin : *pins) uses += EnumKind(pin.Kind) && pin.Enum == name;
            for (const auto& [local, v] : signature.Locals) uses += EnumKind(v.Kind) && v.Enum == name;
        }
        for (const auto& [key, definition] : Structs)
            for (const auto& field : definition.Fields) uses += EnumKind(field.Kind) && field.Enum == name;
        for (const auto& [key, macro] : Macros)
            for (const auto* pins : { &macro.Inputs, &macro.Outputs })
                for (const auto& pin : *pins) uses += EnumKind(pin.Kind) && pin.Enum == name;
        return uses;
    }
    size_t Graph::StructUses(const std::string& name) const
    {
        size_t uses = 0;
        for (const auto& n : Nodes)
        {
            const auto def = Describe(n);
            bool used = n.Inputs.contains("Struct") && n.Inputs.at("Struct") == Value(name);
            for (const auto* pins : { &def.Inputs, &def.Outputs })
                for (const auto& pin : *pins) used |= StructKind(pin.Kind) && pin.Enum == name;
            uses += used;
        }
        for (const auto& [key, v] : Variables) uses += StructKind(v.Kind) && v.Enum == name;
        for (const auto& [key, signature] : Functions)
        {
            for (const auto* pins : { &signature.Inputs, &signature.Outputs })
                for (const auto& pin : *pins) uses += StructKind(pin.Kind) && pin.Enum == name;
            for (const auto& [local, v] : signature.Locals) uses += StructKind(v.Kind) && v.Enum == name;
        }
        for (const auto& [key, definition] : Structs)
            for (const auto& field : definition.Fields) uses += StructKind(field.Kind) && field.Enum == name;
        for (const auto& [key, macro] : Macros)
            for (const auto* pins : { &macro.Inputs, &macro.Outputs })
                for (const auto& pin : *pins) uses += StructKind(pin.Kind) && pin.Enum == name;
        return uses;
    }
    std::string Graph::StructCycle() const
    {
        // Depth-first over the struct fields of Struct/ArrayStruct type; a struct met again on the current path is a cycle.
        std::map<std::string, int> state;
        std::vector<std::string> path;
        std::string cycle;
        std::function<bool(const std::string&)> visit = [&](const std::string& name) {
            auto definition = Structs.find(name);
            if (definition == Structs.end() || state[name] == 2) return false;
            if (state[name] == 1)
            {
                auto start = std::find(path.begin(), path.end(), name);
                for (auto it = start; it != path.end(); ++it) cycle += *it + " -> ";
                cycle += name;
                return true;
            }
            state[name] = 1;
            path.push_back(name);
            for (const auto& field : definition->second.Fields)
                if (StructKind(field.Kind) && visit(field.Enum)) return true;
            path.pop_back();
            state[name] = 2;
            return false;
        };
        for (const auto& [name, definition] : Structs)
            if (visit(name)) return cycle;
        return {};
    }
    bool Graph::RenameStruct(const std::string& from, const std::string& to)
    {
        if (!Structs.contains(from) || to.empty() || to.size() > 64 || Structs.contains(to)) return false;
        // An inherited member is renamed in its parent; a rename of this graph's own is logged for its children.
        if (Inherited.Structs.contains(from)) return false;
        Record("Struct", from, to);
        auto entry = Structs.extract(from);
        entry.key() = to;
        Structs.insert(std::move(entry));
        for (auto& n : Nodes)
            if (auto it = n.Inputs.find("Struct"); it != n.Inputs.end() && it->second == Value(from)) it->second = to;
        auto retype = [&](Type kind, std::string& name) { if (StructKind(kind) && name == from) name = to; };
        for (auto& [key, v] : Variables) retype(v.Kind, v.Enum);
        for (auto& [key, signature] : Functions)
        {
            for (auto* pins : { &signature.Inputs, &signature.Outputs })
                for (auto& pin : *pins) retype(pin.Kind, pin.Enum);
            for (auto& [local, v] : signature.Locals) retype(v.Kind, v.Enum);
        }
        for (auto& [key, definition] : Structs)
            for (auto& field : definition.Fields) retype(field.Kind, field.Enum);
        for (auto& [key, macro] : Macros)
            for (auto* pins : { &macro.Inputs, &macro.Outputs })
                for (auto& pin : *pins) retype(pin.Kind, pin.Enum);
        return true;
    }
    bool Graph::RenameStructField(const std::string& structName, const std::string& from, const std::string& to)
    {
        auto definition = Structs.find(structName);
        if (definition == Structs.end() || to.empty() || to.size() > 64 || to == "Struct" || definition->second.Find(to)) return false;
        auto field = std::find_if(definition->second.Fields.begin(), definition->second.Fields.end(), [&](const Pin& f) { return f.Name == from; });
        if (field == definition->second.Fields.end()) return false;
        // The nodes of this struct and the stored values are found before the field changes its name.
        std::map<uint32_t, std::string> nodes;
        for (const auto& n : Nodes)
            if (n.Kind.starts_with("Struct.") && NamedOf(*this, n, "Struct") == structName) nodes[n.ID] = n.Kind;
        VisitStructValues(*this, [&](const std::string& name, Value& value) {
            if (name != structName || !value.is_object() || !value.contains(from)) return;
            value[to] = std::move(value[from]);
            value.erase(from);
        });
        field->Name = to;
        for (auto& n : Nodes)
        {
            auto kind = nodes.find(n.ID);
            if (kind == nodes.end()) continue;
            if (kind->second == "Struct.Make")
                if (auto literal = n.Inputs.extract(from)) n.Inputs[to] = std::move(literal.mapped());
            if ((kind->second == "Struct.GetField" || kind->second == "Struct.SetField") && n.Inputs.contains("Field") && n.Inputs.at("Field") == Value(from)) n.Inputs["Field"] = to;
        }
        for (auto& l : Links)
        {
            if (auto source = nodes.find(l.From); source != nodes.end() && source->second == "Struct.Break" && l.Output == from) l.Output = to;
            if (auto target = nodes.find(l.To); target != nodes.end() && target->second == "Struct.Make" && l.Input == from) l.Input = to;
        }
        return true;
    }
    size_t Graph::ConformStructValues()
    {
        size_t changed = 0;
        // Inner values are visited before the values that hold them, so one pass conforms nested values too.
        VisitStructValues(*this, [&](const std::string& name, Value& value) {
            auto definition = Structs.find(name);
            if (definition == Structs.end()) return;
            Value next = Value::object();
            for (const auto& field : definition->second.Fields)
            {
                auto member = value.is_object() ? value.find(field.Name) : value.end();
                next[field.Name] = value.is_object() && member != value.end() && Fits(field, *member) ? *member : field.Default;
            }
            if (next != value)
            {
                value = std::move(next);
                ++changed;
            }
        });
        return changed;
    }
    bool Graph::RenameEnum(const std::string& from, const std::string& to)
    {
        if (!Enums.contains(from) || to.empty() || to.size() > 64 || Enums.contains(to)) return false;
        // An inherited member is renamed in its parent; a rename of this graph's own is logged for its children.
        if (Inherited.Enums.contains(from)) return false;
        Record("Enum", from, to);
        auto entry = Enums.extract(from);
        entry.key() = to;
        Enums.insert(std::move(entry));
        for (auto& n : Nodes)
            if (auto it = n.Inputs.find("Enum"); it != n.Inputs.end() && it->second == Value(from)) it->second = to;
        auto retype = [&](Type kind, std::string& name) { if (EnumKind(kind) && name == from) name = to; };
        for (auto& [key, v] : Variables) retype(v.Kind, v.Enum);
        for (auto& [key, signature] : Functions)
        {
            for (auto* pins : { &signature.Inputs, &signature.Outputs })
                for (auto& pin : *pins) retype(pin.Kind, pin.Enum);
            for (auto& [local, v] : signature.Locals) retype(v.Kind, v.Enum);
        }
        for (auto& [key, definition] : Structs)
            for (auto& field : definition.Fields) retype(field.Kind, field.Enum);
        for (auto& [key, macro] : Macros)
            for (auto* pins : { &macro.Inputs, &macro.Outputs })
                for (auto& pin : *pins) retype(pin.Kind, pin.Enum);
        return true;
    }
    Definition Graph::Describe(const Node& n) const
    {
        auto found = Catalog().find(n.Kind);
        if (found == Catalog().end()) return { n.Kind, "Unknown", {}, {}, false };
        auto result = found->second;
        if (n.Kind.starts_with("Macro.")) return DescribeMacro(n, std::move(result));
        if (!Typed(n.Kind))
        {
            const auto has = [&](auto test) { return std::any_of(result.Inputs.begin(), result.Inputs.end(), test) || std::any_of(result.Outputs.begin(), result.Outputs.end(), test); };
            if (has([](const Pin& p) { return StructKind(p.Kind); }) || n.Kind.starts_with("Struct.")) return DescribeStruct(n, std::move(result));
            const bool enumPins = has([](const Pin& p) { return EnumKind(p.Kind); }) || n.Kind == "Enum.Switch";
            if (!enumPins) return result;
            const auto name = EnumOf(*this, n);
            const auto definition = Enums.find(name);
            for (auto* pins : { &result.Inputs, &result.Outputs })
                for (auto& pin : *pins)
                    if (EnumKind(pin.Kind))
                    {
                        pin.Enum = name;
                        if (pin.Kind == Type::Enum && definition != Enums.end() && !definition->second.Entries.empty()) pin.Default = definition->second.Entries.front().Value;
                    }
            if (n.Kind == "Enum.Switch" && definition != Enums.end())
                for (const auto& entry : definition->second.Entries) result.Outputs.push_back({ entry.Name, Type::Exec, nullptr });
            return result;
        }
        auto signature = Functions.find(Name(n));
        if (signature == Functions.end()) return result;
        if (n.Kind == "Function.Entry") result.Outputs.insert(result.Outputs.end(), signature->second.Inputs.begin(), signature->second.Inputs.end());
        if (n.Kind == "Function.Call" || n.Kind == "Parent.Call")
        {
            result.Inputs.insert(result.Inputs.end(), signature->second.Inputs.begin(), signature->second.Inputs.end());
            result.Outputs.insert(result.Outputs.end(), signature->second.Outputs.begin(), signature->second.Outputs.end());
        }
        if (n.Kind == "Function.Return") result.Inputs.insert(result.Inputs.end(), signature->second.Outputs.begin(), signature->second.Outputs.end());
        return result;
    }
    Definition Graph::DescribeStruct(const Node& n, Definition result) const
    {
        const auto name = NamedOf(*this, n, "Struct");
        const auto definition = Structs.find(name);
        for (auto* pins : { &result.Inputs, &result.Outputs })
            for (auto& pin : *pins)
                if (StructKind(pin.Kind))
                {
                    pin.Enum = name;
                    if (definition != Structs.end()) pin.Default = pin.Kind == Type::Struct ? DefaultFor(Type::Struct, name) : Value::array();
                }
        if (definition == Structs.end()) return result;
        const auto& fields = definition->second.Fields;
        // Field pins keep their own named types (an enum, an inner struct).
        if (n.Kind == "Struct.Make") result.Inputs.insert(result.Inputs.end(), fields.begin(), fields.end());
        if (n.Kind == "Struct.Break") result.Outputs = fields;
        if (n.Kind == "Struct.GetField" || n.Kind == "Struct.SetField")
        {
            auto literal = n.Inputs.find("Field");
            const Pin* field = literal != n.Inputs.end() && literal->second.is_string() ? definition->second.Find(literal->second.get<std::string>()) : nullptr;
            if (field && n.Kind == "Struct.GetField") result.Outputs = { { "Value", field->Kind, field->Default, field->Enum } };
            if (field && n.Kind == "Struct.SetField") result.Inputs.push_back({ "FieldValue", field->Kind, field->Default, field->Enum });
        }
        return result;
    }
    Definition Graph::DescribeMacro(const Node& n, Definition result) const
    {
        // An instance takes the macro's pins after its Name literal; inside the body the entry gives the inputs and
        // the exit takes the outputs. A macro without exec pins is a pure (data-only) node.
        const auto macro = Macros.find(n.Kind == "Macro.Instance" ? Name(n) : n.Macro);
        if (macro == Macros.end()) return result;
        const auto& [inputs, outputs] = std::pair{ macro->second.Inputs, macro->second.Outputs };
        if (n.Kind == "Macro.Instance")
        {
            result.Inputs.insert(result.Inputs.end(), inputs.begin(), inputs.end());
            result.Outputs = outputs;
            result.Pure = std::none_of(inputs.begin(), inputs.end(), [](const Pin& p) { return p.Kind == Type::Exec; }) && std::none_of(outputs.begin(), outputs.end(), [](const Pin& p) { return p.Kind == Type::Exec; });
        }
        if (n.Kind == "Macro.Entry") result.Outputs = inputs;
        if (n.Kind == "Macro.Exit") result.Inputs = outputs;
        return result;
    }
    size_t Graph::MacroUses(const std::string& name) const
    {
        return size_t(std::count_if(Nodes.begin(), Nodes.end(), [&](const Node& n) { return n.Kind == "Macro.Instance" && Name(n) == name; }));
    }
    bool Graph::RenameMacro(const std::string& from, const std::string& to)
    {
        if (!Macros.contains(from) || to.empty() || to.size() > 64 || Macros.contains(to)) return false;
        // An inherited member is renamed in its parent; a rename of this graph's own is logged for its children.
        if (Inherited.Macros.contains(from)) return false;
        Record("Macro", from, to);
        auto entry = Macros.extract(from);
        entry.key() = to;
        Macros.insert(std::move(entry));
        for (auto& n : Nodes)
        {
            if (n.Kind == "Macro.Instance" && Name(n) == from) n.Inputs["Name"] = to;
            if (n.Macro == from) n.Macro = to;
        }
        return true;
    }
    bool Graph::RenameMacroPin(const std::string& macro, bool output, const std::string& from, const std::string& to)
    {
        auto definition = Macros.find(macro);
        if (definition == Macros.end() || to.empty() || to.size() > 64 || to == "Name") return false;
        auto& pins = output ? definition->second.Outputs : definition->second.Inputs;
        auto pin = std::find_if(pins.begin(), pins.end(), [&](const Pin& p) { return p.Name == from; });
        if (pin == pins.end() || PinNamed(pins, to)) return false;
        pin->Name = to;
        // An input leaves the entry and enters the instances; an output enters the exit and leaves the instances.
        std::map<uint32_t, std::string> kinds;
        for (auto& n : Nodes)
        {
            const bool instance = n.Kind == "Macro.Instance" && Name(n) == macro;
            if (instance || (n.Macro == macro && (n.Kind == "Macro.Entry" || n.Kind == "Macro.Exit"))) kinds[n.ID] = n.Kind;
            if ((instance && !output) || (n.Kind == "Macro.Exit" && n.Macro == macro && output))
                if (auto literal = n.Inputs.extract(from)) n.Inputs[to] = std::move(literal.mapped());
        }
        for (auto& l : Links)
        {
            auto source = kinds.find(l.From), target = kinds.find(l.To);
            if (source != kinds.end() && l.Output == from && source->second == (output ? "Macro.Instance" : "Macro.Entry")) l.Output = to;
            if (target != kinds.end() && l.Input == from && target->second == (output ? "Macro.Exit" : "Macro.Instance")) l.Input = to;
        }
        return true;
    }
    bool Graph::RenameVariable(const std::string& from, const std::string& to)
    {
        if (!Variables.contains(from) || to.empty() || to.size() > 128 || Variables.contains(to)) return false;
        // An inherited member is renamed in its parent; a rename of this graph's own is logged for its children.
        if (Inherited.Variables.contains(from)) return false;
        Record("Variable", from, to);
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
        // An inherited member is renamed in its parent; a rename of this graph's own is logged for its children.
        if (Inherited.Functions.contains(from)) return false;
        Record("Function", from, to);
        auto entry = Functions.extract(from);
        entry.key() = to;
        Functions.insert(std::move(entry));
        for (auto& n : Nodes)
            if (Typed(n.Kind) && Name(n) == from) n.Inputs["Name"] = to;
        return true;
    }
    bool Graph::RenameFunctionPin(const std::string& function, bool result, const std::string& from, const std::string& to)
    {
        auto signature = Functions.find(function);
        if (signature == Functions.end() || to.empty() || to.size() > 128 || to == "Name" || to == "In" || to == "Then") return false;
        auto& pins = result ? signature->second.Outputs : signature->second.Inputs;
        auto pin = std::find_if(pins.begin(), pins.end(), [&](const Pin& p) { return p.Name == from; });
        if (pin == pins.end() || std::any_of(pins.begin(), pins.end(), [&](const Pin& p) { return p.Name == to; })) return false;
        pin->Name = to;
        std::map<uint32_t, std::string> kinds;
        for (auto& n : Nodes)
            if (Typed(n.Kind) && Name(n) == function)
            {
                kinds[n.ID] = n.Kind;
                if (!result && n.Kind == "Function.Call")
                    if (auto literal = n.Inputs.extract(from)) n.Inputs[to] = std::move(literal.mapped());
            }
        for (auto& l : Links)
        {
            auto source = kinds.find(l.From), target = kinds.find(l.To);
            // A parameter leaves the entry and enters the calls; a result enters the returns and leaves the calls.
            if (source != kinds.end() && l.Output == from && source->second == (result ? "Function.Call" : "Function.Entry")) l.Output = to;
            if (target != kinds.end() && l.Input == from && target->second == (result ? "Function.Return" : "Function.Call")) l.Input = to;
        }
        return true;
    }
    bool Graph::RenameEnumEntry(const std::string& enumName, const std::string& from, const std::string& to)
    {
        auto definition = Enums.find(enumName);
        if (definition == Enums.end() || to.empty() || to.size() > 64 || definition->second.Find(to)) return false;
        auto entry = std::find_if(definition->second.Entries.begin(), definition->second.Entries.end(), [&](const EnumEntry& e) { return e.Name == from; });
        if (entry == definition->second.Entries.end()) return false;
        entry->Name = to;
        std::set<uint32_t> switches;
        for (const auto& n : Nodes)
            if (n.Kind == "Enum.Switch" && EnumOf(*this, n) == enumName) switches.insert(n.ID);
        for (auto& l : Links)
            if (switches.contains(l.From) && l.Output == from) l.Output = to;
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
        fragment.Structs = Structs;
        for (const auto& n : Nodes)
            if (selected.contains(n.ID))
            {
                fragment.Nodes.push_back(n);
                fragment.Nodes.back().Macro.clear(); // the paste chooses the body (PasteNodes)
                if (Typed(n.Kind) && Functions.contains(Name(n))) fragment.Functions[Name(n)] = Functions.at(Name(n));
                // A macro instance carries the macro's pins; the body stays in this graph (PasteNodes needs the macro).
                if (n.Kind == "Macro.Instance" && Macros.contains(Name(n))) fragment.Macros[Name(n)] = Macros.at(Name(n));
            }
        for (const auto& l : Links)
            if (selected.contains(l.From) && selected.contains(l.To)) fragment.Links.push_back(l);
        return fragment.Serialize();
    }
    bool Graph::PasteNodes(const Value& value, float x, float y, std::set<uint32_t>& inserted, std::string& error, const std::string& macro)
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
            for (const auto& [name, definition] : fragment.Structs)
            {
                auto existing = next.Structs.find(name);
                if (existing == next.Structs.end()) next.Structs[name] = definition;
                else if (existing->second != definition) throw std::runtime_error("Struct " + name + " differs in this graph");
            }
            // A macro instance pastes only where the same macro exists: its body is not part of the fragment.
            for (const auto& [name, definition] : fragment.Macros)
            {
                auto existing = next.Macros.find(name);
                if (existing == next.Macros.end() || existing->second != definition) throw std::runtime_error("Macro " + name + " is not in this graph with the same pins");
            }
            if (!macro.empty() && !next.Macros.contains(macro)) throw std::runtime_error("Unknown macro: " + macro);
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
                n.Macro = macro;
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
