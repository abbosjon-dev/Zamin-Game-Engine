#pragma once

#include "Zamin/Scripting/ScriptField.h"

#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace Zamin {

    class ScriptableEntity;

    struct ScriptClassInfo
    {
        std::string Name;
        ScriptableEntity* (*Factory)() = nullptr;
        std::vector<ScriptFieldInfo> Fields;
    };

    // Global registry of C++ script classes. Classes register themselves
    // through ZM_REGISTER_SCRIPT when the game module (.so) is loaded and
    // unregister automatically when it is unloaded (hot reload).
    class ScriptRegistry
    {
    public:
        static void Register(const ScriptClassInfo& info);
        static void Unregister(const std::string& name);

        static const ScriptClassInfo* Find(const std::string& name);
        static const std::map<std::string, ScriptClassInfo>& GetAll();
        static ScriptableEntity* Instantiate(const std::string& name);
    };

    // Loads the game's compiled C++ script module (lib<Game>.so).
    class ScriptEngine
    {
    public:
        // Loads (or reloads) the module. Copies it to a temp file first so the
        // original can be rebuilt while the editor is running.
        static bool LoadModule(const std::filesystem::path& path);
        static void UnloadModule();
        static bool IsModuleLoaded();
        static const std::filesystem::path& GetModulePath();
        static std::filesystem::file_time_type GetLoadedModuleWriteTime();

        // Called before the module is unloaded so live script instances can be destroyed.
        static void SetBeforeUnloadCallback(const std::function<void()>& callback);
    };

    struct ScriptRegistrar
    {
        ScriptRegistrar(const char* name, ScriptableEntity* (*factory)(), std::vector<ScriptFieldInfo> fields)
            : Name(name)
        {
            ScriptRegistry::Register({ name, factory, std::move(fields) });
        }
        ~ScriptRegistrar() { ScriptRegistry::Unregister(Name); }
        std::string Name;
    };

    template<typename Class, typename T>
    ScriptFieldInfo MakeScriptField(const char* name, void* (*accessor)(ScriptableEntity*))
    {
        ScriptFieldInfo info;
        info.Name = name;
        info.Type = ScriptFieldTypeOf<T>();
        info.Accessor = accessor;
        if constexpr (std::is_enum_v<T>)
        {
            info.EnumSize = (uint8_t)sizeof(T);
            info.EnumSigned = std::is_signed_v<std::underlying_type_t<T>>;
        }
        return info;
    }

    // Compile-time checked attribute helpers behind the ZM_FIELD_* macros.
    namespace ScriptFieldAttributes {

        template<typename T>
        ScriptFieldInfo Color(ScriptFieldInfo info, bool hdr)
        {
            static_assert(std::is_same_v<T, glm::vec3> || std::is_same_v<T, glm::vec4>,
                          "ZM_FIELD_COLOR: maydon glm::vec3 yoki glm::vec4 bo'lishi kerak");
            info.Color(hdr);
            return info;
        }

        template<typename T>
        ScriptFieldInfo Range(ScriptFieldInfo info, float min, float max)
        {
            static_assert(std::is_same_v<T, float> || std::is_same_v<T, int> || std::is_same_v<T, glm::vec2> ||
                          std::is_same_v<T, glm::vec3> || std::is_same_v<T, glm::vec4>,
                          "ZM_FIELD_RANGE: maydon float, int, glm::vec2, glm::vec3 yoki glm::vec4 bo'lishi kerak");
            info.Range(min, max);
            return info;
        }

        template<typename T>
        ScriptFieldInfo Enum(ScriptFieldInfo info, std::initializer_list<ScriptEnumOption> options)
        {
            static_assert(std::is_enum_v<T> || std::is_same_v<T, int>,
                          "ZM_FIELD_ENUM: maydon enum (yoki int) bo'lishi kerak");
            info.Options(options);
            return info;
        }

    }

}

#define ZM_FIELD_TYPE(Class, Member) std::remove_cvref_t<decltype(std::declval<Class&>().Member)>

// Registers a member for the inspector:  ZM_REGISTER_SCRIPT(Player, ZM_FIELD(Player, Speed), ...)
// Attributes can be chained:  ZM_FIELD(Player, Speed).Range(0, 20).Tooltip("...").Header("Harakat")
#define ZM_FIELD(Class, Member) \
    ::Zamin::MakeScriptField<Class, ZM_FIELD_TYPE(Class, Member)>(#Member, \
        [](::Zamin::ScriptableEntity* s) -> void* { return &static_cast<Class*>(s)->Member; })

// glm::vec3 / glm::vec4 edited with a color picker (HDR: components may exceed 1).
#define ZM_FIELD_COLOR(Class, Member) \
    ::Zamin::ScriptFieldAttributes::Color<ZM_FIELD_TYPE(Class, Member)>(ZM_FIELD(Class, Member), false)
#define ZM_FIELD_COLOR_HDR(Class, Member) \
    ::Zamin::ScriptFieldAttributes::Color<ZM_FIELD_TYPE(Class, Member)>(ZM_FIELD(Class, Member), true)

// float / int / glm::vecN edited with a slider limited to [Min, Max].
#define ZM_FIELD_RANGE(Class, Member, Min, Max) \
    ::Zamin::ScriptFieldAttributes::Range<ZM_FIELD_TYPE(Class, Member)>(ZM_FIELD(Class, Member), (float)(Min), (float)(Max))

// Help text shown when the mouse is over the field.
#define ZM_FIELD_TOOLTIP(Class, Member, Text) ZM_FIELD(Class, Member).Tooltip(Text)

// enum (or int) shown as a dropdown:  ZM_FIELD_ENUM(Enemy, State, "Idle", "Patrol", {"Boss", 10})
#define ZM_FIELD_ENUM(Class, Member, ...) \
    ::Zamin::ScriptFieldAttributes::Enum<ZM_FIELD_TYPE(Class, Member)>(ZM_FIELD(Class, Member), { __VA_ARGS__ })

#define ZM_REGISTER_SCRIPT(Class, ...) \
    static ::Zamin::ScriptRegistrar s_ZaminScriptRegistrar_##Class(#Class, \
        []() -> ::Zamin::ScriptableEntity* { return new Class(); }, { __VA_ARGS__ });
