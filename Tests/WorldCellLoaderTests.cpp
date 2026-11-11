#include <Zamin/Scene/WorldCellLoader.h>
#include <Zamin/Scene/SceneSerializer.h>
#include <Zamin/Scene/Entity.h>
#include <Zamin/Utils/FileSystem.h>
#include "TestAssertions.h"
#include <chrono>
#include <future>
#include <thread>
#include <sys/stat.h>

using namespace Zamin;
namespace {
    const std::string SceneText = R"({"Scene":"Cell","Version":5,"Entities":[{"Entity":99,"Tag":"Worker entity"}]})";
    struct CellFixture
    {
        TempDirectory Temp{ "zamin-cell-loader" };
        std::filesystem::path Assets;
        WorldManifest World;
        WorldCellLoader::Limits Limits;
        CellFixture(bool dependency = true)
        {
            if (!Temp) throw std::runtime_error("Temporary directory unavailable");
            Assets = Temp.GetPath() / "Assets";
            Limits.MaxCellBytes = 65536;
            Limits.MaxReservedBytes = 131072;
            Limits.MaxWantedCells = 8;
            Limits.MaxJSONNodes = 2048;
            Limits.MaxEntities = 128;
            for (int i = 1; i <= 4; ++i)
            {
                WorldCell c;
                c.ID = UUID(i);
                c.Coordinate.x = i;
                c.ScenePath = "Scenes/Cell" + std::to_string(i) + ".zscene";
                if (dependency && i == 2) c.Dependencies = { UUID(1) };
                World.Cells.push_back(c);
                if (!FileSystem::WriteTextFile(Assets / c.ScenePath, SceneText)) throw std::runtime_error("Cannot write test scene");
            }
        }
        std::unique_ptr<WorldCellLoader> Create()
        {
            std::string error;
            auto loader = WorldCellLoader::Create(World, Assets, Limits, error);
            CHECK(loader != nullptr && error.empty());
            return loader;
        }
    };
    bool Await(WorldCellLoader& loader, UUID id, WorldCellStage expected)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        do
        {
            loader.Poll();
            auto state = loader.GetStatus(id);
            if (state.Stage == expected) return true;
            if (state.Stage == WorldCellStage::Failed) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (std::chrono::steady_clock::now() < deadline);
        return false;
    }
}

TEST(WorldCellLoaderDependencyClosureBackpressureAndRealSceneBuild)
{
    CellFixture f;
    f.Limits.MaxReservedBytes = f.Limits.MaxCellBytes;
    auto loader = f.Create();
    if (!loader) return;
    std::string error;
    CHECK(loader->SetWanted({ UUID(2) }, error));
    CHECK(loader->GetStats().Wanted == 2);
    CHECK(Await(*loader, UUID(1), WorldCellStage::Ready));
    CHECK(loader->GetStatus(UUID(2)).Stage == WorldCellStage::Queued);
    CHECK(loader->GetStats().ReservedSourceBytes == f.Limits.MaxCellBytes);
    auto root = loader->GetStatus(UUID(1));
    CHECK(!loader->TakeReady(UUID(1), root.Generation + 1));
    auto job = loader->TakeReady(UUID(1), root.Generation);
    CHECK(job && !job->IsBuilt() && job->GetEntityCount() == 1);
    if (job)
    {
        Scene scene;
        for (int i = 0; i < 100 && !job->IsBuilt(); ++i) job->Build(scene, 1.0);
        CHECK(job->IsBuilt() && job->GetError().empty());
        auto entity = scene.GetEntityByUUID(UUID(99));
        CHECK(entity && entity.GetName() == "Worker entity");
    }
    CHECK(Await(*loader, UUID(2), WorldCellStage::Ready));
    auto child = loader->GetStatus(UUID(2));
    auto childJob = loader->TakeReady(UUID(2), child.Generation);
    CHECK(childJob && child.SourceBytes == SceneText.size());
    CHECK(loader->GetStats().ReservedSourceBytes == 0);
    CHECK(loader->GetStats().Transferred == 2 && loader->GetStats().WorkerJobs == 2);
    CHECK(loader->SetWanted({}, error));
    CHECK(loader->GetStatus(UUID(1)).Stage == WorldCellStage::Unloaded);
}

