#pragma once
#include "Zamin/AI/NavAtlas.h"
#include <memory>
#include <string>
namespace Zamin {
    // Bounded CPU-only service. Jobs retain immutable atlas snapshots, never ECS/plugin callbacks.
    // Pump grants non-accumulating start tokens and publishes a bounded prefix of completions.
    class NavQueryQueue
    {
    public:
        struct Limits
        {
            uint32_t Workers = 2, Capacity = 64, MaxExpanded = 65536, MaxPoints = 32768;
            uint64_t SnapshotBytes = 64 * 1024 * 1024, ResultBytes = 8 * 1024 * 1024;
            bool Validate(std::string& error) const;
        };
        struct Result
        {
            uint64_t Key = 0, Serial = 0;
            glm::vec2 From{ 0 }, To{ 0 };
            NavPathOptions Options;
            NavRoute Route;
            double Milliseconds = 0;
            std::string Error;
        };
        struct Stats
        {
            size_t Outstanding = 0, Queued = 0, Running = 0, Completed = 0, Available = 0;
            uint64_t SnapshotBytes = 0, ResultBytes = 0;
            uint64_t Submitted = 0, Started = 0, Finished = 0, Published = 0, Delivered = 0;
            uint64_t Cancelled = 0, Rejected = 0, Discarded = 0;
            double WorkerMilliseconds = 0;
        };
        explicit NavQueryQueue(const Limits& limits);
        ~NavQueryQueue();
        NavQueryQueue(const NavQueryQueue&) = delete;
        NavQueryQueue& operator=(const NavQueryQueue&) = delete;
        uint64_t Submit(uint64_t key, std::shared_ptr<const NavAtlas> atlas, glm::vec2 from, glm::vec2 to,
                        const NavPathOptions& options, std::string& error);
        void Pump(uint32_t maxStarts, uint32_t maxPublish);
        bool Poll(uint64_t key, Result& result);
        bool Has(uint64_t key) const;
        void Cancel(uint64_t key);
        void CancelAll();
        Stats GetStats() const;
        std::vector<uint64_t> Keys() const;
    private:
        struct Data;
        std::unique_ptr<Data> m_Data;
    };
}
