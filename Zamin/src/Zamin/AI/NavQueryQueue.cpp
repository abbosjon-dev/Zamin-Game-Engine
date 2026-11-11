#include "Zamin/AI/NavQueryQueue.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>

namespace Zamin {
    bool NavQueryQueue::Limits::Validate(std::string& error) const
    {
        error.clear();
        if (!Workers || Workers > 4 || !Capacity || Capacity > 1024 || !MaxExpanded || MaxExpanded > 1048576 ||
            !MaxPoints || MaxPoints > 1048576 || SnapshotBytes < 1024 || SnapshotBytes > (uint64_t(1) << 32) ||
            ResultBytes < sizeof(NavRoutePoint) || ResultBytes > (uint64_t(1) << 30))
        {
            error = "Invalid navigation queue limits";
            return false;
        }
        return true;
    }
    struct NavQueryQueue::Data
    {
        enum class Stage
        {
            Queued,
            Running,
            Completed,
            Published
        };
        struct Job
        {
            Result Value;
            std::shared_ptr<const NavAtlas> Atlas;
            std::stop_source Stop;
            Stage State = Stage::Queued;
            bool Cancelled = false;
            uint64_t Bytes = 0;
        };
        struct Retained
        {
            size_t References = 0;
            uint64_t Bytes = 0;
        };
        Limits Rules;
        mutable std::mutex Mutex;
        std::condition_variable_any Wake;
        std::unordered_map<uint64_t, std::shared_ptr<Job>> Jobs;
        std::unordered_map<uint64_t, uint64_t> Latest;
        std::unordered_map<const NavAtlas*, Retained> Snapshots;
        std::deque<std::shared_ptr<Job>> Queued, Completed;
        uint32_t Tokens = 0;
        uint64_t Serial = 0;
        bool Closing = false;
        Stats Counters;
        std::vector<std::jthread> Workers;
        explicit Data(const Limits& rules) : Rules(rules)
        {
            for (uint32_t i = 0; i < Rules.Workers; ++i) Workers.emplace_back([this](std::stop_token stop) { Run(stop); });
        }
        ~Data()
        {
            {
                std::lock_guard lock(Mutex);
                Closing = true;
                for (auto& [id, job] : Jobs) job->Stop.request_stop();
            }
            Wake.notify_all();
            Workers.clear(); // join while all shared state and snapshots still exist
        }
        // Mutex held. Release buffers before removing their accounting, including cancelled jobs.
        void Release(const std::shared_ptr<Job>& job)
        {
            const auto pointer = job->Atlas.get();
            job->Atlas.reset();
            if (auto it = Snapshots.find(pointer); it != Snapshots.end() && --it->second.References == 0)
            {
                Counters.SnapshotBytes -= it->second.Bytes;
                Snapshots.erase(it);
            }
            job->Value.Route = {};
            Counters.ResultBytes -= job->Bytes;
            job->Bytes = 0;
            if (auto it = Latest.find(job->Value.Key); it != Latest.end() && it->second == job->Value.Serial) Latest.erase(it);
            Jobs.erase(job->Value.Serial);
        }
        void Cancel(const std::shared_ptr<Job>& job)
        {
            if (job->Cancelled) return;
            job->Cancelled = true;
            job->Stop.request_stop();
            ++Counters.Cancelled;
            if (auto it = Latest.find(job->Value.Key); it != Latest.end() && it->second == job->Value.Serial) Latest.erase(it);
            if (job->State == Stage::Running) return;
            std::erase(Queued, job);
            std::erase(Completed, job);
            Release(job);
        }
        void Run(std::stop_token stop)
        {
            for (;;)
            {
                std::shared_ptr<Job> job;
                {
                    std::unique_lock lock(Mutex);
                    if (!Wake.wait(lock, stop, [&] { return Closing || (Tokens && !Queued.empty()); }) || Closing) return;
                    job = Queued.front();
                    Queued.pop_front();
                    --Tokens;
                    job->State = Stage::Running;
                    ++Counters.Started;
                }
                NavRoute route;
                std::string error;
                const auto start = std::chrono::steady_clock::now();
                try
                {
                    auto options = job->Value.Options;
                    options.MaxExpanded = options.MaxExpanded > 0 ? std::min(options.MaxExpanded, int(Rules.MaxExpanded)) : int(Rules.MaxExpanded);
                    route = job->Atlas->FindRouteCancellable(job->Value.From, job->Value.To, options, job->Stop.get_token());
                    if (route.Points.size() > Rules.MaxPoints)
                    {
                        route.Points = std::vector<NavRoutePoint>{};
                        route.Status = NavPathStatus::Failed;
                        route.Cost = 0;
                        error = "Navigation route point limit exceeded";
                    }
                }
                catch (const std::exception& e)
                {
                    route = {};
                    route.Status = NavPathStatus::Failed;
                    error = std::string(e.what()).substr(0, 1024);
                }
                catch (...)
                {
                    route = {};
                    route.Status = NavPathStatus::Failed;
                    error = "Navigation worker failed";
                }
                if (!route.Revision) route.Revision = job->Atlas->GetRevision();
                const double elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
                {
                    std::lock_guard lock(Mutex);
                    ++Counters.Finished;
                    Counters.WorkerMilliseconds += elapsed;
                    if (job->Cancelled || Closing || stop.stop_requested())
                    {
                        Release(job);
                        continue;
                    }
                    uint64_t bytes = uint64_t(route.Points.capacity()) * sizeof(NavRoutePoint);
                    if (bytes > Rules.ResultBytes - Counters.ResultBytes)
                    {
                        route = {};
                        route.Status = NavPathStatus::Failed;
                        route.Revision = job->Atlas->GetRevision();
                        error = "Navigation result byte budget exceeded";
                        bytes = 0;
                        ++Counters.Discarded;
                    }
                    else if (!error.empty()) ++Counters.Discarded;
                    job->Value.Route = std::move(route);
                    job->Value.Error = std::move(error);
                    job->Value.Milliseconds = elapsed;
                    job->Bytes = bytes;
                    Counters.ResultBytes += bytes;
                    job->State = Stage::Completed;
                    Completed.push_back(job);
                }
            }
        }
    };
    NavQueryQueue::NavQueryQueue(const Limits& limits)
    {
        std::string error;
        if (!limits.Validate(error)) throw std::invalid_argument(error);
        m_Data = std::make_unique<Data>(limits);
    }
    NavQueryQueue::~NavQueryQueue() = default;
    uint64_t NavQueryQueue::Submit(uint64_t key, std::shared_ptr<const NavAtlas> atlas, glm::vec2 from, glm::vec2 to, const NavPathOptions& options, std::string& error)
    {
        auto& d = *m_Data;
        std::lock_guard lock(d.Mutex);
        error.clear();
        auto reject = [&](const char* reason) { error = reason; ++d.Counters.Rejected; return uint64_t(0); };
        if (!key || !atlas || !atlas->GetRevision() || !atlas->GetCellCount() || !std::isfinite(from.x) || !std::isfinite(from.y) ||
            !std::isfinite(to.x) || !std::isfinite(to.y) || d.Serial == UINT64_MAX || d.Closing) return reject("Invalid navigation request");
        std::shared_ptr<Data::Job> previous;
        if (auto it = d.Latest.find(key); it != d.Latest.end()) previous = d.Jobs.at(it->second);
        // Replacing queued/published work can reuse that slot, but a running cancelled job stays
        // charged until its worker acknowledges cancellation. Failed admission preserves old work.
        const bool reusable = previous && previous->State != Data::Stage::Running;
        if (d.Jobs.size() - size_t(reusable) >= d.Rules.Capacity) return reject("Navigation queue capacity reached");
        const auto* pointer = atlas.get();
        const uint64_t bytes = atlas->GetMemoryBytes();
        uint64_t releasable = 0;
        if (reusable && previous->Atlas.get() != pointer)
            if (auto it = d.Snapshots.find(previous->Atlas.get()); it != d.Snapshots.end() && it->second.References == 1) releasable = it->second.Bytes;
        if (!d.Snapshots.contains(pointer) && bytes > d.Rules.SnapshotBytes - (d.Counters.SnapshotBytes - releasable)) return reject("Navigation snapshot byte budget exceeded");
        auto job = std::make_shared<Data::Job>();
        job->Value.Key = key;
        job->Value.Serial = ++d.Serial;
        job->Value.From = from;
        job->Value.To = to;
        job->Value.Options = options;
        if (previous) d.Cancel(previous);
        job->Atlas = std::move(atlas);
        auto& retained = d.Snapshots[pointer];
        if (!retained.References)
        {
            retained.Bytes = bytes;
            d.Counters.SnapshotBytes += bytes;
        }
        ++retained.References;
        d.Jobs.emplace(job->Value.Serial, job);
        d.Latest[key] = job->Value.Serial;
        d.Queued.push_back(job);
        ++d.Counters.Submitted;
        d.Wake.notify_all();
        return job->Value.Serial;
    }
    void NavQueryQueue::Pump(uint32_t maxStarts, uint32_t maxPublish)
    {
        auto& d = *m_Data;
        std::lock_guard lock(d.Mutex);
        d.Tokens = std::min(maxStarts, d.Rules.Capacity);
        for (uint32_t i = 0; i < maxPublish && !d.Completed.empty(); ++i)
        {
            auto job = d.Completed.front();
            d.Completed.pop_front();
            job->State = Data::Stage::Published;
            ++d.Counters.Published;
        }
        d.Wake.notify_all();
    }
    bool NavQueryQueue::Poll(uint64_t key, Result& out)
    {
        auto& d = *m_Data;
        std::lock_guard lock(d.Mutex);
        const auto it = d.Latest.find(key);
        if (it == d.Latest.end()) return false;
        const auto job = d.Jobs.at(it->second);
        if (job->State != Data::Stage::Published) return false;
        out = std::move(job->Value); // keep release metadata in the job after moving its payload
        job->Value.Key = out.Key;
        job->Value.Serial = out.Serial;
        d.Release(job);
        ++d.Counters.Delivered;
        return true;
    }
    bool NavQueryQueue::Has(uint64_t key) const
    {
        auto& d = *m_Data;
        std::lock_guard lock(d.Mutex);
        return d.Latest.contains(key);
    }
    void NavQueryQueue::Cancel(uint64_t key)
    {
        auto& d = *m_Data;
        std::lock_guard lock(d.Mutex);
        if (auto it = d.Latest.find(key); it != d.Latest.end())
        {
            auto job = d.Jobs.at(it->second);
            d.Cancel(job);
        }
    }
    void NavQueryQueue::CancelAll()
    {
        auto& d = *m_Data;
        std::lock_guard lock(d.Mutex);
        std::vector<std::shared_ptr<Data::Job>> jobs;
        jobs.reserve(d.Jobs.size());
        for (auto& [id, job] : d.Jobs) jobs.push_back(job);
        for (auto& job : jobs) d.Cancel(job);
        d.Tokens = 0;
        d.Wake.notify_all();
    }
    std::vector<uint64_t> NavQueryQueue::Keys() const
    {
        auto& d = *m_Data;
        std::lock_guard lock(d.Mutex);
        std::vector<uint64_t> keys;
        keys.reserve(d.Latest.size());
        for (auto [key, serial] : d.Latest) keys.push_back(key);
        return keys;
    }
    NavQueryQueue::Stats NavQueryQueue::GetStats() const
    {
        auto& d = *m_Data;
        std::lock_guard lock(d.Mutex);
        auto result = d.Counters;
        result.Outstanding = d.Jobs.size();
        for (auto& [id, job] : d.Jobs)
            switch (job->State)
            {
                case Data::Stage::Queued:    ++result.Queued; break;
                case Data::Stage::Running:   ++result.Running; break;
                case Data::Stage::Completed: ++result.Completed; break;
                case Data::Stage::Published: ++result.Available; break;
            }
        return result;
    }
}
