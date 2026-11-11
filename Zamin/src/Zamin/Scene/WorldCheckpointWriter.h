#pragma once
#include "Zamin/Scene/WorldCheckpoint.h"
namespace Zamin {
    // One bounded, engine-owned IO job. A snapshot holds JSON strings only, never live ECS/GPU
    // or plugin callbacks. Destruction joins a submitted atomic write instead of abandoning it.
    class WorldCheckpointWriter
    {
    public:
        struct Result
        {
            uint64_t Serial = 0;
            bool Success = false;
            std::filesystem::path File;
            std::string Error;
        };
        WorldCheckpointWriter();
        ~WorldCheckpointWriter();
        bool Submit(const WorldCheckpoint& snapshot, const std::filesystem::path& file, uint64_t serial, std::string& error);
        bool Poll(Result& result);
        bool IsBusy() const;
    private:
        struct Data;
        std::unique_ptr<Data> m_Data;
    };
}
