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
#include "Zamin/Core/Profiler.h"

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
        for (auto e : registry.view<ParticleSystemComponent>()) registry.get<ParticleSystemComponent>(e).Clear();
    }

    static void ResetRuntimeData(Entity e)
    {
        if (auto* c = e.TryGetComponent<Rigidbody2DComponent>()) c->RuntimeBody = nullptr;
        if (auto* c = e.TryGetComponent<BoxCollider2DComponent>()) c->RuntimeFixture = nullptr;
        if (auto* c = e.TryGetComponent<CircleCollider2DComponent>()) c->RuntimeFixture = nullptr;
        if (auto* c = e.TryGetComponent<Rigidbody3DComponent>()) c->RuntimeBodyID = 0xffffffff;
        if (auto* c = e.TryGetComponent<AudioSourceComponent>()) c->RuntimeHandle = 0;
        if (auto* c = e.TryGetComponent<SpriteAnimationComponent>()) c->Time = 0.0f;
        if (auto* c = e.TryGetComponent<TilemapComponent>()) c->RuntimeBody = nullptr;
        if (auto* c = e.TryGetComponent<CharacterController3DComponent>()) { c->RuntimeID = 0xffffffff; c->Grounded = false; }
        if (auto* c = e.TryGetComponent<ParticleSystemComponent>()) { c->Clear(); c->RandomState ^= (uint32_t)(uint64_t)e.GetUUID(); }
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
        if (auto* tm = entity.TryGetComponent<TilemapComponent>(); tm && tm->RuntimeBody && m_PhysicsWorld)
        {
            m_PhysicsWorld->DestroyBody((b2Body*)tm->RuntimeBody);
            tm->RuntimeBody = nullptr;
        }
        if (auto* rb = entity.TryGetComponent<Rigidbody3DComponent>(); rb && rb->RuntimeBodyID != 0xffffffff && m_PhysicsWorld3D)
        {
            m_PhysicsWorld3D->DestroyBody(rb->RuntimeBodyID);
            rb->RuntimeBodyID = 0xffffffff;
        }
        if (auto* cc = entity.TryGetComponent<CharacterController3DComponent>(); cc && cc->RuntimeID != 0xffffffff && m_PhysicsWorld3D)
        {
            m_PhysicsWorld3D->DestroyCharacter(cc->RuntimeID);
            cc->RuntimeID = 0xffffffff;
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
            if (m_PhysicsWorld3D && e.HasComponent<CharacterController3DComponent>())
                CreateCharacter3D(e);
            if (m_PhysicsWorld && e.HasComponent<Rigidbody2DComponent>())
                CreatePhysicsBody(e);
            if (m_PhysicsWorld && e.HasComponent<TilemapComponent>())
                CreateTilemapBody(e);
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
            {
                auto path = Project::GetAssetFileSystemPath(audio.Path);
                audio.RuntimeHandle = audio.Spatial
                    ? AudioEngine::PlaySpatial(path, GetWorldPosition({ e, this }), audio.Volume, audio.Pitch, audio.Loop, audio.MinDistance, audio.MaxDistance)
                    : AudioEngine::Play(path, audio.Volume, audio.Pitch, audio.Loop);
            }
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
        for (auto e : m_Registry.view<TilemapComponent>())
            CreateTilemapBody({ e, this });
    }

    void Scene::CreateTilemapBody(Entity entity)
    {
        auto& tm = entity.GetComponent<TilemapComponent>();
        if (!tm.Collision || tm.RuntimeBody || !m_PhysicsWorld) return;
        tm.EnsureSize();
        glm::vec3 position, rotation, scale;
        Math::DecomposeTransform(GetWorldTransform(entity), position, rotation, scale);

        b2BodyDef def;
        def.type = b2_staticBody;
        def.position.Set(position.x, position.y);
        def.angle = rotation.z;
        def.userData.pointer = (uintptr_t)(uint64_t)entity.GetUUID();
        b2Body* body = m_PhysicsWorld->CreateBody(&def);
        tm.RuntimeBody = body;

        float sx = tm.TileSize * std::abs(scale.x), sy = tm.TileSize * std::abs(scale.y);
        // Merge horizontal runs of solid tiles into single boxes (fewer fixtures, no seams).
        for (int y = 0; y < tm.Height; y++)
        {
            int x = 0;
            while (x < tm.Width)
            {
                if (tm.Get(x, y) < 0) { x++; continue; }
                int start = x;
                while (x < tm.Width && tm.Get(x, y) >= 0) x++;
                float w = (x - start) * sx;
                b2PolygonShape shape;
                shape.SetAsBox(w * 0.5f, sy * 0.5f, b2Vec2(start * sx + w * 0.5f, y * sy + sy * 0.5f), 0.0f);
                b2FixtureDef fd;
                fd.shape = &shape;
                fd.friction = tm.Friction;
                fd.userData.pointer = (uintptr_t)(uint64_t)entity.GetUUID();
                body->CreateFixture(&fd);
            }
        }
    }

    void Scene::OnPhysics2DStop()
    {
        if (!m_PhysicsWorld) return;
        for (auto e : m_Registry.view<Rigidbody2DComponent>()) m_Registry.get<Rigidbody2DComponent>(e).RuntimeBody = nullptr;
        for (auto e : m_Registry.view<BoxCollider2DComponent>()) m_Registry.get<BoxCollider2DComponent>(e).RuntimeFixture = nullptr;
        for (auto e : m_Registry.view<CircleCollider2DComponent>()) m_Registry.get<CircleCollider2DComponent>(e).RuntimeFixture = nullptr;
        for (auto e : m_Registry.view<TilemapComponent>()) m_Registry.get<TilemapComponent>(e).RuntimeBody = nullptr;
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
        if (m_Registry.view<Rigidbody3DComponent>().empty() && m_Registry.view<CharacterController3DComponent>().empty())
            return; // don't spin up Jolt for 2D-only scenes
        m_PhysicsWorld3D = std::make_unique<PhysicsWorld3D>(m_Settings.Gravity3D);
        for (auto e : m_Registry.view<Rigidbody3DComponent>())
            CreatePhysicsBody3D({ e, this });
        m_PhysicsWorld3D->OptimizeBroadPhase();
        for (auto e : m_Registry.view<CharacterController3DComponent>())
            CreateCharacter3D({ e, this });
    }

    void Scene::CreateCharacter3D(Entity entity)
    {
        if (!m_PhysicsWorld3D)
            m_PhysicsWorld3D = std::make_unique<PhysicsWorld3D>(m_Settings.Gravity3D);
        auto& cc = entity.GetComponent<CharacterController3DComponent>();
        if (cc.RuntimeID != 0xffffffff) return;
        CharacterDesc desc;
        desc.EntityID = entity.GetUUID();
        desc.Position = GetWorldPosition(entity);
        desc.Radius = cc.Radius;
        desc.Height = cc.Height;
        desc.MaxSlopeDegrees = cc.MaxSlope;
        desc.Mass = cc.Mass;
        cc.RuntimeID = m_PhysicsWorld3D->CreateCharacter(desc);
        cc.VerticalVelocity = 0.0f;
        cc.Grounded = false;
    }

    void Scene::OnPhysics3DStop()
    {
        if (!m_PhysicsWorld3D) return;
        for (auto e : m_Registry.view<CharacterController3DComponent>())
            m_Registry.get<CharacterController3DComponent>(e).RuntimeID = 0xffffffff;
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
        ZM_PROFILE_SCOPE("Fizika");
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
        m_PhysicsAccumulator += ts;
        int stepCount = std::min(5, (int)(m_PhysicsAccumulator / fixedStep));

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
        while (m_PhysicsAccumulator >= fixedStep && steps < 5)
        {
            if (m_PhysicsWorld)
                m_PhysicsWorld->Step(fixedStep, m_Settings.VelocityIterations, m_Settings.PositionIterations);
            if (m_PhysicsWorld3D)
            {
                m_PhysicsWorld3D->Step(fixedStep);
                StepCharacters(fixedStep);
            }
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
        UpdateUI();
        if (!m_IsPaused || m_StepFrames-- > 0)
        {
            m_Time += ts;

            std::vector<entt::entity> scripted;
            for (auto e : m_Registry.view<NativeScriptComponent>())
                scripted.push_back(e);
            std::reverse(scripted.begin(), scripted.end());

            {
                ZM_PROFILE_SCOPE("Skriptlar");
                for (auto e : scripted)
                    if (m_Registry.valid(e))
                        if (auto* inst = m_Registry.get<NativeScriptComponent>(e).Instance)
                            inst->OnUpdate(ts);
            }

            StepPhysics(ts);

            {
                ZM_PROFILE_SCOPE("Skriptlar");
                for (auto e : scripted)
                    if (m_Registry.valid(e))
                        if (auto* inst = m_Registry.get<NativeScriptComponent>(e).Instance)
                            inst->OnLateUpdate(ts);
            }

            for (auto e : m_Registry.view<SpriteAnimationComponent>())
            {
                auto& anim = m_Registry.get<SpriteAnimationComponent>(e);
                if (anim.Playing) anim.Time += ts;
            }
            UpdateParticles(ts, false);
            UpdateAnimators(ts, false);
            UpdateAudio();

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
        {
            StepPhysics(ts);
            UpdateParticles(ts, false);
            UpdateAnimators(ts, false);
        }
        RenderScene(camera.GetViewMatrix(), camera.GetProjection(), camera.GetPosition(), options);
    }

    void Scene::OnUpdateEditor(Timestep ts, const EditorCamera& camera, const SceneRenderOptions& options)
    {
        UpdateParticles(ts, true);
        UpdateAnimators(ts, true);
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
            ResolveMeshRenderer(mr);
        }
        for (auto e : m_Registry.view<TilemapComponent>())
        {
            auto& tm = m_Registry.get<TilemapComponent>(e);
            tm.Tileset = tm.TilesetPath.empty() ? nullptr
                : TextureLibrary::Load(Project::GetAssetFileSystemPath(tm.TilesetPath), tm.PixelArt ? TextureFilter::Nearest : TextureFilter::Linear);
        }
        for (auto e : m_Registry.view<TextComponent>())
        {
            auto& text = m_Registry.get<TextComponent>(e);
            text.FontAsset = text.FontPath.empty() ? Font::GetDefault() : Font::Load(Project::GetAssetFileSystemPath(text.FontPath));
        }
    }

    void Scene::UpdateAnimators(Timestep ts, bool editor)
    {
        ZM_PROFILE_SCOPE("Animatsiya");
        for (auto e : m_Registry.view<AnimatorComponent>())
        {
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

            bool advance = anim.Playing && (!editor || anim.PlayInEditor);
            if (advance)
            {
                anim.Time += ts * anim.Speed;
                if (anim.IsFading())
                {
                    anim.PreviousTime += ts * anim.PreviousSpeed;
                    anim.FadeElapsed += ts;
                }
            }

            if (!model || !model->IsSkinned())
            {
                anim.JointMatrices.clear();
                continue;
            }
            if (anim.IsFading())
            {
                const AnimationClip* prev = findClip(anim.PreviousClip);
                float w = anim.FadeElapsed / anim.FadeDuration;
                w = w * w * (3.0f - 2.0f * w); // smoothstep
                EvaluatePoseBlended(model->GetSkeleton(), prev, wrap(prev, anim.PreviousTime, anim.PreviousLoop),
                                    clip, wrap(clip, anim.Time, anim.Loop), w, anim.JointMatrices);
            }
            else
                EvaluatePose(model->GetSkeleton(), clip, wrap(clip, anim.Time, anim.Loop), anim.JointMatrices);
        }
    }

    void Scene::UpdateAudio()
    {
        if (!AudioEngine::IsAvailable()) return;
        if (Entity cam = GetPrimaryCameraEntity())
        {
            glm::mat4 w = GetWorldTransform(cam);
            AudioEngine::SetListener(glm::vec3(w[3]), -glm::normalize(glm::vec3(w[2])), glm::normalize(glm::vec3(w[1])));
        }
        for (auto e : m_Registry.view<AudioSourceComponent>())
        {
            auto& a = m_Registry.get<AudioSourceComponent>(e);
            if (a.Spatial && a.RuntimeHandle)
                AudioEngine::SetSoundPosition(a.RuntimeHandle, GetWorldPosition({ e, this }));
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
            if (editor && !ps.PlayInEditor)
            {
                if (!ps.Particles.empty()) ps.Clear();
                continue;
            }
            Entity entity{ e, this };
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
        if (auto* ps = entity.TryGetComponent<ParticleSystemComponent>())
            SpawnParticles(*ps, GetWorldTransform(entity), count);
    }

    // ------------------------------------------------------------------ UI
    static glm::vec2 AnchorOffset(UITransformComponent::AnchorPoint a, const glm::vec2& viewport)
    {
        int i = (int)a;
        float fx = (i % 3) * 0.5f;          // 0, 0.5, 1
        float fy = 1.0f - (i / 3) * 0.5f;   // top = 1
        return { fx * viewport.x, fy * viewport.y };
    }

    static glm::vec2 UICenter(const UITransformComponent& ui, const glm::vec2& viewport)
    {
        return AnchorOffset(ui.Anchor, viewport) + ui.Position; // bottom-left origin, pixels
    }

    void Scene::UpdateUI()
    {
        glm::vec2 viewport = GetViewportSize();
        glm::vec2 mouse = { m_MousePosition.x, viewport.y - m_MousePosition.y }; // to bottom-left origin
        bool pressedNow = m_MouseDown && !m_MouseWasDown;
        bool released = !m_MouseDown && m_MouseWasDown;
        m_MouseWasDown = m_MouseDown;

        entt::entity hovered = entt::null;
        int bestOrder = INT32_MIN;
        for (auto e : m_Registry.view<UITransformComponent, UIButtonComponent>())
        {
            auto& ui = m_Registry.get<UITransformComponent>(e);
            auto& button = m_Registry.get<UIButtonComponent>(e);
            button.Clicked = false;
            button.Hovered = false;
            if (!button.Interactable) continue;
            glm::vec2 c = UICenter(ui, viewport), half = glm::abs(ui.Size) * 0.5f;
            if (std::abs(mouse.x - c.x) <= half.x && std::abs(mouse.y - c.y) <= half.y && ui.Order >= bestOrder)
            {
                hovered = e;
                bestOrder = ui.Order;
            }
        }

        if (hovered != entt::null)
            m_Registry.get<UIButtonComponent>(hovered).Hovered = true;
        if (pressedNow)
            m_PressedButton = hovered;

        std::vector<entt::entity> clicked;
        for (auto e : m_Registry.view<UIButtonComponent>())
        {
            auto& b = m_Registry.get<UIButtonComponent>(e);
            b.Pressed = m_MouseDown && m_PressedButton == e && b.Hovered;
            if (released && m_PressedButton == e && b.Hovered)
            {
                b.Clicked = true;
                clicked.push_back(e);
            }
        }
        if (released)
            m_PressedButton = entt::null;

        if (m_ScriptsRunning)
            for (auto e : clicked)
                if (m_Registry.valid(e))
                    if (auto* nsc = m_Registry.try_get<NativeScriptComponent>(e); nsc && nsc->Instance)
                        nsc->Instance->OnClick();
    }

    glm::vec3 Scene::ScreenToWorld(const glm::vec2& screen, float depthZ)
    {
        Entity cam = GetPrimaryCameraEntity();
        glm::vec2 size = GetViewportSize();
        if (!cam || size.x <= 0.0f || size.y <= 0.0f) return glm::vec3(0.0f);
        glm::mat4 viewProj = cam.GetComponent<CameraComponent>().Camera.GetProjection() * glm::inverse(GetWorldTransform(cam));
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
        auto tex = [](const std::string& p) { return p.empty() ? nullptr : TextureLibrary::Load(Project::GetAssetFileSystemPath(p)); };
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
        mr.Mat.AlbedoMap = tex(mr.AlbedoPath);
        mr.Mat.NormalMap = tex(mr.NormalPath);
        mr.Mat.MetalRoughMap = tex(mr.MetalRoughPath);
        mr.Mat.EmissiveMap = tex(mr.EmissivePath);
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

        ZM_PROFILE_SCOPE("Render");
        m_LastProjection = projection;
        Renderer3D::BeginScene(view, projection, cameraPosition, env);
        for (auto e : m_Registry.view<TransformComponent, MeshRendererComponent>())
        {
            auto& mr = m_Registry.get<MeshRendererComponent>(e);
            if (!mr.MeshAsset && mr.Type != PrimitiveMesh::Model)
                mr.MeshAsset = MeshLibrary::GetPrimitive(mr.Type);
            if (mr.Type == PrimitiveMesh::Model && mr.ModelAsset)
            {
                for (const auto& sub : mr.ModelAsset->GetSubMeshes())
                {
                    Material m = sub.Mat;
                    m.Albedo *= mr.Mat.Albedo;
                    if (mr.Mat.AlbedoMap) m.AlbedoMap = mr.Mat.AlbedoMap;
                    if (mr.Mat.NormalMap) m.NormalMap = mr.Mat.NormalMap;
                    if (mr.Mat.MetalRoughMap) m.MetalRoughMap = mr.Mat.MetalRoughMap;
                    if (mr.Mat.EmissiveMap) m.EmissiveMap = mr.Mat.EmissiveMap;
                    m.Emission = m.Emission * m.EmissionStrength + mr.Mat.Emission * mr.Mat.EmissionStrength;
                    m.EmissionStrength = 1.0f;
                    m.Tiling *= mr.Mat.Tiling;
                    m.NormalStrength *= mr.Mat.NormalStrength;
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

        auto isUI = [&](entt::entity e) { return m_Registry.all_of<UITransformComponent>(e); };
        for (auto e : m_Registry.view<TransformComponent, SpriteRendererComponent>())
            if (!isUI(e)) items.push_back({ m_Registry.get<SpriteRendererComponent>(e).SortingOrder, depthOf(e), 0, e });
        for (auto e : m_Registry.view<TransformComponent, CircleRendererComponent>())
            if (!isUI(e)) items.push_back({ m_Registry.get<CircleRendererComponent>(e).SortingOrder, depthOf(e), 1, e });
        for (auto e : m_Registry.view<TransformComponent, TextComponent>())
            if (!isUI(e)) items.push_back({ 1000, depthOf(e), 2, e });
        for (auto e : m_Registry.view<TransformComponent, TilemapComponent>())
            if (!isUI(e)) items.push_back({ m_Registry.get<TilemapComponent>(e).SortingOrder, depthOf(e), 4, e });
        for (auto e : m_Registry.view<TransformComponent, ParticleSystemComponent>())
            if (!m_Registry.get<ParticleSystemComponent>(e).Particles.empty())
                items.push_back({ m_Registry.get<ParticleSystemComponent>(e).SortingOrder, depthOf(e), 3, e });

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
            else if (item.Kind == 2)
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
            else if (item.Kind == 4)
            {
                auto& tm = m_Registry.get<TilemapComponent>(item.Handle);
                tm.EnsureSize();
                if (!tm.Tileset && !tm.TilesetPath.empty())
                    tm.Tileset = TextureLibrary::Load(Project::GetAssetFileSystemPath(tm.TilesetPath), tm.PixelArt ? TextureFilter::Nearest : TextureFilter::Linear);
                int cols = std::max(1, tm.TilesetColumns), rows = std::max(1, tm.TilesetRows);
                glm::vec2 cell = { 1.0f / cols, 1.0f / rows };
                // tiny inset avoids bleeding between neighbouring tiles
                glm::vec2 inset = tm.Tileset ? glm::vec2(0.5f / tm.Tileset->GetWidth(), 0.5f / tm.Tileset->GetHeight()) : glm::vec2(0.0f);
                for (int y = 0; y < tm.Height; y++)
                    for (int x = 0; x < tm.Width; x++)
                    {
                        int tile = tm.Tiles[(size_t)y * tm.Width + x];
                        if (tile < 0) continue;
                        glm::mat4 t = transform * glm::translate(glm::mat4(1.0f), { (x + 0.5f) * tm.TileSize, (y + 0.5f) * tm.TileSize, 0.0f })
                                                * glm::scale(glm::mat4(1.0f), { tm.TileSize, tm.TileSize, 1.0f });
                        if (tm.Tileset)
                        {
                            int tx = tile % cols, ty = tile / cols;
                            glm::vec2 uvMin = { tx * cell.x, 1.0f - (ty + 1) * cell.y };
                            Renderer2D::DrawQuad(t, tm.Tileset, uvMin + inset, uvMin + cell - inset, tm.Color, id);
                        }
                        else
                            Renderer2D::DrawQuad(t, tm.Color * glm::vec4(0.6f + 0.05f * (tile % 8), 0.6f, 0.65f, 1.0f), id);
                    }
            }
            else
            {
                auto& ps = m_Registry.get<ParticleSystemComponent>(item.Handle);
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
        Renderer2D::EndScene();
        }

        RenderUI();
    }

    void Scene::RenderUI()
    {
        // ---------------- screen-space UI (pixels, bottom-left origin)
        auto uiView = m_Registry.view<UITransformComponent>();
        if (uiView.empty() || m_ViewportWidth == 0 || m_ViewportHeight == 0)
            return;
        glm::vec2 viewport = GetViewportSize();

        std::vector<entt::entity> ui(uiView.begin(), uiView.end());
        std::reverse(ui.begin(), ui.end()); // views iterate newest-first; keep creation order for equal Order
        std::stable_sort(ui.begin(), ui.end(), [&](entt::entity a, entt::entity b) {
            return m_Registry.get<UITransformComponent>(a).Order < m_Registry.get<UITransformComponent>(b).Order;
        });

        RenderCommand::SetDepthTest(false);
        Renderer2D::BeginScene(glm::ortho(0.0f, viewport.x, 0.0f, viewport.y, -1.0f, 1.0f));
        for (auto e : ui)
        {
            const auto& t = m_Registry.get<UITransformComponent>(e);
            glm::vec2 center = UICenter(t, viewport);
            int id = (int)(uint32_t)e;
            glm::mat4 rect = glm::translate(glm::mat4(1.0f), glm::vec3(center, 0.0f)) * glm::scale(glm::mat4(1.0f), glm::vec3(t.Size, 1.0f));

            glm::vec4 tint(1.0f);
            if (auto* b = m_Registry.try_get<UIButtonComponent>(e))
                tint = !b->Interactable ? glm::vec4(0.5f, 0.5f, 0.5f, 1.0f) : b->Pressed ? b->PressedColor : b->Hovered ? b->HoverColor : b->NormalColor;

            if (auto* s = m_Registry.try_get<SpriteRendererComponent>(e))
            {
                glm::vec4 c = s->Color * tint;
                if (s->Texture) Renderer2D::DrawQuad(rect, s->Texture, s->TilingFactor, c, id);
                else Renderer2D::DrawQuad(rect, c, id);
            }
            else if (m_Registry.all_of<UIButtonComponent>(e))
                Renderer2D::DrawQuad(rect, tint, id);

            if (auto* c = m_Registry.try_get<CircleRendererComponent>(e))
                Renderer2D::DrawCircle(rect, c->Color, c->Thickness, c->Fade, id);

            if (auto* text = m_Registry.try_get<TextComponent>(e))
            {
                if (!text->FontAsset) text->FontAsset = Font::GetDefault();
                Renderer2D::TextParams params{ text->Color, text->Kerning, text->LineSpacing };
                float scale = std::max(1.0f, t.FontSize > 0.0f ? t.FontSize : t.Size.y);
                glm::mat4 tm = glm::translate(glm::mat4(1.0f), glm::vec3(center, 0.0f)) * glm::scale(glm::mat4(1.0f), glm::vec3(scale, scale, 1.0f));
                glm::vec2 size = Renderer2D::MeasureString(text->Text, text->FontAsset, params);
                float asc = text->FontAsset->GetAscent();
                // Text is vertically centered on the rect; horizontally centered or left-aligned.
                float x = text->Centered ? -size.x * 0.5f : -t.Size.x * 0.5f / scale;
                tm = tm * glm::translate(glm::mat4(1.0f), { x, size.y * 0.5f - asc, 0.0f });
                Renderer2D::DrawString(text->Text, text->FontAsset, tm, params, id);
            }
        }
        Renderer2D::EndScene();
        RenderCommand::SetDepthTest(true);
    }

}
