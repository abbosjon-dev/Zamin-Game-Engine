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

    private:
        template<typename T>
        void DisplayAddComponentEntry(const char* name, const char* icon);

        void DrawComponents(Entity entity);
        void DrawEntityNode(Entity entity, int depth);
        void DrawEntityContextMenu(Entity entity);
        void DrawScriptComponent(Entity entity);
        const std::unordered_map<std::string, ScriptFieldValue>& GetScriptDefaults(const std::string& className);
        void MarkModified() { if (OnSceneModified) OnSceneModified(); }

    private:
        Ref<Scene> m_Context;
        Entity m_SelectionContext;
        std::string m_Filter;
        std::string m_AddComponentFilter;
        entt::entity m_RenamingEntity = entt::null;
        std::string m_RenameBuffer;
        Entity m_PendingDelete, m_PendingDuplicate, m_PendingCreateChildOf;
        int m_RowIndex = 0;
        bool m_HierarchyFocused = false;
        bool m_TilePaint = false;
        int m_SelectedTile = 0;
        std::unordered_map<std::string, std::unordered_map<std::string, ScriptFieldValue>> m_ScriptDefaults;
    };

}
