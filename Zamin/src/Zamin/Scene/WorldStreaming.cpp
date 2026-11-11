#include "Zamin/Scene/WorldStreaming.h"
#include "Zamin/Scene/SceneManager.h"
#include "Zamin/Scene/Entity.h"
#include "Zamin/Project/Project.h"
#include "Zamin/Scripting/EventBus.h"
#include "Zamin/Utils/FileSystem.h"
#include <nlohmann/json.hpp>
#include <algorithm>
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
            return { { "LoadRadius", p.LoadRadius }, { "UnloadRadius", p.UnloadRadius }, { "MainThreadMilliseconds", p.MainThreadMilliseconds }, { "AxisMask", p.AxisMask }, { "Layers", p.Layers }, { "MaxResidentCells", p.MaxResidentCells }, { "MaxResidentEntities", p.MaxResidentEntities }, { "MaxEntitiesPerCell", p.MaxEntitiesPerCell }, { "MaxBuildingCells", p.MaxBuildingCells }, { "MaxActivationsPerFrame", p.MaxActivationsPerFrame }, { "MaxUnloadsPerFrame", p.MaxUnloadsPerFrame }, { "MaxResolveEntitiesPerFrame", p.MaxResolveEntitiesPerFrame }, { "DeclaredCPUBytes", p.DeclaredCPUBytes }, { "DeclaredGPUBytes", p.DeclaredGPUBytes } };
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
            DeclaredCPUBytes > WorldManifest::MaxDeclaredBytesPerCell || DeclaredGPUBytes > WorldManifest::MaxDeclaredBytesPerCell || Layers.size() > 32)
        {
            error = "Invalid world streaming policy";
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

        void Recount()
        {
            auto previous = Counters;
            Counters = {};
            Counters.Activations = previous.Activations;
            Counters.Unloads = previous.Unloads;
            Counters.LastMilliseconds = previous.LastMilliseconds;
            Counters.PeakMilliseconds = previous.PeakMilliseconds;
            Counters.BudgetBlocked = previous.BudgetBlocked;
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
                Counters.Building += c.Info.Stage == WorldRuntimeStage::Building || c.Info.Stage == WorldRuntimeStage::Resolving;
                Counters.Failed += c.Info.Stage == WorldRuntimeStage::Failed;
            }
            Counters.Loader = Loader->GetStats();
        }
        void Fail(Cell& cell, std::string error)
        {
            cell.Job.reset();
            cell.SceneRef.reset();
            cell.Resolve.clear();
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
                if (distance <= radius * radius) candidates.emplace_back(distance, i);
            }
            std::sort(candidates.begin(), candidates.end());
            size_t count = 0;
            uint64_t cpu = 0, gpu = 0;
            Counters.BudgetBlocked = 0;
            Data::Mask chosen{};
            for (auto [distance, index] : candidates)
            {
                (void)distance;
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
        d->Manifest = manifest;
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
        if (!d.Override)
            if (auto* render = manager.GetRenderScene())
                if (auto camera = render->GetPrimaryCameraEntity()) d.Viewer = glm::dvec3(render->GetWorldTransform(camera)[3]);
        if (!std::isfinite(d.Viewer.x) || !std::isfinite(d.Viewer.y) || !std::isfinite(d.Viewer.z)) return;
        d.Select();
        size_t unloads = 0;
        for (auto it = d.Order.rbegin(); it != d.Order.rend(); ++it)
        {
            auto& c = d.Cells[d.Index.at(*it)];
            if (c.Info.Desired) continue;
            if (c.Info.Stage == WorldRuntimeStage::Active)
            {
                if (unloads >= d.Rules.MaxUnloadsPerFrame) continue;
                for (auto entity = c.Owned.rbegin(); entity != c.Owned.rend(); ++entity)
                    if (auto live = d.Anchor->GetEntityByUUID(*entity)) d.Anchor->DestroyEntity(live);
                c.Owned.clear();
                manager.GetEventBus().Emit("WorldCellUnloaded", CellEvent{ c.Info.ID, d.Manifest.Cells[d.Index.at(c.Info.ID)].ScenePath });
                ++unloads;
                ++d.Counters.Unloads;
            }
            c.Job.reset();
            c.SceneRef.reset();
            c.Resolve.clear();
            c.Info = { c.Info.ID, WorldRuntimeStage::Dormant, false, false, 0, 0, {} };
        }
        d.Recount();
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
            if (wait || d.Counters.Admitted >= d.Rules.MaxResidentCells ||
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
            if (!dependenciesActive) continue;
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
                c.SceneRef = CreateRef<Scene>();
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
                c.Info.Stage = WorldRuntimeStage::Resolving;
                c.Job.reset();
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
                    c.Info.Stage = WorldRuntimeStage::Active;
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
        d.Counters.LastMilliseconds = Elapsed(start);
        d.Counters.PeakMilliseconds = std::max(d.Counters.PeakMilliseconds, d.Counters.LastMilliseconds);
        d.Recount();
    }

    void WorldStreaming::Clear(SceneManager& manager)
    {
        auto& d = *m_Data;
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
        }
        return "Unknown";
    }
}
