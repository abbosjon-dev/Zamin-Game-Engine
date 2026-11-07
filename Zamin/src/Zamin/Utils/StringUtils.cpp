#include "Zamin/Utils/StringUtils.h"

#include <algorithm>
#include <cmath>

namespace Zamin::StringUtils {

    // ---------------------------------------------------------------- trimming
    std::string_view TrimStart(std::string_view text)
    {
        size_t b = 0;
        while (b < text.size() && IsSpace(text[b]))
            b++;
        return text.substr(b);
    }

    std::string_view TrimEnd(std::string_view text)
    {
        size_t e = text.size();
        while (e > 0 && IsSpace(text[e - 1]))
            e--;
        return text.substr(0, e);
    }

    std::string_view Trim(std::string_view text)
    {
        return TrimEnd(TrimStart(text));
    }

    std::string_view Trim(std::string_view text, std::string_view characters)
    {
        const size_t b = text.find_first_not_of(characters);
        if (b == std::string_view::npos)
            return text.substr(text.size());
        const size_t e = text.find_last_not_of(characters);
        return text.substr(b, e - b + 1);
    }

    // ---------------------------------------------------------------- case
    std::string ToLower(std::string_view text)
    {
        std::string out(text);
        ToLowerInPlace(out);
        return out;
    }

    std::string ToUpper(std::string_view text)
    {
        std::string out(text);
        ToUpperInPlace(out);
        return out;
    }

    void ToLowerInPlace(std::string& text)
    {
        for (char& c : text)
            c = ToLower(c);
    }

    void ToUpperInPlace(std::string& text)
    {
        for (char& c : text)
            c = ToUpper(c);
    }

    bool EqualsIgnoreCase(std::string_view a, std::string_view b)
    {
        if (a.size() != b.size())
            return false;
        for (size_t i = 0; i < a.size(); i++)
            if (ToLower(a[i]) != ToLower(b[i]))
                return false;
        return true;
    }

    int CompareIgnoreCase(std::string_view a, std::string_view b)
    {
        const size_t n = std::min(a.size(), b.size());
        for (size_t i = 0; i < n; i++)
        {
            const unsigned char x = (unsigned char)ToLower(a[i]), y = (unsigned char)ToLower(b[i]);
            if (x != y)
                return x < y ? -1 : 1;
        }
        return a.size() == b.size() ? 0 : a.size() < b.size() ? -1 : 1;
    }

    // ---------------------------------------------------------------- searching
    bool StartsWithIgnoreCase(std::string_view text, std::string_view prefix)
    {
        return text.size() >= prefix.size() && EqualsIgnoreCase(text.substr(0, prefix.size()), prefix);
    }

    bool EndsWithIgnoreCase(std::string_view text, std::string_view suffix)
    {
        return text.size() >= suffix.size() && EqualsIgnoreCase(text.substr(text.size() - suffix.size()), suffix);
    }

    size_t FindIgnoreCase(std::string_view text, std::string_view part, size_t from)
    {
        if (part.empty())
            return from <= text.size() ? from : std::string_view::npos;
        for (size_t i = from; i + part.size() <= text.size(); i++)
            if (EqualsIgnoreCase(text.substr(i, part.size()), part))
                return i;
        return std::string_view::npos;
    }

    size_t Count(std::string_view text, std::string_view part)
    {
        if (part.empty())
            return 0;
        size_t count = 0;
        for (size_t at = text.find(part); at != std::string_view::npos; at = text.find(part, at + part.size()))
            count++;
        return count;
    }

    // ---------------------------------------------------------------- splitting
    namespace {

        void AddPart(std::vector<std::string_view>& parts, std::string_view part, SplitFlags flags)
        {
            if (HasFlag(flags, SplitFlags::Trim))
                part = Trim(part);
            if (part.empty() && HasFlag(flags, SplitFlags::SkipEmpty))
                return;
            parts.push_back(part);
        }

        std::vector<std::string> ToStrings(const std::vector<std::string_view>& views)
        {
            return { views.begin(), views.end() };
        }

    }

