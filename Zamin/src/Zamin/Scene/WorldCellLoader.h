#pragma once

#include "Zamin/Scene/WorldManifest.h"
#include <memory>

namespace Zamin {
    class SceneLoadJob;

    enum class WorldCellStage
    {
        Unloaded,
        Queued,
        Reading,
        Ready,
        Transferred,
        Failed
    };

    struct WorldCellLoadStatus
    {
        UUID Cell{ 0 };
        uint64_t Generation = 0;
        WorldCellStage Stage = WorldCellStage::Unloaded;
        size_t SourceBytes = 0;
        std::string Error;
    };

    // CPU staging only: no entities, scripts, physics, textures or GPU uploads are created.
    // Create and all public calls belong to one owner thread. IO/parse always run on private
    // workers, independently of JobSystem's synchronous mode. Destruction cancels and joins.
    class WorldCellLoader
    {
    public:
        struct Limits
        {
            size_t Workers = 2;
            size_t MaxWantedCells = 128;
            size_t MaxCellBytes = 16 * 1024 * 1024;
            // MaxCellBytes charged per queued/running/ready job. Source admission only,
            // not a measured RAM/VRAM limit: JSON DOM, scratch and transferred jobs are extra.
            size_t MaxReservedBytes = 64 * 1024 * 1024;
            size_t MaxJSONNodes = 262144;
            size_t MaxEntities = 32768;
        };
        struct Stats
        {
            size_t Wanted = 0, Queued = 0, Reading = 0, Ready = 0, Transferred = 0, Failed = 0;
            size_t InFlight = 0, ReservedSourceBytes = 0;
            uint64_t WorkerJobs = 0, StaleCompletions = 0;
            size_t OverrideBytes = 0;
        };

        // Linux v1: pins the asset directory; each child is opened without following symlinks.
        static std::unique_ptr<WorldCellLoader> Create(const WorldManifest& manifest,
                                                       const std::filesystem::path& assets, const Limits& limits, std::string& error);
        // A shared immutable source for the next queued generation; existing tasks retain theirs.
        // nullptr returns to authored IO. Total overlay bytes are bounded independently of job quota.
        bool SetSourceOverride(UUID cell, std::shared_ptr<const std::string> source, std::string& error);
        ~WorldCellLoader();
        WorldCellLoader(const WorldCellLoader&) = delete;
        WorldCellLoader& operator=(const WorldCellLoader&) = delete;

        // Replaces the desired set, including its dependency closure. Invalid/oversized requests
        // leave it unchanged. Removing a cell cancels/discards staging, not an activated scene.
        // Failed requests retry only after removal and re-addition. Same set retains generations.
        bool SetWanted(const std::vector<UUID>& cells, std::string& error);
        // Requeues one wanted cell with a fresh generation, preserving other requests. Already
        // transferred jobs remain the caller's responsibility; their late results must be discarded.
        bool Invalidate(UUID cell, std::string& error);
        // Publishes at most maxCompletions completed jobs, then admits dependency-ready work.
        // Never waits for unfinished workers. This is a count limit, not a hard frame-time bound.
        size_t Poll(size_t maxCompletions = 8);
        WorldCellLoadStatus GetStatus(UUID cell) const;
        Stats GetStats() const;
        // Generation must match. Transfers the parsed job and its memory responsibility to the
        // caller, releasing this loader's admission reservation. Include SceneSerializer.h when
        // destroying/using the returned job. Build/activation must happen on the main thread.
        std::unique_ptr<SceneLoadJob> TakeReady(UUID cell, uint64_t generation);

    private:
        struct Data;
        explicit WorldCellLoader(std::unique_ptr<Data> data);
        std::unique_ptr<Data> m_Data;
    };
}
