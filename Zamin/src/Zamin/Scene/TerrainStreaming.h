#pragma once

/// @file
/// @brief Tiled terrain: the .zterrain tile set on disk, background tile IO, the streamer (budget, LOD, stitching).

#include "Zamin/Scene/Foliage.h"
#include "Zamin/Scene/Terrain.h"
#include "Zamin/Scene/TerrainTiles.h"

#include <array>
#include <mutex>
#include <filesystem>
#include <string>
#include <vector>

namespace Zamin {

    // The .zterrain file: the grid of tiles, their shared material layers and foliage look. Every tile is its own
    // file in the tile folder ("<name>.tiles/<x>_<z>.ztile") with its heights, control weights and foliage at
    // every stored LOD level (level k: every 2^k-th sample, so shared edges keep exactly the same samples).
    struct TerrainTileSet
    {
        static constexpr int CurrentVersion = 1;
        static constexpr const char* Extension = ".zterrain";

        uint32_t TilesX = 4, TilesZ = 4;
        uint32_t TileResolution = 129;   // samples per tile side (2^n + 1, 9..257)
        float TileSize = 64.0f;          // m per tile side
        float HeightLimit = 128.0f;
        uint32_t LODLevels = 4;          // stored levels (0 = full)
        std::array<TerrainLayer, 4> Layers = TerrainComponent().Layers;
        // The foliage look of every tile (its instances live in the tiles).
        PrimitiveMesh FoliageNearType = PrimitiveMesh::Cone, FoliageFarType = PrimitiveMesh::Cube;
        std::string FoliageNearModel, FoliageFarModel;
        glm::vec4 FoliageTint{ .3f, .6f, .18f, 1 };
        FoliageSettings Foliage;
        std::string TilesFolder;         // next to the .zterrain ("<name>.tiles")
        std::vector<uint32_t> FoliageCounts; // per tile (z * TilesX + x): the streamer plans memory before reading

        uint32_t TileCount() const { return TilesX * TilesZ; }
        uint32_t LevelResolution(uint32_t level) const { return ((TileResolution - 1) >> level) + 1; }
        // The levels a tile can have (stops before a level would have fewer than 3 samples a side).
        uint32_t UsableLevels() const;
        std::filesystem::path TileFile(const std::filesystem::path& terrainFile, uint32_t x, uint32_t z) const;
        // Problems with the values (empty: valid).
        std::string Validate() const;

        std::string ToJson() const;
        static bool FromJson(const std::string& text, TerrainTileSet& out, std::string* error = nullptr);
        static bool Load(const std::filesystem::path& file, TerrainTileSet& out, std::string* error = nullptr);
        bool Save(const std::filesystem::path& file) const;
    };

    // One tile at one LOD level, in memory.
    struct TerrainTileData
    {
        uint32_t Level = 0, Resolution = 0;
        std::vector<float> Heights;          // Resolution^2, m (tile-local)
        std::vector<glm::vec4> Weights;      // Resolution^2 control weights
        std::vector<FoliageInstance> Foliage; // tile-local positions
        uint64_t FileBytes = 0;              // bytes read from disk for it
    };

    namespace TerrainTileIO {
        // Writes a tile from its full-detail data: every level, decimated (edges keep their exact samples).
        bool Write(const std::filesystem::path& file, const TerrainTileData& full, uint32_t levels, std::string* error = nullptr);
        // Reads one level (only that level's bytes; the foliage only when asked).
        bool Read(const std::filesystem::path& file, uint32_t level, bool foliage, TerrainTileData& out, std::string* error = nullptr);
        // The level `level` of full data (decimation), as Write stores it.
        TerrainTileData Decimate(const TerrainTileData& full, uint32_t level);
    }

    namespace TerrainTileTools {
        // A new tile set on disk: heights from the deterministic world generator, the first layer everywhere and
        // `foliagePerTile` instances per tile (seeded, kept off steep slopes). Overwrites the tile folder.
        bool Create(const std::filesystem::path& terrainFile, TerrainTileSet& set, const TerrainTools::WorldTerrainSettings& world,
                    uint32_t foliagePerTile, std::string& error);
        // Migration from the single-terrain format: `source` (and the packed foliage that stands on it, if any)
        // split into tiles of `tileResolution` samples. When (source.Resolution - 1) is a multiple of
        // (tileResolution - 1) the samples are copied exactly; otherwise the field is resampled bilinearly onto the
        // nearest whole number of tiles. Layers, height limit and foliage go with it.
        bool MigrateFromTerrain(const TerrainComponent& source, const FoliageComponent* foliage, const std::filesystem::path& terrainFile,
                                uint32_t tileResolution, TerrainTileSet& out, std::string& error);
        // The estimated bytes of a resident tile at a level (CPU heights + weights + GPU control/mesh + foliage).
        uint64_t EstimateBytes(const TerrainTileSet& set, uint32_t level, uint32_t foliage);
        // Edge LODs of a tile from its own and its four neighbours' levels (-1: neighbour not resident).
        std::array<uint8_t, 4> EdgeLODs(int self, const std::array<int, 4>& neighbours);
    }

