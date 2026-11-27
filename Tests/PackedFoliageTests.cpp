#include <Zamin/Zamin.h>
#include <Zamin/Scene/Foliage.h>
#include <Zamin/Scene/Terrain.h>
#include <Zamin/Scene/TerrainFoliage.h>
#include "TestAssertions.h"
#include <limits>
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
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
namespace {
    std::vector<glm::mat4> ForestTransforms(const FoliageComponent& c)
    {
        std::vector<glm::mat4> out;
        for (const auto& i : c.Instances) out.push_back(glm::translate(glm::mat4(1), i.Position) * glm::toMat4(glm::quat(i.Rotation)) * glm::scale(glm::mat4(1), glm::vec3(i.Scale)));
        return out;
    }
    const AABB kUnitBox{ { -.5f, -.5f, -.5f }, { .5f, .5f, .5f } };
}
TEST(PackedFoliageHLODBakeIsDeterministic)
{
    FoliageComponent c;
    FoliageTools::GenerateGrid(c, 4096, { 128, 128 }, 7);
    const auto transforms = ForestTransforms(c);
    const auto a = FoliageHLOD::Bake(transforms, kUnitBox, 32), b = FoliageHLOD::Bake(transforms, kUnitBox, 32);
    CHECK(a.IsValid() && a.Hash == b.Hash);
    CHECK(a.Cells.size() == 16 && a.Dimensions == glm::ivec2(4, 4) && a.CellSize == 32);
    CHECK(a.RebakedCells == 16 && a.ReusedCells == 0);
    CHECK(a.Vertices.size() == b.Vertices.size() && a.Indices == b.Indices);
    CHECK(std::memcmp(a.Vertices.data(), b.Vertices.data(), a.Vertices.size() * sizeof(MeshVertex)) == 0);
    // Every instance is in exactly one cell, and the cells' counts add up.
    uint32_t total = 0;
    for (const auto& cell : a.Cells)
    {
        total += cell.Instances;
        CHECK(cell.IndexCount > 0 && cell.IndexCount <= FoliageHLOD::ClumpsPerSide * FoliageHLOD::ClumpsPerSide * 12);
        CHECK(cell.FirstIndex + cell.IndexCount <= a.Indices.size());
    }
    CHECK(total == 4096 && a.InstanceCell.size() == 4096);
    for (size_t i = 0; i < transforms.size(); ++i)
    {
        const auto& cell = a.Cells[a.InstanceCell[i]];
        const auto box = TransformAABB(kUnitBox, transforms[i]);
        CHECK(cell.Bounds.Min.x <= box.Min.x && cell.Bounds.Max.x >= box.Max.x && cell.Bounds.Min.z <= box.Min.z && cell.Bounds.Max.z >= box.Max.z);
    }
    // The proxy stays inside its cell's instance bounds; the whole proxy is far cheaper than the instances.
    for (const auto& cell : a.Cells)
        for (uint32_t k = 0; k < cell.VertexCount; ++k)
        {
            const auto& p = a.Vertices[cell.FirstVertex + k].Position;
            CHECK(p.x >= cell.Bounds.Min.x - 1e-4f && p.x <= cell.Bounds.Max.x + 1e-4f && p.y >= cell.Bounds.Min.y - 1e-4f && p.y <= cell.Bounds.Max.y + 1e-4f);
        }
    CHECK(a.Indices.size() / 3 <= 16 * 16 * 4);
    // A different seed (another forest) bakes differently.
    FoliageComponent other;
    FoliageTools::GenerateGrid(other, 4096, { 128, 128 }, 8);
    CHECK(FoliageHLOD::Bake(ForestTransforms(other), kUnitBox, 32).Hash != a.Hash);
    CHECK(!FoliageHLOD::Bake({}, kUnitBox, 32).IsValid());
}
TEST(PackedFoliageHLODInvalidatesOnlyEditedCells)
{
    FoliageComponent c;
    FoliageTools::GenerateGrid(c, 4096, { 128, 128 }, 7);
    auto transforms = ForestTransforms(c);
    const auto original = FoliageHLOD::Bake(transforms, kUnitBox, 32);
    // A paint edit in one corner cell: raise, remove and add instances there.
    FoliageComponent edited = c;
    for (auto& i : edited.Instances)
        if (i.Position.x < 30 && i.Position.z < 30) i.Position.y += 1.5f;
    edited.Instances.push_back({ { 10, 0, 12 }, { 0, 0, 0 }, 1.2f });
    auto editedTransforms = ForestTransforms(edited);
    const auto rebaked = FoliageHLOD::Bake(editedTransforms, kUnitBox, 32, &original);
    CHECK(rebaked.Hash != original.Hash);
    CHECK(rebaked.RebakedCells == 1 && rebaked.ReusedCells == 15);
    // The reused cells are byte-identical to a full bake of the edited source (the cache cannot drift).
    const auto full = FoliageHLOD::Bake(editedTransforms, kUnitBox, 32);
    CHECK(full.Hash == rebaked.Hash && full.Indices == rebaked.Indices);
    // Undoing the edit restores the original bake from the cache again.
    const auto restored = FoliageHLOD::Bake(transforms, kUnitBox, 32, &rebaked);
    CHECK(restored.Hash == original.Hash && restored.RebakedCells == 1 && restored.ReusedCells == 15);
    // Another cell size is a different lattice: everything is rebuilt.
    const auto coarse = FoliageHLOD::Bake(transforms, kUnitBox, 64, &original);
    CHECK(coarse.Cells.size() == 4 && coarse.RebakedCells == 4 && coarse.ReusedCells == 0);
}
TEST(PackedFoliageHLODCellLimitAndFadeLevels)
{
    // A 4 km strip with 4 m cells would be 1000x1 = fine; 4 km x 4 km is capped at MaxCells by growing the cell.
    std::vector<glm::mat4> transforms;
    for (int z = 0; z <= 4000; z += 40)
        for (int x = 0; x <= 4000; x += 40) transforms.push_back(glm::translate(glm::mat4(1), glm::vec3(float(x), 0, float(z))));
    const auto bake = FoliageHLOD::Bake(transforms, kUnitBox, 4);
    CHECK(bake.Cells.size() <= FoliageHLOD::MaxCells && uint64_t(bake.Dimensions.x) * bake.Dimensions.y <= FoliageHLOD::MaxCells);
    CHECK(bake.CellSize >= 64 && bake.CellSize == 4 * float(1 << int(std::log2(bake.CellSize / 4))));
    CHECK(FoliageHLOD::FadeLevel(99, 100, 10) == 0);
    CHECK(FoliageHLOD::FadeLevel(100, 100, 10) == 0);
    CHECK(FoliageHLOD::FadeLevel(105, 100, 10) == 8);
    CHECK(FoliageHLOD::FadeLevel(110, 100, 10) == FoliageHLOD::FadeLevels);
    CHECK(FoliageHLOD::FadeLevel(500, 100, 10) == FoliageHLOD::FadeLevels);
    CHECK(FoliageHLOD::FadeLevel(100, 100, 0) == FoliageHLOD::FadeLevels && FoliageHLOD::FadeLevel(99.9f, 100, 0) == 0);
    CHECK(FoliageHLOD::FadeLevel(std::numeric_limits<float>::quiet_NaN(), 100, 10) == 0);
}
TEST(PackedFoliageHLODSettingsSanitizeAndSerialize)
{
    FoliageSettings s;
    CHECK(!s.HLODEnabled());
    s.LODDistance = 30;
    s.CullDistance = 200;
    s.HLODDistance = 500;
    s.HLODFade = -3;
    s.HLODCellSize = 1;
    s.Sanitize();
    CHECK(s.HLODDistance == 200 && s.HLODFade == 0 && s.HLODCellSize == 4 && s.HLODEnabled());
    s.HLODDistance = 10;
    s.Sanitize();
    CHECK(s.HLODDistance == 30);
    s.HLODDistance = std::numeric_limits<float>::infinity();
    s.HLODCellSize = std::numeric_limits<float>::quiet_NaN();
    s.Sanitize();
    CHECK(s.HLODDistance == 0 && s.HLODCellSize == 32 && !s.HLODEnabled());
    auto scene = CreateRef<Scene>();
    auto e = scene->CreateEntity("Forest");
    auto& c = e.AddComponent<FoliageComponent>();
    FoliageTools::GenerateGrid(c, 16, { 8, 8 });
    c.Settings.Backend = FoliageBackend::GPU;
    c.Settings.LODDistance = 25;
    c.Settings.CullDistance = 400;
    c.Settings.HLODDistance = 150;
    c.Settings.HLODFade = 12;
    c.Settings.HLODCellSize = 48;
    const auto text = SceneSerializer(scene).SerializeToString();
    auto loaded = CreateRef<Scene>();
    CHECK(SceneSerializer(loaded).DeserializeFromString(text));
    Entity re = loaded->GetEntityByUUID(e.GetUUID());
    const auto& r = re.GetComponent<FoliageComponent>();
    CHECK(r.Settings.Backend == FoliageBackend::GPU && r.Settings.HLODDistance == 150 && r.Settings.HLODFade == 12 && r.Settings.HLODCellSize == 48);
    CHECK(Scene::Copy(scene)->GetEntityByUUID(e.GetUUID()).GetComponent<FoliageComponent>().Settings.HLODDistance == 150);
    // A scene saved before HLOD existed loads with HLOD off.
    auto old = text;
    for (const char* key : { "\"HLODDistance\"", "\"HLODFade\"", "\"HLODCellSize\"" })
    {
        auto at = old.find(key);
        CHECK(at != std::string::npos);
        auto end = old.find_first_of(",}", at);
        old.erase(at, end - at + (old[end] == ',' ? 1 : 0));
    }
    auto legacy = CreateRef<Scene>();
    CHECK(SceneSerializer(legacy).DeserializeFromString(old));
    Entity le = legacy->GetEntityByUUID(e.GetUUID());
    const auto& l = le.GetComponent<FoliageComponent>();
    CHECK(l.Settings.HLODDistance == 0 && l.Settings.HLODCellSize == 32 && l.Settings.HLODFade == 8);
}
TEST(PackedFoliageCPUBoxTestMatchesFrustum)
{
    // The batch's center/extent test (the one the GPU shader makes) agrees with Frustum::Intersects.
    const auto frustum = Frustum::FromViewProjection(glm::perspective(glm::radians(60.f), 1.5f, .1f, 200.f) * glm::lookAt(glm::vec3(3, 9, 4), glm::vec3(40, 0, 50), glm::vec3(0, 1, 0)));
    uint32_t state = 7, agree = 0, inside = 0;
    for (int i = 0; i < 4000; ++i)
    {
        auto next = [&] { state = state * 1664525u + 1013904223u; return float(state >> 8) / float(1 << 24); };
        const glm::vec3 c{ next() * 200 - 60, next() * 40 - 10, next() * 200 - 60 }, e{ .2f + next() * 3, .2f + next() * 6, .2f + next() * 3 };
        const bool a = FoliageInsideFrustum(frustum, c, e), b = frustum.Intersects(AABB::FromCenterExtents(c, e));
        agree += a == b;
        inside += a;
    }
    CHECK(agree == 4000 && inside > 200 && inside < 3800);
}
