// Prefab mode (EditorLayer): a prefab opened on its own and edited as a scene of its own. The scene
// that was open (its history, selection, camera, unsaved state) waits on a stack; a prefab nested in
// the open one opens a level deeper. The prefab's entities keep their IDs in the file (UUID = ID), so
// saving writes the same file back with only what changed. Leaving brings the instances in the scene
// below up to date - one undo step there that also carries the prefab file writes. While a prefab is
// open there is no play mode, no autosave and no scene file (the untitled-scene state plus guards).
#include "EditorLayer.h"
#include "UI.h"

#include <imgui.h>

#include <format>

namespace fs = std::filesystem;

namespace Zamin {

    namespace {
        const ImVec4 s_StageBlue(0.365f, 0.663f, 1.0f, 1.0f); // the prefab colour (#5DA9FF)

        std::string FileName(const std::string& path) { return fs::path(path).filename().string(); }

        // A level saved several times: per file, its text before the first save and after the last.
        std::vector<FileEdit> MergeEdits(const std::vector<FileEdit>& edits)
        {
            std::vector<FileEdit> merged;
            for (const FileEdit& edit : edits)
            {
                auto it = std::find_if(merged.begin(), merged.end(), [&](const FileEdit& m) { return m.Path == edit.Path; });
                if (it == merged.end())
                    merged.push_back(edit);
                else
                    it->After = edit.After;
            }
            std::erase_if(merged, [](const FileEdit& e) { return e.Before && *e.Before == e.After; });
            return merged;
        }

        // A prefab of UI / sprites is edited in the 2D view, one with meshes in 3D.
        bool LooksLike2D(Scene& scene)
        {
            bool flat = false;
            for (Entity e : scene.GetEntitiesInOrder())
            {
                if (e.HasComponent<MeshRendererComponent>() || e.HasComponent<Rigidbody3DComponent>())
                    return false;
                flat |= e.HasComponent<UITransformComponent>() || e.HasComponent<SpriteRendererComponent>() ||
                        e.HasComponent<CircleRendererComponent>() || e.HasComponent<TilemapComponent>() ||
                        e.HasComponent<Rigidbody2DComponent>();
            }
            return flat;
        }

        bool ReadFileState(const fs::path& file, uint64_t& hash, fs::file_time_type& time)
        {
            std::error_code ec;
            if (!fs::is_regular_file(file, ec))
                return false;
            time = fs::last_write_time(file, ec);
            hash = PrefabDatabase::HashText(FileSystem::ReadTextFile(file));
            return true;
        }
    }

    void EditorLayer::RequestPrefabStage(const std::string& prefab, uint64_t focus, bool recover)
    {
        m_PrefabStageOpen.push_back({ prefab, focus, recover });
    }

