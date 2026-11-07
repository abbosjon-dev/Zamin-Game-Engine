// Prefab instances in the editor (EditorLayer): placed linked to their prefab, saved prefabs linked to
// their source, old scenes linked on open and every instance kept up to date with its prefab (open,
// undo / redo), what an edit changed recorded as overrides, the hierarchy's / inspector's Apply /
// Revert / Unpack requests run as undo steps (an Apply's file writes undone with it).
#include "EditorLayer.h"
#include "UI.h"

#include <imgui.h>

#include <format>

namespace fs = std::filesystem;

namespace Zamin {

    namespace {
        double Now() { return (double)GetUnixTime(); }

        std::string FileName(const std::string& path) { return fs::path(path).filename().string(); }
    }

    void EditorLayer::CommitPrefabStep(std::string name, std::vector<FileEdit> files)
    {
        m_PendingUndo = false;
        m_PendingUndoName.clear();
        m_UndoHistory.CommitEdit(TakeSnapshot(), CaptureUndoSelection(), std::move(name), Now(), std::move(files));
        m_UndoSelectionRevision = m_SceneHierarchyPanel.GetSelection().GetRevision();
        m_PendingSelectionUndo = false;
        m_UndoHistoryScroll = true;
        m_SceneDirty = !m_UndoHistory.IsAtSavedState();
        m_SceneStatsDirty = true;
        m_AssetUsage.MarkDirty();
        UpdateWindowTitle();
    }

    void EditorLayer::ApplyUndoFiles(const std::vector<UndoHistory::FileStep>& files)
    {
        for (const UndoHistory::FileStep& step : files)
        {
            if (!step.Edit)
                continue;
            const FileEdit& edit = *step.Edit;
            const fs::path file = m_PrefabDatabase.ToFileSystemPath(edit.Path);
            std::error_code ec;
            // The file should hold what the step left there; a change made outside since is overwritten (said so).
            const std::optional<std::string>& expected = step.Undo ? std::optional<std::string>(edit.After) : edit.Before;
            const bool exists = fs::is_regular_file(file, ec);
            if (exists != expected.has_value() || (exists && FileSystem::ReadTextFile(file) != *expected))
                ZM_WARN("Fayl tashqarida o'zgargan: {} - undo/redo uni ustiga yozdi", edit.Path);
            const std::optional<std::string>& text = step.Undo ? edit.Before : std::optional<std::string>(edit.After);
            if (text)
                m_PrefabDatabase.Write(edit.Path, *text);
            else
            {
                fs::remove(file, ec);
                if (AssetRegistry* registry = Project::GetActive() ? Project::GetAssetRegistry() : nullptr)
                    registry->OnFileRemoved(file);
                m_PrefabDatabase.Invalidate(edit.Path);
            }
            m_ContentBrowserPanel.Refresh();
        }
    }

    bool EditorLayer::SyncPrefabInstances(bool reportIssues)
    {
        if (!m_EditorScene || m_SceneState != SceneState::Edit)
            return false;
        PrefabUtility::SyncResult sync = PrefabUtility::SyncScene(*m_EditorScene, m_PrefabDatabase);
        if (reportIssues)
            for (const std::string& issue : sync.Issues)
                ZM_WARN("Prefab nusxasi yangilanmadi: {}", issue);
        if (!sync.Changed && !sync.Linked)
            return false;
        if (sync.Changed)
            m_EditorScene->ResolveAssets();
        return true;
    }

    bool EditorLayer::RecordPrefabOverrides(const std::string* before, const std::string& after)
    {
        if (!m_EditorScene || m_SceneState != SceneState::Edit)
            return false;
        if (m_EditorScene->GetAllEntitiesWith<PrefabLinkComponent>().empty())
            return false; // no instance in the scene (the common case)
        std::vector<UUID> touched;
        bool removed = false;
        if (before)
        {
            const SceneChange change = DiffSceneSnapshots(*before, after);
            if (change.Valid)
            {
                for (const auto& c : change.Changed)
                    touched.push_back(UUID(c.ID));
                for (uint64_t id : change.AddedIDs)
                    touched.push_back(UUID(id));
                removed = !change.RemovedIDs.empty();
            }
            else
                removed = true; // unknown: every instance is looked at
        }
        else
            removed = true;
        return PrefabUtility::RecordOverrides(*m_EditorScene, touched, removed, m_PrefabDatabase) > 0;
    }

