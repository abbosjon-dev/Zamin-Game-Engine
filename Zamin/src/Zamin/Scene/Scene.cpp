#include "Zamin/Scene/Scene.h"
#include "Zamin/Scene/Entity.h"
#include "Zamin/Scene/Components.h"
#include "Zamin/Scene/SceneSerializer.h"
#include "Zamin/Scripting/ScriptableEntity.h"
#include "Zamin/Scripting/ScriptEngine.h"
#include "Zamin/Physics/PhysicsWorld3D.h"
#include "Zamin/Renderer/Renderer2D.h"
#include "Zamin/Renderer/Renderer3D.h"
#include "Zamin/Renderer/RenderCommand.h"
#include "Zamin/Project/Project.h"
#include "Zamin/Audio/AudioEngine.h"
#include "Zamin/Math/Math.h"
#include "Zamin/Utils/FileSystem.h"

#include <box2d/b2_body.h>
#include <box2d/b2_circle_shape.h>
#include <box2d/b2_contact.h>
#include <box2d/b2_fixture.h>
#include <box2d/b2_polygon_shape.h>
#include <box2d/b2_world.h>
#include <box2d/b2_world_callbacks.h>

#include <algorithm>
#include <functional>

namespace Zamin {

    // ------------------------------------------------------------------ contact listener (2D)
    class ContactListener2D : public b2ContactListener
    {
    public:
        void BeginContact(b2Contact* contact) override { Push(contact, true); }
        void EndContact(b2Contact* contact) override { Push(contact, false); }

        std::vector<ContactEvent> Events;

    private:
        void Push(b2Contact* contact, bool begin)
        {
            uint64_t a = (uint64_t)contact->GetFixtureA()->GetUserData().pointer;
            uint64_t b = (uint64_t)contact->GetFixtureB()->GetUserData().pointer;
            if (a && b)
                Events.push_back({ a, b, begin });
        }
    };

    static b2BodyType ToBox2DBodyType(Rigidbody2DComponent::BodyType type)
    {
        switch (type)
        {
            case Rigidbody2DComponent::BodyType::Static:    return b2_staticBody;
            case Rigidbody2DComponent::BodyType::Dynamic:   return b2_dynamicBody;
            case Rigidbody2DComponent::BodyType::Kinematic: return b2_kinematicBody;
        }
        return b2_staticBody;
    }

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

    static void ResetRuntimeData(entt::registry& registry)
    {
        for (auto e : registry.view<Rigidbody2DComponent>()) registry.get<Rigidbody2DComponent>(e).RuntimeBody = nullptr;
        for (auto e : registry.view<BoxCollider2DComponent>()) registry.get<BoxCollider2DComponent>(e).RuntimeFixture = nullptr;
        for (auto e : registry.view<CircleCollider2DComponent>()) registry.get<CircleCollider2DComponent>(e).RuntimeFixture = nullptr;
        for (auto e : registry.view<Rigidbody3DComponent>()) registry.get<Rigidbody3DComponent>(e).RuntimeBodyID = 0xffffffff;
        for (auto e : registry.view<AudioSourceComponent>()) registry.get<AudioSourceComponent>(e).RuntimeHandle = 0;
        for (auto e : registry.view<SpriteAnimationComponent>()) registry.get<SpriteAnimationComponent>(e).Time = 0.0f;
    }

    static void ResetRuntimeData(Entity e)
    {
        if (auto* c = e.TryGetComponent<Rigidbody2DComponent>()) c->RuntimeBody = nullptr;
        if (auto* c = e.TryGetComponent<BoxCollider2DComponent>()) c->RuntimeFixture = nullptr;
        if (auto* c = e.TryGetComponent<CircleCollider2DComponent>()) c->RuntimeFixture = nullptr;
        if (auto* c = e.TryGetComponent<Rigidbody3DComponent>()) c->RuntimeBodyID = 0xffffffff;
        if (auto* c = e.TryGetComponent<AudioSourceComponent>()) c->RuntimeHandle = 0;
        if (auto* c = e.TryGetComponent<SpriteAnimationComponent>()) c->Time = 0.0f;
    }

    // ------------------------------------------------------------------ scene
    Scene::Scene() = default;

