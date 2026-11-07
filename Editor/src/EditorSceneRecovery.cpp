// A scene file whose JSON is broken (EditorLayer): OpenScene keeps the open scene and asks whether to
// recover what parses. Recovery never changes the broken file before the user saves: it is first copied
// aside (<name>.zscene.corrupt-<YYYYMMDD-HHMMSS>.bak), then every entity that parses on its own is
// loaded (SceneSerializer::RecoverFromString) with the report of what was left out. The scene keeps
// its path and is unsaved. Prefab instances that lost entities with the broken part can take them back
// from their prefabs (structure only: nothing else of theirs changes).
#include "EditorLayer.h"
#include "UI.h"

#include <imgui.h>

#include <format>

namespace fs = std::filesystem;

namespace Zamin {

    bool EditorLayer::RecoverScene(const fs::path& path)
    {
        if (IsInPrefabStage())
        {
            RequestLeavePrefabStages(0, [this, path]() { RecoverScene(path); });
            return false;
        }
        if (m_SceneState != SceneState::Edit)
            OnSceneStop();
        std::error_code ec;
        if (!fs::is_regular_file(path, ec))
            return false;
        // The broken file stays as it was next to it (the saved scene replaces it only when saved).
        fs::path backup;
        if (SceneRecovery::WriteBackup(path, &backup))
            ZM_INFO("Buzilgan sahna fayli nusxasi saqlandi: {}", backup.filename().string());
        else
            ZM_WARN("Buzilgan sahna faylining nusxasi saqlanmadi: {}", path.string());

        Ref<Scene> recovered = CreateRef<Scene>();
        SceneSerializer serializer(recovered);
        const bool ok = serializer.RecoverFromString(FileSystem::ReadTextFile(path));
        SceneLoadReport report = serializer.GetLoadReport();
        report.File = path.string();
        if (!ok)
        {
            ShowSceneLoadReport(report);
            ShowViewportMessage(std::format(ICON_FA_TRIANGLE_EXCLAMATION "  {}: {}", Tr("Sahna tiklanmadi"), path.filename().string()));
            return false;
        }
        recovered->ResolveAssets();
        if (recovered->GetName() == "Tiklangan sahna")
            recovered->SetName(path.stem().string()); // its name was in the lost part

        DiscardSessionAutosave();
        m_EditorScene = recovered;
        m_SceneHierarchyPanel.SetContext({});
        SetActiveScene(m_EditorScene);
        m_EditorScenePath = path;
        m_EditorSceneFileVersion = serializer.GetMigration().FileVersion;
        m_AssetUsage.MarkDirty();
        PrefabUtility::LinkLegacyInstances(*m_EditorScene, m_PrefabDatabase);
        ResetUndoHistory(std::format("Sahna qisman tiklandi: {}", path.stem().string()), false);
        if (SyncPrefabInstances(true))
            m_UndoHistory.ReplaceCurrentScene(TakeSnapshot());
        m_SceneDirty = true; // the file still holds the broken text
        ShowSceneLoadReport(report);
        m_ShowLoadReport = true;
        // Prefab instances missing entities of their prefab (they were in the lost part): offered back.
        for (Entity e : m_EditorScene->GetEntitiesInHierarchyOrder())
            if (PrefabUtility::IsInstanceRoot(e))
                if (const auto* link = e.TryGetComponent<PrefabLinkComponent>(); link && !link->RemovedEntities.empty())
                    m_RecoveredPrefabsPrompt = true;
        const size_t entities = m_EditorScene->GetAllEntitiesWith<IDComponent>().size();
        ZM_WARN("Sahna qisman tiklandi: {} ({} obyekt) - tekshirib, saqlang", path.filename().string(), entities);
        ShowViewportMessage(std::format(ICON_FA_TRUCK_MEDICAL "  {}: {} ({} {})", Tr("Sahna qisman tiklandi"), path.filename().string(),
                                        entities, Tr("obyekt")));
        AddRecentScene(path);
        m_LastAutosaveTime = ImGui::GetTime();
        UpdateWindowTitle();
        return true;
    }

    void EditorLayer::UI_SceneRecoveryPopups()
    {
        // ---- a scene file that does not parse
        if (!m_SceneRecoverPrompt.empty() && !ImGui::IsPopupOpen(Tr("Sahna fayli buzilgan###SceneRecover")))
            ImGui::OpenPopup(Tr("Sahna fayli buzilgan###SceneRecover"));
        if (ImGui::BeginPopupModal(Tr("Sahna fayli buzilgan###SceneRecover"), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextColored(ImVec4(0.98f, 0.55f, 0.45f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION "  '%s' %s",
                               m_SceneRecoverPrompt.filename().string().c_str(), Tr("faylining JSON'i buzilgan - sahna ochilmadi."));
            if (const SceneLoadIssue* main = m_LoadReport.GetMain(); main && main->Type == SceneLoadIssue::Kind::ParseError)
                ImGui::TextDisabled("%s", main->Message.c_str());
            ImGui::Spacing();
            ImGui::TextUnformatted(Tr("Qisman tiklansinmi? O'qiladigan obyektlar va sozlamalar yuklanadi, buzilgan joylar tashlab ketiladi."));
            ImGui::TextDisabled("%s", Tr("Asl fayl avval .bak nusxa sifatida saqlab qo'yiladi; saqlaguningizcha fayl o'zgarmaydi."));
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Button, UI::AccentDarkColor(1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, UI::AccentMidColor());
            if (ImGui::Button(Tr(ICON_FA_TRUCK_MEDICAL "  Qisman tiklash")))
            {
                const fs::path path = m_SceneRecoverPrompt;
                m_SceneRecoverPrompt.clear();
                ImGui::CloseCurrentPopup();
                RecoverScene(path);
            }
            ImGui::PopStyleColor(2);
            ImGui::SameLine();
            if (ImGui::Button(Tr("Bekor qilish")))
            {
                m_SceneRecoverPrompt.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // ---- recovered: prefab instances that lost entities
        if (m_RecoveredPrefabsPrompt && !ImGui::IsPopupOpen(Tr("Prefab nusxalarini tiklash###RecoverPrefabs")))
            ImGui::OpenPopup(Tr("Prefab nusxalarini tiklash###RecoverPrefabs"));
        if (ImGui::BeginPopupModal(Tr("Prefab nusxalarini tiklash###RecoverPrefabs"), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted(Tr("Ba'zi prefab nusxalarida prefabdagi obyektlar yo'q (buzilgan qismda qolgan bo'lishi mumkin)."));
            ImGui::TextDisabled("%s", Tr("Tiklansa, ular prefabdan qayta yaratiladi; nusxalarning boshqa o'zgarishlari qoladi."));
            ImGui::Spacing();
            if (ImGui::Button(Tr(ICON_FA_ROTATE_RIGHT "  Prefab nusxalarini tiklash")) && m_EditorScene)
            {
                size_t restored = 0;
                for (Entity e : m_EditorScene->GetEntitiesInHierarchyOrder())
                    if (PrefabUtility::IsInstanceRoot(e))
                        restored += PrefabUtility::RestoreRemoved(e, m_PrefabDatabase);
                if (restored)
                {
                    m_EditorScene->ResolveAssets();
                    MarkDirty(std::format("Prefab nusxalari tiklandi ({} obyekt)", restored));
                }
                m_RecoveredPrefabsPrompt = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button(Tr("Kerak emas")))
            {
                m_RecoveredPrefabsPrompt = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

}
