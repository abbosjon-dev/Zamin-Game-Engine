#include "Zamin/Scene/Scene.h"
#include "Zamin/Asset/AssetImporter.h"
#include "Zamin/Asset/AssetImportLog.h"
#include "Zamin/Asset/AssetRegistry.h"
#include "Zamin/Scene/Entity.h"
#include "Zamin/Scene/Components.h"
#include "Zamin/Scene/SceneSerializer.h"
#include "Zamin/Scene/Parallax.h"
#include "Zamin/Scene/SortingLayers.h"
#include "Zamin/Scene/TilesetProperties.h"
#include "Zamin/Scripting/ScriptableEntity.h"
#include "Zamin/Scripting/ScriptEngine.h"
#include "Zamin/Scripting/EventBus.h"
#include "Zamin/Physics/PhysicsWorld2D.h"
#include "Zamin/Physics/PhysicsMaterial.h"
#include "Zamin/Physics/Polygon2D.h"
#include "Zamin/Physics/PhysicsWorld3D.h"
#include "Zamin/Physics/CollisionShapes3D.h"
#include "Zamin/Renderer/Renderer2D.h"
#include "Zamin/Renderer/Renderer3D.h"
#include "Zamin/Renderer/RenderCommand.h"
#include "Zamin/Renderer/Renderer.h"
#include "Zamin/Project/AssetFiles.h"
#include "Zamin/Project/Project.h"
#include "Zamin/Audio/AudioEngine.h"
#include "Zamin/Math/Math.h"
#include "Zamin/Utils/FileSystem.h"
#include "Zamin/Core/Profiler.h"
#include "Zamin/Core/SaveData.h"
#include "Zamin/Debug/DebugDraw.h"

#include <algorithm>
#include <functional>
#include <limits>
#include <unordered_set>

namespace Zamin {

    // ------------------------------------------------------------------ copy helpers
    template<typename... Component>
    static void CopyComponent(entt::registry& dst, entt::registry& src, const std::unordered_map<UUID, entt::entity>& enttMap)
    {
        ([&]() {
            auto view = src.view<Component>();
            for (auto srcEntity : view)
            {
                UUID uuid = src.get<IDComponent>(srcEntity).ID;
                auto it = enttMap.find(uuid);
                if (it == enttMap.end()) continue;
                dst.emplace_or_replace<Component>(it->second, view.template get<Component>(srcEntity));
            }
        }(), ...);
    }

    template<typename... Component>
    static void CopyComponent(ComponentGroup<Component...>, entt::registry& dst, entt::registry& src,
                              const std::unordered_map<UUID, entt::entity>& enttMap)
    {
        CopyComponent<Component...>(dst, src, enttMap);
    }

    template<typename... Component>
    static void CopyComponentIfExists(Entity dst, Entity src)
    {
        ([&]() {
            if (src.HasComponent<Component>())
                dst.AddOrReplaceComponent<Component>(src.GetComponent<Component>());
        }(), ...);
    }

    template<typename... Component>
    static void CopyComponentIfExists(ComponentGroup<Component...>, Entity dst, Entity src)
    {
        CopyComponentIfExists<Component...>(dst, src);
    }

    // ------------------------------------------------------------------ 2D joint helpers
    // Calls fn(std::type_identity<T>{}) for every 2D joint component type.
    template<typename... T, typename Fn>
    static void ForEachJoint2DType(ComponentGroup<T...>, Fn&& fn)
    {
        (fn(std::type_identity<T>{}), ...);
    }

    template<typename Fn>
    static void ForEachJoint2DType(Fn&& fn)
    {
        ForEachJoint2DType(Joint2DComponents{}, std::forward<Fn>(fn));
    }

    template<typename T>
    static constexpr Joint2DType Joint2DTypeOf()
    {
        if constexpr (std::is_same_v<T, DistanceJoint2DComponent>) return Joint2DType::Distance;
        else if constexpr (std::is_same_v<T, RevoluteJoint2DComponent>) return Joint2DType::Revolute;
        else if constexpr (std::is_same_v<T, PrismaticJoint2DComponent>) return Joint2DType::Prismatic;
        else if constexpr (std::is_same_v<T, WeldJoint2DComponent>) return Joint2DType::Weld;
        else return Joint2DType::Wheel;
    }

    static const char* Joint2DTypeName(Joint2DType type)
    {
        switch (type)
        {
            case Joint2DType::Distance: return "Distance Joint 2D";
            case Joint2DType::Revolute: return "Revolute Joint 2D";
            case Joint2DType::Prismatic: return "Prismatic Joint 2D";
            case Joint2DType::Weld: return "Weld Joint 2D";
            case Joint2DType::Wheel: return "Wheel Joint 2D";
        }
        return "Joint 2D";
    }

    template<typename T>
    static void ResetJointRuntime(T& joint)
    {
        joint.ResetRuntime();
        if constexpr (std::is_same_v<T, DistanceJoint2DComponent>)
            joint.CurrentDistance = 0.0f;
        else if constexpr (std::is_same_v<T, RevoluteJoint2DComponent>)
            joint.JointAngle = joint.JointSpeed = 0.0f;
        else if constexpr (std::is_same_v<T, PrismaticJoint2DComponent> || std::is_same_v<T, WheelJoint2DComponent>)
            joint.JointTranslation = joint.JointSpeed = 0.0f;
    }

    // The settings of a joint component for the physics world (bodies not filled in).
    template<typename T>
    static PhysicsJoint2DDesc MakeJoint2DDesc(const T& c)
    {
        PhysicsJoint2DDesc d;
        d.Type = Joint2DTypeOf<T>();
        d.Anchor = c.Anchor;
        d.ConnectedAnchor = c.ConnectedAnchor;
        d.AutoConfigureConnectedAnchor = c.AutoConfigureConnectedAnchor;
        d.CollideConnected = c.CollideConnected;
        if constexpr (std::is_same_v<T, DistanceJoint2DComponent>)
        {
            d.Distance = c.Distance;
            d.AutoConfigureDistance = c.AutoConfigureDistance;
            d.MaxDistanceOnly = c.MaxDistanceOnly;
            d.Frequency = c.Frequency;
            d.DampingRatio = c.DampingRatio;
        }
        else if constexpr (std::is_same_v<T, RevoluteJoint2DComponent>)
        {
            d.EnableLimit = c.EnableLimit;
            d.LowerLimit = c.LowerAngle;
            d.UpperLimit = c.UpperAngle;
            d.EnableMotor = c.EnableMotor;
            d.MotorSpeed = c.MotorSpeed;
            d.MaxMotorForce = c.MaxMotorTorque;
        }
        else if constexpr (std::is_same_v<T, PrismaticJoint2DComponent>)
        {
            d.Axis = c.Axis;
            d.EnableLimit = c.EnableLimit;
            d.LowerLimit = c.LowerTranslation;
            d.UpperLimit = c.UpperTranslation;
            d.EnableMotor = c.EnableMotor;
            d.MotorSpeed = c.MotorSpeed;
            d.MaxMotorForce = c.MaxMotorForce;
        }
        else if constexpr (std::is_same_v<T, WeldJoint2DComponent>)
        {
            d.Frequency = c.Frequency;
            d.DampingRatio = c.DampingRatio;
        }
        else
        {
            d.Axis = c.Axis;
            d.Frequency = c.Frequency;
            d.DampingRatio = c.DampingRatio;
            d.EnableLimit = c.EnableLimit;
            d.LowerLimit = c.LowerTranslation;
            d.UpperLimit = c.UpperTranslation;
            d.EnableMotor = c.EnableMotor;
            d.MotorSpeed = c.MotorSpeed;
            d.MaxMotorForce = c.MaxMotorTorque;
        }
        return d;
    }

    // ------------------------------------------------------------------ 3D joint helpers
    // Calls fn(std::type_identity<T>{}) for every 3D joint component type.
    template<typename... T, typename Fn>
    static void ForEachJoint3DType(ComponentGroup<T...>, Fn&& fn)
    {
        (fn(std::type_identity<T>{}), ...);
    }

    template<typename Fn>
    static void ForEachJoint3DType(Fn&& fn)
    {
        ForEachJoint3DType(Joint3DComponents{}, std::forward<Fn>(fn));
    }

    template<typename T>
    static constexpr Joint3DType Joint3DTypeOf()
    {
        if constexpr (std::is_same_v<T, FixedJoint3DComponent>) return Joint3DType::Fixed;
        else if constexpr (std::is_same_v<T, HingeJoint3DComponent>) return Joint3DType::Hinge;
        else if constexpr (std::is_same_v<T, DistanceJoint3DComponent>) return Joint3DType::Distance;
        else if constexpr (std::is_same_v<T, SliderJoint3DComponent>) return Joint3DType::Slider;
        else return Joint3DType::Cone;
    }

    static const char* Joint3DTypeName(Joint3DType type)
    {
        switch (type)
        {
            case Joint3DType::Fixed: return "Fixed Joint 3D";
            case Joint3DType::Hinge: return "Hinge Joint 3D";
            case Joint3DType::Distance: return "Distance Joint 3D";
            case Joint3DType::Slider: return "Slider Joint 3D";
            case Joint3DType::Cone: return "Cone Joint 3D";
        }
        return "Joint 3D";
    }

    template<typename T>
    static void ResetJoint3DRuntime(T& joint)
    {
        joint.ResetRuntime();
        if constexpr (std::is_same_v<T, HingeJoint3DComponent>)
            joint.JointAngle = joint.JointSpeed = 0.0f;
        else if constexpr (std::is_same_v<T, DistanceJoint3DComponent>)
            joint.CurrentDistance = 0.0f;
        else if constexpr (std::is_same_v<T, SliderJoint3DComponent>)
            joint.JointTranslation = joint.JointSpeed = 0.0f;
        else if constexpr (std::is_same_v<T, ConeJoint3DComponent>)
            joint.SwingAngle = joint.TwistAngle = 0.0f;
    }

    // The settings of a 3D joint component for the physics world (bodies, points and axis not filled in).
    template<typename T>
    static PhysicsJoint3DDesc MakeJoint3DDesc(const T& c)
    {
        PhysicsJoint3DDesc d;
        d.Type = Joint3DTypeOf<T>();
        d.CollideConnected = c.CollideConnected;
        if constexpr (std::is_same_v<T, HingeJoint3DComponent>)
        {
            d.EnableLimit = c.EnableLimit;
            d.LowerLimit = c.LowerAngle;
            d.UpperLimit = c.UpperAngle;
            d.EnableMotor = c.EnableMotor;
            d.MotorSpeed = c.MotorSpeed;
            d.MaxMotorForce = c.MaxMotorTorque;
            d.EnableSpring = c.EnableSpring;
            d.SpringTarget = c.SpringTargetAngle;
            d.Frequency = c.SpringFrequency;
            d.DampingRatio = c.SpringDamping;
        }
        else if constexpr (std::is_same_v<T, DistanceJoint3DComponent>)
        {
            d.MinDistance = c.MaxDistanceOnly ? 0.0f : c.MinDistance;
            d.MaxDistance = c.MaxDistance;
            d.Frequency = c.Frequency;
            d.DampingRatio = c.DampingRatio;
        }
        else if constexpr (std::is_same_v<T, SliderJoint3DComponent>)
        {
            d.EnableLimit = c.EnableLimit;
            d.LowerLimit = c.LowerTranslation;
            d.UpperLimit = c.UpperTranslation;
            d.EnableMotor = c.EnableMotor;
            d.MotorSpeed = c.MotorSpeed;
            d.MaxMotorForce = c.MaxMotorForce;
        }
        else if constexpr (std::is_same_v<T, ConeJoint3DComponent>)
        {
            d.EnableLimit = true;
            d.LowerLimit = c.TwistLower;
            d.UpperLimit = c.TwistUpper;
            d.SwingLimit = c.SwingLimit;
            d.FrictionTorque = c.FrictionTorque;
        }
        return d;
    }

    // ------------------------------------------------------------------ 2D collider shapes
    // Which component of an entity a fixture came from (PhysicsWorld2D::SetShapeMaterial).
    enum : int { ShapeTagBox = 1, ShapeTagCircle, ShapeTagPolygon, ShapeTagEdge, ShapeTagTilemap };
    // The 3D colliders of a body (PhysicsBody3DDesc::Shape::Tag): an entity has one of each kind at most.
    enum : int { ShapeTagBox3D = 1, ShapeTagSphere3D, ShapeTagCapsule3D, ShapeTagConvexHull3D, ShapeTagMesh3D };

    // A 3D collider's surface: its physics material asset's values, else its own (with Jolt's bounce
    // threshold).
    template<typename C>
    static PhysicsSurface3D ColliderSurface3D(const C& c)
    {
        PhysicsMaterialData own;
        own.Friction = c.Friction;
        own.Restitution = c.Restitution;
        own.RestitutionThreshold = PhysicsSurface3D::DefaultRestitutionThreshold;
        return PhysicsSurface3D::FromMaterial(PhysicsMaterialLibrary::Resolve(c.MaterialPath, own));
    }

    static void ApplySurface(PhysicsShape2DDesc& shape, const PhysicsMaterialData& surface)
    {
        shape.Density = surface.Density;
        shape.Friction = surface.Friction;
        shape.Restitution = surface.Restitution;
        shape.RestitutionThreshold = surface.RestitutionThreshold;
        shape.FrictionCombine = surface.FrictionCombine;
        shape.RestitutionCombine = surface.RestitutionCombine;
    }

    static PhysicsPlatform2D MakePlatform(const PlatformEffector2DComponent& e)
    {
        PhysicsPlatform2D p;
        p.Enabled = true;
        p.OneWay = e.OneWay;
        p.SurfaceArc = std::clamp(e.SurfaceArc, 0.0f, 360.0f);
        p.RotationOffset = e.RotationOffset;
        p.UseSideFriction = e.UseSideFriction;
        p.UseSideBounce = e.UseSideBounce;
        p.SideArc = std::clamp(e.SideArc, 0.0f, 180.0f);
        return p;
    }

    // A collider's own surface values (used without a material asset).
    template<typename C>
    static PhysicsMaterialData ColliderOwnSurface(const C& c)
    {
        PhysicsMaterialData d;
        d.Friction = c.Friction;
        d.Restitution = c.Restitution;
        d.RestitutionThreshold = c.RestitutionThreshold;
        d.Density = c.Density;
        return d;
    }

    // What every collider shape shares: its surface (material asset or own values), sensor flag,
    // collision layer (its own, else the body's) and the entity's platform effector.
    template<typename C>
    static PhysicsShape2DDesc MakeColliderShape(const C& c, int bodyLayer, const PlatformEffector2DComponent* effector, int tag)
    {
        PhysicsShape2DDesc shape;
        ApplySurface(shape, PhysicsMaterialLibrary::Resolve(c.MaterialPath, ColliderOwnSurface(c)));
        shape.IsSensor = c.IsSensor;
        const CollisionLayers2D& layers = Project::GetCollisionLayers2D();
        const int layer = c.Layer >= 0 ? c.Layer : bodyLayer;
        shape.CategoryBits = layers.GetCategoryBits(layer);
        shape.MaskBits = layers.GetMaskBits(layer);
        shape.Tag = tag;
        if (effector && !c.IsSensor)
            shape.Platform = MakePlatform(*effector);
        return shape;
    }

    // The 2D body an entity has in the running world: its Rigidbody 2D's, else its tilemap's.
    static b2Body* GetBody2D(Entity entity, bool tilemap)
    {
        if (auto* rb = entity.TryGetComponent<Rigidbody2DComponent>(); rb && rb->RuntimeBody)
            return (b2Body*)rb->RuntimeBody;
        if (tilemap)
            if (auto* tm = entity.TryGetComponent<TilemapComponent>(); tm && tm->RuntimeBody)
                return (b2Body*)tm->RuntimeBody;
        return nullptr;
    }

    static void ResetRuntimeData(entt::registry& registry)
    {
        ForEachJoint2DType([&](auto type) {
            using T = typename decltype(type)::type;
            for (auto e : registry.view<T>())
                ResetJointRuntime(registry.get<T>(e));
        });
        ForEachJoint3DType([&](auto type) {
            using T = typename decltype(type)::type;
            for (auto e : registry.view<T>())
                ResetJoint3DRuntime(registry.get<T>(e));
        });
        for (auto e : registry.view<Rigidbody2DComponent>()) registry.get<Rigidbody2DComponent>(e).RuntimeBody = nullptr;
        for (auto e : registry.view<BoxCollider2DComponent>()) registry.get<BoxCollider2DComponent>(e).RuntimeFixture = nullptr;
        for (auto e : registry.view<CircleCollider2DComponent>()) registry.get<CircleCollider2DComponent>(e).RuntimeFixture = nullptr;
        for (auto e : registry.view<PolygonCollider2DComponent>())
        {
            auto& c = registry.get<PolygonCollider2DComponent>(e);
            c.RuntimeFixture = nullptr;
            c.RuntimePieces = 0;
        }
        for (auto e : registry.view<EdgeCollider2DComponent>()) registry.get<EdgeCollider2DComponent>(e).RuntimeFixture = nullptr;
        for (auto e : registry.view<AreaEffector2DComponent>()) registry.get<AreaEffector2DComponent>(e).AffectedBodies = 0;
        for (auto e : registry.view<BuoyancyEffector2DComponent>())
        {
            auto& c = registry.get<BuoyancyEffector2DComponent>(e);
            c.AffectedBodies = 0;
            c.SubmergedArea = 0.0f;
        }
        for (auto e : registry.view<Rigidbody3DComponent>()) registry.get<Rigidbody3DComponent>(e).RuntimeBodyID = 0xffffffff;
        for (auto e : registry.view<CharacterController3DComponent>())
        {
            auto& c = registry.get<CharacterController3DComponent>(e);
            c.RuntimeID = 0xffffffff;
            c.Grounded = false;
            c.Velocity = glm::vec3(0.0f);
        }
        for (auto e : registry.view<AudioSourceComponent>()) registry.get<AudioSourceComponent>(e).ResetRuntime();
        for (auto e : registry.view<AudioListenerComponent>()) registry.get<AudioListenerComponent>(e).ResetRuntime();
        for (auto e : registry.view<AmbientZoneComponent>()) registry.get<AmbientZoneComponent>(e).ResetRuntime();
        for (auto e : registry.view<SpriteAnimationComponent>()) registry.get<SpriteAnimationComponent>(e).Time = 0.0f;
        for (auto e : registry.view<SpriteRendererComponent>()) registry.get<SpriteRendererComponent>(e).StopFlash();
        for (auto e : registry.view<TintAnimationComponent>()) registry.get<TintAnimationComponent>(e).ResetRuntime();
        for (auto e : registry.view<AnimationPlayerComponent>()) registry.get<AnimationPlayerComponent>(e).ResetRuntime();
        for (auto e : registry.view<AnimatorComponent>())
        {
            auto& c = registry.get<AnimatorComponent>(e);
            c.EventsAtStart = true;
            c.RootMotionVelocity = glm::vec3(0.0f);
            c.RootMotionTurnRate = 0.0f;
        }
        for (auto e : registry.view<CameraComponent>()) registry.get<CameraComponent>(e).Shake.ResetRuntime();
        for (auto e : registry.view<Camera2DFollowComponent>()) registry.get<Camera2DFollowComponent>(e).ResetRuntime();
        for (auto e : registry.view<ParticleSystemComponent>()) registry.get<ParticleSystemComponent>(e).Clear();
        for (auto e : registry.view<MeshRendererComponent>()) Scene::ClearMaterialInstances(registry.get<MeshRendererComponent>(e));
        for (auto e : registry.view<UIButtonComponent>())
        {
            auto& b = registry.get<UIButtonComponent>(e);
            b.Hovered = b.Pressed = b.Clicked = false;
        }
        for (auto e : registry.view<UISliderComponent>())
        {
            auto& c = registry.get<UISliderComponent>(e);
            c.Hovered = c.Dragging = c.ValueChanged = false;
        }
        for (auto e : registry.view<UIToggleComponent>())
        {
            auto& c = registry.get<UIToggleComponent>(e);
            c.Hovered = c.Pressed = c.ValueChanged = false;
            c.Transition = c.IsOn ? 1.0f : 0.0f;
        }
        for (auto e : registry.view<UIProgressBarComponent>())
        {
            auto& c = registry.get<UIProgressBarComponent>(e);
            c.DisplayValue = std::clamp(c.Value, 0.0f, 1.0f);
        }
        for (auto e : registry.view<UIInputFieldComponent>()) registry.get<UIInputFieldComponent>(e).ResetRuntime();
        for (auto e : registry.view<UINavigationComponent>()) registry.get<UINavigationComponent>(e).WasActive = false;
        for (auto e : registry.view<UIAnimationComponent>()) registry.get<UIAnimationComponent>(e).ResetRuntime();
        for (auto e : registry.view<UICanvasComponent>()) registry.get<UICanvasComponent>(e).CurrentScale = 1.0f;
        for (auto e : registry.view<UIScrollViewComponent>()) registry.get<UIScrollViewComponent>(e).ResetRuntime();
    }

    static void ResetRuntimeData(Entity e)
    {
        ForEachJoint2DType([&](auto type) {
            using T = typename decltype(type)::type;
            if (auto* c = e.TryGetComponent<T>())
                ResetJointRuntime(*c);
        });
        ForEachJoint3DType([&](auto type) {
            using T = typename decltype(type)::type;
            if (auto* c = e.TryGetComponent<T>())
                ResetJoint3DRuntime(*c);
        });
        if (auto* c = e.TryGetComponent<Rigidbody2DComponent>()) c->RuntimeBody = nullptr;
        if (auto* c = e.TryGetComponent<BoxCollider2DComponent>()) c->RuntimeFixture = nullptr;
        if (auto* c = e.TryGetComponent<CircleCollider2DComponent>()) c->RuntimeFixture = nullptr;
        if (auto* c = e.TryGetComponent<PolygonCollider2DComponent>()) { c->RuntimeFixture = nullptr; c->RuntimePieces = 0; }
        if (auto* c = e.TryGetComponent<EdgeCollider2DComponent>()) c->RuntimeFixture = nullptr;
        if (auto* c = e.TryGetComponent<AreaEffector2DComponent>()) c->AffectedBodies = 0;
        if (auto* c = e.TryGetComponent<BuoyancyEffector2DComponent>()) { c->AffectedBodies = 0; c->SubmergedArea = 0.0f; }
        if (auto* c = e.TryGetComponent<Rigidbody3DComponent>()) c->RuntimeBodyID = 0xffffffff;
        if (auto* c = e.TryGetComponent<AudioSourceComponent>()) c->ResetRuntime();
        if (auto* c = e.TryGetComponent<AudioListenerComponent>()) c->ResetRuntime();
        if (auto* c = e.TryGetComponent<AmbientZoneComponent>()) c->ResetRuntime();
        if (auto* c = e.TryGetComponent<SpriteAnimationComponent>()) c->Time = 0.0f;
        if (auto* c = e.TryGetComponent<SpriteRendererComponent>()) c->StopFlash();
        if (auto* c = e.TryGetComponent<TintAnimationComponent>()) c->ResetRuntime();
        if (auto* c = e.TryGetComponent<AnimationPlayerComponent>()) c->ResetRuntime();
        if (auto* c = e.TryGetComponent<AnimatorComponent>()) { c->EventsAtStart = true; c->RootMotionVelocity = glm::vec3(0.0f); c->RootMotionTurnRate = 0.0f; }
        if (auto* c = e.TryGetComponent<TilemapComponent>()) c->RuntimeBody = nullptr;
        if (auto* c = e.TryGetComponent<CameraComponent>()) c->Shake.ResetRuntime();
        if (auto* c = e.TryGetComponent<Camera2DFollowComponent>()) c->ResetRuntime();
        if (auto* c = e.TryGetComponent<CharacterController3DComponent>()) { c->RuntimeID = 0xffffffff; c->Grounded = false; c->Velocity = glm::vec3(0.0f); }
        if (auto* c = e.TryGetComponent<ParticleSystemComponent>()) { c->Clear(); c->RandomState ^= (uint32_t)(uint64_t)e.GetUUID(); }
        if (auto* c = e.TryGetComponent<UIButtonComponent>()) c->Hovered = c->Pressed = c->Clicked = false;
        if (auto* c = e.TryGetComponent<UISliderComponent>()) c->Hovered = c->Dragging = c->ValueChanged = false;
        if (auto* c = e.TryGetComponent<UIToggleComponent>())
        {
            c->Hovered = c->Pressed = c->ValueChanged = false;
            c->Transition = c->IsOn ? 1.0f : 0.0f;
        }
        if (auto* c = e.TryGetComponent<UIProgressBarComponent>()) c->DisplayValue = std::clamp(c->Value, 0.0f, 1.0f);
        if (auto* c = e.TryGetComponent<UIInputFieldComponent>()) c->ResetRuntime();
        if (auto* c = e.TryGetComponent<UINavigationComponent>()) c->WasActive = false; // a spawned menu's SelectOnEnable counts
        if (auto* c = e.TryGetComponent<UIAnimationComponent>()) c->ResetRuntime();      // a spawned panel's show plays
        if (auto* c = e.TryGetComponent<UICanvasComponent>()) c->CurrentScale = 1.0f;
        if (auto* c = e.TryGetComponent<UIScrollViewComponent>()) c->ResetRuntime();
        if (auto* c = e.TryGetComponent<MeshRendererComponent>())
        {
            // A copy (Instantiate) looks like its original did, with material instances of its own.
            if (c->MaterialInstance)
                c->MaterialInstance = c->MaterialInstance->Clone();
            for (Ref<MaterialAsset>& instance : c->SubmeshMaterialInstances)
                if (instance)
                    instance = instance->Clone();
        }
    }

    // ------------------------------------------------------------------ scene
    Scene::Scene()
        : m_EventBus(std::make_unique<EventBus>())
    {
        // Every way of adding / replacing / removing a name goes through these signals
        // (Entity::SetName uses patch -> on_update), so the name cache knows when to rebuild.
        m_Registry.on_construct<TagComponent>().connect<&Scene::OnNameChanged>(*this);
        m_Registry.on_update<TagComponent>().connect<&Scene::OnNameChanged>(*this);
        m_Registry.on_destroy<TagComponent>().connect<&Scene::OnNameChanged>(*this);
        m_Registry.on_destroy<NativeScriptComponent>().connect<&Scene::OnScriptComponentDestroyed>(*this);
        ForEachJoint2DType([&](auto type) {
            using T = typename decltype(type)::type;
            m_Registry.on_destroy<T>().template connect<&Scene::OnJoint2DDestroyed<T>>(*this);
            m_Registry.on_update<T>().template connect<&Scene::OnJoint2DReplaced<T>>(*this);
        });
        ForEachJoint3DType([&](auto type) {
            using T = typename decltype(type)::type;
            m_Registry.on_destroy<T>().template connect<&Scene::OnJoint3DDestroyed<T>>(*this);
            m_Registry.on_update<T>().template connect<&Scene::OnJoint3DReplaced<T>>(*this);
        });
    }

    Scene::~Scene()
    {
        if (m_ScriptsRunning)
            StopScripts();
        OnPhysics2DStop();
        OnPhysics3DStop();
        m_Registry.on_construct<TagComponent>().disconnect(this);
        m_Registry.on_update<TagComponent>().disconnect(this);
        m_Registry.on_destroy<TagComponent>().disconnect(this);
        m_Registry.on_destroy<NativeScriptComponent>().disconnect(this);
        ForEachJoint2DType([&](auto type) {
            using T = typename decltype(type)::type;
            m_Registry.on_destroy<T>().disconnect(this);
            m_Registry.on_update<T>().disconnect(this);
        });
        ForEachJoint3DType([&](auto type) {
            using T = typename decltype(type)::type;
            m_Registry.on_destroy<T>().disconnect(this);
            m_Registry.on_update<T>().disconnect(this);
        });
        DeleteRetiredScripts();
    }

    Ref<Scene> Scene::Copy(const Ref<Scene>& other)
    {
        Ref<Scene> newScene = CreateRef<Scene>();
        newScene->m_ViewportWidth = other->m_ViewportWidth;
        newScene->m_ViewportHeight = other->m_ViewportHeight;
        newScene->m_Settings = other->m_Settings;
        newScene->m_Name = other->m_Name;

        auto& srcRegistry = other->m_Registry;
        auto& dstRegistry = newScene->m_Registry;
        std::unordered_map<UUID, entt::entity> enttMap;

        for (Entity e : other->GetEntitiesInOrder())
        {
            Entity newEntity = newScene->CreateEntityWithUUID(e.GetUUID(), e.GetName());
            newEntity.GetComponent<TagComponent>().Active = e.IsActive();
            enttMap[e.GetUUID()] = (entt::entity)newEntity;
        }

        CopyComponent(AllComponents{}, dstRegistry, srcRegistry, enttMap);
        ResetRuntimeData(dstRegistry);
        return newScene;
    }

    std::vector<Entity> Scene::GetEntitiesInOrder()
    {
        std::vector<Entity> ordered;
        auto view = m_Registry.view<IDComponent>();
        ordered.reserve(view.size());
        for (auto e : view)
            ordered.emplace_back(e, this);
        std::reverse(ordered.begin(), ordered.end()); // entt views iterate newest-first
        return ordered;
    }

    Entity Scene::CreateEntity(const std::string& name) { return CreateEntityWithUUID(UUID(), name); }

    Entity Scene::CreateEntityWithUUID(UUID uuid, const std::string& name)
    {
        Entity entity = { m_Registry.create(), this };
        entity.AddComponent<IDComponent>(uuid);
        entity.AddComponent<TransformComponent>();
        entity.AddComponent<TagComponent>(name.empty() ? "Entity" : name);
        entity.AddComponent<RelationshipComponent>();
        m_EntityMap[uuid] = entity;
        return entity;
    }

    void Scene::DestroyEntity(Entity entity)
    {
        if (!entity || !entity.IsValid()) return;
        std::vector<entt::entity> order;
        std::unordered_set<entt::entity> visited;
        CollectDestroyOrder(entity, order, visited);
        PrepareDestroyOrder(order);
        DestroyEntityRecursive(entity);
    }

    void Scene::CollectDestroyOrder(Entity entity, std::vector<entt::entity>& order, std::unordered_set<entt::entity>& visited)
    {
        // The order DestroyEntityRecursive goes in: children first (in their order), then the entity.
        if (!entity.IsValid() || !m_Registry.all_of<IDComponent>(entity) || !visited.insert((entt::entity)entity).second)
            return;
        if (const auto* rel = entity.TryGetComponent<RelationshipComponent>())
            for (UUID child : rel->Children)
                if (Entity c = GetEntityByUUID(child))
                    CollectDestroyOrder(c, order, visited);
        order.push_back((entt::entity)entity);
    }

    void Scene::PrepareDestroyOrder(const std::vector<entt::entity>& order)
    {
        // The pools remove by swap-and-pop, which would move the last entity into the hole.
        // The entities about to go are moved to the end of the scene order first (the first
        // one to go last), so each removal pops the end and everything else keeps its order.
        // One pass over the tail of the pool for the whole batch, instead of shifting the tail
        // once per destroyed entity.
        auto& ids = m_Registry.storage<IDComponent>();
        size_t count = 0, first = ids.size();
        for (entt::entity e : order)
            if (ids.contains(e))
            {
                first = std::min(first, ids.index(e));
                count++;
            }
        if (count == 0)
            return;
        if (first == ids.size() - count)
        {
            // Already the tail: nothing to do when it is in the right order, too.
            bool inPlace = true;
            for (size_t i = 0, k = ids.size(); i < order.size() && inPlace; i++)
                if (ids.contains(order[i]))
                    inPlace = ids[--k] == order[i];
            if (inPlace)
                return;
        }
        std::unordered_set<entt::entity> doomed;
        doomed.reserve(count);
        for (entt::entity e : order)
            if (ids.contains(e))
                doomed.insert(e);
        std::vector<entt::entity> desired;
        desired.reserve(ids.size() - first);
        for (size_t i = first; i < ids.size(); i++)
            if (!doomed.contains(ids[i]))
                desired.push_back(ids[i]);
        for (auto it = order.rbegin(); it != order.rend(); ++it)
            if (doomed.erase(*it)) // once each
                desired.push_back(*it);
        // Selection by swapping: slot `first + i` takes its entity from further back.
        for (size_t i = 0; i < desired.size(); i++)
            if (ids[first + i] != desired[i])
                ids.swap_elements(ids[first + i], desired[i]);
    }

    void Scene::DestroyEntityRecursive(Entity entity)
    {
        if (!entity) return;

        // Children first (copy the list: it is modified while destroying).
        if (auto* rel = entity.TryGetComponent<RelationshipComponent>())
        {
            std::vector<UUID> children = rel->Children;
            for (UUID child : children)
                if (Entity c = GetEntityByUUID(child))
                    DestroyEntityRecursive(c);
        }
        // The children's OnDisable / OnDestroy (and the survivors' contact-end callbacks) may
        // have destroyed this entity already; removing them also moves components around in
        // the pools, so the relationship is looked up again.
        if (!entity.IsValid()) return;
        if (auto* rel = entity.TryGetComponent<RelationshipComponent>(); rel && (uint64_t)rel->Parent)
            if (Entity parent = GetEntityByUUID(rel->Parent))
                std::erase(parent.GetComponent<RelationshipComponent>().Children, entity.GetUUID());

        EndContactsOf(entity);
        if (!entity.IsValid()) return; // destroyed by a survivor's OnCollisionEnd / OnTriggerEnd
        DestroyScriptInstance(entity);
        if (!entity.IsValid()) return; // destroyed by its own OnDisable / OnDestroy
        DestroyPhysicsBodies(entity);
        if (m_InactiveInHierarchy.contains(entity))
            m_ActiveCacheDirty = true; // keep the cache free of dead handles
        if (auto* audio = entity.TryGetComponent<AudioSourceComponent>(); audio && audio->RuntimeHandle)
            AudioEngine::Stop(audio->RuntimeHandle, audio->FadeOut); // a fade goes on without the entity
        if (auto* zone = entity.TryGetComponent<AmbientZoneComponent>(); zone && zone->RuntimeHandle)
            AudioEngine::Stop(zone->RuntimeHandle, zone->FadeTime * zone->Level);

        m_EntityMap.erase(entity.GetUUID());
        // Normally already the last one (PrepareDestroyOrder); a script callback above may
        // have created or destroyed entities in between, so it is made sure of here.
        PrepareDestroyOrder({ (entt::entity)entity });
        m_Registry.destroy(entity);
    }

    void Scene::DestroyEntityDeferred(Entity entity)
    {
        if (!entity) return;
        UUID id = entity.GetUUID();
        if (std::find(m_DestroyQueue.begin(), m_DestroyQueue.end(), id) == m_DestroyQueue.end())
            m_DestroyQueue.push_back(id);
    }

    void Scene::FlushDestroyQueue()
    {
        auto queue = std::move(m_DestroyQueue);
        m_DestroyQueue.clear();
        if (queue.empty())
            return;
        std::vector<entt::entity> order;
        std::unordered_set<entt::entity> visited;
        for (UUID id : queue)
            if (Entity e = GetEntityByUUID(id))
                CollectDestroyOrder(e, order, visited);
        PrepareDestroyOrder(order);
        for (UUID id : queue)
            if (Entity e = GetEntityByUUID(id))
                DestroyEntityRecursive(e);
    }

    Entity Scene::DuplicateEntityRecursive(Entity entity, UUID newParent)
    {
        Entity newEntity = CreateEntity(entity.GetName());
        CopyComponentIfExists(AllComponents{}, newEntity, entity);
        ResetRuntimeData(newEntity);
        // The copy keeps the active flag (an inactive template gives inactive clones). It is
        // attached to its parent below without SetParent: the cache is rebuilt when anything is
        // inactive (a fully active scene stays fully active).
        newEntity.GetComponent<TagComponent>().Active = entity.IsActive();
        if (!entity.IsActive() || !m_InactiveInHierarchy.empty())
            m_ActiveCacheDirty = true;

        auto& rel = newEntity.GetComponent<RelationshipComponent>();
        rel.Parent = newParent;
        rel.Children.clear();
        std::vector<UUID> children = entity.GetComponent<RelationshipComponent>().Children;
        for (UUID child : children)
            if (Entity c = GetEntityByUUID(child))
            {
                Entity copy = DuplicateEntityRecursive(c, newEntity.GetUUID());
                newEntity.GetComponent<RelationshipComponent>().Children.push_back(copy.GetUUID());
            }
        return newEntity;
    }

    static void CollectSubtree(Scene& scene, Entity root, std::vector<Entity>& out);

    Entity Scene::DuplicateEntity(Entity entity)
    {
        UUID parent = entity.GetComponent<RelationshipComponent>().Parent;
        Entity copy = DuplicateEntityRecursive(entity, parent);
        if ((uint64_t)parent)
            if (Entity p = GetEntityByUUID(parent))
                p.GetComponent<RelationshipComponent>().Children.push_back(copy.GetUUID());

        // Both subtrees are created in the same depth-first order: pair them up so that
        // EntityRef fields pointing inside the original subtree point inside the copy.
        std::vector<Entity> originals, copies;
        CollectSubtree(*this, entity, originals);
        CollectSubtree(*this, copy, copies);
        if (originals.size() == copies.size())
        {
            std::unordered_map<uint64_t, uint64_t> remap;
            for (size_t i = 0; i < originals.size(); i++)
                remap[(uint64_t)originals[i].GetUUID()] = (uint64_t)copies[i].GetUUID();
            RemapEntityReferences(copies, remap);
        }
        return copy;
    }

