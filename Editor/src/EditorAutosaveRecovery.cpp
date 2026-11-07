// Autosave (EditorLayer): a safety copy of the open scene every few minutes while it has unsaved
// changes, in <project>/.zamin/autosave (the scene's own file is never touched); the project's lock
// file, which tells a session that crashed from one that closed the project; and the prompt that
// offers a copy newer than its scene's file back when that scene is opened. A copy whose answer was
// postponed ("Keyinroq") is put aside (<copy>.kept): this session's copies and saving the scene use
// the usual file, and the kept one is offered again the next time the scene is opened.
#include "EditorLayer.h"
#include "UI.h"

#include <Zamin/Utils/FileSystem.h>
#include <Zamin/Utils/PlatformUtils.h>

#include <imgui.h>

#include <chrono>
#include <ctime>
#include <format>

namespace Zamin {

    namespace fs = std::filesystem;

    namespace {
        int64_t ToUnixTime(fs::file_time_type time)
        {
            const auto sys = std::chrono::system_clock::now() +
                             std::chrono::duration_cast<std::chrono::system_clock::duration>(time - fs::file_time_type::clock::now());
            return (int64_t)std::chrono::duration_cast<std::chrono::seconds>(sys.time_since_epoch()).count();
        }

        std::string ClockTime(int64_t unixSeconds)
        {
            const std::time_t t = (std::time_t)unixSeconds;
            std::tm tm{};
            localtime_r(&t, &tm);
            return std::format("{:02}:{:02}:{:02}", tm.tm_hour, tm.tm_min, tm.tm_sec);
        }

        // "14:02:31 (5 daqiqa oldin)"
        std::string WhenText(fs::file_time_type time)
        {
            const int64_t unix = ToUnixTime(time);
            return std::format("{} ({})", ClockTime(unix), FormatTimeAgo(unix, GetUnixTime()));
        }

