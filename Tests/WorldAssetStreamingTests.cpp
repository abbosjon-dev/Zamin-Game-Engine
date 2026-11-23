// Hard budgets of streamed cell assets: AsyncLoader's upload budget, the staging workers and the
// world policy. CPU only - the GL side (row uploads, real cells) is in ResidencyGPUTests.cpp.
#include <Zamin/Asset/AsyncLoader.h>
#include <Zamin/Asset/AssetImportLog.h>
#include <Zamin/Renderer/Model.h>
#include <Zamin/Renderer/Texture.h>
#include <Zamin/Scene/WorldAssetStager.h>
#include <Zamin/Scene/WorldStreaming.h>
#include <Zamin/Scene/SceneManager.h>
#include <Zamin/Scene/SceneSerializer.h>
#include <Zamin/Scene/Entity.h>
#include <Zamin/Project/Project.h>
#include <Zamin/Utils/FileSystem.h>
#include "TestAssertions.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <thread>

using namespace Zamin;
namespace {
    // Uncompressed RGB TGA: a real image file whose decoded size is width * height * 3.
    void WriteImage(const std::filesystem::path& path, uint16_t width, uint16_t height, uint8_t color = 90)
    {
        std::vector<uint8_t> data(18 + size_t(width) * height * 3, color);
        std::fill(data.begin(), data.begin() + 18, uint8_t(0));
        data[2] = 2;
        data[12] = uint8_t(width & 255);
        data[13] = uint8_t(width >> 8);
        data[14] = uint8_t(height & 255);
        data[15] = uint8_t(height >> 8);
        data[16] = 24;
        CHECK(FileSystem::WriteBinaryFile(path, data.data(), data.size()));
    }
    struct BudgetScope
    {
        AsyncLoader::UploadBudget Previous = AsyncLoader::GetUploadBudget();
        explicit BudgetScope(AsyncLoader::UploadBudget budget)
        {
            AsyncLoader::WaitAll();
            AsyncLoader::SetUploadBudget(budget);
            AsyncLoader::ResetStats();
        }
        ~BudgetScope()
        {
            AsyncLoader::WaitAll();
            AsyncLoader::SetUploadBudget(Previous);
        }
    };
    bool WaitFor(const WorldAssetStager& stager, uint64_t ticket, WorldAssetStager::State state)
    {
        for (int i = 0; i < 20000; ++i)
        {
            if (stager.GetStatus(ticket).Stage == state) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return false;
    }
    WorldAssetStager::Request Tex(const std::filesystem::path& file, bool pixelArt = false)
    {
        WorldAssetStager::Request request;
        request.File = file;
        request.PixelArt = pixelArt;
        return request;
    }
}

TEST(AsyncLoaderUploadBudgetBoundsBytesAndCallsPerUpdateInOrder)
{
    BudgetScope scope({ 3, 100 });
    std::vector<int> order;
    int owners[6];
    const uint64_t sizes[6] = { 40, 40, 40, 10, 10, 10 };
    for (int i = 0; i < 6; ++i) AsyncLoader::QueueUpload(&owners[i], [&order, i] { order.push_back(i); }, sizes[i]);
    CHECK(AsyncLoader::GetPendingUploadBytes() == 150);
    // 40 + 40 fit, the third 40 would pass 100: it waits although smaller ones are behind it (order kept).
    AsyncLoader::Update(1000.0);
    CHECK(order == std::vector<int>({ 0, 1 }));
    // Then 40 + 10 + 10 = three calls: the call limit stops the fourth.
    AsyncLoader::Update(1000.0);
    CHECK(order == std::vector<int>({ 0, 1, 2, 3, 4 }));
    AsyncLoader::Update(1000.0);
    CHECK(order.size() == 6 && AsyncLoader::GetPendingUploadCount() == 0);
    const auto stats = AsyncLoader::GetStats();
    CHECK(stats.PeakUpdateUploadBytes == 80 && stats.PeakUpdateUploadCalls == 3);
    CHECK(stats.UploadBytes == 150 && stats.Uploads == 6 && stats.BudgetDeferrals == 2 && stats.OversizeUploads == 0);
}

TEST(AsyncLoaderUploadBudgetCutsSteppedUploadsAndIsolatesOversizeUnits)
{
    BudgetScope scope({ 0, 64 });
    int stepped = 0, small = 0, big = 0, after = 0;
    uint64_t sent = 0;
    std::vector<uint64_t> pieces;
    // 200 bytes in pieces of at most the allowance: 64 + 64 + 64 + 8.
    AsyncLoader::QueueSteppedUpload(&stepped, [&](uint64_t allowance, bool fresh, uint64_t& used) {
        (void)fresh;
        used = std::min<uint64_t>(allowance, 200 - sent);
        sent += used;
        if (used) pieces.push_back(used); // (asked again with nothing left of the budget: no piece)
        return sent == 200;
    }, 200);
    AsyncLoader::QueueUpload(&small, [&] { ++small; }, 50);
    AsyncLoader::QueueUpload(&big, [&] { ++big; }, 500); // cannot be cut: larger than the whole budget
    AsyncLoader::QueueUpload(&after, [&] { ++after; }, 1);
    for (int frame = 0; frame < 3; ++frame) AsyncLoader::Update(1000.0);
    CHECK(sent == 192 && small == 0 && AsyncLoader::GetPendingUploadBytes() == 8 + 50 + 500 + 1);
    AsyncLoader::Update(1000.0); // the last 8 bytes, then 50 more fit
    CHECK(sent == 200 && small == 1 && big == 0);
    CHECK(pieces == std::vector<uint64_t>({ 64, 64, 64, 8 }));
    AsyncLoader::Update(1000.0); // the oversize unit alone, at the start of an update
    CHECK(big == 1 && after == 0);
    AsyncLoader::Update(1000.0);
    CHECK(after == 1);
    const auto stats = AsyncLoader::GetStats();
    CHECK(stats.OversizeUploads == 1 && stats.PeakUpdateUploadBytes == 500);
    CHECK(stats.UploadBytes == 751 && stats.Uploads == 4);
}

TEST(AsyncLoaderUploadBudgetIsIgnoredByExplicitWaitsAndCancelledPieces)
{
    BudgetScope scope({ 1, 16 });
    int a = 0, b = 0, c = 0;
    uint64_t sentA = 0, sentC = 0;
    AsyncLoader::QueueSteppedUpload(&a, [&](uint64_t allowance, bool, uint64_t& used) {
        used = std::min<uint64_t>(allowance, 100 - sentA);
        sentA += used;
        return sentA == 100;
    }, 100);
    AsyncLoader::QueueUpload(&b, [&] { ++b; }, 40);
    AsyncLoader::QueueSteppedUpload(&c, [&](uint64_t allowance, bool, uint64_t& used) {
        used = std::min<uint64_t>(allowance, 100 - sentC);
        sentC += used;
        return sentC == 100;
    }, 100);
    AsyncLoader::Update(1000.0);
    CHECK(sentA == 16 && AsyncLoader::GetPendingUploadBytes() == 84 + 40 + 100);
    // An owner asked for now gets its whole upload; a cancelled one never runs again.
    CHECK(AsyncLoader::RunUpload(&a) && sentA == 100);
    AsyncLoader::CancelUpload(&c);
    AsyncLoader::WaitAll();
    CHECK(b == 1 && sentC == 0 && AsyncLoader::GetPendingUploadCount() == 0);
    // No budget (the default): everything in one update, as before budgets existed.
    AsyncLoader::SetUploadBudget({});
    int n = 0;
    for (int i = 0; i < 5; ++i) AsyncLoader::QueueUpload(&n, [&] { ++n; }, 1000000);
    AsyncLoader::Update(1000.0);
    CHECK(n == 5);
}

TEST(WorldAssetStagerDecodesOffTheOwnerThreadWithinByteBudgets)
{
    TempDirectory temp("zamin-asset-stager");
    CHECK(bool(temp));
    WriteImage(temp.GetPath() / "a.tga", 64, 32);
    WriteImage(temp.GetPath() / "b.tga", 16, 16);
    WorldAssetStager::Limits limits;
    limits.MaxTicketBytes = 64 * 32 * 3 + 16 * 16 * 3;
    limits.MaxStagedBytes = 2 * limits.MaxTicketBytes; // room for a second ticket while the first holds its bytes
    std::string error;
    auto stager = WorldAssetStager::Create(limits, error);
    CHECK(bool(stager));
    if (!stager) return;
    const auto decodes = TextureLibrary::GetThreadDecodeCount();
    const auto staged = TextureLibrary::GetPredecodedCount();
    // Duplicates collapse; the pixel-art copy is its own cache entry and its own pixels.
    const auto ticket = stager->Submit({ Tex(temp.GetPath() / "a.tga"), Tex(temp.GetPath() / "a.tga"), Tex(temp.GetPath() / "b.tga") }, error);
    CHECK(ticket != 0 && error.empty());
    CHECK(WaitFor(*stager, ticket, WorldAssetStager::State::Ready));
    const auto status = stager->GetStatus(ticket);
    CHECK(status.Files == 2 && status.Staged == 2 && status.Unreadable == 0 && status.Resident == 0);
    CHECK(status.Bytes == 64 * 32 * 3 + 16 * 16 * 3);
    CHECK(status.GPUBytes == status.Bytes + 64 * 32 + 16 * 16); // + a third for each mip chain
    CHECK(TextureLibrary::GetThreadDecodeCount() == decodes);   // nothing decoded by this (the owner) thread
    CHECK(TextureLibrary::GetPredecodedCount() == staged + 2);
    CHECK(TextureLibrary::GetPredecodedBytes() >= status.Bytes);
    CHECK(stager->GetStaged(ticket).size() == 2);
    CHECK(stager->GetStats().ReservedBytes == status.Bytes);
    // One byte more than the ticket budget: refused from the header, nothing of the ticket stays staged.
    WriteImage(temp.GetPath() / "c.tga", 1, 1);
    const auto second = stager->Submit({ Tex(temp.GetPath() / "a.tga", true), Tex(temp.GetPath() / "b.tga", true), Tex(temp.GetPath() / "c.tga") }, error);
    CHECK(WaitFor(*stager, second, WorldAssetStager::State::Failed));
    const auto failed = stager->GetStatus(second);
    CHECK(failed.Error.find("c.tga") != std::string::npos && failed.Error.find("staging budget") != std::string::npos);
    CHECK(TextureLibrary::GetPredecodedCount() == staged + 2);
    stager->Release(second);
    stager->Release(ticket);
    CHECK(TextureLibrary::GetPredecodedCount() == staged);
    const auto stats = stager->GetStats();
    CHECK(stats.ReservedBytes == 0 && stats.Tickets == 0 && stats.BudgetFailures == 1 && stats.Completed == 1);
    CHECK(stats.PeakReservedBytes <= limits.MaxStagedBytes);
}

TEST(WorldAssetStagerFileBudgetsMissingAndCorruptFiles)
{
    TempDirectory temp("zamin-asset-stager");
    CHECK(bool(temp));
    WriteImage(temp.GetPath() / "ok.tga", 8, 8);
    WriteImage(temp.GetPath() / "huge.tga", 64, 64);
    CHECK(FileSystem::WriteTextFile(temp.GetPath() / "corrupt.png", "\x89PNG\r\n\x1a\nnot really a png"));
    WorldAssetStager::Limits limits;
    limits.MaxFileBytes = 4096;
    limits.MaxFilesPerTicket = 4;
    std::string error;
    auto stager = WorldAssetStager::Create(limits, error);
    CHECK(bool(stager));
    if (!stager) return;
    const auto staged = TextureLibrary::GetPredecodedCount();
    // Missing and corrupt files do not fail the ticket: reported once, never decoded again.
    const auto tolerant = stager->Submit({ Tex(temp.GetPath() / "missing.tga"), Tex(temp.GetPath() / "corrupt.png"), Tex(temp.GetPath() / "ok.tga") }, error);
    CHECK(WaitFor(*stager, tolerant, WorldAssetStager::State::Ready));
    const auto status = stager->GetStatus(tolerant);
    CHECK(status.Staged == 1 && status.Unreadable == 2 && stager->GetStaged(tolerant).size() == 1);
    CHECK(AssetImportLog::HasError(temp.GetPath() / "corrupt.png"));
    CHECK(!AssetImportLog::ShouldRetry(temp.GetPath() / "corrupt.png"));
    // A source file over the file budget fails it, with the file's name and both numbers.
    const auto big = stager->Submit({ Tex(temp.GetPath() / "ok.tga"), Tex(temp.GetPath() / "huge.tga") }, error);
    CHECK(WaitFor(*stager, big, WorldAssetStager::State::Failed));
    CHECK(stager->GetStatus(big).Error.find("huge.tga") != std::string::npos);
    CHECK(stager->GetStatus(big).Error.find("4096") != std::string::npos);
    // The other ticket still holds ok.tga: the failed ticket did not drop a shared file.
    CHECK(TextureLibrary::GetPredecodedCount() == staged + 1);
    // Too many files are refused before anything is queued; relative paths too.
    std::vector<WorldAssetStager::Request> many;
    for (int i = 0; i < 5; ++i) many.push_back(Tex(temp.GetPath() / (std::to_string(i) + ".tga")));
    CHECK(stager->Submit(many, error) == 0 && error.find("5 asset files") != std::string::npos);
    CHECK(stager->Submit({ Tex("relative.tga") }, error) == 0 && !error.empty());
    CHECK(stager->GetStats().Tickets == 2);
    stager.reset(); // destruction drops what nobody took
    CHECK(TextureLibrary::GetPredecodedCount() == staged);
    AssetImportLog::Clear(temp.GetPath() / "corrupt.png");
    AssetImportLog::Clear(temp.GetPath() / "missing.tga");
    WorldAssetStager::Limits invalid;
    invalid.MaxTicketBytes = invalid.MaxStagedBytes + 1;
    CHECK(!WorldAssetStager::Create(invalid, error) && !error.empty());
    invalid = {};
    invalid.Workers = 0;
    CHECK(!WorldAssetStager::Create(invalid, error));
    invalid = {};
    invalid.MaxUploadUnitBytes = 100;
    CHECK(!WorldAssetStager::Create(invalid, error));
}

TEST(WorldAssetStagerRapidSubmitAndCancelKeepsReservationsBounded)
{
    TempDirectory temp("zamin-asset-stager");
    CHECK(bool(temp));
    for (int i = 0; i < 12; ++i) WriteImage(temp.GetPath() / (std::to_string(i) + ".tga"), 128, 128, uint8_t(i));
    WorldAssetStager::Limits limits;
    limits.Workers = 3;
    limits.MaxTicketBytes = 4 * 128 * 128 * 3;
    limits.MaxStagedBytes = 2 * limits.MaxTicketBytes; // two tickets at once, the third waits
    limits.MaxTickets = 8;
    std::string error;
    auto stager = WorldAssetStager::Create(limits, error);
    CHECK(bool(stager));
    if (!stager) return;
    const auto staged = TextureLibrary::GetPredecodedCount();
    for (int round = 0; round < 40; ++round)
    {
        std::vector<uint64_t> tickets;
        for (int t = 0; t < 4; ++t)
        {
            std::vector<WorldAssetStager::Request> requests;
            for (int f = 0; f < 4; ++f) requests.push_back(Tex(temp.GetPath() / (std::to_string((round + t * 3 + f) % 12) + ".tga")));
            tickets.push_back(stager->Submit(std::move(requests), error));
            CHECK(tickets.back() != 0);
        }
        if (round % 4 == 3) CHECK(WaitFor(*stager, tickets.front(), WorldAssetStager::State::Ready));
        const auto stats = stager->GetStats();
        CHECK(stats.ReservedBytes <= limits.MaxStagedBytes && stats.Tickets <= limits.MaxTickets);
        for (auto ticket : tickets) stager->Release(ticket); // the camera moved on
    }
    // Cancelled workers finish their current file; then nothing is left staged or reserved.
    for (int i = 0; i < 20000 && (stager->GetStats().ReservedBytes || TextureLibrary::GetPredecodedCount() != staged); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const auto stats = stager->GetStats();
    CHECK(stats.ReservedBytes == 0 && stats.Tickets == 0 && stats.Submitted == 160);
    CHECK(stats.PeakReservedBytes <= limits.MaxStagedBytes);
    CHECK(TextureLibrary::GetPredecodedCount() == staged);
    // The ticket limit refuses, it does not queue without bound.
    std::vector<uint64_t> held;
    for (int i = 0; i < 8; ++i) held.push_back(stager->Submit({ Tex(temp.GetPath() / "0.tga") }, error));
    CHECK(stager->Submit({ Tex(temp.GetPath() / "1.tga") }, error) == 0 && error.find("Too many") != std::string::npos);
    for (auto ticket : held) stager->Release(ticket);
}

TEST(WorldAssetStagerModelsMaterialsAndUploadUnitLimit)
{
    TempDirectory temp("zamin-asset-stager");
    CHECK(bool(temp));
    const auto root = temp.GetPath();
    WriteImage(root / "Textures/albedo.tga", 4, 4);
    WriteImage(root / "Textures/normal.tga", 4, 4);
    CHECK(FileSystem::WriteTextFile(root / "Materials/stone.zmat", R"({"Version":1,"AlbedoMap":"Textures/albedo.tga","NormalMap":"Textures/normal.tga","EmissiveMap":""})"));
    CHECK(FileSystem::WriteTextFile(root / "Models/triangle.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n"));
    std::string grid;
    for (int i = 0; i < 40; ++i) grid += std::format("v {} 0 0\nv {} 1 0\nv {} 0 1\n", i, i, i);
    for (int i = 0; i < 40; ++i) grid += std::format("f {} {} {}\n", i * 3 + 1, i * 3 + 2, i * 3 + 3);
    CHECK(FileSystem::WriteTextFile(root / "Models/grid.obj", grid));
    CHECK(FileSystem::WriteTextFile(root / "Models/broken.obj", "this is not a model\n"));
    WorldAssetStager::Limits limits;
    std::string error;
    auto stager = WorldAssetStager::Create(limits, error);
    CHECK(bool(stager));
    if (!stager) return;
    const auto parses = Model::GetThreadParseCount();
    WorldAssetStager::Request material, model, missing;
    material.Type = WorldAssetStager::Kind::Material;
    material.File = root / "Materials/stone.zmat";
    material.AssetRoot = root;
    model.Type = missing.Type = WorldAssetStager::Kind::Model;
    model.File = root / "Models/triangle.obj";
    missing.File = root / "Models/none.obj";
    const auto ticket = stager->Submit({ material, model, missing, Tex(root / "Textures/albedo.tga") }, error);
    CHECK(WaitFor(*stager, ticket, WorldAssetStager::State::Ready));
    const auto status = stager->GetStatus(ticket);
    // The material is only read for its two textures (albedo named twice: once); the model's mesh waits for the GPU.
    CHECK(status.Files == 5 && status.Staged == 3 && status.Unreadable == 1);
    CHECK(status.Bytes > 2 * 4 * 4 * 3 && status.GPUBytes >= status.Bytes);
    CHECK(Model::GetThreadParseCount() == parses);
    CHECK(!Model::IsCached(model.File));
    // Load takes the parsed model without parsing; its mesh is still to upload (no GPU in this test).
    auto loaded = Model::Load(model.File);
    CHECK(loaded && Model::GetThreadParseCount() == parses && Model::IsCached(model.File));
    CHECK(loaded && !loaded->IsResident() && loaded->GetSubMeshes().size() == 1 && loaded->GetSubMeshes()[0].MeshRef->GetPendingUploadBytes() > 0);
    // A known failure is taken too: no parse on this thread.
    CHECK(!Model::Load(missing.File) && Model::GetThreadParseCount() == parses);
    loaded.reset();
    stager->Release(ticket);
    CHECK(Model::ReleaseIfUnused(model.File)); // the mesh and its queued upload go with the model
    Model::ReleaseIfUnused(missing.File);
    CHECK(AsyncLoader::GetPendingUploadCount() == 0);
    // A mesh larger than the per-frame upload budget cannot be cut: the ticket fails and keeps nothing.
    limits.MaxUploadUnitBytes = 65536;
    stager = WorldAssetStager::Create(limits, error);
    std::string big;
    for (int i = 0; i < 1500; ++i) big += std::format("v {} 0 0\nv {} 1 0\nv {} 0 1\n", i, i, i);
    for (int i = 0; i < 1500; ++i) big += std::format("f {} {} {}\n", i * 3 + 1, i * 3 + 2, i * 3 + 3);
    CHECK(FileSystem::WriteTextFile(root / "Models/big.obj", big));
    WorldAssetStager::Request large = model, small = model, broken = model;
    large.File = root / "Models/big.obj";
    small.File = root / "Models/grid.obj";
    broken.File = root / "Models/broken.obj";
    const auto fits = stager->Submit({ small, broken }, error);
    CHECK(WaitFor(*stager, fits, WorldAssetStager::State::Ready));
    CHECK(stager->GetStatus(fits).Staged >= 1);
    const auto over = stager->Submit({ large }, error);
    CHECK(WaitFor(*stager, over, WorldAssetStager::State::Failed));
    CHECK(stager->GetStatus(over).Error.find("big.obj") != std::string::npos && stager->GetStatus(over).Error.find("upload budget") != std::string::npos);
    stager.reset();
    CHECK(Model::GetThreadParseCount() == parses && AsyncLoader::GetPendingUploadCount() == 0);
    AssetImportLog::ClearAll();
}

TEST(WorldStreamingAssetBudgetPolicyRoundTripDefaultsAndBounds)
{
    TempDirectory temp("zamin-asset-policy");
    CHECK(bool(temp));
    WorldManifest manifest;
    manifest.CellSize = { 10, 10, 10 };
    WorldCell cell;
    cell.ID = UUID(10);
    cell.ScenePath = "Scenes/Cell.zscene";
    manifest.Cells.push_back(cell);
    WorldStreaming::Policy policy;
    CHECK(!policy.AssetStreaming); // older worlds keep the main-thread path
    std::string error;
    const auto file = temp.GetPath() / "Main.zworld";
    CHECK(WorldStreaming::SaveDefinition(file, manifest, policy, error));
    // A definition written before these fields existed loads with the defaults.
    auto text = FileSystem::ReadTextFile(file);
    auto j = nlohmann::json::parse(text);
    for (const char* name : { "AssetStreaming", "MaxAssetFilesPerCell", "MaxAssetUploadsPerFrame", "MaxAssetFileBytes", "MaxAssetBytesPerCell", "MaxStagedAssetBytes", "MaxAssetUploadBytesPerFrame" })
        CHECK(j["Streaming"].erase(name) == 1);
    CHECK(FileSystem::WriteTextFile(file, j.dump()));
    WorldManifest loadedManifest;
    WorldStreaming::Policy loaded;
    loaded.AssetStreaming = true;
    CHECK(WorldStreaming::LoadDefinition(file, loadedManifest, loaded, error));
    CHECK(!loaded.AssetStreaming && loaded.MaxAssetUploadBytesPerFrame == policy.MaxAssetUploadBytesPerFrame && loaded.MaxAssetFilesPerCell == 256);
    policy.AssetStreaming = true;
    policy.MaxAssetFilesPerCell = 17;
    policy.MaxAssetUploadsPerFrame = 5;
    policy.MaxAssetFileBytes = 1234567;
    policy.MaxAssetBytesPerCell = 7654321;
    policy.MaxStagedAssetBytes = 87654321;
    policy.MaxAssetUploadBytesPerFrame = 262144;
    CHECK(WorldStreaming::SaveDefinition(file, manifest, policy, error));
    CHECK(WorldStreaming::LoadDefinition(file, loadedManifest, loaded, error));
    CHECK(loaded.AssetStreaming && loaded.MaxAssetFilesPerCell == 17 && loaded.MaxAssetUploadsPerFrame == 5);
    CHECK(loaded.MaxAssetFileBytes == 1234567 && loaded.MaxAssetBytesPerCell == 7654321 && loaded.MaxStagedAssetBytes == 87654321 && loaded.MaxAssetUploadBytesPerFrame == 262144);
    auto bad = policy;
    bad.MaxAssetUploadBytesPerFrame = 65535; // one row of the widest texture must fit
    CHECK(!bad.Validate(error) && error.find("asset") != std::string::npos);
    bad = policy;
    bad.MaxAssetBytesPerCell = bad.MaxStagedAssetBytes + 1;
    CHECK(!bad.Validate(error));
    bad = policy;
    bad.MaxAssetFilesPerCell = 0;
    CHECK(!bad.Validate(error));
    bad = policy;
    bad.MaxAssetUploadsPerFrame = 257;
    CHECK(!bad.Validate(error));
    // A malformed flag rejects the file without changing the caller's policy.
    j = nlohmann::json::parse(FileSystem::ReadTextFile(file));
    j["Streaming"]["AssetStreaming"] = 1;
    CHECK(FileSystem::WriteTextFile(file, j.dump()));
    auto kept = loaded;
    CHECK(!WorldStreaming::LoadDefinition(file, loadedManifest, loaded, error));
    CHECK(loaded.MaxAssetFilesPerCell == kept.MaxAssetFilesPerCell && loaded.AssetStreaming);
}

TEST(WorldStreamingAssetBudgetActivatesAssetFreeCellsAndRestoresUploadBudget)
{
    TempDirectory temp("zamin-asset-world");
    CHECK(bool(temp));
    auto previous = Project::GetActive();
    const auto assets = temp.GetPath() / "Assets";
    CHECK(FileSystem::WriteTextFile(temp.GetPath() / "W.zproject", R"({"Version":2,"Project":{"Name":"W","AssetDirectory":"Assets"}})"));
    auto project = Project::Load(temp.GetPath() / "W.zproject");
    CHECK(bool(project));
    {
        Scene scene;
        scene.CreateEntityWithUUID(UUID(100), "Plain");
        // A cell whose texture is missing: reported, the cell still activates (no GPU needed for that).
        scene.CreateEntityWithUUID(UUID(101), "Lost sprite").AddComponent<SpriteRendererComponent>().TexturePath = "Textures/none.png";
        CHECK(FileSystem::WriteTextFile(assets / "Scenes/Cell.zscene", SceneSerializer(Ref<Scene>(&scene, [](Scene*) {})).SerializeToString()));
    }
    WorldManifest manifest;
    manifest.CellSize = { 10, 10, 10 };
    WorldCell cell;
    cell.ID = UUID(10);
    cell.ScenePath = "Scenes/Cell.zscene";
    manifest.Cells.push_back(cell);
    WorldStreaming::Policy policy;
    policy.LoadRadius = 1;
    policy.UnloadRadius = 2;
    policy.AssetStreaming = true;
    policy.MaxAssetUploadsPerFrame = 3;
    policy.MaxAssetUploadBytesPerFrame = 131072;
    std::string error;
    CHECK(WorldStreaming::SaveDefinition(assets / "Worlds/Main.zworld", manifest, policy, error));
    const AsyncLoader::UploadBudget before{ 7, 777777 };
    AsyncLoader::SetUploadBudget(before);
    {
        SceneManager manager;
        auto anchor = CreateRef<Scene>();
        manager.Start(anchor);
        CHECK(manager.LoadStreamingWorld("Worlds/Main.zworld", error));
        auto* world = manager.GetWorldStreaming();
        CHECK(world != nullptr);
        if (world)
        {
            CHECK(AsyncLoader::GetUploadBudget() == (AsyncLoader::UploadBudget{ 3, 131072 }));
            world->SetViewerOverride({ 2, 0, 2 });
            bool active = false;
            for (int i = 0; i < 5000 && !active; ++i)
            {
                manager.ProcessRequests();
                AsyncLoader::Update(4.0);
                active = world->GetStats().Active == 1;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            CHECK(active);
            const auto stats = world->GetStats();
            CHECK(stats.AssetStreaming && stats.Failed == 0 && stats.Assets.FilesUnreadable == 1 && stats.Assets.Completed == 1);
            CHECK(stats.MainThreadImageDecodes == 0 && stats.MainThreadModelParses == 0);
            CHECK(stats.Assets.Tickets == 0 && stats.Assets.ReservedBytes == 0); // released on activation
            CHECK(anchor->GetEntityByUUID(UUID(101)) && !anchor->GetEntityByUUID(UUID(101)).GetComponent<SpriteRendererComponent>().Texture);
        }
        manager.Stop();
    }
    CHECK(AsyncLoader::GetUploadBudget() == before); // the world's budget goes with it
    AsyncLoader::SetUploadBudget({});
    AssetImportLog::ClearAll();
    Project::SetActive(previous);
}
