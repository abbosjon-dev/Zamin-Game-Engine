#include <Zamin/Zamin.h>
#include <Zamin/Scene/TerrainFoliage.h>
#include <Zamin/Scene/Terrain.h>
#include "TestAssertions.h"
#include <limits>
using namespace Zamin;
namespace {
    struct Forest
    {
        Ref<Scene> World = CreateRef<Scene>();
        Entity Ground;
        Forest()
        {
            Ground = World->CreateEntity("Ground");
            Ground.AddComponent<TerrainComponent>().Reset(5, { 4, 4 }, 3);
        }
        Entity Tree(glm::vec3 position, bool painted = true)
        {
            auto root = World->CreateEntity("Tree");
            World->SetParent(root, Ground, false);
            root.GetTransform().Translation = position;
            if (painted) root.AddComponent<TagsComponent>().Add("TerrainPainted");
            return root;
        }
    };
}
TEST(TerrainFoliageReprojectsTaggedRootsAndKeepsHierarchy)
{
    Forest f;
    auto tree = f.Tree({ 1, 20, 2 });
    tree.GetTransform().Scale = { 2, 3, 4 };
    tree.GetTransform().Rotation.y = .7f;
    auto child = f.World->CreateEntity("Crown");
    f.World->SetParent(child, tree, false);
    child.GetTransform().Translation = { 0, 2, 0 };
    auto manual = f.Tree({ 2, 30, 2 }, false);
    auto result = TerrainFoliage::Reproject(*f.World, f.Ground);
    CHECK(result.Error.empty() && result.Visited == 1 && result.Changed == 1);
    CHECK(tree.GetTransform().Translation == glm::vec3(1, 3, 2));
    CHECK(tree.GetTransform().Scale == glm::vec3(2, 3, 4));
    CHECK_NEAR(tree.GetTransform().Rotation.y, .7f, 1e-6f);
    CHECK(child.GetTransform().Translation == glm::vec3(0, 2, 0));
    CHECK(manual.GetTransform().Translation.y == 30);
    CHECK(TerrainFoliage::Reproject(*f.World, f.Ground).Changed == 0);
}
TEST(TerrainFoliageRegionOffsetBoundsAndEditorLocks)
{
    Forest f;
    auto near = f.Tree({ 1, 20, 1 }), far = f.Tree({ 3, 20, 3 }), outside = f.Tree({ 5, 20, 1 }), locked = f.Tree({ 1, 20, 1 }), hidden = f.Tree({ 1, 20, 1 });
    f.World->SetLockedInEditor(locked, true);
    f.World->SetHiddenInEditor(hidden, true);
    TerrainReprojectOptions options;
    options.Center = { 1, 1 };
    options.Radius = .5f;
    options.HeightOffset = 2;
    auto result = TerrainFoliage::Reproject(*f.World, f.Ground, options);
    CHECK(result.Changed == 1 && result.Skipped == 4);
    CHECK(near.GetTransform().Translation.y == 5);
    CHECK(far.GetTransform().Translation.y == 20);
    CHECK(outside.GetTransform().Translation.y == 20);
    CHECK(locked.GetTransform().Translation.y == 20);
    CHECK(hidden.GetTransform().Translation.y == 20);
}
TEST(TerrainFoliageAlignsWorldNormalUnderNonuniformParent)
{
    Forest f;
    auto& t = f.Ground.GetComponent<TerrainComponent>();
    for (uint32_t z = 0; z < 5; ++z)
        for (uint32_t x = 0; x < 5; ++x) t.Heights[z * 5 + x] = float(x) + 2;
    t.GeometryChanged();
    f.Ground.GetTransform().Scale = { 2, 3, 1 };
    f.Ground.GetTransform().Rotation = { .2f, .5f, .1f };
    auto tree = f.Tree({ 2, 10, 2 });
    tree.GetTransform().Rotation.y = .9f;
    TerrainReprojectOptions options;
    options.AlignToNormal = true;
    auto result = TerrainFoliage::Reproject(*f.World, f.Ground, options);
    CHECK(result.Changed == 1);
    auto up = glm::normalize(glm::vec3(f.World->GetWorldTransform(tree)[1]));
    auto n = glm::normalize(glm::transpose(glm::inverse(glm::mat3(f.World->GetWorldTransform(f.Ground)))) * TerrainTools::SampleNormal(t, 2, 2));
    CHECK(glm::dot(up, n) > .9999f);
}
TEST(TerrainFoliageRejectsInvalidRuntimeAndSingularInputs)
{
    Forest f;
    auto tree = f.Tree({ 1, 20, 1 });
    TerrainReprojectOptions options;
    options.HeightOffset = std::numeric_limits<float>::quiet_NaN();
    CHECK(!TerrainFoliage::Reproject(*f.World, f.Ground, options).Error.empty());
    CHECK(tree.GetTransform().Translation.y == 20);
    options = {};
    f.Ground.GetTransform().Scale.x = 0;
    CHECK(!TerrainFoliage::Reproject(*f.World, f.Ground, options).Error.empty());
    f.Ground.GetTransform().Scale.x = 1;
    auto foreign = CreateRef<Scene>();
    CHECK(!TerrainFoliage::Reproject(*foreign, f.Ground).Error.empty());
    f.World->OnRuntimeStart();
    CHECK(!TerrainFoliage::Reproject(*f.World, f.Ground).Error.empty());
    f.World->OnRuntimeStop();
}
TEST(TerrainFoliageReprojectSerializesAndCopiesIndependently)
{
    Forest f;
    auto tree = f.Tree({ 1, 30, 2 });
    CHECK(TerrainFoliage::Reproject(*f.World, f.Ground).Changed == 1);
    auto snapshot = SceneSerializer(f.World).SerializeToString();
    auto restored = CreateRef<Scene>();
    CHECK(SceneSerializer(restored).DeserializeFromString(snapshot));
    CHECK(restored->GetEntityByUUID(tree.GetUUID()).GetTransform().Translation == tree.GetTransform().Translation);
    auto clone = Scene::Copy(f.World);
    auto copyGround = clone->GetEntityByUUID(f.Ground.GetUUID());
    auto& t = copyGround.GetComponent<TerrainComponent>();
    for (auto& h : t.Heights) h = 8;
    t.GeometryChanged();
    CHECK(TerrainFoliage::Reproject(*clone, copyGround).Changed == 1);
    CHECK(f.World->GetEntityByUUID(tree.GetUUID()).GetTransform().Translation.y == 3);
    CHECK(clone->GetEntityByUUID(tree.GetUUID()).GetTransform().Translation.y == 8);
}
