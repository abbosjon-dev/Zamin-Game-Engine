// The external code editor: C++ scripts (the inspector's button, the content browser, the scene
// statistics, the Skriptlar menu) and compiler errors open in the user's own editor, at the line.
// The command is in the preferences (Tashqi muharrir); by default $VISUAL / $EDITOR, VS Code or
// xdg-open (see ExternalEditor).
#include "EditorLayer.h"
#include "UI.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <format>

namespace fs = std::filesystem;

namespace Zamin {

    bool EditorLayer::OpenInCodeEditor(const fs::path& file,int line,int column)
    {
        m_ShowCodeEditor=true;
        m_CodeEditorPanel.SetProject(Project::GetProjectDirectory(),Project::GetCacheDirectory());
        const bool opened=m_CodeEditorPanel.Open(file,line,column);
        if(!opened) ZM_WARN("Kod muharririda fayl ochilmadi: {}",file.string());
        return opened;
    }

    bool EditorLayer::OpenInExternalEditor(const fs::path& file, int line, int column)
    {
        const std::string& command = m_Preferences.ExternalEditor.Command;
        std::string error;
        if (!ExternalEditor::Open(command, file, line, column, &error))
        {
            ZM_ERROR("Tashqi muharrirda ochilmadi: {} ({})", file.filename().string(), error);
            ShowViewportMessage(ICON_FA_TRIANGLE_EXCLAMATION "  Tashqi muharrir ishga tushmadi - Sozlamalar > Tashqi muharrir");
            return false;
        }
        ZM_INFO("Tashqi muharrirda ochildi: {}{}", file.filename().string(), line > 0 ? std::format(":{}", line) : std::string());
        return true;
    }

    const SourceLocation& EditorLayer::GetScriptSource(const std::string& className)
    {
        // Files are edited outside the editor: what was found is looked up again after a while.
        const double now = ImGui::GetTime();
        if (now - m_ScriptSourcesTime > 5.0)
        {
            m_ScriptSources.clear();
            m_ScriptSourcesTime = now;
        }
        auto it = m_ScriptSources.find(className);
        if (it == m_ScriptSources.end())
        {
            SourceLocation found;
            if (Project::GetActive())
                found = ExternalEditor::FindScriptSource(Project::GetSourceDirectory(), className);
            it = m_ScriptSources.emplace(className, std::move(found)).first;
        }
        return it->second;
    }

    bool EditorLayer::OpenScriptClassSource(const std::string& className)
    {
        if (className.empty())
            return false;
        m_ScriptSourcesTime = -100.0; // look again now (the class may have just been written)
        const SourceLocation& source = GetScriptSource(className);
        if (!source.IsValid())
        {
            ZM_WARN("'{}' klassi Source papkasida topilmadi (ZM_REGISTER_SCRIPT({}) qidirildi)", className, className);
            return false;
        }
        return OpenInCodeEditor(source.File, source.Line, source.Column);
    }

    void EditorLayer::UI_ExternalEditorPreferences(bool& changed)
    {
        ExternalEditorPreferences& prefs = m_Preferences.ExternalEditor;
        ImGui::SeparatorText(Tr("Tashqi kod muharriri"));
        ImGui::TextWrapped("C++ skriptlar va kompilyatsiya xatolari ichki Kod muharririda ochiladi. "
                           "Undagi 'External editor' tugmasi uchun shu tashqi dasturni tanlang.");
        ImGui::Spacing();
        const std::string automatic = ExternalEditor::GetDefaultCommand();
        bool useDefault = prefs.Command.empty();
        if (ImGui::RadioButton(Tr("Avtomatik"), useDefault) && !useDefault)
        {
            prefs.Command.clear();
            changed = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", automatic.c_str());
        if (ImGui::IsItemHovered())
            UI::Tooltip("$VISUAL, so'ng $EDITOR (vim / nano kabi terminal muharrirlari terminal oynasida), bo'lmasa VS Code,\n"
                        "u ham bo'lmasa xdg-open (fayl turi uchun tizimdagi dastur, qatorsiz).");
        if (ImGui::RadioButton(Tr("O'z buyrug'im"), !useDefault) && useDefault)
        {
            prefs.Command = automatic;
            changed = true;
        }
        ImGui::BeginDisabled(useDefault);
        ImGui::SetNextItemWidth(-FLT_MIN);
        std::string draft = useDefault ? automatic : prefs.Command;
        if (ImGui::InputTextWithHint("##externalCommand", "code -g {file}:{line}:{col}", &draft) && !useDefault)
        {
            prefs.Command = draft;
            changed = true;
        }
        // The installed editors this knows, as quick choices.
        const std::vector<std::string> installed = ExternalEditor::GetInstalledCommands();
        if (!installed.empty())
        {
            ImGui::TextDisabled("O'rnatilgan:");
            for (const std::string& command : installed)
            {
                ImGui::SameLine();
                const std::string program = command.substr(0, command.find(' '));
                if (ImGui::SmallButton(program.c_str()))
                {
                    prefs.Command = command;
                    changed = true;
                }
                if (ImGui::IsItemHovered())
                    UI::Tooltip(command.c_str());
            }
        }
        ImGui::EndDisabled();
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("O'rinbosarlar: {file} fayl, {line} qator, {col} ustun, {dir} papka. {file} bo'lmasa fayl oxiriga qo'shiladi.");
        ImGui::TextWrapped("Misollar: subl {file}:{line}   |   kate -l {line} {file}   |   gedit +{line} {file}");
        ImGui::PopStyleColor();

        // What it would run, and a test with a file of the project.
        fs::path sample = Project::GetActive() ? Project::GetSourceDirectory() : fs::path();
        SourceLocation first;
        for (const auto& [name, info] : ScriptRegistry::GetAll())
            if ((first = GetScriptSource(name)).IsValid())
                break;
        if (!first.IsValid())
            first = { sample.empty() ? fs::path("Source/Game.cpp") : sample / "Game.cpp", 12, 5 };
        std::string preview;
        for (const std::string& arg : ExternalEditor::ExpandCommand(prefs.Command.empty() ? automatic : prefs.Command, first.File, first.Line, first.Column))
            preview += (preview.empty() ? "" : " ") + (arg.find(' ') != std::string::npos ? "'" + arg + "'" : arg);
        ImGui::Spacing();
        ImGui::Text("Ishga tushiriladi (%s, %d-qator):", first.File.filename().string().c_str(), first.Line);
        ImGui::PushStyleColor(ImGuiCol_Text, UI::AccentColor());
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(preview.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        ImGui::BeginDisabled(!fs::exists(first.File));
        if (ImGui::Button(Tr(ICON_FA_PLAY "  Sinab ko'rish")))
            m_ExternalEditorTest = OpenInExternalEditor(first.File, first.Line, first.Column)
                                       ? std::format(ICON_FA_CHECK "  {} ochildi", first.File.filename().string())
                                       : std::string(ICON_FA_TRIANGLE_EXCLAMATION "  Ishga tushmadi - konsolni ko'ring");
        ImGui::EndDisabled();
        if (!m_ExternalEditorTest.empty())
        {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", m_ExternalEditorTest.c_str());
        }
    }

}
