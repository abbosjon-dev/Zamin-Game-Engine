// Tiled terrain: tile files and levels, streaming within a memory budget with distance LOD, crack-free edges
// between tiles of different LOD, sculpting across tile borders, migration from a single terrain. CPU only.
#include "TestAssertions.h"
#include <Zamin/Scene/TerrainLOD.h>
#include <Zamin/Scene/TerrainStreaming.h>
#include <Zamin/Scene/Scene.h>
#include <Zamin/Scene/SceneSerializer.h>
#include <Zamin/Scene/Entity.h>
#include <Zamin/Utils/FileSystem.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cstdio>
#include <map>
#include <set>

using namespace Zamin;

namespace {
    struct TileWorld
    {
        TempDirectory Temp{ "zamin-tiles" };
        std::filesystem::path File;
        TerrainTileSet Set;
        explicit TileWorld(uint32_t tiles = 6, uint32_t resolution = 65, float size = 32.0f, uint32_t foliage = 12)
        {
            File = Temp.GetPath() / "World.zterrain";
            Set.TilesX = Set.TilesZ = tiles;
            Set.TileResolution = resolution;
            Set.TileSize = size;
            TerrainTools::WorldTerrainSettings world;
            world.Amplitude = 10;
            world.Frequency = 0.03f;
            std::string error;
            CHECK(TerrainTileTools::Create(File, Set, world, foliage, error));
            CHECK(error.empty());
        }
        TiledTerrainComponent Component() const
        {
            TiledTerrainComponent c;
            c.Path = File.string(); // absolute: no project in the unit tests
            return c;
        }
    };
    TerrainStreamingView At(glm::vec3 camera)
    {
        TerrainStreamingView view;
        view.Camera = camera;
        view.Synchronous = true;
        return view;
    }
    // The boundary edges (used by one triangle) of a tile's patch meshes, in the component's space, keyed by
    // their rounded end points.
    using Segment = std::pair<std::pair<int64_t, int64_t>, std::pair<int64_t, int64_t>>;
    std::set<std::tuple<int64_t, int64_t, int64_t, int64_t, int64_t, int64_t>> BorderSegments(const TerrainComponent& t, const glm::mat4& local, const glm::vec3& camera,
                                                                                               int axis, float coordinate)
    {
        auto patches = TerrainTools::BuildPatchLayout(t);
        TerrainTools::SelectPatchLODs(patches, t, local, camera);
        std::map<std::pair<glm::ivec3, glm::ivec3>, int> edges;
        auto key = [](glm::vec3 p) { return glm::ivec3(glm::round(p * 1000.0f)); };
        std::set<std::tuple<int64_t, int64_t, int64_t, int64_t, int64_t, int64_t>> out;
        for (const auto& patch : patches)
        {
            std::vector<MeshVertex> vertices;
            std::vector<uint32_t> indices;
            TerrainTools::BuildPatchMesh(t, patch, vertices, indices);
            for (size_t i = 0; i < indices.size(); i += 3)
                for (int e = 0; e < 3; e++)
                {
                    glm::vec3 a = glm::vec3(local * glm::vec4(vertices[indices[i + e]].Position, 1));
                    glm::vec3 b = glm::vec3(local * glm::vec4(vertices[indices[i + (e + 1) % 3]].Position, 1));
                    if (std::abs(a[axis] - coordinate) > 1e-3f || std::abs(b[axis] - coordinate) > 1e-3f) continue;
                    auto ka = key(a), kb = key(b);
                    if (std::tie(ka.x, ka.y, ka.z) > std::tie(kb.x, kb.y, kb.z)) std::swap(ka, kb);
                    out.insert({ ka.x, ka.y, ka.z, kb.x, kb.y, kb.z });
                }
        }
        return out;
    }
}

