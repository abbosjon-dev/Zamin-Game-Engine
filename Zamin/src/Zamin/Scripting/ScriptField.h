#pragma once

#include <glm/glm.hpp>

#include <cstring>
#include <string>
#include <type_traits>

namespace Zamin {

    class ScriptableEntity;

    enum class ScriptFieldType { None = 0, Float, Int, Bool, Vec2, Vec3, Vec4, String };

    const char* ScriptFieldTypeToString(ScriptFieldType type);
    ScriptFieldType ScriptFieldTypeFromString(const std::string& str);

    template<typename T> constexpr ScriptFieldType ScriptFieldTypeOf()
    {
        if constexpr (std::is_same_v<T, float>)            return ScriptFieldType::Float;
        else if constexpr (std::is_same_v<T, int>)         return ScriptFieldType::Int;
        else if constexpr (std::is_same_v<T, bool>)        return ScriptFieldType::Bool;
        else if constexpr (std::is_same_v<T, glm::vec2>)   return ScriptFieldType::Vec2;
        else if constexpr (std::is_same_v<T, glm::vec3>)   return ScriptFieldType::Vec3;
        else if constexpr (std::is_same_v<T, glm::vec4>)   return ScriptFieldType::Vec4;
        else if constexpr (std::is_same_v<T, std::string>) return ScriptFieldType::String;
        else
        {
            static_assert(sizeof(T) == 0, "ZM_FIELD: qo'llab-quvvatlanmaydigan tur (float,int,bool,vec2,vec3,vec4,std::string)");
            return ScriptFieldType::None;
        }
    }

    // Field exposed to the editor inspector.
    struct ScriptFieldInfo
    {
        std::string Name;
        ScriptFieldType Type = ScriptFieldType::None;
        void* (*Accessor)(ScriptableEntity*) = nullptr; // returns pointer to the member in an instance
    };

    // Stored value of a field (lives in NativeScriptComponent, serialized with the scene).
    struct ScriptFieldValue
    {
        ScriptFieldType Type = ScriptFieldType::None;
        alignas(16) unsigned char Data[16]{};
        std::string StringValue;

        template<typename T>
        T Get() const
        {
            if constexpr (std::is_same_v<T, std::string>) return StringValue;
            else { T v; std::memcpy(&v, Data, sizeof(T)); return v; }
        }

        template<typename T>
        void Set(const T& value)
        {
            Type = ScriptFieldTypeOf<T>();
            if constexpr (std::is_same_v<T, std::string>) StringValue = value;
            else { static_assert(sizeof(T) <= 16); std::memcpy(Data, &value, sizeof(T)); }
        }

        // Copy between this value and a live field.
        void ReadFrom(const void* field);
        void WriteTo(void* field) const;
    };

}
