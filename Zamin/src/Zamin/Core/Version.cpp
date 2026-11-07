#include "Zamin/Core/Version.h"

#include <algorithm>
#include <charconv>
#include <format>
#include <vector>

namespace Zamin {

    namespace {

        bool IsDigit(char c) { return c >= '0' && c <= '9'; }
        bool IsIdentifierChar(char c) { return IsDigit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-'; }

        // A version number part: digits, no leading zero (except "0"), fits an int.
        bool ParseNumber(std::string_view text, int& out)
        {
            if (text.empty() || (text.size() > 1 && text[0] == '0'))
                return false;
            for (char c : text)
                if (!IsDigit(c))
                    return false;
            auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), out);
            return ec == std::errc() && ptr == text.data() + text.size();
        }

        std::vector<std::string_view> Split(std::string_view text, char separator)
        {
            std::vector<std::string_view> parts;
            size_t start = 0;
            while (true)
            {
                const size_t end = text.find(separator, start);
                parts.push_back(text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
                if (end == std::string_view::npos)
                    return parts;
                start = end + 1;
            }
        }

        bool IsNumeric(std::string_view identifier)
        {
            for (char c : identifier)
                if (!IsDigit(c))
                    return false;
            return !identifier.empty();
        }

        // Dot-separated identifiers of [0-9A-Za-z-]; numeric ones without leading zeros (pre-release only).
        bool ValidIdentifiers(std::string_view text, bool numericLeadingZeroAllowed)
        {
            if (text.empty())
                return false;
            for (std::string_view id : Split(text, '.'))
            {
                if (id.empty())
                    return false;
                for (char c : id)
                    if (!IsIdentifierChar(c))
                        return false;
                if (!numericLeadingZeroAllowed && IsNumeric(id) && id.size() > 1 && id[0] == '0')
                    return false;
            }
            return true;
        }

        int CompareIdentifiers(std::string_view a, std::string_view b)
        {
            const bool numA = IsNumeric(a), numB = IsNumeric(b);
            if (numA && numB)
            {
                // Same length without leading zeros: compare as text (no overflow on long numbers).
                if (a.size() != b.size())
                    return a.size() < b.size() ? -1 : 1;
                return a.compare(b) < 0 ? -1 : a.compare(b) > 0 ? 1 : 0;
            }
            if (numA != numB)
                return numA ? -1 : 1; // numeric identifiers are lower than alphanumeric ones
            const int c = a.compare(b);
            return c < 0 ? -1 : c > 0 ? 1 : 0;
        }

        std::string_view Trim(std::string_view s)
        {
            while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
                s.remove_prefix(1);
            while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
                s.remove_suffix(1);
            return s;
        }

    }

    std::optional<SemanticVersion> SemanticVersion::Parse(std::string_view text)
    {
        SemanticVersion v;
        // Build metadata after the first '+', then the pre-release after the first '-' of the rest.
        if (const size_t plus = text.find('+'); plus != std::string_view::npos)
        {
            const std::string_view build = text.substr(plus + 1);
            if (!ValidIdentifiers(build, true))
                return std::nullopt;
            v.Build = std::string(build);
            text = text.substr(0, plus);
        }
        if (const size_t dash = text.find('-'); dash != std::string_view::npos)
        {
            const std::string_view pre = text.substr(dash + 1);
            if (!ValidIdentifiers(pre, false))
                return std::nullopt;
            v.PreRelease = std::string(pre);
            text = text.substr(0, dash);
        }
        const std::vector<std::string_view> numbers = Split(text, '.');
        if (numbers.size() != 3 || !ParseNumber(numbers[0], v.Major) || !ParseNumber(numbers[1], v.Minor) || !ParseNumber(numbers[2], v.Patch))
            return std::nullopt;
        return v;
    }

    std::optional<SemanticVersion> SemanticVersion::ParseLenient(std::string_view text)
    {
        text = Trim(text);
        if (!text.empty() && (text.front() == 'v' || text.front() == 'V'))
            text.remove_prefix(1);
        if (auto strict = Parse(text))
            return strict;
        // Missing minor / patch: "1" -> "1.0.0", "1.2-beta" -> "1.2.0-beta".
        const size_t suffix = text.find_first_of("-+");
        std::string core(text.substr(0, suffix));
        const std::string rest(suffix == std::string_view::npos ? std::string_view() : text.substr(suffix));
        const size_t dots = (size_t)std::count(core.begin(), core.end(), '.');
        if (dots >= 2 || core.empty())
            return std::nullopt;
        for (size_t i = dots; i < 2; i++)
            core += ".0";
        return Parse(core + rest);
    }

    std::string SemanticVersion::ToString() const
    {
        std::string text = std::format("{}.{}.{}", Major, Minor, Patch);
        if (!PreRelease.empty())
            text += "-" + PreRelease;
        if (!Build.empty())
            text += "+" + Build;
        return text;
    }

    SemanticVersion SemanticVersion::Bumped(Part part) const
    {
        SemanticVersion next;
        next.Major = Major;
        next.Minor = Minor;
        next.Patch = Patch;
        switch (part)
        {
            case Part::Major: next.Major++; next.Minor = 0; next.Patch = 0; break;
            case Part::Minor: next.Minor++; next.Patch = 0; break;
            case Part::Patch: next.Patch++; break;
        }
        return next;
    }

    int SemanticVersion::Compare(const SemanticVersion& other) const
    {
        if (Major != other.Major) return Major < other.Major ? -1 : 1;
        if (Minor != other.Minor) return Minor < other.Minor ? -1 : 1;
        if (Patch != other.Patch) return Patch < other.Patch ? -1 : 1;
        if (PreRelease == other.PreRelease) return 0;
        // A release is higher than any of its pre-releases.
        if (PreRelease.empty()) return 1;
        if (other.PreRelease.empty()) return -1;
        const std::vector<std::string_view> a = Split(PreRelease, '.'), b = Split(other.PreRelease, '.');
        for (size_t i = 0; i < a.size() && i < b.size(); i++)
            if (const int c = CompareIdentifiers(a[i], b[i]); c != 0)
                return c;
        return a.size() == b.size() ? 0 : a.size() < b.size() ? -1 : 1;
    }

}
