#include <Zamin/Zamin.h>
#include <Zamin/Scene/Foliage.h>
#include <Zamin/Scene/Terrain.h>
#include <Zamin/Scene/TerrainFoliage.h>
#include "TestAssertions.h"
#include <limits>
using namespace Zamin;
TEST(PackedFoliageDistanceFrustumAndHysteresis)
{
    FoliageSettings settings;
    settings.LODDistance = 10;
    settings.CullDistance = 30;
    settings.Hysteresis = .1f;
    Frustum open;
    uint32_t state = 2;
    CHECK(ClassifyFoliage(AABB::FromCenterExtents({ 9, 0, 0 }, { .5f, .5f, .5f }), open, { 0, 0, 0 }, settings, state) == 0);
    CHECK(ClassifyFoliage(AABB::FromCenterExtents({ 10.5f, 0, 0 }, { .5f, .5f, .5f }), open, { 0, 0, 0 }, settings, state) == 0);
    CHECK(ClassifyFoliage(AABB::FromCenterExtents({ 12, 0, 0 }, { .5f, .5f, .5f }), open, { 0, 0, 0 }, settings, state) == 1);
    CHECK(ClassifyFoliage(AABB::FromCenterExtents({ 9.5f, 0, 0 }, { .5f, .5f, .5f }), open, { 0, 0, 0 }, settings, state) == 1);
    CHECK(ClassifyFoliage(AABB::FromCenterExtents({ 8, 0, 0 }, { .5f, .5f, .5f }), open, { 0, 0, 0 }, settings, state) == 0);
    CHECK(ClassifyFoliage(AABB::FromCenterExtents({ 32, 0, 0 }, { .5f, .5f, .5f }), open, { 0, 0, 0 }, settings, state) == 2);
    Frustum clipped;
    clipped.Planes[0] = { 1, 0, 0, 0 };
    state = 2;
    CHECK(ClassifyFoliage(AABB::FromCenterExtents({ -4, 0, 0 }, { 1, 1, 1 }), clipped, { 0, 0, 0 }, settings, state) == 2);
    CHECK(ClassifyFoliage(AABB::FromCenterExtents({ -1, 0, 0 }, { 1, 1, 1 }), clipped, { 0, 0, 0 }, settings, state) < 2);
}
TEST(PackedFoliageValidationAndDeterministicPlacement)
{
    FoliageComponent a, b;
    FoliageTools::GenerateGrid(a, 4096, { 128, 64 }, 42);
    FoliageTools::GenerateGrid(b, 4096, { 128, 64 }, 42);
    CHECK(a.Instances == b.Instances);
    CHECK(a.Instances.size() == 4096);
    a.Instances[0].Scale = std::numeric_limits<float>::quiet_NaN();
    a.Settings.Hysteresis = 9;
    a.Settings.LODDistance = -1;
    a.Settings.CullDistance = 0;
    a.Sanitize();
    CHECK(a.Instances[0].Scale == 1);
    CHECK(a.Settings.Hysteresis == .4f);
    CHECK(a.Settings.LODDistance == 1 && a.Settings.CullDistance == 1);
    auto copy = a;
    CHECK(copy.Instances == a.Instances && !copy.Runtime);
    copy.Instances[0].Position.y = 20;
    CHECK(a.Instances[0].Position.y == 0);
}
TEST(PackedFoliageSaveLoadCopyAndBounds)
{
    auto scene = CreateRef<Scene>();
    auto e = scene->CreateEntity("Forest");
    auto& c = e.AddComponent<FoliageComponent>();
    FoliageTools::GenerateGrid(c, 64, { 16, 16 });
    c.NearType = PrimitiveMesh::Model;
    c.NearModel = "Models/near.obj";
    c.FarType = PrimitiveMesh::Model;
    c.FarModel = "Models/far.obj";
    c.Settings.Backend = FoliageBackend::CPU;
    c.Settings.LODDistance = 17;
    c.Settings.CullDistance = 80;
    auto loaded = CreateRef<Scene>();
    CHECK(SceneSerializer(loaded).DeserializeFromString(SceneSerializer(scene).SerializeToString()));
    auto restored = loaded->GetEntityByUUID(e.GetUUID());
    auto& r = restored.GetComponent<FoliageComponent>();
    CHECK(r.Instances == c.Instances);
    CHECK(r.NearModel == c.NearModel && r.FarModel == c.FarModel);
    CHECK(r.Settings.LODDistance == 17 && r.Settings.Backend == FoliageBackend::CPU);
    CHECK(!r.Runtime);
    AABB bounds;
    CHECK(scene->ComputeBounds({ e }, bounds, false));
    CHECK(bounds.GetSize().x > 10 && bounds.GetSize().z > 10);
    auto clone = Scene::Copy(scene);
    auto ce = clone->GetEntityByUUID(e.GetUUID());
    CHECK(ce.GetComponent<FoliageComponent>().Instances == c.Instances);
}
TEST(PackedFoliageProjectsWithoutBreakingRootTransform)
{
    TerrainComponent t;
    t.Reset(5, { 4, 4 }, 3);
    FoliageComponent c;
    c.Instances = { { { 1, 50, 1 }, { 0, .7f, 0 }, 2 }, { { 10, 50, 10 }, { 0, 0, 0 }, 1 } };
    auto matrix = glm::translate(glm::mat4(1), glm::vec3(0, 1, 0));
    CHECK(FoliageTools::ProjectToTerrain(c, t, matrix, 1) == 1);
    CHECK_NEAR(c.Instances[0].Position.y, 3, 1e-6f);
    CHECK(c.Instances[0].Scale == 2 && c.Instances[0].Rotation.y == .7f);
    CHECK(c.Instances[1].Position.y == 50);
    CHECK(FoliageTools::ProjectToTerrain(c, t, matrix, 1) == 0);
    auto scene = CreateRef<Scene>();
    auto ground = scene->CreateEntity("Ground");
    ground.AddComponent<TerrainComponent>() = t;
    auto forest = scene->CreateEntity("Packed Forest");
    scene->SetParent(forest, ground, false);
    forest.AddComponent<TagsComponent>().Add("TerrainPainted");
    forest.AddComponent<FoliageComponent>() = c;
    auto before = forest.GetTransform().Translation;
    CHECK(TerrainFoliage::Reproject(*scene, ground).Changed == 0);
    CHECK(forest.GetTransform().Translation == before);
}
