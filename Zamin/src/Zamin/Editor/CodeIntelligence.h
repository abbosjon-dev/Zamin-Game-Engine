#pragma once
// Semantic C++ features of the built-in code editor through clangd (LspClient): completion, go-to-definition, rename,
// hover and live diagnostics for the project's sources. Independent of the GUI: the code editor panel feeds it the
// open documents' texts and polls it every frame; every request answers through a callback on that thread.
//
// clangd is optional. It is looked up in the Preferences path, then on PATH (clangd, clangd-20 ... clangd-14); without it
// the status is NotFound and the editor keeps its catalogue completion. A crashed server is restarted (at most
// MaxRestarts times a minute, with a growing delay) and gets the open documents again. Responses for an older version
// of a document than the current one are dropped (stale), so a slow answer never lands on newer text.
//
// The compile database clangd reads is generated for the project (<cache>/clangd/compile_commands.json): the entries of
// the project's own CMake export (<project>/Build/compile_commands.json) and of the engine build's (the sample projects
// built with the engine), one for every source added since from a neighbour's flags, else the SDK's flags.
#include "Zamin/Editor/LspClient.h"
#include <functional>
#include <map>
#include <memory>
#include <optional>

namespace Zamin {
    class CodeIntelligence
    {
    public:
        enum class Status { Off, NotFound, Starting, Ready, Restarting, Failed };
        struct Settings
        {
            std::string ClangdPath;               // Preferences > Code editor; "" = PATH
            std::filesystem::path ProjectRoot, SourceDirectory, CacheDirectory;
            std::filesystem::path EngineBuildDirectory; // where the engine's own compile_commands.json is (samples)
            bool BackgroundIndex = true;          // project-wide rename / references need the index
            bool operator==(const Settings&) const = default;
        };
        static constexpr int MaxRestarts = 4;

        CodeIntelligence();
        ~CodeIntelligence();
        CodeIntelligence(const CodeIntelligence&) = delete;
        CodeIntelligence& operator=(const CodeIntelligence&) = delete;

        // The clangd executable: `preferred` (a path, or a name looked up on PATH), else clangd / clangd-NN on PATH.
        static std::filesystem::path FindClangd(const std::string& preferred = {});
        // Writes <cacheDirectory>/clangd/compile_commands.json for the project's sources; returns the entry count (-1 on
        // an error). Unchanged content is not rewritten.
        static int WriteCompilationDatabase(const Settings& settings, std::string* error = nullptr);

        // (Re)starts clangd for these settings when they changed; an empty ProjectRoot stops it.
        void Configure(const Settings& settings);
        void Shutdown();
        // Delivers responses, restarts a crashed server, keeps the compile database current (new sources). Call every frame.
        void Poll();

        Status GetStatus() const { return m_Status; }
        bool IsReady() const { return m_Status == Status::Ready; }
        // One line for the editor ("clangd 18.1.3 - semantic completion, F12, F2, live errors" / why it is off).
        const std::string& GetStatusText() const { return m_StatusText; }
        const std::filesystem::path& GetClangdPath() const { return m_ClangdPath; }
        // The running server's version ("18.1.3"; "" before it answered initialize).
        const std::string& GetServerVersion() const { return m_ServerVersion; }
        int GetRestartCount() const { return m_TotalRestarts; }
        Lsp::Encoding GetEncoding() const { return m_Client ? m_Client->GetEncoding() : Lsp::Encoding::UTF16; }
        int GetServerPid() const { return m_Client ? m_Client->GetPid() : -1; }

        // Documents (text is UTF-8 with LF). Change sends the incremental difference to the last text sent.
        void Open(const std::filesystem::path& file, const std::string& text);
        void Change(const std::filesystem::path& file, const std::string& text);
        void Save(const std::filesystem::path& file);
        void Close(const std::filesystem::path& file);
        bool IsOpen(const std::filesystem::path& file) const { return m_Documents.count(Key(file)) != 0; }
        int GetVersion(const std::filesystem::path& file) const;
        // The text clangd has for an open document (tests).
        const std::string* GetText(const std::filesystem::path& file) const;

        // Requests at a byte offset of the document's current text. The callback does not run when the document changed
        // in between (a stale response) or was closed; `error` is set on timeouts / server errors.
        void Complete(const std::filesystem::path& file, size_t offset, std::function<void(Lsp::CompletionList, const std::string& error)> done);
        void Definition(const std::filesystem::path& file, size_t offset, std::function<void(std::vector<Lsp::Location>, const std::string& error)> done);
        void Hover(const std::filesystem::path& file, size_t offset, std::function<void(std::string, const std::string& error)> done);
        void Rename(const std::filesystem::path& file, size_t offset, const std::string& newName,
                    std::function<void(std::vector<Lsp::FileEdits>, const std::string& error)> done);

        // The latest diagnostics of a file that match its current version (nullptr: none yet).
        const std::vector<Lsp::Diagnostic>* GetDiagnostics(const std::filesystem::path& file) const;
        const std::map<std::string, std::vector<Lsp::Diagnostic>>& GetAllDiagnostics() const { return m_Diagnostics; }
        // Increases whenever diagnostics arrive (the UI rebuilds its lists).
        uint64_t GetDiagnosticsRevision() const { return m_DiagnosticsRevision; }
        // Measurements: the last completion's round trip, the time from an edit to its diagnostics (ms; -1 none yet).
        double GetLastCompletionMs() const { return m_LastCompletionMs; }
        double GetLastDiagnosticsMs() const { return m_LastDiagnosticsMs; }
        size_t GetStaleDropped() const { return m_StaleDropped; }

        // Tests: the command the next start runs (default: the clangd found + its options).
        std::function<std::vector<std::string>(const std::filesystem::path& clangd, const std::filesystem::path& database)> CommandOverride;

    private:
        struct Document
        {
            std::filesystem::path File;
            std::string Text;
            int Version = 0;
            double ChangedAt = 0; // when the version the diagnostics wait for was sent
        };
        static std::string Key(const std::filesystem::path& file);
        void Start();
        void SetStatus(Status status, std::string text);
        Lsp::Position PositionAt(const Document& document, size_t offset) const;
        Lsp::json TextDocument(const Document& document) const { return { { "uri", Lsp::PathToUri(document.File) } }; }
        void OnNotification(const std::string& method, const Lsp::json& params);
        bool Current(const std::string& key, int version) const;
        std::vector<std::filesystem::path> ListSources() const;

        Settings m_Settings;
        std::unique_ptr<Lsp::LspClient> m_Client;
        std::filesystem::path m_ClangdPath;
        Status m_Status = Status::Off;
        std::string m_StatusText, m_ServerVersion;
        std::map<std::string, Document> m_Documents;
        std::map<std::string, std::vector<Lsp::Diagnostic>> m_Diagnostics;
        uint64_t m_DiagnosticsRevision = 0;
        std::vector<double> m_Restarts; // the times of the recent restarts
        double m_RestartAt = 0;
        int m_TotalRestarts = 0;
        double m_LastCompletionMs = -1, m_LastDiagnosticsMs = -1, m_LastSourceScan = 0;
        size_t m_StaleDropped = 0;
        std::vector<std::filesystem::path> m_KnownSources;
    };
}