    bool EditorLayer::OpenPrefabStage(const std::string& prefabPath, uint64_t focus, bool recover)
    {
        if (!Project::GetActive() || !m_EditorScene)
            return false;
        if (m_SceneState != SceneState::Edit)
        {
            ShowViewportMessage(Tr(ICON_FA_BOX "  Prefab faqat tahrirlash rejimida ochiladi (avval o'yinni to'xtating)"));
            return false;
        }
        std::string prefab = PrefabDatabase::NormalizePath(prefabPath);
        if (fs::path(prefab).is_absolute())
            prefab = PrefabDatabase::NormalizePath(Project::GetAssetRelativePath(prefab));
        if (prefab.empty() || fs::path(prefab).extension() != ".zprefab")
            return false;
        for (const PrefabStageLevel& level : m_PrefabStages)
            if (level.Prefab == prefab)
            {
                ShowViewportMessage(std::format(ICON_FA_BOX "  {}: {}", Tr("Bu prefab allaqachon ochiq"), FileName(prefab)));
                return false;
            }
        CommitUndo(); // what is pending is a step of the scene below

        Ref<Scene> stage = CreateRef<Scene>();
        SceneLoadReport report;
        Entity root = PrefabUtility::LoadStage(*stage, m_PrefabDatabase, prefab, &report, recover);
        report.File = Project::GetAssetFileSystemPath(prefab).string();
        if (!root)
        {
            ShowSceneLoadReport(report);
            const SceneLoadIssue* main = report.GetMain();
            if (main && main->Type == SceneLoadIssue::Kind::ParseError && !recover)
                m_PrefabStageRecoverPrompt = prefab; // the file is broken: recover what parses?
            ZM_WARN("Prefab ochilmadi: {} ({})", prefab, report.Summary());
            return false;
        }
        if (report.HasIssues())
            ShowSceneLoadReport(report);
        stage->ResolveAssets();

        // What was open waits below.
        PrefabStageLevel level;
        level.Prefab = prefab;
        level.OuterScene = m_EditorScene;
        level.OuterPath = m_EditorScenePath;
        level.OuterFileVersion = m_EditorSceneFileVersion;
        std::swap(level.OuterHistory, m_UndoHistory);
        m_UndoHistory = UndoHistory(100, 300);
        level.OuterAutosaved = m_AutosavedScene;
        level.OuterSelection = CaptureUndoSelection();
        level.OuterCamera = m_EditorCamera.GetViewState();
        level.Outer2D = m_2DMode;
        level.OuterDirty = m_SceneDirty;
        ReadFileState(Project::GetAssetFileSystemPath(prefab), level.FileHash, level.FileTime);
        level.NextCheck = ImGui::GetTime() + 1.0;
        m_PrefabStages.push_back(std::move(level));

        m_EditorScene = stage;
        m_SceneHierarchyPanel.SetContext({});
        SetActiveScene(m_EditorScene);
        m_SceneHierarchyPanel.SetPrefabStageMode(true);
        m_EditorScenePath.clear(); // no scene file: the prefab's is written by SavePrefabStage
        m_EditorSceneFileVersion = SceneMigration::CurrentVersion;
        m_SceneDirty = recover; // a recovered prefab is not what its file holds
        m_AssetUsage.MarkDirty();
        // Entered from an instance: the same entity of the prefab is selected.
        Entity selected = focus ? m_EditorScene->GetEntityByUUID(UUID(focus)) : Entity();
        m_SceneHierarchyPanel.SetSelectedEntity(selected ? selected : root);
        ResetUndoHistory(std::format("Prefab ochildi: {}", FileName(prefab)), !recover);
        Set2DMode(LooksLike2D(*m_EditorScene));
        FrameAll();
        ZM_INFO("Prefab rejimi: {}", prefab);
        UpdateWindowTitle();
        return true;
    }

    bool EditorLayer::SavePrefabStage()
    {
        if (!IsInPrefabStage() || m_SceneState != SceneState::Edit)
            return false;
        CommitUndo(); // the file gets the history's current state
        PrefabStageLevel& level = m_PrefabStages.back();
        std::vector<FileEdit> edits;
        std::string error;
        if (!PrefabUtility::SaveStage(*m_EditorScene, m_PrefabDatabase, level.Prefab, &edits, &error))
        {
            ZM_WARN("Prefab saqlanmadi ({}): {}", level.Prefab, error);
            ShowViewportMessage(std::format(ICON_FA_TRIANGLE_EXCLAMATION "  {}: {}", Tr("Saqlanmadi"), error));
            if (error.find("bitta ildiz") != std::string::npos)
                m_PrefabStageWrapPrompt = true; // offered: the roots under a new one
            return false;
        }
        // The nested instances' lists as written (SaveStage records them): the current state holds them.
        if (std::string snapshot = TakeSnapshot(); !m_UndoHistory.GetCurrent().Scene || *m_UndoHistory.GetCurrent().Scene != snapshot)
            m_UndoHistory.ReplaceCurrentScene(std::move(snapshot));
        m_UndoHistory.MarkSaved();
        m_AutosavedScene = m_UndoHistory.GetCurrent().Scene;
        m_SceneDirty = false;
        level.ExternalChange = false;
        ReadFileState(Project::GetAssetFileSystemPath(level.Prefab), level.FileHash, level.FileTime);
        for (const FileEdit& edit : edits)
        {
            ZM_INFO("Prefab saqlandi: {}", edit.Path);
            m_ContentBrowserPanel.InvalidateThumbnail(Project::GetAssetFileSystemPath(edit.Path));
        }
        level.Files.insert(level.Files.end(), edits.begin(), edits.end());
        m_ContentBrowserPanel.Refresh();
        ShowViewportMessage(edits.empty() ? std::format(ICON_FA_FLOPPY_DISK "  {}: {}", Tr("O'zgarish yo'q"), FileName(level.Prefab))
                            : edits.size() > 1 ? std::format(ICON_FA_FLOPPY_DISK "  {}: {} (+{} {})", Tr("Prefab saqlandi"), FileName(level.Prefab),
                                                             edits.size() - 1, Tr("ta bog'liq prefab yangilandi"))
                            : std::format(ICON_FA_FLOPPY_DISK "  {}: {}", Tr("Prefab saqlandi"), FileName(level.Prefab)));
        UpdateWindowTitle();
        return true;
    }

