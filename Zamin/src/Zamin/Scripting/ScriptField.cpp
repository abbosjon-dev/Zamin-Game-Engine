#include "Zamin/Scripting/ScriptField.h"

#include <algorithm>
#include <cmath>
#include <limits>

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
            case ScriptFieldType::Entity: return "Entity";
            case ScriptFieldType::Enum:   return "Enum";
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
        if (s == "Entity") return ScriptFieldType::Entity;
        if (s == "Enum")   return ScriptFieldType::Enum;
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
            case ScriptFieldType::Entity: return sizeof(EntityRef);
            default: return 0;
        }
    }

    // Integer of `size` bytes in memory <-> int64 (memcpy: no aliasing / alignment issues).
    static int64_t ReadInteger(const void* p, size_t size, bool isSigned)
    {
        switch (size)
        {
            case 1: { uint8_t v; std::memcpy(&v, p, 1); return isSigned ? (int64_t)(int8_t)v : (int64_t)v; }
            case 2: { uint16_t v; std::memcpy(&v, p, 2); return isSigned ? (int64_t)(int16_t)v : (int64_t)v; }
            case 4: { uint32_t v; std::memcpy(&v, p, 4); return isSigned ? (int64_t)(int32_t)v : (int64_t)v; }
            default: { int64_t v; std::memcpy(&v, p, 8); return v; }
        }
    }

    static void WriteInteger(void* p, size_t size, int64_t value)
    {
        switch (size)
        {
            case 1: { uint8_t v = (uint8_t)value; std::memcpy(p, &v, 1); break; }
            case 2: { uint16_t v = (uint16_t)value; std::memcpy(p, &v, 2); break; }
            case 4: { uint32_t v = (uint32_t)value; std::memcpy(p, &v, 4); break; }
            default: std::memcpy(p, &value, 8); break;
        }
    }

    void ScriptFieldValue::ReadFrom(const void* field)
    {
        if (Type == ScriptFieldType::String) StringValue = *static_cast<const std::string*>(field);
        else if (Type == ScriptFieldType::Enum) SetEnum(ReadInteger(field, sizeof(int), true));
        else std::memcpy(Data, field, SizeOf(Type));
    }

    void ScriptFieldValue::WriteTo(void* field) const
    {
        if (Type == ScriptFieldType::String) *static_cast<std::string*>(field) = StringValue;
        else if (Type == ScriptFieldType::Enum) WriteInteger(field, sizeof(int), GetEnum());
        else std::memcpy(field, Data, SizeOf(Type));
    }

    // ------------------------------------------------------------------ attributes
    ScriptFieldInfo& ScriptFieldInfo::Range(float min, float max)
    {
        HasRange = true;
        RangeMin = std::min(min, max);
        RangeMax = std::max(min, max);
        return *this;
    }

    ScriptFieldInfo& ScriptFieldInfo::Tooltip(std::string text) { TooltipText = std::move(text); return *this; }
    ScriptFieldInfo& ScriptFieldInfo::Label(std::string text) { DisplayName = std::move(text); return *this; }
    ScriptFieldInfo& ScriptFieldInfo::Header(std::string text) { HeaderText = std::move(text); return *this; }
    ScriptFieldInfo& ScriptFieldInfo::Color(bool hdr) { IsColor = true; IsHdr = hdr; return *this; }
    ScriptFieldInfo& ScriptFieldInfo::Multiline(bool multiline) { IsMultiline = multiline; return *this; }

    ScriptFieldInfo& ScriptFieldInfo::Options(std::initializer_list<ScriptEnumOption> options)
    {
        EnumOptions.clear();
        int64_t next = 0;
        for (const auto& option : options)
        {
            ScriptEnumOption o = option;
            if (o.AutoValue) o.Value = next;
            o.AutoValue = false;
            next = o.Value + 1;
            EnumOptions.push_back(std::move(o));
        }
        return *this;
    }

    const ScriptEnumOption* ScriptFieldInfo::FindOption(int64_t value) const
    {
        for (const auto& o : EnumOptions)
            if (o.Value == value)
                return &o;
        return nullptr;
    }

    void ScriptFieldInfo::ClampToRange(ScriptFieldValue& value) const
    {
        if (!HasRange || value.Type != Type) return;
        auto clampVec = [&]<typename V>(V v) { return glm::clamp(v, V(RangeMin), V(RangeMax)); };
        switch (Type)
        {
            case ScriptFieldType::Float: value.Set(std::clamp(value.Get<float>(), RangeMin, RangeMax)); break;
            case ScriptFieldType::Int:
            {
                // Integer bounds: the largest/smallest whole numbers inside the float range.
                double lo = std::ceil((double)RangeMin), hi = std::floor((double)RangeMax);
                lo = std::clamp(lo, (double)std::numeric_limits<int>::min(), (double)std::numeric_limits<int>::max());
                hi = std::clamp(hi, lo, (double)std::numeric_limits<int>::max());
                value.Set(std::clamp(value.Get<int>(), (int)lo, (int)hi));
                break;
            }
            case ScriptFieldType::Vec2: value.Set(clampVec(value.Get<glm::vec2>())); break;
            case ScriptFieldType::Vec3: value.Set(clampVec(value.Get<glm::vec3>())); break;
            case ScriptFieldType::Vec4: value.Set(clampVec(value.Get<glm::vec4>())); break;
            default: break;
        }
    }

    // ------------------------------------------------------------------ instance access
    bool ScriptFieldInfo::Convert(const ScriptFieldValue& in, ScriptFieldValue& out) const
    {
        if (in.Type == Type)
        {
            out = in;
            return true;
        }
        // A field changed between int and an enum keeps its stored number.
        if (Type == ScriptFieldType::Enum && in.Type == ScriptFieldType::Int)
        {
            out = {};
            out.SetEnum(in.Get<int>());
            return true;
        }
        if (Type == ScriptFieldType::Int && in.Type == ScriptFieldType::Enum)
        {
            out = {};
            int64_t v = std::clamp<int64_t>(in.GetEnum(), std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
            out.Set((int)v);
            return true;
        }
        return false;
    }

    void ScriptFieldInfo::Load(const ScriptableEntity* instance, ScriptFieldValue& out) const
    {
        out = {};
        out.Type = Type;
        if (!instance || !Accessor) return;
        const void* field = Accessor(const_cast<ScriptableEntity*>(instance));
        if (Type == ScriptFieldType::Enum)
            out.SetEnum(ReadInteger(field, EnumSize, EnumSigned));
        else
            out.ReadFrom(field);
    }

    bool ScriptFieldInfo::Store(ScriptableEntity* instance, const ScriptFieldValue& value) const
    {
        if (!instance || !Accessor) return false;
        ScriptFieldValue converted;
        if (!Convert(value, converted)) return false;
        void* field = Accessor(instance);
        if (Type == ScriptFieldType::Enum)
            WriteInteger(field, EnumSize, converted.GetEnum());
        else
            converted.WriteTo(field);
        return true;
    }

}
