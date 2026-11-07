#pragma once

// Text helpers used all over the engine, the editor and game scripts: trimming, case (ASCII),
// splitting / joining, replacing, safe number parsing and formatting, and UTF-8 (decoding,
// encoding, lengths, iterating code points).
//
// Case: ToLower / ToUpper / the *IgnoreCase functions only change the ASCII letters A-Z / a-z.
// Bytes of UTF-8 sequences (>= 0x80) stay as they are, so UTF-8 text is never broken. Uzbek Latin
// is written with ASCII letters plus the apostrophes (' or U+02BB / U+02BC), which have no case, so
// it is handled fully; Cyrillic or accented letters keep their case.
//
// Numbers: ParseInt / ParseFloat read the whole text (surrounding whitespace allowed), never throw,
// do not depend on the C locale and report out-of-range values as failures.

#include <charconv>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <format>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

namespace Zamin::StringUtils {

    // ---------------------------------------------------------------- characters (ASCII)
    // Space, tab, newline, vertical tab, form feed, carriage return (std::isspace in the "C" locale).
    constexpr bool IsSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }
    constexpr bool IsDigit(char c) { return c >= '0' && c <= '9'; }
    constexpr bool IsAlpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
    constexpr bool IsAlnum(char c) { return IsAlpha(c) || IsDigit(c); }
    constexpr char ToLower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c; }
    constexpr char ToUpper(char c) { return c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : c; }

    // ---------------------------------------------------------------- trimming
    std::string_view TrimStart(std::string_view text);
    std::string_view TrimEnd(std::string_view text);
    std::string_view Trim(std::string_view text);
    // Only the given characters (e.g. Trim(path, "/"), Trim(word, "\"'")).
    std::string_view Trim(std::string_view text, std::string_view characters);

    // ---------------------------------------------------------------- case (ASCII, see above)
    std::string ToLower(std::string_view text);
    std::string ToUpper(std::string_view text);
    void ToLowerInPlace(std::string& text);
    void ToUpperInPlace(std::string& text);
    bool EqualsIgnoreCase(std::string_view a, std::string_view b);
    // < 0, 0, > 0 like strcmp, letters compared without their case.
    int CompareIgnoreCase(std::string_view a, std::string_view b);

    // ---------------------------------------------------------------- searching
    inline bool StartsWith(std::string_view text, std::string_view prefix) { return text.starts_with(prefix); }
    inline bool EndsWith(std::string_view text, std::string_view suffix) { return text.ends_with(suffix); }
    inline bool Contains(std::string_view text, std::string_view part) { return text.find(part) != std::string_view::npos; }
    inline bool Contains(std::string_view text, char c) { return text.find(c) != std::string_view::npos; }
    bool StartsWithIgnoreCase(std::string_view text, std::string_view prefix);
    bool EndsWithIgnoreCase(std::string_view text, std::string_view suffix);
    // The byte offset of the first `part` in `text` from `from` on, letters without their case (npos: none).
    size_t FindIgnoreCase(std::string_view text, std::string_view part, size_t from = 0);
    inline bool ContainsIgnoreCase(std::string_view text, std::string_view part) { return FindIgnoreCase(text, part) != std::string_view::npos; }
    // How many times `part` occurs (not overlapping; 0 for an empty part).
    size_t Count(std::string_view text, std::string_view part);

    // ---------------------------------------------------------------- splitting
    enum class SplitFlags : uint8_t
    {
        None = 0,
        SkipEmpty = 1 << 0,   // no empty parts ("a,,b" -> "a", "b")
        Trim = 1 << 1,        // whitespace trimmed off every part (before SkipEmpty looks at it)
    };
    constexpr SplitFlags operator|(SplitFlags a, SplitFlags b) { return (SplitFlags)((uint8_t)a | (uint8_t)b); }
    constexpr bool HasFlag(SplitFlags flags, SplitFlags flag) { return ((uint8_t)flags & (uint8_t)flag) != 0; }

    // The parts between the delimiters ("a,b," -> "a", "b", ""; "" -> one empty part unless SkipEmpty).
    // The views point into `text`.
    std::vector<std::string_view> SplitView(std::string_view text, char delimiter, SplitFlags flags = SplitFlags::None);
    // A delimiter of several characters ("a::b" with "::"); an empty delimiter gives the whole text.
    std::vector<std::string_view> SplitView(std::string_view text, std::string_view delimiter, SplitFlags flags = SplitFlags::None);
    // Split at any of `delimiters` ("a, b;c" with ",;").
    std::vector<std::string_view> SplitAnyView(std::string_view text, std::string_view delimiters, SplitFlags flags = SplitFlags::None);
    std::vector<std::string> Split(std::string_view text, char delimiter, SplitFlags flags = SplitFlags::None);
    std::vector<std::string> Split(std::string_view text, std::string_view delimiter, SplitFlags flags = SplitFlags::None);
    // The lines of a text: "\n", "\r\n" and "\r" end a line; a newline at the very end adds no empty line.
    std::vector<std::string_view> SplitLines(std::string_view text);
    // The words between runs of whitespace (never empty parts).
    std::vector<std::string_view> SplitWhitespace(std::string_view text);
    // Before / after the first `delimiter` (false and nothing changed when there is none).
    bool SplitOnce(std::string_view text, char delimiter, std::string_view& before, std::string_view& after);

    // ---------------------------------------------------------------- joining
    // Any range of things a std::string can be appended from (std::string, std::string_view, const char*).
    template<typename Range>
    std::string Join(const Range& parts, std::string_view separator)
    {
        std::string out;
        bool first = true;
        for (const auto& part : parts)
        {
            if (!first)
                out += separator;
            out += part;
            first = false;
        }
        return out;
    }
    inline std::string Join(std::initializer_list<std::string_view> parts, std::string_view separator)
    {
        return Join<std::initializer_list<std::string_view>>(parts, separator);
    }

    // ---------------------------------------------------------------- replacing
    // The first `from` replaced (the text as it is when there is none or `from` is empty).
    std::string Replace(std::string_view text, std::string_view from, std::string_view to);
    std::string ReplaceAll(std::string_view text, std::string_view from, std::string_view to);
    // In place; how many were replaced.
    size_t ReplaceAllInPlace(std::string& text, std::string_view from, std::string_view to);
    std::string Repeat(std::string_view text, size_t count);
    // Filled up to `width` code points (UTF-8 aware; longer text stays as it is).
    std::string PadLeft(std::string_view text, size_t width, char fill = ' ');
    std::string PadRight(std::string_view text, size_t width, char fill = ' ');
    // Without these characters anywhere ("a-b-c" without "-" -> "abc").
    std::string RemoveAll(std::string_view text, std::string_view characters);

    // ---------------------------------------------------------------- formatting
    // std::format (the format string checked when compiling).
    template<typename... Args>
    std::string Format(std::format_string<Args...> format, Args&&... args)
    {
        return std::format(format, std::forward<Args>(args)...);
    }
    // A format string known only at run time (a translated text, a setting): a broken one ("{" without
    // its "}", more fields than values) gives the format text as it is instead of throwing.
    template<typename... Args>
    std::string FormatRuntime(std::string_view format, const Args&... args)
    {
        try
        {
            return std::vformat(format, std::make_format_args(args...));
        }
        catch (const std::format_error&)
        {
            return std::string(format);
        }
    }
    // "512 B", "1.5 KB", "300 KB", "5.3 MB", "2.0 GB" (1024 per step; one decimal below 10).
    std::string FormatBytes(uint64_t bytes);
    // "42.0 µs", "0.42 ms", "850 ms", "12.4 s", "3 daq 05 s", "1 soat 02 daq" (negative: "-" before it).
    std::string FormatDuration(double seconds);
    // Digits in groups of three: 1234567 -> "1 234 567" (the separator can be changed: ',', '\'').
    std::string FormatThousands(int64_t value, char separator = ' ');
    // Lower-case hex digits, at least `minDigits` (zeros in front): ToHex(255) = "ff", ToHex(255, 4) = "00ff".
    std::string ToHex(uint64_t value, int minDigits = 0);

    // ---------------------------------------------------------------- numbers
    // An integer that is all of `text` (whitespace around it allowed, a '+' or (signed types) '-' in
    // front; base 16 also takes "0x"). Nothing when it is not a number or does not fit in T.
    template<std::integral T>
    std::optional<T> ParseInt(std::string_view text, int base = 10)
    {
        text = Trim(text);
        // One sign at most ('-' only for signed types).
        bool negative = false;
        if (!text.empty() && (text.front() == '+' || (std::is_signed_v<T> && text.front() == '-')))
        {
            negative = text.front() == '-';
            text.remove_prefix(1);
        }
        if (base == 16 && text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
            text.remove_prefix(2);
        if (text.empty() || text.front() == '-' || text.front() == '+')
            return std::nullopt;
        // Read without the sign (as unsigned) so that the most negative value fits too.
        using U = std::make_unsigned_t<T>;
        U magnitude = 0;
        const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), magnitude, base);
        if (ec != std::errc() || end != text.data() + text.size())
            return std::nullopt;
        if constexpr (std::is_signed_v<T>)
        {
            const U limit = negative ? (U)std::numeric_limits<T>::max() + 1u : (U)std::numeric_limits<T>::max();
            if (magnitude > limit)
                return std::nullopt;
            return negative ? (T)(0 - magnitude) : (T)magnitude;
        }
        else
            return (T)magnitude;
    }
    // A decimal number that is all of `text` ("1.5", "-2", "1e3", ".5", "inf"; '.' is the decimal
    // point whatever the locale). Nothing when it is not one or it is out of the type's range.
    template<std::floating_point T>
    std::optional<T> ParseFloat(std::string_view text)
    {
        text = Trim(text);
        if (!text.empty() && text.front() == '+')
        {
            text.remove_prefix(1);
            if (!text.empty() && text.front() == '-')
                return std::nullopt; // "+-1"
        }
        if (text.empty() || text.front() == '+')
            return std::nullopt;
        T value{};
        const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (ec != std::errc() || end != text.data() + text.size())
            return std::nullopt;
        return value;
    }
    // true / false, 1 / 0, yes / no, on / off, ha / yo'q (any case).
    std::optional<bool> ParseBool(std::string_view text);
    // The shortest text that reads back as the same value ("0.1", "1e+20", "3"); integers as they are.
    template<typename T>
        requires std::is_arithmetic_v<T>
    std::string ToString(T value)
    {
        if constexpr (std::is_same_v<T, bool>)
            return value ? "true" : "false";
        else
        {
            char buffer[64];
            const auto [end, ec] = std::to_chars(buffer, buffer + sizeof(buffer), value);
            return ec == std::errc() ? std::string(buffer, end) : std::string();
        }
    }

    // ---------------------------------------------------------------- UTF-8
    constexpr uint32_t ReplacementCharacter = 0xFFFD;
    constexpr uint32_t MaxCodepoint = 0x10FFFF;
    constexpr bool IsUtf8Continuation(char c) { return ((unsigned char)c & 0xC0) == 0x80; }

    // The code point at byte `i` of `text`; `i` moves past it. Invalid UTF-8 (a stray continuation
    // byte, a truncated or overlong sequence, a surrogate, a value past U+10FFFF) gives U+FFFD and
    // moves one byte, so decoding always goes on. `i` must be < text.size().
    uint32_t Utf8Decode(std::string_view text, size_t& i);
    // A code point appended as UTF-8 (surrogates and values past U+10FFFF become U+FFFD).
    void Utf8Append(std::string& out, uint32_t codepoint);
    std::string Utf8Encode(uint32_t codepoint);
    // How many bytes the code point takes in UTF-8 (1 ... 4; 3 for invalid ones: U+FFFD).
    size_t Utf8EncodedSize(uint32_t codepoint);
    // Code points (an invalid byte counts as one: U+FFFD).
    size_t Utf8Length(std::string_view text);
    bool Utf8IsValid(std::string_view text);
    // Every invalid byte replaced by U+FFFD (valid text comes back the same).
    std::string Utf8Sanitize(std::string_view text);
    // At most `maxCodepoints` code points, never cutting one in two.
    std::string_view Utf8Truncate(std::string_view text, size_t maxCodepoints);
    // The byte offset of the code point number `index` (text.size() past the end).
    size_t Utf8ByteOffset(std::string_view text, size_t index);
    std::u32string Utf8ToUtf32(std::string_view text);
    std::string Utf32ToUtf8(std::u32string_view text);

    // `fn(codepoint, byteOffset)` for every code point, in order.
    template<typename Fn>
    void Utf8ForEach(std::string_view text, Fn&& fn)
    {
        size_t i = 0;
        while (i < text.size())
        {
            const size_t offset = i;
            const uint32_t codepoint = Utf8Decode(text, i);
            fn(codepoint, offset);
        }
    }

    // for (uint32_t c : Utf8View(text)) ... - the code points of a UTF-8 text (as Utf8Decode reads them).
    class Utf8View
    {
    public:
        class Iterator
        {
        public:
            using iterator_category = std::forward_iterator_tag;
            using value_type = uint32_t;
            using difference_type = std::ptrdiff_t;
            using pointer = void;
            using reference = uint32_t;

            Iterator() = default;
            Iterator(std::string_view text, size_t offset) : m_Text(text), m_Offset(offset) { Read(); }
            uint32_t operator*() const { return m_Codepoint; }
            // Where the current code point starts in the text.
            size_t Offset() const { return m_Offset; }
            Iterator& operator++()
            {
                m_Offset = m_Next;
                Read();
                return *this;
            }
            Iterator operator++(int)
            {
                Iterator copy = *this;
                ++*this;
                return copy;
            }
            bool operator==(const Iterator& other) const { return m_Offset == other.m_Offset; }

        private:
            void Read()
            {
                m_Next = m_Offset;
                m_Codepoint = m_Offset < m_Text.size() ? Utf8Decode(m_Text, m_Next) : 0;
            }
            std::string_view m_Text;
            size_t m_Offset = 0, m_Next = 0;
            uint32_t m_Codepoint = 0;
        };

        explicit Utf8View(std::string_view text) : m_Text(text) {}
        Iterator begin() const { return Iterator(m_Text, 0); }
        Iterator end() const { return Iterator(m_Text, m_Text.size()); }

    private:
        std::string_view m_Text;
    };

}
