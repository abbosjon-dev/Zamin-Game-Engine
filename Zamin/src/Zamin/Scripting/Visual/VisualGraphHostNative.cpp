#include "VisualGraphHost.h"
#include "Zamin/Core/Log.h"
#include <algorithm>
#include <functional>
#include <stdexcept>
// Native method calls: graphs call reflected C++ methods and free functions (ScriptMethod.h) through the
// Native.Call / Native.Pure nodes. The graph keeps the signature it was built with; the host binds it to the live
// method (again after every module load, unload or hot reload) and runs it on the Target entity's script.
namespace Zamin {
    namespace {
        using Visual::Type;
        Type PinType(const ScriptValueType& t)
        {
            Type element = Type::Exec;
            switch (t.Kind)
            {
                case ScriptValueKind::Bool:   element = Type::Bool; break;
                case ScriptValueKind::Number: element = Type::Number; break;
                case ScriptValueKind::String: element = Type::String; break;
                case ScriptValueKind::Vec2:   element = Type::Vec2; break;
                case ScriptValueKind::Vec3:   element = Type::Vec3; break;
                case ScriptValueKind::Vec4:   element = Type::Vec4; break;
                case ScriptValueKind::Entity: element = Type::Entity; break;
                case ScriptValueKind::Enum:   element = Type::Enum; break;
                case ScriptValueKind::Struct: element = Type::Struct; break;
                default:                      return Type::Exec;
            }
            // ArrayBool..ArrayStruct follow Bool..Struct.
            return t.Array ? Type(int(element) - int(Type::Bool) + int(Type::ArrayBool)) : element;
        }
        // A C++ value type's default graph value (an enum's first option, a struct's field defaults).
        Visual::Value DefaultOf(const ScriptValueType& t, int depth = 0)
        {
            if (t.Array) return Visual::Value::array();
            if (t.Kind == ScriptValueKind::Enum)
            {
                const auto* e = ScriptRegistry::FindEnum(t.Named);
                return e && !e->Options.empty() ? e->Options.front().Value : 0;
            }
            if (t.Kind == ScriptValueKind::Struct)
            {
                Visual::Value v = Visual::Value::object();
                if (const auto* s = ScriptRegistry::FindStruct(t.Named); s && depth < 8)
                    for (const auto& field : s->Fields) v[field.Name] = DefaultOf(field.Type, depth + 1);
                return v;
            }
            return Visual::DefaultValue(PinType(t));
        }
        bool MakePin(const std::string& name, const ScriptValueType& t, Visual::Pin& pin, std::string& error)
        {
            pin = { name, PinType(t), DefaultOf(t), t.Named };
            if (pin.Kind == Type::Exec) error = "unsupported type " + ScriptValueTypeName(t) + " of " + name;
            else if ((t.Kind == ScriptValueKind::Enum && !ScriptRegistry::FindEnum(t.Named)) || (t.Kind == ScriptValueKind::Struct && !ScriptRegistry::FindStruct(t.Named)))
                error = "C++ type " + t.Named + " of " + name + " is not registered (ZM_SCRIPT_ENUM / ZM_SCRIPT_STRUCT)";
            return error.empty();
        }
        // The C++ enum as a graph enum (names and values kept).
        bool EnumOf(const std::string& name, Visual::EnumDefinition& out, std::string& error)
        {
            const auto* info = ScriptRegistry::FindEnum(name);
            if (!info || info->Options.empty() || info->Options.size() > 256)
            {
                error = "C++ enum " + name + " is not registered with 1..256 options";
                return false;
            }
            out = {};
            out.Source = "C++ enum " + name;
            for (const auto& option : info->Options)
            {
                if (option.Name.empty() || option.Name.size() > 64 || out.Find(option.Name) || out.Find(option.Value) || std::abs(option.Value) > (int64_t(1) << 53))
                {
                    error = "C++ enum option cannot become a graph enum entry: " + name + "." + option.Name;
                    return false;
                }
                out.Entries.push_back({ option.Name, option.Value });
            }
            return true;
        }
        bool StructOf(const std::string& name, Visual::StructDefinition& out, std::string& error)
        {
            const auto* info = ScriptRegistry::FindStruct(name);
            if (!info || info->Fields.empty() || info->Fields.size() > 32)
            {
                error = "C++ struct " + name + " is not registered with 1..32 fields";
                return false;
            }
            out = {};
            for (const auto& field : info->Fields)
            {
                Visual::Pin pin;
                if (!MakePin(field.Name, field.Type, pin, error)) return false;
                out.Fields.push_back(std::move(pin));
            }
            return true;
        }
        // The named types a signature uses, inner struct fields first (dependencies before their users).
        void NamedTypes(const std::vector<Visual::Pin>& pins, std::vector<std::pair<bool, std::string>>& order, std::set<std::string>& seen, int depth = 0)
        {
            for (const auto& pin : pins)
            {
                if (!Visual::IsNamed(pin.Kind) || seen.contains(pin.Enum) || depth > 8) continue;
                const bool isStruct = pin.Kind == Type::Struct || pin.Kind == Type::ArrayStruct;
                seen.insert(pin.Enum);
                if (isStruct)
                    if (const auto* info = ScriptRegistry::FindStruct(pin.Enum))
                    {
                        std::vector<Visual::Pin> fields;
                        std::string ignored;
                        for (const auto& field : info->Fields)
                            if (Visual::Pin p; MakePin(field.Name, field.Type, p, ignored)) fields.push_back(p);
                        NamedTypes(fields, order, seen, depth + 1);
                    }
                order.push_back({ isStruct, pin.Enum });
            }
        }
        bool SameFields(const Visual::StructDefinition& a, const Visual::StructDefinition& b)
        {
            return a.Fields.size() == b.Fields.size() && std::equal(a.Fields.begin(), a.Fields.end(), b.Fields.begin(), [](const auto& x, const auto& y) { return x.Name == y.Name && Visual::SameType(x, y); });
        }
    }

