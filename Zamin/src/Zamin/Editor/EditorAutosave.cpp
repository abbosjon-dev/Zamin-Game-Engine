#include "Zamin/Editor/EditorAutosave.h"
#include "Zamin/Editor/RecentFiles.h"
#include "Zamin/Utils/FileSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cerrno>
#include <fstream>

#if defined(__unix__) || defined(__APPLE__)
    #include <signal.h>
    #include <unistd.h>
#endif

namespace Zamin {

    namespace fs = std::filesystem;
    using json = nlohmann::json;

    void AutosaveSettings::Sanitize()
    {
        IntervalMinutes = std::clamp(IntervalMinutes, MinInterval, MaxInterval);
    }

    namespace EditorAutosave {

        fs::path GetDirectory(const fs::path& projectDirectory)
        {
            return projectDirectory / ".zamin" / "autosave";
        }

        fs::path GetFilePath(const fs::path& projectDirectory, std::string_view sceneAssetPath)
        {
            if (sceneAssetPath.empty())
                return GetDirectory(projectDirectory) / UnsavedSceneName;
            // Kept inside the folder: "../x" or an absolute path cannot point out of it.
            fs::path relative = fs::path(sceneAssetPath).lexically_normal().relative_path();
            fs::path clean;
            for (const fs::path& part : relative)
                if (part != ".." && part != ".")
                    clean /= part;
            if (clean.empty())
                return GetDirectory(projectDirectory) / UnsavedSceneName;
            return GetDirectory(projectDirectory) / clean;
        }

        fs::path GetKeptPath(const fs::path& autosaveFile)
        {
            fs::path kept = autosaveFile;
            kept += ".kept";
            return kept;
        }

        fs::path GetLockPath(const fs::path& projectDirectory)
        {
            return projectDirectory / ".zamin" / "editor.lock";
        }

        bool IsDue(const AutosaveSettings& settings, double now, double lastTime, bool hasNewChanges, bool busy)
        {
            if (!settings.Enabled || !hasNewChanges || busy)
                return false;
            return now - lastTime >= settings.GetIntervalSeconds();
        }

        bool WriteFileAtomic(const fs::path& path, std::string_view text)
        {
            std::error_code ec;
            fs::create_directories(path.parent_path(), ec);
            fs::path temp = path;
            temp += ".tmp";
            {
                std::ofstream out(temp, std::ios::binary | std::ios::trunc);
                if (!out)
                    return false;
                out.write(text.data(), (std::streamsize)text.size());
                if (!out)
                    return false;
            }
            fs::rename(temp, path, ec);
            if (ec)
            {
                fs::remove(temp, ec);
                return false;
            }
            return true;
        }

        RecoveryDecision DecideRecovery(const RecoveryCheck& check, bool kept)
        {
            if (!check.AutosaveExists)
                return RecoveryDecision::None;
            if (check.SameContent)
                return RecoveryDecision::Discard;
            if (kept)
                return RecoveryDecision::Offer;
            // Saved after the copy was made: what the copy had is in the file (or was thrown away).
            if (check.SceneExists && check.AutosaveTime <= check.SceneTime)
                return RecoveryDecision::Discard;
            return RecoveryDecision::Offer;
        }

        RecoveryCheck InspectRecovery(const fs::path& autosaveFile, const fs::path& sceneFile)
        {
            RecoveryCheck check;
            std::error_code ec;
            check.AutosaveExists = !autosaveFile.empty() && fs::is_regular_file(autosaveFile, ec);
            if (!check.AutosaveExists)
                return check;
            check.AutosaveTime = fs::last_write_time(autosaveFile, ec);
            check.SceneExists = !sceneFile.empty() && fs::is_regular_file(sceneFile, ec);
            if (check.SceneExists)
            {
                check.SceneTime = fs::last_write_time(sceneFile, ec);
                // Same size first: reading both only when they could be equal.
                if (fs::file_size(autosaveFile, ec) == fs::file_size(sceneFile, ec))
                    check.SameContent = FileSystem::ReadTextFile(autosaveFile) == FileSystem::ReadTextFile(sceneFile);
            }
            return check;
        }

        std::string LockToJson(const LockInfo& info)
        {
            return json{ { "ProcessId", info.ProcessId }, { "StartedAt", info.StartedAt }, { "Host", info.Host } }.dump(2);
        }

        bool LockFromJson(const std::string& text, LockInfo& out)
        {
            json j = json::parse(text, nullptr, false);
            if (j.is_discarded() || !j.is_object() || !j.contains("ProcessId") || !j["ProcessId"].is_number_integer())
                return false;
            LockInfo info;
            info.ProcessId = j["ProcessId"].get<int>();
            if (j.contains("StartedAt") && j["StartedAt"].is_number_integer())
                info.StartedAt = j["StartedAt"].get<int64_t>();
            if (j.contains("Host") && j["Host"].is_string())
                info.Host = j["Host"].get<std::string>();
            out = info;
            return true;
        }

        bool WriteLock(const fs::path& path, const LockInfo& info)
        {
            return WriteFileAtomic(path, LockToJson(info));
        }

        std::optional<LockInfo> ReadLock(const fs::path& path)
        {
            std::error_code ec;
            if (!fs::is_regular_file(path, ec))
                return std::nullopt;
            LockInfo info;
            if (!LockFromJson(FileSystem::ReadTextFile(path), info))
                return LockInfo{}; // there, but unreadable: a session that died while writing it
            return info;
        }

        void RemoveLock(const fs::path& path)
        {
            std::error_code ec;
            fs::remove(path, ec);
        }

        LockState CheckLock(const std::optional<LockInfo>& lock, int currentProcessId, const std::function<bool(int)>& isAlive)
        {
            if (!lock)
                return LockState::Free;
            if (lock->ProcessId == currentProcessId && currentProcessId != 0)
                return LockState::Own;
            if (lock->ProcessId > 0 && isAlive && isAlive(lock->ProcessId))
                return LockState::OtherInstance;
            return LockState::Crashed;
        }

        bool IsProcessAlive(int processId)
        {
            if (processId <= 0)
                return false;
#if defined(__unix__) || defined(__APPLE__)
            // Signal 0 checks the process without touching it (EPERM: alive, someone else's).
            return ::kill((pid_t)processId, 0) == 0 || errno == EPERM;
#else
            return false;
#endif
        }

        int GetCurrentProcessId()
        {
#if defined(__unix__) || defined(__APPLE__)
            return (int)::getpid();
#else
            return 0;
#endif
        }

        LockInfo MakeLock()
        {
            LockInfo info;
            info.ProcessId = GetCurrentProcessId();
            info.StartedAt = GetUnixTime();
#if defined(__unix__) || defined(__APPLE__)
            char host[256] = {};
            if (::gethostname(host, sizeof(host) - 1) == 0)
                info.Host = host;
#endif
            return info;
        }
    }

}
