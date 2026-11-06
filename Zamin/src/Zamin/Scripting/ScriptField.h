#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string>
#include <type_traits>
#include <vector>

namespace Zamin {

    class ScriptableEntity;

    // Reference to another entity in the scene (set in the inspector by drag & drop).
    struct EntityRef
    {
        uint64_t ID = 0;
        explicit operator bool() const { return ID != 0; }
        bool operator==(const EntityRef&) const = default;
    };

    // Enum: any C++ enum member (stored as a 64-bit integer, written back with the
    // member's own size). Give it option names with ZM_FIELD_ENUM for a dropdown.
    enum class ScriptFieldType { None = 0, Float, Int, Bool, Vec2, Vec3, Vec4, String, Entity, Enum };

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
        else if constexpr (std::is_same_v<T, EntityRef>)   return ScriptFieldType::Entity;
        else if constexpr (std::is_enum_v<T>)              return ScriptFieldType::Enum;
        else
        {
            static_assert(sizeof(T) == 0, "ZM_FIELD: qo'llab-quvvatlanmaydigan tur (float,int,bool,vec2,vec3,vec4,std::string,EntityRef,enum)");
            return ScriptFieldType::None;
        }
    }

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
            else if constexpr (std::is_enum_v<T>) return static_cast<T>(GetEnum());
            else { T v; std::memcpy(&v, Data, sizeof(T)); return v; }
        }

        template<typename T>
        void Set(const T& value)
        {
            Type = ScriptFieldTypeOf<T>();
            if constexpr (std::is_same_v<T, std::string>) StringValue = value;
            else if constexpr (std::is_enum_v<T>) SetEnum(static_cast<int64_t>(value));
            else { static_assert(sizeof(T) <= 16); std::memcpy(Data, &value, sizeof(T)); }
        }

        // Enum values (any underlying type) are kept as int64.
        int64_t GetEnum() const { int64_t v; std::memcpy(&v, Data, sizeof(v)); return v; }
        void SetEnum(int64_t value) { Type = ScriptFieldType::Enum; std::memcpy(Data, &value, sizeof(value)); }

        // Copy between this value and a live field. An Enum is treated as int-sized here;
        // ScriptFieldInfo::Load / Store use the member's real size.
        void ReadFrom(const void* field);
        void WriteTo(void* field) const;
    };

    // One named value of an enum field (ZM_FIELD_ENUM / ScriptFieldInfo::Options).
    //   "Idle"            - value = previous value + 1 (0 for the first)
    //   {"Boss", 10}      - explicit value (an enum constant works too: {"Boss", State::Boss})
    struct ScriptEnumOption
    {
        std::string Name;
        int64_t Value = 0;
        bool AutoValue = true;

        ScriptEnumOption(const char* name) : Name(name) {}
        ScriptEnumOption(std::string name) : Name(std::move(name)) {}
        template<typename V> requires (std::is_integral_v<V> || std::is_enum_v<V>)
        ScriptEnumOption(const char* name, V value) : Name(name), Value(static_cast<int64_t>(value)), AutoValue(false) {}
    };

    // Field exposed to the editor inspector. The attribute setters only change how the
    // inspector shows the field and can be chained:
    //   ZM_FIELD(Player, Speed).Range(0.0f, 20.0f).Tooltip("Yurish tezligi (m/s)")
    struct ScriptFieldInfo
    {
        std::string Name;
        ScriptFieldType Type = ScriptFieldType::None;
        void* (*Accessor)(ScriptableEntity*) = nullptr; // returns pointer to the member in an instance

        // Enum members: size (bytes) and signedness of the underlying type.
        uint8_t EnumSize = sizeof(int);
        bool EnumSigned = true;

        // ---- inspector attributes
        std::string DisplayName;    // label (empty = Name)
        std::string TooltipText;    // shown when hovering the field
        std::string HeaderText;     // section title drawn above the field
        bool HasRange = false;      // Float / Int / Vec*: slider limited to [RangeMin, RangeMax]
        float RangeMin = 0.0f, RangeMax = 0.0f;
        bool IsColor = false;       // Vec3 / Vec4 shown as a color picker
        bool IsHdr = false;         // color components may go above 1
        bool IsMultiline = false;   // String: multi-line text box
        std::vector<ScriptEnumOption> EnumOptions; // Enum (or Int) shown as a dropdown

        ScriptFieldInfo& Range(float min, float max);
        ScriptFieldInfo& Tooltip(std::string text);
        ScriptFieldInfo& Label(std::string text);
        ScriptFieldInfo& Header(std::string text);
        ScriptFieldInfo& Color(bool hdr = false);
        ScriptFieldInfo& Multiline(bool multiline = true);
        // Auto values continue from the previous option: {"A", "B", {"Z", 10}, "W"} -> 0, 1, 10, 11
        ScriptFieldInfo& Options(std::initializer_list<ScriptEnumOption> options);

        const std::string& GetDisplayName() const { return DisplayName.empty() ? Name : DisplayName; }
        // The option with this value (nullptr when the value has no name).
        const ScriptEnumOption* FindOption(int64_t value) const;
        // Clamps Float / Int / Vec* values into the range (no-op without one).
        void ClampToRange(ScriptFieldValue& value) const;

        // `in` converted to this field's type: same type, or Int <-> Enum. false otherwise.
        bool Convert(const ScriptFieldValue& in, ScriptFieldValue& out) const;
        // Reads the member of a live instance.
        void Load(const ScriptableEntity* instance, ScriptFieldValue& out) const;
        // Writes a (converted) value into the member; false when the type does not fit.
        bool Store(ScriptableEntity* instance, const ScriptFieldValue& value) const;
    };

}
