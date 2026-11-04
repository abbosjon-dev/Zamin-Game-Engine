#include "ConsolePanel.h"
#include "../UI.h"

#include <Zamin/Core/Log.h>

namespace Zamin {

    void ConsolePanel::OnImGuiRender(bool* open)
    {
        if (!*open)
            return;
        if (!ImGui::Begin(ICON_FA_TERMINAL "  Konsol###Console", open))
        {
            ImGui::End();
            return;
        }

        int counts[5] = {};
        Log::ForEach([&](const LogMessage& m) { counts[(int)m.Level]++; });

        if (UI::ToolbarButton(ICON_FA_ERASER "  Tozalash", "Konsolni tozalash"))
            Log::Clear();
        ImGui::SameLine();
        ImGui::Checkbox("Avto-skroll", &m_AutoScroll);
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();

        auto filterToggle = [](const char* label, bool& value, ImVec4 color, int count) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%s %d", label, count);
            ImGui::PushStyleColor(ImGuiCol_Text, value ? color : ImVec4(0.45f, 0.45f, 0.5f, 1.0f));
            if (UI::ToolbarButton(buf, nullptr, value))
                value = !value;
            ImGui::PopStyleColor();
            ImGui::SameLine();
        };
        filterToggle(ICON_FA_BUG, m_ShowTrace, ImVec4(0.7f, 0.7f, 0.75f, 1.0f), counts[0]);
        filterToggle(ICON_FA_CIRCLE_INFO, m_ShowInfo, ImVec4(0.55f, 0.85f, 0.65f, 1.0f), counts[1]);
        filterToggle(ICON_FA_TRIANGLE_EXCLAMATION, m_ShowWarn, ImVec4(0.95f, 0.78f, 0.3f, 1.0f), counts[2]);
        filterToggle(ICON_FA_CIRCLE_XMARK, m_ShowError, ImVec4(0.95f, 0.4f, 0.4f, 1.0f), counts[3] + counts[4]);
        ImGui::NewLine();
        ImGui::Separator();

        ImGui::BeginChild("##log", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
        if (ImGui::BeginTable("##logtable", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit))
        {
            ImGui::TableSetupColumn("time", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("src", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("msg", ImGuiTableColumnFlags_WidthStretch);
            Log::ForEach([&](const LogMessage& m) {
                bool show = (m.Level == LogLevel::Trace && m_ShowTrace) || (m.Level == LogLevel::Info && m_ShowInfo) ||
                            (m.Level == LogLevel::Warn && m_ShowWarn) || (m.Level >= LogLevel::Error && m_ShowError);
                if (!show) return;
                ImVec4 color;
                const char* icon;
                switch (m.Level)
                {
                    case LogLevel::Trace: color = ImVec4(0.6f, 0.62f, 0.66f, 1.0f); icon = ICON_FA_BUG; break;
                    case LogLevel::Info:  color = ImVec4(0.86f, 0.88f, 0.9f, 1.0f); icon = ICON_FA_CIRCLE_INFO; break;
                    case LogLevel::Warn:  color = ImVec4(0.95f, 0.78f, 0.3f, 1.0f); icon = ICON_FA_TRIANGLE_EXCLAMATION; break;
                    default:              color = ImVec4(0.98f, 0.42f, 0.42f, 1.0f); icon = ICON_FA_CIRCLE_XMARK; break;
                }
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(color, "%s", icon);
                ImGui::SameLine();
                ImGui::TextDisabled("%s", m.Time.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextDisabled("%s", m.FromClient ? "APP" : "ZAMIN");
                ImGui::TableSetColumnIndex(2);
                ImGui::PushStyleColor(ImGuiCol_Text, color);
                ImGui::TextUnformatted(m.Text.c_str());
                ImGui::PopStyleColor();
            });
            ImGui::EndTable();
        }
        size_t total = Log::GetMessageCount();
        if (m_AutoScroll && total != m_LastCount)
            ImGui::SetScrollHereY(1.0f);
        m_LastCount = total;
        ImGui::EndChild();
        ImGui::End();
    }

}
