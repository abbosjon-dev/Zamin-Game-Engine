#include "Zamin/Scripting/ScriptEngine.h"
#include "Zamin/Core/GameState.h"
#include "Zamin/Core/Services.h"
#include "Zamin/Scripting/ScriptableEntity.h"
#include "Zamin/Core/Log.h"
#include "Zamin/Utils/FileSystem.h"

#if defined(__linux__) || defined(__APPLE__)
    #include <dlfcn.h>
    #include <unistd.h>
#endif

namespace Zamin {

    static std::map<std::string, ScriptClassInfo>& Registry()
    {
        static std::map<std::string, ScriptClassInfo> registry;
        return registry;
    }

    void ScriptRegistry::Register(const ScriptClassInfo& info)
    {
        Registry()[info.Name] = info;
    }

    void ScriptRegistry::Unregister(const std::string& name) { Registry().erase(name); }

    const ScriptClassInfo* ScriptRegistry::Find(const std::string& name)
    {
        auto it = Registry().find(name);
        return it != Registry().end() ? &it->second : nullptr;
    }

    const std::map<std::string, ScriptClassInfo>& ScriptRegistry::GetAll() { return Registry(); }

    ScriptableEntity* ScriptRegistry::Instantiate(const std::string& name)
    {
        const ScriptClassInfo* info = Find(name);
        return info && info->Factory ? info->Factory() : nullptr;
    }

    // ------------------------------------------------------------------ modules
    namespace {
        struct ModuleData
        {
            void* Handle = nullptr;
            std::filesystem::path Path;
            std::filesystem::path LoadedCopy;
            std::filesystem::file_time_type WriteTime{};
            std::function<void()> BeforeUnload;
            uint32_t Generation = 0;
        };
        ModuleData s_Module;
    }

    void ScriptEngine::SetBeforeUnloadCallback(const std::function<void()>& callback) { s_Module.BeforeUnload = callback; }

    bool ScriptEngine::IsModuleLoaded() { return s_Module.Handle != nullptr; }
    const std::filesystem::path& ScriptEngine::GetModulePath() { return s_Module.Path; }
    std::filesystem::file_time_type ScriptEngine::GetLoadedModuleWriteTime() { return s_Module.WriteTime; }

    void ScriptEngine::UnloadModule()
    {
        if (!s_Module.Handle)
            return;
        if (s_Module.BeforeUnload)
            s_Module.BeforeUnload();
        // Services and state listeners the module's code made must go while that code is loaded.
        Services::Clear();
        GameState::ClearListeners();
#if defined(__linux__) || defined(__APPLE__)
        dlclose(s_Module.Handle);
#endif
        s_Module.Handle = nullptr;
        std::error_code ec;
        if (!s_Module.LoadedCopy.empty())
            std::filesystem::remove(s_Module.LoadedCopy, ec);
        s_Module.LoadedCopy.clear();
        ZM_CORE_INFO("Skript moduli yopildi");
    }

    bool ScriptEngine::LoadModule(const std::filesystem::path& path)
    {
        UnloadModule();

        std::error_code ec;
        if (!std::filesystem::exists(path, ec))
        {
            ZM_CORE_WARN("Skript moduli topilmadi: '{}' (avval skriptlarni kompilyatsiya qiling)", path.string());
            return false;
        }

#if defined(__linux__) || defined(__APPLE__)
        // Load a uniquely named copy so dlopen never returns a cached handle
        // and the build system can overwrite the original file.
        std::filesystem::path tmpDir = FileSystem::GetTempDirectory() / "zamin-modules";
        std::filesystem::create_directories(tmpDir, ec);
        std::filesystem::path copy = tmpDir / (path.stem().string() + "-" + std::to_string(getpid()) + "-" +
                                               std::to_string(++s_Module.Generation) + path.extension().string());
        std::filesystem::copy_file(path, copy, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec)
        {
            ZM_CORE_ERROR("Modulni nusxalab bo'lmadi: {}", ec.message());
            return false;
        }

        size_t before = ScriptRegistry::GetAll().size();
        void* handle = dlopen(copy.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!handle)
        {
            ZM_CORE_ERROR("Skript moduli yuklanmadi: {}", dlerror());
            std::filesystem::remove(copy, ec);
            return false;
        }
        s_Module.Handle = handle;
        s_Module.Path = path;
        s_Module.LoadedCopy = copy;
        s_Module.WriteTime = std::filesystem::last_write_time(path, ec);
        ZM_CORE_INFO("Skript moduli yuklandi: {} ({} ta skript klassi)", path.filename().string(),
                     ScriptRegistry::GetAll().size() - before);
        return true;
#else
        ZM_CORE_ERROR("Bu platformada skript modullari hali qo'llab-quvvatlanmaydi");
        return false;
#endif
    }

}
