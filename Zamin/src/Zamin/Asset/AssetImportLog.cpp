#include "Zamin/Asset/AssetImportLog.h"
#include "Zamin/Core/Log.h"
#include "Zamin/Project/Project.h"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <unordered_map>

namespace Zamin {

    namespace fs = std::filesystem;

    namespace {

        // The file as it was when it failed: loading it again is pointless until this changes.
        struct FileStamp
        {
            bool Exists = false;
            uintmax_t Size = 0;
            int64_t Time = 0;
            bool operator==(const FileStamp&) const = default;
        };

        struct Entry
        {
            AssetImportError Error;
            FileStamp Stamp;
            bool Stale = false; // the file changed since: report the next failure, try it again
        };

        std::mutex s_Mutex;
        std::unordered_map<std::string, Entry> s_Errors;
        uint64_t s_Revision = 0;
        uint64_t s_Sequence = 0;
        uint64_t s_Logged = 0;

        FileStamp ReadStamp(const fs::path& file)
        {
            FileStamp stamp;
            std::error_code ec;
            if (!fs::is_regular_file(file, ec))
                return stamp;
            stamp.Exists = true;
            uintmax_t size = fs::file_size(file, ec);
            if (!ec)
                stamp.Size = size;
            fs::file_time_type time = fs::last_write_time(file, ec);
            if (!ec)
                stamp.Time = (int64_t)time.time_since_epoch().count();
            return stamp;
        }

        std::string DisplayName(const fs::path& file)
        {
            if (Project::GetActive())
            {
                std::string rel = Project::GetAssetRelativePath(file);
                if (!rel.empty())
                    return rel;
            }
            return file.generic_string();
        }

    }

    std::string AssetImportLog::MakeKey(const fs::path& file)
    {
        std::error_code ec;
        fs::path abs = fs::absolute(file, ec);
        if (ec)
            abs = file;
        fs::path canonical = fs::weakly_canonical(abs, ec);
        return (ec ? abs.lexically_normal() : canonical).string();
    }

    std::string AssetImportLog::CheckReadable(const fs::path& file)
    {
        std::error_code ec;
        if (file.empty())
            return "fayl yo'li bo'sh";
        fs::file_status status = fs::status(file, ec);
        if (ec || status.type() == fs::file_type::not_found)
            return "fayl topilmadi";
        if (status.type() == fs::file_type::directory)
            return "bu papka, fayl emas";
        if (status.type() != fs::file_type::regular)
            return "oddiy fayl emas";
        if (fs::file_size(file, ec) == 0 && !ec)
            return "fayl bo'sh";
        return {};
    }

    std::string AssetImportLog::DescribeImageError(const char* reason)
    {
        std::string raw = reason && *reason ? reason : "unknown";
        const char* words = "rasm buzilgan yoki formati qo'llab-quvvatlanmaydi";
        if (raw == "can't fopen")
            words = "fayl ochilmadi";
        else if (raw == "unknown image type")
            words = "noma'lum rasm formati (PNG, JPG, TGA, BMP, PSD, GIF, HDR yoki PIC bo'lishi kerak)";
        else if (raw == "outofmem")
            words = "xotira yetmadi";
        else if (raw == "too large" || raw == "Image too large")
            words = "rasm juda katta";
        return std::string(words) + " (stb: " + raw + ")";
    }

    bool AssetImportLog::Report(const fs::path& file, std::string_view reason, bool reload)
    {
        std::string key = MakeKey(file);
        FileStamp stamp = ReadStamp(key);
        std::string name = DisplayName(key);
        AssetType type = AssetTypes::FromPath(key);
        std::string text(reason.empty() ? std::string_view("noma'lum xato") : reason);

        bool log = false;
        {
            std::lock_guard lock(s_Mutex);
            auto [it, inserted] = s_Errors.try_emplace(key);
            Entry& entry = it->second;
            if (inserted || entry.Stale || entry.Error.Reason != text || entry.Error.Reload != reload)
            {
                log = true;
                entry.Error.File = key;
                entry.Error.Name = name;
                entry.Error.Type = type;
                entry.Error.Reason = text;
                entry.Error.Reload = reload;
                entry.Error.Count = 1;
                entry.Error.Sequence = ++s_Sequence;
                entry.Stale = false;
                s_Revision++;
                s_Logged++;
            }
            else
                entry.Error.Count++;
            entry.Stamp = stamp;
        }
        if (log)
        {
            const char* what = type == AssetType::Other ? "Asset" : AssetTypes::GetName(type);
            if (reload)
                ZM_CORE_ERROR("{} qayta yuklanmadi: '{}' - {} (avvalgi holati qoldi)", what, name, text);
            else
                ZM_CORE_ERROR("{} yuklanmadi: '{}' - {}", what, name, text);
        }
        return log;
    }

    void AssetImportLog::Clear(const fs::path& file)
    {
        std::string key = MakeKey(file);
        std::lock_guard lock(s_Mutex);
        if (s_Errors.erase(key) > 0)
            s_Revision++;
    }

    void AssetImportLog::OnFileChanged(const fs::path& file)
    {
        std::string key = MakeKey(file);
        std::string prefix = key + "/";
        std::lock_guard lock(s_Mutex);
        for (auto& [k, entry] : s_Errors)
            if (k == key || k.starts_with(prefix))
                entry.Stale = true;
    }

    void AssetImportLog::ClearAll()
    {
        std::lock_guard lock(s_Mutex);
        if (!s_Errors.empty())
            s_Revision++;
        s_Errors.clear();
    }

    bool AssetImportLog::ShouldRetry(const fs::path& file)
    {
        std::string key = MakeKey(file);
        FileStamp stamp;
        {
            std::lock_guard lock(s_Mutex);
            auto it = s_Errors.find(key);
            if (it == s_Errors.end() || it->second.Stale)
                return true;
            stamp = it->second.Stamp;
        }
        if (!(ReadStamp(key) == stamp))
            return true; // written, created or deleted since it failed
        std::lock_guard lock(s_Mutex);
        if (auto it = s_Errors.find(key); it != s_Errors.end())
            it->second.Error.Count++;
        return false;
    }

    std::optional<AssetImportError> AssetImportLog::Find(const fs::path& file)
    {
        std::string key = MakeKey(file);
        std::lock_guard lock(s_Mutex);
        auto it = s_Errors.find(key);
        if (it == s_Errors.end())
            return std::nullopt;
        return it->second.Error;
    }

    std::vector<AssetImportError> AssetImportLog::GetErrors()
    {
        std::vector<AssetImportError> errors;
        {
            std::lock_guard lock(s_Mutex);
            errors.reserve(s_Errors.size());
            for (const auto& [key, entry] : s_Errors)
                errors.push_back(entry.Error);
        }
        std::sort(errors.begin(), errors.end(), [](const AssetImportError& a, const AssetImportError& b) { return a.Name < b.Name; });
        return errors;
    }

    size_t AssetImportLog::GetCount()
    {
        std::lock_guard lock(s_Mutex);
        return s_Errors.size();
    }

    uint64_t AssetImportLog::GetRevision()
    {
        std::lock_guard lock(s_Mutex);
        return s_Revision;
    }

    uint64_t AssetImportLog::GetLoggedCount()
    {
        std::lock_guard lock(s_Mutex);
        return s_Logged;
    }

}
