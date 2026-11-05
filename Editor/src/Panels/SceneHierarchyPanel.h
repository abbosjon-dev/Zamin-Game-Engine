#pragma once

#include <Zamin/Zamin.h>

#include <imgui.h>

#include <filesystem>
#include <functional>
#include <optional>
#include <unordered_map>

namespace Zamin {

    class AssetInspectorPanel;

    // Hierarchy (entity list) + Inspector (component editor).
    class SceneHierarchyPanel
    {
    public:
        // The Inspector shows this panel's asset (a file picked in the content browser) instead of
        // the entities until the entity selection changes (AcknowledgeSelection: picked now).
        void SetAssetInspector(AssetInspectorPanel* panel) { m_AssetInspector = panel; }
        void AcknowledgeSelection() { m_SeenSelectionRevision = m_Selection.GetRevision(); }

        SceneHierarchyPanel() = default;

        // keepSelection: the same entities (by UUID) stay selected in the new scene (play mode, undo).
        void SetContext(const Ref<Scene>& scene, bool keepSelection = false);
        void OnImGuiRender(bool* showHierarchy, bool* showInspector);
        void DrawHierarchy();
        void DrawInspector();

        // ---- selection (Ctrl+click toggles, Shift+click selects a range of rows)
        // The primary entity (the last one picked) is what the inspector shows; the gizmo
        // pivot, focus, rename, tile painting... use it.
        Entity GetSelectedEntity() const { return m_Selection.GetPrimary(); }
        // Replaces the selection with this one entity (an invalid entity clears it).
        void SetSelectedEntity(Entity entity) { m_Selection.Select(entity); }
        const EntitySelection& GetSelection() const { return m_Selection; }
        EntitySelection& GetSelection() { return m_Selection; }
        size_t GetSelectionCount() const { return m_Selection.Size(); }
        bool IsSelected(Entity entity) const { return m_Selection.Contains(entity); }
        // Selected entities without those inside another selected subtree, in hierarchy order:
        // what delete / duplicate / copy / move / the gizmo work on.
        std::vector<Entity> GetSelectedRoots() const;
        // A click outside the hierarchy (viewport): Ctrl toggles, Shift adds, otherwise only this one.
        void SelectWithModifiers(Entity entity, bool ctrl, bool shift);
        void SelectAll();

        // Duplicates / deletes every selected subtree (one undo step for the caller).
        // The copies become the new selection. skipLocked: only the subtrees the gizmo can
        // move (Alt + drag / Ctrl+D while dragging leave locked ones alone).
        size_t DuplicateSelection(bool skipLocked = false);
        size_t DeleteSelection();

        // ---- eye / lock buttons (editor-only flags saved with the scene, see EditorMetadataComponent)
        // H: hides the selection, or shows it again when the primary entity is hidden. Returns how
        // many entities changed (0 = nothing selected / not editable); `hidden` = the new state.
        size_t ToggleSelectionHidden(bool* hidden = nullptr);
        size_t ToggleSelectionLocked(bool* locked = nullptr);
        size_t ShowAll();     // clears every hidden flag of the scene
        size_t UnlockAll();   // clears every lock
        // ---- active state (TagComponent::Active: game state saved with the scene, see Scene::SetActive)
        // Alt+Shift+A: switches the selection off, or on again when the primary entity is off.
        // Works while playing too (the running scene reacts at once: OnDisable / OnEnable, bodies).
        // Returns how many entities were targeted (0 = nothing selected); `active` = the new state.
        size_t ToggleSelectionActive(bool* active = nullptr);
        // Edit mode only: eye / lock toggles, component reordering and pasting.
        void SetSceneEditable(bool editable) { m_SceneEditable = editable; }

        // Inspector headers' open / collapsed state (owned by the editor layer, saved per user).
        void SetFoldState(InspectorFoldState* state) { m_FoldState = state; }

