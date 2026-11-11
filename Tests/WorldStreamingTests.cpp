#include <Zamin/Scene/WorldStreaming.h>
#include <Zamin/Scene/SceneManager.h>
#include <Zamin/Scene/SceneSerializer.h>
#include <Zamin/Scene/Entity.h>
#include <Zamin/Project/Project.h>
#include <Zamin/Project/ExportAssets.h>
#include <Zamin/Asset/AssetReferences.h>
#include <Zamin/Scripting/ScriptableEntity.h>
#include <Zamin/Scripting/ScriptEngine.h>
#include <Zamin/Utils/FileSystem.h>
#include "TestAssertions.h"
#include <chrono>
#include <thread>
#include <limits>
#include <sys/stat.h>

using namespace Zamin;
namespace {
    struct WorldFixture
    {
        TempDirectory Temp{ "zamin-world-runtime" };
        Ref<Project> Previous = Project::GetActive();
        Ref<Project> ProjectRef;
        WorldManifest Manifest;
        WorldStreaming::Policy Policy;
        std::filesystem::path Assets;
        WorldFixture()
        {
            if (!Temp) throw std::runtime_error("No temp directory");
            Assets = Temp.GetPath() / "Assets";
            const auto file = Temp.GetPath() / "WorldTest.zproject";
            CHECK(FileSystem::WriteTextFile(file, R"({"Version":2,"Project":{"Name":"WorldTest","AssetDirectory":"Assets","StartScene":"Scenes/Anchor.zscene"}})"));
            ProjectRef = Project::Load(file);
            if (!ProjectRef) throw std::runtime_error("Test project unavailable");
            Manifest.CellSize = { 10, 10, 10 };
            for (uint64_t i = 0; i < 3; ++i)
            {
                WorldCell cell;
                cell.ID = UUID(i + 10);
                cell.Coordinate.x = int(i * 4);
                cell.ScenePath = "Scenes/Cell" + std::to_string(i) + ".zscene";
                cell.DeclaredCPUBytes = 100;
                cell.DeclaredGPUBytes = 200;
                Manifest.Cells.push_back(cell);
                Scene scene;
                scene.SetName("Cell" + std::to_string(i));
                auto entity = scene.CreateEntityWithUUID(UUID(100 + i), "Cell entity");
                entity.GetTransform().Translation = { 2, 1, 2 };
                CHECK(FileSystem::WriteTextFile(Assets / cell.ScenePath, SceneSerializer(Ref<Scene>(&scene, [](Scene*) {})).SerializeToString()));
            }
            Policy.LoadRadius = 1;
            Policy.UnloadRadius = 4;
            Policy.MaxResidentCells = 2;
            Policy.MainThreadMilliseconds = 20;
            Save();
        }
        ~WorldFixture() { Project::SetActive(Previous); }
        void Save()
        {
            std::string error;
            CHECK(WorldStreaming::SaveDefinition(Assets / "Worlds/Main.zworld", Manifest, Policy, error));
        }
        Ref<Scene> Start(SceneManager& manager)
        {
            auto anchor = CreateRef<Scene>();
            anchor->CreateEntityWithUUID(UUID(1), "Anchor");
            manager.Start(anchor, "Scenes/Anchor.zscene");
            std::string error;
            CHECK(manager.LoadStreamingWorld("Worlds/Main.zworld", error));
            if (auto* world = manager.GetWorldStreaming()) world->SetViewerOverride({ 2, 0, 2 });
            return anchor;
        }
    };
    bool Until(SceneManager& manager, const std::function<bool()>& predicate)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        do
        {
            manager.ProcessRequests();
            if (predicate()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (std::chrono::steady_clock::now() < deadline);
        return false;
    }
    class WorldCellLifecycleProbe : public ScriptableEntity
    {
    public:
        static inline int Created = 0, Destroyed = 0, Started = 0;
        static inline bool SawSibling = false;
        void OnCreate() override
        {
            ++Created;
            SawSibling = bool(GetScene().FindEntityByName("Sibling"));
        }
        void OnStart() override { ++Started; }
        void OnDestroy() override { ++Destroyed; }
    };
    ZM_REGISTER_SCRIPT(WorldCellLifecycleProbe)
}

TEST(WorldStreamingDefinitionPolicyRoundtripAndTransactionalValidation)
{
    WorldFixture f;
    WorldManifest read;
    WorldStreaming::Policy policy;
    std::string error;
    CHECK(WorldStreaming::LoadDefinition(f.Assets / "Worlds/Main.zworld", read, policy, error));
    CHECK(read.Cells == f.Manifest.Cells && policy.LoadRadius == f.Policy.LoadRadius && policy.AxisMask == 5);
    const auto oldName = read.Name;
    auto invalid = policy;
    invalid.UnloadRadius = invalid.LoadRadius - 1;
    CHECK(!WorldStreaming::SaveDefinition(f.Assets / "Worlds/Main.zworld", read, invalid, error));
    invalid = policy;
    invalid.MainThreadMilliseconds = std::numeric_limits<double>::infinity();
    CHECK(!invalid.Validate(error));
    CHECK(FileSystem::WriteTextFile(f.Assets / "Bad.zworld", "{broken"));
    CHECK(!WorldStreaming::LoadDefinition(f.Assets / "Bad.zworld", read, policy, error));
    CHECK(read.Name == oldName && policy.LoadRadius == f.Policy.LoadRadius);
    const auto pipe = f.Assets / "Pipe.zworld";
    CHECK(mkfifo(pipe.c_str(), 0600) == 0);
    CHECK(!WorldStreaming::LoadDefinition(pipe, read, policy, error));
    CHECK(!WorldStreaming::SaveDefinition(f.Assets / "Scenes/Anchor.zscene", read, policy, error));
}

TEST(WorldStreamingCameraHysteresisOffsetUnloadAndReturn)
{
    WorldFixture f;
    SceneManager manager;
    auto anchor = f.Start(manager);
    auto* world = manager.GetWorldStreaming();
    CHECK(world);
    if (!world) return;
    CHECK(Until(manager, [&] { return world->GetStats().Active == 1; }));
    CHECK(anchor->GetEntityByUUID(UUID(100)) && manager.GetSceneCount() == 1);
    CHECK(!manager.UnloadScene(anchor));
    world->SetViewerOverride({ 12, 0, 2 });
    manager.ProcessRequests();
    CHECK(anchor->GetEntityByUUID(UUID(100))); // beyond load radius, still inside unload radius
    world->SetViewerOverride({ 42, 0, 2 });
    CHECK(Until(manager, [&] { return bool(anchor->GetEntityByUUID(UUID(101))); }));
    CHECK(!anchor->GetEntityByUUID(UUID(100)));
    CHECK_NEAR(anchor->GetEntityByUUID(UUID(101)).GetTransform().Translation.x, 42.f, .001f);
    world->SetViewerOverride({ 2, 0, 2 });
    CHECK(Until(manager, [&] { return bool(anchor->GetEntityByUUID(UUID(100))); }));
    CHECK(world->GetStats().Activations >= 3 && world->GetStats().Unloads >= 2);
    manager.ClearStreamingWorld();
    CHECK(!anchor->GetEntityByUUID(UUID(100)) && anchor->GetEntityByUUID(UUID(1)));
}

TEST(WorldStreamingDependenciesLayersAndDeclaredAdmissionBudget)
{
    WorldFixture f;
    f.Manifest.Cells[1].Dependencies = { UUID(10) };
    f.Manifest.Cells[0].Layer = "dependency-only";
    f.Policy.DeclaredCPUBytes = 200;
    f.Policy.DeclaredGPUBytes = 400;
    f.Save();
    SceneManager manager;
    auto anchor = f.Start(manager);
    auto* world = manager.GetWorldStreaming();
    if (!world) return;
    world->SetViewerOverride({ 42, 0, 2 });
    CHECK(Until(manager, [&] { return world->GetStats().Active == 2; }));
    CHECK(anchor->GetEntityByUUID(UUID(100)) && anchor->GetEntityByUUID(UUID(101)));
    CHECK(world->GetStats().DeclaredCPU == 200 && world->GetStats().DeclaredGPU == 400);
    manager.ClearStreamingWorld();
    f.Policy.MaxResidentCells = 1;
    f.Save();
    std::string error;
    CHECK(manager.LoadStreamingWorld("Worlds/Main.zworld", error));
    world = manager.GetWorldStreaming();
    if (!world) return;
    world->SetViewerOverride({ 42, 0, 2 });
    manager.ProcessRequests();
    CHECK(world->GetStats().Active == 0 && world->GetStats().BudgetBlocked > 0);
}

TEST(WorldStreamingBulkImportStartsScriptsAfterAllEntitiesExist)
{
    WorldCellLifecycleProbe::Created = WorldCellLifecycleProbe::Destroyed = WorldCellLifecycleProbe::Started = 0;
    WorldCellLifecycleProbe::SawSibling = false;
    auto anchor = CreateRef<Scene>();
    SceneManager manager;
    manager.Start(anchor);
    Scene staged;
    auto parent = staged.CreateEntityWithUUID(UUID(9001), "Script owner");
    parent.AddComponent<NativeScriptComponent>().ClassName = "WorldCellLifecycleProbe";
    auto sibling = staged.CreateEntityWithUUID(UUID(9002), "Sibling");
    staged.SetParent(sibling, parent, false);
    std::string error;
    auto ids = anchor->ImportStagedCell(staged, error);
    CHECK(error.empty() && ids.size() == 2 && WorldCellLifecycleProbe::Created == 1 && WorldCellLifecycleProbe::SawSibling);
    CHECK(anchor->GetParent(anchor->GetEntityByUUID(UUID(9002))).GetUUID() == UUID(9001));
    manager.UpdateScenes(Timestep(1.f / 60));
    CHECK(WorldCellLifecycleProbe::Started == 1);
    auto conflict = anchor->ImportStagedCell(staged, error);
    CHECK(conflict.empty() && !error.empty() && WorldCellLifecycleProbe::Created == 1);
    anchor->DestroyEntity(anchor->GetEntityByUUID(UUID(9001)));
    CHECK(!anchor->GetEntityByUUID(UUID(9002)) && WorldCellLifecycleProbe::Destroyed == 1);
    manager.Stop();
    CHECK(WorldCellLifecycleProbe::Destroyed == 1);
}

TEST(WorldStreamingSharedPhysics2DAnd3DThenBodyUnload)
{
    for (bool threeD : { false, true })
    {
        WorldFixture f;
        Scene ground;
        auto floor = ground.CreateEntityWithUUID(UUID(100), "Streamed floor");
        floor.GetTransform().Translation = { 2, 0, 2 };
        if (threeD)
        {
            floor.AddComponent<Rigidbody3DComponent>().Type = Rigidbody3DComponent::BodyType::Static;
            floor.AddComponent<BoxCollider3DComponent>().HalfExtents = { 5, .5f, 5 };
        }
        else
        {
            floor.AddComponent<Rigidbody2DComponent>().Type = Rigidbody2DComponent::BodyType::Static;
            floor.AddComponent<BoxCollider2DComponent>().Size = { 5, .5f };
        }
        CHECK(FileSystem::WriteTextFile(f.Assets / f.Manifest.Cells[0].ScenePath, SceneSerializer(Ref<Scene>(&ground, [](Scene*) {})).SerializeToString()));
        SceneManager manager;
        auto anchor = f.Start(manager);
        auto* world = manager.GetWorldStreaming();
        if (!world) return;
        CHECK(Until(manager, [&] { return world->GetStats().Active == 1; }));
        Scene ballScene;
        auto ball = ballScene.CreateEntityWithUUID(UUID(2), "Ball");
        ball.GetTransform().Translation = { 2, 4, 2 };
        if (threeD)
        {
            ball.AddComponent<Rigidbody3DComponent>();
            ball.AddComponent<SphereCollider3DComponent>();
        }
        else
        {
            ball.AddComponent<Rigidbody2DComponent>();
            ball.AddComponent<CircleCollider2DComponent>();
        }
        std::string error;
        CHECK(anchor->ImportStagedCell(ballScene, error).size() == 1);
        for (int i = 0; i < 180; ++i) manager.UpdateScenes(Timestep(1.f / 60));
        auto live = anchor->GetEntityByUUID(UUID(2));
        CHECK(live && live.GetTransform().Translation.y > .7f && live.GetTransform().Translation.y < 1.4f);
        world->SetViewerOverride({ 1000, 0, 1000 });
        manager.ProcessRequests();
        CHECK(!anchor->GetEntityByUUID(UUID(100)));
        for (int i = 0; i < 90; ++i) manager.UpdateScenes(Timestep(1.f / 60));
        CHECK(anchor->GetEntityByUUID(UUID(2)).GetTransform().Translation.y < -2.f);
    }
}

TEST(WorldStreamingEntityQuotaCollisionFailureRetryAndSingleLoad)
{
    WorldFixture f;
    f.Policy.MaxResidentEntities = 1;
    f.Save();
    SceneManager manager;
    auto anchor = f.Start(manager);
    auto* world = manager.GetWorldStreaming();
    if (!world) return;
    anchor->CreateEntityWithUUID(UUID(100), "Conflict");
    CHECK(Until(manager, [&] { return world->GetStats().Failed > 0; }));
    CHECK(anchor->GetEntityByUUID(UUID(100)).GetName() == "Conflict");
    anchor->DestroyEntity(anchor->GetEntityByUUID(UUID(100)));
    world->RetryFailures();
    CHECK(Until(manager, [&] { return world->GetStats().Active == 1; }));
    manager.Stop();
    CHECK(!manager.GetWorldStreaming() && !anchor->IsRunning());
    f.ProjectRef->GetConfig().StreamingWorld = "Worlds/Main.zworld";
    manager.Start(anchor, "Scenes/Anchor.zscene");
    CHECK(manager.GetWorldStreaming());
    CHECK(Until(manager, [&] { return manager.GetWorldStreaming()->GetStats().Active == 1; }));
    auto replacement = CreateRef<Scene>();
    replacement->SetName("Replacement");
    CHECK(FileSystem::WriteTextFile(f.Assets / "Scenes/Replacement.zscene", SceneSerializer(replacement).SerializeToString()));
    manager.LoadScene("Scenes/Replacement.zscene");
    manager.ProcessRequests();
    CHECK(!manager.GetWorldStreaming() && manager.GetActiveScene()->GetName() == "Replacement");
}

TEST(WorldStreamingXYSelectionAndRapidCancellation)
{
    WorldFixture f;
    f.Policy.AxisMask = 3;
    f.Manifest.Cells[1].Coordinate = { 0, 4, 0 };
    f.Save();
    SceneManager manager;
    auto anchor = f.Start(manager);
    auto* world = manager.GetWorldStreaming();
    if (!world) return;
    for (int i = 0; i < 50; ++i)
    {
        world->SetViewerOverride(i % 2 ? glm::dvec3(2, 42, 0) : glm::dvec3(2, 0, 0));
        manager.ProcessRequests();
    }
    world->SetViewerOverride({ 2, 42, 10000 }); // Z is ignored by XY selection.
    CHECK(Until(manager, [&] { return bool(anchor->GetEntityByUUID(UUID(101))); }));
    CHECK_NEAR(anchor->GetEntityByUUID(UUID(101)).GetTransform().Translation.y, 41.f, .001f);
    CHECK(world->GetStats().Admitted <= f.Policy.MaxResidentCells);
    manager.ClearStreamingWorld();
    CHECK(!anchor->GetEntityByUUID(UUID(101)));
}

TEST(WorldStreamingExportReachabilityAndProjectSettingRoundtrip)
{
    WorldFixture f;
    f.ProjectRef->GetConfig().StreamingWorld = "Worlds/Main.zworld";
    f.ProjectRef->GetConfig().BuildScenes = { { "Scenes/Anchor.zscene" } };
    f.ProjectRef->GetConfig().Export.ExcludeUnusedAssets = true;
    CHECK(FileSystem::WriteTextFile(f.Assets / "Scenes/Anchor.zscene", R"({"Scene":"Anchor","Version":5,"Entities":[]})"));
    CHECK(FileSystem::WriteTextFile(f.Assets / "Scenes/Unused.zscene", R"({"Scene":"Unused","Version":5,"Entities":[]})"));
    CHECK(Project::SaveActive(Project::GetProjectFilePath()));
    auto file = Project::GetProjectFilePath();
    CHECK(Project::Load(file)->GetConfig().StreamingWorld == "Worlds/Main.zworld");
    auto plan = ExportAssets::PlanForActiveProject();
    auto has = [&](const std::string& path) { return std::find(plan.Included.begin(), plan.Included.end(), path) != plan.Included.end(); };
    for (const std::string expected : { "Worlds/Main.zworld", "Scenes/Cell0.zscene", "Scenes/Cell2.zscene" })
        if (!has(expected)) ZaminTest::Fail("Missing streamed export asset: " + expected);
    CHECK(!has("Scenes/Unused.zscene"));
    if (!has("Scenes/Cell0.zscene"))
    {
        const auto selected = ExportAssets::CollectScenes(Project::GetActive()->GetConfig(), f.Assets);
        std::string detail = "Export selected:";
        for (const auto& item : selected) detail += " " + item;
        WorldManifest check;
        std::string reason;
        const bool loaded = check.Load(f.Assets / "Worlds/Main.zworld", reason);
        auto joined = FileSystem::SafeJoin(f.Assets, Project::GetActive()->GetConfig().StreamingWorld);
        ZaminTest::Fail(detail + " manifest-load=" + std::to_string(loaded) + " count=" + std::to_string(check.Cells.size()) + " reason=" + reason +
                        " path=" + Project::GetActive()->GetConfig().StreamingWorld.generic_string() + " join=" + joined.generic_string() + " inside=" + std::to_string(FileSystem::IsSubPathOnDisk(joined, f.Assets, false)));
    }
    AssetReferenceIndex index;
    index.BuildForActiveProject();
    const auto reachable = index.CollectReachable({ "Worlds/Main.zworld" });
    CHECK(reachable.contains("Scenes/Cell0.zscene") && reachable.contains("Scenes/Cell2.zscene"));
}


TEST(WorldStreamingTinyBudgetStillProgressesAtManifestCapacity)
{
    WorldFixture f;
    f.Manifest.Cells.clear();
    for (size_t i = 0; i < WorldManifest::MaxCells; ++i)
    {
        WorldCell cell;
        cell.ID = UUID(i + 1);
        cell.Coordinate.x = int(i);
        cell.Layer = "background";
        cell.ScenePath = "Scenes/Unused" + std::to_string(i) + ".zscene";
        f.Manifest.Cells.push_back(std::move(cell));
    }
    auto& last = f.Manifest.Cells.back();
    last.Coordinate = { 0, 0, 0 };
    last.Layer = "main";
    last.ScenePath = "Scenes/Last.zscene";
    CHECK(FileSystem::WriteTextFile(f.Assets / last.ScenePath, R"({"Scene":"Last","Version":5,"Entities":[{"Entity":59999,"Tag":"Last"}]})"));
    f.Policy.MainThreadMilliseconds = .1;
    f.Policy.MaxResidentCells = 1;
    f.Save();
    SceneManager manager;
    auto anchor = f.Start(manager);
    auto* world = manager.GetWorldStreaming();
    if (!world) return;
    CHECK(Until(manager, [&] { return world->GetStats().Active == 1; }));
    CHECK(anchor->GetEntityByUUID(UUID(59999)) && world->GetStats().Admitted == 1);
}