    void EditorLayer::RequestLeavePrefabStages(size_t keep, std::function<void()> then)
    {
        if (keep >= m_PrefabStages.size())
        {
            if (then)
                then();
            return;
        }
        m_PrefabStageLeave.Active = true;
        m_PrefabStageLeave.Keep = keep;
        m_PrefabStageLeave.Asking = false;
        m_PrefabStageLeave.Then = std::move(then);
    }

    void EditorLayer::PopPrefabStage()
    {
        if (!IsInPrefabStage())
            return;
        CommitUndo();
        PrefabStageLevel level = std::move(m_PrefabStages.back());
        m_PrefabStages.pop_back();
        const std::string name = FileName(level.Prefab);

        m_EditorScene = level.OuterScene;
        m_SceneHierarchyPanel.SetContext({});
        SetActiveScene(m_EditorScene);
        m_SceneHierarchyPanel.SetPrefabStageMode(IsInPrefabStage());
        m_EditorScenePath = level.OuterPath;
        m_EditorSceneFileVersion = level.OuterFileVersion;
        std::swap(m_UndoHistory, level.OuterHistory);
        m_AutosavedScene = level.OuterAutosaved;
        m_SceneDirty = level.OuterDirty;
        m_PendingUndo = false;
        m_PendingUndoName.clear();
        ApplyUndoSelection(level.OuterSelection);
        Set2DMode(level.Outer2D);
        m_EditorCamera.SetViewState(level.OuterCamera);
        m_AssetUsage.MarkDirty();
        m_SceneStatsDirty = true;

        // The instances below follow what was saved there (and in the prefabs made of it).
        std::vector<FileEdit> files = MergeEdits(level.Files);
        if (!files.empty())
        {
            SyncPrefabInstances(true);
            // One step that also puts the files back when it is undone.
            CommitPrefabStep(std::format("Prefab tahrirlandi: {}", fs::path(level.Prefab).stem().string()), std::move(files));
            // An outer prefab mode level whose file the save rewrote (a prefab nested in it changed):
            // what it shows is its file again.
            if (IsInPrefabStage() && !level.OuterDirty)
            {
                PrefabStageLevel& outer = m_PrefabStages.back();
                m_UndoHistory.MarkSaved();
                m_SceneDirty = false;
                ReadFileState(Project::GetAssetFileSystemPath(outer.Prefab), outer.FileHash, outer.FileTime);
            }
        }
        else if (SyncPrefabInstances())
        {
            m_UndoHistory.ReplaceCurrentScene(TakeSnapshot());
            m_SceneDirty = !m_UndoHistory.IsAtSavedState();
        }
        ZM_INFO("Prefab rejimi yopildi: {}", name);
        UpdateWindowTitle();
    }

    void EditorLayer::DropPrefabStages()
    {
        if (!IsInPrefabStage())
            return;
        m_PrefabStageLeave = {};
        m_PrefabStageOpen.clear();
        while (IsInPrefabStage())
        {
            if (m_SceneDirty)
                ZM_WARN("Prefab rejimi yopildi, saqlanmagan o'zgarishlar tashlab ketildi: {}", m_PrefabStages.back().Prefab);
            m_SceneDirty = false;
            PopPrefabStage();
        }
    }

    void EditorLayer::ProcessPrefabStageRequests()
    {
        // ---- prefabs asked to open (from the panels, last frame)
        if (!m_PrefabStageOpen.empty() && !m_PrefabStageLeave.Active)
        {
            std::vector<PrefabStageOpenRequest> requests = std::move(m_PrefabStageOpen);
            m_PrefabStageOpen.clear();
            for (const PrefabStageOpenRequest& request : requests)
                OpenPrefabStage(request.Prefab, request.Focus, request.Recover);
        }
        // ---- leaving levels: the clean ones close, a dirty one asks first (the popup answers)
        while (m_PrefabStageLeave.Active && !m_PrefabStageLeave.Asking)
        {
            if (m_PrefabStages.size() <= m_PrefabStageLeave.Keep || m_SceneState != SceneState::Edit)
            {
                std::function<void()> then = std::move(m_PrefabStageLeave.Then);
                m_PrefabStageLeave = {};
                if (then)
                    then();
                break;
            }
            CommitUndo();
            if (m_SceneDirty)
            {
                m_PrefabStageLeave.Asking = true;
                break;
            }
            PopPrefabStage();
        }
        CheckPrefabStageFile();
    }

