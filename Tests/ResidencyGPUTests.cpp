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
