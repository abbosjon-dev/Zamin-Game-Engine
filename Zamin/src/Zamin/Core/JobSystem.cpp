#include "Zamin/Core/JobSystem.h"
#include "Zamin/Core/Log.h"
#include "Zamin/Core/Profiler.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <exception>
#include <format>
#include <mutex>
#include <string>
#include <thread>

#if defined(__linux__)
    #include <pthread.h>
#endif

namespace Zamin {

    namespace Detail {
        struct JobState
        {
            JobSystem::Job Fn;
            // The dependencies not done yet, plus one while Schedule is still adding them: queued at 0.
            std::atomic<int> Pending{ 1 };
            std::mutex Mutex;                                 // guards Finished and Dependents
            bool Finished = false;
            std::vector<std::shared_ptr<JobState>> Dependents; // queued (or counted down) when it is done
            std::atomic<bool> Done{ false };
            Profiler::ScopeToken ProfilerParent;              // the scope it was scheduled in
        };
    }

    namespace {
        using Clock = std::chrono::steady_clock;
        using State = std::shared_ptr<Detail::JobState>;

        struct alignas(64) ThreadCounters
        {
            std::atomic<uint64_t> Jobs{ 0 };
            std::atomic<uint64_t> BusyNanoseconds{ 0 };
        };

        std::mutex s_LifetimeMutex;                 // Init / Shutdown
        std::mutex s_QueueMutex;                    // s_Queue, s_Quit; the condition variables' mutex
        std::condition_variable s_WorkCondition;    // workers: a job was queued (or quit)
        std::condition_variable s_DoneCondition;    // waiting threads: a job finished or one was queued
        std::deque<State> s_Queue;
        bool s_Quit = false;
        std::vector<std::thread> s_Threads;
        std::atomic<bool> s_Running{ false };
        std::atomic<bool> s_Enabled{ true };
        std::atomic<uint32_t> s_WorkerCount{ 0 };
        std::atomic<int> s_Waiters{ 0 };
        std::array<ThreadCounters, JobSystem::MaxWorkers + 1> s_Counters;
        std::atomic<uint64_t> s_ParallelFors{ 0 };
        JobSystem::Stats s_LastFrame;
        JobSystem::Stats s_FrameStart;

        thread_local uint32_t t_ThreadIndex = 0;

        // The profiler's name of a thread's jobs (pointers that stay valid: the profiler keeps them).
        const char* ThreadName(uint32_t index)
        {
            static const std::array<std::string, JobSystem::MaxWorkers + 1> names = [] {
                std::array<std::string, JobSystem::MaxWorkers + 1> n;
                n[0] = "Asosiy oqim";
                for (uint32_t i = 1; i <= JobSystem::MaxWorkers; i++)
                    n[i] = std::format("Ishchi {}", i);
                return n;
            }();
            return names[std::min<size_t>(index, JobSystem::MaxWorkers)].c_str();
        }

        void Count(uint32_t index, Clock::time_point started)
        {
            ThreadCounters& c = s_Counters[std::min<size_t>(index, JobSystem::MaxWorkers)];
            c.Jobs.fetch_add(1, std::memory_order_relaxed);
            c.BusyNanoseconds.fetch_add((uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - started).count(),
                                        std::memory_order_relaxed);
        }

        // Wakes the threads waiting for jobs to finish (Wait / ParallelFor). Taking the lock first
        // makes sure none of them is between checking and going to sleep.
        void NotifyWaiters()
        {
            if (s_Waiters.load() > 0)
            {
                {
                    std::lock_guard lock(s_QueueMutex);
                }
                s_DoneCondition.notify_all();
            }
        }

        void Enqueue(State job)
        {
            {
                std::lock_guard lock(s_QueueMutex);
                s_Queue.push_back(std::move(job));
            }
            s_WorkCondition.notify_one();
            if (s_Waiters.load() > 0)
                s_DoneCondition.notify_all(); // a waiting thread may run it
        }

        State TryPop()
        {
            std::lock_guard lock(s_QueueMutex);
            if (s_Queue.empty())
                return nullptr;
            State job = std::move(s_Queue.front());
            s_Queue.pop_front();
            return job;
        }

