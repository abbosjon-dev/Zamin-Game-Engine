#include "Zamin/Scripting/ScriptEngine.h"
#include "Zamin/Core/GameState.h"
#include "Zamin/Core/Services.h"
#include "Zamin/Scripting/ScriptableEntity.h"
#include "Zamin/Core/Log.h"
#include "Zamin/Utils/FileSystem.h"

#include <cerrno>
#include <string>

#if defined(__linux__) || defined(__APPLE__)
    #include <dlfcn.h>
    #include <signal.h>
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

    int ScriptEngine::RemoveStaleModuleCopies(const std::filesystem::path& folder)
    {
        int removed = 0;
#if defined(__linux__) || defined(__APPLE__)
        std::error_code ec;
        for (auto it = std::filesystem::directory_iterator(folder, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec))
        {
            // <name>-<pid>-<generation><ext>: the pid is the second number from the end.
            const std::string stem = it->path().stem().string();
            const size_t last = stem.rfind('-');
            const size_t before = last == std::string::npos || last == 0 ? std::string::npos : stem.rfind('-', last - 1);
            if (before == std::string::npos)
                continue;
            const std::string pidText = stem.substr(before + 1, last - before - 1);
            if (pidText.empty() || pidText.find_first_not_of("0123456789") != std::string::npos || pidText.size() > 9)
                continue;
            const pid_t pid = (pid_t)std::stol(pidText);
            if (pid <= 0 || pid == getpid() || kill(pid, 0) == 0 || errno != ESRCH)
                continue; // running (or not ours to judge)
            std::error_code removeError;
            if (std::filesystem::remove(it->path(), removeError))
                removed++;
        }
#else
        (void)folder;
#endif
        return removed;
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
        // Copies of runs that crashed or were killed (UnloadModule deletes the others) are not kept for ever.
        if (const int stale = RemoveStaleModuleCopies(tmpDir); stale > 0)
            ZM_CORE_INFO("Eski skript moduli nusxalari o'chirildi: {} ta ({})", stale, tmpDir.string());
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