    void Scene::RemapEntityReferences(const std::vector<Entity>& entities, const std::unordered_map<uint64_t, uint64_t>& oldToNew)
    {
        for (Entity e : entities)
        {
            auto remap = [&](EntityRef& ref) {
                if (auto it = oldToNew.find(ref.ID); ref.ID && it != oldToNew.end())
                    ref.ID = it->second;
            };
            if (auto* follow = e.TryGetComponent<Camera2DFollowComponent>())
                remap(follow->Target);
            if (auto* button = e.TryGetComponent<UIButtonComponent>())
                remap(button->ClickTarget);
            if (auto* scroll = e.TryGetComponent<UIScrollViewComponent>())
            {
                remap(scroll->Content);
                remap(scroll->Viewport);
            }
            if (auto* nav = e.TryGetComponent<UINavigationComponent>())
            {
                remap(nav->Up);
                remap(nav->Down);
                remap(nav->Left);
                remap(nav->Right);
            }
            ForEachJoint2DType([&](auto type) {
                using T = typename decltype(type)::type;
                if (auto* joint = e.TryGetComponent<T>())
                    if (auto it = oldToNew.find(joint->ConnectedBody.ID); joint->ConnectedBody.ID && it != oldToNew.end())
                        joint->ConnectedBody.ID = it->second;
            });
            ForEachJoint3DType([&](auto type) {
                using T = typename decltype(type)::type;
                if (auto* joint = e.TryGetComponent<T>())
                    if (auto it = oldToNew.find(joint->ConnectedBody.ID); joint->ConnectedBody.ID && it != oldToNew.end())
                        joint->ConnectedBody.ID = it->second;
            });
            auto* nsc = e.TryGetComponent<NativeScriptComponent>();
            if (!nsc) continue;
            for (auto& [name, value] : nsc->Fields)
            {
                if (value.Type != ScriptFieldType::Entity) continue;
                EntityRef ref = value.Get<EntityRef>();
                if (auto it = oldToNew.find(ref.ID); ref.ID && it != oldToNew.end())
                    value.Set(EntityRef{ it->second });
            }
        }
    }

    void Scene::OnEntitiesSpawned(const std::vector<Entity>& entities)
    {
        // Inactive ones (an inactive template, or spawned below an inactive parent) get their
        // bodies and OnEnable when they are switched on. They are recorded as switched off first,
        // so a SetActive(true) right after Instantiate() is seen as a change.
        ApplyActiveChanges();
        for (Entity e : entities)
            if (e.IsValid())
            {
                // Tint animations start (PlayOnStart) from their first colour, as at the start of
                // the game: a prefab file's entities arrive with the saved settings only.
                if (auto* tint = e.TryGetComponent<TintAnimationComponent>())
                    tint->ResetRuntime();
                CreatePhysicsBodies(e);
                StartEntityEffects(e); // PlayOnStart sounds of active clones
            }
        SyncJoints2D(); // a spawned ragdoll's joints, once all its bodies exist
        SyncJoints3D();
        if (m_PhysicsWorld || m_PhysicsWorld3D)
            ReportMissingJointBodies(&entities);
        if (m_ScriptsRunning)
        {
            for (Entity e : entities)
                if (e.IsValid() && e.HasComponent<NativeScriptComponent>())
                    CreateScriptInstance(e);
            for (Entity e : entities)
                SyncScriptEnabled(e);
        }
    }

    static void CollectSubtree(Scene& scene, Entity root, std::vector<Entity>& out)
    {
        out.push_back(root);
        for (UUID child : root.GetComponent<RelationshipComponent>().Children)
            if (Entity c = scene.GetEntityByUUID(child))
                CollectSubtree(scene, c, out);
    }

    Entity Scene::Instantiate(Entity prefab) { return InstantiateCopy(prefab, nullptr); }

    Entity Scene::Instantiate(Entity prefab, const glm::vec3& position) { return InstantiateCopy(prefab, &position); }

    Entity Scene::InstantiateCopy(Entity prefab, const glm::vec3* position)
    {
        if (!prefab.IsValid() || prefab.GetScene() != this) return {};
        Entity e = DuplicateEntity(prefab);
        // The copies keep their own active flags. Below a switched-off parent (a template parked
        // in an inactive group) the clone would be switched off as well, whatever the template's
        // own flag says: it becomes a root instead, where the template is in the world - with the
        // group's rotation and scale, also when a position is given (only the position changes).
        if (Entity parent = GetParent(e); parent && !IsActiveInHierarchy(parent))
            SetParent(e, {}, true);
        if (position)
            e.GetComponent<TransformComponent>().Translation = *position;
        std::vector<Entity> spawned;
        CollectSubtree(*this, e, spawned);
        // Spawned copies start visible and unlocked in the editor, as prefab instances do: a
        // template hidden in the editor's view (parked out of sight) does not hide its clones.
        for (Entity s : spawned)
            if (s.HasComponent<EditorMetadataComponent>())
            {
                SetHiddenInEditor(s, false);
                SetLockedInEditor(s, false);
            }
        if (m_IsRunning || m_PhysicsWorld || m_PhysicsWorld3D)
            OnEntitiesSpawned(spawned);
        return e;
    }

    Entity Scene::InstantiatePrefab(const std::filesystem::path& path)
    {
        // A missing / broken prefab is reported once (a script may spawn it every frame).
        if (std::string unreadable = AssetImportLog::CheckReadable(path); !unreadable.empty())
        {
            AssetImportLog::Report(path, unreadable);
            return {};
        }
        std::string text = FileSystem::ReadTextFile(path);
        std::string error;
        std::vector<Entity> entities = SceneSerializer::DeserializeEntities(*this, text, true, nullptr, &error);
        if (entities.empty())
        {
            AssetImportLog::Report(path, error.empty() ? std::string("prefabda obyekt yo'q") : error);
            return {};
        }
        if (!error.empty())
            AssetImportLog::Report(path, error); // read in part
        else
            AssetImportLog::Clear(path);
        ResolveAssets();
        if (m_IsRunning || m_PhysicsWorld || m_PhysicsWorld3D)
            OnEntitiesSpawned(entities);
        return entities.front();
    }

    bool Scene::SavePrefab(Entity root, const std::filesystem::path& path)
    {
        if (!root) return false;
        std::vector<Entity> entities;
        CollectSubtree(*this, root, entities);
        // Joints connected to a body outside the prefab keep the reference: instances spawned in this
        // scene join the same body, in another scene it is missing and the joint is not made.
        std::unordered_set<uint64_t> inside;
        for (Entity e : entities)
            inside.insert((uint64_t)e.GetUUID());
        for (Entity e : entities)
            ForEachJoint2DType([&](auto type) {
                using T = typename decltype(type)::type;
                const auto* joint = e.TryGetComponent<T>();
                if (!joint || !joint->ConnectedBody.ID || inside.count(joint->ConnectedBody.ID))
                    return;
                Entity other = GetEntityByUUID(joint->ConnectedBody.ID);
                ZM_CORE_WARN("Prefab {}: '{}' obyektidagi {} prefabdan tashqaridagi '{}' ga ulangan - boshqa sahnalarda u bo'lmaydi "
                             "va joint yaratilmaydi", path.filename().string(), e.GetName(), Joint2DTypeName(Joint2DTypeOf<T>()),
                             other ? other.GetName() : std::string("?"));
            });
        for (Entity e : entities)
            ForEachJoint3DType([&](auto type) {
                using T = typename decltype(type)::type;
                const auto* joint = e.TryGetComponent<T>();
                if (!joint || !joint->ConnectedBody.ID || inside.count(joint->ConnectedBody.ID))
                    return;
                Entity other = GetEntityByUUID(joint->ConnectedBody.ID);
                ZM_CORE_WARN("Prefab {}: '{}' obyektidagi {} prefabdan tashqaridagi '{}' ga ulangan - boshqa sahnalarda u bo'lmaydi "
                             "va joint yaratilmaydi", path.filename().string(), e.GetName(), Joint3DTypeName(Joint3DTypeOf<T>()),
                             other ? other.GetName() : std::string("?"));
            });
        // Instances should not start hidden / locked in the editor: those flags stay in this scene.
        return FileSystem::WriteTextFile(path, SceneSerializer::SerializeEntities(*this, entities, false));
    }

    // ------------------------------------------------------------------ name cache
    void Scene::RebuildNameCache()
    {
        m_NameCache.clear();
        for (Entity e : GetEntitiesInOrder())
            if (const auto* tag = m_Registry.try_get<TagComponent>(e))
            {
                auto it = m_NameCache.find(std::string_view(tag->Tag));
                if (it == m_NameCache.end())
                    it = m_NameCache.emplace(tag->Tag, std::vector<entt::entity>{}).first;
                it->second.push_back(e);
            }
        m_NameCacheDirty = false;
        m_NameCacheFrame = m_FrameIndex;
        m_NameCacheStats.Rebuilds++;
    }

    bool Scene::IsNameCacheValid()
    {
        size_t cached = 0;
        for (const auto& [name, entities] : m_NameCache)
            for (entt::entity e : entities)
            {
                const auto* tag = m_Registry.valid(e) ? m_Registry.try_get<TagComponent>(e) : nullptr;
                if (!tag || tag->Tag != name)
                    return false;
                cached++;
            }
        return cached == m_Registry.view<TagComponent>().size();
    }

    const std::vector<entt::entity>* Scene::LookupName(std::string_view name)
    {
        // Direct writes to TagComponent::Tag bypass the registry signals: check the
        // whole cache once per frame (cheap compared to scanning on every lookup).
        if (!m_NameCacheDirty && m_NameCacheFrame != m_FrameIndex)
        {
            m_NameCacheFrame = m_FrameIndex;
            m_NameCacheStats.Validations++;
            if (!IsNameCacheValid())
                m_NameCacheDirty = true;
        }
        if (m_NameCacheDirty)
            RebuildNameCache();

        auto it = m_NameCache.find(name);
        if (it == m_NameCache.end())
            return nullptr;
        // Verifying a hit is cheap and catches a direct rename made earlier this frame.
        for (entt::entity e : it->second)
        {
            const auto* tag = m_Registry.valid(e) ? m_Registry.try_get<TagComponent>(e) : nullptr;
            if (!tag || tag->Tag != name)
            {
                RebuildNameCache();
                it = m_NameCache.find(name);
                return it == m_NameCache.end() ? nullptr : &it->second;
            }
        }
        return &it->second;
    }

    Entity Scene::FindEntityByName(std::string_view name)
    {
        const std::vector<entt::entity>* entities = LookupName(name);
        return entities && !entities->empty() ? Entity{ entities->front(), this } : Entity{};
    }

    std::vector<Entity> Scene::FindEntitiesByName(std::string_view name)
    {
        std::vector<Entity> result;
        if (const std::vector<entt::entity>* entities = LookupName(name))
            for (entt::entity e : *entities)
                result.emplace_back(e, this);
        return result;
    }

    // ------------------------------------------------------------------ tags
    void Scene::SortInSceneOrder(std::vector<Entity>& entities)
    {
        // Scene order == position in the IDComponent pool (see GetEntitiesInOrder).
        const auto& ids = m_Registry.storage<IDComponent>();
        std::sort(entities.begin(), entities.end(), [&](const Entity& a, const Entity& b) {
            return ids.index((entt::entity)a) < ids.index((entt::entity)b);
        });
    }

    void Scene::MoveInSceneOrder(Entity entity, Entity before)
    {
        auto& ids = m_Registry.storage<IDComponent>();
        if (!ids.contains(entity) || (before && !ids.contains(before)))
            return;
        size_t from = ids.index(entity);
        size_t to = before ? ids.index(before) : ids.size();
        if (to > from) to--; // the slots after the entity shift down once it is taken out
        if (from == to)
            return;
        // Adjacent swaps: everything else keeps its relative order.
        for (; from < to; from++) ids.swap_elements(ids[from], ids[from + 1]);
        for (; from > to; from--) ids.swap_elements(ids[from], ids[from - 1]);
        // The name lookups list same-named entities in scene order: that order just changed
        // (the cache's own check only notices renames and added / removed entities).
        m_NameCacheDirty = true;
    }

    std::vector<Entity> Scene::FindEntitiesWithTag(std::string_view tag)
    {
        std::vector<Entity> result;
        auto view = m_Registry.view<TagsComponent>();
        for (auto e : view)
            if (view.get<TagsComponent>(e).Has(tag) && m_Registry.all_of<IDComponent>(e))
                result.emplace_back(e, this);
        SortInSceneOrder(result);
        return result;
    }

    Entity Scene::FindEntityWithTag(std::string_view tag)
    {
        const auto& ids = m_Registry.storage<IDComponent>();
        entt::entity best = entt::null;
        auto view = m_Registry.view<TagsComponent>();
        for (auto e : view)
            if (view.get<TagsComponent>(e).Has(tag) && ids.contains(e))
                if (best == entt::null || ids.index(e) < ids.index(best))
                    best = e;
        return best == entt::null ? Entity{} : Entity{ best, this };
    }

    size_t Scene::CountEntitiesWithTag(std::string_view tag)
    {
        size_t n = 0;
        auto view = m_Registry.view<TagsComponent>();
        for (auto e : view)
            if (view.get<TagsComponent>(e).Has(tag))
                n++;
        return n;
    }

    std::vector<std::string> Scene::GetAllTags()
    {
        std::vector<std::string> tags;
        auto view = m_Registry.view<TagsComponent>();
        for (auto e : view)
            for (const auto& t : view.get<TagsComponent>(e).Tags)
                tags.push_back(t);
        std::sort(tags.begin(), tags.end());
        tags.erase(std::unique(tags.begin(), tags.end()), tags.end());
        return tags;
    }

    Entity Scene::GetEntityByUUID(UUID uuid)
    {
        auto it = m_EntityMap.find(uuid);
        if (it != m_EntityMap.end() && m_Registry.valid(it->second))
            return { it->second, this };
        return {};
    }

    Entity Scene::GetPrimaryCameraEntity(bool includeInactive)
    {
        auto view = m_Registry.view<CameraComponent>();
        for (auto entity : view)
            if (view.get<CameraComponent>(entity).Primary && (includeInactive || IsActiveInHierarchy({ entity, this })))
                return Entity{ entity, this };
        return {};
    }

    glm::vec4 Scene::GetClearColor()
    {
        if (Entity cam = GetPrimaryCameraEntity())
            return cam.GetComponent<CameraComponent>().ClearColor;
        return { 0.08f, 0.09f, 0.11f, 1.0f };
    }

    // ------------------------------------------------------------------ hierarchy
    Entity Scene::GetParent(Entity entity)
    {
        if (!entity) return {};
        UUID parent = entity.GetComponent<RelationshipComponent>().Parent;
        return (uint64_t)parent ? GetEntityByUUID(parent) : Entity();
    }

    std::vector<Entity> Scene::GetChildren(Entity entity)
    {
        std::vector<Entity> children;
        if (!entity) return children;
        const auto& ids = entity.GetComponent<RelationshipComponent>().Children;
        children.reserve(ids.size());
        for (UUID id : ids)
            if (Entity c = GetEntityByUUID(id))
                children.push_back(c);
        return children;
    }

    size_t Scene::GetChildCount(Entity entity)
    {
        if (!entity) return 0;
        size_t n = 0;
        for (UUID id : entity.GetComponent<RelationshipComponent>().Children)
            if (GetEntityByUUID(id))
                n++;
        return n;
    }

    Entity Scene::GetChild(Entity entity, size_t index)
    {
        if (!entity) return {};
        for (UUID id : entity.GetComponent<RelationshipComponent>().Children)
            if (Entity c = GetEntityByUUID(id))
                if (index-- == 0)
                    return c;
        return {};
    }

    Entity Scene::FindChild(Entity parent, std::string_view name, bool recursive)
    {
        if (!parent) return {};
        for (UUID id : parent.GetComponent<RelationshipComponent>().Children)
            if (Entity c = GetEntityByUUID(id))
            {
                if (c.GetName() == name)
                    return c;
                if (recursive)
                    if (Entity found = FindChild(c, name, true))
                        return found;
            }
        return {};
    }

    std::vector<Entity> Scene::GetDescendants(Entity entity)
    {
        std::vector<Entity> result;
        if (!entity) return result;
        CollectSubtree(*this, entity, result);
        result.erase(result.begin()); // the entity itself
        return result;
    }

    Entity Scene::GetRoot(Entity entity)
    {
        if (!entity) return {};
        Entity root = entity;
        while (Entity p = GetParent(root))
            root = p;
        return root;
    }

    bool Scene::IsAncestorOf(Entity ancestor, Entity entity)
    {
        Entity p = GetParent(entity);
        while (p)
        {
            if (p == ancestor) return true;
            p = GetParent(p);
        }
        return false;
    }

    // ------------------------------------------------------------------ hierarchy order
    std::vector<Entity> Scene::GetRootEntities()
    {
        std::vector<Entity> roots;
        for (Entity e : GetEntitiesInOrder())
            if (!GetParent(e)) // a dangling parent ID counts as a root (as in the hierarchy panel)
                roots.push_back(e);
        return roots;
    }

    std::vector<Entity> Scene::GetEntitiesInHierarchyOrder()
    {
        std::vector<Entity> result;
        result.reserve(m_Registry.storage<IDComponent>().size());
        for (Entity root : GetRootEntities())
            CollectSubtree(*this, root, result);
        return result;
    }

    int Scene::GetSiblingIndex(Entity entity)
    {
        if (!entity) return -1;
        if (Entity parent = GetParent(entity))
        {
            const auto& children = parent.GetComponent<RelationshipComponent>().Children;
            auto it = std::find(children.begin(), children.end(), entity.GetUUID());
            return it == children.end() ? -1 : (int)(it - children.begin());
        }
        std::vector<Entity> roots = GetRootEntities();
        auto it = std::find(roots.begin(), roots.end(), entity);
        return it == roots.end() ? -1 : (int)(it - roots.begin());
    }

    bool Scene::SetSiblingIndex(Entity entity, int index)
    {
        if (!entity) return false;
        if (Entity parent = GetParent(entity))
        {
            auto& children = parent.GetComponent<RelationshipComponent>().Children;
            auto it = std::find(children.begin(), children.end(), entity.GetUUID());
            if (it == children.end()) return false;
            UUID id = *it;
            children.erase(it);
            size_t pos = index < 0 || (size_t)index > children.size() ? children.size() : (size_t)index;
            children.insert(children.begin() + pos, id);
            return true;
        }

        // Root order is the scene order: the entity goes right before the root that will follow it.
        std::vector<Entity> roots = GetRootEntities();
        auto it = std::find(roots.begin(), roots.end(), entity);
        if (it == roots.end()) return false;
        roots.erase(it);
        size_t pos = index < 0 || (size_t)index > roots.size() ? roots.size() : (size_t)index;
        MoveInSceneOrder(entity, pos < roots.size() ? roots[pos] : Entity());
        return true;
    }

    bool Scene::MoveEntityNextTo(Entity entity, Entity sibling, bool after, bool keepWorldTransform)
    {
        if (!entity || !sibling || entity == sibling) return false;
        Entity parent = GetParent(sibling);
        if (parent && (parent == entity || IsAncestorOf(entity, parent)))
            return false; // the entity would become its own ancestor
        if (GetParent(entity) != parent && !SetParent(entity, parent, keepWorldTransform))
            return false;
        int current = GetSiblingIndex(entity);
        int target = GetSiblingIndex(sibling);
        if (current < 0 || target < 0) return false;
        if (current < target) target--; // the sibling's index once the entity is taken out
        return SetSiblingIndex(entity, after ? target + 1 : target);
    }

    // ------------------------------------------------------------------ multi-entity editing
    std::vector<Entity> Scene::GetTopLevelEntities(const std::vector<Entity>& entities)
    {
        std::unordered_set<entt::entity> wanted;
        for (Entity e : entities)
            if (e.IsValid() && e.GetScene() == this)
                wanted.insert((entt::entity)e);
        std::vector<Entity> result;
        if (wanted.empty()) return result;
        for (Entity e : GetEntitiesInHierarchyOrder())
        {
            if (!wanted.count((entt::entity)e)) continue;
            bool nested = false;
            for (Entity p = GetParent(e); p && !nested; p = GetParent(p))
                nested = wanted.count((entt::entity)p) != 0;
            if (!nested)
                result.push_back(e);
        }
        return result;
    }

    std::vector<Entity> Scene::DuplicateEntities(const std::vector<Entity>& entities)
    {
        std::vector<Entity> tops = GetTopLevelEntities(entities);
        std::vector<Entity> copies, spawned;
        std::unordered_map<uint64_t, uint64_t> remap;
        for (Entity original : tops)
        {
            Entity copy = DuplicateEntity(original);
            MoveEntityNextTo(copy, original, true, false); // DuplicateEntity appends it last
            std::vector<Entity> from, to;
            CollectSubtree(*this, original, from);
            CollectSubtree(*this, copy, to);
            if (from.size() == to.size())
                for (size_t i = 0; i < from.size(); i++)
                    remap[(uint64_t)from[i].GetUUID()] = (uint64_t)to[i].GetUUID();
            spawned.insert(spawned.end(), to.begin(), to.end());
            copies.push_back(copy);
        }
        // DuplicateEntity remaps references inside one subtree; references from one duplicated
        // subtree into another one follow the copies as well.
        if (copies.size() > 1)
            RemapEntityReferences(spawned, remap);
        if (m_IsRunning || m_PhysicsWorld || m_PhysicsWorld3D)
            OnEntitiesSpawned(spawned);
        return copies;
    }

    size_t Scene::DestroyEntities(const std::vector<Entity>& entities)
    {
        // By ID: scripts' OnDisable / OnDestroy may destroy other listed entities first.
        std::vector<UUID> ids;
        std::vector<entt::entity> order;
        std::unordered_set<entt::entity> visited;
        for (Entity e : GetTopLevelEntities(entities))
        {
            ids.push_back(e.GetUUID());
            CollectDestroyOrder(e, order, visited);
        }
        PrepareDestroyOrder(order); // the whole batch in one pass
        size_t destroyed = 0;
        for (UUID id : ids)
            if (Entity e = GetEntityByUUID(id))
            {
                DestroyEntityRecursive(e);
                destroyed++;
            }
        return destroyed;
    }

    // ------------------------------------------------------------------ editor-only flags
    template<typename Flag>
    static bool HasEditorFlag(Scene& scene, Entity entity, bool inherited, Flag flag)
    {
        // Walks up the parents (bounded: a broken file with a parent cycle cannot hang the editor).
        for (int depth = 0; entity && depth < 4096; depth++)
        {
            if (const auto* meta = entity.TryGetComponent<EditorMetadataComponent>(); meta && flag(*meta))
                return true;
            if (!inherited)
                return false;
            entity = scene.GetParent(entity);
        }
        return false;
    }

    template<typename Edit>
    static void EditEditorMetadata(Entity entity, Edit edit)
    {
        auto* meta = entity.TryGetComponent<EditorMetadataComponent>();
        if (!meta)
            meta = &entity.AddComponent<EditorMetadataComponent>();
        edit(*meta);
        if (meta->IsDefault()) // nothing left to remember: the scene file stays as before
            entity.RemoveComponent<EditorMetadataComponent>();
    }

    bool Scene::IsHiddenInEditor(Entity entity, bool inherited)
    {
        return HasEditorFlag(*this, entity, inherited, [](const EditorMetadataComponent& m) { return m.Hidden; });
    }

    bool Scene::IsLockedInEditor(Entity entity, bool inherited)
    {
        return HasEditorFlag(*this, entity, inherited, [](const EditorMetadataComponent& m) { return m.Locked; });
    }

    void Scene::SetHiddenInEditor(Entity entity, bool hidden)
    {
        if (entity && IsHiddenInEditor(entity, false) != hidden)
            EditEditorMetadata(entity, [&](EditorMetadataComponent& m) { m.Hidden = hidden; });
    }

    void Scene::SetLockedInEditor(Entity entity, bool locked)
    {
        if (entity && IsLockedInEditor(entity, false) != locked)
            EditEditorMetadata(entity, [&](EditorMetadataComponent& m) { m.Locked = locked; });
    }

    size_t Scene::CountHiddenInEditor()
    {
        size_t count = 0;
        for (auto e : m_Registry.view<EditorMetadataComponent>())
            count += m_Registry.get<EditorMetadataComponent>(e).Hidden ? 1 : 0;
        return count;
    }

    size_t Scene::CountLockedInEditor()
    {
        size_t count = 0;
        for (auto e : m_Registry.view<EditorMetadataComponent>())
            count += m_Registry.get<EditorMetadataComponent>(e).Locked ? 1 : 0;
        return count;
    }

    template<typename Flag>
    static std::unordered_set<entt::entity> CollectEditorFlagSubtrees(entt::registry& registry,
                                                                     const std::unordered_map<UUID, entt::entity>& entityMap, Flag flag)
    {
        std::unordered_set<entt::entity> result;
        for (auto e : registry.view<EditorMetadataComponent>())
        {
            if (!flag(registry.get<EditorMetadataComponent>(e)) || result.contains(e))
                continue;
            // The subtree, depth-first (children of a flagged entity count too).
            std::vector<entt::entity> stack{ e };
            while (!stack.empty())
            {
                entt::entity current = stack.back();
                stack.pop_back();
                if (!result.insert(current).second)
                    continue;
                for (UUID child : registry.get<RelationshipComponent>(current).Children)
                    if (auto it = entityMap.find(child); it != entityMap.end() && registry.valid(it->second))
                        stack.push_back(it->second);
            }
        }
        return result;
    }

    std::unordered_set<entt::entity> Scene::CollectHiddenInEditor()
    {
        return CollectEditorFlagSubtrees(m_Registry, m_EntityMap, [](const EditorMetadataComponent& m) { return m.Hidden; });
    }

    std::unordered_set<entt::entity> Scene::CollectLockedInEditor()
    {
        return CollectEditorFlagSubtrees(m_Registry, m_EntityMap, [](const EditorMetadataComponent& m) { return m.Locked; });
    }

    // ------------------------------------------------------------------ active state
    void Scene::SetActive(Entity entity, bool active)
    {
        if (!entity.IsValid() || entity.GetScene() != this)
            return;
        auto& tag = entity.GetComponent<TagComponent>();
        if (tag.Active == active)
            return;
        tag.Active = active;
        m_ActiveCacheDirty = true;
        ApplyActiveChanges();
    }

    bool Scene::IsActive(Entity entity)
    {
        return entity.IsValid() && entity.GetComponent<TagComponent>().Active;
    }

    bool Scene::IsActiveInHierarchy(Entity entity)
    {
        if (!entity.IsValid() || entity.GetScene() != this)
            return false;
        EnsureActiveCache();
        return m_InactiveInHierarchy.empty() || !m_InactiveInHierarchy.contains((entt::entity)entity);
    }

    size_t Scene::CountInactiveInHierarchy()
    {
        EnsureActiveCache();
        return m_InactiveInHierarchy.size();
    }

    void Scene::EnsureActiveCache()
    {
        // Once per frame even when nothing was marked: direct writes to TagComponent::Active and
        // raw RelationshipComponent edits bypass SetActive / SetParent.
        if (m_ActiveCacheDirty || m_ActiveCacheFrame != m_FrameIndex)
            RebuildActiveCache();
    }

    void Scene::RebuildActiveCache()
    {
        m_InactiveInHierarchy.clear();
        m_InactiveOrder.clear();
        m_ActiveCacheDirty = false;
        m_ActiveCacheFrame = m_FrameIndex;

        // Entities switched off themselves: usually none, so this is one pass over the flags.
        std::vector<Entity> off;
        for (auto e : m_Registry.view<TagComponent>())
            if (!m_Registry.get<TagComponent>(e).Active && m_Registry.all_of<IDComponent, RelationshipComponent>(e))
                off.emplace_back(e, this);
        if (off.empty())
            return;
        SortInSceneOrder(off);

        for (Entity e : off)
        {
            if (m_InactiveInHierarchy.contains((entt::entity)e))
                continue; // inside a subtree collected already
            // Start from the top-most switched-off ancestor so the subtree comes out parent first
            // (bounded walk: a broken file with a parent cycle cannot hang it).
            Entity top = e;
            int depth = 0;
            for (Entity p = GetParent(e); p && depth < 4096; p = GetParent(p), depth++)
                if (!p.GetComponent<TagComponent>().Active)
                    top = p;
            if (m_InactiveInHierarchy.contains((entt::entity)top))
                continue;
            // Depth-first, children in their order (pushed in reverse).
            std::vector<entt::entity> stack{ (entt::entity)top };
            while (!stack.empty())
            {
                entt::entity current = stack.back();
                stack.pop_back();
                if (!m_InactiveInHierarchy.insert(current).second)
                    continue;
                m_InactiveOrder.push_back(current);
                const auto* rel = m_Registry.try_get<RelationshipComponent>(current);
                if (!rel)
                    continue;
                for (auto it = rel->Children.rbegin(); it != rel->Children.rend(); ++it)
                    if (auto f = m_EntityMap.find(*it); f != m_EntityMap.end() && m_Registry.valid(f->second))
                        stack.push_back(f->second);
            }
        }
    }

    std::unordered_set<entt::entity> Scene::CollectRenderSkipped(const SceneRenderOptions& options)
    {
        EnsureActiveCache();
        std::unordered_set<entt::entity> skipped = m_InactiveInHierarchy;
        if ((options.SkipEditorHidden || options.SkipEditorLocked) && !m_Registry.view<EditorMetadataComponent>().empty())
        {
            if (options.SkipEditorHidden)
                skipped.merge(CollectHiddenInEditor());
            if (options.SkipEditorLocked)
                skipped.merge(CollectLockedInEditor());
        }
        return skipped;
    }

    // ------------------------------------------------------------------ lights
    Scene::LightSet Scene::GatherLights(const glm::vec3& viewPosition, const std::unordered_set<entt::entity>& skipped)
    {
        EnsureActiveCache();
        LightSet set;
        auto counts = [&](entt::entity e) {
            if (skipped.empty() || !skipped.contains(e))
                return true;
            if (m_InactiveInHierarchy.contains(e))
                set.InactiveLights++;
            return false;
        };
        // Directional lights in scene order (as the hierarchy and the scene file list them), not
        // in the pool's order. The sun is the first one with shadows (else the first one).
        for (auto e : m_Registry.view<TransformComponent, DirectionalLightComponent>())
            if (counts(e))
                set.DirectionalLights.push_back(e);
        SortInSceneOrder(set.DirectionalLights);
        set.Sun = PickSun(set.DirectionalLights);
        if (set.DirectionalLights.size() > Renderer3D::MaxDirectionalLights)
        {
            // The sun and the first others in scene order.
            std::vector<entt::entity> kept;
            uint32_t others = 0;
            for (entt::entity e : set.DirectionalLights)
                if (e == set.Sun || others++ < Renderer3D::MaxDirectionalLights - 1)
                    kept.push_back(e);
            set.DroppedDirectionalLights = (uint32_t)(set.DirectionalLights.size() - kept.size());
            set.DirectionalLights = std::move(kept);
        }
        for (auto e : m_Registry.view<TransformComponent, PointLightComponent>())
            if (counts(e))
                set.PointLights.push_back(e);
        for (auto e : m_Registry.view<TransformComponent, SpotLightComponent>())
            if (counts(e))
                set.SpotLights.push_back(e);

        // More than the renderer has slots for: the ones that matter most to the view (strength
        // weighted by how near it is to their range) light the scene.
        if (set.GetLocalLightCount() > Renderer3D::MaxLocalLights)
        {
            std::vector<Renderer3D::LightCandidate> candidates;
            std::vector<entt::entity> all;
            candidates.reserve(set.GetLocalLightCount());
            all.reserve(set.GetLocalLightCount());
            auto strength = [](const glm::vec3& color, float intensity) {
                return std::max(intensity, 0.0f) * std::max({ color.r, color.g, color.b, 0.0f });
            };
            for (entt::entity e : set.PointLights)
            {
                const auto& l = m_Registry.get<PointLightComponent>(e);
                candidates.push_back({ GetWorldPosition({ e, this }), l.Radius, strength(l.Color, l.Intensity) });
                all.push_back(e);
            }
            for (entt::entity e : set.SpotLights)
            {
                const auto& l = m_Registry.get<SpotLightComponent>(e);
                candidates.push_back({ GetWorldPosition({ e, this }), l.Range, strength(l.Color, l.Intensity) });
                all.push_back(e);
            }
            std::vector<size_t> picked = Renderer3D::SelectLights(candidates, viewPosition, Renderer3D::MaxLocalLights);
            set.DroppedLocalLights = (uint32_t)(all.size() - picked.size());
            size_t pointCount = set.PointLights.size();
            set.PointLights.clear();
            set.SpotLights.clear();
            for (size_t index : picked)
                (index < pointCount ? set.PointLights : set.SpotLights).push_back(all[index]);
        }
        return set;
    }

    Scene::LightSet Scene::CollectLights(const glm::vec3& viewPosition, const SceneRenderOptions& options)
    {
        if ((options.SkipEditorHidden || options.SkipEditorLocked) && !m_Registry.view<EditorMetadataComponent>().empty())
            return GatherLights(viewPosition, CollectRenderSkipped(options));
        EnsureActiveCache();
        return GatherLights(viewPosition, m_InactiveInHierarchy);
    }

    Entity Scene::GetSunEntity()
    {
        EnsureActiveCache();
        std::vector<entt::entity> lights;
        for (auto e : m_Registry.view<TransformComponent, DirectionalLightComponent>())
            if (m_InactiveInHierarchy.empty() || !m_InactiveInHierarchy.contains(e))
                lights.push_back(e);
        SortInSceneOrder(lights);
        entt::entity sun = PickSun(lights);
        return sun == entt::null ? Entity() : Entity(sun, this);
    }

    void Scene::SortInSceneOrder(std::vector<entt::entity>& entities)
    {
        const auto& ids = m_Registry.storage<IDComponent>();
        std::erase_if(entities, [&](entt::entity e) { return !ids.contains(e); });
        std::sort(entities.begin(), entities.end(), [&](entt::entity a, entt::entity b) { return ids.index(a) < ids.index(b); });
    }

    entt::entity Scene::PickSun(const std::vector<entt::entity>& directionalLights)
    {
        for (entt::entity e : directionalLights)
            if (m_Registry.get<DirectionalLightComponent>(e).CastShadows)
                return e;
        return directionalLights.empty() ? entt::null : directionalLights.front();
    }

    bool Scene::ShouldScriptBeEnabled(Entity entity, const NativeScriptComponent& nsc)
    {
        return nsc.Enabled && IsActiveInHierarchy(entity);
    }

    void Scene::ApplyActiveChanges()
    {
        if (!m_IsRunning && !m_ScriptsRunning && !m_PhysicsWorld && !m_PhysicsWorld3D)
        {
            m_AppliedInactive.clear(); // edit mode: only drawing / per-frame updates look at the flag
            return;
        }
        EnsureActiveCache();
        if (m_InactiveOrder.empty() && m_AppliedInactive.empty())
            return;

        // Both lists in hierarchy order: a parent before its children, subtrees in scene order.
        std::vector<entt::entity> switchedOff, switchedOn;
        {
            std::unordered_set<entt::entity> applied(m_AppliedInactive.begin(), m_AppliedInactive.end());
            for (entt::entity e : m_InactiveOrder)
                if (!applied.contains(e))
                    switchedOff.push_back(e);
            for (entt::entity e : m_AppliedInactive)
                if (!m_InactiveInHierarchy.contains(e) && m_Registry.valid(e))
                    switchedOn.push_back(e);
        }
        if (switchedOff.empty() && switchedOn.empty())
        {
            m_AppliedInactive = m_InactiveOrder; // drops handles of destroyed entities
            return;
        }
        // Recorded before any callback runs: a script that switches entities from its OnDisable /
        // OnEnable / OnTriggerEnd starts a nested pass from this state. Every step below looks at
        // the entity's current state, so whatever such a nested pass did is not undone here.
        m_AppliedInactive = m_InactiveOrder;
        auto valid = [&](entt::entity e) { return m_Registry.valid(e); };

        // Switched off, in the same order as destroying, one step at a time for the whole batch
        // so every script of a subtree sees the same world: the others hear the contacts end,
        // every script gets OnDisable (parent first), then the bodies leave the worlds and the
        // sounds / particles / button states go.
        for (entt::entity e : switchedOff)
            if (valid(e) && !IsActiveInHierarchy({ e, this }))
                EndContactsOf({ e, this });
        if (m_ScriptsRunning)
            for (entt::entity e : switchedOff)
                if (valid(e))
                    SyncScriptEnabled({ e, this });
        for (entt::entity e : switchedOff)
            if (valid(e) && !IsActiveInHierarchy({ e, this }))
            {
                DestroyPhysicsBodies({ e, this });
                StopEntityEffects({ e, this });
            }

        // Switched on: every body first (at the current transform) and the PlayOnStart sounds,
        // then OnEnable parent first, so a script can already move its children's bodies (or stop
        // their sounds) from its OnEnable.
        for (entt::entity e : switchedOn)
            if (valid(e))
            {
                CreatePhysicsBodies({ e, this }); // nothing when a callback switched it off again
                StartEntityEffects({ e, this });
            }
        if (!switchedOn.empty())
        {
            SyncJoints2D(); // the joints of the bodies that came back
            SyncJoints3D();
        }
        if (m_ScriptsRunning)
            for (entt::entity e : switchedOn)
                if (valid(e))
                    SyncScriptEnabled({ e, this });
    }

