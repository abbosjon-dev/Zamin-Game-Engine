#include "WorldStreamingPanel.h"
#include <Zamin/Scene/WorldStreaming.h>
#include <Zamin/Scene/WorldAuthoring.h>
#include <Zamin/Scene/Entity.h>
#include <Zamin/Core/SaveData.h>
#include <Zamin/Scene/SceneManager.h>
#include <Zamin/Project/Project.h>
#include <Zamin/Utils/FileSystem.h>
#include <imgui.h>
#include <imgui_stdlib.h>
#include <algorithm>
#include <cstdlib>
#include <set>

namespace Zamin {
    namespace {
        std::filesystem::path s_SceneRequest;
        bool s_Open = std::getenv("ZAMIN_EDITOR_WORLD") != nullptr;
        struct State
        {
            std::filesystem::path Project;
            std::string Path = "Worlds/Main.zworld", Error;
            WorldManifest Manifest;
            WorldStreaming::Policy Policy;
            int Selected = -1;
            bool Loaded = false, Observer = false;
            glm::dvec3 Position{ 0 };
            std::array<float, 180> RSSHistory{}, GPUHistory{};
            int HistoryOffset = 0;
            WorldGridSettings Grid;
            std::string TemplateScene;
            WorldGridResult Created;
            std::string CheckpointPath;
            uint64_t AdoptID = 0;
            bool Capture = false;
            bool AuthoringQA = false;
            int CheckpointQA = 0;
            UUID ProbeCell{ 0 }, ProbeActor{ 0 };
        } s;
        ImU32 Color(WorldRuntimeStage stage)
        {
            switch (stage)
            {
                case WorldRuntimeStage::Active:    return IM_COL32(45, 176, 132, 255);
                case WorldRuntimeStage::Failed:    return IM_COL32(215, 76, 89, 255);
                case WorldRuntimeStage::Building:
                case WorldRuntimeStage::Resolving: return IM_COL32(226, 171, 66, 255);
                case WorldRuntimeStage::Loading:   return IM_COL32(75, 128, 224, 255);
                default:                           return IM_COL32(56, 66, 83, 255);
            }
        }
        std::filesystem::path File()
        {
            if (std::filesystem::path(s.Path).extension() != ".zworld") return {};
            auto file = FileSystem::SafeJoin(Project::GetAssetDirectory(), s.Path);
            return !file.empty() && FileSystem::IsSubPathOnDisk(file, Project::GetAssetDirectory(), false) ? file : std::filesystem::path{};
        }
        void Load()
        {
            auto file = File();
            if (file.empty())
            {
                s.Error = "Asset papkasi ichidagi .zworld yolini kiriting";
                return;
            }
            s.Loaded = WorldStreaming::LoadDefinition(file, s.Manifest, s.Policy, s.Error);
            s.Selected = -1;
        }
    }
    std::filesystem::path TakeWorldSceneRequest()
    {
        auto path = std::move(s_SceneRequest);
        s_SceneRequest.clear();
        return path;
    }
    bool* WorldStreamingPanelOpen() { return &s_Open; }
    void DrawWorldStreamingPanel(SceneManager* runtime)
    {
        if (!s_Open) return;
        ImGui::SetNextWindowSize(ImVec2(800, 740), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("World Streaming###WorldStreaming", &s_Open))
        {
            ImGui::End();
            return;
        }
        if (!Project::GetActive())
        {
            ImGui::TextDisabled("Avval loyihani oching.");
            ImGui::End();
            return;
        }
        if (s.Project != Project::GetProjectDirectory())
        {
            s = {};
            s.Project = Project::GetProjectDirectory();
            s.CheckpointPath = (SaveData::GetDirectoryForProject(Project::GetActive()->GetConfig().Name) / "world-checkpoint.json").string();
            const auto configured = Project::GetActive()->GetConfig().StreamingWorld;
            if (!configured.empty())
            {
                s.Path = configured.generic_string();
                Load();
            }
        }
        ImGui::TextColored(ImVec4(.3f, .85f, .72f, 1), "WORLD / 2D + 3D");
        ImGui::SameLine();
        ImGui::TextDisabled("camera cells - lifecycle - residency admission");
        ImGui::SetNextItemWidth(-170);
        ImGui::InputText("##worldpath", &s.Path);
        ImGui::SameLine();
        if (ImGui::Button("Ochish")) Load();
        ImGui::SameLine();
        if (ImGui::Button("Yangi"))
        {
            s.Manifest = {};
            s.Manifest.Name = "World";
            s.Policy = {};
            s.Loaded = true;
            s.Selected = -1;
            s.Error.clear();
        }
        if (const char* qa = std::getenv("ZAMIN_EDITOR_WORLD_AUTHORING"); qa && !s.AuthoringQA)
        {
            s.AuthoringQA = true;
            s.Grid.Columns = 3;
            s.Grid.Rows = 2;
            s.Grid.ThreeD = std::string_view(qa) != "2d";
            s.Grid.CellSize = s.Grid.ThreeD ? glm::vec2(32) : glm::vec2(32, 18);
            s.Grid.Folder = s.Grid.ThreeD ? "AuthoredQA3D" : "AuthoredQA2D";
            if (!CreateWorldGrid(Project::GetAssetDirectory(), s.Grid, s.Created, s.Error)) throw std::runtime_error(s.Error);
            WorldGridResult rejected;
            std::string why;
            if (CreateWorldGrid(Project::GetAssetDirectory(), s.Grid, rejected, why)) throw std::runtime_error("World wizard overwrote an existing bundle");
            auto& config = Project::GetActive()->GetConfig();
            config.StreamingWorld = s.Created.World;
            config.StartScene = s.Created.Anchor;
            if (!Project::SaveActive(Project::GetProjectFilePath())) throw std::runtime_error("World wizard project save failed");
            s.Path = s.Created.World.generic_string();
            Load();
            s_SceneRequest = Project::GetAssetFileSystemPath(s.Created.Anchor);
            ZM_CORE_INFO("World authoring editor QA passed: 6 cells, no overwrite, linked manifest and anchor");
        }
        auto* live = runtime ? runtime->GetWorldStreaming() : nullptr;

        if (std::getenv("ZAMIN_EDITOR_WORLD_AUTHORING")) ImGui::SetNextItemOpen(true, ImGuiCond_Once);
        if (ImGui::CollapsingHeader("Yangi 2D / 3D world yaratish"))
        {
            ImGui::BeginDisabled(runtime && runtime->IsRunning());
            ImGui::InputText("Bundle folder (new)", &s.Grid.Folder);
            ImGui::InputText("World name", &s.Grid.Name);
            ImGui::InputText("Cell template scene (optional)", &s.TemplateScene);
            s.Grid.TemplateScene = s.TemplateScene;
            if (ImGui::Checkbox("3D world", &s.Grid.ThreeD)) s.Grid.CellSize = s.Grid.ThreeD ? glm::vec2(32, 32) : glm::vec2(32, 18);
            ImGui::InputScalar("Columns", ImGuiDataType_U32, &s.Grid.Columns);
            ImGui::InputScalar("Rows", ImGuiDataType_U32, &s.Grid.Rows);
            ImGui::InputFloat2("Cell dimensions", &s.Grid.CellSize.x);
            ImGui::Checkbox("Terrain", &s.Grid.Terrain);
            ImGui::SameLine();
            ImGui::Checkbox("Navigation", &s.Grid.Navigation);
            ImGui::InputScalar("Terrain resolution (9/17/33/65)", ImGuiDataType_U32, &s.Grid.TerrainResolution);
            ImGui::InputScalar("Terrain seed", ImGuiDataType_U32, &s.Grid.Seed);
            ImGui::InputFloat("Nav cell size", &s.Grid.NavigationCellSize);
            ImGui::TextWrapped("CPU authoring: up to 256 cells. Creates camera, terrain/ground and navigation in a new bundle. Existing files stay protected.");
            if (ImGui::Button("World bundle yaratish"))
            {
                if (CreateWorldGrid(Project::GetAssetDirectory(), s.Grid, s.Created, s.Error))
                {
                    s.Path = s.Created.World.generic_string();
                    Load();
                }
            }
            if (!s.Created.World.empty())
            {
                ImGui::Text("Created: %zu cells / %zu files / %.2f MiB", s.Created.Cells, s.Created.Files, s.Created.Bytes / 1048576.0);
                if (ImGui::Button("Loyihaga ulash va anchor ochish"))
                {
                    auto& config = Project::GetActive()->GetConfig();
                    auto oldWorld = config.StreamingWorld, oldScene = config.StartScene;
                    config.StreamingWorld = s.Created.World;
                    config.StartScene = s.Created.Anchor;
                    if (Project::SaveActive(Project::GetProjectFilePath())) s_SceneRequest = Project::GetAssetFileSystemPath(s.Created.Anchor);
                    else
                    {
                        config.StreamingWorld = oldWorld;
                        config.StartScene = oldScene;
                        s.Error = "Project save failed; bundle remains available";
                    }
                }
            }
            ImGui::EndDisabled();
        }

        if (s.Loaded)
        {
            if (ImGui::Button("Saqlash va loyihaga ulash"))
            {
                auto file = File();
                if (!file.empty() && WorldStreaming::SaveDefinition(file, s.Manifest, s.Policy, s.Error))
                {
                    auto& config = Project::GetActive()->GetConfig();
                    auto old = config.StreamingWorld;
                    config.StreamingWorld = s.Path;
                    if (!Project::SaveActive(Project::GetProjectFilePath()))
                    {
                        config.StreamingWorld = old;
                        s.Error = "Loyiha saqlanmadi";
                    }
                }
                else if (file.empty()) s.Error = "World yoli asset papkasidan tashqarida";
            }
            if (runtime && runtime->IsRunning())
            {
                ImGui::SameLine();
                if (ImGui::Button("Runtime qayta ulash"))
                {
                    runtime->LoadStreamingWorld(s.Path, s.Error);
                    live = runtime->GetWorldStreaming();
                }
            }
        }
        if (live)
        {
            if (std::getenv("ZAMIN_EDITOR_WORLD_CHECKPOINT_QA"))
            {
                if (s.CheckpointQA == 0 && live->GetStats().Active)
                {
                    for (const auto& cell : live->GetCells())
                        if (cell.Stage == WorldRuntimeStage::Active)
                        {
                            s.ProbeCell = cell.ID;
                            break;
                        }
                    auto anchor = runtime->GetActiveScene();
                    auto actor = anchor->CreateEntity("Editor checkpoint QA actor");
                    s.ProbeActor = actor.GetUUID();
                    if (live->GetPolicy().NavigationMode == 2)
                    {
                        actor.GetTransform().Translation = { 16, 9, 0 };
                        actor.AddComponent<SpriteRendererComponent>().Color = { 1, .4f, .1f, 1 };
                    }
                    else
                    {
                        actor.GetTransform().Translation = { 16, 16, 16 };
                        actor.AddComponent<MeshRendererComponent>().Mat.Albedo = { 1, .4f, .1f, 1 };
                    }
                    if (!live->AdoptEntity(s.ProbeCell, s.ProbeActor, s.Error) || !live->SaveCheckpoint(s.CheckpointPath, s.Error)) throw std::runtime_error(s.Error);
                    s.CheckpointQA = 1;
                }
                else if (s.CheckpointQA == 1)
                {
                    if (!live->RemoveCellEntity(s.ProbeCell, s.ProbeActor, s.Error) || !live->RequestRestoreCheckpoint(s.CheckpointPath, s.Error)) throw std::runtime_error(s.Error);
                    s.CheckpointQA = 2;
                }
                else if (s.CheckpointQA == 2 && live->GetStats().Restores && !live->GetStats().RestorePending && runtime->GetActiveScene()->GetEntityByUUID(s.ProbeActor))
                {
                    s.CheckpointQA = 3;
                    s.Selected = 0;
                    ZM_CORE_INFO("World checkpoint editor QA passed: adopted actor, atomic save, deletion and staged restore");
                }
            }
            const auto stats = live->GetStats();
            ImGui::Separator();
            ImGui::Text("ACTIVE %zu    LOADING %zu    BUILD %zu    FAILED %zu", stats.Active, stats.Loader.Queued + stats.Loader.Reading, stats.Building, stats.Failed);
            ImGui::Text("Cells %zu / %u   Entities %llu / %u   Budget-blocked %zu", stats.Admitted, live->GetPolicy().MaxResidentCells,
                        (unsigned long long)stats.Entities, live->GetPolicy().MaxResidentEntities, stats.BudgetBlocked);
            ImGui::Text("Declared CPU %.1f MiB   GPU %.1f MiB   Source %.1f MiB", stats.DeclaredCPU / 1048576.0, stats.DeclaredGPU / 1048576.0, stats.Loader.ReservedSourceBytes / 1048576.0);
            ImGui::Text("Update %.2f ms  peak %.2f ms   Activated %llu  Unloaded %llu", stats.LastMilliseconds, stats.PeakMilliseconds,
                        (unsigned long long)stats.Activations, (unsigned long long)stats.Unloads);
            ImGui::TextDisabled("Declared/source reservations are not measured RSS or VRAM.");
            ImGui::Text("Navigation: %zu tiles | %zu cells | %llu rebuilds", stats.NavigationTiles, stats.NavigationCells, (unsigned long long)stats.NavigationRebuilds);
            if (!stats.NavigationError.empty()) ImGui::TextWrapped("Navigation: %s", stats.NavigationError.c_str());
            if (ImGui::CollapsingHeader("Checkpoint / world state", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Text("Commands pending %zu / completed %llu / failed %llu", stats.PendingCommands, (unsigned long long)stats.CompletedCommands, (unsigned long long)stats.FailedCommands);
                ImGui::InputText("Checkpoint file", &s.CheckpointPath);
                bool capture = stats.CaptureOnUnload;
                if (ImGui::Checkbox("Unload holatini RAM da saqlash", &capture)) live->SetCaptureOnUnload(capture);
                ImGui::Text("Snapshots %zu | %.2f MiB | captures %llu | restores %llu | %s", stats.CheckpointCells, stats.CheckpointBytes / 1048576.0,
                            (unsigned long long)stats.Captures, (unsigned long long)stats.Restores, stats.RestorePending ? "RESTORING" : "ready");
                if (ImGui::Button("Checkpoint saqlash")) live->Enqueue({ WorldStreaming::CommandKind::Save, UUID(0), UUID(0), s.CheckpointPath }, s.Error);
                ImGui::SameLine();
                if (ImGui::Button("Diskdan tiklash")) live->Enqueue({ WorldStreaming::CommandKind::Restore, UUID(0), UUID(0), s.CheckpointPath }, s.Error);
                ImGui::SameLine();
                if (ImGui::Button("Authored world ga qaytish")) live->Enqueue({ WorldStreaming::CommandKind::Reset, UUID(0), UUID(0), {} }, s.Error);
                if (!stats.CheckpointError.empty()) ImGui::TextWrapped("Checkpoint: %s", stats.CheckpointError.c_str());
                ImGui::TextWrapped("Component snapshots: transforms, terrain and stored script fields. Runtime C++ heap / physics replay are separate. Disk IO only on explicit save/restore.");
            }
            const auto& memory = stats.Residency;
            s.RSSHistory[s.HistoryOffset] = float(memory.Process.Resident / 1048576.0);
            s.GPUHistory[s.HistoryOffset] = float(memory.RequestedGPUBytes / 1048576.0);
            s.HistoryOffset = (s.HistoryOffset + 1) % int(s.RSSHistory.size());
            ImGui::TextColored(stats.MemoryPressure ? ImVec4(1, .45f, .25f, 1) : ImVec4(.3f, .85f, .72f, 1),
                               "%s | RSS %s | GPU requested %s", stats.MemoryPressure ? "PRESSURE: new work paused" : "MEMORY: ready",
                               memory.Process.Valid ? MemoryStats::FormatBytes(memory.Process.Resident).c_str() : "unavailable",
                               MemoryStats::FormatBytes(memory.RequestedGPUBytes).c_str());
            ImGui::PlotLines("RSS MiB", s.RSSHistory.data(), int(s.RSSHistory.size()), s.HistoryOffset, nullptr, 0, FLT_MAX, ImVec2(0, 36));
            ImGui::PlotLines("GPU requested MiB", s.GPUHistory.data(), int(s.GPUHistory.size()), s.HistoryOffset, nullptr, 0, FLT_MAX, ImVec2(0, 36));
            ImGui::Text("Cache entries %zu | pinned %zu | loading %zu | evicted %llu | pressure frames %llu",
                        memory.Entries, memory.Pinned, memory.Loading, (unsigned long long)stats.CacheEvictions, (unsigned long long)stats.PressureFrames);
            if (ImGui::TreeNode("Cache residency details"))
            {
                for (size_t i = 0; i < memory.Caches.size(); ++i)
                    ImGui::Text("%s: %zu entries / %zu pinned / %zu loading", AssetResidency::Name(i), memory.Caches[i].Entries, memory.Caches[i].Pinned, memory.Caches[i].Loading);
                ImGui::TextWrapped("Live owners and async loads stay pinned. Limits pause new admissions; RSS may remain high after allocator frees. GPU requested is not physical VRAM.");
                ImGui::TreePop();
            }
            if (ImGui::Button("Bo'sh keshni tozalash")) live->TrimCaches();
            if (ImGui::Button("Xatolarni qayta urinish")) live->RetryFailures();
            ImGui::SameLine();
            if (ImGui::Button("Worldni yopish"))
            {
                runtime->ClearStreamingWorld();
                live = nullptr;
            }
            if (live)
            {
                if (ImGui::Checkbox("Test observer", &s.Observer))
                {
                    if (s.Observer) s.Position = live->GetViewer();
                    else live->ClearViewerOverride();
                }
                if (s.Observer && ImGui::InputScalarN("Observer XYZ", ImGuiDataType_Double, &s.Position.x, 3)) live->SetViewerOverride(s.Position);
            }
        }
        else ImGui::TextDisabled("Play: loyiha world'i avtomatik ishga tushadi. Kataklar anchor bilan bir fizikada.");
        if (!s.Error.empty()) ImGui::TextWrapped("Xato: %s", s.Error.c_str());
        if (!s.Loaded)
        {
            ImGui::End();
            return;
        }
        const auto& grid = live ? live->GetManifest() : s.Manifest;
        const auto cells = live ? live->GetCells() : std::vector<WorldStreaming::CellInfo>{};
        const int axis = (s.Policy.AxisMask & 2) ? 1 : 2;
        if (!grid.Cells.empty())
        {
            int minX = grid.Cells[0].Coordinate.x, maxX = minX, minY = grid.Cells[0].Coordinate[axis], maxY = minY;
            for (const auto& c : grid.Cells)
            {
                minX = std::min(minX, c.Coordinate.x);
                maxX = std::max(maxX, c.Coordinate.x);
                minY = std::min(minY, c.Coordinate[axis]);
                maxY = std::max(maxY, c.Coordinate[axis]);
            }
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const float width = ImGui::GetContentRegionAvail().x;
            const float size = std::max(1.f, std::min({ 55.f, width / (float(maxX) - minX + 2), 220.f / (float(maxY) - minY + 2) }));
            ImGui::InvisibleButton("##worldgrid", ImVec2(width, 235));
            auto* draw = ImGui::GetWindowDrawList();
            draw->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + 235), IM_COL32(21, 27, 37, 255), 8);
            for (size_t i = 0; i < grid.Cells.size(); ++i)
            {
                const auto& c = grid.Cells[i];
                ImVec2 p(pos.x + 16 + (float(c.Coordinate.x) - minX) * size, pos.y + 16 + (float(c.Coordinate[axis]) - minY) * size);
                const ImVec2 q(p.x + std::max(.5f, size - 3), p.y + std::max(.5f, size - 3));
                auto stage = i < cells.size() ? cells[i].Stage : WorldRuntimeStage::Dormant;
                draw->AddRectFilled(p, q, Color(stage), 4);
                if (int(i) == s.Selected) draw->AddRect(p, q, IM_COL32(235, 240, 255, 255), 4, 0, 2);
                if (size > 35) draw->AddText(ImVec2(p.x + 5, p.y + 4), IM_COL32_WHITE, std::to_string(i).c_str());
                if (ImGui::IsMouseHoveringRect(p, q))
                {
                    ImGui::SetTooltip("%s / (%d,%d,%d)\n%s\n%s", c.Layer.c_str(), c.Coordinate.x, c.Coordinate.y, c.Coordinate.z, c.ScenePath.c_str(), WorldStreaming::StageName(stage));
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) s.Selected = int(i);
                }
            }
            if (live)
            {
                const auto v = (live->GetViewer() - grid.Origin) / grid.CellSize;
                draw->AddCircleFilled(ImVec2(pos.x + 16 + (float(v.x) - minX) * size, pos.y + 16 + (float(v[axis]) - minY) * size), 5, IM_COL32(255, 245, 165, 255));
            }
        }
        ImGui::TextDisabled("Green: active   Blue: loading   Amber: build/resolve   Red: failed   Dot: viewer");
        if (ImGui::CollapsingHeader("Streaming siyosati"))
        {
            int axes = int(s.Policy.AxisMask);
            ImGui::RadioButton("3D XZ", &axes, 5);
            ImGui::SameLine();
            ImGui::RadioButton("2D XY", &axes, 3);
            s.Policy.AxisMask = uint32_t(axes);
            ImGui::InputDouble("Load radius", &s.Policy.LoadRadius);
            ImGui::InputDouble("Unload radius", &s.Policy.UnloadRadius);
            ImGui::InputDouble("Main-thread soft budget ms", &s.Policy.MainThreadMilliseconds);
            ImGui::InputScalar("Observed RSS limit bytes (0=off)", ImGuiDataType_U64, &s.Policy.ObservedRSSLimitBytes);
            ImGui::InputScalar("Requested GPU limit bytes (0=off)", ImGuiDataType_U64, &s.Policy.RequestedGPULimitBytes);
            ImGui::InputDouble("Pressure resume ratio", &s.Policy.PressureResumeRatio);
            int nav = int(s.Policy.NavigationMode);
            ImGui::RadioButton("Nav off", &nav, 0);
            ImGui::SameLine();
            ImGui::RadioButton("Nav 2D", &nav, 2);
            ImGui::SameLine();
            ImGui::RadioButton("Nav 3D", &nav, 3);
            s.Policy.NavigationMode = uint32_t(nav);
            ImGui::InputScalar("Navigation max cells", ImGuiDataType_U32, &s.Policy.NavigationMaxCells);
            ImGui::InputScalar("Cache entries per kind", ImGuiDataType_U32, &s.Policy.CacheEntriesPerKind);
            ImGui::InputScalar("Max cache evictions per frame", ImGuiDataType_U32, &s.Policy.MaxCacheEvictionsPerFrame);
            int limit = int(s.Policy.MaxResidentCells);
            if (ImGui::InputInt("Resident cell limit", &limit)) s.Policy.MaxResidentCells = uint32_t(std::clamp(limit, 1, 128));
            limit = int(s.Policy.MaxResidentEntities);
            if (ImGui::InputInt("Streamed entity limit", &limit)) s.Policy.MaxResidentEntities = uint32_t(std::clamp(limit, 1, 1000000));
            ImGui::InputScalar("Declared CPU bytes", ImGuiDataType_U64, &s.Policy.DeclaredCPUBytes);
            ImGui::InputScalar("Declared GPU bytes", ImGuiDataType_U64, &s.Policy.DeclaredGPUBytes);
            ImGui::InputScalarN("Origin", ImGuiDataType_Double, &s.Manifest.Origin.x, 3);
            ImGui::InputScalarN("Cell size", ImGuiDataType_Double, &s.Manifest.CellSize.x, 3);
            std::set<std::string> layers;
            for (const auto& c : s.Manifest.Cells) layers.insert(c.Layer);
            for (const auto& layer : layers)
            {
                bool enabled = std::find(s.Policy.Layers.begin(), s.Policy.Layers.end(), layer) != s.Policy.Layers.end();
                if (ImGui::Checkbox(layer.c_str(), &enabled))
                {
                    if (enabled) s.Policy.Layers.push_back(layer);
                    else std::erase(s.Policy.Layers, layer);
                }
            }
            ImGui::TextDisabled("Save/reload applies policy changes. Hard frame deadlines and floating origin are not enabled.");
        }
        if (ImGui::Button("Katak qo'shish"))
        {
            WorldCell c;
            c.ID = UUID();
            c.Coordinate.x = int(s.Manifest.Cells.size());
            c.ScenePath = "Scenes/Cell.zscene";
            s.Manifest.Cells.push_back(c);
            s.Selected = int(s.Manifest.Cells.size() - 1);
        }
        if (s.Selected >= 0 && size_t(s.Selected) < s.Manifest.Cells.size())
        {
            auto& c = s.Manifest.Cells[size_t(s.Selected)];
            ImGui::Text("Cell %s", c.ID.ToString().c_str());
            ImGui::InputText("Scene", &c.ScenePath);
            ImGui::InputText("Layer", &c.Layer);
            ImGui::InputInt3("Coordinate", &c.Coordinate.x);
            ImGui::InputScalar("CPU estimate", ImGuiDataType_U64, &c.DeclaredCPUBytes);
            ImGui::InputScalar("GPU estimate", ImGuiDataType_U64, &c.DeclaredGPUBytes);
            if (size_t(s.Selected) < cells.size() && !cells[size_t(s.Selected)].Error.empty()) ImGui::TextWrapped("Runtime: %s", cells[size_t(s.Selected)].Error.c_str());
            if (!(runtime && runtime->IsRunning()) && ImGui::Button("Katak sahnasini alohida ochish"))
                s_SceneRequest = Project::GetAssetFileSystemPath(c.ScenePath);
            if (live)
            {
                if (ImGui::Button("Shu katak snapshotini olish")) live->CaptureCell(c.ID, s.Error);
                ImGui::InputScalar("Adopt entity UUID (decimal)", ImGuiDataType_U64, &s.AdoptID);
                if (ImGui::Button("Entity subtree ni katakka biriktirish")) live->AdoptEntity(c.ID, UUID(s.AdoptID), s.Error);
                auto anchor = runtime->GetActiveScene();
                for (auto id : live->GetCellEntities(c.ID))
                {
                    auto entity = anchor->GetEntityByUUID(id);
                    if (!entity) continue;
                    ImGui::PushID(id.ToString().c_str());
                    if (ImGui::TreeNode(entity.GetName().c_str()))
                    {
                        ImGui::Text("UUID %s", id.ToString().c_str());
                        ImGui::DragFloat3("Local position", &entity.GetTransform().Translation.x, .1f);
                        if (ImGui::Button("Entity subtree ni o'chirish")) live->RemoveCellEntity(c.ID, id, s.Error);
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }
            }
            if (ImGui::Button("Katakni olib tashlash"))
            {
                s.Manifest.Cells.erase(s.Manifest.Cells.begin() + s.Selected);
                s.Selected = -1;
            }
        }
        ImGui::End();
    }
}
