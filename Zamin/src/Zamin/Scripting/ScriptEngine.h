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
        return { name, ScriptFieldTypeOf<T>(), accessor };
    }

}

#define ZM_FIELD(Class, Member) \
    ::Zamin::MakeScriptField<Class, std::remove_cvref_t<decltype(std::declval<Class&>().Member)>>(#Member, \
        [](::Zamin::ScriptableEntity* s) -> void* { return &static_cast<Class*>(s)->Member; })

#define ZM_REGISTER_SCRIPT(Class, ...) \
    static ::Zamin::ScriptRegistrar s_ZaminScriptRegistrar_##Class(#Class, \
        []() -> ::Zamin::ScriptableEntity* { return new Class(); }, { __VA_ARGS__ });
