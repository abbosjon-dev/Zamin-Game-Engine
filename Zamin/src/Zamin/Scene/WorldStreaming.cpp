#include "Zamin/Scene/WorldStreaming.h"
#include "Zamin/Scene/SceneManager.h"
#include "Zamin/Scene/Entity.h"
#include "Zamin/AI/NavMosaic.h"
#include "Zamin/Scene/WorldCheckpoint.h"
#include "Zamin/Scene/WorldCheckpointWriter.h"
#include "Zamin/Project/Project.h"
#include "Zamin/Asset/AssetImporter.h"
#include "Zamin/Renderer/Texture.h"
#include "Zamin/Scripting/EventBus.h"
#include "Zamin/Utils/FileSystem.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <deque>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits>
#include <set>
#include <thread>
#include <atomic>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

namespace Zamin {
    namespace {
        using Clock = std::chrono::steady_clock;
        using json = nlohmann::json;
        double Elapsed(Clock::time_point start) { return std::chrono::duration<double, std::milli>(Clock::now() - start).count(); }
        json PolicyJSON(const WorldStreaming::Policy& p)
        {
            json links = json::array();
            for (const auto& l : p.NavigationLinks)
                links.push_back({ { "ID", l.ID }, { "From", { l.From.x, l.From.y } }, { "To", { l.To.x, l.To.y } }, { "Cost", l.Cost }, { "Bidirectional", l.Bidirectional }, { "Enabled", l.Enabled } });
            return { { "AsyncNavigation", p.AsyncNavigation }, { "NavigationQueryWorkers", p.NavigationQueryWorkers }, { "NavigationQueryCapacity", p.NavigationQueryCapacity }, { "NavigationQueryStarts", p.NavigationQueryStarts }, { "NavigationQueryResults", p.NavigationQueryResults }, { "NavigationLinks", links }, { "LoadRadius", p.LoadRadius }, { "UnloadRadius", p.UnloadRadius }, { "MainThreadMilliseconds", p.MainThreadMilliseconds }, { "AxisMask", p.AxisMask }, { "Layers", p.Layers }, { "MaxResidentCells", p.MaxResidentCells }, { "MaxResidentEntities", p.MaxResidentEntities }, { "MaxEntitiesPerCell", p.MaxEntitiesPerCell }, { "MaxBuildingCells", p.MaxBuildingCells }, { "MaxActivationsPerFrame", p.MaxActivationsPerFrame }, { "MaxUnloadsPerFrame", p.MaxUnloadsPerFrame }, { "MaxResolveEntitiesPerFrame", p.MaxResolveEntitiesPerFrame }, { "DeclaredCPUBytes", p.DeclaredCPUBytes }, { "DeclaredGPUBytes", p.DeclaredGPUBytes }, { "ObservedRSSLimitBytes", p.ObservedRSSLimitBytes }, { "RequestedGPULimitBytes", p.RequestedGPULimitBytes }, { "PressureResumeRatio", p.PressureResumeRatio }, { "EvictOnPressure", p.EvictOnPressure }, { "PressureCooldownFrames", p.PressureCooldownFrames }, { "CacheEntriesPerKind", p.CacheEntriesPerKind }, { "MaxCacheEvictionsPerFrame", p.MaxCacheEvictionsPerFrame }, { "AssetStreaming", p.AssetStreaming }, { "MaxAssetFilesPerCell", p.MaxAssetFilesPerCell }, { "MaxAssetUploadsPerFrame", p.MaxAssetUploadsPerFrame }, { "MaxAssetFileBytes", p.MaxAssetFileBytes }, { "MaxAssetBytesPerCell", p.MaxAssetBytesPerCell }, { "MaxStagedAssetBytes", p.MaxStagedAssetBytes }, { "MaxAssetUploadBytesPerFrame", p.MaxAssetUploadBytesPerFrame }, { "MaxAssetReleaseBytesPerFrame", p.MaxAssetReleaseBytesPerFrame }, { "NavigationMode", p.NavigationMode }, { "NavigationMaxCells", p.NavigationMaxCells } };
        }
        bool ReadPolicy(const json& j, WorldStreaming::Policy& p, std::string& error)
        {
            if (!j.is_object())
            {
                error = "Streaming must be an object";
                return false;
            }
            auto read = [&](const char* name, auto& value) {
                if (!j.contains(name)) return;
                using T = std::decay_t<decltype(value)>;
                const auto& v = j.at(name);
                if constexpr (std::is_integral_v<T>)
                {
                    if (!v.is_number_unsigned() || v.get<uint64_t>() > std::numeric_limits<T>::max()) throw std::runtime_error("Invalid streaming integer");
                }
                else if (!v.is_number()) throw std::runtime_error("Invalid streaming number");
                value = v.get<T>();
            };
            read("LoadRadius", p.LoadRadius);
            read("UnloadRadius", p.UnloadRadius);
            read("MainThreadMilliseconds", p.MainThreadMilliseconds);
            read("AxisMask", p.AxisMask);
            read("MaxResidentCells", p.MaxResidentCells);
            read("MaxResidentEntities", p.MaxResidentEntities);
            read("MaxEntitiesPerCell", p.MaxEntitiesPerCell);
            read("MaxBuildingCells", p.MaxBuildingCells);
            read("MaxActivationsPerFrame", p.MaxActivationsPerFrame);
            read("MaxUnloadsPerFrame", p.MaxUnloadsPerFrame);
            read("MaxResolveEntitiesPerFrame", p.MaxResolveEntitiesPerFrame);
            read("DeclaredCPUBytes", p.DeclaredCPUBytes);
            read("DeclaredGPUBytes", p.DeclaredGPUBytes);
            read("ObservedRSSLimitBytes", p.ObservedRSSLimitBytes);
            read("RequestedGPULimitBytes", p.RequestedGPULimitBytes);
            read("PressureResumeRatio", p.PressureResumeRatio);
            read("CacheEntriesPerKind", p.CacheEntriesPerKind);
            read("NavigationMode", p.NavigationMode);
            read("NavigationMaxCells", p.NavigationMaxCells);
            read("NavigationQueryWorkers", p.NavigationQueryWorkers);
            read("NavigationQueryCapacity", p.NavigationQueryCapacity);
            read("NavigationQueryStarts", p.NavigationQueryStarts);
            read("NavigationQueryResults", p.NavigationQueryResults);
            if (j.contains("AsyncNavigation")) p.AsyncNavigation = j.at("AsyncNavigation").get<bool>();
            read("MaxCacheEvictionsPerFrame", p.MaxCacheEvictionsPerFrame);
            auto flag = [&](const char* name, bool& value) {
                if (!j.contains(name)) return;
                if (!j.at(name).is_boolean()) throw std::runtime_error("Invalid streaming flag");
                value = j.at(name).get<bool>();
            };
            flag("AssetStreaming", p.AssetStreaming);
            flag("EvictOnPressure", p.EvictOnPressure);
            read("PressureCooldownFrames", p.PressureCooldownFrames);
            read("MaxAssetFilesPerCell", p.MaxAssetFilesPerCell);
            read("MaxAssetUploadsPerFrame", p.MaxAssetUploadsPerFrame);
            read("MaxAssetFileBytes", p.MaxAssetFileBytes);
            read("MaxAssetBytesPerCell", p.MaxAssetBytesPerCell);
            read("MaxStagedAssetBytes", p.MaxStagedAssetBytes);
            read("MaxAssetUploadBytesPerFrame", p.MaxAssetUploadBytesPerFrame);
            read("MaxAssetReleaseBytesPerFrame", p.MaxAssetReleaseBytesPerFrame);
            if (j.contains("NavigationLinks"))
            {
                const auto& links = j.at("NavigationLinks");
                if (!links.is_array() || links.size() > 4096) throw std::runtime_error("Invalid navigation links");
                p.NavigationLinks.clear();
                for (const auto& item : links)
                {
                    if (!item.at("ID").is_number_unsigned() || !item.at("From").is_array() || item.at("From").size() != 2 ||
                        !item.at("To").is_array() || item.at("To").size() != 2 || !item.at("Cost").is_number() ||
                        !item.at("Bidirectional").is_boolean() || !item.at("Enabled").is_boolean()) throw std::runtime_error("Invalid navigation link fields");
                    p.NavigationLinks.push_back({ item.at("ID").get<uint64_t>(), { item.at("From")[0].get<float>(), item.at("From")[1].get<float>() }, { item.at("To")[0].get<float>(), item.at("To")[1].get<float>() }, item.at("Cost").get<float>(), item.at("Bidirectional").get<bool>(), item.at("Enabled").get<bool>() });
                }
            }
            if (j.contains("Layers")) p.Layers = j.at("Layers").get<std::vector<std::string>>();
            return p.Validate(error);
        }
    }
    bool WorldStreaming::Policy::Validate(std::string& error) const
    {
        error.clear();
        if (!std::isfinite(LoadRadius) || !std::isfinite(UnloadRadius) || LoadRadius < 0 || UnloadRadius < LoadRadius || UnloadRadius > 1e6 ||
            !std::isfinite(MainThreadMilliseconds) || MainThreadMilliseconds < .1 || MainThreadMilliseconds > 100 ||
            !AxisMask || AxisMask > 7 || !MaxResidentCells || MaxResidentCells > 128 || !MaxResidentEntities || MaxResidentEntities > 1000000 ||
            !MaxEntitiesPerCell || MaxEntitiesPerCell > 100000 || !MaxBuildingCells || MaxBuildingCells > 8 ||
            !MaxActivationsPerFrame || MaxActivationsPerFrame > 16 || !MaxUnloadsPerFrame || MaxUnloadsPerFrame > 16 ||
            !MaxResolveEntitiesPerFrame || MaxResolveEntitiesPerFrame > 4096 || !DeclaredCPUBytes || !DeclaredGPUBytes ||
            DeclaredCPUBytes > WorldManifest::MaxDeclaredBytesPerCell || DeclaredGPUBytes > WorldManifest::MaxDeclaredBytesPerCell || Layers.size() > 32 ||
            ObservedRSSLimitBytes > WorldManifest::MaxDeclaredBytesPerCell || RequestedGPULimitBytes > WorldManifest::MaxDeclaredBytesPerCell ||
            !std::isfinite(PressureResumeRatio) || PressureResumeRatio < .1 || PressureResumeRatio > .99 ||
            CacheEntriesPerKind > 100000 || !MaxCacheEvictionsPerFrame || MaxCacheEvictionsPerFrame > 256 ||
            (NavigationMode != 0 && NavigationMode != 2 && NavigationMode != 3) || !NavigationMaxCells || NavigationMaxCells > 1048576)
        {
            error = "Invalid world streaming policy";
            return false;
        }
        if (!MaxAssetFilesPerCell || MaxAssetFilesPerCell > 4096 || !MaxAssetUploadsPerFrame || MaxAssetUploadsPerFrame > 256 ||
            !MaxAssetFileBytes || MaxAssetFileBytes > WorldManifest::MaxDeclaredBytesPerCell || !MaxAssetBytesPerCell ||
            MaxAssetBytesPerCell > MaxStagedAssetBytes || MaxStagedAssetBytes > WorldManifest::MaxDeclaredBytesPerCell ||
            MaxAssetUploadBytesPerFrame < 65536 || MaxAssetUploadBytesPerFrame > (uint64_t(1) << 30) ||
            MaxAssetReleaseBytesPerFrame < 65536 || MaxAssetReleaseBytesPerFrame > WorldManifest::MaxDeclaredBytesPerCell ||
            !PressureCooldownFrames || PressureCooldownFrames > 36000)
        {
            error = "Invalid world asset streaming budget";
            return false;
        }
        if (!NavAtlas::ValidateLinks(NavigationLinks, error)) return false;
        if ((AsyncNavigation && !NavigationMode) || !NavigationQueryWorkers || NavigationQueryWorkers > 4 ||
            !NavigationQueryCapacity || NavigationQueryCapacity > 1024 || !NavigationQueryStarts || NavigationQueryStarts > 64 ||
            !NavigationQueryResults || NavigationQueryResults > 64)
        {
            error = "Invalid async navigation policy";
            return false;
        }
        std::set<std::string> unique;
        for (const auto& layer : Layers)
            if (layer.empty() || layer.size() > 64 || !unique.insert(layer).second || !std::all_of(layer.begin(), layer.end(), [](unsigned char c) {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
            }))
            {
                error = "Invalid streaming layer selection";
                return false;
            }
        return true;
    }
    bool WorldStreaming::LoadDefinition(const std::filesystem::path& file, WorldManifest& manifest, Policy& policy, std::string& error)
    {
        try
        {
            struct Handle
            {
                int FD;
                ~Handle()
                {
                    if (FD >= 0) close(FD);
                }
            } in{ open(file.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC) };
            struct stat before
            {
            }, after{};
            if (in.FD < 0 || fstat(in.FD, &before) || !S_ISREG(before.st_mode) || before.st_size < 0 || uint64_t(before.st_size) > WorldManifest::MaxFileBytes)
                throw std::runtime_error("World definition must be a bounded regular file");
            std::string text(size_t(before.st_size), '\0');
            size_t offset = 0;
            while (offset < text.size())
            {
                const auto count = read(in.FD, text.data() + offset, text.size() - offset);
                if (count < 0 && errno == EINTR) continue;
                if (count <= 0) throw std::runtime_error("World definition changed while reading");
                offset += size_t(count);
            }
            char extra;
            if (read(in.FD, &extra, 1) != 0 || fstat(in.FD, &after) || before.st_size != after.st_size ||
                before.st_mtim.tv_sec != after.st_mtim.tv_sec || before.st_mtim.tv_nsec != after.st_mtim.tv_nsec ||
                before.st_ctim.tv_sec != after.st_ctim.tv_sec || before.st_ctim.tv_nsec != after.st_ctim.tv_nsec)
                throw std::runtime_error("World definition changed while reading");
            WorldManifest next;
            Policy settings;
            if (!next.Deserialize(text, error)) return false;
            const auto j = json::parse(text);
            if (j.contains("Streaming") && !ReadPolicy(j.at("Streaming"), settings, error)) return false;
            manifest = std::move(next);
            policy = std::move(settings);
            error.clear();
            return true;
        }
        catch (const std::exception& e)
        {
            error = e.what();
            return false;
        }
    }
    bool WorldStreaming::SaveDefinition(const std::filesystem::path& file, const WorldManifest& manifest, const Policy& policy, std::string& error)
    {
        std::string text;
        if (file.extension() != ".zworld")
        {
            error = "World definitions must use .zworld";
            return false;
        }
        if (!policy.Validate(error) || !manifest.Serialize(text, error)) return false;
        try
        {
            auto j = json::parse(text);
            j["Streaming"] = PolicyJSON(policy);
            text = j.dump(2);
            if (text.size() > WorldManifest::MaxFileBytes || !FileSystem::WriteTextFileAtomic(file, text)) throw std::runtime_error("Cannot save world definition");
            return true;
        }
        catch (const std::exception& e)
        {
            error = e.what();
            return false;
        }
    }

