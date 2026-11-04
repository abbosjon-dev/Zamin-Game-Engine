#include "Zamin/Scene/EntitySelection.h"
#include "Zamin/Scene/Scene.h"
#include "Zamin/Math/Math.h"
#include "Zamin/Scene/ComponentRegistry.h"

#include <algorithm>

namespace Zamin {

    // ------------------------------------------------------------------ selection
    void EntitySelection::Select(Entity entity)
    {
        Clear();
        if (entity)
            Add(entity, true);
    }

    void EntitySelection::Add(Entity entity, bool makePrimary)
    {
        if (!entity) return;
        if (m_Scene && entity.GetScene() != m_Scene)
            Clear(); // one scene at a time
        m_Scene = entity.GetScene();
        if (m_Lookup.insert((entt::entity)entity).second)
            m_Entities.push_back(entity);
        if (makePrimary || !m_Primary)
        {
            m_Primary = entity;
            m_Anchor = entity;
        }
    }

    void EntitySelection::Remove(Entity entity)
    {
        if (!m_Scene || entity.GetScene() != m_Scene || !m_Lookup.erase((entt::entity)entity))
            return;
        std::erase(m_Entities, entity);
        if (m_Primary == entity)
            m_Primary = m_Entities.empty() ? Entity() : m_Entities.back();
        if (m_Anchor == entity)
            m_Anchor = m_Primary;
    }

    void EntitySelection::Toggle(Entity entity)
    {
        if (!entity) return;
        if (Contains(entity))
        {
            Remove(entity);
            m_Anchor = entity; // a following Shift+click still ranges from here
        }
        else
            Add(entity, true);
    }

    void EntitySelection::SelectRange(const std::vector<Entity>& order, Entity to, bool additive)
    {
        if (!to) return;
        auto anchorIt = m_Anchor ? std::find(order.begin(), order.end(), m_Anchor) : order.end();
        auto toIt = std::find(order.begin(), order.end(), to);
        if (anchorIt == order.end() || toIt == order.end())
        {
            if (additive) Add(to, true);
            else Select(to);
            return;
        }
        Entity anchor = m_Anchor;
        if (!additive)
            Clear();
        auto first = std::min(anchorIt, toIt), last = std::max(anchorIt, toIt);
        for (auto it = first; it <= last; ++it)
            Add(*it, false);
        Add(to, true);
        m_Anchor = anchor; // further Shift+clicks range from the same row
    }

    void EntitySelection::Set(const std::vector<Entity>& entities, Entity primary)
    {
        Clear();
        for (Entity e : entities)
            Add(e, false);
        if (primary && Contains(primary))
            m_Primary = primary;
        else if (!m_Entities.empty())
            m_Primary = m_Entities.back();
        m_Anchor = m_Primary;
    }

    void EntitySelection::Clear()
    {
        m_Entities.clear();
        m_Lookup.clear();
        m_Primary = {};
        m_Anchor = {};
        m_Scene = nullptr;
    }

    bool EntitySelection::Contains(Entity entity) const
    {
        // Handles are per scene (and carry a version, so a recycled handle never matches).
        return m_Scene && entity.GetScene() == m_Scene && entity.IsValid() && m_Lookup.count((entt::entity)entity) != 0;
    }

    void EntitySelection::Rebuild()
    {
        m_Lookup.clear();
        for (Entity e : m_Entities)
            m_Lookup.insert((entt::entity)e);
    }

    void EntitySelection::Prune()
    {
        size_t before = m_Entities.size();
        std::erase_if(m_Entities, [](const Entity& e) { return !e.IsValid(); });
        if (m_Entities.size() != before)
            Rebuild();
        // (operator bool means "valid": a destroyed primary is simply not contained any more)
        if (!Contains(m_Primary))
            m_Primary = m_Entities.empty() ? Entity() : m_Entities.back();
        if (!m_Anchor.IsValid())
            m_Anchor = m_Primary;
        if (m_Entities.empty())
            m_Scene = nullptr;
    }

    void EntitySelection::Remap(Scene* scene)
    {
        std::vector<UUID> ids;
        for (Entity e : m_Entities)
            if (e.IsValid())
                ids.push_back(e.GetUUID());
        UUID primary = m_Primary.IsValid() ? m_Primary.GetUUID() : UUID(0);
        UUID anchor = m_Anchor.IsValid() ? m_Anchor.GetUUID() : UUID(0);
        Clear();
        if (!scene) return;
        for (UUID id : ids)
            if (Entity e = scene->GetEntityByUUID(id))
                Add(e, false);
        if ((uint64_t)primary)
            if (Entity e = scene->GetEntityByUUID(primary); e && Contains(e))
                m_Primary = e;
        if (!m_Primary && !m_Entities.empty())
            m_Primary = m_Entities.back();
        m_Anchor = (uint64_t)anchor ? scene->GetEntityByUUID(anchor) : Entity();
        if (!m_Anchor)
            m_Anchor = m_Primary;
    }

    // ------------------------------------------------------------------ group transform
    static glm::vec3 AxisLengths(const glm::mat4& m)
    {
        return { glm::length(glm::vec3(m[0])), glm::length(glm::vec3(m[1])), glm::length(glm::vec3(m[2])) };
    }

