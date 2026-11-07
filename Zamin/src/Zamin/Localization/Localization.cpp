#include "Zamin/Localization/Localization.h"

#include "Zamin/Core/Log.h"
#include "Zamin/Core/SaveData.h"
#include "Zamin/Utils/FileSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <set>
#include <unordered_set>

namespace Zamin {

    using json = nlohmann::json;
    using ordered_json = nlohmann::ordered_json;

    namespace {

        const char* const s_CategoryNames[] = { "zero", "one", "two", "few", "many", "other" };

        bool IsCategoryName(std::string_view name)
        {
            return std::find(std::begin(s_CategoryNames), std::end(s_CategoryNames), name) != std::end(s_CategoryNames);
        }

        // An object of plural forms only, "other" among them.
        bool IsPluralObject(const json& j)
        {
            if (!j.is_object() || j.empty() || !j.contains("other"))
                return false;
            for (auto it = j.begin(); it != j.end(); ++it)
                if (!IsCategoryName(it.key()) || !it->is_string())
                    return false;
            return true;
        }

        void ReadEntries(const json& j, const std::string& prefix, StringTable& out, std::vector<std::string>* warnings)
        {
            for (auto it = j.begin(); it != j.end(); ++it)
            {
                const std::string& name = it.key();
                if (prefix.empty() && name.starts_with('$'))
                    continue; // about the file ($name ...)
                const std::string key = prefix.empty() ? name : prefix + "." + name;
                if (it->is_string())
                    out.Strings[key] = LocalizedString{ it->get<std::string>(), {} };
                else if (IsPluralObject(*it))
                {
                    LocalizedString entry;
                    for (auto f = it->begin(); f != it->end(); ++f)
                        entry.Plural[f.key()] = f->get<std::string>();
                    out.Strings[key] = std::move(entry);
                }
                else if (it->is_object())
                    ReadEntries(*it, key, out, warnings);
                else if (warnings)
                    warnings->push_back(std::format("'{}': matn emas ({}) - o'tkazib yuborildi", key, it->type_name()));
            }
        }

        ordered_json EntryToJson(const LocalizedString& entry)
        {
            if (!entry.IsPlural())
                return entry.Text;
            ordered_json forms = ordered_json::object();
            // CLDR order, not alphabetical.
            for (const char* category : s_CategoryNames)
                if (auto it = entry.Plural.find(std::string_view(category)); it != entry.Plural.end())
                    forms[category] = it->second;
            for (const auto& [category, text] : entry.Plural)
                if (!IsCategoryName(category))
                    forms[category] = text;
            return forms;
        }

        std::vector<std::string> SplitKey(std::string_view key)
        {
            std::vector<std::string> parts;
            size_t start = 0;
            while (true)
            {
                const size_t dot = key.find('.', start);
                parts.emplace_back(key.substr(start, dot == std::string_view::npos ? std::string_view::npos : dot - start));
                if (dot == std::string_view::npos)
                    break;
                start = dot + 1;
            }
            return parts;
        }

        bool IsPluralJson(const ordered_json& j)
        {
            if (!j.is_object() || !j.contains("other"))
                return false;
            for (auto it = j.begin(); it != j.end(); ++it)
                if (!IsCategoryName(it.key()) || !it->is_string())
                    return false;
            return true;
        }

        // ------------------------------------------------------------------ runtime state
        struct State
        {
            LocalizationSettings Settings;
            std::filesystem::path Directory;
            std::vector<StringTable> Tables;
            std::vector<std::string> Offered;
            std::string Language = "uz";
            uint64_t Version = 1;
            std::vector<std::pair<uint64_t, Localization::Listener>> Listeners;
            uint64_t NextListener = 1;
            std::vector<std::string> Missing;
            std::unordered_set<std::string> Warned; // "<lang>|<key>"
            std::vector<std::string> LoadErrors;
        };

        State& S()
        {
            static State s_State;
            return s_State;
        }