    void EditorLayer::ProcessPrefabRequests()
    {
        if (m_PrefabRequests.empty())
            return;
        std::vector<PrefabRequest> requests = std::move(m_PrefabRequests);
        m_PrefabRequests.clear();
        for (const PrefabRequest& request : requests)
            RunPrefabRequest(request);
    }

    void EditorLayer::RunPrefabRequest(const PrefabRequest& request)
    {
        Entity entity = m_EditorScene ? m_EditorScene->GetEntityByUUID(request.Entity) : Entity();
        if (!entity)
            return;
        Entity root = PrefabUtility::GetInstanceRoot(entity);
        if (!root)
            return;
        const std::string prefab = PrefabUtility::GetPrefabPath(root);
        if (request.Operation == PrefabRequest::Op::ShowInBrowser)
        {
            m_ContentBrowserPanel.Reveal({ Project::GetAssetFileSystemPath(prefab) });
            return;
        }
        if (m_SceneState != SceneState::Edit)
        {
            ShowViewportMessage(ICON_FA_BOX "  O'yin rejimida prefab o'zgartirilmaydi");
            return;
        }
        // What is pending becomes its own step first (its overrides recorded).
        CommitUndo();
        entity = m_EditorScene->GetEntityByUUID(request.Entity);
        root = entity ? PrefabUtility::GetInstanceRoot(entity) : Entity();
        if (!entity || !root)
            return;
        const std::string rootName = root.GetName();
        switch (request.Operation)
        {
            case PrefabRequest::Op::Apply:
            {
                PrefabApplyResult result = PrefabUtility::Apply(entity, request.Scope, request.Path, m_PrefabDatabase, request.Target);
                if (!result.Applied)
                {
                    ZM_WARN("Prefabga qo'llanmadi ({}): {}", rootName, result.Error);
                    ShowViewportMessage(std::format(ICON_FA_TRIANGLE_EXCLAMATION "  Qo'llanmadi: {}", result.Error));
                    return;
                }
                m_EditorScene->ResolveAssets();
                for (const std::string& skipped : result.Skipped)
                    ZM_WARN("Prefabga yozilmadi (nusxada o'zgarish bo'lib qoldi): {}", skipped);
                for (const std::string& written : result.Written)
                    ZM_INFO("Prefab yozildi: {}", written);
                m_ContentBrowserPanel.Refresh();
                const std::string what = request.Scope == PrefabScope::Field ? request.Path
                                       : request.Scope == PrefabScope::Component ? std::string(SceneHierarchyPanel::GetComponentDisplayName(request.Path))
                                       : request.Scope == PrefabScope::Entity ? entity.GetName() : rootName;
                CommitPrefabStep(std::format("Prefabga qo'llandi: {} \xe2\x86\x92 {}", what, FileName(result.Target)), std::move(result.Files));
                ShowViewportMessage(result.InstancesSynced
                    ? std::format(ICON_FA_UPLOAD "  Qo'llandi: {} ({} ta boshqa nusxa yangilandi)", FileName(result.Target), result.InstancesSynced)
                    : std::format(ICON_FA_UPLOAD "  Qo'llandi: {}", FileName(result.Target)));
                break;
            }
            case PrefabRequest::Op::Revert:
            {
                if (request.Scope == PrefabScope::Instance && !m_PrefabRevertConfirm.Open)
                {
                    // A full revert deletes what was added to the instance: asked first.
                    std::vector<std::string> added;
                    for (const PrefabOverrideInfo& o : PrefabUtility::GetOverrides(root))
                        if (o.Type == PrefabOverrideInfo::Kind::AddedEntity)
                            added.push_back(o.EntityName);
                    for (Entity e : m_EditorScene->GetDescendants(root))
                        if (PrefabUtility::IsInstanceRoot(e) && PrefabUtility::IsLinked(m_EditorScene->GetParent(e)))
                            added.push_back(e.GetName());
                    if (!added.empty())
                    {
                        m_PrefabRevertConfirm = { root.GetUUID(), std::move(added), true };
                        return;
                    }
                }
                m_PrefabRevertConfirm.Open = false;
                if (!PrefabUtility::Revert(entity, request.Scope, request.Path, m_PrefabDatabase))
                    return;
                m_EditorScene->ResolveAssets();
                MarkDirty(request.Scope == PrefabScope::Instance ? std::format("Prefab nusxasi asliga qaytarildi: {}", rootName)
                          : std::format("Asliga qaytarildi: {} ({})", request.Path.empty() ? entity.GetName() : request.Path, rootName));
                break;
            }
            case PrefabRequest::Op::RestoreRemoved:
            {
                const size_t restored = PrefabUtility::RestoreRemoved(root, m_PrefabDatabase);
                if (!restored)
                    return;
                m_EditorScene->ResolveAssets();
                MarkDirty(std::format("O'chirilganlar tiklandi: {} ({} ta)", rootName, restored));
                break;
            }
            case PrefabRequest::Op::Unpack:
            case PrefabRequest::Op::UnpackCompletely:
            {
                const bool completely = request.Operation == PrefabRequest::Op::UnpackCompletely;
                PrefabUtility::Unpack(root, completely);
                MarkDirty(std::format("Prefab bog'lanishi uzildi: {}", rootName));
                ZM_INFO("Prefab bog'lanishi uzildi: {} ({}){}", rootName, prefab, completely ? " - ichidagi nusxalar bilan" : "");
                break;
            }
            case PrefabRequest::Op::ShowInBrowser:
                break;
        }
    }