    void Scene::StopEntityEffects(Entity entity)
    {
        if (auto* audio = entity.TryGetComponent<AudioSourceComponent>())
        {
            if (audio->RuntimeHandle)
                AudioEngine::Stop(audio->RuntimeHandle, audio->FadeOut); // fades out by itself
            audio->ResetRuntime();
        }
        if (auto* zone = entity.TryGetComponent<AmbientZoneComponent>())
        {
            if (zone->RuntimeHandle)
                AudioEngine::Stop(zone->RuntimeHandle, zone->FadeTime * zone->Level); // from where it is, as it would have faded
            zone->ResetRuntime(); // switched on again: fades in anew
        }
        if (auto* ps = entity.TryGetComponent<ParticleSystemComponent>())
            ps->Clear(); // starts over (Burst included) when the entity comes back
        if (auto* button = entity.TryGetComponent<UIButtonComponent>())
            button->Hovered = button->Pressed = button->Clicked = false;
        if (auto* slider = entity.TryGetComponent<UISliderComponent>())
            slider->Hovered = slider->Dragging = slider->ValueChanged = false;
        if (auto* toggle = entity.TryGetComponent<UIToggleComponent>())
            toggle->Hovered = toggle->Pressed = toggle->ValueChanged = false;
        if (m_FocusedInput == (entt::entity)entity)
            EndInputEdit(nullptr); // editing stops (its text stays)
        if (auto* field = entity.TryGetComponent<UIInputFieldComponent>())
            field->Hovered = field->Selecting = field->TextChanged = field->Submitted = false;
        if (m_PressedUI == (entt::entity)entity)
            m_PressedUI = entt::null; // a press in progress is cancelled, not clicked later
    }

    void Scene::StartEntityEffects(Entity entity)
    {
        if (!m_IsRunning || !IsActiveInHierarchy(entity))
            return;
        if (auto* audio = entity.TryGetComponent<AudioSourceComponent>(); audio && audio->PlayOnStart)
            PlayAudio(entity);
    }

    glm::mat4 Scene::GetWorldTransform(Entity entity)
    {
        glm::mat4 local = entity.GetComponent<TransformComponent>().GetTransform();
        Entity parent = GetParent(entity);
        return parent ? GetWorldTransform(parent) * local : local;
    }

    glm::vec3 Scene::GetWorldPosition(Entity entity) { return glm::vec3(GetWorldTransform(entity)[3]); }

    // ------------------------------------------------------------------ bounds
    static void ExpandBounds(AABB& box, bool& any, const glm::vec3& p)
    {
        box.Min = any ? glm::min(box.Min, p) : p;
        box.Max = any ? glm::max(box.Max, p) : p;
        any = true;
    }

    // The 8 corners of the local box `min..max` through `world`.
    static void ExpandBounds(AABB& box, bool& any, const glm::mat4& world, const glm::vec3& min, const glm::vec3& max)
    {
        for (int i = 0; i < 8; i++)
        {
            glm::vec3 corner((i & 1) ? max.x : min.x, (i & 2) ? max.y : min.y, (i & 4) ? max.z : min.z);
            ExpandBounds(box, any, glm::vec3(world * glm::vec4(corner, 1.0f)));
        }
    }

    // The boxes around what an entity draws, in its own space: mesh / model, sprite, circle, text,
    // tilemap. Returns false when it draws none of these.
    template<typename Fn>
    static bool ForEachLocalBox(Entity entity, Fn&& fn)
    {
        bool any = false;
        auto add = [&](const glm::vec3& min, const glm::vec3& max) {
            fn(min, max);
            any = true;
        };
        if (auto* mr = entity.TryGetComponent<MeshRendererComponent>())
        {
            if (mr->Type == PrimitiveMesh::Model)
            {
                if (mr->ModelAsset)
                    add(mr->ModelAsset->GetBounds().Min, mr->ModelAsset->GetBounds().Max);
            }
            else if (mr->MeshAsset)
                add(mr->MeshAsset->GetBounds().Min, mr->MeshAsset->GetBounds().Max);
            else if (mr->Type == PrimitiveMesh::Plane) // every primitive fits the unit cube
                add({ -0.5f, 0.0f, -0.5f }, { 0.5f, 0.0f, 0.5f });
            else
                add(glm::vec3(-0.5f), glm::vec3(0.5f));
        }
        if (auto* sprite = entity.TryGetComponent<SpriteRendererComponent>())
        {
            // The unit quad - or the rectangle of a sliced / tiled sprite or an off-centre frame.
            const AssetRegistry* registry = Project::GetActive() ? Project::GetAssetRegistry() : nullptr;
            glm::mat4 rect = Scene::GetSpriteRectTransform(*sprite, entity.TryGetComponent<SpriteAnimationComponent>(), registry);
            glm::vec3 c = glm::vec3(rect[3]), h = glm::vec3(rect[0][0], rect[1][1], 0.0f) * 0.5f;
            add(c - glm::abs(h), c + glm::abs(h));
        }
        else if (entity.HasComponent<CircleRendererComponent>())
            add({ -0.5f, -0.5f, 0.0f }, { 0.5f, 0.5f, 0.0f }); // unit quad
        if (auto* text = entity.TryGetComponent<TextComponent>(); text && text->FontAsset)
        {
            // The lines as the text is laid out (see the editor's selection outline).
            const UIRect box = text->LayoutWorld().GetBounds();
            add(glm::vec3(box.Min, 0.0f), glm::vec3(box.Max, 0.0f));
        }
        if (auto* tm = entity.TryGetComponent<TilemapComponent>()) // the position is the bottom-left corner
            add(glm::vec3(0.0f), glm::vec3(tm->Width * tm->TileSize, tm->Height * tm->TileSize, 0.0f));
        return any;
    }

    bool Scene::GetEntityBounds(Entity entity, AABB& out)
    {
        if (!entity.IsValid() || entity.HasComponent<UITransformComponent>())
            return false;
        const glm::mat4 world = GetWorldTransform(entity);
        AABB box;
        bool any = false;
        ForEachLocalBox(entity, [&](const glm::vec3& min, const glm::vec3& max) { ExpandBounds(box, any, world, min, max); });
        if (!any)
            ExpandBounds(box, any, glm::vec3(world[3]));
        out = box;
        return true;
    }

    bool Scene::GetEntityLocalBounds(Entity entity, AABB& out)
    {
        if (!entity.IsValid() || entity.GetScene() != this || entity.HasComponent<UITransformComponent>())
            return false;
        AABB box;
        bool any = false;
        ForEachLocalBox(entity, [&](const glm::vec3& min, const glm::vec3& max) {
            ExpandBounds(box, any, min);
            ExpandBounds(box, any, max);
        });
        if (any)
            out = box;
        return any;
    }

    bool Scene::GetSortingLayer(Entity entity, uint32_t& layer)
    {
        if (!entity || !entity.IsValid())
            return false;
        if (auto* c = entity.TryGetComponent<SpriteRendererComponent>()) { layer = c->SortingLayer; return true; }
        if (auto* c = entity.TryGetComponent<CircleRendererComponent>()) { layer = c->SortingLayer; return true; }
        if (auto* c = entity.TryGetComponent<TextComponent>()) { layer = c->SortingLayer; return true; }
        if (auto* c = entity.TryGetComponent<TilemapComponent>()) { layer = c->SortingLayer; return true; }
        if (auto* c = entity.TryGetComponent<ParticleSystemComponent>()) { layer = c->SortingLayer; return true; }
        return false;
    }

    Entity Scene::GetSortRoot(Entity entity, uint32_t layer)
    {
        Entity root = entity;
        // A broken hierarchy (a cycle) cannot hang the renderer: at most as many steps as entities.
        size_t steps = m_EntityMap.size();
        for (Entity parent = GetParent(root); parent && steps-- > 0; parent = GetParent(parent))
        {
            uint32_t parentLayer = 0;
            if (!GetSortingLayer(parent, parentLayer) || parentLayer != layer)
                break;
            root = parent;
        }
        return root;
    }

    SortKey2D Scene::GetSortKey2D(Entity entity, uint32_t layer, int order, const glm::mat4& view)
    {
        const SortingLayerList& layers = Project::GetSortingLayers();
        const float depth = (view * GetWorldTransform(entity)[3]).z;
        if (!layers.IsYSorted(layer))
            return SortKey2D::Make(layers.GetIndex(layer), order, depth);
        return SortKey2D::Make(layers.GetIndex(layer), order, depth, true, GetWorldPosition(GetSortRoot(entity, layer)).y);
    }

    bool Scene::ComputeBounds(const std::vector<Entity>& entities, AABB& out, bool includeChildren)
    {
        AABB total;
        bool any = false;
        std::unordered_set<entt::entity> visited;
        auto add = [&](Entity e) {
            if (!e.IsValid() || !visited.insert((entt::entity)e).second)
                return;
            AABB box;
            if (!GetEntityBounds(e, box))
                return;
            ExpandBounds(total, any, box.Min);
            ExpandBounds(total, any, box.Max);
        };
        for (Entity e : entities)
        {
            if (!e.IsValid() || e.GetScene() != this)
                continue;
            add(e);
            if (includeChildren)
                for (Entity child : GetDescendants(e))
                    add(child);
        }
        if (any)
            out = total;
        return any;
    }

    // Something the scene view shows in the world (a light, camera or empty group draws nothing).
    static bool HasWorldVisual(Entity e)
    {
        return !e.HasComponent<UITransformComponent>() &&
               (e.HasComponent<MeshRendererComponent>() || e.HasComponent<SpriteRendererComponent>() ||
                e.HasComponent<CircleRendererComponent>() || e.HasComponent<TextComponent>() ||
                e.HasComponent<TilemapComponent>() || e.HasComponent<ParticleSystemComponent>());
    }

    bool Scene::ComputeSceneBounds(AABB& out)
    {
        // What the editor's scene view shows: hidden subtrees (the hierarchy's eye) and
        // inactive ones do not count, and when anything draws, only what draws does - a sun
        // high above the level or the game camera far behind it would push the framed view
        // off the content.
        SceneRenderOptions view;
        view.SkipEditorHidden = true;
        std::unordered_set<entt::entity> hidden = CollectRenderSkipped(view);
        std::vector<Entity> visuals, others;
        for (Entity e : GetEntitiesInOrder())
        {
            if (hidden.contains((entt::entity)e))
                continue;
            (HasWorldVisual(e) ? visuals : others).push_back(e);
        }
        return ComputeBounds(visuals.empty() ? others : visuals, out, false);
    }

    void Scene::SetWorldTransform(Entity entity, const glm::mat4& world)
    {
        Entity parent = GetParent(entity);
        glm::mat4 local = parent ? glm::inverse(GetWorldTransform(parent)) * world : world;
        glm::vec3 t, r, s;
        if (Math::DecomposeTransform(local, t, r, s))
        {
            auto& tc = entity.GetComponent<TransformComponent>();
            tc.Translation = t;
            tc.Rotation = r;
            tc.Scale = s;
        }
    }

    bool Scene::SetParent(Entity child, Entity parent, bool keepWorldTransform)
    {
        if (!child || child == parent) return false;
        if (parent && IsAncestorOf(child, parent)) return false; // would create a cycle

        glm::mat4 world = GetWorldTransform(child);
        auto& rel = child.GetComponent<RelationshipComponent>();
        if ((uint64_t)rel.Parent)
            if (Entity old = GetEntityByUUID(rel.Parent))
                std::erase(old.GetComponent<RelationshipComponent>().Children, child.GetUUID());

        rel.Parent = parent ? parent.GetUUID() : UUID(0);
        if (parent)
            parent.GetComponent<RelationshipComponent>().Children.push_back(child.GetUUID());

        if (keepWorldTransform)
            SetWorldTransform(child, world);
        // Moving below an inactive parent (or out from under one) switches the subtree. In a
        // scene where everything is active nothing can change.
        if (m_ActiveCacheDirty || !m_InactiveInHierarchy.empty())
        {
            m_ActiveCacheDirty = true;
            ApplyActiveChanges();
        }
        return true;
    }

    // ------------------------------------------------------------------ lifecycle
    void Scene::OnRuntimeStart()
    {
        m_IsRunning = true;
        m_Time = 0.0f;
        m_TimeScale = m_TimeScaleBeforePause = 1.0f; // the game's clock runs
        m_UnscaledTime = m_UnscaledDeltaTime = 0.0f;
        m_QuitRequested = false;
        m_AnimationTime = 0.0;
        DebugDraw::Clear(); // the game starts with an empty debug drawing queue
        // Tint animations start (PlayOnStart) from their first colour.
        for (auto e : m_Registry.view<TintAnimationComponent>())
            m_Registry.get<TintAnimationComponent>(e).ResetRuntime();
        ResetUIRuntime(); // nothing pressed or being edited; widgets drawn as they are set
        // Inactive entities start without bodies and without OnEnable.
        EnsureActiveCache();
        m_AppliedInactive = m_InactiveOrder;
        OnPhysics2DStart();
        OnPhysics3DStart();

        // Sources on inactive entities start when they are switched on.
        UpdateAudioListener(0.0f); // the 3D sounds start where the listener is
        std::vector<entt::entity> sources;
        std::vector<std::filesystem::path> preloads; // clips set to Preload (inactive sources' too: they may start later)
        for (auto e : m_Registry.view<AudioSourceComponent>())
        {
            auto& a = m_Registry.get<AudioSourceComponent>(e);
            a.ResetRuntime(); // nothing left from an earlier run
            sources.push_back(e);
            if (!a.Path.empty())
            {
                std::filesystem::path file = Project::GetAssetFileSystemPath(a.Path);
                if (AssetImporter::GetAudioSettings(file).Preload && std::find(preloads.begin(), preloads.end(), file) == preloads.end())
                    preloads.push_back(file);
            }
        }
        for (auto e : m_Registry.view<AmbientZoneComponent>())
        {
            auto& zone = m_Registry.get<AmbientZoneComponent>(e);
            zone.ResetRuntime(); // the zones fade in from silence where the listener is
            if (!zone.Path.empty())
            {
                std::filesystem::path file = Project::GetAssetFileSystemPath(zone.Path);
                if (AssetImporter::GetAudioSettings(file).Preload && std::find(preloads.begin(), preloads.end(), file) == preloads.end())
                    preloads.push_back(file);
            }
        }
        AudioEngine::SetScenePreloads(preloads);
        // Sounds that ended before this run (an earlier scene, the editor's previews) are not its.
        AudioEngine::TakeFinishedSounds();
        m_SoundOwners.clear();
        std::reverse(sources.begin(), sources.end()); // scene order
        for (auto e : sources)
            StartEntityEffects({ e, this });

        StartScripts();
    }

    void Scene::SetGamePaused(bool paused)
    {
        if (paused == IsGamePaused())
            return;
        if (paused)
        {
            m_TimeScaleBeforePause = m_TimeScale;
            m_TimeScale = 0.0f;
        }
        else
            m_TimeScale = m_TimeScaleBeforePause > 0.0f ? m_TimeScaleBeforePause : 1.0f;
    }

    void Scene::OnRuntimeStop()
    {
        m_IsRunning = false;
        m_TimeScale = m_TimeScaleBeforePause = 1.0f;
        StopScripts();
        OnPhysics2DStop();
        OnPhysics3DStop();
        m_AppliedInactive.clear();
        AudioEngine::StopAllExceptMusic(); // the music is the game's: it goes on into the next scene
        for (auto e : m_Registry.view<AudioSourceComponent>())
            m_Registry.get<AudioSourceComponent>(e).ResetRuntime();
        for (auto e : m_Registry.view<AmbientZoneComponent>())
            m_Registry.get<AmbientZoneComponent>(e).ResetRuntime();
        m_SoundOwners.clear();
        ResetUIRuntime();
        SaveData::Save(); // flush what the scripts stored (no-op when nothing changed)
        DebugDraw::Clear(); // nothing the game drew stays in the editor's view
    }

    void Scene::OnSimulationStart()
    {
        m_IsSimulating = true;
        DebugDraw::Clear();
        for (auto e : m_Registry.view<TintAnimationComponent>())
            m_Registry.get<TintAnimationComponent>(e).ResetRuntime();
        EnsureActiveCache();
        m_AppliedInactive = m_InactiveOrder;
        OnPhysics2DStart();
        OnPhysics3DStart();
    }

    void Scene::OnSimulationStop()
    {
        m_IsSimulating = false;
        for (auto e : m_Registry.view<TintAnimationComponent>())
            m_Registry.get<TintAnimationComponent>(e).ResetRuntime();
        OnPhysics2DStop();
        OnPhysics3DStop();
        m_AppliedInactive.clear();
        DebugDraw::Clear();
    }

    void Scene::CreateScriptInstance(Entity entity)
    {
        auto& nsc = entity.GetComponent<NativeScriptComponent>();
        if (nsc.Instance || nsc.ClassName.empty())
            return;
        const ScriptClassInfo* info = ScriptRegistry::Find(nsc.ClassName);
        if (!info)
        {
            ZM_CORE_WARN("Skript klassi topilmadi: '{}' (entity '{}')", nsc.ClassName, entity.GetName());
            return;
        }
        ScriptableEntity* instance = info->Factory();
        instance->m_Entity = entity;
        instance->m_ClassName = info->Name;
        instance->m_OwnerID = (uint64_t)entity.GetUUID();
        for (const auto& field : info->Fields)
            if (auto it = nsc.Fields.find(field.Name); it != nsc.Fields.end())
                field.Store(instance, it->second);
        nsc.Instance = instance;
        instance->OnCreate();
        // OnEnable comes from SyncScriptEnabled once every script of the batch exists.
        if (entity.IsValid())
            if (auto* n = entity.TryGetComponent<NativeScriptComponent>(); n && n->Instance == instance)
                instance->m_Created = true;
    }

    void Scene::SyncScriptEnabled(Entity entity)
    {
        if (!entity.IsValid()) return;
        auto* nsc = entity.TryGetComponent<NativeScriptComponent>();
        if (!nsc || !nsc->Instance) return;
        ScriptableEntity* instance = nsc->Instance;
        if (!instance->m_Created || instance->m_Destroying) return;
        // SetEnabled from inside OnEnable / OnDisable nests here; the transition budget is
        // shared by the whole nested chain so scripts that keep toggling themselves stop.
        constexpr int MaxTransitions = 16;
        if (instance->m_EnableDepth == 0)
            instance->m_EnableTransitions = 0;
        instance->m_EnableDepth++;
        for (;;)
        {
            // The callbacks may add components (storage moves) or destroy the entity.
            nsc = entity.IsValid() ? entity.TryGetComponent<NativeScriptComponent>() : nullptr;
            if (!nsc || nsc->Instance != instance)
                return; // the instance was destroyed inside its own callback
            // Enabled, and the entity active in the hierarchy (an inactive entity's script is off).
            bool wanted = ShouldScriptBeEnabled(entity, *nsc);
            if (wanted == instance->m_EnabledState || instance->m_Destroying)
                break;
            if (++instance->m_EnableTransitions > MaxTransitions)
            {
                if (instance->m_EnableTransitions == MaxTransitions + 1) // once per chain (outer levels unwind here too)
                    ZM_CORE_WARN("'{}': OnEnable/OnDisable skriptni to'xtovsiz yoqib-o'chirmoqda - to'xtatildi", entity.GetName());
                // Settle on the state the script was told last. Only the script's own flag is
                // changed: an inactive entity's script gets no per-frame callbacks anyway.
                if (IsActiveInHierarchy(entity))
                    nsc->Enabled = instance->m_EnabledState;
                break;
            }
            instance->m_EnabledState = wanted;
            if (wanted)
            {
                if (!instance->m_Started) m_ScriptsPendingStart = true;
                instance->OnEnable();
            }
            else
                instance->OnDisable();
        }
        instance->m_EnableDepth--;
    }

    void Scene::SetScriptEnabled(Entity entity, bool enabled)
    {
        if (!entity.IsValid()) return;
        auto* nsc = entity.TryGetComponent<NativeScriptComponent>();
        if (!nsc) return;
        nsc->Enabled = enabled;
        SyncScriptEnabled(entity);
    }

    bool Scene::IsScriptEnabled(Entity entity)
    {
        if (!entity.IsValid()) return false;
        auto* nsc = entity.TryGetComponent<NativeScriptComponent>();
        return nsc && nsc->Enabled;
    }

    void Scene::StartPendingScripts()
    {
        // Scripts spawned or enabled by an OnStart are started in the same pass loop, so they
        // still run OnStart before this frame's OnUpdate. The limit only stops runaway chains.
        for (int pass = 0; pass < 16 && m_ScriptsPendingStart; pass++)
        {
            m_ScriptsPendingStart = false;
            std::vector<entt::entity> pending;
            for (auto e : m_Registry.view<NativeScriptComponent>())
                if (auto* inst = m_Registry.get<NativeScriptComponent>(e).Instance;
                    inst && inst->m_Created && inst->m_EnabledState && !inst->m_Started && !inst->m_Destroying)
                    pending.push_back(e);
            std::reverse(pending.begin(), pending.end()); // scene order
            for (auto e : pending)
            {
                // An earlier OnStart may have disabled or destroyed it, or removed its component.
                auto* nsc = m_Registry.valid(e) ? m_Registry.try_get<NativeScriptComponent>(e) : nullptr;
                auto* inst = nsc ? nsc->Instance : nullptr;
                if (!inst || !inst->m_EnabledState || inst->m_Started || inst->m_Destroying) continue;
                inst->m_Started = true;
                inst->OnStart();
            }
        }
    }

    void Scene::StartScripts()
    {
        m_ScriptsRunning = true;
        m_ActiveContacts.clear();
        std::vector<entt::entity> entities;
        for (auto e : m_Registry.view<NativeScriptComponent>())
            entities.push_back(e);
        std::reverse(entities.begin(), entities.end());
        // Every OnCreate first, then OnEnable: scripts can find each other in OnEnable.
        for (auto e : entities)
            if (m_Registry.valid(e))
                CreateScriptInstance({ e, this });
        for (auto e : entities)
            if (m_Registry.valid(e))
                SyncScriptEnabled({ e, this });
    }

    void Scene::DestroyScriptInstance(Entity entity)
    {
        auto* nsc = entity.TryGetComponent<NativeScriptComponent>();
        if (!nsc || !nsc->Instance)
            return;
        ScriptableEntity* instance = nsc->Instance;
        if (instance->m_Destroying)
            return; // already on its way out (destroyed again from OnDisable / OnDestroy)
        instance->m_Destroying = true;
        if (instance->m_EnabledState)
        {
            instance->m_EnabledState = false;
            instance->OnDisable();
        }
        if (entity.IsValid())
            if (auto* n = entity.TryGetComponent<NativeScriptComponent>(); n && n->Instance == instance)
                n->Instance = nullptr;
        instance->OnDestroy();
        // Handlers capture the instance: they must not outlive it.
        m_EventBus->UnsubscribeOwner(instance->m_OwnerID);
        // Not deleted yet: this may be running inside one of the instance's own callbacks.
        m_RetiredScripts.push_back(instance);
    }

    void Scene::OnScriptComponentDestroyed(entt::registry& registry, entt::entity entity)
    {
        auto& nsc = registry.get<NativeScriptComponent>(entity);
        ScriptableEntity* instance = nsc.Instance;
        // DestroyEntity / Entity::RemoveComponent have already run DestroyScriptInstance; one
        // that is in progress (m_Destroying) still owns the instance.
        if (!instance || instance->m_Destroying)
            return;
        // No callbacks from inside an entt signal (the entity may be half destroyed already).
        nsc.Instance = nullptr;
        instance->m_Destroying = true;
        instance->m_EnabledState = false;
        m_EventBus->UnsubscribeOwner(instance->m_OwnerID);
        m_RetiredScripts.push_back(instance);
    }

    void Scene::DeleteRetiredScripts()
    {
        while (!m_RetiredScripts.empty())
        {
            std::vector<ScriptableEntity*> retired = std::move(m_RetiredScripts);
            m_RetiredScripts.clear();
            for (ScriptableEntity* instance : retired)
                delete instance;
        }
    }

    void Scene::StopScripts()
    {
        m_ScriptsRunning = false;
        std::vector<entt::entity> entities;
        for (auto e : m_Registry.view<NativeScriptComponent>())
            entities.push_back(e);
        for (auto e : entities)
            if (m_Registry.valid(e))
                DestroyScriptInstance({ e, this });
        // Anything left (unowned handlers, queued payloads) may point into the script
        // module, which can be unloaded / reloaded after this.
        m_EventBus->Clear();
        m_ActiveContacts.clear();
        m_ScriptsPendingStart = false;
        DeleteRetiredScripts();
    }

    // ------------------------------------------------------------------ messages
    bool Scene::DeliverMessage(Entity target, const ScriptMessage& message)
    {
        if (!target.IsValid())
            return false;
        auto* nsc = target.TryGetComponent<NativeScriptComponent>();
        if (!nsc || !nsc->Instance)
            return false;
        if (m_MessageDepth >= MaxMessageDepth)
        {
            if (!m_MessageDepthWarned)
                ZM_CORE_WARN("Xabar '{}' juda chuqur ichma-ich yuborildi ({} daraja) va tashlab yuborildi (cheksiz tsikl?)",
                             message.Name, MaxMessageDepth);
            m_MessageDepthWarned = true;
            return false;
        }
        m_MessageDepth++;
        nsc->Instance->ReceiveMessage(message);
        if (--m_MessageDepth == 0)
            m_MessageDepthWarned = false;
        return true;
    }

    bool Scene::SendMessage(Entity target, const ScriptMessage& message)
    {
        return DeliverMessage(target, message);
    }

    int Scene::SendMessageToChildren(Entity root, const ScriptMessage& message, bool includeSelf)
    {
        if (!root) return 0;
        // Snapshot first: receivers may instantiate / reparent entities.
        std::vector<Entity> targets;
        CollectSubtree(*this, root, targets);
        int delivered = 0;
        for (size_t i = includeSelf ? 0 : 1; i < targets.size(); i++)
            delivered += DeliverMessage(targets[i], message) ? 1 : 0;
        return delivered;
    }

    int Scene::SendMessageUpwards(Entity entity, const ScriptMessage& message, bool includeSelf)
    {
        if (!entity) return 0;
        std::vector<Entity> targets;
        for (Entity e = includeSelf ? entity : GetParent(entity); e; e = GetParent(e))
            targets.push_back(e);
        int delivered = 0;
        for (Entity e : targets)
            delivered += DeliverMessage(e, message) ? 1 : 0;
        return delivered;
    }

    int Scene::Broadcast(const ScriptMessage& message)
    {
        std::vector<Entity> targets;
        for (auto e : m_Registry.view<NativeScriptComponent>())
            if (m_Registry.get<NativeScriptComponent>(e).Instance)
                targets.emplace_back(e, this);
        SortInSceneOrder(targets);
        int delivered = 0;
        for (Entity e : targets)
            delivered += DeliverMessage(e, message) ? 1 : 0;
        return delivered;
    }

    void Scene::DestroyScriptInstances()
    {
        bool running = m_ScriptsRunning;
        StopScripts();
        m_ScriptsRunning = running;
    }

    // ------------------------------------------------------------------ physics 2D
    void Scene::OnPhysics2DStart()
    {
        if (m_PhysicsWorld)
            OnPhysics2DStop();
        m_PhysicsWorld = std::make_unique<PhysicsWorld2D>(m_Settings.Gravity);
        m_Settings.Physics2D.Sanitize();
        m_PhysicsWorld->SetSleeping(m_Settings.Physics2D.AllowSleep, m_Settings.Physics2D.TimeToSleep); // before the bodies
        m_PhysicsAccumulator = 0.0f;
        m_Physics2DStepsLastFrame = 0;
        m_ScriptForces2D.clear();
        m_ScriptForceTime2D = 0.0f;
        m_PhysicsMaterialRevision = PhysicsMaterialLibrary::GetRevision();

        for (auto e : m_Registry.view<Rigidbody2DComponent>())
            CreatePhysicsBody({ e, this });
        for (auto e : m_Registry.view<TilemapComponent>())
            CreateTilemapBody({ e, this });
        SyncJoints2D(); // once every body exists
        m_ReportedMissingJointBodies.clear();
        ReportMissingJointBodies(nullptr);
    }

    void Scene::ReportMissingJointBodies(const std::vector<Entity>* entities)
    {
        // A connected body that is not in the scene (deleted, or a prefab made in another scene): the
        // joint waits for it for ever. Said once per missing body while the game runs.
        auto check = [&](Entity e) {
            ForEachJoint2DType([&](auto type) {
                using T = typename decltype(type)::type;
                const auto* joint = e.TryGetComponent<T>();
                if (!m_PhysicsWorld || !joint || !joint->ConnectedBody.ID || GetEntityByUUID(joint->ConnectedBody.ID))
                    return;
                if (!m_ReportedMissingJointBodies.insert(joint->ConnectedBody.ID).second)
                    return;
                ZM_CORE_WARN("'{}' obyektidagi {} ulangan jismi ({:016x}) sahnada yo'q - joint yaratilmaydi", e.GetName(),
                             Joint2DTypeName(Joint2DTypeOf<T>()), joint->ConnectedBody.ID);
            });
            ForEachJoint3DType([&](auto type) {
                using T = typename decltype(type)::type;
                const auto* joint = e.TryGetComponent<T>();
                if (!m_PhysicsWorld3D || !joint || !joint->ConnectedBody.ID || GetEntityByUUID(joint->ConnectedBody.ID))
                    return;
                if (!m_ReportedMissingJointBodies.insert(joint->ConnectedBody.ID).second)
                    return;
                ZM_CORE_WARN("'{}' obyektidagi {} ulangan jismi ({:016x}) sahnada yo'q - joint yaratilmaydi", e.GetName(),
                             Joint3DTypeName(Joint3DTypeOf<T>()), joint->ConnectedBody.ID);
            });
        };
        if (entities)
        {
            for (Entity e : *entities)
                if (e.IsValid())
                    check(e);
            return;
        }
        ForEachJoint2DType([&](auto type) {
            using T = typename decltype(type)::type;
            for (auto e : m_Registry.view<T>())
                check({ e, this });
        });
        ForEachJoint3DType([&](auto type) {
            using T = typename decltype(type)::type;
            for (auto e : m_Registry.view<T>())
                check({ e, this });
        });
    }

    void Scene::CreateTilemapBody(Entity entity)
    {
        auto& tm = entity.GetComponent<TilemapComponent>();
        if (!tm.Collision || tm.RuntimeBody || !m_PhysicsWorld || !IsActiveInHierarchy(entity)) return;
        tm.EnsureSize();
        glm::vec3 position, rotation, scale;
        Math::DecomposeTransform(GetWorldTransform(entity), position, rotation, scale);

        PhysicsBody2DDesc def;
        def.EntityID = entity.GetUUID();
        def.MotionType = (int)Rigidbody2DComponent::BodyType::Static;
        def.Position = { position.x, position.y };
        def.Angle = rotation.z;
        b2Body* body = m_PhysicsWorld->CreateBody(def);
        tm.RuntimeBody = body;

        float sx = tm.TileSize * std::abs(scale.x), sy = tm.TileSize * std::abs(scale.y);
        // Horizontal runs of solid tiles (and of one-way tiles) are single boxes: fewer fixtures, no seams.
        // The tileset's per-tile collision decides which tiles count.
        const TilesetProperties* properties = TilesetProperties::Get(tm.TilesetPath);
        // What every tile box shares: the surface (material asset or the tilemap's own values), the
        // collision layer and a platform effector on the tilemap's entity.
        PhysicsShape2DDesc tileShape;
        PhysicsMaterialData own;
        own.Friction = tm.Friction;
        own.Restitution = tm.Restitution;
        own.RestitutionThreshold = 1.0f;
        own.Density = 0.0f;
        const PhysicsMaterialData surface = PhysicsMaterialLibrary::Resolve(tm.MaterialPath, own);
        ApplySurface(tileShape, surface);
        tileShape.Density = 0.0f; // static
        const CollisionLayers2D& layers = Project::GetCollisionLayers2D();
        tileShape.CategoryBits = layers.GetCategoryBits(tm.CollisionLayer);
        tileShape.MaskBits = layers.GetMaskBits(tm.CollisionLayer);
        tileShape.Tag = ShapeTagTilemap;
        const auto* effector = entity.TryGetComponent<PlatformEffector2DComponent>();
        if (effector)
            tileShape.Platform = MakePlatform(*effector);
        for (const TileColliderBox& run : TilemapCollision::BuildBoxes(tm, properties))
        {
            float w = (run.X1 - run.X0) * sx;
            PhysicsShape2DDesc shape = tileShape;
            shape.HalfExtents = { w * 0.5f, sy * 0.5f };
            shape.Offset = { run.X0 * sx + w * 0.5f, run.Y * sy + sy * 0.5f };
            shape.OneWay = run.Type == TileCollision::OneWay && !effector; // an effector decides for every tile
            m_PhysicsWorld->AddShape(body, shape);
        }
    }

    void Scene::OnPhysics2DStop()
    {
        if (!m_PhysicsWorld) return;
        for (auto e : m_Registry.view<Rigidbody2DComponent>()) m_Registry.get<Rigidbody2DComponent>(e).RuntimeBody = nullptr;
        for (auto e : m_Registry.view<BoxCollider2DComponent>()) m_Registry.get<BoxCollider2DComponent>(e).RuntimeFixture = nullptr;
        for (auto e : m_Registry.view<CircleCollider2DComponent>()) m_Registry.get<CircleCollider2DComponent>(e).RuntimeFixture = nullptr;
        for (auto e : m_Registry.view<PolygonCollider2DComponent>())
        {
            auto& c = m_Registry.get<PolygonCollider2DComponent>(e);
            c.RuntimeFixture = nullptr;
            c.RuntimePieces = 0;
        }
        for (auto e : m_Registry.view<EdgeCollider2DComponent>()) m_Registry.get<EdgeCollider2DComponent>(e).RuntimeFixture = nullptr;
        for (auto e : m_Registry.view<TilemapComponent>()) m_Registry.get<TilemapComponent>(e).RuntimeBody = nullptr;
        for (auto e : m_Registry.view<AreaEffector2DComponent>()) m_Registry.get<AreaEffector2DComponent>(e).AffectedBodies = 0;
        for (auto e : m_Registry.view<BuoyancyEffector2DComponent>())
        {
            auto& c = m_Registry.get<BuoyancyEffector2DComponent>(e);
            c.AffectedBodies = 0;
            c.SubmergedArea = 0.0f;
        }
        ForEachJoint2DType([&](auto type) {
            using T = typename decltype(type)::type;
            for (auto e : m_Registry.view<T>())
                ResetJointRuntime(m_Registry.get<T>(e));
        });
        m_JointBreaks2D.clear();
        m_ScriptForces2D.clear();
        m_ScriptForceTime2D = 0.0f;
        m_PhysicsWorld.reset();
    }

    // ------------------------------------------------------------------ joints 2D
    template<typename T>
    void Scene::OnJoint2DDestroyed(entt::registry& registry, entt::entity entity)
    {
        auto& joint = registry.get<T>(entity);
        if (joint.RuntimeJoint && m_PhysicsWorld)
            m_PhysicsWorld->DestroyJoint((b2Joint*)joint.RuntimeJoint);
        joint.RuntimeJoint = nullptr;
    }

    template<typename T>
    void Scene::OnJoint2DReplaced(entt::registry& registry, entt::entity entity)
    {
        if (!m_PhysicsWorld)
            return;
        // A new value (a pasted component) brings no joint of its own, or another one's: the joint
        // the entity had goes and the next sync makes one for the new settings.
        auto& joint = registry.get<T>(entity);
        b2Joint* keep = (b2Joint*)joint.RuntimeJoint;
        m_PhysicsWorld->ClaimJoint(registry.get<IDComponent>(entity).ID, Joint2DTypeOf<T>(), keep);
        if (keep != joint.RuntimeJoint)
        {
            joint.RuntimeJoint = keep;
            joint.RuntimeConnected = 0;
        }
    }