        template<typename F>
        void Invoke(const F& fn)
        {
            try
            {
                fn();
            }
            catch (const std::exception& e)
            {
                ZM_CORE_ERROR("Vazifa (job) xato bilan to'xtadi: {}", e.what());
            }
            catch (...)
            {
                ZM_CORE_ERROR("Vazifa (job) noma'lum xato bilan to'xtadi");
            }
        }

        void Finish(const State& job)
        {
            std::vector<State> dependents;
            {
                std::lock_guard lock(job->Mutex);
                job->Finished = true;
                dependents.swap(job->Dependents);
            }
            job->Done.store(true);
            for (State& dependent : dependents)
                if (dependent->Pending.fetch_sub(1) == 1)
                    Enqueue(std::move(dependent));
            NotifyWaiters();
        }

        // Runs a queued job on this thread. On a worker its profiler scopes go under the scope that
        // scheduled it, in a node named after the worker.
        void Run(const State& job)
        {
            const uint32_t index = t_ThreadIndex;
            const Clock::time_point started = Clock::now();
            if (index != 0)
            {
                Profiler::SetThreadParent(job->ProfilerParent);
                {
                    ScopedTimer timer(ThreadName(index));
                    if (job->Fn)
                        Invoke(job->Fn);
                }
                Profiler::SetThreadParent({});
            }
            else if (job->Fn)
                Invoke(job->Fn);
            job->Fn = nullptr; // what it captured goes now
            Count(index, started);
            Finish(job);
        }

        void WorkerMain(uint32_t index)
        {
            t_ThreadIndex = index;
#if defined(__linux__)
            const std::string name = std::format("zamin-ishchi-{}", index); // at most 15 characters
            pthread_setname_np(pthread_self(), name.c_str());
#endif
            for (;;)
            {
                State job;
                {
                    std::unique_lock lock(s_QueueMutex);
                    s_WorkCondition.wait(lock, [] { return s_Quit || !s_Queue.empty(); });
                    if (s_Queue.empty())
                        return; // quitting, nothing left
                    job = std::move(s_Queue.front());
                    s_Queue.pop_front();
                }
                Run(job);
            }
        }

        // The worker count ZAMIN_JOBS asks for: -1 when it is not set (or not a number).
        int EnvironmentWorkers()
        {
            const char* value = std::getenv("ZAMIN_JOBS");
            if (!value || !*value)
                return -1;
            if (std::strcmp(value, "off") == 0 || std::strcmp(value, "false") == 0)
                return 0;
            char* end = nullptr;
            const long n = std::strtol(value, &end, 10);
            if (end == value || n < 0)
                return -1;
            return (int)std::min<long>(n, JobSystem::MaxWorkers);
        }

        // ---- ParallelFor's shared state: pieces are taken by counting up; a helper that comes
        // after the last one was taken does nothing (and never touches Fn, which may be gone).
        struct ForState
        {
            const JobSystem::RangeJob* Fn = nullptr;
            uint32_t Count = 0, Grain = 1, Pieces = 0;
            std::atomic<uint32_t> Next{ 0 };
            std::atomic<uint32_t> Completed{ 0 };
        };

        void RunPieces(ForState& state)
        {
            for (;;)
            {
                const uint32_t piece = state.Next.fetch_add(1);
                if (piece >= state.Pieces)
                    return;
                const uint32_t begin = piece * state.Grain;
                const uint32_t end = std::min(state.Count, begin + state.Grain);
                const Clock::time_point started = Clock::now();
                Invoke([&] { (*state.Fn)(begin, end); });
                Count(t_ThreadIndex, started);
                if (state.Completed.fetch_add(1) + 1 == state.Pieces)
                    NotifyWaiters();
            }
        }

        JobSystem::Stats Snapshot()
        {
            JobSystem::Stats stats;
            stats.Workers = s_WorkerCount.load();
            stats.Enabled = s_Enabled.load();
            stats.ParallelFors = s_ParallelFors.load(std::memory_order_relaxed);
            stats.Threads.resize((size_t)stats.Workers + 1);
            for (size_t i = 0; i < stats.Threads.size(); i++)
            {
                stats.Threads[i].Jobs = s_Counters[i].Jobs.load(std::memory_order_relaxed);
                stats.Threads[i].BusyMilliseconds = s_Counters[i].BusyNanoseconds.load(std::memory_order_relaxed) / 1e6;
                stats.Jobs += stats.Threads[i].Jobs;
            }
            return stats;
        }
    }

    bool JobHandle::IsDone() const
    {
        return !m_State || m_State->Done.load();
    }