    std::vector<std::string> NativeMethodKeys()
    {
        std::vector<std::string> keys;
        for (const auto& [name, info] : ScriptRegistry::GetAll())
            for (const auto& method : info.Methods) keys.push_back(name + "." + method.Name);
        for (const auto& [name, library] : ScriptRegistry::GetLibraries())
            for (const auto& function : library.Functions) keys.push_back(name + "." + function.Name);
        std::sort(keys.begin(), keys.end());
        return keys;
    }

    bool NativeSignatureOf(const std::string& key, Visual::NativeSignature& out, std::string& error)
    {
        error.clear();
        const auto* method = ScriptRegistry::FindMethod(key);
        if (!method)
        {
            error = "C++ method " + key + " is not registered (is the game module built and loaded?)";
            return false;
        }
        Visual::NativeSignature signature;
        signature.Static = method->Static;
        signature.Pure = method->IsPure;
        signature.Tooltip = method->TooltipText.substr(0, 1024);
        for (const auto& parameter : method->Parameters)
        {
            Visual::Pin pin;
            if (!MakePin(parameter.Name, parameter.Type, pin, error)) return false;
            signature.Inputs.push_back(std::move(pin));
        }
        if (method->Result.Kind != ScriptValueKind::Void)
        {
            Visual::Pin pin;
            if (!MakePin(method->ResultName, method->Result, pin, error)) return false;
            signature.Outputs.push_back(std::move(pin));
        }
        if (signature.Pure && signature.Outputs.empty())
        {
            error = "pure C++ method " + key + " has no result";
            return false;
        }
        if (!method->Invoke)
        {
            error = "C++ method " + key + " has no invoker";
            return false;
        }
        for (const auto* pins : { &signature.Inputs, &signature.Outputs })
            for (std::set<std::string> names; const auto& pin : *pins)
                if (pin.Name.empty() || pin.Name.size() > 64 || pin.Name == "In" || pin.Name == "Then" || pin.Name == "Method" || pin.Name == "Target" || !names.insert(pin.Name).second)
                {
                    error = "C++ method " + key + " has an empty, reserved (In, Then, Method, Target) or duplicate parameter name: " + pin.Name;
                    return false;
                }
        if (signature.Inputs.size() > 32)
        {
            error = "C++ method " + key + " has more than 32 parameters";
            return false;
        }
        out = std::move(signature);
        return true;
    }

    std::string NativeBindingProblem(const Visual::Graph& graph, const std::string& key, const Visual::NativeSignature& signature)
    {
        Visual::NativeSignature live;
        std::string error;
        if (!NativeSignatureOf(key, live, error)) return error;
        if (!live.Matches(signature)) return "the C++ signature changed: the graph has " + signature.Text() + ", C++ has " + live.Text() + " (refresh the node)";
        std::vector<std::pair<bool, std::string>> named;
        std::set<std::string> seen;
        NamedTypes(live.Inputs, named, seen);
        NamedTypes(live.Outputs, named, seen);
        for (const auto& [isStruct, name] : named)
        {
            if (isStruct)
            {
                Visual::StructDefinition cpp;
                if (!StructOf(name, cpp, error)) return error;
                auto it = graph.Structs.find(name);
                if (it == graph.Structs.end()) return "the graph has no struct " + name + " (refresh the node)";
                if (!SameFields(it->second, cpp)) return "C++ struct " + name + " has other fields than the graph's (refresh the node)";
            }
            else
            {
                Visual::EnumDefinition cpp;
                if (!EnumOf(name, cpp, error)) return error;
                auto it = graph.Enums.find(name);
                if (it == graph.Enums.end()) return "the graph has no enum " + name + " (refresh the node)";
                if (it->second.Entries != cpp.Entries) return "C++ enum " + name + " has other entries than the graph's (refresh the node)";
            }
        }
        return {};
    }

