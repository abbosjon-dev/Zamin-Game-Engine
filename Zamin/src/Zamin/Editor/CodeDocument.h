#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Zamin {
// Source-file state independent of the GUI. Text is UTF-8/LF internally; saves preserve BOM/CRLF.
    class CodeDocument
    {
    public:
        static constexpr size_t MaxBytes = 1024 * 1024;
        bool Open(const std::filesystem::path& file, const std::filesystem::path& recoveryDirectory = {}, const std::filesystem::path& writeRoot = {});
        bool SetText(std::string text);
        bool Save(bool overwriteConflict = false);
        bool Reload();
        bool HasExternalChange() const;
        bool WriteRecovery();
        bool Recover();
        void DiscardRecovery();
        bool Dirty() const { return m_Text != m_Saved; }
        bool HasRecovery() const { return m_Recovery.has_value(); }
        const std::string& Text() const { return m_Text; }
        const std::string& Error() const { return m_Error; }
        const std::filesystem::path& Path() const { return m_Path; }
        const std::filesystem::path& RecoveryPath() const { return m_RecoveryPath; }
        bool CRLF() const { return m_CRLF; }
        bool BOM() const { return m_BOM; }
        static std::vector<size_t> Find(std::string_view text, std::string_view needle, bool caseSensitive = true);
        static std::string ReplaceAll(std::string_view text, std::string_view needle, std::string_view replacement, bool caseSensitive = true);
    private:
        std::filesystem::path m_Path, m_RecoveryPath, m_WriteRoot;
        std::string m_Text, m_Saved, m_Disk, m_Error, m_RecoveryBase;
        std::optional<std::string> m_Recovery;
        bool m_CRLF = false, m_BOM = false;
        static std::string Normalize(std::string_view text);
    };
}