        const StringTable* TableOf(std::string_view language)
        {
            for (const StringTable& t : S().Tables)
                if (t.Language == language)
                    return &t;
            return nullptr;
        }

        void ComputeOffered()
        {
            State& s = S();
            s.Offered.clear();
            if (!s.Settings.Languages.empty())
                s.Offered = s.Settings.Languages;
            else
            {
                for (const StringTable& t : s.Tables)
                    s.Offered.push_back(t.Language);
                std::sort(s.Offered.begin(), s.Offered.end());
                auto it = std::find(s.Offered.begin(), s.Offered.end(), s.Settings.DefaultLanguage);
                if (it != s.Offered.end())
                    std::rotate(s.Offered.begin(), it, it + 1);
            }
            // The default language is always offered (it is the fallback).
            if (std::find(s.Offered.begin(), s.Offered.end(), s.Settings.DefaultLanguage) == s.Offered.end())
                s.Offered.insert(s.Offered.begin(), s.Settings.DefaultLanguage);
        }

        void LoadTables()
        {
            State& s = S();
            s.Tables.clear();
            s.LoadErrors.clear();
            if (s.Directory.empty())
                return;
            std::vector<std::string> languages = s.Settings.Languages;
            if (languages.empty())
                languages = LocalizationTable::FindLanguageFiles(s.Directory);
            if (std::find(languages.begin(), languages.end(), s.Settings.DefaultLanguage) == languages.end())
                languages.push_back(s.Settings.DefaultLanguage);
            for (const std::string& code : languages)
            {
                const std::filesystem::path file = s.Directory / (code + ".json");
                std::error_code ec;
                if (!std::filesystem::exists(file, ec))
                {
                    if (!s.Settings.Languages.empty() || code != s.Settings.DefaultLanguage)
                        s.LoadErrors.push_back(std::format("{}: fayl yo'q", file.filename().string()));
                    continue;
                }
                StringTable table;
                std::string error;
                std::vector<std::string> warnings;
                if (!StringTable::Parse(FileSystem::ReadTextFile(file), table, &error, &warnings))
                {
                    s.LoadErrors.push_back(std::format("{}: {}", file.filename().string(), error));
                    ZM_CORE_WARN("Til fayli o'qilmadi: {} ({})", file.string(), error);
                    continue;
                }
                for (const std::string& w : warnings)
                    ZM_CORE_WARN("Til fayli {}: {}", file.filename().string(), w);
                table.Language = code;
                s.Tables.push_back(std::move(table));
            }
        }

        void Changed()
        {
            State& s = S();
            s.Version++;
            // A copy: a listener may add / remove listeners.
            auto listeners = s.Listeners;
            for (auto& [id, listener] : listeners)
                if (listener)
                    listener(s.Language);
        }

        std::string PickStartLanguage()
        {
            State& s = S();
            if (s.Settings.UseSystemLanguage)
            {
                const std::string system = Localization::DetectSystemLanguage();
                if (!system.empty() && Localization::IsOffered(system))
                    return system;
            }
            return s.Settings.DefaultLanguage;
        }

        bool IsCodeChar(char c)
        {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
        }
    }

    // ------------------------------------------------------------------ LocalizedString
    bool LocalizedString::IsEmpty() const
    {
        if (!Text.empty())
            return false;
        return std::all_of(Plural.begin(), Plural.end(), [](const auto& f) { return f.second.empty(); });
    }

    const std::string& LocalizedString::Get(PluralCategory category) const
    {
        if (Plural.empty())
            return Text;
        if (auto it = Plural.find(std::string_view(Localization::GetPluralCategoryName(category))); it != Plural.end() && !it->second.empty())
            return it->second;
        if (auto it = Plural.find(std::string_view("other")); it != Plural.end() && !it->second.empty())
            return it->second;
        for (const auto& [name, text] : Plural)
            if (!text.empty())
                return text;
        return Text;
    }

