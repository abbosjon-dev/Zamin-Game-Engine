#include "Zamin/Asset/FileWatcher.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <set>
#include <thread>

#if defined(__linux__)
    #include <poll.h>
    #include <sys/inotify.h>
    #include <unistd.h>
    #define ZM_HAS_INOTIFY 1
#else
    #define ZM_HAS_INOTIFY 0
#endif

namespace Zamin {

    namespace fs = std::filesystem;

    static double Now()
    {
        return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    // ------------------------------------------------------------------ queue
    void FileChangeQueue::Push(const fs::path& path, FileChangeType type, bool isDirectory, double time)
    {
        std::string key = path.string();
        auto it = m_Pending.find(key);
        if (it == m_Pending.end())
        {
            m_Pending.emplace(std::move(key), Pending{ FileChange{ path, type, isDirectory }, time, m_NextOrder++ });
            return;
        }
        Pending& pending = it->second;
        FileChangeType before = pending.Change.Type;
        if (before == FileChangeType::Added && type == FileChangeType::Removed)
        {
            m_Pending.erase(it); // came and went before anyone saw it
            return;
        }
        FileChangeType merged;
        if (before == FileChangeType::Added)
            merged = FileChangeType::Added;          // still new, written some more
        else if (type == FileChangeType::Removed)
            merged = FileChangeType::Removed;
        else
            merged = FileChangeType::Modified;       // changed, or removed and put back (a safe save)
        pending.Change.Type = merged;
        pending.Change.IsDirectory = isDirectory;
        pending.Last = time;
    }

    std::vector<FileChange> FileChangeQueue::Pop(double now, double debounce)
    {
        std::vector<std::pair<uint64_t, FileChange>> ready;
        for (auto it = m_Pending.begin(); it != m_Pending.end();)
        {
            if (now - it->second.Last >= debounce)
            {
                ready.emplace_back(it->second.Order, std::move(it->second.Change));
                it = m_Pending.erase(it);
            }
            else
                ++it;
        }
        std::sort(ready.begin(), ready.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        std::vector<FileChange> changes;
        changes.reserve(ready.size());
        for (auto& [order, change] : ready)
            changes.push_back(std::move(change));
        return changes;
    }

    // ------------------------------------------------------------------ watcher state
    struct WatchedEntry
    {
        bool IsDirectory = false;
        bool IsLink = false;   // a link to a folder: listed, but not entered (no loops)
        uintmax_t Size = 0;
        fs::file_time_type Time{};
    };

    static bool IsHiddenName(const fs::path& path)
    {
        std::string name = path.filename().string();
        return name.empty() || name.front() == '.';
    }

    // Component-wise: `path` is strictly below `directory`.
    static bool IsBelow(const fs::path& path, const fs::path& directory)
    {
        auto [d, p] = std::mismatch(directory.begin(), directory.end(), path.begin(), path.end());
        return d == directory.end() && p != path.end();
    }

    struct FileWatcher::State
    {
        fs::path Root;
        FileWatcherOptions Options;
        std::thread Thread;
        std::atomic<bool> Running{ false };
        std::atomic<bool> Native{ false };
        std::atomic<bool> WatchFailed{ false };
        std::mutex Mutex;
        FileChangeQueue Queue; // guarded by Mutex

        // ---- watcher thread only (filled by Start before the thread runs)
        std::map<fs::path, WatchedEntry> Snapshot;   // ordered: a folder's contents follow it
        std::vector<FileChange> Changes;             // found by the current pass
        std::set<fs::path> Written;                  // files closed after writing (inotify)
#if ZM_HAS_INOTIFY
        int Fd = -1;
        std::map<int, fs::path> WatchDirs;
        std::map<fs::path, int> DirWatches;
#endif

        ~State()
        {
#if ZM_HAS_INOTIFY
            if (Fd >= 0)
                close(Fd);
#endif
        }

        void List(const fs::path& directory, bool recursive, std::map<fs::path, WatchedEntry>& out) const
        {
            std::error_code ec;
            for (fs::directory_iterator it(directory, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec))
            {
                if (IsHiddenName(it->path()))
                    continue;
                std::error_code fec;
                WatchedEntry entry;
                entry.IsDirectory = it->is_directory(fec);
                entry.IsLink = entry.IsDirectory && it->is_symlink(fec);
                if (!entry.IsDirectory)
                {
                    uintmax_t size = it->file_size(fec);
                    entry.Size = fec ? 0 : size;
                }
                auto time = it->last_write_time(fec);
                if (!fec)
                    entry.Time = time;
                out[it->path()] = entry;
                if (entry.IsDirectory && !entry.IsLink && recursive)
                    List(it->path(), true, out);
            }
        }

        void AddWatch(const fs::path& directory)
        {
#if ZM_HAS_INOTIFY
            if (Fd < 0)
                return;
            constexpr uint32_t mask = IN_CREATE | IN_DELETE | IN_MODIFY | IN_CLOSE_WRITE | IN_MOVED_FROM | IN_MOVED_TO |
                                      IN_ATTRIB | IN_DELETE_SELF | IN_MOVE_SELF | IN_ONLYDIR | IN_EXCL_UNLINK;
            int wd = inotify_add_watch(Fd, directory.c_str(), mask);
            if (wd < 0)
            {
                WatchFailed = true; // too many folders (fs.inotify.max_user_watches): scans catch the rest
                return;
            }
            WatchDirs[wd] = directory;
            DirWatches[directory] = wd;
#else
            (void)directory;
#endif
        }

        void RemoveWatch(const fs::path& directory)
        {
#if ZM_HAS_INOTIFY
            auto it = DirWatches.find(directory);
            if (it == DirWatches.end())
                return;
            int wd = it->second;
            DirWatches.erase(it);
            // A folder moved elsewhere in the tree keeps its watch under the new name.
            if (auto w = WatchDirs.find(wd); w != WatchDirs.end() && w->second == directory)
            {
                WatchDirs.erase(w);
                inotify_rm_watch(Fd, wd);
            }
#else
            (void)directory;
#endif
        }

        void AddTree(const fs::path& path, const WatchedEntry& entry)
        {
            Snapshot[path] = entry;
            Changes.push_back({ path, FileChangeType::Added, entry.IsDirectory });
            if (!entry.IsDirectory || entry.IsLink)
                return;
            AddWatch(path); // before listing: what appears meanwhile is not missed
            std::map<fs::path, WatchedEntry> below;
            List(path, true, below);
            for (const auto& [p, e] : below)
            {
                if (!Snapshot.emplace(p, e).second)
                    continue;
                Changes.push_back({ p, FileChangeType::Added, e.IsDirectory });
                if (e.IsDirectory && !e.IsLink)
                    AddWatch(p);
            }
        }

        void RemoveTree(const fs::path& path)
        {
            auto first = Snapshot.find(path);
            if (first == Snapshot.end())
                return;
            auto last = std::next(first);
            while (last != Snapshot.end() && IsBelow(last->first, path))
                ++last;
            // Deepest first, the folder itself last.
            for (auto it = std::make_reverse_iterator(last); it != std::make_reverse_iterator(first); ++it)
            {
                Changes.push_back({ it->first, FileChangeType::Removed, it->second.IsDirectory });
                if (it->second.IsDirectory)
                    RemoveWatch(it->first);
                Written.erase(it->first);
            }
            Snapshot.erase(first, last);
        }

        // Compares one folder's direct contents with the snapshot; new sub folders are read whole.
        void RescanDirectory(const fs::path& directory)
        {
            if (directory != Root)
            {
                auto self = Snapshot.find(directory);
                if (self == Snapshot.end() || !self->second.IsDirectory || self->second.IsLink)
                    return;
            }
            std::error_code ec;
            if (!fs::is_directory(directory, ec))
                return; // gone: the parent's scan removes it

            std::map<fs::path, WatchedEntry> current;
            List(directory, false, current);

            std::vector<fs::path> known;
            for (auto it = Snapshot.upper_bound(directory); it != Snapshot.end() && IsBelow(it->first, directory); ++it)
                if (it->first.parent_path() == directory)
                    known.push_back(it->first);
            for (const fs::path& child : known)
            {
                auto old = Snapshot.find(child);
                if (old == Snapshot.end())
                    continue;
                auto now = current.find(child);
                if (now == current.end() || now->second.IsDirectory != old->second.IsDirectory || now->second.IsLink != old->second.IsLink)
                {
                    RemoveTree(child); // gone (or a file became a folder: added again below)
                    continue;
                }
                if (old->second.IsDirectory)
                    continue;
                bool written = Written.erase(child) > 0;
                if (written || now->second.Size != old->second.Size || now->second.Time != old->second.Time)
                {
                    old->second = now->second;
                    Changes.push_back({ child, FileChangeType::Modified, false });
                }
            }
            for (const auto& [path, entry] : current)
                if (!Snapshot.contains(path))
                    AddTree(path, entry);
        }

        void RescanAll()
        {
            std::vector<fs::path> directories{ Root };
            for (const auto& [path, entry] : Snapshot)
                if (entry.IsDirectory && !entry.IsLink)
                    directories.push_back(path);
            for (const fs::path& directory : directories)
            {
#if ZM_HAS_INOTIFY
                if (Native && !DirWatches.contains(directory) && Snapshot.contains(directory))
                    AddWatch(directory);
#endif
                RescanDirectory(directory);
            }
            Written.clear();
        }

#if ZM_HAS_INOTIFY
        void ReadEvents(std::set<fs::path>& dirty, bool& full)
        {
            alignas(inotify_event) char buffer[16 * 1024];
            while (true)
            {
                ssize_t n = read(Fd, buffer, sizeof(buffer));
                if (n <= 0)
                    break;
                for (char* p = buffer; p < buffer + n;)
                {
                    const auto* ev = reinterpret_cast<const inotify_event*>(p);
                    p += sizeof(inotify_event) + ev->len;
                    if (ev->mask & IN_Q_OVERFLOW)
                    {
                        full = true;
                        continue;
                    }
                    auto w = WatchDirs.find(ev->wd);
                    if (w == WatchDirs.end())
                        continue;
                    fs::path directory = w->second;
                    if (ev->mask & IN_IGNORED)
                    {
                        WatchDirs.erase(w);
                        if (auto d = DirWatches.find(directory); d != DirWatches.end() && d->second == ev->wd)
                            DirWatches.erase(d);
                    }
                    if (ev->mask & (IN_IGNORED | IN_DELETE_SELF | IN_MOVE_SELF))
                    {
                        dirty.insert(directory == Root ? Root : directory.parent_path());
                        continue;
                    }
                    dirty.insert(directory);
                    if (ev->len > 0 && !(ev->mask & IN_ISDIR) && (ev->mask & IN_CLOSE_WRITE))
                        Written.insert(directory / fs::path(ev->name));
                }
            }
        }
#endif

        void Flush()
        {
            if (Changes.empty())
                return;
            double now = Now();
            std::lock_guard lock(Mutex);
            for (const FileChange& change : Changes)
                Queue.Push(change.Path, change.Type, change.IsDirectory, now);
            Changes.clear();
        }

        void Run()
        {
            constexpr double NativeRescanInterval = 10.0; // a safety net for missed events
            double nextFull = 0.0;
            bool first = true;
            while (Running)
            {
                std::set<fs::path> dirty;
                bool full = false;
#if ZM_HAS_INOTIFY
                if (Native)
                {
                    pollfd pfd{ Fd, POLLIN, 0 };
                    if (::poll(&pfd, 1, 100) > 0 && (pfd.revents & POLLIN))
                    {
                        ReadEvents(dirty, full);
                        std::this_thread::sleep_for(std::chrono::milliseconds(20)); // let a burst arrive
                        ReadEvents(dirty, full);
                    }
                }
                else
#endif
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                }

                double now = Now();
                double interval = Native && !WatchFailed ? NativeRescanInterval : std::max(0.02, Options.PollInterval);
                if (first)
                {
                    nextFull = now + interval;
                    first = false;
                }
                if (now >= nextFull)
                {
                    full = true;
                    nextFull = now + interval;
                }
                if (full)
                    RescanAll();
                else
                    for (const fs::path& directory : dirty) // parents first (ordered)
                        RescanDirectory(directory);
                Flush();
            }
        }
    };

    // ------------------------------------------------------------------ watcher
    FileWatcher::FileWatcher() = default;

    FileWatcher::~FileWatcher()
    {
        Stop();
    }

    bool FileWatcher::Start(const fs::path& directory, const Options& options)
    {
        Stop();
        std::error_code ec;
        fs::path root = fs::absolute(directory, ec);
        if (ec)
            root = directory;
        root = root.lexically_normal();
        while (!root.has_filename() && root.has_relative_path())
            root = root.parent_path(); // "Assets/" -> "Assets"
        if (!fs::is_directory(root, ec))
            return false;

        auto state = std::make_unique<State>();
        state->Root = root;
        state->Options = options;
#if ZM_HAS_INOTIFY
        if (options.NativeEvents)
        {
            state->Fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
            state->Native = state->Fd >= 0;
        }
#endif
        // The starting point: watches first, so nothing created while listing is missed.
        state->AddWatch(root);
        state->List(root, true, state->Snapshot);
        for (const auto& [path, entry] : state->Snapshot)
            if (entry.IsDirectory && !entry.IsLink)
                state->AddWatch(path);

        state->Running = true;
        State* raw = state.get();
        state->Thread = std::thread([raw]() { raw->Run(); });
        m_State = std::move(state);
        m_Directory = root;
        return true;
    }

    void FileWatcher::Stop()
    {
        if (!m_State)
            return;
        m_State->Running = false;
        if (m_State->Thread.joinable())
            m_State->Thread.join();
        m_State.reset();
        m_Directory.clear();
    }

    bool FileWatcher::IsWatching() const
    {
        return m_State != nullptr;
    }

    bool FileWatcher::UsesNativeEvents() const
    {
        return m_State && m_State->Native && !m_State->WatchFailed;
    }

    const fs::path& FileWatcher::GetDirectory() const
    {
        return m_Directory;
    }

    std::vector<FileChange> FileWatcher::Poll()
    {
        if (!m_State)
            return {};
        std::lock_guard lock(m_State->Mutex);
        return m_State->Queue.Pop(Now(), m_State->Options.Debounce);
    }

}
