// Prefab hot reload (EditorLayer): a prefab file changed outside the editor (a text editor, version
// control, another tool) - the prefabs made of it are brought up to date on disk and the open scene's
// instances follow it, keeping their overrides. Not a step of its own (like opening a scene): the
// current undo state takes the new text and the scene is marked unsaved. While the game runs nothing
// is touched (the running scene spawns the new file through PrefabCache): the instances follow when it
// stops. An open prefab mode level is read again, or - with unsaved edits - shows the banner.
#include "EditorLayer.h"
#include "UI.h"

#include "Zamin/Scene/PrefabCache.h"

#include <imgui.h>

#include <format>

namespace fs = std::filesystem;

namespace Zamin {

    namespace {
        std::string StemOf(const std::string& path) { return fs::path(path).stem().string(); }

        bool Contains(const std::vector<std::string>& list, const std::string& value)
        {
            return std::find(list.begin(), list.end(), value) != list.end();
        }
    }

    void EditorLayer::QueuePrefabReload(const fs::path& file)
    {
        if (!Project::GetActive())
            return;
        std::string rel = PrefabDatabase::NormalizePath(Project::GetAssetRelativePath(file));
        if (!rel.empty() && !Contains(m_PendingPrefabReloads, rel))
            m_PendingPrefabReloads.push_back(std::move(rel));
    }

    void EditorLayer::ProcessPrefabReloads()
    {
        if (m_PendingPrefabReloads.empty() && !m_PrefabReloadAll)
            return;
        if (!Project::GetActive() || !m_EditorScene)
        {
            m_PendingPrefabReloads.clear();
            m_PrefabReloadAll = false;
            return;
        }
        if (m_SceneState != SceneState::Edit)
        {
            // The running game is never touched: its instances follow when it stops.
            if (!m_PrefabReloadNoticeShown)
            {
                ZM_INFO("Prefab fayli o'zgardi: o'yin to'xtagach sahnadagi nusxalar yangilanadi");
                ShowViewportMessage(Tr(ICON_FA_BOX "  Prefab o'zgardi: o'yin to'xtagach nusxalar yangilanadi"));
                m_PrefabReloadNoticeShown = true;
            }
            return;
        }
        // Not in the middle of a drag or a field being typed into: the next frame.
        if (ImGui::IsAnyItemActive() || ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Right))
            return;
        m_PrefabReloadNoticeShown = false;
        std::vector<std::string> paths = std::move(m_PendingPrefabReloads);
        m_PendingPrefabReloads.clear();
        const bool all = m_PrefabReloadAll;
        m_PrefabReloadAll = false;
        if (all)
            m_PrefabDatabase.Clear(); // prefabs moved / renamed outside: everything is read again

        PrefabUtility::ReloadResult reload = PrefabUtility::ReloadFiles(m_PrefabDatabase, paths);
        if (reload.IsEmpty() && !all)
            return; // the editor's own writes, touched files
        for (const std::string& path : reload.Changed)
            PrefabCache::Invalidate(Project::GetAssetFileSystemPath(path));
        // A removed prefab the open scene still has instances of (a rename in the content browser moved
        // them to the new path already: nothing to say then).
        for (const std::string& path : reload.Removed)
            for (entt::entity id : m_EditorScene->GetAllEntitiesWith<EditorMetadataComponent>())
                if (Entity e{ id, m_EditorScene.get() }; PrefabUtility::IsInstanceRoot(e) && PrefabDatabase::NormalizePath(PrefabUtility::GetPrefabPath(e)) == path)
                {
                    ZM_WARN("Prefab fayli o'chirildi: {} - uning nusxalari ma'lumotlarini saqlaydi", path);
                    break;
                }
        for (const std::string& path : reload.Failed)
            ZM_WARN("Prefab yangilanmadi (ichidagi prefab o'zgardi, lekin fayl o'qilmadi yoki yangiroq formatda): {}", path);
        for (const std::string& path : reload.Rewritten)
            m_ContentBrowserPanel.InvalidateThumbnail(Project::GetAssetFileSystemPath(path));
        if (!reload.Rewritten.empty())
        {
            m_ContentBrowserPanel.Refresh();
            ZM_INFO("Prefab tashqarida o'zgardi ({}): {} ta bog'liq prefab fayli yangilandi", reload.Changed.empty() ? std::string("?") : reload.Changed.front(),
                    reload.Rewritten.size());
        }

        // A prefab open in prefab mode: its file was changed (outside, or rewritten because a prefab
        // nested in it changed). Without unsaved edits it is read again; with them, an outside change
        // shows the banner and a nested change only updates the nested instances.
        const std::vector<std::string>* filter = all ? nullptr : &reload.Affected;
        if (IsInPrefabStage())
        {
            PrefabStageLevel& level = m_PrefabStages.back();
            if ((all || Contains(reload.Affected, level.Prefab)) && !m_SceneDirty)
            {
                ReloadPrefabStage();
                return;
            }
            if (Contains(reload.Changed, level.Prefab))
                level.ExternalChange = true; // the banner: read it again, or keep the edits
        }

        // The scene's instances follow (their overrides kept).
        CommitUndo(); // what is pending is a step of its own first
        PrefabUtility::SyncResult sync = PrefabUtility::SyncScene(*m_EditorScene, m_PrefabDatabase, filter);
        for (const std::string& issue : sync.Issues)
            ZM_WARN("Prefab nusxasi yangilanmadi: {}", issue);
        if (!sync.Changed && !sync.Linked)
            return;
        if (sync.Changed)
            m_EditorScene->ResolveAssets();
        m_UndoHistory.ReplaceCurrentScene(TakeSnapshot());
        m_SceneDirty = !m_UndoHistory.IsAtSavedState();
        m_SceneStatsDirty = true;
        m_AssetUsage.MarkDirty();
        UpdateWindowTitle();
        if (!sync.Changed)
            return;
        const std::string what = reload.Changed.size() == 1 ? StemOf(reload.Changed.front())
                               : reload.Changed.empty() ? Tr("prefablar")
                               : std::format("{} {}", reload.Changed.size(), Tr("ta prefab"));
        for (const std::string& path : reload.Changed)
            ZM_INFO("Prefab tashqarida o'zgardi: {}", path);
        ZM_INFO("Prefab yangilandi: {} - sahnada {} ta nusxa yangilandi", what, sync.Changed);
        ShowViewportMessage(std::format(ICON_FA_ROTATE "  {}: {} ({} {})", Tr("Prefab yangilandi"), what, sync.Changed, Tr("nusxa")));
    }

}
