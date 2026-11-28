#include "Zamin/Editor/CodeIntelligence.h"
#include "Zamin/Utils/FileSystem.h"
#include "ZaminSDKCompileFlags.inc"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <set>
#include <sstream>
#include <unistd.h>

namespace Zamin {
    namespace fs = std::filesystem;
    using Lsp::json;
    namespace {
        double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
        bool IsSource(const fs::path& file)
        {
            const auto e = file.extension().string();
            return e == ".cpp" || e == ".cc" || e == ".cxx" || e == ".c++";
        }
        bool Executable(const fs::path& file)
        {
            std::error_code ec;
            return fs::is_regular_file(file, ec) && access(file.c_str(), X_OK) == 0;
        }
        fs::path OnPath(const std::string& name)
        {
            const char* path = std::getenv("PATH");
            if (!path) return {};
            std::stringstream list(path);
            for (std::string dir; std::getline(list, dir, ':');)
                if (!dir.empty() && Executable(fs::path(dir) / name)) return fs::path(dir) / name;
            return {};
        }
        // A compile command line ("command" form) split as a POSIX shell would (quotes, backslashes).
        std::vector<std::string> SplitCommand(const std::string& command)
        {
            std::vector<std::string> out;
            std::string current;
            bool any = false;
            char quote = 0;
            for (size_t i = 0; i < command.size(); ++i)
            {
                const char c = command[i];
                if (quote)
                {
                    if (c == quote) quote = 0;
                    else if (c == '\\' && quote == '"' && i + 1 < command.size() && (command[i + 1] == '"' || command[i + 1] == '\\')) current += command[++i];
                    else current += c;
                }
                else if (c == '\'' || c == '"')
                {
                    quote = c;
                    any = true;
                }
                else if (c == '\\' && i + 1 < command.size())
                {
                    current += command[++i];
                    any = true;
                }
                else if (c == ' ' || c == '\t' || c == '\n')
                {
                    if (any || !current.empty()) out.push_back(current);
                    current.clear();
                    any = false;
                }
                else current += c;
            }
            if (any || !current.empty()) out.push_back(current);
            return out;
        }
        // GCC options clang's driver does not know: clangd would show "unknown argument" in every file.
        bool GccOnly(const std::string& arg)
        {
            return arg == "-fno-gnu-unique" || arg.starts_with("-fconcepts-diagnostics-depth") || arg.starts_with("-fdeps-") ||
                   arg == "-fmodules-ts" || arg.starts_with("-fmodule-mapper") || arg == "-fno-semantic-interposition";
        }
        std::vector<std::string> Arguments(const json& entry)
        {
            std::vector<std::string> args;
            if (auto a = entry.find("arguments"); a != entry.end() && a->is_array())
            {
                for (const auto& v : *a)
                    if (v.is_string()) args.push_back(v.get<std::string>());
            }
            else if (auto c = entry.find("command"); c != entry.end() && c->is_string()) args = SplitCommand(c->get<std::string>());
            std::erase_if(args, GccOnly);
            if (std::find(args.begin(), args.end(), "-Wno-unknown-warning-option") == args.end()) args.push_back("-Wno-unknown-warning-option");
            return args;
        }
        fs::path EntryFile(const json& entry)
        {
            if (!entry.is_object() || !entry.contains("file") || !entry["file"].is_string()) return {};
            fs::path file = entry["file"].get<std::string>();
            if (file.is_relative() && entry.contains("directory") && entry["directory"].is_string()) file = fs::path(entry["directory"].get<std::string>()) / file;
            std::error_code ec;
            auto canonical = fs::weakly_canonical(file, ec);
            return ec ? file.lexically_normal() : canonical;
        }
        void ReadDatabase(const fs::path& file, const fs::path& under, std::map<std::string, json>& out)
        {
            std::error_code ec;
            if (!fs::is_regular_file(file, ec)) return;
            json list = json::parse(FileSystem::ReadTextFile(file), nullptr, false);
            if (!list.is_array()) return;
            for (const auto& entry : list)
            {
                const fs::path source = EntryFile(entry);
                if (source.empty() || !FileSystem::IsSubPath(source, under) || out.count(source.string())) continue;
                json copy = { { "directory", entry.value("directory", file.parent_path().string()) }, { "file", source.string() }, { "arguments", Arguments(entry) } };
                out.emplace(source.string(), std::move(copy));
            }
        }
    }

