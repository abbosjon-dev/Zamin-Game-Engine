#include "Zamin/Editor/EditorLayouts.h"
#include "Zamin/Editor/EditorAutosave.h"
#include "Zamin/Editor/RecentFiles.h"
#include "Zamin/Utils/FileSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <format>

namespace Zamin {

    using json = nlohmann::json;
    namespace fs = std::filesystem;

    namespace {
        struct BuiltInInfo
        {
            const char* Key;
            const char* Name;
        };
        const BuiltInInfo s_BuiltIns[] = {
            { "Default", "Standart" },
            { "2D", "2D" },
            { "Animation", "Animatsiya" },
            { "FourViews", "4 ko'rinish" },
        };
        static_assert(std::size(s_BuiltIns) == (size_t)BuiltInLayout::Count);

        std::string Lower(std::string_view text)
        {
            std::string out(text);
            for (char& c : out)
                c = (char)std::tolower((unsigned char)c);
            return out;
        }

        bool EqualsIgnoreCase(std::string_view a, std::string_view b)
        {
            return a.size() == b.size() && Lower(a) == Lower(b);
        }
    }

    // ------------------------------------------------------------------ a layout
    bool EditorLayout::IsPanelOpen(const std::string& key, bool fallback) const
    {
        auto it = Panels.find(key);
        return it == Panels.end() ? fallback : it->second;
    }

    std::string EditorLayout::ToJson() const
    {
        json j;
        j["ZaminEditorLayout"] = 1;
        j["Name"] = Name;
        j["SavedAt"] = SavedAt;
        json panels = json::object();
        for (const auto& [key, open] : Panels)
            panels[key] = open;
        j["Panels"] = std::move(panels);
        j["ImGui"] = Ini;
        return j.dump(2);
    }

    bool EditorLayout::FromJson(const std::string& text)
    {
        json j = json::parse(text, nullptr, false);
        if (j.is_discarded() || !j.is_object() || !j.contains("ZaminEditorLayout"))
            return false;
        auto ini = j.find("ImGui");
        if (ini == j.end() || !ini->is_string())
            return false;
        Ini = ini->get<std::string>();
        if (auto name = j.find("Name"); name != j.end() && name->is_string() && !name->get<std::string>().empty())
            Name = name->get<std::string>();
        if (auto saved = j.find("SavedAt"); saved != j.end() && saved->is_number_integer())
            SavedAt = saved->get<int64_t>();
        Panels.clear();
        if (auto panels = j.find("Panels"); panels != j.end() && panels->is_object())
            for (const auto& [key, open] : panels->items())
                if (!key.empty() && open.is_boolean())
                    Panels[key] = open.get<bool>();
        return true;
    }

    // ------------------------------------------------------------------ the library
    void EditorLayoutLibrary::SetDirectory(fs::path directory)
    {
        m_Directory = std::move(directory);
        m_Names.clear();
    }

