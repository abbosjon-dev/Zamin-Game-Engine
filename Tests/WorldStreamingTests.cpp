#include <Zamin/Scripting/Visual/VisualGraphHost.h>
#include <Zamin/Scene/WorldStreaming.h>
#include <Zamin/Scene/WorldCheckpoint.h>
#include <Zamin/Scene/SceneManager.h>
#include <Zamin/Scene/SceneSerializer.h>
#include <Zamin/Scene/Entity.h>
#include <Zamin/Project/Project.h>
#include <Zamin/Project/ExportAssets.h>
#include <Zamin/Renderer/MaterialAsset.h>
#include <Zamin/Asset/AssetReferences.h>
#include <Zamin/Scripting/ScriptableEntity.h>
#include <Zamin/Scripting/ScriptEngine.h>
#include <Zamin/Utils/FileSystem.h>
#include "TestAssertions.h"
#include <chrono>
#include <thread>
#include <limits>
#include <sys/stat.h>
#include <nlohmann/json.hpp>

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

TEST(WorldStreamingObservedMemoryGateDoesNotDropLiveCells)
{
    WorldFixture fixture;
    fixture.Policy.ObservedRSSLimitBytes = 1;
    fixture.Save();
    SceneManager manager;
    auto anchor = fixture.Start(manager);
    for (int i = 0; i < 10; ++i) manager.ProcessRequests();
    const auto stats = manager.GetWorldStreaming()->GetStats();
    if (stats.Residency.Process.Valid)
    {
        CHECK(stats.MemoryPressure && stats.Active == 0 && stats.Admitted == 0 && stats.PressureFrames >= 1);
        CHECK(anchor->GetEntityByUUID(UUID(1)));
    }
    manager.Stop();
    fixture.Policy.ObservedRSSLimitBytes = 0;
    fixture.Save();
    anchor = fixture.Start(manager);
    CHECK(Until(manager, [&] { return manager.GetWorldStreaming()->GetStats().Active == 1; }));
    CHECK(!manager.GetWorldStreaming()->GetStats().MemoryPressure);
    manager.Stop();
}

TEST(WorldStreamingNavigationAcrossLoadedCellsAndRetirement)
{
    WorldFixture fixture;
    fixture.Policy.MaxResidentCells = 3;
    fixture.Policy.LoadRadius = 11;
    fixture.Policy.UnloadRadius = 12;
    fixture.Policy.NavigationMode = 2;
    for (size_t i = 0; i < fixture.Manifest.Cells.size(); ++i)
    {
        auto& cell = fixture.Manifest.Cells[i];
        cell.Coordinate.x = int(i);
        auto scene = CreateRef<Scene>();
        auto e = scene->CreateEntityWithUUID(UUID(100 + i), "Tile nav");
        e.GetTransform().Translation = { 5, 5, 0 };
        auto& grid = e.AddComponent<NavGrid2DComponent>();
        grid.AutoBounds = false;
        grid.Size = { 10, 10 };
        grid.CellSize = 1;
        grid.AgentRadius = 0;
        grid.UseColliders = false;
        grid.UseTilemaps = false;
        CHECK(FileSystem::WriteTextFile(fixture.Assets / cell.ScenePath, SceneSerializer(scene).SerializeToString()));
    }
    fixture.Save();
    SceneManager manager;
    auto anchor = fixture.Start(manager);
    auto* world = manager.GetWorldStreaming();
    world->SetViewerOverride({ 15, 5, 0 });
    CHECK(Until(manager, [&] { return world->GetStats().NavigationTiles == 3; }));
    auto root = anchor->GetEntityByUUID(world->GetNavigationEntity(2));
    CHECK(root && root.GetComponent<NavGrid2DComponent>().Baked);
    if (root)
    {
        CHECK(root.GetComponent<NavGrid2DComponent>().Grid.CountWalkable() == 300);
        CHECK(anchor->FindNavGridEntity({ 1, 1, 0 }, 2) == root);
        CHECK(world->GetStats().NavigationError.empty());
    }
    Scene walkerScene;
    auto authoredWalker = walkerScene.CreateEntityWithUUID(UUID(777), "Cross-cell walker");
    authoredWalker.GetTransform().Translation = { 1, 5, 0 };
    auto& authoredAgent = authoredWalker.AddComponent<NavAgentComponent>();
    authoredAgent.Speed = 8;
    authoredAgent.Acceleration = 40;
    std::string walkerError;
    CHECK(anchor->ImportStagedCell(walkerScene, walkerError).size() == 1);
    auto walker = anchor->GetEntityByUUID(UUID(777));
    CHECK(anchor->SetNavDestination(walker, { 25, 5, 0 }));
    for (int i = 0; i < 240; ++i) manager.UpdateScenes(Timestep(1.f / 60));
    CHECK(walker.GetTransform().Translation.x > 24.5f);
    CHECK(walker.GetComponent<NavAgentComponent>().Arrived);
    CHECK(anchor->SetNavDestination(walker, { 1, 5, 0 }));
    for (int i = 0; i < 30; ++i) manager.UpdateScenes(Timestep(1.f / 60));
    world->SetViewerOverride({ 1000, 1000, 0 });
    CHECK(Until(manager, [&] { return world->GetStats().Active == 0 && world->GetStats().NavigationCells == 0; }));
    const float stoppedAt = walker.GetTransform().Translation.x;
    for (int i = 0; i < 60; ++i) manager.UpdateScenes(Timestep(1.f / 60));
    CHECK(std::abs(walker.GetTransform().Translation.x - stoppedAt) < 1.1f);
    CHECK(root && !root.GetComponent<NavGrid2DComponent>().Baked);
    world->SetViewerOverride({ 15, 5, 0 });
    CHECK(Until(manager, [&] { return world->GetStats().NavigationTiles == 3 && world->GetStats().NavigationCells == 300; }));
    auto automatic = anchor->CreateEntity("Automatic navigation agent");
    automatic.AddComponent<NavAgentComponent>().RepathRequested = false;
    auto explicitAgent = anchor->CreateEntity("Explicit grid navigation agent");
    auto& explicitState = explicitAgent.AddComponent<NavAgentComponent>();
    explicitState.Grid.ID = UUID(100);
    explicitState.RepathRequested = false;
    auto tile = anchor->GetEntityByUUID(UUID(101));
    auto& grid = tile.GetComponent<NavGrid2DComponent>().Grid;
    grid.Reset(10, 10, 1, { 0, 0 }); // deliberate overlap invalidates the entire stale mosaic
    manager.ProcessRequests();
    CHECK(!root.GetComponent<NavGrid2DComponent>().Baked && !world->GetStats().NavigationError.empty());
    CHECK(!world->GetNavigationAtlas() && !world->GetStats().AtlasError.empty());
    CHECK(automatic.GetComponent<NavAgentComponent>().RepathRequested);
    CHECK(!explicitAgent.GetComponent<NavAgentComponent>().RepathRequested);
    grid.Reset(10, 10, 1, { 10, 0 });
    manager.ProcessRequests();
    CHECK(root.GetComponent<NavGrid2DComponent>().Baked && world->GetStats().NavigationError.empty());
    const auto rootID = root.GetUUID();
    manager.ClearStreamingWorld();
    CHECK(!anchor->GetEntityByUUID(rootID));
    manager.Stop();
}

TEST(WorldStreamingResidencyPolicyRoundTripAndBounds)
{
    WorldFixture fixture;
    fixture.Policy.ObservedRSSLimitBytes = 12345678;
    fixture.Policy.RequestedGPULimitBytes = 87654321;
    fixture.Policy.PressureResumeRatio = .75;
    fixture.Policy.CacheEntriesPerKind = 7;
    fixture.Policy.MaxCacheEvictionsPerFrame = 3;
    fixture.Policy.NavigationMode = 3;
    fixture.Policy.NavigationMaxCells = 4096;
    fixture.Save();
    WorldManifest manifest;
    WorldStreaming::Policy restored;
    std::string error;
    CHECK(WorldStreaming::LoadDefinition(fixture.Assets / "Worlds/Main.zworld", manifest, restored, error));
    CHECK(restored.ObservedRSSLimitBytes == 12345678 && restored.RequestedGPULimitBytes == 87654321);
    CHECK(restored.PressureResumeRatio == .75 && restored.CacheEntriesPerKind == 7 && restored.MaxCacheEvictionsPerFrame == 3);
    CHECK(restored.NavigationMode == 3 && restored.NavigationMaxCells == 4096);
    restored.NavigationMode = 1;
    CHECK(!restored.Validate(error));
    restored.NavigationMode = 2;
    restored.PressureResumeRatio = 1;
    CHECK(!restored.Validate(error));
    restored.PressureResumeRatio = .5;
    restored.MaxCacheEvictionsPerFrame = 0;
    CHECK(!restored.Validate(error));
}

TEST(WorldStreamingPressureRetiresCellsBeforeNewAdmission)
{
    WorldFixture fixture;
    fixture.Policy.RequestedGPULimitBytes = AssetResidency::Inspect().RequestedGPUBytes + 1;
    fixture.Save();
    SceneManager manager;
    auto anchor = fixture.Start(manager);
    CHECK(Until(manager, [&] { return manager.GetWorldStreaming()->GetStats().Active == 1; }));
    auto* world = manager.GetWorldStreaming();
    TrackedMemory external(MemoryCategory::Textures);
    external.Set(100);
    manager.ProcessRequests();
    CHECK(world->GetStats().MemoryPressure && world->GetStats().Active == 1);
    CHECK(anchor->GetEntityByUUID(UUID(100)));
    world->SetViewerOverride({ 42, 0, 2 });
    for (int i = 0; i < 10; ++i) manager.ProcessRequests();
    CHECK(world->GetStats().Active == 0 && world->GetStats().Admitted == 0);
    external.Set(0);
    CHECK(Until(manager, [&] { return world->GetStats().Active == 1; }));
    CHECK(anchor->GetEntityByUUID(UUID(101)) && !world->GetStats().MemoryPressure);
    manager.Stop();
}