    // ------------------------------------------------------------------ StringTable
    const LocalizedString* StringTable::Find(std::string_view key) const
    {
        auto it = Strings.find(key);
        return it != Strings.end() ? &it->second : nullptr;
    }

    bool StringTable::Has(std::string_view key) const
    {
        const LocalizedString* entry = Find(key);
        return entry && !entry->IsEmpty();
    }

    bool StringTable::Parse(const std::string& text, StringTable& out, std::string* error, std::vector<std::string>* warnings)
    {
        json j = json::parse(text, nullptr, false);
        if (j.is_discarded() || !j.is_object())
        {
            if (error)
                *error = j.is_discarded() ? "JSON xatosi" : "JSON obyekt emas";
            return false;
        }
        StringTable table;
        table.Language = out.Language;
        if (auto it = j.find("$name"); it != j.end() && it->is_string())
            table.DisplayName = it->get<std::string>();
        ReadEntries(j, "", table, warnings);
        out = std::move(table);
        return true;
    }

    std::string StringTable::ToJson() const
    {
        ordered_json root = ordered_json::object();
        if (!DisplayName.empty())
            root["$name"] = DisplayName;
        std::vector<std::pair<std::string, const LocalizedString*>> flat;
        for (const auto& [key, entry] : Strings)
        {
            const std::vector<std::string> parts = SplitKey(key);
            ordered_json* node = &root;
            bool ok = !parts.empty();
            for (size_t i = 0; ok && i + 1 < parts.size(); i++)
            {
                ordered_json& child = (*node)[parts[i]];
                if (child.is_null())
                    child = ordered_json::object();
                // A text there already, or a plural entry: this key cannot nest.
                if (!child.is_object() || IsPluralJson(child))
                    ok = false;
                else
                    node = &child;
            }
            if (ok && (!node->contains(parts.back())))
                (*node)[parts.back()] = EntryToJson(entry);
            else
                flat.emplace_back(key, &entry);
        }
        // Objects left empty by a key that could not nest.
        for (const auto& [key, entry] : flat)
            root[key] = EntryToJson(*entry);
        std::function<void(ordered_json&)> prune = [&](ordered_json& j) {
            for (auto it = j.begin(); it != j.end();)
            {
                if (it->is_object() && !IsPluralJson(*it))
                {
                    prune(*it);
                    if (it->empty())
                    {
                        it = j.erase(it);
                        continue;
                    }
                }
                ++it;
            }
        };
        prune(root);
        return root.dump(4);
    }

    // ------------------------------------------------------------------ LocalizationSettings
    bool LocalizationSettings::IsValidCode(std::string_view code)
    {
        if (code.size() < 2 || code.size() > 8)
            return false;
        return std::all_of(code.begin(), code.end(), IsCodeChar) && code.front() != '-' && code.front() != '_';
    }

    void LocalizationSettings::Sanitize()
    {
        if (!IsValidCode(DefaultLanguage))
            DefaultLanguage = "uz";
        std::vector<std::string> languages;
        for (const std::string& code : Languages)
            if (IsValidCode(code) && std::find(languages.begin(), languages.end(), code) == languages.end())
                languages.push_back(code);
        // A list without the default language: it is added (it is the fallback).
        if (!languages.empty() && std::find(languages.begin(), languages.end(), DefaultLanguage) == languages.end())
            languages.insert(languages.begin(), DefaultLanguage);
        Languages = std::move(languages);
        std::string dir = Directory.generic_string();
        while (!dir.empty() && (dir.back() == '/' || dir.back() == '\\'))
            dir.pop_back();
        if (dir.empty() || Directory.is_absolute() || dir.find("..") != std::string::npos)
            dir = "Localization";
        Directory = dir;
    }

    std::string LocalizationSettings::ToJson() const
    {
        ordered_json j;
        j["DefaultLanguage"] = DefaultLanguage;
        j["Languages"] = Languages;
        j["Directory"] = Directory.generic_string();
        j["UseSystemLanguage"] = UseSystemLanguage;
        return j.dump(4);
    }

