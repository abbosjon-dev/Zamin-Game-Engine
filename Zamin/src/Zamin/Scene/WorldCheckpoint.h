#pragma once
#include "Zamin/Scene/WorldManifest.h"
#include <map>
#include <memory>
namespace Zamin {
    // Complete cell snapshots; absence means authored source, empty Entities means a deleted cell.
    // Identity is compared exactly, not a non-cryptographic hash. No live scene is mutated by IO.
    class WorldCheckpoint
    {
    public:
        static constexpr size_t MaxCellBytes = 8 * 1024 * 1024, MaxTotalBytes = 64 * 1024 * 1024;
        static constexpr size_t MaxFileBytes = 160 * 1024 * 1024;
        explicit WorldCheckpoint(const WorldManifest& manifest);
        bool Put(UUID cell, std::string document, std::string& error);
        std::shared_ptr<const std::string> Get(UUID cell) const;
        size_t GetBytes() const { return m_Bytes; }
        size_t GetCount() const { return m_Cells.size(); }
        bool Save(const std::filesystem::path& file, std::string& error) const;
        bool Load(const std::filesystem::path& file, std::string& error);
        const std::map<uint64_t, std::shared_ptr<const std::string>>& GetCells() const { return m_Cells; }
        static bool ValidateDocument(std::string_view text, std::string& error);
    private:
        std::string m_Identity;
        std::map<uint64_t, bool> m_Allowed;
        std::map<uint64_t, std::shared_ptr<const std::string>> m_Cells;
        size_t m_Bytes = 0;
    };
}