TEST(TerrainTileFileLevelsAndDamage)
{
    TempDirectory temp("zamin-tile-file");
    TerrainTileData full;
    full.Resolution = 33;
    for (uint32_t z = 0; z < 33; z++)
        for (uint32_t x = 0; x < 33; x++)
        {
            full.Heights.push_back(float(x) * 0.5f + float(z));
            full.Weights.push_back(x < 16 ? glm::vec4(1, 0, 0, 0) : glm::vec4(0, 0, 1, 0));
        }
    full.Foliage = { { { 1, 2, 3 }, { 0, 45, 0 }, 1.5f }, { { 4, 5, 6 }, { 0, 90, 0 }, 0.5f } };
    const auto file = temp.GetPath() / "t/0_0.ztile";
    std::string error;
    CHECK(TerrainTileIO::Write(file, full, 4, &error));
    for (uint32_t level = 0; level < 4; level++)
    {
        TerrainTileData read;
        CHECK(TerrainTileIO::Read(file, level, level == 0, read, &error));
        const uint32_t r = (32 >> level) + 1, step = 1u << level;
        CHECK(read.Level == level && read.Resolution == r && read.Heights.size() == size_t(r) * r);
        // Decimation: level k keeps every 2^k-th sample exactly (shared edges stay identical between tiles).
        for (uint32_t z = 0; z < r; z++)
            for (uint32_t x = 0; x < r; x++)
                CHECK(read.Heights[size_t(z) * r + x] == full.Heights[size_t(z * step) * 33 + x * step]);
        CHECK(read.Weights[r - 1] == glm::vec4(0, 0, 1, 0));
        CHECK(read.Foliage.size() == (level == 0 ? 2u : 0u));
        CHECK(read.FileBytes < 24 + 80 + uint64_t(r) * r * 8 + 100);
    }
    TerrainTileData f;
    CHECK(TerrainTileIO::Read(file, 0, true, f) && f.Foliage[1].Position == glm::vec3(4, 5, 6) && f.Foliage[0].Scale == 1.5f);
    // Damaged and foreign files are refused with a reason.
    auto bytes = FileSystem::ReadBinaryFile(file);
    bytes.resize(bytes.size() / 2);
    CHECK(FileSystem::WriteBinaryFile(file, bytes.data(), bytes.size()));
    CHECK(!TerrainTileIO::Read(file, 0, true, f, &error) && !error.empty());
    FileSystem::WriteTextFile(file, "not a tile");
    CHECK(!TerrainTileIO::Read(file, 0, false, f, &error));
    CHECK(!TerrainTileIO::Read(temp.GetPath() / "none.ztile", 0, false, f, &error));
}

TEST(TerrainTileSetCreateAndSharedSamples)
{
    TileWorld world(3, 33, 16.0f, 5);
    TerrainTileSet loaded;
    std::string error;
    CHECK(TerrainTileSet::Load(world.File, loaded, &error));
    CHECK(loaded.TileCount() == 9 && loaded.UsableLevels() == 4 && loaded.FoliageCounts.size() == 9);
    // Neighbouring tiles share their edge samples bit for bit (absolute-position generator).
    for (uint32_t z = 0; z < 3; z++)
        for (uint32_t x = 0; x + 1 < 3; x++)
        {
            TerrainTileData a, b;
            CHECK(TerrainTileIO::Read(loaded.TileFile(world.File, x, z), 0, false, a));
            CHECK(TerrainTileIO::Read(loaded.TileFile(world.File, x + 1, z), 0, false, b));
            for (uint32_t k = 0; k < 33; k++) CHECK(a.Heights[size_t(k) * 33 + 32] == b.Heights[size_t(k) * 33]);
        }
    TerrainTileSet bad = loaded;
    bad.TileResolution = 100;
    CHECK(!bad.Validate().empty());
    CHECK(!TerrainTileSet::FromJson("{\"Version\": 99}", bad, &error));
}