    void EditorLayer::CheckPrefabStageFile()
    {
        if (!IsInPrefabStage() || m_SceneState != SceneState::Edit)
            return;
        PrefabStageLevel& level = m_PrefabStages.back();
        const double now = ImGui::GetTime();
        if (now < level.NextCheck)
            return;
        level.NextCheck = now + 1.0;
        const fs::path file = Project::GetAssetFileSystemPath(level.Prefab);
        std::error_code ec;
        if (!fs::is_regular_file(file, ec))
            return; // removed: what is open can still be saved (it is written again)
        if (fs::last_write_time(file, ec) == level.FileTime)
            return;
        uint64_t hash = 0;
        fs::file_time_type time;
        ReadFileState(file, hash, time);
        level.FileTime = time;
        if (hash == level.FileHash || m_PrefabDatabase.IsOwnWrite(file, hash))
        {
            level.FileHash = hash;
            return;
        }
        if (m_SceneDirty)
        {
            level.ExternalChange = true; // the banner: read it again, or keep the edits
            return;
        }
        ZM_INFO("Prefab fayli tashqarida o'zgardi, qayta o'qildi: {}", level.Prefab);
        ReloadPrefabStage();
    }

    void EditorLayer::ReloadPrefabStage()
    {
        if (!IsInPrefabStage())
            return;
        PrefabStageLevel& level = m_PrefabStages.back();
        m_PrefabDatabase.Invalidate(level.Prefab);
        Ref<Scene> stage = CreateRef<Scene>();
        SceneLoadReport report;
        Entity root = PrefabUtility::LoadStage(*stage, m_PrefabDatabase, level.Prefab, &report);
        report.File = Project::GetAssetFileSystemPath(level.Prefab).string();
        if (!root)
        {
            ShowSceneLoadReport(report);
            return; // a file half written: tried again after the next change
        }
        stage->ResolveAssets();
        // The IDs are the file's: the same entities stay selected.
        const UndoSelection selection = CaptureUndoSelection();
        m_EditorScene = stage;
        SetActiveScene(m_EditorScene);
        ApplyUndoSelection(selection);
        m_SceneDirty = false;
        level.ExternalChange = false;
        ReadFileState(Project::GetAssetFileSystemPath(level.Prefab), level.FileHash, level.FileTime);
        ResetUndoHistory(std::format("Prefab qayta o'qildi: {}", FileName(level.Prefab)), true);
        ShowViewportMessage(std::format(ICON_FA_ROTATE "  {}: {}", Tr("Prefab fayli o'zgardi, qayta o'qildi"), FileName(level.Prefab)));
        UpdateWindowTitle();
    }

    std::string EditorLayer::GetPrefabStageTitle() const
    {
        if (!IsInPrefabStage())
            return {};
        return std::format("{} (Prefab){}", FileName(m_PrefabStages.back().Prefab), m_SceneDirty ? " *" : "");
    }

    float EditorLayer::GetPrefabStageBarHeight() const
    {
        if (!IsInPrefabStage())
            return 0.0f;
        const float row = ImGui::GetFrameHeight() + 10.0f;
        return m_PrefabStages.back().ExternalChange ? row * 2.0f : row;
    }

