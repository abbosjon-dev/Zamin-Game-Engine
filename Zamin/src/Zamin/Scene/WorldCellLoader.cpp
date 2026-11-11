#include "Zamin/Scene/WorldCellLoader.h"
#include "Zamin/Scene/SceneSerializer.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <limits>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace Zamin {
    namespace {
        struct FD
        {
            int Value = -1;
            explicit FD(int value = -1) : Value(value) {}
            ~FD()
            {
                if (Value >= 0) close(Value);
            }
            FD(const FD&) = delete;
            FD& operator=(const FD&) = delete;
        };

        // SAX preflight bounds DOM work without constructing a second JSON tree. Keys and values
        // both count; duplicate keys and excessive nesting are rejected before SceneLoadJob parse.
        struct SceneSAX : nlohmann::json_sax<nlohmann::json>
        {
            struct Frame
            {
                bool Object, Entities;
                std::set<std::string> Keys;
                std::string Key;
            };
            const WorldCellLoader::Limits& Limits;
            const std::atomic<bool>& Cancel;
            std::vector<Frame> Stack;
            size_t Nodes = 0, Entities = 0;
            bool SceneSeen = false, VersionSeen = false, EntitiesSeen = false;
            std::string Error;
            SceneSAX(const WorldCellLoader::Limits& limits, const std::atomic<bool>& cancel) : Limits(limits), Cancel(cancel) {}
            bool Fail(const char* message)
            {
                Error = message;
                return false;
            }
            bool Count()
            {
                if (Cancel.load(std::memory_order_relaxed)) return Fail("Cell load cancelled");
                return ++Nodes <= Limits.MaxJSONNodes || Fail("Scene JSON node limit exceeded");
            }
            bool Value(char type, int64_t version = -1)
            {
                if (!Count()) return false;
                if (Stack.empty()) return type == 'o' || Fail("Scene root must be an object");
                if (Stack.back().Entities && ++Entities > Limits.MaxEntities) return Fail("Scene entity limit exceeded");
                if (Stack.size() == 1 && Stack.back().Object)
                {
                    const auto& key = Stack.back().Key;
                    if (key == "Scene")
                    {
                        SceneSeen = true;
                        return type == 's' || Fail("Scene name must be a string");
                    }
                    if (key == "Entities")
                    {
                        EntitiesSeen = true;
                        return type == 'a' || Fail("Entities must be an array");
                    }
                    if (key == "Version")
                    {
                        VersionSeen = true;
                        return type == 'i' && version >= 0 && version <= SceneMigration::CurrentVersion ? true : Fail("Unsupported scene version");
                    }
                }
                return true;
            }
            bool Push(bool object)
            {
                const bool entities = Stack.size() == 1 && Stack.back().Object && Stack.back().Key == "Entities";
                if (!Value(object ? 'o' : 'a')) return false;
                if (Stack.size() >= 64) return Fail("Scene nesting limit exceeded");
                Stack.push_back({ object, entities, {}, {} });
                return true;
            }
            bool null() override { return Value('n'); }
            bool boolean(bool) override { return Value('b'); }
            bool number_integer(number_integer_t v) override { return Value('i', v); }
            bool number_unsigned(number_unsigned_t v) override { return Value('i', v <= uint64_t(INT64_MAX) ? int64_t(v) : -1); }
            bool number_float(number_float_t, const string_t&) override { return Value('f'); }
            bool string(string_t&) override { return Value('s'); }
            bool binary(binary_t&) override { return Fail("Binary JSON is not supported"); }
            bool start_object(std::size_t) override { return Push(true); }
            bool start_array(std::size_t) override { return Push(false); }
            bool key(string_t& value) override
            {
                if (!Count()) return false;
                auto& frame = Stack.back();
                frame.Key = value;
                return frame.Keys.insert(value).second || Fail("Duplicate scene JSON key");
            }
            bool end_object() override
            {
                Stack.pop_back();
                return true;
            }
            bool end_array() override
            {
                Stack.pop_back();
                return true;
            }
            bool parse_error(std::size_t, const std::string&, const nlohmann::detail::exception&) override { return Fail("Invalid scene JSON"); }
        };
    }

    struct WorldCellLoader::Data
    {
        struct Task
        {
            UUID Cell{ 0 };
            uint64_t Generation = 0;
            std::string Path, Error;
            size_t SourceBytes = 0;
            std::atomic<bool> Cancelled{ false }, Started{ false }, Done{ false };
            std::unique_ptr<SceneLoadJob> Job;
        };
        struct Entry
        {
            WorldCellLoadStatus Status;
            bool Wanted = false;
            size_t CellIndex = 0;
            std::shared_ptr<Task> Pending;
            std::unique_ptr<SceneLoadJob> Ready;
        };
        FD Root;
        Limits Budget;
        WorldManifest Manifest;
        std::vector<UUID> Order;
        std::unordered_map<uint64_t, Entry> Entries;
        std::thread::id Owner = std::this_thread::get_id();
        uint64_t NextGeneration = 0, Stale = 0;
        size_t Reserved = 0;
        std::vector<std::shared_ptr<Task>> Tasks;
        std::atomic<uint64_t> WorkerJobs{ 0 };
        std::mutex Mutex;
        std::condition_variable_any Wake;
        std::deque<std::shared_ptr<Task>> Queue;
        // Last: joined before the fields the workers access are destroyed, including Root.
        std::vector<std::jthread> Workers;

        ~Data()
        {
            for (auto& task : Tasks) task->Cancelled = true;
            for (auto& worker : Workers) worker.request_stop();
            Wake.notify_all();
        }
        void CheckOwner() const
        {
            if (Owner != std::this_thread::get_id()) throw std::logic_error("WorldCellLoader called from a different owner thread");
        }
        void Read(Task& task)
        {
            // Open each path segment relative to the pinned root, rejecting every symlink. O_NONBLOCK
            // prevents a swapped FIFO from blocking open; fstat rejects all non-regular files.
            FD directory(fcntl(Root.Value, F_DUPFD_CLOEXEC, 0));
            if (directory.Value < 0) throw std::runtime_error("Cannot duplicate world asset directory");
            std::vector<std::string> parts;
            for (const auto& part : std::filesystem::path(task.Path)) parts.push_back(part.string());
            for (size_t i = 0; i + 1 < parts.size(); ++i)
            {
                int next = openat(directory.Value, parts[i].c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
                if (next < 0) throw std::runtime_error("Cannot open cell directory (missing or symlink)");
                close(directory.Value);
                directory.Value = next;
            }
            FD file(openat(directory.Value, parts.back().c_str(), O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC));
            struct stat before
            {
            }, after{};
            if (file.Value < 0 || fstat(file.Value, &before) || !S_ISREG(before.st_mode)) throw std::runtime_error("Cell scene is not a readable regular file");
            if (before.st_size < 0 || uint64_t(before.st_size) > Budget.MaxCellBytes) throw std::runtime_error("Scene byte limit exceeded");
            task.SourceBytes = size_t(before.st_size);
            std::string text(task.SourceBytes, '\0');
            size_t offset = 0;
            while (offset < text.size())
            {
                if (task.Cancelled) return;
                const auto count = read(file.Value, text.data() + offset, std::min(size_t(65536), text.size() - offset));
                if (count < 0 && errno == EINTR) continue;
                if (count <= 0) throw std::runtime_error("Cell scene changed or could not be read");
                offset += size_t(count);
            }
            char extra;
            if (read(file.Value, &extra, 1) != 0 || fstat(file.Value, &after) || before.st_size != after.st_size ||
                before.st_mtim.tv_sec != after.st_mtim.tv_sec || before.st_mtim.tv_nsec != after.st_mtim.tv_nsec ||
                before.st_ctim.tv_sec != after.st_ctim.tv_sec || before.st_ctim.tv_nsec != after.st_ctim.tv_nsec)
                throw std::runtime_error("Cell scene changed while reading");
            SceneSAX sax(Budget, task.Cancelled);
            if (!nlohmann::json::sax_parse(text, &sax)) throw std::runtime_error(sax.Error);
            if (!sax.SceneSeen || !sax.VersionSeen || !sax.EntitiesSeen) throw std::runtime_error("Missing scene fields");
            if (task.Cancelled) return;
            auto job = std::make_unique<SceneLoadJob>();
            if (!job->ParseString(text)) throw std::runtime_error(job->GetError());
            if (!task.Cancelled) task.Job = std::move(job);
        }
        void Worker(std::stop_token stop)
        {
            while (!stop.stop_requested())
            {
                std::shared_ptr<Task> task;
                {
                    std::unique_lock lock(Mutex);
                    if (!Wake.wait(lock, stop, [this] { return !Queue.empty(); })) return;
                    task = std::move(Queue.front());
                    Queue.pop_front();
                }
                if (!task->Cancelled)
                {
                    task->Started = true;
                    ++WorkerJobs;
                    try
                    {
                        Read(*task);
                    }
                    catch (const std::exception& e)
                    {
                        task->Error = e.what();
                    }
                    catch (...)
                    {
                        task->Error = "Unknown cell IO/parse failure";
                    }
                }
                task->Done.store(true, std::memory_order_release);
            }
        }
        void Pump()
        {
            for (auto id : Order)
            {
                auto& entry = Entries.at(id);
                if (!entry.Wanted || entry.Status.Stage != WorldCellStage::Queued || entry.Pending) continue;
                const auto* cell = &Manifest.Cells[entry.CellIndex];
                bool wait = false;
                for (auto dependency : cell->Dependencies)
                {
                    const auto stage = Entries.at(dependency).Status.Stage;
                    if (stage == WorldCellStage::Failed)
                    {
                        entry.Status.Stage = WorldCellStage::Failed;
                        entry.Status.Error = "Cell dependency failed";
                        break;
                    }
                    if (stage != WorldCellStage::Ready && stage != WorldCellStage::Transferred) wait = true;
                }
                if (wait || entry.Status.Stage == WorldCellStage::Failed || Reserved > Budget.MaxReservedBytes - Budget.MaxCellBytes) continue;
                auto task = std::make_shared<Task>();
                task->Cell = id;
                task->Generation = entry.Status.Generation;
                task->Path = cell->ScenePath;
                {
                    std::lock_guard lock(Mutex);
                    Queue.push_back(task); // Last potentially allocating step, before publishing owner state.
                    Tasks.push_back(task); // Capacity reserved at Create, bounded by the admission quota.
                    entry.Pending = task;
                    Reserved += Budget.MaxCellBytes;
                }
                Wake.notify_one();
            }
        }
    };

    WorldCellLoader::WorldCellLoader(std::unique_ptr<Data> data) : m_Data(std::move(data)) {}
    WorldCellLoader::~WorldCellLoader() = default;

    std::unique_ptr<WorldCellLoader> WorldCellLoader::Create(const WorldManifest& manifest,
                                                             const std::filesystem::path& assets, const Limits& limits, std::string& error)
    {
        error.clear();
        if (!manifest.Validate(error)) return nullptr;
        if (!limits.Workers || limits.Workers > 4 || !limits.MaxWantedCells || limits.MaxWantedCells > WorldManifest::MaxCells ||
            !limits.MaxCellBytes || limits.MaxCellBytes > 64 * 1024 * 1024 || limits.MaxReservedBytes < limits.MaxCellBytes ||
            limits.MaxReservedBytes > 512 * 1024 * 1024 || limits.MaxReservedBytes / limits.MaxCellBytes > 64 ||
            !limits.MaxJSONNodes || limits.MaxJSONNodes > 1000000 || !limits.MaxEntities || limits.MaxEntities > 100000)
        {
            error = "Invalid world cell loader limits";
            return nullptr;
        }
        try
        {
            auto data = std::make_unique<Data>();
            if (assets.empty()) throw std::runtime_error("Empty world asset directory");
            data->Root.Value = open(assets.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
            if (data->Root.Value < 0) throw std::runtime_error("Cannot open world asset directory");
            data->Tasks.reserve(limits.MaxReservedBytes / limits.MaxCellBytes);
            data->Budget = limits;
            data->Manifest = manifest;
            if (!manifest.DependencyOrder(data->Order, error)) return nullptr;
            for (size_t i = 0; i < manifest.Cells.size(); ++i)
            {
                auto& entry = data->Entries[manifest.Cells[i].ID];
                entry.Status.Cell = manifest.Cells[i].ID;
                entry.CellIndex = i;
            }
            for (size_t i = 0; i < limits.Workers; ++i) data->Workers.emplace_back([ptr = data.get()](std::stop_token stop) { ptr->Worker(stop); });
            return std::unique_ptr<WorldCellLoader>(new WorldCellLoader(std::move(data)));
        }
        catch (const std::exception& e)
        {
            error = e.what();
            return nullptr;
        }
    }

    bool WorldCellLoader::SetWanted(const std::vector<UUID>& cells, std::string& error)
    {
        auto& d = *m_Data;
        d.CheckOwner();
        error.clear();
        if (cells.size() > WorldManifest::MaxCells)
        {
            error = "World cell request limit exceeded";
            return false;
        }
        std::unordered_set<uint64_t> wanted;
        std::vector<UUID> pending = cells;
        while (!pending.empty())
        {
            auto id = pending.back();
            pending.pop_back();
            if (!d.Entries.contains(id))
            {
                error = "Unknown world cell UUID";
                return false;
            }
            if (!wanted.insert(id).second) continue;
            if (wanted.size() > d.Budget.MaxWantedCells)
            {
                error = "World dependency closure exceeds wanted cell limit";
                return false;
            }
            const auto* cell = &d.Manifest.Cells[d.Entries.at(id).CellIndex];
            pending.insert(pending.end(), cell->Dependencies.begin(), cell->Dependencies.end());
        }
        size_t added = 0;
        for (auto id : wanted)
            if (!d.Entries.at(id).Wanted) ++added;
        if (added > std::numeric_limits<uint64_t>::max() - d.NextGeneration)
        {
            error = "World cell generation exhausted";
            return false;
        }
        for (auto id : d.Order)
        {
            auto& e = d.Entries.at(id);
            const bool next = wanted.contains(id);
            if (e.Wanted == next) continue;
            if (e.Pending)
            {
                e.Pending->Cancelled = true;
                e.Pending.reset();
            }
            if (e.Ready)
            {
                e.Ready.reset();
                d.Reserved -= d.Budget.MaxCellBytes;
            }
            e.Wanted = next;
            e.Status.Error.clear();
            e.Status.SourceBytes = 0;
            e.Status.Stage = next ? WorldCellStage::Queued : WorldCellStage::Unloaded;
            if (next) e.Status.Generation = ++d.NextGeneration;
        }
        d.Pump();
        return true;
    }

    bool WorldCellLoader::Invalidate(UUID cell, std::string& error)
    {
        auto& d = *m_Data;
        d.CheckOwner();
        error.clear();
        auto it = d.Entries.find(cell);
        if (it == d.Entries.end() || !it->second.Wanted)
        {
            error = "Cell is not wanted";
            return false;
        }
        if (d.NextGeneration == std::numeric_limits<uint64_t>::max())
        {
            error = "World cell generation exhausted";
            return false;
        }
        auto& e = it->second;
        if (e.Pending)
        {
            e.Pending->Cancelled = true;
            e.Pending.reset();
        }
        if (e.Ready)
        {
            e.Ready.reset();
            d.Reserved -= d.Budget.MaxCellBytes;
        }
        e.Status = { cell, ++d.NextGeneration, WorldCellStage::Queued, 0, {} };
        d.Pump();
        return true;
    }

    size_t WorldCellLoader::Poll(size_t maxCompletions)
    {
        auto& d = *m_Data;
        d.CheckOwner();
        if (!maxCompletions) return 0;
        size_t completed = 0;
        for (size_t i = 0; i < d.Tasks.size() && completed < maxCompletions;)
        {
            auto task = d.Tasks[i];
            if (!task->Done.load(std::memory_order_acquire))
            {
                ++i;
                continue;
            }
            auto& e = d.Entries.at(task->Cell);
            if (e.Wanted && e.Pending == task && e.Status.Generation == task->Generation && !task->Cancelled)
            {
                e.Pending.reset();
                e.Status.SourceBytes = task->SourceBytes;
                if (task->Job)
                {
                    e.Ready = std::move(task->Job);
                    e.Status.Stage = WorldCellStage::Ready;
                }
                else
                {
                    e.Status.Stage = WorldCellStage::Failed;
                    e.Status.Error = task->Error.empty() ? "Cell load failed" : task->Error;
                    d.Reserved -= d.Budget.MaxCellBytes;
                }
            }
            else
            {
                ++d.Stale;
                d.Reserved -= d.Budget.MaxCellBytes;
            }
            d.Tasks.erase(d.Tasks.begin() + std::ptrdiff_t(i));
            ++completed;
        }
        d.Pump();
        return completed;
    }

    WorldCellLoadStatus WorldCellLoader::GetStatus(UUID cell) const
    {
        auto& d = *m_Data;
        d.CheckOwner();
        const auto found = d.Entries.find(cell);
        if (found == d.Entries.end()) return { cell, 0, WorldCellStage::Unloaded, 0, "Unknown world cell UUID" };
        auto result = found->second.Status;
        if (found->second.Pending && found->second.Pending->Started) result.Stage = WorldCellStage::Reading;
        return result;
    }
    WorldCellLoader::Stats WorldCellLoader::GetStats() const
    {
        auto& d = *m_Data;
        d.CheckOwner();
        Stats stats;
        stats.InFlight = d.Tasks.size();
        stats.ReservedSourceBytes = d.Reserved;
        stats.WorkerJobs = d.WorkerJobs.load();
        stats.StaleCompletions = d.Stale;
        for (const auto& [id, e] : d.Entries)
        {
            if (!e.Wanted) continue;
            ++stats.Wanted;
            switch (GetStatus(UUID(id)).Stage)
            {
                case WorldCellStage::Queued:      ++stats.Queued; break;
                case WorldCellStage::Reading:     ++stats.Reading; break;
                case WorldCellStage::Ready:       ++stats.Ready; break;
                case WorldCellStage::Transferred: ++stats.Transferred; break;
                case WorldCellStage::Failed:      ++stats.Failed; break;
                default:                          break;
            }
        }
        return stats;
    }
    std::unique_ptr<SceneLoadJob> WorldCellLoader::TakeReady(UUID cell, uint64_t generation)
    {
        auto& d = *m_Data;
        d.CheckOwner();
        auto found = d.Entries.find(cell);
        if (found == d.Entries.end()) return nullptr;
        auto& e = found->second;
        if (!e.Wanted || e.Status.Generation != generation || e.Status.Stage != WorldCellStage::Ready) return nullptr;
        auto result = std::move(e.Ready);
        e.Status.Stage = WorldCellStage::Transferred;
        d.Reserved -= d.Budget.MaxCellBytes;
        return result;
    }
}