        // Hierarchy search text ("#tag" filters by gameplay tag).
        void SetFilter(const std::string& filter) { m_Filter = filter; }
        // Keeps the inspector scrolled to its end for a few frames (screenshots / automation).
        void ScrollInspectorToBottom() { m_ScrollInspectorFrames = 30; }

        // Row icon of an entity (by its main component) and the inspector header name of a
        // component key (status bar, menus).
        static const char* GetEntityIcon(Entity entity);
        static const char* GetComponentDisplayName(std::string_view key);

        // Entity creation shortcuts (also used by the editor menu).
        Entity CreateEntityPreset(const char* preset);
        void DrawCreateMenu();

        // Invalidate cached script defaults (after module reload).
        void OnScriptsReloaded() { m_ScriptDefaults.clear(); }
        bool IsHierarchyFocused() const { return m_HierarchyFocused; }

        // Tilemap painting (used by the viewport)
        bool IsTilePainting() const
        {
            Entity e = GetSelectedEntity();
            // Hidden / locked tilemaps are not painted in the viewport.
            return m_TilePaint && e && e.HasComponent<TilemapComponent>() &&
                   !e.GetScene()->IsHiddenInEditor(e) && !e.GetScene()->IsLockedInEditor(e);
        }
        int GetSelectedTile() const { return m_SelectedTile; }

        std::function<void(Entity)> OnFocusRequested;
        std::function<void()> OnSceneModified;
        std::function<void(Entity)> OnSavePrefabRequested;
        // An asset (absolute path) is to be edited in the inspector (a mesh renderer's material).
        std::function<void(const std::filesystem::path&)> OnAssetEditRequested;
        std::function<void(const std::filesystem::path&, Entity parent)> OnPrefabDropped;
        // The project's tag list changed (a new tag was typed in the inspector).
        std::function<void()> OnProjectSettingsModified;
        // The fold state (SetFoldState) changed: the editor saves its view settings.
        std::function<void()> OnFoldStateModified;

    private:
        // Where a hierarchy row drop puts the dragged entities.
        enum class DropZone { None, Before, After, Into, FirstChild, RootEnd };
        struct PendingClick { Entity Target; bool Ctrl = false, Shift = false; };
        struct PendingDrop { UUID Dragged = 0; Entity Target; DropZone Zone = DropZone::None; };

        template<typename T>
        void DisplayAddComponentEntry(const char* name, const char* icon);
        // The Material part of the Mesh Renderer section: the material asset (and per-submesh ones)
        // or the inline material. True when something changed.
        bool DrawMeshMaterial(Entity entity, struct MeshRendererComponent& c);
        // Inspector sections are collected first (AddSection) and then drawn in the entity's
        // component order (ComponentRegistry::GetOrderedKeys).
        struct InspectorSection { std::string_view Key; std::function<void()> Draw; };
        template<typename T, typename UIFunction>
        void AddSection(const char* icon, const std::string& name, Entity entity, UIFunction uiFunction,
                        bool removable, bool& modified, const std::function<bool(T&)>& settingsMenu = {});
        template<typename T, typename UIFunction>
        void DrawSection(const char* icon, const std::string& name, Entity entity, UIFunction& uiFunction,
                         bool removable, bool& modified, const std::function<bool(T&)>& settingsMenu);
        // Copy / paste values / move up-down items of a component's menu. True when it changed the scene.
        bool DrawComponentMenuItems(Entity entity, std::string_view key);
        // Header drag & drop: dropping a component header before / after another one reorders them.
        void DrawSectionDragDrop(std::string_view key);
        // Fold-state key of a header: the component key, scripts per class.
        static std::string FoldKey(Entity entity, std::string_view key);
        // Reads the component clipboard (the system clipboard first, then our own copy).
        void RefreshComponentClipboard();
        // Pastes the clipboard component onto the selected entities (the primary when not
        // selected): addMissing = also to those without that component. Returns how many changed.
        size_t PasteComponent(Entity entity, bool addMissing);
        // Entities a component menu acts on: the selection when `entity` is part of it.
        std::vector<Entity> GetMenuTargets(Entity entity) const;
        // Eye / lock buttons left of a hierarchy row.
        void DrawRowToggles(Entity entity, const ImVec2& rowMin, const ImVec2& rowMax);
        // Sets the flag on `entity`, or on the whole selection when `entity` is part of it.
        size_t SetFlagFromRow(Entity entity, bool lockFlag, bool value);
        // The same for the active flag (allowed while playing). Returns how many were targeted.
        size_t SetActiveFromRow(Entity entity, bool active);
        // Active checkbox of the inspector header and the "inactive" note under the name.
        void DrawActiveToggle(Entity entity);
        void DrawInactiveNote(Entity entity);
        void ApplyPendingClick();
        void ApplyPendingDrop();
        // Accepts a hierarchy entity dropped on the row drawn last (before / after it, or into it).
        void DrawRowDropTarget(Entity entity, bool open, bool hasChildren);
        // Transform header menu: reset / copy / paste (applied to every selected entity).
        bool DrawTransformMenu(Entity entity);
        bool PeekTransformClipboard(TransformComponent& out);