    bool LocalizationSettings::FromJson(const std::string& text, LocalizationSettings& out)
    {
        json j = json::parse(text, nullptr, false);
        if (j.is_discarded() || !j.is_object())
            return false;
        LocalizationSettings s;
        if (auto it = j.find("DefaultLanguage"); it != j.end() && it->is_string())
            s.DefaultLanguage = it->get<std::string>();
        if (auto it = j.find("Languages"); it != j.end() && it->is_array())
            for (const json& code : *it)
                if (code.is_string())
                    s.Languages.push_back(code.get<std::string>());
        if (auto it = j.find("Directory"); it != j.end() && it->is_string())
            s.Directory = it->get<std::string>();
        if (auto it = j.find("UseSystemLanguage"); it != j.end() && it->is_boolean())
            s.UseSystemLanguage = it->get<bool>();
        s.Sanitize();
        out = std::move(s);
        return true;
    }

    // ------------------------------------------------------------------ Localization
    void Localization::SetSettings(const LocalizationSettings& settings, const std::filesystem::path& directory)
    {
        SetSettings(settings, directory, false);
    }

    void Localization::SetSettings(const LocalizationSettings& settings, const std::filesystem::path& directory, bool keepLanguage)
    {
        State& s = S();
        const std::string previous = s.Language;
        s.Settings = settings;
        s.Settings.Sanitize();
        s.Directory = directory;
        s.Missing.clear();
        s.Warned.clear();
        LoadTables();
        ComputeOffered();
        s.Language = keepLanguage && IsOffered(previous) ? previous : PickStartLanguage();
        if (!s.Tables.empty())
            ZM_CORE_INFO("Lokalizatsiya: {} ta til ({}), joriy til: {}", s.Tables.size(), s.Directory.filename().string(), s.Language);
        Changed();
    }

    const LocalizationSettings& Localization::GetSettings() { return S().Settings; }
    const std::filesystem::path& Localization::GetDirectory() { return S().Directory; }

    void Localization::SetTables(std::vector<StringTable> tables, const LocalizationSettings& settings)
    {
        State& s = S();
        s.Settings = settings;
        s.Settings.Sanitize();
        s.Directory.clear();
        s.Tables = std::move(tables);
        s.Missing.clear();
        s.Warned.clear();
        s.LoadErrors.clear();
        ComputeOffered();
        s.Language = PickStartLanguage();
        Changed();
    }

    void Localization::Reload()
    {
        State& s = S();
        if (s.Directory.empty())
            return;
        const std::string language = s.Language;
        s.Missing.clear();
        s.Warned.clear();
        LoadTables();
        ComputeOffered();
        s.Language = IsOffered(language) ? language : s.Settings.DefaultLanguage;
        Changed();
    }

    void Localization::Clear()
    {
        State& s = S();
        s.Settings = LocalizationSettings();
        s.Directory.clear();
        s.Tables.clear();
        s.Missing.clear();
        s.Warned.clear();
        s.LoadErrors.clear();
        ComputeOffered();
        s.Language = s.Settings.DefaultLanguage;
        Changed();
    }

    std::vector<std::string> Localization::GetLanguages() { return S().Offered; }

    bool Localization::IsOffered(std::string_view code)
    {
        const auto& offered = S().Offered;
        return std::find(offered.begin(), offered.end(), code) != offered.end();
    }

    bool Localization::SetLanguage(std::string_view code)
    {
        State& s = S();
        if (!IsOffered(code))
            return false;
        if (s.Language == code)
            return true;
        s.Language = std::string(code);
        ZM_CORE_INFO("Til: {} ({})", GetLanguageName(code), s.Language);
        Changed();
        return true;
    }

