#pragma once

/// @file
/// @brief AsyncLoader: textures and models loaded in the background (tasks 486, 487).

#include <cstdint>
#include <functional>
#include <string>

namespace Zamin {

    /// Background loading: the slow part of a load (reading and decoding files, parsing a model)
    /// runs on a job system worker; what needs the GPU (texture and mesh uploads) waits for the main
    /// thread, which does it in Update - a few milliseconds' worth a frame, so a big scene opens
    /// without one long stall. Until then a texture shows a placeholder (TextureLibrary::LoadAsync)
    /// and a model draws nothing (Model::LoadAsync); both are the same objects all along, so what
    /// holds them sees the real content as soon as it is there.
    ///
    /// Off (SetEnabled(false)) every LoadAsync loads at once like Load: tests and headless captures
    /// (ZAMIN_SCREENSHOT, ZAMIN_MAX_FRAMES) stay frame-exact. Without job system workers the work
    /// runs at once on the calling thread and only the uploads wait for Update.
    class AsyncLoader
    {
    public:
        /// On / off for the LoadAsync calls from now on (the loads under way finish either way).
        static void SetEnabled(bool enabled);
        static bool IsEnabled();
        /// What an application starts with: `wanted`, except for automated captures (ZAMIN_SCREENSHOT
        /// or ZAMIN_MAX_FRAMES set: off); ZAMIN_ASYNC_LOAD=0 / 1 decides over both.
        static bool GetStartupMode(bool wanted);

        /// A load: `work` on a worker (no GPU, no scene, nothing of the main thread's), then `finish` on
        /// the main thread in Update. Returns its id.
        static uint64_t Enqueue(std::string name, std::function<void()> work, std::function<void()> finish);
        /// Main thread, once a frame: the finishes of the loads whose work is done and the queued GPU
        /// uploads, until `budgetMilliseconds` are used (one of each at least). Returns how many ran.
        static size_t Update(double budgetMilliseconds = 4.0);
        /// Until that load has finished (its work waited for, its finish run now) - a Load of what is
        /// being loaded in the background.
        static void Wait(uint64_t id);
        /// Every load and upload finished (a capture, a test, the editor before it saves a thumbnail).
        static void WaitAll();
        static bool IsPending(uint64_t id);
        static size_t GetPendingCount();       ///< loads not finished yet
        static size_t GetPendingUploadCount(); ///< GPU uploads waiting

        /// ---- GPU work put off: on a thread where this is on, textures and meshes are made without
        /// their GPU objects (the CPU data kept) and queued for Update to upload.
        static bool IsDeferringGPU();
        class DeferGPU
        {
        public:
            DeferGPU();
            ~DeferGPU();
            DeferGPU(const DeferGPU&) = delete;
            DeferGPU& operator=(const DeferGPU&) = delete;
        private:
            bool m_Previous;
        };
        /// A GPU upload of `owner` (a texture, a mesh) for Update; CancelUpload when it goes first. Any thread.
        static void QueueUpload(const void* owner, std::function<void()> upload);
        static void CancelUpload(const void* owner);
        /// Runs `owner`'s queued upload now, if it has one (main thread). True when it ran.
        static bool RunUpload(const void* owner);

        struct Stats
        {
            uint64_t Loads = 0;          ///< Enqueue calls
            uint64_t Finished = 0;       ///< loads whose finish ran
            uint64_t Uploads = 0;        ///< GPU uploads done in Update / RunUpload
            double WorkMilliseconds = 0.0;    ///< on the workers
            double FinishMilliseconds = 0.0;  ///< on the main thread (finishes)
            double UploadMilliseconds = 0.0;  ///< on the main thread (uploads)
            double LongestUpdateMilliseconds = 0.0; ///< the most one Update took
        };
        static Stats GetStats();
        static void ResetStats();
    };

}
