#include "TestAssertions.h"
#include <Zamin/Asset/CacheResidency.h>
#include <Zamin/AI/NavMosaic.h>
#include <Zamin/AI/NavPathfinder.h>
#include <Zamin/Scene/TerrainTiles.h>
#include <Zamin/Scene/TerrainLOD.h>
#include <Zamin/Scene/Scene.h>
#include <Zamin/Scene/Entity.h>
#include <Zamin/Scene/SceneSerializer.h>
#include <glm/gtc/matrix_transform.hpp>
#include <limits>
#include <memory>
using namespace Zamin;
namespace {
    struct Resource
    {
        bool Ready = true;
        uint64_t Bytes = 10;
    };
    using Cache = std::unordered_map<std::string, std::shared_ptr<Resource>>;
    constexpr auto Ready = [](const auto& value) { return value->Ready; };
    constexpr auto Bytes = [](const auto& value) { return value->Bytes; };
    std::vector<const NavGrid*> Tiles(const NavGrid& a, const NavGrid& b) { return { &a, &b }; }
}
TEST(ResidencyLRUPreservesLiveOwnersAndLoading)
{
    Cache cache;
    CacheRecency<std::string> lru;
    for (const auto* key : { "old", "live", "loading", "recent" })
    {
        cache[key] = std::make_shared<Resource>();
        lru.Touch(key);
    }
    auto live = cache.at("live");
    cache.at("loading")->Ready = false;
    auto stats = lru.Inspect(cache, Ready, Bytes);
    CHECK(stats.Entries == 4 && stats.Pinned == 2 && stats.Loading == 1 && stats.RequestedBytes == 40);
    auto keys = lru.Select(cache, 0, 0, 1, Ready, Bytes);
    CHECK(keys.size() == 1 && keys[0] == "old");
    cache.erase(keys[0]);
    keys = lru.Select(cache, 0, 0, 8, Ready, Bytes);
    CHECK(keys.size() == 1 && keys[0] == "recent");
    live.reset();
    cache.at("loading")->Ready = true;
    CHECK(lru.Select(cache, 0, 0, 8, Ready, Bytes).size() == 3);
}
TEST(ResidencyLRUTouchAndBudgetSemantics)
{
    Cache cache;
    CacheRecency<std::string> lru;
    cache["a"] = std::make_shared<Resource>();
    lru.Touch("a");
    cache["b"] = std::make_shared<Resource>();
    lru.Touch("b");
    lru.Touch("a");
    auto keys = lru.Select(cache, 1, UINT64_MAX, 10, Ready, Bytes);
    CHECK(keys.size() == 1 && keys[0] == "b");
    CHECK(lru.Select(cache, 9, 15, 10, Ready, Bytes).size() == 1);
    CHECK(lru.Select(cache, 0, 0, 0, Ready, Bytes).empty());
    CHECK(lru.Select(cache, 2, 20, 10, Ready, Bytes).empty());
    cache.erase("a");
    cache["missing"] = nullptr;
    CHECK(lru.Select(cache, 0, 0, 10, Ready, Bytes).size() == 2);
}
TEST(ResidencyPressureHysteresisAndDisabledSignals)
{
    ResidencyPressure pressure;
    CHECK(!pressure.Update(99, true, 0, 100, 0, .8));
    CHECK(pressure.Update(100, true, 0, 100, 0, .8));
    CHECK(pressure.Update(90, true, 0, 100, 0, .8));
    CHECK(pressure.Update(80, true, 0, 100, 0, .8));
    CHECK(!pressure.Update(79, true, 0, 100, 0, .8));
    CHECK(pressure.Update(1, true, 0, 1, 0, .1));
    CHECK(!pressure.Update(0, true, 0, 1, 0, .1));
    CHECK(!pressure.Update(999, false, 99, 100, 100, .8));
    CHECK(pressure.Update(0, false, 100, 100, 100, .8));
    CHECK(!pressure.Update(UINT64_MAX, true, UINT64_MAX, 0, 0, .8));
    CHECK(pressure.Update(UINT64_MAX, true, 0, UINT64_MAX, 0, .99));
    CHECK(!pressure.Update(0, true, 0, 100, 0, std::numeric_limits<double>::quiet_NaN()));
}
TEST(NavMosaicCrossTilePathAndUnloadHole)
{
    NavGrid a(4, 4, 1, { 0, 0 }), b(4, 4, 1, { 4, 0 }), c(4, 4, 1, { 8, 0 }), mosaic;
    std::string error;
    std::vector<const NavGrid*> tiles{ &a, &b, &c };
    CHECK(BuildNavMosaic(tiles, mosaic, error));
    NavPathfinder finder;
    NavPathResult path;
    NavPathOptions options;
    options.SnapRadius = 0;
    CHECK(finder.FindPath(mosaic, glm::vec2(.5f, .5f), glm::vec2(11.5f, .5f), path, options) == NavPathStatus::Complete);
    const auto serial = mosaic.GetSerial();
    tiles = { &a, &c };
    CHECK(BuildNavMosaic(tiles, mosaic, error));
    CHECK(mosaic.GetSerial() != serial);
    CHECK(!mosaic.IsWalkable(4, 0) && mosaic.IsWalkable(8, 0));
    CHECK(finder.FindPath(mosaic, glm::vec2(.5f, .5f), glm::vec2(11.5f, .5f), path, options) != NavPathStatus::Complete);
}
TEST(NavMosaicHeightCostCarveAndClimb)
{
    NavGrid a(2, 2, 1, { 0, 0 }), b(2, 2, 1, { 2, 0 }), mosaic;
    a.EnableHeights();
    b.EnableHeights();
    a.SetMaxClimb(.4f);
    b.SetMaxClimb(.4f);
    b.SetFloorHeight(0, 0, 2);
    b.SetCost(1, 1, 7);
    a.Carve({ 0 });
    std::string error;
    CHECK(BuildNavMosaic(Tiles(a, b), mosaic, error));
    CHECK(mosaic.HasHeights() && !mosaic.IsWalkable(0, 0));
    CHECK_NEAR(mosaic.GetCost(3, 1), 7, 1e-6f);
    CHECK_NEAR(mosaic.GetFloorHeight(2, 0), 2, 1e-6f);
    CHECK(!mosaic.CanStep(1, 0, 2, 0));
    a.ClearCarves();
    CHECK(BuildNavMosaic(Tiles(a, b), mosaic, error));
    CHECK(mosaic.IsWalkable(0, 0));
}
TEST(NavMosaicRejectsOverlapsAlignmentAndHugeBoundsTransactionally)
{
    NavGrid a(2, 2, 1), b(2, 2, 1, { 1, 0 }), output(1, 1);
    output.SetCost(0, 0, 8);
    const auto serial = output.GetSerial();
    std::string error;
    CHECK(!BuildNavMosaic(Tiles(a, b), output, error));
    CHECK(output.GetWidth() == 1 && output.GetCost(0, 0) == 8 && output.GetSerial() == serial);
    b.Reset(2, 2, 1, { 2.5f, 0 });
    CHECK(!BuildNavMosaic(Tiles(a, b), output, error));
    b.Reset(2, 2, 2, { 2, 0 });
    CHECK(!BuildNavMosaic(Tiles(a, b), output, error));
    b.Reset(2, 2, 1, { 1e8f, 0 });
    CHECK(!BuildNavMosaic(Tiles(a, b), output, error));
    b.Reset(2, 2, 1, { 2, 0 });
    CHECK(!BuildNavMosaic(Tiles(a, b), output, error, 7));
    CHECK(BuildNavMosaic(Tiles(a, b), output, error, 8));
    b.EnableHeights();
    CHECK(!BuildNavMosaic(Tiles(a, b), output, error));
    CHECK(!BuildNavMosaic({}, output, error));
}
TEST(NavMosaicNegativeOriginAndInputOrder)
{
    NavGrid a(3, 2, .5f, { -3, -2 }), b(3, 2, .5f, { -1.5f, -2 }), x, y;
    std::string error;
    CHECK(BuildNavMosaic(Tiles(a, b), x, error));
    CHECK(BuildNavMosaic(Tiles(b, a), y, error));
    CHECK(x.GetOrigin() == y.GetOrigin() && x.GetSize() == y.GetSize());
    CHECK(x.CountWalkable() == 12 && y.CountWalkable() == 12);
}
TEST(WorldTerrainContinuousGeneratorAndRangeRollback)
{
    TerrainComponent a, b;
    a.Reset(17, { 32, 32 });
    b.Reset(33, { 32, 32 });
    TerrainTools::WorldTerrainSettings settings{ 91, 12, 5, .02f };
    std::string error;
    CHECK(TerrainTools::GenerateWorldTile(a, { -32, 0, -32 }, settings, error));
    CHECK(TerrainTools::GenerateWorldTile(b, { 0, 3, -32 }, settings, error));
    CHECK(a.LockBorderLOD && b.LockBorderLOD);
    for (uint32_t i = 0; i < a.Resolution; ++i)
        CHECK_NEAR(a.Heights[size_t(i) * 17 + 16], b.Heights[size_t(i * 2) * 33] + 3, 2e-6f);
    const auto before = a.Heights;
    const auto revision = a.GeometryRevision;
    settings.Base = -100;
    CHECK(!TerrainTools::GenerateWorldTile(a, { 0, 0, 0 }, settings, error));
    CHECK(a.Heights == before && a.GeometryRevision == revision);
}
TEST(WorldTerrainStitchUnequalResolutionAllDirections)
{
    for (bool x : { false, true })
        for (bool reverse : { false, true })
        {
            TerrainComponent a, b;
            a.Reset(9, { 32, 32 }, 2);
            b.Reset(17, { 32, 32 }, 8);
            glm::dvec3 oa(0), ob(x ? 32 : 0, 3, x ? 0 : 32);
            if (reverse)
            {
                oa = ob;
                ob = { 0, 0, 0 };
            }
        // Deliberately different fine edge waves must collapse onto the common coarse polyline.
            for (size_t i = 0; i < b.Heights.size(); ++i) b.Heights[i] += float(i % 3) * .1f;
            std::string error;
            CHECK(TerrainTools::StitchTiles(a, oa, b, ob, 2, error));
            for (int i = 0; i <= 64; ++i)
            {
                const float u = float(i) * .5f;
                const float ax = x ? (reverse ? 0 : 32) : u, az = x ? u : (reverse ? 0 : 32);
                const float bx = x ? (reverse ? 32 : 0) : u, bz = x ? u : (reverse ? 32 : 0);
                CHECK_NEAR(TerrainTools::SampleHeight(a, ax, az) + float(oa.y), TerrainTools::SampleHeight(b, bx, bz) + float(ob.y), 3e-6f);
            }
            CHECK(a.LockBorderLOD && b.LockBorderLOD);
        }
}
TEST(WorldTerrainStitchFailureIsAtomic)
{
    TerrainComponent a, b;
    a.Reset(9, { 32, 32 }, 2);
    b.Reset(10, { 32, 32 }, 8);
    const auto aa = a.Heights, bb = b.Heights;
    std::string error;
    CHECK(!TerrainTools::StitchTiles(a, { 0, 0, 0 }, b, { 32, 0, 0 }, 2, error));
    CHECK(a.Heights == aa && b.Heights == bb);
    b.Reset(9, { 32, 32 }, 8);
    CHECK(!TerrainTools::StitchTiles(a, { 0, 0, 0 }, b, { 32, 0, 1 }, 2, error));
    CHECK(!TerrainTools::StitchTiles(a, { 0, 0, 0 }, a, { 32, 0, 0 }, 2, error));
    CHECK(!TerrainTools::StitchTiles(a, { 0, 0, 0 }, b, { 32, 200, 0 }, 2, error));
    b.Heights[0] = std::numeric_limits<float>::quiet_NaN();
    CHECK(!TerrainTools::StitchTiles(a, { 0, 0, 0 }, b, { 32, 0, 0 }, 2, error));
    CHECK(a.Heights == aa && a.GeometryRevision == 2);
}
TEST(WorldTerrainBorderLODAndSerialization)
{
    TerrainComponent terrain;
    terrain.Reset(257, { 256, 256 });
    terrain.LockBorderLOD = true;
    auto patches = TerrainTools::BuildPatchLayout(terrain);
    TerrainTools::SelectPatchLODs(patches, terrain, glm::mat4(1), { 128, 100000, 128 });
    bool interiorReduced = false;
    for (const auto& p : patches)
    {
        if (p.X == 0 || p.Z == 0 || p.X + p.CellsX == 256 || p.Z + p.CellsZ == 256) CHECK(p.LOD == 0);
        else interiorReduced |= p.LOD > 0;
    }
    CHECK(interiorReduced);
    auto scene = CreateRef<Scene>();
    auto entity = scene->CreateEntity("terrain");
    entity.AddComponent<TerrainComponent>(terrain);
    auto restored = CreateRef<Scene>();
    CHECK(SceneSerializer(restored).DeserializeFromString(SceneSerializer(scene).SerializeToString()));
    CHECK(restored->FindEntityByName("terrain").GetComponent<TerrainComponent>().LockBorderLOD);
    TerrainComponent copy = terrain;
    CHECK(copy.LockBorderLOD && !copy.Runtime);
}

TEST(ResidencyBytePlannerDoesNotLoseOverflowDebt)
{
    Cache cache;
    CacheRecency<std::string> lru;
    cache["a"] = std::make_shared<Resource>();
    cache["b"] = std::make_shared<Resource>();
    cache["a"]->Bytes = UINT64_MAX;
    cache["b"]->Bytes = 1;
    lru.Touch("a");
    lru.Touch("b");
    CHECK(lru.Inspect(cache, Ready, Bytes).RequestedBytes == UINT64_MAX);
    CHECK(lru.Select(cache, 100, 0, 10, Ready, Bytes).size() == 2);
    CHECK(lru.Select(cache, 100, 1, 10, Ready, Bytes).size() == 1);
    CHECK(lru.Select(cache, 100, UINT64_MAX, 10, Ready, Bytes).size() == 1);
}