    struct WorldStreaming::Data
    {
        struct Cell
        {
            CellInfo Info;
            Ref<Scene> SceneRef;
            std::unique_ptr<SceneLoadJob> Job;
            std::vector<UUID> Resolve;
            std::vector<UUID> Owned;
            size_t Resolved = 0;
            // AssetStreaming: the staging ticket and what pins the cell's textures / models until it is active.
            struct AssetStage
            {
                uint64_t Ticket = 0, GPUBytes = 0;
                bool Adopted = false;
                uint32_t Attempts = 0;
                std::vector<Ref<Texture2D>> Textures;
                std::vector<Ref<Model>> Models;
            } Assets;
            double Distance = 0;    // to the viewer, as selection last measured it
            uint64_t GPUAtBuild = 0; // tracked GPU requests when it began to build
            uint64_t HeldUntil = 0; // residency limit: not selected before this tick
            bool AwaitsRoom = false; // its measured assets did not fit under the GPU limit
        };
        WorldManifest Manifest;
        Policy Rules;
        Ref<Scene> Anchor;
        std::unique_ptr<WorldCellLoader> Loader;
        std::vector<Cell> Cells;
        std::unordered_map<uint64_t, size_t> Index;
        std::vector<UUID> Order;
        using Mask = std::array<uint64_t, WorldManifest::MaxCells / 64>;
        std::vector<Mask> Closures;
        glm::dvec3 Viewer{ 0 };
        bool Override = false;
        Stats Counters;
        std::unique_ptr<WorldCheckpoint> State, PendingState;
        bool CaptureOnUnload = false;
        std::deque<std::pair<uint64_t, Command>> Commands;
        uint64_t NextCommand = 0;
        std::unique_ptr<WorldCheckpointWriter> Writer;
        bool Capture(size_t index, WorldCheckpoint& state, std::string& error)
        {
            auto& cell = Cells[index];
            if (cell.Info.Stage != WorldRuntimeStage::Active)
            {
                error = "Only active cells can be captured";
                return false;
            }
            std::vector<Entity> entities;
            std::unordered_set<uint64_t> ids;
            for (auto id : cell.Owned)
                if (auto live = Anchor->GetEntityByUUID(id))
                {
                    entities.push_back(live);
                    ids.insert(uint64_t(id));
                }
            for (auto entity : entities)
                for (auto child : Anchor->GetChildren(entity))
                    if (!ids.contains(uint64_t(child.GetUUID())))
                    {
                        error = "Checkpoint cell has an unowned child; adopt its subtree first";
                        return false;
                    }
            for (auto entity : entities)
                if (auto parent = Anchor->GetParent(entity); parent && !ids.contains(uint64_t(parent.GetUUID())))
                {
                    error = "Checkpoint cell has a parent outside its ownership";
                    return false;
                }
            try
            {
                auto doc = json::parse(SceneSerializer::SerializeEntities(*Anchor, entities, false));
                doc.erase("ZaminEntities");
                doc.erase("Kind");
                doc["Scene"] = Manifest.Cells[index].ScenePath;
                const auto offset = Manifest.Bounds(Manifest.Cells[index]).first;
                for (auto& entity : doc["Entities"])
                    if (!entity.contains("Parent") || entity["Parent"] == 0)
                    {
                        auto& value = entity["Transform"]["Translation"];
                        for (int axis = 0; axis < 3; ++axis) value[axis] = value.at(axis).get<double>() - offset[axis];
                    }
                return state.Put(cell.Info.ID, doc.dump(), error);
            }
            catch (const std::exception& e)
            {
                error = e.what();
                return false;
            }
        }
        bool PublishOverrides(const WorldCheckpoint& state, std::string& error)
        {
            // Clear first so replacing a full-size snapshot never double-charges old/new overlays.
            for (const auto& source : Manifest.Cells)
                if (!Loader->SetSourceOverride(source.ID, nullptr, error)) return false;
            for (const auto& [id, text] : state.GetCells())
                if (!Loader->SetSourceOverride(UUID(id), text, error)) return false;
            return true;
        }
        std::unique_ptr<WorldAssetStager> Stager;
        AsyncLoader::UploadBudget PreviousBudget;
        AsyncLoader::Stats UploadBase;
        uint64_t DecodeBase = 0, ParseBase = 0;
        ~Data()
        {
            if (ReleaseThread.joinable()) ReleaseThread.join();
            for (auto& cell : Cells) ReleaseAssets(cell, true);
            if (Stager) AsyncLoader::SetUploadBudget(PreviousBudget);
        }
        // The asset files the staged cell scene's components load when they are resolved (main thread).
        static std::vector<WorldAssetStager::Request> CollectAssets(Scene& scene)
        {
            std::vector<WorldAssetStager::Request> out;
            const auto root = Project::GetAssetDirectory();
            auto texture = [&](const std::string& path, bool pixelArt) {
                if (path.empty()) return;
                WorldAssetStager::Request request;
                request.File = Project::GetAssetFileSystemPath(path);
                request.PixelArt = pixelArt;
                if (request.File.is_absolute()) out.push_back(std::move(request));
            };
            auto material = [&](const std::string& path) {
                if (path.empty()) return;
                WorldAssetStager::Request request;
                request.Type = WorldAssetStager::Kind::Material;
                request.File = Project::GetAssetFileSystemPath(path);
                request.AssetRoot = root;
                if (request.File.is_absolute()) out.push_back(std::move(request));
            };
            for (auto entity : scene.GetEntitiesInOrder())
            {
                if (const auto* sprite = entity.TryGetComponent<SpriteRendererComponent>()) texture(sprite->TexturePath, sprite->PixelArt);
                if (const auto* tilemap = entity.TryGetComponent<TilemapComponent>()) texture(tilemap->TilesetPath, tilemap->PixelArt);
                if (const auto* image = entity.TryGetComponent<UIImageComponent>()) texture(image->TexturePath, image->PixelArt);
                if (const auto* mesh = entity.TryGetComponent<MeshRendererComponent>())
                {
                    if (mesh->Type == PrimitiveMesh::Model && !mesh->ModelPath.empty())
                    {
                        WorldAssetStager::Request request;
                        request.Type = WorldAssetStager::Kind::Model;
                        request.File = Project::GetAssetFileSystemPath(mesh->ModelPath);
                        if (request.File.is_absolute())
                        {
                            request.Model = AssetImporter::GetModelSettings(request.File);
                            out.push_back(std::move(request));
                        }
                    }
                    for (const auto* path : { &mesh->AlbedoPath, &mesh->NormalPath, &mesh->MetalRoughPath, &mesh->EmissivePath }) texture(*path, false);
                    material(mesh->MaterialPath);
                    for (const auto& path : mesh->SubmeshMaterials) material(path);
                }
            }
            return out;
        }
        // The cell lets go of its asset pins and its ticket. `cancel`: before activation - textures still
        // loading that nothing else waits for leave the cache with their uploads, a model nobody drew too.
        void ReleaseAssets(Cell& cell, bool cancel)
        {
            auto& stage = cell.Assets;
            if (cancel && (stage.Ticket || !stage.Textures.empty() || !stage.Models.empty())) ++Counters.AssetCancellations;
            if (cancel)
            {
                for (auto& model : stage.Models)
                {
                    if (!model || model->IsResident()) continue;
                    std::vector<Ref<Texture2D>> maps;
                    for (const auto& sub : model->GetSubMeshes())
                        for (const auto& map : { sub.Mat.AlbedoMap, sub.Mat.NormalMap, sub.Mat.MetalRoughMap, sub.Mat.EmissiveMap })
                            if (map && !map->IsReady()) maps.push_back(map);
                    const auto path = model->GetPath();
                    model.reset();
                    if (Model::ReleaseIfUnused(path))
                        for (auto& map : maps) TextureLibrary::ReleasePending(map);
                }
                for (auto& texture : stage.Textures) TextureLibrary::ReleasePending(texture);
            }
            if (stage.Ticket && Stager) Stager->Release(stage.Ticket);
            stage = {};
        }
        // True once everything the cell's components load is on the GPU. False while it waits; a budget
        // failure sets `error`.
        bool AdvanceAssets(Cell& cell, std::string& error)
        {
            error.clear();
            auto& stage = cell.Assets;
            if (!stage.Ticket && !stage.Adopted)
            {
                // What the caches already have is pinned now, so that trimming cannot take it away before the
                // cell resolves (it would be loaded again by the main thread); the rest goes to the workers.
                std::vector<WorldAssetStager::Request> requests;
                for (auto& request : CollectAssets(*cell.SceneRef))
                {
                    if (request.Type == WorldAssetStager::Kind::Texture)
                    {
                        if (auto texture = TextureLibrary::LoadStaged(request.File, request.PixelArt))
                        {
                            stage.Textures.push_back(std::move(texture));
                            continue;
                        }
                    }
                    else if (request.Type == WorldAssetStager::Kind::Model && Model::IsCached(request.File))
                    {
                        if (auto model = Model::Load(request.File)) stage.Models.push_back(std::move(model));
                        continue;
                    }
                    requests.push_back(std::move(request));
                }
                if (requests.empty()) stage.Adopted = true;
                else
                {
                    stage.Ticket = Stager->Submit(std::move(requests), error);
                    return false;
                }
            }
            if (!stage.Adopted)
            {
                const auto status = Stager->GetStatus(stage.Ticket);
                if (status.Stage == WorldAssetStager::State::Failed)
                {
                    error = status.Error.empty() ? "World asset budget exceeded" : status.Error;
                    return false;
                }
                if (status.Stage != WorldAssetStager::State::Ready) return false;
                cell.Info.MeasuredGPUBytes = std::max(cell.Info.MeasuredGPUBytes, status.GPUBytes);
                if (!FitsGPULimit(cell, status.GPUBytes))
                {
                    // Nothing of it is uploaded: back to dormant until that much is free (Select checks it).
                    cell.AwaitsRoom = true;
                    ++Counters.ResidencyRefusals;
                    return false;
                }
                bool missing = false;
                for (const auto& request : Stager->GetStaged(stage.Ticket))
                    if (request.Type == WorldAssetStager::Kind::Texture)
                    {
                        if (auto texture = TextureLibrary::LoadStaged(request.File, request.PixelArt)) stage.Textures.push_back(std::move(texture));
                        else missing = true; // evicted between staging and now
                    }
                    else if (auto model = Model::Load(request.File)) stage.Models.push_back(std::move(model));
                if (missing && ++stage.Attempts < 3)
                {
                    // Staged again with a new ticket; what was taken so far stays pinned by this cell.
                    Stager->Release(stage.Ticket);
                    stage.Ticket = 0;
                    ++Counters.AssetRestages;
                    return false;
                }
                stage.Adopted = true;
                stage.GPUBytes = status.GPUBytes;
            }
            for (const auto& texture : stage.Textures)
                if (!texture->IsReady()) return false;
            for (const auto& model : stage.Models)
                if (!model->IsResident()) return false;
            return true;
        }
        // Tracked GPU bytes the cells still loading are expected to ask for (their measured sizes).
        uint64_t InFlightGPU(const Cell* except) const
        {
            uint64_t bytes = 0;
            for (const auto& cell : Cells)
                if (&cell != except && cell.Info.Admitted && cell.Info.Stage != WorldRuntimeStage::Active)
                    bytes += std::min(std::max(cell.Info.MeasuredGPUBytes, cell.Assets.GPUBytes), UINT64_MAX - bytes);
            return bytes;
        }
        bool FitsGPULimit(const Cell& cell, uint64_t bytes) const
        {
            if (!Rules.RequestedGPULimitBytes || !bytes) return true;
            const uint64_t used = AssetResidency::Inspect().RequestedGPUBytes, flight = InFlightGPU(&cell);
            // (strictly below: reaching the limit is pressure already)
            return used < Rules.RequestedGPULimitBytes && flight < Rules.RequestedGPULimitBytes - used && bytes < Rules.RequestedGPULimitBytes - used - flight;
        }
        // Residency limit: the farthest admitted cell nothing admitted depends on is held out (the normal
        // unload path retires it next tick). Never a cell the viewer is in.
        bool EvictFarthest()
        {
            Cell* victim = nullptr;
            for (size_t i = 0; i < Cells.size(); ++i)
            {
                auto& cell = Cells[i];
                if (!cell.Info.Admitted || cell.Distance <= 0 || (victim && cell.Distance <= victim->Distance)) continue;
                bool needed = false;
                for (size_t other = 0; other < Cells.size() && !needed; ++other)
                    needed = other != i && Cells[other].Info.Admitted && (Closures[other][i / 64] & (uint64_t(1) << (i % 64)));
                if (!needed) victim = &cell;
            }
            if (!victim) return false;
            victim->HeldUntil = Tick + Rules.PressureCooldownFrames;
            // With a GPU limit it also waits until what it was measured to need is free again.
            victim->AwaitsRoom = Rules.RequestedGPULimitBytes && victim->Info.MeasuredGPUBytes;
            ++Counters.PressureEvictions;
            return true;
        }
        ResidencyPressure Pressure;
        bool RoomWanted = false; // a cell waits for GPU room: unused cache entries are given up first
        bool FreedSinceRelease = false; // cells or cache entries went since freed memory was last handed back
        uint64_t ReleasedThisFrame = 0; // tracked GPU bytes cache trimming released in this tick
        bool TrimAgain = false;
        // Handing freed heap memory back walks the heap (milliseconds): done by a helper thread, one at a time.
        std::thread ReleaseThread;
        std::atomic<bool> ReleaseBusy{ false };
        std::atomic<double> ReleaseMilliseconds{ 0 };
        void ReleaseFreeMemory()
        {
            if (ReleaseBusy.exchange(true)) return;
            if (ReleaseThread.joinable()) ReleaseThread.join();
            ReleaseThread = std::thread([this] {
                const auto before = Clock::now();
                MemoryStats::ReleaseFreeMemory();
                ReleaseMilliseconds.store(Elapsed(before));
                ReleaseBusy.store(false);
            });
        }
        Clock::time_point NextRSSAction{};
        uint64_t Tick = 0;
        uint64_t Frame = 0;
        size_t CacheEvictionsThisFrame = 0;
        size_t Trim(size_t target)
        {
            const size_t allowance = Rules.MaxCacheEvictionsPerFrame > CacheEvictionsThisFrame ? Rules.MaxCacheEvictionsPerFrame - CacheEvictionsThisFrame : 0;
            // One entry at a time: with the asset budget on, trimming also stops once this frame has released
            // MaxAssetReleaseBytesPerFrame of tracked GPU requests (freeing large GPU objects costs main-thread
            // time too). An entry larger than what is left still goes when it is the frame's first.
            size_t count = 0;
            if (!Stager) count = AssetResidency::Trim(target, allowance);
            else
                while (count < allowance && (ReleasedThisFrame == 0 || ReleasedThisFrame < Rules.MaxAssetReleaseBytesPerFrame))
                {
                    const uint64_t before = MemoryStats::Get(MemoryCategory::Textures).Bytes + MemoryStats::Get(MemoryCategory::Meshes).Bytes;
                    if (!AssetResidency::Trim(target, 1)) break;
                    const uint64_t after = MemoryStats::Get(MemoryCategory::Textures).Bytes + MemoryStats::Get(MemoryCategory::Meshes).Bytes;
                    ReleasedThisFrame += before > after ? before - after : 0;
                    ++count;
                }
            // Stopped by the release budget: the next tick goes on instead of waiting for the periodic scan.
            TrimAgain = Stager && count && ReleasedThisFrame >= Rules.MaxAssetReleaseBytesPerFrame;
            FreedSinceRelease |= count > 0;
            CacheEvictionsThisFrame += count;
            Counters.CacheEvictions += count;
            return count;
        }
        UUID NavigationRoot{ 0 };
        std::shared_ptr<const NavAtlas> Atlas;
        bool NavigationDirty = false;
        std::unique_ptr<NavQueryQueue> Queries;
        std::unordered_map<uint64_t, uint32_t> QueryEntities;
        void PumpQueries()
        {
            if (!Queries) return;
            for (auto key : Queries->Keys())
            {
                auto entity = Anchor->GetEntityByUUID(UUID(key));
                const auto* agent = entity ? entity.TryGetComponent<NavAgentComponent>() : nullptr;
                const auto binding = QueryEntities.find(key);
                if (!agent || !agent->WorldNavigation || !agent->HasDestination || !Anchor->IsActiveInHierarchy(entity) || binding == QueryEntities.end() || binding->second != uint32_t(entity))
                {
                    Queries->Cancel(key);
                    QueryEntities.erase(key);
                }
            }
            Queries->Pump(Rules.NavigationQueryStarts, Rules.NavigationQueryResults);
        }
        std::vector<std::array<uint64_t, 4>> NavigationStamp;
        void UpdateNavigation()
        {
            if (!Rules.NavigationMode) return;
            std::vector<std::pair<uint64_t, const NavGrid*>> sources;
            for (const auto& cell : Cells)
                if (cell.Info.Stage == WorldRuntimeStage::Active)
                    for (auto id : cell.Owned)
                    {
                        auto e = Anchor->GetEntityByUUID(id);
                        if (!e || !Anchor->IsActiveInHierarchy(e)) continue;
                        if (Rules.NavigationMode == 2)
                        {
                            if (const auto* g = e.TryGetComponent<NavGrid2DComponent>(); g && g->Baked && !g->Grid.IsEmpty())
                                sources.emplace_back(uint64_t(id), &g->Grid);
                        }
                        else if (const auto* g = e.TryGetComponent<NavGrid3DComponent>(); g && g->Baked && !g->Grid.IsEmpty())
                            sources.emplace_back(uint64_t(id), &g->Grid);
                    }
            std::sort(sources.begin(), sources.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
            std::vector<std::array<uint64_t, 4>> stamp;
            std::vector<const NavGrid*> grids;
            for (const auto& [id, g] : sources)
            {
                stamp.push_back({ id, g->GetSerial(), g->GetRevision(), g->GetCarveRevision() });
                grids.push_back(g);
            }
            if (stamp == NavigationStamp && !NavigationDirty) return;
            NavigationDirty = false;
            if (Queries) Queries->CancelAll();
            QueryEntities.clear();
            NavigationStamp = std::move(stamp);
            std::vector<NavAtlas::Source> tiles;
            for (const auto& [id, grid] : sources) tiles.push_back({ id, grid });
            auto atlas = std::make_shared<NavAtlas>();
            if (atlas->Build(tiles, Rules.NavigationLinks, Counters.AtlasError, Rules.NavigationMaxCells)) Atlas = std::move(atlas);
            else Atlas.reset(); // old retained snapshots remain immutable; current queries fail closed
            Counters.AtlasCells = Atlas ? Atlas->GetCellCount() : 0;
            Counters.AtlasBytes = Atlas ? Atlas->GetMemoryBytes() : 0;
            Counters.ActiveNavigationLinks = Atlas ? Atlas->GetActiveLinks() : 0;
            auto root = Anchor->GetEntityByUUID(NavigationRoot);
            if (!root)
            {
                root = Anchor->CreateEntity("World navigation mosaic");
                NavigationRoot = root.GetUUID();
                if (Rules.NavigationMode == 2)
                {
                    auto& c = root.AddComponent<NavGrid2DComponent>();
                    c.Paths.BakeOnStart = false;
                    c.AutoRebuild = false;
                }
                else
                {
                    auto& c = root.AddComponent<NavGrid3DComponent>();
                    c.Paths.BakeOnStart = false;
                }
            }
            auto& destination = Rules.NavigationMode == 2 ? root.GetComponent<NavGrid2DComponent>().Grid : root.GetComponent<NavGrid3DComponent>().Grid;
            std::string error;
            const bool ready = !grids.empty() && BuildNavMosaic(grids, destination, error, Rules.NavigationMaxCells);
            if (!ready) destination.Clear(); // never expose the previous tile topology after failure
            if (Rules.NavigationMode == 2) root.GetComponent<NavGrid2DComponent>().Baked = ready;
            else root.GetComponent<NavGrid3DComponent>().Baked = ready;
            Counters.NavigationTiles = grids.size();
            Counters.NavigationCells = ready ? (Rules.NavigationMode == 2 ? root.GetComponent<NavGrid2DComponent>().Grid.GetCellCount() : root.GetComponent<NavGrid3DComponent>().Grid.GetCellCount()) : 0;
            Counters.NavigationError = std::move(error);
            ++Counters.NavigationRebuilds;
            // Topology may change while revisions happen to match. All automatic / mosaic-bound
            // agents replan explicitly; references to authored grids retain their own semantics.
            for (auto handle : Anchor->GetAllEntitiesWith<NavAgentComponent>())
            {
                auto& agent = Entity(handle, Anchor.get()).GetComponent<NavAgentComponent>();
                if (!agent.Grid || agent.Grid.ID == NavigationRoot) agent.RepathRequested = true;
            }
        }

        void Recount()
        {
            auto previous = Counters;
            Counters = {};
            Counters.Activations = previous.Activations;
            Counters.Unloads = previous.Unloads;
            Counters.Residency = previous.Residency;
            Counters.MemoryPressure = previous.MemoryPressure;
            Counters.CacheEvictions = previous.CacheEvictions;
            Counters.PressureFrames = previous.PressureFrames;
            Counters.AtlasCells = previous.AtlasCells;
            Counters.AtlasBytes = previous.AtlasBytes;
            Counters.ActiveNavigationLinks = previous.ActiveNavigationLinks;
            Counters.AtlasError = previous.AtlasError;
            Counters.NavigationQueries = Queries ? Queries->GetStats() : NavQueryQueue::Stats{};
            Counters.NavigationStaleResults = previous.NavigationStaleResults;
            Counters.NavigationTiles = previous.NavigationTiles;
            Counters.NavigationCells = previous.NavigationCells;
            Counters.NavigationRebuilds = previous.NavigationRebuilds;
            Counters.NavigationError = previous.NavigationError;
            Counters.PendingCommands = Commands.size() + (Writer && Writer->IsBusy() ? 1 : 0);
            Counters.CompletedCommands = previous.CompletedCommands;
            Counters.FailedCommands = previous.FailedCommands;
            Counters.Captures = previous.Captures;
            Counters.Restores = previous.Restores;
            Counters.CheckpointCells = State ? State->GetCount() : 0;
            Counters.CheckpointBytes = State ? State->GetBytes() : 0;
            Counters.CaptureOnUnload = CaptureOnUnload;
            Counters.RestorePending = bool(PendingState);
            Counters.CheckpointError = previous.CheckpointError;
            Counters.LastMilliseconds = previous.LastMilliseconds;
            Counters.PeakMilliseconds = previous.PeakMilliseconds;
            Counters.BudgetBlocked = previous.BudgetBlocked;
            Counters.AssetCancellations = previous.AssetCancellations;
            Counters.PressureEvictions = previous.PressureEvictions;
            Counters.ResidencyRefusals = previous.ResidencyRefusals;
            Counters.ResidencyBlocked = previous.ResidencyBlocked;
            Counters.MemoryReleases = previous.MemoryReleases;
            Counters.PeakMemoryReleaseMilliseconds = std::max(previous.PeakMemoryReleaseMilliseconds, ReleaseMilliseconds.load());
            Counters.AssetRestages = previous.AssetRestages;
            Counters.AssetStreaming = bool(Stager);
            if (Stager)
            {
                Counters.Assets = Stager->GetStats();
                auto uploads = AsyncLoader::GetStats();
                // (peaks are the process's since its statistics were last reset; the sums are this world's)
                uploads.Loads -= std::min(uploads.Loads, UploadBase.Loads);
                uploads.Finished -= std::min(uploads.Finished, UploadBase.Finished);
                uploads.Uploads -= std::min(uploads.Uploads, UploadBase.Uploads);
                uploads.UploadBytes -= std::min(uploads.UploadBytes, UploadBase.UploadBytes);
                uploads.UploadCalls -= std::min(uploads.UploadCalls, UploadBase.UploadCalls);
                uploads.BudgetDeferrals -= std::min(uploads.BudgetDeferrals, UploadBase.BudgetDeferrals);
                uploads.OversizeUploads -= std::min(uploads.OversizeUploads, UploadBase.OversizeUploads);
                uploads.UploadMilliseconds -= std::min(uploads.UploadMilliseconds, UploadBase.UploadMilliseconds);
                Counters.Uploads = uploads;
                Counters.PendingUploads = AsyncLoader::GetPendingUploadCount();
                Counters.PendingUploadBytes = AsyncLoader::GetPendingUploadBytes();
            }
            Counters.MainThreadImageDecodes = TextureLibrary::GetThreadDecodeCount() - DecodeBase;
            Counters.MainThreadModelParses = Model::GetThreadParseCount() - ParseBase;
            for (size_t i = 0; i < Cells.size(); ++i)
            {
                const auto& c = Cells[i];
                Counters.Desired += c.Info.Desired;
                if (c.Info.Admitted)
                {
                    ++Counters.Admitted;
                    Counters.Entities += c.Info.Entities;
                    Counters.DeclaredCPU += Manifest.Cells[i].DeclaredCPUBytes;
                    Counters.DeclaredGPU += Manifest.Cells[i].DeclaredGPUBytes;
                }
                Counters.Active += c.Info.Stage == WorldRuntimeStage::Active;
                Counters.Building += c.Info.Stage == WorldRuntimeStage::Building || c.Info.Stage == WorldRuntimeStage::Resolving || c.Info.Stage == WorldRuntimeStage::Staging;
                Counters.Failed += c.Info.Stage == WorldRuntimeStage::Failed;
            }
            Counters.Loader = Loader->GetStats();
        }
        void Fail(Cell& cell, std::string error)
        {
            cell.Job.reset();
            cell.SceneRef.reset();
            ReleaseAssets(cell, true);
            cell.Resolve.clear();
            cell.Resolved = 0;
            cell.Info.Stage = WorldRuntimeStage::Failed;
            cell.Info.Error = std::move(error);
            cell.Info.Admitted = false;
            cell.Info.Entities = 0;
            Recount();
        }
        void Select()
        {
            std::vector<std::pair<double, size_t>> candidates;
            for (size_t i = 0; i < Cells.size(); ++i)
            {
                auto& c = Cells[i];
                const double radius = (c.Info.Admitted || c.Info.Desired) ? Rules.UnloadRadius : Rules.LoadRadius;
                c.Info.Desired = false;
                const auto& source = Manifest.Cells[i];
                if (std::find(Rules.Layers.begin(), Rules.Layers.end(), source.Layer) == Rules.Layers.end()) continue;
                auto [min, max] = Manifest.Bounds(source);
                double distance = 0;
                for (int axis = 0; axis < 3; ++axis)
                    if (Rules.AxisMask & (1u << axis))
                    {
                        const double delta = std::max({ min[axis] - Viewer[axis], 0.0, Viewer[axis] - max[axis] });
                        distance += delta * delta;
                    }
                c.Distance = std::sqrt(distance);
                if (distance <= radius * radius) candidates.emplace_back(distance, i);
            }
            std::sort(candidates.begin(), candidates.end());
            size_t count = 0;
            uint64_t cpu = 0, gpu = 0;
            Counters.BudgetBlocked = 0;
            Counters.ResidencyBlocked = 0;
            // Residency limits: cells cooling down after an eviction, and cells whose measured assets do not
            // fit under the GPU limit right now, are not selected - nor is anything that depends on them.
            Data::Mask held{};
            RoomWanted = false;
            uint64_t room = 0, flight = 0;
            if (Rules.RequestedGPULimitBytes)
            {
                const uint64_t used = AssetResidency::Inspect().RequestedGPUBytes;
                flight = InFlightGPU(nullptr);
                room = used < Rules.RequestedGPULimitBytes ? Rules.RequestedGPULimitBytes - used : 0;
                room = flight < room ? room - flight : 0;
            }
            for (size_t i = 0; i < Cells.size(); ++i)
            {
                auto& c = Cells[i];
                bool hold = c.HeldUntil > Tick;
                if (c.AwaitsRoom)
                {
                    // (its own in-flight share is in `flight` until it is retired)
                    if (c.Info.Admitted || c.Info.MeasuredGPUBytes >= room)
                    {
                        hold = true;
                        RoomWanted |= c.Distance <= Rules.LoadRadius; // (only a cell the viewer would load asks the caches)
                    }
                    else
                    {
                        c.AwaitsRoom = false;
                        room -= c.Info.MeasuredGPUBytes; // one at a time takes the room
                    }
                }
                c.Info.Held = hold;
                if (hold) held[i / 64] |= uint64_t(1) << (i % 64);
            }
            Data::Mask chosen{};
            for (auto [distance, index] : candidates)
            {
                (void)distance;
                bool blocked = false;
                for (size_t word = 0; word < held.size() && !blocked; ++word) blocked = (Closures[index][word] & held[word]) != 0;
                if (blocked)
                {
                    ++Counters.ResidencyBlocked;
                    continue;
                }
                Data::Mask addition{};
                size_t extraCount = 0;
                for (size_t word = 0; word < chosen.size(); ++word)
                {
                    addition[word] = Closures[index][word] & ~chosen[word];
                    extraCount += std::popcount(addition[word]);
                }
                if (count + extraCount > Rules.MaxResidentCells)
                {
                    ++Counters.BudgetBlocked;
                    continue;
                }
                uint64_t extraCPU = 0, extraGPU = 0;
                for (size_t word = 0; word < addition.size(); ++word)
                    for (uint64_t bits = addition[word]; bits; bits &= bits - 1)
                    {
                        const size_t i = word * 64 + std::countr_zero(bits);
                        extraCPU += Manifest.Cells[i].DeclaredCPUBytes;
                        extraGPU += Manifest.Cells[i].DeclaredGPUBytes;
                    }
                if (extraCPU > Rules.DeclaredCPUBytes - cpu || extraGPU > Rules.DeclaredGPUBytes - gpu)
                {
                    ++Counters.BudgetBlocked;
                    continue;
                }
                for (size_t word = 0; word < chosen.size(); ++word) chosen[word] |= addition[word];
                count += extraCount;
                cpu += extraCPU;
                gpu += extraGPU;
            }
            for (size_t i = 0; i < Cells.size(); ++i) Cells[i].Info.Desired = (chosen[i / 64] & (uint64_t(1) << (i % 64))) != 0;
        }
    };

