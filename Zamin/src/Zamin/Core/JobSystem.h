#pragma once

#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <span>
#include <vector>

// A thread pool for work that splits into independent pieces (particle systems, animator poses,
// a loop over many items):
//
//   JobSystem::ParallelFor(count, 0, [&](uint32_t begin, uint32_t end) {
//       for (uint32_t i = begin; i < end; i++)
//           results[i] = Compute(items[i]); // each piece writes only its own data
//   });
//
//   JobHandle load = JobSystem::Schedule([&] { LoadData(); });
//   JobHandle build = JobSystem::Schedule([&] { BuildIndex(); }, load); // after `load`
//   JobSystem::Wait(build);
//
// The application starts it (JobSystem::Init: one worker thread less than the CPU has hardware
// threads, the main thread being the other one). Without Init - or switched off with SetEnabled(false),
// ZAMIN_JOBS=0 or the player's --jobs 0 - every job runs right away on the thread that schedules it,
// in order: the same results, nothing in parallel (deterministic tests, debugging).
//
// A job must not touch what other jobs running at the same time write (the scene's registry, the
// renderer, the physics world, scripts): the engine runs only work on disjoint data in parallel and
// does everything else on the main thread before or after.
namespace Zamin {

    namespace Detail { struct JobState; }

    // A scheduled job: wait for it (JobSystem::Wait) or let other jobs wait for it (Schedule with it
    // as a dependency). An empty handle counts as done.
    class JobHandle
    {
    public:
        JobHandle() = default;
        bool IsValid() const { return (bool)m_State; }
        // It has run (and so have the jobs it depended on).
        bool IsDone() const;
    private:
        friend class JobSystem;
        std::shared_ptr<Detail::JobState> m_State;
    };

    class JobSystem
    {
    public:
        using Job = std::function<void()>;
        // A piece of a ParallelFor: the indices begin .. end - 1.
        using RangeJob = std::function<void(uint32_t begin, uint32_t end)>;

        static constexpr uint32_t MaxWorkers = 64;

        // Starts `workers` worker threads (0: DefaultWorkerCount; ZAMIN_JOBS=<n> overrides, 0 there
        // switches it off). Started already: restarts with the new count (after the jobs queued so far).
        static void Init(uint32_t workers = 0);
        // Runs what is still queued, then stops the workers.
        static void Shutdown();
        static bool IsRunning();
        // The hardware threads less one (the main thread), at least 1, at most MaxWorkers.
        static uint32_t DefaultWorkerCount();
        // 0 while not running.
        static uint32_t GetWorkerCount();

        // Off: jobs run on the thread that schedules them, at once, in order (on by default).
        static void SetEnabled(bool enabled);
        static bool IsEnabled();
        // Jobs really run in parallel now (running, enabled, workers).
        static bool IsParallel();

        // ---- jobs
        static JobHandle Schedule(Job job);
        // Runs once `dependency` / all the `dependencies` are done.
        static JobHandle Schedule(Job job, const JobHandle& dependency);
        static JobHandle Schedule(Job job, std::span<const JobHandle> dependencies);
        static JobHandle Schedule(Job job, std::initializer_list<JobHandle> dependencies)
        {
            return Schedule(std::move(job), std::span<const JobHandle>(dependencies.begin(), dependencies.size()));
        }
        // A handle that is done when all of these are.
        static JobHandle Combine(std::span<const JobHandle> handles);
        // Returns when the job is done; the waiting thread runs queued jobs meanwhile (so a job may wait
        // for another one without blocking a worker).
        static void Wait(const JobHandle& handle);
        static void Wait(std::span<const JobHandle> handles);

        // fn(begin, end) over 0 .. count - 1 in pieces of `grain` indices (0: about four pieces per
        // thread) on the workers and the calling thread; returns when every piece is done. Pieces
        // may run in any order and at the same time; switched off they run in order on this thread.
        static void ParallelFor(uint32_t count, uint32_t grain, const RangeJob& fn);
        // The same without waiting (after `dependency`): the handle is done when every piece is.
        static JobHandle ScheduleParallelFor(uint32_t count, uint32_t grain, RangeJob fn, const JobHandle& dependency = {});
        // The piece size ParallelFor takes for grain 0.
        static uint32_t GetDefaultGrain(uint32_t count);

        // 0 on threads that are not workers (the main thread), 1 .. GetWorkerCount() on the workers:
        // an index into per-thread scratch data (GetWorkerCount() + 1 entries).
        static uint32_t GetThreadIndex();
        static bool IsWorkerThread() { return GetThreadIndex() != 0; }

        // ---- statistics (the Stats panel): per thread, index as GetThreadIndex
        struct ThreadStats
        {
            uint64_t Jobs = 0;           // jobs / ParallelFor pieces it ran
            double BusyMilliseconds = 0; // the time they took
        };
        struct Stats
        {
            uint32_t Workers = 0;
            bool Enabled = true;
            uint64_t Jobs = 0;           // all threads together
            uint64_t ParallelFors = 0;   // ParallelFor / ScheduleParallelFor calls
            std::vector<ThreadStats> Threads;
        };
        // Since the start (or ResetStats).
        static Stats GetStats();
        // The last frame's (the frame before the last EndFrame), for the Stats panel.
        static const Stats& GetLastFrameStats();
        // A frame ends (the application calls it once a frame): GetLastFrameStats becomes this frame's.
        static void EndFrame();
        static void ResetStats();
    };

}