    const std::string& Localization::GetLanguage() { return S().Language; }
    const std::string& Localization::GetDefaultLanguage() { return S().Settings.DefaultLanguage; }

    bool Localization::NextLanguage()
    {
        const auto& offered = S().Offered;
        if (offered.size() < 2)
            return false;
        auto it = std::find(offered.begin(), offered.end(), S().Language);
        const size_t index = it == offered.end() ? 0 : (size_t)(it - offered.begin() + 1) % offered.size();
        return SetLanguage(offered[index]);
    }

    std::string Localization::GetLanguageName(std::string_view code)
    {
        if (const StringTable* table = TableOf(code); table && !table->DisplayName.empty())
            return table->DisplayName;
        static const std::pair<const char*, const char*> s_Names[] = {
            { "uz", "O'zbekcha" }, { "ru", "Русский" }, { "en", "English" }, { "tr", "Türkçe" }, { "kk", "Қазақша" },
            { "ky", "Кыргызча" }, { "tg", "Тоҷикӣ" }, { "de", "Deutsch" }, { "fr", "Français" }, { "es", "Español" },
            { "it", "Italiano" }, { "pt", "Português" }, { "uk", "Українська" }, { "pl", "Polski" }, { "ja", "日本語" },
            { "zh", "中文" }, { "ko", "한국어" }, { "ar", "العربية" }, { "fa", "فارسی" }, { "az", "Azərbaycanca" },
        };
        for (const auto& [c, name] : s_Names)
            if (code == c)
                return name;
        return std::string(code);
    }

