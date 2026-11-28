#include "Zamin/Editor/LspClient.h"
#include <chrono>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace Zamin::Lsp {
    double LspClient::Now()
    {
        return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    LspClient::~LspClient() { Stop(0.5); }

    bool LspClient::Start(const Options& options)
    {
        Stop(0.2);
        m_Error.clear();
        m_ExitReason.clear();
        if (options.Command.empty())
        {
            m_Error = "No language server command";
            return false;
        }
        int sockets[2];
        // A socket pair, not pipes: writes use MSG_NOSIGNAL, so a server that died never raises SIGPIPE in the editor.
        if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets) != 0)
        {
            m_Error = std::string("socketpair: ") + std::strerror(errno);
            return false;
        }
        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions, sockets[1], STDIN_FILENO);
        posix_spawn_file_actions_adddup2(&actions, sockets[1], STDOUT_FILENO);
        const std::string log = options.StderrLog.empty() ? std::string("/dev/null") : options.StderrLog.string();
        posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (!options.Root.empty()) posix_spawn_file_actions_addchdir_np(&actions, options.Root.c_str());
        posix_spawnattr_t attributes;
        posix_spawnattr_init(&attributes);
        posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP); // a terminal's Ctrl+C goes to the editor only
        posix_spawnattr_setpgroup(&attributes, 0);
        std::vector<char*> argv;
        for (const auto& a : options.Command) argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);
        pid_t pid = -1;
        const int spawned = posix_spawnp(&pid, argv[0], &actions, &attributes, argv.data(), environ);
        posix_spawn_file_actions_destroy(&actions);
        posix_spawnattr_destroy(&attributes);
        close(sockets[1]);
        if (spawned != 0)
        {
            close(sockets[0]);
            m_Error = "Could not start " + options.Command[0] + ": " + std::strerror(spawned);
            return false;
        }
        m_Pid = pid;
        m_Socket = sockets[0];
        m_Closed = false;
        m_Eof = false;
        m_State = State::Starting;
        m_Encoding = Encoding::UTF16;
        m_Reader = std::thread([this] { ReaderLoop(); });
        m_Writer = std::thread([this] { WriterLoop(); });

        const std::string root = PathToUri(options.Root);
        json capabilities = {
            { "general", { { "positionEncodings", { "utf-8", "utf-16" } } } },
            { "textDocument",
              { { "synchronization", { { "didSave", true }, { "dynamicRegistration", false } } },
                { "completion", { { "completionItem", { { "snippetSupport", false }, { "documentationFormat", { "plaintext" } } } }, { "contextSupport", true } } },
                { "hover", { { "contentFormat", { "plaintext" } } } },
                { "definition", { { "linkSupport", true } } },
                { "rename", { { "prepareSupport", true } } },
                { "publishDiagnostics", { { "versionSupport", true }, { "relatedInformation", false } } } } },
            { "workspace", { { "workspaceEdit", { { "documentChanges", true } } }, { "workspaceFolders", true }, { "configuration", false } } },
            { "window", { { "workDoneProgress", false } } }
        };
        json params = { { "processId", int(getpid()) }, { "rootUri", root }, { "capabilities", capabilities },
                        { "workspaceFolders", json::array({ { { "uri", root }, { "name", options.Root.filename().string() } } }) },
                        { "offsetEncoding", { "utf-8", "utf-16" } }, // clangd's extension before LSP 3.17's positionEncodings
                        { "clientInfo", { { "name", "Zamin Editor" } } } };
        if (!options.InitializationOptions.is_null()) params["initializationOptions"] = options.InitializationOptions;
        m_InitializeId = m_NextId++;
        m_Pending[m_InitializeId] = { [this](const json& result, const std::string& error) {
                                         if (!error.empty())
                                         {
                                             Fail("initialize failed: " + error);
                                             return;
                                         }
                                         m_Capabilities = result.value("capabilities", json::object());
                                         m_ServerInfo = result.value("serverInfo", json::object());
                                         const std::string encoding = m_Capabilities.value("positionEncoding", result.value("offsetEncoding", std::string("utf-16")));
                                         m_Encoding = encoding == "utf-8" ? Encoding::UTF8 : Encoding::UTF16;
                                         m_State = State::Ready;
                                         Send({ { "jsonrpc", "2.0" }, { "method", "initialized" }, { "params", json::object() } });
                                         for (auto& message : m_Queued) Send(message.second);
                                         m_Queued.clear();
                                         if (OnReady) OnReady();
                                     },
                                      Now() + options.InitializeTimeout, "initialize" };
        Send({ { "jsonrpc", "2.0" }, { "id", m_InitializeId }, { "method", "initialize" }, { "params", params } });
        return true;
    }

    void LspClient::Send(const json& message)
    {
        {
            std::lock_guard lock(m_Mutex);
            m_Outbox.push_back(Frame(message));
        }
        m_Wake.notify_one();
    }

    int LspClient::Request(const std::string& method, json params, Callback callback, double timeout)
    {
        const int id = m_NextId++;
        json message = { { "jsonrpc", "2.0" }, { "id", id }, { "method", method }, { "params", std::move(params) } };
        if (m_State == State::Stopped || m_State == State::Exited)
        {
            if (callback) callback(nullptr, "server stopped");
            return id;
        }
        m_Pending[id] = { std::move(callback), Now() + timeout, method };
        if (m_State == State::Ready) Send(message);
        else m_Queued.emplace_back(method, std::move(message));
        return id;
    }

    void LspClient::Notify(const std::string& method, json params)
    {
        if (m_State == State::Stopped || m_State == State::Exited) return;
        json message = { { "jsonrpc", "2.0" }, { "method", method }, { "params", std::move(params) } };
        if (m_State == State::Ready) Send(message);
        else m_Queued.emplace_back(method, std::move(message));
    }

    void LspClient::Cancel(int id)
    {
        if (m_Pending.erase(id) && m_State == State::Ready) Send({ { "jsonrpc", "2.0" }, { "method", "$/cancelRequest" }, { "params", { { "id", id } } } });
    }

    void LspClient::ReaderLoop()
    {
        FrameReader reader;
        char buffer[65536];
        while (!m_Closed)
        {
            const ssize_t n = recv(m_Socket, buffer, sizeof(buffer), 0);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) break;
            reader.Feed(std::string_view(buffer, size_t(n)));
            json message;
            for (;;)
            {
                if (!reader.Next(message))
                {
                    if (reader.Error().empty()) break;
                    continue; // a broken message is skipped
                }
                // A request from the server: answered at once (null), so the server never waits on the editor.
                if (message.contains("method") && message.contains("id"))
                {
                    std::lock_guard lock(m_Mutex);
                    m_Outbox.push_back(Frame({ { "jsonrpc", "2.0" }, { "id", message["id"] }, { "result", nullptr } }));
                    m_Wake.notify_one();
                    if (message["method"] != "workspace/configuration") continue;
                }
                std::lock_guard lock(m_Mutex);
                m_Inbox.push_back(std::move(message));
            }
        }
        m_Eof = true;
        m_Wake.notify_one();
    }

    void LspClient::WriterLoop()
    {
        for (;;)
        {
            std::string data;
            {
                std::unique_lock lock(m_Mutex);
                m_Wake.wait(lock, [this] { return m_Closed || m_Eof || !m_Outbox.empty(); });
                if (m_Outbox.empty()) return;
                data = std::move(m_Outbox.front());
                m_Outbox.pop_front();
            }
            size_t sent = 0;
            while (sent < data.size())
            {
                const ssize_t n = send(m_Socket, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
                if (n < 0 && errno == EINTR) continue;
                if (n <= 0) return; // the server is gone; the reader reports it
                sent += size_t(n);
            }
        }
    }

    void LspClient::Fail(const std::string& reason)
    {
        if (m_State == State::Stopped) return;
        m_State = State::Exited;
        m_ExitReason = reason;
        m_Queued.clear();
        auto pending = std::move(m_Pending);
        m_Pending.clear();
        for (auto& [id, p] : pending)
            if (p.Done) p.Done(nullptr, "server stopped");
        if (OnExit) OnExit(reason);
    }

    void LspClient::Poll()
    {
        if (m_State == State::Stopped) return;
        std::deque<json> inbox;
        {
            std::lock_guard lock(m_Mutex);
            inbox.swap(m_Inbox);
        }
        for (auto& message : inbox)
        {
            if (message.contains("id") && !message.contains("method"))
            {
                if (!message["id"].is_number_integer()) continue;
                auto it = m_Pending.find(message["id"].get<int>());
                if (it == m_Pending.end()) continue; // cancelled or timed out: a stale response
                auto done = std::move(it->second.Done);
                m_Pending.erase(it);
                std::string error;
                if (auto e = message.find("error"); e != message.end() && e->is_object()) error = e->value("message", std::string("error"));
                if (done) done(message.value("result", json()), error);
            }
            else if (message.contains("method") && OnNotification)
                OnNotification(message["method"].get<std::string>(), message.value("params", json::object()));
            if (m_State == State::Stopped || m_State == State::Exited) return;
        }
        const double now = Now();
        for (auto it = m_Pending.begin(); it != m_Pending.end();)
        {
            if (now < it->second.Deadline)
            {
                ++it;
                continue;
            }
            auto pending = std::move(it->second);
            const int id = it->first;
            it = m_Pending.erase(it);
            if (id == m_InitializeId)
            {
                Fail("the server did not answer initialize");
                return;
            }
            if (m_State == State::Ready) Send({ { "jsonrpc", "2.0" }, { "method", "$/cancelRequest" }, { "params", { { "id", id } } } });
            if (pending.Done) pending.Done(nullptr, "timeout");
            if (m_State != State::Ready && m_State != State::Starting) return;
        }
        if (m_Eof && m_Pid > 0)
        {
            int status = 0;
            std::string reason = "the server closed its output";
            if (waitpid(m_Pid, &status, WNOHANG) == m_Pid)
            {
                reason = WIFSIGNALED(status) ? "crashed (signal " + std::to_string(WTERMSIG(status)) + ")" : "exited (" + std::to_string(WEXITSTATUS(status)) + ")";
                m_Pid = -1;
            }
            else
            {
                kill(m_Pid, SIGKILL);
                waitpid(m_Pid, &status, 0);
                m_Pid = -1;
            }
            Fail(reason);
        }
    }

    void LspClient::Stop(double wait)
    {
        if (m_Socket < 0 && m_Pid < 0 && !m_Reader.joinable()) return;
        if (m_State == State::Ready && !m_Eof)
        {
            // shutdown, then exit: the server stops on its own; killed only when it does not within `wait`.
            Send({ { "jsonrpc", "2.0" }, { "id", m_NextId++ }, { "method", "shutdown" }, { "params", nullptr } });
            Send({ { "jsonrpc", "2.0" }, { "method", "exit" }, { "params", nullptr } });
        }
        const double until = Now() + wait;
        int status = 0;
        bool reaped = m_Pid <= 0;
        while (!reaped && Now() < until)
        {
            if (waitpid(m_Pid, &status, WNOHANG) == m_Pid) reaped = true;
            else std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (!reaped)
        {
            kill(m_Pid, SIGKILL);
            waitpid(m_Pid, &status, 0);
        }
        m_Pid = -1;
        m_Closed = true;
        if (m_Socket >= 0) shutdown(m_Socket, SHUT_RDWR);
        m_Wake.notify_all();
        if (m_Reader.joinable()) m_Reader.join();
        if (m_Writer.joinable()) m_Writer.join();
        if (m_Socket >= 0) close(m_Socket);
        m_Socket = -1;
        {
            std::lock_guard lock(m_Mutex);
            m_Outbox.clear();
            m_Inbox.clear();
        }
        m_Queued.clear();
        auto pending = std::move(m_Pending);
        m_Pending.clear();
        m_State = State::Stopped;
        for (auto& [id, p] : pending)
            if (p.Done) p.Done(nullptr, "server stopped");
    }
}