    size_t EditorLayoutLibrary::Refresh()
    {
        m_Names.clear();
        m_SavedAt.clear();
        std::error_code ec;
        if (m_Directory.empty() || !fs::is_directory(m_Directory, ec))
            return 0;
        for (auto it = fs::directory_iterator(m_Directory, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
        {
            const fs::path& path = it->path();
            if (path.extension() != Extension || !it->is_regular_file(ec))
                continue;
            const std::string name = path.stem().string();
            if (!IsValidName(name) || Contains(name))
                continue;
            EditorLayout layout;
            if (!layout.FromJson(FileSystem::ReadTextFile(path)))
                continue; // not a layout file
            m_Names.push_back(name);
            m_SavedAt[name] = layout.SavedAt;
        }
        std::sort(m_Names.begin(), m_Names.end(), [](const std::string& a, const std::string& b) {
            const std::string la = Lower(a), lb = Lower(b);
            return la != lb ? la < lb : a < b;
        });
        return m_Names.size();
    }

    std::string EditorLayoutLibrary::FindStored(std::string_view name) const
    {
        for (const std::string& stored : m_Names)
            if (EqualsIgnoreCase(stored, name))
                return stored;
        return {};
    }

    bool EditorLayoutLibrary::Contains(std::string_view name) const
    {
        return !FindStored(CleanName(name)).empty();
    }

    int64_t EditorLayoutLibrary::GetSavedAt(std::string_view name) const
    {
        auto it = m_SavedAt.find(FindStored(CleanName(name)));
        return it == m_SavedAt.end() ? 0 : it->second;
    }

    fs::path EditorLayoutLibrary::GetPath(std::string_view name) const
    {
        return m_Directory / (CleanName(name) + Extension);
    }

    bool EditorLayoutLibrary::Save(const EditorLayout& layout, std::string* error)
    {
        const std::string name = CleanName(layout.Name);
        std::string reason;
        if (!IsValidName(name, &reason))
        {
            if (error) *error = reason;
            return false;
        }
        if (m_Directory.empty())
        {
            if (error) *error = "Joylashuvlar papkasi yo'q";
            return false;
        }
        std::error_code ec;
        fs::create_directories(m_Directory, ec);
        // The same name in another letter case: its file goes, the new spelling takes its place.
        if (const std::string stored = FindStored(name); !stored.empty() && stored != name)
            fs::remove(GetPath(stored), ec);
        EditorLayout copy = layout;
        copy.Name = name;
        if (copy.SavedAt == 0)
            copy.SavedAt = GetUnixTime();
        if (!EditorAutosave::WriteFileAtomic(GetPath(name), copy.ToJson()))
        {
            if (error) *error = std::format("Fayl yozilmadi: {}", GetPath(name).string());
            return false;
        }
        Refresh();
        return true;
    }

    std::optional<EditorLayout> EditorLayoutLibrary::Load(std::string_view name) const
    {
        const std::string stored = FindStored(CleanName(name));
        const fs::path path = GetPath(stored.empty() ? CleanName(name) : stored);
        std::error_code ec;
        if (!fs::is_regular_file(path, ec))
            return std::nullopt;
        EditorLayout layout;
        layout.Name = path.stem().string();
        if (!layout.FromJson(FileSystem::ReadTextFile(path)))
            return std::nullopt;
        layout.Name = path.stem().string(); // the file decides (a renamed file keeps its new name)
        return layout;
    }

    bool EditorLayoutLibrary::Remove(std::string_view name)
    {
        const std::string stored = FindStored(CleanName(name));
        if (stored.empty())
            return false;
        std::error_code ec;
        const bool removed = fs::remove(GetPath(stored), ec);
        Refresh();
        return removed;
    }

    bool EditorLayoutLibrary::Rename(std::string_view from, std::string_view to, std::string* error)
    {
        const std::string stored = FindStored(CleanName(from));
        if (stored.empty())
        {
            if (error) *error = "Bunday joylashuv yo'q";
            return false;
        }
        const std::string name = CleanName(to);
        std::string reason;
        if (!IsValidName(name, &reason))
        {
            if (error) *error = reason;
            return false;
        }
        if (const std::string other = FindStored(name); !other.empty() && other != stored)
        {
            if (error) *error = std::format("'{}' nomli joylashuv bor", other);
            return false;
        }
        if (name == stored)
            return true;
        std::optional<EditorLayout> layout = Load(stored);
        if (!layout)
        {
            if (error) *error = "Joylashuv fayli o'qilmadi";
            return false;
        }
        layout->Name = name;
        std::error_code ec;
        if (!EditorAutosave::WriteFileAtomic(GetPath(name), layout->ToJson()))
        {
            if (error) *error = std::format("Fayl yozilmadi: {}", GetPath(name).string());
            return false;
        }
        if (GetPath(stored) != GetPath(name))
            fs::remove(GetPath(stored), ec);
        Refresh();
        return true;
    }

    std::string EditorLayoutLibrary::CleanName(std::string_view name)
    {
        std::string out;
        out.reserve(name.size());
        bool space = false;
        for (char c : name)
        {
            if (std::isspace((unsigned char)c))
            {
                space = !out.empty();
                continue;
            }
            if (space)
                out += ' ';
            space = false;
            out += c;
        }
        return out;
    }

    bool EditorLayoutLibrary::IsValidName(std::string_view name, std::string* reason)
    {
        auto fail = [&](std::string text) {
            if (reason) *reason = std::move(text);
            return false;
        };
        if (name.empty())
            return fail("Nom bo'sh");
        if (name.size() > MaxNameLength)
            return fail(std::format("Nom juda uzun (ko'pi bilan {} bayt)", MaxNameLength));
        if (name.front() == '.')
            return fail("Nom nuqta bilan boshlanmasin");
        if (name.front() == ' ' || name.back() == ' ')
            return fail("Nom bo'sh joy bilan boshlanmasin / tugamasin");
        for (char c : name)
        {
            if ((unsigned char)c < 0x20 || c == 0x7f)
                return fail("Nomda boshqaruv belgisi bor");
            if (std::string_view("/\\:*?\"<>|").find(c) != std::string_view::npos)
                return fail(std::format("Nomda '{}' belgisi bo'lmasin", c));
        }
        if (FindBuiltIn(name))
            return fail("Bu nom editorning o'z joylashuviniki");
        return true;
    }

    std::string EditorLayoutLibrary::MakeUniqueName(std::string_view base) const
    {
        std::string name = CleanName(base);
        if (name.empty())
            name = "Joylashuv";
        if (name.size() > MaxNameLength - 4)
            name.resize(MaxNameLength - 4);
        while (!name.empty() && name.back() == ' ')
            name.pop_back();
        if (IsValidName(name) && !Contains(name))
            return name;
        for (int i = 2; i < 10000; i++)
        {
            std::string candidate = std::format("{} {}", name, i);
            if (IsValidName(candidate) && !Contains(candidate))
                return candidate;
        }
        return name;
    }

    const char* EditorLayoutLibrary::GetBuiltInName(BuiltInLayout layout)
    {
        const size_t i = (size_t)layout;
        return s_BuiltIns[i < std::size(s_BuiltIns) ? i : 0].Name;
    }

    const char* EditorLayoutLibrary::GetBuiltInKey(BuiltInLayout layout)
    {
        const size_t i = (size_t)layout;
        return s_BuiltIns[i < std::size(s_BuiltIns) ? i : 0].Key;
    }

    std::optional<BuiltInLayout> EditorLayoutLibrary::FindBuiltIn(std::string_view nameOrKey)
    {
        for (size_t i = 0; i < std::size(s_BuiltIns); i++)
            if (EqualsIgnoreCase(nameOrKey, s_BuiltIns[i].Key) || EqualsIgnoreCase(nameOrKey, s_BuiltIns[i].Name))
                return (BuiltInLayout)i;
        return std::nullopt;
    }

}
