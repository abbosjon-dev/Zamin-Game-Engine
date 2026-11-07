// The scene's undo (EditorLayer): snapshots of the editor scene with the selection, every step
// named (by the action, or from what the snapshots say changed), selection changes as steps of
// their own (Sozlamalar), and the "Undo tarixi" window that lists the steps (a click goes there).
#include "EditorLayer.h"
#include "UI.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <ImGuizmo.h>

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <format>

namespace Zamin {

    namespace {
        // The time a step was made, as "14:02:31" (local time).
        std::string ClockTime(double unixSeconds)
        {
            if (unixSeconds <= 0.0)
                return {};
            const std::time_t t = (std::time_t)unixSeconds;
            std::tm tm{};
            localtime_r(&t, &tm);
            return std::format("{:02}:{:02}:{:02}", tm.tm_hour, tm.tm_min, tm.tm_sec);
        }

        double Now() { return (double)GetUnixTime(); }
    }

    std::string EditorLayer::TakeSnapshot()
    {
        std::string snapshot;
        // The scene's own values, not the Animation window's preview pose.
        m_AnimationPanel.WithoutPreview([&]() { snapshot = SceneSerializer(m_EditorScene).SerializeToString(); });
        return snapshot;
    }

    UndoSelection EditorLayer::CaptureUndoSelection() const
    {
        UndoSelection selection;
        const EntitySelection& picked = m_SceneHierarchyPanel.GetSelection();
        selection.Entities.reserve(picked.Size());
        for (Entity e : picked.GetEntities())
            if (e.IsValid())
                selection.Entities.push_back((uint64_t)e.GetUUID());
        if (Entity primary = picked.GetPrimary(); primary.IsValid())
            selection.Primary = (uint64_t)primary.GetUUID();
        return selection;
    }

    void EditorLayer::ApplyUndoSelection(const UndoSelection& selection)
    {
        std::vector<Entity> entities;
        Entity primary;
        if (m_ActiveScene)
            for (uint64_t id : selection.Entities)
                if (Entity e = m_ActiveScene->GetEntityByUUID(UUID(id)))
                {
                    entities.push_back(e);
                    if (id == selection.Primary)
                        primary = e;
                }
        m_SceneHierarchyPanel.GetSelection().Set(entities, primary);
        m_UndoSelectionRevision = m_SceneHierarchyPanel.GetSelection().GetRevision();
        m_PendingSelectionUndo = false;
    }

    void EditorLayer::ResetUndoHistory(std::string name, bool saved)
    {
        m_PendingUndo = false;
        m_PendingUndoName.clear();
        m_PendingSelectionUndo = false;
        if (!m_EditorScene)
            return;
        if (name.empty())
            name = std::format("Sahna: {}", m_EditorScene->GetName());
        m_UndoHistory.Reset(TakeSnapshot(), CaptureUndoSelection(), std::move(name), Now(), saved);
        m_UndoSelectionRevision = m_SceneHierarchyPanel.GetSelection().GetRevision();
        m_UndoHistoryScroll = true;
        // What the file holds needs no safety copy.
        m_AutosavedScene = saved ? m_UndoHistory.GetCurrent().Scene : nullptr;
    }

    bool EditorLayer::IsEditInProgress() const
    {
        return ImGui::IsAnyItemActive() || ImGuizmo::IsUsingAny() || m_GizmoEdit.IsActive() || ImGui::IsMouseDown(ImGuiMouseButton_Left);
    }

