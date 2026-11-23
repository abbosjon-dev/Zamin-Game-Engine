#include "Zamin/Scene/WorldAssetStager.h"
#include "Zamin/Renderer/Texture.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <format>
#include <fstream>
#include <mutex>
#include <set>
#include <thread>
#include <unordered_map>

namespace Zamin {
    namespace {
        using Clock = std::chrono::steady_clock;
        constexpr uint64_t MaxMaterialBytes = 1024 * 1024;

        std::string RequestKey(const WorldAssetStager::Request& request)
        {
            const char kind = request.Type == WorldAssetStager::Kind::Texture ? (request.PixelArt ? 'P' : 'T') : request.Type == WorldAssetStager::Kind::Model ? 'M' : 'A';
            return kind + request.File.lexically_normal().string();
        }
        // The texture paths a material file names, read without the material library (any thread).
        std::vector<std::string> ReadMaterialTextures(const std::filesystem::path& file)
        {
            std::vector<std::string> out;
            std::ifstream in(file, std::ios::binary);
            if (!in) return out;
            std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            const auto j = nlohmann::json::parse(text, nullptr, false);
            if (!j.is_object()) return out;
            for (const char* name : { "AlbedoMap", "MetalRoughMap", "NormalMap", "EmissiveMap" })
                if (auto it = j.find(name); it != j.end() && it->is_string() && !it->get_ref<const std::string&>().empty())
                    out.push_back(it->get<std::string>());
            return out;
        }
    }

    bool WorldAssetStager::Limits::Validate(std::string& error) const
    {
        error.clear();
        if (!Workers || Workers > 8 || !MaxTickets || MaxTickets > 64 || !MaxFilesPerTicket || MaxFilesPerTicket > 4096 ||
            !MaxFileBytes || !MaxTicketBytes || MaxTicketBytes > MaxStagedBytes || MaxStagedBytes > (uint64_t(1) << 40) ||
            (MaxUploadUnitBytes && MaxUploadUnitBytes < 65536))
        {
            error = "Invalid world asset staging limits";
            return false;
        }
        return true;
    }

    struct WorldAssetStager::Data
    {
        struct Ticket
        {
            uint64_t ID = 0;
            std::vector<Request> Requests;
            std::atomic<bool> Cancel{ false };
            bool Abandoned = false; // released while a worker still stages it
            Status Result;
            std::vector<Request> Usable; // staged or resident
            std::vector<Request> Held;   // counted in Refs by this ticket
            uint64_t Reserved = 0;
        };
        Limits Rules;
        mutable std::mutex Mutex;
        std::condition_variable Wake;
        std::deque<std::shared_ptr<Ticket>> Queue;
        std::unordered_map<uint64_t, std::shared_ptr<Ticket>> Tickets;
        std::unordered_map<std::string, size_t> Refs;
        uint64_t Reserved = 0, Next = 1;
        Stats Counters;
        bool Stop = false;
        std::vector<std::thread> Workers;
        std::thread::id Owner = std::this_thread::get_id();