    WorldStreaming::WorldStreaming(std::unique_ptr<Data> data) : m_Data(std::move(data)) {}
    WorldStreaming::~WorldStreaming() = default;
    std::unique_ptr<WorldStreaming> WorldStreaming::Create(SceneManager& manager, const WorldManifest& manifest,
                                                           const Policy& policy, const std::filesystem::path& assets, std::string& error)
    {
        if (!manager.IsRunning() || !manager.GetActiveScene())
        {
            error = "Streaming needs a running anchor scene";
            return nullptr;
        }
        if (!manifest.Validate(error) || !policy.Validate(error)) return nullptr;
        auto d = std::make_unique<Data>();
        std::set<std::string> paths;
        for (const auto& cell : manifest.Cells)
            if (!paths.insert(std::filesystem::path(cell.ScenePath).lexically_normal().generic_string()).second)
            {
                error = "Streaming v1 requires distinct scene files per cell";
                return nullptr;
            }
        WorldCellLoader::Limits limits;
        limits.MaxWantedCells = policy.MaxResidentCells;
        limits.MaxEntities = policy.MaxEntitiesPerCell;
        d->Loader = WorldCellLoader::Create(manifest, assets, limits, error);
        if (!d->Loader) return nullptr;
        d->DecodeBase = TextureLibrary::GetThreadDecodeCount();
        d->ParseBase = Model::GetThreadParseCount();
        if (policy.AssetStreaming)
        {
            WorldAssetStager::Limits staging;
            staging.MaxTickets = 16;
            staging.MaxFilesPerTicket = policy.MaxAssetFilesPerCell;
            staging.MaxFileBytes = policy.MaxAssetFileBytes;
            staging.MaxTicketBytes = policy.MaxAssetBytesPerCell;
            staging.MaxStagedBytes = policy.MaxStagedAssetBytes;
            staging.MaxUploadUnitBytes = policy.MaxAssetUploadBytesPerFrame;
            auto stager = WorldAssetStager::Create(staging, error);
            if (!stager) return nullptr;
            // Set only once nothing can fail any more: ~Data restores it.
            d->PreviousBudget = AsyncLoader::GetUploadBudget();
            d->UploadBase = AsyncLoader::GetStats();
            d->Stager = std::move(stager);
            AsyncLoader::SetUploadBudget({ policy.MaxAssetUploadsPerFrame, policy.MaxAssetUploadBytesPerFrame });
        }
        d->Manifest = manifest;
        d->State = std::make_unique<WorldCheckpoint>(manifest);
        d->Rules = policy;
        d->Anchor = manager.GetActiveScene();
        manifest.DependencyOrder(d->Order, error);
        d->Cells.resize(manifest.Cells.size());
        for (size_t i = 0; i < manifest.Cells.size(); ++i)
        {
            d->Index[manifest.Cells[i].ID] = i;
            d->Cells[i].Info.ID = manifest.Cells[i].ID;
        }
        d->Closures.resize(manifest.Cells.size());
        for (auto id : d->Order)
        {
            const size_t i = d->Index.at(id);
            auto& mask = d->Closures[i];
            mask[i / 64] |= uint64_t(1) << (i % 64);
            for (auto dependency : manifest.Cells[i].Dependencies)
                for (size_t word = 0; word < mask.size(); ++word) mask[word] |= d->Closures[d->Index.at(dependency)][word];
        }
        return std::unique_ptr<WorldStreaming>(new WorldStreaming(std::move(d)));
    }

