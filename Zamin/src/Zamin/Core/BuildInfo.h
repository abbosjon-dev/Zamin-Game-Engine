#pragma once

#include <string>

namespace Zamin {

    struct ProjectConfig;

    // What this build of the engine is: its version, the git commit it was built from (and whether the tree
    // had changes then), the dates, the build type and the compiler (cmake/GenerateBuildInfo.cmake writes
    // them at build time). ZaminPlayer --version prints them with the game's own version; the logs and
    // crash reports start with them; scripts can show them (a settings menu's corner).
    struct BuildInfo
    {
        std::string EngineVersion; // "0.1.0" (ZM_ENGINE_VERSION)
        std::string Commit;        // "fe60e8ff11" (10 hex digits); "" when not built from a git checkout
        bool Dirty = false;        // tracked files had uncommitted changes
        std::string CommitDate;    // "2026-11-07" (the commit's date; "" without git)
        std::string BuildDate;     // "2026-11-07" (the day it was built, UTC; SOURCE_DATE_EPOCH sets it)
        std::string BuildType;     // "Debug", "Release", "RelWithDebInfo" ...
        std::string Compiler;      // "GCC 13.2.0", "Clang 18.1.3"
        bool Shipping = false;     // a Shipping build (ZAMIN_SHIPPING: no development aids)

        // This build of the engine.
        static const BuildInfo& Get();

        // The commit with a mark for changes: "fe60e8ff11", "fe60e8ff11-dirty"; "" when unknown.
        std::string GetCommitText() const;
        // One line (the logs, crash reports): "Zamin Engine 0.1.0 (kommit fe60e8ff11, build 2026-11-07, Debug, GCC 13.2.0)".
        std::string Describe() const;
        // A short tag for a corner of the screen: "0.1.0 (fe60e8f)" / "0.1.0 (fe60e8f*)" / "0.1.0".
        std::string GetShortVersion() const;
    };

    // The game's and the engine's versions together: what ZaminPlayer --version prints, what the logs start
    // with and what a settings menu shows in its corner.
    struct VersionInfo
    {
        std::string Product; // the game's name (Project settings > Player > Product, else the project's name)
        std::string Company; // "" when not set
        std::string Version; // the game's version ("1.0.0")
        bool HasGame = false; // false: no project (only the engine is described)
        BuildInfo Engine = BuildInfo::Get();

        // The active project's game (none: only the engine).
        static VersionInfo ForActiveProject();
        static VersionInfo ForConfig(const ProjectConfig& config);

        // ZaminPlayer --version (lines, the last one ends with '\n'):
        //   Zamin Platformer 0.4.0 - Zamin Studio
        //   Zamin Engine 0.1.0
        //     Kommit:  fe60e8ff11 (2026-11-07)
        //     Build:   2026-11-07, Debug, GCC 13.2.0
        std::string Format(const std::string& program = "Zamin Player") const;
        // One line for a settings menu's corner: "Zamin Platformer v0.4.0  |  Zamin Engine 0.1.0 (fe60e8f)".
        std::string FormatShort() const;
    };

}
