#include "CodeEditorPanel.h"
#include "Zamin/Editor/CodeDocument.h"
#include "Zamin/Project/Project.h"
#include "Zamin/Editor/ExternalEditor.h"
#include "Zamin/Utils/FileSystem.h"
#include "../../../vendor/ImGuiColorTextEdit/TextEditor.h"
#include <imgui.h>
#include <imgui_stdlib.h>
#include <algorithm>
#include <cstring>
#include <chrono>
#include <cctype>
#include <format>

namespace Zamin {
    namespace {
        const std::vector<std::pair<std::string, std::string>> API = {
#include "../CodeAPICatalog.inc"
        };
        struct Snippet
        {
            const char* Name;
            const char* Text;
        };
        const Snippet Snippets[] = {
            { "Update movement", "void OnUpdate(Zamin::Timestep dt) override\n{\n    Transform().Translation.x += 2.0f * (float)dt;\n}\n" },
            { "2D input movement", "glm::vec2 direction = Zamin::Input::GetAxis2D();\nSetVelocity(direction * Speed);\n" },
            { "3D impulse", "ApplyImpulse3D({ 0.0f, 5.0f, 0.0f });\n" },
            { "Trigger callback", "void OnTriggerBegin(Zamin::Entity other) override\n{\n    if (other.HasTag(\"Player\"))\n        Destroy();\n}\n" },
            { "Collision callback", "void OnCollisionBegin(Zamin::Entity other) override\n{\n    // Handle the contact here.\n}\n" },
            { "Delayed action", "Invoke(1.0f, [this]() { Destroy(); });\n" },
            { "Message handler", "void OnMessage(const Zamin::ScriptMessage& message) override\n{\n    if (message.Name == \"Damage\")\n        Health -= message.Get<float>();\n}\n" },
            { "Expose a field", "// Place registration after the class definition.\nZM_REGISTER_SCRIPT(MyScript, ZM_FIELD(MyScript, Speed))\n" }
        };
        bool Contains(const std::string& text, const std::string& query) { return query.empty() || !CodeDocument::Find(text, query, false).empty(); }
        ImFont* CodeFont()
        {
            for (auto* f : ImGui::GetIO().Fonts->Fonts)
                if (f->ConfigData && std::strncmp(f->ConfigData->Name, "ZaminCode", 9) == 0) return f;
            return ImGui::GetFont();
        }
        std::pair<int, int> Position(std::string_view text, size_t offset)
        {
            int line = 0;
            size_t start = 0;
            for (size_t i = 0; i < std::min(offset, text.size()); ++i)
                if (text[i] == '\n')
                {
                    ++line;
                    start = i + 1;
                }
            return { line, (int)(offset - start) };
        }
    }
    struct CodeEditorPanel::Impl
    {
        struct Tab
        {
            CodeDocument Document;
            TextEditor Editor;
            bool ReadOnly = false, PendingRecovery = false;
            double ChangedAt = 0;
            uint64_t Revision = 0;
            int JumpLine = -1, JumpColumn = 0;
        };
        std::filesystem::path Root, Cache;
        std::vector<std::unique_ptr<Tab>> Tabs;
        std::vector<CompilerDiagnostic> Diagnostics;
        std::vector<std::filesystem::path> Files;
        size_t Active = 0;
        int Close = -1;
        bool Focus = false, Focused = false, Building = false, CaseSensitive = false, ShowFind = false, ShowAPI = true, Whitespace = false;
        float Zoom = 1;
        std::string Error, FindText, ReplaceText, APIQuery, FileQuery;
        size_t NextMatch = 0;
        double LastScan = -100, LastDiskCheck = -100;
        bool ExternalChanged = false, TextFocused = false, Completion = false;
        int CompleteLine = 0, CompleteStart = 0, CompleteEnd = 0;
        Tab* Current() { return Active < Tabs.size() ? Tabs[Active].get() : nullptr; }
        void Sync(Tab& t)
        {
            if (t.Editor.GetRevision() != t.Revision)
            {
                auto text = t.Editor.GetText();
                if (!t.Document.SetText(text))
                {
                    Error = t.Document.Error();
                    t.Editor.SetText(t.Document.Text());
                }
                else
                {
                    t.PendingRecovery = true;
                    t.ChangedAt = ImGui::GetTime();
                }
                t.Revision = t.Editor.GetRevision();
            }
        }
        bool Save(Tab& t, bool overwrite = false)
        {
            Sync(t);
            if (t.ReadOnly)
            {
                Error = "This file is read-only: source outside the project cannot be overwritten.";
                return false;
            }
            if (!t.Document.Save(overwrite))
            {
                Error = t.Document.Error();
                return false;
            }
            t.PendingRecovery = false;
            ExternalChanged = false;
            Error.clear();
            return true;
        }
        void Insert(Tab& t, const std::string& text)
        {
            if (t.ReadOnly) return;
            t.Editor.InsertText(text);
            Sync(t);
            Focus = true;
        }
        void Scan()
        {
            Files.clear();
            if (Root.empty()) return;
            std::error_code ec;
            auto source = Project::GetSourceDirectory();
            if (source.empty()) source = Root / "Source";
            for (std::filesystem::recursive_directory_iterator it(source, std::filesystem::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec))
            {
                if (it->is_directory(ec) && it->path().filename().string().starts_with('.')) it.disable_recursion_pending();
                if (it->is_regular_file(ec) && ExternalEditor::IsSourceFile(it->path())) Files.push_back(it->path());
                if (Files.size() >= 2000) break;
            }
            std::sort(Files.begin(), Files.end());
        }
    };
    CodeEditorPanel::CodeEditorPanel() : m_Impl(std::make_unique<Impl>()) {}
    CodeEditorPanel::~CodeEditorPanel() { FlushRecovery(); }
    void CodeEditorPanel::SetProject(const std::filesystem::path& root, const std::filesystem::path& cache)
    {
        auto& i = *m_Impl;
        if (i.Root == root && i.Cache == cache) return;
        FlushRecovery();
        i.Tabs.clear();
        i.Root = root;
        i.Cache = cache;
        i.Active = 0;
        i.Diagnostics.clear();
        i.Error.clear();
        i.Scan();
        i.ExternalChanged = false;
    }
    void CodeEditorPanel::SetBuildRunning(bool running) { m_Impl->Building = running; }
    bool CodeEditorPanel::IsFocused() const { return m_Impl->Focused; }
    void CodeEditorPanel::SetDiagnostics(const std::vector<CompilerDiagnostic>& diagnostics) { m_Impl->Diagnostics = diagnostics; }
    void CodeEditorPanel::FlushRecovery()
    {
        for (auto& t : m_Impl->Tabs)
            if (t->PendingRecovery && !t->ReadOnly)
            {
                if (t->Document.WriteRecovery()) t->PendingRecovery = false;
                else m_Impl->Error = "Recovery file could not be written; save your code before closing.";
            }
    }
    bool CodeEditorPanel::SaveAll()
    {
        for (auto& t : m_Impl->Tabs)
            if ((m_Impl->Sync(*t), t->Document.Dirty()) && !m_Impl->Save(*t)) return false;
        return true;
    }
    bool CodeEditorPanel::Open(const std::filesystem::path& file, int line, int column)
    {
        auto& i = *m_Impl;
        std::error_code ec;
        auto path = std::filesystem::canonical(file, ec);
        if (ec)
        {
            i.Error = "Source file not found: " + file.string();
            return false;
        }
        for (size_t n = 0; n < i.Tabs.size(); ++n)
            if (i.Tabs[n]->Document.Path() == path)
            {
                i.Active = n;
                i.Focus = true;
                if (line > 0)
                {
                    i.Tabs[n]->JumpLine = line - 1;
                    i.Tabs[n]->JumpColumn = std::max(0, column - 1);
                }
                return true;
            }
        if (i.Tabs.size() >= 16)
        {
            i.Error = "Close a tab first (16-file limit).";
            return false;
        }
        auto t = std::make_unique<Impl::Tab>();
        if (!t->Document.Open(path, i.Cache / "code-recovery", i.Root))
        {
            i.Error = t->Document.Error();
            return false;
        }
        t->ReadOnly = i.Root.empty() || !FileSystem::IsSubPathOnDisk(path, i.Root);
        t->Editor.SetLanguageDefinition(ExternalEditor::IsSourceFile(path) ? TextEditor::LanguageDefinitionId::Cpp : TextEditor::LanguageDefinitionId::None);
        t->Editor.SetPalette(TextEditor::PaletteId::Dark);
        t->Editor.SetTabSize(4);
        t->Editor.SetAutoIndentEnabled(true);
        t->Editor.SetShowWhitespacesEnabled(false);
        t->Editor.SetReadOnlyEnabled(t->ReadOnly);
        t->Editor.SetText(t->Document.Text());
        if (line > 0)
        {
            t->JumpLine = line - 1;
            t->JumpColumn = std::max(0, column - 1);
        }
        i.Tabs.push_back(std::move(t));
        i.Active = i.Tabs.size() - 1;
        i.Focus = true;
        i.Error.clear();
        return true;
    }
    void CodeEditorPanel::OnImGuiRender(bool* show)
    {
        auto& i = *m_Impl;
        if (!*show)
        {
            i.Focused = false;
            FlushRecovery();
            return;
        }
        if (i.Focus) ImGui::SetNextWindowFocus();
        ImGui::SetNextWindowSize({ 1400, 800 }, ImGuiCond_FirstUseEver);
        bool visible = ImGui::Begin("Kod muharriri###CodeEditor", show);
        i.Focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        if (!*show)
        {
            bool dirty = false;
            for (auto& t : i.Tabs) dirty |= t->Document.Dirty();
            if (dirty)
            {
                *show = true;
                i.Close = -2;
                ImGui::OpenPopup("Unsaved code");
            }
        }
        if (!visible)
        {
            ImGui::End();
            return;
        }
        auto* tab = i.Current();
        const bool shortcuts = i.Focused;
        bool requestSave = false, requestSaveAll = false, requestBuild = false;
        auto& io = ImGui::GetIO();
        if (ImGui::Button("New script") && OnNewScript) OnNewScript();
        ImGui::SameLine();
        ImGui::BeginDisabled(!tab || tab->ReadOnly);
        bool save = ImGui::Button("Save  Ctrl+S");
        ImGui::SameLine();
        ImGui::EndDisabled();
        if (ImGui::Button("Save all")) requestSaveAll = true;
        ImGui::BeginDisabled(false);
        ImGui::EndDisabled();
        if (save || (shortcuts && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)))
        {
            if (tab)
            {
                if (io.KeyShift) requestSaveAll = true;
                else requestSave = true;
            }
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(i.Building || i.Root.empty());
        if ((ImGui::Button(i.Building ? "Building..." : "Build + reload  F7") || (shortcuts && ImGui::IsKeyPressed(ImGuiKey_F7, false))) && OnBuild)
        {
            if (!i.Building && !i.Root.empty()) requestBuild = true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Find / Replace") || (shortcuts && io.KeyCtrl && (ImGui::IsKeyPressed(ImGuiKey_F, false) || ImGui::IsKeyPressed(ImGuiKey_H, false)))) i.ShowFind = !i.ShowFind;
        ImGui::SameLine();
        ImGui::Checkbox("API", &i.ShowAPI);
        ImGui::SameLine();
        if (ImGui::Checkbox("Whitespace", &i.Whitespace))
            for (auto& t : i.Tabs) t->Editor.SetShowWhitespacesEnabled(i.Whitespace);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(85);
        ImGui::SliderFloat("Zoom", &i.Zoom, .75f, 1.5f, "%.2f");
        if (!i.Error.empty()) ImGui::TextColored({ 1, .45f, .4f, 1 }, "%s", i.Error.c_str());
        if (tab && tab->Document.HasRecovery())
        {
            ImGui::TextColored({ 1, .8f, .4f, 1 }, "Unsaved recovery found for this file.");
            ImGui::SameLine();
            if (ImGui::Button("Recover edits"))
            {
                tab->Document.Recover();
                tab->Editor.SetText(tab->Document.Text());
                tab->Revision = tab->Editor.GetRevision();
                tab->PendingRecovery = true;
                tab->ChangedAt = ImGui::GetTime();
            }
            ImGui::SameLine();
            if (ImGui::Button("Discard recovery")) tab->Document.DiscardRecovery();
        }
        if (tab && ImGui::GetTime() - i.LastDiskCheck > 1)
        {
            i.ExternalChanged = tab->Document.HasExternalChange();
            i.LastDiskCheck = ImGui::GetTime();
        }
        if (tab && i.ExternalChanged)
        {
            ImGui::TextColored({ 1, .75f, .3f, 1 }, "File changed on disk. Your edits are kept.");
            ImGui::SameLine();
            if (ImGui::Button("Reload disk version"))
            {
                ImGui::OpenPopup("Reload file?");
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(tab->ReadOnly);
            if (ImGui::Button("Overwrite disk...")) ImGui::OpenPopup("Overwrite file?");
            ImGui::EndDisabled();
        }
        if (ImGui::BeginPopupModal("Reload file?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted("Discard this tab's edits and reload from disk?");
            if (ImGui::Button("Reload"))
            {
                if (tab)
                {
                    if (tab->Document.Reload())
                    {
                        tab->Document.DiscardRecovery();
                        tab->Editor.SetText(tab->Document.Text());
                        tab->Revision = tab->Editor.GetRevision();
                        tab->PendingRecovery = false;
                        i.ExternalChanged = false;
                    }
                    else i.Error = tab->Document.Error();
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if (ImGui::BeginPopupModal("Overwrite file?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted("Replace the external version with this tab's text?");
            if (ImGui::Button("Overwrite"))
            {
                if (tab) i.Save(*tab, true);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if (i.ShowFind)
        {
            ImGui::SetNextItemWidth(210);
            ImGui::InputTextWithHint("##find", "Find text", &i.FindText);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(210);
            ImGui::InputTextWithHint("##replace", "Replacement", &i.ReplaceText);
            ImGui::SameLine();
            ImGui::Checkbox("Case", &i.CaseSensitive);
            ImGui::SameLine();
            if (ImGui::Button("Next") && tab && !i.FindText.empty())
            {
                auto hits = CodeDocument::Find(tab->Document.Text(), i.FindText, i.CaseSensitive);
                if (!hits.empty())
                {
                    auto offset = hits[i.NextMatch++ % hits.size()];
                    auto a = Position(tab->Document.Text(), offset), b = Position(tab->Document.Text(), offset + i.FindText.size());
                    tab->Editor.SelectRegion(a.first, a.second, b.first, b.second);
                    tab->Editor.SetViewAtLine(a.first, TextEditor::SetViewAtLineMode::Centered);
                }
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(!tab || tab->ReadOnly);
            if (ImGui::Button("Replace all") && tab && !i.FindText.empty())
            {
                try
                {
                    auto replacement = CodeDocument::ReplaceAll(tab->Document.Text(), i.FindText, i.ReplaceText, i.CaseSensitive);
                    tab->Editor.SelectAll();
                    i.Insert(*tab, replacement);
                }
                catch (const std::exception& e)
                {
                    i.Error = e.what();
                }
            }
            ImGui::EndDisabled();
        }
        ImGui::Separator();
        float bottom = 24;
        float h = std::max(150.f, ImGui::GetContentRegionAvail().y - bottom);
        ImGui::BeginChild("sourceTree", { 200, h }, ImGuiChildFlags_Borders);
        ImGui::TextUnformatted("PROJECT / SOURCE");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##files", "Filter files...", &i.FileQuery);
        if (ImGui::Button("Refresh") || ImGui::GetTime() - i.LastScan > 5)
        {
            i.Scan();
            i.LastScan = ImGui::GetTime();
        }
        for (auto& file : i.Files)
        {
            auto label = file.lexically_relative(i.Root).generic_string();
            if (Contains(label, i.FileQuery) && ImGui::Selectable(label.c_str(), tab && tab->Document.Path() == std::filesystem::weakly_canonical(file))) Open(file);
        }
        if (i.Files.empty()) ImGui::TextWrapped("No C++ files yet. Use New script to create a gameplay script.");
        ImGui::EndChild();
        ImGui::SameLine();
        float codeWidth = std::max(200.f, ImGui::GetContentRegionAvail().x - (i.ShowAPI ? 275 : 0));
        ImGui::BeginChild("codeArea", { codeWidth, h });
        if (ImGui::BeginTabBar("sourceTabs", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_FittingPolicyScroll))
        {
            for (size_t n = 0; n < i.Tabs.size(); ++n)
            {
                auto& t = *i.Tabs[n];
                bool open = true;
                std::string title = t.Document.Path().filename().string() + "###" + t.Document.Path().generic_string();
                auto flags = (n == i.Active && i.Focus ? ImGuiTabItemFlags_SetSelected : 0) | (t.Document.Dirty() ? ImGuiTabItemFlags_UnsavedDocument : 0);
                if (ImGui::BeginTabItem(title.c_str(), &open, flags))
                {
                    if (i.Active != n)
                    {
                        i.Active = n;
                        i.LastDiskCheck = -100;
                    }
                    tab = &t;
                    int line = 0, col = 0;
                    t.Editor.GetCursorPosition(line, col);
                    if (ImGui::SmallButton("Undo")) t.Editor.Undo();
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Redo")) t.Editor.Redo();
                    ImGui::SameLine();
                    if (ImGui::SmallButton("External editor") && OnExternalOpen) OnExternalOpen(t.Document.Path(), line + 1, col + 1);
                    ImGui::SameLine();
                    static int gotoLine = 1;
                    ImGui::SetNextItemWidth(70);
                    ImGui::InputInt("##goto", &gotoLine, 0);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Go to line"))
                    {
                        t.JumpLine = std::clamp(gotoLine - 1, 0, std::max(0, t.Editor.GetLineCount() - 1));
                        t.JumpColumn = 0;
                    }
                    if (t.ReadOnly) ImGui::TextDisabled("READ ONLY / outside the current project");
                    if (t.JumpLine >= 0)
                    {
                        int row = std::clamp(t.JumpLine, 0, std::max(0, t.Editor.GetLineCount() - 1));
                        t.Editor.SetCursorPosition(row, t.JumpColumn);
                        t.Editor.SetViewAtLine(row, TextEditor::SetViewAtLineMode::Centered);
                        t.JumpLine = -1;
                    }
                    ImGui::PushFont(CodeFont());
                    ImGui::SetWindowFontScale(i.Zoom);
                    if (i.Focus) ImGui::SetNextWindowFocus();
                    bool focused = t.Editor.Render("##source", false, { 0, std::max(80.f, ImGui::GetContentRegionAvail().y - 90) });
                    i.Focused |= focused;
                    i.TextFocused = focused;

                    ImGui::SetWindowFontScale(1);
                    ImGui::PopFont();
                    i.Focus = false;
                    i.Sync(t);
                    if (t.PendingRecovery && ImGui::GetTime() - t.ChangedAt > .75)
                    {
                        if (t.Document.WriteRecovery()) t.PendingRecovery = false;
                        else i.Error = "Cannot write recovery copy. Save the source file.";
                        t.ChangedAt = ImGui::GetTime();
                    }
                    ImGui::BeginChild("fileDiagnostics", { 0, 80 }, ImGuiChildFlags_Borders);
                    bool any = false;
                    for (auto& d : i.Diagnostics)
                    {
                        std::error_code ec;
                        if (!d.File.empty() && std::filesystem::weakly_canonical(d.File, ec) == t.Document.Path())
                        {
                            any = true;
                            ImGui::PushStyleColor(ImGuiCol_Text, d.Severity == DiagnosticSeverity::Error ? ImVec4(1, .45f, .4f, 1) : ImVec4(1, .8f, .4f, 1));
                            if (ImGui::Selectable((std::to_string(d.Line) + ":" + std::to_string(d.Column) + "  " + d.Message).c_str()))
                            {
                                t.JumpLine = std::max(0, d.Line - 1);
                                t.JumpColumn = std::max(0, d.Column - 1);
                                i.Focus = true;
                            }
                            ImGui::PopStyleColor();
                        }
                    }
                    if (!any) ImGui::TextDisabled("Build messages for this file appear here.");
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
                if (!open)
                {
                    if (t.Document.Dirty())
                    {
                        i.Close = (int)n;
                    }
                    else { i.Close = (int)n; }
                }
            }
            ImGui::EndTabBar();
        }
        if (i.Tabs.empty()) ImGui::TextWrapped("Open a script from the Source list, Content Browser, Inspector, or a compiler error.");
        ImGui::EndChild();
        if (i.ShowAPI)
        {
            ImGui::SameLine();
            ImGui::BeginChild("codeAPI", { 0, h }, ImGuiChildFlags_Borders);
            ImGui::TextUnformatted("ZAMIN API / SNIPPETS");
            ImGui::InputTextWithHint("##api", "Search API...", &i.APIQuery);
            if (ImGui::CollapsingHeader("Gameplay snippets", ImGuiTreeNodeFlags_DefaultOpen))
                for (auto& s : Snippets)
                    if (Contains(s.Name, i.APIQuery))
                    {
                        ImGui::BeginDisabled(!tab || tab->ReadOnly);
                        if (ImGui::Selectable(s.Name) && tab) i.Insert(*tab, s.Text);
                        ImGui::EndDisabled();
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", s.Text);
                    }
            ImGui::SeparatorText("ScriptableEntity API");
            ImGui::TextDisabled("Names / signatures; C++ compiler checks types");
            for (auto& [name, signature] : API)
                if (Contains(name, i.APIQuery))
                {
                    ImGui::BeginDisabled(!tab || tab->ReadOnly);
                    if (ImGui::Selectable(name.c_str()) && tab) i.Insert(*tab, name);
                    ImGui::EndDisabled();
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", signature.c_str());
                }
            ImGui::EndChild();
        }
        if (tab)
        {
            int line, col;
            tab->Editor.GetCursorPosition(line, col);
            ImGui::TextDisabled("%s | Ln %d, Col %d | UTF-8%s / %s | %s", tab->Document.Path().filename().string().c_str(), line + 1, col + 1, tab->Document.BOM() ? " BOM" : "", tab->Document.CRLF() ? "CRLF" : "LF", tab->Document.Dirty() ? "Modified" : "Saved");
        }
// Render close confirmation in the same root popup scope regardless of which tab was clicked.
        if ((i.Close == -2 || (i.Close >= 0 && (size_t)i.Close < i.Tabs.size() && i.Tabs[i.Close]->Document.Dirty())) && !ImGui::IsPopupOpen("Unsaved code")) ImGui::OpenPopup("Unsaved code");
        if (ImGui::BeginPopupModal("Unsaved code", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted("Save modified code before closing?");
            if (ImGui::Button("Save"))
            {
                bool ok = i.Close == -2 ? SaveAll() : i.Save(*i.Tabs[i.Close]);
                if (ok)
                {
                    if (i.Close == -2) *show = false;
                    else i.Tabs.erase(i.Tabs.begin() + i.Close);
                    i.Close = -1;
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Discard"))
            {
                if (i.Close == -2)
                {
                    for (auto& t : i.Tabs) t->Document.DiscardRecovery();
                    i.Tabs.clear();
                    i.Active = 0;
                    *show = false;
                }
                else
                {
                    i.Tabs[i.Close]->Document.DiscardRecovery();
                    i.Tabs.erase(i.Tabs.begin() + i.Close);
                }
                i.Close = -1;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
            {
                i.Close = -1;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        else if (i.Close >= 0 && (size_t)i.Close < i.Tabs.size() && !i.Tabs[i.Close]->Document.Dirty())
        {
            i.Tabs.erase(i.Tabs.begin() + i.Close);
            i.Close = -1;
        }
        if (i.Active >= i.Tabs.size()) i.Active = i.Tabs.empty() ? 0 : i.Tabs.size() - 1;
        tab = i.Current();

        if (tab && i.TextFocused && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Space, false))
        {
            int line, byte;
            tab->Editor.GetCursorBytePosition(line, byte);
            auto lines = tab->Editor.GetTextLines();
            if (line >= 0 && (size_t)line < lines.size())
            {
                byte = std::clamp(byte, 0, (int)lines[line].size());
                int start = byte;
                while (start > 0 && (std::isalnum((unsigned char)lines[line][start - 1]) || lines[line][start - 1] == '_')) --start;
                i.CompleteLine = line;
                i.CompleteStart = start;
                i.CompleteEnd = byte;
                i.APIQuery = lines[line].substr(start, byte - start);
                i.Completion = true;
                ImGui::OpenPopup("API completion");
            }
        }
        if (ImGui::BeginPopup("API completion"))
        {
            if (i.Completion)
            {
                ImGui::SetKeyboardFocusHere();
                i.Completion = false;
            }
            ImGui::InputTextWithHint("##complete", "API name...", &i.APIQuery);
            ImGui::TextDisabled("API names; compiler validates C++ types");
            int count = 0;
            for (auto& [name, signature] : API)
                if (Contains(name, i.APIQuery) && count++ < 20)
                {
                    if (ImGui::Selectable(name.c_str()) || (count == 1 && ImGui::IsKeyPressed(ImGuiKey_Enter, false)))
                    {
                        if (tab)
                        {
                            tab->Editor.SelectRegion(i.CompleteLine, i.CompleteStart, i.CompleteLine, i.CompleteEnd);
                            i.Insert(*tab, name);
                        }
                        ImGui::CloseCurrentPopup();
                        break;
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", signature.c_str());
                }
            ImGui::EndPopup();
        }
        if (requestSave)
        {
            if (auto* t = i.Current()) i.Save(*t);
        }
        if (requestSaveAll) SaveAll();
        if (requestBuild && SaveAll() && OnBuild) OnBuild();

        ImGui::End();
    }
}