    void Scene::SyncJoints2D()
    {
        if (!m_PhysicsWorld)
            return;
        ForEachJoint2DType([&](auto type) {
            using T = typename decltype(type)::type;
            for (auto e : m_Registry.view<T>())
            {
                Entity entity{ e, this };
                auto& joint = m_Registry.get<T>(e);
                if (joint.RuntimeJoint && joint.RuntimeConnected != joint.ConnectedBody.ID)
                {
                    // Connected to something else now (a script grabbed another body): made again.
                    m_PhysicsWorld->DestroyJoint((b2Joint*)joint.RuntimeJoint);
                    joint.RuntimeJoint = nullptr;
                }
                if (joint.RuntimeJoint)
                {
                    m_PhysicsWorld->UpdateJoint((b2Joint*)joint.RuntimeJoint, MakeJoint2DDesc(joint));
                    continue;
                }
                if (joint.Broken)
                    continue;
                b2Body* body = GetBody2D(entity, false);
                if (!body)
                    continue; // no Rigidbody 2D, inactive, or the world is not running it yet
                b2Body* connected = nullptr;
                if (joint.ConnectedBody.ID)
                {
                    Entity other = GetEntityByUUID(joint.ConnectedBody.ID);
                    if (!other || other == entity || !(connected = GetBody2D(other, true)))
                        continue; // waits for the connected body (gone, inactive, without a body)
                }
                PhysicsJoint2DDesc desc = MakeJoint2DDesc(joint);
                desc.EntityID = entity.GetUUID();
                desc.Body = body;
                desc.ConnectedBody = connected;
                joint.RuntimeJoint = m_PhysicsWorld->CreateJoint(desc);
                joint.RuntimeConnected = joint.ConnectedBody.ID;
                if (!joint.RuntimeJoint)
                    continue;
                // The auto-configured values are shown in the inspector while the game runs.
                joint.ConnectedAnchor = desc.ConnectedAnchor;
                if constexpr (std::is_same_v<T, DistanceJoint2DComponent>)
                    joint.Distance = desc.Distance;
            }
        });
    }

    void Scene::ForgetJoints2D(const std::vector<PhysicsJoint2DRef>& joints)
    {
        for (const PhysicsJoint2DRef& ref : joints)
        {
            Entity owner = GetEntityByUUID(ref.EntityID);
            if (!owner)
                continue;
            ForEachJoint2DType([&](auto type) {
                using T = typename decltype(type)::type;
                if (Joint2DTypeOf<T>() != ref.Type)
                    return;
                if (auto* joint = owner.TryGetComponent<T>())
                {
                    joint->RuntimeJoint = nullptr;
                    joint->RuntimeConnected = 0;
                }
            });
        }
    }

    void Scene::CheckJointBreaks2D(float dt)
    {
        if (!m_PhysicsWorld || dt <= 0.0f)
            return;
        ForEachJoint2DType([&](auto type) {
            using T = typename decltype(type)::type;
            for (auto e : m_Registry.view<T>())
            {
                auto& joint = m_Registry.get<T>(e);
                if (!joint.RuntimeJoint || (joint.BreakForce <= 0.0f && joint.BreakTorque <= 0.0f))
                    continue;
                PhysicsJoint2DState state = PhysicsWorld2D::GetJointState((const b2Joint*)joint.RuntimeJoint, 1.0f / dt);
                if (!(joint.BreakForce > 0.0f && state.Force > joint.BreakForce) && !(joint.BreakTorque > 0.0f && state.Torque > joint.BreakTorque))
                    continue;
                m_PhysicsWorld->DestroyJoint((b2Joint*)joint.RuntimeJoint);
                joint.RuntimeJoint = nullptr;
                joint.RuntimeConnected = 0;
                joint.Broken = true;
                joint.ReactionForce = state.Force;
                joint.ReactionTorque = state.Torque;
                m_JointBreaks2D.push_back({ m_Registry.get<IDComponent>(e).ID, Joint2DTypeOf<T>(), joint.ConnectedBody.ID, state.Force, state.Torque });
            }
        });
    }

    void Scene::UpdateJointStates2D(float dt)
    {
        if (!m_PhysicsWorld || dt <= 0.0f)
            return;
        ForEachJoint2DType([&](auto type) {
            using T = typename decltype(type)::type;
            for (auto e : m_Registry.view<T>())
            {
                auto& joint = m_Registry.get<T>(e);
                if (!joint.RuntimeJoint)
                    continue;
                PhysicsJoint2DState state = PhysicsWorld2D::GetJointState((const b2Joint*)joint.RuntimeJoint, 1.0f / dt);
                joint.ReactionForce = state.Force;
                joint.ReactionTorque = state.Torque;
                if constexpr (std::is_same_v<T, DistanceJoint2DComponent>)
                    joint.CurrentDistance = state.Translation;
                else if constexpr (std::is_same_v<T, RevoluteJoint2DComponent>)
                {
                    joint.JointAngle = state.Angle;
                    joint.JointSpeed = state.Speed;
                }
                else if constexpr (std::is_same_v<T, PrismaticJoint2DComponent> || std::is_same_v<T, WheelJoint2DComponent>)
                {
                    joint.JointTranslation = state.Translation;
                    joint.JointSpeed = state.Speed;
                }
            }
        });
    }

    void Scene::DispatchJointBreaks2D()
    {
        if (m_JointBreaks2D.empty())
            return;
        std::vector<JointBreak2DRecord> breaks = std::move(m_JointBreaks2D);
        m_JointBreaks2D.clear();
        if (!m_ScriptsRunning)
            return;
        for (const JointBreak2DRecord& record : breaks)
        {
            Entity entity = GetEntityByUUID(record.Entity);
            if (!entity)
                continue; // destroyed by an earlier callback
            auto* nsc = entity.TryGetComponent<NativeScriptComponent>();
            if (!nsc || !nsc->Instance || nsc->Instance->m_Destroying)
                continue;
            JointBreak2D info;
            info.Type = record.Type;
            info.ConnectedBody = record.Connected ? GetEntityByUUID(record.Connected) : Entity();
            info.Force = record.Force;
            info.Torque = record.Torque;
            nsc->Instance->OnJointBreak(info);
        }
    }

    // ------------------------------------------------------------------ joints 3D
    template<typename T>
    void Scene::OnJoint3DDestroyed(entt::registry& registry, entt::entity entity)
    {
        auto& joint = registry.get<T>(entity);
        if (joint.RuntimeJoint && m_PhysicsWorld3D)
            m_PhysicsWorld3D->DestroyJoint(joint.RuntimeJoint);
        joint.RuntimeJoint = 0;
    }

    template<typename T>
    void Scene::OnJoint3DReplaced(entt::registry& registry, entt::entity entity)
    {
        if (!m_PhysicsWorld3D)
            return;
        // A new value (a pasted component) brings no joint of its own, or another one's: the joint
        // the entity had goes and the next sync makes one for the new settings.
        auto& joint = registry.get<T>(entity);
        uint32_t keep = joint.RuntimeJoint;
        m_PhysicsWorld3D->ClaimJoint(registry.get<IDComponent>(entity).ID, Joint3DTypeOf<T>(), keep);
        if (keep != joint.RuntimeJoint)
        {
            joint.RuntimeJoint = keep;
            joint.RuntimeConnected = 0;
        }
    }

    void Scene::SyncJoints3D()
    {
        if (!m_PhysicsWorld3D)
            return;
        auto bodyOf = [](Entity e) -> uint32_t {
            const auto* rb = e.TryGetComponent<Rigidbody3DComponent>();
            return rb ? rb->RuntimeBodyID : PhysicsJoint3DDesc::NoBody;
        };
        ForEachJoint3DType([&](auto type) {
            using T = typename decltype(type)::type;
            for (auto e : m_Registry.view<T>())
            {
                Entity entity{ e, this };
                auto& joint = m_Registry.get<T>(e);
                if (joint.RuntimeJoint && joint.RuntimeConnected != joint.ConnectedBody.ID)
                {
                    // Connected to something else now (a script grabbed another body): made again.
                    m_PhysicsWorld3D->DestroyJoint(joint.RuntimeJoint);
                    joint.RuntimeJoint = 0;
                }
                if (joint.RuntimeJoint)
                {
                    m_PhysicsWorld3D->UpdateJoint(joint.RuntimeJoint, MakeJoint3DDesc(joint));
                    continue;
                }
                if (joint.Broken)
                    continue;
                const uint32_t body = bodyOf(entity);
                if (body == PhysicsJoint3DDesc::NoBody)
                    continue; // no Rigidbody 3D, inactive, or the world is not running it yet
                Entity other;
                uint32_t connected = PhysicsJoint3DDesc::NoBody;
                if (joint.ConnectedBody.ID)
                {
                    other = GetEntityByUUID(joint.ConnectedBody.ID);
                    if (!other || other == entity || (connected = bodyOf(other)) == PhysicsJoint3DDesc::NoBody)
                        continue; // waits for the connected body (gone, inactive, without a body)
                }
                // The points in the world as the entities are now (their bodies were made there).
                const glm::mat4 own = GetWorldTransform(entity);
                const glm::mat4 otherTransform = other ? GetWorldTransform(other) : glm::mat4(1.0f);
                const glm::vec3 worldAnchor = glm::vec3(own * glm::vec4(joint.Anchor, 1.0f));
                if (joint.AutoConfigureConnectedAnchor)
                    joint.ConnectedAnchor = other ? glm::vec3(glm::inverse(otherTransform) * glm::vec4(worldAnchor, 1.0f)) : worldAnchor;
                const glm::vec3 worldConnected = glm::vec3(otherTransform * glm::vec4(joint.ConnectedAnchor, 1.0f));

                PhysicsJoint3DDesc desc = MakeJoint3DDesc(joint);
                desc.EntityID = entity.GetUUID();
                desc.Body = body;
                desc.ConnectedBody = connected;
                desc.Anchor = worldAnchor;
                desc.ConnectedAnchor = worldConnected;
                if constexpr (std::is_same_v<T, HingeJoint3DComponent> || std::is_same_v<T, SliderJoint3DComponent> ||
                              std::is_same_v<T, ConeJoint3DComponent>)
                {
                    glm::vec3 p, r, sc;
                    Math::DecomposeTransform(own, p, r, sc);
                    desc.Axis = glm::quat(r) * joint.Axis;
                }
                if constexpr (std::is_same_v<T, DistanceJoint3DComponent>)
                    if (joint.AutoConfigureDistance)
                    {
                        // The distance they are at now (written back: the inspector shows it while it runs).
                        const float current = glm::length(worldAnchor - worldConnected);
                        joint.MaxDistance = current;
                        joint.MinDistance = joint.MaxDistanceOnly ? 0.0f : current;
                        desc.MinDistance = joint.MinDistance;
                        desc.MaxDistance = joint.MaxDistance;
                    }
                joint.RuntimeJoint = m_PhysicsWorld3D->CreateJoint(desc);
                joint.RuntimeConnected = joint.ConnectedBody.ID;
            }
        });
    }

    void Scene::ForgetJoints3D(const std::vector<PhysicsJoint3DRef>& joints)
    {
        for (const PhysicsJoint3DRef& ref : joints)
        {
            Entity owner = GetEntityByUUID(ref.EntityID);
            if (!owner)
                continue;
            ForEachJoint3DType([&](auto type) {
                using T = typename decltype(type)::type;
                if (Joint3DTypeOf<T>() != ref.Type)
                    return;
                if (auto* joint = owner.TryGetComponent<T>())
                {
                    joint->RuntimeJoint = 0;
                    joint->RuntimeConnected = 0;
                }
            });
        }
    }

    void Scene::CheckJointBreaks3D(float dt)
    {
        if (!m_PhysicsWorld3D || dt <= 0.0f)
            return;
        ForEachJoint3DType([&](auto type) {
            using T = typename decltype(type)::type;
            for (auto e : m_Registry.view<T>())
            {
                auto& joint = m_Registry.get<T>(e);
                if (!joint.RuntimeJoint || (joint.BreakForce <= 0.0f && joint.BreakTorque <= 0.0f))
                    continue;
                PhysicsJoint3DState state = m_PhysicsWorld3D->GetJointState(joint.RuntimeJoint, 1.0f / dt);
                if (!(joint.BreakForce > 0.0f && state.Force > joint.BreakForce) && !(joint.BreakTorque > 0.0f && state.Torque > joint.BreakTorque))
                    continue;
                m_PhysicsWorld3D->DestroyJoint(joint.RuntimeJoint);
                joint.RuntimeJoint = 0;
                joint.RuntimeConnected = 0;
                joint.Broken = true;
                joint.ReactionForce = state.Force;
                joint.ReactionTorque = state.Torque;
                m_JointBreaks3D.push_back({ m_Registry.get<IDComponent>(e).ID, Joint3DTypeOf<T>(), joint.ConnectedBody.ID, state.Force, state.Torque });
            }
        });
    }

    void Scene::UpdateJointStates3D(float dt)
    {
        if (!m_PhysicsWorld3D || dt <= 0.0f)
            return;
        ForEachJoint3DType([&](auto type) {
            using T = typename decltype(type)::type;
            for (auto e : m_Registry.view<T>())
            {
                auto& joint = m_Registry.get<T>(e);
                if (!joint.RuntimeJoint)
                    continue;
                PhysicsJoint3DState state = m_PhysicsWorld3D->GetJointState(joint.RuntimeJoint, 1.0f / dt);
                joint.ReactionForce = state.Force;
                joint.ReactionTorque = state.Torque;
                if constexpr (std::is_same_v<T, HingeJoint3DComponent>)
                {
                    joint.JointAngle = state.Angle;
                    joint.JointSpeed = state.Speed;
                }
                else if constexpr (std::is_same_v<T, DistanceJoint3DComponent>)
                    joint.CurrentDistance = state.Translation;
                else if constexpr (std::is_same_v<T, SliderJoint3DComponent>)
                {
                    joint.JointTranslation = state.Translation;
                    joint.JointSpeed = state.Speed;
                }
                else if constexpr (std::is_same_v<T, ConeJoint3DComponent>)
                {
                    joint.SwingAngle = state.Angle;
                    joint.TwistAngle = state.Twist;
                }
            }
        });
    }

    void Scene::DispatchJointBreaks3D()
    {
        if (m_JointBreaks3D.empty())
            return;
        std::vector<JointBreak3DRecord> breaks = std::move(m_JointBreaks3D);
        m_JointBreaks3D.clear();
        if (!m_ScriptsRunning)
            return;
        for (const JointBreak3DRecord& record : breaks)
        {
            Entity entity = GetEntityByUUID(record.Entity);
            if (!entity)
                continue; // destroyed by an earlier callback
            auto* nsc = entity.TryGetComponent<NativeScriptComponent>();
            if (!nsc || !nsc->Instance || nsc->Instance->m_Destroying)
                continue;
            JointBreak3D info;
            info.Type = record.Type;
            info.ConnectedBody = record.Connected ? GetEntityByUUID(record.Connected) : Entity();
            info.Force = record.Force;
            info.Torque = record.Torque;
            nsc->Instance->OnJointBreak3D(info);
        }
    }

    void Scene::CreatePhysicsBody(Entity entity)
    {
        auto& rb2d = entity.GetComponent<Rigidbody2DComponent>();
        if (rb2d.RuntimeBody || !m_PhysicsWorld || !IsActiveInHierarchy(entity)) return;

        glm::vec3 position, rotation, scale;
        Math::DecomposeTransform(GetWorldTransform(entity), position, rotation, scale);

        PhysicsBody2DDesc bodyDef;
        bodyDef.EntityID = entity.GetUUID();
        bodyDef.MotionType = (int)rb2d.Type;
        bodyDef.Position = { position.x, position.y };
        bodyDef.Angle = rotation.z;
        bodyDef.FixedRotation = rb2d.FixedRotation;
        bodyDef.GravityScale = rb2d.GravityScale;
        bodyDef.LinearDamping = rb2d.LinearDamping;
        bodyDef.AngularDamping = rb2d.AngularDamping;
        bodyDef.Bullet = rb2d.Bullet;
        bodyDef.CanSleep = rb2d.Sleeping != Rigidbody2DComponent::SleepMode::NeverSleep;
        bodyDef.StartAwake = rb2d.Sleeping != Rigidbody2DComponent::SleepMode::StartAsleep;

        b2Body* body = m_PhysicsWorld->CreateBody(bodyDef);
        rb2d.RuntimeBody = body;

        const glm::vec2 bodyScale(std::abs(scale.x), std::abs(scale.y));
        const auto* effector = entity.TryGetComponent<PlatformEffector2DComponent>();
        if (auto* bc2d = entity.TryGetComponent<BoxCollider2DComponent>())
        {
            PhysicsShape2DDesc shape = MakeColliderShape(*bc2d, rb2d.Layer, effector, ShapeTagBox);
            shape.Type = PhysicsShape2DDesc::Kind::Box;
            shape.Offset = bc2d->Offset;
            shape.HalfExtents = { std::max(0.001f, bc2d->Size.x * bodyScale.x), std::max(0.001f, bc2d->Size.y * bodyScale.y) };
            shape.OneWay = bc2d->OneWay && !effector; // an effector decides for every collider of the entity
            bc2d->RuntimeFixture = m_PhysicsWorld->AddShape(body, shape);
        }

        if (auto* cc2d = entity.TryGetComponent<CircleCollider2DComponent>())
        {
            PhysicsShape2DDesc shape = MakeColliderShape(*cc2d, rb2d.Layer, effector, ShapeTagCircle);
            shape.Type = PhysicsShape2DDesc::Kind::Circle;
            shape.Offset = cc2d->Offset;
            shape.Radius = std::max(0.001f, std::abs(scale.x) * cc2d->Radius);
            cc2d->RuntimeFixture = m_PhysicsWorld->AddShape(body, shape);
        }

        if (auto* pc2d = entity.TryGetComponent<PolygonCollider2DComponent>())
        {
            // Convex pieces of the scaled outline: one fixture each.
            PhysicsShape2DDesc shape = MakeColliderShape(*pc2d, rb2d.Layer, effector, ShapeTagPolygon);
            shape.Type = PhysicsShape2DDesc::Kind::Polygon;
            pc2d->RuntimeFixture = nullptr;
            pc2d->RuntimePieces = 0;
            for (const std::vector<glm::vec2>& piece : DecomposePolygonCollider(*pc2d, bodyScale).Pieces)
            {
                shape.Points = piece;
                if (b2Fixture* fixture = m_PhysicsWorld->AddShape(body, shape))
                {
                    if (!pc2d->RuntimeFixture)
                        pc2d->RuntimeFixture = fixture;
                    pc2d->RuntimePieces++;
                }
            }
        }

        if (auto* ec2d = entity.TryGetComponent<EdgeCollider2DComponent>())
        {
            PhysicsShape2DDesc shape = MakeColliderShape(*ec2d, rb2d.Layer, effector, ShapeTagEdge);
            shape.Type = ec2d->OneSided ? PhysicsShape2DDesc::Kind::Chain : PhysicsShape2DDesc::Kind::Edge;
            shape.Density = 0.0f; // no area
            shape.Loop = ec2d->Loop;
            shape.Offset = ec2d->Offset;
            for (const glm::vec2& p : ec2d->Points)
                shape.Points.push_back(p * bodyScale);
            ec2d->RuntimeFixture = m_PhysicsWorld->AddShape(body, shape);
        }
    }

    Polygon2D::Decomposition Scene::DecomposePolygonCollider(const PolygonCollider2DComponent& collider, const glm::vec2& scale)
    {
        std::vector<glm::vec2> points;
        points.reserve(collider.Points.size());
        for (const glm::vec2& p : collider.Points)
            points.push_back(collider.Offset + p * scale);
        return Polygon2D::Decompose(points);
    }

    void Scene::ApplyPhysicsMaterials()
    {
        // A physics material asset changed (hot reload, the editor): the colliders naming one get its values.
        m_PhysicsMaterialRevision = PhysicsMaterialLibrary::GetRevision();
        if (m_PhysicsWorld3D)
        {
            auto apply3D = [&](auto type, int tag) {
                using T = typename decltype(type)::type;
                for (auto e : m_Registry.view<T, Rigidbody3DComponent>())
                {
                    const auto& c = m_Registry.get<T>(e);
                    const auto& rb = m_Registry.get<Rigidbody3DComponent>(e);
                    if (!c.MaterialPath.empty() && rb.RuntimeBodyID != 0xffffffff)
                        m_PhysicsWorld3D->SetShapeSurface(rb.RuntimeBodyID, tag, ColliderSurface3D(c));
                }
            };
            apply3D(std::type_identity<BoxCollider3DComponent>(), ShapeTagBox3D);
            apply3D(std::type_identity<SphereCollider3DComponent>(), ShapeTagSphere3D);
            apply3D(std::type_identity<CapsuleCollider3DComponent>(), ShapeTagCapsule3D);
            apply3D(std::type_identity<ConvexHullCollider3DComponent>(), ShapeTagConvexHull3D);
            apply3D(std::type_identity<MeshCollider3DComponent>(), ShapeTagMesh3D);
        }
        if (!m_PhysicsWorld)
            return;
        auto apply = [&](auto type, int tag) {
            using T = typename decltype(type)::type;
            for (auto e : m_Registry.view<T, Rigidbody2DComponent>())
            {
                auto& c = m_Registry.get<T>(e);
                auto& rb = m_Registry.get<Rigidbody2DComponent>(e);
                if (c.MaterialPath.empty() || !rb.RuntimeBody)
                    continue;
                PhysicsMaterialData surface = PhysicsMaterialLibrary::Resolve(c.MaterialPath, ColliderOwnSurface(c));
                if constexpr (std::is_same_v<T, EdgeCollider2DComponent>)
                    surface.Density = 0.0f;
                m_PhysicsWorld->SetShapeMaterial((b2Body*)rb.RuntimeBody, tag, surface);
            }
        };
        apply(std::type_identity<BoxCollider2DComponent>(), ShapeTagBox);
        apply(std::type_identity<CircleCollider2DComponent>(), ShapeTagCircle);
        apply(std::type_identity<PolygonCollider2DComponent>(), ShapeTagPolygon);
        apply(std::type_identity<EdgeCollider2DComponent>(), ShapeTagEdge);
        for (auto e : m_Registry.view<TilemapComponent>())
        {
            auto& tm = m_Registry.get<TilemapComponent>(e);
            if (tm.MaterialPath.empty() || !tm.RuntimeBody)
                continue;
            PhysicsMaterialData own;
            own.Friction = tm.Friction;
            own.Restitution = tm.Restitution;
            own.RestitutionThreshold = 1.0f;
            PhysicsMaterialData surface = PhysicsMaterialLibrary::Resolve(tm.MaterialPath, own);
            surface.Density = 0.0f;
            m_PhysicsWorld->SetShapeMaterial((b2Body*)tm.RuntimeBody, ShapeTagTilemap, surface);
        }
    }

    // ------------------------------------------------------------------ physics 3D
    void Scene::OnPhysics3DStart()
    {
        OnPhysics3DStop();
        if (m_Registry.view<Rigidbody3DComponent>().empty() && m_Registry.view<CharacterController3DComponent>().empty())
            return; // don't spin up Jolt for 2D-only scenes
        MakePhysicsWorld3D();
        for (auto e : m_Registry.view<Rigidbody3DComponent>())
            CreatePhysicsBody3D({ e, this });
        m_PhysicsWorld3D->OptimizeBroadPhase();
        for (auto e : m_Registry.view<CharacterController3DComponent>())
            CreateCharacter3D({ e, this });
        m_JointBreaks3D.clear();
        SyncJoints3D(); // once every body exists
        ReportMissingJointBodies(nullptr);
    }

    void Scene::MakePhysicsWorld3D()
    {
        m_PhysicsWorld3D = std::make_unique<PhysicsWorld3D>(m_Settings.Gravity3D);
        // The project's collision layers (shared with 2D physics); StepPhysics keeps it up to date.
        m_PhysicsWorld3D->SetCollisionMatrix(Project::GetCollisionLayers2D().Masks);
        m_Settings.Physics3D.Sanitize();
        m_PhysicsWorld3D->SetSleeping(m_Settings.Physics3D.AllowSleep, m_Settings.Physics3D.TimeToSleep);
        if (!m_PhysicsWorld)
            m_PhysicsMaterialRevision = PhysicsMaterialLibrary::GetRevision(); // the colliders get the materials as they are now
    }

    void Scene::CreateCharacter3D(Entity entity)
    {
        if (!IsActiveInHierarchy(entity))
            return;
        if (!m_PhysicsWorld3D)
            MakePhysicsWorld3D();
        auto& cc = entity.GetComponent<CharacterController3DComponent>();
        if (cc.RuntimeID != 0xffffffff) return;
        CharacterDesc desc;
        desc.EntityID = entity.GetUUID();
        desc.Position = GetWorldPosition(entity);
        desc.Radius = cc.Radius;
        desc.Height = cc.Height;
        desc.MaxSlopeDegrees = cc.MaxSlope;
        desc.Mass = cc.Mass;
        desc.Layer = CollisionLayers2D::Clamp(cc.Layer);
        desc.PushBodies = cc.PushBodies;
        desc.PushForce = cc.PushForce;
        cc.RuntimeID = m_PhysicsWorld3D->CreateCharacter(desc);
        cc.VerticalVelocity = 0.0f;
        cc.Grounded = false;
        cc.Velocity = glm::vec3(0.0f);
    }

    void Scene::OnPhysics3DStop()
    {
        if (!m_PhysicsWorld3D) return;
        for (auto e : m_Registry.view<CharacterController3DComponent>())
            m_Registry.get<CharacterController3DComponent>(e).RuntimeID = 0xffffffff;
        for (auto e : m_Registry.view<Rigidbody3DComponent>())
            m_Registry.get<Rigidbody3DComponent>(e).RuntimeBodyID = 0xffffffff;
        ForEachJoint3DType([&](auto type) {
            using T = typename decltype(type)::type;
            for (auto e : m_Registry.view<T>())
                ResetJoint3DRuntime(m_Registry.get<T>(e));
        });
        m_JointBreaks3D.clear();
        m_PhysicsWorld3D.reset();
    }

    void Scene::CreatePhysicsBody3D(Entity entity)
    {
        if (!IsActiveInHierarchy(entity))
            return;
        if (!m_PhysicsWorld3D)
            MakePhysicsWorld3D();

        auto& rb = entity.GetComponent<Rigidbody3DComponent>();
        if (rb.RuntimeBodyID != 0xffffffff) return;

        glm::vec3 position, rotation, scale;
        Math::DecomposeTransform(GetWorldTransform(entity), position, rotation, scale);
        scale = glm::abs(scale);

        PhysicsBody3DDesc desc;
        desc.EntityID = entity.GetUUID();
        desc.MotionType = (int)rb.Type;
        desc.Position = position;
        desc.Rotation = glm::quat(rotation);
        desc.Mass = rb.Mass;
        desc.LinearDamping = rb.LinearDamping;
        desc.AngularDamping = rb.AngularDamping;
        desc.GravityFactor = rb.GravityFactor;
        desc.LockRotation = rb.LockRotation;
        desc.Layer = CollisionLayers2D::Clamp(rb.Layer);
        desc.ContinuousCollision = rb.ContinuousCollision;
        desc.AllowSleeping = rb.Sleeping != Rigidbody3DComponent::SleepMode::NeverSleep;
        desc.StartAsleep = rb.Sleeping == Rigidbody3DComponent::SleepMode::StartAsleep;
        // What every collider has: its surface (material asset or own values), trigger flag, kind.
        auto common = [](PhysicsBody3DDesc::Shape& s, const auto& c, int tag) {
            s.Surface = ColliderSurface3D(c);
            s.IsSensor = c.IsSensor;
            s.Tag = tag;
        };

        if (auto* c = entity.TryGetComponent<BoxCollider3DComponent>())
        {
            PhysicsBody3DDesc::Shape s;
            s.Type = PhysicsBody3DDesc::Shape::Kind::Box;
            s.HalfExtents = c->HalfExtents * scale;
            s.Offset = c->Offset * scale;
            common(s, *c, ShapeTagBox3D);
            desc.Shapes.push_back(s);
        }
        if (auto* c = entity.TryGetComponent<SphereCollider3DComponent>())
        {
            PhysicsBody3DDesc::Shape s;
            s.Type = PhysicsBody3DDesc::Shape::Kind::Sphere;
            s.Radius = c->Radius * std::max({ scale.x, scale.y, scale.z });
            s.Offset = c->Offset * scale;
            common(s, *c, ShapeTagSphere3D);
            desc.Shapes.push_back(s);
        }
        if (auto* c = entity.TryGetComponent<CapsuleCollider3DComponent>())
        {
            PhysicsBody3DDesc::Shape s;
            s.Type = PhysicsBody3DDesc::Shape::Kind::Capsule;
            s.Radius = c->Radius * std::max(scale.x, scale.z);
            s.HalfHeight = c->HalfHeight * scale.y;
            s.Offset = c->Offset * scale;
            common(s, *c, ShapeTagCapsule3D);
            desc.Shapes.push_back(s);
        }
        if (auto* c = entity.TryGetComponent<ConvexHullCollider3DComponent>())
        {
            std::string error;
            if (Ref<const ConvexHull3D> hull = GetConvexHullCollider(entity, &error))
            {
                PhysicsBody3DDesc::Shape s;
                s.Type = PhysicsBody3DDesc::Shape::Kind::ConvexHull;
                s.Hull = hull;
                s.Scale = scale;
                s.Offset = c->Offset * scale;
                common(s, *c, ShapeTagConvexHull3D);
                desc.Shapes.push_back(s);
            }
            else
                ZM_CORE_WARN("'{}': Convex Hull Collider 3D qurilmadi - {}", entity.GetName(), error);
        }
        if (auto* c = entity.TryGetComponent<MeshCollider3DComponent>())
        {
            std::string error;
            if (rb.Type == Rigidbody3DComponent::BodyType::Dynamic)
                ZM_CORE_WARN("'{}': Mesh Collider 3D faqat statik yoki kinematik jismda ishlaydi - dinamik jism uni hisobga olmaydi "
                             "(Convex Hull Collider 3D ishlating)", entity.GetName());
            else if (Ref<const CollisionMesh3D> mesh = GetMeshCollider(entity, &error))
            {
                PhysicsBody3DDesc::Shape s;
                s.Type = PhysicsBody3DDesc::Shape::Kind::Mesh;
                s.Mesh = mesh;
                s.Scale = scale;
                s.Offset = c->Offset * scale;
                common(s, *c, ShapeTagMesh3D);
                desc.Shapes.push_back(s);
            }
            else
                ZM_CORE_WARN("'{}': Mesh Collider 3D qurilmadi - {}", entity.GetName(), error);
        }
        if (desc.Shapes.empty())
        {
            if (!entity.HasComponent<MeshCollider3DComponent>())
                ZM_CORE_WARN("'{}': Rigidbody3D uchun collider yo'q (Box/Sphere/Capsule/Convex Hull/Mesh Collider 3D qo'shing)", entity.GetName());
            return;
        }
        rb.RuntimeBodyID = m_PhysicsWorld3D->CreateBody(desc);
    }

    // The model a 3D collider of `entity` is made from: its own ModelPath, else the Mesh Renderer's
    // model or primitive. False (with the reason) when there is none.
    static bool GetColliderSource(Entity entity, const std::string& ownModel, std::string& modelPath, PrimitiveMesh& primitive,
                                  std::string* error)
    {
        modelPath = ownModel;
        primitive = PrimitiveMesh::Model;
        if (modelPath.empty())
            if (const auto* mr = entity.TryGetComponent<MeshRendererComponent>())
            {
                if (mr->Type == PrimitiveMesh::Model)
                    modelPath = mr->ModelPath;
                else
                    primitive = mr->Type;
            }
        if (modelPath.empty() && primitive == PrimitiveMesh::Model)
        {
            if (error)
                *error = "model yo'q (Mesh Renderer qo'shing yoki model faylini tanlang)";
            return false;
        }
        return true;
    }

    Ref<const ConvexHull3D> Scene::GetConvexHullCollider(Entity entity, std::string* error)
    {
        const auto* c = entity ? entity.TryGetComponent<ConvexHullCollider3DComponent>() : nullptr;
        if (!c)
        {
            if (error)
                *error = "Convex Hull Collider 3D yo'q";
            return nullptr;
        }
        std::string modelPath;
        PrimitiveMesh primitive;
        if (!GetColliderSource(entity, c->ModelPath, modelPath, primitive, error))
            return nullptr;
        return CollisionShapes3D::GetConvexHull(modelPath, primitive, c->MaxVertices, error);
    }

    Ref<const CollisionMesh3D> Scene::GetMeshCollider(Entity entity, std::string* error)
    {
        const auto* c = entity ? entity.TryGetComponent<MeshCollider3DComponent>() : nullptr;
        if (!c)
        {
            if (error)
                *error = "Mesh Collider 3D yo'q";
            return nullptr;
        }
        std::string modelPath;
        PrimitiveMesh primitive;
        if (!GetColliderSource(entity, c->ModelPath, modelPath, primitive, error))
            return nullptr;
        return CollisionShapes3D::GetMesh(modelPath, primitive, error);
    }

    void Scene::CreatePhysicsBodies(Entity entity)
    {
        if (!IsActiveInHierarchy(entity))
            return;
        // A 3D body / character spawned into a game that had none (a prefab instance in a 2D or an
        // empty scene): Jolt starts now. The 2D world is there whenever physics runs.
        if (!m_PhysicsWorld3D && m_PhysicsWorld &&
            (entity.HasComponent<Rigidbody3DComponent>() || entity.HasComponent<CharacterController3DComponent>()))
            MakePhysicsWorld3D();
        if (m_PhysicsWorld3D && entity.HasComponent<CharacterController3DComponent>())
            CreateCharacter3D(entity);
        if (m_PhysicsWorld && entity.HasComponent<Rigidbody2DComponent>())
        {
            CreatePhysicsBody(entity);
            m_PhysicsWorld->WakeBodiesOverlapping((b2Body*)entity.GetComponent<Rigidbody2DComponent>().RuntimeBody);
        }
        if (m_PhysicsWorld && entity.HasComponent<TilemapComponent>())
        {
            CreateTilemapBody(entity);
            m_PhysicsWorld->WakeBodiesOverlapping((b2Body*)entity.GetComponent<TilemapComponent>().RuntimeBody);
        }
        if (m_PhysicsWorld3D && entity.HasComponent<Rigidbody3DComponent>())
        {
            CreatePhysicsBody3D(entity);
            // A body that starts asleep waits to be knocked (waking what is around would wake it too).
            const auto& rb = entity.GetComponent<Rigidbody3DComponent>();
            if (rb.Sleeping != Rigidbody3DComponent::SleepMode::StartAsleep)
                m_PhysicsWorld3D->WakeBodiesAround(rb.RuntimeBodyID);
        }
    }

    void Scene::DestroyPhysicsBodies(Entity entity)
    {
        if (m_ContactDispatchDepth > 0)
            m_ContactBodiesReplaced.insert((uint64_t)entity.GetUUID());
        if (auto* rb = entity.TryGetComponent<Rigidbody2DComponent>(); rb && rb->RuntimeBody && m_PhysicsWorld)
        {
            std::vector<PhysicsJoint2DRef> joints;
            m_PhysicsWorld->DestroyBody((b2Body*)rb->RuntimeBody, &joints); // its fixtures and joints go with it
            rb->RuntimeBody = nullptr;
            ForgetJoints2D(joints);
            if (auto* c = entity.TryGetComponent<BoxCollider2DComponent>()) c->RuntimeFixture = nullptr;
            if (auto* c = entity.TryGetComponent<CircleCollider2DComponent>()) c->RuntimeFixture = nullptr;
            if (auto* c = entity.TryGetComponent<PolygonCollider2DComponent>()) { c->RuntimeFixture = nullptr; c->RuntimePieces = 0; }
            if (auto* c = entity.TryGetComponent<EdgeCollider2DComponent>()) c->RuntimeFixture = nullptr;
        }
        if (auto* tm = entity.TryGetComponent<TilemapComponent>(); tm && tm->RuntimeBody && m_PhysicsWorld)
        {
            std::vector<PhysicsJoint2DRef> joints;
            m_PhysicsWorld->DestroyBody((b2Body*)tm->RuntimeBody, &joints);
            tm->RuntimeBody = nullptr;
            ForgetJoints2D(joints);
        }
        if (auto* rb = entity.TryGetComponent<Rigidbody3DComponent>(); rb && rb->RuntimeBodyID != 0xffffffff && m_PhysicsWorld3D)
        {
            std::vector<PhysicsJoint3DRef> joints;
            m_PhysicsWorld3D->DestroyBody(rb->RuntimeBodyID, &joints); // its joints go with it
            rb->RuntimeBodyID = 0xffffffff;
            ForgetJoints3D(joints);
        }
        if (auto* cc = entity.TryGetComponent<CharacterController3DComponent>(); cc && cc->RuntimeID != 0xffffffff && m_PhysicsWorld3D)
        {
            m_PhysicsWorld3D->DestroyCharacter(cc->RuntimeID);
            cc->RuntimeID = 0xffffffff;
            cc->Grounded = false;
            cc->Velocity = glm::vec3(0.0f); // no body: it does not move
        }
    }

    void Scene::DeliverContact(Entity self, Entity other, bool begin, bool sensor)
    {
        if (!self.IsValid() || !other.IsValid()) return;
        auto* nsc = self.TryGetComponent<NativeScriptComponent>();
        if (!nsc || !nsc->Instance || nsc->Instance->m_Destroying) return;
        ScriptableEntity* s = nsc->Instance;
        if (sensor) begin ? s->OnTriggerBegin(other) : s->OnTriggerEnd(other);
        else        begin ? s->OnCollisionBegin(other) : s->OnCollisionEnd(other);
    }

