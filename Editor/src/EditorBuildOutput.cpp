// The script build's messages: every line the build prints is parsed as it comes (GCC / Clang
// errors and warnings with their notes and source lines, linker and CMake errors). The
// "Kompilyatsiya" window lists them (filter by severity, search), a double click opens the file at
// the line in the external editor; the status bar shows how many errors / warnings the last build had.
#include "EditorLayer.h"
#include "UI.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <format>

namespace fs = std::filesystem;

namespace Zamin {

    namespace {
        const ImVec4 s_ErrorColor(0.96f, 0.42f, 0.38f, 1.0f);
        const ImVec4 s_WarningColor(0.96f, 0.76f, 0.32f, 1.0f);
        const ImVec4 s_NoteColor(0.52f, 0.70f, 0.95f, 1.0f);

        ImVec4 SeverityColor(DiagnosticSeverity severity)
        {
            return severity == DiagnosticSeverity::Error ? s_ErrorColor : severity == DiagnosticSeverity::Warning ? s_WarningColor : s_NoteColor;
        }

        const char* SeverityIcon(DiagnosticSeverity severity)
        {
            return severity == DiagnosticSeverity::Error ? ICON_FA_CIRCLE_XMARK
                   : severity == DiagnosticSeverity::Warning ? ICON_FA_TRIANGLE_EXCLAMATION : ICON_FA_CIRCLE_INFO;
        }

        bool ContainsNoCase(std::string_view text, std::string_view needle)
        {
            if (needle.empty())
                return true;
            return std::search(text.begin(), text.end(), needle.begin(), needle.end(), [](char a, char b) {
                       return std::tolower((unsigned char)a) == std::tolower((unsigned char)b);
                   }) != text.end();
        }

        // The source lines a compiler printed under a message, in table rows: GCC's "  12 |   code" and
        // "     |   ~~~^~~" (Clang's without the gutter). The UI font is not monospaced, so the marker
        // line is drawn as an underline measured against the code line above it (a fix-it hint as
        // text at its place).
        void DrawSnippet(const std::vector<std::string>& lines, const ImVec4& color, float indent)
        {
            const float gutterWidth = ImGui::CalcTextSize("00000 ").x;
            std::string_view code; // the last code line's text (after the gutter)
            for (const std::string& raw : lines)
            {
                std::string_view line(raw);
                std::string_view number, body = line;
                if (size_t bar = line.find('|'); bar != std::string_view::npos && line.substr(0, bar).find_first_not_of(" 0123456789") == std::string_view::npos)
                {
                    number = line.substr(0, bar);
                    while (!number.empty() && number.front() == ' ')
                        number.remove_prefix(1);
                    while (!number.empty() && number.back() == ' ')
                        number.remove_suffix(1);
                    body = line.substr(bar + 1);
                }
                const bool markers = !body.empty() && body.find_first_not_of(" ~^-+") == std::string_view::npos &&
                                     body.find_first_of("~^") != std::string_view::npos;
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                const ImVec2 at(ImGui::GetCursorScreenPos().x + indent, ImGui::GetCursorScreenPos().y);
                ImDrawList* dl = ImGui::GetWindowDrawList();
                const float lineHeight = ImGui::GetTextLineHeight();
                const ImU32 gutterColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);
                // The gutter: the line number right-aligned, then the bar.
                if (!number.empty())
                {
                    const std::string n(number);
                    dl->AddText(ImVec2(at.x + gutterWidth - ImGui::CalcTextSize((n + " ").c_str()).x, at.y), gutterColor, n.c_str());
                }
                dl->AddLine(ImVec2(at.x + gutterWidth, at.y), ImVec2(at.x + gutterWidth, at.y + lineHeight), gutterColor);
                const float x = at.x + gutterWidth + 6.0f;
                const size_t first = body.find_first_not_of(' ');
                if (markers && !code.empty())
                {
                    // Under the code: from the first marker to the last one, the caret a small triangle.
                    const size_t last = body.find_last_not_of(' ') + 1;
                    auto offset = [&](size_t column) {
                        std::string prefix(code.substr(0, std::min(column, code.size())));
                        if (column > code.size())
                            prefix.append(column - code.size(), ' ');
                        return ImGui::CalcTextSize(prefix.c_str()).x;
                    };
                    const float x0 = x + offset(first), x1 = std::max(x + offset(last), x0 + 4.0f);
                    const ImU32 c = ImGui::ColorConvertFloat4ToU32(color);
                    const float y = at.y + 3.0f;
                    dl->AddLine(ImVec2(x0, y), ImVec2(x1, y), c, 2.0f);
                    if (size_t caret = body.find('^'); caret != std::string_view::npos)
                    {
                        const float cx = x + offset(caret) + ImGui::CalcTextSize("^").x * 0.5f;
                        dl->AddTriangleFilled(ImVec2(cx, y - 1.0f), ImVec2(cx - 4.0f, y + 6.0f), ImVec2(cx + 4.0f, y + 6.0f), c);
                    }
                    ImGui::Dummy(ImVec2(x1 - at.x, lineHeight * 0.6f));
                    continue;
                }
                if (number.empty() && !code.empty() && first != std::string_view::npos && body != line)
                {
                    // A fix-it hint ("did you mean"): green, where it goes.
                    const std::string prefix(code.substr(0, std::min(first, code.size())));
                    const std::string hint(body.substr(first));
                    dl->AddText(ImVec2(x + ImGui::CalcTextSize(prefix.c_str()).x, at.y), IM_COL32(110, 215, 130, 255), hint.c_str());
                    ImGui::Dummy(ImVec2(gutterWidth + ImGui::CalcTextSize((prefix + hint).c_str()).x + 6.0f, lineHeight));
                    continue;
                }
                const std::string text(body);
                dl->AddText(ImVec2(x, at.y), ImGui::GetColorU32(ImVec4(0.80f, 0.83f, 0.88f, 1.0f)), text.c_str());
                ImGui::Dummy(ImVec2(gutterWidth + ImGui::CalcTextSize(text.c_str()).x + 6.0f, lineHeight));
                code = body;
            }
        }