TEST(WorldStreamingTerrainSeamSharesPhysicalFloor)
{
    WorldFixture fixture;
    fixture.Policy.LoadRadius = 11;
    fixture.Policy.UnloadRadius = 12;
    fixture.Manifest.Cells[1].Coordinate.x = 1;
    for (size_t i = 0; i < 2; ++i)
    {
        auto source = CreateRef<Scene>();
        auto floor = source->CreateEntityWithUUID(UUID(100 + i), "Terrain floor");
        floor.AddComponent<TerrainComponent>().Reset(i ? 17 : 9, { 10, 10 }, 2);
        floor.GetComponent<TerrainComponent>().LockBorderLOD = true;
        floor.AddComponent<Rigidbody3DComponent>().Type = Rigidbody3DComponent::BodyType::Static;
        floor.AddComponent<MeshCollider3DComponent>();
        CHECK(FileSystem::WriteTextFile(fixture.Assets / fixture.Manifest.Cells[i].ScenePath, SceneSerializer(source).SerializeToString()));
    }
    fixture.Save();
    SceneManager manager;
    auto anchor = fixture.Start(manager);
    manager.GetWorldStreaming()->SetViewerOverride({ 10, 0, 5 });
    CHECK(Until(manager, [&] { return manager.GetWorldStreaming()->GetStats().Active == 2; }));
    Scene staged;
    for (uint64_t i = 0; i < 3; ++i)
    {
        auto ball = staged.CreateEntityWithUUID(UUID(500 + i), "Seam collision probe");
        ball.GetTransform().Translation = { 9.8f + .2f * float(i), 5, 2 + 3 * float(i) };
        ball.AddComponent<Rigidbody3DComponent>();
        ball.AddComponent<SphereCollider3DComponent>();
    }
    std::string error;
    CHECK(anchor->ImportStagedCell(staged, error).size() == 3);
    for (int i = 0; i < 240; ++i) manager.UpdateScenes(Timestep(1.f / 60));
    for (uint64_t i = 0; i < 3; ++i)
    {
        const auto position = anchor->GetEntityByUUID(UUID(500 + i)).GetTransform().Translation;
        CHECK(position.y > 2.35f && position.y < 2.7f);
    }
    manager.Stop();
}

TEST(WorldStreamingReloadChangedEntityCountResetsResolutionCursor)
{
    WorldFixture fixture;
    auto write = [&](size_t count) {
        auto source = CreateRef<Scene>();
        for (size_t i = 0; i < count; ++i) source->CreateEntityWithUUID(UUID(100 + i), "Reloaded entity");
        CHECK(FileSystem::WriteTextFile(fixture.Assets / fixture.Manifest.Cells[0].ScenePath, SceneSerializer(source).SerializeToString()));
    };
    write(3);
    fixture.Save();
    SceneManager manager;
    auto anchor = fixture.Start(manager);
    auto* world = manager.GetWorldStreaming();
    CHECK(Until(manager, [&] { return world->GetStats().Active == 1; }));
    for (size_t count : { 1u, 4u, 2u, 0u, 3u })
    {
        world->SetViewerOverride({ 1000, 0, 1000 });
        CHECK(Until(manager, [&] { return world->GetStats().Active == 0; }));
        write(count);
        world->SetViewerOverride({ 2, 0, 2 });
        CHECK(Until(manager, [&] { return world->GetStats().Active == 1; }));
        CHECK(world->GetStats().Entities == count);
        for (size_t i = 0; i < count; ++i) CHECK(anchor->GetEntityByUUID(UUID(100 + i)));
        CHECK(!anchor->GetEntityByUUID(UUID(100 + count)));
    }
    manager.Stop();
}

TEST(WorldStreamingManualAndAutomaticTrimShareFrameAllowance)
{
    WorldFixture fixture;
    fixture.Policy.MaxCacheEvictionsPerFrame = 1;
    fixture.Policy.CacheEntriesPerKind = 100000;
    fixture.Save();
    std::vector<std::filesystem::path> files;
    for (int i = 0; i < 3; ++i)
    {
        auto file = fixture.Assets / ("Material" + std::to_string(i) + ".zmaterial");
        CHECK(MaterialSerializer::Serialize(MaterialData{}, file));
        CHECK(MaterialLibrary::Load(file));
        files.push_back(file);
    }
    SceneManager manager;
    auto anchor = fixture.Start(manager);
    auto* world = manager.GetWorldStreaming();
    CHECK(Until(manager, [&] { return world->GetStats().Active == 1; }));
    const auto before = world->GetStats().CacheEvictions;
    CHECK(world->TrimCaches() == 1);
    CHECK(world->TrimCaches() == 0 && world->TrimCaches() == 0);
    CHECK(world->GetStats().CacheEvictions == before + 1);
    manager.ProcessRequests();
    CHECK(world->TrimCaches() == 1 && world->TrimCaches() == 0);
    manager.Stop();
    for (const auto& file : files) MaterialLibrary::ReleaseIfUnused(file);
}

TEST(WorldCheckpointRuntimeCapturesEditsDeletesAndAdoptedSubtrees)
{
    WorldFixture fixture;
    fixture.Manifest.Origin = { 100, -3, 50 };
    fixture.Save();
    SceneManager manager;
    auto anchor = fixture.Start(manager);
    auto* world = manager.GetWorldStreaming();
    world->SetViewerOverride({ 102, 0, 52 });
    CHECK(Until(manager, [&] { return world->GetStats().Active == 1; }));
    world->SetCaptureOnUnload(true);
    auto actor = anchor->GetEntityByUUID(UUID(100));
    actor.GetTransform().Translation.x = 107;
    auto spawned = anchor->CreateEntityWithUUID(UUID(900), "Spawned persistent actor");
    spawned.GetTransform().Translation = { 105, 2, 55 };
    auto child = anchor->CreateEntityWithUUID(UUID(901), "Persistent child");
    child.GetTransform().Translation = { 1, 2, 3 };
    anchor->SetParent(child, spawned, false);
    std::string error;
    CHECK(world->AdoptEntity(UUID(10), UUID(900), error));
    CHECK(world->GetCellEntities(UUID(10)).size() == 3);
    auto file = fixture.Temp.GetPath() / "save/slot.json";
    CHECK(world->SaveCheckpoint(file, error));
    CHECK(world->GetStats().CheckpointCells == 1);
    CHECK(world->RemoveCellEntity(UUID(10), UUID(100), error));
    CHECK(!anchor->GetEntityByUUID(UUID(100)));
    world->SetViewerOverride({ 1000, 0, 1000 });
    CHECK(Until(manager, [&] { return world->GetStats().Active == 0; }));
    world->SetViewerOverride({ 102, 0, 52 });
    CHECK(Until(manager, [&] { return world->GetStats().Active == 1; }));
    CHECK(!anchor->GetEntityByUUID(UUID(100)));
    CHECK(anchor->GetEntityByUUID(UUID(900)) && anchor->GetEntityByUUID(UUID(901)));
    CHECK_NEAR(anchor->GetEntityByUUID(UUID(900)).GetTransform().Translation.x, 105, 1e-5f);
    CHECK(anchor->GetParent(anchor->GetEntityByUUID(UUID(901))).GetUUID() == UUID(900));
    CHECK(world->RequestRestoreCheckpoint(file, error));
    CHECK(Until(manager, [&] { return !world->GetStats().RestorePending && world->GetStats().Active == 1; }));
    CHECK(anchor->GetEntityByUUID(UUID(100)));
    CHECK_NEAR(anchor->GetEntityByUUID(UUID(100)).GetTransform().Translation.x, 107, 1e-5f);
    CHECK(world->GetStats().Restores == 1);
    CHECK(world->RequestResetToAuthored(error));
    CHECK(Until(manager, [&] { return !world->GetStats().RestorePending && world->GetStats().Active == 1; }));
    CHECK_NEAR(anchor->GetEntityByUUID(UUID(100)).GetTransform().Translation.x, 102, 1e-5f);
    CHECK(!anchor->GetEntityByUUID(UUID(900)) && !anchor->GetEntityByUUID(UUID(901)));
    manager.Stop();
}
TEST(WorldCheckpointFailedCaptureKeepsLiveCellAndPreviousFile)
{
    WorldFixture fixture;
    SceneManager manager;
    auto anchor = fixture.Start(manager);
    auto* world = manager.GetWorldStreaming();
    CHECK(Until(manager, [&] { return world->GetStats().Active == 1; }));
    std::string error;
    const auto file = fixture.Temp.GetPath() / "slot.json";
    CHECK(world->SaveCheckpoint(file, error));
    const auto before = FileSystem::ReadTextFile(file);
    anchor->SetParent(anchor->GetEntityByUUID(UUID(100)), anchor->GetEntityByUUID(UUID(1)), false);
    CHECK(!world->SaveCheckpoint(file, error));
    CHECK(FileSystem::ReadTextFile(file) == before);
    world->SetCaptureOnUnload(true);
    world->SetViewerOverride({ 1000, 0, 1000 });
    for (int i = 0; i < 5; ++i) manager.ProcessRequests();
    CHECK(world->GetStats().Active == 1 && !world->GetStats().CheckpointError.empty());
    CHECK(anchor->GetEntityByUUID(UUID(100)));
    world->SetCaptureOnUnload(false);
    CHECK(Until(manager, [&] { return world->GetStats().Active == 0; }));
    manager.Stop();
}
TEST(WorldCheckpointRestorePreflightAndOwnershipAreTransactional)
{
    WorldFixture fixture;
    fixture.Policy.MaxEntitiesPerCell = 2;
    fixture.Save();
    SceneManager manager;
    auto anchor = fixture.Start(manager);
    auto* world = manager.GetWorldStreaming();
    CHECK(Until(manager, [&] { return world->GetStats().Active == 1; }));
    std::string error;
    CHECK(!world->AdoptEntity(UUID(999), UUID(1), error));
    CHECK(!world->RemoveCellEntity(UUID(10), UUID(1), error));
    auto root = anchor->CreateEntity("Too big root");
    auto child = anchor->CreateEntity("Child");
    anchor->SetParent(child, root, false);
    CHECK(!world->AdoptEntity(UUID(10), root.GetUUID(), error));
    CHECK(world->GetCellEntities(UUID(10)).size() == 1);
    const auto file = fixture.Temp.GetPath() / "invalid.json";
    CHECK(FileSystem::WriteTextFile(file, "{"));
    CHECK(!world->RequestRestoreCheckpoint(file, error));
    CHECK(!world->GetStats().RestorePending && anchor->GetEntityByUUID(UUID(100)));
    auto snapshot = CreateRef<Scene>();
    snapshot->CreateEntityWithUUID(UUID(1), "Conflicts with anchor");
    WorldCheckpoint state(fixture.Manifest);
    CHECK(state.Put(UUID(10), SceneSerializer(snapshot).SerializeToString(), error));
    CHECK(state.Save(file, error));
    CHECK(!world->RequestRestoreCheckpoint(file, error));
    CHECK(anchor->GetEntityByUUID(UUID(100)) && world->GetStats().Active == 1);
    CHECK(!world->GetStats().RestorePending);
    manager.Stop();
}
TEST(WorldCheckpointSessionRestartRestoresColdCells)
{
    WorldFixture fixture;
    auto file = fixture.Temp.GetPath() / "cold.json";
    {
        SceneManager manager;
        auto anchor = fixture.Start(manager);
        auto* world = manager.GetWorldStreaming();
        CHECK(Until(manager, [&] { return world->GetStats().Active == 1; }));
        anchor->GetEntityByUUID(UUID(100)).GetTransform().Translation.x = 8;
        std::string error;
        CHECK(world->SaveCheckpoint(file, error));
        manager.Stop();
    }
    SceneManager manager;
    auto anchor = fixture.Start(manager);
    auto* world = manager.GetWorldStreaming();
    std::string error;
    CHECK(world->RequestRestoreCheckpoint(file, error));
    CHECK(Until(manager, [&] { return world->GetStats().Active == 1 && !world->GetStats().RestorePending; }));
    CHECK_NEAR(anchor->GetEntityByUUID(UUID(100)).GetTransform().Translation.x, 8, 1e-5f);
    manager.Stop();
}

