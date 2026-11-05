#include "Zamin/Scene/ComponentRegistry.h"

#include <algorithm>

namespace Zamin {

    template<typename T>
    static ComponentTypeInfo MakeTypeInfo()
    {
        return { ComponentKey<T>(),
                 [](Entity e) { return e.HasComponent<T>(); },
                 [](Entity e) { e.RemoveComponent<T>(); },
                 [](Entity from, Entity to) { to.AddOrReplaceComponent<T>(from.GetComponent<T>()); } };
    }

    const std::vector<ComponentTypeInfo>& ComponentRegistry::GetTypes()
    {
        // The order the inspector draws them in by default.
        static const std::vector<ComponentTypeInfo> s_Types = {
            MakeTypeInfo<TransformComponent>(),
            MakeTypeInfo<TagsComponent>(),
            MakeTypeInfo<CameraComponent>(),
            MakeTypeInfo<SpriteRendererComponent>(),
            MakeTypeInfo<SpriteAnimationComponent>(),
            MakeTypeInfo<CircleRendererComponent>(),
            MakeTypeInfo<TextComponent>(),
            MakeTypeInfo<MeshRendererComponent>(),
            MakeTypeInfo<AnimatorComponent>(),
            MakeTypeInfo<DirectionalLightComponent>(),
            MakeTypeInfo<PointLightComponent>(),
            MakeTypeInfo<SpotLightComponent>(),
            MakeTypeInfo<Rigidbody2DComponent>(),
            MakeTypeInfo<BoxCollider2DComponent>(),
            MakeTypeInfo<CircleCollider2DComponent>(),
            MakeTypeInfo<Rigidbody3DComponent>(),
            MakeTypeInfo<BoxCollider3DComponent>(),
            MakeTypeInfo<SphereCollider3DComponent>(),
            MakeTypeInfo<CapsuleCollider3DComponent>(),
            MakeTypeInfo<CharacterController3DComponent>(),
            MakeTypeInfo<TilemapComponent>(),
            MakeTypeInfo<ParallaxComponent>(),
            MakeTypeInfo<ParticleSystemComponent>(),
            MakeTypeInfo<UITransformComponent>(),
            MakeTypeInfo<UIButtonComponent>(),
            MakeTypeInfo<AudioSourceComponent>(),
            MakeTypeInfo<NativeScriptComponent>(),
        };
        return s_Types;
    }

    const ComponentTypeInfo* ComponentRegistry::Find(std::string_view key)
    {
        for (const auto& type : GetTypes())
            if (type.Key == key)
                return &type;
        return nullptr;
    }

    bool ComponentRegistry::Has(Entity entity, std::string_view key)
    {
        const ComponentTypeInfo* type = Find(key);
        return type && entity && type->Has(entity);
    }

    bool ComponentRegistry::Remove(Entity entity, std::string_view key)
    {
        const ComponentTypeInfo* type = Find(key);
        if (!type || !entity || key == ComponentKey<TransformComponent>() || !type->Has(entity))
            return false;
        type->Remove(entity);
        return true;
    }

    static bool Contains(const std::vector<std::string>& keys, std::string_view key)
    {
        return std::find(keys.begin(), keys.end(), key) != keys.end();
    }

    std::vector<std::string> ComponentRegistry::SortKeys(const std::vector<std::string>& present, const std::vector<std::string>& order)
    {
        constexpr std::string_view transform = ComponentKey<TransformComponent>();
        std::vector<std::string> sorted;
        sorted.reserve(present.size());
        if (Contains(present, transform))
            sorted.emplace_back(transform);
        for (const auto& key : order)
            if (key != transform && Contains(present, key) && !Contains(sorted, key))
                sorted.push_back(key);
        for (const auto& key : present)
            if (!Contains(sorted, key))
                sorted.push_back(key);
        return sorted;
    }

    std::vector<std::string> ComponentRegistry::GetKeys(Entity entity)
    {
        std::vector<std::string> keys;
        if (!entity)
            return keys;
        for (const auto& type : GetTypes())
            if (type.Has(entity))
                keys.emplace_back(type.Key);
        return keys;
    }

    std::vector<std::string> ComponentRegistry::GetOrderedKeys(Entity entity)
    {
        std::vector<std::string> keys = GetKeys(entity);
        if (const auto* meta = entity ? entity.TryGetComponent<EditorMetadataComponent>() : nullptr; meta && !meta->ComponentOrder.empty())
            return SortKeys(keys, meta->ComponentOrder);
        return keys;
    }

    int ComponentRegistry::GetComponentIndex(Entity entity, std::string_view key)
    {
        std::vector<std::string> keys = GetOrderedKeys(entity);
        auto it = std::find(keys.begin(), keys.end(), key);
        return it == keys.end() ? -1 : (int)(it - keys.begin());
    }

    bool ComponentRegistry::MoveComponent(Entity entity, std::string_view key, int index)
    {
        constexpr std::string_view transform = ComponentKey<TransformComponent>();
        if (!entity || key == transform)
            return false;
        std::vector<std::string> keys = GetOrderedKeys(entity);
        auto it = std::find(keys.begin(), keys.end(), key);
        if (it == keys.end())
            return false;

        const int from = (int)(it - keys.begin());
        const int first = !keys.empty() && keys.front() == transform ? 1 : 0;
        const int last = (int)keys.size() - 1;
        const int to = index < 0 || index > last ? last : std::max(index, first);
        if (to == from)
            return false;
        std::string moved = std::move(*it);
        keys.erase(it);
        keys.insert(keys.begin() + to, std::move(moved));

        std::vector<std::string> order, defaults;
        for (auto& k : keys)
            if (k != transform) order.push_back(k);
        for (auto& k : GetKeys(entity))
            if (k != transform) defaults.push_back(k);

        auto* meta = entity.TryGetComponent<EditorMetadataComponent>();
        if (order == defaults)
        {
            // Back to the default order: nothing to store (the scene file stays as it was).
            if (meta)
            {
                meta->ComponentOrder.clear();
                if (meta->IsDefault())
                    entity.RemoveComponent<EditorMetadataComponent>();
            }
        }
        else
        {
            if (!meta)
                meta = &entity.AddComponent<EditorMetadataComponent>();
            meta->ComponentOrder = std::move(order);
        }
        return true;
    }

}
