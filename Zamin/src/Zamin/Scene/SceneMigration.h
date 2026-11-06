#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace Zamin {

    // Scene files (.zscene) say which version of the format they were written in ("Version"). A file
    // of an older version is brought up to the current one when it is loaded: a chain of migrations,
    // one step per version (v0 -> v1 -> v2 ...), each a function over the parsed file, runs before
    // anything is read from it - the scene in memory is what the current format would have given.
    // The file itself stays as it is until the scene is saved (then it is written in the current
    // version). What was migrated is logged once per file version (SceneMigration::LogMigration);
    // a file of a newer version than this engine knows is read as far as it is understood, with a
    // warning in the load report.
    //
    // The versions:
    //   0 - no "Version" key (written by hand or by a tool): entity IDs may be strings, the name may
    //       be under "Name".
    //   1 - the original format: Text "Centered" (before alignment), UITransform "Anchor" (one of the
    //       9 anchor points, before AnchorMin / AnchorMax / Pivot).
    //   2 - scene metadata ("Metadata": description, author, tags); Text alignment and UI anchors in
    //       their current keys only.
    struct SceneMigrationStep
    {
        int From = 0, To = 0;
        std::string Description;   // what the step changes (shown in the log)
        size_t Changes = 0;        // in a SceneMigrationResult: the places it changed in the file
    };

    struct SceneMigrationResult
    {
        int FileVersion = 0;       // as written (0: no "Version" key)
        int Version = 0;           // after the migration: the current one (a newer file keeps its own)
        std::vector<SceneMigrationStep> Steps; // applied, oldest first

        bool Migrated() const { return !Steps.empty(); }
        // Written by a newer engine: read as far as this one understands it.
        bool IsNewer() const;
        size_t GetChangeCount() const;
        // One line: "v1 -> v2 (1 qadam, 3 ta o'zgarish)"; "" when nothing was migrated.
        std::string Summary() const;
    };

    class SceneMigration
    {
    public:
        // The version scene files are written in.
        static constexpr int CurrentVersion = 2;

        // Every step this engine knows, oldest first: From 0 .. CurrentVersion - 1, each To = From + 1.
        static const std::vector<SceneMigrationStep>& GetChain();
        // The "Version" of scene file text: 0 when it has none, -1 when the text is not a scene.
        static int ReadVersion(const std::string& text);
        // Brings scene file text to the current version (tools, tests: what loading does, as text).
        // False, with the text untouched and the reason in `error`, when it is not a scene file.
        // A file already in the current version (or a newer one) is left as it is.
        static bool MigrateText(std::string& text, SceneMigrationResult* result = nullptr, std::string* error = nullptr);
        // Logs a migration of `file` (info: the steps and their changes) - once per file version: the
        // editor reads a scene file again and again (thumbnails, the scenes panel, play mode).
        // Returns whether it was logged now.
        static bool LogMigration(const std::string& file, const SceneMigrationResult& result);
        // Forgets which files were logged (tests).
        static void ResetLog();
    };

}