TEST(TerrainStreamingBudgetAndDistanceLOD)
{
    TileWorld world(8, 65, 32.0f, 8);
    auto c = world.Component();
    c.LoadRadius = 400;
    c.UnloadRadius = 450;
    c.LODDistance = 40;
    c.MaxLOD = 3;
    c.MaxLoadsPerFrame = 64;
    c.MemoryBudgetMB = 64;
    TerrainStreaming::Update(c, At({ 16, 30, 16 }));
    const auto& stats = c.Streamer->Stats;
    CHECK(stats.Error.empty());
    for (int i = 0; i < 4; i++) TerrainStreaming::Update(c, At({ 16, 30, 16 }));
    CHECK(stats.Resident == 64);
    // Near the camera full detail, farther coarser: the corner tile sits at LOD 0, the far corner at the coarsest.
    auto level = [&](uint32_t x, uint32_t z) { return c.Streamer->Tiles[z * 8 + x].Level; };
    CHECK(level(0, 0) == 0 && level(7, 7) == 3 && level(3, 0) >= 1);
    CHECK(stats.LODCounts[0] >= 1 && stats.LODCounts[3] >= 1);
    // A tight budget: the farthest tiles coarsened first, then dropped; resident bytes never above it.
    c.MemoryBudgetMB = 2;
    c.MaxLOD = 1; // coarsening alone cannot fit every tile: the farthest are dropped too
    uint64_t worst = 0;
    for (int frame = 0; frame < 40; frame++)
    {
        const glm::vec3 camera(16.0f + frame * 5.0f, 30, 16.0f + frame * 3.0f);
        TerrainStreaming::Update(c, At(camera));
        worst = std::max(worst, stats.ResidentBytes + stats.InFlightBytes);
        CHECK(stats.ResidentBytes <= stats.BudgetBytes);
    }
    CHECK(worst <= (2ull << 20));
    CHECK(stats.OverBudgetDrops > 0 && stats.Coarsened > 0 && stats.Resident < 64);
    // The tile under the camera is still there at full detail (nearest first).
    const glm::vec2 last(16.0f + 39 * 5.0f, 16.0f + 39 * 3.0f);
    CHECK(level(uint32_t(last.x / 32), uint32_t(last.y / 32)) == 0);
    std::printf("    Streaming: 2 MiB budget, %u resident, %llu bytes, %llu drops, %llu coarsened, %llu loads, %llu unloads\n", stats.Resident,
                (unsigned long long)stats.ResidentBytes, (unsigned long long)stats.OverBudgetDrops, (unsigned long long)stats.Coarsened,
                (unsigned long long)stats.TotalLoads, (unsigned long long)stats.TotalUnloads);
    // Loads per frame are limited.
    TerrainStreaming::Close(c);
    c.MemoryBudgetMB = 64;
    c.MaxLOD = 3;
    c.MaxLoadsPerFrame = 3;
    TerrainStreaming::Update(c, At({ 128, 30, 128 }));
    CHECK(c.Streamer->Stats.LoadsThisFrame <= 3 && c.Streamer->Stats.Resident <= 3);
    // Unload radius: a camera far away unloads everything.
    for (int i = 0; i < 30; i++) TerrainStreaming::Update(c, At({ 128, 30, 128 }));
    CHECK(c.Streamer->Stats.Resident == 64);
    for (int i = 0; i < 3; i++) TerrainStreaming::Update(c, At({ 5000, 30, 5000 }));
    CHECK(c.Streamer->Stats.Resident == 0 && c.Streamer->Stats.TotalUnloads >= 64);
}

TEST(TerrainStreamingAsyncReads)
{
    TileWorld world(4, 33, 16.0f, 4);
    auto c = world.Component();
    c.MaxLoadsPerFrame = 2;
    TerrainStreamingView view;
    view.Camera = { 8, 20, 8 };
    for (int frame = 0; frame < 200 && (!c.Streamer || c.Streamer->Stats.Resident < 16); frame++)
    {
        TerrainStreaming::Update(c, view);
        TerrainStreaming::WaitIdle(c);
        CHECK(c.Streamer->Stats.LoadsThisFrame <= 2);
    }
    CHECK(c.Streamer->Stats.Resident == 16 && c.Streamer->Stats.Loading == 0);
    CHECK(c.Streamer->Stats.IOMilliseconds > 0.0 && c.Streamer->Stats.BytesRead > 0);
    // A once-per-frame guard: the same frame number does nothing the second time.
    view.Frame = 7;
    TerrainStreaming::Update(c, view);
    const uint64_t loads = c.Streamer->Stats.TotalLoads;
    view.Camera = { 5000, 0, 5000 };
    TerrainStreaming::Update(c, view);
    CHECK(c.Streamer->Stats.TotalLoads == loads && c.Streamer->Stats.Resident == 16);
    // Closing with reads in flight is safe (the reader drops results of a closed streamer).
    view.Frame = 0;
    view.Camera = { 8, 20, 8 };
    TerrainStreaming::Close(c);
    TerrainStreaming::Update(c, view);
    TerrainStreaming::Close(c);
    TerrainStreaming::WaitIdle(c);
}

