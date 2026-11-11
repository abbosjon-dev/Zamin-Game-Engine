#include "Zamin/Scene/WorldAuthoring.h"
#include "Zamin/Scene/WorldStreaming.h"
#include "Zamin/Scene/Scene.h"
#include "Zamin/Scene/Entity.h"
#include "Zamin/Scene/SceneSerializer.h"
#include "Zamin/Scene/TerrainTiles.h"
#include "Zamin/Utils/FileSystem.h"
#include <nlohmann/json.hpp>
#include <cmath>
#include <thread>
#include <chrono>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <linux/fs.h>
namespace Zamin {
    bool WorldGridSettings::Validate(std::string& error) const
    {
        error.clear();
        if (Folder.empty() || Folder.size() > 48 || !std::all_of(Folder.begin(), Folder.end(), [](unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-'; }) ||
            Name.empty() || Name.size() > 64 || !Columns || !Rows || Columns > 32 || Rows > 32 || Columns * Rows > 256 ||
            !std::isfinite(CellSize.x) || !std::isfinite(CellSize.y) || CellSize.x < 8 || CellSize.y < 8 || CellSize.x > 256 || CellSize.y > 256 ||
            (TerrainResolution != 9 && TerrainResolution != 17 && TerrainResolution != 33 && TerrainResolution != 65) ||
            !std::isfinite(NavigationCellSize) || NavigationCellSize < .25f || NavigationCellSize > 16)
        {
            error = "Invalid grid settings (1..256 cells, safe new folder, finite sizes)";
            return false;
        }
        if (Navigation)
            for (float size : { CellSize.x, CellSize.y })
                if (std::abs(size / NavigationCellSize - std::round(size / NavigationCellSize)) > .0001f)
                {
                    error = "Navigation cell size must exactly divide world cell dimensions";
                    return false;
                }
        return true;
    }
    bool CreateWorldGrid(const std::filesystem::path& assets, const WorldGridSettings& settings, WorldGridResult& output, std::string& error)
    {
        if (!settings.Validate(error)) return false;
        struct Handle
        {
            int FD = -1;
            ~Handle()
            {
                if (FD >= 0) close(FD);
            }
        } root{ open(assets.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC) };
        if (root.FD < 0)
        {
            error = "World asset root must be an existing directory, not a symlink";
            return false;
        }
        const auto staging = ".world-stage-" + UUID().ToString();
        if (mkdirat(root.FD, staging.c_str(), 0700))
        {
            error = "Cannot create world staging directory";
            return false;
        }
        auto work = std::filesystem::path("/proc/self/fd") / std::to_string(root.FD) / staging;
        struct Cleanup
        {
            std::filesystem::path Path;
            ~Cleanup()
            {
                if (!Path.empty())
                {
                    std::error_code ec;
                    std::filesystem::remove_all(Path, ec);
                }
            }
        } cleanup{ work };
        try
        {
            WorldGridResult result;
            result.World = std::filesystem::path(settings.Folder) / "Main.zworld";
            result.Anchor = std::filesystem::path(settings.Folder) / "Anchor.zscene";
            WorldManifest manifest;
            manifest.Name = settings.Name;
            manifest.CellSize = settings.ThreeD ? glm::dvec3(settings.CellSize.x, 32, settings.CellSize.y) : glm::dvec3(settings.CellSize.x, settings.CellSize.y, 32);
            WorldStreaming::Policy policy;
            policy.AxisMask = settings.ThreeD ? 5 : 3;
            policy.LoadRadius = std::max(settings.CellSize.x, settings.CellSize.y);
            policy.UnloadRadius = policy.LoadRadius * 1.5;
            policy.NavigationMode = settings.Navigation ? (settings.ThreeD ? 3 : 2) : 0;
            auto write = [&](const std::filesystem::path& relative, const std::string& text, const char* type) {
                if (result.Bytes + text.size() > 128 * 1024 * 1024) throw std::runtime_error("World bundle byte limit exceeded");
                if (!FileSystem::WriteTextFile(work / relative, text)) throw std::runtime_error("Cannot write staged world file");
                const auto metadata = nlohmann::json({ { "UUID", UUID().ToString() }, { "Type", type }, { "Import", nlohmann::json::object() } }).dump(2);
                if (!FileSystem::WriteTextFile((work / relative).string() + ".zmeta", metadata)) throw std::runtime_error("Cannot write world metadata");
                result.Bytes += text.size() + metadata.size();
                result.Files += 2;
            };
            std::string prototype;
            if (!settings.TemplateScene.empty())
            {
                WorldManifest sourceManifest;
                WorldCell sourceCell;
                sourceCell.ID = UUID(1);
                sourceCell.ScenePath = settings.TemplateScene.generic_string();
                sourceManifest.Cells.push_back(sourceCell);
                WorldCellLoader::Limits limits;
                limits.Workers = 1;
                limits.MaxCellBytes = 8 * 1024 * 1024;
                limits.MaxReservedBytes = limits.MaxCellBytes;
                limits.MaxWantedCells = 1;
                auto loader = WorldCellLoader::Create(sourceManifest, assets, limits, error);
                if (!loader || !loader->SetWanted({ UUID(1) }, error)) throw std::runtime_error(error);
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
                while (loader->GetStatus(UUID(1)).Stage != WorldCellStage::Ready)
                {
                    loader->Poll();
                    auto status = loader->GetStatus(UUID(1));
                    if (status.Stage == WorldCellStage::Failed) throw std::runtime_error(status.Error);
                    if (std::chrono::steady_clock::now() > deadline) throw std::runtime_error("Cell template load timed out");
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                auto status = loader->GetStatus(UUID(1));
                auto job = loader->TakeReady(UUID(1), status.Generation);
                auto source = CreateRef<Scene>();
                while (!job->Build(*source, 10))
                    if (!job->GetError().empty()) throw std::runtime_error(job->GetError());
                if (job->GetReport().HasIssues()) throw std::runtime_error("Cell template contains invalid components or hierarchy");
                for (auto entity : source->GetEntitiesInOrder())
                    if (entity.HasComponent<CameraComponent>() || entity.HasComponent<SubSceneComponent>())
                        throw std::runtime_error("Cell templates cannot own cameras or SubScene components");
                prototype = SceneSerializer::SerializeEntities(*source, source->GetEntitiesInOrder(), false);
            }
            for (uint32_t y = 0; y < settings.Rows; ++y)
                for (uint32_t x = 0; x < settings.Columns; ++x)
                {
                    auto scene = CreateRef<Scene>();
                    scene->SetName(settings.Name + " " + std::to_string(x) + "," + std::to_string(y));
                    if (!prototype.empty())
                    {
                        SceneLoadReport report;
                        SceneSerializer::EntitySetReadOptions options;
                        options.GenerateNewUUIDs = true;
                        options.Report = &report;
                        options.Error = &error;
                        SceneSerializer::DeserializeEntities(*scene, prototype, options);
                        if (report.HasIssues() || !error.empty()) throw std::runtime_error("Cannot clone cell template: " + error);
                    }
                    else
                    {
                        auto ground = scene->CreateEntity(settings.ThreeD ? "World terrain" : "World ground");
                        if (settings.ThreeD)
                        {
                            if (settings.Terrain)
                            {
                                auto& terrain = ground.AddComponent<TerrainComponent>();
                                terrain.Reset(settings.TerrainResolution, settings.CellSize);
                                TerrainTools::WorldTerrainSettings noise;
                                noise.Seed = settings.Seed;
                                if (!TerrainTools::GenerateWorldTile(terrain, { double(x) * settings.CellSize.x, 0, double(y) * settings.CellSize.y }, noise, error)) throw std::runtime_error(error);
                                ground.AddComponent<MeshCollider3DComponent>();
                            }
                            else
                            {
                                ground.GetTransform().Translation = { settings.CellSize.x * .5f, -.5f, settings.CellSize.y * .5f };
                                ground.GetTransform().Scale = { settings.CellSize.x, 1, settings.CellSize.y };
                                ground.AddComponent<MeshRendererComponent>().Type = PrimitiveMesh::Cube;
                                ground.AddComponent<BoxCollider3DComponent>();
                            }
                            ground.AddComponent<Rigidbody3DComponent>().Type = Rigidbody3DComponent::BodyType::Static;
                        }
                        else
                        {
                            ground.GetTransform().Translation = { settings.CellSize.x * .5f, settings.CellSize.y * .5f, -2 };
                            ground.GetTransform().Scale = { settings.CellSize.x, settings.CellSize.y, 1 };
                            ground.AddComponent<SpriteRendererComponent>().Color = { .12f, .22f, .28f, 1 };
                            auto obstacle = scene->CreateEntity("Navigation obstacle");
                            obstacle.GetTransform().Translation = { settings.CellSize.x * .25f, settings.CellSize.y * .5f, 0 };
                            obstacle.GetTransform().Scale = { 2, 3, 1 };
                            obstacle.AddComponent<SpriteRendererComponent>().Color = { .3f, .75f, .6f, 1 };
                            obstacle.AddComponent<Rigidbody2DComponent>().Type = Rigidbody2DComponent::BodyType::Static;
                            obstacle.AddComponent<BoxCollider2DComponent>();
                        }
                        if (settings.Navigation)
                        {
                            auto nav = scene->CreateEntity("Cell navigation");
                            if (settings.ThreeD)
                            {
                                nav.GetTransform().Translation = { settings.CellSize.x * .5f, 12, settings.CellSize.y * .5f };
                                auto& c = nav.AddComponent<NavGrid3DComponent>();
                                c.Size = { settings.CellSize.x, 32, settings.CellSize.y };
                                c.CellSize = settings.NavigationCellSize;
                                c.StepHeight = 2;
                            }
                            else
                            {
                                nav.GetTransform().Translation = { settings.CellSize.x * .5f, settings.CellSize.y * .5f, 0 };
                                auto& c = nav.AddComponent<NavGrid2DComponent>();
                                c.AutoBounds = false;
                                c.Size = settings.CellSize;
                                c.CellSize = settings.NavigationCellSize;
                                c.UseTilemaps = false;
                            }
                        }
                    }
                    auto name = "Cells/Cell_" + std::to_string(x) + "_" + std::to_string(y) + ".zscene";
                    auto text = SceneSerializer(scene).SerializeToString();
                    write(name, text, "Scene");
                    WorldCell cell;
                    cell.ID = UUID();
                    cell.Coordinate = settings.ThreeD ? glm::ivec3(x, 0, y) : glm::ivec3(x, y, 0);
                    cell.ScenePath = settings.Folder + "/" + name;
                    cell.DeclaredCPUBytes = text.size() * 4 + 1024 * 1024;
                    cell.DeclaredGPUBytes = 1024 * 1024;
                    manifest.Cells.push_back(cell);
                }
            auto anchor = CreateRef<Scene>();
            anchor->SetName(settings.Name + " anchor");
            auto camera = anchor->CreateEntity("Main Camera");
            auto& cam = camera.AddComponent<CameraComponent>();
            cam.Primary = true;
            if (settings.ThreeD)
            {
                cam.Camera.SetPerspective(.95f, .1f, 2000);
                camera.GetTransform().Translation = { settings.CellSize.x * .5f, 32, settings.CellSize.y * .5f + 28 };
                camera.GetTransform().Rotation = { -.65f, 0, 0 };
                auto sun = anchor->CreateEntity("Sun");
                sun.AddComponent<DirectionalLightComponent>();
                sun.GetTransform().Rotation = { -.7f, -.4f, 0 };
            }
            else
            {
                cam.Camera.SetOrthographic(std::max(settings.CellSize.x, settings.CellSize.y), -100, 100);
                camera.GetTransform().Translation = { settings.CellSize.x * .5f, settings.CellSize.y * .5f, 50 };
            }
            write("Anchor.zscene", SceneSerializer(anchor).SerializeToString(), "Scene");
            if (!WorldStreaming::SaveDefinition(work / "Main.zworld", manifest, policy, error)) throw std::runtime_error(error);
            result.Files++;
            result.Cells = manifest.Cells.size();
            result.Bytes += std::filesystem::file_size(work / "Main.zworld");
            if (syscall(SYS_renameat2, root.FD, staging.c_str(), root.FD, settings.Folder.c_str(), RENAME_NOREPLACE)) throw std::runtime_error("World publish failed: destination exists or rename denied");
            cleanup.Path.clear();
            output = std::move(result);
            error.clear();
            return true;
        }
        catch (const std::exception& e)
        {
            error = e.what();
            return false;
        }
    }
}
