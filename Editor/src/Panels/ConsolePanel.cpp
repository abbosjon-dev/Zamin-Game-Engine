#include "ConsolePanel.h"
#include "../UI.h"

#include <imgui_stdlib.h>

#include <algorithm>
#include <cstdio>
#include <string_view>

namespace Zamin {

    namespace {
        struct LevelStyle
        {
            ImVec4 Color;
            const char* Icon;
        };

        LevelStyle StyleOf(LogLevel level)
        {
            switch (level)
            {
                case LogLevel::Trace: return { ImVec4(0.6f, 0.62f, 0.66f, 1.0f), ICON_FA_BUG };
                case LogLevel::Info:  return { ImVec4(0.86f, 0.88f, 0.9f, 1.0f), ICON_FA_CIRCLE_INFO };
                case LogLevel::Warn:  return { ImVec4(0.95f, 0.78f, 0.3f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION };
                default:              return { ImVec4(0.98f, 0.42f, 0.42f, 1.0f), ICON_FA_CIRCLE_XMARK };
            }
        }

        // The first line of a message, and how many more it has.
        std::string_view FirstLine(const std::string& text, int& moreLines)
        {
            moreLines = 0;
            size_t end = text.find('\n');
            if (end == std::string::npos)
                return text;
            for (size_t i = end; i < text.size(); i++)
                if (text[i] == '\n' && i + 1 < text.size())
                    moreLines++;
            return std::string_view(text).substr(0, end);
        }

        // A rounded count badge ("12") at `pos`, `height` high; returns its width.
        float CountBadge(ImDrawList* dl, ImVec2 pos, float height, uint32_t count, ImU32 color)
        {
            char text[16];
            std::snprintf(text, sizeof(text), "%u", count);
            const ImVec2 size = ImGui::CalcTextSize(text);
            const float width = std::max(height, size.x + height * 0.7f);
            dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), color, height * 0.5f);
            dl->AddText(ImVec2(pos.x + (width - size.x) * 0.5f, pos.y + (height - size.y) * 0.5f), IM_COL32(240, 242, 245, 255), text);
            return width;
        }
    }

    bool ConsolePanel::ConsumePreferencesChanged()
    {
        bool changed = m_PrefsChanged;
        m_PrefsChanged = false;
        return changed;
    }

    bool ConsolePanel::ConsumeLogLevelsChanged()
    {
        bool changed = m_LogLevelsChanged;
        m_LogLevelsChanged = false;
        return changed;
    }

    void ConsolePanel::ApplyAutomation(const std::string& spec)
    {
        std::string_view rest(spec);
        while (!rest.empty())
        {
            size_t end = rest.find(';');
            std::string_view part = rest.substr(0, end);
            rest = end == std::string_view::npos ? std::string_view() : rest.substr(end + 1);
            if (part.starts_with("filter="))
                m_FilterText = std::string(part.substr(7));
            else if (part == "regex")
                m_Regex = true;
            else if (part == "case")
                m_CaseSensitive = true;
            else if (part == "collapse")
                m_Prefs.Collapse = true;
            else if (part == "trace")
                m_Prefs.ShowTrace = true;
            else if (part == "notime")
                m_Prefs.ShowTimestamps = false;
            else if (part.starts_with("select="))
                m_AutomationSelect = std::atoi(std::string(part.substr(7)).c_str());
            else if (part == "options")
                m_OpenOptions = m_FocusWindow = true; // (a docked console behind another tab comes to the front)
        }
    }

    ConsoleFilterSettings ConsolePanel::GetFilter() const
    {
        ConsoleFilterSettings filter;
        filter.Text = m_FilterText;
        filter.Regex = m_Regex;
        filter.CaseSensitive = m_CaseSensitive;
        filter.ShowTrace = m_Prefs.ShowTrace;
        filter.ShowInfo = m_Prefs.ShowInfo;
        filter.ShowWarn = m_Prefs.ShowWarn;
        filter.ShowError = m_Prefs.ShowError;
        filter.Collapse = m_Prefs.Collapse;
        return filter;
    }

    void ConsolePanel::RefreshView()
    {
        const uint64_t revision = Log::GetRevision();
        const ConsoleFilterSettings filter = GetFilter();
        if (revision == m_Revision && filter == m_ViewFilter)
            return;
        if (revision != m_Revision)
        {
            m_Messages = Log::GetMessages(&m_FirstIndex);
            m_Revision = revision;
        }
        m_Matcher.Set(filter.Text, filter.Regex, filter.CaseSensitive);
        m_View = BuildConsoleView(m_Messages, filter, m_Matcher);
        m_ViewFilter = filter;
        if (m_AutomationSelect >= 0 && m_AutomationSelect < (int)m_View.Rows.size())
        {
            m_Selected = m_FirstIndex + m_View.Rows[m_AutomationSelect].First;
            m_AutomationSelect = -1;
            m_ScrollToSelected = true;
        }
    }

    void ConsolePanel::CopyRows(const std::vector<size_t>& rows, bool textOnly)
    {
        std::string text;
        for (size_t r : rows)
        {
            if (r >= m_View.Rows.size())
                continue;
            const ConsoleRow& row = m_View.Rows[r];
            const LogMessage& m = m_Messages[row.Last];
            if (!text.empty())
                text += '\n';
            text += textOnly ? m.Text : FormatConsoleMessage(m, m_Prefs.ShowTimestamps, row.Count);
        }
        ImGui::SetClipboardText(text.c_str());
    }

    void ConsolePanel::OnImGuiRender(bool* open)
    {
        m_Focused = false;
        if (!*open)
            return;
        if (m_FocusWindow)
        {
            ImGui::SetNextWindowFocus();
            m_FocusWindow = false;
        }
        if (!ImGui::Begin(Tr(ICON_FA_TERMINAL "  Konsol###Console"), open))
        {
            ImGui::End();
            return;
        }
        m_Focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        RefreshView();
        DrawToolbar();
        RefreshView(); // the toolbar may have changed the filter

        // The selected message's details take the bottom of the window.
        int selectedRow = -1;
        for (size_t i = 0; i < m_View.Rows.size() && m_Selected != UINT64_MAX; i++)
            if (m_FirstIndex + m_View.Rows[i].First == m_Selected)
            {
                selectedRow = (int)i;
                break;
            }
        const float detailsHeight = selectedRow >= 0 ? std::min(ImGui::GetTextLineHeightWithSpacing() * 6.0f + 12.0f,
                                                                ImGui::GetContentRegionAvail().y * 0.45f)
                                                     : 0.0f;
        if (ImGui::BeginChild("##log", ImVec2(0, -detailsHeight), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar))
            DrawMessages();
        ImGui::EndChild();
        if (selectedRow >= 0)
            DrawDetails(detailsHeight);

        // Ctrl+C copies the selected message (the search field copies its own text).
        if (selectedRow >= 0 && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput &&
            ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false))
            CopyRows({ (size_t)selectedRow }, false);
        ImGui::End();
    }

    void ConsolePanel::DrawToolbar()
    {
        if (UI::ToolbarButton(Tr(ICON_FA_ERASER "  Tozalash"), Tr("Konsolni tozalash")))
        {
            Log::Clear();
            m_Selected = UINT64_MAX;
        }
        ImGui::SameLine();
        if (UI::ToolbarButton(ICON_FA_LAYER_GROUP, m_Prefs.Collapse ? "Bir xil xabarlar guruhlangan (bosing: hammasi alohida)"
                                                                    : "Bir xil xabarlarni guruhlash (soni bilan)", m_Prefs.Collapse))
        {
            m_Prefs.Collapse = !m_Prefs.Collapse;
            m_PrefsChanged = true;
        }
        ImGui::SameLine();
        if (UI::ToolbarButton(ICON_FA_CLOCK, m_Prefs.ShowTimestamps ? "Vaqt ko'rsatilmoqda (bosing: yashirish)" : "Vaqtni ko'rsatish",
                              m_Prefs.ShowTimestamps))
        {
            m_Prefs.ShowTimestamps = !m_Prefs.ShowTimestamps;
            m_PrefsChanged = true;
        }
        ImGui::SameLine();
        // The gear is lit while a logger drops messages below its level (they are not even kept).
        const LogLevel engineLevel = Log::GetLevel(LogSource::Engine), appLevel = Log::GetLevel(LogSource::App);
        const bool dropping = engineLevel != LogLevel::Trace || appLevel != LogLevel::Trace;
        const std::string gearTip = dropping ? std::format("Konsol sozlamalari\nYoziladi: dvigatel - {}, editor/o'yin - {} va yuqori",
                                                           Log::LevelDisplayName(engineLevel), Log::LevelDisplayName(appLevel))
                                             : std::string("Konsol sozlamalari (log darajalari ham)");
        // (Automation keeps it open: other windows taking the focus at start-up would close it.)
        if (UI::ToolbarButton(ICON_FA_GEAR, gearTip.c_str(), dropping) || (m_OpenOptions && !ImGui::IsPopupOpen("##ConsoleOptions")))
            ImGui::OpenPopup("##ConsoleOptions");
        if (ImGui::BeginPopup("##ConsoleOptions"))
        {
            m_PrefsChanged |= ImGui::Checkbox(Tr("Avto-skroll (yangi xabarga)"), &m_Prefs.AutoScroll);
            m_PrefsChanged |= ImGui::Checkbox(Tr("O'yin boshlanganda tozalash (Play)"), &m_Prefs.ClearOnPlay);
            m_PrefsChanged |= ImGui::Checkbox(Tr("Vaqtni ko'rsatish"), &m_Prefs.ShowTimestamps);
            m_PrefsChanged |= ImGui::Checkbox(Tr("Bir xil xabarlarni guruhlash"), &m_Prefs.Collapse);
            ImGui::Separator();
            ImGui::Checkbox(Tr("Qidiruv: regex (muntazam ifoda)"), &m_Regex);
            ImGui::Checkbox(Tr("Qidiruv: katta-kichik harf farqi"), &m_CaseSensitive);
            // What is written at all (below the level: not in the console, the terminal or the file).
            ImGui::SeparatorText(Tr("Yoziladigan darajalar"));
            auto levelMenu = [&](const char* label, LogSource source) {
                const LogLevel current = Log::GetLevel(source);
                ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.0f);
                if (ImGui::BeginCombo(label, Log::LevelDisplayName(current)))
                {
                    for (int l = (int)LogLevel::Trace; l <= (int)LogLevel::Off; l++)
                        if (ImGui::Selectable(Log::LevelDisplayName((LogLevel)l), l == (int)current))
                        {
                            Log::SetLevel(source, (LogLevel)l);
                            m_LogLevelsChanged = true;
                        }
                    ImGui::EndCombo();
                }
            };
            levelMenu(Tr("Dvigatel (ZAMIN)"), LogSource::Engine);
            levelMenu(Tr("Editor va o'yin (APP)"), LogSource::App);
            ImGui::TextDisabled("%s", Tr("Pastrog'i yozilmaydi; log fayli: Sozlamalar > Log"));
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();

        auto levelToggle = [&](const char* icon, bool& value, ImVec4 color, size_t count, const char* tip) {
            char label[64];
            std::snprintf(label, sizeof(label), "%s %zu", icon, count);
            ImGui::PushStyleColor(ImGuiCol_Text, value ? color : ImVec4(0.45f, 0.45f, 0.5f, 1.0f));
            if (UI::ToolbarButton(label, tip, value))
            {
                value = !value;
                m_PrefsChanged = true;
            }
            ImGui::PopStyleColor();
            ImGui::SameLine();
        };
        const auto& counts = m_View.LevelCounts;
        levelToggle(ICON_FA_BUG, m_Prefs.ShowTrace, ImVec4(0.7f, 0.7f, 0.75f, 1.0f), counts[(int)LogLevel::Trace], "Trace xabarlari");
        levelToggle(ICON_FA_CIRCLE_INFO, m_Prefs.ShowInfo, ImVec4(0.55f, 0.85f, 0.65f, 1.0f), counts[(int)LogLevel::Info], "Ma'lumot xabarlari");
        levelToggle(ICON_FA_TRIANGLE_EXCLAMATION, m_Prefs.ShowWarn, ImVec4(0.95f, 0.78f, 0.3f, 1.0f), counts[(int)LogLevel::Warn], "Ogohlantirishlar");
        levelToggle(ICON_FA_CIRCLE_XMARK, m_Prefs.ShowError, ImVec4(0.95f, 0.4f, 0.4f, 1.0f), m_View.ErrorCount(), "Xatolar");

        // The search: the rest of the row, with a clear button (while it has text) and the regex /
        // case toggles after it.
        const bool filtering = !m_FilterText.empty();
        const float toggleWidth = ImGui::GetFrameHeight() + 6.0f;
        const float searchWidth = ImGui::GetContentRegionAvail().x - (toggleWidth + 2.0f) * (filtering ? 3.0f : 2.0f);
        if (searchWidth > 80.0f)
        {
            const bool invalid = !m_Matcher.IsValid() && m_Regex && filtering;
            ImGui::PushStyleColor(ImGuiCol_Border, invalid ? ImVec4(0.95f, 0.35f, 0.35f, 1.0f) : UI::AccentColor(0.8f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, invalid ? 1.5f : filtering ? 1.0f : 0.0f);
            ImGui::SetNextItemWidth(searchWidth);
            const char* hint = m_Regex ? ICON_FA_MAGNIFYING_GLASS "  Regex (masalan: ^Sahna|xato\\s+\\d+)"
                                       : ICON_FA_MAGNIFYING_GLASS "  Xabarlardan qidirish...";
            ImGui::InputTextWithHint("##consoleFilter", Tr(hint), &m_FilterText);
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
            if (invalid && ImGui::IsItemHovered())
                UI::Tooltip(("Regex xato: " + m_Matcher.GetError()).c_str());
            else if (filtering && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                UI::Tooltip(std::format("Filtrga mos: {} ta qator", m_View.Rows.size()).c_str());
        }
        else
            ImGui::NewLine();
        if (filtering)
        {
            ImGui::SameLine(0.0f, 2.0f);
            if (UI::ToolbarButton(ICON_FA_XMARK "##clearFilter", "Qidiruvni tozalash", false, ImVec2(toggleWidth, 0)))
                m_FilterText.clear();
        }
        ImGui::SameLine(0.0f, 2.0f);
        if (UI::ToolbarButton(".*", m_Regex ? "Regex qidiruv yoqilgan" : "Regex qidiruv (muntazam ifoda)", m_Regex, ImVec2(toggleWidth, 0)))
            m_Regex = !m_Regex;
        ImGui::SameLine(0.0f, 2.0f);
        if (UI::ToolbarButton("Aa", m_CaseSensitive ? "Katta-kichik harf farq qiladi" : "Katta-kichik harf farq qilmaydi",
                              m_CaseSensitive, ImVec2(toggleWidth, 0)))
            m_CaseSensitive = !m_CaseSensitive;
        ImGui::Separator();
    }

    void ConsolePanel::DrawMessages()
    {
        // New messages scroll the list down only when it was at the bottom (reading older ones stays).
        const uint64_t total = Log::GetTotalWritten();
        if (total != m_LastTotal && m_Prefs.AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 2.0f)
            m_ScrollToBottom = true;
        m_LastTotal = total;

        if (m_View.Rows.empty())
        {
            ImGui::Spacing();
            if (m_Messages.empty())
                ImGui::TextDisabled("  Xabarlar yo'q");
            else
                ImGui::TextDisabled("  Filtrga mos xabar yo'q (%zu ta xabar yashirilgan)", m_Messages.size());
            return;
        }

        const bool collapse = m_ViewFilter.Collapse;
        const int columns = collapse ? 4 : 3;
        if (!ImGui::BeginTable("##logTable", columns, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit))
            return;
        ImGui::TableSetupColumn("lvl", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("src", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("msg", ImGuiTableColumnFlags_WidthStretch);
        if (collapse)
            ImGui::TableSetupColumn("count", ImGuiTableColumnFlags_WidthFixed);

        std::vector<size_t> contextRow;
        ImGuiListClipper clipper;
        clipper.Begin((int)m_View.Rows.size());
        int scrollTo = -1;
        if (m_ScrollToSelected)
        {
            for (size_t i = 0; i < m_View.Rows.size(); i++)
                if (m_FirstIndex + m_View.Rows[i].First == m_Selected)
                    scrollTo = (int)i;
            if (scrollTo >= 0)
                clipper.IncludeItemByIndex(scrollTo);
            m_ScrollToSelected = false;
        }
        while (clipper.Step())
        {
            for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; r++)
            {
                const ConsoleRow& row = m_View.Rows[r];
                const LogMessage& first = m_Messages[row.First];
                const LogMessage& latest = m_Messages[row.Last];
                const LevelStyle style = StyleOf(first.Level);
                const uint64_t id = m_FirstIndex + row.First;
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(r);
                const bool selected = id == m_Selected;
                if (ImGui::Selectable("##row", selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap))
                    m_Selected = selected ? UINT64_MAX : id;
                if (r == scrollTo)
                {
                    ImGui::SetScrollHereY(0.5f);
                    m_ScrollToBottom = false;
                }
                if (ImGui::BeginPopupContextItem("##rowMenu"))
                {
                    m_Selected = id;
                    if (ImGui::MenuItem(Tr(ICON_FA_COPY "  Nusxalash"), "Ctrl+C")) CopyRows({ (size_t)r }, false);
                    if (ImGui::MenuItem(Tr(ICON_FA_COPY "  Faqat matnni nusxalash"))) CopyRows({ (size_t)r }, true);
                    if (ImGui::MenuItem(Tr(ICON_FA_LIST "  Ko'rinayotganlarning hammasini nusxalash")))
                    {
                        contextRow.resize(m_View.Rows.size());
                        for (size_t i = 0; i < contextRow.size(); i++)
                            contextRow[i] = i;
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem(Tr(ICON_FA_FILTER "  Shu matn bo'yicha filtrlash")))
                    {
                        int more = 0;
                        m_FilterText = std::string(FirstLine(first.Text, more));
                        m_Regex = false;
                    }
                    ImGui::EndPopup();
                }
                ImGui::SameLine(0.0f, 0.0f);
                ImGui::TextColored(style.Color, "%s", style.Icon);
                if (m_Prefs.ShowTimestamps)
                {
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", latest.Time.c_str());
                }
                ImGui::TableSetColumnIndex(1);
                ImGui::TextDisabled("%s", first.FromClient ? "APP" : "ZAMIN");
                ImGui::TableSetColumnIndex(2);
                int more = 0;
                std::string_view line = FirstLine(first.Text, more);
                ImGui::PushStyleColor(ImGuiCol_Text, style.Color);
                ImGui::TextUnformatted(line.data(), line.data() + line.size());
                ImGui::PopStyleColor();
                if (more > 0)
                {
                    ImGui::SameLine();
                    ImGui::TextDisabled("(+%d qator)", more);
                }
                if (collapse)
                {
                    ImGui::TableSetColumnIndex(3);
                    const float h = ImGui::GetTextLineHeight();
                    const ImVec2 at = ImGui::GetCursorScreenPos();
                    if (row.Count > 1)
                    {
                        const ImU32 color = first.Level >= LogLevel::Error ? IM_COL32(170, 60, 60, 255)
                                          : first.Level == LogLevel::Warn ? IM_COL32(150, 115, 30, 255) : IM_COL32(70, 76, 88, 255);
                        float w = CountBadge(ImGui::GetWindowDrawList(), at, h, row.Count, color);
                        ImGui::Dummy(ImVec2(w, h));
                    }
                    else
                        ImGui::Dummy(ImVec2(h, h));
                }
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
        if (!contextRow.empty())
            CopyRows(contextRow, false);
        if (m_ScrollToBottom)
        {
            ImGui::SetScrollHereY(1.0f);
            m_ScrollToBottom = false;
        }
    }

    void ConsolePanel::DrawDetails(float height)
    {
        int selectedRow = -1;
        for (size_t i = 0; i < m_View.Rows.size(); i++)
            if (m_FirstIndex + m_View.Rows[i].First == m_Selected)
                selectedRow = (int)i;
        if (selectedRow < 0)
            return;
        const ConsoleRow& row = m_View.Rows[selectedRow];
        const LogMessage& m = m_Messages[row.Last];
        const LevelStyle style = StyleOf(m.Level);
        ImGui::Separator();
        if (ImGui::BeginChild("##details", ImVec2(0, height - ImGui::GetStyle().ItemSpacing.y - 2.0f)))
        {
            ImGui::TextColored(style.Color, "%s  %s", style.Icon, Log::LevelToString(m.Level));
            ImGui::SameLine();
            ImGui::TextDisabled("%s  |  %s", m.Time.c_str(), m.FromClient ? "APP" : "ZAMIN");
            if (row.Count > 1)
            {
                ImGui::SameLine();
                ImGui::TextDisabled("|  %u marta (birinchisi %s da)", row.Count, m_Messages[row.First].Time.c_str());
            }
            const float buttonW = ImGui::CalcTextSize(ICON_FA_COPY "  Nusxalash").x + ImGui::GetStyle().FramePadding.x * 2.0f;
            ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 8.0f, ImGui::GetWindowContentRegionMax().x - buttonW * 2.0f - 6.0f));
            if (ImGui::SmallButton(Tr(ICON_FA_COPY "  Nusxalash")))
                CopyRows({ (size_t)selectedRow }, true);
            ImGui::SameLine();
            if (ImGui::SmallButton(Tr(ICON_FA_XMARK "  Yopish")))
                m_Selected = UINT64_MAX;
            // Read-only text: a part of it can be selected with the mouse and copied.
            std::string text = m.Text;
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0.25f));
            ImGui::InputTextMultiline("##text", &text, ImVec2(-FLT_MIN, -FLT_MIN), ImGuiInputTextFlags_ReadOnly);
            ImGui::PopStyleColor();
        }
        ImGui::EndChild();
    }

}