    void WorldStreaming::Update(SceneManager& manager)
    {
        auto& d = *m_Data;
        const auto start = Clock::now();
        ++d.Tick;
        d.CacheEvictionsThisFrame = 0;
        d.ReleasedThisFrame = 0;
        if (d.Writer)
        {
            WorldCheckpointWriter::Result completed;
            if (d.Writer->Poll(completed))
            {
                ++d.Counters.CompletedCommands;
                d.Counters.FailedCommands += !completed.Success;
                if (!completed.Success) d.Counters.CheckpointError = completed.Error;
                else d.Counters.CheckpointError.clear();
                manager.GetEventBus().Emit("WorldCommandCompleted", CommandResult{ completed.Serial, CommandKind::Save, completed.Success, completed.Error });
            }
        }
        // Process a fixed prefix: completion handlers may enqueue next-tick work, never recurse.
        const size_t commandCount = std::min(size_t(4), d.Commands.size());
        for (size_t i = 0; i < commandCount; ++i)
        {
            if (d.Writer && d.Writer->IsBusy()) break;
            auto [serial, command] = std::move(d.Commands.front());
            d.Commands.pop_front();
            bool asynchronous = false;
            CommandResult result;
            result.Serial = serial;
            result.Kind = command.Kind;
            try
            {
                switch (command.Kind)
                {
                    case CommandKind::Capture: result.Success = CaptureCell(command.Cell, result.Error); break;
                    case CommandKind::Save:
                    {
                        auto next = *d.State;
                        uint64_t captures = 0;
                        bool valid = !d.PendingState;
                        if (!valid) result.Error = "Restore pending";
                        for (size_t cell = 0; valid && cell < d.Cells.size(); ++cell)
                            if (d.Cells[cell].Info.Stage == WorldRuntimeStage::Active)
                            {
                                valid = d.Capture(cell, next, result.Error);
                                ++captures;
                            }
                        if (valid)
                        {
                            if (!d.Writer) d.Writer = std::make_unique<WorldCheckpointWriter>();
                            if (d.Writer->Submit(next, command.File, serial, result.Error))
                            {
                                d.PublishOverrides(next, result.Error);
                                *d.State = std::move(next);
                                d.Counters.Captures += captures;
                                result.Success = true;
                                asynchronous = true;
                            }
                        }
                        break;
                    }
                    case CommandKind::Restore: result.Success = RequestRestoreCheckpoint(command.File, result.Error); break;
                    case CommandKind::Reset:   result.Success = RequestResetToAuthored(result.Error); break;
                    case CommandKind::Adopt:   result.Success = AdoptEntity(command.Cell, command.Entity, result.Error); break;
                    case CommandKind::Remove:  result.Success = RemoveCellEntity(command.Cell, command.Entity, result.Error); break;
                }
            }
            catch (const std::exception& e)
            {
                result.Error = e.what();
            }
            if (asynchronous) continue;
            ++d.Counters.CompletedCommands;
            d.Counters.FailedCommands += !result.Success;
            if (!result.Success) d.Counters.CheckpointError = result.Error;
            manager.GetEventBus().Emit("WorldCommandCompleted", result);
        }
        if (!d.Override)
            if (auto* render = manager.GetRenderScene())
                if (auto camera = render->GetPrimaryCameraEntity()) d.Viewer = glm::dvec3(render->GetWorldTransform(camera)[3]);
        if (!std::isfinite(d.Viewer.x) || !std::isfinite(d.Viewer.y) || !std::isfinite(d.Viewer.z)) return;
        d.Select();
        if (d.PendingState)
            for (auto& cell : d.Cells) cell.Info.Desired = false;
        size_t unloads = 0;
        for (auto it = d.Order.rbegin(); it != d.Order.rend(); ++it)
        {
            auto& c = d.Cells[d.Index.at(*it)];
            if (c.Info.Desired) continue;
            if (c.Info.Stage == WorldRuntimeStage::Active)
            {
                if (unloads >= d.Rules.MaxUnloadsPerFrame) continue;
                if (d.CaptureOnUnload && !d.PendingState)
                {
                    std::string captureError;
                    if (!d.Capture(d.Index.at(c.Info.ID), *d.State, captureError) || !d.Loader->SetSourceOverride(c.Info.ID, d.State->Get(c.Info.ID), captureError))
                    {
                        c.Info.Error = captureError;
                        d.Counters.CheckpointError = captureError;
                        continue;
                    }
                    ++d.Counters.Captures;
                }
                for (auto entity = c.Owned.rbegin(); entity != c.Owned.rend(); ++entity)
                    if (auto live = d.Anchor->GetEntityByUUID(*entity)) d.Anchor->DestroyEntity(live);
                c.Owned.clear();
                manager.GetEventBus().Emit("WorldCellUnloaded", CellEvent{ c.Info.ID, d.Manifest.Cells[d.Index.at(c.Info.ID)].ScenePath });
                ++unloads;
                ++d.Counters.Unloads;
            }
            c.Job.reset();
            c.SceneRef.reset();
            d.ReleaseAssets(c, true); // (after the staged scene: its components hold no pending texture any more)
            c.Resolve.clear();
            c.Resolved = 0;
            const auto measured = c.Info.MeasuredGPUBytes;
            c.Info = { c.Info.ID, WorldRuntimeStage::Dormant, false, false, 0, 0, {} };
            c.Info.MeasuredGPUBytes = measured; // what its assets asked of the GPU is remembered across visits
            c.Info.Held = c.HeldUntil > d.Tick || c.AwaitsRoom;
        }
        d.Recount();
        if (d.PendingState)
        {
            std::vector<UUID> retained;
            for (const auto& cell : d.Cells)
                if (cell.Info.Admitted) retained.push_back(cell.Info.ID);
            std::string restoreError;
            d.Loader->SetWanted(retained, restoreError);
            d.Loader->Poll();
            if (retained.empty())
            {
                if (d.PublishOverrides(*d.PendingState, restoreError))
                {
                    d.State = std::move(d.PendingState);
                    ++d.Counters.Restores;
                    for (auto& cell : d.Cells)
                    {
                        cell.Info.Stage = WorldRuntimeStage::Dormant;
                        cell.Info.Error.clear();
                    }
                    d.Counters.CheckpointError.clear();
                }
                else
                {
                    d.Counters.CheckpointError = restoreError;
                    d.PendingState.reset();
                }
            }
            d.UpdateNavigation();
            d.PumpQueries();
            d.Recount();
            d.Counters.LastMilliseconds = Elapsed(start);
            d.Counters.PeakMilliseconds = std::max(d.Counters.PeakMilliseconds, d.Counters.LastMilliseconds);
            return;
        }
        d.Counters.Residency = AssetResidency::Inspect();
        auto evaluatePressure = [&] {
            const auto& memory = d.Counters.Residency;
            return d.Pressure.Update(memory.Process.Resident, memory.Process.Valid, memory.RequestedGPUBytes,
                                     d.Rules.ObservedRSSLimitBytes, d.Rules.RequestedGPULimitBytes, d.Rules.PressureResumeRatio);
        };
        d.Counters.MemoryPressure = evaluatePressure();
        // Scan on retirement, periodically, or under pressure; no wait for async work and no live eviction.
        if (unloads || ++d.Frame % 60 == 0 || d.Counters.MemoryPressure || d.RoomWanted || d.TrimAgain)
        {
            d.Trim(d.Counters.MemoryPressure || d.RoomWanted ? 0 : d.Rules.CacheEntriesPerKind);
            d.Counters.Residency = AssetResidency::Inspect();
            d.Counters.MemoryPressure = evaluatePressure();
        }
        d.Counters.PressureFrames += d.Counters.MemoryPressure;
        d.FreedSinceRelease |= unloads > 0;
        // Still over a limit and the caches have nothing more to give: one cell goes.
        if (d.Counters.MemoryPressure && d.Rules.EvictOnPressure && !unloads && d.CacheEvictionsThisFrame < d.Rules.MaxCacheEvictionsPerFrame)
        {
            const auto& memory = d.Counters.Residency;
            const bool gpuSide = d.Rules.RequestedGPULimitBytes &&
                                 static_cast<long double>(memory.RequestedGPUBytes) >= static_cast<long double>(d.Rules.RequestedGPULimitBytes) * d.Rules.PressureResumeRatio;
            if (gpuSide) d.EvictFarthest(); // tracked requests are exact and immediate
            else if (d.FreedSinceRelease)
            {
                // Observed RSS lags what was freed: the allocator keeps it. Hand it back, then wait for a fresh
                // sample (they are read at most twice a second) before deciding that a cell has to go.
                d.ReleaseFreeMemory(); // (on its helper thread)
                d.FreedSinceRelease = false;
                ++d.Counters.MemoryReleases;
                d.NextRSSAction = Clock::now() + std::chrono::milliseconds(750);
            }
            else if (Clock::now() >= d.NextRSSAction)
            {
                d.EvictFarthest();
                d.NextRSSAction = Clock::now() + std::chrono::milliseconds(750);
            }
        }
        for (auto id : d.Order)
        {
            const size_t index = d.Index.at(id);
            auto& c = d.Cells[index];
            const auto& source = d.Manifest.Cells[index];
            if (!c.Info.Desired || c.Info.Admitted || c.Info.Stage == WorldRuntimeStage::Failed) continue;
            bool wait = false, failed = false;
            for (auto dependency : source.Dependencies)
            {
                const auto& dep = d.Cells[d.Index.at(dependency)].Info;
                wait |= !dep.Admitted;
                failed |= dep.Stage == WorldRuntimeStage::Failed;
            }
            if (failed)
            {
                d.Fail(c, "Runtime cell dependency failed");
                continue;
            }
            if (d.Counters.MemoryPressure || wait || d.Counters.Admitted >= d.Rules.MaxResidentCells ||
                source.DeclaredCPUBytes > d.Rules.DeclaredCPUBytes - d.Counters.DeclaredCPU || source.DeclaredGPUBytes > d.Rules.DeclaredGPUBytes - d.Counters.DeclaredGPU) continue;
            c.Info.Admitted = true;
            c.Info.Stage = WorldRuntimeStage::Loading;
            ++d.Counters.Admitted;
            d.Counters.DeclaredCPU += source.DeclaredCPUBytes;
            d.Counters.DeclaredGPU += source.DeclaredGPUBytes;
        }
        std::vector<UUID> wanted;
        for (const auto& c : d.Cells)
            if (c.Info.Admitted) wanted.push_back(c.Info.ID);
        std::string error;
        if (!d.Loader->SetWanted(wanted, error)) return;
        d.Loader->Poll();
        size_t activations = 0, resolved = 0;
        bool advanced = false;
        d.Recount();
        for (auto id : d.Order)
        {
            auto& c = d.Cells[d.Index.at(id)];
            if (!c.Info.Desired || !c.Info.Admitted || c.Info.Stage == WorldRuntimeStage::Active) continue;
            const auto& source = d.Manifest.Cells[d.Index.at(id)];
            bool dependenciesActive = true, dependencyFailed = false;
            for (auto dependency : source.Dependencies)
            {
                const auto stage = d.Cells[d.Index.at(dependency)].Info.Stage;
                dependenciesActive &= stage == WorldRuntimeStage::Active;
                dependencyFailed |= stage == WorldRuntimeStage::Failed;
            }
            if (dependencyFailed)
            {
                d.Fail(c, "Runtime cell dependency failed");
                continue;
            }
            if (!dependenciesActive || d.Counters.MemoryPressure) continue;
            if (advanced && Elapsed(start) >= d.Rules.MainThreadMilliseconds) break;
            if (c.Info.Stage == WorldRuntimeStage::Loading)
            {
                auto status = d.Loader->GetStatus(id);
                c.Info.Generation = status.Generation;
                if (status.Stage == WorldCellStage::Failed)
                {
                    d.Fail(c, status.Error);
                    continue;
                }
                if (status.Stage != WorldCellStage::Ready || d.Counters.Building >= d.Rules.MaxBuildingCells) continue;
                c.Job = d.Loader->TakeReady(id, status.Generation);
                if (!c.Job) continue;
                if (c.Job->GetEntityCount() > d.Rules.MaxEntitiesPerCell || c.Job->GetEntityCount() > d.Rules.MaxResidentEntities - d.Counters.Entities)
                {
                    d.Fail(c, "Resident entity budget exceeded");
                    continue;
                }
                c.Info.Entities = c.Job->GetEntityCount();
                d.Counters.Entities += c.Info.Entities;
                c.Resolved = 0;
                c.SceneRef = CreateRef<Scene>();
                c.GPUAtBuild = AssetResidency::Inspect().RequestedGPUBytes;
                c.Info.Stage = WorldRuntimeStage::Building;
                ++d.Counters.Building;
            }
            if (c.Info.Stage == WorldRuntimeStage::Building)
            {
                advanced = true;
                if (!c.Job->Build(*c.SceneRef, std::max(0.0, d.Rules.MainThreadMilliseconds - Elapsed(start)))) continue;
                if (!c.Job->GetError().empty())
                {
                    d.Fail(c, c.Job->GetError());
                    continue;
                }
                if (d.State->Get(id) && c.Job->GetReport().HasIssues())
                {
                    d.Fail(c, "Checkpoint scene contained invalid components or hierarchy");
                    continue;
                }
                const auto offset = d.Manifest.Bounds(source).first;
                if (std::abs(offset.x) > 1e6 || std::abs(offset.y) > 1e6 || std::abs(offset.z) > 1e6)
                {
                    d.Fail(c, "Floating origin is required outside +/-1000000 runtime units");
                    continue;
                }
                bool invalid = false;
                for (auto entity : c.SceneRef->GetEntitiesInOrder())
                {
                    if (entity.HasComponent<SubSceneComponent>())
                    {
                        invalid = true;
                        error = "Streamed cells cannot contain SubScene components";
                        break;
                    }
                    if (manager.FindEntityByUUID(entity.GetUUID()))
                    {
                        invalid = true;
                        error = "Cell entity UUID collides with a loaded scene";
                        break;
                    }
                    if (!c.SceneRef->GetParent(entity)) entity.GetTransform().Translation += glm::vec3(offset);
                    c.Resolve.push_back(entity.GetUUID());
                }
                if (invalid)
                {
                    d.Fail(c, error);
                    continue;
                }
                c.Info.Stage = d.Stager ? WorldRuntimeStage::Staging : WorldRuntimeStage::Resolving;
                c.Job.reset();
            }
            if (c.Info.Stage == WorldRuntimeStage::Staging)
            {
                // Workers decode / parse; AsyncLoader::Update uploads within its budget. Nothing is loaded here.
                if (!d.AdvanceAssets(c, error))
                {
                    if (!error.empty()) d.Fail(c, error);
                    continue;
                }
                c.Info.Stage = WorldRuntimeStage::Resolving;
            }
            if (c.Info.Stage == WorldRuntimeStage::Resolving)
            {
                advanced = true;
                bool failed = false;
                while (c.Resolved < c.Resolve.size() && resolved < d.Rules.MaxResolveEntitiesPerFrame && (!resolved || Elapsed(start) < d.Rules.MainThreadMilliseconds))
                {
                    try
                    {
                        if (auto entity = c.SceneRef->GetEntityByUUID(c.Resolve[c.Resolved])) c.SceneRef->ResolveEntityAssets(entity);
                    }
                    catch (const std::exception& e)
                    {
                        d.Fail(c, e.what());
                        failed = true;
                        break;
                    }
                    ++c.Resolved;
                    ++resolved;
                }
                if (failed) continue;
                if (c.Resolved != c.Resolve.size() || activations >= d.Rules.MaxActivationsPerFrame) continue;
                try
                {
                    c.Owned = d.Anchor->ImportStagedCell(*c.SceneRef, error);
                    if (!error.empty())
                    {
                        d.Fail(c, error);
                        continue;
                    }
                    c.SceneRef.reset();
                    d.ReleaseAssets(c, false); // the imported entities hold them now
                    c.Info.Stage = WorldRuntimeStage::Active;
                    // One cell builds at a time: the growth of the tracked GPU requests since it began is its own
                    // (assets, generated meshes). Remembered for the GPU limit's admission.
                    if (d.Rules.MaxBuildingCells == 1)
                        if (const uint64_t now = AssetResidency::Inspect().RequestedGPUBytes; now > c.GPUAtBuild)
                            c.Info.MeasuredGPUBytes = std::max(c.Info.MeasuredGPUBytes, now - c.GPUAtBuild);
                    c.Resolve.clear();
                    manager.GetEventBus().Emit("WorldCellLoaded", CellEvent{ c.Info.ID, source.ScenePath });
                    ++activations;
                    ++d.Counters.Activations;
                    if (d.Counters.Building) --d.Counters.Building;
                }
                catch (const std::exception& e)
                {
                    for (auto entity : c.Owned)
                        if (auto live = d.Anchor->GetEntityByUUID(entity)) d.Anchor->DestroyEntity(live);
                    c.Owned.clear();
                    d.Fail(c, e.what());
                }
            }
        }
        d.UpdateNavigation();
        d.PumpQueries();
        d.Counters.LastMilliseconds = Elapsed(start);
        d.Counters.PeakMilliseconds = std::max(d.Counters.PeakMilliseconds, d.Counters.LastMilliseconds);
        d.Recount();
    }