    // ------------------------------------------------------------------ lifetime
    uint32_t JobSystem::DefaultWorkerCount()
    {
        const unsigned hardware = std::thread::hardware_concurrency();
        return std::clamp<uint32_t>(hardware > 1 ? hardware - 1 : 1, 1, MaxWorkers);
    }

    void JobSystem::Init(uint32_t workers)
    {
        if (IsWorkerThread())
            return; // a job cannot restart the pool it runs in
        Shutdown();
        std::lock_guard lifetime(s_LifetimeMutex);
        const int fromEnvironment = EnvironmentWorkers();
        if (fromEnvironment >= 0)
            workers = (uint32_t)fromEnvironment;
        else if (workers == 0)
            workers = DefaultWorkerCount();
        workers = std::min(workers, MaxWorkers);
        if (workers == 0)
        {
            ZM_CORE_INFO("Vazifalar tizimi o'chiq (ZAMIN_JOBS=0): hamma ish asosiy oqimda");
            return;
        }
        {
            std::lock_guard lock(s_QueueMutex);
            s_Quit = false;
        }
        s_WorkerCount.store(workers);
        s_Threads.reserve(workers);
        for (uint32_t i = 1; i <= workers; i++)
            s_Threads.emplace_back(WorkerMain, i);
        s_Running.store(true);
        s_FrameStart = Snapshot();
        ZM_CORE_INFO("Vazifalar tizimi: {} ishchi oqim", workers);
    }

    void JobSystem::Shutdown()
    {
        if (IsWorkerThread())
            return;
        std::lock_guard lifetime(s_LifetimeMutex);
        if (s_Threads.empty())
            return;
        {
            std::lock_guard lock(s_QueueMutex);
            s_Quit = true; // the workers empty the queue first
        }
        s_WorkCondition.notify_all();
        for (std::thread& t : s_Threads)
            t.join();
        s_Threads.clear();
        s_Running.store(false);
        s_WorkerCount.store(0);
        // Nothing should be left; whatever is runs here.
        while (State job = TryPop())
            Run(job);
        NotifyWaiters();
    }

    bool JobSystem::IsRunning() { return s_Running.load(); }
    uint32_t JobSystem::GetWorkerCount() { return s_WorkerCount.load(); }
    void JobSystem::SetEnabled(bool enabled) { s_Enabled.store(enabled); }
    bool JobSystem::IsEnabled() { return s_Enabled.load(); }
    bool JobSystem::IsParallel() { return s_Running.load() && s_Enabled.load() && s_WorkerCount.load() > 0; }
    uint32_t JobSystem::GetThreadIndex() { return t_ThreadIndex; }

    // ------------------------------------------------------------------ jobs
    JobHandle JobSystem::Schedule(Job job)
    {
        return Schedule(std::move(job), std::span<const JobHandle>());
    }

    JobHandle JobSystem::Schedule(Job job, const JobHandle& dependency)
    {
        return Schedule(std::move(job), std::span<const JobHandle>(&dependency, 1));
    }

    JobHandle JobSystem::Schedule(Job job, std::span<const JobHandle> dependencies)
    {
        JobHandle handle;
        handle.m_State = std::make_shared<Detail::JobState>();
        Detail::JobState& state = *handle.m_State;
        if (!IsParallel())
        {
            // Here and now, in order (what it waits for first - queued while it was on).
            Wait(dependencies);
            const Clock::time_point started = Clock::now();
            if (job)
                Invoke(job);
            Count(t_ThreadIndex, started);
            state.Finished = true;
            state.Done.store(true);
            return handle;
        }
        state.Fn = std::move(job);
        state.ProfilerParent = Profiler::GetCurrentScope();
        for (const JobHandle& dependency : dependencies)
        {
            if (!dependency.m_State)
                continue;
            std::lock_guard lock(dependency.m_State->Mutex);
            if (!dependency.m_State->Finished)
            {
                dependency.m_State->Dependents.push_back(handle.m_State);
                state.Pending.fetch_add(1);
            }
        }
        if (state.Pending.fetch_sub(1) == 1)
            Enqueue(handle.m_State);
        return handle;
    }

    JobHandle JobSystem::Combine(std::span<const JobHandle> handles)
    {
        return Schedule(Job(), handles);
    }

