#pragma once

/// @file
/// @brief Entity: a handle to one object of a Scene and its components.

#include "Zamin/Core/UUID.h"
#include "Zamin/Scene/Scene.h"
#include "Zamin/Scene/Components.h"

#include <entt/entt.hpp>

namespace Zamin {

    /// Lightweight handle (entt id + owning scene). Cheap to copy.
    ///
    /// Every entity made by Scene::CreateEntity has an IDComponent (its UUID, kept in scene files), a
    /// TagComponent (its name and active flag), a TransformComponent and a RelationshipComponent (its
    /// parent and children). Components are plain structs
    /// (Zamin/Scene/Components.h) stored in the scene's EnTT registry:
    /// @code
    /// Zamin::Entity box = scene.CreateEntity("Box");
    /// box.GetTransform().Translation = { 0.0f, 2.0f, 0.0f };
    /// auto& body = box.AddComponent<Zamin::Rigidbody2DComponent>();
    /// body.Type = Zamin::Rigidbody2DComponent::BodyType::Dynamic;
    /// box.AddComponent<Zamin::BoxCollider2DComponent>();
    /// if (auto* sprite = box.TryGetComponent<Zamin::SpriteRendererComponent>()) sprite->Color = { 1, 0, 0, 1 };
    /// @endcode
    /// A default-constructed Entity (or one whose entity was destroyed) is invalid: `if (entity)`.
    class Entity
    {
    public:
        Entity() = default;   ///< an invalid handle
        Entity(entt::entity handle, Scene* scene) : m_EntityHandle(handle), m_Scene(scene) {}

        /// Adds a component of type T built from `args` and returns it. The entity must not have one
        /// (EnTT asserts in Debug builds); AddOrReplaceComponent otherwise.
        template<typename T, typename... Args>
        T& AddComponent(Args&&... args)
        {
            T& component = m_Scene->m_Registry.emplace<T>(m_EntityHandle, std::forward<Args>(args)...);
            m_Scene->OnComponentAdded<T>(*this, component);
            return component;
        }

        /// Adds T, or replaces the one the entity has (a running script is destroyed first when T is
        /// NativeScriptComponent).
        template<typename T, typename... Args>
        T& AddOrReplaceComponent(Args&&... args)
        {
            if constexpr (std::is_same_v<T, NativeScriptComponent>)
                m_Scene->DestroyScriptInstance(*this); // the running script ends with its component
            T& component = m_Scene->m_Registry.emplace_or_replace<T>(m_EntityHandle, std::forward<Args>(args)...);
            m_Scene->OnComponentAdded<T>(*this, component);
            return component;
        }

        /// The entity's T; it must have one (HasComponent / TryGetComponent when unsure).
        template<typename T>
        T& GetComponent() { return m_Scene->m_Registry.get<T>(m_EntityHandle); }

        /// The entity's T; it must have one.
        template<typename T>
        const T& GetComponent() const { return m_Scene->m_Registry.get<T>(m_EntityHandle); }

        /// The entity's T, nullptr when it has none.
        template<typename T>
        T* TryGetComponent() { return m_Scene->m_Registry.try_get<T>(m_EntityHandle); }

        template<typename T>
        bool HasComponent() const { return m_Scene->m_Registry.all_of<T>(m_EntityHandle); }

        /// Removes the entity's T. A running script (NativeScriptComponent) gets OnDisable / OnDestroy
        /// first, while its component still exists.
        template<typename T>
        void RemoveComponent()
        {
            if constexpr (std::is_same_v<T, NativeScriptComponent>)
            {
                // A running script gets OnDisable / OnDestroy while its component still exists.
                m_Scene->DestroyScriptInstance(*this);
                if (!IsValid())
                    return; // its OnDestroy destroyed the entity
            }
            m_Scene->m_Registry.remove<T>(m_EntityHandle);
        }

        /// It belongs to a scene and its entity still exists there.
        bool IsValid() const { return m_Scene && m_EntityHandle != entt::null && m_Scene->m_Registry.valid(m_EntityHandle); }
        operator bool() const { return IsValid(); }                       ///< IsValid
        operator entt::entity() const { return m_EntityHandle; }          ///< the EnTT handle
        operator uint32_t() const { return (uint32_t)m_EntityHandle; }    ///< the EnTT handle as a number (editor picking)

        /// The entity's persistent ID (IDComponent): stays the same across saves, loads and play mode copies.
        UUID GetUUID() const { return GetComponent<IDComponent>().ID; }
        const std::string& GetName() const { return GetComponent<TagComponent>().Tag; }  ///< TagComponent::Tag
        /// Renames the entity and keeps Scene::FindEntityByName's cache up to date
        /// (writing TagComponent::Tag directly is only noticed on the next frame).
        void SetName(std::string name)
        {
            if (name.empty()) name = "Entity";
            m_Scene->m_Registry.patch<TagComponent>(m_EntityHandle, [&](TagComponent& t) { t.Tag = std::move(name); });
        }

