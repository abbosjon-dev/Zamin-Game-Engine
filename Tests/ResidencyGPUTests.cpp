// Real GL ownership checks: cache eviction must free GPU allocations while live refs still draw.
#include <Zamin/Zamin.h>
#include <Zamin/Asset/AssetResidency.h>
#include <Zamin/Asset/AsyncLoader.h>
#include <Zamin/Utils/FileSystem.h>
#include <fstream>
#include <Zamin/Scene/WorldStreaming.h>
#include <thread>
#include <chrono>
#include <stdexcept>
#include <format>
#include <glad/glad.h>
#include <Zamin/Asset/AssetImportLog.h>
using namespace Zamin;
namespace {
    void Check(bool ok, const char* why)
    {
        if (!ok) throw std::runtime_error(why);
    }
    class TestApp : public Application
    {
    public:
        using Application::Application;
    };
    void WriteFixtureImage(const std::filesystem::path& path, uint8_t color)
    {
        // Uncompressed 2x2 RGB TGA. Distinct real files exercise the library, not procedural refs.
        std::array<uint8_t, 30> data{};
        data[2] = 2;
        data[12] = 2;
        data[14] = 2;
        data[16] = 24;
        for (size_t i = 18; i < data.size(); ++i) data[i] = color;
        Check(FileSystem::WriteBinaryFile(path, data.data(), data.size()), "write texture");
    }