    void JobSystem::Wait(const JobHandle& handle)
    {
        if (!handle.m_State)
            return;
        const Detail::JobState& state = *handle.m_State;
        while (!state.Done.load())
        {
            if (State job = TryPop())
            {
                Run(job);
                continue;
            }
            std::unique_lock lock(s_QueueMutex);
            s_Waiters.fetch_add(1);
            s_DoneCondition.wait(lock, [&] { return state.Done.load() || !s_Queue.empty(); });
            s_Waiters.fetch_sub(1);
        }
    }

    void JobSystem::Wait(std::span<const JobHandle> handles)
    {
        for (const JobHandle& handle : handles)
            Wait(handle);
    }

    // ------------------------------------------------------------------ ParallelFor
    uint32_t JobSystem::GetDefaultGrain(uint32_t count)
    {
        const uint32_t threads = (IsParallel() ? GetWorkerCount() : 0) + 1;
        const uint32_t pieces = threads * 4;
        return std::max<uint32_t>(1, (count + pieces - 1) / pieces);
    }

    void JobSystem::ParallelFor(uint32_t count, uint32_t grain, const RangeJob& fn)
    {
        if (count == 0 || !fn)
            return;
        if (grain == 0)
            grain = GetDefaultGrain(count);
        s_ParallelFors.fetch_add(1, std::memory_order_relaxed);
        auto state = std::make_shared<ForState>();
        state->Fn = &fn;
        state->Count = count;
        state->Grain = grain;
        state->Pieces = (uint32_t)(((uint64_t)count + grain - 1) / grain);
        if (!IsParallel() || state->Pieces <= 1)
        {
            RunPieces(*state); // in order, here
            return;
        }
        // Helpers on the workers (one per piece beyond the caller's, at most one per worker), then
        // the caller takes pieces too.
        const uint32_t helpers = std::min(GetWorkerCount(), state->Pieces - 1);
        const Profiler::ScopeToken parent = Profiler::GetCurrentScope();
        for (uint32_t i = 0; i < helpers; i++)
        {
            auto job = std::make_shared<Detail::JobState>();
            job->Fn = [state] { RunPieces(*state); };
            job->ProfilerParent = parent;
            job->Pending.store(0);
            Enqueue(std::move(job));
        }
        RunPieces(*state);
        // The pieces the workers took are running: wait for them (not for helpers that came late).
        if (state->Completed.load() < state->Pieces)
        {
            std::unique_lock lock(s_QueueMutex);
            s_Waiters.fetch_add(1);
            s_DoneCondition.wait(lock, [&] { return state->Completed.load() >= state->Pieces; });
            s_Waiters.fetch_sub(1);
        }
    }

    JobHandle JobSystem::ScheduleParallelFor(uint32_t count, uint32_t grain, RangeJob fn, const JobHandle& dependency)
    {
        return Schedule([count, grain, fn = std::move(fn)] { ParallelFor(count, grain, fn); }, dependency);
    }

    // ------------------------------------------------------------------ statistics
    JobSystem::Stats JobSystem::GetStats()
    {
        return Snapshot();
    }

    const JobSystem::Stats& JobSystem::GetLastFrameStats()
    {
        return s_LastFrame;
    }

    void JobSystem::EndFrame()
    {
        Stats now = Snapshot();
        Stats frame = now;
        frame.Jobs = 0;
        frame.ParallelFors = now.ParallelFors - std::min(now.ParallelFors, s_FrameStart.ParallelFors);
        for (size_t i = 0; i < frame.Threads.size(); i++)
        {
            const ThreadStats before = i < s_FrameStart.Threads.size() ? s_FrameStart.Threads[i] : ThreadStats();
            frame.Threads[i].Jobs = now.Threads[i].Jobs - std::min(now.Threads[i].Jobs, before.Jobs);
            frame.Threads[i].BusyMilliseconds = std::max(0.0, now.Threads[i].BusyMilliseconds - before.BusyMilliseconds);
            frame.Jobs += frame.Threads[i].Jobs;
        }
        s_LastFrame = std::move(frame);
        s_FrameStart = std::move(now);
    }

    void JobSystem::ResetStats()
    {
        for (ThreadCounters& c : s_Counters)
        {
            c.Jobs.store(0);
            c.BusyNanoseconds.store(0);
        }
        s_ParallelFors.store(0);
        s_FrameStart = Snapshot();
        s_LastFrame = Stats();
    }

}