TEST(WorldCheckpointCommandQueueIsBoundedFIFOAndFrameLimited)
{
    WorldFixture fixture;
    SceneManager manager;
    auto anchor = fixture.Start(manager);
    auto* world = manager.GetWorldStreaming();
    CHECK(Until(manager, [&] { return world->GetStats().Active == 1; }));
    std::string error;
    auto spawned = anchor->CreateEntity("Queued actor");
    const auto id = spawned.GetUUID();
    const auto file = fixture.Temp.GetPath() / "queued.json";
    const auto first = world->Enqueue({ WorldStreaming::CommandKind::Adopt, UUID(10), id, {} }, error);
    CHECK(first != 0 && world->Enqueue({ WorldStreaming::CommandKind::Save, UUID(0), UUID(0), file }, error) > first);
    CHECK(!std::filesystem::exists(file));
    CHECK(Until(manager, [&] { return std::filesystem::exists(file) && world->GetStats().CompletedCommands == 2; }));
    CHECK(world->GetStats().FailedCommands == 0);
    for (int i = 0; i < 64; ++i) CHECK(world->Enqueue({ WorldStreaming::CommandKind::Capture, UUID(999), UUID(0), {} }, error) != 0);
    CHECK(world->Enqueue({}, error) == 0);
    const auto completed = world->GetStats().CompletedCommands;
    manager.ProcessRequests();
    CHECK(world->GetStats().CompletedCommands == completed + 4 && world->GetStats().PendingCommands == 60);
    CHECK(world->GetStats().FailedCommands == 4);
    manager.Stop();
}
TEST(WorldCheckpointCaptureRequiresExplicitChildOwnership)
{
    WorldFixture fixture;
    SceneManager manager;
    auto anchor = fixture.Start(manager);
    auto* world = manager.GetWorldStreaming();
    CHECK(Until(manager, [&] { return world->GetStats().Active == 1; }));
    auto owned = anchor->GetEntityByUUID(UUID(100));
    auto child = anchor->CreateEntity("Unregistered child");
    anchor->SetParent(child, owned, false);
    std::string error;
    CHECK(!world->CaptureCell(UUID(10), error));
    CHECK(world->AdoptEntity(UUID(10), owned.GetUUID(), error));
    CHECK(world->CaptureCell(UUID(10), error) && world->GetCellEntities(UUID(10)).size() == 2);
    manager.Stop();
}

TEST(WorldCheckpointComponentPreflightRejectsBadSnapshotBeforeRetirement)
{
    WorldFixture fixture;
    SceneManager manager;
    auto anchor = fixture.Start(manager);
    auto* world = manager.GetWorldStreaming();
    CHECK(Until(manager, [&] { return world->GetStats().Active == 1; }));
    std::string error;
    auto source = CreateRef<Scene>();
    source->CreateEntityWithUUID(UUID(100), "Broken component");
    auto doc = nlohmann::json::parse(SceneSerializer(source).SerializeToString());
    doc["Entities"][0]["UnknownComponentForCheckpoint"] = { { "Data", 1 } };
    WorldCheckpoint state(fixture.Manifest);
    CHECK(state.Put(UUID(10), doc.dump(), error));
    const auto file = fixture.Temp.GetPath() / "broken-components.json";
    CHECK(state.Save(file, error));
    CHECK(!world->RequestRestoreCheckpoint(file, error));
    CHECK(world->GetStats().Active == 1 && !world->GetStats().RestorePending);
    CHECK(anchor->GetEntityByUUID(UUID(100)));
    manager.Stop();
}
TEST(WorldStreamingCellPreviewDoesNotStartTheWholeConfiguredWorld)
{
    WorldFixture fixture;
    fixture.ProjectRef->GetConfig().StreamingWorld = "Worlds/Main.zworld";
    auto scene = CreateRef<Scene>();
    CHECK(SceneSerializer(scene).Deserialize(fixture.Assets / fixture.Manifest.Cells[0].ScenePath));
    SceneManager manager;
    manager.Start(scene, fixture.Manifest.Cells[0].ScenePath);
    CHECK(!manager.GetWorldStreaming());
    manager.Stop();
    auto anchor = CreateRef<Scene>();
    manager.Start(anchor, "Scenes/Anchor.zscene");
    CHECK(manager.GetWorldStreaming());
    manager.Stop();
}

TEST(WorldStreamingSparseAtlasLinksRoundTripAndUnload)
{
    WorldFixture fixture;
    fixture.Policy.NavigationMode = 2;
    fixture.Policy.NavigationMaxCells = 300;
    fixture.Policy.MaxResidentCells = 3;
    fixture.Policy.LoadRadius = 100000;
    fixture.Policy.UnloadRadius = 100001;
    fixture.Policy.NavigationLinks = { { 501, { 5.5f, 5.5f }, { 40005.5f, 5.5f }, 2, false, true },
                                       { 502, { 40005.5f, 5.5f }, { 80005.5f, 5.5f }, 3, false, true } };
    for (size_t i = 0; i < fixture.Manifest.Cells.size(); ++i)
    {
        auto& cell = fixture.Manifest.Cells[i];
        cell.Coordinate.x = int(i * 4000);
        auto scene = CreateRef<Scene>();
        auto entity = scene->CreateEntityWithUUID(UUID(100 + i), "Sparse nav tile");
        entity.GetTransform().Translation = { 5, 5, 0 };
        auto& grid = entity.AddComponent<NavGrid2DComponent>();
        grid.AutoBounds = false;
        grid.Size = { 10, 10 };
        grid.CellSize = 1;
        grid.AgentRadius = 0;
        grid.UseColliders = false;
        grid.UseTilemaps = false;
        CHECK(FileSystem::WriteTextFile(fixture.Assets / cell.ScenePath, SceneSerializer(scene).SerializeToString()));
    }
    fixture.Save();
    WorldManifest manifest;
    WorldStreaming::Policy policy;
    std::string error;
    CHECK(WorldStreaming::LoadDefinition(fixture.Assets / "Worlds/Main.zworld", manifest, policy, error));
    CHECK(policy.NavigationLinks.size() == 2 && policy.NavigationLinks[1].Cost == 3 && !policy.NavigationLinks[0].Bidirectional);
    SceneManager manager;
    auto anchor = fixture.Start(manager);
    auto* world = manager.GetWorldStreaming();
    CHECK(Until(manager, [&] { return world->GetStats().AtlasCells == 300; }));
    const auto snapshot = world->GetNavigationAtlas();
    CHECK(snapshot && snapshot->GetActiveLinks() == 2);
    CHECK(!world->GetStats().NavigationError.empty() && world->GetStats().AtlasError.empty());
    auto route = world->FindNavigationRoute({ 5.5f, 5.5f }, { 80005.5f, 5.5f });
    CHECK(route.Status == NavPathStatus::Complete && route.Points.size() == 3);
    CHECK_NEAR(route.Cost, 5, .001);
    NavRouteFollower follower;
    CHECK(follower.SetRoute(route));
    CHECK(!world->SetNavigationLinks({ { 0, {}, {}, 1 } }, error));
    CHECK(world->GetNavigationAtlas()->GetRevision() == snapshot->GetRevision());
    CHECK(world->SetNavigationLinks({}, error));
    manager.ProcessRequests();
    const auto changed = world->GetNavigationAtlas();
    CHECK(changed && changed->GetRevision() != snapshot->GetRevision());
    CHECK(follower.Advance(changed->GetRevision(), 10) == NavRouteFollower::State::Stale);
    CHECK(world->FindNavigationRoute({ 5.5f, 5.5f }, { 80005.5f, 5.5f }).Status == NavPathStatus::Partial);
    CHECK(snapshot->FindRoute({ 5.5f, 5.5f }, { 80005.5f, 5.5f }).Status == NavPathStatus::Complete);
    world->SetViewerOverride({ 1000000, 1000000, 0 });
    CHECK(Until(manager, [&] { return world->GetStats().Active == 0 && world->GetStats().AtlasCells == 0; }));
    CHECK(world->FindNavigationRoute({ 5.5f, 5.5f }, { 80005.5f, 5.5f }).Status == NavPathStatus::Failed);
    manager.Stop();
    CHECK(snapshot->FindRoute({ 5.5f, 5.5f }, { 80005.5f, 5.5f }).Status == NavPathStatus::Complete);
}

TEST(WorldStreamingNavigationLinkDefinitionRejectsMalformedTransactionally)
{
    WorldFixture fixture;
    WorldManifest manifest = fixture.Manifest;
    WorldStreaming::Policy policy = fixture.Policy;
    policy.NavigationMaxCells = 777;
    std::string error;
    const auto file = fixture.Assets / "Worlds/Main.zworld";
    std::string text;
    CHECK(fixture.Manifest.Serialize(text, error));
    auto doc = nlohmann::json::parse(text);
    doc["Streaming"]["NavigationLinks"] = nlohmann::json::array({ { { "ID", -1 }, { "From", { 0, 0 } }, { "To", { 1, 1 } }, { "Cost", 1 }, { "Bidirectional", true }, { "Enabled", true } } });
    CHECK(FileSystem::WriteTextFile(file, doc.dump()));
    CHECK(!WorldStreaming::LoadDefinition(file, manifest, policy, error));
    CHECK(policy.NavigationMaxCells == 777 && manifest.Cells.size() == 3);
    doc["Streaming"]["NavigationLinks"][0]["ID"] = uint64_t(1);
    doc["Streaming"]["NavigationLinks"][0]["Enabled"] = "yes";
    CHECK(FileSystem::WriteTextFile(file, doc.dump()));
    CHECK(!WorldStreaming::LoadDefinition(file, manifest, policy, error));
    CHECK(policy.NavigationMaxCells == 777);
}

