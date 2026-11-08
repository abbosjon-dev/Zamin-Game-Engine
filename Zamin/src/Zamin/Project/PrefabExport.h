#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace Zamin {

    class AssetReferenceIndex;
    class PrefabDatabase;
    struct ExportAssetPlan;

    // One thing the export's prefab check found.
    struct PrefabExportIssue
    {
        enum class Severity : uint8_t { Info, Warning, Error };
        Severity Level = Severity::Warning;
        std::string Rule;      // "R1" ... "R10" (docs/Prefabs.md)
        std::string File;      // the scene / prefab / source file it is about (asset-relative; code: project-relative)
        std::string Message;   // Uzbek, for the report
    };

    struct PrefabExportResult
    {
        std::vector<PrefabExportIssue> Issues;
        // What the export writes instead of copying a scene / prefab file (asset-relative path -> text):
        // the prefab links (editor-only data) taken out, stale instances brought up to date. The source
        // files are never changed.
        std::map<std::string, std::string> Baked;
        size_t PrefabsChecked = 0;
        size_t Updated = 0;    // files whose instances were older than their prefabs (brought up to date in the copy)
        size_t Stripped = 0;   // files written without their prefab links

        size_t Count(PrefabExportIssue::Severity level) const;
        // Errors (and with `warningsAsErrors` warnings too) that should fail the export.
        bool HasErrors(bool warningsAsErrors = false) const;
        // "3 ta prefab tekshirildi: 0 xato, 1 ogohlantirish".
        std::string Summary() const;
    };

    // The prefab part of an export (the editor's Build and "Prefab'larni tekshirish"). Main thread
    // only, before the export thread starts: it reads the prefabs through the editor's PrefabDatabase.
    // Rules (docs/Prefabs.md):
    //   R1  a prefab the game names is missing (a scene's / prefab's script field, a C++ string literal:
    //       Instantiate would give nothing)                                                    error
    //   R2  a prefab does not parse / has no entities (partly unreadable: warning)              error
    //   R3  a prefab of a newer format than this engine                                         error
    //   R4  IDs missing / repeated, several roots, parent loops / parents outside it            warning
    //   R5  unknown component keys                                                              warning
    //   R6  references to entities outside the prefab (EntityRef, joints named)                 warning
    //   R7  assets a prefab names that are missing                                              warning
    //   R8  a nested / variant / scene instance's prefab is missing (its baked data still goes)  warning
    //   R9  prefabs nested in each other in a cycle                                             error
    //   R10 overrides of parts the prefab no longer has; older instances that could not be linked warning
    class PrefabExport
    {
    public:
        // `plan`: the files the export takes (asset-relative). `index`: the reference index the plan was
        // made from (C++ literals naming missing prefabs); nullptr - not looked at.
        static PrefabExportResult Prepare(const ExportAssetPlan& plan, PrefabDatabase& db, const AssetReferenceIndex* index = nullptr);
        // Takes the editor's prefab link data out of a scene / prefab file's text ("Editor": "Prefab",
        // "PrefabUUID", "PrefabEntity", "PrefabHash", "Overrides", "RemovedComponents", "RemovedEntities";
        // an "Editor" left empty goes too). False (text unchanged) when it has none or is not JSON.
        static bool StripLinks(std::string& text);
    };

}
