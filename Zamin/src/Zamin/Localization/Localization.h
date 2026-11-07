#pragma once

#include <cstdint>
#include <filesystem>
#include <format>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace Zamin {

    // The plural categories of CLDR: which form of a counted text a language uses for a number
    // ("1 tanga" / "5 tanga"; "1 монета" / "2 монеты" / "5 монет").
    enum class PluralCategory { Zero = 0, One, Two, Few, Many, Other };

    // One translated text: plain, or a plural entry with a form per category ("one", "few", "other" ...).
    struct LocalizedString
    {
        std::string Text;                                       // a plain text ("" with plural forms)
        std::map<std::string, std::string, std::less<>> Plural; // category name ("one", "other" ...) -> text

        bool IsPlural() const { return !Plural.empty(); }
        // Nothing to show: no text and no plural form (counts as missing: the default language's is used).
        bool IsEmpty() const;
        // The text for a count's category: that form, else "other", else the first form, else Text.
        const std::string& Get(PluralCategory category) const;
        bool operator==(const LocalizedString&) const = default;
    };

    // The strings of one language: the file Assets/<Localization dir>/<code>.json. Nested JSON objects
    // become dotted keys ({ "menu": { "start": "Boshlash" } } -> "menu.start"); an object holding only
    // plural forms ("one", "few", "many", "other" ...) is one plural entry. Keys starting with '$' are
    // about the file: "$name" is the language's own name ("O'zbekcha", "Русский").
    struct StringTable
    {
        std::string Language;    // "uz", "ru", "en" ...
        std::string DisplayName; // "$name" ("": a built-in name, see Localization::GetLanguageName)
        std::map<std::string, LocalizedString, std::less<>> Strings;

        const LocalizedString* Find(std::string_view key) const;
        // Has the key with something to show (an empty text counts as missing).
        bool Has(std::string_view key) const;

        // Reads a file's JSON text. False (`error`) when it is not a JSON object; values of other
        // types (numbers, arrays) are skipped, each with a line in `warnings`.
        static bool Parse(const std::string& text, StringTable& out, std::string* error = nullptr, std::vector<std::string>* warnings = nullptr);
        // The file's JSON: "$name" first, then the keys sorted and nested at their dots (a key that
        // cannot nest - "a" is a text and "a.b" too - is written flat with its dots).
        std::string ToJson() const;
        bool operator==(const StringTable&) const = default;
    };

    // Project settings > Localization (the .zproject "Localization" section).
    struct LocalizationSettings
    {
        // The language the game starts in and whose strings are used where another language lacks one.
        std::string DefaultLanguage = "uz";
        // The languages the game offers, in the order a language menu shows them (empty: every
        // <code>.json file of the folder, the default language first).
        std::vector<std::string> Languages;
        // The folder of the language files, relative to the assets.
        std::filesystem::path Directory = "Localization";
        // Start in the system's language ($LC_ALL / $LC_MESSAGES / $LANG) when the game offers it.
        bool UseSystemLanguage = false;

        // A language code: 2..8 letters, digits, '-' or '_' ("uz", "pt-BR", "zh_Hans").
        static bool IsValidCode(std::string_view code);
        // A valid default language (else "uz"), valid unique codes, a relative folder.
        void Sanitize();
        bool IsDefault() const { return *this == LocalizationSettings(); }
        std::string ToJson() const;
        // Unknown / missing values: defaults; then sanitized. False for text that is not a JSON object.
        static bool FromJson(const std::string& text, LocalizationSettings& out);
        bool operator==(const LocalizationSettings&) const = default;
    };

    // The game's texts in the player's language (scripts: Localize("menu.start")). The project's
    // language files are loaded when the project is (Project::Load); Text components with a
    // localization key follow the language by themselves, scripts get OnLanguageChanged.
    //
    //   Localize("menu.start")                       -> "Boshlash" / "Начать" / "Start"
    //   Localize("hud.score", score)                 -> "Ochko: {0}" with {0} = score
    //   LocalizePlural("hud.coins", coins)           -> "{0} tanga" / "{0} монеты" ... ({0} = the count)
    //   Localization::SetLanguage("ru"); Localization::SaveSettings();  // a language menu
    class Localization
    {
    public:
        // SaveData key of the player's chosen language (SaveSettings / LoadSettings).
        static constexpr const char* SettingsKey = "Zamin.Language";

        // ---- setup (Project::Load; the editor after the settings changed)
        // `directory`: the language files' folder (absolute). Loads every offered language and picks
        // the start language (the system's one when asked for and offered, else the default one).
        static void SetSettings(const LocalizationSettings& settings, const std::filesystem::path& directory);
        // Keeps the current language when it is still offered (the editor's preview).
        static void SetSettings(const LocalizationSettings& settings, const std::filesystem::path& directory, bool keepLanguage);
        static const LocalizationSettings& GetSettings();
        static const std::filesystem::path& GetDirectory();
        // Tables given directly (tests, tools): no files are read.
        static void SetTables(std::vector<StringTable> tables, const LocalizationSettings& settings);
        // Reads the language files again (after they were edited); the language stays.
        static void Reload();
        // No settings, no tables, the default language (no project).
        static void Clear();

        // ---- languages
        // The offered languages (the settings' list, else the files found: default first, then by code).
        static std::vector<std::string> GetLanguages();
        static bool IsOffered(std::string_view code);
        // False (nothing changes) for a language that is not offered.
        static bool SetLanguage(std::string_view code);
        static const std::string& GetLanguage();
        static const std::string& GetDefaultLanguage();
        // The next offered language (wraps around): a language button's click.
        static bool NextLanguage();
        // The language's own name: its file's "$name", else a built-in one ("O'zbekcha", "Русский",
        // "English" ...), else the code.
        static std::string GetLanguageName(std::string_view code);
        // The system's language from $LC_ALL / $LC_MESSAGES / $LANG ("ru_RU.UTF-8" -> "ru"; "" for C / POSIX / none).
        static std::string DetectSystemLanguage();
        // "uz_UZ.UTF-8@latin" / "en-US" -> "uz" / "en" ("" when it is not a language).
        static std::string ParseLocaleName(std::string_view locale);

        // ---- strings
        // The current language's text, else the default language's; nullptr when neither has the key.
        static const LocalizedString* FindEntry(std::string_view key);
        static const std::string* Find(std::string_view key);
        static bool Has(std::string_view key) { return Find(key) != nullptr; }
        // The text as it is in the file (not formatted); the key itself when it is missing (warned once).
        static std::string Get(std::string_view key);
        // The form for `count` of a plural entry in the current language (a plain entry: its text).
        static std::string GetPlural(std::string_view key, double count);
        // The text with {0}, {1} ... ({0:.1f}, {1:>4} ... as std::format) filled in; without arguments
        // the text is as it is (braces need no escaping).
        template<typename... Args>
        static std::string Localize(std::string_view key, const Args&... args)
        {
            if constexpr (sizeof...(Args) == 0)
                return Get(key);
            else
                return Format(Get(key), args...);
        }
        // {0} is the count, the other arguments follow as {1}, {2} ...
        template<typename N, typename... Args>
        static std::string LocalizePlural(std::string_view key, N count, const Args&... args)
        {
            static_assert(std::is_arithmetic_v<N>, "LocalizePlural: the count must be a number");
            return Format(GetPlural(key, (double)count), count, args...);
        }
        // A broken pattern ("{", "{5}" with fewer arguments) comes back as it is.
        template<typename... Args>
        static std::string Format(std::string_view pattern, const Args&... args)
        {
            try
            {
                return std::vformat(pattern, std::make_format_args(args...));
            }
            catch (const std::exception&)
            {
                return std::string(pattern);
            }
        }
        // The plural category of `count` in a language (CLDR rules for integers of the common
        // languages; others: one for 1, other otherwise; ja / zh / ko ...: always other).
        static PluralCategory GetPluralCategory(std::string_view language, double count);
        // The categories a language uses ("one", "other" / "one", "few", "many", "other" ...): the
        // forms the table editor asks for.
        static std::vector<std::string> GetPluralCategories(std::string_view language);
        static const char* GetPluralCategoryName(PluralCategory category);

        // ---- changes
        // Goes up whenever the language changes or the tables are reloaded (Text components compare it).
        static uint64_t GetVersion();
        using Listener = std::function<void(const std::string& language)>;
        static uint64_t AddListener(Listener listener);
        static void RemoveListener(uint64_t id);

        // ---- the player's choice (a language menu), kept with SaveData
        static bool SaveSettings();
        // The saved language when it is still offered (false: nothing saved / not offered).
        static bool LoadSettings();
        static bool HasSavedSettings();
        static void ClearSavedSettings();

        // ---- tables
        static const StringTable* GetTable(std::string_view language);
        // Keys asked for (Get / Localize) that no language had, in the order they were asked for.
        static std::vector<std::string> GetMissingKeys();
        // Problems of the last load (unreadable files ...), for the editor.
        static const std::vector<std::string>& GetLoadErrors();
    };

    // Script shortcuts: Localize("menu.start"), Localize("hud.score", score), LocalizePlural("hud.coins", n).
    template<typename... Args>
    inline std::string Localize(std::string_view key, const Args&... args)
    {
        return Localization::Localize(key, args...);
    }

    template<typename N, typename... Args>
    inline std::string LocalizePlural(std::string_view key, N count, const Args&... args)
    {
        return Localization::LocalizePlural(key, count, args...);
    }

    // Every language's strings side by side: the editor's localization table (keys x languages) and
    // tools. Columns are languages, rows the keys of any of them.
    class LocalizationTable
    {
    public:
        std::vector<StringTable> Languages; // column order

        // Every key of any language, sorted.
        std::vector<std::string> GetKeys() const;
        int FindLanguage(std::string_view code) const;
        const LocalizedString* Get(std::string_view key, std::string_view language) const;
        // Sets (adds) a key's text in a language (false: no such language).
        bool Set(const std::string& key, std::string_view language, LocalizedString value);
        // A new key with empty texts in every language (false: empty, invalid or taken).
        bool AddKey(const std::string& key);
        // False when `from` is unknown or `to` is invalid / taken.
        bool RenameKey(const std::string& from, const std::string& to);
        bool RemoveKey(const std::string& key);
        // A new column (false: invalid code or there already).
        bool AddLanguage(const std::string& code);
        bool RemoveLanguage(std::string_view code);

        // The key has no text (or an empty one) in that language.
        bool IsMissing(std::string_view key, std::string_view language) const;
        // The keys of the table the language lacks (or has empty).
        std::vector<std::string> GetMissing(std::string_view language) const;
        size_t CountMissing(std::string_view language) const;
        // A key name: not empty, no spaces / control characters, does not start with '$' or '.',
        // no empty part between dots.
        static bool IsValidKey(std::string_view key);
        // The {0}, {1:.2f} ... placeholders of a text (their argument indices; {} counts in order).
        static std::vector<int> GetPlaceholders(std::string_view text);
        // The language's text uses other placeholders than the reference language's (a translation
        // that would show the wrong numbers).
        bool HasPlaceholderMismatch(std::string_view key, std::string_view language, std::string_view reference) const;

        // <directory>/<code>.json of each language (missing files: empty columns; `errors` gets why a
        // file could not be read).
        static LocalizationTable Load(const std::filesystem::path& directory, const std::vector<std::string>& languages,
                                      std::vector<std::string>* errors = nullptr);
        // The <code>.json file of each language found in the folder (sorted by code).
        static std::vector<std::string> FindLanguageFiles(const std::filesystem::path& directory);
        // Writes every column's file (the folder is made). False when one could not be written.
        bool Save(const std::filesystem::path& directory, std::vector<std::string>* errors = nullptr) const;

        bool operator==(const LocalizationTable&) const = default;
    };

}
