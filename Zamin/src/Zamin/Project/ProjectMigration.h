#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace Zamin {

    // Project files (.zproject) say which version of the format they were written in (the top-level
    // "Version"). An older file is brought up to the current version when it is read - a chain of
    // migrations, one step per version, each over the parsed file - before any setting is taken from
    // it. The file on disk stays as it is until the project is saved (then in the current version).
    // A file of a newer version is read as far as it is understood; what this engine does not know of
    // it (and of any file: sections of tools / plugins, keys of a newer engine) is kept and written back
    // when the project is saved (ProjectConfig::ExtraSections).
    //
    // The versions:
    //   0 - flat (written by hand / the first tools): "Name", "StartScene" ... at the top, no "Project".
    //   1 - the original format: the "Project" object, no "Version"; build scenes may be plain paths.
    //   2 - "Version"; build scenes always { "Path", "Enabled" [, "UUID"] }; the "Player" section
    //       (window, icon, company / product / version); sections in a fixed order.
    struct ProjectMigrationStep
    {
        int From = 0, To = 0;
        std::string Description; // what the step changes (shown in the log)
        size_t Changes = 0;      // in a ProjectMigrationResult: the places it changed in the file
    };

    struct ProjectMigrationResult
    {
        int FileVersion = 0;     // as written (the version the keys say)
        int Version = 0;         // after the migration: the current one (a newer file keeps its own)
        std::vector<ProjectMigrationStep> Steps; // applied, oldest first

        bool Migrated() const { return !Steps.empty(); }
        bool IsNewer() const;
        size_t GetChangeCount() const;
        // One line: "v1 -> v2 (1 qadam, 3 ta o'zgarish)"; "" when nothing was migrated.
        std::string Summary() const;
    };

    class ProjectMigration
    {
    public:
        // The version project files are written in.
        static constexpr int CurrentVersion = 2;

        // Every step this engine knows, oldest first (From 0 .. CurrentVersion - 1, To = From + 1).
        static const std::vector<ProjectMigrationStep>& GetChain();
        // The version of project file text: its "Version", else 1 with a "Project" object, else 0 for a
        // flat object with a "Name"; -1 when the text is not a project file.
        static int ReadVersion(const std::string& text);
        // Brings project file text to the current version (keys keep their order; new ones go where
        // the current format puts them). False, with the text untouched and the reason in `error`, when
        // it is not a project file. Text already in the current (or a newer) version is left as it is.
        static bool MigrateText(std::string& text, ProjectMigrationResult* result = nullptr, std::string* error = nullptr);
    };

}
