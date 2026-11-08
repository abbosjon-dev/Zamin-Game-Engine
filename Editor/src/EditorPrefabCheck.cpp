// "Prefab'larni tekshirish" (Fayl menu, the Build dialog): the export's prefab check (PrefabExport,
// rules R1-R10 of docs/prefabs.md) run on its own - what an export would report about the prefabs the
// game gets, and which scenes / prefabs it would write without their prefab links (or bring up to date).
#include "EditorLayer.h"
#include "UI.h"

#include <imgui.h>

#include <format>

namespace fs = std::filesystem;

namespace Zamin {

    namespace {
        const char* RuleText(const std::string& rule)
        {
            if (rule == "R1") return "Nomlangan prefab yo'q";
            if (rule == "R2") return "Prefab o'qilmadi";
            if (rule == "R3") return "Yangiroq format";
            if (rule == "R4") return "ID / ildiz / ierarxiya";
            if (rule == "R5") return "Noma'lum komponent";
            if (rule == "R6") return "Tashqi havola";
            if (rule == "R7") return "Asset topilmadi";
            if (rule == "R8") return "Bog'langan prefab topilmadi";
            if (rule == "R9") return "Prefab sikli";
            if (rule == "R10") return "Eskirgan o'zgarish / bog'lanmagan nusxa";
            return "Ma'lumot";
        }
    }

    void EditorLayer::RunPrefabCheck()
    {
        if (!Project::GetActive())
            return;
        AssetReferenceIndex index;
        const ExportAssetPlan plan = ExportAssets::PlanForActiveProject(nullptr, nullptr, &index);
        m_PrefabCheck = PrefabExport::Prepare(plan, m_PrefabDatabase, &index);
        m_PrefabCheckDone = true;
        m_ShowPrefabCheck = true;
        const bool strict = Project::GetActive()->GetConfig().Export.PrefabWarningsAsErrors;
        for (const PrefabExportIssue& issue : m_PrefabCheck.Issues)
        {
            const std::string line = issue.File.empty() ? issue.Message : std::format("{} ({}): {}", issue.File, issue.Rule, issue.Message);
            if (issue.Level == PrefabExportIssue::Severity::Error || (strict && issue.Level == PrefabExportIssue::Severity::Warning))
                ZM_ERROR("Prefab tekshiruvi: {}", line);
            else if (issue.Level == PrefabExportIssue::Severity::Warning)
                ZM_WARN("Prefab tekshiruvi: {}", line);
            else
                ZM_INFO("Prefab tekshiruvi: {}", line);
        }
        ZM_INFO("Prefab tekshiruvi: {}", m_PrefabCheck.Summary());
    }

    void EditorLayer::UI_PrefabCheckWindow()
    {
        if (!m_ShowPrefabCheck || !m_PrefabCheckDone)
            return;
        const PrefabExportResult& r = m_PrefabCheck;
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowSize(ImVec2(std::min(940.0f, viewport->WorkSize.x - 40.0f), std::min(520.0f, viewport->WorkSize.y - 30.0f)), ImGuiCond_Appearing);
        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f, viewport->WorkPos.y + viewport->WorkSize.y * 0.5f),
                                ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (!ImGui::Begin(Tr(ICON_FA_BOX "  Prefab tekshiruvi###PrefabCheck"), &m_ShowPrefabCheck, ImGuiWindowFlags_NoCollapse))
        {
            ImGui::End();
            return;
        }
        const ImVec4 green(0.45f, 0.85f, 0.55f, 1.0f), yellow(0.95f, 0.75f, 0.32f, 1.0f), red(0.95f, 0.45f, 0.40f, 1.0f), blue(0.365f, 0.663f, 1.0f, 1.0f);
        const bool strict = Project::GetActive() && Project::GetActive()->GetConfig().Export.PrefabWarningsAsErrors;
        const size_t errors = r.Count(PrefabExportIssue::Severity::Error), warnings = r.Count(PrefabExportIssue::Severity::Warning);

