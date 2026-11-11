#include "Zamin/Scene/WorldCheckpointWriter.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <optional>
namespace Zamin {
    struct WorldCheckpointWriter::Data
    {
        mutable std::mutex Mutex;
        std::condition_variable_any Wake;
        struct Job
        {
            WorldCheckpoint Snapshot;
            Result Output;
        };
        std::optional<Job> Pending;
        std::optional<Result> Finished;
        bool Busy = false;
        std::jthread Worker;
        Data() : Worker([this](std::stop_token stop) {
                     while (true)
                     {
                         std::optional<Job> job;
                         {
                             std::unique_lock lock(Mutex);
                             Wake.wait(lock, stop, [&] { return Pending.has_value(); });
                             if (!Pending) break;
                             job = std::move(Pending);
                             Pending.reset();
                         }
                         try
                         {
                             job->Output.Success = job->Snapshot.Save(job->Output.File, job->Output.Error);
                         }
                         catch (const std::exception& e)
                         {
                             job->Output.Error = e.what();
                         }
                         {
                             std::lock_guard lock(Mutex);
                             Finished = std::move(job->Output);
                         }
                         if (stop.stop_requested()) break;
                     }
                 }) {}
        ~Data()
        {
            Worker.request_stop();
            Wake.notify_all();
            if (Worker.joinable()) Worker.join();
        }
    };
    WorldCheckpointWriter::WorldCheckpointWriter() : m_Data(std::make_unique<Data>()) {}
    WorldCheckpointWriter::~WorldCheckpointWriter() = default;
    bool WorldCheckpointWriter::Submit(const WorldCheckpoint& snapshot, const std::filesystem::path& file, uint64_t serial, std::string& error)
    {
        auto& d = *m_Data;
        std::lock_guard lock(d.Mutex);
        if (d.Busy || file.empty() || file.native().size() > 4096)
        {
            error = "Checkpoint writer busy or invalid output path";
            return false;
        }
        d.Pending.emplace(Data::Job{ snapshot, Result{ serial, false, file, {} } });
        d.Busy = true;
        d.Wake.notify_one();
        error.clear();
        return true;
    }
    bool WorldCheckpointWriter::Poll(Result& result)
    {
        auto& d = *m_Data;
        std::lock_guard lock(d.Mutex);
        if (!d.Finished) return false;
        result = std::move(*d.Finished);
        d.Finished.reset();
        d.Busy = false;
        return true;
    }
    bool WorldCheckpointWriter::IsBusy() const
    {
        std::lock_guard lock(m_Data->Mutex);
        return m_Data->Busy;
    }
}