    // Uncompressed RGB TGA with a different colour in every pixel: a wrong row order or a missing
    // piece of a budgeted upload shows in a read-back.
    void WritePatternImage(const std::filesystem::path& path, uint16_t width, uint16_t height, uint8_t seed)
    {
        std::vector<uint8_t> data(18 + size_t(width) * height * 3);
        data[2] = 2;
        data[12] = uint8_t(width & 255);
        data[13] = uint8_t(width >> 8);
        data[14] = uint8_t(height & 255);
        data[15] = uint8_t(height >> 8);
        data[16] = 24;
        for (size_t y = 0; y < height; ++y)
            for (size_t x = 0; x < width; ++x)
            {
                uint8_t* pixel = &data[18 + (y * width + x) * 3];
                pixel[0] = uint8_t(x * 3 + seed);
                pixel[1] = uint8_t(y * 5 + seed);
                pixel[2] = uint8_t((x ^ y) + seed);
            }
        Check(FileSystem::WriteBinaryFile(path, data.data(), data.size()), "write pattern texture");
    }
    std::vector<uint8_t> ReadBack(const Ref<Texture2D>& texture, int level)
    {
        const uint32_t width = std::max(1u, texture->GetWidth() >> level), height = std::max(1u, texture->GetHeight() >> level);
        std::vector<uint8_t> pixels(size_t(width) * height * 3);
        glBindTexture(GL_TEXTURE_2D, texture->GetRendererID());
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glGetTexImage(GL_TEXTURE_2D, level, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
        return pixels;
    }
    // Any thread but this one: the staging path must not decode on the thread that owns the GL context.
    template<class Fn>
    void OnWorker(Fn&& fn)
    {
        std::thread worker(std::forward<Fn>(fn));
        worker.join();
    }
    struct StreamedWorld
    {
        SceneManager Manager;
        Ref<Scene> Anchor = CreateRef<Scene>();
        WorldStreaming* World = nullptr;
        explicit StreamedWorld(const WorldStreaming::Policy& policy, const std::filesystem::path& assets, const WorldManifest& manifest)
        {
            std::string error;
            Check(WorldStreaming::SaveDefinition(assets / "Worlds/Main.zworld", manifest, policy, error), "asset world definition");
            Manager.Start(Anchor);
            Check(Manager.LoadStreamingWorld("Worlds/Main.zworld", error), "start asset world");
            World = Manager.GetWorldStreaming();
        }
        // One host frame: the world's update and the frame's single budgeted upload pass.
        void Frame()
        {
            Manager.ProcessRequests();
            AsyncLoader::Update(4.0);
        }
        template<class Predicate>
        int Until(Predicate&& done, int limit = 6000)
        {
            for (int frame = 1; frame <= limit; ++frame)
            {
                Frame();
                if (done()) return frame;
                std::this_thread::sleep_for(std::chrono::microseconds(500));
            }
            return -1;
        }
    };
    // Hard budgets of streamed assets on a real GL context: row uploads, staged cells, cancellation.
    void RunAssetBudgetChecks(const std::filesystem::path& root, uint64_t baseline)
    {
        std::filesystem::create_directories(root);
        const AsyncLoader::UploadBudget unlimited = AsyncLoader::GetUploadBudget();
        // ---- one texture, uploaded in rows within a byte budget, equal to the unbudgeted upload.
        const auto pattern = root / "pattern.tga";
        WritePatternImage(pattern, 64, 48, 7);
        auto reference = TextureLibrary::Load(pattern);
        Check(reference && reference->IsReady(), "reference texture");
        const auto base0 = ReadBack(reference, 0), base2 = ReadBack(reference, 2);
        reference.reset();
        Check(TextureLibrary::TrimUnused(0, 8) == 1, "reference texture evicted");
        // A second copy of the same picture made the unbudgeted way stays for the mip comparison.
        const auto patternCopy = root / "pattern-copy.tga";
        WritePatternImage(patternCopy, 64, 48, 7);
        auto reference2 = TextureLibrary::Load(patternCopy);
        const auto decodes = TextureLibrary::GetThreadDecodeCount();
        TextureLibrary::StagedTexture info;
        bool stagedOk = false;
        OnWorker([&] { stagedOk = TextureLibrary::Stage(pattern, false, UINT64_MAX, info); });
        // 9216 base bytes and the 3069 bytes of the mip chain the worker made.
        const uint64_t patternBytes = 64 * 48 * 3 + (32 * 24 + 16 * 12 + 8 * 6 + 4 * 3 + 2 * 1 + 1) * 3;
        Check(stagedOk && info.DecodedBytes == patternBytes && !info.Cached, "texture staged on a worker with its mip chain");
        AsyncLoader::WaitAll();
        AsyncLoader::ResetStats();
        AsyncLoader::SetUploadBudget({ 0, 64 * 3 * 5 }); // five rows an update
        auto staged = TextureLibrary::LoadStaged(pattern, false);
        Check(staged && !staged->IsReady() && staged->IsLoaded() && staged->GetRendererID() == 0, "staged texture is a placeholder until uploaded");
        Check(TextureLibrary::GetPredecodedCount() == 0 && AsyncLoader::GetPendingUploadBytes() == patternBytes, "staged pixels moved to the upload queue");
        Check(TextureLibrary::TrimUnused(0, 8) == 0, "texture being uploaded stays pinned");
        int updates = 0;
        while (!staged->IsReady() && updates < 100)
        {
            AsyncLoader::Update(1000.0);
            ++updates;
            Check(staged->IsReady() || staged->GetRendererID() == 0, "partly uploaded texture is never exposed");
        }
        // The storage is a piece of its own (one update), then 12285 bytes at 960 an update: fourteen updates
        // at least; whole rows only, so a few more at most.
        Check(updates >= 14 && updates <= 21, "row upload spread over the expected updates");
        auto stats = AsyncLoader::GetStats();
        Check(stats.PeakUpdateUploadBytes <= 64 * 3 * 5 && stats.OversizeUploads == 0 && stats.Uploads == 1, "byte budget held in every update");
        Check(stats.UploadBytes == patternBytes + 64 * 3 * 5 && stats.BudgetDeferrals >= 13, "all bytes, the storage charge and the deferrals counted");
        Check(ReadBack(staged, 0) == base0, "budgeted upload equals the unbudgeted base level");
        // The chain is the worker's 2x2 averages, not the driver's glGenerateMipmap: the same picture within rounding.
        auto maxDifference = [](const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
            int worst = a.size() == b.size() ? 0 : 255;
            for (size_t i = 0; i < a.size() && i < b.size(); ++i) worst = std::max(worst, std::abs(int(a[i]) - int(b[i])));
            return worst;
        };
        const int mipDifference = std::max(maxDifference(ReadBack(staged, 1), ReadBack(reference2, 1)), maxDifference(ReadBack(staged, 2), base2));
        std::printf("staged mip chain vs glGenerateMipmap: max channel difference %d\n", mipDifference);
        Check(mipDifference <= 2, "CPU mip chain matches the driver's within rounding");
        reference2.reset();
        Check(TextureLibrary::GetThreadDecodeCount() == decodes, "no decode on the GL thread");
        Check(TextureLibrary::LoadStaged(pattern, false) == staged && TextureLibrary::Load(pattern) == staged, "staged texture is the cache entry");
        staged.reset();
        Check(TextureLibrary::TrimUnused(0, 8) == 2, "uploaded staged texture and the comparison copy collectible");
        // A forced Load in the middle of a budgeted upload finishes it without sending rows twice.
        OnWorker([&] { stagedOk = TextureLibrary::Stage(pattern, false, UINT64_MAX, info); });
        staged = TextureLibrary::LoadStaged(pattern, false);
        AsyncLoader::Update(1000.0);
        Check(staged && !staged->IsReady(), "upload under way");
        auto forced = TextureLibrary::Load(pattern);
        Check(forced == staged && staged->IsReady() && ReadBack(staged, 0) == base0 && maxDifference(ReadBack(staged, 2), base2) <= 2, "forced finish completes the same texture");
        Check(AsyncLoader::GetPendingUploadCount() == 0, "forced finish leaves no queued upload");
        forced.reset();
        staged.reset();
        Check(TextureLibrary::TrimUnused(0, 8) == 1, "forced texture collectible");
        // Cancelled in the middle: the cache entry, the queued upload and the half-filled GL object go.
        OnWorker([&] { stagedOk = TextureLibrary::Stage(pattern, false, UINT64_MAX, info); });
        staged = TextureLibrary::LoadStaged(pattern, false);
        AsyncLoader::Update(1000.0);
        Check(MemoryStats::Get(MemoryCategory::Textures).Bytes > baseline, "partial upload is tracked");
        auto extra = staged;
        Check(!TextureLibrary::ReleasePending(staged) && staged, "a pending texture something else holds is kept");
        extra.reset();
        Check(TextureLibrary::ReleasePending(staged) && !staged, "pending texture released");
        Check(AsyncLoader::GetPendingUploadCount() == 0 && !TextureLibrary::IsLoaded(pattern), "cancelled upload left the queue and the cache");
        Check(MemoryStats::Get(MemoryCategory::Textures).Bytes == baseline, "cancelled partial upload returned its GPU allocation");
        AsyncLoader::SetUploadBudget(unlimited);

        // ---- a streamed cell: sprite texture, model, material texture - staged, uploaded in budget, cancelled, failed.
        auto previousProject = Project::GetActive();
        struct RestoreProject
        {
            Ref<Project> Previous;
            ~RestoreProject() { Project::SetActive(Previous); }
        } restore{ previousProject };
        const auto assets = root / "Assets";
        Check(FileSystem::WriteTextFile(root / "World.zproject", R"({"Version":2,"Project":{"Name":"AssetBudgetWorld","AssetDirectory":"Assets"}})"), "asset world project");
        Check(bool(Project::Load(root / "World.zproject")), "load asset world project");
        WritePatternImage(assets / "Textures/sprite.tga", 256, 256, 1);
        WritePatternImage(assets / "Textures/stone.tga", 128, 128, 2);
        WritePatternImage(assets / "Textures/inline.tga", 64, 64, 3);
        Check(FileSystem::WriteTextFile(assets / "Materials/stone.zmat", R"({"Version":1,"Shader":"Lit","AlbedoMap":"Textures/stone.tga"})"), "material fixture");
        std::string obj;
        for (int i = 0; i < 200; ++i) obj += std::format("v {} 0 0\nv {} 1 0\nv {} 0 1\n", i, i, i);
        for (int i = 0; i < 200; ++i) obj += std::format("f {} {} {}\n", i * 3 + 1, i * 3 + 2, i * 3 + 3);
        Check(FileSystem::WriteTextFile(assets / "Models/strip.obj", obj), "model fixture");
        {
            auto authored = CreateRef<Scene>();
            authored->CreateEntityWithUUID(UUID(200), "Sprite").AddComponent<SpriteRendererComponent>().TexturePath = "Textures/sprite.tga";
            auto& mesh = authored->CreateEntityWithUUID(UUID(201), "Model").AddComponent<MeshRendererComponent>();
            mesh.Type = PrimitiveMesh::Model;
            mesh.ModelPath = "Models/strip.obj";
            mesh.MaterialPath = "Materials/stone.zmat";
            mesh.AlbedoPath = "Textures/inline.tga";
            authored->CreateEntityWithUUID(UUID(202), "Lost").AddComponent<SpriteRendererComponent>().TexturePath = "Textures/missing.tga";
            Check(FileSystem::WriteTextFile(assets / "Scenes/Cell.zscene", SceneSerializer(authored).SerializeToString()), "asset cell file");
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
        policy.CacheEntriesPerKind = 0;
        policy.AssetStreaming = true;
        policy.MaxAssetUploadsPerFrame = 4;
        policy.MaxAssetUploadBytesPerFrame = 65536;
        const uint64_t textureBytes = (256 * 256 * 3 + 128 * 128 * 3 + 64 * 64 * 3) * 4 / 3 - 3; // with their mip chains
        const glm::dvec3 inside{ 2, 0, 2 }, away{ 1000, 0, 1000 };
        const auto meshBaseline = MemoryStats::Get(MemoryCategory::Meshes).Bytes;
        {
            AsyncLoader::WaitAll();
            AsyncLoader::ResetStats();
            const auto parseBase = Model::GetThreadParseCount(), decodeBase = TextureLibrary::GetThreadDecodeCount();
            StreamedWorld run(policy, assets, manifest);
            run.World->SetViewerOverride(inside);
            const int frames = run.Until([&] { return run.World->GetStats().Active == 1; });
            Check(frames > 0, "budgeted asset cell activated");
            auto worldStats = run.World->GetStats();
            Check(worldStats.Failed == 0 && worldStats.MainThreadImageDecodes == 0 && worldStats.MainThreadModelParses == 0, "main thread decoded and parsed nothing");
            Check(Model::GetThreadParseCount() == parseBase && TextureLibrary::GetThreadDecodeCount() == decodeBase, "thread counters agree");
            Check(worldStats.Assets.FilesStaged == 4 && worldStats.Assets.FilesUnreadable == 1 && worldStats.Assets.Tickets == 0 && worldStats.Assets.ReservedBytes == 0, "staging accounted and released");
            Check(worldStats.Uploads.OversizeUploads == 0 && worldStats.Uploads.UploadBytes >= textureBytes, "upload bytes accounted");
            stats = AsyncLoader::GetStats();
            Check(stats.PeakUpdateUploadBytes <= 65536 && stats.PeakUpdateUploadCalls <= 4 && stats.OversizeUploads == 0, "per-frame upload budget held");
            // 262144 texture bytes at 65536 a frame cannot be on the GPU in fewer than four upload frames.
            Check(frames >= 4 && stats.BudgetDeferrals >= 3, "uploads were spread over frames");
            auto sprite = run.Anchor->GetEntityByUUID(UUID(200)).GetComponent<SpriteRendererComponent>().Texture;
            auto& mesh = run.Anchor->GetEntityByUUID(UUID(201)).GetComponent<MeshRendererComponent>();
            Check(sprite && sprite->IsReady() && sprite->GetRendererID() != 0 && sprite->GetWidth() == 256, "sprite texture resident at activation");
            Check(mesh.ModelAsset && mesh.ModelAsset->IsResident() && mesh.Mat.AlbedoMap && mesh.Mat.AlbedoMap->IsReady(), "model and inline map resident at activation");
            Check(mesh.MaterialRef && mesh.MaterialRef->GetMaterial().AlbedoMap && mesh.MaterialRef->GetMaterial().AlbedoMap->IsReady(), "material texture resident at activation");
            Check(!run.Anchor->GetEntityByUUID(UUID(202)).GetComponent<SpriteRendererComponent>().Texture, "missing texture leaves the entity without one");
            sprite.reset();
            // Return: the retired cell's assets were trimmed (cache target 0), so it stages and uploads again.
            run.World->SetViewerOverride(away);
            Check(run.Until([&] { return run.World->GetStats().Active == 0 && AssetResidency::Inspect().Caches[AssetResidency::Textures].Entries == 0; }) > 0, "retired cell assets evicted");
            Check(MemoryStats::Get(MemoryCategory::Textures).Bytes == baseline, "retired cell returned its texture allocations");
            run.World->SetViewerOverride(inside);
            Check(run.Until([&] { return run.World->GetStats().Active == 1; }) > 0, "returned cell activated again");
            worldStats = run.World->GetStats();
            Check(worldStats.Assets.Completed == 2 && worldStats.MainThreadImageDecodes == 0 && worldStats.MainThreadModelParses == 0, "return staged again off the main thread");
            // Cancel: leave while the uploads are under way; nothing of the cell stays staged, queued or allocated.
            run.World->SetViewerOverride(away);
            Check(run.Until([&] { return run.World->GetStats().Active == 0 && AssetResidency::Inspect().Caches[AssetResidency::Textures].Entries == 0; }) > 0, "retired again");
            run.World->SetViewerOverride(inside);
            Check(run.Until([&] { return run.World->GetStats().PendingUploadBytes > 0 && MemoryStats::Get(MemoryCategory::Textures).Bytes > baseline; }) > 0, "uploads under way");
            Check(run.World->GetStats().Active == 0, "cell not active while its assets upload");
            run.World->SetViewerOverride(away);
            Check(run.Until([&] {
                const auto now = run.World->GetStats();
                return now.Admitted == 0 && now.PendingUploads == 0 && now.Assets.ReservedBytes == 0 && AssetResidency::Inspect().Entries == 0;
            }) > 0, "cancelled cell drained");
            worldStats = run.World->GetStats();
            Check(worldStats.AssetCancellations >= 1 && worldStats.Failed == 0 && worldStats.Activations == 2, "cancellation counted, nothing activated");
            Check(TextureLibrary::GetPredecodedCount() == 0, "no staged pixels left after cancel");
            Check(MemoryStats::Get(MemoryCategory::Textures).Bytes == baseline, "cancelled cell returned its texture allocations");
            Check(MemoryStats::Get(MemoryCategory::Meshes).Bytes == meshBaseline, "cancelled cell returned its mesh allocations");
            // Fast camera: in and out every few frames; reservations and the queue stay bounded, then it settles.
            uint64_t peakReserved = 0, peakPending = 0;
            for (int i = 0; i < 400; ++i)
            {
                run.World->SetViewerOverride((i / 3) % 2 ? away : inside);
                run.Frame();
                const auto now = run.World->GetStats();
                peakReserved = std::max(peakReserved, now.Assets.ReservedBytes);
                peakPending = std::max(peakPending, now.PendingUploadBytes);
                Check(now.Admitted <= 1 && now.Assets.Tickets <= 16, "fast camera keeps admission bounded");
            }
            Check(peakReserved <= policy.MaxStagedAssetBytes && peakPending <= 2 * (textureBytes + 65536), "fast camera keeps staged and queued bytes bounded");
            run.World->SetViewerOverride(inside);
            Check(run.Until([&] { return run.World->GetStats().Active == 1; }) > 0, "settles after fast camera movement");
            Check(run.World->GetStats().MainThreadImageDecodes == 0 && run.World->GetStats().Failed == 0, "fast camera never decoded on the main thread");
            stats = AsyncLoader::GetStats();
            Check(stats.PeakUpdateUploadBytes <= 65536 && stats.OversizeUploads == 0, "budget held through return, cancel and fast camera");
            run.Manager.Stop();
        }
        Check(AsyncLoader::GetUploadBudget() == unlimited, "world restored the upload budget");
        AssetResidency::Trim(0, 64);
        AsyncLoader::WaitAll();
        AssetResidency::Trim(0, 64);
        Check(MemoryStats::Get(MemoryCategory::Textures).Bytes == baseline, "asset world returned texture allocations");
        // Without the budget (the pre-existing path) the same cell is decoded and parsed by the main thread:
        // the counters the acceptance relies on do see it.
        {
            auto legacy = policy;
            legacy.AssetStreaming = false;
            // As in the Player's captures: no background loading, so the cell's resolve loads everything itself.
            struct AsyncOff
            {
                bool Previous = AsyncLoader::IsEnabled();
                AsyncOff() { AsyncLoader::SetEnabled(false); }
                ~AsyncOff() { AsyncLoader::SetEnabled(Previous); }
            } asyncOff;
            StreamedWorld run(legacy, assets, manifest);
            run.World->SetViewerOverride(inside);
            Check(run.Until([&] { return run.World->GetStats().Active == 1; }) > 0, "legacy cell activated");
            const auto worldStats = run.World->GetStats();
            Check(!worldStats.AssetStreaming && worldStats.MainThreadImageDecodes >= 3 && worldStats.MainThreadModelParses >= 1, "legacy path loads on the main thread");
            run.Manager.Stop();
        }
        AssetResidency::Trim(0, 64);
        AssetImportLog::ClearAll();
        // A file over the file budget fails its cell with the file's name; a fixed file and a retry activate it.
        {
            auto strict = policy;
            strict.MaxAssetFileBytes = 100000; // sprite.tga is 196626 bytes
            StreamedWorld run(strict, assets, manifest);
            run.World->SetViewerOverride(inside);
            Check(run.Until([&] { return run.World->GetStats().Failed == 1; }) > 0, "over-budget cell failed");
            const auto cells = run.World->GetCells();
            Check(cells.size() == 1 && cells[0].Error.find("sprite.tga") != std::string::npos && cells[0].Error.find("100000") != std::string::npos, "failure names the file and the budget");
            auto worldStats = run.World->GetStats();
            Check(worldStats.Active == 0 && worldStats.Assets.BudgetFailures == 1 && worldStats.Assets.ReservedBytes == 0 && worldStats.MainThreadImageDecodes == 0, "failed cell kept nothing");
            for (int i = 0; i < 20; ++i) run.Frame();
            Check(TextureLibrary::GetPredecodedCount() == 0 && run.World->GetStats().Failed == 1, "failed cell stays failed without retry");
            WritePatternImage(assets / "Textures/sprite.tga", 32, 32, 1);
            run.World->RetryFailures();
            Check(run.Until([&] { return run.World->GetStats().Active == 1; }) > 0, "fixed file activates after retry");
            Check(run.Anchor->GetEntityByUUID(UUID(200)).GetComponent<SpriteRendererComponent>().Texture->GetWidth() == 32, "retried cell uses the fixed file");
            run.Manager.Stop();
        }
        AssetResidency::Trim(0, 64);
        AsyncLoader::WaitAll();
        AssetResidency::Trim(0, 64);
        AssetImportLog::ClearAll();
        Check(MemoryStats::Get(MemoryCategory::Textures).Bytes == baseline, "asset budget checks returned texture allocations");
        std::puts("PASS asset budgets: row uploads, staged cells, return, cancel, fast camera, missing and over-budget files");
    }
}
int main(int argc, char** argv)
{
    ApplicationSpecification spec;
    spec.Name = "Residency GPU ownership";
    spec.EnableImGui = false;
    spec.Window.Width = 320;
    spec.Window.Height = 240;
    spec.Window.VSync = false;
    spec.CommandLineArgs = { argc, argv };
    TestApp app(spec);
    try
    {
        TempDirectory temp("zamin-residency-gpu");
        Check(bool(temp), "temporary directory");
        TextureLibrary::Clear();
        const auto baseline = MemoryStats::Get(MemoryCategory::Textures).Bytes;
        for (int i = 0; i < 4; ++i) WriteFixtureImage(temp.GetPath() / (std::to_string(i) + ".tga"), uint8_t(30 + i * 40));
        auto old = TextureLibrary::Load(temp.GetPath() / "0.tga");
        Check(old && old->IsLoaded(), "real texture load");
        old.reset();
        auto live = TextureLibrary::Load(temp.GetPath() / "1.tga");
        auto recent = TextureLibrary::Load(temp.GetPath() / "2.tga");
        recent.reset();
        auto touch = TextureLibrary::Load(temp.GetPath() / "0.tga");
        touch.reset();
        Check(TextureLibrary::TrimUnused(2, 1) == 1, "one eviction per allowance");
        Check(!TextureLibrary::IsLoaded(temp.GetPath() / "2.tga"), "LRU hit refresh");
        Check(TextureLibrary::IsLoaded(temp.GetPath() / "0.tga"), "recent acquisition retained");
        Check(TextureLibrary::TrimUnused(0, 8) == 1, "live texture stays pinned");
        Check(live->IsLoaded() && live->GetRendererID() != 0, "live GL object survives trim");
        live->Bind();
        live.reset();
        Check(TextureLibrary::TrimUnused(0, 8) == 1, "released live texture is collectible");
        Check(MemoryStats::Get(MemoryCategory::Textures).Bytes == baseline, "texture allocation accounting returns to baseline");
        AsyncLoader::SetEnabled(true);
        auto loading = TextureLibrary::LoadAsync(temp.GetPath() / "3.tga");
        Check(bool(loading), "async texture returned");
        loading.reset();
        Check(TextureLibrary::TrimUnused(0, 8) == 0, "unfinished async load stays pinned");
        AsyncLoader::WaitAll();
        Check(TextureLibrary::TrimUnused(0, 8) == 1, "finished orphan is collectible");
        // A model owns its meshes; library cleanup must not invalidate an external model owner.
        const auto obj = temp.GetPath() / "triangle.obj";
        Check(FileSystem::WriteTextFile(obj, "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n"), "model fixture");
        auto model = Model::Load(obj);
        Check(bool(model), "model fixture loaded");
        Check(Model::TrimUnused(0, 8) == 0, "live model pinned");
        model.reset();
        Check(Model::TrimUnused(0, 8) == 1, "unused model collectible");
        MeshLibrary::TrimUnused(0, 1000);
        const auto beforeMesh = MemoryStats::Get(MemoryCategory::Meshes).Bytes;
        auto mesh = MeshLibrary::LoadOBJ(obj);
        Check(bool(mesh), "OBJ mesh loaded");
        Check(MeshLibrary::TrimUnused(0, 8) == 0, "live mesh pinned");
        mesh.reset();
        Check(MeshLibrary::TrimUnused(0, 8) == 1, "unused mesh collectible");
        Check(MemoryStats::Get(MemoryCategory::Meshes).Bytes == beforeMesh, "mesh GPU accounting returns");
        MaterialData data;
        const auto materialFile = temp.GetPath() / "test.zmaterial";
        Check(MaterialSerializer::Serialize(data, materialFile), "write material");
        auto material = MaterialLibrary::Load(materialFile);
        Check(bool(material), "material loaded");
        Check(MaterialLibrary::TrimUnused(0, 8) == 0, "live material pinned");
        material.reset();
        Check(MaterialLibrary::TrimUnused(0, 8) == 1, "unused material collectible");
        {
            AsyncLoader::DeferGPU deferred;
            auto queued = MeshLibrary::GetPrimitive(PrimitiveMesh::Cylinder, 2);
            Check(!queued->IsUploaded(), "deferred mesh queued without GL upload");
        }
        Check(MeshLibrary::GetResidencyStats().Loading == 1, "queued mesh reported loading");
        Check(MeshLibrary::TrimUnused(0, 8) == 0, "pending raw-pointer upload stays pinned");
        AsyncLoader::WaitAll();
        Check(MeshLibrary::TrimUnused(0, 8) == 1, "completed mesh upload collectible");
        for (int i = 0; i < 32; ++i)
        {
            const auto file = temp.GetPath() / ("cycle" + std::to_string(i) + ".tga");
            WriteFixtureImage(file, uint8_t(i));
            auto texture = TextureLibrary::Load(file);
            Check(texture && texture->IsLoaded(), "cycle texture loaded");
            texture.reset();
            TextureLibrary::TrimUnused(2, 1);
            Check(TextureLibrary::GetResidencyStats().Entries <= 2, "repeated residency remains bounded");
            Check(MemoryStats::Get(MemoryCategory::Textures).Bytes <= baseline + 4096, "tracked texture memory stays bounded");
        }
        TextureLibrary::TrimUnused(0, 8);
        Check(MemoryStats::Get(MemoryCategory::Textures).Bytes == baseline, "cycles release GPU allocations");
        {
            AsyncLoader::Immediate immediate;
            auto previousProject = Project::GetActive();
            const auto projectFile = temp.GetPath() / "World.zproject";
            const auto assets = temp.GetPath() / "Assets";
            Check(FileSystem::WriteTextFile(projectFile, R"({"Version":2,"Project":{"Name":"ResidencyWorld","AssetDirectory":"Assets"}})"), "world project");
            std::filesystem::create_directories(assets / "Textures");
            WriteFixtureImage(assets / "Textures/resident.tga", 120);
            auto project = Project::Load(projectFile);
            Check(bool(project), "load world fixture project");
            struct RestoreProject
            {
                Ref<Project> Previous;
                ~RestoreProject() { Project::SetActive(Previous); }
            } restore{ previousProject };
            auto authored = CreateRef<Scene>();
            authored->CreateEntityWithUUID(UUID(100), "Resident sprite").AddComponent<SpriteRendererComponent>().TexturePath = "Textures/resident.tga";
            Check(FileSystem::WriteTextFile(assets / "Scenes/Cell.zscene", SceneSerializer(authored).SerializeToString()), "world cell file");
            WorldManifest manifest;
            manifest.CellSize = { 10, 10, 10 };
            WorldCell cell;
            cell.ID = UUID(10);
            cell.ScenePath = "Scenes/Cell.zscene";
            manifest.Cells.push_back(cell);
            WorldStreaming::Policy policy;
            policy.LoadRadius = 1;
            policy.UnloadRadius = 2;
            std::string error;
            Check(WorldStreaming::SaveDefinition(assets / "Worlds/Main.zworld", manifest, policy, error), "world definition");
            SceneManager manager;
            auto anchor = CreateRef<Scene>();
            manager.Start(anchor);
            Check(manager.LoadStreamingWorld("Worlds/Main.zworld", error), "start streaming");
            auto* world = manager.GetWorldStreaming();
            auto until = [&](size_t active) {
                for (int i = 0; i < 2000; ++i)
                {
                    manager.ProcessRequests();
                    if (world->GetStats().Active == active) return true;
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                return false;
            };
            for (int cycle = 0; cycle < 3; ++cycle)
            {
                world->SetViewerOverride({ 2, 0, 2 });
                Check(until(1), "load / return world cell");
                auto entity = anchor->GetEntityByUUID(UUID(100));
                Check(entity && entity.GetComponent<SpriteRendererComponent>().Texture && entity.GetComponent<SpriteRendererComponent>().Texture->IsReady(),
                      "returned cell resolves the first texture again");
                world->SetViewerOverride({ 1000, 0, 1000 });
                Check(until(0), "retire world cell");
            }
            manager.Stop();
            TextureLibrary::TrimUnused(0, 8);
        }
        Check(MemoryStats::Get(MemoryCategory::Textures).Bytes == baseline, "streamed texture allocations returned");
        RunAssetBudgetChecks(temp.GetPath() / "budget", baseline);
        const auto snapshot = AssetResidency::Inspect();
        Check(snapshot.Caches[AssetResidency::Textures].Entries == 0, "cache snapshot after trim");
        AsyncLoader::SetEnabled(false);
        std::puts("PASS residency GPU: LRU, live pins, async pins, model/mesh ownership, memory accounting");
        return 0;
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "FAIL residency GPU: %s\n", e.what());
        return 1;
    }
}
