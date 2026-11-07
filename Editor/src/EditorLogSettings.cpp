// The editor's log: the loggers' levels from the preferences (Sozlamalar > Log; at start-up the
// command line's --log-level / ZAMIN_LOG_LEVEL over them), the editor's own log files
// (~/.local/share/zamin/editor/logs: editor-<date>.log, the last N kept, latest.log) and the handler
// that keeps a failed ZM_ASSERT of a game script from taking the editor down.
#include "EditorLayer.h"
#include "UI.h"

#include <Zamin/Core/BuildInfo.h>
#include <Zamin/Core/SaveData.h>
#include <Zamin/Utils/FileSystem.h>
#include <Zamin/Utils/PlatformUtils.h>

#include <cstdlib>
#include <cstring>
#include <ctime>
#include <format>

namespace Zamin {

    namespace fs = std::filesystem;

    namespace {

        // A level picker: Trace ... Off with each one's name (true when changed).
        bool LevelCombo(const char* label, LogLevel& level, const char* tooltip)
        {
            bool changed = false;
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 11.0f);
            if (ImGui::BeginCombo(label, Log::LevelDisplayName(level)))
            {
                for (int l = (int)LogLevel::Trace; l <= (int)LogLevel::Off; l++)
                {
                    const LogLevel option = (LogLevel)l;
                    const std::string text = option == LogLevel::Off ? std::string(Log::LevelDisplayName(option))
                                                                     : std::format("{} va yuqori", Log::LevelDisplayName(option));
                    if (ImGui::Selectable(text.c_str(), option == level))
                    {
                        level = option;
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            if (tooltip && ImGui::IsItemHovered())
                UI::Tooltip(tooltip);
            return changed;
        }

    }

    fs::path EditorLayer::GetEditorLogDirectory()
    {
        return SaveData::GetDirectoryFor("", "editor") / "logs";
    }

    void EditorLayer::ApplyLogPreferences(bool startup)
    {
        const LogLevelSettings levels = startup ? m_StartupLogLevels.Over(m_Preferences.Logging.Levels) : m_Preferences.Logging.Levels;
        levels.Apply();
        if (startup && !levels.IsDefault())
            ZM_CORE_INFO("Log darajalari: {}{}", levels.Describe(),
                         m_StartupLogLevels.IsEmpty() ? "" : std::format(" (buyruq qatori / ZAMIN_LOG_LEVEL: {})", m_StartupLogLevels.ToSpec()));
        UpdateEditorLogFile();
    }

    void EditorLayer::UpdateEditorLogFile()
    {
        const LogPreferences& prefs = m_Preferences.Logging;
        if (!prefs.WriteFile)
        {
            if (LogFile::IsOpen())
            {
                ZM_CORE_INFO("Editor log fayli yopildi (Sozlamalar > Log)");
                LogFile::Close();
            }
            return;
        }
        if (LogFile::IsOpen())
            return;
        char started[32];
        const std::time_t now = std::time(nullptr);
        std::tm tm{};
        localtime_r(&now, &tm);
        std::strftime(started, sizeof(started), "%Y-%m-%d %H:%M:%S", &tm);
        const std::string header = std::format("# Zamin Editor\n# {}\n# Boshlandi: {}\n# Loyiha: {}\n", BuildInfo::Get().Describe(), started,
                                               m_StartupProject.empty() ? "-" : m_StartupProject.string());
        std::string error;
        if (LogFile::OpenRotating({ GetEditorLogDirectory(), "editor", prefs.KeepFiles, true, header }, &error))
            ZM_CORE_INFO("Editor log fayli: {}", LogFile::GetPath().string());
        else
            ZM_CORE_WARN("Editor log fayli ochilmadi ({}) - log faqat konsol va terminalda", error);
    }

    void EditorLayer::InstallAssertHandler()
    {
        // The game's failed asserts (ZM_ASSERT in its scripts) are logged as critical and the editor goes
        // on (a debugger attached: it stops there); the engine's own (ZM_CORE_ASSERT) keep the default.
        Assert::SetHandler([](const AssertInfo& info) {
            if (Assert::IsDebuggerAttached())
                return AssertAction::Break;
            if (!info.Core)
                return AssertAction::Continue;
            switch (Assert::GetMode())
            {
                case AssertMode::Continue: return AssertAction::Continue;
                case AssertMode::Break:    return AssertAction::Break;
                case AssertMode::Abort:
                case AssertMode::Auto:     return info.Fatal ? AssertAction::Abort : AssertAction::Continue;
            }
            return AssertAction::Continue;
        });
    }

    void EditorLayer::UI_LogPreferences(bool& changed)
    {
        LogPreferences& prefs = m_Preferences.Logging;
        ImGui::SeparatorText(Tr(ICON_FA_FILTER "  Log darajalari"));
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s", "Tanlangan darajadan pastdagi xabarlar umuman yozilmaydi: konsolda ham, terminalda ham, log faylida ham "
                                 "(yuqoridagi belgilar esa faqat konsolda ko'rsatishni tanlaydi).");
        ImGui::PopStyleColor();
        // The live levels (the command line may have set them at start-up); a change is kept.
        LogLevel engine = Log::GetLevel(LogSource::Engine), app = Log::GetLevel(LogSource::App);
        if (LevelCombo(Tr("Dvigatel (ZAMIN)"), engine, "Dvigatelning o'z xabarlari: ZM_CORE_INFO, ZM_CORE_WARN ..."))
        {
            prefs.Levels.Engine = engine;
            Log::SetLevel(LogSource::Engine, engine);
            changed = true;
        }
        if (LevelCombo(Tr("Editor va o'yin (APP)"), app, "Editor va o'yin skriptlarining xabarlari: ZM_INFO, ZM_WARN ..."))
        {
            prefs.Levels.App = app;
            Log::SetLevel(LogSource::App, app);
            changed = true;
        }
        if (!m_StartupLogLevels.IsEmpty())
            ImGui::TextColored(ImVec4(0.55f, 0.75f, 0.95f, 1.0f), ICON_FA_CIRCLE_INFO "  Ishga tushirishda --log-level / ZAMIN_LOG_LEVEL: %s",
                               m_StartupLogLevels.ToSpec().c_str());

        ImGui::SeparatorText(Tr(ICON_FA_FILE "  Log fayli"));
        if (ImGui::Checkbox(Tr("Log faylga yozish"), &prefs.WriteFile))
        {
            UpdateEditorLogFile();
            changed = true;
        }
        if (ImGui::IsItemHovered())
            UI::Tooltip("Har sessiya o'z faylida: editor-<sana>.log, latest.log - oxirgisi. Crash yoki xato bo'lganda nima bo'lganini "
                        "keyin o'qish mumkin");
        ImGui::BeginDisabled(!prefs.WriteFile);
        LogLevel file = Log::GetFileLevel();
        if (LevelCombo(Tr("Faylga yoziladigan daraja"), file, "Faylga faqat shu darajadagi va yuqoriroq xabarlar yoziladi (konsolda hammasi qoladi)"))
        {
            prefs.Levels.File = file;
            Log::SetFileLevel(file);
            changed = true;
        }
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 11.0f);
        if (ImGui::SliderInt(Tr("Saqlanadigan fayllar"), &prefs.KeepFiles, LogPreferences::MinKeepFiles, 30, "%d ta"))
        {
            prefs.Sanitize();
            changed = true;
        }
        if (ImGui::IsItemDeactivatedAfterEdit() && LogFile::IsOpen())
            LogFile::RemoveOldLogs(LogFile::GetDirectory(), "editor", prefs.KeepFiles);
        if (ImGui::IsItemHovered())
            UI::Tooltip("Eng eski editor-<sana>.log fayllari o'chiriladi (shu sessiyaniki ham hisobda)");
        ImGui::EndDisabled();

        const fs::path current = LogFile::GetPath();
        const std::string shown = FileSystem::ShortenHomePath(current.empty() ? GetEditorLogDirectory() : current);
        // The folder button first: the path may be long (it is cut at the window's edge, whole in the tooltip).
        std::error_code ec;
        const fs::path folder = current.empty() ? GetEditorLogDirectory() : current.parent_path();
        if (fs::is_directory(folder, ec))
        {
            if (ImGui::SmallButton(Tr(ICON_FA_FOLDER_OPEN "  Papkani ochish")))
                Platform::OpenInFileBrowser(folder);
            ImGui::SameLine();
        }
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s  %s", current.empty() ? ICON_FA_FOLDER : ICON_FA_FILE, current.empty() ? Tr("(yozilmayapti)") : shown.c_str());
        if (ImGui::IsItemHovered() && !current.empty())
            UI::Tooltip(current.string().c_str());
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s", "Player uchun: Project settings > Player > Log. Buyruq qatori: --log-level warn yoki "
                                 "engine=warn,app=info,file=error (ZAMIN_LOG_LEVEL ham) - ishga tushishda bulardan ustun.");
        ImGui::PopStyleColor();
    }

}
