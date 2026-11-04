#pragma once

#include <Zamin/Zamin.h>

#include <filesystem>
#include <functional>
#include <unordered_map>

namespace Zamin {

    // Hierarchy (entity list) + Inspector (component editor).
    class SceneHierarchyPanel
    {
    public:
        SceneHierarchyPanel() = default;

        void SetContext(const Ref<Scene>& scene);
        void OnImGuiRender(bool* showHierarchy, bool* showInspector);
        void DrawHierarchy();
        void DrawInspector();

        Entity GetSelectedEntity() const { return m_SelectionContext; }
        void SetSelectedEntity(Entity entity) { m_SelectionContext = entity; }
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
        bool IsTilePainting() const { return m_TilePaint && m_SelectionContext && m_SelectionContext.IsValid() && m_SelectionContext.HasComponent<TilemapComponent>(); }
        int GetSelectedTile() const { return m_SelectedTile; }

        std::function<void(Entity)> OnFocusRequested;
        std::function<void()> OnSceneModified;
        std::function<void(Entity)> OnSavePrefabRequested;
        std::function<void(const std::filesystem::path&, Entity parent)> OnPrefabDropped;
        // The project's tag list changed (a new tag was typed in the inspector).
        std::function<void()> OnProjectSettingsModified;

    private:
        template<typename T>
        void DisplayAddComponentEntry(const char* name, const char* icon);

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
        Entity m_SelectionContext;
        std::string m_Filter;
        std::string m_AddComponentFilter;
        std::string m_EntityPickerFilter;
        std::string m_NewTagBuffer;
        entt::entity m_RenamingEntity = entt::null;
        int m_ScrollInspectorFrames = 0;
        std::string m_RenameBuffer;
        Entity m_PendingDelete, m_PendingDuplicate, m_PendingCreateChildOf;
        int m_RowIndex = 0;
        bool m_HierarchyFocused = false;
        bool m_TilePaint = false;
        int m_SelectedTile = 0;
        std::unordered_map<std::string, std::unordered_map<std::string, ScriptFieldValue>> m_ScriptDefaults;
    };

}