        /// @name Gameplay tags
        /// Gameplay tags (TagsComponent)
        /// @{
        /// The entity has this gameplay tag (TagsComponent).
        bool HasTag(std::string_view tag) const
        {
            const auto* tags = m_Scene->m_Registry.try_get<TagsComponent>(m_EntityHandle);
            return tags && tags->Has(tag);
        }
        /// Adds a TagsComponent when needed. Returns false for empty / duplicate tags. Through the
        /// registry (patch): the scene's tag index follows at once.
        bool AddTag(std::string_view tag)
        {
            if (tag.empty()) return false;
            auto* tags = m_Scene->m_Registry.try_get<TagsComponent>(m_EntityHandle);
            if (tags && tags->Has(tag)) return false;
            if (!tags) AddComponent<TagsComponent>();
            bool added = false;
            m_Scene->m_Registry.patch<TagsComponent>(m_EntityHandle, [&](TagsComponent& t) { added = t.Add(tag); });
            return added;
        }
        /// Returns false when the entity did not have it.
        bool RemoveTag(std::string_view tag)
        {
            auto* tags = m_Scene->m_Registry.try_get<TagsComponent>(m_EntityHandle);
            if (!tags || !tags->Has(tag)) return false;
            bool removed = false;
            m_Scene->m_Registry.patch<TagsComponent>(m_EntityHandle, [&](TagsComponent& t) { removed = t.Remove(tag); });
            return removed;
        }
        /// @}
        /// @name Active state
        /// Active state (see Scene::SetActive)
        /// The entity's own flag; IsActiveInHierarchy also looks at the ancestors.
        /// @{
        bool IsActive() const { return m_Scene && m_Scene->IsActive(*this); }
        void SetActive(bool active) { if (m_Scene) m_Scene->SetActive(*this, active); }
        bool IsActiveInHierarchy() const { return m_Scene && m_Scene->IsActiveInHierarchy(*this); }
        /// @}

        /// The local transform (relative to the parent): Translation, Rotation (Euler radians), Scale.
        TransformComponent& GetTransform() { return GetComponent<TransformComponent>(); }
        Scene* GetScene() const { return m_Scene; }  ///< the scene the entity lives in

        bool operator==(const Entity& other) const { return m_EntityHandle == other.m_EntityHandle && m_Scene == other.m_Scene; }
        bool operator!=(const Entity& other) const { return !(*this == other); }

    private:
        entt::entity m_EntityHandle{ entt::null };
        Scene* m_Scene = nullptr;
    };

}

namespace Zamin {

    template<typename T>
    std::vector<Entity> Scene::FindEntitiesWith()
    {
        std::vector<Entity> result;
        auto view = m_Registry.view<T>();
        result.reserve(view.size());
        for (entt::entity e : view)
            if (m_Registry.all_of<IDComponent>(e))
                result.emplace_back(e, this);
        SortInSceneOrder(result);
        return result;
    }

    template<typename T>
    Entity Scene::FindEntityWith()
    {
        const auto& ids = m_Registry.storage<IDComponent>();
        entt::entity best = entt::null;
        for (entt::entity e : m_Registry.view<T>())
            if (ids.contains(e) && (best == entt::null || ids.index(e) < ids.index(best)))
                best = e;
        return best == entt::null ? Entity{} : Entity{ best, this };
    }

    template<typename T>
    std::vector<T*> Scene::FindObjectsOfType()
    {
        std::vector<T*> result;
        if constexpr (std::is_base_of_v<ScriptableEntity, T>)
        {
            for (Entity e : FindEntitiesWith<NativeScriptComponent>())
                if (T* script = dynamic_cast<T*>(e.GetComponent<NativeScriptComponent>().Instance))
                    result.push_back(script);
        }
        else
        {
            for (Entity e : FindEntitiesWith<T>())
                result.push_back(&e.GetComponent<T>());
        }
        return result;
    }

    template<typename T>
    T* Scene::FindObjectOfType()
    {
        if constexpr (std::is_base_of_v<ScriptableEntity, T>)
        {
            for (Entity e : FindEntitiesWith<NativeScriptComponent>())
                if (T* script = dynamic_cast<T*>(e.GetComponent<NativeScriptComponent>().Instance))
                    return script;
            return nullptr;
        }
        else
        {
            Entity e = FindEntityWith<T>();
            return e ? &e.GetComponent<T>() : nullptr;
        }
    }

    template<typename T>
    inline void Scene::OnComponentAdded(Entity, T& component)
    {
        if constexpr (std::is_same_v<T, CameraComponent>)
        {
            if (m_ViewportWidth > 0 && m_ViewportHeight > 0)
                component.Camera.SetViewportSize(m_ViewportWidth, m_ViewportHeight);
        }
        else
        {
            (void)component;
        }
    }

}