TEST(TerrainStreamingSeamsBetweenLODs)
{
    TileWorld world(4, 65, 32.0f, 0);
    auto c = world.Component();
    c.LoadRadius = 1000;
    c.UnloadRadius = 1000;
    c.LODDistance = 20;
    c.MaxLOD = 3;
    c.MaxLoadsPerFrame = 64;
    const glm::vec3 camera(2, 25, 2);
    for (int i = 0; i < 3; i++) TerrainStreaming::Update(c, At(camera));
    auto& tiles = c.Streamer->Tiles;
    std::set<int> levels;
    for (const auto& t : tiles) levels.insert(t.Level);
    CHECK(levels.size() >= 3); // several LODs side by side
    CHECK(TerrainStreaming::MaxSharedEdgeError(c) == 0.0f);
    // Every shared edge: the boundary segments both tiles' meshes put on it are the same (no T-junction gaps).
    uint32_t edgesChecked = 0, mixed = 0;
    for (uint32_t z = 0; z < 4; z++)
        for (uint32_t x = 0; x < 4; x++)
            for (auto [dx, dz] : { std::pair{ 1u, 0u }, std::pair{ 0u, 1u } })
            {
                if (x + dx >= 4 || z + dz >= 4) continue;
                const auto& a = tiles[z * 4 + x];
                const auto& b = tiles[(z + dz) * 4 + x + dx];
                const glm::mat4 la = glm::translate(glm::mat4(1), { x * 32.0f, 0, z * 32.0f });
                const glm::mat4 lb = glm::translate(glm::mat4(1), { (x + dx) * 32.0f, 0, (z + dz) * 32.0f });
                const int axis = dx ? 0 : 2;
                const float coordinate = dx ? (x + 1) * 32.0f : (z + 1) * 32.0f;
                auto sa = BorderSegments(*a.Terrain, la, camera, axis, coordinate), sb = BorderSegments(*b.Terrain, lb, camera, axis, coordinate);
                CHECK(!sa.empty() && sa == sb);
                edgesChecked++;
                mixed += a.Level != b.Level;
            }
    CHECK(edgesChecked == 24 && mixed >= 4);
    std::printf("    Seams: %u shared edges watertight, %u between different LODs\n", edgesChecked, mixed);
    // Without the stitch (EdgeLOD cleared) a fine tile next to a coarse one would not match.
    for (uint32_t z = 0; z < 4; z++)
        for (uint32_t x = 0; x + 1 < 4; x++)
        {
            auto& a = tiles[z * 4 + x];
            auto& b = tiles[z * 4 + x + 1];
            if (b.Level <= a.Level) continue;
            const auto saved = a.Terrain->EdgeLOD;
            a.Terrain->EdgeLOD = {};
            const glm::mat4 la = glm::translate(glm::mat4(1), { x * 32.0f, 0, z * 32.0f }), lb = glm::translate(glm::mat4(1), { (x + 1) * 32.0f, 0, z * 32.0f });
            CHECK(BorderSegments(*a.Terrain, la, camera, 0, (x + 1) * 32.0f) != BorderSegments(*b.Terrain, lb, camera, 0, (x + 1) * 32.0f));
            a.Terrain->EdgeLOD = saved;
            z = 4;
            break;
        }
    CHECK(TerrainTileTools::EdgeLODs(1, { 3, -1, 1, 0 }) == (std::array<uint8_t, 4>{ 2, 0, 0, 0 }));
}

