#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Zamin {

    // The entities selected in an undo state, by UUID in the order they were picked, and the
    // primary one (shown in the inspector).
    struct UndoSelection
    {
        std::vector<uint64_t> Entities;
        uint64_t Primary = 0;

        bool IsEmpty() const { return Entities.empty(); }
        bool operator==(const UndoSelection&) const = default;
    };

    // A file an undo step wrote besides the scene (a prefab Apply writes the .zprefab): its text
    // before (none: the step made the file) and after. Undo puts `Before` back, Redo `After`.
    struct FileEdit
    {
        std::string Path;                    // relative to the assets ('/')
        std::optional<std::string> Before;
        std::string After;
    };

    enum class UndoEntryKind : uint8_t
    {
        Initial,    // the scene as it was opened (or after the history was cleared)
        Edit,       // the scene changed
        Selection,  // only the selection changed
    };

    // The editor scene's undo: a list of states - the scene as text plus the selection - each with
    // the name of the step that led to it, and a cursor at the state the scene is in now. Undo and
    // Redo move the cursor one step, JumpTo (the history panel) to any state; a new step drops the
    // states after the cursor. A step that only changed the selection shares the scene text of the
    // state before it, so selection steps cost almost nothing.
    class UndoHistory
    {
    public:
        struct Entry
        {
            std::string Name;
            UndoEntryKind Kind = UndoEntryKind::Edit;
            double Time = 0.0;                         // when it was made (the editor's clock)
            std::shared_ptr<const std::string> Scene;
            UndoSelection Selection;
            std::vector<FileEdit> Files;               // files the step wrote (shown with a file icon)
        };
        // One file of a step the move goes back over (Undo: write Edit->Before) or forward over
        // (Redo: write Edit->After), in the order they are to be written.
        struct FileStep
        {
            const FileEdit* Edit = nullptr;
            bool Undo = false;
        };
        // What a cursor move asks of the editor: the state to show, and whether its scene differs
        // from the one shown before the move (else only the selection is put back).
        struct Move
        {
            const Entry* Target = nullptr;             // null: nothing moved
            bool SceneChanged = false;
            std::vector<FileStep> Files;
        };

        // At most `maxEdits` scene steps and `maxSelections` selection steps are kept (the oldest
        // ones drop off; the first state left becomes where undo stops).
        static constexpr size_t DefaultMemoryBudget = 128 * 1024 * 1024;
        explicit UndoHistory(size_t maxEdits = 100, size_t maxSelections = 200, size_t memoryBudget = DefaultMemoryBudget);
        void SetMemoryBudget(size_t bytes);
        size_t GetMemoryBudget() const { return m_MemoryBudget; }
        // Unique scene texts, file edits, names and selections; allocator overhead is excluded.
        size_t GetRetainedBytes() const;

        // Only `scene` (Initial). `saved`: it is what the scene's file holds.
        void Reset(std::string scene, UndoSelection selection, std::string name, double time = 0.0, bool saved = true);
        bool IsEmpty() const { return m_Entries.empty(); }

        // A new state after the current one (the states after the cursor are dropped). False
        // (nothing added) when the scene text is the same as the current state's.
        bool CommitEdit(std::string scene, UndoSelection selection, std::string name, double time = 0.0);
        // A step that also wrote files (always added when `files` is not empty).
        bool CommitEdit(std::string scene, UndoSelection selection, std::string name, double time, std::vector<FileEdit> files);
        // The current state's scene becomes `scene` without a new step (the prefabs it uses changed
        // on disk and their instances followed): it is no longer the saved one unless it is the same.
        void ReplaceCurrentScene(std::string scene);
        // A step that only changes the selection; false when it is the current one already.
        bool CommitSelection(UndoSelection selection, std::string name, double time = 0.0);
        // The current state's selection, without a step (selection steps switched off; after play).
        void SetCurrentSelection(UndoSelection selection);

        bool CanUndo() const { return m_Current > 0; }
        bool CanRedo() const { return m_Current + 1 < m_Entries.size(); }
        Move Undo();
        Move Redo();
        Move JumpTo(size_t index);

        // The step Undo would take back / Redo would make again ("" when there is none).
        const std::string& GetUndoName() const;
        const std::string& GetRedoName() const;

        // The state the scene is in (an empty entry before Reset).
        const Entry& GetCurrent() const;
        size_t GetCurrentIndex() const { return m_Current; }
        const std::vector<Entry>& GetEntries() const { return m_Entries; }
        size_t CountEdits() const;
        size_t CountSelections() const;
        // Bytes of scene text held (a shared text once).
        size_t GetMemoryUsage() const;

        // The scene's file now holds the current state's scene (saved); ClearSaved: no state does.
        void MarkSaved();
        void ClearSaved();
        // Whether the current state's scene is the saved one (undo back to it: nothing to save).
        bool IsAtSavedState() const;
        // Whether that state's scene is what the file holds (the history window marks it).
        bool IsSavedState(size_t index) const;

        // Keeps only the current state, as the new Initial one named `name`.
        void ClearHistory(std::string name, double time = 0.0);

        // Changes every scene text (paths in them moved with the assets); a shared text once. The
        // files the steps wrote follow too: their texts through `rewrite`, their paths (relative to
        // the assets) through `rewritePath` - a prefab renamed after an Apply is what Undo writes.
        void RewriteScenes(const std::function<void(std::string&)>& rewrite,
                           const std::function<void(std::string&)>& rewritePath = {});

    private:
        void Trim();

    private:
        std::vector<Entry> m_Entries;
        size_t m_Current = 0;
        std::weak_ptr<const std::string> m_Saved;
        size_t m_MaxEdits, m_MaxSelections, m_MemoryBudget;
    };

    // What changed between two scene snapshots (SceneSerializer texts): entities added, removed
    // and changed (matched by UUID) and scene-level keys. Only what differs is looked into, so a
    // big scene with one change costs one pass over the texts.
    struct SceneChange
    {
        struct EntityChange
        {
            std::string Tag, OldTag;                          // the entity's name now / before
            std::vector<std::string> Added, Removed, Changed; // its top-level keys ("Transform", "Tag", "Parent" ...)
            std::string Field;                                // one key changed in one field: that field
            uint64_t ID = 0;                                  // its UUID
        };
        bool Valid = false;                                   // both texts could be read
        std::vector<std::string> Added, Removed;              // names of the entities
        std::vector<uint64_t> AddedIDs, RemovedIDs;           // their UUIDs (in the same order)
        std::vector<EntityChange> Changed;
        std::vector<std::string> SceneKeys;                   // "Settings", "Metadata", "Scene", "RootOrder"
        std::string SceneField;                               // only one key of Settings changed: it

        bool IsEmpty() const { return Added.empty() && Removed.empty() && Changed.empty() && SceneKeys.empty(); }
    };
    SceneChange DiffSceneSnapshots(std::string_view before, std::string_view after);
    // A short name for the undo history: "Transform.Translation o'zgardi (Player)",
    // "Qo'shildi: Cube (+2)", "Komponent qo'shildi: Rigidbody2D (Player)" ...
    std::string DescribeSceneChange(const SceneChange& change);
    std::string DescribeSceneChange(std::string_view before, std::string_view after);
    // "Tanlash: Player", "Tanlash: Player (+2)", "Tanlov bekor qilindi" (`names`: the primary first).
    std::string DescribeSelection(const std::vector<std::string>& names);

}