    glm::mat4 GroupTransformEdit::ComputePivot(Scene& scene, const std::vector<Entity>& entities, Entity primary,
                                               bool center, bool orientToPrimary)
    {
        if (!primary && !entities.empty())
            primary = entities.front();
        if (!center || entities.empty())
            return primary ? scene.GetWorldTransform(primary) : glm::mat4(1.0f);

        glm::vec3 sum(0.0f);
        int count = 0;
        for (Entity e : entities)
            if (e)
            {
                sum += scene.GetWorldPosition(e);
                count++;
            }
        glm::mat4 pivot(1.0f);
        if (orientToPrimary && primary)
        {
            glm::mat4 world = scene.GetWorldTransform(primary);
            glm::vec3 lengths = AxisLengths(world);
            for (int i = 0; i < 3; i++)
                pivot[i] = glm::vec4(lengths[i] > 1e-6f ? glm::vec3(world[i]) / lengths[i] : glm::vec3(i == 0, i == 1, i == 2), 0.0f);
        }
        pivot[3] = glm::vec4(count ? sum / (float)count : glm::vec3(0.0f), 1.0f);
        return pivot;
    }

    void GroupTransformEdit::Begin(Scene& scene, const std::vector<Entity>& entities, const glm::mat4& pivot)
    {
        m_Scene = &scene;
        m_StartPivot = pivot;
        m_Items.clear();
        for (Entity e : entities)
            if (e.IsValid() && e.GetScene() == &scene)
                m_Items.push_back({ e.GetUUID(), scene.GetWorldTransform(e), e.GetComponent<TransformComponent>() });
    }

    void GroupTransformEdit::Update(const glm::mat4& pivot, Operation operation)
    {
        if (!m_Scene) return;
        const glm::mat4 delta = pivot * glm::inverse(m_StartPivot);
        glm::vec3 ratio(1.0f);
        if (operation == Operation::Scale)
        {
            glm::vec3 from = AxisLengths(m_StartPivot), to = AxisLengths(pivot);
            for (int i = 0; i < 3; i++)
                ratio[i] = from[i] > 1e-6f ? to[i] / from[i] : 1.0f;
        }

        for (const Item& item : m_Items)
        {
            Entity e = m_Scene->GetEntityByUUID(item.ID);
            if (!e) continue;
            Entity parent = m_Scene->GetParent(e);
            const glm::mat4 toLocal = parent ? glm::inverse(m_Scene->GetWorldTransform(parent)) : glm::mat4(1.0f);
            auto& tc = e.GetComponent<TransformComponent>();

            const glm::vec3 worldPosition = glm::vec3(delta * item.StartWorld[3]);
            const glm::vec3 localPosition = glm::vec3(toLocal * glm::vec4(worldPosition, 1.0f));
            switch (operation)
            {
                case Operation::Translate:
                    tc.Translation = localPosition;
                    break;
                case Operation::Scale:
                    tc.Translation = localPosition;
                    tc.Rotation = item.StartLocal.Rotation;
                    tc.Scale = item.StartLocal.Scale * ratio;
                    break;
                case Operation::Rotate:
                {
                    glm::vec3 t, r, s;
                    if (Math::DecomposeTransform(toLocal * delta * item.StartWorld, t, r, s))
                    {
                        tc.Translation = t;
                        tc.Rotation = r;
                        tc.Scale = item.StartLocal.Scale; // a rotation never changes the size
                    }
                    break;
                }
            }
        }
    }

    void GroupTransformEdit::Cancel()
    {
        if (m_Scene)
            for (const Item& item : m_Items)
                if (Entity e = m_Scene->GetEntityByUUID(item.ID))
                    e.GetComponent<TransformComponent>() = item.StartLocal;
        End();
    }

    void GroupTransformEdit::End()
    {
        m_Scene = nullptr;
        m_Items.clear();
    }

    // ------------------------------------------------------------------ status bar summary
    static size_t CountDescendants(Scene& scene, Entity entity, int depth = 0)
    {
        if (depth > 256) return 0; // broken (cyclic) hierarchies
        size_t count = 0;
        for (UUID child : entity.GetComponent<RelationshipComponent>().Children)
            if (Entity c = scene.GetEntityByUUID(child))
                count += 1 + CountDescendants(scene, c, depth + 1);
        return count;
    }

    SelectionSummary SummarizeSelection(Scene& scene, const EntitySelection& selection)
    {
        SelectionSummary s;
        std::vector<Entity> entities;
        for (Entity e : selection.GetEntities())
            if (e && e.GetScene() == &scene)
                entities.push_back(e);
        Entity primary = selection.GetPrimary();
        if (entities.empty() || !primary || primary.GetScene() != &scene)
            return s;

        s.Count = entities.size();
        s.Primary = primary;
        s.Name = primary.GetName();
        s.ID = (uint64_t)primary.GetUUID();
        s.Path = s.Name;
        int depth = 0;
        for (Entity parent = scene.GetParent(primary); parent && depth < 256; parent = scene.GetParent(parent), depth++)
            s.Path = parent.GetName() + "/" + s.Path;
        s.Position = scene.GetWorldPosition(primary);
        s.Components = ComponentRegistry::GetOrderedKeys(primary);
        s.ChildCount = primary.GetComponent<RelationshipComponent>().Children.size();
        s.DescendantCount = CountDescendants(scene, primary);
        if (primary.HasComponent<TagsComponent>())
            s.Tags = primary.GetComponent<TagsComponent>().Tags;
        s.Hidden = scene.IsHiddenInEditor(primary);
        s.Locked = scene.IsLockedInEditor(primary);

        glm::vec3 sum(0.0f);
        for (Entity e : entities)
        {
            sum += scene.GetWorldPosition(e);
            if (scene.IsHiddenInEditor(e)) s.HiddenCount++;
            if (scene.IsLockedInEditor(e)) s.LockedCount++;
        }
        s.Center = sum / (float)entities.size();
        return s;
    }

}