        // The file as the window shows it: relative to the project when inside it.
        std::string ShownFile(const std::string& file)
        {
            if (file.empty())
                return {};
            if (Project::GetActive())
            {
                std::error_code ec;
                fs::path relative = fs::path(file).lexically_relative(Project::GetProjectDirectory());
                if (!relative.empty() && !relative.string().starts_with(".."))
                    return relative.generic_string();
            }
            return file;
        }
    }

    void EditorLayer::BeginBuildOutput(const fs::path& buildDirectory)
    {
        std::lock_guard lock(m_BuildOutputMutex);
        m_BuildParser.Clear();
        m_BuildLog.clear();
        m_BuildOutputRevision++;
        m_BuildDirectory = buildDirectory;
        m_BuildResult = BuildResult::Running;
        m_BuildSelected = -1;
    }

    void EditorLayer::AddBuildOutputLine(const std::string& line)
    {
        std::lock_guard lock(m_BuildOutputMutex);
        m_BuildLog.push_back(line);
        m_BuildParser.AddLine(line);
        m_BuildOutputRevision++;
    }

    void EditorLayer::FinishBuildOutput(bool succeeded, double seconds)
    {
        {
            std::lock_guard lock(m_BuildOutputMutex);
            m_BuildParser.Finish();
            m_BuildParser.ResolvePaths(m_BuildDirectory); // the compiler ran in the build folder
            m_BuildDiagnostics = m_BuildParser.GetDiagnostics();
            m_BuildErrors = m_BuildParser.GetErrorCount();
            m_BuildWarnings = m_BuildParser.GetWarningCount();
            m_BuildShownRevision = m_BuildOutputRevision;
        }
        m_BuildResult = succeeded ? BuildResult::Succeeded : BuildResult::Failed;
        m_BuildSeconds = seconds;
        WakeEditor();
        if (!succeeded)
        {
            // The errors in front (the first one picked).
            m_ShowBuildOutput = true;
            m_FocusBuildOutput = true;
            m_BuildOutputTab = m_BuildDiagnostics.empty() ? 1 : 0;
            for (size_t i = 0; i < m_BuildDiagnostics.size(); i++)
                if (m_BuildDiagnostics[i].Severity == DiagnosticSeverity::Error)
                {
                    m_BuildSelected = (int)i;
                    break;
                }
        }
    }

    void EditorLayer::LoadBuildLogAutomation(const fs::path& file)
    {
        std::string text = FileSystem::ReadTextFile(file);
        if (text.empty())
        {
            ZM_WARN("ZAMIN_EDITOR_BUILD_LOG: '{}' o'qilmadi", file.string());
            return;
        }
        BeginBuildOutput(Project::GetActive() ? Project::GetProjectDirectory() / "Build" : file.parent_path());
        size_t start = 0;
        while (start < text.size())
        {
            size_t end = text.find('\n', start);
            if (end == std::string::npos)
                end = text.size();
            AddBuildOutputLine(text.substr(start, end - start));
            start = end + 1;
        }
        FinishBuildOutput(false, 4.2);
        ZM_INFO("Kompilyatsiya natijasi yuklandi: {} xato, {} ogohlantirish", m_BuildErrors, m_BuildWarnings);
    }

    void EditorLayer::OpenDiagnostic(const CompilerDiagnostic& diagnostic)
    {
        if (diagnostic.File.empty())
            return;
        fs::path file(diagnostic.File);
        std::error_code ec;
        if (!fs::exists(file, ec))
        {
            ZM_WARN("Fayl topilmadi: {}", diagnostic.File);
            return;
        }
        OpenInExternalEditor(file, diagnostic.Line, diagnostic.Column);
    }

    void EditorLayer::UI_BuildStatusBadge()
    {
        if (m_Building || (m_BuildErrors == 0 && m_BuildWarnings == 0 && m_BuildResult != BuildResult::Failed))
            return;
        ImGui::TextDisabled("|");
        // One clickable piece: the errors in red, the warnings in yellow.
        std::string text;
        if (m_BuildErrors > 0 || m_BuildResult == BuildResult::Failed)
            text += std::format(ICON_FA_CIRCLE_XMARK " {}", m_BuildErrors);
        if (m_BuildWarnings > 0)
            text += std::format("{}" ICON_FA_TRIANGLE_EXCLAMATION " {}", text.empty() ? "" : "  ", m_BuildWarnings);
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const ImVec2 size = ImGui::CalcTextSize(text.c_str());
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(1, 1, 1, 0.08f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(1, 1, 1, 0.14f));
        if (ImGui::Selectable("##buildBadge", false, ImGuiSelectableFlags_None, size))
        {
            m_ShowBuildOutput = true;
            m_FocusBuildOutput = true;
        }
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered())
            UI::Tooltip(std::format("Oxirgi kompilyatsiya: {} xato, {} ogohlantirish\nBosing: Kompilyatsiya oynasi", m_BuildErrors, m_BuildWarnings).c_str());
        // The colours drawn over the selectable.
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float x = at.x;
        if (m_BuildErrors > 0 || m_BuildResult == BuildResult::Failed)
        {
            std::string errors = std::format(ICON_FA_CIRCLE_XMARK " {}", m_BuildErrors);
            dl->AddText(ImVec2(x, at.y), ImGui::ColorConvertFloat4ToU32(s_ErrorColor), errors.c_str());
            x += ImGui::CalcTextSize((errors + "  ").c_str()).x;
        }
        if (m_BuildWarnings > 0)
            dl->AddText(ImVec2(x, at.y), ImGui::ColorConvertFloat4ToU32(s_WarningColor),
                        std::format(ICON_FA_TRIANGLE_EXCLAMATION " {}", m_BuildWarnings).c_str());
    }

    void EditorLayer::UI_BuildOutput()
    {
        if (!m_ShowBuildOutput)
            return;
        if (m_FocusBuildOutput)
        {
            // First opened: a tab next to the console (automation, ZAMIN_EDITOR_BUILD_IN_VIEWPORT: over
            // the viewport, for screenshots).
            static const bool s_InViewport = std::getenv("ZAMIN_EDITOR_BUILD_IN_VIEWPORT") != nullptr;
            if (ImGuiWindow* dock = ImGui::FindWindowByName(s_InViewport ? "###Viewport" : "###Console"); dock && dock->DockId)
                ImGui::SetNextWindowDockID(dock->DockId, s_InViewport ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowFocus();
            m_FocusBuildOutput = false;
        }
        ImGui::SetNextWindowSize(ImVec2(820, 380), ImGuiCond_FirstUseEver);
        const std::string title = m_Building ? std::string(ICON_FA_SCREWDRIVER_WRENCH "  Kompilyatsiya...###BuildOutput")
                                  : m_BuildErrors > 0 ? std::format(ICON_FA_SCREWDRIVER_WRENCH "  Kompilyatsiya ({} xato)###BuildOutput", m_BuildErrors)
                                                      : std::string(ICON_FA_SCREWDRIVER_WRENCH "  Kompilyatsiya###BuildOutput");
        if (!ImGui::Begin(title.c_str(), &m_ShowBuildOutput))
        {
            ImGui::End();
            return;
        }

        // While building: the messages so far (a copy, the build thread keeps adding).
        if (m_Building)
        {
            std::lock_guard lock(m_BuildOutputMutex);
            if (m_BuildShownRevision != m_BuildOutputRevision)
            {
                m_BuildDiagnostics = m_BuildParser.GetDiagnostics();
                m_BuildErrors = m_BuildParser.GetErrorCount();
                m_BuildWarnings = m_BuildParser.GetWarningCount();
                m_BuildShownRevision = m_BuildOutputRevision;
            }
        }

        // ---- the toolbar: build, the result, severity filters, search
        const bool hasProject = Project::GetActive() != nullptr;
        ImGui::BeginDisabled(!hasProject || m_Building);
        if (ImGui::Button(ICON_FA_HAMMER "  Kompilyatsiya"))
            BuildScripts(true);
        ImGui::EndDisabled();
        ImGui::SameLine();
        switch (m_BuildResult)
        {
            case BuildResult::Running:
            {
                const char* spin[] = { "|", "/", "-", "\\" };
                ImGui::TextColored(s_WarningColor, "%s  ketmoqda %s", ICON_FA_SPINNER, spin[(int)(ImGui::GetTime() * 8.0) % 4]);
                break;
            }
            case BuildResult::Succeeded:
                ImGui::TextColored(ImVec4(0.4f, 0.85f, 0.5f, 1.0f), ICON_FA_CIRCLE_CHECK "  muvaffaqiyatli (%.1f s)", m_BuildSeconds);
                break;
            case BuildResult::Failed:
                ImGui::TextColored(s_ErrorColor, ICON_FA_CIRCLE_XMARK "  muvaffaqiyatsiz (%.1f s)", m_BuildSeconds);
                break;
            default:
                ImGui::TextDisabled("hali kompilyatsiya qilinmagan");
                break;
        }
        size_t notes = 0;
        for (const CompilerDiagnostic& d : m_BuildDiagnostics)
            notes += d.Notes.size() + (d.Severity == DiagnosticSeverity::Note ? 1 : 0);
        auto toggle = [&](bool& on, const char* icon, const char* label, size_t count, const ImVec4& color, const char* tip) {
            ImGui::SameLine();
            const std::string text = std::format("{} {}  {}##{}", icon, count, label, label);
            if (on)
                ImGui::PushStyleColor(ImGuiCol_Button, UI::AccentDarkColor());
            ImGui::PushStyleColor(ImGuiCol_Text, on ? color : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            if (ImGui::Button(text.c_str()))
                on = !on;
            ImGui::PopStyleColor(on ? 2 : 1);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                UI::Tooltip(tip);
        };
        toggle(m_BuildShowErrors, ICON_FA_CIRCLE_XMARK, "Xatolar", m_BuildErrors, s_ErrorColor, "Xatolarni ko'rsatish / yashirish");
        toggle(m_BuildShowWarnings, ICON_FA_TRIANGLE_EXCLAMATION, "Ogohlantirishlar", m_BuildWarnings, s_WarningColor,
               "Ogohlantirishlarni ko'rsatish / yashirish");
        toggle(m_BuildShowNotes, ICON_FA_CIRCLE_INFO, "Izohlar", notes, s_NoteColor, "Xabarlar ostidagi izohlar (note:) va kod satrlari");
        ImGui::SameLine();
        UI::SearchBox("##buildSearch", m_BuildSearch, "Xabar yoki fayl bo'yicha qidirish...");

        if (ImGui::BeginTabBar("##buildTabs"))
        {
            const ImGuiTabItemFlags problemsFlags = m_BuildOutputTab == 0 ? ImGuiTabItemFlags_SetSelected : 0;
            const ImGuiTabItemFlags outputFlags = m_BuildOutputTab == 1 ? ImGuiTabItemFlags_SetSelected : 0;
            m_BuildOutputTab = -1;
            if (ImGui::BeginTabItem(std::format(ICON_FA_CIRCLE_EXCLAMATION "  Muammolar ({})###buildProblems", m_BuildErrors + m_BuildWarnings).c_str(),
                                    nullptr, problemsFlags))
            {
                if (m_BuildDiagnostics.empty())
                    ImGui::TextDisabled(m_BuildResult == BuildResult::None ? "Skriptlarni kompilyatsiya qiling (Ctrl+B): xatolar shu yerda fayl va qatori bilan chiqadi"
                                        : m_BuildResult == BuildResult::Failed ? "Xabarlar ajratib olinmadi - 'To'liq chiqish'ni ko'ring"
                                                                               : "Xato yoki ogohlantirish yo'q");
                else if (ImGui::BeginTable("##buildTable", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable |
                                                                   ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX))
                {
                    ImGui::TableSetupScrollFreeze(0, 1);
                    ImGui::TableSetupColumn("Xabar", ImGuiTableColumnFlags_WidthStretch, 3.0f);
                    ImGui::TableSetupColumn("Fayl", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                    ImGui::TableSetupColumn("Qator", ImGuiTableColumnFlags_WidthFixed, 64.0f);
                    ImGui::TableHeadersRow();
                    ImGui::PushStyleColor(ImGuiCol_Header, UI::AccentU32(0.26f));
                    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, UI::AccentU32(0.18f));
                    ImGui::PushStyleColor(ImGuiCol_HeaderActive, UI::AccentU32(0.34f));

                    // One row: the message (a tree node when it has more to show), the file, the line.
                    auto row = [&](const CompilerDiagnostic& d, int index, bool child) -> bool {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        const bool hasMore = !child && ((m_BuildShowNotes && (!d.Notes.empty() || !d.Snippet.empty())) || !d.Context.empty());
                        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_FramePadding;
                        if (!hasMore)
                            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
                        else if (!d.Notes.empty())
                            flags |= ImGuiTreeNodeFlags_DefaultOpen; // the notes say why (candidates, where it was declared)
                        if (!child && index == m_BuildSelected)
                            flags |= ImGuiTreeNodeFlags_Selected;
                        ImGui::PushStyleColor(ImGuiCol_Text, SeverityColor(d.Severity));
                        const bool open = ImGui::TreeNodeEx(d.Linker ? "##l" : d.CMake ? "##c" : "##d", flags, "%s", SeverityIcon(d.Severity));
                        ImGui::PopStyleColor();
                        const bool hovered = ImGui::IsItemHovered();
                        if (ImGui::IsItemClicked() && !child)
                            m_BuildSelected = index;
                        if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                            OpenDiagnostic(d);
                        if (ImGui::BeginPopupContextItem("##diagnosticMenu"))
                        {
                            if (ImGui::MenuItem(ICON_FA_PEN_TO_SQUARE "  Tashqi muharrirda ochish", nullptr, false, d.HasLocation() || !d.File.empty()))
                                OpenDiagnostic(d);
                            if (ImGui::MenuItem(ICON_FA_COPY "  Xabarni nusxalash"))
                                ImGui::SetClipboardText(d.Format().c_str());
                            ImGui::EndPopup();
                        }
                        ImGui::SameLine();
                        if (d.Linker)
                            ImGui::TextDisabled("[linker]");
                        else if (d.CMake)
                            ImGui::TextDisabled("[CMake]");
                        if (d.Linker || d.CMake)
                            ImGui::SameLine();
                        ImGui::TextUnformatted(d.Message.c_str());
                        if (!d.Option.empty())
                        {
                            ImGui::SameLine();
                            ImGui::TextDisabled("[%s]", d.Option.c_str());
                        }
                        ImGui::TableSetColumnIndex(1);
                        const std::string file = ShownFile(d.File);
                        ImGui::TextDisabled("%s", file.c_str());
                        if (!file.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                            UI::Tooltip(std::format("{}\nIkki marta bosing: tashqi muharrirda ochish", d.File).c_str());
                        ImGui::TableSetColumnIndex(2);
                        if (d.Line > 0)
                            ImGui::TextDisabled(d.Column > 0 ? "%d:%d" : "%d", d.Line, d.Column);
                        return open && hasMore;
                    };

                    for (int i = 0; i < (int)m_BuildDiagnostics.size(); i++)
                    {
                        const CompilerDiagnostic& d = m_BuildDiagnostics[i];
                        if ((d.Severity == DiagnosticSeverity::Error && !m_BuildShowErrors) ||
                            (d.Severity == DiagnosticSeverity::Warning && !m_BuildShowWarnings) ||
                            (d.Severity == DiagnosticSeverity::Note && !m_BuildShowNotes))
                            continue;
                        if (!m_BuildSearch.empty() && !ContainsNoCase(d.Message, m_BuildSearch) && !ContainsNoCase(d.File, m_BuildSearch) &&
                            !ContainsNoCase(d.Option, m_BuildSearch))
                            continue;
                        ImGui::PushID(i);
                        if (row(d, i, false))
                        {
                            for (const std::string& context : d.Context)
                            {
                                ImGui::TableNextRow();
                                ImGui::TableSetColumnIndex(0);
                                ImGui::TextDisabled("%s", context.c_str());
                            }
                            if (m_BuildShowNotes)
                            {
                                DrawSnippet(d.Snippet, SeverityColor(d.Severity), 0.0f);
                                for (size_t n = 0; n < d.Notes.size(); n++)
                                {
                                    ImGui::PushID((int)n);
                                    if (row(d.Notes[n], i, true))
                                        ImGui::TreePop();
                                    DrawSnippet(d.Notes[n].Snippet, s_NoteColor, ImGui::GetStyle().IndentSpacing);
                                    ImGui::PopID();
                                }
                            }
                            ImGui::TreePop();
                        }
                        ImGui::PopID();
                    }
                    ImGui::PopStyleColor(3);
                    ImGui::EndTable();
                }
                ImGui::EndTabItem();
            }
            size_t lines = 0;
            {
                std::lock_guard lock(m_BuildOutputMutex);
                lines = m_BuildLog.size();
            }
            if (ImGui::BeginTabItem(std::format(ICON_FA_TERMINAL "  To'liq chiqish ({} qator)###buildLog", lines).c_str(), nullptr, outputFlags))
            {
                if (ImGui::BeginChild("##buildLogText", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar))
                {
                    std::lock_guard lock(m_BuildOutputMutex);
                    ImGuiListClipper clipper;
                    clipper.Begin((int)m_BuildLog.size());
                    while (clipper.Step())
                        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
                        {
                            const std::string& line = m_BuildLog[i];
                            CompilerDiagnostic parsed;
                            if (CompilerOutputParser::ParseDiagnosticLine(line, parsed))
                                ImGui::TextColored(SeverityColor(parsed.Severity), "%s", line.c_str());
                            else
                                ImGui::TextDisabled("%s", line.c_str());
                        }
                    if (m_Building && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f)
                        ImGui::SetScrollHereY(1.0f); // follow the new lines
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::End();
    }

}
