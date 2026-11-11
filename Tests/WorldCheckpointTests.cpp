#include "TestAssertions.h"
#include <Zamin/Scene/WorldCheckpoint.h>
#include <Zamin/Scene/WorldCheckpointWriter.h>
#include <thread>
#include <chrono>
#include <Zamin/Scene/WorldAuthoring.h>
#include <Zamin/Scene/WorldStreaming.h>
#include <Zamin/Scene/SceneSerializer.h>
#include <Zamin/Scene/Entity.h>
#include <Zamin/Utils/FileSystem.h>
#include <nlohmann/json.hpp>
#include <sys/stat.h>
#include <set>
using namespace Zamin;
namespace {
    WorldManifest Manifest()
    {
        WorldManifest manifest;
        for (uint64_t i = 1; i <= 3; ++i)
        {
            WorldCell cell;
            cell.ID = UUID(i);
            cell.Coordinate.x = int(i - 1);
            cell.ScenePath = "Cells/" + std::to_string(i) + ".zscene";
            manifest.Cells.push_back(cell);
        }
        return manifest;
    }
    std::string Document(uint64_t id = 100)
    {
        auto scene = CreateRef<Scene>();
        scene->CreateEntityWithUUID(UUID(id), "Persisted");
        return SceneSerializer(scene).SerializeToString();
    }
}
TEST(WorldCheckpointAtomicRoundtripReplacementAndEmptyCell)
{
    TempDirectory temp("zamin-checkpoint");
    WorldCheckpoint state(Manifest());
    std::string error;
    CHECK(state.Put(UUID(1), Document(), error));
    auto first = state.Get(UUID(1));
    auto empty = CreateRef<Scene>();
    CHECK(state.Put(UUID(2), SceneSerializer(empty).SerializeToString(), error));
    CHECK(state.GetCount() == 2 && state.GetBytes() == state.Get(UUID(1))->size() + state.Get(UUID(2))->size());
    CHECK(state.Put(UUID(1), Document(200), error));
    CHECK(first && first->find("Persisted") != std::string::npos);
    const auto file = temp.GetPath() / "slot/checkpoint.json";
    CHECK(state.Save(file, error));
    WorldCheckpoint restored(Manifest());
    CHECK(restored.Load(file, error));
    CHECK(restored.GetCount() == 2 && restored.GetBytes() == state.GetBytes() && *restored.Get(UUID(1)) == *state.Get(UUID(1)));
    CHECK(!restored.Get(UUID(3)) && !restored.Get(UUID(999)));
}
TEST(WorldCheckpointRejectsBrokenFilesWithoutChangingState)
{
    TempDirectory temp("zamin-checkpoint-bad");
    WorldCheckpoint state(Manifest());
    std::string error;
    CHECK(state.Put(UUID(1), Document(), error));
    const auto bytes = state.GetBytes();
    const auto first = state.Get(UUID(1));
    auto file = temp.GetPath() / "bad.json";
    for (const auto& text : { std::string("{"), std::string("[]"), std::string(R"({"Format":"ZaminWorldCheckpoint","Version":2})") })
    {
        CHECK(FileSystem::WriteTextFile(file, text));
        CHECK(!state.Load(file, error));
        CHECK(state.GetBytes() == bytes && state.Get(UUID(1)) == first);
    }
    WorldCheckpoint other(Manifest());
    CHECK(other.Put(UUID(2), Document(), error));
    CHECK(other.Save(file, error));
    auto doc = nlohmann::json::parse(FileSystem::ReadTextFile(file));
    doc["Cells"].push_back(doc["Cells"][0]);
    CHECK(FileSystem::WriteTextFile(file, doc.dump()));
    CHECK(!state.Load(file, error));
    CHECK(state.Get(UUID(1)) == first);
    auto different = Manifest();
    different.Origin.x = 10;
    WorldCheckpoint wrong(different);
    CHECK(!wrong.Load(file, error));
    CHECK(!state.Put(UUID(99), Document(), error));
    CHECK(!state.Put(UUID(1), std::string(WorldCheckpoint::MaxCellBytes + 1, ' '), error));
    CHECK(state.Get(UUID(1)) == first);
}
TEST(WorldCheckpointRejectsDuplicateKeysCyclesAndUnknownParents)
{
    std::string error;
    auto doc = nlohmann::json::parse(Document());
    CHECK(!WorldCheckpoint::ValidateDocument(R"({"Scene":"a","Scene":"b","Version":5,"Entities":[]})", error));
    doc["Entities"].push_back(doc["Entities"][0]);
    CHECK(!WorldCheckpoint::ValidateDocument(doc.dump(), error));
    doc["Entities"][1]["Entity"] = 101;
    doc["Entities"][0]["Parent"] = 101;
    doc["Entities"][1]["Parent"] = 100;
    CHECK(!WorldCheckpoint::ValidateDocument(doc.dump(), error));
    doc["Entities"][1].erase("Parent");
    doc["Entities"][0]["Parent"] = 999;
    CHECK(!WorldCheckpoint::ValidateDocument(doc.dump(), error));
    doc["Entities"][0].erase("Parent");
    doc["Entities"][0]["SubScene"] = { { "Path", "other.zscene" } };
    CHECK(!WorldCheckpoint::ValidateDocument(doc.dump(), error));
    doc["Entities"][0].erase("SubScene");
    CHECK(WorldCheckpoint::ValidateDocument(doc.dump(), error));
}
TEST(WorldCheckpointRejectsSymlinkAndFIFOWritesPreservePreviousFile)
{
    TempDirectory temp("zamin-checkpoint-io");
    WorldCheckpoint state(Manifest());
    std::string error;
    CHECK(state.Put(UUID(1), Document(), error));
    auto file = temp.GetPath() / "good.json";
    CHECK(state.Save(file, error));
    const auto good = FileSystem::ReadTextFile(file);
    auto link = temp.GetPath() / "link";
    std::filesystem::create_symlink(file, link);
    CHECK(!state.Load(link, error));
    auto fifo = temp.GetPath() / "fifo";
    CHECK(mkfifo(fifo.c_str(), 0600) == 0);
    CHECK(!state.Load(fifo, error));
    auto dir = temp.GetPath() / "directory";
    std::filesystem::create_directory(dir);
    CHECK(!state.Save(dir, error));
    CHECK(FileSystem::ReadTextFile(file) == good);
}
TEST(WorldGridAuthoringCreatesPortable2DAnd3DBundles)
{
    for (bool threeD : { false, true })
    {
        TempDirectory temp("zamin-world-grid");
        WorldGridSettings settings;
        settings.ThreeD = threeD;
        settings.Columns = 3;
        settings.Rows = 2;
        settings.CellSize = threeD ? glm::vec2(32) : glm::vec2(32, 18);
        WorldGridResult result;
        std::string error;
        CHECK(CreateWorldGrid(temp.GetPath(), settings, result, error));
        CHECK(result.Cells == 6 && result.Files == 15 && result.Bytes > 0);
        WorldManifest manifest;
        WorldStreaming::Policy policy;
        CHECK(WorldStreaming::LoadDefinition(temp.GetPath() / result.World, manifest, policy, error));
        CHECK(manifest.Cells.size() == 6 && policy.NavigationMode == uint32_t(threeD ? 3 : 2));
        std::set<uint64_t> ids;
        for (const auto& cell : manifest.Cells)
        {
            CHECK(!WorldManifest::ResolveScene(temp.GetPath(), cell).empty());
            auto scene = CreateRef<Scene>();
            CHECK(SceneSerializer(scene).Deserialize(temp.GetPath() / cell.ScenePath));
            for (auto entity : scene->GetEntitiesInOrder()) CHECK(ids.insert(uint64_t(entity.GetUUID())).second);
            if (threeD) CHECK(scene->FindEntityByName("World terrain").GetComponent<TerrainComponent>().LockBorderLOD);
            else CHECK(scene->FindEntityByName("Cell navigation").HasComponent<NavGrid2DComponent>());
        }
        auto anchor = CreateRef<Scene>();
        CHECK(SceneSerializer(anchor).Deserialize(temp.GetPath() / result.Anchor));
        CHECK(anchor->GetPrimaryCameraEntity());
        for (auto entity : anchor->GetEntitiesInOrder()) CHECK(ids.insert(uint64_t(entity.GetUUID())).second);
        CHECK(!CreateWorldGrid(temp.GetPath(), settings, result, error)); // no replacement of a published bundle
        CHECK(manifest.Cells.size() == 6 && std::filesystem::exists(temp.GetPath() / result.World));
        for (const auto& entry : std::filesystem::directory_iterator(temp.GetPath())) CHECK(!entry.path().filename().string().starts_with(".world-stage-"));
    }
}
TEST(WorldGridAuthoringBoundsFailureAndSymlinkProtection)
{
    TempDirectory temp("zamin-world-grid-errors");
    WorldGridSettings settings;
    WorldGridResult result;
    result.Cells = 99;
    std::string error;
    for (const char* name : { "../escape", "bad/name", ".", "name with spaces" })
    {
        settings.Folder = name;
        CHECK(!CreateWorldGrid(temp.GetPath(), settings, result, error));
        CHECK(result.Cells == 99);
    }
    settings.Folder = "Valid";
    settings.Columns = 0;
    CHECK(!CreateWorldGrid(temp.GetPath(), settings, result, error));
    settings.Columns = 32;
    settings.Rows = 32;
    CHECK(!CreateWorldGrid(temp.GetPath(), settings, result, error));
    settings.Columns = 2;
    settings.Rows = 2;
    settings.NavigationCellSize = 3;
    CHECK(!CreateWorldGrid(temp.GetPath(), settings, result, error));
    settings.NavigationCellSize = 2;
    auto link = temp.GetPath() / "link";
    std::filesystem::create_directory_symlink(temp.GetPath(), link);
    CHECK(!CreateWorldGrid(link, settings, result, error));
    settings.ThreeD = true;
    settings.Terrain = false;
    CHECK(CreateWorldGrid(temp.GetPath(), settings, result, error));
}

