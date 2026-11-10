#include <Zamin/Zamin.h>
#include <Zamin/Scene/Terrain.h>
#include <Zamin/Asset/AssetReferences.h>
#include <Zamin/Project/AssetFiles.h>
#include <Zamin/Utils/FileSystem.h>
#include "TestFramework.h"
#include <cmath>
#include <filesystem>
#include <limits>
#include <nlohmann/json.hpp>
using namespace Zamin;
namespace {
    struct TerrainTest
    {
        TerrainTest(const char* name, std::function<void()> fn) { ZaminTest::Registry().push_back({ name, std::move(fn) }); }
    };
#define TEST(name)                       \
    void name();                         \
    TerrainTest reg_##name(#name, name); \
    void name()
#define CHECK(v)                                                  \
    do {                                                          \
        if (!(v)) ZaminTest::CheckFailed(__FILE__, __LINE__, #v); \
    } while (false)
#define NEAR(a, b, e) CHECK(std::abs((a) - (b)) <= (e))
    TEST(TerrainBrushesAndNormalizedPaint)
    {
        TerrainComponent t;
        t.Reset(33, { 32, 32 }, 8);
        TerrainBrushSettings b;
        b.Radius = 4;
        b.Strength = 10;
        CHECK(TerrainTools::ApplyBrush(t, { 16, 16 }, b, 0.1f));
        NEAR(t.Heights[16 * 33 + 16], 9, 1e-5f);
        NEAR(t.Heights[0], 8, 1e-6f);
        b.Tool = TerrainBrush::Lower;
        CHECK(TerrainTools::ApplyBrush(t, { 16, 16 }, b, 0.1f));
        NEAR(t.Heights[16 * 33 + 16], 8, 1e-5f);
        b.Tool = TerrainBrush::Flatten;
        b.TargetHeight = 20;
        TerrainTools::ApplyBrush(t, { 16, 16 }, b, 0.1f);
        NEAR(t.Heights[16 * 33 + 16], 20, 1e-5f);
        b.Tool = TerrainBrush::Smooth;
        TerrainTools::ApplyBrush(t, { 16, 16 }, b, 0.1f);
        CHECK(t.Heights[16 * 33 + 16] < 20);
        b.Tool = TerrainBrush::Paint;
        b.Layer = 2;
        CHECK(TerrainTools::ApplyBrush(t, { 16, 16 }, b, 0.1f));
        auto w = t.Weights[16 * 33 + 16];
        NEAR(w.x + w.y + w.z + w.w, 1, 1e-6f);
        CHECK(w.z > 0.9f);
        auto before = t.Heights;
        CHECK(!TerrainTools::ApplyBrush(t, { 100000, 100000 }, b, 0.1f));
        CHECK(before == t.Heights);
        b.Radius = std::numeric_limits<float>::quiet_NaN();
        CHECK(!TerrainTools::ApplyBrush(t, { 0, 0 }, b, 0.1f));
    }
    TEST(TerrainWeightValidationDoesNotDriftAcrossFrames)
    {
        TerrainComponent t;
        t.Reset(3, { 2, 2 });
        t.Weights[0] = { 0.419553995f, 0.473033875f, 0.107019663f, 0.000392500137f };
        t.Weights[1] = { .9f, .8f, .7f, .6f };
        t.EnsureSize();
        auto stable = t.Weights;
        for (int frame = 0; frame < 120; ++frame) t.EnsureSize();
        CHECK(t.Weights == stable);
        for (auto w : t.Weights) NEAR(w.x + w.y + w.z + w.w, 1, 1e-6f);
    }
    TEST(TerrainGeometryRaycastAndSeeds)
    {
        TerrainComponent t;
        t.Reset(17, { 16, 16 }, 3);
        std::vector<MeshVertex> vertices;
        std::vector<uint32_t> indices;
        TerrainTools::BuildMesh(t, vertices, indices);
        CHECK(vertices.size() == 289);
        CHECK(indices.size() == 16 * 16 * 6);
        auto a = vertices[indices[0]].Position, b = vertices[indices[1]].Position, c = vertices[indices[2]].Position;
        CHECK(glm::cross(b - a, c - a).y > 0);
        TerrainHit hit;
        CHECK(TerrainTools::Raycast(t, { 8, 20, 8 }, { 0, -1, 0 }, hit));
        NEAR(hit.Position.y, 3, 1e-5f);
        NEAR(hit.Distance, 17, 1e-5f);
        CHECK(!TerrainTools::Raycast(t, { -1, 20, 8 }, { 0, -1, 0 }, hit));
        CHECK(!TerrainTools::Raycast(t, { 8, 20, 8 }, { 0, 1, 0 }, hit));
        TerrainTools::Generate(t, 42, 12, 0.1f);
        auto copy = t;
        TerrainTools::Generate(copy, 42, 12, 0.1f);
        CHECK(t.Heights == copy.Heights);
        TerrainTools::Generate(copy, 43, 12, 0.1f);
        CHECK(t.Heights != copy.Heights);
        for (float x : { 0.0f, 1.5f, 8.5f, 16.0f })
        {
            CHECK(TerrainTools::Raycast(t, { x, 30, 7.3f }, { 0, -1, 0 }, hit));
            NEAR(hit.Position.y, TerrainTools::SampleHeight(t, x, 7.3f), 1e-4f);
        }
        CHECK(TerrainTools::Raycast(t, { -10, 20, 8 }, glm::normalize(glm::vec3(1, -0.8f, 0)), hit));
        NEAR(hit.Position.y, TerrainTools::SampleHeight(t, hit.Position.x, hit.Position.z), 1e-3f);
    }
    TEST(TerrainSerializationCopyAndHeightmap)
    {
        auto scene = CreateRef<Scene>();
        auto e = scene->CreateEntity("Test Terrain");
        auto& t = e.AddComponent<TerrainComponent>();
        t.Reset(17, { 20, 30 }, 8);
        TerrainTools::Generate(t, 19, 12, 0.1f);
        TerrainBrushSettings b;
        b.Tool = TerrainBrush::Paint;
        b.Layer = 3;
        TerrainTools::ApplyBrush(t, { 10, 15 }, b, 0.1f);
        t.LODEnabled = false;
        t.LODDistanceScale = 3.5f;
        t.Layers[2].TexturePath = "Textures/stone.png";
        auto collision = TerrainTools::GetCollisionMesh(t);
        CHECK(collision && collision->Indices.size() == 16 * 16 * 6);
        auto copy = t;
        CHECK(!copy.Runtime);
        copy.Heights[0] += 2;
        copy.GeometryChanged();
        CHECK(TerrainTools::GetCollisionMesh(copy) != collision);
        NEAR(t.Heights[0], copy.Heights[0] - 2, 1e-5f);
        std::string snapshot = SceneSerializer(scene).SerializeToString();
        auto loaded = CreateRef<Scene>();
        CHECK(SceneSerializer(loaded).DeserializeFromString(snapshot));
        auto entity = loaded->GetEntityByUUID(e.GetUUID());
        CHECK(entity.HasComponent<TerrainComponent>());
        auto& restored = entity.GetComponent<TerrainComponent>();
        CHECK(restored.LODEnabled == t.LODEnabled);
        NEAR(restored.LODDistanceScale, t.LODDistanceScale, 1e-6f);
        CHECK(restored.Heights == t.Heights);
        CHECK(restored.Size == t.Size);
        CHECK(restored.Layers[2].TexturePath == t.Layers[2].TexturePath);
        NEAR(restored.Weights[8 * 17 + 8].w, t.Weights[8 * 17 + 8].w, 1e-5f);
        auto path = std::filesystem::temp_directory_path() / ("zamin-terrain-" + std::to_string(uint64_t(UUID())) + ".r16");
        std::string error;
        CHECK(TerrainTools::ExportHeightmap(t, path, error));
        auto imported = t;
        std::fill(imported.Heights.begin(), imported.Heights.end(), 0);
        CHECK(TerrainTools::ImportHeightmap(imported, path, error));
        for (size_t i = 0; i < t.Heights.size(); ++i) NEAR(imported.Heights[i], t.Heights[i], t.HeightLimit / 65535.0f + 1e-5f);
        std::filesystem::remove(path);
        auto malformed = snapshot;
        auto key = malformed.find("\"Resolution\"");
        CHECK(key != std::string::npos);
        auto begin = malformed.find(':', key) + 1, end = malformed.find_first_of(",}", begin);
        malformed.replace(begin, end - begin, "100000");
        auto defaults = CreateRef<Scene>();
        auto fresh = defaults->CreateEntity("Fresh terrain");
        fresh.AddComponent<TerrainComponent>();
        auto freshCopy = CreateRef<Scene>();
        CHECK(SceneSerializer(freshCopy).DeserializeFromString(SceneSerializer(defaults).SerializeToString()));
        auto& freshTerrain = freshCopy->GetEntityByUUID(fresh.GetUUID()).GetComponent<TerrainComponent>();
        CHECK(freshTerrain.Heights.size() == size_t(freshTerrain.Resolution) * freshTerrain.Resolution);
        auto bad = CreateRef<Scene>();
        SceneSerializer invalid(bad);
        CHECK(invalid.DeserializeFromString(malformed));
        CHECK(!bad->GetEntityByUUID(e.GetUUID()).HasComponent<TerrainComponent>());
        CHECK(!invalid.GetLoadReport().Issues.empty());
    }
    TEST(TerrainPBRSerializationAndCopy)
    {
        auto scene = CreateRef<Scene>();
        auto e = scene->CreateEntity("PBR terrain");
        auto& t = e.AddComponent<TerrainComponent>();
        t.Reset(5, { 4, 8 }, 1);
        auto& l = t.Layers[3];
        l.NormalMapPath = "Textures/rock-normal.png";
        l.MaskMapPath = "Textures/rock-orm.png";
        l.TexturePath = "Textures/rock.png";
        l.Color = { 0.7f, 0.6f, 0.5f };
        l.Tiling = 3;
        l.Roughness = .4f;
        l.Metallic = .8f;
        l.Occlusion = .6f;
        l.NormalStrength = 2.5f;
        auto loaded = CreateRef<Scene>();
        CHECK(SceneSerializer(loaded).DeserializeFromString(SceneSerializer(scene).SerializeToString()));
        auto& restored = loaded->GetEntityByUUID(e.GetUUID()).GetComponent<TerrainComponent>();
        const auto& r = restored.Layers[3];
        CHECK(r.NormalMapPath == l.NormalMapPath);
        CHECK(r.MaskMapPath == l.MaskMapPath);
        CHECK(r.TexturePath == l.TexturePath);
        CHECK(r.Color == l.Color);
        NEAR(r.Tiling, l.Tiling, 1e-6f);
        NEAR(r.Roughness, l.Roughness, 1e-6f);
        NEAR(r.Metallic, l.Metallic, 1e-6f);
        NEAR(r.Occlusion, l.Occlusion, 1e-6f);
        NEAR(r.NormalStrength, l.NormalStrength, 1e-6f);
        auto copy = Scene::Copy(scene);
        const auto copiedEntity = copy->GetEntityByUUID(e.GetUUID());
        const auto& copied = copiedEntity.GetComponent<TerrainComponent>();
        CHECK(copied.Layers[3].MaskMapPath == l.MaskMapPath);
        CHECK(!copied.Runtime);
    }
    TEST(TerrainPBRInvalidValuesAreSanitized)
    {
        TerrainComponent t;
        t.Reset(3, { 2, 2 });
        auto& l = t.Layers[0];
        l.Roughness = -4;
        l.Metallic = 5;
        l.Occlusion = -2;
        l.NormalStrength = 100;
        t.EnsureSize();
        NEAR(l.Roughness, .04f, 1e-6f);
        NEAR(l.Metallic, 1, 1e-6f);
        NEAR(l.Occlusion, 0, 1e-6f);
        NEAR(l.NormalStrength, 4, 1e-6f);
        l.Roughness = l.Metallic = l.Occlusion = l.NormalStrength = std::numeric_limits<float>::quiet_NaN();
        t.EnsureSize();
        NEAR(l.Roughness, .95f, 1e-6f);
        NEAR(l.Metallic, 0, 1e-6f);
        NEAR(l.Occlusion, 1, 1e-6f);
        NEAR(l.NormalStrength, 1, 1e-6f);
    }
    TEST(TerrainLegacyLayersKeepPBRDefaults)
    {
        auto scene = CreateRef<Scene>();
        auto e = scene->CreateEntity("Legacy terrain");
        e.AddComponent<TerrainComponent>().Reset(3, { 2, 2 });
        auto snapshot = nlohmann::json::parse(SceneSerializer(scene).SerializeToString());
        for (auto& entity : snapshot["Entities"])
            if (entity.contains("Terrain"))
                for (auto& layer : entity["Terrain"]["Layers"])
                    for (auto key : { "NormalMap", "MaskMap", "Roughness", "Metallic", "Occlusion", "NormalStrength" }) layer.erase(key);
        auto loaded = CreateRef<Scene>();
        CHECK(SceneSerializer(loaded).DeserializeFromString(snapshot.dump()));
        auto& l = loaded->GetEntityByUUID(e.GetUUID()).GetComponent<TerrainComponent>().Layers[0];
        CHECK(l.NormalMapPath.empty() && l.MaskMapPath.empty());
        NEAR(l.Roughness, .95f, 1e-6f);
        NEAR(l.Metallic, 0, 1e-6f);
        NEAR(l.Occlusion, 1, 1e-6f);
        NEAR(l.NormalStrength, 1, 1e-6f);
    }
    TEST(TerrainPBRReferencesFollowRenameAndExportReachability)
    {
        auto root = std::filesystem::temp_directory_path() / ("zamin-terrain-pbr-refs-" + std::to_string(uint64_t(UUID())));
        auto assets = root / "Assets";
        std::filesystem::create_directories(assets / "Textures");
        std::filesystem::create_directories(assets / "Scenes");
        for (auto name : { "albedo.png", "normal.png", "orm.png" }) CHECK(FileSystem::WriteTextFile(assets / "Textures" / name, "asset-reference fixture"));
        auto scene = CreateRef<Scene>();
        auto e = scene->CreateEntity("Export terrain");
        auto& terrain = e.AddComponent<TerrainComponent>();
        terrain.Reset(3, { 2, 2 });
        auto& l = terrain.Layers[0];
        l.TexturePath = "Textures/albedo.png";
        l.NormalMapPath = "Textures/normal.png";
        l.MaskMapPath = "Textures/orm.png";
        CHECK(SceneSerializer(scene).Serialize(assets / "Scenes/Terrain.zscene"));
        AssetReferenceIndex index;
        AssetReferenceIndex::Sources sources;
        sources.AssetDirectory = assets;
        index.Build(sources);
        CHECK(index.GetReferences("Scenes/Terrain.zscene").size() == 3);
        auto reachable = index.CollectReachable({ "Scenes/Terrain.zscene" }, false);
        for (auto name : { "Textures/albedo.png", "Textures/normal.png", "Textures/orm.png" }) CHECK(reachable.contains(name));
        auto moved = AssetFiles::Rename(assets / "Textures", "Surfaces", assets);
        CHECK(moved.Errors.empty());
        auto restored = CreateRef<Scene>();
        CHECK(SceneSerializer(restored).Deserialize(assets / "Scenes/Terrain.zscene"));
        auto& r = restored->GetEntityByUUID(e.GetUUID()).GetComponent<TerrainComponent>().Layers[0];
        CHECK(r.TexturePath == "Surfaces/albedo.png");
        CHECK(r.NormalMapPath == "Surfaces/normal.png");
        CHECK(r.MaskMapPath == "Surfaces/orm.png");
        index.Build(sources);
        CHECK(index.GetMissing().empty());
        CHECK(index.GetReferences("Scenes/Terrain.zscene").size() == 3);
        std::filesystem::remove_all(root);
    }
    TEST(TerrainJoltCollisionAndPlayCopy)
    {
        auto scene = CreateRef<Scene>();
        auto e = scene->CreateEntity("Terrain");
        auto& t = e.AddComponent<TerrainComponent>();
        t.Reset(17, { 16, 16 }, 3);
        e.AddComponent<Rigidbody3DComponent>().Type = Rigidbody3DComponent::BodyType::Static;
        e.AddComponent<MeshCollider3DComponent>();
        auto ball = scene->CreateEntity("Ball");
        ball.GetTransform().Translation = { 8, 10, 8 };
        ball.AddComponent<Rigidbody3DComponent>();
        ball.AddComponent<SphereCollider3DComponent>();
        auto play = Scene::Copy(scene);
        CHECK(play->GetEntityByUUID(e.GetUUID()).HasComponent<TerrainComponent>());
        scene->OnRuntimeStart();
        CHECK(e.GetComponent<Rigidbody3DComponent>().RuntimeBodyID != 0xffffffff);
        for (int i = 0; i < 240; ++i) scene->OnUpdateRuntime(1.0f / 60.0f);
        CHECK(ball.GetTransform().Translation.y > 3.3f);
        CHECK(ball.GetTransform().Translation.y < 3.8f);
        auto hit = scene->Raycast3D({ 2, 20, 2 }, { 0, -1, 0 }, 30);
        CHECK(hit.Hit);
        NEAR(hit.Point.y, 3, 0.02f);
        scene->OnRuntimeStop();
    }
    TEST(TerrainSceneWriteFailurePreservesOriginal)
    {
        auto dir = std::filesystem::temp_directory_path() / ("zamin-terrain-write-" + std::to_string(uint64_t(UUID())));
        std::filesystem::create_directories(dir);
        auto scene = CreateRef<Scene>();
        scene->CreateEntity("Persistent").AddComponent<TerrainComponent>().Reset(5, { 4, 4 }, 3);
        auto file = dir / "Map.zscene";
        CHECK(SceneSerializer(scene).Serialize(file));
        auto text = FileSystem::ReadTextFile(file);
        CHECK(!text.empty());
        auto folder = dir / "blocked.zscene";
        std::filesystem::create_directory(folder);
        CHECK(!SceneSerializer(scene).Serialize(folder));
        CHECK(FileSystem::ReadTextFile(file) == text);
        if (std::filesystem::exists("/dev/full")) CHECK(!FileSystem::WriteTextFile("/dev/full", "terrain"));
        std::filesystem::remove_all(dir);
    }
}
