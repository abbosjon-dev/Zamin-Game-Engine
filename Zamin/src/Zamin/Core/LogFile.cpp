#include "Zamin/Core/LogFile.h"

#include "Zamin/Core/Log.h"
#include "Zamin/Core/SaveData.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

namespace Zamin {

    namespace fs = std::filesystem;

    namespace {

        struct State
        {
            std::mutex Mutex;
            fs::path Path, LatestPath;
            bool AtExitRegistered = false;
        };

        State& S()
        {
            static State s;
            return s;
        }

        std::FILE* OpenForWriting(const fs::path& path, std::string* error)
        {
            std::error_code ec;
            if (path.has_parent_path())
                fs::create_directories(path.parent_path(), ec);
            std::FILE* file = std::fopen(path.c_str(), "w");
            if (!file && error)
                *error = std::format("{}: {}", path.string(), std::strerror(errno));
            return file;
        }

        void CloseAtExit()
        {
            LogFile::Close();
        }

        // Attaches the files and remembers their paths (the previous ones are closed first).
        void Attach(std::FILE* primary, std::FILE* secondary, const fs::path& path, const fs::path& latest, const std::string& header)
        {
            LogFile::Close();
            State& s = S();
            {
                std::lock_guard lock(s.Mutex);
                s.Path = path;
                s.LatestPath = latest;
                if (!s.AtExitRegistered)
                {
                    std::atexit(CloseAtExit);
                    s.AtExitRegistered = true;
                }
            }
            Log::AttachFiles(primary, secondary, header);
        }

    }

    fs::path LogFile::GetDefaultDirectory(std::string_view company, std::string_view product)
    {
        return SaveData::GetDirectoryFor(company, product) / "logs";
    }

    std::string LogFile::MakeFileName(std::string_view prefix, std::time_t time)
    {
        std::tm tm{};
        localtime_r(&time, &tm);
        char stamp[32];
        std::strftime(stamp, sizeof(stamp), "%Y-%m-%d_%H-%M-%S", &tm);
        return std::format("{}-{}.log", prefix, stamp);
    }

    std::vector<fs::path> LogFile::ListLogs(const fs::path& directory, std::string_view prefix)
    {
        std::vector<fs::path> logs;
        std::error_code ec;
        const std::string start = std::string(prefix) + "-";
        for (fs::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec))
        {
            std::error_code fec;
            if (!it->is_regular_file(fec))
                continue;
            const std::string name = it->path().filename().string();
            if (name.starts_with(start) && name.ends_with(".log") && name != LatestFileName)
                logs.push_back(it->path());
        }
        std::sort(logs.begin(), logs.end(), [](const fs::path& a, const fs::path& b) { return a.filename().string() < b.filename().string(); });
        return logs;
    }

    size_t LogFile::RemoveOldLogs(const fs::path& directory, std::string_view prefix, int keep)
    {
        std::vector<fs::path> logs = ListLogs(directory, prefix);
        const size_t keepCount = (size_t)std::max(keep, 1);
        size_t removed = 0;
        for (size_t i = 0; i + keepCount < logs.size(); i++)
        {
            std::error_code ec;
            if (fs::remove(logs[i], ec))
                removed++;
        }
        return removed;
    }

    bool LogFile::OpenRotating(const Options& options, std::string* error)
    {
        if (options.Directory.empty())
        {
            if (error)
                *error = "log papkasi ko'rsatilmagan";
            return false;
        }
        // This run's file (a second run in the same second gets "_2" ... after the name).
        const std::string base = MakeFileName(options.Prefix, std::time(nullptr));
        fs::path path = options.Directory / base;
        std::error_code ec;
        for (int n = 2; fs::exists(path, ec) && n < 100; n++)
            path = options.Directory / std::format("{}_{}.log", base.substr(0, base.size() - 4), n);
        std::FILE* primary = OpenForWriting(path, error);
        if (!primary)
            return false;
        // The oldest go (this run's file is the newest by its name, it is never one of them).
        std::vector<fs::path> logs = ListLogs(options.Directory, options.Prefix);
        logs.erase(std::remove(logs.begin(), logs.end(), path), logs.end());
        const size_t keepOthers = (size_t)std::max(options.Keep, 1) - 1;
        for (size_t i = 0; i + keepOthers < logs.size(); i++)
            fs::remove(logs[i], ec);

        fs::path latest;
        std::FILE* secondary = nullptr;
        if (options.Latest)
        {
            latest = options.Directory / LatestFileName;
            secondary = OpenForWriting(latest, nullptr);
            if (!secondary)
                latest.clear();
        }
        Attach(primary, secondary, path, latest, options.Header);
        return true;
    }

    bool LogFile::Open(const fs::path& file, const std::string& header, std::string* error)
    {
        std::FILE* primary = OpenForWriting(file, error);
        if (!primary)
            return false;
        std::error_code ec;
        const fs::path absolute = fs::absolute(file, ec);
        Attach(primary, nullptr, ec ? file : absolute, {}, header);
        return true;
    }

    void LogFile::Close()
    {
        std::FILE* primary = nullptr;
        std::FILE* secondary = nullptr;
        Log::DetachFiles(&primary, &secondary);
        if (primary)
            std::fclose(primary);
        if (secondary)
            std::fclose(secondary);
        State& s = S();
        std::lock_guard lock(s.Mutex);
        s.Path.clear();
        s.LatestPath.clear();
    }

    bool LogFile::IsOpen()
    {
        State& s = S();
        std::lock_guard lock(s.Mutex);
        return !s.Path.empty();
    }

    fs::path LogFile::GetPath()
    {
        State& s = S();
        std::lock_guard lock(s.Mutex);
        return s.Path;
    }

    fs::path LogFile::GetLatestPath()
    {
        State& s = S();
        std::lock_guard lock(s.Mutex);
        return s.LatestPath;
    }

    fs::path LogFile::GetDirectory()
    {
        State& s = S();
        std::lock_guard lock(s.Mutex);
        return s.Path.empty() ? fs::path() : s.Path.parent_path();
    }

}