    void EditorLayer::WritePrefab(Entity entity, const fs::path& path)
    {
        if (!entity)
            return;
        std::error_code ec;
        std::optional<std::string> before;
        if (fs::is_regular_file(path, ec))
            before = FileSystem::ReadTextFile(path);
        const bool edit = m_SceneState == SceneState::Edit;
        if (edit)
            CommitUndo();
        if (!m_ActiveScene->SavePrefab(entity, path))
            return;
        const std::string rel = Project::GetAssetRelativePath(path);
        ZM_INFO("Prefab saqlandi: {}", rel);
        // The flags are saved as they are: instances start the same way.
        if (!entity.IsActive())
            ZM_INFO("'{}' nofaol holatda saqlandi: prefab nusxalari ham nofaol boshlanadi (SetActive(true) bilan yoqiladi)", entity.GetName());
        // A handle and a .zmeta at once (the link keeps the prefab's UUID).
        if (AssetRegistry* registry = Project::GetAssetRegistry())
            registry->OnFileAdded(path);
        m_PrefabDatabase.Invalidate(rel);
        m_ContentBrowserPanel.Refresh();
        if (!edit)
            return; // the game's copy: nothing to link
        // The source becomes the prefab's first instance (its own UUIDs are the prefab's IDs).
        PrefabUtility::LinkSource(entity, rel, m_PrefabDatabase);
        const std::string name = entity.GetName();
        // Other instances of a prefab that was replaced follow the new file.
        std::vector<std::string> only{ rel };
        PrefabUtility::SyncResult sync = PrefabUtility::SyncScene(*m_EditorScene, m_PrefabDatabase, &only);
        if (sync.Changed)
            m_EditorScene->ResolveAssets();
        std::vector<FileEdit> files;
        files.push_back({ rel, std::move(before), FileSystem::ReadTextFile(path) });
        CommitPrefabStep(std::format("Prefab saqlandi: {}", name), std::move(files));
        m_SceneHierarchyPanel.SetSelectedEntity(entity);
    }

