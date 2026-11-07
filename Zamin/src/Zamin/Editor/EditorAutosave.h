#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace Zamin {

    // How often the editor writes a safety copy of the open scene while it has unsaved changes
    // (Tahrirlash > Sozlamalar > Avtosaqlash). The copy never replaces the scene's own file.
    struct AutosaveSettings
    {
        bool Enabled = true;
        int IntervalMinutes = 5;

        static constexpr int MinInterval = 1, MaxInterval = 60;

        double GetIntervalSeconds() const { return IntervalMinutes * 60.0; }
        void Sanitize();
    };

    // Where the safety copies go, when one is due, and whether one found on opening a scene is
    // offered back (Recovery). A lock file tells a crashed editor session from a closed one.
    namespace EditorAutosave {

        // <project>/.zamin/autosave
        std::filesystem::path GetDirectory(const std::filesystem::path& projectDirectory);
        // The copy of a scene: its path under Assets kept (Scenes/Level1.zscene ->
        // .zamin/autosave/Scenes/Level1.zscene); a scene never saved is UnsavedSceneName.
        std::filesystem::path GetFilePath(const std::filesystem::path& projectDirectory, std::string_view sceneAssetPath);
        inline constexpr const char* UnsavedSceneName = "Nomsiz.zscene";
        // A copy put aside when its recovery was postponed ("Keyinroq"): next to it with ".kept"
        // added, where this session's copies of the scene (and saving it) do not reach it.
        std::filesystem::path GetKeptPath(const std::filesystem::path& autosaveFile);
        // <project>/.zamin/editor.lock: written while the editor has the project open.
        std::filesystem::path GetLockPath(const std::filesystem::path& projectDirectory);

        // Whether a copy is due now: enabled, there are changes the last copy does not have, the
        // interval has passed since the last copy (or since the scene was opened / saved), and the
        // editor is not in the middle of something (a drag, a field being typed into, play mode).
        bool IsDue(const AutosaveSettings& settings, double now, double lastTime, bool hasNewChanges, bool busy);

        // Written to a temporary file and renamed over the old copy: a crash while writing never
        // leaves half a scene behind.
        bool WriteFileAtomic(const std::filesystem::path& path, std::string_view text);

        // ---- recovery
        enum class RecoveryDecision
        {
            None,     // no copy
            Offer,    // the copy is newer than the scene's file (or the scene has no file): ask
            Discard,  // the copy is stale (the scene was saved after it, or it is the same): delete it
        };
        struct RecoveryCheck
        {
            bool AutosaveExists = false;
            std::filesystem::file_time_type AutosaveTime{};
            bool SceneExists = false;
            std::filesystem::file_time_type SceneTime{};
            bool SameContent = false;
        };
        // A kept copy (`kept`) is offered whatever the file's time - the user asked to be asked
        // again - unless it holds just what the file does.
        RecoveryDecision DecideRecovery(const RecoveryCheck& check, bool kept = false);
        // The facts about a copy and its scene's file (`sceneFile` empty: a scene never saved).
        RecoveryCheck InspectRecovery(const std::filesystem::path& autosaveFile, const std::filesystem::path& sceneFile);

        // ---- the lock file
        struct LockInfo
        {
            int ProcessId = 0;
            int64_t StartedAt = 0;   // seconds since the Unix epoch
            std::string Host;
        };
        std::string LockToJson(const LockInfo& info);
        bool LockFromJson(const std::string& text, LockInfo& out);
        bool WriteLock(const std::filesystem::path& path, const LockInfo& info);
        std::optional<LockInfo> ReadLock(const std::filesystem::path& path);
        void RemoveLock(const std::filesystem::path& path);

        enum class LockState
        {
            Free,          // no lock: the last session closed the project normally
            Crashed,       // a lock of a process that is gone: that session ended without closing
            OtherInstance, // a lock of another running editor: the project is open twice
            Own,           // this process's lock
        };
        LockState CheckLock(const std::optional<LockInfo>& lock, int currentProcessId, const std::function<bool(int)>& isAlive);
        bool IsProcessAlive(int processId);
        int GetCurrentProcessId();
        LockInfo MakeLock();   // this process, now
    }

}