    Scene::~Scene()
    {
        if (m_ScriptsRunning)
            StopScripts();
        OnPhysics2DStop();
        OnPhysics3DStop();
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
        if (!entity) return;

        // Children first (copy the list: it is modified while destroying).
        if (auto* rel = entity.TryGetComponent<RelationshipComponent>())
        {
            std::vector<UUID> children = rel->Children;
            for (UUID child : children)
                if (Entity c = GetEntityByUUID(child))
                    DestroyEntity(c);
            if ((uint64_t)rel->Parent)
                if (Entity parent = GetEntityByUUID(rel->Parent))
                    std::erase(parent.GetComponent<RelationshipComponent>().Children, entity.GetUUID());
        }

        if (auto* nsc = entity.TryGetComponent<NativeScriptComponent>(); nsc && nsc->Instance)
        {
            ScriptableEntity* instance = nsc->Instance;
            nsc->Instance = nullptr;
            instance->OnDestroy();
            delete instance;
        }
        if (auto* rb = entity.TryGetComponent<Rigidbody2DComponent>(); rb && rb->RuntimeBody && m_PhysicsWorld)
        {
            m_PhysicsWorld->DestroyBody((b2Body*)rb->RuntimeBody);
            rb->RuntimeBody = nullptr;
        }
        if (auto* rb = entity.TryGetComponent<Rigidbody3DComponent>(); rb && rb->RuntimeBodyID != 0xffffffff && m_PhysicsWorld3D)
        {
            m_PhysicsWorld3D->DestroyBody(rb->RuntimeBodyID);
            rb->RuntimeBodyID = 0xffffffff;
        }
        if (auto* audio = entity.TryGetComponent<AudioSourceComponent>(); audio && audio->RuntimeHandle)
            AudioEngine::Stop(audio->RuntimeHandle);

        m_EntityMap.erase(entity.GetUUID());
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
        for (UUID id : queue)
            if (Entity e = GetEntityByUUID(id))
                DestroyEntity(e);
    }

    Entity Scene::DuplicateEntityRecursive(Entity entity, UUID newParent)
    {
        Entity newEntity = CreateEntity(entity.GetName());
        CopyComponentIfExists(AllComponents{}, newEntity, entity);
        ResetRuntimeData(newEntity);

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

    Entity Scene::DuplicateEntity(Entity entity)
    {
        UUID parent = entity.GetComponent<RelationshipComponent>().Parent;
        Entity copy = DuplicateEntityRecursive(entity, parent);
        if ((uint64_t)parent)
            if (Entity p = GetEntityByUUID(parent))
                p.GetComponent<RelationshipComponent>().Children.push_back(copy.GetUUID());
        return copy;
    }

    void Scene::OnEntitiesSpawned(const std::vector<Entity>& entities)
    {
        for (Entity e : entities)
        {
            if (m_PhysicsWorld && e.HasComponent<Rigidbody2DComponent>())
                CreatePhysicsBody(e);
            if (m_PhysicsWorld3D && e.HasComponent<Rigidbody3DComponent>())
                CreatePhysicsBody3D(e);
        }
        if (m_ScriptsRunning)
            for (Entity e : entities)
                if (e.IsValid() && e.HasComponent<NativeScriptComponent>())
                    CreateScriptInstance(e);
    }

    static void CollectSubtree(Scene& scene, Entity root, std::vector<Entity>& out)
    {
        out.push_back(root);
        for (UUID child : root.GetComponent<RelationshipComponent>().Children)
            if (Entity c = scene.GetEntityByUUID(child))
                CollectSubtree(scene, c, out);
    }

    Entity Scene::Instantiate(Entity prefab)
    {
        if (!prefab) return {};
        Entity e = DuplicateEntity(prefab);
        std::vector<Entity> spawned;
        CollectSubtree(*this, e, spawned);
        if (m_IsRunning || m_PhysicsWorld || m_PhysicsWorld3D)
            OnEntitiesSpawned(spawned);
        return e;
    }

    Entity Scene::InstantiatePrefab(const std::filesystem::path& path)
    {
        std::string text = FileSystem::ReadTextFile(path);
        if (text.empty())
            return {};
        std::vector<Entity> entities = SceneSerializer::DeserializeEntities(*this, text, true);
        if (entities.empty())
            return {};
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
        return FileSystem::WriteTextFile(path, SceneSerializer::SerializeEntities(*this, entities));
    }

    Entity Scene::FindEntityByName(std::string_view name)
    {
        auto view = m_Registry.view<TagComponent>();
        for (auto entity : view)
            if (view.get<TagComponent>(entity).Tag == name)
                return Entity{ entity, this };
        return {};
    }

    Entity Scene::GetEntityByUUID(UUID uuid)
    {
        auto it = m_EntityMap.find(uuid);
        if (it != m_EntityMap.end() && m_Registry.valid(it->second))
            return { it->second, this };
        return {};
    }

    Entity Scene::GetPrimaryCameraEntity()
    {
        auto view = m_Registry.view<CameraComponent>();
        for (auto entity : view)
            if (view.get<CameraComponent>(entity).Primary)
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

    glm::mat4 Scene::GetWorldTransform(Entity entity)
    {
        glm::mat4 local = entity.GetComponent<TransformComponent>().GetTransform();
        Entity parent = GetParent(entity);
        return parent ? GetWorldTransform(parent) * local : local;
    }

    glm::vec3 Scene::GetWorldPosition(Entity entity) { return glm::vec3(GetWorldTransform(entity)[3]); }

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
        return true;
    }

    // ------------------------------------------------------------------ lifecycle
    void Scene::OnRuntimeStart()
    {
        m_IsRunning = true;
        m_Time = 0.0f;
        OnPhysics2DStart();
        OnPhysics3DStart();

        for (auto e : m_Registry.view<AudioSourceComponent>())
        {
            auto& audio = m_Registry.get<AudioSourceComponent>(e);
            if (audio.PlayOnStart && !audio.Path.empty())
                audio.RuntimeHandle = AudioEngine::Play(Project::GetAssetFileSystemPath(audio.Path), audio.Volume, audio.Pitch, audio.Loop);
        }

        StartScripts();
    }

    void Scene::OnRuntimeStop()
    {
        m_IsRunning = false;
        StopScripts();
        OnPhysics2DStop();
        OnPhysics3DStop();
        AudioEngine::StopAll();
    }

    void Scene::OnSimulationStart() { OnPhysics2DStart(); OnPhysics3DStart(); }
    void Scene::OnSimulationStop() { OnPhysics2DStop(); OnPhysics3DStop(); }

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
        for (const auto& field : info->Fields)
        {
            auto it = nsc.Fields.find(field.Name);
            if (it != nsc.Fields.end() && it->second.Type == field.Type)
                it->second.WriteTo(field.Accessor(instance));
        }
        nsc.Instance = instance;
        instance->OnCreate();
    }