    std::vector<std::string_view> SplitView(std::string_view text, char delimiter, SplitFlags flags)
    {
        std::vector<std::string_view> parts;
        size_t start = 0;
        for (size_t i = 0; i <= text.size(); i++)
            if (i == text.size() || text[i] == delimiter)
            {
                AddPart(parts, text.substr(start, i - start), flags);
                start = i + 1;
            }
        return parts;
    }

    std::vector<std::string_view> SplitView(std::string_view text, std::string_view delimiter, SplitFlags flags)
    {
        std::vector<std::string_view> parts;
        if (delimiter.empty())
        {
            AddPart(parts, text, flags);
            return parts;
        }
        size_t start = 0;
        while (true)
        {
            const size_t at = text.find(delimiter, start);
            if (at == std::string_view::npos)
            {
                AddPart(parts, text.substr(start), flags);
                break;
            }
            AddPart(parts, text.substr(start, at - start), flags);
            start = at + delimiter.size();
        }
        return parts;
    }

    std::vector<std::string_view> SplitAnyView(std::string_view text, std::string_view delimiters, SplitFlags flags)
    {
        std::vector<std::string_view> parts;
        size_t start = 0;
        for (size_t i = 0; i <= text.size(); i++)
            if (i == text.size() || delimiters.find(text[i]) != std::string_view::npos)
            {
                AddPart(parts, text.substr(start, i - start), flags);
                start = i + 1;
            }
        return parts;
    }

    std::vector<std::string> Split(std::string_view text, char delimiter, SplitFlags flags)
    {
        return ToStrings(SplitView(text, delimiter, flags));
    }

    std::vector<std::string> Split(std::string_view text, std::string_view delimiter, SplitFlags flags)
    {
        return ToStrings(SplitView(text, delimiter, flags));
    }

    std::vector<std::string_view> SplitLines(std::string_view text)
    {
        std::vector<std::string_view> lines;
        size_t start = 0, i = 0;
        while (i < text.size())
        {
            if (text[i] == '\n' || text[i] == '\r')
            {
                lines.push_back(text.substr(start, i - start));
                if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n')
                    i++;
                start = ++i;
            }
            else
                i++;
        }
        if (start < text.size())
            lines.push_back(text.substr(start));
        return lines;
    }

    std::vector<std::string_view> SplitWhitespace(std::string_view text)
    {
        std::vector<std::string_view> words;
        size_t i = 0;
        while (i < text.size())
        {
            while (i < text.size() && IsSpace(text[i]))
                i++;
            const size_t start = i;
            while (i < text.size() && !IsSpace(text[i]))
                i++;
            if (i > start)
                words.push_back(text.substr(start, i - start));
        }
        return words;
    }

    bool SplitOnce(std::string_view text, char delimiter, std::string_view& before, std::string_view& after)
    {
        const size_t at = text.find(delimiter);
        if (at == std::string_view::npos)
            return false;
        before = text.substr(0, at);
        after = text.substr(at + 1);
        return true;
    }

    // ---------------------------------------------------------------- replacing
    std::string Replace(std::string_view text, std::string_view from, std::string_view to)
    {
        std::string out(text);
        if (from.empty())
            return out;
        if (const size_t at = out.find(from); at != std::string::npos)
            out.replace(at, from.size(), to);
        return out;
    }

    std::string ReplaceAll(std::string_view text, std::string_view from, std::string_view to)
    {
        std::string out(text);
        ReplaceAllInPlace(out, from, to);
        return out;
    }