TEST(WorldCellLoaderCancellationDropsOldGenerationAndRetriesExplicitly)
{
    CellFixture f;
    auto loader = f.Create();
    if (!loader) return;
    std::string error;
    CHECK(loader->SetWanted({ UUID(1) }, error));
    const auto old = loader->GetStatus(UUID(1)).Generation;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (loader->GetStatus(UUID(1)).Stage == WorldCellStage::Queued && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    // Worker entered this request; without Poll even a finished parse stays unpublished.
    CHECK(loader->GetStatus(UUID(1)).Stage == WorldCellStage::Reading);
    CHECK(loader->GetStats().WorkerJobs >= 1);
    CHECK(loader->SetWanted({}, error));
    CHECK(loader->SetWanted({ UUID(1) }, error));
    const auto fresh = loader->GetStatus(UUID(1)).Generation;
    CHECK(fresh > old);
    CHECK(Await(*loader, UUID(1), WorldCellStage::Ready));
    CHECK(!loader->TakeReady(UUID(1), old));
    CHECK(loader->GetStats().StaleCompletions >= 1);
    CHECK(loader->TakeReady(UUID(1), fresh) != nullptr);
    CHECK(loader->SetWanted({}, error));
    CHECK(FileSystem::WriteTextFile(f.Assets / f.World.Cells[0].ScenePath, "{broken"));
    CHECK(loader->SetWanted({ UUID(1) }, error));
    CHECK(Await(*loader, UUID(1), WorldCellStage::Failed));
    const auto failed = loader->GetStatus(UUID(1)).Generation;
    CHECK(FileSystem::WriteTextFile(f.Assets / f.World.Cells[0].ScenePath, SceneText));
    CHECK(loader->SetWanted({ UUID(1) }, error));
    CHECK(loader->GetStatus(UUID(1)).Stage == WorldCellStage::Failed);
    CHECK(loader->GetStatus(UUID(1)).Generation == failed);
    CHECK(loader->SetWanted({}, error) && loader->SetWanted({ UUID(1) }, error));
    CHECK(Await(*loader, UUID(1), WorldCellStage::Ready));
    CHECK(loader->GetStatus(UUID(1)).Generation > failed);
}

TEST(WorldCellLoaderInvalidRequestsPreserveWantedAndDependencyFailurePropagates)
{
    CellFixture f;
    f.Limits.MaxWantedCells = 1;
    auto limited = f.Create();
    if (!limited) return;
    std::string error;
    CHECK(limited->SetWanted({ UUID(3) }, error));
    const auto generation = limited->GetStatus(UUID(3)).Generation;
    CHECK(!limited->SetWanted({ UUID(2) }, error)); // closure requires two slots
    CHECK(!limited->SetWanted({ UUID(999) }, error));
    CHECK(limited->GetStats().Wanted == 1 && limited->GetStatus(UUID(3)).Generation == generation);
    CHECK(Await(*limited, UUID(3), WorldCellStage::Ready));
    CHECK(limited->Poll(0) == 0);
    CHECK(!limited->TakeReady(UUID(999), generation));
    auto loader = WorldCellLoader::Create(f.World, f.Assets, WorldCellLoader::Limits{}, error);
    CHECK(loader != nullptr);
    if (!loader) return;
    std::filesystem::remove(f.Assets / f.World.Cells[0].ScenePath);
    CHECK(loader->SetWanted({ UUID(2) }, error));
    CHECK(Await(*loader, UUID(2), WorldCellStage::Failed));
    CHECK(loader->GetStatus(UUID(1)).Stage == WorldCellStage::Failed);
    CHECK(loader->GetStats().WorkerJobs == 1 && loader->GetStats().ReservedSourceBytes == 0);
}

TEST(WorldCellLoaderRejectsUntrustedSceneStructuresAndLimits)
{
    CellFixture f;
    std::vector<std::string> bad{
        "{broken", "[]", R"({"Scene":"Cell","Entities":[]})",
        R"({"Scene":"Cell","Version":-1,"Entities":[]})",
        R"({"Scene":"Cell","Version":999,"Entities":[]})",
        R"({"Scene":"Cell","Version":5.0,"Entities":[]})",
        R"({"Scene":42,"Version":5,"Entities":[]})",
        R"({"Scene":"Cell","Version":5,"Entities":{}})",
        R"({"Scene":"Cell","Version":5,"Version":5,"Entities":[]})",
        R"({"Scene":"Cell","Version":5,"Entities":[],"Extra":)" + std::string(65, '[') + "0" + std::string(65, ']') + "}",
        std::string(f.Limits.MaxCellBytes + 1, ' ')
    };
    for (const auto& text : bad)
    {
        CHECK(FileSystem::WriteTextFile(f.Assets / f.World.Cells[0].ScenePath, text));
        auto loader = f.Create();
        if (!loader) return;
        std::string error;
        CHECK(loader->SetWanted({ UUID(1) }, error));
        CHECK(Await(*loader, UUID(1), WorldCellStage::Failed));
        CHECK(!loader->GetStatus(UUID(1)).Error.empty());
        CHECK(loader->GetStats().ReservedSourceBytes == 0);
    }
    for (bool nodeLimit : { false, true })
    {
        f.Limits.MaxEntities = nodeLimit ? 128 : 1;
        f.Limits.MaxJSONNodes = nodeLimit ? 5 : 2048;
        CHECK(FileSystem::WriteTextFile(f.Assets / f.World.Cells[0].ScenePath, R"({"Scene":"Cell","Version":5,"Entities":[{},{}]})"));
        auto loader = f.Create();
        if (!loader) return;
        std::string error;
        CHECK(loader->SetWanted({ UUID(1) }, error));
        CHECK(Await(*loader, UUID(1), WorldCellStage::Failed));
    }
}

TEST(WorldCellLoaderRejectsSymlinksAndSpecialFiles)
{
    CellFixture f;
    const auto file = f.Assets / f.World.Cells[0].ScenePath;
    const auto external = f.Temp.GetPath() / "Outside.zscene";
    CHECK(FileSystem::WriteTextFile(external, SceneText));
    for (int mode = 0; mode < 3; ++mode)
    {
        std::filesystem::remove(file);
        if (mode == 0) std::filesystem::create_symlink(external, file);
        else if (mode == 1) { CHECK(mkfifo(file.c_str(), 0600) == 0); }
        else std::filesystem::create_directory(file);
        auto loader = f.Create();
        if (!loader) return;
        std::string error;
        CHECK(loader->SetWanted({ UUID(1) }, error));
        CHECK(Await(*loader, UUID(1), WorldCellStage::Failed));
    }
    std::filesystem::remove_all(f.Assets / "Scenes");
    std::filesystem::create_directory_symlink(f.Temp.GetPath(), f.Assets / "Scenes");
    auto loader = f.Create();
    if (!loader) return;
    std::string error;
    CHECK(loader->SetWanted({ UUID(1) }, error));
    CHECK(Await(*loader, UUID(1), WorldCellStage::Failed));
}

TEST(WorldCellLoaderPinsAssetRootAgainstPathReplacement)
{
    CellFixture f;
    auto loader = f.Create();
    if (!loader) return;
    const auto moved = f.Temp.GetPath() / "OriginalAssets";
    std::filesystem::rename(f.Assets, moved);
    CHECK(FileSystem::WriteTextFile(f.Assets / f.World.Cells[0].ScenePath, "{broken replacement"));
    std::string error;
    CHECK(loader->SetWanted({ UUID(1) }, error));
    CHECK(Await(*loader, UUID(1), WorldCellStage::Ready));
    auto job = loader->TakeReady(UUID(1), loader->GetStatus(UUID(1)).Generation);
    CHECK(job && job->GetEntityCount() == 1);
}

TEST(WorldCellLoaderRapidCameraChangesKeepReservationsBounded)
{
    CellFixture f(false);
    f.Limits.MaxReservedBytes = f.Limits.MaxCellBytes;
    auto loader = f.Create();
    if (!loader) return;
    std::string error;
    UUID last(0);
    for (size_t i = 0; i < 100; ++i)
    {
        last = UUID(i % 4 + 1);
        CHECK(loader->SetWanted({ last }, error));
        loader->Poll(1);
        const auto stats = loader->GetStats();
        CHECK(stats.Wanted == 1 && stats.ReservedSourceBytes <= f.Limits.MaxReservedBytes && stats.InFlight <= 1);
    }
    CHECK(Await(*loader, last, WorldCellStage::Ready));
    for (uint64_t id = 1; id <= 4; ++id)
        if (UUID(id) != last) CHECK(loader->GetStatus(UUID(id)).Stage == WorldCellStage::Unloaded);
    CHECK(loader->SetWanted({}, error));
    CHECK(loader->GetStats().ReservedSourceBytes == 0);
    // Destruction while work is queued/reading must stop workers before freeing their data/root FD.
    for (int i = 0; i < 8; ++i)
    {
        auto pending = f.Create();
        if (!pending) return;
        CHECK(pending->SetWanted({ UUID(1), UUID(3) }, error));
    }
}

TEST(WorldCellLoaderValidatesConfigurationAndOwnerThread)
{
    CellFixture f;
    std::string error;
    auto invalid = f.Limits;
    invalid.Workers = 0;
    CHECK(!WorldCellLoader::Create(f.World, f.Assets, invalid, error));
    invalid = f.Limits;
    invalid.MaxReservedBytes = invalid.MaxCellBytes - 1;
    CHECK(!WorldCellLoader::Create(f.World, f.Assets, invalid, error));
    CHECK(!WorldCellLoader::Create(f.World, {}, f.Limits, error));
    CHECK(!WorldCellLoader::Create(f.World, f.Temp.GetPath() / "missing", f.Limits, error));
    auto badWorld = f.World;
    badWorld.Cells[0].ScenePath = "../Outside.zscene";
    CHECK(!WorldCellLoader::Create(badWorld, f.Assets, f.Limits, error));
    auto loader = f.Create();
    if (!loader) return;
    auto wrongThread = std::async(std::launch::async, [&] {
        try
        {
            loader->GetStats();
        }
        catch (const std::logic_error&)
        {
            return true;
        }
        return false;
    });
    CHECK(wrongThread.get());
    CHECK(loader->GetStats().WorkerJobs == 0);
}

TEST(WorldCellLoaderStagesReal2DAnd3DScenesWithoutActivation)
{
    CellFixture f(false);
    f.Limits = WorldCellLoader::Limits{};
    const auto source = std::filesystem::path(ZAMIN_SOURCE_DIR);
    const std::filesystem::path scenes[] = {
        source / "Projects/Platformer2D/Assets/Scenes/Level1.zscene",
        source / "Projects/FoliageWorkshop/Assets/Scenes/Forest.zscene"
    };
    size_t expected[2]{};
    for (size_t i = 0; i < 2; ++i)
    {
        SceneLoadJob reference;
        CHECK(reference.ParseString(FileSystem::ReadTextFile(scenes[i])));
        expected[i] = reference.GetEntityCount();
        CHECK(expected[i] > 0);
        std::filesystem::copy_file(scenes[i], f.Assets / f.World.Cells[i].ScenePath,
                                   std::filesystem::copy_options::overwrite_existing);
    }
    auto loader = f.Create();
    if (!loader) return;
    std::string error;
    CHECK(loader->SetWanted({ UUID(1), UUID(2) }, error));
    for (size_t i = 0; i < 2; ++i)
    {
        const UUID id(i + 1);
        CHECK(Await(*loader, id, WorldCellStage::Ready));
        auto status = loader->GetStatus(id);
        auto job = loader->TakeReady(id, status.Generation);
        CHECK(job && !job->IsBuilt() && job->GetEntityCount() == expected[i]);
        if (job && i == 0) CHECK(job->GetMigration().Migrated());
    }
    CHECK(loader->GetStats().WorkerJobs == 2 && loader->GetStats().ReservedSourceBytes == 0);
}

TEST(WorldCellOverrideIsImmutableUntilNextGeneration)
{
    CellFixture fixture(false);
    auto loader = fixture.Create();
    std::string error;
    auto first = std::make_shared<const std::string>(R"({"Scene":"Overlay","Version":5,"Entities":[{"Entity":700,"Tag":"First override"}]})");
    CHECK(loader->SetSourceOverride(UUID(1), first, error));
    CHECK(loader->GetStats().OverrideBytes == first->size());
    CHECK(loader->SetWanted({ UUID(1) }, error));
    CHECK(Await(*loader, UUID(1), WorldCellStage::Ready));
    auto second = std::make_shared<const std::string>(R"({"Scene":"Overlay","Version":5,"Entities":[{"Entity":701,"Tag":"Second override"}]})");
    CHECK(loader->SetSourceOverride(UUID(1), second, error));
    auto status = loader->GetStatus(UUID(1));
    auto job = loader->TakeReady(UUID(1), status.Generation);
    Scene scene;
    CHECK(job && job->Build(scene, 100));
    CHECK(scene.GetEntityByUUID(UUID(700)) && !scene.GetEntityByUUID(UUID(701)));
    CHECK(loader->SetWanted({}, error));
    CHECK(loader->SetWanted({ UUID(1) }, error));
    CHECK(Await(*loader, UUID(1), WorldCellStage::Ready));
    status = loader->GetStatus(UUID(1));
    job = loader->TakeReady(UUID(1), status.Generation);
    Scene next;
    CHECK(job && job->Build(next, 100));
    CHECK(next.GetEntityByUUID(UUID(701)));
    CHECK(loader->SetWanted({}, error));
    CHECK(loader->SetSourceOverride(UUID(1), nullptr, error));
    CHECK(loader->GetStats().OverrideBytes == 0);
    CHECK(loader->SetWanted({ UUID(1) }, error));
    CHECK(Await(*loader, UUID(1), WorldCellStage::Ready));
    status = loader->GetStatus(UUID(1));
    job = loader->TakeReady(UUID(1), status.Generation);
    Scene authored;
    CHECK(job && job->Build(authored, 100));
    CHECK(authored.GetEntityByUUID(UUID(99)));
}
TEST(WorldCellOverrideBoundsMalformedAndMissingAuthoredSource)
{
    CellFixture fixture(false);
    auto loader = fixture.Create();
    std::string error;
    CHECK(!loader->SetSourceOverride(UUID(999), std::make_shared<const std::string>(SceneText), error));
    CHECK(!loader->SetSourceOverride(UUID(1), std::make_shared<const std::string>(fixture.Limits.MaxCellBytes + 1, 'x'), error));
    CHECK(loader->SetSourceOverride(UUID(1), std::make_shared<const std::string>("{"), error));
    CHECK(loader->SetWanted({ UUID(1) }, error));
    CHECK(Await(*loader, UUID(1), WorldCellStage::Failed));
    CHECK(loader->SetWanted({}, error));
    CHECK(loader->SetSourceOverride(UUID(1), std::make_shared<const std::string>(SceneText), error));
    std::filesystem::remove(fixture.Assets / fixture.World.Cells[0].ScenePath);
    CHECK(loader->SetWanted({ UUID(1) }, error));
    CHECK(Await(*loader, UUID(1), WorldCellStage::Ready));
}