    void Scene::StartScripts()
    {
        m_ScriptsRunning = true;
        std::vector<entt::entity> entities;
        for (auto e : m_Registry.view<NativeScriptComponent>())
            entities.push_back(e);
        std::reverse(entities.begin(), entities.end());
        for (auto e : entities)
            if (m_Registry.valid(e))
                CreateScriptInstance({ e, this });
    }

    void Scene::StopScripts()
    {
        m_ScriptsRunning = false;
        for (auto e : m_Registry.view<NativeScriptComponent>())
        {
            auto& nsc = m_Registry.get<NativeScriptComponent>(e);
            if (nsc.Instance)
            {
                ScriptableEntity* instance = nsc.Instance;
                nsc.Instance = nullptr;
                instance->OnDestroy();
                delete instance;
            }
        }
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
        m_PhysicsWorld = new b2World({ m_Settings.Gravity.x, m_Settings.Gravity.y });
        m_ContactListener = new ContactListener2D();
        m_PhysicsWorld->SetContactListener(m_ContactListener);
        m_PhysicsAccumulator = 0.0f;

        for (auto e : m_Registry.view<Rigidbody2DComponent>())
            CreatePhysicsBody({ e, this });
    }

    void Scene::OnPhysics2DStop()
    {
        if (!m_PhysicsWorld) return;
        for (auto e : m_Registry.view<Rigidbody2DComponent>()) m_Registry.get<Rigidbody2DComponent>(e).RuntimeBody = nullptr;
        for (auto e : m_Registry.view<BoxCollider2DComponent>()) m_Registry.get<BoxCollider2DComponent>(e).RuntimeFixture = nullptr;
        for (auto e : m_Registry.view<CircleCollider2DComponent>()) m_Registry.get<CircleCollider2DComponent>(e).RuntimeFixture = nullptr;
        delete m_PhysicsWorld;
        m_PhysicsWorld = nullptr;
        delete m_ContactListener;
        m_ContactListener = nullptr;
    }