    std::map<std::string, std::string> NativeBindingProblems(const Visual::Graph& graph)
    {
        std::map<std::string, std::string> problems;
        for (const auto& key : graph.NativesUsed())
        {
            auto it = graph.Natives.find(key);
            if (it == graph.Natives.end()) continue; // Validate reports it
            if (auto problem = NativeBindingProblem(graph, key, it->second); !problem.empty()) problems[key] = problem;
        }
        // The parent chain's calls run too.
        if (graph.Inherited.Base)
            for (auto& [key, problem] : NativeBindingProblems(*graph.Inherited.Base)) problems.try_emplace(key, problem);
        return problems;
    }

    bool ImportNativeTypes(Visual::Graph& graph, const Visual::NativeSignature& signature, std::string& error, bool refresh)
    {
        error.clear();
        std::vector<std::pair<bool, std::string>> named;
        std::set<std::string> seen;
        NamedTypes(signature.Inputs, named, seen);
        NamedTypes(signature.Outputs, named, seen);
        Visual::Graph next = graph;
        bool conform = false;
        for (const auto& [isStruct, name] : named)
        {
            if (isStruct)
            {
                Visual::StructDefinition cpp;
                if (!StructOf(name, cpp, error)) return false;
                auto it = next.Structs.find(name);
                if (it == next.Structs.end())
                {
                    if (next.Structs.size() >= 64) return error = "struct limit 64", false;
                    next.Structs[name] = std::move(cpp);
                }
                else if (!SameFields(it->second, cpp))
                {
                    if (!refresh || next.IsInherited("Struct", name)) return error = "the graph's struct " + name + " differs from the C++ struct", false;
                    it->second = std::move(cpp);
                    conform = true;
                }
            }
            else
            {
                Visual::EnumDefinition cpp;
                if (!EnumOf(name, cpp, error)) return false;
                auto it = next.Enums.find(name);
                if (it == next.Enums.end())
                {
                    if (next.Enums.size() >= 64) return error = "enum limit 64", false;
                    next.Enums[name] = std::move(cpp);
                }
                else if (it->second.Entries != cpp.Entries)
                {
                    if (!refresh || it->second.Source != cpp.Source || next.IsInherited("Enum", name)) return error = "the graph's enum " + name + " differs from the C++ enum", false;
                    it->second = std::move(cpp);
                }
            }
        }
        if (conform) next.ConformStructValues();
        graph = std::move(next);
        return true;
    }

    Visual::Value VisualGraphHost::CallNative(Entity target, const std::map<std::string, Visual::Value>& args)
    {
        const auto key = args.at("Method").get<std::string>();
        auto& binding = m_Natives[key];
        if (binding.Generation != ScriptRegistry::Generation())
        {
            // First use, or the registry changed (module load, unload, hot reload): bind again.
            binding = {};
            binding.Generation = ScriptRegistry::Generation();
            auto signature = VM.Natives().find(key);
            if (signature == VM.Natives().end()) binding.Problem = "no signature in the graph";
            else binding.Problem = NativeBindingProblem(VM.Loaded(), key, signature->second);
            if (binding.Problem.empty()) binding.Method = ScriptRegistry::FindMethod(key, &binding.Owner);
        }
        if (!binding.Problem.empty() || !binding.Method) throw std::runtime_error("Native method " + key + ": " + (binding.Problem.empty() ? "not bound" : binding.Problem));
        ScriptableEntity* self = nullptr;
        if (!binding.Method->Static)
        {
            auto* script = target.TryGetComponent<NativeScriptComponent>();
            if (!script || script->ClassName != binding.Owner || !script->Instance || script->Instance->GetScriptClassName() != binding.Owner)
                throw std::runtime_error("Native method " + key + ": the Target entity has no live " + binding.Owner + " script");
            self = script->Instance;
        }
        std::vector<Visual::Value> values;
        values.reserve(binding.Method->Parameters.size());
        for (const auto& parameter : binding.Method->Parameters) values.push_back(args.at(parameter.Name));
        try
        {
            return binding.Method->Invoke(self, values.data());
        }
        catch (const std::exception& e)
        {
            throw std::runtime_error("Native method " + key + ": " + e.what());
        }
    }
}
