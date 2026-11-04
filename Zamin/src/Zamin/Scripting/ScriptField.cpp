#include "Zamin/Scripting/ScriptField.h"

namespace Zamin {

    const char* ScriptFieldTypeToString(ScriptFieldType type)
    {
        switch (type)
        {
            case ScriptFieldType::Float:  return "Float";
            case ScriptFieldType::Int:    return "Int";
            case ScriptFieldType::Bool:   return "Bool";
            case ScriptFieldType::Vec2:   return "Vec2";
            case ScriptFieldType::Vec3:   return "Vec3";
            case ScriptFieldType::Vec4:   return "Vec4";
            case ScriptFieldType::String: return "String";
            default: return "None";
        }
    }

    ScriptFieldType ScriptFieldTypeFromString(const std::string& s)
    {
        if (s == "Float")  return ScriptFieldType::Float;
        if (s == "Int")    return ScriptFieldType::Int;
        if (s == "Bool")   return ScriptFieldType::Bool;
        if (s == "Vec2")   return ScriptFieldType::Vec2;
        if (s == "Vec3")   return ScriptFieldType::Vec3;
        if (s == "Vec4")   return ScriptFieldType::Vec4;
        if (s == "String") return ScriptFieldType::String;
        return ScriptFieldType::None;
    }

    static size_t SizeOf(ScriptFieldType type)
    {
        switch (type)
        {
            case ScriptFieldType::Float: return sizeof(float);
            case ScriptFieldType::Int:   return sizeof(int);
            case ScriptFieldType::Bool:  return sizeof(bool);
            case ScriptFieldType::Vec2:  return sizeof(glm::vec2);
            case ScriptFieldType::Vec3:  return sizeof(glm::vec3);
            case ScriptFieldType::Vec4:  return sizeof(glm::vec4);
            default: return 0;
        }
    }

    void ScriptFieldValue::ReadFrom(const void* field)
    {
        if (Type == ScriptFieldType::String) StringValue = *static_cast<const std::string*>(field);
        else std::memcpy(Data, field, SizeOf(Type));
    }

    void ScriptFieldValue::WriteTo(void* field) const
    {
        if (Type == ScriptFieldType::String) *static_cast<std::string*>(field) = StringValue;
        else std::memcpy(field, Data, SizeOf(Type));
    }

}