    void Scene::CreatePhysicsBody(Entity entity)
    {
        auto& rb2d = entity.GetComponent<Rigidbody2DComponent>();
        if (rb2d.RuntimeBody) return;

        glm::vec3 position, rotation, scale;
        Math::DecomposeTransform(GetWorldTransform(entity), position, rotation, scale);
        uintptr_t id = (uintptr_t)(uint64_t)entity.GetUUID();

        b2BodyDef bodyDef;
        bodyDef.type = ToBox2DBodyType(rb2d.Type);
        bodyDef.position.Set(position.x, position.y);
        bodyDef.angle = rotation.z;
        bodyDef.fixedRotation = rb2d.FixedRotation;
        bodyDef.gravityScale = rb2d.GravityScale;
        bodyDef.linearDamping = rb2d.LinearDamping;
        bodyDef.angularDamping = rb2d.AngularDamping;
        bodyDef.bullet = rb2d.Bullet;
        bodyDef.userData.pointer = id;

        b2Body* body = m_PhysicsWorld->CreateBody(&bodyDef);
        rb2d.RuntimeBody = body;

        if (auto* bc2d = entity.TryGetComponent<BoxCollider2DComponent>())
        {
            b2PolygonShape shape;
            shape.SetAsBox(std::max(0.001f, bc2d->Size.x * std::abs(scale.x)),
                           std::max(0.001f, bc2d->Size.y * std::abs(scale.y)),
                           b2Vec2(bc2d->Offset.x, bc2d->Offset.y), 0.0f);
            b2FixtureDef fixtureDef;
            fixtureDef.shape = &shape;
            fixtureDef.density = bc2d->Density;
            fixtureDef.friction = bc2d->Friction;
            fixtureDef.restitution = bc2d->Restitution;
            fixtureDef.restitutionThreshold = bc2d->RestitutionThreshold;
            fixtureDef.isSensor = bc2d->IsSensor;
            fixtureDef.userData.pointer = id;
            bc2d->RuntimeFixture = body->CreateFixture(&fixtureDef);
        }

        if (auto* cc2d = entity.TryGetComponent<CircleCollider2DComponent>())
        {
            b2CircleShape shape;
            shape.m_p.Set(cc2d->Offset.x, cc2d->Offset.y);
            shape.m_radius = std::max(0.001f, std::abs(scale.x) * cc2d->Radius);
            b2FixtureDef fixtureDef;
            fixtureDef.shape = &shape;
            fixtureDef.density = cc2d->Density;
            fixtureDef.friction = cc2d->Friction;
            fixtureDef.restitution = cc2d->Restitution;
            fixtureDef.restitutionThreshold = cc2d->RestitutionThreshold;
            fixtureDef.isSensor = cc2d->IsSensor;
            fixtureDef.userData.pointer = id;
            cc2d->RuntimeFixture = body->CreateFixture(&fixtureDef);
        }
    }

    // ------------------------------------------------------------------ physics 3D
    void Scene::OnPhysics3DStart()
    {
        OnPhysics3DStop();
        if (m_Registry.view<Rigidbody3DComponent>().empty())
            return; // don't spin up Jolt for 2D-only scenes
        m_PhysicsWorld3D = std::make_unique<PhysicsWorld3D>(m_Settings.Gravity3D);
        for (auto e : m_Registry.view<Rigidbody3DComponent>())
            CreatePhysicsBody3D({ e, this });
        m_PhysicsWorld3D->OptimizeBroadPhase();
    }

    void Scene::OnPhysics3DStop()
    {
        if (!m_PhysicsWorld3D) return;
        for (auto e : m_Registry.view<Rigidbody3DComponent>())
            m_Registry.get<Rigidbody3DComponent>(e).RuntimeBodyID = 0xffffffff;
        m_PhysicsWorld3D.reset();
    }