    CodeIntelligence::CodeIntelligence() = default;
    CodeIntelligence::~CodeIntelligence() { Shutdown(); }

    std::string CodeIntelligence::Key(const fs::path& file)
    {
        std::error_code ec;
        auto canonical = fs::weakly_canonical(file, ec);
        return (ec ? file.lexically_normal() : canonical).string();
    }

    fs::path CodeIntelligence::FindClangd(const std::string& preferred)
    {
        if (!preferred.empty())
        {
            if (preferred.find('/') != std::string::npos) return Executable(preferred) ? fs::path(preferred) : fs::path();
            return OnPath(preferred);
        }
        if (auto found = OnPath("clangd"); !found.empty()) return found;
        for (int version = 21; version >= 14; --version)
            if (auto found = OnPath("clangd-" + std::to_string(version)); !found.empty()) return found;
        return {};
    }

    std::vector<fs::path> CodeIntelligence::ListSources() const
    {
        std::vector<fs::path> out;
        std::error_code ec;
        const fs::path root = m_Settings.SourceDirectory.empty() ? m_Settings.ProjectRoot / "Source" : m_Settings.SourceDirectory;
        for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec))
        {
            if (it->is_directory(ec) && it->path().filename().string().starts_with('.')) it.disable_recursion_pending();
            if (it->is_regular_file(ec) && IsSource(it->path())) out.push_back(Key(it->path()));
            if (out.size() >= 4000) break;
        }
        std::sort(out.begin(), out.end());
        return out;
    }

    int CodeIntelligence::WriteCompilationDatabase(const Settings& settings, std::string* error)
    {
        CodeIntelligence scan;
        scan.m_Settings = settings;
        const auto sources = scan.ListSources();
        std::map<std::string, json> known;
        const fs::path root = Key(settings.ProjectRoot);
        ReadDatabase(settings.ProjectRoot / "Build" / "compile_commands.json", root, known);
        if (!settings.EngineBuildDirectory.empty()) ReadDatabase(settings.EngineBuildDirectory / "compile_commands.json", root, known);
        json list = json::array();
        const json* model = nullptr;
        for (const auto& source : sources)
            if (auto it = known.find(source.string()); it != known.end())
            {
                model = &it->second;
                break;
            }
        for (const auto& source : sources)
        {
            if (auto it = known.find(source.string()); it != known.end())
            {
                list.push_back(it->second);
                continue;
            }
            json entry;
            if (model)
            {
                // A source added since the project was configured: its neighbour's flags, with this file.
                const std::string from = (*model)["file"].get<std::string>();
                std::vector<std::string> args = (*model)["arguments"].get<std::vector<std::string>>();
                for (size_t i = 0; i < args.size(); ++i)
                {
                    if (args[i] == from || (fs::path(args[i]).is_relative() && Key(fs::path((*model)["directory"].get<std::string>()) / args[i]) == from)) args[i] = source.string();
                    else if (args[i] == "-o" && i + 1 < args.size()) args[++i] = (fs::path(settings.CacheDirectory) / "clangd" / (source.filename().string() + ".o")).string();
                }
                entry = { { "directory", (*model)["directory"] }, { "file", source.string() }, { "arguments", args } };
            }
            else
            {
                // No compile database yet: the SDK's own flags (the engine build's).
                std::vector<std::string> args = { ZAMIN_SDK_CXX_COMPILER, "-std=c++20", "-fPIC" };
                const char* includes[] = { ZAMIN_SDK_INCLUDES nullptr };
                const char* definitions[] = { ZAMIN_SDK_DEFINITIONS nullptr };
                for (const char** i = includes; *i; ++i) args.push_back((i == includes ? std::string("-I") : std::string("-isystem")) + *i);
                for (const char** d = definitions; *d; ++d) args.push_back(std::string("-D") + *d);
                args.push_back("-I" + (settings.SourceDirectory.empty() ? (settings.ProjectRoot / "Source") : settings.SourceDirectory).string());
                args.push_back("-Wno-unknown-warning-option");
                args.push_back("-c");
                args.push_back(source.string());
                entry = { { "directory", settings.ProjectRoot.string() }, { "file", source.string() }, { "arguments", args } };
            }
            list.push_back(std::move(entry));
        }
        const fs::path dir = settings.CacheDirectory / "clangd";
        std::error_code ec;
        fs::create_directories(dir, ec);
        const std::string text = list.dump(2) + "\n";
        const fs::path file = dir / "compile_commands.json";
        if ((!fs::exists(file, ec) || FileSystem::ReadTextFile(file) != text) && !FileSystem::WriteTextFileAtomic(file, text))
        {
            if (error) *error = "Cannot write " + file.string();
            return -1;
        }
        return int(list.size());
    }

    void CodeIntelligence::SetStatus(Status status, std::string text)
    {
        m_Status = status;
        m_StatusText = std::move(text);
    }

    void CodeIntelligence::Configure(const Settings& settings)
    {
        if (settings == m_Settings && m_Status != Status::Off) return;
        Shutdown();
        m_Settings = settings;
        m_Restarts.clear();
        if (settings.ProjectRoot.empty()) return;
        Start();
    }

    void CodeIntelligence::Shutdown()
    {
        if (m_Client) m_Client->Stop(1.0);
        m_Client.reset();
        m_Documents.clear();
        m_Diagnostics.clear();
        ++m_DiagnosticsRevision;
        SetStatus(Status::Off, {});
    }

    void CodeIntelligence::Start()
    {
        m_ClangdPath = FindClangd(m_Settings.ClangdPath);
        if (m_ClangdPath.empty())
        {
            SetStatus(Status::NotFound, m_Settings.ClangdPath.empty()
                                            ? "clangd not found - catalogue completion only (install clangd or set its path in Preferences > Code editor)"
                                            : "clangd not found at '" + m_Settings.ClangdPath + "' - catalogue completion only (Preferences > Code editor)");
            return;
        }
        std::string error;
        m_KnownSources = ListSources();
        m_LastSourceScan = Now();
        if (WriteCompilationDatabase(m_Settings, &error) < 0)
        {
            SetStatus(Status::Failed, "clangd: " + error);
            return;
        }
        const fs::path database = m_Settings.CacheDirectory / "clangd";
        std::vector<std::string> command = CommandOverride ? CommandOverride(m_ClangdPath, database)
                                                           : std::vector<std::string>{ m_ClangdPath.string(),
                                                                                       "--compile-commands-dir=" + database.string(),
                                                                                       m_Settings.BackgroundIndex ? "--background-index" : "--background-index=false",
                                                                                       "--header-insertion=never",
                                                                                       "--header-insertion-decorators=false",
                                                                                       "--limit-results=200",
                                                                                       "--pch-storage=memory",
                                                                                       "-j=2",
                                                                                       "--log=error" };
        m_Client = std::make_unique<Lsp::LspClient>();
        m_Client->OnNotification = [this](const std::string& method, const json& params) { OnNotification(method, params); };
        m_Client->OnReady = [this] {
            // "Ubuntu clangd version 18.1.3 (1ubuntu1)" / "18.1.8": the first word that starts with a digit.
            std::string version, info = m_Client->GetServerInfo().value("version", std::string());
            std::istringstream words(info);
            for (std::string word; words >> word;)
                if (!word.empty() && std::isdigit((unsigned char)word[0]))
                {
                    version = word;
                    break;
                }
            m_ServerVersion = version;
            SetStatus(Status::Ready, "clangd " + version + " - semantic completion, F12 definition, F2 rename, live errors");
            for (auto& [key, document] : m_Documents)
            {
                document.ChangedAt = Now();
                m_Client->Notify("textDocument/didOpen", { { "textDocument", { { "uri", Lsp::PathToUri(document.File) }, { "languageId", "cpp" }, { "version", document.Version }, { "text", document.Text } } } });
            }
        };
        m_Client->OnExit = [this](const std::string& reason) {
            const double now = Now();
            std::erase_if(m_Restarts, [&](double t) { return now - t > 60; });
            if (int(m_Restarts.size()) >= MaxRestarts)
            {
                SetStatus(Status::Failed, "clangd stopped " + std::to_string(m_Restarts.size() + 1) + " times in a minute (" + reason +
                                              ") - catalogue completion only; reopen the project to try again");
                return;
            }
            m_RestartAt = now + std::min(8.0, double(1 << m_Restarts.size()) * 0.5);
            m_Restarts.push_back(now);
            SetStatus(Status::Restarting, "clangd " + reason + " - restarting...");
        };
        Lsp::LspClient::Options options;
        options.Command = std::move(command);
        options.Root = m_Settings.ProjectRoot;
        options.StderrLog = database / "clangd.log";
        options.InitializationOptions = { { "clangdFileStatus", false } };
        if (!m_Client->Start(options))
        {
            SetStatus(Status::Failed, m_Client->Error() + " - catalogue completion only");
            m_Client.reset();
            return;
        }
        SetStatus(Status::Starting, "clangd starting...");
    }

    void CodeIntelligence::Poll()
    {
        if (m_Client) m_Client->Poll();
        const double now = Now();
        if (m_Status == Status::Restarting && now >= m_RestartAt)
        {
            ++m_TotalRestarts;
            if (m_Client) m_Client->Stop(0.2);
            m_Client.reset();
            Start();
            return;
        }
        // New or removed sources: a new compile database (clangd reads it again when it changed).
        if ((m_Status == Status::Ready || m_Status == Status::Starting) && now - m_LastSourceScan > 2.0)
        {
            m_LastSourceScan = now;
            auto sources = ListSources();
            if (sources != m_KnownSources)
            {
                m_KnownSources = std::move(sources);
                WriteCompilationDatabase(m_Settings);
            }
        }
    }

    void CodeIntelligence::Open(const fs::path& file, const std::string& text)
    {
        const std::string key = Key(file);
        if (m_Documents.count(key))
        {
            Change(file, text);
            return;
        }
        auto& document = m_Documents[key];
        document.File = key;
        document.Text = text;
        document.Version = 1;
        document.ChangedAt = Now();
        if (IsReady())
            m_Client->Notify("textDocument/didOpen", { { "textDocument", { { "uri", Lsp::PathToUri(document.File) }, { "languageId", "cpp" }, { "version", document.Version }, { "text", text } } } });
    }

    void CodeIntelligence::Change(const fs::path& file, const std::string& text)
    {
        auto it = m_Documents.find(Key(file));
        if (it == m_Documents.end())
        {
            Open(file, text);
            return;
        }
        auto& document = it->second;
        if (document.Text == text) return;
        auto change = Lsp::ComputeChange(document.Text, text, GetEncoding());
        document.Text = text;
        ++document.Version;
        document.ChangedAt = Now();
        if (IsReady() && change)
            m_Client->Notify("textDocument/didChange", { { "textDocument", { { "uri", Lsp::PathToUri(document.File) }, { "version", document.Version } } },
                                                         { "contentChanges", json::array({ Lsp::ToJson(*change) }) } });
    }

    void CodeIntelligence::Save(const fs::path& file)
    {
        auto it = m_Documents.find(Key(file));
        if (it != m_Documents.end() && IsReady()) m_Client->Notify("textDocument/didSave", { { "textDocument", TextDocument(it->second) } });
    }

    void CodeIntelligence::Close(const fs::path& file)
    {
        const std::string key = Key(file);
        auto it = m_Documents.find(key);
        if (it == m_Documents.end()) return;
        if (IsReady()) m_Client->Notify("textDocument/didClose", { { "textDocument", TextDocument(it->second) } });
        m_Documents.erase(it);
        if (m_Diagnostics.erase(key)) ++m_DiagnosticsRevision;
    }

    int CodeIntelligence::GetVersion(const fs::path& file) const
    {
        auto it = m_Documents.find(Key(file));
        return it == m_Documents.end() ? 0 : it->second.Version;
    }

    const std::string* CodeIntelligence::GetText(const fs::path& file) const
    {
        auto it = m_Documents.find(Key(file));
        return it == m_Documents.end() ? nullptr : &it->second.Text;
    }

    Lsp::Position CodeIntelligence::PositionAt(const Document& document, size_t offset) const { return Lsp::PositionOf(document.Text, offset, GetEncoding()); }

    bool CodeIntelligence::Current(const std::string& key, int version) const
    {
        auto it = m_Documents.find(key);
        return it != m_Documents.end() && it->second.Version == version;
    }

    void CodeIntelligence::Complete(const fs::path& file, size_t offset, std::function<void(Lsp::CompletionList, const std::string&)> done)
    {
        const std::string key = Key(file);
        auto it = m_Documents.find(key);
        if (!IsReady() || it == m_Documents.end())
        {
            done({}, "clangd is not ready");
            return;
        }
        const int version = it->second.Version;
        const double start = Now();
        m_Client->Request("textDocument/completion", { { "textDocument", TextDocument(it->second) }, { "position", Lsp::ToJson(PositionAt(it->second, offset)) }, { "context", { { "triggerKind", 1 } } } },
                          [this, key, version, start, done](const json& result, const std::string& error) {
                              if (!Current(key, version))
                              {
                                  ++m_StaleDropped;
                                  return;
                              }
                              m_LastCompletionMs = (Now() - start) * 1000.0;
                              done(error.empty() ? Lsp::ParseCompletion(result) : Lsp::CompletionList{}, error);
                          },
                          8.0);
    }

    void CodeIntelligence::Definition(const fs::path& file, size_t offset, std::function<void(std::vector<Lsp::Location>, const std::string&)> done)
    {
        const std::string key = Key(file);
        auto it = m_Documents.find(key);
        if (!IsReady() || it == m_Documents.end())
        {
            done({}, "clangd is not ready");
            return;
        }
        const int version = it->second.Version;
        m_Client->Request("textDocument/definition", { { "textDocument", TextDocument(it->second) }, { "position", Lsp::ToJson(PositionAt(it->second, offset)) } },
                          [this, key, version, done](const json& result, const std::string& error) {
                              if (!Current(key, version))
                              {
                                  ++m_StaleDropped;
                                  return;
                              }
                              done(error.empty() ? Lsp::ParseLocations(result) : std::vector<Lsp::Location>{}, error);
                          },
                          10.0);
    }

    void CodeIntelligence::Hover(const fs::path& file, size_t offset, std::function<void(std::string, const std::string&)> done)
    {
        const std::string key = Key(file);
        auto it = m_Documents.find(key);
        if (!IsReady() || it == m_Documents.end())
        {
            done({}, "clangd is not ready");
            return;
        }
        const int version = it->second.Version;
        m_Client->Request("textDocument/hover", { { "textDocument", TextDocument(it->second) }, { "position", Lsp::ToJson(PositionAt(it->second, offset)) } },
                          [this, key, version, done](const json& result, const std::string& error) {
                              if (!Current(key, version))
                              {
                                  ++m_StaleDropped;
                                  return;
                              }
                              done(error.empty() ? Lsp::ParseHover(result) : std::string(), error);
                          },
                          5.0);
    }

    void CodeIntelligence::Rename(const fs::path& file, size_t offset, const std::string& newName, std::function<void(std::vector<Lsp::FileEdits>, const std::string&)> done)
    {
        const std::string key = Key(file);
        auto it = m_Documents.find(key);
        if (!IsReady() || it == m_Documents.end())
        {
            done({}, "clangd is not ready");
            return;
        }
        const int version = it->second.Version;
        m_Client->Request("textDocument/rename", { { "textDocument", TextDocument(it->second) }, { "position", Lsp::ToJson(PositionAt(it->second, offset)) }, { "newName", newName } },
                          [this, key, version, done](const json& result, const std::string& error) {
                              if (!Current(key, version))
                              {
                                  ++m_StaleDropped;
                                  return;
                              }
                              std::string problem = error;
                              auto edits = problem.empty() ? Lsp::ParseWorkspaceEdit(result, &problem) : std::vector<Lsp::FileEdits>{};
                              if (problem.empty() && edits.empty()) problem = "Nothing to rename here";
                              done(std::move(edits), problem);
                          },
                          30.0);
    }

    const std::vector<Lsp::Diagnostic>* CodeIntelligence::GetDiagnostics(const fs::path& file) const
    {
        auto it = m_Diagnostics.find(Key(file));
        return it == m_Diagnostics.end() ? nullptr : &it->second;
    }

    void CodeIntelligence::OnNotification(const std::string& method, const json& params)
    {
        if (method != "textDocument/publishDiagnostics") return;
        auto published = Lsp::ParseDiagnostics(params);
        if (published.File.empty()) return;
        const std::string key = Key(published.File);
        auto it = m_Documents.find(key);
        if (it != m_Documents.end() && published.Version && *published.Version != it->second.Version)
        {
            ++m_StaleDropped; // for an older text: positions would be wrong
            return;
        }
        if (it != m_Documents.end() && it->second.ChangedAt > 0)
        {
            m_LastDiagnosticsMs = (Now() - it->second.ChangedAt) * 1000.0;
            it->second.ChangedAt = 0;
        }
        m_Diagnostics[key] = std::move(published.Diagnostics);
        ++m_DiagnosticsRevision;
    }
}
