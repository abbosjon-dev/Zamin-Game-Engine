#include "Zamin/Scene/SceneRecoveryJson.h"

#include <cctype>
#include <ctime>
#include <format>

namespace Zamin {

    using json = nlohmann::json;

    namespace {

        constexpr size_t npos = std::string::npos;

        bool IsSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

        // A tolerant scanner over JSON text: strings (with escapes) are skipped as a whole, brackets
        // are counted (their kinds are not matched: broken text is what it is used on).
        struct Scanner
        {
            const std::string& Text;
            size_t N;

            explicit Scanner(const std::string& text) : Text(text), N(text.size()) {}

            size_t SkipSpace(size_t i) const
            {
                while (i < N && IsSpace(Text[i]))
                    i++;
                return i;
            }

            // The closing quote of the string that starts at `from` (Text[from] == '"'), or npos.
            size_t StringEnd(size_t from) const
            {
                for (size_t k = from + 1; k < N; k++)
                {
                    if (Text[k] == '\\')
                    {
                        k++;
                        continue;
                    }
                    if (Text[k] == '"')
                        return k;
                    if (Text[k] == '\n')
                        return npos; // JSON strings do not span lines: the quote was broken
                }
                return npos;
            }

            // One past the end of the value that starts at `from`, or npos when the text ends first.
            size_t ValueEnd(size_t from) const
            {
                if (from >= N)
                    return npos;
                const char c = Text[from];
                if (c == '"')
                {
                    const size_t e = StringEnd(from);
                    return e == npos ? npos : e + 1;
                }
                if (c == '{' || c == '[')
                {
                    int depth = 0;
                    for (size_t k = from; k < N; k++)
                    {
                        const char d = Text[k];
                        if (d == '"')
                        {
                            k = StringEnd(k);
                            if (k == npos)
                                return npos;
                        }
                        else if (d == '{' || d == '[')
                            depth++;
                        else if (d == '}' || d == ']')
                        {
                            if (--depth == 0)
                                return k + 1;
                        }
                    }
                    return npos;
                }
                // A number / true / false / null: up to the next separator (the text ending first
                // may have cut it short).
                size_t k = from;
                while (k < N && Text[k] != ',' && Text[k] != '}' && Text[k] != ']' && !IsSpace(Text[k]))
                    k++;
                return k < N ? k : npos;
            }

            // The column of `at` when only white space is in front of it on its line (else npos).
            size_t LineIndent(size_t at) const
            {
                size_t k = at;
                while (k > 0 && Text[k - 1] != '\n')
                {
                    if (!IsSpace(Text[k - 1]))
                        return npos;
                    k--;
                }
                return at - k;
            }

            // The next line after `from` whose first character (after `indent` spaces) is '{': where
            // the next entry of a pretty-printed list starts. npos when there is none.
            size_t NextEntryLine(size_t from, size_t indent) const
            {
                for (size_t k = Text.find('\n', from); k != npos; k = Text.find('\n', k + 1))
                {
                    size_t start = k + 1, s = start;
                    while (s < N && (Text[s] == ' ' || Text[s] == '\t'))
                        s++;
                    if (s - start == indent && s < N && Text[s] == '{')
                        return s;
                    if (s - start < indent && s < N && Text[s] == ']')
                        return npos; // the list ends first
                }
                return npos;
            }
        };

        bool TryParse(const std::string& text, size_t begin, size_t end, json& out)
        {
            out = json::parse(text.begin() + (ptrdiff_t)begin, text.begin() + (ptrdiff_t)end, nullptr, false);
            return !out.is_discarded();
        }
    }

    bool SceneRecoveryJson::NestsDeeperThan(const std::string& text, int maxDepth)
    {
        int depth = 0;
        bool inString = false;
        for (size_t i = 0; i < text.size(); i++)
        {
            const char c = text[i];
            if (inString)
            {
                if (c == '\\')
                    i++;
                else if (c == '"' || c == '\n')
                    inString = false;
                continue;
            }
            if (c == '"')
                inString = true;
            else if (c == '{' || c == '[')
            {
                if (++depth > maxDepth)
                    return true;
            }
            else if ((c == '}' || c == ']') && depth > 0)
                depth--;
        }
        return false;
    }