    void Scene::CreatePhysicsBody3D(Entity entity)
    {
        if (!m_PhysicsWorld3D)
            m_PhysicsWorld3D = std::make_unique<PhysicsWorld3D>(m_Settings.Gravity3D);

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

        if (auto* c = entity.TryGetComponent<BoxCollider3DComponent>())
        {
            PhysicsBody3DDesc::Shape s;
            s.Type = PhysicsBody3DDesc::Shape::Kind::Box;
            s.HalfExtents = c->HalfExtents * scale;
            s.Offset = c->Offset * scale;
            s.Friction = c->Friction; s.Restitution = c->Restitution; s.IsSensor = c->IsSensor;
            desc.Shapes.push_back(s);
        }
        if (auto* c = entity.TryGetComponent<SphereCollider3DComponent>())
        {
            PhysicsBody3DDesc::Shape s;
            s.Type = PhysicsBody3DDesc::Shape::Kind::Sphere;
            s.Radius = c->Radius * std::max({ scale.x, scale.y, scale.z });
            s.Offset = c->Offset * scale;
            s.Friction = c->Friction; s.Restitution = c->Restitution; s.IsSensor = c->IsSensor;
            desc.Shapes.push_back(s);
        }
        if (auto* c = entity.TryGetComponent<CapsuleCollider3DComponent>())
        {
            PhysicsBody3DDesc::Shape s;
            s.Type = PhysicsBody3DDesc::Shape::Kind::Capsule;
            s.Radius = c->Radius * std::max(scale.x, scale.z);
            s.HalfHeight = c->HalfHeight * scale.y;
            s.Offset = c->Offset * scale;
            s.Friction = c->Friction; s.Restitution = c->Restitution; s.IsSensor = c->IsSensor;
            desc.Shapes.push_back(s);
        }
        if (desc.Shapes.empty())
        {
            ZM_CORE_WARN("'{}': Rigidbody3D uchun collider yo'q (Box/Sphere/Capsule Collider 3D qo'shing)", entity.GetName());
            return;
        }
        rb.RuntimeBodyID = m_PhysicsWorld3D->CreateBody(desc);
    }

