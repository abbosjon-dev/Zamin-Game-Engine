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
        };

        struct State
        {
            bool Enabled = false;
            uint64_t NextId = 1;
            std::vector<std::shared_ptr<Load>> Loads; // main thread only, in order
            std::recursive_mutex UploadMutex;         // the uploads: any thread queues, the main thread runs
            std::deque<Upload> Uploads;
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

        // One queued upload (the oldest); false when there is none.
        bool RunOneUpload()
        {
            State& s = S();
            std::lock_guard lock(s.UploadMutex); // held while it runs: its owner cannot go meanwhile
            if (s.Uploads.empty())
                return false;
            Upload upload = std::move(s.Uploads.front());
            s.Uploads.pop_front();
            const auto start = Clock::now();
            if (upload.Run)
                upload.Run();
            const double ms = Ms(start);
            std::lock_guard statsLock(s.StatsMutex);
            s.Stats.Uploads++;
            s.Stats.UploadMilliseconds += ms;
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
        // GPU uploads within what is left of the budget (one at least).
        bool first = true;
        while ((first || Ms(start) < budgetMilliseconds) && RunOneUpload())
        {
            first = false;
            ran++;
        }
        const double ms = Ms(start);
        std::lock_guard lock(s.StatsMutex);
        s.Stats.LongestUpdateMilliseconds = std::max(s.Stats.LongestUpdateMilliseconds, ms);
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

    void AsyncLoader::QueueUpload(const void* owner, std::function<void()> upload)
    {
        std::lock_guard lock(S().UploadMutex);
        S().Uploads.push_back({ owner, std::move(upload) });
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
        if (upload.Run)
            upload.Run();
        const double ms = Ms(start);
        std::lock_guard statsLock(s.StatsMutex);
        s.Stats.Uploads++;
        s.Stats.UploadMilliseconds += ms;
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