namespace {
    void ConfigureWorldAgent(WorldFixture& f, bool threeD = false)
    {
        f.Policy.NavigationMode = threeD ? 3 : 2;
        f.Policy.AxisMask = threeD ? 5 : 3;
        f.Policy.LoadRadius = 100000;
        f.Policy.UnloadRadius = 100001;
        f.Policy.MaxResidentCells = 3;
        f.Policy.NavigationLinks = { { 501, { 2.5f, 5.5f }, { 40002.5f, 5.5f }, 1, true, true },
                                     { 502, { 40005.5f, 5.5f }, { 80002.5f, 5.5f }, 1, true, true } };
        for (size_t i = 0; i < f.Manifest.Cells.size(); ++i)
        {
            auto& cell = f.Manifest.Cells[i];
            cell.Coordinate.x = int(i * 4000);
            auto scene = CreateRef<Scene>();
            auto entity = scene->CreateEntityWithUUID(UUID(100 + i), "Agent navigation tile");
            if (threeD)
            {
                entity.GetTransform().Translation = { 5, 1, 5 };
                auto& grid = entity.AddComponent<NavGrid3DComponent>();
                grid.Size = { 10, 4, 10 };
                grid.CellSize = 1;
                grid.AgentRadius = .2f;
                auto floor = scene->CreateEntityWithUUID(UUID(200 + i), "Agent floor");
                floor.GetTransform().Translation = { 5, -.1f, 5 };
                floor.AddComponent<Rigidbody3DComponent>().Type = Rigidbody3DComponent::BodyType::Static;
                floor.AddComponent<BoxCollider3DComponent>().HalfExtents = { 5, .1f, 5 };
            }
            else
            {
                entity.GetTransform().Translation = { 5, 5, 0 };
                auto& grid = entity.AddComponent<NavGrid2DComponent>();
                grid.AutoBounds = false;
                grid.Size = { 10, 10 };
                grid.CellSize = 1;
                grid.AgentRadius = 0;
                grid.UseColliders = grid.UseTilemaps = false;
            }
            CHECK(FileSystem::WriteTextFile(f.Assets / cell.ScenePath, SceneSerializer(scene).SerializeToString()));
        }
        f.Save();
    }
    bool StepAgent(SceneManager& manager, const std::function<bool()>& condition, int frames = 300)
    {
        for (int i = 0; i < frames; ++i)
        {
            manager.UpdateScenes(Timestep(1.f / 60));
            if (condition()) return true;
        }
        return condition();
    }
    Entity WorldAgent(Scene& scene, bool controller = false, uint64_t id = 90001)
    {
        Scene staged;
        auto entity = staged.CreateEntityWithUUID(UUID(id), "Traversal actor");
        entity.GetTransform().Translation = controller ? glm::vec3(1.5f, .02f, 5.5f) : glm::vec3(1.5f, 5.5f, 0);
        auto& a = entity.AddComponent<NavAgentComponent>();
        a.WorldNavigation = true;
        a.Speed = 12;
        a.Acceleration = 0;
        a.RepathInterval = 0;
        a.UpdateRotation = false;
        if (controller)
        {
            auto& cc = entity.AddComponent<CharacterController3DComponent>();
            cc.Radius = .2f;
            cc.Height = 1.2f;
        }
        std::string error;
        CHECK(scene.ImportStagedCell(staged, error).size() == 1);
        return scene.GetEntityByUUID(UUID(id));
    }
    Entity AgentWall(Scene& scene, glm::vec3 position, glm::vec3 size)
    {
        Scene staged;
        auto wall = staged.CreateEntity("Controller wall");
        const auto id = wall.GetUUID();
        wall.GetTransform().Translation = position;
        wall.AddComponent<Rigidbody3DComponent>().Type = Rigidbody3DComponent::BodyType::Static;
        wall.AddComponent<BoxCollider3DComponent>().HalfExtents = size;
        std::string error;
        CHECK(scene.ImportStagedCell(staged, error).size() == 1);
        return scene.GetEntityByUUID(id);
    }
}
TEST(WorldNavAgentWalksPortalsAndRequiresExactCompletionToken)
{
    WorldFixture f;
    ConfigureWorldAgent(f);
    SceneManager manager;
    auto scene = f.Start(manager);
    auto* world = manager.GetWorldStreaming();
    CHECK(Until(manager, [&] { return world->GetStats().AtlasCells == 300; }));
    auto entity = WorldAgent(*scene);
    auto& a = entity.GetComponent<NavAgentComponent>();
    CHECK(scene->SetNavDestination(entity, { 80005.5f, 5.5f, 0 }));
    CHECK(StepAgent(manager, [&] { return scene->GetNavTraversalLink(entity) == 501; }));
    const auto entry = scene->GetWorldPosition(entity);
    const auto ticket = a.TraversalTicket;
    CHECK(!a.IsMoving() && !a.Arrived && ticket);
    Scene foreign;
    auto copy = foreign.CreateEntityWithUUID(entity.GetUUID());
    copy.AddComponent<NavAgentComponent>() = a;
    CHECK(!scene->GetNavTraversalLink(copy));
    CHECK(!scene->CompleteNavTraversal(copy, 501, a.AtlasRevision, ticket));
    CHECK(!scene->SetNavDestination(copy, { 1, 2, 0 }));
    CHECK(!scene->CompleteNavTraversal(entity, 502, a.AtlasRevision, ticket));
    CHECK(!scene->CompleteNavTraversal(entity, 501, a.AtlasRevision + 1, ticket));
    CHECK(!scene->CompleteNavTraversal(entity, 501, a.AtlasRevision, ticket + 1));
    CHECK(scene->GetWorldPosition(entity) == entry);
    CHECK(scene->CompleteNavTraversal(entity, 501, a.AtlasRevision, ticket));
    CHECK(!scene->CompleteNavTraversal(entity, 501, a.AtlasRevision, ticket));
    CHECK_NEAR(scene->GetWorldPosition(entity).x, 40002.5f, .001);
    CHECK(StepAgent(manager, [&] { return scene->GetNavTraversalLink(entity) == 502; }));
    CHECK(scene->CompleteNavTraversal(entity, 502, a.AtlasRevision, a.TraversalTicket));
    CHECK(StepAgent(manager, [&] { return a.Arrived; }));
    CHECK(a.TraversalCompletions == 2 && a.PathStatus == NavPathStatus::Complete);
    CHECK_NEAR(scene->GetWorldPosition(entity).x, 80005.5f, .15);
    manager.Stop();
}
TEST(WorldNavAgentStaleAndCancelledRequestsCannotCompleteLater)
{
    WorldFixture f;
    ConfigureWorldAgent(f);
    SceneManager manager;
    auto scene = f.Start(manager);
    auto* world = manager.GetWorldStreaming();
    CHECK(Until(manager, [&] { return world->GetStats().AtlasCells == 300; }));
    auto entity = WorldAgent(*scene);
    auto& a = entity.GetComponent<NavAgentComponent>();
    CHECK(scene->SetNavDestination(entity, { 40005.5f, 5.5f, 0 }));
    CHECK(StepAgent(manager, [&] { return scene->GetNavTraversalLink(entity) == 501; }));
    const auto revision = a.AtlasRevision, ticket = a.TraversalTicket;
    std::string error;
    CHECK(world->SetNavigationLinks(f.Policy.NavigationLinks, error));
    manager.ProcessRequests();
    CHECK(!scene->GetNavTraversalLink(entity));
    CHECK(!scene->CompleteNavTraversal(entity, 501, revision, ticket));
    CHECK(StepAgent(manager, [&] { return scene->GetNavTraversalLink(entity) == 501; }));
    CHECK(a.BlockedRepaths > 0 && a.TraversalTicket != ticket);
    const auto newTicket = a.TraversalTicket, newRevision = a.AtlasRevision;
    CHECK(scene->CancelNavTraversal(entity));
    CHECK(!a.HasDestination && !a.PendingTraversalLink);
    CHECK(scene->SetNavDestination(entity, { 40005.5f, 5.5f, 0 }));
    CHECK(StepAgent(manager, [&] { return scene->GetNavTraversalLink(entity) == 501; }));
    CHECK(!scene->CompleteNavTraversal(entity, 501, newRevision, newTicket));
    CHECK(scene->CompleteNavTraversal(entity, 501, a.AtlasRevision, a.TraversalTicket));
    manager.Stop();
}
TEST(WorldNavAgentUnloadStopsAndReturnReplans)
{
    WorldFixture f;
    ConfigureWorldAgent(f);
    SceneManager manager;
    auto scene = f.Start(manager);
    auto* world = manager.GetWorldStreaming();
    CHECK(Until(manager, [&] { return world->GetStats().AtlasCells == 300; }));
    auto entity = WorldAgent(*scene);
    auto& a = entity.GetComponent<NavAgentComponent>();
    CHECK(scene->SetNavDestination(entity, { 40005.5f, 5.5f, 0 }));
    CHECK(StepAgent(manager, [&] { return scene->GetNavTraversalLink(entity) != 0; }));
    world->SetViewerOverride({ 1000000, 1000000, 0 });
    CHECK(Until(manager, [&] { return world->GetStats().AtlasCells == 0; }));
    const auto position = scene->GetWorldPosition(entity);
    StepAgent(manager, [] { return false; }, 30);
    CHECK(a.PathStatus == NavPathStatus::Failed && !a.PendingTraversalLink && scene->GetWorldPosition(entity) == position);
    world->SetViewerOverride({ 2, 0, 2 });
    CHECK(Until(manager, [&] { return world->GetStats().AtlasCells == 300; }));
    CHECK(StepAgent(manager, [&] { return scene->GetNavTraversalLink(entity) != 0; }));
    manager.Stop();
}
TEST(WorldNavAgentControllerCannotWalkThroughPhysicalWallOrOccupiedExit)
{
    WorldFixture f;
    ConfigureWorldAgent(f, true);
    SceneManager manager;
    auto scene = f.Start(manager);
    auto* world = manager.GetWorldStreaming();
    CHECK(Until(manager, [&] { return world->GetStats().AtlasCells == 300; }));
    auto entity = WorldAgent(*scene, true);
    auto& a = entity.GetComponent<NavAgentComponent>();
    a.BaseOffset = 5; // transform-only setting must not offset a controller whose origin is its feet
    auto wall = AgentWall(*scene, { 2.1f, .9f, 5.5f }, { .1f, .9f, 2 });
    CHECK(scene->SetNavDestination(entity, { 40005.5f, 0, 5.5f }));
    StepAgent(manager, [] { return false; }, 60);
    CHECK(!a.Arrived && !a.PendingTraversalLink && scene->GetWorldPosition(entity).x < 2);
    scene->DestroyEntity(wall);
    CHECK(StepAgent(manager, [&] { return scene->GetNavTraversalLink(entity) == 501; }));
    auto blocker = AgentWall(*scene, { 40002.5f, .9f, 5.5f }, { .5f, .9f, .5f });
    const auto old = scene->GetWorldPosition(entity);
    CHECK(!scene->CompleteNavTraversal(entity, 501, a.AtlasRevision, a.TraversalTicket));
    CHECK(scene->GetWorldPosition(entity) == old);
    scene->DestroyEntity(blocker);
    CHECK(scene->CompleteNavTraversal(entity, 501, a.AtlasRevision, a.TraversalTicket));
    CHECK(StepAgent(manager, [&] { return a.Arrived; }));
    CHECK_NEAR(scene->GetWorldPosition(entity).x, 40005.5f, .15);
    CHECK(std::abs(scene->GetWorldPosition(entity).y) < .15);
    manager.Stop();
}
TEST(WorldNavAgentSerializationCopyAndRuntimeReset)
{
    auto source = CreateRef<Scene>();
    auto entity = source->CreateEntity("World agent");
    auto& a = entity.AddComponent<NavAgentComponent>();
    a.WorldNavigation = true;
    a.WorldMaxExpanded = 777;
    a.PendingTraversalLink = 5;
    a.TraversalTicket = 123;
    a.AtlasRevision = 99;
    a.PathLinks = { 5 };
    a.TraversalCompletions = 3;
    auto restored = CreateRef<Scene>();
    CHECK(SceneSerializer(restored).DeserializeFromString(SceneSerializer(source).SerializeToString()));
    auto& loaded = restored->FindEntityByName("World agent").GetComponent<NavAgentComponent>();
    CHECK(loaded.WorldNavigation && loaded.WorldMaxExpanded == 777 && !loaded.PendingTraversalLink && loaded.PathLinks.empty());
    auto copy = Scene::Copy(source);
    auto& cloned = copy->FindEntityByName("World agent").GetComponent<NavAgentComponent>();
    CHECK(cloned.WorldNavigation && cloned.WorldMaxExpanded == 777 && !cloned.PendingTraversalLink && !cloned.TraversalTicket && !cloned.TraversalCompletions);
    a.Stop(true);
    CHECK(a.PathLinks.empty() && !a.PendingTraversalLink && !a.TraversalTicket);
}
TEST(WorldNavAgentPeriodicRepathDoesNotTurnBackWithinStartCell)
{
    WorldFixture f;
    ConfigureWorldAgent(f);
    SceneManager manager;
    auto scene = f.Start(manager);
    auto* world = manager.GetWorldStreaming();
    CHECK(Until(manager, [&] { return world->GetStats().AtlasCells == 300; }));
    auto entity = WorldAgent(*scene);
    auto& a = entity.GetComponent<NavAgentComponent>();
    a.RepathInterval = .02f;
    a.Speed = 3;
    CHECK(scene->SetNavDestination(entity, { 8.1f, 5.5f, 0 }));
    float previous = scene->GetWorldPosition(entity).x;
    for (int i = 0; i < 180; ++i)
    {
        manager.UpdateScenes(Timestep(1.f / 60));
        const float next = scene->GetWorldPosition(entity).x;
        CHECK(next + .0001f >= previous);
        previous = next;
    }
    CHECK(a.Arrived && a.PathRequests > 5);
    CHECK(!scene->SetNavDestination(entity, { NAN, 0, 0 }));
    manager.Stop();
}