    void Scene::DispatchContactEvents(const std::vector<ContactEvent>& events)
    {
        if (!m_ScriptsRunning) return;
        for (const auto& ev : events)
        {
            Entity a = GetEntityByUUID(ev.A), b = GetEntityByUUID(ev.B);
            if (!a || !b) continue;
            auto dispatch = [&](Entity self, Entity other) {
                if (!self.IsValid() || !other.IsValid()) return;
                if (auto* nsc = self.TryGetComponent<NativeScriptComponent>(); nsc && nsc->Instance)
                    ev.Begin ? nsc->Instance->OnCollisionBegin(other) : nsc->Instance->OnCollisionEnd(other);
            };
            dispatch(a, b);
            dispatch(b, a);
        }
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

    void Scene::StepPhysics(Timestep ts)
    {
        if (!m_PhysicsWorld && !m_PhysicsWorld3D) return;
        constexpr float fixedStep = 1.0f / 60.0f;

        // Kinematic/static bodies follow their transforms (lets scripts move them directly).
        if (m_PhysicsWorld)
        {
            for (auto e : m_Registry.view<Rigidbody2DComponent>())
            {
                auto& rb2d = m_Registry.get<Rigidbody2DComponent>(e);
                if (!rb2d.RuntimeBody || rb2d.Type == Rigidbody2DComponent::BodyType::Dynamic) continue;
                glm::vec3 p = GetWorldPosition({ e, this });
                float angle = m_Registry.get<TransformComponent>(e).Rotation.z;
                b2Body* body = (b2Body*)rb2d.RuntimeBody;
                const b2Vec2& bp = body->GetPosition();
                if (bp.x != p.x || bp.y != p.y || body->GetAngle() != angle)
                    body->SetTransform({ p.x, p.y }, angle);
            }
        }
        if (m_PhysicsWorld3D)
        {
            for (auto e : m_Registry.view<Rigidbody3DComponent>())
            {
                auto& rb = m_Registry.get<Rigidbody3DComponent>(e);
                if (rb.RuntimeBodyID == 0xffffffff || rb.Type == Rigidbody3DComponent::BodyType::Dynamic) continue;
                glm::vec3 p, r, s;
                Math::DecomposeTransform(GetWorldTransform({ e, this }), p, r, s);
                glm::vec3 bp; glm::quat bq;
                m_PhysicsWorld3D->GetTransform(rb.RuntimeBodyID, bp, bq);
                glm::quat q(r);
                if (glm::length(bp - p) > 1e-5f || std::abs(glm::dot(bq, q)) < 0.99999f)
                {
                    if (rb.Type == Rigidbody3DComponent::BodyType::Kinematic)
                        m_PhysicsWorld3D->MoveKinematic(rb.RuntimeBodyID, p, q, fixedStep);
                    else
                        m_PhysicsWorld3D->SetTransform(rb.RuntimeBodyID, p, q);
                }
            }
        }

        m_PhysicsAccumulator += ts;
        int steps = 0;
        while (m_PhysicsAccumulator >= fixedStep && steps < 5)
        {
            if (m_PhysicsWorld)
                m_PhysicsWorld->Step(fixedStep, m_Settings.VelocityIterations, m_Settings.PositionIterations);
            if (m_PhysicsWorld3D)
                m_PhysicsWorld3D->Step(fixedStep);
            m_PhysicsAccumulator -= fixedStep;
            steps++;
        }
        if (steps == 5)
            m_PhysicsAccumulator = 0.0f;

        if (m_PhysicsWorld)
        {
            for (auto e : m_Registry.view<Rigidbody2DComponent>())
            {
                auto& rb2d = m_Registry.get<Rigidbody2DComponent>(e);
                if (!rb2d.RuntimeBody || rb2d.Type != Rigidbody2DComponent::BodyType::Dynamic) continue;
                b2Body* body = (b2Body*)rb2d.RuntimeBody;
                const auto& position = body->GetPosition();
                Entity entity{ e, this };
                if (!GetParent(entity))
                {
                    auto& t = m_Registry.get<TransformComponent>(e);
                    t.Translation.x = position.x;
                    t.Translation.y = position.y;
                    t.Rotation.z = body->GetAngle();
                }
                else
                {
                    glm::vec3 wp = GetWorldPosition(entity);
                    ApplyWorldPose(*this, entity, { position.x, position.y, wp.z }, glm::quat(glm::vec3(0, 0, body->GetAngle())));
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
        if (m_ContactListener)
        {
            events = std::move(m_ContactListener->Events);
            m_ContactListener->Events.clear();
        }
        if (m_PhysicsWorld3D)
        {
            auto e3 = m_PhysicsWorld3D->TakeEvents();
            events.insert(events.end(), e3.begin(), e3.end());
        }
        DispatchContactEvents(events);
    }

    // ------------------------------------------------------------------ queries
    RaycastHit Scene::Raycast2D(const glm::vec2& origin, const glm::vec2& direction, float maxDistance)
    {
        RaycastHit hit;
        if (!m_PhysicsWorld || glm::length(direction) == 0.0f) return hit;
        glm::vec2 dir = glm::normalize(direction);

        struct Callback : b2RayCastCallback
        {
            RaycastHit* Out;
            float MaxDistance;
            float ReportFixture(b2Fixture* fixture, const b2Vec2& point, const b2Vec2& normal, float fraction) override
            {
                if (fixture->IsSensor()) return -1.0f; // ignore triggers
                Out->Hit = true;
                Out->EntityID = (uint64_t)fixture->GetUserData().pointer;
                Out->Point = { point.x, point.y, 0.0f };
                Out->Normal = { normal.x, normal.y, 0.0f };
                Out->Distance = fraction * MaxDistance;
                return fraction; // clip to closest
            }
        } cb;
        cb.Out = &hit;
        cb.MaxDistance = maxDistance;
        b2Vec2 p1(origin.x, origin.y), p2(origin.x + dir.x * maxDistance, origin.y + dir.y * maxDistance);
        m_PhysicsWorld->RayCast(&cb, p1, p2);
        return hit;
    }

    RaycastHit Scene::Raycast3D(const glm::vec3& origin, const glm::vec3& direction, float maxDistance)
    {
        if (!m_PhysicsWorld3D) return {};
        return m_PhysicsWorld3D->CastRay(origin, direction, maxDistance);
    }

    // ------------------------------------------------------------------ update
    void Scene::OnUpdateRuntime(Timestep ts)
    {
        if (!m_IsPaused || m_StepFrames-- > 0)
        {
            m_Time += ts;

            std::vector<entt::entity> scripted;
            for (auto e : m_Registry.view<NativeScriptComponent>())
                scripted.push_back(e);
            std::reverse(scripted.begin(), scripted.end());

            for (auto e : scripted)
                if (m_Registry.valid(e))
                    if (auto* inst = m_Registry.get<NativeScriptComponent>(e).Instance)
                        inst->OnUpdate(ts);

            StepPhysics(ts);

            for (auto e : scripted)
                if (m_Registry.valid(e))
                    if (auto* inst = m_Registry.get<NativeScriptComponent>(e).Instance)
                        inst->OnLateUpdate(ts);

            for (auto e : m_Registry.view<SpriteAnimationComponent>())
            {
                auto& anim = m_Registry.get<SpriteAnimationComponent>(e);
                if (anim.Playing) anim.Time += ts;
            }

            FlushDestroyQueue();
        }

        Entity cameraEntity = GetPrimaryCameraEntity();
        if (!cameraEntity)
            return;
        auto& camera = cameraEntity.GetComponent<CameraComponent>();
        glm::mat4 cameraWorld = GetWorldTransform(cameraEntity);
        RenderScene(glm::inverse(cameraWorld), camera.Camera.GetProjection(), glm::vec3(cameraWorld[3]), {});
    }

    void Scene::OnUpdateSimulation(Timestep ts, const EditorCamera& camera, const SceneRenderOptions& options)
    {
        if (!m_IsPaused || m_StepFrames-- > 0)
            StepPhysics(ts);
        RenderScene(camera.GetViewMatrix(), camera.GetProjection(), camera.GetPosition(), options);
    }

    void Scene::OnUpdateEditor(Timestep, const EditorCamera& camera, const SceneRenderOptions& options)
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
        for (auto e : m_Registry.view<SpriteRendererComponent>())
        {
            auto& sprite = m_Registry.get<SpriteRendererComponent>(e);
            sprite.Texture = sprite.TexturePath.empty() ? nullptr
                : TextureLibrary::Load(Project::GetAssetFileSystemPath(sprite.TexturePath), sprite.PixelArt ? TextureFilter::Nearest : TextureFilter::Linear);
        }
        for (auto e : m_Registry.view<MeshRendererComponent>())
        {
            auto& mr = m_Registry.get<MeshRendererComponent>(e);
            mr.MeshAsset = mr.Type == PrimitiveMesh::Model
                ? (mr.ModelPath.empty() ? nullptr : MeshLibrary::LoadOBJ(Project::GetAssetFileSystemPath(mr.ModelPath)))
                : MeshLibrary::GetPrimitive(mr.Type);
            mr.Mat.AlbedoMap = mr.AlbedoPath.empty() ? nullptr : TextureLibrary::Load(Project::GetAssetFileSystemPath(mr.AlbedoPath));
        }
        for (auto e : m_Registry.view<TextComponent>())
        {
            auto& text = m_Registry.get<TextComponent>(e);
            text.FontAsset = text.FontPath.empty() ? Font::GetDefault() : Font::Load(Project::GetAssetFileSystemPath(text.FontPath));
        }
    }

    // ------------------------------------------------------------------ rendering
    void Scene::RenderScene(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& cameraPosition,
                            const SceneRenderOptions& options)
    {
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
            return worldCache.emplace(e, local).first->second;
        };

        // ---------------- 3D
        SceneEnvironment env;
        env.DrawSky = m_Settings.DrawSky;
        env.SkyTopColor = m_Settings.SkyTopColor;
        env.SkyHorizonColor = m_Settings.SkyHorizonColor;
        env.GroundColor = m_Settings.GroundColor;
        env.AmbientIntensity = m_Settings.AmbientIntensity;
        env.Exposure = m_Settings.Exposure;
        env.ShadowDistance = m_Settings.ShadowDistance;

        for (auto e : m_Registry.view<TransformComponent, DirectionalLightComponent>())
        {
            const auto& l = m_Registry.get<DirectionalLightComponent>(e);
            env.HasSun = true;
            env.Sun.Direction = glm::normalize(glm::vec3(world(e) * glm::vec4(0, 0, -1, 0)));
            env.Sun.Color = l.Color;
            env.Sun.Intensity = l.Intensity;
            env.Sun.CastShadows = l.CastShadows;
            break;
        }
        for (auto e : m_Registry.view<TransformComponent, PointLightComponent>())
        {
            const auto& l = m_Registry.get<PointLightComponent>(e);
            env.PointLights.push_back({ glm::vec3(world(e)[3]), l.Color, l.Intensity, l.Radius });
        }

        Renderer3D::BeginScene(view, projection, cameraPosition, env);
        for (auto e : m_Registry.view<TransformComponent, MeshRendererComponent>())
        {
            auto& mr = m_Registry.get<MeshRendererComponent>(e);
            if (!mr.MeshAsset && mr.Type != PrimitiveMesh::Model)
                mr.MeshAsset = MeshLibrary::GetPrimitive(mr.Type);
            if (mr.MeshAsset)
                Renderer3D::SubmitMesh(mr.MeshAsset, world(e), mr.Mat, (int)(uint32_t)e, mr.CastShadows);
        }
        Renderer3D::EndScene();

        if (options.ShowGrid)
            Renderer3D::DrawGrid(view, projection, cameraPosition, options.GridPlane);

        // ---------------- 2D (sorted back-to-front for correct blending)
        struct Item2D
        {
            int Order;
            float Depth;
            int Kind; // 0 sprite, 1 circle, 2 text
            entt::entity Handle;
        };
        std::vector<Item2D> items;
        auto depthOf = [&](entt::entity e) { return (view * world(e)[3]).z; };

        for (auto e : m_Registry.view<TransformComponent, SpriteRendererComponent>())
            items.push_back({ m_Registry.get<SpriteRendererComponent>(e).SortingOrder, depthOf(e), 0, e });
        for (auto e : m_Registry.view<TransformComponent, CircleRendererComponent>())
            items.push_back({ m_Registry.get<CircleRendererComponent>(e).SortingOrder, depthOf(e), 1, e });
        for (auto e : m_Registry.view<TransformComponent, TextComponent>())
            items.push_back({ 1000, depthOf(e), 2, e });

        if (items.empty())
            return;

        std::stable_sort(items.begin(), items.end(), [](const Item2D& a, const Item2D& b) {
            if (a.Order != b.Order) return a.Order < b.Order;
            return a.Depth < b.Depth; // more negative = farther away
        });

        Renderer2D::BeginScene(projection * view);
        for (const auto& item : items)
        {
            const glm::mat4& transform = world(item.Handle);
            int id = (int)(uint32_t)item.Handle;
            if (item.Kind == 0)
            {
                auto& s = m_Registry.get<SpriteRendererComponent>(item.Handle);
                glm::mat4 t = transform;
                if (s.FlipX || s.FlipY)
                    t = t * glm::scale(glm::mat4(1.0f), { s.FlipX ? -1.0f : 1.0f, s.FlipY ? -1.0f : 1.0f, 1.0f });

                if (s.Texture)
                {
                    if (auto* anim = m_Registry.try_get<SpriteAnimationComponent>(item.Handle))
                    {
                        int cols = std::max(1, anim->Columns), rows = std::max(1, anim->Rows);
                        int frame = std::clamp(anim->GetCurrentFrame(), 0, cols * rows - 1);
                        int cx = frame % cols, cy = frame / cols;
                        glm::vec2 cell = { 1.0f / cols, 1.0f / rows };
                        // Frames are numbered left-to-right, top-to-bottom (texture v is flipped).
                        glm::vec2 uvMin = { cx * cell.x, 1.0f - (cy + 1) * cell.y };
                        Renderer2D::DrawQuad(t, s.Texture, uvMin, uvMin + cell, s.Color, id);
                    }
                    else
                        Renderer2D::DrawQuad(t, s.Texture, s.TilingFactor, s.Color, id);
                }
                else
                    Renderer2D::DrawQuad(t, s.Color, id);
            }
            else if (item.Kind == 1)
            {
                auto& c = m_Registry.get<CircleRendererComponent>(item.Handle);
                Renderer2D::DrawCircle(transform, c.Color, c.Thickness, c.Fade, id);
            }
            else
            {
                auto& text = m_Registry.get<TextComponent>(item.Handle);
                if (!text.FontAsset)
                    text.FontAsset = Font::GetDefault();
                Renderer2D::TextParams params{ text.Color, text.Kerning, text.LineSpacing };
                glm::mat4 t = transform;
                if (text.Centered)
                {
                    glm::vec2 size = Renderer2D::MeasureString(text.Text, text.FontAsset, params);
                    float firstLine = text.FontAsset->GetAscent();
                    t = t * glm::translate(glm::mat4(1.0f), { -size.x * 0.5f, size.y * 0.5f - firstLine, 0.0f });
                }
                Renderer2D::DrawString(text.Text, text.FontAsset, t, params, id);
            }
        }
        Renderer2D::EndScene();
    }

}
