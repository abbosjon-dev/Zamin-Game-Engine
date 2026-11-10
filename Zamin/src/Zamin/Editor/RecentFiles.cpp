#include "Zamin/Editor/RecentFiles.h"
#include "Zamin/Utils/FileSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <format>

namespace Zamin {

    using json = nlohmann::json;

    std::string RecentFileList::Normalize(std::string_view path)
    {
        if (path.empty())
            return {};
        std::string text = std::filesystem::path(path).lexically_normal().generic_string();
        // "a/b/" and "a/b" are the same folder.
        while (text.size() > 1 && text.back() == '/')
            text.pop_back();
        return text;
    }

    void RecentFileList::Add(std::string_view path, std::string_view owner, int64_t time)
    {
        std::string key = Normalize(path);
        if (key.empty())
            return;
        Remove(key);
        m_Entries.insert(m_Entries.begin(), Entry{ std::move(key), Normalize(owner), time < 0 ? GetUnixTime() : time });
        if (m_Entries.size() > m_Capacity)
            m_Entries.resize(m_Capacity);
    }

    bool RecentFileList::Remove(std::string_view path)
    {
        const std::string key = Normalize(path);
        return std::erase_if(m_Entries, [&](const Entry& e) { return e.Path == key; }) > 0;
    }

    void RecentFileList::Clear() { m_Entries.clear(); }

    size_t RecentFileList::ClearOwner(std::string_view owner)
    {
        const std::string key = Normalize(owner);
        return std::erase_if(m_Entries, [&](const Entry& e) { return e.Owner == key; });
    }

    size_t RecentFileList::Prune(const std::function<bool(const std::string&)>& exists)
    {
        return std::erase_if(m_Entries, [&](const Entry& e) { return !exists(e.Path); });
    }

    size_t RecentFileList::Rename(std::string_view from, std::string_view to)
    {
        const std::string a = Normalize(from), b = Normalize(to);
        if (a.empty() || b.empty() || a == b)
            return 0;
        // The path itself, or one inside it as a folder ("a/b" moves "a/b/c", not "a/bc").
        auto moved = [&](std::string& path) {
            if (path == a)
                path = b;
            else if (path.size() > a.size() && path.compare(0, a.size(), a) == 0 && path[a.size()] == '/')
                path = b + path.substr(a.size());
            else
                return false;
            return true;
        };
        size_t count = 0;
        for (Entry& e : m_Entries)
        {
            if (moved(e.Path))
                count++;
            moved(e.Owner);
        }
        // A move onto a listed path leaves two of it: the newer one stays.
        for (size_t i = 0; i < m_Entries.size(); i++)
            for (size_t j = i + 1; j < m_Entries.size();)
            {
                if (m_Entries[j].Path == m_Entries[i].Path)
                    m_Entries.erase(m_Entries.begin() + (std::ptrdiff_t)j);
                else
                    j++;
            }
        return count;
    }

    bool RecentFileList::Contains(std::string_view path) const
    {
        const std::string key = Normalize(path);
        return std::any_of(m_Entries.begin(), m_Entries.end(), [&](const Entry& e) { return e.Path == key; });
    }

    std::vector<RecentFileList::Entry> RecentFileList::GetForOwner(std::string_view owner, size_t limit) const
    {
        const std::string key = Normalize(owner);
        std::vector<Entry> out;
        for (const Entry& e : m_Entries)
            if (e.Owner == key && (limit == 0 || out.size() < limit))
                out.push_back(e);
        return out;
    }

    std::string RecentFileList::ToJson() const
    {
        json list = json::array();
        for (const Entry& e : m_Entries)
        {
            json item = { { "Path", e.Path }, { "Time", e.Time } };
            if (!e.Owner.empty())
                item["Owner"] = e.Owner;
            list.push_back(std::move(item));
        }
        return json{ { "Recent", std::move(list) } }.dump(2);
    }

    bool RecentFileList::FromJson(const std::string& text)
    {
        json j = json::parse(text, nullptr, false);
        if (j.is_discarded())
            return false;
        const json* list = nullptr;
        if (j.is_array())
            list = &j; // the older format: ["path", ...]
        else if (j.is_object() && j.contains("Recent") && j["Recent"].is_array())
            list = &j["Recent"];
        if (!list)
            return false;
        m_Entries.clear();
        // Oldest first, so Add leaves the newest at the front.
        for (auto it = list->rbegin(); it != list->rend(); ++it)
        {
            if (it->is_string())
                Add(it->get<std::string>(), {}, 0);
            else if (it->is_object() && it->contains("Path") && (*it)["Path"].is_string())
            {
                const json& item = *it;
                const std::string owner = item.contains("Owner") && item["Owner"].is_string() ? item["Owner"].get<std::string>() : std::string();
                const int64_t time = item.contains("Time") && item["Time"].is_number_integer() ? item["Time"].get<int64_t>() : 0;
                Add(item["Path"].get<std::string>(), owner, std::max<int64_t>(time, 0));
            }
        }
        return true;
    }

    bool RecentFileList::Save(const std::filesystem::path& path) const
    {
        return FileSystem::WriteTextFileAtomic(path, ToJson());
    }

    bool RecentFileList::Load(const std::filesystem::path& path)
    {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec))
            return false;
        return FromJson(FileSystem::ReadTextFile(path));
    }

    std::string FormatTimeAgo(int64_t then, int64_t now)
    {
        if (then <= 0)
            return {};
        const int64_t seconds = std::max<int64_t>(now - then, 0);
        if (seconds < 60) return "hozirgina";
        if (seconds < 3600) return std::format("{} daqiqa oldin", seconds / 60);
        if (seconds < 86400) return std::format("{} soat oldin", seconds / 3600);
        const int64_t days = seconds / 86400;
        if (days == 1) return "kecha";
        if (days < 7) return std::format("{} kun oldin", days);
        if (days < 31) return std::format("{} hafta oldin", days / 7);
        // Older: the date.
        const std::time_t t = (std::time_t)then;
        std::tm tm{};
        localtime_r(&t, &tm);
        return std::format("{:04}-{:02}-{:02}", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    }

    int64_t GetUnixTime()
    {
        return (int64_t)std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    }

}