        bool CanAdmit() const { return Reserved == 0 || Rules.MaxTicketBytes <= Rules.MaxStagedBytes - std::min(Reserved, Rules.MaxStagedBytes); }
        // Mutex held. Staged data nobody else waits for is dropped (a no-op for what the libraries took).
        void DropHeld(Ticket& ticket, std::vector<Request>& drop)
        {
            for (auto& request : ticket.Held)
                if (auto it = Refs.find(RequestKey(request)); it != Refs.end() && --it->second == 0)
                {
                    Refs.erase(it);
                    drop.push_back(std::move(request));
                }
            ticket.Held.clear();
            ticket.Usable.clear();
        }
        static void Drop(const std::vector<Request>& drop)
        {
            for (const auto& request : drop)
                if (request.Type == Kind::Texture) TextureLibrary::DropStaged(request.File, request.PixelArt);
                else if (request.Type == Kind::Model) Model::DropStaged(request.File);
        }
        bool Hold(Ticket& ticket, const Request& request)
        {
            std::lock_guard lock(Mutex);
            ++Refs[RequestKey(request)];
            ticket.Held.push_back(request);
            return true;
        }
        void Unhold(Ticket& ticket, std::vector<Request>& drop)
        {
            std::lock_guard lock(Mutex);
            auto request = std::move(ticket.Held.back());
            ticket.Held.pop_back();
            if (auto it = Refs.find(RequestKey(request)); it != Refs.end() && --it->second == 0)
            {
                Refs.erase(it);
                drop.push_back(std::move(request));
            }
        }
        // Worker: stages the ticket's files in order. False when a budget was exceeded (Result.Error).
        bool Run(Ticket& ticket)
        {
            auto& result = ticket.Result;
            auto fail = [&](const std::filesystem::path& file, std::string why) {
                result.Error = std::format("{}: {}", file.filename().string(), why);
                return false;
            };
            std::set<std::string> seen;
            for (const auto& request : ticket.Requests) seen.insert(RequestKey(request));
            // (materials append the textures they name: indexed, the vector may grow)
            for (size_t i = 0; i < ticket.Requests.size(); ++i)
            {
                if (ticket.Cancel.load(std::memory_order_relaxed)) return true;
                const Request request = ticket.Requests[i];
                std::error_code ec;
                const bool regular = std::filesystem::is_regular_file(request.File, ec) && !ec;
                const uint64_t size = regular ? uint64_t(std::filesystem::file_size(request.File, ec)) : 0;
                if (regular && !ec && size > Rules.MaxFileBytes)
                    return fail(request.File, std::format("source file is {} bytes, over the streaming file budget of {}", size, Rules.MaxFileBytes));
                std::vector<Request> drop;
                if (request.Type == Kind::Material)
                {
                    if (!regular || size > MaxMaterialBytes) continue; // the material library reports it
                    for (const auto& map : ReadMaterialTextures(request.File))
                    {
                        Request texture;
                        texture.File = std::filesystem::path(map).is_absolute() ? std::filesystem::path(map) : request.AssetRoot / map;
                        if (!seen.insert(RequestKey(texture)).second) continue;
                        if (ticket.Requests.size() >= Rules.MaxFilesPerTicket)
                            return fail(request.File, std::format("the cell names more than {} asset files", Rules.MaxFilesPerTicket));
                        ticket.Requests.push_back(std::move(texture));
                    }
                    continue;
                }
                Hold(ticket, request); // before staging: a ticket released meanwhile cannot drop what this one is about to share
                if (request.Type == Kind::Texture)
                {
                    TextureLibrary::StagedTexture info;
                    const uint64_t room = Rules.MaxTicketBytes > result.Bytes ? Rules.MaxTicketBytes - result.Bytes : 0;
                    if (!TextureLibrary::Stage(request.File, request.PixelArt, room, info))
                    {
                        Unhold(ticket, drop);
                        Drop(drop);
                        if (info.OverBudget)
                            return fail(request.File, std::format("{}x{} image needs {} decoded bytes, over the cell's remaining staging budget of {}", info.Width, info.Height, info.DecodedBytes, room));
                        ++result.Unreadable;
                        continue;
                    }
                    if (info.Cached)
                    {
                        Unhold(ticket, drop);
                        Drop(drop); // (dropping a cached file's leftover pixels is harmless)
                        ++result.Resident;
                    }
                    else
                    {
                        ++result.Staged;
                        result.Bytes += info.DecodedBytes;
                        result.GPUBytes += info.DecodedBytes + info.DecodedBytes / 3;
                    }
                    ticket.Usable.push_back(request);
                }
                else
                {
                    Model::StagedModel info;
                    if (!Model::Stage(request.File, request.Model, info))
                    {
                        // (kept held: Model::Load takes the recorded failure instead of parsing again)
                        ++result.Unreadable;
                        continue;
                    }
                    ++result.Staged;
                    result.Bytes += info.MeshBytes;
                    result.GPUBytes += info.MeshBytes;
                    ticket.Usable.push_back(request);
                    if (Rules.MaxUploadUnitBytes && info.LargestMeshBytes > Rules.MaxUploadUnitBytes)
                        return fail(request.File, std::format("a mesh of {} bytes cannot be uploaded within the per-frame upload budget of {}", info.LargestMeshBytes, Rules.MaxUploadUnitBytes));
                    if (result.Bytes > Rules.MaxTicketBytes)
                        return fail(request.File, std::format("the cell's assets need {} staged bytes, over its budget of {}", result.Bytes, Rules.MaxTicketBytes));
                }
            }
            return true;
        }
        void Work()
        {
            for (;;)
            {
                std::shared_ptr<Ticket> ticket;
                {
                    std::unique_lock lock(Mutex);
                    Wake.wait(lock, [&] { return Stop || (!Queue.empty() && CanAdmit()); });
                    if (Stop) return;
                    ticket = std::move(Queue.front());
                    Queue.pop_front();
                    ticket->Result.Stage = State::Staging;
                    ticket->Reserved = Rules.MaxTicketBytes;
                    Reserved += ticket->Reserved;
                    Counters.PeakReservedBytes = std::max(Counters.PeakReservedBytes, Reserved);
                }
                const auto start = Clock::now();
                const bool ok = Run(*ticket);
                const double ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
                std::vector<Request> drop;
                {
                    std::lock_guard lock(Mutex);
                    Counters.WorkerMilliseconds += ms;
                    Reserved -= ticket->Reserved;
                    ticket->Reserved = 0;
                    if (ticket->Abandoned || ticket->Cancel.load())
                    {
                        DropHeld(*ticket, drop);
                        ++Counters.Cancelled;
                    }
                    else if (!ok)
                    {
                        DropHeld(*ticket, drop);
                        ticket->Result.Bytes = ticket->Result.GPUBytes = 0;
                        ticket->Result.Stage = State::Failed;
                        ++Counters.BudgetFailures;
                    }
                    else
                    {
                        ticket->Result.Files = ticket->Requests.size();
                        ticket->Reserved = ticket->Result.Bytes;
                        Reserved += ticket->Reserved;
                        Counters.FilesStaged += ticket->Result.Staged;
                        Counters.FilesUnreadable += ticket->Result.Unreadable;
                        Counters.StagedBytes += ticket->Result.Bytes;
                        ++Counters.Completed;
                        ticket->Result.Stage = State::Ready;
                    }
                }
                Drop(drop);
                Wake.notify_all();
            }
        }
    };