    void EditorLayer::CommitUndo()
    {
        if (!m_PendingUndo || m_SceneState != SceneState::Edit)
            return;
        m_PendingUndo = false;
        std::string current = TakeSnapshot();
        const std::shared_ptr<const std::string>& before = m_UndoHistory.GetCurrent().Scene;
        if (before && *before == current)
        {
            // Changed and changed back: still the state it was in.
            m_PendingUndoName.clear();
            if (m_UndoHistory.IsAtSavedState() && m_SceneDirty)
            {
                m_SceneDirty = false;
                UpdateWindowTitle();
            }
            return;
        }
        std::string name = !m_PendingUndoName.empty() ? std::move(m_PendingUndoName)
                         : before ? DescribeSceneChange(*before, current) : std::string("O'zgarish");
        m_PendingUndoName.clear();
        // The selection the change left (a duplicate picks the copies, a delete clears it) is part of it.
        m_UndoHistory.CommitEdit(std::move(current), CaptureUndoSelection(), std::move(name), Now());
        m_UndoSelectionRevision = m_SceneHierarchyPanel.GetSelection().GetRevision();
        m_PendingSelectionUndo = false;
        m_UndoHistoryScroll = true;
        // Edited back to what the file holds: nothing to save.
        if (m_UndoHistory.IsAtSavedState() && m_SceneDirty)
        {
            m_SceneDirty = false;
            UpdateWindowTitle();
        }
    }

    void EditorLayer::CommitSelectionUndo()
    {
        m_PendingSelectionUndo = false;
        if (m_SceneState != SceneState::Edit)
            return;
        UndoSelection selection = CaptureUndoSelection();
        m_UndoSelectionRevision = m_SceneHierarchyPanel.GetSelection().GetRevision();
        if (!m_Preferences.Undo.RecordSelection)
        {
            // Not a step: an edit's undo still brings back the selection it had.
            m_UndoHistory.SetCurrentSelection(std::move(selection));
            return;
        }
        // The primary entity's name first.
        std::vector<std::string> names;
        names.reserve(selection.Entities.size());
        if (Entity primary = m_ActiveScene ? m_ActiveScene->GetEntityByUUID(UUID(selection.Primary)) : Entity(); primary && selection.Primary)
            names.push_back(primary.GetName());
        for (uint64_t id : selection.Entities)
            if (id != selection.Primary)
                if (Entity e = m_ActiveScene->GetEntityByUUID(UUID(id)))
                    names.push_back(e.GetName());
        if (m_UndoHistory.CommitSelection(std::move(selection), DescribeSelection(names), Now()))
            m_UndoHistoryScroll = true;
    }

    void EditorLayer::UpdateUndoTracking()
    {
        // Play / simulate: the history waits (the selection is the game's); back in edit mode the
        // current state takes whatever is selected then, without a step.
        if (m_SceneState != SceneState::Edit || !m_EditorScene)
        {
            m_UndoSelectionSync = true;
            return;
        }
        const uint64_t revision = m_SceneHierarchyPanel.GetSelection().GetRevision();
        if (m_UndoSelectionSync)
        {
            m_UndoSelectionSync = false;
            m_UndoHistory.SetCurrentSelection(CaptureUndoSelection());
            m_UndoSelectionRevision = revision;
            m_PendingSelectionUndo = false;
        }
        else if (revision != m_UndoSelectionRevision)
        {
            m_UndoSelectionRevision = revision;
            m_PendingSelectionUndo = CaptureUndoSelection() != m_UndoHistory.GetCurrent().Selection;
        }
        if (IsEditInProgress())
            return;
        if (m_PendingUndo)
            CommitUndo();
        else if (m_PendingSelectionUndo)
            CommitSelectionUndo();
    }

    void EditorLayer::RestoreSnapshot(const std::string& snapshot)
    {
        Ref<Scene> scene = CreateRef<Scene>();
        if (!SceneSerializer(scene).DeserializeFromString(snapshot))
            return;
        scene->ResolveAssets();
        m_EditorScene = scene;
        SetActiveScene(m_EditorScene, false);
        m_SceneDirty = true;
        m_AssetUsage.MarkDirty(); // undo / redo brings back (or takes away) what used assets
        UpdateWindowTitle();
    }

    void EditorLayer::ApplyUndoMove(const UndoHistory::Move& move)
    {
        if (!move.Target)
            return;
        if (move.SceneChanged && move.Target->Scene)
            RestoreSnapshot(*move.Target->Scene);
        ApplyUndoSelection(move.Target->Selection);
        m_SceneDirty = !m_UndoHistory.IsAtSavedState();
        m_UndoHistoryScroll = true;
        UpdateWindowTitle();
    }

