#pragma once

#include "Zamin/Core/UUID.h"
#include "Zamin/Editor/UndoHistory.h"
#include "Zamin/Scene/Entity.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Zamin {

    class PrefabDatabase;
    struct SceneLoadReport;

    // What an Apply / Revert covers: the whole instance, one of its entities, one component of an
    // entity, or one value (an override path, PrefabOverrides.h).
    enum class PrefabScope : uint8_t { Instance, Entity, Component, Field };

    struct PrefabApplyResult
    {
        bool Applied = false;
        std::string Error;                    // why nothing was written (Uzbek, for the user)
        std::string Target;                   // the prefab the values went into
        std::vector<std::string> Written;     // every prefab file written (the target, then the ones made of it)
        std::vector<std::string> Skipped;     // what stayed an override, and why ("Player: NavAgent/Target - ...")
        size_t InstancesSynced = 0;           // other instances in the scene that took the change
        std::vector<FileEdit> Files;          // the writes, for undo
    };

    // One thing an instance changed (the inspector's list).
    struct PrefabOverrideInfo
    {
        enum class Kind : uint8_t { Value, AddedComponent, RemovedComponent, AddedEntity, RemovedEntity };
        Kind Type = Kind::Value;
        UUID Entity = 0;           // the instance's entity (0 for a removed one)
        uint64_t LocalID = 0;      // its ID in the prefab
        std::string Path;          // the override path; the component's key; "" for entities
        std::string EntityName;    // the entity's name (a removed one: its name in the prefab)
    };

    // Prefab instances that stay linked to their prefab (editor only; the game never looks at the
    // links). An instance is the subtree under a ROOT that has EditorMetadataComponent::Prefab; its
    // entities carry their prefab IDs (PrefabLinkComponent::PrefabEntity) and what the instance
    // changed (override paths, removed components, removed entities). Everything else follows the
    // prefab: SyncInstance brings an instance up to date when the prefab file changed, Apply writes the
    // instance's changes into the prefab (and every other instance follows), Revert puts the prefab's
    // values back. Entities added to the instance stay the instance's. Prefabs nested in a prefab
    // belong to its instance in a scene (one link layer per file); the nested layer is kept in the
    // prefab file and brought up to date through PrefabDatabase.
    //
    // None of this runs on a scene that is playing (the play copy keeps the links unused).
    class PrefabUtility
    {
    public:
        // ---- what an entity is
        // The instance root the entity belongs to (the nearest ancestor-or-self with a prefab).
        static Entity GetInstanceRoot(Entity entity);
        static bool IsInstanceRoot(Entity entity);
        static std::string GetPrefabPath(Entity root);
        // Its ID in the prefab of its instance (0: not linked, or not in an instance).
        static uint64_t GetPrefabEntity(Entity entity);
        // In an instance and from its prefab.
        static bool IsLinked(Entity entity);
        // In an instance, added there (not from its prefab).
        static bool IsAdded(Entity entity);
        // An instance root without links (an older scene, SetPrefabSource): linked on open
        // (LinkLegacyInstances).
        static bool IsLegacyInstance(Entity root);
        // The entity's own overrides / removed components (the root: also removed entities).
        static bool HasOverrides(Entity entity);
        // The entities of the instance, depth-first (instances nested below it as a scene-level
        // instance are not).
        static std::vector<Entity> GetMembers(Entity root);

        // ---- instances
        // A linked instance of the prefab (under `parent`): every entity linked, the root's hash set.
        // A prefab that cannot be linked (several roots, missing / repeated IDs) is placed as a plain
        // copy with only its prefab named (warned). Null when it cannot be read.
        static Entity InstantiateLinked(Scene& scene, PrefabDatabase& db, std::string_view relPath, Entity parent = {});
        // `root` was just saved as the prefab `relPath` (Scene::SavePrefab): it becomes its first
        // instance - its entities linked by their own UUIDs (the IDs the file has), no overrides.
        static void LinkSource(Entity root, std::string_view relPath, PrefabDatabase& db);
        // Pairs the entities of legacy instances with their prefab's (the root with the root, the
        // children by name in order) and records what differs. Entity data is not touched. Returns
        // how many instances were linked.
        static size_t LinkLegacyInstances(Scene& scene, PrefabDatabase& db);
        // Links that mean nothing any more are removed: entities with a link but no instance root
        // (a pasted child of an instance), a prefab ID used twice in one instance (the entities in
        // `created` lose theirs, else the later one). Returns how many links were removed.
        static size_t NormalizeLinks(Scene& scene, const std::vector<Entity>* created = nullptr);

        // ---- overrides
        // Records what the instance differs in from its prefab (the lists on its entities). True
        // when a list changed.
        static bool RecordOverrides(Entity root, PrefabDatabase& db);
        // The same for the instances the entities `touched` belong to (an undo step's changes);
        // `entitiesRemoved`: entities were deleted (every instance missing some is looked at).
        static size_t RecordOverrides(Scene& scene, const std::vector<UUID>& touched, bool entitiesRemoved, PrefabDatabase& db);
        // Everything the instance changed, entity by entity (removed entities last).
        static std::vector<PrefabOverrideInfo> GetOverrides(Entity root);
        // Overrides of parts the prefab no longer has (a sync drops them): "<entity>: <path>" each.
        // Read only (the export's check).
        static std::vector<std::string> FindOrphanOverrides(Entity root, PrefabDatabase& db);

        // ---- following the prefab
        // The instance as its prefab makes it, with its overrides kept: values, components,
        // entities made / removed, parents, child order. In place (entity handles stay valid).
        // Nothing to do when its hash is the prefab's (unless `force`). True when it changed.
        static bool SyncInstance(Entity root, PrefabDatabase& db, bool force = false);
        struct SyncResult
        {
            size_t Instances = 0;              // linked instances looked at
            size_t Changed = 0;                // of them changed
            size_t Linked = 0;                 // legacy instances linked first
            std::vector<std::string> Issues;   // missing / broken prefabs (Uzbek)
        };
        // Every instance in the scene (of `prefabs` only, when given). Assets are not resolved
        // (Scene::ResolveAssets afterwards).
        static SyncResult SyncScene(Scene& scene, PrefabDatabase& db, const std::vector<std::string>* prefabs = nullptr, bool force = false);

        // ---- apply / revert
        // The prefabs the entity's values can be applied to: its instance's prefab first, then the
        // prefabs nested in it the entity comes from (the deepest last).
        static std::vector<std::string> GetApplyTargets(Entity entity, PrefabDatabase& db);
        // Writes what the instance changed (`scope` of `entity`; `path`: the component key or the
        // override path) into its prefab - or `target`, a prefab it is nested in (GetApplyTargets) -
        // then brings the prefabs made of it and the scene's other instances up to date. References
        // to entities outside the instance are not written (they stay overrides).
        static PrefabApplyResult Apply(Entity entity, PrefabScope scope, std::string_view path, PrefabDatabase& db, std::string_view target = {});
        // Puts the prefab's values back (`scope` as for Apply). Instance: everything - added entities
        // are destroyed, removed ones made again. True when something changed.
        static bool Revert(Entity entity, PrefabScope scope, std::string_view path, PrefabDatabase& db);
        // Makes the prefab entities the instance deleted again, nothing else. Returns how many.
        static size_t RestoreRemoved(Entity root, PrefabDatabase& db);
        // The instance becomes plain entities: its links and its prefab are removed (`completely`:
        // also of the instances nested below it). Returns how many entities were unlinked.
        static size_t Unpack(Entity root, bool completely = false);
        // A prefab file brought up to date with the prefabs nested in it (written only when that
        // changes something). False when it cannot be read or written.
        static bool SyncPrefabFile(PrefabDatabase& db, std::string_view relPath, std::vector<FileEdit>* edits = nullptr);

        // ---- hot reload: prefab files changed outside the editor (a text editor, version control)
        struct ReloadResult
        {
            std::vector<std::string> Changed;     // prefab files whose text changed (outside the editor)
            std::vector<std::string> Removed;     // prefab files gone (their instances keep their data)
            std::vector<std::string> Ignored;     // the editor's own writes, touched files with the same text
            std::vector<std::string> Rewritten;   // prefabs made of the changed ones, brought up to date on disk (in this order)
            std::vector<std::string> Failed;      // such prefabs that could not be brought up to date (unreadable, newer format)
            std::vector<std::string> Affected;    // Changed + Removed + every prefab made of them: the instances to sync
            std::vector<FileEdit> Files;          // the rewrites

            bool IsEmpty() const { return Affected.empty(); }
        };
        // Looks at the prefab files the file watcher reported (PrefabDatabase::CheckFile: own writes
        // and touches are not changes), forgets what was read of the changed ones and rewrites the
        // prefabs that nest them (or are their variants) - each after the ones it is made of, only
        // when its text changes (own writes: the watcher's next report of them is ignored). Calling it
        // again for the same files changes nothing. The scene's instances are not touched: SyncScene
        // with Affected afterwards (never on a running scene).
        static ReloadResult ReloadFiles(PrefabDatabase& db, const std::vector<std::string>& relPaths);

        // ---- variants: a prefab made of another one (its base). Its root is an instance of the base
        // in its own file (the root link written), so it follows the base - every entity, value and
        // child the variant did not change - and adds its own changes on top. Variants of variants
        // make a chain (up to PrefabDatabase::MaxDepth). Its file is flat like every prefab's (the
        // game reads it as it is).
        // A new variant of `base` with nothing changed yet, written to `variantPath` ("": "<Base>
        // Variant.zprefab" next to the base, made unique); its root is named after its file. The path
        // written, "" with `error` when it cannot be made (the base unreadable, the file there already).
        static std::string CreateVariant(PrefabDatabase& db, std::string_view base, std::string_view variantPath = {},
                                         std::vector<FileEdit>* edits = nullptr, std::string* error = nullptr);
        // What the instance changed becomes a new variant of its prefab (`variantPath`, the instance's
        // UUIDs as its IDs) and the instance becomes the variant's first instance (nothing changed
        // left). False with `error` when it cannot be (not linked, the file there already ...).
        static bool SaveAsVariant(Entity root, PrefabDatabase& db, std::string_view variantPath, std::vector<FileEdit>* edits = nullptr,
                                  std::string* error = nullptr);
        // The prefab's base when it is a variant ("" when it is not one).
        static std::string GetVariantBase(PrefabDatabase& db, std::string_view relPath);
        // The prefab, then its base, the base's base ... (each once; a missing base ends it).
        static std::vector<std::string> GetVariantChain(PrefabDatabase& db, std::string_view relPath);
        // `wanted` when no file is there, else "<stem> (1).zprefab", "<stem> (2).zprefab" ...
        static std::string MakeUniquePath(PrefabDatabase& db, std::string_view wanted);
        // The root's PrefabUUID looked up again (the project's registry knows a just-written file's
        // .zmeta now).
        static void RefreshPrefabUUID(Entity root);

        // ---- prefab mode (the editor opens a prefab on its own, as a scene of its own)
        // The prefab's entities into the empty `stage` with their IDs in the file as UUIDs (what it
        // saves keeps them), the prefabs nested in it as linked instances brought up to date with
        // their prefabs (their values, Apply / Revert work there as in a scene). `report` gets what
        // could not be read; `recover`: a file whose text is broken is salvaged (SceneRecovery). The
        // root (the first one), or null when nothing could be read.
        static Entity LoadStage(Scene& stage, PrefabDatabase& db, std::string_view relPath, SceneLoadReport* report = nullptr,
                                bool recover = false);
        // The text a prefab mode scene is saved as: its one root's subtree depth-first, the nested
        // instances with their links (their overrides recorded first). Unchanged since LoadStage:
        // the file's text as SavePrefab wrote it. "" with `error` when it has no root or several.
        static std::string SerializeStage(Scene& stage, PrefabDatabase& db, std::string* error = nullptr);
        // Writes the stage into the prefab (PrefabDatabase::Write: atomic, `edits` for undo) and
        // brings the prefabs made of it up to date (their files too). Refused (false, `error`): more
        // than one root, a nested prefab that is made of this one (a cycle), a file of a newer format,
        // a file whose load reported errors (PrefabDatabase::CountLoadErrors: what could not be read
        // would be lost) unless `overwriteDamaged` (the user chose so, the original backed up).
        // True without writing when the file holds that text already.
        static bool SaveStage(Scene& stage, PrefabDatabase& db, std::string_view relPath, std::vector<FileEdit>* edits = nullptr,
                              std::string* error = nullptr, bool overwriteDamaged = false);
    };

}
