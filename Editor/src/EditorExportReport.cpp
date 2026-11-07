// The export report window: what an exported game's folder holds (the total, every kind of file with its
// share, the largest files), the assets left out, how the script module was made, what strip saved, and
// the warnings and errors of the export. It opens when an export ends; File > "Eksport hisoboti" opens
// the last one again (or the export-report.json of the build folder).
#include "EditorLayer.h"
#include "UI.h"

#include <Zamin/Utils/PlatformUtils.h>

#include <imgui.h>

#include <algorithm>
#include <format>

namespace Zamin {

    namespace fs = std::filesystem;

    namespace {
        // A colour per category (the bars of the window).
        ImVec4 CategoryColor(ExportCategory category)
        {
            switch (category)
            {
                case ExportCategory::Binaries: return ImVec4(0.42f, 0.62f, 0.95f, 1.0f);
                case ExportCategory::Textures: return ImVec4(0.93f, 0.62f, 0.30f, 1.0f);
                case ExportCategory::Models: return ImVec4(0.55f, 0.82f, 0.45f, 1.0f);
                case ExportCategory::Audio: return ImVec4(0.82f, 0.48f, 0.86f, 1.0f);
                case ExportCategory::Scenes: return ImVec4(0.36f, 0.80f, 0.80f, 1.0f);
                case ExportCategory::Fonts: return ImVec4(0.90f, 0.82f, 0.40f, 1.0f);
                case ExportCategory::Data: return ImVec4(0.62f, 0.66f, 0.72f, 1.0f);
                default: return ImVec4(0.45f, 0.47f, 0.52f, 1.0f);
            }
        }

        const char* CategoryIcon(ExportCategory category)
        {
            switch (category)
            {
                case ExportCategory::Binaries: return ICON_FA_MICROCHIP;
                case ExportCategory::Textures: return ICON_FA_IMAGE;
                case ExportCategory::Models: return ICON_FA_CUBE;
                case ExportCategory::Audio: return ICON_FA_MUSIC;
                case ExportCategory::Scenes: return ICON_FA_FILM;
                case ExportCategory::Fonts: return ICON_FA_FONT;
                case ExportCategory::Data: return ICON_FA_DATABASE;
                default: return ICON_FA_FILE;
            }
        }

        // A horizontal bar of `fraction` of `width` in `color` (the rest a dim track).
        void SizeBar(float fraction, const ImVec4& color, float width, float height)
        {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            ImDrawList* draw = ImGui::GetWindowDrawList();
            const float y = p.y + (ImGui::GetTextLineHeight() - height) * 0.5f;
            draw->AddRectFilled(ImVec2(p.x, y), ImVec2(p.x + width, y + height), ImGui::GetColorU32(ImGuiCol_FrameBg), height * 0.5f);
            if (fraction > 0.0f)
                draw->AddRectFilled(ImVec2(p.x, y), ImVec2(p.x + std::max(height, width * std::clamp(fraction, 0.0f, 1.0f)), y + height),
                                    ImGui::GetColorU32(color), height * 0.5f);
            ImGui::Dummy(ImVec2(width, ImGui::GetTextLineHeight()));
        }
    }

    void EditorLayer::TakeFinishedExportReport()
    {
        std::lock_guard<std::mutex> lock(m_ExportReportMutex);
        if (!m_PendingExportReport)
            return;
        m_ExportReport = std::move(*m_PendingExportReport);
        m_PendingExportReport.reset();
        m_HasExportReport = true;
        m_ShowExportReport = true;
    }

    bool EditorLayer::OpenExportReport(const fs::path& outputDirectory)
    {
        ExportReport report;
        if (!ExportReport::Read(outputDirectory, report))
            return false;
        m_ExportReport = std::move(report);
        m_HasExportReport = true;
        m_ShowExportReport = true;
        return true;
    }