    void EditorLayer::UI_PrefabStageBar(ImVec2 at, float width)
    {
        if (!IsInPrefabStage())
            return;
        const ImGuiStyle& style = ImGui::GetStyle();
        const float height = GetPrefabStageBarHeight();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(at, ImVec2(at.x + width, at.y + height), IM_COL32(24, 38, 58, 255));
        dl->AddLine(ImVec2(at.x, at.y + height - 1.0f), ImVec2(at.x + width, at.y + height - 1.0f), ImGui::GetColorU32(s_StageBlue), 2.0f);
        ImGui::SetCursorScreenPos(ImVec2(at.x + 8.0f, at.y + 5.0f));
        ImGui::PushID("##prefabStageBar");
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.45f, 0.65f, 0.55f));
        // ◀ Sahna "Level1" › Enemy.zprefab › Weapon.zprefab: a level clicked is gone back to.
        std::optional<size_t> leaveTo;
        const PrefabStageLevel& first = m_PrefabStages.front();
        const std::string sceneName = first.OuterScene ? first.OuterScene->GetName() : std::string();
        const std::string sceneLabel = std::format(ICON_FA_CHEVRON_LEFT "  {} \"{}\"###stageScene", Tr("Sahna"), sceneName);
        if (ImGui::Button(sceneLabel.c_str()))
            leaveTo = 0;
        if (ImGui::IsItemHovered())
            UI::Tooltip(Tr("Sahnaga qaytish (saqlanmagan o'zgarishlar bo'lsa, so'raladi)"));
        for (size_t i = 0; i < m_PrefabStages.size(); i++)
        {
            ImGui::SameLine(0.0f, 2.0f);
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled(ICON_FA_CHEVRON_RIGHT);
            ImGui::SameLine(0.0f, 2.0f);
            const bool current = i + 1 == m_PrefabStages.size();
            // A level below the open one keeps its unsaved state on the next level's stack entry.
            const bool dirty = current ? m_SceneDirty : m_PrefabStages[i + 1].OuterDirty;
            const std::string label = std::format(ICON_FA_BOX "  {}{}###stage{}", FileName(m_PrefabStages[i].Prefab), dirty ? " *" : "", i);
            ImGui::PushStyleColor(ImGuiCol_Text, current ? s_StageBlue : ImVec4(0.70f, 0.80f, 0.92f, 1.0f));
            if (current)
                ImGui::PushFont(ImGuiLayer::GetBoldFont());
            if (ImGui::Button(label.c_str()) && !current)
                leaveTo = i + 1;
            if (current)
                ImGui::PopFont();
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                UI::Tooltip(m_PrefabStages[i].Prefab.c_str());
        }
        ImGui::PopStyleColor(2);
        // [Saqlash] [Yopish] on the right.
        const char* saveLabel = Tr(ICON_FA_FLOPPY_DISK "  Saqlash");
        const char* closeLabel = Tr(ICON_FA_XMARK "  Yopish");
        const float buttons = ImGui::CalcTextSize(saveLabel).x + ImGui::CalcTextSize(closeLabel).x + style.FramePadding.x * 4.0f + style.ItemSpacing.x;
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 12.0f, at.x - ImGui::GetWindowPos().x + width - buttons - 10.0f));
        ImGui::BeginDisabled(!m_SceneDirty);
        ImGui::PushStyleColor(ImGuiCol_Button, UI::AccentDarkColor(1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, UI::AccentMidColor());
        if (ImGui::Button(saveLabel))
            SavePrefabStage();
        ImGui::PopStyleColor(2);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            UI::Tooltip(Tr("Prefabni saqlash (Ctrl+S): uning nusxalari va undan yasalgan prefablar yangilanadi"));
        ImGui::SameLine();
        if (ImGui::Button(closeLabel))
            leaveTo = m_PrefabStages.size() - 1;
        if (ImGui::IsItemHovered())
            UI::Tooltip(Tr("Prefab rejimini yopish (bir daraja orqaga)"));

        // Changed outside while there are unsaved edits here.
        if (m_PrefabStages.back().ExternalChange)
        {
            ImGui::SetCursorScreenPos(ImVec2(at.x + 10.0f, at.y + height * 0.5f + 3.0f));
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(ImVec4(0.98f, 0.75f, 0.35f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION "  %s", Tr("Fayl tashqarida o'zgardi:"));
            ImGui::SameLine();
            if (ImGui::SmallButton(Tr(ICON_FA_ROTATE "  Qayta yuklash")))
                ReloadPrefabStage();
            if (ImGui::IsItemHovered())
                UI::Tooltip(Tr("Fayldagisini o'qish (bu yerdagi saqlanmagan o'zgarishlar yo'qoladi)"));
            ImGui::SameLine();
            if (ImGui::SmallButton(Tr("Meniki qolsin")))
                m_PrefabStages.back().ExternalChange = false;
            if (ImGui::IsItemHovered())
                UI::Tooltip(Tr("Bu yerdagisi qoladi (saqlansa, fayldagi o'zgarish ustidan yoziladi)"));
        }
        ImGui::PopID();
        if (leaveTo)
            RequestLeavePrefabStages(*leaveTo);
    }

    void EditorLayer::UI_PrefabStagePopups()
    {
        // ---- leaving a level with unsaved edits
        if (m_PrefabStageLeave.Active && m_PrefabStageLeave.Asking && IsInPrefabStage() &&
            !ImGui::IsPopupOpen(Tr("Prefab saqlanmagan###PrefabStageLeave")))
            ImGui::OpenPopup(Tr("Prefab saqlanmagan###PrefabStageLeave"));
        if (ImGui::BeginPopupModal(Tr("Prefab saqlanmagan###PrefabStageLeave"), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            const std::string name = IsInPrefabStage() ? FileName(m_PrefabStages.back().Prefab) : std::string();
            ImGui::TextUnformatted(std::format("'{}' {}", name, Tr("prefabida saqlanmagan o'zgarishlar bor.")).c_str());
            ImGui::TextDisabled("%s", Tr("Saqlansa, uning barcha nusxalari yangilanadi."));
            ImGui::Spacing();
            auto done = [&]() {
                m_PrefabStageLeave.Asking = false;
                ImGui::CloseCurrentPopup();
            };
            if (ImGui::Button(Tr(ICON_FA_FLOPPY_DISK "  Saqlash")))
            {
                if (SavePrefabStage())
                    PopPrefabStage();
                else
                    m_PrefabStageLeave = {}; // not saved: it stays open
                done();
            }
            ImGui::SameLine();
            if (ImGui::Button(Tr("Saqlamaslik")))
            {
                PopPrefabStage();
                done();
            }
            ImGui::SameLine();
            if (ImGui::Button(Tr("Bekor qilish")) || !IsInPrefabStage())
            {
                m_PrefabStageLeave = {};
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // ---- a prefab whose file does not parse: what does?
        if (!m_PrefabStageRecoverPrompt.empty() && !ImGui::IsPopupOpen(Tr("Prefab fayli buzilgan###PrefabStageRecover")))
            ImGui::OpenPopup(Tr("Prefab fayli buzilgan###PrefabStageRecover"));
        if (ImGui::BeginPopupModal(Tr("Prefab fayli buzilgan###PrefabStageRecover"), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted(std::format("'{}' {}", FileName(m_PrefabStageRecoverPrompt), Tr("faylining JSON'i buzilgan.")).c_str());
            ImGui::TextDisabled("%s", Tr("Qisman tiklash: o'qiladigan obyektlar ochiladi, buzilgan joylar tashlab ketiladi."));
            ImGui::TextDisabled("%s", Tr("Asl fayl .bak nusxa sifatida saqlab qo'yiladi; saqlaguningizcha fayl o'zgarmaydi."));
            ImGui::Spacing();
            if (ImGui::Button(Tr(ICON_FA_TRUCK_MEDICAL "  Qisman tiklash")))
            {
                fs::path backup;
                if (SceneRecovery::WriteBackup(Project::GetAssetFileSystemPath(m_PrefabStageRecoverPrompt), &backup))
                    ZM_INFO("Buzilgan prefab nusxasi saqlandi: {}", backup.filename().string());
                RequestPrefabStage(m_PrefabStageRecoverPrompt, 0, true);
                m_PrefabStageRecoverPrompt.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button(Tr("Bekor qilish")))
            {
                m_PrefabStageRecoverPrompt.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // ---- saving a prefab with several roots: under a new one?
        if (m_PrefabStageWrapPrompt && IsInPrefabStage() && !ImGui::IsPopupOpen(Tr("Prefabda bitta ildiz bo'lishi kerak###PrefabStageWrap")))
            ImGui::OpenPopup(Tr("Prefabda bitta ildiz bo'lishi kerak###PrefabStageWrap"));
        if (ImGui::BeginPopupModal(Tr("Prefabda bitta ildiz bo'lishi kerak###PrefabStageWrap"), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted(Tr("Prefab bitta ildiz obyektdan iborat bo'lishi kerak (nusxalari unga bog'lanadi)."));
            ImGui::Spacing();
            if (ImGui::Button(Tr(ICON_FA_BOX "  Yangi ildizga o'rash")) && IsInPrefabStage())
            {
                std::vector<Entity> roots;
                for (Entity e : m_EditorScene->GetEntitiesInHierarchyOrder())
                    if (!m_EditorScene->GetParent(e))
                        roots.push_back(e);
                Entity wrapper = m_EditorScene->CreateEntity(fs::path(m_PrefabStages.back().Prefab).stem().string());
                for (Entity e : roots)
                    m_EditorScene->SetParent(e, wrapper, true);
                MarkDirty(Tr("Ildizlar yangi ildizga o'raldi"));
                CommitUndo();
                m_SceneHierarchyPanel.SetSelectedEntity(wrapper);
                m_PrefabStageWrapPrompt = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button(Tr("Bekor qilish")))
            {
                m_PrefabStageWrapPrompt = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (!IsInPrefabStage())
            m_PrefabStageWrapPrompt = false;
    }

}