    struct TerrainStreamingStats
    {
        uint32_t Tiles = 0, Resident = 0, Loading = 0, Wanted = 0;
        uint32_t LoadsThisFrame = 0, UnloadsThisFrame = 0, PeakLoadsPerFrame = 0, LODChanges = 0;
        uint64_t ResidentBytes = 0, BudgetBytes = 0, PeakResidentBytes = 0, InFlightBytes = 0;
        uint64_t TotalLoads = 0, TotalUnloads = 0, BytesRead = 0, OverBudgetDrops = 0, Coarsened = 0, Evictions = 0;
        std::array<uint32_t, 8> LODCounts{};
        uint32_t Colliders = 0, FoliageTiles = 0, DirtyTiles = 0;
        double UpdateMilliseconds = 0.0, IOMilliseconds = 0.0; // main thread this frame; worker total
        std::string Error;
    };

    // The streaming state of one TiledTerrainComponent (its Streamer). Main thread, except the IO worker's reads.
    struct TiledTerrainComponent::Runtime
    {
        struct Tile
        {
            int Level = -1, Requested = -1;  // resident / being read (-1: none)
            Ref<TerrainComponent> Terrain;
            Ref<FoliageComponent> Foliage;
            uint64_t Bytes = 0, RequestedBytes = 0;
            float Distance = 0.0f;
            bool Dirty = false;              // edited, not saved (pinned at full detail)
            uint64_t Generation = 0;
            uint32_t BodyId = 0xffffffff;    // its static collider in the scene's 3D physics (PhysicsWorld3D::NoBody: none)
            const TerrainComponent* BodyTerrain = nullptr;
            uint64_t BodyRevision = 0;
        };
        struct Completed
        {
            uint32_t Index = 0;
            uint64_t Generation = 0;
            bool Ok = false;
            TerrainTileData Data;
            std::string Error;
            double Milliseconds = 0.0;
        };
        std::filesystem::path File;          // the .zterrain on disk
        std::string OpenedPath;
        TerrainTileSet Set;
        std::vector<Tile> Tiles;
        TerrainStreamingStats Stats;
        uint64_t LastUpdateFrame = 0;
        bool DebugLayers = false;            // the tiles show their LOD colours (ShowTileLOD)
        // Completed reads, filled by the IO worker.
        std::mutex CompletedMutex;
        std::vector<Completed> CompletedReads;
        // Sculpt undo: one entry per stroke, the full-detail tiles it touched as they were before.
        struct UndoTile
        {
            uint32_t Index = 0;
            std::vector<float> Heights;
            std::vector<glm::vec4> Weights;
        };
        std::vector<std::vector<UndoTile>> UndoStack, RedoStack;
        bool InStroke = false;
    };

    // The policy of one Update.
    struct TerrainStreamingView
    {
        glm::mat4 World{ 1.0f };
        glm::vec3 Camera{ 0.0f };   // world space
        bool Runtime = false;       // play / Player (colliders)
        bool Synchronous = false;   // read on this thread (tests, headless captures)
        uint64_t Frame = 0;         // the application's frame: one update per frame however many views draw (0: always)
    };

    class TerrainStreaming
    {
    public:
        // Opens (or re-opens when Path changed) the tile set and streams the tiles around the camera: plans the
        // levels within the budget (nearest first, farthest coarsened first, dropped last), starts reads, makes the
        // finished ones resident, stitches edges. At most once per frame (Frame); returns the stats.
        static const TerrainStreamingStats& Update(TiledTerrainComponent& component, const TerrainStreamingView& view);
        // The resident tiles to draw: their terrain and their local transform (tile origin) in the component.
        struct DrawTile
        {
            const TerrainComponent* Terrain = nullptr;
            FoliageComponent* Foliage = nullptr;
            glm::mat4 Local{ 1.0f };
            int Level = 0;
            uint32_t X = 0, Z = 0;
        };
        static std::vector<DrawTile> GetDrawTiles(const TiledTerrainComponent& component, const glm::vec3& cameraLocal, float foliageDistance);
        // Drops every tile (unsaved edits too) and the open set.
        static void Close(TiledTerrainComponent& component);
        // Waits until no read of this component is in flight (tests, save).
        static void WaitIdle(TiledTerrainComponent& component);

        // ---- editing (main thread, Edit mode)
        // A ray in the component's local space against the resident tiles (full detail where loaded).
        static bool Raycast(const TiledTerrainComponent& component, const glm::vec3& origin, const glm::vec3& direction, TerrainHit& hit);
        static float SampleHeight(const TiledTerrainComponent& component, glm::vec2 local, bool* resident = nullptr);
        // A brush stamp at a local X/Z: every tile it reaches is made resident at full detail (read now when it is
        // not), edited with its tile-local centre, then the shared edges and corners are made equal again.
        static bool ApplyBrush(TiledTerrainComponent& component, glm::vec2 center, const TerrainBrushSettings& brush, float seconds);
        static void BeginStroke(TiledTerrainComponent& component);
        static void EndStroke(TiledTerrainComponent& component);
        static bool Undo(TiledTerrainComponent& component);
        static bool Redo(TiledTerrainComponent& component);
        // Writes the edited tiles (all their levels) and the .zterrain. Returns how many tiles were written.
        static size_t SaveDirty(TiledTerrainComponent& component, std::string* error = nullptr);
        static size_t DirtyCount(const TiledTerrainComponent& component);
        // The largest height difference between two resident neighbours' shared samples (0 when stitched right).
        static float MaxSharedEdgeError(const TiledTerrainComponent& component);
    };

}
