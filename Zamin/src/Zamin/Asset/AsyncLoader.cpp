#include "Zamin/Asset/AsyncLoader.h"
#include "Zamin/Core/JobSystem.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

namespace Zamin {

    namespace {
        using Clock = std::chrono::steady_clock;
        double Ms(Clock::time_point start) { return std::chrono::duration<double, std::milli>(Clock::now() - start).count(); }

        struct Load
        {
            uint64_t Id = 0;
            std::string Name;
            std::function<void()> Work, Finish;
            JobHandle Job;
            std::atomic<bool> Done{ false };
            double WorkMs = 0.0;
        };

        struct Upload
        {
            const void* Owner = nullptr;
            std::function<void()> Run;
            AsyncLoader::UploadStep Step; // instead of Run: an upload cut into pieces
            uint64_t Bytes = 0;           // what it still has to send (as declared)
        };

        struct State
        {
            bool Enabled = false;
            uint64_t NextId = 1;
            std::vector<std::shared_ptr<Load>> Loads; // main thread only, in order
            std::recursive_mutex UploadMutex;         // the uploads: any thread queues, the main thread runs
            std::deque<Upload> Uploads;
            AsyncLoader::UploadBudget Budget; // main thread
            std::mutex StatsMutex;
            AsyncLoader::Stats Stats;
        };

        // Never destroyed: textures and meshes in static caches cancel their uploads as the program ends.
        State& S()
        {
            static State* s_State = new State();
            return *s_State;
        }

        thread_local bool t_DeferGPU = false;

        void RunFinish(Load& load)
        {
            const auto start = Clock::now();
            if (load.Finish)
                load.Finish();
            load.Finish = nullptr;
            const double ms = Ms(start);
            std::lock_guard lock(S().StatsMutex);
            S().Stats.Finished++;
            S().Stats.FinishMilliseconds += ms;
            S().Stats.WorkMilliseconds += load.WorkMs;
        }

        // What one Update has spent of the upload budget.
        struct UploadSpend
        {
            uint64_t Bytes = 0;
            size_t Calls = 0;
        };

        void CountUpload(State& s, double ms, uint64_t bytes, bool finished, bool oversize)
        {
            std::lock_guard statsLock(s.StatsMutex);
            s.Stats.Uploads += finished;
            s.Stats.UploadMilliseconds += ms;
            s.Stats.UploadBytes += bytes;
            s.Stats.OversizeUploads += oversize;
        }

        // The oldest queued upload, as far as the budget allows (`spend`: this Update's; nullptr: no budget -
        // the whole upload at once). False when nothing ran: the queue is empty or the budget is used up
        // (`deferred` set then).
        bool RunOneUpload(UploadSpend* spend = nullptr, bool* deferred = nullptr)
        {
            State& s = S();
            std::lock_guard lock(s.UploadMutex); // held while it runs: its owner cannot go meanwhile
            if (s.Uploads.empty())
                return false;
            const AsyncLoader::UploadBudget budget = spend ? s.Budget : AsyncLoader::UploadBudget{};
            auto defer = [&] {
                if (deferred)
                    *deferred = true;
                return false;
            };
            if (budget.MaxUploads && spend->Calls >= budget.MaxUploads)
                return defer();
            const uint64_t left = !budget.MaxBytes ? UINT64_MAX : budget.MaxBytes > spend->Bytes ? budget.MaxBytes - spend->Bytes : 0;
            const bool fresh = !spend || spend->Bytes == 0;
            if (!s.Uploads.front().Step && s.Uploads.front().Bytes > left && !fresh)
                return defer();
            Upload upload = std::move(s.Uploads.front());
            s.Uploads.pop_front();
            const auto start = Clock::now();
            uint64_t used = 0;
            bool finished = true;
            if (upload.Step)
                finished = upload.Step(left, fresh, used);
            else
            {
                if (upload.Run)
                    upload.Run();
                used = upload.Bytes;
            }
            const bool ran = finished || used > 0;
            if (!finished)
            {
                upload.Bytes = upload.Bytes > used ? upload.Bytes - used : 0;
                s.Uploads.push_front(std::move(upload)); // (its place kept: the uploads stay in order)
            }
            if (!ran)
                return defer();
            CountUpload(s, Ms(start), used, finished, used > left);
            if (spend)
            {
                spend->Bytes += used;
                spend->Calls++;
            }
            return true;
        }
    }

