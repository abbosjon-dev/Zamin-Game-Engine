#pragma once

#include "Zamin/Scene/Components.h"
#include "Zamin/Scene/Entity.h"

#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace Zamin {

    // Names of the components in scene files ("Camera", "Rigidbody2D", ...). The inspector's
    // component copy / paste, its component order and its remembered collapsed headers use them.
    template<typename T>
    constexpr std::string_view ComponentKey()
    {
        if constexpr (std::is_same_v<T, TransformComponent>) return "Transform";
        else if constexpr (std::is_same_v<T, TagsComponent>) return "Tags";
        else if constexpr (std::is_same_v<T, CameraComponent>) return "Camera";
        else if constexpr (std::is_same_v<T, SpriteRendererComponent>) return "SpriteRenderer";
        else if constexpr (std::is_same_v<T, SpriteAnimationComponent>) return "SpriteAnimation";
        else if constexpr (std::is_same_v<T, CircleRendererComponent>) return "CircleRenderer";
        else if constexpr (std::is_same_v<T, TextComponent>) return "Text";
        else if constexpr (std::is_same_v<T, MeshRendererComponent>) return "MeshRenderer";
        else if constexpr (std::is_same_v<T, AnimatorComponent>) return "Animator";
        else if constexpr (std::is_same_v<T, DirectionalLightComponent>) return "DirectionalLight";
        else if constexpr (std::is_same_v<T, PointLightComponent>) return "PointLight";
        else if constexpr (std::is_same_v<T, SpotLightComponent>) return "SpotLight";
        else if constexpr (std::is_same_v<T, Light2DComponent>) return "Light2D";
        else if constexpr (std::is_same_v<T, TintAnimationComponent>) return "TintAnimation";
        else if constexpr (std::is_same_v<T, Rigidbody2DComponent>) return "Rigidbody2D";
        else if constexpr (std::is_same_v<T, BoxCollider2DComponent>) return "BoxCollider2D";
        else if constexpr (std::is_same_v<T, CircleCollider2DComponent>) return "CircleCollider2D";
        else if constexpr (std::is_same_v<T, PolygonCollider2DComponent>) return "PolygonCollider2D";
        else if constexpr (std::is_same_v<T, EdgeCollider2DComponent>) return "EdgeCollider2D";
        else if constexpr (std::is_same_v<T, PlatformEffector2DComponent>) return "PlatformEffector2D";
        else if constexpr (std::is_same_v<T, AreaEffector2DComponent>) return "AreaEffector2D";
        else if constexpr (std::is_same_v<T, BuoyancyEffector2DComponent>) return "BuoyancyEffector2D";
        else if constexpr (std::is_same_v<T, DistanceJoint2DComponent>) return "DistanceJoint2D";
        else if constexpr (std::is_same_v<T, RevoluteJoint2DComponent>) return "RevoluteJoint2D";
        else if constexpr (std::is_same_v<T, PrismaticJoint2DComponent>) return "PrismaticJoint2D";
        else if constexpr (std::is_same_v<T, WeldJoint2DComponent>) return "WeldJoint2D";
        else if constexpr (std::is_same_v<T, WheelJoint2DComponent>) return "WheelJoint2D";
        else if constexpr (std::is_same_v<T, Rigidbody3DComponent>) return "Rigidbody3D";
        else if constexpr (std::is_same_v<T, BoxCollider3DComponent>) return "BoxCollider3D";
        else if constexpr (std::is_same_v<T, SphereCollider3DComponent>) return "SphereCollider3D";
        else if constexpr (std::is_same_v<T, CapsuleCollider3DComponent>) return "CapsuleCollider3D";
        else if constexpr (std::is_same_v<T, ConvexHullCollider3DComponent>) return "ConvexHullCollider3D";
        else if constexpr (std::is_same_v<T, MeshCollider3DComponent>) return "MeshCollider3D";
        else if constexpr (std::is_same_v<T, FixedJoint3DComponent>) return "FixedJoint3D";
        else if constexpr (std::is_same_v<T, HingeJoint3DComponent>) return "HingeJoint3D";
        else if constexpr (std::is_same_v<T, DistanceJoint3DComponent>) return "DistanceJoint3D";
        else if constexpr (std::is_same_v<T, SliderJoint3DComponent>) return "SliderJoint3D";
        else if constexpr (std::is_same_v<T, ConeJoint3DComponent>) return "ConeJoint3D";
        else if constexpr (std::is_same_v<T, CharacterController3DComponent>) return "CharacterController3D";
        else if constexpr (std::is_same_v<T, TilemapComponent>) return "Tilemap";
        else if constexpr (std::is_same_v<T, ParallaxComponent>) return "Parallax";
        else if constexpr (std::is_same_v<T, Camera2DFollowComponent>) return "Camera2DFollow";
        else if constexpr (std::is_same_v<T, ParticleSystemComponent>) return "ParticleSystem";
        else if constexpr (std::is_same_v<T, UITransformComponent>) return "UITransform";
        else if constexpr (std::is_same_v<T, UIButtonComponent>) return "UIButton";
        else if constexpr (std::is_same_v<T, AudioSourceComponent>) return "AudioSource";
        else if constexpr (std::is_same_v<T, AudioListenerComponent>) return "AudioListener";
        else if constexpr (std::is_same_v<T, AmbientZoneComponent>) return "AmbientZone";
        else if constexpr (std::is_same_v<T, NativeScriptComponent>) return "NativeScript";
        else static_assert(sizeof(T) == 0, "ComponentKey: this component has no scene-file name");
    }

    struct ComponentTypeInfo
    {
        std::string_view Key;
        bool (*Has)(Entity);
        void (*Remove)(Entity);
        // Adds `from`'s component to `to`, or overwrites the one `to` has (the entities may be in
        // different scenes).
        void (*Copy)(Entity from, Entity to);
    };

    // The components an entity can have one of each, as the editor sees them.
    class ComponentRegistry
    {
    public:
        // Every copyable / reorderable component in the inspector's default order (Transform
        // first). Not listed: IDComponent, TagComponent (the name), RelationshipComponent and
        // EditorMetadataComponent - they belong to the entity itself.
        static const std::vector<ComponentTypeInfo>& GetTypes();
        static const ComponentTypeInfo* Find(std::string_view key);
        static bool Has(Entity entity, std::string_view key);
        // False for unknown keys and for Transform (every entity has one).
        static bool Remove(Entity entity, std::string_view key);

        // ---- inspector order (EditorMetadataComponent::ComponentOrder)
        // `present` (keys in the default order) arranged by `order`: Transform first, then the
        // keys `order` lists in that order, then the rest (added later) in the default order.
        static std::vector<std::string> SortKeys(const std::vector<std::string>& present, const std::vector<std::string>& order);
        // Keys of the entity's components in the default order.
        static std::vector<std::string> GetKeys(Entity entity);
        // Keys of the entity's components in its inspector order.
        static std::vector<std::string> GetOrderedKeys(Entity entity);
        // Moves component `key` to position `index` of the inspector order (negative or past the
        // end = last; Transform cannot move and nothing goes above it). The new order is stored
        // on the entity, or cleared when it is the default one again. False when nothing changed.
        static bool MoveComponent(Entity entity, std::string_view key, int index);
        // Position of `key` in the entity's inspector order (-1 when it has no such component).
        static int GetComponentIndex(Entity entity, std::string_view key);
    };

}