        void DrawComponents(Entity entity);
        void DrawEntityNode(Entity entity, int depth);
        void DrawEntityContextMenu(Entity entity);
        void DrawScriptComponent(Entity entity, bool& modified);
        // One script field with its attributes (header, label, tooltip, range, color, enum).
        bool DrawScriptField(const ScriptFieldInfo& field, ScriptFieldValue& value);
        // EntityRef script field: drop from the hierarchy, pick from a list, locate, clear.
        bool DrawEntityRefField(const char* name, EntityRef& ref, const char* tooltip = nullptr);
        // Gameplay tags: removable chips + add field with suggestions.
        bool DrawTagsComponent(TagsComponent& c);
        bool MatchesFilter(Entity entity, const std::string& lowerFilter);
        const std::unordered_map<std::string, ScriptFieldValue>& GetScriptDefaults(const std::string& className);
        void MarkModified() { if (OnSceneModified) OnSceneModified(); }

    private:
        Ref<Scene> m_Context;
        EntitySelection m_Selection;
        std::vector<Entity> m_VisibleRows;   // hierarchy rows in display order (Shift+click ranges)
        PendingClick m_PendingClick;
        PendingDrop m_PendingDrop;
        Entity m_ClickedSelected;            // pressed on an already selected row: decided on release
        std::optional<TransformComponent> m_TransformClipboard;
        TransformComponent m_PasteTransform;
        bool m_CanPasteTransform = false;
        std::string m_Filter;
        std::string m_AddComponentFilter;
        std::string m_EntityPickerFilter;
        std::string m_NewTagBuffer;
        entt::entity m_RenamingEntity = entt::null;
        int m_ScrollInspectorFrames = 0;
        AssetInspectorPanel* m_AssetInspector = nullptr;
        uint64_t m_SeenSelectionRevision = 0;
        std::string m_RenameBuffer;
        bool m_PendingDelete = false, m_PendingDuplicate = false;
        int m_RowIndex = 0;
        bool m_HierarchyFocused = false;
        bool m_TilePaint = false;
        int m_SelectedTile = 0;
        std::unordered_map<std::string, std::unordered_map<std::string, ScriptFieldValue>> m_ScriptDefaults;

        // inspector: sections of the entity being drawn, component clipboard, reordering
        std::vector<InspectorSection> m_Sections;
        std::vector<std::string> m_SectionOrder;   // the drawn entity's component keys, in order
        std::string m_ComponentClipboard;          // our own copy (the system clipboard is read first)
        std::string m_PasteText, m_PasteKey;       // clipboard contents when the menu opened
        struct PendingMove { std::string Key, Target; bool After = false; int Index = -1; };
        PendingMove m_PendingMove;                 // applied after the inspector is drawn
        InspectorFoldState* m_FoldState = nullptr;
        bool m_SceneEditable = true;
        float m_GutterX = 0.0f;                    // left edge of the hierarchy's eye / lock column
    };

}
