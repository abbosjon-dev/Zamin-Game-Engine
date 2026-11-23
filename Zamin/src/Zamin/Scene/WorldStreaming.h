#pragma once

#include "Zamin/Scene/WorldCellLoader.h"
#include "Zamin/AI/NavAtlas.h"
#include "Zamin/AI/NavQueryQueue.h"
#include "Zamin/Core/Base.h"
#include "Zamin/Asset/AssetResidency.h"
#include "Zamin/Asset/AsyncLoader.h"
#include "Zamin/Scene/WorldAssetStager.h"

namespace Zamin {
    class Scene;
    class SceneManager;

    enum class WorldRuntimeStage
    {
        Dormant,
        Loading,
        Building,
        Resolving,
        Active,
        Failed,
        Staging // AssetStreaming: its textures / models are decoded on workers and uploaded within the budget
    };

    class WorldStreaming
    {
    public:
        struct Policy
        {
            double LoadRadius = 32, UnloadRadius = 48, MainThreadMilliseconds = 3;
            uint32_t AxisMask = 5; // XY: 3, XZ: 5, XYZ: 7.
            uint32_t MaxResidentCells = 12, MaxResidentEntities = 16384, MaxEntitiesPerCell = 4096;
            uint32_t MaxBuildingCells = 1, MaxActivationsPerFrame = 1, MaxUnloadsPerFrame = 2, MaxResolveEntitiesPerFrame = 64;
            uint64_t DeclaredCPUBytes = 512 * 1024 * 1024, DeclaredGPUBytes = 512 * 1024 * 1024;
            // Zero disables a signal. These pause new work; they are not OS / physical VRAM caps.
            uint64_t ObservedRSSLimitBytes = 0, RequestedGPULimitBytes = 0;
            double PressureResumeRatio = .9;
            // With EvictOnPressure the two signals above are residency limits the world gets back under:
            // once trimming the caches frees nothing more, the farthest admitted cell that no other
            // admitted cell depends on is retired - never the cell the viewer is in; under the RSS limit only
            // after freed heap memory was handed back to the system and a fresh RSS sample still says so (one
            // cell per 0.75 s) - and held out for PressureCooldownFrames. With AssetStreaming a cell whose staged assets would take the
            // tracked GPU requests past RequestedGPULimitBytes is not uploaded at all: it waits (its
            // measured size is remembered) until that much is free. Tracked requests and observed RSS,
            // not physical VRAM; memory a cell makes while it resolves (terrain meshes) is not predicted.
            bool EvictOnPressure = false;
            uint32_t PressureCooldownFrames = 120;
            uint32_t NavigationMode = 0; // 0 off, 2 XY grid, 3 XZ height grid
            uint32_t NavigationMaxCells = 262144;
            bool AsyncNavigation = false;
            uint32_t NavigationQueryWorkers = 2, NavigationQueryCapacity = 64;
            uint32_t NavigationQueryStarts = 4, NavigationQueryResults = 8;
            std::vector<NavTraversalLink> NavigationLinks;
            uint32_t CacheEntriesPerKind = 128, MaxCacheEvictionsPerFrame = 8;
            // ---- cell assets (textures, models, the textures of materials). Off: the main thread loads
            // them while the cell resolves, as before these budgets existed (older worlds are unchanged).
            // On: a cell's files are decoded / parsed by staging workers under the file and byte budgets
            // below and uploaded by AsyncLoader::Update - at most MaxAssetUploadsPerFrame upload calls and
            // MaxAssetUploadBytesPerFrame bytes per Update (the host calls it once a frame). The cell is
            // activated only when all of them are on the GPU. A cell that exceeds a budget fails with the
            // file's name; a missing / broken file is reported and the cell lives without it. The upload
            // budget is the process's while the world runs (it is AsyncLoader's).
            bool AssetStreaming = false;
            uint32_t MaxAssetFilesPerCell = 256, MaxAssetUploadsPerFrame = 16;
            uint64_t MaxAssetFileBytes = 64ull * 1024 * 1024;         // one source file on disk
            uint64_t MaxAssetBytesPerCell = 256ull * 1024 * 1024;     // decoded pixels + mesh data of one cell
            uint64_t MaxStagedAssetBytes = 512ull * 1024 * 1024;      // all cells being staged / uploaded
            uint64_t MaxAssetUploadBytesPerFrame = 4ull * 1024 * 1024; // also the largest mesh a cell may have
            // Tracked GPU bytes cache trimming may release per frame (one entry always may go): freeing large
            // GPU objects is main-thread work as well.
            uint64_t MaxAssetReleaseBytesPerFrame = 32ull * 1024 * 1024;
            std::vector<std::string> Layers{ "main" };
            bool Validate(std::string& error) const;
        };
        struct CellInfo
        {
            UUID ID{ 0 };
            WorldRuntimeStage Stage = WorldRuntimeStage::Dormant;
            bool Desired = false, Admitted = false;
            uint64_t Generation = 0;
            size_t Entities = 0;
            std::string Error;
            // Tracked GPU bytes its staged assets asked for when they were last measured (0: unknown).
            uint64_t MeasuredGPUBytes = 0;
            bool Held = false; // kept out by a residency limit
        };
        enum class CommandKind
        {
            Capture,
            Save,
            Restore,
            Reset,
            Adopt,
            Remove
        };
        struct Command
        {
            CommandKind Kind = CommandKind::Capture;
            UUID Cell{ 0 }, Entity{ 0 };
            std::filesystem::path File;
        };
        struct CommandResult
        {
            uint64_t Serial = 0;
            CommandKind Kind = CommandKind::Capture;
            bool Success = false;
            std::string Error;
        };
        // Script-safe owner-thread queue: no plugin callbacks are retained. FIFO, at most 64
        // pending and four executed per tick. Results emit WorldCommandCompleted on the world bus.
        uint64_t Enqueue(Command command, std::string& error);
        struct CellEvent
        {
            UUID ID{ 0 };
            std::string ScenePath;
        };
        struct Stats
        {
            size_t Desired = 0, Admitted = 0, Active = 0, Building = 0, Failed = 0, BudgetBlocked = 0;
            uint64_t Entities = 0, DeclaredCPU = 0, DeclaredGPU = 0, Activations = 0, Unloads = 0;
            double LastMilliseconds = 0, PeakMilliseconds = 0;
            WorldCellLoader::Stats Loader;
            AssetResidency::Snapshot Residency;
            bool MemoryPressure = false;
            uint64_t CacheEvictions = 0, PressureFrames = 0;
            size_t NavigationTiles = 0, NavigationCells = 0;
            uint64_t NavigationRebuilds = 0;
            size_t AtlasCells = 0, AtlasBytes = 0, ActiveNavigationLinks = 0;
            std::string AtlasError;
            NavQueryQueue::Stats NavigationQueries;
            uint64_t NavigationStaleResults = 0;
            std::string NavigationError;
            size_t CheckpointCells = 0, CheckpointBytes = 0;
            uint64_t Captures = 0, Restores = 0;
            size_t PendingCommands = 0;
            uint64_t CompletedCommands = 0, FailedCommands = 0;
            bool CaptureOnUnload = false, RestorePending = false;
            std::string CheckpointError;
            // ---- cell assets (Policy::AssetStreaming). The decode / parse counters are this world's main
            // thread since it started: they stay 0 unless a file had to be loaded outside the staging path.
            bool AssetStreaming = false;
            WorldAssetStager::Stats Assets;
            AsyncLoader::Stats Uploads; // since the world started
            size_t PendingUploads = 0;
            uint64_t PendingUploadBytes = 0;
            uint64_t MainThreadImageDecodes = 0, MainThreadModelParses = 0;
            uint64_t AssetCancellations = 0, AssetRestages = 0;
            // ---- residency limits (Policy::EvictOnPressure, RequestedGPULimitBytes with AssetStreaming).
            uint64_t PressureEvictions = 0;  // cells retired to get back under a limit
            uint64_t ResidencyRefusals = 0;  // staged cells not uploaded because they would pass the GPU limit
            size_t ResidencyBlocked = 0;     // cells held out right now (cooling down, or too large for what is free)
            uint64_t MemoryReleases = 0;     // times freed heap memory was handed back to the system under an RSS limit
            double PeakMemoryReleaseMilliseconds = 0; // on its helper thread, not in a frame
        };
        static bool LoadDefinition(const std::filesystem::path& file, WorldManifest& manifest, Policy& policy, std::string& error);
        static bool SaveDefinition(const std::filesystem::path& file, const WorldManifest& manifest, const Policy& policy, std::string& error);
        ~WorldStreaming();
        const WorldManifest& GetManifest() const;
        const Policy& GetPolicy() const;
        std::vector<CellInfo> GetCells() const;
        Stats GetStats() const;
        glm::dvec3 GetViewer() const;
        // Host between frames: a deterministic camera path / tool can override the render camera.
        void SetViewerOverride(const glm::dvec3& viewer);
        void ClearViewerOverride();
        void RetryFailures();
        // Host between frames, honors the same per-frame eviction allowance.
        size_t TrimCaches();
        // Owner thread after carving, before agent commands. Does not grant queue start credits.
        void SynchronizeNavigation();
        UUID GetNavigationEntity(int dimension) const;
        // Owner thread publishes snapshots; retained immutable snapshots are safe on workers.
        std::shared_ptr<const NavAtlas> GetNavigationAtlas() const;
        NavRoute FindNavigationRoute(glm::vec2 from, glm::vec2 to, const NavPathOptions& options = {}) const;
        // Owner thread, applied at the next world update; routes become stale on publication.
        bool SetNavigationLinks(std::vector<NavTraversalLink> links, std::string& error);
        // Owner-thread async agent queries. Poll only exposes Pump-published results.
        uint64_t SubmitNavigationQuery(UUID entity, glm::vec2 from, glm::vec2 to, const NavPathOptions& options, std::string& error);
        bool PollNavigationQuery(UUID entity, NavQueryQueue::Result& result);
        bool HasNavigationQuery(UUID entity) const;
        void CancelNavigationQuery(UUID entity);
        // Host between frames. Checkpoints contain serialized components; not a physics replay.
        void SetCaptureOnUnload(bool enabled);
        bool CaptureCell(UUID cell, std::string& error);
        bool SaveCheckpoint(const std::filesystem::path& file, std::string& error);
        bool RequestRestoreCheckpoint(const std::filesystem::path& file, std::string& error);
        bool RequestResetToAuthored(std::string& error);
        std::vector<UUID> GetCellEntities(UUID cell) const;
        bool AdoptEntity(UUID cell, UUID entity, std::string& error);
        bool RemoveCellEntity(UUID cell, UUID entity, std::string& error);
        bool IsAnchor(const Scene* scene) const;
        static const char* StageName(WorldRuntimeStage stage);

    private:
        friend class SceneManager;
        struct Data;
        explicit WorldStreaming(std::unique_ptr<Data> data);
        static std::unique_ptr<WorldStreaming> Create(SceneManager& manager, const WorldManifest& manifest,
                                                      const Policy& policy, const std::filesystem::path& assets, std::string& error);
        void Update(SceneManager& manager);
        void Clear(SceneManager& manager);
        std::unique_ptr<Data> m_Data;
    };
}