    void EditorLayer::Undo()
    {
        if (m_SceneState != SceneState::Edit)
            return;
        // What is pending becomes a step first (and is the one taken back).
        CommitUndo();
        if (m_PendingSelectionUndo)
            CommitSelectionUndo();
        if (!m_UndoHistory.CanUndo())
            return;
        const std::string name = m_UndoHistory.GetUndoName();
        ApplyUndoMove(m_UndoHistory.Undo());
        ShowViewportMessage(ICON_FA_ROTATE_LEFT "  Bekor qilindi: " + name);
    }

    void EditorLayer::Redo()
    {
        if (m_SceneState != SceneState::Edit || !m_UndoHistory.CanRedo())
            return;
        const std::string name = m_UndoHistory.GetRedoName();
        ApplyUndoMove(m_UndoHistory.Redo());
        ShowViewportMessage(ICON_FA_ROTATE_RIGHT "  Qaytarildi: " + name);
    }

    void EditorLayer::JumpToUndoState(size_t index)
    {
        if (m_SceneState != SceneState::Edit)
            return;
        // A pending change becomes a step first; the list it was clicked in is then out of date.
        const size_t entries = m_UndoHistory.GetEntries().size(), current = m_UndoHistory.GetCurrentIndex();
        CommitUndo();
        if (m_PendingSelectionUndo)
            CommitSelectionUndo();
        if (m_UndoHistory.GetEntries().size() != entries || m_UndoHistory.GetCurrentIndex() != current)
            return;
        ApplyUndoMove(m_UndoHistory.JumpTo(index));
    }