    std::optional<json> SceneRecoveryJson::Salvage(const std::string& text, SceneRecovery::Result* stats)
    {
        SceneRecovery::Result local;
        SceneRecovery::Result& r = stats ? *stats : local;
        r = {};
        if (NestsDeeperThan(text, SceneRecovery::MaxDepth))
        {
            r.Error = std::format("JSON {} qavatdan chuqur: tiklanmaydi", SceneRecovery::MaxDepth);
            return std::nullopt;
        }
        const Scanner sc(text);
        size_t i = sc.SkipSpace(0);
        if (i >= sc.N || text[i] != '{')
        {
            r.Error = "JSON obyekt emas: tiklab bo'lmaydi";
            return std::nullopt;
        }
        json root = json::object();
        bool closed = false, sawEntities = false;
        i++;
        while (true)
        {
            while (i < sc.N && (IsSpace(text[i]) || text[i] == ','))
                i++;
            if (i >= sc.N)
                break;
            if (text[i] == '}')
            {
                closed = true;
                i++;
                break;
            }
            if (text[i] != '"')
            {
                // Garbage between the top-level parts: up to the next key (or the end of the file's object).
                const size_t from = i;
                while (i < sc.N && text[i] != '"' && text[i] != '}')
                    i++;
                r.BytesSkipped += i - from;
                continue;
            }
            const size_t keyEnd = sc.StringEnd(i);
            if (keyEnd == npos)
            {
                r.BytesSkipped += sc.N - i;
                i = sc.N;
                break;
            }
            json keyJson;
            const size_t keyStart = i;
            i = sc.SkipSpace(keyEnd + 1);
            if (!TryParse(text, keyStart, keyEnd + 1, keyJson) || !keyJson.is_string() || i >= sc.N || text[i] != ':')
            {
                r.BytesSkipped += i - keyStart;
                continue;
            }
            const std::string key = keyJson.get<std::string>();
            i = sc.SkipSpace(i + 1);
            if (i >= sc.N)
                break;
            if (key == "Entities" && text[i] == '[')
            {
                sawEntities = true;
                json list = json::array();
                i++;
                bool listClosed = false;
                while (true)
                {
                    while (i < sc.N && (IsSpace(text[i]) || text[i] == ','))
                        i++;
                    if (i >= sc.N)
                        break;
                    if (text[i] == ']')
                    {
                        listClosed = true;
                        i++;
                        break;
                    }
                    if (text[i] != '{')
                    {
                        // Garbage between entries: resync on the next entry (or the list's end).
                        const size_t from = i;
                        while (i < sc.N && text[i] != '{' && text[i] != ']')
                            i++;
                        r.BytesSkipped += i - from;
                        continue;
                    }
                    const size_t start = i;
                    const size_t end = sc.ValueEnd(start);
                    json entry;
                    if (end != npos && TryParse(text, start, end, entry) && entry.is_object())
                    {
                        list.push_back(std::move(entry));
                        r.Entities++;
                        i = end;
                        continue;
                    }
                    // A broken entry (or one the text ends in). In a pretty-printed file every entry
                    // starts on a line of its own at the same indent: the next one is found there even
                    // when this one lost a bracket.
                    r.EntriesDropped++;
                    const size_t indent = sc.LineIndent(start);
                    const size_t next = indent != npos ? sc.NextEntryLine(start, indent) : npos;
                    if (next != npos && (end == npos || next < end))
                    {
                        r.BytesSkipped += next - start;
                        i = next;
                        continue;
                    }
                    if (end == npos)
                    {
                        r.BytesSkipped += sc.N - start;
                        i = sc.N;
                        break;
                    }
                    r.BytesSkipped += end - start;
                    i = end;
                }
                root["Entities"] = std::move(list);
                if (!listClosed)
                    break;
                continue;
            }
            const size_t end = sc.ValueEnd(i);
            json value;
            if (end == npos)
            {
                r.LostKeys.push_back(key);
                r.BytesSkipped += sc.N - i;
                i = sc.N;
                break;
            }
            if (TryParse(text, i, end, value))
                root[key] = std::move(value);
            else
            {
                r.LostKeys.push_back(key);
                r.BytesSkipped += end - i;
            }
            i = end;
        }
        if (closed)
        {
            const size_t rest = sc.SkipSpace(i);
            r.BytesSkipped += sc.N - rest; // anything after the file's object
        }
        r.Truncated = !closed;
        if (!sawEntities && !root.contains("Scene") && !root.contains("ZaminEntities"))
        {
            r.Error = "sahna fayli emas (\"Scene\" / \"Entities\" topilmadi)";
            return std::nullopt;
        }
        if (!root.contains("Entities"))
            root["Entities"] = json::array();
        r.Recovered = true;
        return root;
    }

    SceneRecovery::Result SceneRecovery::Salvage(const std::string& text)
    {
        Result result;
        if (std::optional<json> root = SceneRecoveryJson::Salvage(text, &result))
            result.Text = root->dump(2);
        return result;
    }

    std::filesystem::path SceneRecovery::BackupPath(const std::filesystem::path& file, std::chrono::system_clock::time_point when)
    {
        const std::time_t t = std::chrono::system_clock::to_time_t(when);
        std::tm tm{};
        localtime_r(&t, &tm);
        char stamp[32];
        std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tm);
        std::filesystem::path out = file;
        out += std::format(".corrupt-{}.bak", stamp);
        return out;
    }

    bool SceneRecovery::WriteBackup(const std::filesystem::path& file, std::filesystem::path* backup)
    {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(file, ec))
            return false;
        std::filesystem::path target = BackupPath(file);
        for (int n = 2; std::filesystem::exists(target, ec) && n < 100; n++) // two in one second
        {
            target = BackupPath(file);
            target.replace_extension(std::format(".{}.bak", n));
        }
        std::filesystem::copy_file(file, target, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec)
            return false;
        if (backup)
            *backup = target;
        return true;
    }

    std::string SceneRecovery::Summary(const Result& result)
    {
        std::string text = std::format("{} obyekt tiklandi, {} bayt o'tkazib yuborildi", result.Entities, result.BytesSkipped);
        if (result.EntriesDropped)
            text += std::format(" ({} ta buzilgan yozuv tashlab ketildi)", result.EntriesDropped);
        return text;
    }

}
