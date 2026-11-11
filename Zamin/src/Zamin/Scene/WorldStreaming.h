#pragma once

#include "Zamin/Scene/WorldCellLoader.h"
#include "Zamin/AI/NavAtlas.h"
#include "Zamin/Core/Base.h"
#include "Zamin/Asset/AssetResidency.h"

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
        Failed
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
            uint32_t NavigationMode = 0; // 0 off, 2 XY grid, 3 XZ height grid
            uint32_t NavigationMaxCells = 262144;
            std::vector<NavTraversalLink> NavigationLinks;
            uint32_t CacheEntriesPerKind = 128, MaxCacheEvictionsPerFrame = 8;
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
            std::string NavigationError;
            size_t CheckpointCells = 0, CheckpointBytes = 0;
            uint64_t Captures = 0, Restores = 0;
            size_t PendingCommands = 0;
            uint64_t CompletedCommands = 0, FailedCommands = 0;
            bool CaptureOnUnload = false, RestorePending = false;
            std::string CheckpointError;
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
        UUID GetNavigationEntity(int dimension) const;
        // Owner thread publishes snapshots; retained immutable snapshots are safe on workers.
        std::shared_ptr<const NavAtlas> GetNavigationAtlas() const;
        NavRoute FindNavigationRoute(glm::vec2 from, glm::vec2 to, const NavPathOptions& options = {}) const;
        // Owner thread, applied at the next world update; routes become stale on publication.
        bool SetNavigationLinks(std::vector<NavTraversalLink> links, std::string& error);
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
