#pragma once

#include "Zamin/Scene/Scene.h"
#include "Zamin/Scene/SceneLoadTiming.h"
#include "Zamin/Scene/SceneMigration.h"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Zamin {

    // Scenes are stored as human-readable JSON (*.zscene).
    class Entity;
    struct TransformComponent;

    // A problem found while loading a scene file. Fatal ones mean nothing was loaded (the file is
    // missing / unreadable, its JSON is broken, it is not a scene); with the others the scene is
    // loaded without what was wrong: an entry that is not an entity, a component that cannot be read
    // (the entity keeps its other components), an unknown component (from a newer engine or a typo -
    // left out, and lost when the scene is saved again), a missing asset file, a duplicate entity ID
    // (the second one gets a new ID), a parent that is not in the file or a parent loop (the entity
    // becomes a root).
    struct SceneLoadIssue
    {
        enum class Kind
        {
            FileMissing = 0, Unreadable, ParseError, NotAScene,
            BadEntity, BadComponent, UnknownComponent, MissingAsset, DuplicateID, BadHierarchy,
            NewerVersion   // written by a newer engine (SceneMigration): read as far as it is understood
        };
        enum class Severity { Warning = 0, Error, Fatal };

        Kind Type = Kind::Unreadable;
        Severity Level = Severity::Error;
        std::string Message;         // in words, with its context (the entity, the component, the asset)
        std::string Entity;          // the entity's name ("" for the file / the scene settings)
        uint64_t EntityID = 0;
        std::string Component;       // the component's key in the file ("SpriteRenderer"; "Settings")
        std::string Asset;           // MissingAsset: the path (relative to the assets)
        int Line = 0, Column = 0;    // ParseError: where in the file (1-based)
        std::string SourceLine;      // ParseError: that line of the file

        static const char* KindName(Kind kind);
    };

    // Everything a scene load found (SceneSerializer::GetLoadReport, SceneLoadJob::GetReport,
    // SceneManager::TakeLoadReports). Empty after a clean load.
    struct SceneLoadReport
    {
        std::string File;                    // the file as it was asked for (or "" for text)
        std::vector<SceneLoadIssue> Issues;
        size_t EntitiesLoaded = 0;
        size_t EntitiesSkipped = 0;          // entries that were not entities

        bool Failed() const;                 // a fatal issue: nothing was loaded
        bool HasIssues() const { return !Issues.empty(); }
        size_t Count(SceneLoadIssue::Severity level) const;
        // The fatal one (or the first error, else the first issue); null when empty.
        const SceneLoadIssue* GetMain() const;
        // One line: "3 ta xato, 2 ta ogohlantirish" / the fatal reason.
        std::string Summary() const;
        // Every issue on a line of its own (at most `maxLines`, then how many more).
        std::string Format(size_t maxLines = 50) const;
        // To the console: a summary line, then each issue (errors as errors, the rest as warnings).
        void Log() const;
        void Add(SceneLoadIssue issue);
        void Clear() { Issues.clear(); EntitiesLoaded = EntitiesSkipped = 0; }
    };

    // A scene file's description of itself, read without loading the scene (the editor's Scenes
    // panel and inspector, a game's level select): SceneSerializer::ReadSceneInfo.
    struct SceneFileInfo
    {
        std::string Name;                 // "Scene" (else the file's name)
        int Version = 0;                  // the format version it was written in (SceneMigration)
        SceneMetadata Metadata;
        size_t EntityCount = 0;
        size_t RootCount = 0;             // entities without a parent
        std::filesystem::path Thumbnail;  // SceneMetadata::GetThumbnailPath when the picture exists
        std::string Error;                // why the file could not be read ("" when it could)
    };

    //
    // Asset references are paths relative to the asset folder ("Texture": "Textures/coin.png").
    // When the active project's asset registry knows the file, its UUID is written next to the
    // path ("TextureUUID": "3f2a..."). Loading uses the path; when that file is gone but the UUID
    // belongs to an asset somewhere else (renamed / moved outside the editor), the asset's new
    // path is used instead and the repair is reported (GetRepairedReferences).
    class SceneSerializer
    {
    public:
        struct AssetReferenceRepair
        {
            std::string OldPath, NewPath; // relative to the asset folder
        };

        explicit SceneSerializer(const Ref<Scene>& scene) : m_Scene(scene) {}

        bool Serialize(const std::filesystem::path& filepath);
        bool Deserialize(const std::filesystem::path& filepath);

        // Scene files are written so that the same scene always gives the same text, whatever its
        // history (diffs show only what changed): the entities sorted by UUID, every object's keys
        // sorted, the hierarchy written out ("RootOrder": the root entities in order; "ChildOrder":
        // a parent's children when they are not in UUID order). Loading makes the entities in
        // hierarchy order (files without "RootOrder", written before: in file order).
        std::string SerializeToString();
        bool DeserializeFromString(const std::string& text);
        // Why the last Deserialize / DeserializeFromString call failed ("" after a success). A scene
        // file that cannot be read is reported once with its path (AssetImportLog).
        const std::string& GetLastError() const { return m_LastError; }
        // Every problem the last Deserialize* call found (also after a success: what was left out).
        // Deserialize(file) logs it; DeserializeFromString leaves that to the caller (Log()).
        const SceneLoadReport& GetLoadReport() const { return m_Report; }
        // Asset paths the last Deserialize* call took from UUIDs (the scene in memory differs from
        // its file until it is saved).
        const std::vector<AssetReferenceRepair>& GetRepairedReferences() const { return m_RepairedReferences; }
        // The format migration of the last Deserialize* call (see SceneMigration): the file's
        // version and the steps that brought it to the current one. Deserialize(file) logs it once
        // per file version; DeserializeFromString leaves that to the caller.
        const SceneMigrationResult& GetMigration() const { return m_Migration; }
        // How long the last Deserialize* call took: reading the file (Deserialize only), parsing, the
        // migration and making the entities (see SceneLoadTiming; the assets are resolved later).
        const SceneLoadTiming& GetTiming() const { return m_Timing; }

        // Entity subsets (prefabs, copy/paste). Parents outside the set become roots.
        // includeEditorFlags = false (prefabs): the hierarchy's hidden / locked flags are left
        // out, the inspector component order is kept.
        static std::string SerializeEntities(Scene& scene, const std::vector<Entity>& entities, bool includeEditorFlags = true);
        // Returns created entities, roots first. generateNewUUIDs = true for prefabs/paste.
        // `repairs` receives the asset paths taken from UUIDs (see above).
        // `error` (optional) receives why nothing could be read; the reason is logged only without it.
        static std::vector<Entity> DeserializeEntities(Scene& scene, const std::string& text, bool generateNewUUIDs,
                                                       std::vector<AssetReferenceRepair>* repairs = nullptr, std::string* error = nullptr);

        // The name, version, metadata and entity count of a scene file (it is parsed, not loaded).
        // False with `out.Error` when it cannot be read or is not a scene.
        static bool ReadSceneInfo(const std::filesystem::path& file, SceneFileInfo& out);
        static bool ReadSceneInfoFromString(const std::string& text, SceneFileInfo& out);

        // Transform clipboard text (inspector Copy / Paste). Deserialize returns false for any
        // text that is not a copied transform.
        static std::string SerializeTransform(const TransformComponent& transform);
        static bool DeserializeTransform(const std::string& text, TransformComponent& out);

        // Component clipboard text (inspector: copy one component, paste it onto other entities).
        // `key` is the component's name in scene files (ComponentKey<T>()); empty text when the
        // entity has no such component.
        static std::string SerializeComponent(Entity entity, std::string_view key);
        // Key of the component in copied text ("" when the text is not a copied component).
        static std::string GetSerializedComponentKey(const std::string& text);
        // Adds the copied component to `target`, or overwrites the values of the one it already
        // has. False (target unchanged) for any other text. Textures / meshes / fonts are not
        // loaded here: call Scene::ResolveAssets() afterwards.
        static bool DeserializeComponent(const std::string& text, Entity target);

    private:
        Ref<Scene> m_Scene;
        std::vector<AssetReferenceRepair> m_RepairedReferences;
        SceneLoadReport m_Report;
        SceneMigrationResult m_Migration;
        SceneLoadTiming m_Timing;
        std::string m_LastError;       // why the last Deserialize* call failed
        bool m_QuietErrors = false;    // Deserialize(file) reports the error with the file's path
    };

    // A scene file loaded in steps (SceneManager's asynchronous loading): Parse on any thread - it
    // only reads and parses the file - then Build on the main thread a slice at a time (settings,
    // the entities, then the hierarchy). The result is what SceneSerializer::Deserialize makes.
    class SceneLoadJob
    {
    public:
        SceneLoadJob();
        ~SceneLoadJob();
        SceneLoadJob(const SceneLoadJob&) = delete;
        SceneLoadJob& operator=(const SceneLoadJob&) = delete;

        // Any thread. False with GetError() when the file cannot be read or is not a scene.
        bool Parse(const std::filesystem::path& path);
        bool ParseString(const std::string& text);
        // Any thread, after Parse: the image files the entities name (as written: relative to the
        // assets), each once - decoded ahead of the main thread's texture uploads.
        std::vector<std::string> CollectImagePaths() const;
        size_t GetEntityCount() const;
        // Main thread: makes the scene into `scene` for about `budgetMs` milliseconds. True once it is
        // finished (or failed: GetError()); call again until then.
        bool Build(Scene& scene, double budgetMs);
        bool IsBuilt() const;
        float GetBuildProgress() const; // 0..1
        const std::string& GetError() const;
        const std::vector<SceneSerializer::AssetReferenceRepair>& GetRepairedReferences() const;
        // What Parse / Build found (see SceneLoadReport); complete once it is built or failed.
        const SceneLoadReport& GetReport() const;
        // The format migration Parse applied (see SceneMigration; Parse(file) logs it).
        const SceneMigrationResult& GetMigration() const;
        // How long it took so far: Parse's stages (reading, parsing, the migration - on the thread
        // that parsed) and Build's slices together (Deserialize).
        const SceneLoadTiming& GetTiming() const;

    private:
        struct Data;
        std::unique_ptr<Data> m_Data;
    };

}