TEST(WorldNavAgentVisualGraphDrivesDestinationAndPortalCompletion)
{
    WorldFixture f;
    ConfigureWorldAgent(f);
    SceneManager manager;
    auto scene = f.Start(manager);
    auto* world = manager.GetWorldStreaming();
    CHECK(Until(manager, [&] { return world->GetStats().AtlasCells == 300; }));
    Visual::Graph graph;
    graph.Variables["Success"] = { Visual::Type::Bool, false };
    graph.Nodes = { { 1, "Event.Start" }, { 2, "SetWorldDestination", 0, 0, { { "Position", { 40005.5, 5.5, 0 } } } }, { 3, "Event.NavTraversalRequested" }, { 4, "CompleteNavTraversal" }, { 5, "Set.Bool", 0, 0, { { "Name", "Success" } } } };
    graph.Links = { { 1, "Then", 2, "In" }, { 3, "Then", 4, "In" }, { 3, "Link", 4, "Link" }, { 3, "Revision", 4, "Revision" }, { 3, "Ticket", 4, "Ticket" }, { 4, "Then", 5, "In" }, { 4, "Success", 5, "Value" } };
    CHECK(graph.Validate().empty());
    CHECK(graph.Save(f.Assets / "Scripts/Traversal.zgraph"));
    Scene staged;
    auto source = staged.CreateEntityWithUUID(UUID(91001), "Graph traversal actor");
    source.GetTransform().Translation = { 1.5f, 5.5f, 0 };
    source.AddComponent<NavAgentComponent>().Speed = 12;
    auto& script = source.AddComponent<NativeScriptComponent>();
    script.ClassName = "Zamin.VisualGraph";
    script.Fields["Graph"].Set(std::string("Scripts/Traversal.zgraph"));
    std::string error;
    CHECK(scene->ImportStagedCell(staged, error).size() == 1);
    auto entity = scene->GetEntityByUUID(UUID(91001));
    auto& a = entity.GetComponent<NavAgentComponent>();
    CHECK(StepAgent(manager, [&] { return a.Arrived; }, 600));
    auto* host = dynamic_cast<VisualGraphHost*>(entity.GetComponent<NativeScriptComponent>().Instance);
    CHECK(host && host->VM.Error().empty() && host->VM.Variables().at("Success") == true);
    CHECK(a.WorldNavigation && a.TraversalCompletions == 1);
    CHECK_NEAR(scene->GetWorldPosition(entity).x, 40005.5f, .15);
    manager.Stop();
}

TEST(WorldNavAgentGraphOverflowDoesNotChangeNavigationMode)
{
    WorldFixture f;
    ConfigureWorldAgent(f);
    SceneManager manager;
    auto scene = f.Start(manager);
    auto* world = manager.GetWorldStreaming();
    CHECK(Until(manager, [&] { return world->GetStats().AtlasCells == 300; }));
    Visual::Graph graph;
    graph.Nodes = { { 1, "Event.Start" }, { 2, "SetWorldDestination", 0, 0, { { "Position", { 1e100, 5.5, 0 } } } } };
    graph.Links = { { 1, "Then", 2, "In" } };
    CHECK(graph.Save(f.Assets / "Scripts/Overflow.zgraph"));
    Scene staged;
    auto source = staged.CreateEntityWithUUID(UUID(91002), "Invalid graph actor");
    source.GetTransform().Translation = { 1.5f, 5.5f, 0 };
    source.AddComponent<NavAgentComponent>();
    auto& script = source.AddComponent<NativeScriptComponent>();
    script.ClassName = "Zamin.VisualGraph";
    script.Fields["Graph"].Set(std::string("Scripts/Overflow.zgraph"));
    std::string error;
    CHECK(scene->ImportStagedCell(staged, error).size() == 1);
    manager.UpdateScenes(Timestep(1.f / 60));
    auto actor = scene->GetEntityByUUID(UUID(91002));
    auto* host = dynamic_cast<VisualGraphHost*>(actor.GetComponent<NativeScriptComponent>().Instance);
    CHECK(host && !host->VM.Error().empty());
    CHECK(!actor.GetComponent<NavAgentComponent>().WorldNavigation && !actor.GetComponent<NavAgentComponent>().HasDestination);
    CHECK_NEAR(scene->GetWorldPosition(actor).x, 1.5f, .001);
    manager.Stop();
}