    std::string Localization::ParseLocaleName(std::string_view locale)
    {
        std::string code;
        for (char c : locale)
        {
            if (c == '_' || c == '-' || c == '.' || c == '@')
                break;
            if (!(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z'))
                return {};
            code += (char)(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
        }
        if (code.size() < 2 || code.size() > 3 || code == "c")
            return {};
        return code;
    }

    std::string Localization::DetectSystemLanguage()
    {
        for (const char* variable : { "LC_ALL", "LC_MESSAGES", "LANG" })
            if (const char* value = std::getenv(variable); value && *value)
                return ParseLocaleName(value); // the first one set decides (POSIX order)
        return {};
    }

    const LocalizedString* Localization::FindEntry(std::string_view key)
    {
        State& s = S();
        if (const StringTable* table = TableOf(s.Language))
            if (const LocalizedString* entry = table->Find(key); entry && !entry->IsEmpty())
                return entry;
        if (s.Language != s.Settings.DefaultLanguage)
            if (const StringTable* table = TableOf(s.Settings.DefaultLanguage))
                if (const LocalizedString* entry = table->Find(key); entry && !entry->IsEmpty())
                    return entry;
        return nullptr;
    }

    const std::string* Localization::Find(std::string_view key)
    {
        const LocalizedString* entry = FindEntry(key);
        return entry ? &entry->Get(PluralCategory::Other) : nullptr;
    }

    std::string Localization::Get(std::string_view key)
    {
        if (const std::string* text = Find(key))
            return *text;
        State& s = S();
        const std::string id = s.Language + "|" + std::string(key);
        if (s.Warned.insert(id).second)
        {
            s.Missing.emplace_back(key);
            ZM_CORE_WARN("Lokalizatsiya: '{}' kaliti '{}' tilida ham, standart '{}' tilida ham yo'q", key, s.Language, s.Settings.DefaultLanguage);
        }
        return std::string(key);
    }

    std::string Localization::GetPlural(std::string_view key, double count)
    {
        State& s = S();
        // The form for the language the entry comes from (a fallback to the default language uses its rules).
        if (const StringTable* table = TableOf(s.Language))
            if (const LocalizedString* entry = table->Find(key); entry && !entry->IsEmpty())
                return entry->Get(GetPluralCategory(s.Language, count));
        if (const StringTable* table = TableOf(s.Settings.DefaultLanguage))
            if (const LocalizedString* entry = table->Find(key); entry && !entry->IsEmpty())
                return entry->Get(GetPluralCategory(s.Settings.DefaultLanguage, count));
        return Get(key); // warns
    }

    PluralCategory Localization::GetPluralCategory(std::string_view language, double count)
    {
        const double n = std::fabs(count);
        const bool integer = std::isfinite(n) && n == std::floor(n);
        const long long i = integer ? (long long)std::min(n, 9.0e15) : 0;
        // No plural forms at all.
        static const char* const s_Other[] = { "ja", "zh", "ko", "vi", "th", "id", "ms" };
        for (const char* code : s_Other)
            if (language == code)
                return PluralCategory::Other;
        // East Slavic: one / few / many (fractions: other).
        if (language == "ru" || language == "uk" || language == "be")
        {
            if (!integer)
                return PluralCategory::Other;
            const long long mod10 = i % 10, mod100 = i % 100;
            if (mod10 == 1 && mod100 != 11)
                return PluralCategory::One;
            if (mod10 >= 2 && mod10 <= 4 && (mod100 < 12 || mod100 > 14))
                return PluralCategory::Few;
            return PluralCategory::Many;
        }
        if (language == "pl")
        {
            if (!integer)
                return PluralCategory::Other;
            const long long mod10 = i % 10, mod100 = i % 100;
            if (i == 1)
                return PluralCategory::One;
            if (mod10 >= 2 && mod10 <= 4 && (mod100 < 12 || mod100 > 14))
                return PluralCategory::Few;
            return PluralCategory::Many;
        }
        // French, Portuguese (Brazil): 0 and 1 are one.
        if (language == "fr")
            return n < 2.0 ? PluralCategory::One : PluralCategory::Other;
        if (language == "ar")
        {
            if (!integer)
                return PluralCategory::Other;
            const long long mod100 = i % 100;
            if (i == 0) return PluralCategory::Zero;
            if (i == 1) return PluralCategory::One;
            if (i == 2) return PluralCategory::Two;
            if (mod100 >= 3 && mod100 <= 10) return PluralCategory::Few;
            if (mod100 >= 11) return PluralCategory::Many;
            return PluralCategory::Other;
        }
        // English, Uzbek, German, Turkish, Spanish ... : one for exactly 1.
        return integer && i == 1 ? PluralCategory::One : PluralCategory::Other;
    }

    std::vector<std::string> Localization::GetPluralCategories(std::string_view language)
    {
        for (const char* code : { "ja", "zh", "ko", "vi", "th", "id", "ms" })
            if (language == code)
                return { "other" };
        if (language == "ru" || language == "uk" || language == "be" || language == "pl")
            return { "one", "few", "many", "other" };
        if (language == "ar")
            return { "zero", "one", "two", "few", "many", "other" };
        return { "one", "other" };
    }

    const char* Localization::GetPluralCategoryName(PluralCategory category)
    {
        const int index = std::clamp((int)category, 0, 5);
        return s_CategoryNames[index];
    }

    uint64_t Localization::GetVersion() { return S().Version; }

    uint64_t Localization::AddListener(Listener listener)
    {
        State& s = S();
        const uint64_t id = s.NextListener++;
        s.Listeners.emplace_back(id, std::move(listener));
        return id;
    }

    void Localization::RemoveListener(uint64_t id)
    {
        auto& listeners = S().Listeners;
        listeners.erase(std::remove_if(listeners.begin(), listeners.end(), [&](const auto& l) { return l.first == id; }), listeners.end());
    }

    bool Localization::SaveSettings()
    {
        SaveData::SetString(SettingsKey, S().Language);
        return SaveData::Save();
    }

    bool Localization::LoadSettings()
    {
        if (SaveData::GetType(SettingsKey) != SaveValueType::String)
            return false;
        const std::string code = SaveData::GetString(SettingsKey);
        if (!IsOffered(code))
        {
            ZM_CORE_WARN("Saqlangan til '{}' o'yinda yo'q - '{}' qoldi", code, S().Language);
            return false;
        }
        return SetLanguage(code);
    }

    bool Localization::HasSavedSettings() { return SaveData::GetType(SettingsKey) == SaveValueType::String; }

    void Localization::ClearSavedSettings()
    {
        SaveData::DeleteKey(SettingsKey);
        SaveData::Save();
    }

    const StringTable* Localization::GetTable(std::string_view language) { return TableOf(language); }
    std::vector<std::string> Localization::GetMissingKeys() { return S().Missing; }
    const std::vector<std::string>& Localization::GetLoadErrors() { return S().LoadErrors; }

    // ------------------------------------------------------------------ LocalizationTable
    std::vector<std::string> LocalizationTable::GetKeys() const
    {
        std::set<std::string, std::less<>> keys;
        for (const StringTable& t : Languages)
            for (const auto& [key, entry] : t.Strings)
                keys.insert(key);
        return { keys.begin(), keys.end() };
    }

    int LocalizationTable::FindLanguage(std::string_view code) const
    {
        for (size_t i = 0; i < Languages.size(); i++)
            if (Languages[i].Language == code)
                return (int)i;
        return -1;
    }

    const LocalizedString* LocalizationTable::Get(std::string_view key, std::string_view language) const
    {
        const int index = FindLanguage(language);
        return index >= 0 ? Languages[(size_t)index].Find(key) : nullptr;
    }

    bool LocalizationTable::Set(const std::string& key, std::string_view language, LocalizedString value)
    {
        const int index = FindLanguage(language);
        if (index < 0 || !IsValidKey(key))
            return false;
        Languages[(size_t)index].Strings[key] = std::move(value);
        return true;
    }

    bool LocalizationTable::AddKey(const std::string& key)
    {
        if (!IsValidKey(key))
            return false;
        for (const StringTable& t : Languages)
            if (t.Find(key))
                return false;
        for (StringTable& t : Languages)
            t.Strings[key] = LocalizedString();
        return !Languages.empty();
    }

    bool LocalizationTable::RenameKey(const std::string& from, const std::string& to)
    {
        if (from == to || !IsValidKey(to))
            return false;
        bool found = false;
        for (const StringTable& t : Languages)
        {
            if (t.Find(to))
                return false;
            found |= t.Find(from) != nullptr;
        }
        if (!found)
            return false;
        for (StringTable& t : Languages)
            if (auto node = t.Strings.extract(from); !node.empty())
            {
                node.key() = to;
                t.Strings.insert(std::move(node));
            }
        return true;
    }

    bool LocalizationTable::RemoveKey(const std::string& key)
    {
        bool removed = false;
        for (StringTable& t : Languages)
            removed |= t.Strings.erase(key) > 0;
        return removed;
    }

    bool LocalizationTable::AddLanguage(const std::string& code)
    {
        if (!LocalizationSettings::IsValidCode(code) || FindLanguage(code) >= 0)
            return false;
        StringTable table;
        table.Language = code;
        Languages.push_back(std::move(table));
        return true;
    }

    bool LocalizationTable::RemoveLanguage(std::string_view code)
    {
        const int index = FindLanguage(code);
        if (index < 0)
            return false;
        Languages.erase(Languages.begin() + index);
        return true;
    }

    bool LocalizationTable::IsMissing(std::string_view key, std::string_view language) const
    {
        const LocalizedString* entry = Get(key, language);
        return !entry || entry->IsEmpty();
    }

    std::vector<std::string> LocalizationTable::GetMissing(std::string_view language) const
    {
        std::vector<std::string> missing;
        if (FindLanguage(language) < 0)
            return missing;
        for (const std::string& key : GetKeys())
            if (IsMissing(key, language))
                missing.push_back(key);
        return missing;
    }

    size_t LocalizationTable::CountMissing(std::string_view language) const { return GetMissing(language).size(); }

    bool LocalizationTable::IsValidKey(std::string_view key)
    {
        if (key.empty() || key.front() == '$' || key.front() == '.' || key.back() == '.' || key.find("..") != std::string_view::npos)
            return false;
        for (char c : key)
            if ((unsigned char)c <= 0x20 || c == 0x7F)
                return false;
        return true;
    }

    std::vector<int> LocalizationTable::GetPlaceholders(std::string_view text)
    {
        std::vector<int> found;
        int automatic = 0;
        for (size_t i = 0; i < text.size(); i++)
        {
            if (text[i] != '{')
                continue;
            if (i + 1 < text.size() && text[i + 1] == '{')
            {
                i++; // "{{": a brace
                continue;
            }
            const size_t end = text.find('}', i);
            if (end == std::string_view::npos)
                break;
            std::string_view inside = text.substr(i + 1, end - i - 1);
            const size_t colon = inside.find(':');
            std::string_view index = inside.substr(0, colon);
            int value = -1;
            if (index.empty())
                value = automatic++;
            else if (std::all_of(index.begin(), index.end(), [](char c) { return c >= '0' && c <= '9'; }) && index.size() < 4)
                value = std::stoi(std::string(index));
            if (value >= 0 && std::find(found.begin(), found.end(), value) == found.end())
                found.push_back(value);
            i = end;
        }
        std::sort(found.begin(), found.end());
        return found;
    }

    bool LocalizationTable::HasPlaceholderMismatch(std::string_view key, std::string_view language, std::string_view reference) const
    {
        const LocalizedString* entry = Get(key, language);
        const LocalizedString* ref = Get(key, reference);
        if (!entry || !ref || entry->IsEmpty() || ref->IsEmpty() || language == reference)
            return false;
        // Plural forms: the "other" forms are compared (one / few may leave the count out: "bitta tanga").
        return GetPlaceholders(entry->Get(PluralCategory::Other)) != GetPlaceholders(ref->Get(PluralCategory::Other));
    }

    LocalizationTable LocalizationTable::Load(const std::filesystem::path& directory, const std::vector<std::string>& languages,
                                              std::vector<std::string>* errors)
    {
        LocalizationTable table;
        for (const std::string& code : languages)
        {
            StringTable strings;
            strings.Language = code;
            const std::filesystem::path file = directory / (code + ".json");
            std::error_code ec;
            if (std::filesystem::exists(file, ec))
            {
                std::string error;
                if (!StringTable::Parse(FileSystem::ReadTextFile(file), strings, &error) && errors)
                    errors->push_back(std::format("{}: {}", file.filename().string(), error));
                strings.Language = code;
            }
            table.Languages.push_back(std::move(strings));
        }
        return table;
    }

    std::vector<std::string> LocalizationTable::FindLanguageFiles(const std::filesystem::path& directory)
    {
        std::vector<std::string> codes;
        std::error_code ec;
        if (!std::filesystem::is_directory(directory, ec))
            return codes;
        for (const auto& entry : std::filesystem::directory_iterator(directory, ec))
        {
            if (!entry.is_regular_file(ec) || entry.path().extension() != ".json")
                continue;
            const std::string code = entry.path().stem().string();
            if (LocalizationSettings::IsValidCode(code))
                codes.push_back(code);
        }
        std::sort(codes.begin(), codes.end());
        return codes;
    }

    bool LocalizationTable::Save(const std::filesystem::path& directory, std::vector<std::string>* errors) const
    {
        std::error_code ec;
        std::filesystem::create_directories(directory, ec);
        bool ok = true;
        for (const StringTable& t : Languages)
        {
            const std::filesystem::path file = directory / (t.Language + ".json");
            if (!FileSystem::WriteTextFile(file, t.ToJson() + "\n"))
            {
                ok = false;
                if (errors)
                    errors->push_back(std::format("{}: yozilmadi", file.string()));
            }
        }
        return ok;
    }

}