    // ------------------------------------------------------------------ the window
    void EditorLayer::UI_UndoHistory()
    {
        if (!m_ShowUndoHistory)
            return;
        const float em = ImGui::GetFontSize();
        ImGui::SetNextWindowSize(ImVec2(em * 24.0f, em * 30.0f), ImGuiCond_FirstUseEver);
        // First opened: a tab next to the Inspector.
        if (ImGuiWindow* inspector = ImGui::FindWindowByName("###Inspector"); inspector && inspector->DockId)
            ImGui::SetNextWindowDockID(inspector->DockId, ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(Tr(ICON_FA_CLOCK_ROTATE_LEFT "  Undo tarixi###UndoHistory"), &m_ShowUndoHistory))
        {
            ImGui::End();
            return;
        }
        const bool editing = m_SceneState == SceneState::Edit;

        // ---- toolbar
        ImGui::BeginDisabled(!editing || !(m_UndoHistory.CanUndo() || m_PendingUndo));
        if (UI::ToolbarButton(ICON_FA_ROTATE_LEFT, std::format("Bekor qilish ({})", ShortcutLabel(EditorShortcut::Undo)).c_str()))
            Undo();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!editing || !m_UndoHistory.CanRedo());
        if (UI::ToolbarButton(ICON_FA_ROTATE_RIGHT, std::format("Qaytarish ({})", ShortcutLabel(EditorShortcut::Redo)).c_str()))
            Redo();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::SameLine();
        // Compact toggles (the window is often a narrow tab next to the Inspector).
        if (UI::ToolbarButton(ICON_FA_ARROW_POINTER, m_UndoHistoryEditsOnly ? "Tanlov qadamlari yashirilgan (bosing - ko'rsatish)"
                                                                           : "Tanlov qadamlari ko'rinadi (bosing - yashirish)", !m_UndoHistoryEditsOnly))
            m_UndoHistoryEditsOnly = !m_UndoHistoryEditsOnly;
        ImGui::SameLine();
        if (UI::ToolbarButton(ICON_FA_CIRCLE_DOT, m_Preferences.Undo.RecordSelection
                                  ? "Tanlov o'zgarishi alohida undo qadami (yoqiq). Bosing - o'chirish:\no'zgarishning undo'si o'sha paytdagi tanlovni baribir qaytaradi"
                                  : "Tanlov o'zgarishi undo qadami emas (o'chiq). Bosing - yoqish: Ctrl+Z oldingi tanlovni qaytaradi",
                              m_Preferences.Undo.RecordSelection))
        {
            m_Preferences.Undo.RecordSelection = !m_Preferences.Undo.RecordSelection;
            MarkPreferencesDirty();
        }
        ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::GetFrameHeight());
        ImGui::BeginDisabled(!editing || m_UndoHistory.GetEntries().size() <= 1);
        if (UI::ToolbarButton(ICON_FA_BROOM, "Tarixni tozalash (hozirgi holat qoladi)"))
        {
            CommitUndo();
            m_UndoHistory.ClearHistory("Tarix tozalandi", Now());
        }
        ImGui::EndDisabled();
        if (!editing)
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f), ICON_FA_PLAY "  O'yin rejimida undo yo'q - to'xtatilgach davom etadi");

        // ---- the steps: the current one highlighted, those after it (redo) dimmed
        const auto& entries = m_UndoHistory.GetEntries();
        const size_t current = m_UndoHistory.GetCurrentIndex();
        std::vector<size_t> rows;
        rows.reserve(entries.size());
        for (size_t i = 0; i < entries.size(); i++)
            if (!m_UndoHistoryEditsOnly || entries[i].Kind != UndoEntryKind::Selection || i == current)
                rows.push_back(i);
        const float footer = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
        size_t clicked = (size_t)-1;
        if (ImGui::BeginChild("##steps", ImVec2(0.0f, -footer), ImGuiChildFlags_Borders))
        {
            ImGuiListClipper clipper;
            clipper.Begin((int)rows.size());
            if (m_UndoHistoryScroll)
            {
                // The current row is drawn so that it can be scrolled to.
                auto it = std::find(rows.begin(), rows.end(), current);
                if (it != rows.end())
                    clipper.IncludeItemByIndex((int)(it - rows.begin()));
            }
            const float timeWidth = ImGui::CalcTextSize("00:00:00").x;
            while (clipper.Step())
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++)
                {
                    const size_t i = rows[(size_t)row];
                    const UndoHistory::Entry& e = entries[i];
                    const bool isCurrent = i == current, future = i > current;
                    const char* icon = e.Kind == UndoEntryKind::Initial ? ICON_FA_FILE : e.Kind == UndoEntryKind::Selection ? ICON_FA_ARROW_POINTER : ICON_FA_PEN;
                    ImGui::PushID((int)i);
                    if (future)
                        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                    else if (e.Kind == UndoEntryKind::Selection && !isCurrent)
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.68f, 0.72f, 0.78f, 1.0f));
                    else
                        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_Text));
                    if (isCurrent)
                        ImGui::PushStyleColor(ImGuiCol_Header, UI::AccentDarkColor(0.85f));
                    const ImVec2 rowStart = ImGui::GetCursorScreenPos();
                    if (ImGui::Selectable("##step", isCurrent, ImGuiSelectableFlags_AllowOverlap) && !isCurrent)
                        clicked = i;
                    const bool hovered = ImGui::IsItemHovered();
                    if (isCurrent)
                        ImGui::PopStyleColor();
                    // The name (cut with "..." before the time), the saved state's mark, the time.
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
                    const std::string time = ClockTime(e.Time);
                    // The saved mark on the state the file holds (not on the selection steps sharing its scene).
                    const bool saved = e.Kind != UndoEntryKind::Selection && m_UndoHistory.IsSavedState(i);
                    const float nameRight = right - timeWidth - em * (saved ? 2.2f : 0.8f);
                    const std::string label = std::format("{}  {}", icon, e.Name);
                    ImGui::RenderTextEllipsis(dl, rowStart, ImVec2(nameRight, rowStart.y + ImGui::GetTextLineHeight()), nameRight, nameRight,
                                              label.c_str(), nullptr, nullptr);
                    ImGui::PopStyleColor();
                    dl->AddText(ImVec2(right - timeWidth, rowStart.y), ImGui::GetColorU32(ImGuiCol_TextDisabled), time.c_str());
                    if (saved)
                        dl->AddText(ImVec2(right - timeWidth - em * 1.6f, rowStart.y), UI::AccentU32(), ICON_FA_FLOPPY_DISK);
                    if (hovered)
                    {
                        const char* kind = e.Kind == UndoEntryKind::Initial ? "Boshlang'ich holat" : e.Kind == UndoEntryKind::Selection ? "Tanlov" : "O'zgarish";
                        std::string tip = std::format("{}\n{} - {}", e.Name, kind, time);
                        if (saved)
                            tip += "\nFayldagi (saqlangan) holat";
                        if (!isCurrent)
                            tip += editing ? "\nBosing: shu holatga o'tish" : "\nO'yin rejimida o'tib bo'lmaydi";
                        UI::Tooltip(tip.c_str());
                    }
                    if (isCurrent && m_UndoHistoryScroll)
                    {
                        ImGui::SetScrollHereY(0.5f);
                        m_UndoHistoryScroll = false;
                    }
                    ImGui::PopID();
                }
            if (m_PendingUndo)
                ImGui::TextDisabled(ICON_FA_PEN "  ...o'zgarish (tugma qo'yib yuborilganda yoziladi)");
        }
        ImGui::EndChild();
        if (clicked != (size_t)-1)
            JumpToUndoState(clicked);

        ImGui::TextDisabled("%zu o'zgarish, %zu tanlash  |  %.1f MB", m_UndoHistory.CountEdits(), m_UndoHistory.CountSelections(),
                            (double)m_UndoHistory.GetMemoryUsage() / (1024.0 * 1024.0));
        ImGui::End();
    }


    // ------------------------------------------------------------------ automation
    void EditorLayer::ApplyUndoAutomation(std::string_view spec)
    {
        if (m_SceneState != SceneState::Edit || !m_EditorScene)
            return;
        auto find = [&](std::string_view name) {
            for (auto e : m_EditorScene->GetAllEntitiesWith<TagComponent>())
            {
                Entity entity{ e, m_EditorScene.get() };
                if (entity.GetName() == name)
                    return entity;
            }
            return Entity();
        };
        while (!spec.empty())
        {
            const size_t end = spec.find(';');
            const std::string_view item = spec.substr(0, end);
            spec = end == std::string_view::npos ? std::string_view() : spec.substr(end + 1);
            const size_t eq = item.find('=');
            const std::string_view op = item.substr(0, eq);
            const std::string value = eq == std::string_view::npos ? std::string() : std::string(item.substr(eq + 1));
            if (op == "history")
            {
                m_ShowUndoHistory = true;
                m_UndoHistoryScroll = true;
            }
            else if (op == "select")
            {
                m_SceneHierarchyPanel.SetSelectedEntity(find(value));
                CommitSelectionUndo();
            }
            else if (op == "move" || op == "rename")
            {
                const size_t comma = value.find(',');
                Entity entity = find(std::string_view(value).substr(0, comma));
                if (!entity || comma == std::string::npos)
                    continue;
                if (op == "rename")
                    entity.GetComponent<TagComponent>().Tag = value.substr(comma + 1);
                else
                {
                    glm::vec3 delta(0.0f);
                    std::sscanf(value.c_str() + comma + 1, "%f,%f,%f", &delta.x, &delta.y, &delta.z);
                    entity.GetComponent<TransformComponent>().Translation += delta;
                }
                MarkDirty();
                CommitUndo();
            }
            else if (op == "duplicate" || op == "delete")
            {
                op == "duplicate" ? DuplicateSelectedEntity() : DeleteSelectedEntity();
                CommitUndo();
            }
            else if (op == "undo")
            {
                for (int i = std::max(1, std::atoi(value.c_str())); i > 0; i--)
                    Undo();
            }
        }
        m_UndoSelectionRevision = m_SceneHierarchyPanel.GetSelection().GetRevision();
    }

}