        size_t CountAutosaves(const fs::path& directory)
        {
            size_t count = 0;
            std::error_code ec;
            if (!fs::is_directory(directory, ec))
                return 0;
            for (auto it = fs::recursive_directory_iterator(directory, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
                if (it->is_regular_file(ec) && (it->path().extension() == ".zscene" || it->path().extension() == ".kept"))
                    count++;
            return count;
        }
    }

    // ------------------------------------------------------------------ the lock file
    void EditorLayer::AcquireProjectLock()
    {
        ReleaseProjectLock();
        if (!Project::GetActive())
            return;
        const fs::path directory = Project::GetProjectDirectory();
        const fs::path lockPath = EditorAutosave::GetLockPath(directory);
        const std::optional<EditorAutosave::LockInfo> lock = EditorAutosave::ReadLock(lockPath);
        using State = EditorAutosave::LockState;
        const State state = EditorAutosave::CheckLock(lock, EditorAutosave::GetCurrentProcessId(), EditorAutosave::IsProcessAlive);
        m_PreviousSessionCrashed = state == State::Crashed;
        if (state == State::Crashed)
            ZM_WARN("Editor oldingi safar bu loyihani yopmasdan to'xtagan - avtomatik saqlangan nusxalar tekshiriladi");
        else if (state == State::OtherInstance)
            ZM_WARN("Bu loyiha boshqa editor oynasida ham ochiq (jarayon {}) - bir vaqtda tahrirlash o'zgarishlarni yo'qotishi mumkin",
                    lock ? lock->ProcessId : 0);
        if (EditorAutosave::WriteLock(lockPath, EditorAutosave::MakeLock()))
            m_LockedProject = directory;
        else
            ZM_WARN("Loyiha qulf fayli yozilmadi: {}", lockPath.string());
    }

    void EditorLayer::ReleaseProjectLock()
    {
        if (m_LockedProject.empty())
            return;
        // Only our own lock (another editor that opened the project later keeps its one).
        const fs::path lockPath = EditorAutosave::GetLockPath(m_LockedProject);
        if (auto lock = EditorAutosave::ReadLock(lockPath); lock && lock->ProcessId == EditorAutosave::GetCurrentProcessId())
            EditorAutosave::RemoveLock(lockPath);
        m_LockedProject.clear();
    }

    // ------------------------------------------------------------------ copies
    fs::path EditorLayer::GetAutosavePath() const
    {
        if (!Project::GetActive())
            return {};
        const std::string scene = m_EditorScenePath.empty() ? std::string() : Project::GetAssetRelativePath(m_EditorScenePath);
        return EditorAutosave::GetFilePath(Project::GetProjectDirectory(), scene);
    }

    void EditorLayer::UpdateAutosave()
    {
        // Prefab mode: no safety copy (it would be taken for the untitled scene's).
        if (!Project::GetActive() || !m_EditorScene || IsInPrefabStage())
            return;
        const double now = ImGui::GetTime();
        const auto& scene = m_UndoHistory.GetCurrent().Scene;
        const bool hasNew = m_SceneDirty && scene && scene != m_AutosavedScene;
        if (!hasNew)
        {
            // The interval runs from the first change the copy does not have yet.
            m_LastAutosaveTime = now;
            return;
        }
        // Not while playing, while an edit is still held or before the latest change is an undo step.
        const bool busy = m_SceneState != SceneState::Edit || m_PendingUndo || IsEditInProgress();
        if (EditorAutosave::IsDue(m_Preferences.Autosave, now, m_LastAutosaveTime, hasNew, busy))
            WriteAutosave(false);
    }

    bool EditorLayer::WriteAutosave(bool manual)
    {
        if (IsInPrefabStage())
            return false;
        if (m_SceneState != SceneState::Edit || !m_EditorScene)
            return false;
        CommitUndo(); // the snapshot is the scene as it is now
        const std::shared_ptr<const std::string> scene = m_UndoHistory.GetCurrent().Scene;
        const fs::path path = GetAutosavePath();
        m_LastAutosaveTime = ImGui::GetTime();
        if (!scene || path.empty())
            return false;
        if (!EditorAutosave::WriteFileAtomic(path, *scene))
        {
            ZM_WARN("Avtomatik saqlanmadi: {}", path.string());
            return false;
        }
        m_AutosavedScene = scene;
        m_SessionAutosaves.insert(path.lexically_normal().string());
        m_AutosaveNotice = std::format(ICON_FA_FLOPPY_DISK "  Avtosaqlandi {}", ClockTime(GetUnixTime()));
        m_AutosaveNoticeTime = ImGui::GetTime();
        const std::string shown = path.lexically_relative(Project::GetProjectDirectory()).generic_string();
        if (manual)
            ZM_INFO("Sahnaning nusxasi saqlandi: {}", shown);
        else
            ZM_INFO("Sahna avtomatik saqlandi: {} (fayl o'zgarmadi)", shown);
        return true;
    }

    void EditorLayer::DiscardSessionAutosave()
    {
        const fs::path path = GetAutosavePath();
        if (path.empty())
            return;
        if (m_SessionAutosaves.erase(path.lexically_normal().string()) > 0)
        {
            std::error_code ec;
            fs::remove(path, ec);
        }
        m_AutosavedScene.reset();
    }

    // ------------------------------------------------------------------ recovery
    void EditorLayer::CheckAutosaveRecovery(const fs::path& scenePath)
    {
        if (!Project::GetActive())
            return;
        const std::string relative = scenePath.empty() ? std::string() : Project::GetAssetRelativePath(scenePath);
        const fs::path autosave = EditorAutosave::GetFilePath(Project::GetProjectDirectory(), relative);
        // The last session's copy first; else one put aside earlier (offered whatever the file's time).
        for (const bool kept : { false, true })
        {
            const fs::path path = kept ? EditorAutosave::GetKeptPath(autosave) : autosave;
            const EditorAutosave::RecoveryCheck check = EditorAutosave::InspectRecovery(path, scenePath);
            switch (EditorAutosave::DecideRecovery(check, kept))
            {
                case EditorAutosave::RecoveryDecision::None:
                    continue;
                case EditorAutosave::RecoveryDecision::Discard:
                {
                    // Older than the file (or the same): what it had was saved or thrown away since.
                    std::error_code ec;
                    fs::remove(path, ec);
                    continue;
                }
                case EditorAutosave::RecoveryDecision::Offer:
                    break;
            }
            m_Recovery = {};
            m_Recovery.Pending = true;
            m_Recovery.Autosave = path;
            m_Recovery.Kept = kept;
            m_Recovery.Scene = scenePath;
            m_Recovery.SceneName = scenePath.empty() ? std::string("Nomsiz sahna") : scenePath.stem().string();
            m_Recovery.AutosaveTime = check.AutosaveTime;
            m_Recovery.SceneTime = check.SceneTime;
            m_Recovery.SceneExists = check.SceneExists;
            m_Recovery.Crashed = m_PreviousSessionCrashed && !kept;
            if (kept)
                ZM_WARN("'{}' sahnasining keyinroqqa qoldirilgan nusxasi bor ({}) - tiklash mumkin", m_Recovery.SceneName, WhenText(check.AutosaveTime));
            else
                ZM_WARN("'{}' sahnasining avtomatik saqlangan nusxasi topildi ({}) - tiklash mumkin", m_Recovery.SceneName, WhenText(check.AutosaveTime));
            return;
        }
    }

    void EditorLayer::DeferAutosaveRecovery()
    {
        if (m_Recovery.Autosave.empty() || m_Recovery.Kept)
            return; // already put aside: it stays where it is
        // This session's copies of the scene go to the usual file and saving the scene deletes it:
        // the offered copy moves out of their way (a copy put aside before is replaced by this newer one).
        const fs::path kept = EditorAutosave::GetKeptPath(m_Recovery.Autosave);
        std::error_code ec;
        const bool replaced = fs::exists(kept, ec);
        fs::rename(m_Recovery.Autosave, kept, ec);
        if (ec)
        {
            ZM_WARN("Nusxa chetga olinmadi ({}): avtosaqlash yoki sahnani saqlash uni almashtirishi mumkin", ec.message());
            return;
        }
        m_SessionAutosaves.erase(m_Recovery.Autosave.lexically_normal().string());
        m_Recovery.Autosave = kept;
        m_Recovery.Kept = true;
        const std::string shown = Project::GetActive() ? kept.lexically_relative(Project::GetProjectDirectory()).generic_string() : kept.string();
        ZM_INFO("'{}' nusxasi keyinroqqa qoldirildi ({}): sahna keyingi ochilganda yana so'raladi", m_Recovery.SceneName, shown);
        if (replaced)
            ZM_WARN("Avvalroq qoldirilgan nusxa shu yangisi bilan almashtirildi: {}", shown);
    }

    void EditorLayer::DeleteOfferedAutosave()
    {
        if (m_Recovery.Autosave.empty())
            return;
        std::error_code ec;
        fs::remove(m_Recovery.Autosave, ec);
        m_SessionAutosaves.erase(m_Recovery.Autosave.lexically_normal().string());
        ZM_INFO("Avtomatik saqlangan nusxa o'chirildi: {}", m_Recovery.SceneName);
    }

    void EditorLayer::RecoverAutosave()
    {
        const std::string text = FileSystem::ReadTextFile(m_Recovery.Autosave);
        Ref<Scene> scene = CreateRef<Scene>();
        if (text.empty() || !SceneSerializer(scene).DeserializeFromString(text))
        {
            ZM_ERROR("Avtomatik saqlangan nusxa o'qilmadi: {}", m_Recovery.Autosave.string());
            return;
        }
        if (m_SceneState != SceneState::Edit)
            OnSceneStop();
        // The copy's scene must be the open one (another may have been opened since the prompt).
        std::error_code ec;
        const bool sameScene = m_Recovery.Scene.empty() ? m_EditorScenePath.empty() : fs::equivalent(m_Recovery.Scene, m_EditorScenePath, ec);
        if (!sameScene)
        {
            const AutosaveRecovery recovery = m_Recovery;
            if (recovery.Scene.empty())
                NewScene();
            else if (!OpenScene(recovery.Scene))
                return;
            m_Recovery = recovery;
            m_Recovery.Pending = false;
        }
        // The file's state stays the step before: undo goes back to it.
        CommitUndo();
        scene->ResolveAssets();
        m_EditorScene = scene;
        SetActiveScene(m_EditorScene, false);
        m_SceneDirty = true;
        m_AssetUsage.MarkDirty();
        m_UndoHistory.CommitEdit(TakeSnapshot(), CaptureUndoSelection(), "Avtosaqlashdan tiklandi", (double)GetUnixTime());
        m_UndoSelectionRevision = m_SceneHierarchyPanel.GetSelection().GetRevision();
        m_UndoHistoryScroll = true;
        // The copy now belongs to this session (written again with the next change, deleted on save);
        // one that was put aside goes back to the usual file for that.
        if (const fs::path usual = GetAutosavePath(); m_Recovery.Kept && !usual.empty())
        {
            fs::rename(m_Recovery.Autosave, usual, ec);
            if (ec)
                ZM_WARN("Qoldirilgan nusxa avtosaqlash fayliga ko'chirilmadi ({}): {}", ec.message(), m_Recovery.Autosave.string());
            else
            {
                m_Recovery.Autosave = usual;
                m_Recovery.Kept = false;
            }
        }
        m_SessionAutosaves.insert(m_Recovery.Autosave.lexically_normal().string());
        m_AutosavedScene = m_UndoHistory.GetCurrent().Scene;
        m_LastAutosaveTime = ImGui::GetTime();
        ZM_INFO("'{}' avtomatik saqlangan nusxadan tiklandi ({}) - faylga yozish uchun saqlang", m_Recovery.SceneName,
                WhenText(m_Recovery.AutosaveTime));
        ShowViewportMessage(ICON_FA_LIFE_RING "  Avtosaqlashdan tiklandi - saqlash uchun " + std::string(ShortcutLabel(EditorShortcut::SaveScene)));
        UpdateWindowTitle();
    }

    void EditorLayer::UI_AutosaveRecovery()
    {
        constexpr const char* id = ICON_FA_LIFE_RING "  Avtomatik saqlangan nusxa###AutosaveRecovery";
        if (m_Recovery.Pending && !m_ShowLauncher)
        {
            ImGui::OpenPopup("###AutosaveRecovery");
            m_Recovery.Pending = false;
        }
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (!ImGui::BeginPopupModal(Tr(id), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return;
        if (m_Recovery.CloseRequested)
        {
            m_Recovery.CloseRequested = false;
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }
        const float em = ImGui::GetFontSize();
        ImGui::PushTextWrapPos(em * 30.0f);
        if (m_Recovery.Crashed)
        {
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION "  Editor oldingi safar kutilmaganda yopilgan.");
            ImGui::Spacing();
        }
        if (m_Recovery.Kept)
        {
            ImGui::TextWrapped("'%s' sahnasining avvalroq \"Keyinroq\" bilan qoldirilgan nusxasi bor (saqlanmagan o'zgarishlar).",
                               m_Recovery.SceneName.c_str());
            if (m_Recovery.SceneExists && m_Recovery.SceneTime > m_Recovery.AutosaveTime)
                ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f),
                                   ICON_FA_TRIANGLE_EXCLAMATION "  Sahna fayli nusxadan keyin saqlangan: tiklash fayldagi keyingi o'zgarishlar "
                                   "o'rniga nusxani qo'yadi.");
        }
        else
            ImGui::TextWrapped("'%s' sahnasining saqlanmagan o'zgarishlari avtomatik saqlangan nusxada bor. Nusxa sahna faylidan yangiroq.",
                               m_Recovery.SceneName.c_str());
        ImGui::Spacing();
        if (ImGui::BeginTable("##recovery", 2, ImGuiTableFlags_SizingFixedFit))
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled(ICON_FA_LIFE_RING "  Nusxa");
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(WhenText(m_Recovery.AutosaveTime).c_str());
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled(ICON_FA_FILE "  Fayl");
            ImGui::TableSetColumnIndex(1);
            if (m_Recovery.SceneExists)
                ImGui::TextUnformatted(WhenText(m_Recovery.SceneTime).c_str());
            else
                ImGui::TextDisabled("hali saqlanmagan sahna");
            ImGui::EndTable();
        }
        const std::string shown = Project::GetActive() ? m_Recovery.Autosave.lexically_relative(Project::GetProjectDirectory()).generic_string()
                                                       : m_Recovery.Autosave.string();
        ImGui::TextDisabled("%s", shown.c_str());
        ImGui::Spacing();
        ImGui::TextDisabled("Tiklangan sahna saqlanmagan bo'ladi: undo fayldagi holatga qaytaradi.");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();

