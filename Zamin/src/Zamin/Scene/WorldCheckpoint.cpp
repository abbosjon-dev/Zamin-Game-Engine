#include "Zamin/Scene/WorldCheckpoint.h"
#include "Zamin/Scene/SceneMigration.h"
#include "Zamin/Utils/FileSystem.h"
#include <nlohmann/json.hpp>
#include <set>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
namespace Zamin {
    using json = nlohmann::json;
    namespace {
        json ParseBounded(std::string_view text, size_t nodes)
        {
            size_t count = 0;
            std::vector<std::set<std::string>> keys;
            return json::parse(text, [&](int depth, json::parse_event_t event, json& value) {
                if (depth > 64 || ++count > nodes) throw std::runtime_error("Checkpoint JSON complexity limit exceeded");
                if (event == json::parse_event_t::object_start) keys.emplace_back();
                if (event == json::parse_event_t::key && !keys.back().insert(value.get<std::string>()).second)
                    throw std::runtime_error("Duplicate checkpoint JSON key");
                if (event == json::parse_event_t::object_end) keys.pop_back();
                return true;
            });
        }
        std::string ReadBounded(const std::filesystem::path& file)
        {
            struct Handle
            {
                int FD;
                ~Handle()
                {
                    if (FD >= 0) close(FD);
                }
            } in{ open(file.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC) };
            struct stat before
            {
            }, after{};
            if (in.FD < 0 || fstat(in.FD, &before) || !S_ISREG(before.st_mode) || before.st_size < 0 || uint64_t(before.st_size) > WorldCheckpoint::MaxFileBytes)
                throw std::runtime_error("Checkpoint must be a bounded regular file");
            std::string text(size_t(before.st_size), '\0');
            size_t pos = 0;
            while (pos < text.size())
            {
                auto n = read(in.FD, text.data() + pos, std::min(size_t(65536), text.size() - pos));
                if (n < 0 && errno == EINTR) continue;
                if (n <= 0) throw std::runtime_error("Checkpoint read failed");
                pos += size_t(n);
            }
            char extra;
            if (read(in.FD, &extra, 1) != 0 || fstat(in.FD, &after) || before.st_size != after.st_size || before.st_mtim.tv_sec != after.st_mtim.tv_sec || before.st_mtim.tv_nsec != after.st_mtim.tv_nsec || before.st_ctim.tv_sec != after.st_ctim.tv_sec || before.st_ctim.tv_nsec != after.st_ctim.tv_nsec)
                throw std::runtime_error("Checkpoint changed while reading");
            return text;
        }
    }
    WorldCheckpoint::WorldCheckpoint(const WorldManifest& manifest)
    {
        std::string error;
        if (!manifest.Serialize(m_Identity, error)) throw std::invalid_argument(error);
        // Compact canonical identity bounds envelope size and ignores formatting differences.
        m_Identity = json::parse(m_Identity).dump();
        for (const auto& cell : manifest.Cells) m_Allowed.emplace(uint64_t(cell.ID), true);
    }
    bool WorldCheckpoint::ValidateDocument(std::string_view text, std::string& error)
    {
        try
        {
            if (text.size() > MaxCellBytes) throw std::runtime_error("Checkpoint cell byte limit exceeded");
            auto doc = ParseBounded(text, 262144);
            if (!doc.is_object() || !doc.contains("Scene") || !doc["Scene"].is_string() || !doc.contains("Version") || !doc["Version"].is_number_integer() || doc["Version"] != SceneMigration::CurrentVersion || !doc.contains("Entities") || !doc["Entities"].is_array() || doc["Entities"].size() > 32768)
                throw std::runtime_error("Invalid checkpoint scene document");
            std::map<uint64_t, uint64_t> parents;
            for (const auto& entity : doc["Entities"])
            {
                if (!entity.is_object() || !entity.contains("Entity") || !entity["Entity"].is_number_unsigned()) throw std::runtime_error("Invalid checkpoint entity");
                auto id = entity["Entity"].get<uint64_t>();
                if (!id || parents.contains(id)) throw std::runtime_error("Duplicate checkpoint entity ID");
                uint64_t parent = 0;
                if (entity.contains("Parent"))
                {
                    if (!entity["Parent"].is_number_unsigned()) throw std::runtime_error("Invalid checkpoint parent");
                    parent = entity["Parent"].get<uint64_t>();
                }
                parents[id] = parent;
                if (entity.contains("SubScene")) throw std::runtime_error("Checkpoint cells cannot contain SubScene");
            }
            std::map<uint64_t, uint8_t> done;
            for (const auto& [id, parent] : parents)
            {
                (void)parent;
                std::vector<uint64_t> path;
                auto cursor = id;
                while (cursor && done[cursor] != 2)
                {
                    if (!parents.contains(cursor) || done[cursor] == 1) throw std::runtime_error("Invalid checkpoint hierarchy");
                    done[cursor] = 1;
                    path.push_back(cursor);
                    cursor = parents.at(cursor);
                }
                for (auto visited : path) done[visited] = 2;
            }
            error.clear();
            return true;
        }
        catch (const std::exception& e)
        {
            error = e.what();
            return false;
        }
    }
    bool WorldCheckpoint::Put(UUID cell, std::string document, std::string& error)
    {
        const auto key = uint64_t(cell);
        if (!m_Allowed.contains(key))
        {
            error = "Unknown checkpoint cell";
            return false;
        }
        const auto old = Get(cell);
        const size_t previous = old ? old->size() : 0;
        if (document.size() > MaxTotalBytes - (m_Bytes - previous))
        {
            error = "Checkpoint total byte limit exceeded";
            return false;
        }
        if (!ValidateDocument(document, error)) return false;
        auto next = std::make_shared<const std::string>(std::move(document));
        m_Cells[key] = next;
        m_Bytes = m_Bytes - previous + next->size();
        return true;
    }
    std::shared_ptr<const std::string> WorldCheckpoint::Get(UUID cell) const
    {
        auto it = m_Cells.find(uint64_t(cell));
        return it == m_Cells.end() ? nullptr : it->second;
    }
    bool WorldCheckpoint::Save(const std::filesystem::path& file, std::string& error) const
    {
        try
        {
            json cells = json::array();
            for (const auto& [id, text] : m_Cells) cells.push_back({ { "Cell", UUID(id).ToString() }, { "Scene", *text } });
            const auto text = json({ { "Format", "ZaminWorldCheckpoint" }, { "Version", 1 }, { "World", m_Identity }, { "Cells", cells } }).dump();
            if (text.size() > MaxFileBytes || !FileSystem::WriteTextFileAtomic(file, text)) throw std::runtime_error("Cannot atomically save checkpoint");
            error.clear();
            return true;
        }
        catch (const std::exception& e)
        {
            error = e.what();
            return false;
        }
    }
    bool WorldCheckpoint::Load(const std::filesystem::path& file, std::string& error)
    {
        try
        {
            auto doc = ParseBounded(ReadBounded(file), 262144);
            if (!doc.is_object() || doc.value("Format", std::string{}) != "ZaminWorldCheckpoint" || doc.at("Version") != 1 || doc.at("World").get<std::string>() != m_Identity || !doc.at("Cells").is_array() || doc["Cells"].size() > m_Allowed.size()) throw std::runtime_error("Checkpoint world identity or format mismatch");
            auto next = *this;
            next.m_Cells.clear();
            next.m_Bytes = 0;
            for (const auto& cell : doc["Cells"])
            {
                const auto id = UUID::FromString(cell.at("Cell").get<std::string>());
                if (!id || !id->IsValid() || next.Get(*id) || !next.Put(*id, cell.at("Scene").get<std::string>(), error)) throw std::runtime_error(error.empty() ? "Duplicate checkpoint cell" : error);
            }
            *this = std::move(next);
            error.clear();
            return true;
        }
        catch (const std::exception& e)
        {
            error = e.what();
            return false;
        }
    }
}
