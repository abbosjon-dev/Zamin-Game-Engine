#pragma once

#include "Zamin/Scene/WorldCellLoader.h"
#include "Zamin/Core/Base.h"

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
