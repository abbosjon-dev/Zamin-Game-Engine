#pragma once
// A language server (clangd) run as a child process, JSON-RPC over its stdin/stdout. A reader and a writer thread do
// all the I/O, so nothing the editor calls blocks on the server: requests and notifications are queued, responses,
// server notifications, timeouts and the server's exit are delivered to callbacks by Poll() on the caller's thread
// (the editor's frame). Server-to-client requests (workDoneProgress/create, configuration...) are answered with null.
#include "Zamin/Editor/LspProtocol.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <thread>

namespace Zamin::Lsp {
    class LspClient
    {
    public:
        // The result, or the error ("timeout", "server stopped", the server's error message).
        using Callback = std::function<void(const json& result, const std::string& error)>;
        enum class State { Stopped, Starting, Ready, Exited };
        struct Options
        {
            std::vector<std::string> Command;   // argv; [0] found on PATH when it has no '/'
            std::filesystem::path Root;         // rootUri / workspace folder
            std::filesystem::path StderrLog;    // the server's stderr (empty: dropped)
            json InitializationOptions;         // initializationOptions
            double InitializeTimeout = 20.0;    // seconds
        };
        LspClient() = default;
        ~LspClient();
        LspClient(const LspClient&) = delete;
        LspClient& operator=(const LspClient&) = delete;

        // Starts the process and sends initialize; GetState() is Ready once the server answered (OnReady). False when
        // the process could not be started (Error()).
        bool Start(const Options& options);
        // shutdown + exit, then waits up to `wait` seconds before killing the process. Pending callbacks get "server stopped".
        void Stop(double wait = 1.0);
        // Delivers what arrived (responses, notifications, timeouts, the exit). Call it often (every frame).
        void Poll();

        // Queued at once (before Ready they wait until initialize was answered). Returns the request id.
        int Request(const std::string& method, json params, Callback callback, double timeout = 10.0);
        void Notify(const std::string& method, json params);
        // $/cancelRequest; the callback is dropped.
        void Cancel(int id);

        State GetState() const { return m_State; }
        const json& GetServerCapabilities() const { return m_Capabilities; }
        const json& GetServerInfo() const { return m_ServerInfo; }
        Encoding GetEncoding() const { return m_Encoding; }
        const std::string& Error() const { return m_Error; }
        int GetPid() const { return m_Pid; }
        size_t GetPendingCount() const { return m_Pending.size(); }
        // The process exited (or crashed) without Stop: its exit status / signal text.
        const std::string& ExitReason() const { return m_ExitReason; }

        std::function<void()> OnReady;
        std::function<void(const std::string& method, const json& params)> OnNotification;
        std::function<void(const std::string& reason)> OnExit;

    private:
        struct Pending
        {
            Callback Done;
            double Deadline = 0;
            std::string Method;
        };
        void Send(const json& message);
        void ReaderLoop();
        void WriterLoop();
        void Fail(const std::string& reason);
        static double Now();

        int m_Socket = -1, m_Pid = -1;
        std::thread m_Reader, m_Writer;
        std::mutex m_Mutex;
        std::condition_variable m_Wake;
        std::deque<std::string> m_Outbox;
        std::deque<json> m_Inbox;
        std::atomic<bool> m_Closed{ false }, m_Eof{ false };
        std::map<int, Pending> m_Pending;
        std::vector<std::pair<std::string, json>> m_Queued; // notifications/requests before Ready
        std::vector<int> m_QueuedIds;
        int m_NextId = 1, m_InitializeId = 0;
        State m_State = State::Stopped;
        Encoding m_Encoding = Encoding::UTF16;
        json m_Capabilities = json::object(), m_ServerInfo = json::object();
        std::string m_Error, m_ExitReason;
    };
}