    void Scene::DispatchContactEvents(const std::vector<ContactEvent>& events)
    {
        if (!m_ScriptsRunning) return;
        m_ContactDispatchDepth++;
        for (const auto& ev : events)
        {
            uint64_t idA = (uint64_t)ev.A, idB = (uint64_t)ev.B;
            if (idA == idB) continue;
            // An earlier callback of this batch switched one of them off (and maybe on again):
            // its contacts were ended then (EndContactsOf) and a new body reports its own.
            if (!m_ContactBodiesReplaced.empty() && (m_ContactBodiesReplaced.contains(idA) || m_ContactBodiesReplaced.contains(idB)))
                continue;
            Entity a = GetEntityByUUID(ev.A), b = GetEntityByUUID(ev.B);
            if (!a || !b) continue;

            // One Begin/End per entity pair (and kind): several fixtures, tilemap tiles or a
            // character with its inner body produce several physics contacts.
            auto key = std::make_tuple(std::min(idA, idB), std::max(idA, idB), ev.Sensor);
            if (ev.Begin)
            {
                // Switched off by an earlier callback of this batch (its body is gone already).
                if (!IsActiveInHierarchy(a) || !IsActiveInHierarchy(b)) continue;
                if (m_ActiveContacts[key]++ > 0) continue;
            }
            else
            {
                auto it = m_ActiveContacts.find(key);
                if (it == m_ActiveContacts.end()) continue; // its Begin was never delivered
                if (--it->second > 0) continue;
                m_ActiveContacts.erase(it);
            }
            DeliverContact(a, b, ev.Begin, ev.Sensor);
            DeliverContact(b, a, ev.Begin, ev.Sensor);
        }
        if (--m_ContactDispatchDepth == 0)
            m_ContactBodiesReplaced.clear();
    }

    void Scene::EndContactsOf(Entity entity)
    {
        if (!m_ScriptsRunning || m_ActiveContacts.empty()) return;
        uint64_t id = (uint64_t)entity.GetUUID();
        std::vector<std::pair<uint64_t, bool>> ended; // (other entity, sensor)
        for (auto it = m_ActiveContacts.begin(); it != m_ActiveContacts.end();)
        {
            auto [lo, hi, sensor] = it->first;
            if (lo == id || hi == id)
            {
                ended.emplace_back(lo == id ? hi : lo, sensor);
                it = m_ActiveContacts.erase(it);
            }
            else
                ++it;
        }
        // Only the survivors hear about it: `entity` is being destroyed (still valid here).
        for (auto [other, sensor] : ended)
            DeliverContact(GetEntityByUUID(other), entity, false, sensor);
    }

    // Writes a simulated world pose back into the (possibly parented) transform.
    static void ApplyWorldPose(Scene& scene, Entity e, const glm::vec3& position, const glm::quat& rotation)
    {
        auto& t = e.GetComponent<TransformComponent>();
        if (!scene.GetParent(e))
        {
            t.Translation = position;
            t.Rotation = glm::eulerAngles(rotation);
            return;
        }
        glm::mat4 world = glm::translate(glm::mat4(1.0f), position) * glm::toMat4(rotation) * glm::scale(glm::mat4(1.0f), t.Scale);
        glm::vec3 keepScale = t.Scale;
        scene.SetWorldTransform(e, world);
        t.Scale = keepScale;
    }

    void Scene::ApplyEffectors2D(float dt)
    {
        if (!m_PhysicsWorld)
            return;
        for (auto e : m_Registry.view<AreaEffector2DComponent>())
        {
            auto& c = m_Registry.get<AreaEffector2DComponent>(e);
            const auto* rb = m_Registry.try_get<Rigidbody2DComponent>(e);
            c.AffectedBodies = 0;
            if (!rb || !rb->RuntimeBody)
                continue;
            b2Body* body = (b2Body*)rb->RuntimeBody;
            const float angle = glm::radians(c.ForceAngle) + (c.UseGlobalAngle ? 0.0f : PhysicsWorld2D::GetAngle(body));
            PhysicsAreaEffector2D s;
            s.Force = glm::vec2(std::cos(angle), std::sin(angle)) * c.ForceMagnitude;
            s.Variation = std::max(0.0f, c.ForceVariation);
            s.ScaleWithMass = c.ScaleWithMass;
            s.Drag = std::max(0.0f, c.Drag);
            s.AngularDrag = std::max(0.0f, c.AngularDrag);
            s.LayerMask = c.ColliderMask;
            c.AffectedBodies = m_PhysicsWorld->ApplyAreaEffector(body, s, dt).Bodies;
        }
        for (auto e : m_Registry.view<BuoyancyEffector2DComponent>())
        {
            auto& c = m_Registry.get<BuoyancyEffector2DComponent>(e);
            const auto* rb = m_Registry.try_get<Rigidbody2DComponent>(e);
            c.AffectedBodies = 0;
            c.SubmergedArea = 0.0f;
            if (!rb || !rb->RuntimeBody)
                continue;
            const float angle = glm::radians(c.FlowAngle);
            PhysicsBuoyancy2D s;
            s.SurfaceLevel = c.SurfaceLevel;
            s.Density = std::max(0.0f, c.Density);
            s.LinearDrag = std::max(0.0f, c.LinearDrag);
            s.AngularDrag = std::max(0.0f, c.AngularDrag);
            s.Flow = glm::vec2(std::cos(angle), std::sin(angle)) * c.FlowMagnitude;
            s.FlowVariation = std::max(0.0f, c.FlowVariation);
            s.LayerMask = c.ColliderMask;
            const PhysicsEffectorResult2D result = m_PhysicsWorld->ApplyBuoyancy((b2Body*)rb->RuntimeBody, s, dt);
            c.AffectedBodies = result.Bodies;
            c.SubmergedArea = result.SubmergedArea;
        }
    }

    // ------------------------------------------------------------------ physics settings, 2D sleeping
    void SceneSettings::PhysicsStepSettings::Sanitize()
    {
        StepRate = std::clamp(StepRate, MinRate, MaxRate);
        MaxStepsPerFrame = std::clamp(MaxStepsPerFrame, 1, MaxSteps);
    }

    void SceneSettings::Physics2DSettings::Sanitize()
    {
        VelocityIterations = std::clamp(VelocityIterations, 1, MaxIterations);
        PositionIterations = std::clamp(PositionIterations, 1, MaxIterations);
        SubSteps = std::clamp(SubSteps, 1, MaxSubSteps);
        TimeToSleep = std::isfinite(TimeToSleep) ? std::clamp(TimeToSleep, 0.05f, 60.0f) : 0.5f;
    }

    void SceneSettings::Physics3DSettings::Sanitize()
    {
        TimeToSleep = std::isfinite(TimeToSleep) ? std::clamp(TimeToSleep, 0.05f, 60.0f) : 0.5f;
    }

    bool Scene::IsSleeping3D(Entity entity)
    {
        const auto* rb = entity ? entity.TryGetComponent<Rigidbody3DComponent>() : nullptr;
        if (!m_PhysicsWorld3D || !rb || rb->RuntimeBodyID == 0xffffffff)
            return false;
        return m_PhysicsWorld3D->IsBodySleeping(rb->RuntimeBodyID);
    }

    bool Scene::SetAwake3D(Entity entity, bool awake)
    {
        const auto* rb = entity ? entity.TryGetComponent<Rigidbody3DComponent>() : nullptr;
        if (!m_PhysicsWorld3D || !rb || rb->RuntimeBodyID == 0xffffffff)
            return false;
        return m_PhysicsWorld3D->SetBodyAwake(rb->RuntimeBodyID, awake);
    }

    PhysicsStats3D Scene::GetPhysics3DStats() const
    {
        return m_PhysicsWorld3D ? m_PhysicsWorld3D->GetStats() : PhysicsStats3D();
    }

    bool Scene::IsSleeping2D(Entity entity)
    {
        const auto* rb = entity ? entity.TryGetComponent<Rigidbody2DComponent>() : nullptr;
        if (!m_PhysicsWorld || !rb || !rb->RuntimeBody || rb->Type == Rigidbody2DComponent::BodyType::Static)
            return false;
        const b2Body* body = (const b2Body*)rb->RuntimeBody;
        return PhysicsWorld2D::IsEnabled(body) && !PhysicsWorld2D::IsAwake(body);
    }

    bool Scene::SetAwake2D(Entity entity, bool awake)
    {
        auto* rb = entity ? entity.TryGetComponent<Rigidbody2DComponent>() : nullptr;
        if (!m_PhysicsWorld || !rb || !rb->RuntimeBody || rb->Type == Rigidbody2DComponent::BodyType::Static)
            return false;
        m_PhysicsWorld->SetAwake((b2Body*)rb->RuntimeBody, awake);
        return true;
    }

    bool Scene::ApplyForce2D(Entity entity, const glm::vec2& force)
    {
        const auto* rb = entity ? entity.TryGetComponent<Rigidbody2DComponent>() : nullptr;
        if (!m_PhysicsWorld || !rb || !rb->RuntimeBody || rb->Type != Rigidbody2DComponent::BodyType::Dynamic)
            return false;
        if (!std::isfinite(force.x) || !std::isfinite(force.y))
            return false;
        b2Body* body = (b2Body*)rb->RuntimeBody;
        if (!PhysicsWorld2D::IsAwake(body))
            m_PhysicsWorld->SetAwake(body, true); // as b2Body::ApplyForce(..., wake = true)
        m_ScriptForces2D[(entt::entity)entity].Frame += force;
        return true;
    }

    void Scene::ApplyScriptForces2D()
    {
        if (m_ScriptForces2D.empty() || m_ScriptForceTime2D <= 0.0f)
            return;
        const float invTime = 1.0f / m_ScriptForceTime2D;
        for (const auto& [e, f] : m_ScriptForces2D)
        {
            const auto* rb = m_Registry.valid(e) ? m_Registry.try_get<Rigidbody2DComponent>(e) : nullptr;
            if (!rb || !rb->RuntimeBody || f.Impulse == glm::vec2(0.0f))
                continue;
            PhysicsWorld2D::ApplyForceToCenter((b2Body*)rb->RuntimeBody, f.Impulse * invTime);
        }
    }

    PhysicsStats2D Scene::GetPhysics2DStats() const
    {
        return m_PhysicsWorld ? m_PhysicsWorld->GetStats() : PhysicsStats2D();
    }

    bool Scene::DropThroughPlatforms(Entity entity, float seconds)
    {
        if (!m_PhysicsWorld || !entity || !entity.HasComponent<Rigidbody2DComponent>())
            return false;
        m_PhysicsWorld->DropThroughPlatforms(entity.GetUUID(), (b2Body*)entity.GetComponent<Rigidbody2DComponent>().RuntimeBody, seconds);
        return true;
    }

    void Scene::StepPhysics(Timestep ts)
    {
        m_Physics2DStepsLastFrame = 0;
        if (!m_PhysicsWorld && !m_PhysicsWorld3D) return;
        ZM_PROFILE_SCOPE("Fizika");
        // The settings may change while the game runs (the editor's scene settings, scripts).
        m_Settings.PhysicsStep.Sanitize();
        m_Settings.Physics2D.Sanitize();
        const float fixedStep = m_Settings.PhysicsStep.GetFixedStep();
        const int maxSteps = m_Settings.PhysicsStep.MaxStepsPerFrame;
        const SceneSettings::Physics2DSettings& physics2D = m_Settings.Physics2D;
        const float subStep = fixedStep / (float)physics2D.SubSteps;
        const bool debugContacts = DebugDraw::IsShowingContacts();
        if (m_PhysicsWorld3D)
        {
            m_PhysicsWorld3D->SetDebugContactsEnabled(debugContacts);
            // The project's layer matrix (Project settings) and the bodies' layers (inspector, scripts)
            // may change while the game runs.
            m_PhysicsWorld3D->SetCollisionMatrix(Project::GetCollisionLayers2D().Masks);
            // So may the sleeping settings, continuous collision and how characters push.
            m_Settings.Physics3D.Sanitize();
            m_PhysicsWorld3D->SetSleeping(m_Settings.Physics3D.AllowSleep, m_Settings.Physics3D.TimeToSleep);
            for (auto e : m_Registry.view<Rigidbody3DComponent>())
                if (const auto& rb = m_Registry.get<Rigidbody3DComponent>(e); rb.RuntimeBodyID != 0xffffffff)
                {
                    m_PhysicsWorld3D->SetBodyLayer(rb.RuntimeBodyID, CollisionLayers2D::Clamp(rb.Layer));
                    m_PhysicsWorld3D->SetBodyAllowSleeping(rb.RuntimeBodyID, rb.Sleeping != Rigidbody3DComponent::SleepMode::NeverSleep);
                    m_PhysicsWorld3D->SetBodyContinuous(rb.RuntimeBodyID, rb.ContinuousCollision);
                }
            for (auto e : m_Registry.view<CharacterController3DComponent>())
                if (const auto& cc = m_Registry.get<CharacterController3DComponent>(e); cc.RuntimeID != 0xffffffff)
                {
                    m_PhysicsWorld3D->SetCharacterLayer(cc.RuntimeID, CollisionLayers2D::Clamp(cc.Layer));
                    m_PhysicsWorld3D->SetCharacterPush(cc.RuntimeID, cc.PushBodies, cc.PushForce);
                }
        }

        // Kinematic/static bodies follow their transforms (lets scripts move them directly).
        if (m_PhysicsWorld)
        {
            m_PhysicsWorld->SetSleeping(physics2D.AllowSleep, physics2D.TimeToSleep);
            for (auto e : m_Registry.view<Rigidbody2DComponent>())
            {
                auto& rb2d = m_Registry.get<Rigidbody2DComponent>(e);
                if (!rb2d.RuntimeBody)
                    continue;
                m_PhysicsWorld->SetCanSleep((b2Body*)rb2d.RuntimeBody, rb2d.Sleeping != Rigidbody2DComponent::SleepMode::NeverSleep);
                if (rb2d.Type == Rigidbody2DComponent::BodyType::Dynamic) continue;
                glm::vec3 p = GetWorldPosition({ e, this });
                float angle = m_Registry.get<TransformComponent>(e).Rotation.z;
                b2Body* body = (b2Body*)rb2d.RuntimeBody;
                const glm::vec2 bp = PhysicsWorld2D::GetPosition(body);
                if (bp.x != p.x || bp.y != p.y || PhysicsWorld2D::GetAngle(body) != angle)
                {
                    PhysicsWorld2D::SetTransform(body, { p.x, p.y }, angle);
                    // Moved: not keeping still (Box2D does not wake a body it teleports; a sleeping
                    // kinematic body's contacts would not be updated). Static bodies never sleep.
                    if (rb2d.Type == Rigidbody2DComponent::BodyType::Kinematic)
                        m_PhysicsWorld->SetAwake(body, true);
                }
            }
        }
        // Joints whose bodies appeared since the last frame, live motor / limit settings.
        SyncJoints2D();
        SyncJoints3D();
        // Physics material assets edited / reloaded since the last frame.
        if ((m_PhysicsWorld || m_PhysicsWorld3D) && m_PhysicsMaterialRevision != PhysicsMaterialLibrary::GetRevision())
            ApplyPhysicsMaterials();
        m_PhysicsAccumulator += ts;
        int stepCount = std::min(maxSteps, (int)(m_PhysicsAccumulator / fixedStep));
        // Script forces (ApplyForce2D): Box2D forgets a body's force after every b2World::Step, so
        // this frame's forces join the time-weighted sum of the frames not simulated yet, and every
        // step and sub-step below applies their average - a steady force acts the same at any frame
        // rate, step rate and sub-step count.
        if (m_PhysicsWorld)
        {
            m_ScriptForceTime2D += ts;
            for (auto& [e, f] : m_ScriptForces2D)
            {
                f.Impulse += f.Frame * (float)ts;
                f.Frame = glm::vec2(0.0f);
            }
        }

        // Kinematic 3D bodies: velocity is chosen so the target is reached after this frame's steps.
        if (m_PhysicsWorld3D && stepCount > 0)
        {
            float frameDt = stepCount * fixedStep;
            for (auto e : m_Registry.view<Rigidbody3DComponent>())
            {
                auto& rb = m_Registry.get<Rigidbody3DComponent>(e);
                if (rb.RuntimeBodyID == 0xffffffff || rb.Type == Rigidbody3DComponent::BodyType::Dynamic) continue;
                glm::vec3 p, r, sc;
                Math::DecomposeTransform(GetWorldTransform({ e, this }), p, r, sc);
                if (glm::any(glm::isnan(p)) || glm::any(glm::isnan(r))) continue;
                glm::vec3 bp; glm::quat bq;
                m_PhysicsWorld3D->GetTransform(rb.RuntimeBodyID, bp, bq);
                glm::quat q(r);
                bool moved = glm::length(bp - p) > 1e-5f || std::abs(glm::dot(bq, q)) < 0.99999f;
                if (rb.Type == Rigidbody3DComponent::BodyType::Kinematic)
                {
                    if (moved) m_PhysicsWorld3D->MoveKinematic(rb.RuntimeBodyID, p, q, frameDt);
                    else m_PhysicsWorld3D->SetLinearVelocity(rb.RuntimeBodyID, glm::vec3(0.0f));
                }
                else if (moved)
                    m_PhysicsWorld3D->SetTransform(rb.RuntimeBodyID, p, q);
            }
        }

        int steps = 0;
        while (m_PhysicsAccumulator >= fixedStep && steps < maxSteps)
        {
            if (m_PhysicsWorld)
            {
                ZM_PROFILE_SCOPE("Box2D (2D)");
                for (int sub = 0; sub < physics2D.SubSteps; sub++)
                {
                    ApplyEffectors2D(subStep);
                    ApplyScriptForces2D();
                    m_PhysicsWorld->Step(subStep, physics2D.VelocityIterations, physics2D.PositionIterations);
                    CheckJointBreaks2D(subStep);
                    m_Physics2DStepsLastFrame++;
                }
            }
            if (m_PhysicsWorld3D)
            {
                {
                    ZM_PROFILE_SCOPE("Jolt (3D)");
                    m_PhysicsWorld3D->Step(fixedStep);
                    CheckJointBreaks3D(fixedStep);
                }
                ZM_PROFILE_SCOPE("Personajlar (3D)");
                StepCharacters(fixedStep);
            }
            m_PhysicsAccumulator -= fixedStep;
            steps++;
        }
        if (steps == maxSteps)
            m_PhysicsAccumulator = 0.0f;
        if (steps > 0)
        {
            UpdateJointStates2D(subStep); // the reaction forces come from the last Box2D step's impulses
            UpdateJointStates3D(fixedStep);
            // The frames' script forces have acted; forces applied during the steps (joint break
            // callbacks) wait for the next frame.
            m_ScriptForceTime2D = 0.0f;
            std::erase_if(m_ScriptForces2D, [](auto& item) {
                item.second.Impulse = glm::vec2(0.0f);
                return item.second.Frame == glm::vec2(0.0f);
            });
        }

        if (m_PhysicsWorld)
        {
            for (auto e : m_Registry.view<Rigidbody2DComponent>())
            {
                auto& rb2d = m_Registry.get<Rigidbody2DComponent>(e);
                if (!rb2d.RuntimeBody || rb2d.Type != Rigidbody2DComponent::BodyType::Dynamic) continue;
                const b2Body* body = (const b2Body*)rb2d.RuntimeBody;
                const glm::vec2 position = PhysicsWorld2D::GetPosition(body);
                const float angle = PhysicsWorld2D::GetAngle(body);
                Entity entity{ e, this };
                if (!GetParent(entity))
                {
                    auto& t = m_Registry.get<TransformComponent>(e);
                    t.Translation.x = position.x;
                    t.Translation.y = position.y;
                    t.Rotation.z = angle;
                }
                else
                {
                    glm::vec3 wp = GetWorldPosition(entity);
                    ApplyWorldPose(*this, entity, { position.x, position.y, wp.z }, glm::quat(glm::vec3(0, 0, angle)));
                }
            }
        }
        if (m_PhysicsWorld3D)
        {
            for (auto e : m_Registry.view<Rigidbody3DComponent>())
            {
                auto& rb = m_Registry.get<Rigidbody3DComponent>(e);
                if (rb.RuntimeBodyID == 0xffffffff || rb.Type != Rigidbody3DComponent::BodyType::Dynamic) continue;
                glm::vec3 p; glm::quat q;
                m_PhysicsWorld3D->GetTransform(rb.RuntimeBodyID, p, q);
                ApplyWorldPose(*this, { e, this }, p, q);
            }
        }

        // Dispatch collision events after the step (worlds are unlocked now).
        std::vector<ContactEvent> events;
        if (m_PhysicsWorld)
            events = m_PhysicsWorld->TakeEvents();
        if (m_PhysicsWorld3D)
        {
            auto e3 = m_PhysicsWorld3D->TakeEvents();
            events.insert(events.end(), e3.begin(), e3.end());
        }
        DispatchContactEvents(events);
        DispatchJointBreaks2D();
        DispatchJointBreaks3D();

        if (debugContacts)
            DrawPhysicsContacts();
        if (DebugDraw::IsShowingJoints())
            DrawPhysicsJoints();
        if (DebugDraw::IsShowingVelocities() || DebugDraw::IsShowingBodyVelocities())
            DrawPhysicsVelocities();
    }

    void Scene::DrawPhysicsVelocities()
    {
        if (DebugDraw::IsShowingVelocities() && m_PhysicsWorld3D)
            for (auto e : m_Registry.view<CharacterController3DComponent>())
            {
                const auto& cc = m_Registry.get<CharacterController3DComponent>(e);
                if (cc.RuntimeID == 0xffffffff)
                    continue;
                // From the middle of the capsule (the entity stands on its feet).
                glm::vec3 center = m_PhysicsWorld3D->GetCharacterPosition(cc.RuntimeID) + glm::vec3(0.0f, cc.Height * 0.5f, 0.0f);
                glm::vec3 desired = cc.MoveVelocity + glm::vec3(0.0f, cc.VerticalVelocity, 0.0f);
                DebugDraw::CharacterVelocity(center, cc.Velocity, desired);
            }
        if (!DebugDraw::IsShowingBodyVelocities())
            return;
        if (m_PhysicsWorld)
            for (auto e : m_Registry.view<Rigidbody2DComponent>())
            {
                const auto& rb = m_Registry.get<Rigidbody2DComponent>(e);
                if (!rb.RuntimeBody || rb.Type != Rigidbody2DComponent::BodyType::Dynamic)
                    continue;
                const b2Body* body = (const b2Body*)rb.RuntimeBody;
                if (!PhysicsWorld2D::IsEnabled(body) || !PhysicsWorld2D::IsAwake(body))
                    continue;
                const glm::vec2 c = PhysicsWorld2D::GetWorldCenter(body), v = PhysicsWorld2D::GetLinearVelocity(body);
                DebugDraw::BodyVelocity({ c.x, c.y, GetWorldPosition({ e, this }).z }, { v.x, v.y, 0.0f });
            }
        if (m_PhysicsWorld3D)
            for (auto e : m_Registry.view<Rigidbody3DComponent>())
            {
                const auto& rb = m_Registry.get<Rigidbody3DComponent>(e);
                if (rb.RuntimeBodyID == 0xffffffff || rb.Type != Rigidbody3DComponent::BodyType::Dynamic)
                    continue;
                glm::vec3 p; glm::quat q;
                m_PhysicsWorld3D->GetTransform(rb.RuntimeBodyID, p, q);
                DebugDraw::BodyVelocity(p, m_PhysicsWorld3D->GetLinearVelocity(rb.RuntimeBodyID));
            }
    }

    void Scene::DrawPhysicsContacts()
    {
        // Every frame (also one without a physics step): the contacts as they are now.
        std::vector<PhysicsContactPoint> contacts;
        if (m_PhysicsWorld)
        {
            std::vector<PhysicsContactPoint2D> points;
            m_PhysicsWorld->GetDebugContacts(points);
            contacts.reserve(points.size());
            UUID lastEntity = 0;
            float z = 0.0f;
            for (const PhysicsContactPoint2D& p : points)
            {
                // The 2D world is flat: the points get the depth of the first body's entity.
                if (p.EntityA != lastEntity || &p == points.data())
                {
                    lastEntity = p.EntityA;
                    z = 0.0f;
                    if (auto it = m_EntityMap.find(p.EntityA); it != m_EntityMap.end() && m_Registry.valid(it->second))
                        z = GetWorldPosition({ it->second, this }).z;
                }
                PhysicsContactPoint c;
                c.Position = { p.Position.x, p.Position.y, z };
                c.Normal = { p.Normal.x, p.Normal.y, 0.0f };
                c.Penetration = p.Penetration;
                c.Impulse = p.Impulse;
                c.EntityA = p.EntityA;
                c.EntityB = p.EntityB;
                c.Sleeping = p.Sleeping;
                contacts.push_back(c);
            }
        }
        if (m_PhysicsWorld3D)
            m_PhysicsWorld3D->GetDebugContacts(contacts);
        DebugDraw::PhysicsContacts(contacts);
    }

    void Scene::DrawPhysicsJoints()
    {
        auto load = [](const auto& joint) {
            if (joint.BreakForce <= 0.0f && joint.BreakTorque <= 0.0f)
                return -1.0f;
            float l = 0.0f;
            if (joint.BreakForce > 0.0f)
                l = std::max(l, joint.ReactionForce / joint.BreakForce);
            if (joint.BreakTorque > 0.0f)
                l = std::max(l, joint.ReactionTorque / joint.BreakTorque);
            return l;
        };
        auto connectedOf = [&](const EntityRef& ref, Entity self) {
            Entity other = ref.ID ? GetEntityByUUID(ref.ID) : Entity();
            return other == self ? Entity() : other;
        };
        if (m_PhysicsWorld)
            ForEachJoint2DType([&](auto type) {
                using T = typename decltype(type)::type;
                for (auto e : m_Registry.view<T>())
                {
                    const auto& joint = m_Registry.get<T>(e);
                    if (!joint.RuntimeJoint && !joint.Broken)
                        continue;
                    Entity entity{ e, this };
                    // A 2D body's frame: its position and turn (anchors and axes are unscaled).
                    glm::vec3 position, rotation, scale;
                    Math::DecomposeTransform(GetWorldTransform(entity), position, rotation, scale);
                    const float cs = std::cos(rotation.z), sn = std::sin(rotation.z);
                    auto turn = [&](const glm::vec2& v) { return glm::vec2(cs * v.x - sn * v.y, sn * v.x + cs * v.y); };
                    PhysicsJointDebug d;
                    d.BodyCenter = position;
                    if (joint.Broken)
                    {
                        // Where it was attached: the anchor on this body now.
                        d.Broken = true;
                        d.Anchor = { glm::vec2(position) + turn(joint.Anchor), position.z };
                        DebugDraw::PhysicsJoint(d);
                        continue;
                    }
                    glm::vec2 anchor, connected;
                    PhysicsWorld2D::GetJointAnchors((const b2Joint*)joint.RuntimeJoint, anchor, connected);
                    d.Anchor = { anchor, d.BodyCenter.z };
                    d.ConnectedAnchor = { connected, d.BodyCenter.z };
                    if (Entity other = connectedOf(joint.ConnectedBody, entity))
                    {
                        d.HasConnectedBody = true;
                        d.ConnectedCenter = GetWorldPosition(other);
                        d.ConnectedCenter.z = d.BodyCenter.z;
                    }
                    if constexpr (std::is_same_v<T, PrismaticJoint2DComponent> || std::is_same_v<T, WheelJoint2DComponent>)
                    {
                        d.Axis = { turn(joint.Axis), 0.0f };
                        d.AnchorsMeet = false;
                    }
                    else if constexpr (std::is_same_v<T, DistanceJoint2DComponent>)
                        d.AnchorsMeet = false;
                    d.Force = joint.ReactionForce;
                    d.Torque = joint.ReactionTorque;
                    d.Load = load(joint);
                    DebugDraw::PhysicsJoint(d);
                }
            });
        if (m_PhysicsWorld3D)
            ForEachJoint3DType([&](auto type) {
                using T = typename decltype(type)::type;
                for (auto e : m_Registry.view<T>())
                {
                    const auto& joint = m_Registry.get<T>(e);
                    if (!joint.RuntimeJoint && !joint.Broken)
                        continue;
                    Entity entity{ e, this };
                    const glm::mat4 world = GetWorldTransform(entity);
                    PhysicsJointDebug d;
                    d.BodyCenter = glm::vec3(world[3]);
                    glm::vec3 axis(0.0f);
                    if (joint.Broken || !m_PhysicsWorld3D->GetJointFrame(joint.RuntimeJoint, d.Anchor, d.ConnectedAnchor, axis))
                    {
                        d.Broken = joint.Broken;
                        d.Anchor = glm::vec3(world * glm::vec4(joint.Anchor, 1.0f));
                        if (d.Broken)
                            DebugDraw::PhysicsJoint(d);
                        continue;
                    }
                    if (Entity other = connectedOf(joint.ConnectedBody, entity))
                    {
                        d.HasConnectedBody = true;
                        d.ConnectedCenter = GetWorldPosition(other);
                    }
                    if constexpr (std::is_same_v<T, HingeJoint3DComponent> || std::is_same_v<T, SliderJoint3DComponent> ||
                                  std::is_same_v<T, ConeJoint3DComponent>)
                        d.Axis = axis;
                    d.AnchorsMeet = !std::is_same_v<T, DistanceJoint3DComponent> && !std::is_same_v<T, SliderJoint3DComponent>;
                    d.Force = joint.ReactionForce;
                    d.Torque = joint.ReactionTorque;
                    d.Load = load(joint);
                    DebugDraw::PhysicsJoint(d);
                }
            });
    }

    void Scene::StepCharacters(float dt)
    {
        for (auto e : m_Registry.view<CharacterController3DComponent>())
        {
            auto& cc = m_Registry.get<CharacterController3DComponent>(e);
            if (cc.RuntimeID == 0xffffffff) continue;
            Entity entity{ e, this };
            // Teleports done by scripts (transform changed) are applied to the character.
            glm::vec3 wp = GetWorldPosition(entity);
            glm::vec3 cp = m_PhysicsWorld3D->GetCharacterPosition(cc.RuntimeID);
            if (glm::length(wp - cp) > 0.01f) m_PhysicsWorld3D->SetCharacterPosition(cc.RuntimeID, wp);
            const glm::vec3 start = m_PhysicsWorld3D->GetCharacterPosition(cc.RuntimeID);

            bool grounded = m_PhysicsWorld3D->IsCharacterGrounded(cc.RuntimeID);
            if (grounded && cc.VerticalVelocity <= 0.0f)
                cc.VerticalVelocity = 0.0f;
            else
                cc.VerticalVelocity += m_Settings.Gravity3D.y * cc.GravityScale * dt;
            glm::vec3 velocity = cc.MoveVelocity + glm::vec3(0.0f, cc.VerticalVelocity, 0.0f);
            if (grounded) velocity += m_PhysicsWorld3D->GetCharacterGroundVelocity(cc.RuntimeID);

            m_PhysicsWorld3D->UpdateCharacter(cc.RuntimeID, velocity, dt, cc.StepHeight, cc.StickToFloor && cc.VerticalVelocity <= 0.0f);
            cc.Grounded = m_PhysicsWorld3D->IsCharacterGrounded(cc.RuntimeID);
            glm::vec3 newVel = m_PhysicsWorld3D->GetCharacterVelocity(cc.RuntimeID);
            if (cc.VerticalVelocity > 0.0f && newVel.y < cc.VerticalVelocity * 0.5f)
                cc.VerticalVelocity = std::min(cc.VerticalVelocity, std::max(0.0f, newVel.y)); // bonked the ceiling

            glm::vec3 p = m_PhysicsWorld3D->GetCharacterPosition(cc.RuntimeID);
            cc.Velocity = dt > 0.0f ? (p - start) / dt : glm::vec3(0.0f);
            if (!GetParent(entity)) m_Registry.get<TransformComponent>(e).Translation = p;
            else
            {
                auto& t = m_Registry.get<TransformComponent>(e);
                glm::mat4 parentInv = glm::inverse(GetWorldTransform(GetParent(entity)));
                t.Translation = glm::vec3(parentInv * glm::vec4(p, 1.0f));
            }
        }
    }

    // ------------------------------------------------------------------ queries
    RaycastHit Scene::Raycast2D(const glm::vec2& origin, const glm::vec2& direction, float maxDistance, uint16_t layerMask)
    {
        if (!m_PhysicsWorld || glm::length(direction) == 0.0f) return {};
        RaycastHit hit = m_PhysicsWorld->CastRay(origin, direction, maxDistance, layerMask);
        DebugDraw::PhysicsRaycast(glm::vec3(origin, 0.0f), glm::vec3(glm::normalize(direction), 0.0f), maxDistance, hit);
        return hit;
    }

    std::vector<RaycastHit> Scene::Raycast2DAll(const glm::vec2& origin, const glm::vec2& direction, float maxDistance, uint16_t layerMask,
                                                bool includeTriggers)
    {
        if (!m_PhysicsWorld || glm::length(direction) == 0.0f) return {};
        std::vector<RaycastHit> hits = m_PhysicsWorld->CastRayAll(origin, direction, maxDistance, layerMask, includeTriggers);
        DebugDraw::PhysicsRaycastAll(glm::vec3(origin, 0.0f), glm::vec3(glm::normalize(direction), 0.0f), maxDistance, hits);
        return hits;
    }

    // The entities of an overlap query (IDs of the running world) and its debug drawing.
    static std::vector<Entity> ResolveOverlap(Scene& scene, const std::vector<UUID>& ids, const glm::vec2& center,
                                              const std::vector<glm::vec3>& outline)
    {
        std::vector<Entity> out;
        std::vector<glm::vec3> positions;
        out.reserve(ids.size());
        for (UUID id : ids)
            if (Entity e = scene.GetEntityByUUID(id))
            {
                out.push_back(e);
                if (DebugDraw::IsShowingRaycasts())
                    positions.push_back(scene.GetWorldPosition(e));
            }
        DebugDraw::PhysicsOverlap(glm::vec3(center, 0.0f), outline, positions);
        return out;
    }

    std::vector<Entity> Scene::OverlapCircle2D(const glm::vec2& center, float radius, uint16_t layerMask, bool includeTriggers)
    {
        if (!m_PhysicsWorld) return {};
        std::vector<glm::vec3> outline;
        if (DebugDraw::IsShowingRaycasts())
            for (const glm::vec2& p : Polygon2D::CirclePolygon(center, std::max(radius, 0.0f), 32))
                outline.push_back(glm::vec3(p, 0.0f));
        return ResolveOverlap(*this, m_PhysicsWorld->OverlapCircle(center, radius, layerMask, includeTriggers), center, outline);
    }

    std::vector<Entity> Scene::OverlapBox2D(const glm::vec2& center, const glm::vec2& halfExtents, float angleDegrees, uint16_t layerMask,
                                            bool includeTriggers)
    {
        if (!m_PhysicsWorld) return {};
        const float angle = glm::radians(angleDegrees);
        std::vector<glm::vec3> outline;
        if (DebugDraw::IsShowingRaycasts())
        {
            const glm::vec2 x(std::cos(angle), std::sin(angle)), y(-x.y, x.x), h = glm::abs(halfExtents);
            for (const glm::vec2& corner : { -x * h.x - y * h.y, x * h.x - y * h.y, x * h.x + y * h.y, -x * h.x + y * h.y })
                outline.push_back(glm::vec3(center + corner, 0.0f));
        }
        return ResolveOverlap(*this, m_PhysicsWorld->OverlapBox(center, halfExtents, angle, layerMask, includeTriggers), center, outline);
    }

    std::vector<Entity> Scene::OverlapPoint2D(const glm::vec2& point, uint16_t layerMask, bool includeTriggers)
    {
        if (!m_PhysicsWorld) return {};
        return ResolveOverlap(*this, m_PhysicsWorld->OverlapPoint(point, layerMask, includeTriggers), point, {});
    }

    RaycastHit Scene::Raycast3D(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, uint16_t layerMask)
    {
        if (!m_PhysicsWorld3D || glm::length(direction) == 0.0f) return {};
        RaycastHit hit = m_PhysicsWorld3D->CastRay(origin, direction, maxDistance, layerMask, false);
        DebugDraw::PhysicsRaycast(origin, direction, maxDistance, hit);
        return hit;
    }

    std::vector<RaycastHit> Scene::Raycast3DAll(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, uint16_t layerMask,
                                                bool includeTriggers)
    {
        if (!m_PhysicsWorld3D || glm::length(direction) == 0.0f) return {};
        std::vector<RaycastHit> hits = m_PhysicsWorld3D->CastRayAll(origin, direction, maxDistance, layerMask, includeTriggers);
        DebugDraw::PhysicsRaycastAll(origin, glm::normalize(direction), maxDistance, hits);
        return hits;
    }

