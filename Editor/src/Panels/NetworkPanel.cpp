#include "NetworkPanel.h"
#include "VisualGraphLayout.h"
#include <Zamin/Core/Log.h>
#include <Zamin/Editor/EditorLanguage.h>
#include <Zamin/ImGui/Icons.h>
#include <Zamin/Network/NetGame.h>
#include <Zamin/Project/Project.h>
#include <Zamin/Scene/Scene.h>
#include <Zamin/Utils/FileSystem.h>
#include <imgui.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <format>
#include <memory>
#include <signal.h>
#include <spawn.h>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

extern char** environ;

namespace Zamin {
    namespace {
        struct State
        {
            bool Open = std::getenv("ZAMIN_EDITOR_NETWORK") != nullptr;
            // Play with clients: the editor hosts; Players - 1 client processes join it.
            bool Enabled = false, Headless = false;
            int Players = 2, Port = 7777;
            Net::ConditionerSettings Conditioner;
            std::unique_ptr<Net::Session> Session;
            std::vector<pid_t> Processes;
            std::vector<std::filesystem::path> Logs;
            std::string Error;
        } s;

        // A client process: ZaminPlayer <project> --connect 127.0.0.1:<port> --scene <scene> (--headless, conditioner).
        pid_t Launch(int index, const std::filesystem::path& scenePath)
        {
            const auto player = FileSystem::GetExecutableDirectory() / "ZaminPlayer";
            const auto log = std::filesystem::temp_directory_path() / std::format("zamin-net-client-{}-{}.log", ::getpid(), index);
            std::vector<std::string> args = { player.string(), Project::GetProjectFilePath().string(), "--connect", std::format("127.0.0.1:{}", s.Port),
                                              "--no-splash", "--log-file", log.string() };
            if (!scenePath.empty()) args.insert(args.end(), { "--scene", Project::GetAssetRelativePath(scenePath) });
            if (s.Headless) args.push_back("--headless");
            else args.insert(args.end(), { "--windowed", "--resolution", "800x450" });
            if (s.Conditioner.Enabled)
                args.insert(args.end(), { "--net-conditioner", std::format("latency={:.0f},jitter={:.0f},loss={:.1f},dup={:.1f},reorder={:.1f},seed={}", s.Conditioner.LatencyMs,
                                                                         s.Conditioner.JitterMs, s.Conditioner.LossPercent, s.Conditioner.DuplicatePercent,
                                                                         s.Conditioner.ReorderPercent, 100 + index) });
            std::vector<char*> argv;
            for (auto& a : args) argv.push_back(a.data());
            argv.push_back(nullptr);
            pid_t pid = 0;
            if (posix_spawn(&pid, player.c_str(), nullptr, nullptr, argv.data(), environ) != 0) return 0;
            s.Logs.push_back(log);
            return pid;
        }

        void StopProcesses()
        {
            for (pid_t pid : s.Processes) ::kill(pid, SIGTERM);
            const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            for (pid_t pid : s.Processes)
            {
                int status = 0;
                while (::waitpid(pid, &status, WNOHANG) == 0)
                {
                    if (std::chrono::steady_clock::now() > until)
                    {
                        ::kill(pid, SIGKILL);
                        ::waitpid(pid, &status, 0);
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                }
            }
            s.Processes.clear();
        }

        double KB(double bytes) { return bytes / 1024.0; }
    }

    bool* NetworkPanelOpen() { return &s.Open; }

    void NetworkPlayStarted(Scene& scene, const std::filesystem::path& scenePath)
    {
        s.Error.clear();
        if (!s.Enabled || !Project::GetActive()) return;
        s.Session = std::make_unique<Net::Session>();
        Net::LaunchOptions options;
        options.Server = options.Host = true;
        options.Port = uint16_t(std::clamp(s.Port, 1, 65535));
        std::string error;
        if (!Net::Launch(*s.Session, scene, Net::SettingsFor(Project::GetActive()->GetConfig()), options, &error))
        {
            s.Error = error;
            ZM_ERROR("Network: the host could not start: {}", error);
            s.Session.reset();
            return;
        }
        if (auto* conditioner = s.Session->GetConditioner()) conditioner->SetSettings(s.Conditioner);
        for (int i = 1; i < s.Players; ++i)
            if (pid_t pid = Launch(i, scenePath)) s.Processes.push_back(pid);
            else ZM_ERROR("Network: client {} could not be started", i);
        ZM_INFO("Network: hosting on port {} with {} client process(es)", options.Port, s.Processes.size());
    }