        // ---- the head: the result, what an export would do with the files
        ImGui::PushFont(ImGuiLayer::GetTitleFont());
        if (r.HasErrors(strict))
            ImGui::TextColored(red, ICON_FA_CIRCLE_EXCLAMATION "  %s", Tr("Eksport xato bilan tugaydi"));
        else if (warnings)
            ImGui::TextColored(yellow, ICON_FA_TRIANGLE_EXCLAMATION "  %s", Tr("Ogohlantirishlar bor"));
        else
            ImGui::TextColored(green, ICON_FA_CIRCLE_CHECK "  %s", Tr("Prefablar eksportga tayyor"));
        ImGui::PopFont();
        ImGui::Text("%zu %s  |  %zu %s  |  %zu %s", r.PrefabsChecked, Tr("ta prefab tekshirildi"), errors, Tr("xato"), warnings, Tr("ogohlantirish"));
        if (strict)
        {
            ImGui::SameLine();
            ImGui::TextColored(yellow, "(%s)", Tr("ogohlantirishlar xato hisoblanadi"));
        }
        ImGui::TextColored(blue, ICON_FA_FILE_EXPORT "  %zu %s", r.Stripped, Tr("ta sahna/prefab prefab bog'lanishlarisiz yoziladi"));
        if (ImGui::IsItemHovered())
            UI::Tooltip(Tr("Bog'lanishlar (Editor: Prefab, PrefabEntity, Overrides ...) faqat editorniki: eksport nusxasida olib tashlanadi, "
                           "loyihadagi fayllar o'zgarmaydi"));
        if (r.Updated)
            ImGui::TextColored(blue, ICON_FA_ROTATE "  %zu %s", r.Updated, Tr("ta faylda eskirgan nusxalar eksportda yangilanadi"));
        ImGui::Spacing();

        // ---- what was found
        ImGui::SeparatorText(Tr("Topilganlar"));
        if (r.Issues.empty())
            ImGui::TextDisabled("%s", Tr("Hech qanday muammo topilmadi"));
        else if (ImGui::BeginTable("##prefabIssues", 4, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                                                            ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable,
                                   ImVec2(0.0f, ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() - 6.0f)))
        {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("##level", ImGuiTableColumnFlags_WidthFixed, 22.0f);
            ImGui::TableSetupColumn(Tr("Qoida"), ImGuiTableColumnFlags_WidthFixed, 235.0f);
            ImGui::TableSetupColumn(Tr("Fayl"), ImGuiTableColumnFlags_WidthFixed, 190.0f);
            ImGui::TableSetupColumn(Tr("Tafsilot"), ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            for (size_t i = 0; i < r.Issues.size(); i++)
            {
                const PrefabExportIssue& issue = r.Issues[i];
                ImGui::PushID((int)i);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                const bool error = issue.Level == PrefabExportIssue::Severity::Error;
                const bool warning = issue.Level == PrefabExportIssue::Severity::Warning;
                if (error)
                    ImGui::TextColored(red, ICON_FA_CIRCLE_EXCLAMATION);
                else if (warning)
                    ImGui::TextColored(strict ? red : yellow, ICON_FA_TRIANGLE_EXCLAMATION);
                else
                    ImGui::TextColored(blue, ICON_FA_CIRCLE_INFO);
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", issue.Rule.c_str());
                ImGui::SameLine();
                ImGui::TextUnformatted(Tr(RuleText(issue.Rule)));
                ImGui::TableNextColumn();
                // The file: a click shows it in the content browser (an asset; code files are not there).
                const fs::path asset = issue.File.empty() ? fs::path() : Project::GetAssetFileSystemPath(issue.File);
                std::error_code ec;
                if (!issue.File.empty() && fs::is_regular_file(asset, ec))
                {
                    if (ImGui::Selectable(issue.File.c_str(), false, ImGuiSelectableFlags_SpanAllColumns))
                        RevealAsset(asset);
                    if (ImGui::IsItemHovered())
                        UI::Tooltip(Tr("Kontent brauzerda ko'rsatish"));
                }
                else
                    ImGui::TextUnformatted(issue.File.c_str());
                ImGui::TableNextColumn();
                ImGui::TextWrapped("%s", issue.Message.c_str());
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (ImGui::Button(Tr(ICON_FA_ROTATE "  Qayta tekshirish")))
            RunPrefabCheck();
        ImGui::SameLine();
        if (ImGui::Button(Tr("Yopish")))
            m_ShowPrefabCheck = false;
        ImGui::End();
    }

}