    void EditorLayer::UI_ExportReportWindow()
    {
        if (!m_ShowExportReport || !m_HasExportReport)
            return;
        const ExportReport& r = m_ExportReport;
        ImGui::SetNextWindowSize(ImVec2(780, 790), ImGuiCond_FirstUseEver);
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f, viewport->WorkPos.y + viewport->WorkSize.y * 0.5f),
                                ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
        if (!ImGui::Begin(Tr(ICON_FA_ROCKET "  Eksport hisoboti###ExportReport"), &m_ShowExportReport, ImGuiWindowFlags_NoCollapse))
        {
            ImGui::End();
            return;
        }

        // ---- the head: the game, the result, the total
        ImGui::PushFont(ImGuiLayer::GetTitleFont());
        ImGui::Text("%s %s", r.Game.c_str(), r.Version.c_str());
        ImGui::PopFont();
        ImGui::SameLine();
        const bool clean = r.Succeeded && r.Warnings.empty();
        const ImVec4 green(0.45f, 0.85f, 0.55f, 1.0f), yellow(0.95f, 0.75f, 0.32f, 1.0f), red(0.95f, 0.45f, 0.40f, 1.0f);
        if (!r.Succeeded)
            ImGui::TextColored(red, ICON_FA_CIRCLE_EXCLAMATION "  %s (%zu)", Tr("Xatolar bilan"), r.Errors.size());
        else if (!clean)
            ImGui::TextColored(yellow, ICON_FA_TRIANGLE_EXCLAMATION "  %s (%zu)", Tr("Ogohlantirishlar bilan"), r.Warnings.size());
        else
            ImGui::TextColored(green, ICON_FA_CIRCLE_CHECK "  %s", Tr("Muvaffaqiyatli"));
        ImGui::TextDisabled("%s  |  %s  |  %.1f s", r.OutputDirectory.c_str(), r.Date.c_str(), r.Seconds);
        ImGui::Spacing();
        ImGui::Text("%s: %s", Tr("Jami"), ExportAssets::FormatSize(r.TotalBytes).c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("(%zu %s)", r.FileCount, Tr("ta fayl"));
        if (!r.ScriptModule.empty())
            ImGui::TextDisabled(ICON_FA_CODE "  %s: %s", Tr("Skript moduli"), r.ScriptModule.c_str());
        if (r.StrippedBytes > 0)
            ImGui::TextDisabled(ICON_FA_MICROCHIP "  strip: %s %s", Tr("binarlar kichraydi:"), ExportAssets::FormatSize(r.StrippedBytes).c_str());

        // ---- every kind of file, its share of the folder
        ImGui::SeparatorText(Tr("Turlar bo'yicha"));
        if (ImGui::BeginTable("##categories", 4, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg))
        {
            ImGui::TableSetupColumn("##name", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupColumn("##bar", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("##size", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupColumn("##files", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            // The largest first.
            std::vector<size_t> order;
            for (size_t i = 0; i < r.Categories.size(); i++)
                if (r.Categories[i].Files > 0)
                    order.push_back(i);
            std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return r.Categories[a].Bytes > r.Categories[b].Bytes; });
            for (size_t i : order)
            {
                const ExportCategory category = (ExportCategory)i;
                const ExportReport::CategoryTotal& total = r.Categories[i];
                const float fraction = r.TotalBytes ? (float)((double)total.Bytes / (double)r.TotalBytes) : 0.0f;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextColored(CategoryColor(category), "%s", CategoryIcon(category));
                ImGui::SameLine();
                ImGui::TextUnformatted(Tr(ExportReport::GetCategoryName(category)));
                ImGui::TableNextColumn();
                SizeBar(fraction, CategoryColor(category), ImGui::GetContentRegionAvail().x, 9.0f);
                ImGui::TableNextColumn();
                ImGui::Text("%10s  %5.1f%%", ExportAssets::FormatSize(total.Bytes).c_str(), fraction * 100.0f);
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%zu %s", total.Files, Tr("ta"));
            }
            ImGui::EndTable();
        }

        // ---- the largest files
        ImGui::SeparatorText(Tr("Eng katta fayllar"));
        if (ImGui::BeginTable("##largest", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV,
                              ImVec2(0.0f, std::min(8.0f, (float)r.Largest.size() + 1.0f) * ImGui::GetFrameHeight())))
        {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn(Tr("Hajmi"), ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableSetupColumn(Tr("Tur"), ImGuiTableColumnFlags_WidthFixed, 130.0f);
            ImGui::TableSetupColumn(Tr("Fayl"), ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            for (const ExportReportFile& file : r.Largest)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(ExportAssets::FormatSize(file.Size).c_str());
                ImGui::TableNextColumn();
                ImGui::TextColored(CategoryColor(file.Category), "%s", CategoryIcon(file.Category));
                ImGui::SameLine();
                ImGui::TextDisabled("%s", Tr(ExportReport::GetCategoryName(file.Category)));
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(file.Path.c_str());
            }
            ImGui::EndTable();
        }

        // ---- the assets left out
        const std::string excludedTitle = std::format("{} ({} {}, {})###excluded", Tr("Chiqarib tashlangan assetlar"), r.Excluded.size(), Tr("ta"),
                                                      ExportAssets::FormatSize(r.ExcludedBytes));
        if (ImGui::CollapsingHeader(excludedTitle.c_str(), r.Excluded.empty() ? ImGuiTreeNodeFlags_None : ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (r.Excluded.empty())
                ImGui::TextDisabled("%s", Tr("Hamma assetlar o'yinda ishlatiladi"));
            else if (ImGui::BeginTable("##excluded", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                                       ImVec2(0.0f, std::min(6.0f, (float)r.Excluded.size()) * ImGui::GetFrameHeight() + 4.0f)))
            {
                ImGui::TableSetupColumn("##size", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableSetupColumn("##path", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("##reason", ImGuiTableColumnFlags_WidthFixed, 260.0f);
                for (const ExcludedAsset& asset : r.Excluded)
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(ExportAssets::FormatSize(asset.Size).c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(asset.Path.c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextDisabled("%s", asset.Reason.c_str());
                }
                ImGui::EndTable();
            }
        }

        // ---- what went wrong
        const std::string problemsTitle = std::format("{} ({})###problems", Tr("Ogohlantirishlar va xatolar"), r.Warnings.size() + r.Errors.size());
        if (ImGui::CollapsingHeader(problemsTitle.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (r.Warnings.empty() && r.Errors.empty())
                ImGui::TextColored(green, ICON_FA_CIRCLE_CHECK "  %s", Tr("Ogohlantirish yo'q"));
            for (const std::string& error : r.Errors)
            {
                ImGui::TextColored(red, ICON_FA_CIRCLE_EXCLAMATION);
                ImGui::SameLine();
                ImGui::TextWrapped("%s", error.c_str());
            }
            for (const std::string& warning : r.Warnings)
            {
                ImGui::TextColored(yellow, ICON_FA_TRIANGLE_EXCLAMATION);
                ImGui::SameLine();
                ImGui::TextWrapped("%s", warning.c_str());
            }
        }

        // ---- buttons
        ImGui::Spacing();
        ImGui::Separator();
        if (ImGui::Button(Tr(ICON_FA_FOLDER_OPEN "  Papkani ochish")))
            Platform::OpenInFileBrowser(r.OutputDirectory);
        ImGui::SameLine();
        if (ImGui::Button(Tr(ICON_FA_FILE "  Hisobot fayli (.txt)")))
            Platform::OpenInFileBrowser(fs::path(r.OutputDirectory) / ExportReport::TextFileName);
        if (ImGui::IsItemHovered())
            UI::Tooltip(std::format("{}\n{}", (fs::path(r.OutputDirectory) / ExportReport::TextFileName).string(),
                                    (fs::path(r.OutputDirectory) / ExportReport::JsonFileName).string())
                            .c_str());
        ImGui::SameLine();
        if (ImGui::Button(Tr(ICON_FA_COPY "  Nusxalash")))
            ImGui::SetClipboardText(r.ToText().c_str());
        if (ImGui::IsItemHovered())
            UI::Tooltip(Tr("Hisobot matni buferga"));
        ImGui::SameLine(ImGui::GetWindowWidth() - 120.0f);
        if (ImGui::Button(Tr("Yopish"), ImVec2(100, 0)))
            m_ShowExportReport = false;
        ImGui::End();
    }

}