    void NetworkPlayStopped()
    {
        if (s.Session)
        {
            s.Session->Stop();
            s.Session.reset();
        }
        StopProcesses();
    }

    void DrawNetworkPanel()
    {
        if (!s.Open) return;
        ImGui::SetNextWindowSize({ 560, 620 }, ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(Tr("Tarmoq###Network"), &s.Open))
        {
            ImGui::End();
            return;
        }
        GraphLayout::PushScope("network");
        // ---- The session
        ImGui::SeparatorText(Tr("Sessiya###Session"));
        if (!s.Session)
        {
            ImGui::TextDisabled("%s", Tr("Tarmoq sessiyasi yo'q (Play bosing)"));
            GraphLayout::RecordValue("network/clients", 0.0);
            GraphLayout::RecordValue("network/role", "none");
        }
        else
        {
            const auto stats = s.Session->GetStats();
            const auto clients = s.Session->GetClients();
            const size_t connected = size_t(std::count_if(clients.begin(), clients.end(), [](const Net::SessionClient& x) { return x.Connected; }));
            ImGui::Text("%s", TrFormat("Xost, port {} | tick {} | {} obyekt | {} mijoz", s.Session->GetLocalAddress().Port, stats.Tick, stats.NetworkedEntities, connected).c_str());
            ImGui::Text("%s", TrFormat("Tick: {:.3f} ms (o'rtacha {:.3f}, eng ko'p {:.3f})", stats.TickMs, stats.AverageTickMs, stats.MaxTickMs).c_str());
            ImGui::Text("%s", TrFormat("Snapshotlar: {} ({:.1f} KB), RPC: {} yuborildi, {} keldi, {} rad etildi", stats.SnapshotsSent, KB(double(stats.SnapshotBytes)),
                                       stats.RpcsSent, stats.RpcsReceived, stats.RpcsRejected).c_str());
            GraphLayout::RecordValue("network/role", "host");
            GraphLayout::RecordValue("network/clients", double(connected));
            GraphLayout::RecordValue("network/entities", double(stats.NetworkedEntities));
            GraphLayout::RecordValue("network/rpcsReceived", double(stats.RpcsReceived));
            if (ImGui::BeginTable("clients", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit))
            {
                for (const char* column : { "ID", "Address", "RTT", "Loss", "Down KB/s", "Up KB/s", "Entities" }) ImGui::TableSetupColumn(column);
                ImGui::TableHeadersRow();
                for (const auto& client : clients)
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("%u%s", client.Id, client.Connected ? "" : " (lost)");
                    GraphLayout::RecordKey(std::format("client/{}", client.Id));
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(client.Peer.ToString().c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("%.0f ms", client.Stats.RttMs);
                    ImGui::TableNextColumn();
                    ImGui::Text("%.1f%%", client.Stats.LossPercent);
                    // (down / up: the client's view - what the server sends it is its download)
                    ImGui::TableNextColumn();
                    ImGui::Text("%.2f", KB(client.Stats.SentBytesPerSecond));
                    ImGui::TableNextColumn();
                    ImGui::Text("%.2f", KB(client.Stats.ReceivedBytesPerSecond));
                    ImGui::TableNextColumn();
                    ImGui::Text("%zu", client.Relevant);
                    GraphLayout::RecordValue(std::format("network/client/{}/rttMs", client.Id), client.Stats.RttMs);
                }
                ImGui::EndTable();
            }
            ImGui::TextDisabled("%s", TrFormat("Mijoz jarayonlari: {}", s.Processes.size()).c_str());
        }
        // ---- Play with clients
        ImGui::SeparatorText(Tr("Mijozlar bilan o'ynash###Play with clients"));
        const bool running = s.Session != nullptr;
        ImGui::BeginDisabled(running);
        ImGui::Checkbox(Tr("Play: editor xost, qolganlari mijoz###Host on Play"), &s.Enabled);
        GraphLayout::Record(Tr("Play: editor xost, qolganlari mijoz###Host on Play"));
        ImGui::SetNextItemWidth(160);
        ImGui::SliderInt(Tr("O'yinchilar###Players"), &s.Players, 1, 8);
        GraphLayout::Record(Tr("O'yinchilar###Players"));
        ImGui::SameLine();
        if (GraphLayout::SmallButton("-###Fewer players")) s.Players = std::max(1, s.Players - 1);
        ImGui::SameLine();
        if (GraphLayout::SmallButton("+###More players")) s.Players = std::min(8, s.Players + 1);
        ImGui::SetNextItemWidth(160);
        ImGui::InputInt(Tr("Port###Port"), &s.Port);
        s.Port = std::clamp(s.Port, 1, 65535);
        ImGui::Checkbox(Tr("Mijozlar oynasiz (testlar)###Headless clients"), &s.Headless);
        GraphLayout::Record(Tr("Mijozlar oynasiz (testlar)###Headless clients"));
        ImGui::EndDisabled();
        ImGui::TextDisabled("%s", Tr("Play bosilganda editor xost bo'ladi (mijoz 0), qolgan o'yinchilar ZaminPlayer jarayonlari"));
        if (!s.Error.empty()) ImGui::TextColored({ 1.0f, 0.45f, 0.4f, 1.0f }, "%s", s.Error.c_str());

        // ---- Link conditioner
        ImGui::SeparatorText(Tr("Aloqa sharoiti###Link conditioner"));
        auto c = s.Conditioner;
        ImGui::Checkbox(Tr("Yoqilgan###Conditioner on"), &c.Enabled);
        GraphLayout::Record(Tr("Yoqilgan###Conditioner on"));
        ImGui::BeginDisabled(!c.Enabled);
        ImGui::SliderFloat(Tr("Kechikish, ms###Latency"), &c.LatencyMs, 0.0f, 500.0f, "%.0f");
        GraphLayout::Record(Tr("Kechikish, ms###Latency"));
        ImGui::SliderFloat(Tr("Tebranish, ms###Jitter"), &c.JitterMs, 0.0f, 200.0f, "%.0f");
        ImGui::SliderFloat(Tr("Yo'qotish, %###Loss"), &c.LossPercent, 0.0f, 50.0f, "%.1f");
        GraphLayout::Record(Tr("Yo'qotish, %###Loss"));
        ImGui::SliderFloat(Tr("Takrorlash, %###Duplicate"), &c.DuplicatePercent, 0.0f, 50.0f, "%.1f");
        ImGui::SliderFloat(Tr("Tartib buzilishi, %###Reorder"), &c.ReorderPercent, 0.0f, 50.0f, "%.1f");
        ImGui::EndDisabled();
        if (ImGui::Button(Tr("100 ms, 5% yo'qotish###Preset bad link")))
        {
            c.Enabled = true;
            c.LatencyMs = 50;
            c.JitterMs = 10;
            c.LossPercent = 5;
        }
        GraphLayout::Record(Tr("100 ms, 5% yo'qotish###Preset bad link"));
        if (!(c == s.Conditioner))
        {
            s.Conditioner = c;
            if (s.Session && s.Session->GetConditioner()) s.Session->GetConditioner()->SetSettings(c);
        }
        ImGui::TextDisabled("%s", Tr("Xostning chiquvchi paketlariga darhol; mijoz jarayonlariga ular ishga tushganda"));
        GraphLayout::RecordValue("network/conditioner", c.Describe());

        // ---- Log
        if (s.Session && ImGui::CollapsingHeader(Tr("Sessiya jurnali###Session log"), ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::BeginChild("netlog", { 0, 140 }, ImGuiChildFlags_Borders);
            const auto& log = s.Session->GetLog();
            for (auto it = log.size() > 60 ? log.end() - 60 : log.begin(); it != log.end(); ++it) ImGui::TextUnformatted(it->Text.c_str());
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4) ImGui::SetScrollHereY(1.0f);
            ImGui::EndChild();
        }
        GraphLayout::RecordRegion("network");
        GraphLayout::PopScope();
        ImGui::End();
    }
}