TEST(WorldGridAuthoringClonesTemplateHierarchyAndRemapsReferences)
{
    TempDirectory temp("zamin-world-template");
    auto source = CreateRef<Scene>();
    auto parent = source->CreateEntityWithUUID(UUID(700), "Template parent"), child = source->CreateEntityWithUUID(UUID(701), "Template child");
    source->SetParent(child, parent, false);
    child.GetTransform().Translation = { 1, 2, 3 };
    CHECK(FileSystem::WriteTextFile(temp.GetPath() / "Template.zscene", SceneSerializer(source).SerializeToString()));
    WorldGridSettings settings;
    settings.Columns = 2;
    settings.Rows = 1;
    settings.TemplateScene = "Template.zscene";
    WorldGridResult result;
    std::string error;
    CHECK(CreateWorldGrid(temp.GetPath(), settings, result, error));
    WorldManifest manifest;
    WorldStreaming::Policy policy;
    CHECK(WorldStreaming::LoadDefinition(temp.GetPath() / result.World, manifest, policy, error));
    std::set<uint64_t> ids;
    for (const auto& cell : manifest.Cells)
    {
        auto clone = CreateRef<Scene>();
        CHECK(SceneSerializer(clone).Deserialize(temp.GetPath() / cell.ScenePath));
        auto p = clone->FindEntityByName("Template parent"), c = clone->FindEntityByName("Template child");
        CHECK(p && c && p.GetUUID() != UUID(700) && c.GetUUID() != UUID(701));
        CHECK(clone->GetParent(c) == p);
        CHECK(ids.insert(uint64_t(p.GetUUID())).second && ids.insert(uint64_t(c.GetUUID())).second);
        CHECK(c.GetTransform().Translation == glm::vec3(1, 2, 3));
    }
}
TEST(WorldGridAuthoringTemplateFailureLeavesNoBundle)
{
    TempDirectory temp("zamin-world-template-bad");
    auto source = CreateRef<Scene>();
    source->CreateEntity("Forbidden cell camera").AddComponent<CameraComponent>();
    CHECK(FileSystem::WriteTextFile(temp.GetPath() / "Camera.zscene", SceneSerializer(source).SerializeToString()));
    WorldGridSettings settings;
    settings.TemplateScene = "Camera.zscene";
    WorldGridResult result;
    std::string error;
    CHECK(!CreateWorldGrid(temp.GetPath(), settings, result, error));
    CHECK(!std::filesystem::exists(temp.GetPath() / settings.Folder));
    settings.TemplateScene = "../outside.zscene";
    CHECK(!CreateWorldGrid(temp.GetPath(), settings, result, error));
    for (const auto& entry : std::filesystem::directory_iterator(temp.GetPath())) CHECK(!entry.path().filename().string().starts_with(".world-stage-"));
}

