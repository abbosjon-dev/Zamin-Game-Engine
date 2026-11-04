#pragma once

#include <Zamin/Zamin.h>

#include <filesystem>
#include <functional>
#include <optional>
#include <unordered_map>

namespace Zamin {

    // Hierarchy (entity list) + Inspector (component editor).
    class SceneHierarchyPanel
    {
    public:
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
        // The copies become the new selection.
        size_t DuplicateSelection();
        size_t DeleteSelection();

        // Hierarchy search text ("#tag" filters by gameplay tag).
        void SetFilter(const std::string& filter) { m_Filter = filter; }
        // Keeps the inspector scrolled to its end for a few frames (screenshots / automation).
        void ScrollInspectorToBottom() { m_ScrollInspectorFrames = 30; }

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
            return m_TilePaint && e && e.HasComponent<TilemapComponent>();
        }
        int GetSelectedTile() const { return m_SelectedTile; }

        std::function<void(Entity)> OnFocusRequested;
        std::function<void()> OnSceneModified;
        std::function<void(Entity)> OnSavePrefabRequested;
        std::function<void(const std::filesystem::path&, Entity parent)> OnPrefabDropped;
        // The project's tag list changed (a new tag was typed in the inspector).
        std::function<void()> OnProjectSettingsModified;

    private:
        // Where a hierarchy row drop puts the dragged entities.
        enum class DropZone { None, Before, After, Into, FirstChild, RootEnd };
        struct PendingClick { Entity Target; bool Ctrl = false, Shift = false; };
        struct PendingDrop { UUID Dragged = 0; Entity Target; DropZone Zone = DropZone::None; };

        template<typename T>
        void DisplayAddComponentEntry(const char* name, const char* icon);
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
        void DrawScriptComponent(Entity entity);
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
        std::string m_RenameBuffer;
        bool m_PendingDelete = false, m_PendingDuplicate = false;
        int m_RowIndex = 0;
        bool m_HierarchyFocused = false;
        bool m_TilePaint = false;
        int m_SelectedTile = 0;
        std::unordered_map<std::string, std::unordered_map<std::string, ScriptFieldValue>> m_ScriptDefaults;
    };

}
