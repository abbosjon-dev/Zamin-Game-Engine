#include "Zamin/Core/SaveData.h"
#include "Zamin/Core/Log.h"
#include "Zamin/Project/Project.h"
#include "Zamin/Utils/FileSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <map>
#include <variant>

namespace Zamin {

    using json = nlohmann::json;

    namespace {

        using Value = std::variant<int, float, std::string, bool>;

        struct Store
        {
            std::map<std::string, Value, std::less<>> Values;
            std::filesystem::path LoadedPath; // the file `Values` belong to
            std::filesystem::path FixedPath;  // SetFilePath() override
            bool Loaded = false;
            bool Dirty = false;
        };

        Store& S()
        {
            static Store s_Store;
            return s_Store;
        }

        constexpr const char* s_FileName = "savedata.json";
        constexpr int s_FormatVersion = 1;

        std::string SanitizeName(std::string_view name)
        {
            std::string out;
            for (char ch : name)
            {
                unsigned char c = (unsigned char)ch;
                bool ok = c >= 0x80 || (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                          c == ' ' || c == '-' || c == '_' || c == '.';
                out += ok ? ch : '_';
            }
            // No leading / trailing spaces or dots (hidden folders, "..").
            size_t begin = out.find_first_not_of(" .");
            size_t end = out.find_last_not_of(" .");
            out = begin == std::string::npos ? std::string() : out.substr(begin, end - begin + 1);
            return out.empty() ? "Untitled" : out;
        }

        std::filesystem::path ResolvePath()
        {
            const Store& s = S();
            if (!s.FixedPath.empty())
                return s.FixedPath;
            auto project = Project::GetActive();
            if (!project)
                return SaveData::GetDirectoryForProject("Zamin") / s_FileName;
            const ProjectConfig& config = project->GetConfig();
            return SaveData::GetDirectoryFor(config.Player.CompanyName, config.Player.GetProductName(config.Name)) / s_FileName;
        }

        bool WriteFile(Store& s)
        {
            json data = json::object();
            for (const auto& [key, value] : s.Values)
                std::visit([&](const auto& v) { data[key] = v; }, value);
            json root = { { "Version", s_FormatVersion }, { "Data", std::move(data) } };

            std::error_code ec;
            std::filesystem::create_directories(s.LoadedPath.parent_path(), ec);
            std::filesystem::path temp = s.LoadedPath;
            temp += ".tmp";
            if (!FileSystem::WriteTextFile(temp, root.dump(4)))
            {
                ZM_CORE_ERROR("SaveData yozilmadi: {}", s.LoadedPath.string());
                return false;
            }
            std::filesystem::rename(temp, s.LoadedPath, ec);
            if (ec)
            {
                ZM_CORE_ERROR("SaveData yozilmadi: {} ({})", s.LoadedPath.string(), ec.message());
                std::filesystem::remove(temp, ec);
                return false;
            }
            s.Dirty = false;
            return true;
        }

        bool LoadFile(Store& s, const std::filesystem::path& path)
        {
            s.Values.clear();
            s.LoadedPath = path;
            s.Loaded = true;
            s.Dirty = false;

            std::error_code ec;
            if (!std::filesystem::exists(path, ec))
                return true; // nothing saved yet
            std::string text = FileSystem::ReadTextFile(path);
            try
            {
                json root = json::parse(text);
                const json& data = root.at("Data");
                for (auto it = data.begin(); it != data.end(); ++it)
                {
                    const json& v = it.value();
                    if (v.is_boolean())
                        s.Values[it.key()] = v.get<bool>();
                    else if (v.is_number_integer())
                    {
                        // number_unsigned values above INT64_MAX are clamped, too
                        long long n = v.is_number_unsigned()
                            ? (long long)std::min<unsigned long long>(v.get<unsigned long long>(), (unsigned long long)std::numeric_limits<int>::max())
                            : v.get<long long>();
                        s.Values[it.key()] = (int)std::clamp<long long>(n, std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
                    }
                    else if (v.is_number_float())
                        s.Values[it.key()] = v.get<float>();
                    else if (v.is_string())
                        s.Values[it.key()] = v.get<std::string>();
                    else
                        ZM_CORE_WARN("SaveData: '{}' kalitining qiymati tushunarsiz, o'tkazib yuborildi", it.key());
                }
                return true;
            }
            catch (const std::exception& e)
            {
                // Keep the broken file next to the new one instead of overwriting it later.
                s.Values.clear();
                std::filesystem::path backup = path;
                backup += ".bak";
                std::filesystem::rename(path, backup, ec);
                ZM_CORE_ERROR("SaveData fayli buzilgan ({}): {} -> {}", e.what(), path.string(), backup.filename().string());
                return false;
            }
        }

        // The data of the current file (loads it on first use / after the project changed).
        Store& Data()
        {
            Store& s = S();
            std::filesystem::path path = ResolvePath();
            if (!s.Loaded || path != s.LoadedPath)
            {
                if (s.Loaded && s.Dirty)
                    WriteFile(s); // keep what the previous project stored
                LoadFile(s, path);
            }
            return s;
        }

        const Value* Find(std::string_view key)
        {
            const Store& s = Data();
            auto it = s.Values.find(key);
            return it != s.Values.end() ? &it->second : nullptr;
        }

        void Set(std::string_view key, Value value)
        {
            Store& s = Data();
            auto it = s.Values.find(key);
            if (it != s.Values.end())
            {
                if (it->second == value)
                    return;
                it->second = std::move(value);
            }
            else
                s.Values.emplace(std::string(key), std::move(value));
            s.Dirty = true;
        }

    }

    void SaveData::SetInt(std::string_view key, int value) { Set(key, Value(std::in_place_type<int>, value)); }
    void SaveData::SetFloat(std::string_view key, float value) { Set(key, Value(std::in_place_type<float>, value)); }
    void SaveData::SetString(std::string_view key, std::string_view value) { Set(key, Value(std::in_place_type<std::string>, value)); }
    void SaveData::SetBool(std::string_view key, bool value) { Set(key, Value(std::in_place_type<bool>, value)); }

    int SaveData::GetInt(std::string_view key, int defaultValue)
    {
        const Value* v = Find(key);
        if (!v) return defaultValue;
        if (const int* i = std::get_if<int>(v)) return *i;
        if (const float* f = std::get_if<float>(v))
        {
            if (!(std::abs(*f) < 2.1e9f)) return defaultValue; // NaN / out of range
            return (int)*f;
        }
        return defaultValue;
    }

    float SaveData::GetFloat(std::string_view key, float defaultValue)
    {
        const Value* v = Find(key);
        if (!v) return defaultValue;
        if (const float* f = std::get_if<float>(v)) return *f;
        if (const int* i = std::get_if<int>(v)) return (float)*i;
        return defaultValue;
    }

    std::string SaveData::GetString(std::string_view key, std::string_view defaultValue)
    {
        const Value* v = Find(key);
        if (const std::string* str = v ? std::get_if<std::string>(v) : nullptr)
            return *str;
        return std::string(defaultValue);
    }

    bool SaveData::GetBool(std::string_view key, bool defaultValue)
    {
        const Value* v = Find(key);
        if (const bool* b = v ? std::get_if<bool>(v) : nullptr)
            return *b;
        return defaultValue;
    }

    bool SaveData::HasKey(std::string_view key) { return Find(key) != nullptr; }

    SaveValueType SaveData::GetType(std::string_view key)
    {
        const Value* v = Find(key);
        if (!v) return SaveValueType::None;
        switch (v->index())
        {
            case 0: return SaveValueType::Int;
            case 1: return SaveValueType::Float;
            case 2: return SaveValueType::String;
            case 3: return SaveValueType::Bool;
        }
        return SaveValueType::None;
    }

    void SaveData::DeleteKey(std::string_view key)
    {
        Store& s = Data();
        auto it = s.Values.find(key);
        if (it == s.Values.end())
            return;
        s.Values.erase(it);
        s.Dirty = true;
    }

    void SaveData::DeleteAll()
    {
        Store& s = Data();
        if (s.Values.empty())
            return;
        s.Values.clear();
        s.Dirty = true;
    }

    std::vector<std::string> SaveData::GetKeys()
    {
        std::vector<std::string> keys;
        for (const auto& [key, value] : Data().Values)
            keys.push_back(key);
        return keys;
    }

    bool SaveData::Save()
    {
        Store& s = S();
        if (!s.Loaded || !s.Dirty)
            return true; // nothing to write (untouched data is never loaded just to be saved)
        if (!WriteFile(s))
            return false;
        ZM_CORE_INFO("SaveData saqlandi: {}", s.LoadedPath.string());
        return true;
    }

    bool SaveData::Reload()
    {
        Store& s = S();
        s.Loaded = false;
        s.Dirty = false;
        return LoadFile(s, ResolvePath());
    }

    bool SaveData::IsDirty() { return S().Loaded && S().Dirty; }

    std::filesystem::path SaveData::GetFilePath() { return ResolvePath(); }

    std::filesystem::path SaveData::GetDirectoryForProject(std::string_view projectName) { return GetDirectoryFor({}, projectName); }

    std::filesystem::path SaveData::GetDirectoryFor(std::string_view company, std::string_view product)
    {
        std::filesystem::path base;
#ifdef _WIN32
        if (const char* appData = std::getenv("APPDATA"); appData && *appData)
            base = appData;
#else
        if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg && std::filesystem::path(xdg).is_absolute())
            base = xdg;
        else if (const char* home = std::getenv("HOME"); home && *home)
            base = std::filesystem::path(home) / ".local" / "share";
#endif
        if (base.empty())
        {
            std::error_code ec;
            base = std::filesystem::temp_directory_path(ec);
        }
        // Without a company (or one of only spaces / dots) the engine's own folder: where every game
        // kept its data before company names.
        const bool hasCompany = company.find_first_not_of(" .") != std::string_view::npos;
        return base / (hasCompany ? SanitizeName(company) : std::string("zamin")) / SanitizeName(product);
    }

    void SaveData::SetFilePath(const std::filesystem::path& path)
    {
        Store& s = S();
        if (s.Loaded && s.Dirty)
            WriteFile(s);
        s.FixedPath = path;
        s.Values.clear();
        s.Loaded = false;
        s.Dirty = false;
    }

    const char* SaveData::TypeToString(SaveValueType type)
    {
        switch (type)
        {
            case SaveValueType::Int:    return "Int";
            case SaveValueType::Float:  return "Float";
            case SaveValueType::String: return "String";
            case SaveValueType::Bool:   return "Bool";
            default:                    return "-";
        }
    }

}
