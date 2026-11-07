#pragma once

#include <compare>
#include <optional>
#include <string>
#include <string_view>

// The engine's own version (Application::GetEngineVersion, written into .zproject files).
#define ZM_ENGINE_VERSION "0.1.0"

namespace Zamin {

    // A semantic version (https://semver.org): MAJOR.MINOR.PATCH, optionally with a pre-release
    // ("-beta.2") and build metadata ("+20261107"). The game's version (Project settings > Player) is
    // one; Application::GetVersion returns it as text.
    struct SemanticVersion
    {
        int Major = 0, Minor = 0, Patch = 0;
        std::string PreRelease; // "" or dot-separated identifiers: "alpha", "rc.1"
        std::string Build;      // "" or dot-separated identifiers (ignored when comparing)

        enum class Part { Major = 0, Minor, Patch };

        // Strict MAJOR.MINOR.PATCH[-pre][+build]: no leading zeros, identifiers [0-9A-Za-z-].
        // std::nullopt for anything else ("1.2", "v1.0.0", "1.0.0-", " 1.0.0").
        static std::optional<SemanticVersion> Parse(std::string_view text);
        static bool IsValid(std::string_view text) { return Parse(text).has_value(); }
        // Like Parse, but also takes what people often type: a leading 'v', missing parts ("1.2" ->
        // 1.2.0, "2" -> 2.0.0) and surrounding spaces.
        static std::optional<SemanticVersion> ParseLenient(std::string_view text);

        std::string ToString() const;
        // The next version: Major -> (M+1).0.0, Minor -> M.(m+1).0, Patch -> M.m.(p+1); the pre-release
        // and build parts are dropped.
        SemanticVersion Bumped(Part part) const;

        // Precedence by semver: numbers first, a pre-release is lower than the release, build metadata
        // is ignored. <0, 0, >0.
        int Compare(const SemanticVersion& other) const;
        bool operator==(const SemanticVersion& other) const { return Compare(other) == 0; }
        std::strong_ordering operator<=>(const SemanticVersion& other) const { return Compare(other) <=> 0; }
    };

}