    size_t ReplaceAllInPlace(std::string& text, std::string_view from, std::string_view to)
    {
        if (from.empty())
            return 0;
        size_t count = 0;
        for (size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size()))
        {
            text.replace(at, from.size(), to);
            count++;
        }
        return count;
    }

    std::string Repeat(std::string_view text, size_t count)
    {
        std::string out;
        out.reserve(text.size() * count);
        for (size_t i = 0; i < count; i++)
            out += text;
        return out;
    }

    std::string PadLeft(std::string_view text, size_t width, char fill)
    {
        const size_t length = Utf8Length(text);
        return length >= width ? std::string(text) : std::string(width - length, fill) + std::string(text);
    }

    std::string PadRight(std::string_view text, size_t width, char fill)
    {
        const size_t length = Utf8Length(text);
        return length >= width ? std::string(text) : std::string(text) + std::string(width - length, fill);
    }

    std::string RemoveAll(std::string_view text, std::string_view characters)
    {
        std::string out;
        out.reserve(text.size());
        for (char c : text)
            if (characters.find(c) == std::string_view::npos)
                out += c;
        return out;
    }

    // ---------------------------------------------------------------- formatting
    std::string FormatBytes(uint64_t bytes)
    {
        if (bytes < 1024)
            return std::format("{} B", bytes);
        static const char* const s_Units[] = { "KB", "MB", "GB", "TB", "PB" };
        double value = (double)bytes / 1024.0;
        int unit = 0;
        while (value >= 1024.0 && unit < 4)
        {
            value /= 1024.0;
            unit++;
        }
        return value < 10.0 ? std::format("{:.1f} {}", value, s_Units[unit]) : std::format("{:.0f} {}", value, s_Units[unit]);
    }

    std::string FormatDuration(double seconds)
    {
        if (!std::isfinite(seconds))
            return seconds > 0.0 ? "cheksiz" : seconds < 0.0 ? "-cheksiz" : "?";
        if (seconds < 0.0)
            return "-" + FormatDuration(-seconds);
        const double ms = seconds * 1000.0;
        if (ms < 0.1)
            return std::format("{:.1f} \xC2\xB5s", ms * 1000.0);
        if (ms < 10.0)
            return std::format("{:.2f} ms", ms);
        if (ms < 999.5)
            return std::format("{:.0f} ms", ms);
        if (seconds < 59.95)
            return std::format("{:.1f} s", seconds);
        const uint64_t whole = (uint64_t)std::llround(seconds);
        if (whole < 3600)
            return std::format("{} daq {:02} s", whole / 60, whole % 60);
        const uint64_t minutes = (whole + 30) / 60;
        return std::format("{} soat {:02} daq", minutes / 60, minutes % 60);
    }

    std::string FormatThousands(int64_t value, char separator)
    {
        const bool negative = value < 0;
        uint64_t magnitude = negative ? 0ull - (uint64_t)value : (uint64_t)value;
        std::string digits = std::to_string(magnitude);
        std::string out;
        out.reserve(digits.size() + digits.size() / 3 + 1);
        if (negative)
            out += '-';
        for (size_t i = 0; i < digits.size(); i++)
        {
            if (i > 0 && (digits.size() - i) % 3 == 0)
                out += separator;
            out += digits[i];
        }
        return out;
    }

    std::string ToHex(uint64_t value, int minDigits)
    {
        std::string out = std::format("{:x}", value);
        if ((int)out.size() < minDigits)
            out.insert(0, (size_t)(minDigits - (int)out.size()), '0');
        return out;
    }

    std::optional<bool> ParseBool(std::string_view text)
    {
        text = Trim(text);
        for (std::string_view yes : { "true", "1", "yes", "on", "ha" })
            if (EqualsIgnoreCase(text, yes))
                return true;
        for (std::string_view no : { "false", "0", "no", "off", "yo'q", "yoq" })
            if (EqualsIgnoreCase(text, no))
                return false;
        return std::nullopt;
    }

    // ---------------------------------------------------------------- UTF-8
    uint32_t Utf8Decode(std::string_view text, size_t& i)
    {
        const unsigned char lead = (unsigned char)text[i];
        if (lead < 0x80)
        {
            i++;
            return lead;
        }
        size_t extra;
        uint32_t codepoint, smallest;
        if (lead >= 0xC2 && lead <= 0xDF)
        {
            extra = 1;
            codepoint = lead & 0x1Fu;
            smallest = 0x80;
        }
        else if (lead >= 0xE0 && lead <= 0xEF)
        {
            extra = 2;
            codepoint = lead & 0x0Fu;
            smallest = 0x800;
        }
        else if (lead >= 0xF0 && lead <= 0xF4)
        {
            extra = 3;
            codepoint = lead & 0x07u;
            smallest = 0x10000;
        }
        else
        {
            // A continuation byte without its lead, 0xC0 / 0xC1 (always overlong) or 0xF5 ... 0xFF.
            i++;
            return ReplacementCharacter;
        }
        if (i + extra >= text.size())
        {
            i++;
            return ReplacementCharacter; // cut off at the end
        }
        for (size_t k = 1; k <= extra; k++)
        {
            const char c = text[i + k];
            if (!IsUtf8Continuation(c))
            {
                i++;
                return ReplacementCharacter;
            }
            codepoint = (codepoint << 6) | ((unsigned char)c & 0x3Fu);
        }
        if (codepoint < smallest || codepoint > MaxCodepoint || (codepoint >= 0xD800 && codepoint <= 0xDFFF))
        {
            i++;
            return ReplacementCharacter;
        }
        i += extra + 1;
        return codepoint;
    }

    void Utf8Append(std::string& out, uint32_t cp)
    {
        if (cp > MaxCodepoint || (cp >= 0xD800 && cp <= 0xDFFF))
            cp = ReplacementCharacter; // not a character
        if (cp < 0x80)
            out += (char)cp;
        else if (cp < 0x800)
        {
            out += (char)(0xC0 | (cp >> 6));
            out += (char)(0x80 | (cp & 0x3F));
        }
        else if (cp < 0x10000)
        {
            out += (char)(0xE0 | (cp >> 12));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        }
        else
        {
            out += (char)(0xF0 | (cp >> 18));
            out += (char)(0x80 | ((cp >> 12) & 0x3F));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        }
    }

    std::string Utf8Encode(uint32_t codepoint)
    {
        std::string out;
        Utf8Append(out, codepoint);
        return out;
    }

    size_t Utf8EncodedSize(uint32_t cp)
    {
        if (cp > MaxCodepoint || (cp >= 0xD800 && cp <= 0xDFFF))
            return 3;
        return cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;
    }

    size_t Utf8Length(std::string_view text)
    {
        size_t count = 0, i = 0;
        while (i < text.size())
        {
            // ASCII runs without decoding.
            if ((unsigned char)text[i] < 0x80)
                i++;
            else
                Utf8Decode(text, i);
            count++;
        }
        return count;
    }

    bool Utf8IsValid(std::string_view text)
    {
        size_t i = 0;
        while (i < text.size())
        {
            const size_t before = i;
            if (Utf8Decode(text, i) == ReplacementCharacter)
            {
                // Valid only when it really was an encoded U+FFFD (EF BF BD).
                if (i - before != 3)
                    return false;
            }
        }
        return true;
    }

    std::string Utf8Sanitize(std::string_view text)
    {
        std::string out;
        out.reserve(text.size());
        size_t i = 0;
        while (i < text.size())
        {
            const size_t before = i;
            const uint32_t codepoint = Utf8Decode(text, i);
            if (codepoint == ReplacementCharacter && i - before != 3)
                Utf8Append(out, ReplacementCharacter);
            else
                out.append(text.substr(before, i - before));
        }
        return out;
    }

    std::string_view Utf8Truncate(std::string_view text, size_t maxCodepoints)
    {
        return text.substr(0, Utf8ByteOffset(text, maxCodepoints));
    }

    size_t Utf8ByteOffset(std::string_view text, size_t index)
    {
        size_t i = 0;
        for (size_t n = 0; n < index && i < text.size(); n++)
            Utf8Decode(text, i);
        return i;
    }

    std::u32string Utf8ToUtf32(std::string_view text)
    {
        std::u32string out;
        out.reserve(text.size());
        size_t i = 0;
        while (i < text.size())
            out += (char32_t)Utf8Decode(text, i);
        return out;
    }

    std::string Utf32ToUtf8(std::u32string_view text)
    {
        std::string out;
        out.reserve(text.size());
        for (char32_t c : text)
            Utf8Append(out, (uint32_t)c);
        return out;
    }

}