namespace {
    bool StepAsyncAgent(SceneManager& manager, const std::function<bool()>& condition)
    {
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        do {
            manager.ProcessRequests();
            manager.UpdateScenes(Timestep(1.f / 60));
            if (condition()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (std::chrono::steady_clock::now() < end);
        return condition();
    }
    void AsyncPolicy(WorldFixture& fixture)
    {
        fixture.Policy.AsyncNavigation = true;
        fixture.Policy.NavigationQueryWorkers = 1;
        fixture.Policy.NavigationQueryCapacity = 4;
        fixture.Policy.NavigationQueryStarts = 1;
        fixture.Policy.NavigationQueryResults = 1;
        fixture.Save();
    }
}
TEST(WorldAsyncNavigationPolicyRoundTripAndLimits)
{
    WorldFixture f;
    ConfigureWorldAgent(f);
    AsyncPolicy(f);
    WorldManifest manifest;
    WorldStreaming::Policy policy;
    std::string error;
    CHECK(WorldStreaming::LoadDefinition(f.Assets / "Worlds/Main.zworld", manifest, policy, error));
    CHECK(policy.AsyncNavigation && policy.NavigationQueryWorkers == 1 && policy.NavigationQueryCapacity == 4 && policy.NavigationQueryStarts == 1);
    policy.NavigationQueryWorkers = 0;
    CHECK(!policy.Validate(error));
    policy.NavigationQueryWorkers = 1;
    policy.NavigationQueryCapacity = 1025;
    CHECK(!policy.Validate(error));
    policy.NavigationQueryCapacity = 4;
    policy.NavigationQueryResults = 0;
    CHECK(!policy.Validate(error));
}
TEST(WorldAsyncNavigationPendingStopsUntilOwnerPublishesAndStillTraverses)
{
    WorldFixture f;
    ConfigureWorldAgent(f);
    AsyncPolicy(f);
    SceneManager manager;
    auto scene = f.Start(manager);
    auto* world = manager.GetWorldStreaming();
    CHECK(Until(manager, [&] { return world->GetStats().AtlasCells == 300; }));
    auto entity = WorldAgent(*scene);
    auto& a = entity.GetComponent<NavAgentComponent>();
    const auto start = scene->GetWorldPosition(entity);
    CHECK(scene->SetNavDestination(entity, { 40005.5f, 5.5f, 0 }));
    CHECK(world->HasNavigationQuery(entity.GetUUID()) && a.PathStatus == NavPathStatus::None && a.Path.empty() && !a.Arrived);
    CHECK(scene->GetWorldPosition(entity) == start);
    CHECK(StepAsyncAgent(manager, [&] { return scene->GetNavTraversalLink(entity) == 501; }));
    CHECK(scene->CompleteNavTraversal(entity, 501, a.AtlasRevision, a.TraversalTicket));
    CHECK(StepAsyncAgent(manager, [&] { return a.Arrived; }));
    manager.ProcessRequests();
    CHECK(world->GetStats().NavigationQueries.Delivered >= 1);
    CHECK_NEAR(scene->GetWorldPosition(entity).x, 40005.5f, .15);
    manager.Stop();
}
TEST(WorldAsyncNavigationRejectsChangedGoalAndCrossedStartCell)
{
    WorldFixture f;
    ConfigureWorldAgent(f);
    AsyncPolicy(f);
    SceneManager manager;
    auto scene = f.Start(manager);
    auto* world = manager.GetWorldStreaming();
    CHECK(Until(manager, [&] { return world->GetStats().AtlasCells == 300; }));
    auto entity = WorldAgent(*scene);
    auto& a = entity.GetComponent<NavAgentComponent>();
    entity.GetTransform().Translation.x = 1.99f;
    CHECK(scene->SetNavDestination(entity, { 40005.5f, 5.5f, 0 }));
    CHECK(Until(manager, [&] { return world->GetStats().NavigationQueries.Available == 1; }));
    entity.GetTransform().Translation.x = 2.01f; // less than .1 drift, but crossed a cell boundary
    NavQueryQueue::Result result;
    CHECK(!world->PollNavigationQuery(entity.GetUUID(), result));
    CHECK(a.Path.empty() && !a.PendingTraversalLink);
    manager.ProcessRequests();
    CHECK(world->GetStats().NavigationStaleResults == 1);
    CHECK(scene->SetNavDestination(entity, { 40005.5f, 5.5f, 0 }));
    CHECK(Until(manager, [&] { return world->GetStats().NavigationQueries.Available == 1; }));
    a.Destination = { 8.5f, 5.5f, 0 };
    CHECK(!world->PollNavigationQuery(entity.GetUUID(), result));
    CHECK(StepAsyncAgent(manager, [&] { return a.Arrived; }));
    CHECK_NEAR(scene->GetWorldPosition(entity).x, 8.5f, .15);
    manager.Stop();
}
TEST(WorldAsyncNavigationCancelDestroyAndTopologyRetirePendingJobs)
{
    WorldFixture f;
    ConfigureWorldAgent(f);
    AsyncPolicy(f);
    SceneManager manager;
    auto scene = f.Start(manager);
    auto* world = manager.GetWorldStreaming();
    CHECK(Until(manager, [&] { return world->GetStats().AtlasCells == 300; }));
    auto entity = WorldAgent(*scene);
    auto& a = entity.GetComponent<NavAgentComponent>();
    CHECK(scene->SetNavDestination(entity, { 40005.5f, 5.5f, 0 }));
    a.Stop(true);
    manager.ProcessRequests();
    CHECK(!world->HasNavigationQuery(entity.GetUUID()));
    CHECK(scene->SetNavDestination(entity, { 40005.5f, 5.5f, 0 }));
    const auto id = entity.GetUUID();
    scene->DestroyEntity(entity);
    manager.ProcessRequests();
    CHECK(!world->HasNavigationQuery(id));
    entity = WorldAgent(*scene);
    CHECK(scene->SetNavDestination(entity, { 40005.5f, 5.5f, 0 }));
    CHECK(Until(manager, [&] { return world->GetStats().NavigationQueries.Available == 1; }));
    std::string error;
    CHECK(world->SetNavigationLinks({}, error));
    manager.ProcessRequests();
    NavQueryQueue::Result result;
    CHECK(!world->PollNavigationQuery(entity.GetUUID(), result));
    CHECK(world->GetStats().NavigationQueries.Cancelled >= 3);
    manager.ClearStreamingWorld();
    manager.Stop();
}
TEST(WorldAsyncNavigationBackpressureEventuallyServesEveryAgent)
{
    WorldFixture f;
    ConfigureWorldAgent(f);
    AsyncPolicy(f);
    f.Policy.NavigationQueryCapacity = 2;
    f.Save();
    SceneManager manager;
    auto scene = f.Start(manager);
    auto* world = manager.GetWorldStreaming();
    CHECK(Until(manager, [&] { return world->GetStats().AtlasCells == 300; }));
    std::vector<Entity> agents;
    for (uint64_t i = 0; i < 8; ++i)
    {
        auto entity = WorldAgent(*scene, false, 92000 + i);
        agents.push_back(entity);
        scene->SetNavDestination(entity, { 8.5f, 5.5f, 0 });
    }
    CHECK(StepAsyncAgent(manager, [&] { return std::all_of(agents.begin(), agents.end(), [](Entity e) { return e.GetComponent<NavAgentComponent>().Arrived; }); }));
    manager.ProcessRequests();
    const auto stats = world->GetStats().NavigationQueries;
    CHECK(stats.Delivered >= 8 && stats.Rejected > 0 && stats.Outstanding <= 2);
    for (auto entity : agents) CHECK_NEAR(scene->GetWorldPosition(entity).x, 8.5f, .15);
    manager.Stop();
}
TEST(WorldAsyncNavigationControllerWallAndExitOccupancyRemainAuthoritative)
{
    WorldFixture f;
    ConfigureWorldAgent(f, true);
    AsyncPolicy(f);
    SceneManager manager;
    auto scene = f.Start(manager);
    auto* world = manager.GetWorldStreaming();
    CHECK(Until(manager, [&] { return world->GetStats().AtlasCells == 300; }));
    auto entity = WorldAgent(*scene, true);
    auto& a = entity.GetComponent<NavAgentComponent>();
    auto wall = AgentWall(*scene, { 2.1f, .9f, 5.5f }, { .1f, .9f, 2 });
    CHECK(scene->SetNavDestination(entity, { 40005.5f, 0, 5.5f }));
    CHECK(StepAsyncAgent(manager, [&] { return !world->HasNavigationQuery(entity.GetUUID()); }));
    StepAgent(manager, [] { return false; }, 60);
    CHECK(!a.PendingTraversalLink && scene->GetWorldPosition(entity).x < 2);
    scene->DestroyEntity(wall);
    CHECK(StepAsyncAgent(manager, [&] { return scene->GetNavTraversalLink(entity) == 501; }));
    auto blocker = AgentWall(*scene, { 40002.5f, .9f, 5.5f }, { .5f, .9f, .5f });
    CHECK(!scene->CompleteNavTraversal(entity, 501, a.AtlasRevision, a.TraversalTicket));
    scene->DestroyEntity(blocker);
    CHECK(scene->CompleteNavTraversal(entity, 501, a.AtlasRevision, a.TraversalTicket));
    CHECK(StepAsyncAgent(manager, [&] { return a.Arrived; }));
    manager.Stop();
}

TEST(WorldAsyncNavigationDoesNotDeliverToRecreatedUUID)
{
    WorldFixture f;
    ConfigureWorldAgent(f);
    AsyncPolicy(f);
    SceneManager manager;
    auto scene = f.Start(manager);
    auto* world = manager.GetWorldStreaming();
    CHECK(Until(manager, [&] { return world->GetStats().AtlasCells == 300; }));
    auto old = WorldAgent(*scene);
    CHECK(scene->SetNavDestination(old, { 40005.5f, 5.5f, 0 }));
    CHECK(Until(manager, [&] { return world->GetStats().NavigationQueries.Available == 1; }));
    const auto handle = uint32_t(old);
    scene->DestroyEntity(old);
    auto replacement = WorldAgent(*scene);
    CHECK(uint32_t(replacement) != handle);
    auto& a = replacement.GetComponent<NavAgentComponent>();
    a.HasDestination = true;
    a.Destination = { 40005.5f, 5.5f, 0 };
    NavQueryQueue::Result result;
    CHECK(!world->PollNavigationQuery(replacement.GetUUID(), result));
    CHECK(a.Path.empty() && !a.PendingTraversalLink);
    manager.Stop();
}

TEST(WorldAsyncNavigationGraphPendingReflectsOwnerQueue)
{
    WorldFixture f;
    ConfigureWorldAgent(f);
    AsyncPolicy(f);
    SceneManager manager;
    auto scene = f.Start(manager);
    auto* world = manager.GetWorldStreaming();
    CHECK(Until(manager, [&] { return world->GetStats().AtlasCells == 300; }));
    Visual::Graph graph;
    graph.Variables["Searching"] = { Visual::Type::Bool, false };
    graph.Nodes = { { 1, "Event.Start" }, { 2, "SetWorldDestination", 0, 0, { { "Position", { 8.5, 5.5, 0 } } } }, { 3, "Event.Update" }, { 4, "NavQueryPending" }, { 5, "Set.Bool", 0, 0, { { "Name", "Searching" } } } };
    graph.Links = { { 1, "Then", 2, "In" }, { 3, "Then", 5, "In" }, { 4, "Value", 5, "Value" } };
    CHECK(graph.Validate().empty());
    CHECK(graph.Save(f.Assets / "Scripts/AsyncState.zgraph"));
    Scene staged;
    auto source = staged.CreateEntityWithUUID(UUID(93001), "Async graph actor");
    source.GetTransform().Translation = { 1.5f, 5.5f, 0 };
    source.AddComponent<NavAgentComponent>();
    auto& script = source.AddComponent<NativeScriptComponent>();
    script.ClassName = "Zamin.VisualGraph";
    script.Fields["Graph"].Set(std::string("Scripts/AsyncState.zgraph"));
    std::string error;
    CHECK(scene->ImportStagedCell(staged, error).size() == 1);
    auto actor = scene->GetEntityByUUID(UUID(93001));
    manager.UpdateScenes(Timestep(1.f / 60));
    auto* host = dynamic_cast<VisualGraphHost*>(actor.GetComponent<NativeScriptComponent>().Instance);
    CHECK(host && host->VM.Error().empty() && host->VM.Variables().at("Searching") == true);
    CHECK(world->HasNavigationQuery(actor.GetUUID()));
    CHECK(StepAsyncAgent(manager, [&] { return !world->HasNavigationQuery(actor.GetUUID()) && host->VM.Variables().at("Searching") == false; }));
    CHECK(host->VM.Error().empty());
    manager.Stop();
}

TEST(WorldAsyncNavigationEmptyAtlasFailsAndReturnResumes)
{
    WorldFixture f;
    ConfigureWorldAgent(f);
    AsyncPolicy(f);
    SceneManager manager;
    auto scene = f.Start(manager);
    auto* world = manager.GetWorldStreaming();
    CHECK(Until(manager, [&] { return world->GetStats().AtlasCells == 300; }));
    auto actor = WorldAgent(*scene);
    auto& a = actor.GetComponent<NavAgentComponent>();
    CHECK(scene->SetNavDestination(actor, { 40005.5f, 5.5f, 0 }));
    world->SetViewerOverride({ 1000000, 1000000, 0 });
    CHECK(Until(manager, [&] { return world->GetStats().AtlasCells == 0; }));
    manager.UpdateScenes(Timestep(1.f / 60));
    CHECK(a.PathStatus == NavPathStatus::Failed && a.Path.empty() && !a.PendingTraversalLink && glm::length(a.Velocity) == 0);
    CHECK(!world->HasNavigationQuery(actor.GetUUID()));
    world->SetViewerOverride({ 2, 0, 2 });
    CHECK(Until(manager, [&] { return world->GetStats().AtlasCells == 300; }));
    CHECK(StepAsyncAgent(manager, [&] { return scene->GetNavTraversalLink(actor) == 501; }));
    manager.Stop();
}

TEST(WorldCrowdSettingsSerializeAndRuntimeReset)
{
    auto source = CreateRef<Scene>();
    auto actor = source->CreateEntity("Crowd actor");
    auto& a = actor.AddComponent<NavAgentComponent>();
    a.WorldNavigation = a.CrowdAvoidance = true;
    a.CrowdNeighborDistance = 7;
    a.CrowdTimeHorizon = 2;
    a.CrowdPriority = 3;
    a.CrowdHeight = 1.2f;
    a.CrowdBlocked = a.CrowdLimited = true;
    a.CrowdNeighbors = 9;
    a.PreferredVelocity = { 1, 2, 3 };
    auto restored = CreateRef<Scene>();
    CHECK(SceneSerializer(restored).DeserializeFromString(SceneSerializer(source).SerializeToString()));
    auto& b = restored->GetEntityByUUID(actor.GetUUID()).GetComponent<NavAgentComponent>();
    CHECK(b.WorldNavigation && b.CrowdAvoidance);
    CHECK_NEAR(b.CrowdNeighborDistance, 7, .001);
    CHECK_NEAR(b.CrowdTimeHorizon, 2, .001);
    CHECK_NEAR(b.CrowdPriority, 3, .001);
    CHECK_NEAR(b.CrowdHeight, 1.2, .001);
    CHECK(b.CrowdNeighbors == 0 && !b.CrowdBlocked && !b.CrowdLimited && b.PreferredVelocity == glm::vec3(0));
    a.ResetRuntime();
    CHECK(a.CrowdAvoidance && a.CrowdPriority == 3 && a.PreferredVelocity == glm::vec3(0));
}
TEST(WorldSmoothingUsesExistingAgentSetting)
{
    WorldFixture f;
    ConfigureWorldAgent(f);
    SceneManager manager;
    auto scene = f.Start(manager);
    auto* world = manager.GetWorldStreaming();
    CHECK(Until(manager, [&] { return world->GetStats().AtlasCells == 300; }));
    auto e = WorldAgent(*scene);
    auto& a = e.GetComponent<NavAgentComponent>();
    a.SmoothPath = false;
    CHECK(scene->SetNavDestination(e, { 8.5f, 5.5f, 0 }));
    const auto raw = a.Path.size();
    a.SmoothPath = true;
    CHECK(scene->SetNavDestination(e, { 8.5f, 5.5f, 0 }));
    CHECK(a.Path.size() < raw && a.Path.size() == 2);
    CHECK(StepAgent(manager, [&] { return a.Arrived; }));
    manager.Stop();
}
TEST(WorldCrowdHeadOnAgentsReachGoalsIn2DAnd3D)
{
    for (bool three : { false, true })
    {
        WorldFixture f;
        ConfigureWorldAgent(f, three);
        SceneManager manager;
        auto scene = f.Start(manager);
        auto* world = manager.GetWorldStreaming();
        CHECK(Until(manager, [&] { return world->GetStats().AtlasCells > 0; }));
        Scene staged;
        std::array<UUID, 2> ids{ UUID(97001), UUID(97002) };
        for (size_t i = 0; i < 2; ++i)
        {
            auto actor = staged.CreateEntityWithUUID(ids[i], "Crowd walker");
            float x = i ? 8.5f : 1.5f;
            actor.GetTransform().Translation = three ? glm::vec3(x, .02f, 5.5f) : glm::vec3(x, 5.5f, 0);
            auto& a = actor.AddComponent<NavAgentComponent>();
            a.WorldNavigation = a.CrowdAvoidance = true;
            a.Speed = 2;
            a.Acceleration = 0;
            a.RepathInterval = 0;
            a.Radius = .25f;
            a.CrowdHeight = 1.2f;
            if (three)
            {
                auto& cc = actor.AddComponent<CharacterController3DComponent>();
                cc.Radius = .2f;
                cc.Height = 1.2f;
            }
        }
        std::string error;
        CHECK(scene->ImportStagedCell(staged, error).size() == 2);
        auto first = scene->GetEntityByUUID(ids[0]), second = scene->GetEntityByUUID(ids[1]);
        CHECK(scene->SetNavDestination(first, three ? glm::vec3(8.5f, 0, 5.5f) : glm::vec3(8.5f, 5.5f, 0)));
        CHECK(scene->SetNavDestination(second, three ? glm::vec3(1.5f, 0, 5.5f) : glm::vec3(1.5f, 5.5f, 0)));
        float separation = 100;
        bool neighbors = false;
        CHECK(StepAgent(manager, [&] {
            auto p = scene->GetWorldPosition(first), q = scene->GetWorldPosition(second);
            if (three) { p.y = q.y = 0; }
            separation = std::min(separation, glm::length(p - q));
            neighbors |= first.GetComponent<NavAgentComponent>().CrowdNeighbors > 0;
            return first.GetComponent<NavAgentComponent>().Arrived && second.GetComponent<NavAgentComponent>().Arrived;
        }, 1200));
        CHECK(separation > .45f && neighbors);
        manager.Stop();
    }
}
TEST(WorldMovementGuardCannotWalkThroughMissingTiles)
{
    WorldFixture f;
    ConfigureWorldAgent(f);
    SceneManager manager;
    auto scene = f.Start(manager);
    auto* world = manager.GetWorldStreaming();
    CHECK(Until(manager, [&] { return world->GetStats().AtlasCells == 300; }));
    auto e = WorldAgent(*scene);
    auto& a = e.GetComponent<NavAgentComponent>();
    CHECK(scene->SetNavDestination(e, { 8.5f, 5.5f, 0 }));
    a.Speed = 1000;
    a.Path.back() = { 40005.5f, 5.5f, 0 };
    bool blocked = false;
    for (int i = 0; i < 20; ++i)
    {
        manager.UpdateScenes(Timestep(.1f));
        CHECK(scene->GetWorldPosition(e).x < 10);
        blocked |= a.CrowdBlocked;
    }
    CHECK(blocked);
    manager.Stop();
}
TEST(WorldAsyncSmoothingChangeRejectsOldOptions)
{
    WorldFixture f;
    ConfigureWorldAgent(f);
    AsyncPolicy(f);
    SceneManager manager;
    auto scene = f.Start(manager);
    auto* world = manager.GetWorldStreaming();
    CHECK(Until(manager, [&] { return world->GetStats().AtlasCells == 300; }));
    auto e = WorldAgent(*scene);
    auto& a = e.GetComponent<NavAgentComponent>();
    CHECK(scene->SetNavDestination(e, { 8.5f, 5.5f, 0 }));
    a.SmoothPath = false;
    CHECK(StepAsyncAgent(manager, [&] { return a.Arrived; }));
    CHECK(world->GetStats().NavigationStaleResults >= 1);
    manager.Stop();
}

namespace {
    Entity RecoveryDoor(Scene& scene, bool three = false, float x = 4.5f)
    {
        auto door = scene.CreateEntity("Recovery barrier");
        door.GetTransform().Translation = three ? glm::vec3(x, 1, 5.5f) : glm::vec3(x, 5.5f, 0);
        door.AddComponent<NavObstacleComponent>().Size = three ? glm::vec3(1, 3, 30) : glm::vec3(1, 30, 1);
        return door;
    }
    void EnableRecovery(NavAgentComponent& a)
    {
        a.RecoverySettings.Enabled = true;
        a.RecoverySettings.StallSeconds = .1f;
        a.RecoverySettings.CooldownSeconds = .1f;
        a.RecoverySettings.MaxRetries = 2;
    }
}
TEST(WorldCarvingPublishesBeforeAgentMotionIn2DAnd3D)
{
    for (bool three : { false, true })
    {
        WorldFixture f;
        ConfigureWorldAgent(f, three);
        SceneManager manager;
        auto scene = f.Start(manager);
        auto* world = manager.GetWorldStreaming();
        CHECK(Until(manager, [&] { return world->GetStats().AtlasCells > 0; }));
        auto actor = WorldAgent(*scene, three);
        auto& a = actor.GetComponent<NavAgentComponent>();
        a.Speed = 1000;
        CHECK(scene->SetNavDestination(actor, three ? glm::vec3(8.5f, 0, 5.5f) : glm::vec3(8.5f, 5.5f, 0)));
        const auto revision = world->GetNavigationAtlas()->GetRevision();
        RecoveryDoor(*scene, three);
        manager.UpdateScenes(Timestep(.1f));
        CHECK(world->GetNavigationAtlas()->GetRevision() != revision);
        CHECK(scene->GetWorldPosition(actor).x < 4);
        CHECK(!world->GetNavigationAtlas()->CanWalkSegment({ 1.5f, 5.5f }, { 8.5f, 5.5f }));
        manager.Stop();
    }
}
TEST(WorldMosaicBindingCarvesSourceTilesWithoutRebuildFeedback)
{
    WorldFixture f;
    ConfigureWorldAgent(f);
    SceneManager manager;
    auto scene = f.Start(manager);
    auto* world = manager.GetWorldStreaming();
    CHECK(Until(manager, [&] { return world->GetStats().AtlasCells == 300; }));
    auto door = RecoveryDoor(*scene);
    auto& obstacle = door.GetComponent<NavObstacleComponent>();
    const auto mosaic = world->GetNavigationEntity(2);
    obstacle.Grid.ID = mosaic;
    scene->UpdateNavObstacles();
    CHECK(obstacle.GetCarvedCellCount() > 0);
    for (const auto& carving : obstacle.Carvings) CHECK(carving.Grid != uint64_t(mosaic));
    const auto revision = world->GetNavigationAtlas()->GetRevision(), builds = world->GetStats().NavigationRebuilds;
    const auto carves = obstacle.CarveCount;
    for (int i = 0; i < 30; ++i)
    {
        manager.ProcessRequests();
        manager.UpdateScenes(Timestep(1.f / 60));
    }
    CHECK(world->GetNavigationAtlas()->GetRevision() == revision && world->GetStats().NavigationRebuilds == builds && obstacle.CarveCount == carves);
    manager.Stop();
}
TEST(WorldObstacleDisableDestroyAndRetainedSnapshotsAreCoherent)
{
    WorldFixture f;
    ConfigureWorldAgent(f);
    SceneManager manager;
    auto scene = f.Start(manager);
    auto* world = manager.GetWorldStreaming();
    CHECK(Until(manager, [&] { return world->GetStats().AtlasCells == 300; }));
    auto door = RecoveryDoor(*scene);
    scene->UpdateNavObstacles();
    auto retained = world->GetNavigationAtlas();
    CHECK(!retained->CanWalkSegment({ 1.5f, 5.5f }, { 8.5f, 5.5f }));
    door.SetActive(false);
    scene->UpdateNavObstacles();
    CHECK(world->GetNavigationAtlas()->CanWalkSegment({ 1.5f, 5.5f }, { 8.5f, 5.5f }));
    door.SetActive(true);
    scene->UpdateNavObstacles();
    CHECK(!world->GetNavigationAtlas()->CanWalkSegment({ 1.5f, 5.5f }, { 8.5f, 5.5f }));
    scene->DestroyEntity(door);
    scene->UpdateNavObstacles();
    CHECK(world->GetNavigationAtlas()->CanWalkSegment({ 1.5f, 5.5f }, { 8.5f, 5.5f }));
    CHECK(!retained->CanWalkSegment({ 1.5f, 5.5f }, { 8.5f, 5.5f }));
    manager.Stop();
}
TEST(WorldObstacleInvalidatesQueuedQueryBeforeDelivery)
{
    WorldFixture f;
    ConfigureWorldAgent(f);
    AsyncPolicy(f);
    SceneManager manager;
    auto scene = f.Start(manager);
    auto* world = manager.GetWorldStreaming();
    CHECK(Until(manager, [&] { return world->GetStats().AtlasCells == 300; }));
    auto actor = WorldAgent(*scene);
    CHECK(scene->SetNavDestination(actor, { 8.5f, 5.5f, 0 }) && world->HasNavigationQuery(actor.GetUUID()));
    RecoveryDoor(*scene);
    scene->UpdateNavObstacles();
    CHECK(!world->HasNavigationQuery(actor.GetUUID()));
    CHECK(world->GetStats().NavigationQueries.Cancelled >= 1);
    CHECK(StepAsyncAgent(manager, [&] { return actor.GetComponent<NavAgentComponent>().Arrived; }));
    CHECK(actor.GetComponent<NavAgentComponent>().PathStatus == NavPathStatus::Partial && scene->GetWorldPosition(actor).x < 4);
    manager.Stop();
}
TEST(WorldRecoveryPhysicalControllerStallHasBoundedRetries)
{
    WorldFixture f;
    ConfigureWorldAgent(f, true);
    SceneManager manager;
    auto scene = f.Start(manager);
    auto* world = manager.GetWorldStreaming();
    CHECK(Until(manager, [&] { return world->GetStats().AtlasCells > 0; }));
    auto actor = WorldAgent(*scene, true);
    auto& a = actor.GetComponent<NavAgentComponent>();
    a.Speed = 2;
    EnableRecovery(a);
    auto wall = AgentWall(*scene, { 3.5f, 1, 5.5f }, { .4f, 1, .7f });
    CHECK(scene->SetNavDestination(actor, { 8.5f, 0, 5.5f }));
    CHECK(StepAgent(manager, [&] { return a.Recovery.IsExhausted(); }, 600));
    CHECK(a.HasDestination && !a.IsMoving() && a.Recovery.GetRetries() == 2 && a.PathRequests == 3);
    CHECK(scene->GetWorldPosition(actor).x < 3.3f && glm::length(a.Velocity) < .001f);
    scene->DestroyEntity(wall);
    a.RepathRequested = true;
    CHECK(StepAgent(manager, [&] { return a.Arrived; }, 600));
    CHECK(a.Recovery.GetRetries() == 0);
    manager.Stop();
}
TEST(WorldRecoveryPartialEndExhaustsAndNewTopologyResumesIntent)
{
    WorldFixture f;
    ConfigureWorldAgent(f);
    SceneManager manager;
    auto scene = f.Start(manager);
    auto* world = manager.GetWorldStreaming();
    CHECK(Until(manager, [&] { return world->GetStats().AtlasCells == 300; }));
    auto actor = WorldAgent(*scene);
    auto& a = actor.GetComponent<NavAgentComponent>();
    EnableRecovery(a);
    auto door = RecoveryDoor(*scene);
    CHECK(scene->SetNavDestination(actor, { 8.5f, 5.5f, 0 }));
    CHECK(StepAgent(manager, [&] { return a.Recovery.IsExhausted(); }, 600));
    CHECK(a.HasDestination && a.PathStatus == NavPathStatus::Partial && !a.IsMoving());
    scene->DestroyEntity(door);
    scene->UpdateNavObstacles();
    CHECK(StepAgent(manager, [&] { return a.Arrived && a.PathStatus == NavPathStatus::Complete; }, 600));
    CHECK(a.Recovery.GetRetries() == 0 && scene->GetWorldPosition(actor).x > 8.3f);
    manager.Stop();
}
TEST(WorldRecoverySettingsSerializeCopyAndClampWithoutRuntimeState)
{
    auto source = CreateRef<Scene>();
    auto actor = source->CreateEntity("Recovering actor");
    auto& a = actor.AddComponent<NavAgentComponent>();
    EnableRecovery(a);
    a.WorldNavigation = true;
    NavRecovery::Input in;
    in.HasIntent = true;
    in.Delta = .1f;
    for (int i = 0; i < 50; ++i) a.Recovery.Observe(a.RecoverySettings, in);
    CHECK(a.Recovery.IsExhausted());
    auto doc = nlohmann::json::parse(SceneSerializer(source).SerializeToString());
    auto restored = CreateRef<Scene>();
    CHECK(SceneSerializer(restored).DeserializeFromString(doc.dump()));
    auto& b = restored->GetEntityByUUID(actor.GetUUID()).GetComponent<NavAgentComponent>();
    CHECK(b.RecoverySettings.Enabled && b.RecoverySettings.MaxRetries == 2 && !b.Recovery.IsExhausted());
    auto copied = Scene::Copy(source);
    CHECK(copied->GetEntityByUUID(actor.GetUUID()).GetComponent<NavAgentComponent>().Recovery.GetRetries() == 0);
    auto& config = doc["Entities"][0]["NavAgent"]["Recovery"];
    config["MaxRetries"] = 999;
    config["StallSeconds"] = -1;
    config["CooldownSeconds"] = 1e9;
    auto clamped = CreateRef<Scene>();
    CHECK(SceneSerializer(clamped).DeserializeFromString(doc.dump()));
    auto& c = clamped->GetEntityByUUID(actor.GetUUID()).GetComponent<NavAgentComponent>();
    CHECK(c.RecoverySettings.Validate() && c.RecoverySettings.MaxRetries == 16 && c.RecoverySettings.StallSeconds == .1f);
}
TEST(WorldObstacleAtPortalExitRevokesCompletionBeforeNextTick)
{
    WorldFixture f;
    ConfigureWorldAgent(f);
    SceneManager manager;
    auto scene = f.Start(manager);
    auto* world = manager.GetWorldStreaming();
    CHECK(Until(manager, [&] { return world->GetStats().AtlasCells == 300; }));
    auto actor = WorldAgent(*scene);
    auto& a = actor.GetComponent<NavAgentComponent>();
    CHECK(scene->SetNavDestination(actor, { 40005.5f, 5.5f, 0 }));
    CHECK(StepAgent(manager, [&] { return scene->GetNavTraversalLink(actor) == 501; }));
    const auto position = scene->GetWorldPosition(actor);
    const auto revision = a.AtlasRevision, ticket = a.TraversalTicket;
    RecoveryDoor(*scene, false, 40002.5f);
    CHECK(!scene->CompleteNavTraversal(actor, 501, revision, ticket));
    CHECK(scene->GetWorldPosition(actor) == position && !scene->GetNavTraversalLink(actor));
    CHECK(world->GetNavigationAtlas()->GetRevision() != revision);
    manager.Stop();
}
TEST(WorldRecoveryGraphReportsExhaustionAndRetriesExplicitly)
{
    WorldFixture f;
    ConfigureWorldAgent(f);
    SceneManager manager;
    auto scene = f.Start(manager);
    auto* world = manager.GetWorldStreaming();
    CHECK(Until(manager, [&] { return world->GetStats().AtlasCells == 300; }));
    Visual::Graph graph;
    graph.Variables["Exhausted"] = { Visual::Type::Bool, false };
    graph.Variables["Success"] = { Visual::Type::Bool, false };
    graph.Nodes = { { 1, "Event.Update" }, { 2, "NavRecoveryState" }, { 3, "Set.Bool", 0, 0, { { "Name", "Exhausted" } } }, { 4, "Event.Message" }, { 5, "RetryNavigation" }, { 6, "Set.Bool", 0, 0, { { "Name", "Success" } } } };
    graph.Links = { { 1, "Then", 3, "In" }, { 2, "Exhausted", 3, "Value" }, { 4, "Then", 5, "In" }, { 5, "Then", 6, "In" }, { 5, "Success", 6, "Value" } };
    CHECK(graph.Save(f.Assets / "Scripts/Recovery.zgraph"));
    Scene staged;
    auto e = staged.CreateEntityWithUUID(UUID(99002), "Recovery graph actor");
    e.GetTransform().Translation = { 1.5f, 5.5f, 0 };
    auto& nav = e.AddComponent<NavAgentComponent>();
    nav.WorldNavigation = true;
    EnableRecovery(nav);
    auto& script = e.AddComponent<NativeScriptComponent>();
    script.ClassName = "Zamin.VisualGraph";
    script.Fields["Graph"].Set(std::string("Scripts/Recovery.zgraph"));
    std::string error;
    CHECK(scene->ImportStagedCell(staged, error).size() == 1);
    auto actor = scene->GetEntityByUUID(UUID(99002));
    auto& a = actor.GetComponent<NavAgentComponent>();
    a.HasDestination = true;
    a.Destination = { 8.5f, 5.5f, 0 };
    a.AtlasRevision = world->GetNavigationAtlas()->GetRevision();
    NavRecovery::Input in;
    in.HasIntent = true;
    in.Delta = .1f;
    for (int i = 0; i < 50; ++i) a.Recovery.Observe(a.RecoverySettings, in);
    manager.UpdateScenes(Timestep(1.f / 60));
    auto* host = dynamic_cast<VisualGraphHost*>(actor.GetComponent<NativeScriptComponent>().Instance);
    CHECK(host && host->VM.Variables().at("Exhausted") == true);
    host->Emit("Message", {});
    CHECK(!a.Recovery.IsExhausted() && a.RepathRequested && host->VM.Variables().at("Success") == true);
    manager.Stop();
}