TEST(TerrainStreamingSculptAcrossTileBorders)
{
    TileWorld world(3, 33, 16.0f, 6);
    auto c = world.Component();
    c.LODDistance = 4; // most tiles start coarse: the brush must read them at full detail
    c.MaxLOD = 2;
    for (int i = 0; i < 3; i++) TerrainStreaming::Update(c, At({ 40, 40, 40 }));
    const float before = TerrainStreaming::SampleHeight(c, { 16, 16 });
    TerrainBrushSettings brush;
    brush.Tool = TerrainBrush::Raise;
    brush.Radius = 5;
    brush.Strength = 20;
    TerrainStreaming::BeginStroke(c);
    for (int i = 0; i < 5; i++) CHECK(TerrainStreaming::ApplyBrush(c, { 16, 16 }, brush, 0.1f)); // the corner of four tiles
    brush.Tool = TerrainBrush::Smooth;
    CHECK(TerrainStreaming::ApplyBrush(c, { 16, 20 }, brush, 0.1f));
    brush.Tool = TerrainBrush::Paint;
    brush.Layer = 2;
    CHECK(TerrainStreaming::ApplyBrush(c, { 32, 8 }, brush, 0.1f));
    TerrainStreaming::EndStroke(c);
    const float raised = TerrainStreaming::SampleHeight(c, { 16, 16 });
    CHECK(raised > before + 1.0f);
    // The four tiles around the corner and the painted edge's pair are full detail, unsaved and agree on shared samples.
    for (uint32_t i : { 0u, 1u, 3u, 4u }) CHECK(c.Streamer->Tiles[i].Level == 0 && c.Streamer->Tiles[i].Dirty);
    CHECK(TerrainStreaming::MaxSharedEdgeError(c) == 0.0f);
    CHECK(TerrainStreaming::DirtyCount(c) >= 5);
    // Undo / Redo of the stroke.
    CHECK(TerrainStreaming::Undo(c));
    CHECK_NEAR(TerrainStreaming::SampleHeight(c, { 16, 16 }), before, 1e-4f);
    CHECK(TerrainStreaming::Redo(c));
    CHECK_NEAR(TerrainStreaming::SampleHeight(c, { 16, 16 }), raised, 1e-4f);
    // Saved tiles read back the same (all levels rewritten), the set reopened from disk.
    std::string error;
    CHECK(TerrainStreaming::SaveDirty(c, &error) >= 5 && error.empty());
    CHECK(TerrainStreaming::DirtyCount(c) == 0);
    TerrainStreaming::Close(c);
    c.LODDistance = 1000;
    for (int i = 0; i < 3; i++) TerrainStreaming::Update(c, At({ 16, 40, 16 }));
    CHECK_NEAR(TerrainStreaming::SampleHeight(c, { 16, 16 }), raised, 1e-4f);
    CHECK(TerrainStreaming::MaxSharedEdgeError(c) == 0.0f);
    TerrainTileData coarse;
    TerrainTileSet set;
    CHECK(TerrainTileSet::Load(world.File, set));
    CHECK(TerrainTileIO::Read(set.TileFile(world.File, 0, 0), 1, false, coarse) && coarse.Heights.back() == c.Streamer->Tiles[0].Terrain->Heights.back());
    // Raycast through the resident tiles.
    TerrainHit hit;
    CHECK(TerrainStreaming::Raycast(c, { 16, 200, 16 }, { 0, -1, 0 }, hit));
    CHECK_NEAR(hit.Position.y, raised, 0.05f);
}