    void EditorLayer::UI_PrefabPopups()
    {
        // ---- Save as prefab: a file of that name is there already
        if (m_PrefabOverwrite.Open)
        {
            ImGui::OpenPopup(Tr("Prefab allaqachon bor###PrefabOverwrite"));
            m_PrefabOverwrite.Open = false;
        }
        if (ImGui::BeginPopupModal(Tr("Prefab allaqachon bor###PrefabOverwrite"), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            Entity entity = m_ActiveScene ? m_ActiveScene->GetEntityByUUID(m_PrefabOverwrite.Entity) : Entity();
            const std::string file = m_PrefabOverwrite.Path.filename().string();
            ImGui::TextUnformatted(std::format("'{}' {}", file, Tr("nomli prefab bor.")).c_str());
            ImGui::TextDisabled("%s", Tr("Almashtirilsa, uning boshqa nusxalari yangi prefabga o'tadi."));
            ImGui::Spacing();
            // "Enemy (1).zprefab": the first free name.
            fs::path fresh;
            for (int i = 1; i < 1000; i++)
            {
                fs::path candidate = m_PrefabOverwrite.Path.parent_path() / std::format("{} ({}).zprefab", m_PrefabOverwrite.Path.stem().string(), i);
                std::error_code ec;
                if (!fs::exists(candidate, ec))
                {
                    fresh = candidate;
                    break;
                }
            }
            if (ImGui::Button(Tr(ICON_FA_FLOPPY_DISK "  Almashtirish")) && entity)
            {
                WritePrefab(entity, m_PrefabOverwrite.Path);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            const std::string freshLabel = std::format(ICON_FA_PLUS "  {} ({})", Tr("Yangi nom"), fresh.filename().string());
            if (!fresh.empty() && ImGui::Button(freshLabel.c_str()) && entity)
            {
                WritePrefab(entity, fresh);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button(Tr("Bekor qilish")) || !entity)
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        // ---- Revert the whole instance: the added entities go
        if (m_PrefabRevertConfirm.Open && !ImGui::IsPopupOpen(Tr("Prefab nusxasini asliga qaytarish###PrefabRevert")))
            ImGui::OpenPopup(Tr("Prefab nusxasini asliga qaytarish###PrefabRevert"));
        if (ImGui::BeginPopupModal(Tr("Prefab nusxasini asliga qaytarish###PrefabRevert"), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted(Tr("Nusxaga qo'shilgan obyektlar o'chiriladi:"));
            for (size_t i = 0; i < m_PrefabRevertConfirm.Added.size() && i < 12; i++)
                ImGui::BulletText("%s", m_PrefabRevertConfirm.Added[i].c_str());
            if (m_PrefabRevertConfirm.Added.size() > 12)
                ImGui::TextDisabled("... +%zu", m_PrefabRevertConfirm.Added.size() - 12);
            ImGui::Spacing();
            if (ImGui::Button(Tr(ICON_FA_ROTATE_LEFT "  Qaytarish")))
            {
                PrefabRequest request;
                request.Operation = PrefabRequest::Op::Revert;
                request.Entity = m_PrefabRevertConfirm.Root;
                RunPrefabRequest(request); // Open still set: no second question
                m_PrefabRevertConfirm = {};
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button(Tr("Bekor qilish")))
            {
                m_PrefabRevertConfirm = {};
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }


    // ZAMIN_EDITOR_PREFAB="color=<name>,r,g,b;scale=<name>,s;remove=<name>,<Component>;child=<parent>,<name>;
    // create=<name>;save=<name>;select=<name>;top=<name>;move=<name>,x,y,z;instantiate=<prefab>[,<parent>[,<name>]];
    // apply=<name>[,<path>[,<target>]];revert=<name>[,<path>];unpack=<name>;undo;redo;changes": prefab
    // instance edits made one by one as undo steps (screenshots, checks). "changes" opens the inspector's list.
    void EditorLayer::ApplyPrefabAutomation(std::string_view spec)
    {
        if (m_SceneState != SceneState::Edit || !m_EditorScene)
            return;
        auto find = [&](std::string_view name) { return m_EditorScene->FindEntityByName(std::string(name)); };
        auto split = [](const std::string& value) {
            std::vector<std::string> parts;
            size_t start = 0;
            while (true)
            {
                const size_t comma = value.find(',', start);
                parts.push_back(value.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
                if (comma == std::string::npos)
                    break;
                start = comma + 1;
            }
            return parts;
        };
        while (!spec.empty())
        {
            const size_t end = spec.find(';');
            const std::string_view item = spec.substr(0, end);
            spec = end == std::string_view::npos ? std::string_view() : spec.substr(end + 1);
            const size_t eq = item.find('=');
            const std::string_view op = item.substr(0, eq);
            const std::vector<std::string> args = split(eq == std::string_view::npos ? std::string() : std::string(item.substr(eq + 1)));
            Entity e = args.empty() ? Entity() : find(args[0]);
            if (op == "changes")
            {
                m_SceneHierarchyPanel.OpenPrefabChanges();
                continue;
            }
            if (op == "undo" || op == "redo")
            {
                op == "undo" ? Undo() : Redo();
                continue;
            }
            if (op == "instantiate" && !args.empty())
            {
                // instantiate=<prefab path>[,<parent>[,<name>]]
                Entity parent = args.size() >= 2 && !args[1].empty() ? find(args[1]) : Entity();
                if (Entity root = InstantiatePrefabInEditor(args[0], parent); root && args.size() >= 3)
                    root.SetName(args[2]);
                CommitUndo();
                continue;
            }
            if (op == "create" && !args.empty())
            {
                m_EditorScene->CreateEntity(args[0]);
                MarkDirty();
                CommitUndo();
                continue;
            }
            if (!e)
                continue;
            if (op == "color" && args.size() >= 4)
            {
                const glm::vec4 color(std::stof(args[1]), std::stof(args[2]), std::stof(args[3]), 1.0f);
                if (auto* text = e.TryGetComponent<TextComponent>())
                    text->Color = color;
                else if (auto* image = e.TryGetComponent<UIImageComponent>())
                    image->Color = color;
                else if (auto* sprite = e.TryGetComponent<SpriteRendererComponent>())
                    sprite->Color = color;
                MarkDirty();
                CommitUndo();
            }
            else if (op == "scale" && args.size() >= 2)
            {
                e.GetComponent<TransformComponent>().Scale = glm::vec3(std::stof(args[1]));
                MarkDirty();
                CommitUndo();
            }
            else if (op == "remove" && args.size() >= 2)
            {
                if (ComponentRegistry::Remove(e, args[1]))
                {
                    MarkDirty();
                    CommitUndo();
                }
            }
            else if (op == "child" && args.size() >= 2)
            {
                Entity child = m_EditorScene->CreateEntity(args[1]);
                m_EditorScene->SetParent(child, e, false);
                MarkDirty();
                CommitUndo();
            }
            else if (op == "save")
                SavePrefab(e);
            else if (op == "top")
                m_EditorScene->SetSiblingIndex(e, 0);
            else if (op == "select")
            {
                m_SceneHierarchyPanel.SetSelectedEntity(e);
                CommitSelectionUndo();
            }
            else if (op == "move" && args.size() >= 4)
            {
                e.GetComponent<TransformComponent>().Translation = { std::stof(args[1]), std::stof(args[2]), std::stof(args[3]) };
                MarkDirty();
                CommitUndo();
            }
            else if (op == "apply" || op == "revert" || op == "unpack")
            {
                PrefabRequest request;
                request.Entity = e.GetUUID();
                request.Operation = op == "apply" ? PrefabRequest::Op::Apply : op == "revert" ? PrefabRequest::Op::Revert : PrefabRequest::Op::Unpack;
                request.Scope = args.size() >= 2 ? PrefabScope::Field : PrefabScope::Instance;
                if (args.size() >= 2)
                    request.Path = args[1];
                if (args.size() >= 3)
                    request.Target = args[2];
                if (request.Operation == PrefabRequest::Op::Revert && request.Scope == PrefabScope::Instance)
                    m_PrefabRevertConfirm.Open = true; // automation: no question
                RunPrefabRequest(request);
                m_PrefabRevertConfirm = {};
                CommitUndo();
            }
        }
        m_UndoSelectionRevision = m_SceneHierarchyPanel.GetSelection().GetRevision();
    }

}