    // A capsule between the centres of its two end spheres: its centre, rotation (its Y along the
    // two points) and shape.
    static PhysicsQueryShape3D CapsuleBetween(const glm::vec3& point0, const glm::vec3& point1, float radius, glm::vec3& center,
                                              glm::quat& rotation)
    {
        center = (point0 + point1) * 0.5f;
        rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        const glm::vec3 axis = point1 - point0;
        const float length = glm::length(axis);
        if (length > 1e-6f && std::isfinite(length))
        {
            const glm::vec3 dir = axis / length;
            const float cosine = glm::dot(glm::vec3(0.0f, 1.0f, 0.0f), dir);
            if (cosine < -0.99999f)
                rotation = glm::angleAxis(glm::pi<float>(), glm::vec3(1.0f, 0.0f, 0.0f));
            else if (cosine < 0.99999f)
                rotation = glm::angleAxis(std::acos(std::clamp(cosine, -1.0f, 1.0f)), glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), dir)));
        }
        return PhysicsQueryShape3D::Capsule(radius, std::isfinite(length) ? length * 0.5f : 0.0f);
    }

    std::vector<Entity> Scene::Overlap3D(const PhysicsQueryShape3D& shape, const glm::vec3& center, const glm::quat& rotation, uint16_t layerMask,
                                         bool includeTriggers)
    {
        if (!m_PhysicsWorld3D) return {};
        std::vector<Entity> out;
        std::vector<glm::vec3> positions;
        for (UUID id : m_PhysicsWorld3D->Overlap(shape, center, rotation, layerMask, includeTriggers))
            if (Entity e = GetEntityByUUID(id))
            {
                out.push_back(e);
                if (DebugDraw::IsShowingRaycasts())
                    positions.push_back(GetWorldPosition(e));
            }
        DebugDraw::PhysicsOverlap3D(shape, center, rotation, positions);
        return out;
    }

    std::vector<Entity> Scene::OverlapSphere3D(const glm::vec3& center, float radius, uint16_t layerMask, bool includeTriggers)
    {
        return Overlap3D(PhysicsQueryShape3D::Sphere(radius), center, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), layerMask, includeTriggers);
    }

    std::vector<Entity> Scene::OverlapBox3D(const glm::vec3& center, const glm::vec3& halfExtents, const glm::quat& rotation, uint16_t layerMask,
                                            bool includeTriggers)
    {
        return Overlap3D(PhysicsQueryShape3D::Box(halfExtents), center, rotation, layerMask, includeTriggers);
    }

    std::vector<Entity> Scene::OverlapCapsule3D(const glm::vec3& point0, const glm::vec3& point1, float radius, uint16_t layerMask,
                                                bool includeTriggers)
    {
        glm::vec3 center;
        glm::quat rotation;
        const PhysicsQueryShape3D shape = CapsuleBetween(point0, point1, radius, center, rotation);
        return Overlap3D(shape, center, rotation, layerMask, includeTriggers);
    }

    std::vector<RaycastHit> Scene::ShapeCast3D(const PhysicsQueryShape3D& shape, const glm::vec3& origin, const glm::quat& rotation,
                                               const glm::vec3& direction, float maxDistance, uint16_t layerMask, bool includeTriggers, bool all)
    {
        if (!m_PhysicsWorld3D || glm::length(direction) == 0.0f) return {};
        std::vector<RaycastHit> hits;
        if (all)
            hits = m_PhysicsWorld3D->CastShapeAll(shape, origin, rotation, direction, maxDistance, layerMask, includeTriggers);
        else if (RaycastHit hit = m_PhysicsWorld3D->CastShape(shape, origin, rotation, direction, maxDistance, layerMask, includeTriggers))
            hits.push_back(hit);
        DebugDraw::PhysicsShapeCast(shape, origin, rotation, direction, maxDistance, hits);
        return hits;
    }

    RaycastHit Scene::SphereCast3D(const glm::vec3& origin, float radius, const glm::vec3& direction, float maxDistance, uint16_t layerMask,
                                   bool includeTriggers)
    {
        std::vector<RaycastHit> hits = ShapeCast3D(PhysicsQueryShape3D::Sphere(radius), origin, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), direction,
                                                   maxDistance, layerMask, includeTriggers, false);
        return hits.empty() ? RaycastHit() : hits[0];
    }

    std::vector<RaycastHit> Scene::SphereCast3DAll(const glm::vec3& origin, float radius, const glm::vec3& direction, float maxDistance,
                                                   uint16_t layerMask, bool includeTriggers)
    {
        return ShapeCast3D(PhysicsQueryShape3D::Sphere(radius), origin, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), direction, maxDistance, layerMask,
                           includeTriggers, true);
    }

    RaycastHit Scene::BoxCast3D(const glm::vec3& center, const glm::vec3& halfExtents, const glm::vec3& direction, const glm::quat& rotation,
                                float maxDistance, uint16_t layerMask, bool includeTriggers)
    {
        std::vector<RaycastHit> hits = ShapeCast3D(PhysicsQueryShape3D::Box(halfExtents), center, rotation, direction, maxDistance, layerMask,
                                                   includeTriggers, false);
        return hits.empty() ? RaycastHit() : hits[0];
    }

    std::vector<RaycastHit> Scene::BoxCast3DAll(const glm::vec3& center, const glm::vec3& halfExtents, const glm::vec3& direction,
                                                const glm::quat& rotation, float maxDistance, uint16_t layerMask, bool includeTriggers)
    {
        return ShapeCast3D(PhysicsQueryShape3D::Box(halfExtents), center, rotation, direction, maxDistance, layerMask, includeTriggers, true);
    }

    RaycastHit Scene::CapsuleCast3D(const glm::vec3& point0, const glm::vec3& point1, float radius, const glm::vec3& direction,
                                    float maxDistance, uint16_t layerMask, bool includeTriggers)
    {
        glm::vec3 center;
        glm::quat rotation;
        const PhysicsQueryShape3D shape = CapsuleBetween(point0, point1, radius, center, rotation);
        std::vector<RaycastHit> hits = ShapeCast3D(shape, center, rotation, direction, maxDistance, layerMask, includeTriggers, false);
        return hits.empty() ? RaycastHit() : hits[0];
    }

    std::vector<RaycastHit> Scene::CapsuleCast3DAll(const glm::vec3& point0, const glm::vec3& point1, float radius, const glm::vec3& direction,
                                                    float maxDistance, uint16_t layerMask, bool includeTriggers)
    {
        glm::vec3 center;
        glm::quat rotation;
        const PhysicsQueryShape3D shape = CapsuleBetween(point0, point1, radius, center, rotation);
        return ShapeCast3D(shape, center, rotation, direction, maxDistance, layerMask, includeTriggers, true);
    }

    // ------------------------------------------------------------------ update
    void Scene::UpdatePausedScripts()
    {
        ZM_PROFILE_SCOPE("Skriptlar");
        ApplyActiveChanges();
        std::vector<entt::entity> scripted;
        auto collectScripted = [&]() {
            scripted.clear();
            for (auto e : m_Registry.view<NativeScriptComponent>())
                scripted.push_back(e);
            std::reverse(scripted.begin(), scripted.end());
        };
        if (m_ScriptsRunning)
        {
            collectScripted();
            for (auto e : scripted)
                if (auto* nsc = m_Registry.valid(e) ? m_Registry.try_get<NativeScriptComponent>(e) : nullptr;
                    nsc && nsc->Instance && nsc->Instance->m_EnabledState != ShouldScriptBeEnabled({ e, this }, *nsc))
                    SyncScriptEnabled({ e, this });
            StartPendingScripts();
        }
        collectScripted();
        // Only the scripts that asked to run while the game is paused; the game's time is 0.
        auto active = [&](entt::entity e) -> ScriptableEntity* {
            auto* nsc = m_Registry.valid(e) ? m_Registry.try_get<NativeScriptComponent>(e) : nullptr;
            return nsc && nsc->Instance && nsc->Instance->IsActiveForUpdate() && nsc->Instance->m_UpdateWhenPaused &&
                           IsActiveInHierarchy({ e, this })
                       ? nsc->Instance
                       : nullptr;
        };
        for (auto e : scripted)
            if (auto* inst = active(e))
                inst->OnUpdate(Timestep(0.0f));
        for (auto e : scripted)
            if (auto* inst = active(e))
                inst->OnLateUpdate(Timestep(0.0f));
        m_EventBus->Flush();
        FlushDestroyQueue();
        DeleteRetiredScripts();
    }

    void Scene::OnUpdateRuntime(Timestep ts)
    {
        m_FrameIndex++;
        DeleteRetiredScripts(); // destroyed between frames (editor, other code)
        const bool gamePausedBeforeUI = IsGamePaused();
        UpdateUI(ts); // real time: the UI works while the game is paused
        const bool advance = !m_IsPaused || m_StepFrames-- > 0; // the editor's pause / step
        if (advance)
        {
            m_UnscaledDeltaTime = ts;
            m_UnscaledTime += ts;
        }
        // A pause from the UI stops the game at once; a resume from it (a pause menu's button) lets it
        // go on from the next frame: this frame's input was the menu's - the gamepad A that pressed
        // "Resume" is often also the game's Jump, and the game's scripts must not see it as a new press.
        if (advance && (gamePausedBeforeUI || IsGamePaused()))
        {
            // The game is paused (time scale 0): only the scripts that run while it is (a pause menu);
            // the last game frame's debug shapes stay.
            DebugDraw::Hold();
            UpdatePausedScripts();
        }
        else if (advance)
        {
            ts = Timestep((float)ts * m_TimeScale); // the game's time
            m_Time += ts;
            m_AnimationTime += (double)ts;
            // Hit flashes fade with the game's time (paused: they wait).
            for (auto e : m_Registry.view<SpriteRendererComponent>())
                if (auto& sprite = m_Registry.get<SpriteRendererComponent>(e); sprite.FlashTime > 0.0f)
                    sprite.UpdateFlash(ts);
            // Game time goes on: last frame's one-frame debug shapes go (paused: they stay).
            DebugDraw::Advance(ts);

            std::vector<entt::entity> scripted;
            auto collectScripted = [&]() {
                scripted.clear();
                for (auto e : m_Registry.view<NativeScriptComponent>())
                    scripted.push_back(e);
                std::reverse(scripted.begin(), scripted.end());
            };
            // Only scripts that are enabled, have run OnStart and whose entity is active in the
            // hierarchy get per-frame callbacks (timers included).
            auto active = [&](entt::entity e) -> ScriptableEntity* {
                auto* nsc = m_Registry.valid(e) ? m_Registry.try_get<NativeScriptComponent>(e) : nullptr;
                return nsc && nsc->Instance && nsc->Instance->IsActiveForUpdate() && IsActiveInHierarchy({ e, this })
                    ? nsc->Instance : nullptr;
            };

            // Active flags written straight into TagComponent (inspector, other code) and raw
            // reparenting: bodies and scripts follow before anything runs this frame.
            ApplyActiveChanges();

            {
                ZM_PROFILE_SCOPE("Skriptlar");
                if (m_ScriptsRunning)
                {
                    // Enabled flags written straight into the component (inspector, other
                    // code) -> OnEnable / OnDisable; then OnStart before the first OnUpdate.
                    collectScripted();
                    for (auto e : scripted)
                        if (auto* nsc = m_Registry.valid(e) ? m_Registry.try_get<NativeScriptComponent>(e) : nullptr;
                            nsc && nsc->Instance && nsc->Instance->m_EnabledState != ShouldScriptBeEnabled({ e, this }, *nsc))
                            SyncScriptEnabled({ e, this });
                    StartPendingScripts();
                }
                // Entities spawned from now on wait for the next frame (OnStart first).
                collectScripted();
                for (auto e : scripted)
                    if (auto* inst = active(e))
                        inst->OnUpdate(ts);
                for (auto e : scripted)
                    if (auto* inst = active(e))
                        inst->TickTimers(ts);
            }

            StepPhysics(ts);
            // Property animations write their values before OnLateUpdate (scripts may read or
            // override them there); their events fire now.
            UpdateAnimationPlayers(ts, false);

            {
                ZM_PROFILE_SCOPE("Skriptlar");
                for (auto e : scripted)
                    if (auto* inst = active(e))
                        inst->OnLateUpdate(ts);
                // Events queued with EmitDeferred during this frame.
                m_EventBus->Flush();
            }

            // Cameras follow their targets where physics and the scripts left them; shakes go on.
            UpdateCameraFollow(ts);
            for (auto e : m_Registry.view<CameraComponent>())
                m_Registry.get<CameraComponent>(e).Shake.Update(ts);

            for (auto e : m_Registry.view<SpriteAnimationComponent>())
            {
                auto& anim = m_Registry.get<SpriteAnimationComponent>(e);
                if (anim.Playing && IsActiveInHierarchy({ e, this })) // inactive: paused where it is
                    anim.Time += ts;
            }
            UpdateTintAnimations(ts, false);
            UpdateParticles(ts, false);
            UpdateAnimators(ts, false);
            UpdateAudio(ts);

            FlushDestroyQueue();
            DeleteRetiredScripts();
        }
        else
            DebugDraw::Hold(); // paused: the last game frame's shapes stay, nothing piles up

        m_UIDeferred = false;
        Entity cameraEntity = GetPrimaryCameraEntity();
        if (!cameraEntity)
            return;
        auto& camera = cameraEntity.GetComponent<CameraComponent>();
        glm::mat4 cameraWorld = GetCameraViewTransform(cameraEntity); // with its shake (pixel perfect: on the pixel grid)
        SceneRenderOptions options;
        options.Parallax = true; // the game camera moves the parallax layers
        PixelPerfectView pixel;
        if (!GetPixelPerfectView(pixel))
        {
            RenderScene(glm::inverse(cameraWorld), camera.Camera.GetProjection(), glm::vec3(cameraWorld[3]), options);
            return;
        }
        // Pixel perfect: drawn over the whole render target; the screen <-> world mapping (overlays,
        // debug drawing, ScreenToWorld) is what the view shows of it.
        if (camera.PixelPerfect.SnapSprites)
            options.PixelSnap = pixel.SnapUnit;
        // Upscaling: the UI is drawn by the host at the screen's resolution (crisp text).
        options.DeferUI = m_UIDeferred = pixel.Upscale;
        const float nearClip = camera.Camera.GetOrthographicNearClip(), farClip = camera.Camera.GetOrthographicFarClip();
        RenderScene(glm::inverse(cameraWorld), PixelPerfect::GetProjection(pixel, nearClip, farClip, true), glm::vec3(cameraWorld[3]), options);
        m_LastProjection = PixelPerfect::GetProjection(pixel, nearClip, farClip, false);
    }

    void Scene::OnUpdateSimulation(Timestep ts, const EditorCamera& camera, const SceneRenderOptions& options)
    {
        m_FrameIndex++;
        if (!m_IsPaused || m_StepFrames-- > 0)
        {
            DebugDraw::Advance(ts);
            m_AnimationTime += (double)ts;
            ApplyActiveChanges(); // flags changed in the inspector while simulating
            StepPhysics(ts);
            UpdateAnimationPlayers(ts, false);
            UpdateTintAnimations(ts, false);
            UpdateParticles(ts, false);
            UpdateAnimators(ts, false);
        }
        else
            DebugDraw::Hold();
        RenderScene(camera.GetViewMatrix(), camera.GetProjection(), camera.GetPosition(), options);
    }

    void Scene::OnUpdateEditor(Timestep ts, const EditorCamera& camera, const SceneRenderOptions& options)
    {
        m_FrameIndex++;
        DebugDraw::Advance(ts); // the edit scene's time is the editor's
        m_AnimationTime += (double)ts; // animated tiles play in the scene view
        UpdateTintAnimations(ts, true);
        UpdateParticles(ts, true);
        UpdateAnimators(ts, true);
        RenderScene(camera.GetViewMatrix(), camera.GetProjection(), camera.GetPosition(), options);
    }

    void Scene::OnRenderEditor(const EditorCamera& camera, const SceneRenderOptions& options)
    {
        RenderScene(camera.GetViewMatrix(), camera.GetProjection(), camera.GetPosition(), options);
    }

    void Scene::OnViewportResize(uint32_t width, uint32_t height)
    {
        if (m_ViewportWidth == width && m_ViewportHeight == height)
            return;
        m_ViewportWidth = width;
        m_ViewportHeight = height;
        auto view = m_Registry.view<CameraComponent>();
        for (auto entity : view)
        {
            auto& cameraComponent = view.get<CameraComponent>(entity);
            if (!cameraComponent.FixedAspectRatio)
                cameraComponent.Camera.SetViewportSize(width, height);
        }
    }

    void Scene::ResolveAssets()
    {
        for (auto e : m_Registry.view<IDComponent>())
            ResolveEntityAssets({ e, this });
        m_EnvMapPath.clear(); // the environment map is looked up again when the scene is drawn next
    }

    void Scene::ResolveEntityAssets(Entity entity)
    {
        if (!entity)
            return;
        if (auto* sprite = entity.TryGetComponent<SpriteRendererComponent>())
            sprite->Texture = sprite->TexturePath.empty() ? nullptr
                : TextureLibrary::Load(Project::GetAssetFileSystemPath(sprite->TexturePath), sprite->PixelArt ? TextureFilter::Nearest : TextureFilter::Linear);
        if (auto* mr = entity.TryGetComponent<MeshRendererComponent>())
            ResolveMeshRenderer(*mr);
        if (auto* tm = entity.TryGetComponent<TilemapComponent>())
            tm->Tileset = tm->TilesetPath.empty() ? nullptr
                : TextureLibrary::Load(Project::GetAssetFileSystemPath(tm->TilesetPath), tm->PixelArt ? TextureFilter::Nearest : TextureFilter::Linear);
        if (auto* text = entity.TryGetComponent<TextComponent>())
            text->FontAsset = text->FontPath.empty() ? Font::GetDefault() : Font::Load(Project::GetAssetFileSystemPath(text->FontPath));
        if (auto* image = entity.TryGetComponent<UIImageComponent>())
            image->Texture = image->TexturePath.empty() ? nullptr
                : TextureLibrary::Load(Project::GetAssetFileSystemPath(image->TexturePath), image->PixelArt ? TextureFilter::Nearest : TextureFilter::Linear);
        if (auto* field = entity.TryGetComponent<UIInputFieldComponent>())
            field->FontAsset = field->FontPath.empty() ? Font::GetDefault() : Font::Load(Project::GetAssetFileSystemPath(field->FontPath));
        if (auto* player = entity.TryGetComponent<AnimationPlayerComponent>())
            player->ResolveClips();
    }

    // ------------------------------------------------------------------ sprites
    SpriteRegion Scene::GetSpriteRegion(const SpriteRendererComponent& sprite, const SpriteAnimationComponent* animation,
                                        const AssetRegistry* registry)
    {
        uint32_t width = sprite.Texture ? sprite.Texture->GetWidth() : 1;
        uint32_t height = sprite.Texture ? sprite.Texture->GetHeight() : 1;
        const SpriteSheet* sheet = nullptr;
        bool sheetLooked = false;
        auto findFrame = [&](const std::string& name) -> const SpriteFrame* {
            if (name.empty())
                return nullptr;
            if (!sheetLooked)
            {
                sheet = SpriteSheet::Get(registry, sprite.TexturePath);
                sheetLooked = true;
            }
            return sheet ? sheet->Find(name) : nullptr;
        };
        if (animation)
        {
            if (!animation->UsesFrameList())
                return SpriteRegion::GridCell(animation->Columns, animation->Rows, animation->GetCurrentFrame(), width, height);
            if (const SpriteFrame* frame = findFrame(animation->GetCurrentFrameName()))
                return SpriteRegion::FromFrame(*frame, width, height);
            // a frame the sheet does not have: the renderer's own frame / the whole texture
        }
        if (const SpriteFrame* frame = findFrame(sprite.SpriteName))
            return SpriteRegion::FromFrame(*frame, width, height);
        return SpriteRegion::WholeTexture(width, height);
    }

    SpriteGeometry Scene::GetSpriteGeometry(const SpriteRendererComponent& sprite, const SpriteRegion& region, const glm::vec2* uiSize)
    {
        SpriteGeometry g;
        g.Mode = sprite.DrawMode;
        g.Size = uiSize ? *uiSize : sprite.Size;
        g.Pivot = region.Pivot;
        g.UVMin = region.UVMin;
        g.UVMax = region.UVMax;
        g.RegionPixels = region.Pixels;
        g.Border = sprite.HasSliceBorder() ? sprite.SliceBorder : region.Border;
        g.PixelsPerUnit = uiSize ? 1.0f : sprite.PixelsPerUnit;
        if (!sprite.Texture && g.Mode != SpriteDrawMode::Simple)
        {
            // No picture: one plain rectangle of that size.
            g.Mode = SpriteDrawMode::Sliced;
            g.Border = glm::vec4(0.0f);
        }
        return g;
    }

    bool Scene::CanDrawOutline(const SpriteRendererComponent& s, const SpriteAnimationComponent* animation,
                               const AssetRegistry* registry, const glm::vec2* uiSize)
    {
        if (!s.Texture)
            return false;
        const bool frames = !s.SpriteName.empty() || (animation && animation->UsesFrameList());
        SpriteRegion region = frames || s.DrawMode != SpriteDrawMode::Simple ? GetSpriteRegion(s, animation, registry) : SpriteRegion();
        if (s.DrawMode == SpriteDrawMode::Simple && (!frames || region.Whole))
            return (animation && !animation->UsesFrameList()) || s.TilingFactor == 1.0f; // a grid cell, or the texture once
        // One quad with the region's whole picture: the outline grows it by its width, so the
        // picture's edge has to be the quad's (a cut tile or a 9-slice piece would be outlined inside).
        static thread_local std::vector<SpriteQuad> quads;
        quads.clear();
        GetSpriteGeometry(s, region, uiSize).Build(quads, 2);
        if (quads.size() != 1)
            return false;
        auto same = [](const glm::vec2& a, const glm::vec2& b) { return glm::all(glm::lessThanEqual(glm::abs(a - b), glm::vec2(1e-5f))); };
        const SpriteQuad& q = quads[0];
        return same(glm::min(q.UVMin, q.UVMax), glm::min(region.UVMin, region.UVMax)) &&
               same(glm::max(q.UVMin, q.UVMax), glm::max(region.UVMin, region.UVMax));
    }

    void Scene::DrawSprite(const glm::mat4& transform, const SpriteRendererComponent& s, const SpriteAnimationComponent* animation,
                           const glm::vec4& color, int entityID, const AssetRegistry* registry, const glm::vec2* uiSize)
    {
        const bool frames = !s.SpriteName.empty() || (animation && animation->UsesFrameList());
        if (s.HasEffects())
        {
            // The same quads as below, drawn with the hit flash / outline. The outline grows the
            // sprite's quad by its width (the picture is not cut at its edge); sprites drawn in
            // several pieces (9-slice borders, tiles, a repeated texture) get the flash only.
            Renderer2D::SpriteEffect effect;
            effect.FlashAmount = s.GetFlashAmount();
            effect.FlashColor = glm::vec4(s.GetFlashColor(), 1.0f);
            effect.OutlineColor = s.OutlineColor;
            effect.OutlineThickness = s.OutlineThickness;
            if (effect.HasOutline() && !CanDrawOutline(s, animation, registry, uiSize))
                effect.OutlineThickness = 0.0f;
            static thread_local std::vector<SpriteQuad> effectQuads;
            effectQuads.clear();
            glm::vec2 pixels(1.0f);
            SpriteRegion region = s.Texture && (frames || s.DrawMode != SpriteDrawMode::Simple) ? GetSpriteRegion(s, animation, registry) : SpriteRegion();
            if (s.DrawMode == SpriteDrawMode::Simple && (!frames || region.Whole))
            {
                SpriteQuad q{ { -0.5f, -0.5f }, { 0.5f, 0.5f }, { 0.0f, 0.0f }, { 1.0f, 1.0f } };
                if (s.Texture && animation && !animation->UsesFrameList())
                {
                    SpriteRegion cell = SpriteRegion::GridCell(animation->Columns, animation->Rows, animation->GetCurrentFrame(),
                                                               s.Texture->GetWidth(), s.Texture->GetHeight());
                    q.UVMin = cell.UVMin;
                    q.UVMax = cell.UVMax;
                    pixels = cell.Pixels;
                }
                else if (s.Texture)
                {
                    q.UVMax = glm::vec2(s.TilingFactor); // the texture repeats
                    pixels = glm::vec2((float)s.Texture->GetWidth(), (float)s.Texture->GetHeight());
                }
                effectQuads.push_back(q);
            }
            else
            {
                GetSpriteGeometry(s, region, uiSize).Build(effectQuads);
                pixels = region.Pixels;
            }
            for (const SpriteQuad& quad : effectQuads)
            {
                glm::vec4 rect;
                SpriteQuad q = GrowSpriteQuad(quad, pixels, effect.HasOutline() ? effect.OutlineThickness : 0.0f, rect);
                glm::vec2 center = (q.Min + q.Max) * 0.5f, size = q.Max - q.Min;
                glm::mat4 m = transform * glm::translate(glm::mat4(1.0f), glm::vec3(center, 0.0f)) * glm::scale(glm::mat4(1.0f), glm::vec3(size, 1.0f));
                Renderer2D::DrawQuad(m, s.Texture, q.UVMin, q.UVMax, color, effect, rect, entityID);
            }
            return;
        }
        if (s.DrawMode == SpriteDrawMode::Simple && !frames)
        {
            // The quad itself: the whole texture (repeated TilingFactor times) or a grid cell.
            if (!s.Texture)
                Renderer2D::DrawQuad(transform, color, entityID);
            else if (animation)
            {
                SpriteRegion cell = SpriteRegion::GridCell(animation->Columns, animation->Rows, animation->GetCurrentFrame(),
                                                           s.Texture->GetWidth(), s.Texture->GetHeight());
                Renderer2D::DrawQuad(transform, s.Texture, cell.UVMin, cell.UVMax, color, entityID);
            }
            else
                Renderer2D::DrawQuad(transform, s.Texture, s.TilingFactor, color, entityID);
            return;
        }
        SpriteRegion region = s.Texture ? GetSpriteRegion(s, animation, registry) : SpriteRegion();
        if (s.DrawMode == SpriteDrawMode::Simple && region.Whole)
        {
            // A frame name the sheet does not (yet) have: the whole picture, as before.
            if (s.Texture)
                Renderer2D::DrawQuad(transform, s.Texture, s.TilingFactor, color, entityID);
            else
                Renderer2D::DrawQuad(transform, color, entityID);
            return;
        }
        static thread_local std::vector<SpriteQuad> quads;
        quads.clear();
        GetSpriteGeometry(s, region, uiSize).Build(quads);
        for (const SpriteQuad& q : quads)
        {
            glm::vec2 center = (q.Min + q.Max) * 0.5f, size = q.Max - q.Min;
            glm::mat4 m = transform * glm::translate(glm::mat4(1.0f), glm::vec3(center, 0.0f)) * glm::scale(glm::mat4(1.0f), glm::vec3(size, 1.0f));
            if (s.Texture)
                Renderer2D::DrawQuad(m, s.Texture, q.UVMin, q.UVMax, color, entityID);
            else
                Renderer2D::DrawQuad(m, color, entityID);
        }
    }

    glm::mat4 Scene::GetSpriteRectTransform(const SpriteRendererComponent& sprite, const SpriteAnimationComponent* animation,
                                            const AssetRegistry* registry)
    {
        const bool frames = !sprite.SpriteName.empty() || (animation && animation->UsesFrameList());
        if (sprite.DrawMode == SpriteDrawMode::Simple && !frames)
            return glm::mat4(1.0f);
        SpriteRegion region = sprite.Texture ? GetSpriteRegion(sprite, animation, registry) : SpriteRegion();
        glm::vec2 min, max;
        GetSpriteGeometry(sprite, region).GetBounds(min, max);
        glm::vec2 size = glm::max(max - min, glm::vec2(1e-4f));
        return glm::translate(glm::mat4(1.0f), glm::vec3((min + max) * 0.5f, 0.0f)) * glm::scale(glm::mat4(1.0f), glm::vec3(size, 1.0f));
    }

    void Scene::UpdateAnimators(Timestep ts, bool editor)
    {
        ZM_PROFILE_SCOPE("Animatsiya");
        std::vector<PendingAnimationEvent> firedEvents;
        for (auto e : m_Registry.view<AnimatorComponent>())
        {
            // Inactive in the hierarchy: not updated at all - time, pose and state machine wait
            // (triggers set meanwhile stay pending) and continue where they stopped.
            if (!IsActiveInHierarchy({ e, this }))
                continue;
            auto& anim = m_Registry.get<AnimatorComponent>(e);
            auto* mr = m_Registry.try_get<MeshRendererComponent>(e);
            Ref<Model> model = mr ? mr->ModelAsset : nullptr;
            static const std::vector<AnimationClip> s_NoClips;
            const auto& clips = model ? model->GetAnimations() : s_NoClips;
            auto findClip = [&](const std::string& name) -> const AnimationClip* {
                if (!model) return nullptr;
                int index = name.empty() ? (clips.empty() ? -1 : 0) : model->FindAnimation(name);
                return index >= 0 ? &clips[index] : nullptr;
            };
            auto wrap = [](const AnimationClip* c, float t, bool loop) {
                if (!c || c->Duration <= 0.0f) return t;
                return loop ? std::fmod(std::fmod(t, c->Duration) + c->Duration, c->Duration) : std::clamp(t, 0.0f, c->Duration);
            };
            // ---- state machine (runtime only; the editor previews the default state)
            if (anim.UsesStateMachine())
            {
                auto enter = [&](int index, float fade) {
                    const AnimatorState& st = anim.States[index];
                    anim.CurrentState = index;
                    anim.Play(st.Clip, st.Loop, st.Speed, fade);
                };
                if (anim.CurrentState < 0 || anim.CurrentState >= (int)anim.States.size())
                    enter(std::clamp(anim.DefaultState, 0, (int)anim.States.size() - 1), 0.0f);
                else if (!editor)
                {
                    const AnimationClip* cur = findClip(anim.Clip);
                    float normalized = cur && cur->Duration > 0.0f ? anim.Time / cur->Duration : 1.0f;
                    for (const auto& tr : anim.Transitions)
                    {
                        if (tr.To < 0 || tr.To >= (int)anim.States.size()) continue;
                        if (tr.From >= 0 && tr.From != anim.CurrentState) continue;
                        if (tr.From < 0 && tr.To == anim.CurrentState) continue;
                        if (tr.HasExitTime && normalized < tr.ExitTime) continue;
                        bool ok = true;
                        for (const auto& c : tr.Conditions)
                        {
                            const AnimatorParameter* p = anim.FindParameter(c.Parameter);
                            float v = p ? p->Value : 0.0f;
                            switch (c.Mode)
                            {
                                case AnimatorCondition::Op::Greater:   ok = v > c.Threshold; break;
                                case AnimatorCondition::Op::Less:      ok = v < c.Threshold; break;
                                case AnimatorCondition::Op::IsTrue:    ok = v != 0.0f; break;
                                case AnimatorCondition::Op::IsFalse:   ok = v == 0.0f; break;
                                case AnimatorCondition::Op::Triggered: ok = v != 0.0f; break;
                            }
                            if (!ok) break;
                        }
                        if (!ok) continue;
                        for (const auto& c : tr.Conditions) // consume triggers
                            if (c.Mode == AnimatorCondition::Op::Triggered)
                                if (auto* p = anim.FindParameter(c.Parameter)) p->Value = 0.0f;
                        enter(tr.To, tr.Fade);
                        break;
                    }
                }
                const AnimatorState& st = anim.States[anim.CurrentState];
                anim.Speed = st.Speed;
                if (!st.SpeedParameter.empty())
                    if (auto* p = anim.FindParameter(st.SpeedParameter)) anim.Speed = st.Speed * p->Value;
            }

            const AnimationClip* clip = findClip(anim.Clip);

            const bool advance = anim.Playing && (!editor || anim.PlayInEditor);
            const float time0 = anim.Time, previousTime0 = anim.PreviousTime;
            const bool fading0 = anim.IsFading();
            if (advance)
            {
                anim.Time += ts * anim.Speed;
                if (anim.IsFading())
                {
                    anim.PreviousTime += ts * anim.PreviousSpeed;
                    anim.FadeElapsed += ts;
                }
            }

            // Events of the playing clip (play mode): the moments the time went past.
            if (advance && !editor && clip && !anim.Events.empty())
                CollectAnimatorEvents({ e, this }, anim, *clip, wrap(clip, time0, anim.Loop), ts * anim.Speed, firedEvents);

            if (!model || !model->IsSkinned())
            {
                anim.JointMatrices.clear();
                anim.RootMotionVelocity = glm::vec3(0.0f);
                anim.RootMotionTurnRate = 0.0f;
                continue;
            }
            const Skeleton& skeleton = model->GetSkeleton();
            const AnimationClip* prev = fading0 || anim.IsFading() ? findClip(anim.PreviousClip) : nullptr;
            const int rootJoint = anim.ApplyRootMotion ? FindRootMotionJoint(skeleton, clip ? clip : prev, anim.RootMotionBone) : -1;
            float w = anim.IsFading() ? anim.FadeElapsed / anim.FadeDuration : 1.0f;
            w = w * w * (3.0f - 2.0f * w); // smoothstep
            if (anim.IsFading())
                EvaluatePoseBlended(skeleton, prev, wrap(prev, anim.PreviousTime, anim.PreviousLoop),
                                    clip, wrap(clip, anim.Time, anim.Loop), w, anim.JointMatrices, rootJoint);
            else
                EvaluatePose(skeleton, clip, wrap(clip, anim.Time, anim.Loop), anim.JointMatrices, rootJoint);

            // Root motion moves the entity (play mode; the editor's preview stays in place).
            anim.RootMotionVelocity = glm::vec3(0.0f);
            anim.RootMotionTurnRate = 0.0f;
            if (rootJoint >= 0 && advance && !editor && ts > 0.0f)
            {
                RootMotion motion;
                if (clip)
                    motion = ExtractRootMotion(skeleton, *clip, rootJoint, time0, anim.Time, anim.Loop);
                if (fading0 && prev)
                {
                    RootMotion old = ExtractRootMotion(skeleton, *prev, rootJoint, previousTime0, anim.PreviousTime, anim.PreviousLoop);
                    motion.Translation = glm::mix(old.Translation, motion.Translation, w);
                    motion.Yaw = glm::mix(old.Yaw, motion.Yaw, w);
                }
                ApplyRootMotion({ e, this }, anim, motion, ts);
            }
        }
        for (const auto& fired : firedEvents)
            FireAnimationEvent(GetEntityByUUID(fired.Entity), fired.Event, fired.Clip, true);
    }

    void Scene::CollectAnimatorEvents(Entity entity, AnimatorComponent& anim, const AnimationClip& clip, float localTime, float delta,
                                      std::vector<PendingAnimationEvent>& out)
    {
        std::vector<const AnimatorEvent*> events;
        for (const AnimatorEvent& ev : anim.Events)
            if (ev.Clip.empty() || ev.Clip == clip.Name)
                events.push_back(&ev);
        ClipCursor cursor;
        cursor.Time = localTime;
        cursor.AtStart = anim.EventsAtStart;
        const AnimationWrap wrapMode = anim.Loop ? AnimationWrap::Loop : AnimationWrap::Once;
        // Not looping and already at its end (in the direction it plays): nothing more happens.
        if (!anim.Loop && ((delta > 0.0f && localTime >= clip.Duration) || (delta < 0.0f && localTime <= 0.0f)))
            cursor.Finished = true;
        std::vector<ClipSpan> spans;
        AdvanceClip(cursor, delta, clip.Duration, wrapMode, &spans);
        if (delta != 0.0f)
            anim.EventsAtStart = cursor.AtStart;
        if (events.empty())
            return;
        std::vector<size_t> hit;
        const float duration = clip.Duration;
        CollectEvents(spans, events.size(), [&](size_t i) { return std::clamp(events[i]->Time, 0.0f, 1.0f) * duration; }, hit);
        for (size_t i : hit)
            out.push_back({ entity.GetUUID(), { events[i]->Time, events[i]->Name, events[i]->Parameter }, clip.Name });
    }

    void Scene::ApplyRootMotion(Entity entity, AnimatorComponent& anim, const RootMotion& motion, float ts)
    {
        auto& tc = entity.GetComponent<TransformComponent>();
        const glm::mat4 world = GetWorldTransform(entity);
        // The root's walk is in the model's space: the entity's rotation and scale turn it into the world's.
        const glm::vec3 worldDelta = glm::mat3(world) * motion.Translation;
        anim.RootMotionVelocity = worldDelta / ts;
        anim.RootMotionTurnRate = motion.Yaw / ts;
        if (motion.Yaw != 0.0f)
        {
            if (std::abs(tc.Rotation.x) < 1e-5f && std::abs(tc.Rotation.z) < 1e-5f)
                tc.Rotation.y = std::remainder(tc.Rotation.y + motion.Yaw, glm::two_pi<float>());
            else
                tc.Rotation = glm::eulerAngles(glm::quat(tc.Rotation) * glm::angleAxis(motion.Yaw, glm::vec3(0.0f, 1.0f, 0.0f)));
        }
        if (auto* cc = entity.TryGetComponent<CharacterController3DComponent>(); cc && cc->RuntimeID != 0xffffffff)
        {
            cc->Move(anim.RootMotionVelocity); // walked by the character next physics step (collisions, slopes)
            return;
        }
        if (Entity parent = GetParent(entity))
            tc.Translation += glm::inverse(glm::mat3(GetWorldTransform(parent))) * worldDelta;
        else
            tc.Translation += worldDelta;
    }

    void Scene::FireAnimationEvent(Entity entity, const AnimationEvent& event, const std::string& clip, bool skeletal)
    {
        if (!entity || !m_ScriptsRunning)
            return;
        AnimationEventInfo info;
        info.Name = event.Name;
        info.Parameter = event.Parameter;
        info.Clip = clip;
        info.Time = event.Time;
        info.Source = entity;
        info.Skeletal = skeletal;
        if (auto* nsc = entity.TryGetComponent<NativeScriptComponent>(); nsc && nsc->Instance && !nsc->Instance->m_Destroying)
            nsc->Instance->ReceiveAnimationEvent(info);
        // A handler may have destroyed the entity: the bus still hears of the event.
        m_EventBus->Emit("AnimationEvent", info, entity.IsValid() ? entity : Entity());
    }

    void Scene::UpdateAnimationPlayers(Timestep ts, bool editor)
    {
        if (m_Registry.view<AnimationPlayerComponent>().empty())
            return;
        ZM_PROFILE_SCOPE("Animatsiya");
        std::vector<ClipSpan> spans;
        std::vector<size_t> hit;
        std::vector<PendingAnimationEvent> fired;
        for (auto e : m_Registry.view<AnimationPlayerComponent>())
        {
            if (!IsActiveInHierarchy({ e, this })) // inactive: paused where it is
                continue;
            auto& player = m_Registry.get<AnimationPlayerComponent>(e);
            player.ResolveClips();
            if (!player.Started)
            {
                player.Started = true;
                if (player.PlayOnStart && !editor)
                    player.Play(0);
            }
            const PropertyClip* clip = player.GetCurrentClip();
            if (!clip || (!player.Playing && !player.PoseDirty))
                continue;
            spans.clear();
            if (player.Playing)
            {
                AdvanceClip(player.Cursor, ts * player.Speed * player.PlaySpeed, clip->Duration, player.GetWrap(), &spans);
                if (player.Cursor.Finished)
                    player.Playing = false; // stays on its last values
            }
            player.PoseDirty = false;
            ApplyPropertyClip({ e, this }, *clip, player.Cursor.Time);
            if (editor || clip->Events.empty())
                continue;
            hit.clear();
            CollectEvents(spans, clip->Events.size(), [&](size_t i) { return clip->Events[i].Time; }, hit);
            for (size_t i : hit)
                fired.push_back({ Entity(e, this).GetUUID(), clip->Events[i], player.GetCurrentClipName() });
        }
        // After every player has moved: a handler may play, stop or destroy things.
        for (const auto& event : fired)
            FireAnimationEvent(GetEntityByUUID(event.Entity), event.Event, event.Clip, false);
    }

    void Scene::UpdateTintAnimations(Timestep ts, bool editor)
    {
        for (auto e : m_Registry.view<TintAnimationComponent>())
        {
            auto& tint = m_Registry.get<TintAnimationComponent>(e);
            if (editor)
            {
                // The inspector's preview: it plays round and round (a finished one starts over).
                if (!tint.EditorPreview)
                    continue;
                tint.Elapsed += ts;
                if (tint.IsFinished())
                    tint.Elapsed = 0.0f;
            }
            else if (tint.Playing && IsActiveInHierarchy({ e, this })) // inactive: it waits where it is
                tint.Update(ts);
        }
    }

    glm::vec4 Scene::GetTint(Entity entity)
    {
        const auto* tint = entity ? entity.TryGetComponent<TintAnimationComponent>() : nullptr;
        return tint ? tint->GetColor() : glm::vec4(1.0f);
    }

    bool Scene::PlayAudio(Entity entity, float fadeInSeconds)
    {
        if (!m_IsRunning || !entity.IsValid() || entity.GetScene() != this || !IsActiveInHierarchy(entity))
            return false;
        auto* audio = entity.TryGetComponent<AudioSourceComponent>();
        if (!audio || audio->Path.empty())
            return false;
        if (audio->RuntimeHandle)
            AudioEngine::Stop(audio->RuntimeHandle);
        SoundParams params;
        params.Volume = audio->Volume;
        params.Pitch = audio->Pitch;
        params.Loop = audio->Loop;
        params.Group = audio->Output.empty() ? std::string(AudioSettings::SFX) : audio->Output;
        params.Stream = audio->Stream;
        params.Spatial = audio->Spatial;
        params.Position = GetWorldPosition(entity);
        params.MinDistance = audio->MinDistance;
        params.MaxDistance = audio->MaxDistance;
        params.VolumeRandom = audio->VolumeRandom;
        params.PitchRandom = audio->PitchRandom;
        params.Priority = audio->Priority;
        params.MaxInstances = audio->MaxInstances;
        params.Steal = audio->Steal;
        params.DopplerFactor = audio->DopplerFactor;
        // A source that keeps moving keeps its measured velocity (restarted while flying past).
        params.Velocity = audio->HasLastPosition ? audio->Velocity : glm::vec3(0.0f);
        params.FadeIn = std::isfinite(fadeInSeconds) && fadeInSeconds >= 0.0f ? fadeInSeconds : audio->FadeIn;
        params.Filter = audio->Filter;
        params.Cone = audio->Cone;
        params.Direction = glm::vec3(GetWorldTransform(entity) * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f));
        audio->RuntimeHandle = AudioEngine::Play(Project::GetAssetFileSystemPath(audio->Path), params);
        // Its owner is known from now on: it is reported to this entity even if the source is
        // restarted or stopped in the frame the sound ended in.
        TrackSound(audio->RuntimeHandle, entity, true);
        return audio->RuntimeHandle != 0;
    }

    void Scene::StopAudio(Entity entity, float fadeOutSeconds)
    {
        if (auto* audio = GetAudioSource(entity); audio && audio->RuntimeHandle)
        {
            float fade = std::isfinite(fadeOutSeconds) && fadeOutSeconds >= 0.0f ? fadeOutSeconds : audio->FadeOut;
            // With a fade the sound goes on by itself until it is silent; the source is free now.
            AudioEngine::Stop(audio->RuntimeHandle, fade);
            // Its measured velocity too: not measured while stopped, it would be stale (a Doppler
            // jump) when the source plays again somewhere else.
            audio->ResetRuntime();
        }
    }

    bool Scene::FadeAudio(Entity entity, float volume, float seconds)
    {
        auto* audio = GetAudioSource(entity);
        return audio && audio->RuntimeHandle && AudioEngine::FadeTo(audio->RuntimeHandle, volume, seconds);
    }

    float Scene::GetAudioFade(Entity entity)
    {
        auto* audio = GetAudioSource(entity);
        return audio && audio->RuntimeHandle ? AudioEngine::GetFade(audio->RuntimeHandle) : 1.0f;
    }

    void Scene::SetAudioFilter(Entity entity, const AudioFilterSettings& filter)
    {
        auto* audio = GetAudioSource(entity);
        if (!audio)
            return;
        audio->Filter = filter;
        audio->Filter.Sanitize();
        if (audio->RuntimeHandle)
            AudioEngine::SetSoundFilter(audio->RuntimeHandle, audio->Filter);
    }

    void Scene::SetAudioLowPass(Entity entity, float cutoffHz)
    {
        auto* audio = GetAudioSource(entity);
        if (!audio || !std::isfinite(cutoffHz))
            return;
        AudioFilterSettings filter = audio->Filter;
        filter.LowPass = cutoffHz > 0.0f;
        if (filter.LowPass)
            filter.LowPassCutoff = cutoffHz;
        SetAudioFilter(entity, filter);
    }

    void Scene::SetAudioHighPass(Entity entity, float cutoffHz)
    {
        auto* audio = GetAudioSource(entity);
        if (!audio || !std::isfinite(cutoffHz))
            return;
        AudioFilterSettings filter = audio->Filter;
        filter.HighPass = cutoffHz > 0.0f;
        if (filter.HighPass)
            filter.HighPassCutoff = cutoffHz;
        SetAudioFilter(entity, filter);
    }

    bool Scene::IsAudioPlaying(Entity entity)
    {
        auto* audio = GetAudioSource(entity);
        return audio && audio->RuntimeHandle && AudioEngine::IsPlaying(audio->RuntimeHandle);
    }

    AudioSourceComponent* Scene::GetAudioSource(Entity entity)
    {
        if (!entity.IsValid() || entity.GetScene() != this)
            return nullptr;
        return entity.TryGetComponent<AudioSourceComponent>();
    }

    bool Scene::PauseAudio(Entity entity)
    {
        auto* audio = GetAudioSource(entity);
        return audio && audio->RuntimeHandle && AudioEngine::Pause(audio->RuntimeHandle);
    }

    bool Scene::ResumeAudio(Entity entity)
    {
        if (!m_IsRunning || !IsActiveInHierarchy(entity))
            return false;
        auto* audio = GetAudioSource(entity);
        return audio && audio->RuntimeHandle && AudioEngine::Resume(audio->RuntimeHandle);
    }

    bool Scene::IsAudioPaused(Entity entity)
    {
        auto* audio = GetAudioSource(entity);
        return audio && audio->RuntimeHandle && AudioEngine::IsPaused(audio->RuntimeHandle);
    }

    void Scene::SetAudioVolume(Entity entity, float volume)
    {
        auto* audio = GetAudioSource(entity);
        if (!audio || !std::isfinite(volume))
            return;
        audio->Volume = std::max(0.0f, volume);
        if (audio->RuntimeHandle)
            AudioEngine::SetVolume(audio->RuntimeHandle, audio->Volume);
    }

    void Scene::SetAudioPitch(Entity entity, float pitch)
    {
        auto* audio = GetAudioSource(entity);
        if (!audio || !std::isfinite(pitch))
            return;
        audio->Pitch = std::max(0.01f, pitch);
        if (audio->RuntimeHandle)
            AudioEngine::SetPitch(audio->RuntimeHandle, audio->Pitch);
    }

    float Scene::GetAudioTime(Entity entity)
    {
        auto* audio = GetAudioSource(entity);
        return audio && audio->RuntimeHandle ? AudioEngine::GetCursor(audio->RuntimeHandle) : 0.0f;
    }

    bool Scene::SetAudioTime(Entity entity, float seconds)
    {
        auto* audio = GetAudioSource(entity);
        if (!audio || !audio->RuntimeHandle || !AudioEngine::IsAvailable() || !std::isfinite(seconds))
            return false;
        SoundInfo info;
        if (!AudioEngine::GetSoundInfo(audio->RuntimeHandle, info))
            return false;
        AudioEngine::Seek(audio->RuntimeHandle, std::max(0.0f, seconds));
        return true;
    }

    float Scene::GetAudioLength(Entity entity)
    {
        auto* audio = GetAudioSource(entity);
        return audio && audio->RuntimeHandle ? AudioEngine::GetLength(audio->RuntimeHandle) : 0.0f;
    }

    bool Scene::GetAudioInfo(Entity entity, SoundInfo& info)
    {
        auto* audio = GetAudioSource(entity);
        return audio && audio->RuntimeHandle && AudioEngine::GetSoundInfo(audio->RuntimeHandle, info);
    }

    Entity Scene::GetActiveAudioListener()
    {
        Entity best;
        int bestPriority = 0;
        // The view runs from the newest to the oldest: ">=" lets the oldest win a tie.
        for (auto e : m_Registry.view<AudioListenerComponent>())
        {
            const auto& listener = m_Registry.get<AudioListenerComponent>(e);
            Entity entity{ e, this };
            if (!listener.Enabled || !IsActiveInHierarchy(entity))
                continue;
            if (!best || listener.Priority >= bestPriority)
            {
                best = entity;
                bestPriority = listener.Priority;
            }
        }
        return best;
    }

    Entity Scene::GetAudioListenerEntity()
    {
        if (Entity listener = GetActiveAudioListener())
            return listener;
        return GetPrimaryCameraEntity();
    }

    // A jump faster than this (units per second) is a teleport (respawn, a moved spawn point, a cut
    // to another place), not movement: it gives no Doppler sweep.
    static constexpr float AudioTeleportSpeed = 150.0f;

    void Scene::UpdateAudioListener(float ts)
    {
        // Listeners measure how they move every frame (also those not listening right now: one that
        // takes over has its velocity at once).
        for (auto e : m_Registry.view<AudioListenerComponent>())
        {
            auto& listener = m_Registry.get<AudioListenerComponent>(e);
            glm::vec3 position = GetWorldPosition({ e, this });
            if (listener.TrackVelocity && listener.HasLastPosition && ts > 0.0f)
            {
                // A teleport keeps the velocity it had: every 3D sound's pitch would jump otherwise.
                glm::vec3 velocity = (position - listener.LastPosition) / ts;
                if (glm::length(velocity) <= AudioTeleportSpeed)
                    listener.Velocity = velocity;
            }
            else if (!listener.TrackVelocity)
                listener.Velocity = glm::vec3(0.0f);
            listener.LastPosition = position;
            listener.HasLastPosition = true;
        }
        if (!AudioEngine::IsAvailable())
            return;
        Entity entity = GetAudioListenerEntity();
        if (!entity)
            return;
        glm::mat4 w = GetWorldTransform(entity);
        glm::vec3 forward = glm::vec3(w[2]), up = glm::vec3(w[1]);
        forward = glm::length(forward) > 1e-6f ? -glm::normalize(forward) : glm::vec3(0.0f, 0.0f, -1.0f);
        up = glm::length(up) > 1e-6f ? glm::normalize(up) : glm::vec3(0.0f, 1.0f, 0.0f);
        AudioEngine::SetListener(glm::vec3(w[3]), forward, up);
        const auto* listener = entity.TryGetComponent<AudioListenerComponent>();
        AudioEngine::SetListenerVelocity(listener ? listener->Velocity : glm::vec3(0.0f));
    }

    float Scene::GetAmbientZoneDistance(const AmbientZoneComponent& zone, const glm::mat4& world, const glm::vec3& point)
    {
        const glm::vec3 offset = point - glm::vec3(world[3]);
        if (zone.Shape == AmbientZoneComponent::ZoneShape::Sphere)
        {
            const float scale = std::max({ glm::length(glm::vec3(world[0])), glm::length(glm::vec3(world[1])), glm::length(glm::vec3(world[2])) });
            const float radius = std::isfinite(zone.Radius) ? std::max(0.0f, zone.Radius) * scale : 0.0f;
            return std::max(0.0f, glm::length(offset) - radius);
        }
        // The box: how far the point is outside each face, along the entity's (scaled, turned) axes.
        glm::vec3 outside(0.0f);
        for (int i = 0; i < 3; i++)
        {
            const glm::vec3 axis(world[i]);
            const float length = glm::length(axis);
            const float size = std::isfinite(zone.Size[i]) ? std::max(0.0f, zone.Size[i]) : 0.0f;
            const float along = length > 1e-6f ? glm::dot(offset, axis / length) : offset[i];
            outside[i] = std::max(0.0f, std::abs(along) - size * 0.5f * length);
        }
        return glm::length(outside);
    }

    float Scene::GetAmbientZoneWeight(const AmbientZoneComponent& zone, const glm::mat4& world, const glm::vec3& point)
    {
        const float distance = GetAmbientZoneDistance(zone, world, point);
        if (distance <= 0.0f)
            return 1.0f;
        if (!(zone.BlendDistance > 0.0f) || !std::isfinite(distance))
            return 0.0f;
        // Smooth at both ends of the blend: no step at the edge, a soft start far out.
        const float t = std::clamp(distance / zone.BlendDistance, 0.0f, 1.0f);
        return 1.0f - t * t * (3.0f - 2.0f * t);
    }

    float Scene::GetAmbientZoneLevel(Entity entity)
    {
        if (!entity.IsValid() || entity.GetScene() != this)
            return 0.0f;
        const auto* zone = entity.TryGetComponent<AmbientZoneComponent>();
        return zone ? zone->Level : 0.0f;
    }

    void Scene::UpdateAmbientZones(float ts)
    {
        auto view = m_Registry.view<AmbientZoneComponent>();
        if (view.size() == 0)
            return;
        Entity listenerEntity = GetAudioListenerEntity();
        const glm::vec3 listener = listenerEntity ? GetWorldPosition(listenerEntity) : glm::vec3(0.0f);
        const bool engine = AudioEngine::IsAvailable();
        ts = std::isfinite(ts) ? std::max(0.0f, ts) : 0.0f;
        for (auto e : view)
        {
            auto& zone = view.get<AmbientZoneComponent>(e);
            Entity entity{ e, this };
            const bool on = listenerEntity && !zone.Path.empty() && IsActiveInHierarchy(entity);
            if (on)
            {
                const glm::mat4 world = GetWorldTransform(entity);
                zone.Distance = GetAmbientZoneDistance(zone, world, listener);
                zone.Target = GetAmbientZoneWeight(zone, world, listener);
            }
            else
                zone.Target = 0.0f;
            // The level follows: at most from silent to full in FadeTime.
            if (zone.FadeTime > 0.0f && std::isfinite(zone.FadeTime))
            {
                const float step = ts / zone.FadeTime;
                zone.Level = zone.Level < zone.Target ? std::min(zone.Target, zone.Level + step) : std::max(zone.Target, zone.Level - step);
            }
            else
                zone.Level = zone.Target;
            zone.RetryIn = std::max(0.0f, zone.RetryIn - ts);
            if (!engine)
                continue;
            // Gone by itself (the voice limit took its place): started again while it is heard.
            if (zone.RuntimeHandle && !AudioEngine::IsPlaying(zone.RuntimeHandle) && !AudioEngine::IsPaused(zone.RuntimeHandle))
                zone.RuntimeHandle = 0;
            const float volume = std::max(0.0f, zone.Volume) * zone.Level;
            if (zone.Level > 0.0f)
            {
                if (!zone.RuntimeHandle && zone.RetryIn <= 0.0f)
                {
                    SoundParams params;
                    params.Volume = volume;
                    params.Pitch = zone.Pitch;
                    params.Loop = true;
                    params.Group = zone.Output.empty() ? std::string(AudioSettings::SFX) : zone.Output;
                    params.Stream = zone.Stream;
                    params.Priority = zone.Priority;
                    zone.RuntimeHandle = AudioEngine::Play(Project::GetAssetFileSystemPath(zone.Path), params);
                    if (!zone.RuntimeHandle)
                        zone.RetryIn = 0.5f; // no room (or a bad file): not every frame
                }
                if (zone.RuntimeHandle)
                {
                    AudioEngine::SetVolume(zone.RuntimeHandle, volume);
                    AudioEngine::SetPitch(zone.RuntimeHandle, zone.Pitch);
                }
            }
            else if (zone.RuntimeHandle)
            {
                AudioEngine::Stop(zone.RuntimeHandle);
                zone.RuntimeHandle = 0;
            }
        }
    }

    void Scene::UpdateAudio(float ts)
    {
        ZM_PROFILE_SCOPE("Audio");
        UpdateAudioListener(ts);
        UpdateAmbientZones(ts);
        const bool engine = AudioEngine::IsAvailable();
        for (auto e : m_Registry.view<AudioSourceComponent>())
        {
            auto& a = m_Registry.get<AudioSourceComponent>(e);
            if (!a.RuntimeHandle)
                continue;
            if (a.Spatial)
            {
                glm::vec3 position = GetWorldPosition({ e, this });
                if (a.HasLastPosition && ts > 0.0f)
                {
                    glm::vec3 velocity = (position - a.LastPosition) / ts;
                    a.Velocity = glm::length(velocity) <= AudioTeleportSpeed ? velocity : glm::vec3(0.0f);
                }
                a.LastPosition = position;
                a.HasLastPosition = true;
                if (engine)
                {
                    AudioEngine::SetSoundPosition(a.RuntimeHandle, position);
                    AudioEngine::SetSoundVelocity(a.RuntimeHandle, a.Velocity);
                    AudioEngine::SetSoundDopplerFactor(a.RuntimeHandle, a.DopplerFactor);
                    AudioEngine::SetSoundDistances(a.RuntimeHandle, a.MinDistance, a.MaxDistance);
                    AudioEngine::SetSoundCone(a.RuntimeHandle, a.Cone);
                    if (a.Cone.IsActive())
                        AudioEngine::SetSoundDirection(a.RuntimeHandle, glm::vec3(GetWorldTransform({ e, this }) * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
                }
            }
            else
                a.Velocity = glm::vec3(0.0f);
            if (engine)
            {
                // The component is the sound's truth: inspector edits during play are heard at once
                // (the random variation picked at the start stays).
                AudioEngine::SetVolume(a.RuntimeHandle, a.Volume);
                AudioEngine::SetPitch(a.RuntimeHandle, a.Pitch);
                AudioEngine::SetLooping(a.RuntimeHandle, a.Loop);
                AudioEngine::SetSoundFilter(a.RuntimeHandle, a.Filter);
            }
        }
        DispatchFinishedSounds();
    }

    void Scene::TrackSound(uint32_t handle, Entity owner, bool fromAudioSource)
    {
        if (!handle || !owner.IsValid() || owner.GetScene() != this)
            return;
        m_SoundOwners[handle] = { owner.GetUUID(), fromAudioSource };
    }

    void Scene::DispatchFinishedSounds()
    {
        std::vector<FinishedSound> finished = AudioEngine::TakeFinishedSounds();
        // Sounds stopped on purpose never report back: their entries are dropped now and then. Only
        // here, right after the queue was taken: an entry whose sound ended but waits there (this
        // frame's scripts ran in between) must not go before it is dispatched.
        if (m_SoundOwners.size() >= 256)
            std::erase_if(m_SoundOwners, [&](const auto& entry) {
                const uint32_t handle = entry.first;
                if (AudioEngine::IsPlaying(handle) || AudioEngine::IsPaused(handle) || AudioEngine::IsStopping(handle))
                    return false;
                return std::none_of(finished.begin(), finished.end(), [handle](const FinishedSound& f) { return f.Handle == handle; });
            });
        for (const FinishedSound& sound : finished)
        {
            SoundFinished info;
            info.Handle = sound.Handle;
            info.Stolen = sound.Reason == SoundEndReason::Stolen;
            if (auto it = m_SoundOwners.find(sound.Handle); it != m_SoundOwners.end())
            {
                info.Source = GetEntityByUUID(it->second.Entity);
                info.FromAudioSource = it->second.FromAudioSource;
                m_SoundOwners.erase(it);
            }
            if (!info.Source || !m_ScriptsRunning)
                continue; // not this scene's (an editor preview, an earlier scene) or destroyed since
            info.Clip = Project::GetAssetRelativePath(sound.File);
            Entity source = info.Source;
            if (auto* nsc = source.TryGetComponent<NativeScriptComponent>(); nsc && nsc->Instance && !nsc->Instance->m_Destroying)
                nsc->Instance->OnSoundFinished(info);
            // A callback may have destroyed the entity: the bus still hears of the sound.
            m_EventBus->Emit("SoundFinished", info, source.IsValid() ? source : Entity());
        }
    }

    // ------------------------------------------------------------------ particles
    static float Rand01(uint32_t& state)
    {
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        return (state & 0xFFFFFF) / float(0x1000000);
    }

    static glm::vec3 RandomInCone(const glm::vec3& direction, float spreadDegrees, uint32_t& rng)
    {
        glm::vec3 dir = glm::length(direction) > 0.0f ? glm::normalize(direction) : glm::vec3(0, 1, 0);
        float spread = glm::radians(std::clamp(spreadDegrees, 0.0f, 180.0f));
        float cosTheta = 1.0f - Rand01(rng) * (1.0f - std::cos(spread));
        float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
        float phi = Rand01(rng) * glm::two_pi<float>();
        glm::vec3 up = std::abs(dir.y) < 0.99f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
        glm::vec3 t = glm::normalize(glm::cross(up, dir));
        glm::vec3 b = glm::cross(dir, t);
        return glm::normalize(t * (std::cos(phi) * sinTheta) + b * (std::sin(phi) * sinTheta) + dir * cosTheta);
    }

    static void SpawnParticles(ParticleSystemComponent& ps, const glm::mat4& emitterWorld, int count)
    {
        uint32_t& rng = ps.RandomState;
        for (int i = 0; i < count && (int)ps.Particles.size() < ps.MaxParticles; i++)
        {
            Particle p;
            glm::vec3 local(0.0f);
            switch (ps.Shape)
            {
                case ParticleSystemComponent::EmitterShape::Circle:
                {
                    float a = Rand01(rng) * glm::two_pi<float>(), r = std::sqrt(Rand01(rng)) * ps.ShapeRadius;
                    local = { std::cos(a) * r, std::sin(a) * r, 0.0f };
                    break;
                }
                case ParticleSystemComponent::EmitterShape::Sphere:
                    local = RandomInCone({ 0, 1, 0 }, 180.0f, rng) * (std::cbrt(Rand01(rng)) * ps.ShapeRadius);
                    break;
                case ParticleSystemComponent::EmitterShape::Box:
                    local = (glm::vec3(Rand01(rng), Rand01(rng), Rand01(rng)) * 2.0f - 1.0f) * ps.ShapeExtents;
                    break;
                default: break;
            }
            glm::vec3 dir = RandomInCone(ps.Direction, ps.Spread, rng);
            float speed = ps.SpeedMin + (ps.SpeedMax - ps.SpeedMin) * Rand01(rng);
            if (ps.WorldSpace)
            {
                p.Position = glm::vec3(emitterWorld * glm::vec4(local, 1.0f));
                p.Velocity = glm::normalize(glm::vec3(emitterWorld * glm::vec4(dir, 0.0f))) * speed;
            }
            else
            {
                p.Position = local;
                p.Velocity = dir * speed;
            }
            p.Lifetime = p.Life = std::max(0.01f, ps.LifetimeMin + (ps.LifetimeMax - ps.LifetimeMin) * Rand01(rng));
            p.Rotation = Rand01(rng) * glm::two_pi<float>();
            p.AngularVelocity = (Rand01(rng) * 2.0f - 1.0f) * ps.AngularVelocityMax;
            ps.Particles.push_back(p);
        }
    }

    void Scene::UpdateParticles(Timestep ts, bool editor)
    {
        ZM_PROFILE_SCOPE("Particle'lar");
        for (auto e : m_Registry.view<ParticleSystemComponent>())
        {
            auto& ps = m_Registry.get<ParticleSystemComponent>(e);
            Entity entity{ e, this };
            // Not previewed in the editor, or inactive in the hierarchy: stopped and empty, so it
            // starts over (Burst included) when it plays again. Edit mode and direct writes to
            // TagComponent::Active end up here; at runtime SetActive already cleared it.
            if ((editor && !ps.PlayInEditor) || !IsActiveInHierarchy(entity))
            {
                if (!ps.Particles.empty() || ps.BurstDone || ps.EmitAccumulator != 0.0f)
                    ps.Clear();
                continue;
            }
            glm::mat4 world = GetWorldTransform(entity);

            if (ps.Playing)
            {
                if (!ps.BurstDone)
                {
                    SpawnParticles(ps, world, ps.Burst);
                    ps.BurstDone = true;
                }
                if (ps.Looping || !editor)
                {
                    ps.EmitAccumulator += ps.EmissionRate * ts;
                    int count = (int)ps.EmitAccumulator;
                    ps.EmitAccumulator -= count;
                    if (count > 0) SpawnParticles(ps, world, count);
                }
                if (!ps.Looping && ps.BurstDone && ps.EmissionRate <= 0.0f && ps.Particles.empty() && editor)
                    ps.BurstDone = false; // keep previewing bursts in the editor
            }

            float drag = std::max(0.0f, 1.0f - ps.Drag * ts);
            for (auto& p : ps.Particles)
            {
                p.Velocity += ps.Gravity * (float)ts;
                p.Velocity *= drag;
                p.Position += p.Velocity * (float)ts;
                p.Rotation += p.AngularVelocity * ts;
                p.Life -= ts;
            }
            std::erase_if(ps.Particles, [](const Particle& p) { return p.Life <= 0.0f; });
        }
    }

    void Scene::EmitParticles(Entity entity, int count)
    {
        if (!entity.IsValid() || entity.GetScene() != this || !IsActiveInHierarchy(entity))
            return;
        if (auto* ps = entity.TryGetComponent<ParticleSystemComponent>())
            SpawnParticles(*ps, GetWorldTransform(entity), count);
    }

    // ------------------------------------------------------------------ cameras (2D follow, shake)
    void Scene::UpdateCameraFollow(float dt)
    {
        auto view = m_Registry.view<Camera2DFollowComponent>();
        if (view.empty())
            return;
        for (auto e : view)
        {
            Entity entity{ e, this };
            auto& follow = m_Registry.get<Camera2DFollowComponent>(e);
            if (!follow.Target || !IsActiveInHierarchy(entity))
                continue;
            Entity target = GetEntityByUUID(follow.Target.ID);
            if (!target || target == entity || !IsActiveInHierarchy(target))
                continue; // nothing to follow: the camera stays where it is
            glm::mat4 world = GetWorldTransform(entity);
            glm::vec3 targetPosition = GetWorldPosition(target);
            glm::vec2 halfView(0.0f);
            if (auto* camera = m_Registry.try_get<CameraComponent>(e))
                halfView = Camera2DFollow::GetHalfView(camera->IsPixelPerfect() ? GetCameraProjection(entity) : camera->Camera.GetProjection(),
                                                       world, targetPosition.z);
            glm::vec2 position = Camera2DFollow::Step(follow, glm::vec2(world[3]), glm::vec2(targetPosition), halfView, dt);
            if (position == glm::vec2(world[3]))
                continue;
            auto& transform = m_Registry.get<TransformComponent>(e);
            if ((uint64_t)m_Registry.get<RelationshipComponent>(e).Parent)
            {
                world[3].x = position.x;
                world[3].y = position.y;
                SetWorldTransform(entity, world);
            }
            else
            {
                transform.Translation.x = position.x;
                transform.Translation.y = position.y;
            }
        }
    }

    bool Scene::ShakeCamera(float intensity, float duration, float frequency)
    {
        return ShakeCamera(GetPrimaryCameraEntity(), intensity, duration, frequency);
    }

    bool Scene::ShakeCamera(Entity camera, float intensity, float duration, float frequency)
    {
        if (!camera || !camera.IsValid() || camera.GetScene() != this || !camera.HasComponent<CameraComponent>())
            return false;
        camera.GetComponent<CameraComponent>().Shake.Add(intensity, duration, frequency);
        return true;
    }

    void Scene::StopCameraShake()
    {
        for (auto e : m_Registry.view<CameraComponent>())
            m_Registry.get<CameraComponent>(e).Shake.Stop();
    }

    glm::mat4 Scene::GetCameraViewTransform(Entity camera)
    {
        if (!camera || !camera.IsValid())
            return glm::mat4(1.0f);
        glm::mat4 world = GetWorldTransform(camera);
        auto* c = camera.TryGetComponent<CameraComponent>();
        if (c && c->Shake.IsActive())
            world = world * c->Shake.GetMatrix();
        // Pixel perfect: the view moves in whole pixels (its edges on the pixel grid).
        if (c && c->IsPixelPerfect() && c->PixelPerfect.SnapCamera && m_ViewportWidth > 0 && m_ViewportHeight > 0)
        {
            PixelPerfectView view = PixelPerfect::Compute(c->PixelPerfect, m_ViewportWidth, m_ViewportHeight);
            glm::vec2 snapped = PixelPerfect::SnapCamera(view, glm::vec2(world[3]));
            world[3].x = snapped.x;
            world[3].y = snapped.y;
        }
        return world;
    }

    glm::mat4 Scene::GetCameraProjection(Entity camera)
    {
        if (!camera || !camera.IsValid() || !camera.HasComponent<CameraComponent>())
            return glm::mat4(1.0f);
        const auto& c = camera.GetComponent<CameraComponent>();
        if (!c.IsPixelPerfect() || m_ViewportWidth == 0 || m_ViewportHeight == 0)
            return c.Camera.GetProjection();
        PixelPerfectView view = PixelPerfect::Compute(c.PixelPerfect, m_ViewportWidth, m_ViewportHeight);
        return PixelPerfect::GetProjection(view, c.Camera.GetOrthographicNearClip(), c.Camera.GetOrthographicFarClip(), false);
    }

    bool Scene::GetPixelPerfectView(PixelPerfectView& out)
    {
        Entity camera = GetPrimaryCameraEntity();
        if (!camera || m_ViewportWidth == 0 || m_ViewportHeight == 0)
            return false;
        const auto& c = camera.GetComponent<CameraComponent>();
        if (!c.IsPixelPerfect())
            return false;
        out = PixelPerfect::Compute(c.PixelPerfect, m_ViewportWidth, m_ViewportHeight);
        return true;
    }

    glm::uvec2 Scene::GetRuntimeRenderResolution(uint32_t width, uint32_t height)
    {
        Entity camera = GetPrimaryCameraEntity();
        if (camera && width > 0 && height > 0)
            if (const auto& c = camera.GetComponent<CameraComponent>(); c.IsPixelPerfect())
                return PixelPerfect::Compute(c.PixelPerfect, width, height).RenderSize; // no render scale: it would blur the pixels
        return Renderer::GetRenderResolution(width, height, m_Settings.RenderScale);
    }

    glm::vec3 Scene::ScreenToWorld(const glm::vec2& screen, float depthZ)
    {
        Entity cam = GetPrimaryCameraEntity();
        glm::vec2 size = GetViewportSize();
        if (!cam || size.x <= 0.0f || size.y <= 0.0f) return glm::vec3(0.0f);
        glm::mat4 viewProj = GetCameraProjection(cam) * glm::inverse(GetWorldTransform(cam));
        glm::mat4 inv = glm::inverse(viewProj);
        glm::vec2 ndc = { screen.x / size.x * 2.0f - 1.0f, 1.0f - screen.y / size.y * 2.0f };
        glm::vec4 n = inv * glm::vec4(ndc, -1.0f, 1.0f), f = inv * glm::vec4(ndc, 1.0f, 1.0f);
        glm::vec3 nearP = glm::vec3(n) / n.w, farP = glm::vec3(f) / f.w;
        glm::vec3 dir = farP - nearP;
        if (std::abs(dir.z) < 1e-6f) return nearP;
        float t = (depthZ - nearP.z) / dir.z;
        return nearP + dir * t;
    }

    void Scene::ResolveMeshRenderer(MeshRendererComponent& mr)
    {
        if (mr.Type == PrimitiveMesh::Model)
        {
            mr.MeshAsset = nullptr;
            mr.ModelAsset = mr.ModelPath.empty() ? nullptr : Model::Load(Project::GetAssetFileSystemPath(mr.ModelPath));
        }
        else
        {
            mr.ModelAsset = nullptr;
            mr.MeshAsset = MeshLibrary::GetPrimitive(mr.Type);
        }
        ResolveMeshMaterials(mr);
    }

    void Scene::ResolveMeshMaterials(MeshRendererComponent& mr)
    {
        auto tex = [](const std::string& p) { return MaterialLibrary::LoadTexture(p); };
        mr.Mat.AlbedoMap = tex(mr.AlbedoPath);
        mr.Mat.NormalMap = tex(mr.NormalPath);
        mr.Mat.MetalRoughMap = tex(mr.MetalRoughPath);
        mr.Mat.EmissiveMap = tex(mr.EmissivePath);
        mr.MaterialRef = mr.MaterialPath.empty() ? nullptr : MaterialLibrary::Load(Project::GetAssetFileSystemPath(mr.MaterialPath));
        mr.SubmeshMaterialRefs.assign(mr.SubmeshMaterials.size(), nullptr);
        for (size_t i = 0; i < mr.SubmeshMaterials.size(); i++)
            if (!mr.SubmeshMaterials[i].empty())
                mr.SubmeshMaterialRefs[i] = MaterialLibrary::Load(Project::GetAssetFileSystemPath(mr.SubmeshMaterials[i]));
    }

    Ref<MaterialAsset> Scene::GetMeshMaterialAsset(const MeshRendererComponent& mr, size_t submesh)
    {
        if (mr.Type == PrimitiveMesh::Model && submesh < mr.SubmeshMaterialRefs.size() && mr.SubmeshMaterialRefs[submesh])
            return mr.SubmeshMaterialRefs[submesh];
        return mr.MaterialRef;
    }

    MaterialData Scene::GetInlineMaterialData(const MeshRendererComponent& mr)
    {
        MaterialData data = MaterialData::FromMaterial(mr.Mat);
        data.AlbedoMap = mr.AlbedoPath;
        data.NormalMap = mr.NormalPath;
        data.MetalRoughMap = mr.MetalRoughPath;
        data.EmissiveMap = mr.EmissivePath;
        return data;
    }

    bool Scene::SetMeshMaterial(MeshRendererComponent& mr, const std::string& assetPath, int submesh)
    {
        if (!assetPath.empty() && AssetFiles::GetType(assetPath) != AssetType::Material)
            return false;
        if (submesh < 0)
            mr.MaterialPath = assetPath;
        else
        {
            if (mr.SubmeshMaterials.size() <= (size_t)submesh)
                mr.SubmeshMaterials.resize((size_t)submesh + 1);
            mr.SubmeshMaterials[(size_t)submesh] = assetPath;
            while (!mr.SubmeshMaterials.empty() && mr.SubmeshMaterials.back().empty())
                mr.SubmeshMaterials.pop_back(); // nothing kept for the parts without one
        }
        ClearMaterialInstances(mr); // copies of what it used before
        ResolveMeshMaterials(mr);
        return true;
    }

    Ref<MaterialAsset> Scene::GetMaterialInstance(MeshRendererComponent& mr)
    {
        if (!mr.MaterialInstance)
            mr.MaterialInstance = mr.MaterialRef ? mr.MaterialRef->Clone() : MaterialAsset::CreateInstance(GetInlineMaterialData(mr), mr.Mat);
        return mr.MaterialInstance;
    }

    Ref<MaterialAsset> Scene::GetMaterialInstance(MeshRendererComponent& mr, size_t submesh)
    {
        if (mr.Type != PrimitiveMesh::Model)
            return GetMaterialInstance(mr);
        const Material* modelMaterial = nullptr;
        if (mr.ModelAsset)
        {
            const auto& subMeshes = mr.ModelAsset->GetSubMeshes();
            if (submesh >= subMeshes.size())
                return nullptr;
            modelMaterial = &subMeshes[submesh].Mat;
        }
        if (mr.SubmeshMaterialInstances.size() <= submesh)
            mr.SubmeshMaterialInstances.resize(submesh + 1);
        Ref<MaterialAsset>& instance = mr.SubmeshMaterialInstances[submesh];
        if (instance)
            return instance;
        // A copy of what the part shows now: its slot's asset, the renderer's material (asset or
        // its instance), or the model's own material with the (instance of the) inline tint.
        Ref<MaterialAsset> slot = submesh < mr.SubmeshMaterialRefs.size() ? mr.SubmeshMaterialRefs[submesh] : nullptr;
        if (slot)
            instance = slot->Clone();
        else if (mr.MaterialRef)
            instance = (mr.MaterialInstance ? mr.MaterialInstance : mr.MaterialRef)->Clone();
        else
        {
            Material composed = ComposeMeshMaterial(mr, modelMaterial, submesh);
            MaterialData data = MaterialData::FromMaterial(composed);
            // Texture paths of the inline maps that replaced the model's (the model's own have none).
            MaterialData tint = mr.MaterialInstance ? mr.MaterialInstance->GetData() : GetInlineMaterialData(mr);
            const Material& tintMaterial = mr.MaterialInstance ? mr.MaterialInstance->GetMaterial() : mr.Mat;
            if (tintMaterial.AlbedoMap || !modelMaterial) data.AlbedoMap = tint.AlbedoMap;
            if (tintMaterial.NormalMap || !modelMaterial) data.NormalMap = tint.NormalMap;
            if (tintMaterial.MetalRoughMap || !modelMaterial) data.MetalRoughMap = tint.MetalRoughMap;
            if (tintMaterial.EmissiveMap || !modelMaterial) data.EmissiveMap = tint.EmissiveMap;
            instance = MaterialAsset::CreateInstance(data, composed);
        }
        return instance;
    }

    bool Scene::HasMaterialInstances(const MeshRendererComponent& mr)
    {
        return mr.MaterialInstance || std::any_of(mr.SubmeshMaterialInstances.begin(), mr.SubmeshMaterialInstances.end(),
                                                  [](const Ref<MaterialAsset>& m) { return m != nullptr; });
    }

    void Scene::ClearMaterialInstances(MeshRendererComponent& mr)
    {
        mr.MaterialInstance = nullptr;
        mr.SubmeshMaterialInstances.clear();
    }

    Material Scene::ComposeMeshMaterial(const MeshRendererComponent& mr, const Material* modelMaterial, size_t submesh)
    {
        // A part's own instance, then its slot's asset (more specific than the renderer's material).
        if (modelMaterial && submesh < mr.SubmeshMaterialInstances.size() && mr.SubmeshMaterialInstances[submesh])
            return mr.SubmeshMaterialInstances[submesh]->GetMaterial();
        if (modelMaterial && submesh < mr.SubmeshMaterialRefs.size() && mr.SubmeshMaterialRefs[submesh])
            return mr.SubmeshMaterialRefs[submesh]->GetMaterial();
        // The renderer's material asset (or a script's instance of it).
        if (mr.MaterialRef)
            return (mr.MaterialInstance ? mr.MaterialInstance : mr.MaterialRef)->GetMaterial();
        const Material& inlineMat = mr.MaterialInstance ? mr.MaterialInstance->GetMaterial() : mr.Mat;
        if (!modelMaterial)
            return inlineMat;
        // The model's own material with the inline material as tint / overrides.
        Material m = *modelMaterial;
        m.Albedo *= inlineMat.Albedo;
        if (inlineMat.AlbedoMap) m.AlbedoMap = inlineMat.AlbedoMap;
        if (inlineMat.NormalMap) m.NormalMap = inlineMat.NormalMap;
        if (inlineMat.MetalRoughMap) m.MetalRoughMap = inlineMat.MetalRoughMap;
        if (inlineMat.EmissiveMap) m.EmissiveMap = inlineMat.EmissiveMap;
        m.Emission = m.Emission * m.EmissionStrength + inlineMat.Emission * inlineMat.EmissionStrength;
        m.EmissionStrength = 1.0f;
        m.Tiling *= inlineMat.Tiling;
        m.Offset = m.Offset * inlineMat.Tiling + inlineMat.Offset;
        m.NormalStrength *= inlineMat.NormalStrength;
        // Modes: the model's own unless the inline material asks for something else.
        if (inlineMat.Shader != MaterialShader::Lit)
            m.Shader = inlineMat.Shader;
        if (inlineMat.BlendMode != MaterialBlendMode::Opaque)
        {
            m.BlendMode = inlineMat.BlendMode;
            m.AlphaCutoff = inlineMat.AlphaCutoff;
            m.PreserveSpecular = inlineMat.PreserveSpecular;
        }
        m.DoubleSided = m.DoubleSided || inlineMat.DoubleSided;
        if (inlineMat.SortPriority != 0)
            m.SortPriority = inlineMat.SortPriority;
        return m;
    }

    // ------------------------------------------------------------------ rendering
    std::vector<TilemapChunk> GetVisibleTilemapChunks(const TilemapComponent& tilemap, const glm::mat4& world, const Frustum* frustum,
                                                      uint32_t* culled)
    {
        std::vector<TilemapChunk> visible;
        const int width = std::max(1, tilemap.Width), height = std::max(1, tilemap.Height);
        const float size = tilemap.TileSize;
        for (int y0 = 0; y0 < height; y0 += TilemapChunkSize)
            for (int x0 = 0; x0 < width; x0 += TilemapChunkSize)
            {
                TilemapChunk chunk{ x0, y0, std::min(x0 + TilemapChunkSize, width), std::min(y0 + TilemapChunkSize, height) };
                if (frustum)
                {
                    // The tiles' quads cover [x * size, (x + 1) * size] (a negative size mirrors them).
                    glm::vec3 a(chunk.X0 * size, chunk.Y0 * size, 0.0f), b(chunk.X1 * size, chunk.Y1 * size, 0.0f);
                    if (!frustum->Intersects(TransformAABB(AABB{ glm::min(a, b), glm::max(a, b) }, world)))
                    {
                        if (culled) (*culled)++;
                        continue;
                    }
                }
                visible.push_back(chunk);
            }
        return visible;
    }

    void Scene::RenderScene(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& cameraPosition,
                            const SceneRenderOptions& options)
    {
        // Parallax layers move with the camera that draws (their children with them).
        const bool parallax = options.Parallax && !m_Registry.view<ParallaxComponent>().empty();
        // World transforms (memoized for this frame).
        std::unordered_map<entt::entity, glm::mat4> worldCache;
        worldCache.reserve(m_Registry.view<TransformComponent>().size());
        std::function<const glm::mat4&(entt::entity)> world = [&](entt::entity e) -> const glm::mat4& {
            auto it = worldCache.find(e);
            if (it != worldCache.end()) return it->second;
            glm::mat4 local = m_Registry.get<TransformComponent>(e).GetTransform();
            UUID parent = m_Registry.get<RelationshipComponent>(e).Parent;
            if ((uint64_t)parent)
            {
                auto pit = m_EntityMap.find(parent);
                if (pit != m_EntityMap.end() && m_Registry.valid(pit->second))
                    local = world(pit->second) * local;
            }
            if (parallax)
                if (const auto* p = m_Registry.try_get<ParallaxComponent>(e))
                {
                    glm::vec2 offset = ParallaxMath::GetOffset(glm::vec2(cameraPosition), glm::vec2(local[3]), p->Factor);
                    local = glm::translate(glm::mat4(1.0f), glm::vec3(offset, 0.0f)) * local;
                }
            return worldCache.emplace(e, local).first->second;
        };

        // Inactive entities (and everything below them) are never drawn and their lights do not
        // count. Editor scene view: entities hidden with the hierarchy's eye (and their subtrees)
        // are skipped as well; the picking pass skips the locked ones, too.
        // Without editor flags the active cache is used as it is (no copy of a big pool of
        // switched-off entities every frame); nothing below rebuilds it.
        EnsureActiveCache();
        std::unordered_set<entt::entity> withEditorFlags;
        if ((options.SkipEditorHidden || options.SkipEditorLocked) && !m_Registry.view<EditorMetadataComponent>().empty())
            withEditorFlags = CollectRenderSkipped(options);
        const std::unordered_set<entt::entity>& hidden = withEditorFlags.empty() ? m_InactiveInHierarchy : withEditorFlags;
        auto skip = [&](entt::entity e) { return !hidden.empty() && hidden.contains(e); };

        // ---------------- 3D
        SceneEnvironment env;
        env.DrawSky = m_Settings.DrawSky;
        env.SkyTopColor = m_Settings.SkyTopColor;
        env.SkyHorizonColor = m_Settings.SkyHorizonColor;
        env.GroundColor = m_Settings.GroundColor;
        env.Sky = m_Settings.Sky;
        env.AmbientIntensity = m_Settings.AmbientIntensity;
        env.Exposure = m_Settings.Exposure;
        env.ShadowDistance = m_Settings.ShadowDistance;
        env.Shadows = m_Settings.Shadows;
        if (m_EnvMapPath != m_Settings.EnvironmentMap)
        {
            m_EnvMapPath = m_Settings.EnvironmentMap;
            m_EnvMap = m_EnvMapPath.empty() ? nullptr : EnvironmentMap::Load(Project::GetAssetFileSystemPath(m_EnvMapPath));
        }
        env.EnvMap = m_EnvMap;
        env.EnvIntensity = m_Settings.EnvironmentIntensity;
        env.EnvRotation = m_Settings.EnvironmentRotation;
        env.FogEnabled = m_Settings.FogEnabled;
        env.FogColor = m_Settings.FogColor;
        env.FogDensity = m_Settings.FogDensity;
        env.FogHeightFalloff = m_Settings.FogHeightFalloff;
        env.DebugView = options.DebugView;
        env.Wireframe = options.Wireframe;

        // Only the lights that count: inactive (and skipped) ones take no sun / point light slot.
        const LightSet lights = GatherLights(cameraPosition, hidden);
        if (lights.Sun != entt::null)
        {
            const auto& l = m_Registry.get<DirectionalLightComponent>(lights.Sun);
            env.HasSun = true;
            env.Sun.Direction = glm::normalize(glm::vec3(world(lights.Sun) * glm::vec4(0, 0, -1, 0)));
            env.Sun.Color = l.Color;
            env.Sun.Intensity = l.Intensity;
            env.Sun.CastShadows = l.CastShadows;
        }
        // The other directional lights light the scene without shadows.
        for (entt::entity e : lights.DirectionalLights)
        {
            if (e == lights.Sun) continue;
            const auto& l = m_Registry.get<DirectionalLightComponent>(e);
            DirectionalLight extra;
            extra.Direction = glm::normalize(glm::vec3(world(e) * glm::vec4(0, 0, -1, 0)));
            extra.Color = l.Color;
            extra.Intensity = l.Intensity;
            extra.CastShadows = false;
            env.ExtraSuns.push_back(extra);
        }
        env.PointLights.reserve(lights.PointLights.size());
        for (entt::entity e : lights.PointLights)
        {
            const auto& l = m_Registry.get<PointLightComponent>(e);
            env.PointLights.push_back({ glm::vec3(world(e)[3]), l.Color, l.Intensity, l.Radius });
        }
        env.SpotLights.reserve(lights.SpotLights.size());
        for (entt::entity e : lights.SpotLights)
        {
            const auto& l = m_Registry.get<SpotLightComponent>(e);
            const glm::mat4& t = world(e);
            glm::vec3 forward = glm::vec3(t * glm::vec4(0, 0, -1, 0));
            float len = glm::length(forward);
            env.SpotLights.push_back({ glm::vec3(t[3]), len > 1e-6f ? forward / len : glm::vec3(0, -1, 0), l.Color, l.Intensity,
                                       l.Range, l.GetInnerAngle(), l.GetOuterAngle() });
        }

        ZM_PROFILE_SCOPE("Render");
        m_LastProjection = projection;
        m_LastView = view;
        Renderer3D::BeginScene(view, projection, cameraPosition, env);
        for (auto e : m_Registry.view<TransformComponent, MeshRendererComponent>())
        {
            if (skip(e)) continue;
            auto& mr = m_Registry.get<MeshRendererComponent>(e);
            if (!mr.MeshAsset && mr.Type != PrimitiveMesh::Model)
                mr.MeshAsset = MeshLibrary::GetPrimitive(mr.Type);
            if (mr.Type == PrimitiveMesh::Model && mr.ModelAsset)
            {
                const auto& subMeshes = mr.ModelAsset->GetSubMeshes();
                bool preview = options.MaterialPreview && e == options.MaterialPreviewEntity;
                for (size_t index = 0; index < subMeshes.size(); index++)
                {
                    const auto& sub = subMeshes[index];
                    Material m = preview && (options.MaterialPreviewSubmesh < 0 || (size_t)options.MaterialPreviewSubmesh == index)
                               ? options.MaterialPreview->GetMaterial() : ComposeMeshMaterial(mr, &sub.Mat, index);
                    const std::vector<glm::mat4>* joints = nullptr;
                    if (sub.MeshRef->IsSkinned())
                        if (auto* anim = m_Registry.try_get<AnimatorComponent>(e); anim && !anim->JointMatrices.empty())
                            joints = &anim->JointMatrices;
                    if (sub.MeshRef->IsSkinned() && !joints)
                    {
                        // bind pose
                        static thread_local std::vector<glm::mat4> bindPose;
                        EvaluatePose(mr.ModelAsset->GetSkeleton(), nullptr, 0.0f, bindPose);
                        joints = &bindPose;
                    }
                    Renderer3D::SubmitMesh(sub.MeshRef, world(e), m, (int)(uint32_t)e, mr.CastShadows, joints);
                }
            }
            else if (mr.MeshAsset)
            {
                // The material asset (or a script's instance), else the inline material.
                const Material& m = options.MaterialPreview && e == options.MaterialPreviewEntity ? options.MaterialPreview->GetMaterial()
                                  : mr.MaterialInstance ? mr.MaterialInstance->GetMaterial()
                                  : mr.MaterialRef ? mr.MaterialRef->GetMaterial() : mr.Mat;
                Renderer3D::SubmitMesh(mr.MeshAsset, world(e), m, (int)(uint32_t)e, mr.CastShadows);
            }
        }
        if (options.ShowGrid)
            Renderer3D::SubmitGrid(options.GridPlane, options.Grid); // under the transparent surfaces
        {
            ZM_PROFILE_SCOPE("3D");
            Renderer3D::EndScene();
        }

        // ---------------- 2D (sorted back-to-front for correct blending)
        ZM_PROFILE_SCOPE("2D va UI");
        struct Item2D
        {
            SortKey2D Key;
            int Kind; // 0 sprite, 1 circle, 2 text, 3 particles, 4 tilemap
            entt::entity Handle;
            int Layer = 0; // tilemaps: the tile layer (each visible one is sorted on its own)
        };
        std::vector<Item2D> items;
        auto depthOf = [&](entt::entity e) { return (view * world(e)[3]).z; };
        const SortingLayerList& sortingLayers = Project::GetSortingLayers();
        // Y-sorted layers (top-down games): the sort point is the entity's world position - of
        // its topmost ancestor that draws on the same layer, so the parts of one character (a
        // hat, a weapon) go with it and the order in the layer sorts them among themselves.
        auto keyOf = [&](entt::entity e, uint32_t layer, int order) {
            if (!sortingLayers.IsYSorted(layer))
                return SortKey2D::Make(sortingLayers.GetIndex(layer), order, depthOf(e));
            return SortKey2D::Make(sortingLayers.GetIndex(layer), order, depthOf(e), true, world((entt::entity)GetSortRoot({ e, this }, layer))[3].y);
        };
        // Sprite sheet frames are looked up in the asset registry (only when a sprite uses one).
        const AssetRegistry* assetRegistry = nullptr;
        bool assetRegistryLooked = false;
        auto registry = [&]() {
            if (!assetRegistryLooked)
            {
                assetRegistry = Project::GetActive() ? Project::GetAssetRegistry() : nullptr;
                assetRegistryLooked = true;
            }
            return assetRegistry;
        };
        auto needsRegistry = [](const SpriteRendererComponent& s, const SpriteAnimationComponent* anim) {
            return !s.SpriteName.empty() || (anim && anim->UsesFrameList());
        };
        // Parallax sprites that repeat fill the view with copies (never culled as one quad).
        auto repeats = [&](entt::entity e) -> const ParallaxComponent* {
            if (!parallax) return nullptr;
            const auto* p = m_Registry.try_get<ParallaxComponent>(e);
            return p && (p->RepeatX || p->RepeatY) ? p : nullptr;
        };

        // Screen-space UI is drawn by RenderUI; hidden / inactive entities not at all.
        auto notWorld2D = [&](entt::entity e) { return m_Registry.all_of<UITransformComponent>(e) || skip(e); };
        // Frustum culling: what lies entirely outside the camera's view is not drawn (tilemaps
        // chunk by chunk, see below).
        auto& stats = Renderer::GetStats();
        const bool culling2D = Renderer3D::IsFrustumCullingEnabled();
        const Frustum frustum2D = Frustum::FromViewProjection(projection * view);
        auto seen = [&](const AABB& bounds) {
            if (!culling2D || frustum2D.Intersects(bounds))
                return true;
            stats.CulledSpriteCount++;
            return false;
        };
        for (auto e : m_Registry.view<TransformComponent, SpriteRendererComponent>())
        {
            if (notWorld2D(e))
                continue;
            const auto& s = m_Registry.get<SpriteRendererComponent>(e);
            if (!repeats(e))
            {
                const auto* anim = m_Registry.try_get<SpriteAnimationComponent>(e);
                glm::mat4 rect = s.DrawMode == SpriteDrawMode::Simple && !needsRegistry(s, anim)
                               ? world(e) : world(e) * GetSpriteRectTransform(s, anim, needsRegistry(s, anim) ? registry() : nullptr);
                if (!seen(Renderer2D::GetQuadBounds(rect)))
                    continue;
            }
            items.push_back({ keyOf(e, s.SortingLayer, s.SortingOrder), 0, e });
        }
        for (auto e : m_Registry.view<TransformComponent, CircleRendererComponent>())
            if (!notWorld2D(e) && seen(Renderer2D::GetQuadBounds(world(e))))
            {
                const auto& c = m_Registry.get<CircleRendererComponent>(e);
                items.push_back({ keyOf(e, c.SortingLayer, c.SortingOrder), 1, e });
            }
        for (auto e : m_Registry.view<TransformComponent, TextComponent>())
        {
            if (notWorld2D(e)) continue;
            auto& text = m_Registry.get<TextComponent>(e);
            if (!text.FontAsset)
                text.FontAsset = Font::GetDefault();
            if (culling2D)
            {
                // The lines' box, as the draw below lays them out.
                if (text.Text.empty())
                    continue; // nothing to draw
                const UIRect box = text.LayoutWorld().GetBounds();
                if (!seen(TransformAABB(AABB{ glm::vec3(box.Min, 0.0f), glm::vec3(box.Max, 0.0f) }, world(e))))
                    continue;
            }
            items.push_back({ keyOf(e, text.SortingLayer, text.SortingOrder), 2, e });
        }
        for (auto e : m_Registry.view<TransformComponent, TilemapComponent>())
            if (!notWorld2D(e))
            {
                // Every visible tile layer: at the tilemap's order plus its own (list order breaks ties).
                const auto& tm = m_Registry.get<TilemapComponent>(e);
                for (int layer = 0; layer < (int)tm.Layers.size(); layer++)
                {
                    const TilemapLayer& l = tm.Layers[(size_t)layer];
                    if (l.Visible && l.Opacity > 0.0f)
                        items.push_back({ keyOf(e, tm.SortingLayer, tm.SortingOrder + l.SortingOrder), 4, e, layer });
                }
            }
        for (auto e : m_Registry.view<TransformComponent, ParticleSystemComponent>())
        {
            const auto& ps = m_Registry.get<ParticleSystemComponent>(e);
            if (ps.Particles.empty() || skip(e)) continue;
            if (culling2D)
            {
                // Every particle quad (any rotation, billboarded or not) fits in a sphere of half
                // its diagonal around its position.
                const glm::mat4& emitter = world(e);
                float radius = std::max(std::abs(ps.StartSize), std::abs(ps.EndSize)) * 0.70711f;
                AABB box{ glm::vec3(std::numeric_limits<float>::max()), glm::vec3(-std::numeric_limits<float>::max()) };
                for (const auto& p : ps.Particles)
                {
                    glm::vec3 pos = ps.WorldSpace ? p.Position : glm::vec3(emitter * glm::vec4(p.Position, 1.0f));
                    box.Min = glm::min(box.Min, pos);
                    box.Max = glm::max(box.Max, pos);
                }
                box.Min -= glm::vec3(radius);
                box.Max += glm::vec3(radius);
                if (!seen(box))
                    continue;
            }
            items.push_back({ keyOf(e, ps.SortingLayer, ps.SortingOrder), 3, e });
        }
        for (const Item2D& item : items)
            if (item.Kind != 4)
                stats.SpriteCount++;

        // camera basis for billboards
        glm::mat4 invView = glm::inverse(view);
        glm::vec3 camRight = glm::vec3(invView[0]), camUp = glm::vec3(invView[1]), camBack = glm::vec3(invView[2]);
        glm::mat4 billboard(1.0f);
        billboard[0] = glm::vec4(camRight, 0.0f);
        billboard[1] = glm::vec4(camUp, 0.0f);
        billboard[2] = glm::vec4(camBack, 0.0f);

        if (!items.empty())
        {

        std::stable_sort(items.begin(), items.end(), [](const Item2D& a, const Item2D& b) {
            return SortKey2D::Less(a.Key, b.Key); // layer, (Y), order in the layer, then farther first
        });
        const glm::mat4 viewProjection = projection * view;

        // 2D lights for the Lit renderers (only looked up when one is Lit).
        bool anyLit = false;
        for (const Item2D& item : items)
            if ((item.Kind == 0 && m_Registry.get<SpriteRendererComponent>(item.Handle).Lit) ||
                (item.Kind == 4 && m_Registry.get<TilemapComponent>(item.Handle).Lit))
            {
                anyLit = true;
                break;
            }
        if (anyLit)
            Renderer2D::SetLighting(GatherLights2D(world, glm::vec2(cameraPosition), culling2D ? &frustum2D : nullptr, hidden));
        // Tint animations multiply the renderer's colour.
        const bool tints = !m_Registry.view<TintAnimationComponent>().empty();
        auto tintOf = [&](entt::entity e) {
            const auto* tint = tints ? m_Registry.try_get<TintAnimationComponent>(e) : nullptr;
            return tint ? tint->GetColor() : glm::vec4(1.0f);
        };
        // Pixel-perfect camera: drawn at whole pixels (the transform stays).
        auto snapped = [&](glm::mat4 m) {
            if (options.PixelSnap > 0.0f)
            {
                glm::vec2 p = PixelPerfect::Snap(glm::vec2(m[3]), options.PixelSnap);
                m[3].x = p.x;
                m[3].y = p.y;
            }
            return m;
        };

        Renderer2D::BeginScene(projection * view);
        for (const auto& item : items)
        {
            const glm::mat4& transform = world(item.Handle);
            int id = (int)(uint32_t)item.Handle;
            Renderer2D::SetLit(anyLit && ((item.Kind == 0 && m_Registry.get<SpriteRendererComponent>(item.Handle).Lit) ||
                                          (item.Kind == 4 && m_Registry.get<TilemapComponent>(item.Handle).Lit)));
            if (item.Kind == 0)
            {
                auto& s = m_Registry.get<SpriteRendererComponent>(item.Handle);
                glm::mat4 t = snapped(transform);
                const glm::vec4 color = s.Color * tintOf(item.Handle);
                if (s.FlipX || s.FlipY)
                    t = t * glm::scale(glm::mat4(1.0f), { s.FlipX ? -1.0f : 1.0f, s.FlipY ? -1.0f : 1.0f, 1.0f });
                const auto* anim = m_Registry.try_get<SpriteAnimationComponent>(item.Handle);
                const AssetRegistry* sheets = needsRegistry(s, anim) ? registry() : nullptr;
                const ParallaxComponent* repeat = repeats(item.Handle);
                if (!repeat)
                {
                    DrawSprite(t, s, anim, color, id, sheets);
                    continue;
                }
                // An endless parallax background: copies side by side (and / or above each other)
                // wherever the camera sees the sprite's plane.
                AABB box = Renderer2D::GetQuadBounds(t * GetSpriteRectTransform(s, anim, sheets));
                glm::vec2 size = glm::vec2(box.Max - box.Min);
                glm::vec2 spacing = { repeat->Spacing.x > 0.0f ? repeat->Spacing.x : size.x, repeat->Spacing.y > 0.0f ? repeat->Spacing.y : size.y };
                glm::vec2 center = glm::vec2(box.Min + box.Max) * 0.5f;
                glm::vec2 viewMin, viewMax;
                int x0 = 0, x1 = 0, y0 = 0, y1 = 0;
                if (ParallaxMath::GetViewRectAtDepth(viewProjection, (box.Min.z + box.Max.z) * 0.5f, viewMin, viewMax))
                {
                    if (repeat->RepeatX && !ParallaxMath::GetRepeatRange(center.x, size.x, spacing.x, viewMin.x, viewMax.x, x0, x1, 128))
                        continue; // no copy in view
                    if (repeat->RepeatY && !ParallaxMath::GetRepeatRange(center.y, size.y, spacing.y, viewMin.y, viewMax.y, y0, y1, 128))
                        continue;
                }
                for (int ky = y0; ky <= y1; ky++)
                    for (int kx = x0; kx <= x1; kx++)
                    {
                        glm::mat4 copy = glm::translate(glm::mat4(1.0f), { kx * spacing.x, ky * spacing.y, 0.0f }) * t;
                        DrawSprite(copy, s, anim, color, id, sheets);
                    }
            }
            else if (item.Kind == 1)
            {
                auto& c = m_Registry.get<CircleRendererComponent>(item.Handle);
                Renderer2D::DrawCircle(transform, c.Color * tintOf(item.Handle), c.Thickness, c.Fade, id);
            }
            else if (item.Kind == 2)
            {
                auto& text = m_Registry.get<TextComponent>(item.Handle);
                Renderer2D::TextParams params{ text.Color * tintOf(item.Handle), text.Kerning, text.LineSpacing };
                TextLayout::Draw(text.Text, text.LayoutWorld(), text.FontAsset, 1.0f, transform, params, id);
            }
            else if (item.Kind == 4)
            {
                auto& tm = m_Registry.get<TilemapComponent>(item.Handle);
                tm.EnsureSize();
                if (!tm.IsValidLayer(item.Layer))
                    continue;
                const TilemapLayer& layer = tm.Layers[(size_t)item.Layer];
                const glm::vec4 tmColor = tm.Color * tintOf(item.Handle);
                const glm::vec4 color = layer.Opacity >= 1.0f ? tmColor : tmColor * glm::vec4(1.0f, 1.0f, 1.0f, std::max(0.0f, layer.Opacity));
                const glm::mat4 origin = snapped(transform);
                const bool previewed = item.Handle == options.TilePreviewEntity && item.Layer == options.TilePreviewLayer &&
                                       options.TilePreviewTiles && options.TilePreviewTiles->size() == layer.Tiles.size();
                const std::vector<int>& tiles = previewed ? *options.TilePreviewTiles : layer.Tiles;
                // Animated tiles of the tileset (its .zmeta) show their frame of the scene's clock.
                const TilesetProperties* tileProperties = tm.TilesetPath.empty() ? nullptr : TilesetProperties::Get(registry(), tm.TilesetPath);
                if (tileProperties && !tileProperties->HasAnimations())
                    tileProperties = nullptr;
                if (!tm.Tileset && !tm.TilesetPath.empty())
                    tm.Tileset = TextureLibrary::Load(Project::GetAssetFileSystemPath(tm.TilesetPath), tm.PixelArt ? TextureFilter::Nearest : TextureFilter::Linear);
                int cols = std::max(1, tm.TilesetColumns), rows = std::max(1, tm.TilesetRows);
                glm::vec2 cell = { 1.0f / cols, 1.0f / rows };
                // The UVs are moved in a little, so a neighbouring tile of the tileset never shows at
                // an edge: half a texel when filtered (linear filtering reads the texels around), a
                // hair with nearest filtering (pixel art) - there half a texel would squeeze a tile's
                // texels into one fewer and draw its first and last ones narrower (or, under a
                // pixel-perfect camera, one texel twice and another not at all).
                const float insetTexels = tm.Tileset && tm.Tileset->GetSpecification().Filter == TextureFilter::Nearest ? 1.0f / 256.0f : 0.5f;
                glm::vec2 inset = tm.Tileset ? glm::vec2(insetTexels / tm.Tileset->GetWidth(), insetTexels / tm.Tileset->GetHeight()) : glm::vec2(0.0f);
                const std::vector<TilemapChunk> chunks =
                    GetVisibleTilemapChunks(tm, transform, culling2D ? &frustum2D : nullptr, &stats.CulledTileChunkCount);
                stats.TileChunkCount += (uint32_t)chunks.size();
                for (const TilemapChunk& chunk : chunks)
                for (int y = chunk.Y0; y < chunk.Y1; y++)
                    for (int x = chunk.X0; x < chunk.X1; x++)
                    {
                        int tile = tiles[(size_t)y * tm.Width + x];
                        if (tile < 0) continue;
                        if (tileProperties)
                            tile = tileProperties->Resolve(tile, m_AnimationTime);
                        glm::mat4 t = origin * glm::translate(glm::mat4(1.0f), { (x + 0.5f) * tm.TileSize, (y + 0.5f) * tm.TileSize, 0.0f })
                                             * glm::scale(glm::mat4(1.0f), { tm.TileSize, tm.TileSize, 1.0f });
                        if (tm.Tileset)
                        {
                            int tx = tile % cols, ty = tile / cols;
                            glm::vec2 uvMin = { tx * cell.x, 1.0f - (ty + 1) * cell.y };
                            Renderer2D::DrawQuad(t, tm.Tileset, uvMin + inset, uvMin + cell - inset, color, id);
                        }
                        else
                            Renderer2D::DrawQuad(t, color * glm::vec4(0.6f + 0.05f * (tile % 8), 0.6f, 0.65f, 1.0f), id);
                    }
            }
            else
            {
                auto& ps = m_Registry.get<ParticleSystemComponent>(item.Handle);
                // What came before is drawn with depth writes; the particles without them.
                Renderer2D::Flush();
                if (ps.Additive) Renderer2D::SetBlendMode(BlendMode::Additive);
                RenderCommand::SetDepthWrite(false);
                if (!ps.TexturePath.empty() && !ps.Texture)
                    ps.Texture = TextureLibrary::Load(Project::GetAssetFileSystemPath(ps.TexturePath));
                for (const auto& p : ps.Particles)
                {
                    float t = 1.0f - p.Life / p.Lifetime;
                    glm::vec4 color = glm::mix(ps.StartColor, ps.EndColor, t);
                    float size = glm::mix(ps.StartSize, ps.EndSize, t);
                    glm::vec3 pos = ps.WorldSpace ? p.Position : glm::vec3(transform * glm::vec4(p.Position, 1.0f));
                    glm::mat4 m = glm::translate(glm::mat4(1.0f), pos);
                    if (ps.Billboard) m = m * billboard;
                    m = m * glm::rotate(glm::mat4(1.0f), p.Rotation, glm::vec3(0, 0, 1)) * glm::scale(glm::mat4(1.0f), glm::vec3(size));
                    if (ps.Texture)
                        Renderer2D::DrawQuad(m, ps.Texture, 1.0f, color, id);
                    else if (ps.RenderAsCircle)
                        Renderer2D::DrawCircle(m, color, 1.0f, 0.6f, id);
                    else
                        Renderer2D::DrawQuad(m, color, id);
                }
                Renderer2D::Flush();
                RenderCommand::SetDepthWrite(true);
                if (ps.Additive) Renderer2D::SetBlendMode(BlendMode::Alpha);
            }
        }
        Renderer2D::SetLit(false);
        Renderer2D::EndScene();
        }

        if (!options.DeferUI)
            RenderUI(hidden.empty() ? nullptr : &hidden);
    }

    void Scene::RenderDeferredUI()
    {
        if (!m_UIDeferred)
            return;
        EnsureActiveCache();
        // The finished picture stays opaque: blending the UI's alpha into the target's alpha would
        // leave see-through holes under translucent panels (the editor's viewport image, screenshots).
        RenderCommand::SetColorWriteMask(0, true, false);
        Renderer2D::SetDisplayOutput(true);
        RenderUI(m_InactiveInHierarchy.empty() ? nullptr : &m_InactiveInHierarchy);
        Renderer2D::SetDisplayOutput(false); // draws what is pending
        RenderCommand::SetColorWriteMask(0, true, true);
    }

    Lighting2DData Scene::GatherLights2D(const std::function<const glm::mat4&(entt::entity)>& world, const glm::vec2& viewCenter,
                                         const Frustum* frustum, const std::unordered_set<entt::entity>& skipped)
    {
        Lighting2DData data;
        const auto& settings = m_Settings.Lighting2D;
        data.Ambient = glm::pow(glm::max(settings.AmbientColor, glm::vec3(0.0f)), glm::vec3(2.2f)) * std::max(0.0f, settings.AmbientIntensity);
        struct Candidate
        {
            float Distance;
            Light2DData Light;
        };
        std::vector<Candidate> candidates;
        for (auto e : m_Registry.view<TransformComponent, Light2DComponent>())
        {
            if (!skipped.empty() && skipped.contains(e))
                continue;
            const auto& l = m_Registry.get<Light2DComponent>(e);
            if (l.Radius <= 0.0f || l.Intensity <= 0.0f)
                continue;
            const glm::mat4& t = world(e);
            glm::vec2 position(t[3]);
            if (frustum)
            {
                glm::vec3 p(t[3]);
                if (!frustum->Intersects(AABB{ p - glm::vec3(l.Radius, l.Radius, 0.0f), p + glm::vec3(l.Radius, l.Radius, 0.0f) }))
                    continue;
            }
            glm::vec4 tint(1.0f);
            if (const auto* anim = m_Registry.try_get<TintAnimationComponent>(e))
                tint = anim->GetColor();
            float intensity = l.Intensity * tint.a;
            if (l.Flicker > 0.0f)
                intensity *= Light2DMath::Flicker(m_AnimationTime, l.Flicker, l.FlickerSpeed, (uint32_t)(uint64_t)m_Registry.get<IDComponent>(e).ID);
            if (intensity <= 0.0f)
                continue;
            Light2DData light;
            light.Position = position;
            light.Radius = l.Radius;
            light.InnerRadius = std::clamp(l.InnerRadius, 0.0f, l.Radius);
            light.Color = glm::pow(glm::max(l.Color * glm::vec3(tint), glm::vec3(0.0f)), glm::vec3(2.2f)) * intensity;
            light.Falloff = std::max(0.01f, l.Falloff);
            if (l.Type == Light2DType::Spot && l.SpotAngle < 360.0f)
            {
                glm::vec2 axis(t * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
                float len = glm::length(axis);
                light.Spot = true;
                light.Direction = len > 1e-6f ? axis / len : glm::vec2(1.0f, 0.0f);
                float half = glm::radians(std::clamp(l.SpotAngle, 1.0f, 360.0f) * 0.5f);
                light.CosOuter = std::cos(half);
                light.CosInner = std::cos(half * (1.0f - std::clamp(l.SpotSoftness, 0.0f, 1.0f)));
            }
            candidates.push_back({ glm::length(position - viewCenter) - l.Radius, light });
        }
        // The nearest first (their edge: a big light far away still reaches the view); at most the
        // shader's number.
        std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) { return a.Distance < b.Distance; });
        if (candidates.size() > (size_t)Light2DMath::MaxLights)
            candidates.resize((size_t)Light2DMath::MaxLights);
        data.Lights.reserve(candidates.size());
        for (const Candidate& c : candidates)
            data.Lights.push_back(c.Light);
        return data;
    }

    Lighting2DData Scene::CollectLights2D(const glm::vec2& viewCenter)
    {
        EnsureActiveCache();
        std::unordered_map<entt::entity, glm::mat4> cache;
        std::function<const glm::mat4&(entt::entity)> world = [&](entt::entity e) -> const glm::mat4& {
            auto it = cache.find(e);
            if (it == cache.end())
                it = cache.emplace(e, GetWorldTransform({ e, this })).first;
            return it->second;
        };
        return GatherLights2D(world, viewCenter, nullptr, m_InactiveInHierarchy);
    }


}