TEST(TerrainMigrationFromSingleTerrain)
{
    TempDirectory temp("zamin-migrate");
    TerrainComponent source;
    source.Reset(129, { 128, 128 }, 5);
    TerrainTools::Generate(source, 11, 12, 0.03f);
    TerrainBrushSettings paint;
    paint.Tool = TerrainBrush::Paint;
    paint.Layer = 1;
    paint.Radius = 10;
    paint.Strength = 50;
    TerrainTools::ApplyBrush(source, { 70, 70 }, paint, 0.1f);
    source.Layers[0].TexturePath = "Textures/grass.png";
    FoliageComponent foliage;
    FoliageTools::GenerateGrid(foliage, 100, { 120, 120 }, 3);
    TerrainTileSet set;
    std::string error;
    const auto file = temp.GetPath() / "Map.zterrain";
    CHECK(TerrainTileTools::MigrateFromTerrain(source, &foliage, file, 33, set, error));
    CHECK(set.TilesX == 4 && set.TilesZ == 4 && set.TileSize == 32.0f && set.Layers[0].TexturePath == "Textures/grass.png");
    uint32_t foliageTotal = 0;
    for (uint32_t z = 0; z < 4; z++)
        for (uint32_t x = 0; x < 4; x++)
        {
            TerrainTileData tile;
            CHECK(TerrainTileIO::Read(set.TileFile(file, x, z), 0, true, tile));
            foliageTotal += uint32_t(tile.Foliage.size());
            // Exact copy: every sample equals the single terrain's sample at the same place.
            for (uint32_t j = 0; j < 33; j += 4)
                for (uint32_t i = 0; i < 33; i += 4)
                {
                    const size_t from = size_t(z * 32 + j) * 129 + x * 32 + i;
                    CHECK(tile.Heights[size_t(j) * 33 + i] == source.Heights[from]);
                    CHECK(glm::length(tile.Weights[size_t(j) * 33 + i] - source.Weights[from]) < 0.01f);
                }
            for (const auto& f : tile.Foliage) CHECK(f.Position.x >= 0 && f.Position.x <= 32 && f.Position.z >= 0 && f.Position.z <= 32);
        }
    CHECK(foliageTotal == foliage.Instances.size());
    // A size that does not split evenly is resampled onto whole tiles (corners kept).
    TerrainComponent odd;
    odd.Reset(100, { 99, 99 }, 3);
    TerrainTools::Generate(odd, 5, 6, 0.05f);
    CHECK(TerrainTileTools::MigrateFromTerrain(odd, nullptr, temp.GetPath() / "Odd.zterrain", 33, set, error));
    CHECK(set.TilesX == 3);
    TerrainTileData corner;
    CHECK(TerrainTileIO::Read(set.TileFile(temp.GetPath() / "Odd.zterrain", 0, 0), 0, false, corner) && corner.Heights[0] == odd.Heights[0]);
    TerrainComponent wide;
    wide.Reset(65, { 64, 32 }, 1);
    CHECK(!TerrainTileTools::MigrateFromTerrain(wide, nullptr, temp.GetPath() / "Wide.zterrain", 33, set, error) && !error.empty());
    // The component round-trips through a scene file; a copy streams on its own.
    auto scene = CreateRef<Scene>();
    auto e = scene->CreateEntity("Tiled");
    auto& c = e.AddComponent<TiledTerrainComponent>();
    c.Path = "Maps/Map.zterrain";
    c.LoadRadius = 250;
    c.UnloadRadius = 300;
    c.MemoryBudgetMB = 48;
    c.ShowTileLOD = true;
    c.MaxLOD = 2;
    auto loaded = CreateRef<Scene>();
    const std::string text = SceneSerializer(scene).SerializeToString();
    CHECK(SceneSerializer(loaded).DeserializeFromString(text));
    auto restored = loaded->GetEntityByUUID(e.GetUUID());
    CHECK(restored.HasComponent<TiledTerrainComponent>());
    const auto& r = restored.GetComponent<TiledTerrainComponent>();
    CHECK(r.Path == c.Path && r.LoadRadius == 250 && r.UnloadRadius == 300 && r.MemoryBudgetMB == 48 && r.ShowTileLOD && r.MaxLOD == 2);
    c.Streamer = CreateRef<TiledTerrainComponent::Runtime>();
    TiledTerrainComponent copy = c;
    CHECK(!copy.Streamer);
}