        const ImVec2 button(em * 9.5f, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, UI::AccentDarkColor(1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, UI::AccentMidColor());
        if (ImGui::Button(Tr(ICON_FA_LIFE_RING "  Tiklash"), button))
        {
            RecoverAutosave();
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor(2);
        ImGui::SameLine();
        if (ImGui::Button(Tr(ICON_FA_TRASH "  Nusxani o'chirish"), button))
        {
            DeleteOfferedAutosave();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(Tr("Keyinroq"), button))
        {
            DeferAutosaveRecovery();
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::IsItemHovered())
            UI::Tooltip("Nusxa chetga olinadi va qoladi: bu sessiyadagi avtosaqlash va sahnani saqlash unga tegmaydi, "
                        "sahna keyingi ochilganda yana so'raladi");
        ImGui::EndPopup();
    }

    // ------------------------------------------------------------------ the preferences' page
    void EditorLayer::UI_AutosavePreferences(bool& changed)
    {
        const float em = ImGui::GetFontSize();
        AutosaveSettings& autosave = m_Preferences.Autosave;
        ImGui::SeparatorText(Tr("Avtomatik saqlash"));
        changed |= ImGui::Checkbox(Tr("Saqlanmagan o'zgarishlarning nusxasini avtomatik yozish"), &autosave.Enabled);
        ImGui::BeginDisabled(!autosave.Enabled);
        ImGui::SetNextItemWidth(em * 14.0f);
        if (ImGui::SliderInt("Har necha daqiqada", &autosave.IntervalMinutes, AutosaveSettings::MinInterval, AutosaveSettings::MaxInterval, "%d daqiqa"))
        {
            autosave.Sanitize();
            changed = true;
        }
        ImGui::EndDisabled();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("Nusxa <loyiha>/.zamin/autosave ga yoziladi - sahna faylining o'zi o'zgarmaydi. Editor kutilmaganda "
                            "yopilsa, sahna keyingi ochilganda nusxadan tiklashni taklif qiladi.");
        ImGui::PopTextWrapPos();
        if (Project::GetActive())
        {
            const fs::path directory = EditorAutosave::GetDirectory(Project::GetProjectDirectory());
            const size_t count = CountAutosaves(directory);
            ImGui::Spacing();
            if (m_AutosaveNoticeTime > 0.0)
                ImGui::TextUnformatted(m_AutosaveNotice.c_str());
            else
                ImGui::TextDisabled(ICON_FA_CLOCK "  Bu sessiyada hali nusxa yozilmadi");
            ImGui::SameLine();
            ImGui::TextDisabled("|  papkada %zu ta nusxa", count);
            ImGui::BeginDisabled(m_SceneState != SceneState::Edit);
            if (ImGui::Button(Tr(ICON_FA_FLOPPY_DISK "  Hozir nusxa yozish")))
                WriteAutosave(true);
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button(Tr(ICON_FA_FOLDER_OPEN "  Papkani ochish")))
            {
                std::error_code ec;
                fs::create_directories(directory, ec);
                Platform::OpenInFileBrowser(directory);
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(count == 0);
            if (ImGui::Button(Tr(ICON_FA_TRASH "  Hammasini o'chirish")))
            {
                std::error_code ec;
                fs::remove_all(directory, ec);
                m_SessionAutosaves.clear();
                m_AutosavedScene.reset();
                ZM_INFO("Avtomatik saqlangan nusxalar o'chirildi");
            }
            ImGui::EndDisabled();
        }

        ImGui::SeparatorText(Tr("Undo"));
        changed |= ImGui::Checkbox(Tr("Tanlov o'zgarishlari ham undo qadami"), &m_Preferences.Undo.RecordSelection);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("Yoqiq: Ctrl+Z oldingi tanlovni qaytaradi. O'chiq: faqat sahna o'zgarishlari qadam bo'ladi "
                            "(ularning undo'si o'sha paytdagi tanlovni baribir qaytaradi).");
        ImGui::PopTextWrapPos();
        if (ImGui::Button(Tr(ICON_FA_CLOCK_ROTATE_LEFT "  Undo tarixi oynasi")))
        {
            m_ShowUndoHistory = true;
            m_UndoHistoryScroll = true;
        }

        ImGui::SeparatorText(Tr("Klaviatura"));
        ImGui::Text("%zu ta yorliq, %zu ta o'zgartirilgan", m_Shortcuts.GetAll().size(), m_Shortcuts.CountModified());
        if (size_t conflicts = m_Shortcuts.CountConflicts(); conflicts > 0)
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.35f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION "  %zu ta to'qnashuv", conflicts);
        }
        if (ImGui::Button(std::format(ICON_FA_KEYBOARD "  Klaviatura yorliqlari ({})", ShortcutLabel(EditorShortcut::ShortcutsWindow)).c_str()))
            m_ShowShortcuts = true;
    }

}