    WorldAssetStager::WorldAssetStager(std::unique_ptr<Data> data) : m_Data(std::move(data)) {}
    std::unique_ptr<WorldAssetStager> WorldAssetStager::Create(const Limits& limits, std::string& error)
    {
        if (!limits.Validate(error)) return nullptr;
        auto data = std::make_unique<Data>();
        data->Rules = limits;
        auto* raw = data.get();
        for (size_t i = 0; i < limits.Workers; ++i) data->Workers.emplace_back([raw] { raw->Work(); });
        return std::unique_ptr<WorldAssetStager>(new WorldAssetStager(std::move(data)));
    }
    WorldAssetStager::~WorldAssetStager()
    {
        auto& d = *m_Data;
        {
            std::lock_guard lock(d.Mutex);
            d.Stop = true;
            for (auto& [id, ticket] : d.Tickets)
            {
                (void)id;
                ticket->Cancel = true;
            }
        }
        d.Wake.notify_all();
        for (auto& worker : d.Workers) worker.join();
        std::vector<Request> drop;
        for (auto& [id, ticket] : d.Tickets)
        {
            (void)id;
            d.DropHeld(*ticket, drop);
        }
        Data::Drop(drop);
    }
    uint64_t WorldAssetStager::Submit(std::vector<Request> requests, std::string& error)
    {
        auto& d = *m_Data;
        error.clear();
        if (std::this_thread::get_id() != d.Owner)
        {
            error = "World asset stager used off its owner thread";
            return 0;
        }
        std::set<std::string> seen;
        std::vector<Request> unique;
        for (auto& request : requests)
        {
            if (request.File.empty() || !request.File.is_absolute())
            {
                error = "World asset requests need absolute file paths";
                return 0;
            }
            if (!seen.insert(RequestKey(request)).second) continue;
            if (request.Type == Kind::Model && Model::IsCached(request.File)) continue;
            unique.push_back(std::move(request));
        }
        if (unique.size() > d.Rules.MaxFilesPerTicket)
        {
            error = std::format("The cell names {} asset files, over the streaming budget of {}", unique.size(), d.Rules.MaxFilesPerTicket);
            return 0;
        }
        auto ticket = std::make_shared<Data::Ticket>();
        ticket->Requests = std::move(unique);
        ticket->Result.Stage = State::Queued;
        ticket->Result.Files = ticket->Requests.size();
        {
            std::lock_guard lock(d.Mutex);
            if (d.Tickets.size() >= d.Rules.MaxTickets)
            {
                error = "Too many world asset tickets";
                return 0;
            }
            ticket->ID = d.Next++;
            d.Tickets[ticket->ID] = ticket;
            d.Queue.push_back(ticket);
            ++d.Counters.Submitted;
        }
        d.Wake.notify_one();
        return ticket->ID;
    }
    WorldAssetStager::Status WorldAssetStager::GetStatus(uint64_t id) const
    {
        std::lock_guard lock(m_Data->Mutex);
        const auto it = m_Data->Tickets.find(id);
        if (it == m_Data->Tickets.end()) return {};
        // A worker still writes the counters of a ticket it stages: only the stage is published until then.
        if (it->second->Result.Stage == State::Queued || it->second->Result.Stage == State::Staging)
        {
            Status status;
            status.Stage = it->second->Result.Stage;
            return status;
        }
        return it->second->Result;
    }
    std::vector<WorldAssetStager::Request> WorldAssetStager::GetStaged(uint64_t id) const
    {
        std::lock_guard lock(m_Data->Mutex);
        const auto it = m_Data->Tickets.find(id);
        return it != m_Data->Tickets.end() && it->second->Result.Stage == State::Ready ? it->second->Usable : std::vector<Request>{};
    }
    void WorldAssetStager::Release(uint64_t id)
    {
        auto& d = *m_Data;
        std::vector<Request> drop;
        {
            std::lock_guard lock(d.Mutex);
            const auto it = d.Tickets.find(id);
            if (it == d.Tickets.end()) return;
            auto ticket = it->second;
            d.Tickets.erase(it);
            if (ticket->Result.Stage == State::Queued)
            {
                std::erase(d.Queue, ticket);
                ++d.Counters.Cancelled;
            }
            else if (ticket->Result.Stage == State::Staging)
            {
                ticket->Abandoned = true; // its worker drops what it staged
                ticket->Cancel = true;
            }
            else
            {
                d.DropHeld(*ticket, drop);
                d.Reserved -= ticket->Reserved;
                ticket->Reserved = 0;
            }
        }
        Data::Drop(drop);
        d.Wake.notify_all();
    }
    WorldAssetStager::Stats WorldAssetStager::GetStats() const
    {
        std::lock_guard lock(m_Data->Mutex);
        auto stats = m_Data->Counters;
        stats.Tickets = m_Data->Tickets.size();
        stats.ReservedBytes = m_Data->Reserved;
        for (const auto& [id, ticket] : m_Data->Tickets)
        {
            (void)id;
            stats.Queued += ticket->Result.Stage == State::Queued;
            stats.Staging += ticket->Result.Stage == State::Staging;
            stats.Ready += ticket->Result.Stage == State::Ready;
        }
        return stats;
    }
    const WorldAssetStager::Limits& WorldAssetStager::GetLimits() const { return m_Data->Rules; }
}
