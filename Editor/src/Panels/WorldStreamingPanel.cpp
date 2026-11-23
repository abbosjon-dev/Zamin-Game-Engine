#include "WorldStreamingPanel.h"
#include <Zamin/Scene/WorldStreaming.h>
#include <Zamin/Scene/WorldAuthoring.h>
#include <Zamin/Scene/Entity.h>
#include <Zamin/Core/SaveData.h>
#include <Zamin/Scene/SceneManager.h>
#include <Zamin/Project/Project.h>
#include <Zamin/Utils/FileSystem.h>
#include <Zamin/Editor/EditorLanguage.h>
#include <format>
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
            glm::vec2 RouteFrom{ 8, 9 }, RouteTo{ 48, 9 };
            NavRoute Route;
            bool RoutePreview = false, NavigationQA = false;
            UUID ProbeCell{ 0 }, ProbeActor{ 0 };
        } s;
        ImU32 Color(WorldRuntimeStage stage)
        {
            switch (stage)
            {
                case WorldRuntimeStage::Active:    return IM_COL32(45, 176, 132, 255);
                case WorldRuntimeStage::Failed:    return IM_COL32(215, 76, 89, 255);
                case WorldRuntimeStage::Building:
                case WorldRuntimeStage::Staging:
                case WorldRuntimeStage::Resolving: return IM_COL32(226, 171, 66, 255);
                case WorldRuntimeStage::Loading:   return IM_COL32(75, 128, 224, 255);
                default:                           return IM_COL32(56, 66, 83, 255);
            }
        }
        // A translated line with values: the English text keeps the same {} placeholders.
        template<class... Args>
        void Line(const char* uzbek, Args&&... args)
        {
            ImGui::TextUnformatted(std::vformat(Tr(uzbek), std::make_format_args(args...)).c_str());
        }
        double MiB(uint64_t bytes) { return double(bytes) / 1048576.0; }
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
                s.Error = Tr("Asset papkasi ichidagi .zworld yolini kiriting");
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
            ImGui::TextDisabled("%s", Tr("Avval loyihani oching."));
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
        ImGui::TextDisabled("%s", Tr("kamera kataklari - hayot sikli - rezidentlik qabuli"));
        ImGui::SetNextItemWidth(-170);
        ImGui::InputText("##worldpath", &s.Path);
        ImGui::SameLine();
        if (ImGui::Button(Tr("Ochish"))) Load();
        ImGui::SameLine();
        if (ImGui::Button(Tr("Yangi")))
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
        if (ImGui::CollapsingHeader(Tr("Yangi 2D / 3D world yaratish")))
        {
            ImGui::BeginDisabled(runtime && runtime->IsRunning());
            ImGui::InputText(Tr("Bundle papkasi (yangi)"), &s.Grid.Folder);
            ImGui::InputText(Tr("World nomi"), &s.Grid.Name);
            ImGui::InputText(Tr("Katak shablon sahnasi (ixtiyoriy)"), &s.TemplateScene);
            s.Grid.TemplateScene = s.TemplateScene;
            if (ImGui::Checkbox("3D world", &s.Grid.ThreeD)) s.Grid.CellSize = s.Grid.ThreeD ? glm::vec2(32, 32) : glm::vec2(32, 18);
            ImGui::InputScalar(Tr("Ustunlar"), ImGuiDataType_U32, &s.Grid.Columns);
            ImGui::InputScalar(Tr("Qatorlar"), ImGuiDataType_U32, &s.Grid.Rows);
            ImGui::InputFloat2(Tr("Katak o'lchamlari"), &s.Grid.CellSize.x);
            ImGui::Checkbox(Tr("Relyef"), &s.Grid.Terrain);
            ImGui::SameLine();
            ImGui::Checkbox(Tr("Navigatsiya"), &s.Grid.Navigation);
            ImGui::InputScalar(Tr("Relyef aniqligi (9/17/33/65)"), ImGuiDataType_U32, &s.Grid.TerrainResolution);
            ImGui::InputScalar(Tr("Relyef seed'i"), ImGuiDataType_U32, &s.Grid.Seed);
            ImGui::InputFloat(Tr("Nav katak o'lchami"), &s.Grid.NavigationCellSize);
            ImGui::TextWrapped("%s", Tr("CPU mualliflik: 256 tagacha katak. Yangi bundle'da kamera, relyef/zamin va navigatsiya yaratiladi. Mavjud fayllar himoyalangan."));
            if (ImGui::Button(Tr("World bundle yaratish")))
            {
                if (CreateWorldGrid(Project::GetAssetDirectory(), s.Grid, s.Created, s.Error))
                {
                    s.Path = s.Created.World.generic_string();
                    Load();
                }
            }
            if (!s.Created.World.empty())
            {
                ImGui::Text(Tr("Yaratildi: %zu katak / %zu fayl / %.2f MiB"), s.Created.Cells, s.Created.Files, s.Created.Bytes / 1048576.0);
                if (ImGui::Button(Tr("Loyihaga ulash va anchor ochish")))
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
                        s.Error = Tr("Loyiha saqlanmadi; bundle saqlanib qoladi");
                    }
                }
            }
            ImGui::EndDisabled();
        }

        if (s.Loaded)
        {
            if (ImGui::Button(Tr("Saqlash va loyihaga ulash")))
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
                        s.Error = Tr("Loyiha saqlanmadi");
                    }
                }
                else if (file.empty()) s.Error = Tr("World yoli asset papkasidan tashqarida");
            }
            if (runtime && runtime->IsRunning())
            {
                ImGui::SameLine();
                if (ImGui::Button(Tr("Runtime qayta ulash")))
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
            if (std::getenv("ZAMIN_EDITOR_WORLD_NAVIGATION_QA"))
            {
                auto route = live->FindNavigationRoute(s.RouteFrom, s.RouteTo);
                if (route.Status == NavPathStatus::Complete && route.Points.size() > 2)
                {
                    s.Route = std::move(route);
                    s.RoutePreview = true;
                    if (!s.NavigationQA) ZM_CORE_INFO("World navigation editor QA passed: sparse route preview, {} points", s.Route.Points.size());
                    s.NavigationQA = true;
                }
            }
            const auto stats = live->GetStats();
            ImGui::Separator();
            ImGui::Text(Tr("FAOL %zu    YUKLANMOQDA %zu    BUILD %zu    XATO %zu"), stats.Active, stats.Loader.Queued + stats.Loader.Reading, stats.Building, stats.Failed);
            ImGui::Text(Tr("Kataklar %zu / %u   Obyektlar %llu / %u   Byudjet to'sgan %zu"), stats.Admitted, live->GetPolicy().MaxResidentCells,
                        (unsigned long long)stats.Entities, live->GetPolicy().MaxResidentEntities, stats.BudgetBlocked);
            ImGui::Text(Tr("E'lon qilingan CPU %.1f MiB   GPU %.1f MiB   Manba %.1f MiB"), stats.DeclaredCPU / 1048576.0, stats.DeclaredGPU / 1048576.0, stats.Loader.ReservedSourceBytes / 1048576.0);
            ImGui::Text(Tr("Update %.2f ms  cho'qqi %.2f ms   Faollashdi %llu  Unload %llu"), stats.LastMilliseconds, stats.PeakMilliseconds,
                        (unsigned long long)stats.Activations, (unsigned long long)stats.Unloads);
            ImGui::TextDisabled("%s", Tr("E'lon qilingan/manba zaxiralari o'lchangan RSS yoki VRAM emas."));
            if (ImGui::CollapsingHeader(Tr("Asset oqimi va rezidentlik limitlari"), ImGuiTreeNodeFlags_DefaultOpen))
            {
                const auto& policy = live->GetPolicy();
                if (!stats.AssetStreaming)
                    ImGui::TextWrapped("%s", Tr("Asset byudjeti o'chiq: katak texture va modellarini asosiy oqim yuklaydi (siyosatda yoqiladi)."));
                else
                {
                    const size_t uploads = stats.PendingUploads;
                    const double pending = MiB(stats.PendingUploadBytes), reserved = MiB(stats.Assets.ReservedBytes), limit = MiB(policy.MaxStagedAssetBytes);
                    Line("Tayyorlash: {} ta fayl, {} ta o'qilmadi, {} ta bekor qilindi, {} ta byudjetdan oshdi", stats.Assets.FilesStaged, stats.Assets.FilesUnreadable,
                         stats.AssetCancellations, stats.Assets.BudgetFailures);
                    Line("Navbat: {} ta yuklash, {:.2f} MiB | band qilingan {:.1f} / {:.0f} MiB", uploads, pending, reserved, limit);
                    const double sent = MiB(stats.Uploads.UploadBytes), peak = MiB(stats.Uploads.PeakUpdateUploadBytes), budget = MiB(policy.MaxAssetUploadBytesPerFrame);
                    Line("GPU'ga yuborildi: {:.1f} MiB | kadr cho'qqisi {:.2f} / {:.2f} MiB | kechiktirish {} | byudjetdan katta {}", sent, peak, budget,
                         stats.Uploads.BudgetDeferrals, stats.Uploads.OversizeUploads);
                    const bool clean = stats.MainThreadImageDecodes == 0 && stats.MainThreadModelParses == 0;
                    ImGui::PushStyleColor(ImGuiCol_Text, clean ? ImVec4(.3f, .85f, .72f, 1) : ImVec4(1, .45f, .25f, 1));
                    Line("Asosiy oqimda: {} ta rasm decode, {} ta model parse", stats.MainThreadImageDecodes, stats.MainThreadModelParses);
                    ImGui::PopStyleColor();
                }
                Line("Limit: {} ta katak chiqarildi, {} ta ushlab turilibdi, {} ta rad etildi, xotira {} marta qaytarildi", stats.PressureEvictions, stats.ResidencyBlocked,
                     stats.ResidencyRefusals, stats.MemoryReleases);
                ImGui::TextDisabled("%s", Tr("Kuzatilgan RSS va so'ralgan GPU baytlari; jismoniy VRAM emas."));
            }
            ImGui::Text(Tr("Navigatsiya: %zu tile | %zu katak | %llu qayta qurish"), stats.NavigationTiles, stats.NavigationCells, (unsigned long long)stats.NavigationRebuilds);
            if (!stats.NavigationError.empty()) ImGui::TextWrapped(Tr("Navigatsiya: %s"), stats.NavigationError.c_str());
            if (ImGui::CollapsingHeader(Tr("Siyrak navigatsiya / yo'l"), ImGuiTreeNodeFlags_DefaultOpen))
            {
                const auto atlas = live->GetNavigationAtlas();
                const auto frame = runtime && runtime->GetActiveScene() ? runtime->GetActiveScene()->GetNavFrameStats() : Scene::NavFrameStats{};
                ImGui::Text(Tr("Crowd agentlari %u | tekshiruv %u | cheklangan %u | to'silgan %u | %.2f ms"), frame.CrowdAgents, frame.CrowdNeighborChecks, frame.CrowdLimited, frame.CrowdBlocked, frame.CrowdMilliseconds);
                uint32_t monitored = 0, retries = 0, exhausted = 0, waiting = 0;
                if (runtime && runtime->GetActiveScene() && live->IsAnchor(runtime->GetActiveScene().get()))
                {
                    auto& owner = *runtime->GetActiveScene();
                    for (auto handle : owner.GetAllEntitiesWith<NavAgentComponent>())
                    {
                        Entity actor(handle, &owner);
                        const auto& agent = actor.GetComponent<NavAgentComponent>();
                        if (!agent.WorldNavigation || !agent.RecoverySettings.Enabled || !owner.IsActiveInHierarchy(actor)) continue;
                        ++monitored;
                        retries += agent.Recovery.GetRetries();
                        exhausted += agent.Recovery.IsExhausted();
                        waiting += agent.Recovery.GetPhase() == NavRecovery::Phase::WaitingPath;
                    }
                }
                ImGui::Text(Tr("Tiklash: kuzatilmoqda %u | kutmoqda %u | tugagan %u | urinishlar %u"), monitored, waiting, exhausted, retries);
                const auto& queries = stats.NavigationQueries;
                ImGui::Text(Tr("Async so'rovlar: %s | navbatda %zu ishlamoqda %zu tayyor %zu"), live->GetPolicy().AsyncNavigation ? Tr("yoqilgan") : Tr("o'chiq"), queries.Queued, queries.Running, queries.Available);
                ImGui::Text(Tr("So'rovlar %llu | yetkazildi %llu | bekor %llu | rad %llu | eskirgan %llu"), (unsigned long long)queries.Submitted, (unsigned long long)queries.Delivered, (unsigned long long)queries.Cancelled, (unsigned long long)queries.Rejected, (unsigned long long)stats.NavigationStaleResults);
                ImGui::Text(Tr("Saqlangan snapshotlar %.2f MiB | natijalar %.2f MiB | worker jami %.2f ms"), queries.SnapshotBytes / 1048576.0, queries.ResultBytes / 1048576.0, queries.WorkerMilliseconds);
                ImGui::Text(Tr("Yuklangan kataklar %zu | %.2f MiB | faol linklar %zu"), stats.AtlasCells, stats.AtlasBytes / 1048576.0, stats.ActiveNavigationLinks);
                if (!stats.AtlasError.empty()) ImGui::TextWrapped("Atlas: %s", stats.AtlasError.c_str());
                ImGui::InputFloat2(Tr("Yo'l boshi (XY / XZ)"), &s.RouteFrom.x);
                ImGui::InputFloat2(Tr("Yo'l maqsadi (XY / XZ)"), &s.RouteTo.x);
                if (ImGui::Button(Tr("Siyrak yo'lni topish")))
                {
                    s.Route = live->FindNavigationRoute(s.RouteFrom, s.RouteTo);
                    s.RoutePreview = true;
                }
                ImGui::SameLine();
                if (ImGui::Button(Tr("Yo'lni tozalash"))) s.RoutePreview = false;
                if (s.RoutePreview)
                {
                    const bool stale = !atlas || s.Route.Revision != atlas->GetRevision();
                    ImGui::Text(Tr("%s | narx %.2f | %zu kengaytirildi | %zu nuqta %s"), NavPathStatusName(s.Route.Status), s.Route.Cost, s.Route.Expanded, s.Route.Points.size(), stale ? Tr("ESKIRGAN - qayta so'rang") : "");
                    const ImVec2 origin = ImGui::GetCursorScreenPos();
                    const ImVec2 size(std::max(200.f, ImGui::GetContentRegionAvail().x), 110);
                    auto* draw = ImGui::GetWindowDrawList();
                    draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(19, 25, 37, 255), 5);
                    if (s.Route.Points.size() > 1)
                    {
                        glm::vec2 lo = s.Route.Points.front().Position, hi = lo;
                        for (const auto& p : s.Route.Points)
                        {
                            lo = glm::min(lo, p.Position);
                            hi = glm::max(hi, p.Position);
                        }
                        auto project = [&](glm::vec2 p) { const auto uv = (p - lo) / glm::max(hi - lo, glm::vec2(1)); return ImVec2(origin.x + 12 + uv.x * (size.x - 24), origin.y + 12 + uv.y * (size.y - 24)); };
                        for (size_t i = 1; i < s.Route.Points.size(); ++i)
                        {
                            const auto a = project(s.Route.Points[i - 1].Position), b = project(s.Route.Points[i].Position);
                            draw->AddLine(a, b, stale ? IM_COL32(120, 120, 120, 255) : (s.Route.Points[i].IncomingLink ? IM_COL32(203, 114, 255, 255) : IM_COL32(65, 218, 175, 255)), 2);
                        }
                    }
                    ImGui::Dummy(size);
                    ImGui::TextDisabled("%s", Tr("Yashil: yurish; binafsha: aniq traversal. Ko'rinish to'qnashuv simulyatsiyasi emas."));
                }
                if (ImGui::Button(Tr("Muallif linklarini ishlayotgan worldga qo'llash"))) live->SetNavigationLinks(s.Policy.NavigationLinks, s.Error);
            }
            if (ImGui::CollapsingHeader(Tr("Checkpoint / world holati"), ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Text(Tr("Buyruqlar: kutmoqda %zu / bajarildi %llu / xato %llu"), stats.PendingCommands, (unsigned long long)stats.CompletedCommands, (unsigned long long)stats.FailedCommands);
                ImGui::InputText(Tr("Checkpoint fayli"), &s.CheckpointPath);
                bool capture = stats.CaptureOnUnload;
                if (ImGui::Checkbox(Tr("Unload holatini RAM da saqlash"), &capture)) live->SetCaptureOnUnload(capture);
                ImGui::Text(Tr("Snapshotlar %zu | %.2f MiB | capture %llu | tiklash %llu | %s"), stats.CheckpointCells, stats.CheckpointBytes / 1048576.0,
                            (unsigned long long)stats.Captures, (unsigned long long)stats.Restores, stats.RestorePending ? Tr("TIKLANMOQDA") : Tr("tayyor"));
                if (ImGui::Button(Tr("Checkpoint saqlash"))) live->Enqueue({ WorldStreaming::CommandKind::Save, UUID(0), UUID(0), s.CheckpointPath }, s.Error);
                ImGui::SameLine();
                if (ImGui::Button(Tr("Diskdan tiklash"))) live->Enqueue({ WorldStreaming::CommandKind::Restore, UUID(0), UUID(0), s.CheckpointPath }, s.Error);
                ImGui::SameLine();
                if (ImGui::Button(Tr("Authored world ga qaytish"))) live->Enqueue({ WorldStreaming::CommandKind::Reset, UUID(0), UUID(0), {} }, s.Error);
                if (!stats.CheckpointError.empty()) ImGui::TextWrapped("Checkpoint: %s", stats.CheckpointError.c_str());
                ImGui::TextWrapped("%s", Tr("Komponent snapshotlari: transformlar, relyef va saqlangan skript maydonlari. Runtime C++ heap / fizika replay alohida. Disk IO faqat aniq save/restore'da."));
            }
            const auto& memory = stats.Residency;
            s.RSSHistory[s.HistoryOffset] = float(memory.Process.Resident / 1048576.0);
            s.GPUHistory[s.HistoryOffset] = float(memory.RequestedGPUBytes / 1048576.0);
            s.HistoryOffset = (s.HistoryOffset + 1) % int(s.RSSHistory.size());
            ImGui::TextColored(stats.MemoryPressure ? ImVec4(1, .45f, .25f, 1) : ImVec4(.3f, .85f, .72f, 1),
                               Tr("%s | RSS %s | so'ralgan GPU %s"), stats.MemoryPressure ? Tr("BOSIM: yangi ish to'xtatildi") : Tr("XOTIRA: tayyor"),
                               memory.Process.Valid ? MemoryStats::FormatBytes(memory.Process.Resident).c_str() : Tr("mavjud emas"),
                               MemoryStats::FormatBytes(memory.RequestedGPUBytes).c_str());
            ImGui::PlotLines("RSS MiB", s.RSSHistory.data(), int(s.RSSHistory.size()), s.HistoryOffset, nullptr, 0, FLT_MAX, ImVec2(0, 36));
            ImGui::PlotLines(Tr("So'ralgan GPU MiB"), s.GPUHistory.data(), int(s.GPUHistory.size()), s.HistoryOffset, nullptr, 0, FLT_MAX, ImVec2(0, 36));
            ImGui::Text(Tr("Kesh yozuvlari %zu | pin %zu | yuklanmoqda %zu | chiqarildi %llu | bosim kadrlari %llu"),
                        memory.Entries, memory.Pinned, memory.Loading, (unsigned long long)stats.CacheEvictions, (unsigned long long)stats.PressureFrames);
            if (ImGui::TreeNode(Tr("Kesh rezidentligi tafsilotlari")))
            {
                for (size_t i = 0; i < memory.Caches.size(); ++i)
                    ImGui::Text(Tr("%s: %zu yozuv / %zu pin / %zu yuklanmoqda"), AssetResidency::Name(i), memory.Caches[i].Entries, memory.Caches[i].Pinned, memory.Caches[i].Loading);
                ImGui::TextWrapped("%s", Tr("Jonli egalar va async yuklashlar pin bo'lib turadi. Limitlar yangi qabulni to'xtatadi; allocator bo'shatgandan keyin ham RSS yuqori qolishi mumkin. So'ralgan GPU jismoniy VRAM emas."));
                ImGui::TreePop();
            }
            if (ImGui::Button(Tr("Bo'sh keshni tozalash"))) live->TrimCaches();
            if (ImGui::Button(Tr("Xatolarni qayta urinish"))) live->RetryFailures();
            ImGui::SameLine();
            if (ImGui::Button(Tr("Worldni yopish")))
            {
                runtime->ClearStreamingWorld();
                live = nullptr;
            }
            if (live)
            {
                if (ImGui::Checkbox(Tr("Sinov kuzatuvchisi"), &s.Observer))
                {
                    if (s.Observer) s.Position = live->GetViewer();
                    else live->ClearViewerOverride();
                }
                if (s.Observer && ImGui::InputScalarN(Tr("Kuzatuvchi XYZ"), ImGuiDataType_Double, &s.Position.x, 3)) live->SetViewerOverride(s.Position);
            }
        }
        else ImGui::TextDisabled("%s", Tr("Play: loyiha world'i avtomatik ishga tushadi. Kataklar anchor bilan bir fizikada."));
        if (!s.Error.empty()) ImGui::TextWrapped(Tr("Xato: %s"), s.Error.c_str());
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
                if (i < cells.size() && cells[i].Held) draw->AddRect(p, q, IM_COL32(255, 140, 70, 255), 4, 0, 2); // held out by a residency limit
                if (int(i) == s.Selected) draw->AddRect(p, q, IM_COL32(235, 240, 255, 255), 4, 0, 2);
                if (size > 35) draw->AddText(ImVec2(p.x + 5, p.y + 4), IM_COL32_WHITE, std::to_string(i).c_str());
                if (ImGui::IsMouseHoveringRect(p, q))
                {
                    const bool held = i < cells.size() && cells[i].Held;
                    ImGui::SetTooltip("%s / (%d,%d,%d)\n%s\n%s%s", c.Layer.c_str(), c.Coordinate.x, c.Coordinate.y, c.Coordinate.z, c.ScenePath.c_str(), WorldStreaming::StageName(stage),
                                      held ? Tr("\nRezidentlik limiti ushlab turibdi") : "");
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) s.Selected = int(i);
                }
            }
            if (live)
            {
                const auto v = (live->GetViewer() - grid.Origin) / grid.CellSize;
                draw->AddCircleFilled(ImVec2(pos.x + 16 + (float(v.x) - minX) * size, pos.y + 16 + (float(v[axis]) - minY) * size), 5, IM_COL32(255, 245, 165, 255));
            }
        }
        ImGui::TextDisabled("%s", Tr("Yashil: faol   Ko'k: yuklanmoqda   Sariq: build/resolve   Qizil: xato   Nuqta: kuzatuvchi"));
        if (ImGui::CollapsingHeader(Tr("Streaming siyosati")))
        {
            int axes = int(s.Policy.AxisMask);
            ImGui::RadioButton("3D XZ", &axes, 5);
            ImGui::SameLine();
            ImGui::RadioButton("2D XY", &axes, 3);
            s.Policy.AxisMask = uint32_t(axes);
            ImGui::InputDouble(Tr("Yuklash radiusi"), &s.Policy.LoadRadius);
            ImGui::InputDouble(Tr("Unload radiusi"), &s.Policy.UnloadRadius);
            ImGui::InputDouble(Tr("Asosiy oqim yumshoq byudjeti ms"), &s.Policy.MainThreadMilliseconds);
            ImGui::InputScalar(Tr("Kuzatilgan RSS limiti, bayt (0=o'chiq)"), ImGuiDataType_U64, &s.Policy.ObservedRSSLimitBytes);
            ImGui::InputScalar(Tr("So'ralgan GPU limiti, bayt (0=o'chiq)"), ImGuiDataType_U64, &s.Policy.RequestedGPULimitBytes);
            ImGui::InputDouble(Tr("Bosimdan qaytish nisbati"), &s.Policy.PressureResumeRatio);
            int nav = int(s.Policy.NavigationMode);
            ImGui::RadioButton(Tr("Nav o'chiq"), &nav, 0);
            ImGui::SameLine();
            ImGui::RadioButton("Nav 2D", &nav, 2);
            ImGui::SameLine();
            ImGui::RadioButton("Nav 3D", &nav, 3);
            s.Policy.NavigationMode = uint32_t(nav);
            ImGui::InputScalar(Tr("Navigatsiya maksimal kataklari"), ImGuiDataType_U32, &s.Policy.NavigationMaxCells);
            ImGui::Checkbox(Tr("Async navigatsiya workerlari"), &s.Policy.AsyncNavigation);
            ImGui::InputScalar(Tr("Navigatsiya workerlari (1..4)"), ImGuiDataType_U32, &s.Policy.NavigationQueryWorkers);
            ImGui::InputScalar(Tr("Navigatsiya navbati sig'imi"), ImGuiDataType_U32, &s.Policy.NavigationQueryCapacity);
            ImGui::InputScalar(Tr("Tick'dagi navigatsiya startlari"), ImGuiDataType_U32, &s.Policy.NavigationQueryStarts);
            ImGui::InputScalar(Tr("Tick'dagi navigatsiya natijalari"), ImGuiDataType_U32, &s.Policy.NavigationQueryResults);
            if (ImGui::TreeNode(Tr("Traversal linklari (.zworld da saqlanadi)")))
            {
                for (size_t i = 0; i < s.Policy.NavigationLinks.size(); ++i)
                {
                    auto& link = s.Policy.NavigationLinks[i];
                    ImGui::PushID(int(i));
                    ImGui::Text("Link %llu", (unsigned long long)link.ID);
                    ImGui::InputFloat2(Tr("Qayerdan"), &link.From.x);
                    ImGui::InputFloat2(Tr("Qayerga"), &link.To.x);
                    ImGui::InputFloat(Tr("Traversal narxi"), &link.Cost);
                    ImGui::Checkbox(Tr("Ikki yo'nalishli"), &link.Bidirectional);
                    ImGui::SameLine();
                    ImGui::Checkbox(Tr("Yoqilgan"), &link.Enabled);
                    const bool remove = ImGui::Button(Tr("Linkni o'chirish"));
                    ImGui::PopID();
                    if (remove)
                    {
                        s.Policy.NavigationLinks.erase(s.Policy.NavigationLinks.begin() + i);
                        break;
                    }
                }
                if (s.Policy.NavigationLinks.size() < 4096 && ImGui::Button(Tr("Traversal link qo'shish")))
                    s.Policy.NavigationLinks.push_back({ uint64_t(UUID()), s.RouteFrom, s.RouteTo, 1, true, true });
                ImGui::TextWrapped("%s", Tr("Faqat yuklangan, yurish mumkin bo'lgan uchlar linkni faollashtiradi. Gameplay traversalni aniq yakunlashi kerak."));
                ImGui::TreePop();
            }
            ImGui::Checkbox(Tr("Limitdan oshganda uzoq kataklarni chiqarish"), &s.Policy.EvictOnPressure);
            ImGui::InputScalar(Tr("Chiqarilgan katak kutadigan kadrlar"), ImGuiDataType_U32, &s.Policy.PressureCooldownFrames);
            if (ImGui::TreeNode(Tr("Asset byudjeti (texture / model)")))
            {
                ImGui::Checkbox(Tr("Assetlarni fonda tayyorlash va byudjet bilan yuklash"), &s.Policy.AssetStreaming);
                ImGui::InputScalar(Tr("Kadrdagi yuklashlar soni"), ImGuiDataType_U32, &s.Policy.MaxAssetUploadsPerFrame);
                ImGui::InputScalar(Tr("Kadrdagi yuklash baytlari"), ImGuiDataType_U64, &s.Policy.MaxAssetUploadBytesPerFrame);
                ImGui::InputScalar(Tr("Kadrda bo'shatiladigan GPU baytlari"), ImGuiDataType_U64, &s.Policy.MaxAssetReleaseBytesPerFrame);
                ImGui::InputScalar(Tr("Katakdagi fayllar soni"), ImGuiDataType_U32, &s.Policy.MaxAssetFilesPerCell);
                ImGui::InputScalar(Tr("Bitta fayl baytlari"), ImGuiDataType_U64, &s.Policy.MaxAssetFileBytes);
                ImGui::InputScalar(Tr("Katakning tayyorlangan baytlari"), ImGuiDataType_U64, &s.Policy.MaxAssetBytesPerCell);
                ImGui::InputScalar(Tr("Jami tayyorlangan baytlar"), ImGuiDataType_U64, &s.Policy.MaxStagedAssetBytes);
                ImGui::TextWrapped("%s", Tr("Byudjetdan oshgan katak fayl nomi bilan xato beradi; yo'q yoki buzilgan fayl xabar qilinadi va katak usiz yuklanadi."));
                ImGui::TreePop();
            }
            ImGui::InputScalar(Tr("Har turdagi kesh yozuvlari"), ImGuiDataType_U32, &s.Policy.CacheEntriesPerKind);
            ImGui::InputScalar(Tr("Kadrdagi maksimal kesh chiqarishlari"), ImGuiDataType_U32, &s.Policy.MaxCacheEvictionsPerFrame);
            int limit = int(s.Policy.MaxResidentCells);
            if (ImGui::InputInt(Tr("Rezident kataklar limiti"), &limit)) s.Policy.MaxResidentCells = uint32_t(std::clamp(limit, 1, 128));
            limit = int(s.Policy.MaxResidentEntities);
            if (ImGui::InputInt(Tr("Oqimlangan obyektlar limiti"), &limit)) s.Policy.MaxResidentEntities = uint32_t(std::clamp(limit, 1, 1000000));
            ImGui::InputScalar(Tr("E'lon qilingan CPU baytlari"), ImGuiDataType_U64, &s.Policy.DeclaredCPUBytes);
            ImGui::InputScalar(Tr("E'lon qilingan GPU baytlari"), ImGuiDataType_U64, &s.Policy.DeclaredGPUBytes);
            ImGui::InputScalarN(Tr("Boshlanish nuqtasi"), ImGuiDataType_Double, &s.Manifest.Origin.x, 3);
            ImGui::InputScalarN(Tr("Katak o'lchami"), ImGuiDataType_Double, &s.Manifest.CellSize.x, 3);
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
            ImGui::TextDisabled("%s", Tr("Save/reload siyosat o'zgarishlarini qo'llaydi. Butun kadr uchun qat'iy deadline va floating origin yoqilmagan."));
        }
        if (ImGui::Button(Tr("Katak qo'shish")))
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
            ImGui::Text(Tr("Katak %s"), c.ID.ToString().c_str());
            ImGui::InputText(Tr("Sahna"), &c.ScenePath);
            ImGui::InputText(Tr("Qatlam"), &c.Layer);
            ImGui::InputInt3(Tr("Koordinata"), &c.Coordinate.x);
            ImGui::InputScalar(Tr("CPU bahosi"), ImGuiDataType_U64, &c.DeclaredCPUBytes);
            ImGui::InputScalar(Tr("GPU bahosi"), ImGuiDataType_U64, &c.DeclaredGPUBytes);
            if (size_t(s.Selected) < cells.size() && !cells[size_t(s.Selected)].Error.empty()) ImGui::TextWrapped("Runtime: %s", cells[size_t(s.Selected)].Error.c_str());
            if (!(runtime && runtime->IsRunning()) && ImGui::Button(Tr("Katak sahnasini alohida ochish")))
                s_SceneRequest = Project::GetAssetFileSystemPath(c.ScenePath);
            if (live)
            {
                if (ImGui::Button(Tr("Shu katak snapshotini olish"))) live->CaptureCell(c.ID, s.Error);
                ImGui::InputScalar(Tr("Biriktiriladigan obyekt UUID'i (o'nlik)"), ImGuiDataType_U64, &s.AdoptID);
                if (ImGui::Button(Tr("Entity subtree ni katakka biriktirish"))) live->AdoptEntity(c.ID, UUID(s.AdoptID), s.Error);
                auto anchor = runtime->GetActiveScene();
                for (auto id : live->GetCellEntities(c.ID))
                {
                    auto entity = anchor->GetEntityByUUID(id);
                    if (!entity) continue;
                    ImGui::PushID(id.ToString().c_str());
                    if (ImGui::TreeNode(entity.GetName().c_str()))
                    {
                        ImGui::Text("UUID %s", id.ToString().c_str());
                        ImGui::DragFloat3(Tr("Lokal pozitsiya"), &entity.GetTransform().Translation.x, .1f);
                        if (ImGui::Button(Tr("Entity subtree ni o'chirish"))) live->RemoveCellEntity(c.ID, id, s.Error);
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }
            }
            if (ImGui::Button(Tr("Katakni olib tashlash")))
            {
                s.Manifest.Cells.erase(s.Manifest.Cells.begin() + s.Selected);
                s.Selected = -1;
            }
        }
        ImGui::End();
    }
}
