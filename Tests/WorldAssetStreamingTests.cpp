// Hard budgets of streamed cell assets: AsyncLoader's upload budget, the staging workers and the
// world policy. CPU only - the GL side (row uploads, real cells) is in ResidencyGPUTests.cpp.
#include <Zamin/Asset/AsyncLoader.h>
#include <Zamin/Asset/AssetImportLog.h>
#include <Zamin/Renderer/Model.h>
#include <Zamin/Renderer/Texture.h>
#include <Zamin/Scene/WorldAssetStager.h>
#include <Zamin/Scene/WorldStreaming.h>
#include <Zamin/Scene/WorldTour.h>
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
    // Bytes of an RGB image with the mip chain staging makes for it.
    uint64_t WithMips(uint32_t width, uint32_t height)
    {
        uint64_t bytes = uint64_t(width) * height * 3;
        while (width > 1 || height > 1)
        {
            width = std::max(1u, width / 2);
            height = std::max(1u, height / 2);
            bytes += uint64_t(width) * height * 3;
        }
        return bytes;
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
    limits.MaxTicketBytes = WithMips(64, 32) + WithMips(16, 16);
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
    // The mip chains are made by the worker too and counted: the upload only copies rows.
    CHECK(WithMips(64, 32) == 8193 && WithMips(16, 16) == 1023);
    CHECK(status.Bytes == WithMips(64, 32) + WithMips(16, 16));
    CHECK(status.GPUBytes == status.Bytes);
    CHECK(TextureLibrary::GetThreadDecodeCount() == decodes);   // nothing decoded by this (the owner) thread
    CHECK(TextureLibrary::GetPredecodedCount() == staged + 2);
    CHECK(TextureLibrary::GetPredecodedBytes() >= status.Bytes);
    CHECK(stager->GetStaged(ticket).size() == 2);
    CHECK(stager->GetStats().ReservedBytes == status.Bytes);
    // Pixel-art copies have no mip chain (6144 + 768 bytes) and fit; what is left is the remaining
    // budget: with 9216 - 6912 = 2304 left, a 32x32 image (3072 + 1023 for its chain) is refused from the
    // header, and nothing of the ticket stays staged.
    WriteImage(temp.GetPath() / "c.tga", 32, 32);
    const auto second = stager->Submit({ Tex(temp.GetPath() / "a.tga", true), Tex(temp.GetPath() / "b.tga", true), Tex(temp.GetPath() / "c.tga") }, error);
    CHECK(WaitFor(*stager, second, WorldAssetStager::State::Failed));
    const auto failed = stager->GetStatus(second);
    CHECK(failed.Error.find("c.tga") != std::string::npos && failed.Error.find("staging budget") != std::string::npos);
    CHECK(failed.Error.find("4095") != std::string::npos && failed.Error.find("2304") != std::string::npos); // needed and left
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
    limits.MaxTicketBytes = 4 * WithMips(128, 128);
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
    CHECK(status.Bytes > 2 * WithMips(4, 4) && status.GPUBytes == status.Bytes);
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
    for (const char* name : { "AssetStreaming", "MaxAssetFilesPerCell", "MaxAssetUploadsPerFrame", "MaxAssetFileBytes", "MaxAssetBytesPerCell", "MaxStagedAssetBytes", "MaxAssetUploadBytesPerFrame", "MaxAssetReleaseBytesPerFrame" })
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
    policy.MaxAssetReleaseBytesPerFrame = 1048576;
    CHECK(WorldStreaming::SaveDefinition(file, manifest, policy, error));
    CHECK(WorldStreaming::LoadDefinition(file, loadedManifest, loaded, error));
    CHECK(loaded.AssetStreaming && loaded.MaxAssetFilesPerCell == 17 && loaded.MaxAssetUploadsPerFrame == 5);
    CHECK(loaded.MaxAssetFileBytes == 1234567 && loaded.MaxAssetBytesPerCell == 7654321 && loaded.MaxStagedAssetBytes == 87654321 && loaded.MaxAssetUploadBytesPerFrame == 262144 &&
          loaded.MaxAssetReleaseBytesPerFrame == 1048576);
    auto bad = policy;
    bad.MaxAssetUploadBytesPerFrame = 65535; // one row of the widest texture must fit
    CHECK(!bad.Validate(error) && error.find("asset") != std::string::npos);
    bad = policy;
    bad.MaxAssetBytesPerCell = bad.MaxStagedAssetBytes + 1;
    CHECK(!bad.Validate(error));
    bad = policy;
    bad.MaxAssetReleaseBytesPerFrame = 100;
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

namespace {
    // Three cells in a row along X (10 units each); the viewer stands in the first one.
    struct ResidencyWorld
    {
        TempDirectory Temp{ "zamin-residency-world" };
        Ref<Project> Previous = Project::GetActive();
        WorldManifest Manifest;
        WorldStreaming::Policy Policy;
        std::filesystem::path Assets;
        ResidencyWorld()
        {
            Assets = Temp.GetPath() / "Assets";
            CHECK(FileSystem::WriteTextFile(Temp.GetPath() / "R.zproject", R"({"Version":2,"Project":{"Name":"R","AssetDirectory":"Assets"}})"));
            CHECK(bool(Project::Load(Temp.GetPath() / "R.zproject")));
            Manifest.CellSize = { 10, 10, 10 };
            for (uint64_t i = 0; i < 3; ++i)
            {
                WorldCell cell;
                cell.ID = UUID(10 + i);
                cell.Coordinate.x = int(i);
                cell.ScenePath = "Scenes/Cell" + std::to_string(i) + ".zscene";
                Manifest.Cells.push_back(cell);
                Scene scene;
                scene.CreateEntityWithUUID(UUID(100 + i), "Cell entity");
                CHECK(FileSystem::WriteTextFile(Assets / cell.ScenePath, SceneSerializer(Ref<Scene>(&scene, [](Scene*) {})).SerializeToString()));
            }
            Policy.LoadRadius = 25;
            Policy.UnloadRadius = 30;
            Policy.MaxResidentCells = 3;
            Policy.MainThreadMilliseconds = 20;
            Policy.MaxActivationsPerFrame = 4;
            Policy.MaxBuildingCells = 4;
            Policy.PressureCooldownFrames = 5;
        }
        ~ResidencyWorld() { Project::SetActive(Previous); }
        Ref<Scene> Start(SceneManager& manager)
        {
            std::string error;
            CHECK(WorldStreaming::SaveDefinition(Assets / "Worlds/Main.zworld", Manifest, Policy, error));
            auto anchor = CreateRef<Scene>();
            manager.Start(anchor);
            CHECK(manager.LoadStreamingWorld("Worlds/Main.zworld", error));
            if (auto* world = manager.GetWorldStreaming()) world->SetViewerOverride({ 2, 0, 2 });
            return anchor;
        }
    };
    bool Settle(SceneManager& manager, const std::function<bool()>& done, int frames = 20000)
    {
        for (int i = 0; i < frames; ++i)
        {
            manager.ProcessRequests();
            if (done()) return true;
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
        return false;
    }
}

TEST(WorldStreamingResidencyLimitEvictsFarthestCellsAndReturnsAfterCooldown)
{
    ResidencyWorld fixture;
    fixture.Policy.RequestedGPULimitBytes = AssetResidency::Inspect().RequestedGPUBytes + 1000;
    fixture.Policy.PressureResumeRatio = .9;
    fixture.Policy.EvictOnPressure = true;
    SceneManager manager;
    auto anchor = fixture.Start(manager);
    auto* world = manager.GetWorldStreaming();
    CHECK(world != nullptr);
    if (!world) return;
    CHECK(Settle(manager, [&] { return world->GetStats().Active == 3; }));
    CHECK(world->GetStats().PressureEvictions == 0 && !world->GetStats().MemoryPressure);
    // Something outside the world takes the tracked GPU requests over the limit.
    TrackedMemory external(MemoryCategory::Textures);
    external.Set(2000);
    // The farthest cell goes first, alone.
    CHECK(Settle(manager, [&] { return world->GetStats().Active == 2; }));
    CHECK(!anchor->GetEntityByUUID(UUID(102)) && anchor->GetEntityByUUID(UUID(101)) && anchor->GetEntityByUUID(UUID(100)));
    CHECK(world->GetStats().PressureEvictions == 1);
    // Then the next one; the cell the viewer stands in is never evicted, however long the pressure lasts.
    CHECK(Settle(manager, [&] { return world->GetStats().Active == 1; }));
    for (int i = 0; i < 40; ++i) manager.ProcessRequests();
    auto stats = world->GetStats();
    CHECK(stats.Active == 1 && stats.Admitted == 1 && stats.PressureEvictions == 2 && stats.MemoryPressure && stats.Failed == 0);
    CHECK(anchor->GetEntityByUUID(UUID(100)) && !anchor->GetEntityByUUID(UUID(101)));
    CHECK(stats.Unloads == 2);
    // Back under the low watermark: the cells return once their cooldown has passed.
    external.Set(0);
    CHECK(Settle(manager, [&] { return world->GetStats().Active == 3; }));
    stats = world->GetStats();
    CHECK(!stats.MemoryPressure && stats.ResidencyBlocked == 0 && stats.PressureEvictions == 2 && stats.Activations == 5);
    for (const auto& cell : world->GetCells()) CHECK(!cell.Held);
    manager.Stop();
}

TEST(WorldStreamingResidencyLimitWithoutEvictionOnlyPausesAdmission)
{
    // The behaviour before EvictOnPressure existed, and still the default: live cells are never dropped.
    ResidencyWorld fixture;
    fixture.Policy.RequestedGPULimitBytes = AssetResidency::Inspect().RequestedGPUBytes + 1000;
    CHECK(!fixture.Policy.EvictOnPressure);
    SceneManager manager;
    auto anchor = fixture.Start(manager);
    auto* world = manager.GetWorldStreaming();
    CHECK(world != nullptr);
    if (!world) return;
    CHECK(Settle(manager, [&] { return world->GetStats().Active == 3; }));
    TrackedMemory external(MemoryCategory::Textures);
    external.Set(2000);
    for (int i = 0; i < 60; ++i) manager.ProcessRequests();
    const auto stats = world->GetStats();
    CHECK(stats.MemoryPressure && stats.Active == 3 && stats.PressureEvictions == 0 && stats.Unloads == 0);
    CHECK(anchor->GetEntityByUUID(UUID(102)));
    manager.Stop();
}

TEST(WorldStreamingResidencyLimitKeepsDependenciesOfRemainingCells)
{
    ResidencyWorld fixture;
    // The middle cell needs the far one: the far cell cannot go before it, although it is farther.
    fixture.Manifest.Cells[1].Dependencies.push_back(UUID(12));
    fixture.Policy.RequestedGPULimitBytes = AssetResidency::Inspect().RequestedGPUBytes + 1000;
    fixture.Policy.EvictOnPressure = true;
    fixture.Policy.PressureCooldownFrames = 36000;
    SceneManager manager;
    auto anchor = fixture.Start(manager);
    auto* world = manager.GetWorldStreaming();
    CHECK(world != nullptr);
    if (!world) return;
    CHECK(Settle(manager, [&] { return world->GetStats().Active == 3; }));
    TrackedMemory external(MemoryCategory::Textures);
    external.Set(2000);
    CHECK(Settle(manager, [&] { return world->GetStats().Active == 2; }));
    CHECK(!anchor->GetEntityByUUID(UUID(101)) && anchor->GetEntityByUUID(UUID(102)));
    CHECK(Settle(manager, [&] { return world->GetStats().Active == 1; }));
    CHECK(anchor->GetEntityByUUID(UUID(100)) && !anchor->GetEntityByUUID(UUID(102)));
    // Held out: with the pressure gone the long cooldown still keeps both away, and reports them.
    external.Set(0);
    for (int i = 0; i < 40; ++i) manager.ProcessRequests();
    const auto stats = world->GetStats();
    CHECK(stats.Active == 1 && !stats.MemoryPressure && stats.ResidencyBlocked == 2);
    size_t held = 0;
    for (const auto& cell : world->GetCells()) held += cell.Held;
    CHECK(held == 2);
    manager.Stop();
    // Policy bounds and round trip of the two fields.
    std::string error;
    auto policy = fixture.Policy;
    CHECK(WorldStreaming::SaveDefinition(fixture.Assets / "Worlds/Copy.zworld", fixture.Manifest, policy, error));
    WorldManifest manifest;
    WorldStreaming::Policy loaded;
    CHECK(WorldStreaming::LoadDefinition(fixture.Assets / "Worlds/Copy.zworld", manifest, loaded, error));
    CHECK(loaded.EvictOnPressure && loaded.PressureCooldownFrames == 36000);
    policy.PressureCooldownFrames = 0;
    CHECK(!policy.Validate(error));
    policy.PressureCooldownFrames = 36001;
    CHECK(!policy.Validate(error));
    CHECK(!WorldStreaming::Policy{}.EvictOnPressure && WorldStreaming::Policy{}.PressureCooldownFrames == 120);
}

TEST(WorldTourEvaluatesByFrameNumberWithJumpsAndRoundTrips)
{
    WorldTour tour;
    tour.Name = "test";
    tour.Warmup = 7;
    tour.Keys = { { 0, { 0, 1, 0 }, false }, { 10, { 10, 1, 20 }, false }, { 20, { 100, 1, 100 }, true }, { 30, { 100, 1, 130 }, false } };
    std::string error;
    CHECK(tour.Validate(error) && tour.GetLength() == 30);
    // Linear between keys, by frame number alone.
    CHECK(tour.Evaluate(0) == glm::dvec3(0, 1, 0) && tour.Evaluate(5) == glm::dvec3(5, 1, 10) && tour.Evaluate(10) == glm::dvec3(10, 1, 20));
    // A jump key is not travelled towards: the camera rests, then is there at once.
    CHECK(tour.Evaluate(15) == glm::dvec3(10, 1, 20) && tour.Evaluate(19) == glm::dvec3(10, 1, 20) && tour.Evaluate(20) == glm::dvec3(100, 1, 100));
    CHECK(tour.Evaluate(25) == glm::dvec3(100, 1, 115) && tour.Evaluate(30) == glm::dvec3(100, 1, 130) && tour.Evaluate(999999) == glm::dvec3(100, 1, 130));
    // Repeatable: the same frame gives the same bits whatever was asked before.
    for (uint32_t frame : { 3u, 17u, 3u, 29u, 3u }) CHECK(tour.Evaluate(frame) == tour.Evaluate(frame));
    std::string text;
    CHECK(tour.Serialize(text, error));
    WorldTour loaded;
    CHECK(loaded.Deserialize(text, error));
    CHECK(loaded.Name == "test" && loaded.Warmup == 7 && loaded.Keys.size() == 4 && loaded.Keys[2].Jump && !loaded.Keys[1].Jump);
    for (uint32_t frame = 0; frame <= 30; ++frame) CHECK(loaded.Evaluate(frame) == tour.Evaluate(frame));
    TempDirectory temp("zamin-tour");
    CHECK(bool(temp));
    CHECK(WorldTour::Save(temp.GetPath() / "a.ztour", tour, error));
    CHECK(!WorldTour::Save(temp.GetPath() / "a.json", tour, error) && error.find(".ztour") != std::string::npos);
    WorldTour fromFile;
    CHECK(WorldTour::Load(temp.GetPath() / "a.ztour", fromFile, error) && fromFile.GetLength() == 30);
    CHECK(!WorldTour::Load(temp.GetPath() / "none.ztour", fromFile, error) && fromFile.GetLength() == 30);
    // Malformed files are refused without changing the tour.
    auto refuse = [&](const std::function<void(nlohmann::json&)>& change) {
        auto j = nlohmann::json::parse(text);
        change(j);
        WorldTour kept = tour;
        const bool ok = kept.Deserialize(j.dump(), error);
        return !ok && !error.empty() && kept.Keys.size() == 4 && kept.Name == "test";
    };
    CHECK(refuse([](auto& j) { j["Format"] = "Other"; }));
    CHECK(refuse([](auto& j) { j["Version"] = 2; }));
    CHECK(refuse([](auto& j) { j["Keys"][0]["Frame"] = 1; }));          // must start at frame 0
    CHECK(refuse([](auto& j) { j["Keys"][2]["Frame"] = 10; }));         // frames must increase
    CHECK(refuse([](auto& j) { j["Keys"][1]["Position"][0] = 2e6; }));  // outside the world's range
    CHECK(refuse([](auto& j) { j["Keys"][1]["Position"] = { 1, 2 }; }));
    CHECK(refuse([](auto& j) { j["Keys"][1]["Jump"] = 1; }));
    CHECK(refuse([](auto& j) { j["Warmup"] = 10001; }));
    CHECK(refuse([](auto& j) { j["Keys"] = nlohmann::json::array(); }));
    CHECK(refuse([](auto& j) { j["Keys"][3]["Frame"] = 1000001; }));
    WorldTour kept = tour;
    CHECK(!kept.Deserialize("{ not json", error) && kept.Keys.size() == 4);
    CHECK(!kept.Deserialize(std::string(WorldTour::MaxFileBytes + 1, ' '), error));
}

TEST(WorldTourReportPercentilesAndRunnerRecordsEveryTourFrameOnce)
{
    // Nearest-rank percentiles over 1..100.
    std::vector<double> values;
    for (int i = 100; i >= 1; --i) values.push_back(i);
    const auto d = WorldTourReport::Distribute(values);
    CHECK(d.P50 == 50 && d.P95 == 95 && d.P99 == 99 && d.Max == 100 && d.Average == 50.5);
    CHECK(WorldTourReport::Distribute({}).Max == 0 && WorldTourReport::Distribute({ 7 }).P99 == 7);
    // The runner on a real scene manager: warm-up frames are not recorded, every tour frame is, once.
    SceneManager manager;
    auto scene = CreateRef<Scene>();
    auto camera = scene->CreateEntityWithUUID(UUID(5), "Camera");
    camera.AddComponent<CameraComponent>().Primary = true;
    manager.Start(scene);
    WorldTour tour;
    tour.Name = "runner";
    tour.Warmup = 3;
    tour.Keys = { { 0, { 1, 2, 3 }, false }, { 4, { 5, 2, 3 }, false } };
    WorldTourRunner runner;
    std::string error;
    WorldTour broken = tour;
    broken.Keys.clear();
    CHECK(!runner.Start(broken, error) && !runner.IsActive());
    CHECK(runner.Start(tour, error) && runner.IsActive() && !runner.IsFinished());
    std::vector<float> seen;
    for (int tick = 0; tick < 20; ++tick)
    {
        runner.BeginFrame(manager, 10.0 + tick);
        seen.push_back(camera.GetTransform().Translation.x);
        manager.ProcessRequests();
    }
    // Three warm-up ticks at the first key, then frames 0..4, then the camera stays at the last key.
    CHECK(seen[0] == 1 && seen[2] == 1 && seen[3] == 1 && seen[4] == 2 && seen[5] == 3 && seen[6] == 4 && seen[7] == 5 && seen[19] == 5);
    CHECK(runner.IsFinished());
    const auto& samples = runner.GetReport().GetSamples();
    CHECK(samples.size() == 5);
    for (size_t i = 0; i < samples.size(); ++i) CHECK(samples[i].Frame == i && samples[i].FrameMs == 10.0 + 4 + double(i));
    const auto summary = runner.GetReport().Summarize();
    CHECK(summary.Frames == 5 && summary.FrameMs.Max == 18.0 && summary.FrameMs.P50 == 16.0);
    TempDirectory temp("zamin-tour-report");
    CHECK(runner.WriteReport(temp.GetPath() / "report.json", &manager, error));
    const auto j = nlohmann::json::parse(FileSystem::ReadTextFile(temp.GetPath() / "report.json"));
    CHECK(j.at("Format") == "ZaminWorldTourReport" && j.at("Complete") == true && j.at("Samples").size() == 5 && j.at("TourFrames") == 5);
    CHECK(j.at("Columns").size() == j.at("Samples")[0].size() && j.at("Summary").at("FrameMs").at("Max") == 18.0);
    manager.Stop();
}