    void WorldStreaming::Clear(SceneManager& manager)
    {
        auto& d = *m_Data;
        if (auto root = d.Anchor->GetEntityByUUID(d.NavigationRoot)) d.Anchor->DestroyEntity(root);
        d.NavigationRoot = UUID(0);
        for (auto it = d.Order.rbegin(); it != d.Order.rend(); ++it)
        {
            auto& c = d.Cells[d.Index.at(*it)];
            if (c.Info.Stage == WorldRuntimeStage::Active)
            {
                for (auto entity = c.Owned.rbegin(); entity != c.Owned.rend(); ++entity)
                    if (auto live = d.Anchor->GetEntityByUUID(*entity)) d.Anchor->DestroyEntity(live);
                c.Owned.clear();
                manager.GetEventBus().Emit("WorldCellUnloaded", CellEvent{ c.Info.ID, d.Manifest.Cells[d.Index.at(c.Info.ID)].ScenePath });
            }
        }
    }
    const WorldManifest& WorldStreaming::GetManifest() const { return m_Data->Manifest; }
    const WorldStreaming::Policy& WorldStreaming::GetPolicy() const { return m_Data->Rules; }
    std::vector<WorldStreaming::CellInfo> WorldStreaming::GetCells() const
    {
        std::vector<CellInfo> info;
        for (const auto& c : m_Data->Cells) info.push_back(c.Info);
        return info;
    }
    WorldStreaming::Stats WorldStreaming::GetStats() const { return m_Data->Counters; }
    glm::dvec3 WorldStreaming::GetViewer() const { return m_Data->Viewer; }
    void WorldStreaming::SetViewerOverride(const glm::dvec3& viewer)
    {
        m_Data->Viewer = viewer;
        m_Data->Override = true;
    }
    void WorldStreaming::ClearViewerOverride() { m_Data->Override = false; }
    void WorldStreaming::SynchronizeNavigation()
    {
        m_Data->UpdateNavigation();
        m_Data->Counters.NavigationQueries = m_Data->Queries ? m_Data->Queries->GetStats() : NavQueryQueue::Stats{};
    }
    UUID WorldStreaming::GetNavigationEntity(int dimension) const
    {
        const auto& d = *m_Data;
        return d.Rules.NavigationMode && (dimension == 0 || dimension == int(d.Rules.NavigationMode)) ? d.NavigationRoot : UUID(0);
    }
    std::shared_ptr<const NavAtlas> WorldStreaming::GetNavigationAtlas() const { return m_Data->Atlas; }
    NavRoute WorldStreaming::FindNavigationRoute(glm::vec2 from, glm::vec2 to, const NavPathOptions& options) const
    {
        if (const auto atlas = GetNavigationAtlas()) return atlas->FindRoute(from, to, options);
        NavRoute result;
        result.Status = NavPathStatus::Failed;
        return result;
    }
    uint64_t WorldStreaming::SubmitNavigationQuery(UUID id, glm::vec2 from, glm::vec2 to, const NavPathOptions& options, std::string& error)
    {
        auto& d = *m_Data;
        auto entity = d.Anchor->GetEntityByUUID(id);
        const auto* agent = entity ? entity.TryGetComponent<NavAgentComponent>() : nullptr;
        if (!d.Rules.AsyncNavigation || !agent || !agent->WorldNavigation || !agent->HasDestination || !d.Anchor->IsActiveInHierarchy(entity))
        {
            error = "Navigation request needs an active world agent";
            return 0;
        }
        if (!d.Queries)
        {
            NavQueryQueue::Limits limits;
            limits.Workers = d.Rules.NavigationQueryWorkers;
            limits.Capacity = d.Rules.NavigationQueryCapacity;
            d.Queries = std::make_unique<NavQueryQueue>(limits);
        }
        const auto serial = d.Queries->Submit(uint64_t(id), d.Atlas, from, to, options, error);
        if (serial) d.QueryEntities[uint64_t(id)] = uint32_t(entity);
        return serial;
    }
    bool WorldStreaming::HasNavigationQuery(UUID id) const { return m_Data->Queries && m_Data->Queries->Has(uint64_t(id)); }
    void WorldStreaming::CancelNavigationQuery(UUID id)
    {
        if (m_Data->Queries) m_Data->Queries->Cancel(uint64_t(id));
    }
    bool WorldStreaming::PollNavigationQuery(UUID id, NavQueryQueue::Result& result)
    {
        auto& d = *m_Data;
        if (!d.Queries || !d.Queries->Poll(uint64_t(id), result)) return false;
        auto entity = d.Anchor->GetEntityByUUID(id);
        auto* agent = entity ? entity.TryGetComponent<NavAgentComponent>() : nullptr;
        const auto binding = d.QueryEntities.find(uint64_t(id));
        const bool sameEntity = entity && binding != d.QueryEntities.end() && binding->second == uint32_t(entity);
        d.QueryEntities.erase(uint64_t(id));
        bool valid = sameEntity && d.Atlas && result.Route.Revision == d.Atlas->GetRevision() && agent && agent->WorldNavigation && agent->HasDestination && d.Anchor->IsActiveInHierarchy(entity);
        if (valid)
        {
            const auto position = d.Anchor->GetWorldPosition(entity), destination = agent->Destination;
            const bool three = d.Rules.NavigationMode == 3;
            const auto plane = [&](glm::vec3 p) { return three ? glm::vec2(p.x, p.z) : glm::vec2(p); };
            const float offset = entity.HasComponent<CharacterController3DComponent>() ? 0.f : agent->BaseOffset;
            valid = !agent->RepathRequested && result.To == plane(destination) && glm::length(result.From - plane(position)) <= .1f && d.Atlas->SameWalkableCell(result.From, plane(position)) &&
                    result.Options.SmoothAtlas == agent->SmoothPath && result.Options.MaxExpanded == int(std::clamp(agent->WorldMaxExpanded, 1u, 1048576u)) &&
                    (!three || std::abs(position.y - offset - result.Options.StartHeight) <= .2f);
        }
        if (!valid)
        {
            ++d.Counters.NavigationStaleResults;
            result = {};
            return false;
        }
        return true;
    }
    bool WorldStreaming::SetNavigationLinks(std::vector<NavTraversalLink> links, std::string& error)
    {
        if (!NavAtlas::ValidateLinks(links, error)) return false;
        m_Data->Rules.NavigationLinks = std::move(links);
        m_Data->NavigationDirty = true;
        return true;
    }
    uint64_t WorldStreaming::Enqueue(Command command, std::string& error)
    {
        auto& d = *m_Data;
        if (d.Commands.size() + (d.Writer && d.Writer->IsBusy() ? 1 : 0) >= 64 || d.NextCommand == UINT64_MAX || command.File.native().size() > 4096 ||
            int(command.Kind) < int(CommandKind::Capture) || int(command.Kind) > int(CommandKind::Remove))
        {
            error = "Invalid world command or pending queue limit reached";
            return 0;
        }
        const auto serial = ++d.NextCommand;
        d.Commands.emplace_back(serial, std::move(command));
        d.Recount();
        error.clear();
        return serial;
    }
    void WorldStreaming::SetCaptureOnUnload(bool enabled)
    {
        m_Data->CaptureOnUnload = enabled;
        m_Data->Recount();
    }
    bool WorldStreaming::CaptureCell(UUID cell, std::string& error)
    {
        auto& d = *m_Data;
        const auto it = d.Index.find(uint64_t(cell));
        if (it == d.Index.end() || d.PendingState)
        {
            error = "Unknown cell or restore pending";
            return false;
        }
        auto next = *d.State;
        if (!d.Capture(it->second, next, error) || !d.Loader->SetSourceOverride(cell, next.Get(cell), error)) return false;
        *d.State = std::move(next);
        ++d.Counters.Captures;
        d.Counters.CheckpointError.clear();
        d.Recount();
        return true;
    }
    bool WorldStreaming::SaveCheckpoint(const std::filesystem::path& file, std::string& error)
    {
        auto& d = *m_Data;
        if (d.PendingState || (d.Writer && d.Writer->IsBusy()))
        {
            error = "Restore or checkpoint write pending";
            return false;
        }
        auto next = *d.State;
        uint64_t captures = 0;
        for (size_t i = 0; i < d.Cells.size(); ++i)
            if (d.Cells[i].Info.Stage == WorldRuntimeStage::Active)
            {
                if (!d.Capture(i, next, error)) return false;
                ++captures;
            }
        if (!next.Save(file, error)) return false;
        if (!d.PublishOverrides(next, error)) return false;
        *d.State = std::move(next);
        d.Counters.Captures += captures;
        d.Counters.CheckpointError.clear();
        d.Recount();
        return true;
    }
    bool WorldStreaming::RequestRestoreCheckpoint(const std::filesystem::path& file, std::string& error)
    {
        auto& d = *m_Data;
        auto next = std::make_unique<WorldCheckpoint>(d.Manifest);
        if (!next->Load(file, error)) return false;
        // Validate all components before retiring any live cell. No assets / scripts / physics
        // are started by a non-running staging scene. GPU and runtime activation still happen later.
        std::unordered_set<uint64_t> restoredIDs, ownedIDs;
        for (const auto& cell : d.Cells)
            for (auto id : cell.Owned) ownedIDs.insert(uint64_t(id));
        for (const auto& [id, text] : next->GetCells())
        {
            (void)id;
            SceneLoadJob job;
            Scene staged;
            if (!job.ParseString(*text))
            {
                error = job.GetError();
                return false;
            }
            while (!job.Build(staged, 10))
                if (!job.GetError().empty())
                {
                    error = job.GetError();
                    return false;
                }
            if (!job.GetError().empty() || job.GetReport().HasIssues())
            {
                error = "Checkpoint component preflight failed";
                return false;
            }
            if (job.GetEntityCount() > d.Rules.MaxEntitiesPerCell)
            {
                error = "Checkpoint exceeds per-cell entity budget";
                return false;
            }
            for (auto entity : staged.GetEntitiesInOrder())
            {
                const auto key = uint64_t(entity.GetUUID());
                if (!restoredIDs.insert(key).second || (d.Anchor->GetEntityByUUID(entity.GetUUID()) && !ownedIDs.contains(key)))
                {
                    error = "Checkpoint entity UUID conflicts with another cell or persistent anchor";
                    return false;
                }
            }
        }
        d.PendingState = std::move(next);
        d.Counters.CheckpointError.clear();
        d.Recount();
        return true;
    }
    bool WorldStreaming::RequestResetToAuthored(std::string& error)
    {
        auto& d = *m_Data;
        d.PendingState = std::make_unique<WorldCheckpoint>(d.Manifest);
        error.clear();
        d.Recount();
        return true;
    }
    std::vector<UUID> WorldStreaming::GetCellEntities(UUID cell) const
    {
        const auto& d = *m_Data;
        std::vector<UUID> out;
        auto it = d.Index.find(uint64_t(cell));
        if (it != d.Index.end())
            for (auto id : d.Cells[it->second].Owned)
                if (d.Anchor->GetEntityByUUID(id)) out.push_back(id);
        return out;
    }
    bool WorldStreaming::AdoptEntity(UUID cell, UUID entity, std::string& error)
    {
        auto& d = *m_Data;
        auto it = d.Index.find(uint64_t(cell));
        auto root = d.Anchor->GetEntityByUUID(entity);
        if (it == d.Index.end() || !root || d.PendingState || d.Cells[it->second].Info.Stage != WorldRuntimeStage::Active || entity == d.NavigationRoot)
        {
            error = "Adoption needs an active cell and a live anchor entity";
            return false;
        }
        auto& target = d.Cells[it->second];
        std::vector<UUID> add;
        std::vector<Entity> pending{ root };
        while (!pending.empty())
        {
            auto e = pending.back();
            pending.pop_back();
            if (e.GetUUID() == d.NavigationRoot)
            {
                error = "Internal navigation entity cannot be adopted";
                return false;
            }
            for (size_t i = 0; i < d.Cells.size(); ++i)
                if (std::find(d.Cells[i].Owned.begin(), d.Cells[i].Owned.end(), e.GetUUID()) != d.Cells[i].Owned.end())
                {
                    if (i != it->second)
                    {
                        error = "Entity is owned by another cell";
                        return false;
                    }
                    goto owned;
                }
            add.push_back(e.GetUUID());
        owned:
            for (auto child : d.Anchor->GetChildren(e)) pending.push_back(child);
        }
        if (auto parent = d.Anchor->GetParent(root); parent && std::find(target.Owned.begin(), target.Owned.end(), parent.GetUUID()) == target.Owned.end())
        {
            error = "Adopted root parent must belong to the target cell";
            return false;
        }
        if (add.size() > d.Rules.MaxEntitiesPerCell - target.Info.Entities || add.size() > d.Rules.MaxResidentEntities - d.Counters.Entities)
        {
            error = "Adoption entity budget exceeded";
            return false;
        }
        target.Owned.insert(target.Owned.end(), add.begin(), add.end());
        target.Info.Entities += add.size();
        d.Recount();
        error.clear();
        return true;
    }
    bool WorldStreaming::RemoveCellEntity(UUID cell, UUID entity, std::string& error)
    {
        auto& d = *m_Data;
        auto it = d.Index.find(uint64_t(cell));
        auto root = d.Anchor->GetEntityByUUID(entity);
        if (it == d.Index.end() || !root || d.PendingState)
        {
            error = "Unknown cell/entity or restore pending";
            return false;
        }
        auto& target = d.Cells[it->second];
        std::vector<UUID> remove;
        std::vector<Entity> pending{ root };
        while (!pending.empty())
        {
            auto e = pending.back();
            pending.pop_back();
            if (std::find(target.Owned.begin(), target.Owned.end(), e.GetUUID()) == target.Owned.end())
            {
                error = "Subtree contains an entity outside cell ownership";
                return false;
            }
            remove.push_back(e.GetUUID());
            for (auto child : d.Anchor->GetChildren(e)) pending.push_back(child);
        }
        d.Anchor->DestroyEntity(root);
        std::erase_if(target.Owned, [&](UUID id) { return std::find(remove.begin(), remove.end(), id) != remove.end(); });
        target.Info.Entities -= std::min(target.Info.Entities, remove.size());
        d.Recount();
        error.clear();
        return true;
    }
    size_t WorldStreaming::TrimCaches()
    {
        auto& d = *m_Data;
        const auto count = d.Trim(0);
        d.Counters.Residency = AssetResidency::Inspect();
        return count;
    }
    void WorldStreaming::RetryFailures()
    {
        auto& d = *m_Data;
        std::string error;
        for (auto& c : d.Cells)
            if (c.Info.Stage == WorldRuntimeStage::Failed)
            {
                d.Loader->Invalidate(c.Info.ID, error);
                c.Info.Stage = WorldRuntimeStage::Dormant;
                c.Info.Error.clear();
                c.Info.Admitted = false;
            }
    }
    bool WorldStreaming::IsAnchor(const Scene* scene) const { return scene && m_Data->Anchor.get() == scene; }
    const char* WorldStreaming::StageName(WorldRuntimeStage stage)
    {
        switch (stage)
        {
            case WorldRuntimeStage::Dormant:   return "Dormant";
            case WorldRuntimeStage::Loading:   return "Loading";
            case WorldRuntimeStage::Building:  return "Building";
            case WorldRuntimeStage::Resolving: return "Resolving";
            case WorldRuntimeStage::Active:    return "Active";
            case WorldRuntimeStage::Failed:    return "Failed";
            case WorldRuntimeStage::Staging:   return "Staging";
        }
        return "Unknown";
    }
}
