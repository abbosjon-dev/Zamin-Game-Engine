#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Zamin {

    enum class FileChangeType : uint8_t { Added = 0, Modified, Removed };

    struct FileChange
    {
        std::filesystem::path Path;   // the watched directory joined with the changed name
        FileChangeType Type = FileChangeType::Modified;
        bool IsDirectory = false;
    };

    // Collects raw changes and hands each path out once it has settled: a file written in several
    // steps, or saved through a temporary file (removed + added again), comes out as one change.
    // Not thread-safe on its own (FileWatcher guards it).
    class FileChangeQueue
    {
    public:
        // `time` in seconds (any steady clock).
        void Push(const std::filesystem::path& path, FileChangeType type, bool isDirectory, double time);
        // The paths with no new change for `debounce` seconds at `now`, in the order they first changed.
        std::vector<FileChange> Pop(double now, double debounce);
        size_t GetPendingCount() const { return m_Pending.size(); }
        void Clear() { m_Pending.clear(); }

    private:
        struct Pending
        {
            FileChange Change;
            double Last = 0.0;
            uint64_t Order = 0;
        };
        std::unordered_map<std::string, Pending> m_Pending;
        uint64_t m_NextOrder = 0;
    };

    struct FileWatcherOptions
    {
        double Debounce = 0.3;       // seconds without a new change before one is reported
        double PollInterval = 1.0;   // seconds between scans without native events
        bool NativeEvents = true;    // inotify (Linux)
    };

    // Watches a directory and everything below it (names starting with '.' are skipped) on a
    // background thread: inotify on Linux, otherwise (or when inotify is not available) a scan of
    // the file times and sizes every PollInterval. Changes are collected there and handed out on the
    // main thread by Poll(), once a frame.
    class FileWatcher
    {
    public:
        using Options = FileWatcherOptions;

        FileWatcher();
        ~FileWatcher();
        FileWatcher(const FileWatcher&) = delete;
        FileWatcher& operator=(const FileWatcher&) = delete;

        // Starts watching (a previous watch is stopped first). False when `directory` is not one.
        bool Start(const std::filesystem::path& directory, const Options& options = {});
        void Stop();
        bool IsWatching() const;
        // inotify is in use (false: scanning every PollInterval).
        bool UsesNativeEvents() const;
        const std::filesystem::path& GetDirectory() const;

        // The changes that settled since the last call, oldest first. Main thread, once a frame.
        std::vector<FileChange> Poll();

    private:
        struct State;
        std::unique_ptr<State> m_State;
        std::filesystem::path m_Directory;
    };

}
