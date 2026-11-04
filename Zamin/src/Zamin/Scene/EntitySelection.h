#pragma once

#include "Zamin/Core/UUID.h"
#include "Zamin/Scene/Entity.h"
#include "Zamin/Scene/Components.h"

#include <glm/glm.hpp>

#include <string>
#include <unordered_set>
#include <vector>

namespace Zamin {

    class Scene;

    // Editor-style multi-selection: an ordered set of entities plus a primary one, the most
    // recently picked (shown in the inspector, carries the gizmo, gets renamed / focused...).
    // The anchor is where a Shift+click range starts (the last plain or Ctrl click).
    class EntitySelection
    {
    public:
        // Only `entity` (an invalid entity clears the selection).
        void Select(Entity entity);
        // Adds `entity`; it becomes the primary one (and the anchor) when makePrimary is set.
        void Add(Entity entity, bool makePrimary = true);
        void Remove(Entity entity);
        // Ctrl+click: removes a selected entity, adds an unselected one.
        void Toggle(Entity entity);
        // Shift+click: every entity between the anchor and `to` in `order` (the rows as the
        // hierarchy lists them). additive keeps the current selection (Ctrl+Shift+click).
        // Without a listed anchor only `to` is selected.
        void SelectRange(const std::vector<Entity>& order, Entity to, bool additive = false);
        // `primary` defaults to the last valid entity of the list.
        void Set(const std::vector<Entity>& entities, Entity primary = {});
        void Clear();

        bool Contains(Entity entity) const;
        bool IsEmpty() const { return m_Entities.empty(); }
        size_t Size() const { return m_Entities.size(); }
        Entity GetPrimary() const { return m_Primary; }
        Entity GetAnchor() const { return m_Anchor; }
        // In the order they were picked.
        const std::vector<Entity>& GetEntities() const { return m_Entities; }

        // Forgets destroyed entities; the primary falls back to the most recently picked one left.
        void Prune();
        // The same entities (matched by UUID) in `scene` (nullptr clears): entering / leaving
        // play mode, undo. The currently selected entities must still be alive.
        void Remap(Scene* scene);

    private:
        void Rebuild();

    private:
        std::vector<Entity> m_Entities;
        std::unordered_set<entt::entity> m_Lookup;
        Scene* m_Scene = nullptr; // every selected entity belongs to this scene
        Entity m_Primary, m_Anchor;
    };

    // What the editor's status bar shows about the selection.
    struct SelectionSummary
    {
        size_t Count = 0;                    // selected entities (0: nothing else is filled in)
        Entity Primary;
        std::string Name;                    // the primary entity's
        std::string Path;                    // its parents' names and its own: "World/Player/Weapon"
        uint64_t ID = 0;
        glm::vec3 Position{ 0.0f };          // the primary's world position
        glm::vec3 Center{ 0.0f };            // average world position of every selected entity
        std::vector<std::string> Components; // the primary's component keys in inspector order (Transform first)
        size_t ChildCount = 0;               // direct children of the primary
        size_t DescendantCount = 0;          // everything below it
        std::vector<std::string> Tags;       // gameplay tags of the primary
        bool Hidden = false, Locked = false; // the primary's eye / lock state (inherited ones too)
        size_t HiddenCount = 0, LockedCount = 0; // selected entities hidden / locked
        // The primary is not active in the hierarchy (TagComponent::Active): its own flag is off
        // (InactiveSelf) or an ancestor's is.
        bool Inactive = false, InactiveSelf = false;
        size_t InactiveCount = 0;            // selected entities not active in the hierarchy
    };
    SelectionSummary SummarizeSelection(Scene& scene, const EntitySelection& selection);

    // Moves, rotates or scales several entities together like one object (the editor gizmo
    // dragged with a multi-selection). Begin() records where everything was; each Update()
    // applies the pivot's change since Begin(), so nothing drifts during a long drag.
    class GroupTransformEdit
    {
    public:
        enum class Operation { Translate = 0, Rotate = 1, Scale = 2 };

        // Gizmo matrix for a selection: the primary entity's world transform, or (center) the
        // average world position of `entities` oriented like the primary (orientToPrimary) or
        // along the world axes, with unit scale.
        static glm::mat4 ComputePivot(Scene& scene, const std::vector<Entity>& entities, Entity primary,
                                      bool center, bool orientToPrimary);

        // `entities` must not contain each other's descendants (see Scene::GetTopLevelEntities).
        void Begin(Scene& scene, const std::vector<Entity>& entities, const glm::mat4& pivot);
        // Translate: positions only. Rotate: positions and orientations around the pivot.
        // Scale: positions spread from the pivot and each local scale is multiplied by the
        // pivot's scale change (per axis).
        void Update(const glm::mat4& pivot, Operation operation);
        // Puts every entity back where it was at Begin() (and ends the edit).
        void Cancel();
        void End();

        bool IsActive() const { return m_Scene != nullptr; }
        Scene* GetScene() const { return m_Scene; }
        size_t GetCount() const { return m_Items.size(); }
        const glm::mat4& GetStartPivot() const { return m_StartPivot; }
        // Whether `pivot` differs from the one the edit started with (more than float noise):
        // a gizmo that was clicked but not dragged has not moved.
        bool HasMoved(const glm::mat4& pivot) const;

    private:
        struct Item
        {
            UUID ID;
            glm::mat4 StartWorld{ 1.0f };
            TransformComponent StartLocal;
        };
        Scene* m_Scene = nullptr;
        glm::mat4 m_StartPivot{ 1.0f };
        std::vector<Item> m_Items;
    };

}