TEST(WorldCheckpointWriterIsBoundedAndOwnsImmutableSnapshot)
{
    TempDirectory temp("zamin-checkpoint-writer");
    WorldCheckpoint state(Manifest());
    std::string error;
    CHECK(state.Put(UUID(1), Document(100), error));
    WorldCheckpointWriter writer;
    auto file = temp.GetPath() / "async.json";
    CHECK(writer.Submit(state, file, 7, error));
    CHECK(writer.IsBusy());
    CHECK(!writer.Submit(state, file, 8, error));
    CHECK(state.Put(UUID(1), Document(200), error)); // an active world may change while the old snapshot is writing
    WorldCheckpointWriter::Result result;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!writer.Poll(result) && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    CHECK(result.Serial == 7 && result.Success && !writer.IsBusy());
    WorldCheckpoint restored(Manifest());
    CHECK(restored.Load(file, error));
    auto doc = nlohmann::json::parse(*restored.Get(UUID(1)));
    CHECK(doc["Entities"][0]["Entity"] == 100);
}
TEST(WorldCheckpointWriterJoinsAndReportsIOFailure)
{
    TempDirectory temp("zamin-checkpoint-writer-exit");
    WorldCheckpoint state(Manifest());
    std::string error;
    CHECK(state.Put(UUID(1), Document(), error));
    const auto file = temp.GetPath() / "exit.json";
    {
        WorldCheckpointWriter writer;
        CHECK(writer.Submit(state, file, 1, error));
    }
    CHECK(std::filesystem::exists(file));
    WorldCheckpoint restored(Manifest());
    CHECK(restored.Load(file, error));
    auto blocked = temp.GetPath() / "blocked";
    std::filesystem::create_directory(blocked);
    WorldCheckpointWriter writer;
    CHECK(writer.Submit(state, blocked, 2, error));
    WorldCheckpointWriter::Result result;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!writer.Poll(result) && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    CHECK(result.Serial == 2 && !result.Success && !result.Error.empty());
    CHECK(restored.Load(file, error));
}
