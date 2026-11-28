#include "CodeEditorPanel.h"
#include "VisualGraphLayout.h"
#include "Zamin/Editor/CodeDocument.h"
#include "Zamin/Editor/CodeIntelligence.h"
#include "Zamin/Editor/EditorLanguage.h"
#include "Zamin/Project/Project.h"
#include "Zamin/Editor/ExternalEditor.h"
#include "Zamin/Utils/FileSystem.h"
#include "../../../vendor/ImGuiColorTextEdit/TextEditor.h"
#include <imgui.h>
#include <imgui_internal.h>
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
        // The byte offset of (line, byte in the line) in a text.
        size_t Offset(std::string_view text, int line, int byte)
        {
            size_t at = 0;
            for (int l = 0; l < line && at < text.size(); ++l)
            {
                const size_t next = text.find('\n', at);
                if (next == std::string_view::npos) return text.size();
                at = next + 1;
            }
            const size_t end = std::min(text.find('\n', at), text.size());
            return std::min(at + (size_t)std::max(0, byte), end);
        }
        bool IsIdentifier(const std::string& name)
        {
            if (name.empty() || std::isdigit((unsigned char)name[0])) return false;
            return std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::isalnum(c) || c == '_'; });
        }
        ImU32 SeverityColor(int severity)
        {
            return severity == 1 ? IM_COL32(255, 85, 85, 255) : severity == 2 ? IM_COL32(235, 190, 60, 255) : IM_COL32(110, 170, 255, 255);
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
            bool Cpp = false;               // a C++ file clangd knows
            uint64_t SentRevision = ~0ull;  // the editor revision clangd has
            std::vector<Lsp::Diagnostic> Live; // clangd's diagnostics, the squiggles' messages (same order)
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
        CodeEditorPanel* Owner = nullptr;
        // clangd (CodeIntelligence): settings, semantic completion, rename, hover, transient notices.
        std::string ClangdPath;
        bool UseClangd = true;
        uint64_t SeenDiagnostics = ~0ull;
        std::vector<CompilerDiagnostic> Live;
        std::vector<Lsp::CompletionItem> Semantic;
        bool SemanticPending = false;
        double SemanticAsked = 0;
        std::string SemanticError;
        int CompleteSelected = 0;
        bool RenameOpenRequest = false, RenameFocus = false, RenamePending = false;
        std::string RenameOld, RenameNew, RenameError;
        std::filesystem::path RenameFile;
        size_t RenameOffset = 0;
        std::vector<Lsp::FileChange> RenamePlan;
        struct RenameUndo
        {
            std::vector<std::pair<std::filesystem::path, int>> Tabs; // the file and its undo index after the rename
            std::string From, To;
        };
        std::optional<RenameUndo> LastRename;
        std::string Notice;
        double NoticeAt = -100;
        int HoverLine = -1, HoverChar = -1, HoverAskedLine = -1, HoverAskedChar = -1;
        double HoverSince = 0;
        std::string HoverText;
        bool HoverPending = false;
        Tab* Current() { return Active < Tabs.size() ? Tabs[Active].get() : nullptr; }
        Tab* Find(const std::filesystem::path& file)
        {
            std::error_code ec;
            const auto path = std::filesystem::weakly_canonical(file, ec);
            for (auto& t : Tabs)
                if (t->Document.Path() == path) return t.get();
            return nullptr;
        }
        void Say(std::string text)
        {
            Notice = std::move(text);
            NoticeAt = ImGui::GetTime();
        }
        // clangd gets every change of an open C++ file (incremental didChange).
        void Send(Tab& t)
        {
            Sync(t);
            if (t.Cpp && t.SentRevision != t.Revision)
            {
                Intel.Change(t.Document.Path(), t.Document.Text());
                t.SentRevision = t.Revision;
            }
        }
        size_t CursorOffset(Tab& t)
        {
            int line = 0, byte = 0;
            t.Editor.GetCursorBytePosition(line, byte);
            return Offset(t.Document.Text(), line, byte);
        }
        void RebuildDiagnostics();
        void GoToDefinition(Tab& t, size_t offset);
        void Navigate(const Lsp::Location& location);
        void RequestRename(Tab& t);
        void ApplyRename();
        void UndoRename();
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
        CodeIntelligence Intel; // last: destroyed first, while the state its callbacks write is still alive
    };

    void CodeEditorPanel::Impl::RebuildDiagnostics()
    {
        Live.clear();
        const auto encoding = Intel.GetEncoding();
        for (auto& t : Tabs)
        {
            t->Live.clear();
            std::vector<TextEditor::Squiggle> squiggles;
            const auto* list = t->Cpp ? Intel.GetDiagnostics(t->Document.Path()) : nullptr;
            if (list)
                for (const auto& d : *list)
                {
                    const std::string& text = t->Document.Text();
                    const auto a = Position(text, Lsp::OffsetOf(text, d.Range.Start, encoding));
                    auto b = Position(text, Lsp::OffsetOf(text, d.Range.End, encoding));
                    if (b.first != a.first) b = { a.first, (int)(Offset(text, a.first, 1 << 30) - Offset(text, a.first, 0)) };
                    squiggles.push_back({ a.first, a.second, b.second, SeverityColor(d.Severity) });
                    t->Live.push_back(d);
                    CompilerDiagnostic c;
                    c.Severity = d.Severity == 1 ? DiagnosticSeverity::Error : d.Severity == 2 ? DiagnosticSeverity::Warning : DiagnosticSeverity::Note;
                    c.File = t->Document.Path().string();
                    c.Line = a.first + 1;
                    c.Column = a.second + 1;
                    c.Message = d.Message;
                    Live.push_back(std::move(c));
                }
            t->Editor.SetSquiggles(std::move(squiggles));
        }
    }

    void CodeEditorPanel::Impl::GoToDefinition(Tab& t, size_t offset)
    {
        if (!t.Cpp) return;
        if (!Intel.IsReady())
        {
            Say(Tr("Ta'rifga o'tish uchun clangd kerak (Sozlamalar > Kod muharriri)"));
            return;
        }
        Send(t);
        Intel.Definition(t.Document.Path(), offset, [this](std::vector<Lsp::Location> found, const std::string& error) {
            if (found.empty()) Say(error.empty() ? std::string(Tr("Ta'rif topilmadi")) : "clangd: " + error);
            else Navigate(found.front());
        });
    }

    void CodeEditorPanel::Impl::Navigate(const Lsp::Location& location)
    {
        std::string text;
        if (auto* open = Find(location.File)) text = open->Document.Text();
        else
        {
            text = FileSystem::ReadTextFile(location.File);
            std::erase(text, '\r');
        }
        const auto at = Position(text, Lsp::OffsetOf(text, location.Range.Start, Intel.GetEncoding()));
        if (Owner->Open(location.File, at.first + 1, at.second + 1))
            Say(TrFormat("Ta'rif: {}:{}", location.File.filename().string(), at.first + 1));
    }

    void CodeEditorPanel::Impl::RequestRename(Tab& t)
    {
        RenameError.clear();
        RenamePlan.clear();
        RenamePending = true;
        Send(t);
        Intel.Rename(t.Document.Path(), RenameOffset, RenameNew, [this](std::vector<Lsp::FileEdits> edits, const std::string& error) {
            RenamePending = false;
            if (!error.empty())
            {
                RenameError = "clangd: " + error;
                return;
            }
            auto read = [this](const std::filesystem::path& file) -> std::optional<std::string> {
                if (auto* open = Find(file))
                {
                    Sync(*open);
                    return open->Document.Text();
                }
                std::error_code ec;
                if (!std::filesystem::is_regular_file(file, ec)) return std::nullopt;
                std::string text = FileSystem::ReadTextFile(file);
                std::erase(text, '\r');
                return text;
            };
            std::string problem;
            RenamePlan = Lsp::PlanChanges(edits, Intel.GetEncoding(), read, &problem);
            if (!problem.empty()) RenameError = problem;
            for (const auto& change : RenamePlan)
                if (Root.empty() || !FileSystem::IsSubPathOnDisk(change.File, Root))
                {
                    RenameError = TrFormat("{} loyihadan tashqarida (faqat o'qish uchun): nom o'zgartirilmaydi", change.File.filename().string());
                    RenamePlan.clear();
                    break;
                }
        });
    }

    // Every file of the plan in a tab, each change one undoable edit, then every file saved (atomic writes). When a file
    // cannot be opened, changed since the preview or saved, every tab is put back (undo, saved again): all or nothing.
    void CodeEditorPanel::Impl::ApplyRename()
    {
        const std::filesystem::path keep = Current() ? Current()->Document.Path() : std::filesystem::path();
        std::vector<Tab*> tabs;
        std::vector<std::filesystem::path> opened;
        std::string problem;
        for (const auto& change : RenamePlan)
        {
            Tab* t = Find(change.File);
            if (!t)
            {
                if (!Owner->Open(change.File)) problem = Error;
                else opened.push_back(change.File);
                t = Find(change.File);
            }
            if (!t)
            {
                if (problem.empty()) problem = TrFormat("{} ochilmadi", change.File.filename().string());
                break;
            }
            Sync(*t);
            if (t->ReadOnly) problem = TrFormat("{} faqat o'qish uchun", change.File.filename().string());
            else if (t->Document.Text() != change.Before) problem = TrFormat("{} ko'rib chiqilgandan keyin o'zgardi - qaytadan ko'rib chiqing", change.File.filename().string());
            if (!problem.empty()) break;
            tabs.push_back(t);
        }
        auto closeOpened = [&] {
            for (const auto& file : opened)
                for (size_t n = 0; n < Tabs.size(); ++n)
                    if (Tabs[n]->Document.Path() == std::filesystem::weakly_canonical(file) && !Tabs[n]->Document.Dirty())
                    {
                        if (Tabs[n]->Cpp) Intel.Close(Tabs[n]->Document.Path());
                        Tabs.erase(Tabs.begin() + (std::ptrdiff_t)n);
                        break;
                    }
        };
        auto restoreActive = [&] {
            for (size_t n = 0; n < Tabs.size(); ++n)
                if (Tabs[n]->Document.Path() == keep) Active = n;
            Focus = true;
        };
        if (!problem.empty())
        {
            closeOpened();
            restoreActive();
            RenameError = problem;
            return;
        }
        RenameUndo undo{ {}, RenameOld, RenameNew };
        std::vector<Tab*> edited;
        auto rollback = [&](size_t saved) {
            for (size_t n = 0; n < edited.size(); ++n)
            {
                edited[n]->Editor.Undo();
                Sync(*edited[n]);
                if (n < saved) Save(*edited[n]);
            }
        };
        for (size_t n = 0; n < tabs.size(); ++n)
        {
            auto& t = *tabs[n];
            const auto& change = RenamePlan[n];
            auto region = Lsp::ComputeChange(change.Before, change.After, Lsp::Encoding::UTF8); // UTF-8 characters = bytes
            if (!region) continue;
            t.Editor.SelectRegion(region->Range.Start.Line, region->Range.Start.Character, region->Range.End.Line, region->Range.End.Character);
            t.Editor.InsertText(region->Text);
            Sync(t);
            edited.push_back(&t);
            if (t.Document.Text() != change.After)
            {
                rollback(0);
                closeOpened();
                restoreActive();
                RenameError = TrFormat("{}: tahrir qo'llanmadi", t.Document.Path().filename().string());
                return;
            }
            undo.Tabs.emplace_back(t.Document.Path(), t.Editor.GetUndoIndex());
        }
        for (size_t n = 0; n < edited.size(); ++n)
            if (!Save(*edited[n]))
            {
                const std::string why = Error;
                rollback(n);
                closeOpened();
                restoreActive();
                RenameError = TrFormat("Saqlanmadi ({}) - hamma fayllar qaytarildi", why);
                return;
            }
            else if (edited[n]->Cpp) Intel.Save(edited[n]->Document.Path());
        restoreActive();
        Say(TrFormat("'{}' -> '{}': {} ta fayl - Ctrl+Z yoki 'Nom o'zgarishini bekor qilish' qaytaradi", RenameOld, RenameNew, undo.Tabs.size()));
        LastRename = std::move(undo);
        RenamePlan.clear();
    }

    void CodeEditorPanel::Impl::UndoRename()
    {
        if (!LastRename) return;
        int undone = 0;
        for (const auto& [file, index] : LastRename->Tabs)
            if (Tab* t = Find(file); t && t->Editor.GetUndoIndex() == index && t->Editor.CanUndo())
            {
                t->Editor.Undo();
                Sync(*t);
                if (Save(*t))
                {
                    ++undone;
                    if (t->Cpp) Intel.Save(t->Document.Path());
                }
            }
        Say(TrFormat("Nom o'zgarishi bekor qilindi: {} ta fayl", undone));
        LastRename.reset();
    }
    CodeEditorPanel::CodeEditorPanel() : m_Impl(std::make_unique<Impl>()) { m_Impl->Owner = this; }
    void CodeEditorPanel::SetClangd(const std::string& path, bool enabled)
    {
        m_Impl->ClangdPath = path;
        m_Impl->UseClangd = enabled;
    }
    const std::vector<CompilerDiagnostic>& CodeEditorPanel::GetLiveDiagnostics() const { return m_Impl->Live; }
    std::string CodeEditorPanel::GetClangdStatus() const
    {
        const auto& intel = m_Impl->Intel;
        switch (intel.GetStatus())
        {
            case CodeIntelligence::Status::Off: return m_Impl->UseClangd ? Tr("clangd: loyiha ochilganda ishga tushadi") : Tr("clangd o'chirilgan - faqat katalog to'ldirish");
            case CodeIntelligence::Status::NotFound:
                return m_Impl->ClangdPath.empty() ? Tr("clangd topilmadi - faqat katalog to'ldirish (clangd o'rnating yoki yo'lini Sozlamalar > Kod muharririda kiriting)")
                                                  : TrFormat("clangd '{}' da topilmadi - faqat katalog to'ldirish (Sozlamalar > Kod muharriri)", m_Impl->ClangdPath);
            case CodeIntelligence::Status::Starting: return Tr("clangd ishga tushmoqda...");
            case CodeIntelligence::Status::Ready: return TrFormat("clangd {} - semantik to'ldirish, F12 ta'rif, F2 nomini o'zgartirish, jonli xatolar", intel.GetServerVersion());
            default: return intel.GetStatusText();
        }
    }
    CodeEditorPanel::~CodeEditorPanel() { FlushRecovery(); }
    void CodeEditorPanel::SetProject(const std::filesystem::path& root, const std::filesystem::path& cache)
    {
        auto& i = *m_Impl;
        if (i.Root == root && i.Cache == cache) return;
        FlushRecovery();
        for (auto& t : i.Tabs)
            if (t->Cpp) i.Intel.Close(t->Document.Path());
        i.Tabs.clear();
        i.LastRename.reset();
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
        t->Editor.SetCtrlClickNavigates(true);
        t->Cpp = ExternalEditor::IsSourceFile(path);
        if (t->Cpp)
        {
            i.Intel.Open(path, t->Document.Text());
            t->Revision = t->Editor.GetRevision();
            t->SentRevision = t->Revision;
        }
        if (line > 0)
        {
            t->JumpLine = line - 1;
            t->JumpColumn = std::max(0, column - 1);
        }
        i.Tabs.push_back(std::move(t));
        i.SeenDiagnostics = ~0ull;
        i.Active = i.Tabs.size() - 1;
        i.Focus = true;
        i.Error.clear();
        return true;
    }
    void CodeEditorPanel::OnImGuiRender(bool* show)
    {
        auto& i = *m_Impl;
        // clangd follows the project and the preferences; it runs while tabs are open even when the window is hidden.
        {
            CodeIntelligence::Settings settings;
            if (i.UseClangd && !i.Root.empty())
            {
                settings.ClangdPath = i.ClangdPath;
                settings.ProjectRoot = i.Root;
                settings.SourceDirectory = Project::GetSourceDirectory().empty() ? i.Root / "Source" : Project::GetSourceDirectory();
                settings.CacheDirectory = i.Cache.empty() ? i.Root / ".zamin" : i.Cache;
                settings.EngineBuildDirectory = FileSystem::GetExecutableDirectory().parent_path();
            }
            if (settings.ProjectRoot.empty()) { if (i.Intel.GetStatus() != CodeIntelligence::Status::Off) i.Intel.Shutdown(); }
            else i.Intel.Configure(settings);
            const bool wasReady = i.Intel.IsReady();
            i.Intel.Poll();
            if (!wasReady && i.Intel.IsReady())
                for (auto& t : i.Tabs) t->SentRevision = t->Revision; // reopened with the current text
            for (auto& t : i.Tabs)
                if (t->Cpp && t->SentRevision != t->Editor.GetRevision()) i.Send(*t);
            if (i.Intel.GetDiagnosticsRevision() != i.SeenDiagnostics)
            {
                i.SeenDiagnostics = i.Intel.GetDiagnosticsRevision();
                i.RebuildDiagnostics();
            }
        }
        if (!*show)
        {
            i.Focused = false;
            FlushRecovery();
            return;
        }
        if (i.Focus) ImGui::SetNextWindowFocus();
        ImGui::SetNextWindowSize({ 1400, 800 }, ImGuiCond_FirstUseEver);
        bool visible = ImGui::Begin(Tr("Kod muharriri###CodeEditor"), show);
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
        GraphLayout::RecordRegion("CodeEditor");
        if (GraphLayout::Recording())
        {
            const ImRect title = ImGui::GetCurrentWindow()->TitleBarRect();
            GraphLayout::RecordRect("CodeEditor/Title", title.Min, title.Max);
        }
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
        {
            // The semantic features' state: one line (clangd's version, or why only the catalogue completion works).
            const auto status = i.Intel.GetStatus();
            const std::string line = GetClangdStatus();
            if (status == CodeIntelligence::Status::Ready) ImGui::TextDisabled("%s", line.c_str());
            else if (status == CodeIntelligence::Status::Starting || status == CodeIntelligence::Status::Off) ImGui::TextDisabled("%s", line.c_str());
            else ImGui::TextColored({ 1, .72f, .3f, 1 }, "%s", line.c_str());
            GraphLayout::RecordKey("CodeEditor/ClangdStatus");
            static const char* names[] = { "Off", "NotFound", "Starting", "Ready", "Restarting", "Failed" };
            GraphLayout::RecordValue("CodeEditor/Clangd", std::string(names[(int)status]));
            GraphLayout::RecordValue("CodeEditor/ClangdStatus", line);
            GraphLayout::RecordValue("CodeEditor/CompletionMs", i.Intel.GetLastCompletionMs());
            GraphLayout::RecordValue("CodeEditor/DiagnosticsMs", i.Intel.GetLastDiagnosticsMs());
            if (!i.Notice.empty() && ImGui::GetTime() - i.NoticeAt < 12)
            {
                ImGui::SameLine();
                ImGui::TextColored({ .55f, .85f, 1, 1 }, "|  %s", i.Notice.c_str());
                GraphLayout::RecordKey("CodeEditor/Notice");
                GraphLayout::RecordValue("CodeEditor/Notice", i.Notice);
                if (i.LastRename)
                {
                    ImGui::SameLine();
                    if (ImGui::SmallButton(Tr("Nom o'zgarishini bekor qilish"))) i.UndoRename();
                    GraphLayout::RecordKey("CodeEditor/UndoRename");
                }
            }
        }
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
            if (!Contains(label, i.FileQuery)) continue;
            if (ImGui::Selectable(label.c_str(), tab && tab->Document.Path() == std::filesystem::weakly_canonical(file))) Open(file);
            GraphLayout::RecordKey("CodeEditor/Source/" + label);
        }
        if (i.Files.empty()) ImGui::TextWrapped("No C++ files yet. Use New script to create a gameplay script.");
        ImGui::EndChild();
        ImGui::SameLine();
        float codeWidth = std::max(200.f, ImGui::GetContentRegionAvail().x - (i.ShowAPI ? 275 : 0));
        ImGui::BeginChild("codeArea", { codeWidth, h });
        if (ImGui::BeginTabBar("sourceTabs", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_FittingPolicyScroll))
        {
            // A tab opened or chosen this frame: ImGui selects it from the next frame on, so the tab still shown
            // this frame must not take Active back.
            const int select = i.Focus ? (int)i.Active : -1;
            for (size_t n = 0; n < i.Tabs.size(); ++n)
            {
                auto& t = *i.Tabs[n];
                bool open = true;
                std::string title = t.Document.Path().filename().string() + "###" + t.Document.Path().generic_string();
                auto flags = ((int)n == select ? ImGuiTabItemFlags_SetSelected : 0) | (t.Document.Dirty() ? ImGuiTabItemFlags_UnsavedDocument : 0);
                if (ImGui::BeginTabItem(title.c_str(), &open, flags))
                {
                    if (i.Active != n && select < 0)
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
                    // Ctrl+Z right after a rename undoes it in every file it changed (the editor's own undo is held back
                    // for this frame); F12 / Ctrl+click: go to the definition; F2: rename the symbol at the cursor.
                    const bool keys = i.TextFocused && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
                    bool holdUndo = false;
                    if (keys && i.LastRename && io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false))
                        for (const auto& [file, index] : i.LastRename->Tabs)
                            if (file == t.Document.Path() && t.Editor.GetUndoIndex() == index) holdUndo = true;
                    if (holdUndo)
                    {
                        i.UndoRename();
                        t.Editor.SetReadOnlyEnabled(true); // this frame's Ctrl+Z is the rename's undo, not one more
                    }
                    ImGui::PushFont(CodeFont());
                    ImGui::SetWindowFontScale(i.Zoom);
                    if (i.Focus && ((int)n == select || select < 0)) ImGui::SetNextWindowFocus();
                    bool focused = t.Editor.Render("##source", false, { 0, std::max(80.f, ImGui::GetContentRegionAvail().y - 90) });
                    const ImVec2 textMin = ImGui::GetItemRectMin(), textMax = ImGui::GetItemRectMax();
                    if (holdUndo) t.Editor.SetReadOnlyEnabled(t.ReadOnly);
                    i.Focused |= focused;
                    i.TextFocused = focused;
                    if (GraphLayout::Recording())
                    {
                        GraphLayout::RecordRect("CodeEditor/Text", textMin, textMax);
                        const ImVec2 origin = t.Editor.GetCharScreenPosition(0, 0);
                        GraphLayout::RecordValue("CodeEditor/OriginX", origin.x);
                        GraphLayout::RecordValue("CodeEditor/OriginY", origin.y);
                        GraphLayout::RecordValue("CodeEditor/LineHeight", t.Editor.GetLineHeight());
                        GraphLayout::RecordValue("CodeEditor/CharWidth", t.Editor.GetCharWidth());
                        GraphLayout::RecordValue("CodeEditor/ActiveFile", t.Document.Path().string());
                        int cl = 0, cb = 0;
                        t.Editor.GetCursorBytePosition(cl, cb);
                        GraphLayout::RecordValue("CodeEditor/CursorLine", cl);
                        GraphLayout::RecordValue("CodeEditor/CursorByte", cb);
                        GraphLayout::RecordValue("CodeEditor/Squiggles", (double)t.Editor.GetSquiggles().size());
                        GraphLayout::RecordValue("CodeEditor/Dirty", t.Document.Dirty() ? 1.0 : 0.0);
                        GraphLayout::RecordValue("CodeEditor/ReadOnly", t.ReadOnly ? 1.0 : 0.0);
                        for (size_t n = 0; n < t.Editor.GetSquiggles().size(); ++n)
                        {
                            const auto& q = t.Editor.GetSquiggles()[n];
                            const ImVec2 a = t.Editor.GetCharScreenPosition(q.mLine, q.mStartChar), b = t.Editor.GetCharScreenPosition(q.mLine, q.mEndChar);
                            GraphLayout::RecordRect("CodeEditor/Squiggle/" + std::to_string(n), a, { std::max(b.x, a.x + t.Editor.GetCharWidth()), a.y + t.Editor.GetLineHeight() });
                            if (n < t.Live.size()) GraphLayout::RecordValue("CodeEditor/SquiggleMessage/" + std::to_string(n), t.Live[n].Message);
                        }
                    }

                    ImGui::SetWindowFontScale(1);
                    ImGui::PopFont();
                    if ((int)n == select || select < 0) i.Focus = false; // a newly chosen tab keeps the request until it is shown
                    i.Sync(t);
                    int clickLine = 0, clickByte = 0;
                    if (t.Editor.ConsumeCtrlClick(clickLine, clickByte))
                    {
                        t.Editor.ClearExtraCursors();
                        i.GoToDefinition(t, Offset(t.Document.Text(), clickLine, clickByte));
                    }
                    if (keys && ImGui::IsKeyPressed(ImGuiKey_F12, false)) i.GoToDefinition(t, i.CursorOffset(t));
                    if (keys && ImGui::IsKeyPressed(ImGuiKey_F2, false) && t.Cpp)
                    {
                        const size_t offset = i.CursorOffset(t);
                        const auto [a, b] = Lsp::IdentifierAt(t.Document.Text(), offset);
                        if (a == b) i.Say(Tr("Nomini o'zgartirish uchun kursorni nom ustiga qo'ying"));
                        else if (!i.Intel.IsReady()) i.Say(Tr("Nom o'zgartirish uchun clangd kerak (Sozlamalar > Kod muharriri)"));
                        else if (t.ReadOnly) i.Say(Tr("Bu fayl faqat o'qish uchun"));
                        else
                        {
                            i.RenameOld = t.Document.Text().substr(a, b - a);
                            i.RenameNew = i.RenameOld;
                            i.RenameFile = t.Document.Path();
                            i.RenameOffset = a;
                            i.RenamePlan.clear();
                            i.RenameError.clear();
                            i.RenamePending = false;
                            i.RenameOpenRequest = true;
                            i.RenameFocus = true;
                        }
                    }
                    // A squiggle's message, else clangd's hover text for the name under the mouse (after 0.5 s).
                    if (int hovered = t.Editor.GetHoveredSquiggle(); hovered >= 0 && hovered < (int)t.Live.size())
                        ImGui::SetTooltip("%s", t.Live[hovered].Message.c_str());
                    else if (t.Cpp && i.Intel.IsReady() && ImGui::IsMouseHoveringRect(textMin, textMax) && !ImGui::IsAnyMouseDown() &&
                             !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId))
                    {
                        int line = 0, byte = 0;
                        if (t.Editor.GetMouseCharPosition(line, byte))
                        {
                            if (line != i.HoverLine || byte != i.HoverChar)
                            {
                                i.HoverLine = line;
                                i.HoverChar = byte;
                                i.HoverSince = ImGui::GetTime();
                                i.HoverText.clear();
                            }
                            else if (ImGui::GetTime() - i.HoverSince > .5 && (i.HoverAskedLine != line || i.HoverAskedChar != byte) && !i.HoverPending)
                            {
                                i.HoverAskedLine = line;
                                i.HoverAskedChar = byte;
                                i.HoverPending = true;
                                i.Send(t);
                                i.Intel.Hover(t.Document.Path(), Offset(t.Document.Text(), line, byte), [&i, line, byte](std::string text, const std::string&) {
                                    i.HoverPending = false;
                                    if (line == i.HoverLine && byte == i.HoverChar) i.HoverText = std::move(text);
                                });
                            }
                            if (!i.HoverText.empty() && line == i.HoverAskedLine && byte == i.HoverAskedChar)
                            {
                                ImGui::BeginTooltip();
                                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 40);
                                ImGui::TextUnformatted(i.HoverText.substr(0, 1500).c_str());
                                ImGui::PopTextWrapPos();
                                ImGui::EndTooltip();
                            }
                        }
                        else i.HoverLine = -1;
                    }
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
                            GraphLayout::RecordKey("CodeEditor/FileDiagnostic/" + std::to_string(&d - i.Diagnostics.data()));
                            ImGui::PopStyleColor();
                        }
                    }
                    for (size_t n = 0; n < t.Live.size(); ++n)
                    {
                        const auto& d = t.Live[n];
                        any = true;
                        const auto& q = t.Editor.GetSquiggles();
                        const int line = n < q.size() ? q[n].mLine : 0, byte = n < q.size() ? q[n].mStartChar : 0;
                        ImGui::PushStyleColor(ImGuiCol_Text, d.Severity == 1 ? ImVec4(1, .45f, .4f, 1) : d.Severity == 2 ? ImVec4(1, .8f, .4f, 1) : ImVec4(.55f, .75f, 1, 1));
                        if (ImGui::Selectable((std::to_string(line + 1) + ":" + std::to_string(byte + 1) + "  clangd: " + d.Message + "##live" + std::to_string(n)).c_str()))
                        {
                            t.JumpLine = line;
                            t.JumpColumn = byte;
                            i.Focus = true;
                        }
                        GraphLayout::RecordKey("CodeEditor/LiveDiagnostic/" + std::to_string(n));
                        ImGui::PopStyleColor();
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
                    else
                    {
                        if (i.Tabs[i.Close]->Cpp) i.Intel.Close(i.Tabs[i.Close]->Document.Path());
                        i.Tabs.erase(i.Tabs.begin() + i.Close);
                    }
                    i.Close = -1;
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Discard"))
            {
                if (i.Close == -2)
                {
                    for (auto& t : i.Tabs)
                    {
                        t->Document.DiscardRecovery();
                        if (t->Cpp) i.Intel.Close(t->Document.Path());
                    }
                    i.Tabs.clear();
                    i.Active = 0;
                    *show = false;
                }
                else
                {
                    i.Tabs[i.Close]->Document.DiscardRecovery();
                    if (i.Tabs[i.Close]->Cpp) i.Intel.Close(i.Tabs[i.Close]->Document.Path());
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
            if (i.Tabs[i.Close]->Cpp) i.Intel.Close(i.Tabs[i.Close]->Document.Path());
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
                i.CompleteSelected = 0;
                i.Semantic.clear();
                i.SemanticError.clear();
                i.SemanticPending = false;
                // Semantic completion from clangd (project classes, members, locals); the catalogue when it is not there.
                if (tab->Cpp && i.Intel.IsReady())
                {
                    i.Send(*tab);
                    i.SemanticPending = true;
                    i.SemanticAsked = ImGui::GetTime();
                    i.Intel.Complete(tab->Document.Path(), Offset(tab->Document.Text(), line, byte), [&i](Lsp::CompletionList list, const std::string& error) {
                        i.SemanticPending = false;
                        i.SemanticError = error;
                        i.Semantic = std::move(list.Items);
                    });
                }
                ImGui::OpenPopup("API completion");
            }
        }
        if (i.SemanticPending && ImGui::GetTime() - i.SemanticAsked > 8)
        {
            i.SemanticPending = false; // a stale answer never comes: the catalogue instead
            i.SemanticError = "no answer";
        }
        if (ImGui::BeginPopup("API completion"))
        {
            if (i.Completion)
            {
                ImGui::SetKeyboardFocusHere();
                i.Completion = false;
            }
            ImGui::InputTextWithHint("##complete", "API name...", &i.APIQuery);
            GraphLayout::RecordKey("CodeEditor/CompletionQuery");
            // The candidates: clangd's (when it answered with any), else the Zamin API catalogue.
            struct Candidate
            {
                std::string Label, Insert, Detail;
                int StartByte;
            };
            std::vector<Candidate> candidates;
            for (const auto& item : i.Semantic)
            {
                const std::string& filter = item.FilterText.empty() ? item.Label : item.FilterText;
                if (!Contains(filter, i.APIQuery) || candidates.size() >= 40) continue;
                int startByte = i.CompleteStart;
                if (item.Edit && item.Edit->Range.Start.Line == i.CompleteLine && tab)
                {
                    const std::string& text = tab->Document.Text();
                    startByte = Position(text, Lsp::OffsetOf(text, item.Edit->Range.Start, i.Intel.GetEncoding())).second;
                    if (startByte > i.CompleteEnd) startByte = i.CompleteStart;
                }
                candidates.push_back({ item.Label, item.Text(), item.Detail.empty() ? std::string(item.KindName()) : item.Detail, startByte });
            }
            const bool semantic = !candidates.empty();
            if (!semantic)
                for (auto& [name, signature] : API)
                    if (Contains(name, i.APIQuery) && candidates.size() < 20) candidates.push_back({ name, name, signature, i.CompleteStart });
            const char* source = semantic ? "clangd" : i.SemanticPending ? "pending" : "catalogue";
            GraphLayout::RecordValue("CodeEditor/CompletionSource", std::string(source));
            GraphLayout::RecordValue("CodeEditor/CompletionCount", (double)candidates.size());
            if (semantic) ImGui::TextDisabled("%s", Tr("clangd: loyiha turlari va a'zolari"));
            else if (i.SemanticPending) ImGui::TextDisabled("%s", Tr("clangd'dan so'ralmoqda (hozircha API katalogi)"));
            else ImGui::TextDisabled("API names; compiler validates C++ types");
            if (!candidates.empty())
            {
                if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) i.CompleteSelected = std::min(i.CompleteSelected + 1, (int)candidates.size() - 1);
                if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) i.CompleteSelected = std::max(i.CompleteSelected - 1, 0);
            }
            i.CompleteSelected = std::clamp(i.CompleteSelected, 0, std::max(0, (int)candidates.size() - 1));
            const bool enter = ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
            for (size_t n = 0; n < candidates.size(); ++n)
            {
                const auto& c = candidates[n];
                const bool picked = ImGui::Selectable((c.Label + "##c" + std::to_string(n)).c_str(), (int)n == i.CompleteSelected) || (enter && (int)n == i.CompleteSelected);
                GraphLayout::RecordKey("CodeEditor/Completion/" + c.Label);
                if (!c.Detail.empty())
                {
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", c.Detail.c_str());
                }
                if (picked)
                {
                    if (tab)
                    {
                        tab->Editor.SelectRegion(i.CompleteLine, c.StartByte, i.CompleteLine, i.CompleteEnd);
                        i.Insert(*tab, c.Insert);
                    }
                    ImGui::CloseCurrentPopup();
                    break;
                }
                if (ImGui::IsItemHovered() && !c.Detail.empty()) ImGui::SetTooltip("%s", c.Detail.c_str());
            }
            if (GraphLayout::Recording()) GraphLayout::RecordRegion("CodeEditor/CompletionPopup");
            ImGui::EndPopup();
        }
        // F2: the new name, a preview of every edit in every file, applied all together (one undo restores them).
        if (i.RenameOpenRequest)
        {
            ImGui::OpenPopup(Tr("Nomini o'zgartirish###CodeRename"));
            i.RenameOpenRequest = false;
        }
        if (ImGui::BeginPopupModal(Tr("Nomini o'zgartirish###CodeRename"), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text(Tr("'%s' yangi nomi:"), i.RenameOld.c_str());
            if (i.RenameFocus)
            {
                ImGui::SetKeyboardFocusHere();
                i.RenameFocus = false;
            }
            ImGui::SetNextItemWidth(720);
            const bool submitted = ImGui::InputText("##renameName", &i.RenameNew, ImGuiInputTextFlags_EnterReturnsTrue);
            GraphLayout::RecordKey("CodeEditor/RenameName");
            const bool valid = IsIdentifier(i.RenameNew) && i.RenameNew != i.RenameOld;
            ImGui::BeginDisabled(!valid || i.RenamePending);
            bool preview = ImGui::Button((std::string(EditorText::Choose("Ko'rib chiqish", "Preview")) + "###renamePreview").c_str());
            GraphLayout::RecordKey("CodeEditor/RenamePreview");
            ImGui::EndDisabled();
            if ((preview || (submitted && i.RenamePlan.empty())) && valid && !i.RenamePending)
                if (auto* t = i.Find(i.RenameFile)) i.RequestRename(*t);
            ImGui::SameLine();
            ImGui::BeginDisabled(i.RenamePlan.empty() || i.RenamePending);
            const bool apply = ImGui::Button(TrFormat("Qo'llash ({} ta fayl)###renameApply", i.RenamePlan.size()).c_str()) ||
                               (submitted && !i.RenamePlan.empty());
            GraphLayout::RecordKey("CodeEditor/RenameApply");
            ImGui::EndDisabled();
            ImGui::SameLine();
            const bool cancel = ImGui::Button(Tr("Bekor")) || ImGui::IsKeyPressed(ImGuiKey_Escape, false);
            GraphLayout::RecordKey("CodeEditor/RenameCancel");
            if (!valid && !i.RenameNew.empty() && i.RenameNew != i.RenameOld) ImGui::TextColored({ 1, .45f, .4f, 1 }, "%s", Tr("C++ nomi emas"));
            if (i.RenamePending) ImGui::TextDisabled("%s", Tr("clangd'dan so'ralmoqda..."));
            if (!i.RenameError.empty()) ImGui::TextColored({ 1, .45f, .4f, 1 }, "%s", i.RenameError.c_str());
            GraphLayout::RecordValue("CodeEditor/RenameFiles", (double)i.RenamePlan.size());
            GraphLayout::RecordValue("CodeEditor/RenameError", i.RenameError);
            if (!i.RenamePlan.empty())
            {
                int edits = 0;
                for (const auto& c : i.RenamePlan) edits += c.Edits;
                ImGui::SeparatorText(TrFormat("Ko'rib chiqish: {} ta o'zgarish, {} ta fayl", edits, i.RenamePlan.size()).c_str());
                ImGui::BeginChild("renamePreview", { 720, std::min(360.f, 60.f + 22.f * (float)edits + 26.f * (float)i.RenamePlan.size()) }, ImGuiChildFlags_Borders);
                for (const auto& c : i.RenamePlan)
                {
                    const std::string label = i.Root.empty() ? c.File.filename().string() : c.File.lexically_relative(i.Root).generic_string();
                    ImGui::TextColored({ .55f, .85f, 1, 1 }, "%s", label.c_str());
                    GraphLayout::RecordKey("CodeEditor/RenameFile/" + c.File.filename().string());
                    ImGui::SameLine();
                    ImGui::TextDisabled("(%d)", c.Edits);
                    ImGui::PushFont(CodeFont());
                    for (const auto& line : Lsp::PreviewChange(c, 20))
                    {
                        ImGui::TextColored({ 1, .55f, .5f, 1 }, "%5d - %s", line.Line, line.Before.c_str());
                        ImGui::TextColored({ .5f, .9f, .55f, 1 }, "%5d + %s", line.Line, line.After.c_str());
                    }
                    ImGui::PopFont();
                }
                ImGui::EndChild();
            }
            if (GraphLayout::Recording()) GraphLayout::RecordRegion("CodeEditor/RenamePopup");
            if (apply && !i.RenamePlan.empty())
            {
                i.ApplyRename();
                if (i.RenameError.empty()) ImGui::CloseCurrentPopup();
            }
            if (cancel)
            {
                i.RenamePlan.clear();
                ImGui::CloseCurrentPopup();
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
