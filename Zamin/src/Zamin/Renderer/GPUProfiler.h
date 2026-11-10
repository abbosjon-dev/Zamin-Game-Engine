#pragma once
#include <cstdint>

namespace Zamin {
    struct GPUFrameStats
    {
        bool Supported = false, Enabled = true;
        double LastMilliseconds = 0, AverageMilliseconds = 0, MinimumMilliseconds = 0, MaximumMilliseconds = 0;
        uint64_t CompletedSamples = 0, DroppedSamples = 0;
        uint32_t PendingQueries = 0;
    };
    // Main GL-context thread only. Eight asynchronous timestamp pairs; results are never waited for.
    // Times the GPU timeline between frame markers; excludes swap/vsync and frame limiter.
    class GPUProfiler
    {
    public:
        static void BeginFrame();
        static void EndFrame();
        static void SetEnabled(bool enabled);
        static const GPUFrameStats& GetStats();
        static void Shutdown();
    };
}
