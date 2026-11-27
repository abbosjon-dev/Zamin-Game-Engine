#include "Zamin/Scripting/ScriptMethod.h"
#include "Zamin/Scripting/ScriptEngine.h"

namespace Zamin {

    std::string ScriptValueTypeName(const ScriptValueType& type)
    {
        static const char* names[] = { "Void", "Bool", "Number", "String", "Vec2", "Vec3", "Vec4", "Entity", "Enum", "Struct" };
        const auto index = size_t(type.Kind);
        std::string name = index < std::size(names) ? names[index] : "Invalid";
        if (!type.Named.empty()) name += " " + type.Named;
        if (type.Array) name += "[]";
        return name;
    }

    ScriptMethodInfo& ScriptMethodInfo::Params(std::initializer_list<const char*> names)
    {
        size_t i = 0;
        for (const char* name : names)
            if (i < Parameters.size() && name) Parameters[i++].Name = name;
        return *this;
    }

    namespace ScriptReflection {
        void Fail(const std::string& message) { throw std::runtime_error(message); }
        double Number(const ScriptValue& value)
        {
            if (!value.is_number()) Fail("expected a Number");
            const double number = value.get<double>();
            if (!std::isfinite(number)) Fail("expected a finite Number");
            return number;
        }
    }

    ScriptEnumRegistrar::ScriptEnumRegistrar(const char* name, std::initializer_list<ScriptEnumOption> options) : Name(name)
    {
        // Auto values continue from the previous option, as in ZM_FIELD_ENUM.
        ScriptFieldInfo resolved;
        resolved.Options(options);
        ScriptRegistry::RegisterEnum({ Name, resolved.EnumOptions });
    }
    ScriptEnumRegistrar::~ScriptEnumRegistrar() { ScriptRegistry::UnregisterEnum(Name); }

    ScriptStructRegistrar::ScriptStructRegistrar(const ScriptStructInfo& info) : Name(info.Name) { ScriptRegistry::RegisterStruct(info); }
    ScriptStructRegistrar::~ScriptStructRegistrar() { ScriptRegistry::UnregisterStruct(Name); }

    ScriptLibraryRegistrar::ScriptLibraryRegistrar(const char* name, std::vector<ScriptMethodInfo> functions) : Name(name)
    {
        ScriptRegistry::RegisterLibrary({ Name, std::move(functions) });
    }
    ScriptLibraryRegistrar::~ScriptLibraryRegistrar() { ScriptRegistry::UnregisterLibrary(Name); }

}
