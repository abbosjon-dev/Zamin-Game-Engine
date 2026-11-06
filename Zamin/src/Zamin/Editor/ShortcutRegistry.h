#pragma once

#include "Zamin/Core/KeyCodes.h"

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace Zamin {

    // A key with the modifiers held with it ("Ctrl+Shift+S"). An empty chord has no key (unbound).
    struct KeyChord
    {
        Key KeyCode = Key(0);
        bool Ctrl = false, Shift = false, Alt = false;

        KeyChord() = default;
        constexpr KeyChord(Key key, bool ctrl = false, bool shift = false, bool alt = false)
            : KeyCode(key), Ctrl(ctrl), Shift(shift), Alt(alt) {}

        bool IsEmpty() const { return (uint16_t)KeyCode == 0; }
        bool operator==(const KeyChord&) const = default;

        // Stable text for files: "Ctrl+Shift+S", "Alt+H", "Delete" ("" for an empty chord). The
        // key names are KeyToString's.
        std::string ToString() const;
        // What menus and the shortcuts window show: "Ctrl+Shift+S", "Ctrl+,", "Del", "Esc", "Num 1".
        std::string GetDisplayName() const;
        // ToString's form; modifiers in any order and letter case ("shift+ctrl+s"), "Control" for
        // Ctrl, the shown key names too ("Ctrl+,", "Del"). "" gives an empty chord. False: not a chord.
        static bool Parse(std::string_view text, KeyChord& out);
        // Shift / Ctrl / Alt / Super keys: held with a key, never a chord's key themselves.
        static bool IsModifierKey(Key key);
    };

    // Where a shortcut works. A scope is a set of situations (ShortcutSituation bits) the editor is
    // in this frame; two shortcuts with the same chord conflict when their scopes share a situation
    // and neither scope lies inside the other: a narrower scope wins where both apply (a panel's own
    // Delete over the selection's, anything over a Global one), which is by design.
    enum class ShortcutScope : uint8_t
    {
        Global = 0,      // anywhere in the editor (not while typing into a field)
        Selection,       // the hierarchy has the keyboard, or the scene view the mouse / keyboard
        SceneView,       // the scene view in edit / simulate mode (camera framing, bookmarks)
        ViewportTools,   // the scene view in edit mode with the transform tools
        TilePaint,       // the scene view while painting tiles
        HandleEdit,      // the scene view while collider points / joint anchors are edited
        Play,            // while the game runs in the editor
        Hierarchy,       // the hierarchy window has the keyboard
        ContentBrowser,
        Animation,       // the Animation (timeline) window
        AnimatorGraph,
        SpriteEditor,
        Console,
        Camera,          // mouse gestures and held keys of the editor camera (listed, not bound)
        Count
    };

    // What the editor is doing this frame: the bits it hands to ShortcutRegistry::Match.
    namespace ShortcutSituation {
        enum : uint32_t
        {
            Hierarchy       = 1u << 0,
            SceneView       = 1u << 1,   // the scene view (edit / simulate, not flying the camera)
            ViewportTools   = 1u << 2,   // ... in edit mode with the transform tools
            ViewportTile    = 1u << 3,   // ... painting tiles
            ViewportHandles = 1u << 4,   // ... editing points / anchors
            Playing         = 1u << 5,
            ContentBrowser  = 1u << 6,
            Animation       = 1u << 7,
            AnimatorGraph   = 1u << 8,
            SpriteEditor    = 1u << 9,
            Console         = 1u << 10,
            Editor          = 1u << 31,  // always: the editor has the keyboard (Global shortcuts)
            All             = 0xffffffffu,
        };
    }

    // The situations a scope covers (Global: all of them).
    uint32_t GetScopeSituations(ShortcutScope scope);
    // "Hamma joyda", "Viewport (tahrirlash)" ... (the shortcuts window's column).
    const char* GetScopeName(ShortcutScope scope);

    struct ShortcutInfo
    {
        std::string Id;            // "file.save": stable, the preferences store bindings under it
        std::string Category;      // "Fayl", "Tahrirlash" ... (the shortcuts window groups by it)
        std::string Name;          // "Sahnani saqlash"
        ShortcutScope Scope = ShortcutScope::Global;
        KeyChord Default, Chord;
        // Listed only (a panel's own key, a mouse gesture): not rebindable, never runs an action.
        // A fixed chord still takes its key where its scope applies (Match finds it).
        bool Fixed = false;
        std::string FixedText;     // shown in place of a chord ("O'ng tugma + WASD")
        std::string Display;       // Chord.GetDisplayName() (or FixedText): menus show its c_str()
        std::function<void()> Action; // what the editor does (Bind)

        bool IsModified() const { return !Fixed && Chord != Default; }
    };

    // The editor's keyboard shortcuts: what each one is called, its default and current chord,
    // where it works. The editor dispatches the keys pressed each frame through Match.
    class ShortcutRegistry
    {
    public:
        // Adds a shortcut (one with the same id is replaced) and returns it.
        ShortcutInfo& Register(std::string id, std::string category, std::string name, KeyChord chord,
                               ShortcutScope scope = ShortcutScope::Global);
        // A listed-only entry with a chord (a panel's own key) or a text (a mouse gesture).
        ShortcutInfo& RegisterFixed(std::string id, std::string category, std::string name, KeyChord chord,
                                    ShortcutScope scope, std::string text = {});
        // The action a (non-fixed) shortcut runs. False: no such shortcut.
        bool Bind(std::string_view id, std::function<void()> action);

        const ShortcutInfo* Find(std::string_view id) const;
        // The chord as menus show it ("" when unbound or unknown).
        const char* GetDisplay(std::string_view id) const;

        // The shortcut a chord pressed now belongs to, among those whose scope covers one of
        // `situations`: the narrowest scope's (fewest situations), the first registered of equals.
        // Fixed ones count (they hold their key for their panel). Null: none / an empty chord.
        const ShortcutInfo* Match(const KeyChord& chord, uint32_t situations) const;

        // Fixed or unknown shortcuts cannot be rebound (false). An empty chord unbinds.
        bool Rebind(std::string_view id, const KeyChord& chord);
        bool ResetToDefault(std::string_view id);
        void ResetAll();

        // Whether two scopes can both apply with neither inside the other (a real conflict).
        static bool ScopesConflict(ShortcutScope a, ShortcutScope b);
        // The other shortcuts `chord` in `scope` would conflict with (`except`: an id left out).
        std::vector<const ShortcutInfo*> FindConflicts(const KeyChord& chord, ShortcutScope scope, std::string_view except = {}) const;
        std::vector<const ShortcutInfo*> FindConflicts(std::string_view id) const;
        // Shortcuts that have at least one conflict.
        size_t CountConflicts() const;
        size_t CountModified() const;

        // The search of the shortcuts window: every word of `query` is found (case-insensitive) in
        // the name, the category, the id, the shown chord or the scope's name. Empty: everything.
        static bool MatchesSearch(const ShortcutInfo& shortcut, std::string_view query);

        // The changed bindings: id -> chord text ("" = unbound); what the preferences store.
        std::map<std::string, std::string> GetOverrides() const;
        // Bindings from the preferences over the defaults (everything else back to its default).
        // Unknown ids, fixed ones and texts that are not chords are skipped; returns how many applied.
        size_t ApplyOverrides(const std::map<std::string, std::string>& overrides);

        const std::vector<ShortcutInfo>& GetAll() const { return m_Shortcuts; }
        // In the order they were first used.
        std::vector<std::string> GetCategories() const;
        bool IsEmpty() const { return m_Shortcuts.empty(); }

    private:
        ShortcutInfo* FindMutable(std::string_view id);

    private:
        std::vector<ShortcutInfo> m_Shortcuts;
    };

    // The ids of the editor's shortcuts (the actions the editor binds to them).
    namespace EditorShortcut {
        inline constexpr const char* NewScene = "file.new_scene";
        inline constexpr const char* OpenScene = "file.open_scene";
        inline constexpr const char* SaveScene = "file.save";
        inline constexpr const char* SaveSceneAs = "file.save_as";
        inline constexpr const char* OpenProject = "file.open_project";
        inline constexpr const char* Undo = "edit.undo";
        inline constexpr const char* Redo = "edit.redo";
        inline constexpr const char* RedoAlt = "edit.redo_alt";
        inline constexpr const char* UndoHistory = "edit.undo_history";
        inline constexpr const char* Copy = "edit.copy";
        inline constexpr const char* Paste = "edit.paste";
        inline constexpr const char* Duplicate = "edit.duplicate";
        inline constexpr const char* Preferences = "edit.preferences";
        inline constexpr const char* Delete = "selection.delete";
        inline constexpr const char* SelectAll = "selection.select_all";
        inline constexpr const char* ToggleActive = "selection.toggle_active";
        inline constexpr const char* ToggleHidden = "selection.toggle_hidden";
        inline constexpr const char* ShowAllHidden = "selection.show_all";
        inline constexpr const char* PlayToggle = "play.toggle";
        inline constexpr const char* PlayStop = "play.stop";
        inline constexpr const char* BuildScripts = "scripts.build";
        inline constexpr const char* ToolNone = "tools.none";
        inline constexpr const char* ToolMove = "tools.move";
        inline constexpr const char* ToolRotate = "tools.rotate";
        inline constexpr const char* ToolScale = "tools.scale";
        inline constexpr const char* FrameSelection = "view.frame_selection";
        inline constexpr const char* FrameAll = "view.frame_all";
        inline constexpr const char* TileBrush = "tiles.brush";
        inline constexpr const char* TileRectangle = "tiles.rectangle";
        inline constexpr const char* TileFill = "tiles.fill";
        inline constexpr const char* TileEraser = "tiles.eraser";
        inline constexpr const char* TilePicker = "tiles.picker";
        inline constexpr const char* TileExit = "tiles.exit";
        inline constexpr const char* TileExitNone = "tiles.exit_none";
        inline constexpr const char* TileExitMove = "tiles.exit_move";
        inline constexpr const char* HandlesExit = "handles.exit";
        inline constexpr const char* HandlesExitNone = "handles.exit_none";
        inline constexpr const char* HandlesExitMove = "handles.exit_move";
        inline constexpr const char* ShortcutsWindow = "help.shortcuts";
        // Camera bookmarks: "view.bookmark_save_1" ... / "view.bookmark_recall_1" ...
        std::string BookmarkSave(int slot);
        std::string BookmarkRecall(int slot);
        inline constexpr int BookmarkSlots = 5;
    }

    // Every shortcut of the editor with its default chord (the editor binds the actions), and the
    // keys panels handle themselves and the camera's mouse gestures as fixed entries.
    void RegisterEditorShortcuts(ShortcutRegistry& registry);

}