    void AsyncLoader::SetEnabled(bool enabled) { S().Enabled = enabled; }
    bool AsyncLoader::IsEnabled() { return S().Enabled; }

    bool AsyncLoader::GetStartupMode(bool wanted)
    {
        if (const char* env = std::getenv("ZAMIN_ASYNC_LOAD"))
            return std::strcmp(env, "0") != 0;
        if (std::getenv("ZAMIN_SCREENSHOT") || std::getenv("ZAMIN_MAX_FRAMES"))
            return false; // captures: the same picture whatever the workers' speed
        return wanted;
    }

    uint64_t AsyncLoader::Enqueue(std::string name, std::function<void()> work, std::function<void()> finish)
    {
        State& s = S();
        auto load = std::make_shared<Load>();
        load->Id = s.NextId++;
        load->Name = std::move(name);
        load->Work = std::move(work);
        load->Finish = std::move(finish);
        {
            std::lock_guard lock(s.StatsMutex);
            s.Stats.Loads++;
        }
        s.Loads.push_back(load);
        // The job holds the load (it may outlive a WaitAll that already finished it - never: Wait waits for it).
        load->Job = JobSystem::Schedule([load] {
            const auto start = Clock::now();
            {
                DeferGPU defer; // what the work makes for the GPU is uploaded by the main thread
                if (load->Work)
                    load->Work();
            }
            load->WorkMs = Ms(start);
            load->Done.store(true, std::memory_order_release);
        });
        return load->Id;
    }

    size_t AsyncLoader::Update(double budgetMilliseconds)
    {
        State& s = S();
        const auto start = Clock::now();
        size_t ran = 0;
        // The loads whose work is done, in order (a later one may wait for an earlier one).
        for (size_t i = 0; i < s.Loads.size();)
        {
            std::shared_ptr<Load> load = s.Loads[i];
            if (!load->Done.load(std::memory_order_acquire))
            {
                i++;
                continue;
            }
            JobSystem::Wait(load->Job);
            s.Loads.erase(s.Loads.begin() + (std::ptrdiff_t)i);
            RunFinish(*load);
            ran++;
            if (Ms(start) >= budgetMilliseconds)
                break;
        }
        // GPU uploads within what is left of the time (one at least) and of the upload budget.
        bool first = true, deferred = false;
        UploadSpend spend;
        while ((first || Ms(start) < budgetMilliseconds) && RunOneUpload(&spend, &deferred))
        {
            first = false;
            ran++;
        }
        const double ms = Ms(start);
        std::lock_guard lock(s.StatsMutex);
        s.Stats.LongestUpdateMilliseconds = std::max(s.Stats.LongestUpdateMilliseconds, ms);
        s.Stats.UploadCalls += spend.Calls;
        s.Stats.PeakUpdateUploadBytes = std::max(s.Stats.PeakUpdateUploadBytes, spend.Bytes);
        s.Stats.PeakUpdateUploadCalls = std::max<uint64_t>(s.Stats.PeakUpdateUploadCalls, spend.Calls);
        s.Stats.BudgetDeferrals += deferred;
        return ran;
    }

    void AsyncLoader::Wait(uint64_t id)
    {
        State& s = S();
        auto it = std::find_if(s.Loads.begin(), s.Loads.end(), [id](const auto& load) { return load->Id == id; });
        if (it == s.Loads.end())
            return;
        std::shared_ptr<Load> load = *it;
        JobSystem::Wait(load->Job); // (runs queued jobs meanwhile)
        it = std::find_if(s.Loads.begin(), s.Loads.end(), [id](const auto& l) { return l->Id == id; });
        if (it != s.Loads.end())
            s.Loads.erase(it);
        RunFinish(*load);
    }

