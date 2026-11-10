#include "CodeDocument.h"
#include "Zamin/Utils/FileSystem.h"
#include "Zamin/Utils/StringUtils.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <algorithm>
#include <stdexcept>

namespace Zamin {
    namespace {
        std::string Read(const std::filesystem::path& p)
        {
            std::error_code ec;
            auto n = std::filesystem::file_size(p, ec);
            if (ec || n > CodeDocument::MaxBytes) throw std::runtime_error("File missing, unreadable or exceeds 1 MB");
            std::ifstream f(p, std::ios::binary);
            if (!f) throw std::runtime_error("Cannot read source file");
            std::string s((size_t)n, '\0');
            if (n && !f.read(s.data(), (std::streamsize)n)) throw std::runtime_error("Source file changed while reading");
            return s;
        }
        bool Valid(std::string_view s) { return s.size() <= CodeDocument::MaxBytes && s.find('\0') == std::string_view::npos && StringUtils::Utf8IsValid(s); }
    }
    std::string CodeDocument::Normalize(std::string_view s)
    {
        if (s.starts_with("\xef\xbb\xbf")) s.remove_prefix(3);
        std::string out;
        out.reserve(s.size());
        for (size_t i = 0; i < s.size(); ++i)
        {
            if (s[i] == '\r')
            {
                out += '\n';
                if (i + 1 < s.size() && s[i + 1] == '\n') ++i;
            }
            else out += s[i];
        }
        return out;
    }
    bool CodeDocument::Open(const std::filesystem::path& file, const std::filesystem::path& recoveryDirectory, const std::filesystem::path& writeRoot)
    {
        try
        {
            auto path = std::filesystem::canonical(file);
            auto raw = Read(path);
            if (!Valid(raw)) throw std::runtime_error("Only UTF-8 text files without NUL bytes are supported");
            m_Path = path;
            m_Disk = raw;
            m_Text = m_Saved = Normalize(raw);
            m_BOM = raw.starts_with("\xef\xbb\xbf");
            m_CRLF = raw.find("\r\n") != std::string::npos;
            m_WriteRoot = writeRoot;
            m_Error.clear();
            m_Recovery.reset();
            m_RecoveryBase.clear();
            m_RecoveryPath.clear();
            if (!recoveryDirectory.empty())
            {
                uint64_t hash = 14695981039346656037ull;
                for (unsigned char c : path.generic_string())
                {
                    hash ^= c;
                    hash *= 1099511628211ull;
                }
                m_RecoveryPath = recoveryDirectory / (std::to_string(hash) + ".json");
                std::error_code ec;
                if (std::filesystem::is_regular_file(m_RecoveryPath, ec) && std::filesystem::file_size(m_RecoveryPath, ec) <= 4 * MaxBytes)
                {
                    try
                    {
                        std::ifstream f(m_RecoveryPath);
                        auto j = nlohmann::json::parse(f, [](int depth, nlohmann::json::parse_event_t, nlohmann::json&) {if(depth>8)throw std::runtime_error("Invalid recovery nesting");return true; });
                        auto edited = j.at("Text").get<std::string>();
                        auto base = j.at("Base").get<std::string>();
                        if (j.value("Version", 0) == 1 && j.value("Path", "") == path.generic_string() && Valid(edited) && Valid(base) && edited != m_Text)
                        {
                            m_Recovery = edited;
                            m_RecoveryBase = base;
                        }
                    }
                    catch (...)
                    {/* A damaged recovery never prevents opening the source. */
                    }
                }
            }
            return true;
        }
        catch (const std::exception& e)
        {
            m_Error = e.what();
            return false;
        }
    }
    bool CodeDocument::SetText(std::string text)
    {
        if (!Valid(text))
        {
            m_Error = "Text must be UTF-8, without NUL, and at most 1 MB";
            return false;
        }
        m_Text = Normalize(text);
        m_Error.clear();
        return true;
    }
    bool CodeDocument::HasExternalChange() const
    {
        try
        {
            return Read(m_Path) != m_Disk;
        }
        catch (...)
        {
            return true;
        }
    }
    bool CodeDocument::Save(bool overwrite)
    {
        m_Error.clear();
        if (m_Path.empty())
        {
            m_Error = "No source file open";
            return false;
        }
        if (!m_WriteRoot.empty() && !FileSystem::IsSubPathOnDisk(m_Path, m_WriteRoot))
        {
            m_Error = "Source file is outside the project";
            return false;
        }
        if (!overwrite && HasExternalChange())
        {
            m_Error = "File changed on disk. Reload or explicitly overwrite after reviewing it.";
            return false;
        }
        std::string bytes = m_BOM ? "\xef\xbb\xbf" : "";
        for (char c : m_Text)
        {
            if (c == '\n' && m_CRLF) bytes += '\r';
            bytes += c;
        }
        if (bytes.size() > MaxBytes)
        {
            m_Error = "Encoded file exceeds 1 MB";
            return false;
        }
        if (!FileSystem::WriteTextFileAtomic(m_Path, bytes))
        {
            m_Error = "Save failed; edited text was kept";
            return false;
        }
        m_Disk = bytes;
        m_Saved = m_Text;
        DiscardRecovery();
        return true;
    }
    bool CodeDocument::Reload() { return Open(m_Path, m_RecoveryPath.empty() ? std::filesystem::path{} : m_RecoveryPath.parent_path(), m_WriteRoot); }
    bool CodeDocument::WriteRecovery()
    {
        if (m_RecoveryPath.empty()) return true;
        if (!Dirty())
        {
            DiscardRecovery();
            return true;
        }
        auto json = nlohmann::json{ { "Version", 1 }, { "Path", m_Path.generic_string() }, { "Base", m_Disk }, { "Text", m_Text } };
        return FileSystem::WriteTextFileAtomic(m_RecoveryPath, json.dump());
    }
    bool CodeDocument::Recover()
    {
        if (!m_Recovery) return false;
        m_Text = *m_Recovery;
        m_Disk = m_RecoveryBase;
        m_Saved = Normalize(m_Disk);
        m_BOM = m_Disk.starts_with("\xef\xbb\xbf");
        m_CRLF = m_Disk.find("\r\n") != std::string::npos;
        m_Recovery.reset();
        return true;
    }
    void CodeDocument::DiscardRecovery()
    {
        std::error_code ec;
        if (!m_RecoveryPath.empty()) std::filesystem::remove(m_RecoveryPath, ec);
        m_Recovery.reset();
        m_RecoveryBase.clear();
    }
    std::vector<size_t> CodeDocument::Find(std::string_view text, std::string_view needle, bool sensitive)
    {
        std::vector<size_t> result;
        if (needle.empty()) return result;
        auto fold = [](unsigned char c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; };
        for (size_t pos = 0; pos + needle.size() <= text.size();)
        {
            bool same = true;
            for (size_t i = 0; i < needle.size(); ++i)
                if (sensitive ? text[pos + i] != needle[i] : fold(text[pos + i]) != fold(needle[i]))
                {
                    same = false;
                    break;
                }
            if (same)
            {
                result.push_back(pos);
                pos += needle.size();
            }
            else ++pos;
        }
        return result;
    }
    std::string CodeDocument::ReplaceAll(std::string_view text, std::string_view needle, std::string_view replacement, bool sensitive)
    {
        auto hits = Find(text, needle, sensitive);
        std::string out;
        size_t at = 0;
        for (size_t pos : hits)
        {
            if (out.size() + pos - at + replacement.size() > MaxBytes) throw std::runtime_error("Replacement would exceed 1 MB");
            out.append(text.substr(at, pos - at));
            out.append(replacement);
            at = pos + needle.size();
        }
        if (out.size() + text.size() - at > MaxBytes) throw std::runtime_error("Replacement would exceed 1 MB");
        out.append(text.substr(at));
        return out;
    }
}