    void AsyncLoader::WaitAll()
    {
        State& s = S();
        while (!s.Loads.empty())
            Wait(s.Loads.front()->Id);
        while (RunOneUpload())
        {
        }
    }

    AsyncLoader::Immediate::Immediate() : m_Previous(S().Enabled)
    {
        WaitAll();
        S().Enabled = false;
    }

    AsyncLoader::Immediate::~Immediate() { S().Enabled = m_Previous; }

    bool AsyncLoader::IsPending(uint64_t id)
    {
        const State& s = S();
        return std::any_of(s.Loads.begin(), s.Loads.end(), [id](const auto& load) { return load->Id == id; });
    }

    size_t AsyncLoader::GetPendingCount() { return S().Loads.size(); }

    size_t AsyncLoader::GetPendingUploadCount()
    {
        std::lock_guard lock(S().UploadMutex);
        return S().Uploads.size();
    }

    bool AsyncLoader::IsDeferringGPU() { return t_DeferGPU; }

    AsyncLoader::DeferGPU::DeferGPU() : m_Previous(t_DeferGPU) { t_DeferGPU = true; }
    AsyncLoader::DeferGPU::~DeferGPU() { t_DeferGPU = m_Previous; }

    uint64_t AsyncLoader::GetPendingUploadBytes()
    {
        std::lock_guard lock(S().UploadMutex);
        uint64_t bytes = 0;
        for (const Upload& upload : S().Uploads)
            bytes += std::min(upload.Bytes, UINT64_MAX - bytes);
        return bytes;
    }

    void AsyncLoader::SetUploadBudget(const UploadBudget& budget) { S().Budget = budget; }
    AsyncLoader::UploadBudget AsyncLoader::GetUploadBudget() { return S().Budget; }

    void AsyncLoader::QueueUpload(const void* owner, std::function<void()> upload, uint64_t bytes)
    {
        std::lock_guard lock(S().UploadMutex);
        S().Uploads.push_back({ owner, std::move(upload), nullptr, bytes });
    }

    void AsyncLoader::QueueSteppedUpload(const void* owner, UploadStep step, uint64_t bytes)
    {
        std::lock_guard lock(S().UploadMutex);
        S().Uploads.push_back({ owner, nullptr, std::move(step), bytes });
    }

    void AsyncLoader::CancelUpload(const void* owner)
    {
        // Let go of after the queue is whole again: an upload may hold its owner (a texture), whose destructor
        // then cancels again.
        std::vector<Upload> cancelled;
        {
            std::lock_guard lock(S().UploadMutex);
            auto& uploads = S().Uploads;
            for (auto it = uploads.begin(); it != uploads.end();)
            {
                if (it->Owner != owner)
                {
                    ++it;
                    continue;
                }
                cancelled.push_back(std::move(*it));
                it = uploads.erase(it);
            }
        }
    }

    bool AsyncLoader::RunUpload(const void* owner)
    {
        State& s = S();
        std::lock_guard lock(s.UploadMutex);
        auto it = std::find_if(s.Uploads.begin(), s.Uploads.end(), [owner](const Upload& u) { return u.Owner == owner; });
        if (it == s.Uploads.end())
            return false;
        Upload upload = std::move(*it);
        s.Uploads.erase(it);
        const auto start = Clock::now();
        uint64_t total = upload.Step ? 0 : upload.Bytes;
        if (upload.Run)
            upload.Run();
        for (bool finished = !upload.Step; !finished;) // (asked for now: the whole upload, whatever the budget)
        {
            uint64_t used = 0;
            finished = upload.Step(UINT64_MAX, true, used);
            total += used;
        }
        CountUpload(s, Ms(start), total, true, false);
        return true;
    }

    AsyncLoader::Stats AsyncLoader::GetStats()
    {
        std::lock_guard lock(S().StatsMutex);
        return S().Stats;
    }

    void AsyncLoader::ResetStats()
    {
        std::lock_guard lock(S().StatsMutex);
        S().Stats = {};
    }

}
