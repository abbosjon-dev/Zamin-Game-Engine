#include "Zamin/Scene/Scene.h"
#include "Zamin/Scene/Entity.h"
#include "Zamin/Scene/Components.h"
#include "Zamin/Scripting/ScriptableEntity.h"
#include "Zamin/Scripting/ScriptEngine.h"
#include "Zamin/Renderer/Renderer2D.h"
#include "Zamin/Renderer/Renderer3D.h"
#include "Zamin/Renderer/RenderCommand.h"
#include "Zamin/Project/Project.h"
#include "Zamin/Audio/AudioEngine.h"

#include <box2d/b2_body.h>
#include <box2d/b2_circle_shape.h>
#include <box2d/b2_contact.h>
#include <box2d/b2_fixture.h>
#include <box2d/b2_polygon_shape.h>
#include <box2d/b2_world.h>
#include <box2d/b2_world_callbacks.h>

#include <algorithm>

namespace Zamin {

    // ------------------------------------------------------------------ contact listener
    struct ContactEvent
    {
        UUID A, B;
        bool Begin;
    };

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

    // ------------------------------------------------------------------ helpers
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
        for (auto e : registry.view<AudioSourceComponent>()) registry.get<AudioSourceComponent>(e).RuntimeHandle = 0;
    }

    // ------------------------------------------------------------------ scene
    Scene::Scene() = default;

    Scene::~Scene()
    {
        if (m_ScriptsRunning)
            StopScripts();
        OnPhysics2DStop();
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

        // Preserve creation order (entt views iterate newest-first).
        std::vector<entt::entity> ordered;
        for (auto e : srcRegistry.view<IDComponent>())
            ordered.push_back(e);
        std::reverse(ordered.begin(), ordered.end());

        for (auto e : ordered)
        {
            UUID uuid = srcRegistry.get<IDComponent>(e).ID;
            const auto& name = srcRegistry.get<TagComponent>(e).Tag;
            Entity newEntity = newScene->CreateEntityWithUUID(uuid, name);
            enttMap[uuid] = (entt::entity)newEntity;
        }

        CopyComponent(AllComponents{}, dstRegistry, srcRegistry, enttMap);
        ResetRuntimeData(dstRegistry);
        return newScene;
    }

    Entity Scene::CreateEntity(const std::string& name) { return CreateEntityWithUUID(UUID(), name); }

    Entity Scene::CreateEntityWithUUID(UUID uuid, const std::string& name)
    {
        Entity entity = { m_Registry.create(), this };
        entity.AddComponent<IDComponent>(uuid);
        entity.AddComponent<TransformComponent>();
        entity.AddComponent<TagComponent>(name.empty() ? "Entity" : name);
        m_EntityMap[uuid] = entity;
        return entity;
    }

    void Scene::DestroyEntity(Entity entity)
    {
        if (!entity) return;
        if (auto* nsc = entity.TryGetComponent<NativeScriptComponent>(); nsc && nsc->Instance)
        {
            nsc->Instance->OnDestroy();
            delete nsc->Instance;
            nsc->Instance = nullptr;
        }
        if (auto* rb = entity.TryGetComponent<Rigidbody2DComponent>(); rb && rb->RuntimeBody && m_PhysicsWorld)
        {
            m_PhysicsWorld->DestroyBody((b2Body*)rb->RuntimeBody);
            rb->RuntimeBody = nullptr;
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

    Entity Scene::DuplicateEntity(Entity entity)
    {
        Entity newEntity = CreateEntity(entity.GetName());
        CopyComponentIfExists(AllComponents{}, newEntity, entity);
        if (auto* rb = newEntity.TryGetComponent<Rigidbody2DComponent>()) rb->RuntimeBody = nullptr;
        if (auto* bc = newEntity.TryGetComponent<BoxCollider2DComponent>()) bc->RuntimeFixture = nullptr;
        if (auto* cc = newEntity.TryGetComponent<CircleCollider2DComponent>()) cc->RuntimeFixture = nullptr;
        if (auto* as = newEntity.TryGetComponent<AudioSourceComponent>()) as->RuntimeHandle = 0;
        return newEntity;
    }

    Entity Scene::Instantiate(Entity prefab)
    {
        if (!prefab) return {};
        Entity e = DuplicateEntity(prefab);
        if (m_PhysicsWorld && e.HasComponent<Rigidbody2DComponent>())
            CreatePhysicsBody(e);
        if (m_ScriptsRunning && e.HasComponent<NativeScriptComponent>())
            CreateScriptInstance(e);
        return e;
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

    // ------------------------------------------------------------------ lifecycle
    void Scene::OnRuntimeStart()
    {
        m_IsRunning = true;
        m_Time = 0.0f;
        OnPhysics2DStart();

        // Audio
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
        AudioEngine::StopAll();
    }

    void Scene::OnSimulationStart() { OnPhysics2DStart(); }
    void Scene::OnSimulationStop() { OnPhysics2DStop(); }

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
        nsc.Instance = info->Factory();
        nsc.Instance->m_Entity = entity;
        // Apply inspector values to the fresh instance.
        for (const auto& field : info->Fields)
        {
            auto it = nsc.Fields.find(field.Name);
            if (it != nsc.Fields.end() && it->second.Type == field.Type)
                it->second.WriteTo(field.Accessor(nsc.Instance));
        }
        nsc.Instance->OnCreate();
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
                nsc.Instance->OnDestroy();
                delete nsc.Instance;
                nsc.Instance = nullptr;
            }
        }
    }

    void Scene::DestroyScriptInstances()
    {
        bool running = m_ScriptsRunning;
        StopScripts();
        m_ScriptsRunning = running;
    }

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
        ResetRuntimeData(m_Registry);
        delete m_PhysicsWorld;
        m_PhysicsWorld = nullptr;
        delete m_ContactListener;
        m_ContactListener = nullptr;
    }

    void Scene::CreatePhysicsBody(Entity entity)
    {
        auto& transform = entity.GetComponent<TransformComponent>();
        auto& rb2d = entity.GetComponent<Rigidbody2DComponent>();
        uintptr_t id = (uintptr_t)(uint64_t)entity.GetUUID();

        b2BodyDef bodyDef;
        bodyDef.type = ToBox2DBodyType(rb2d.Type);
        bodyDef.position.Set(transform.Translation.x, transform.Translation.y);
        bodyDef.angle = transform.Rotation.z;
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
            shape.SetAsBox(std::max(0.001f, bc2d->Size.x * std::abs(transform.Scale.x)),
                           std::max(0.001f, bc2d->Size.y * std::abs(transform.Scale.y)),
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
            shape.m_radius = std::max(0.001f, std::abs(transform.Scale.x) * cc2d->Radius);
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

    void Scene::StepPhysics(Timestep ts)
    {
        if (!m_PhysicsWorld) return;

        // Kinematic/static bodies follow their transforms (lets scripts move them directly).
        for (auto e : m_Registry.view<Rigidbody2DComponent>())
        {
            auto& rb2d = m_Registry.get<Rigidbody2DComponent>(e);
            if (!rb2d.RuntimeBody || rb2d.Type == Rigidbody2DComponent::BodyType::Dynamic) continue;
            auto& t = m_Registry.get<TransformComponent>(e);
            b2Body* body = (b2Body*)rb2d.RuntimeBody;
            const b2Vec2& p = body->GetPosition();
            if (p.x != t.Translation.x || p.y != t.Translation.y || body->GetAngle() != t.Rotation.z)
                body->SetTransform({ t.Translation.x, t.Translation.y }, t.Rotation.z);
        }

        constexpr float fixedStep = 1.0f / 60.0f;
        m_PhysicsAccumulator += ts;
        int steps = 0;
        while (m_PhysicsAccumulator >= fixedStep && steps < 5)
        {
            m_PhysicsWorld->Step(fixedStep, m_Settings.VelocityIterations, m_Settings.PositionIterations);
            m_PhysicsAccumulator -= fixedStep;
            steps++;
        }
        if (steps == 5)
            m_PhysicsAccumulator = 0.0f;

        for (auto e : m_Registry.view<Rigidbody2DComponent>())
        {
            auto& rb2d = m_Registry.get<Rigidbody2DComponent>(e);
            if (!rb2d.RuntimeBody) continue;
            auto& t = m_Registry.get<TransformComponent>(e);
            b2Body* body = (b2Body*)rb2d.RuntimeBody;
            const auto& position = body->GetPosition();
            t.Translation.x = position.x;
            t.Translation.y = position.y;
            t.Rotation.z = body->GetAngle();
        }

        // Dispatch collision events after the step (the world is unlocked now).
        if (m_ContactListener && m_ScriptsRunning)
        {
            auto events = std::move(m_ContactListener->Events);
            m_ContactListener->Events.clear();
            for (const auto& ev : events)
            {
                Entity a = GetEntityByUUID(ev.A), b = GetEntityByUUID(ev.B);
                if (!a || !b) continue;
                auto dispatch = [&](Entity self, Entity other) {
                    if (auto* nsc = self.TryGetComponent<NativeScriptComponent>(); nsc && nsc->Instance)
                        ev.Begin ? nsc->Instance->OnCollisionBegin(other) : nsc->Instance->OnCollisionEnd(other);
                };
                dispatch(a, b);
                if (a && b) dispatch(b, a);
            }
        }
        else if (m_ContactListener)
        {
            m_ContactListener->Events.clear();
        }
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

            for (auto e : scripted)
                if (m_Registry.valid(e))
                    if (auto* inst = m_Registry.get<NativeScriptComponent>(e).Instance)
                        inst->OnUpdate(ts);

            StepPhysics(ts);

            for (auto e : scripted)
                if (m_Registry.valid(e))
                    if (auto* inst = m_Registry.get<NativeScriptComponent>(e).Instance)
                        inst->OnLateUpdate(ts);

            FlushDestroyQueue();
        }

        // Render from the primary camera
        Entity cameraEntity = GetPrimaryCameraEntity();
        if (!cameraEntity)
            return;
        auto& camera = cameraEntity.GetComponent<CameraComponent>();
        auto& transform = cameraEntity.GetComponent<TransformComponent>();
        glm::mat4 view = glm::inverse(transform.GetTransform());
        RenderScene(view, camera.Camera.GetProjection(), transform.Translation, {});
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
            sprite.Texture = sprite.TexturePath.empty() ? nullptr : TextureLibrary::Load(Project::GetAssetFileSystemPath(sprite.TexturePath));
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
        // ---------------- 3D
        SceneEnvironment env;
        env.DrawSky = m_Settings.DrawSky;
        env.SkyTopColor = m_Settings.SkyTopColor;
        env.SkyHorizonColor = m_Settings.SkyHorizonColor;
        env.GroundColor = m_Settings.GroundColor;
        env.AmbientIntensity = m_Settings.AmbientIntensity;
        env.Exposure = m_Settings.Exposure;
        env.ShadowDistance = m_Settings.ShadowDistance;

        {
            auto lights = m_Registry.view<TransformComponent, DirectionalLightComponent>();
            for (auto e : lights)
            {
                auto [t, l] = lights.get<TransformComponent, DirectionalLightComponent>(e);
                env.HasSun = true;
                env.Sun.Direction = t.GetForward();
                env.Sun.Color = l.Color;
                env.Sun.Intensity = l.Intensity;
                env.Sun.CastShadows = l.CastShadows;
                break;
            }
            auto points = m_Registry.view<TransformComponent, PointLightComponent>();
            for (auto e : points)
            {
                auto [t, l] = points.get<TransformComponent, PointLightComponent>(e);
                env.PointLights.push_back({ t.Translation, l.Color, l.Intensity, l.Radius });
            }
        }

        Renderer3D::BeginScene(view, projection, cameraPosition, env);
        {
            auto meshes = m_Registry.view<TransformComponent, MeshRendererComponent>();
            for (auto e : meshes)
            {
                auto [t, mr] = meshes.get<TransformComponent, MeshRendererComponent>(e);
                if (!mr.MeshAsset && mr.Type != PrimitiveMesh::Model)
                    mr.MeshAsset = MeshLibrary::GetPrimitive(mr.Type);
                if (mr.MeshAsset)
                    Renderer3D::SubmitMesh(mr.MeshAsset, t.GetTransform(), mr.Mat, (int)(uint32_t)e, mr.CastShadows);
            }
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
        auto depthOf = [&](const TransformComponent& t) { return (view * glm::vec4(t.Translation, 1.0f)).z; };

        for (auto e : m_Registry.view<TransformComponent, SpriteRendererComponent>())
            items.push_back({ m_Registry.get<SpriteRendererComponent>(e).SortingOrder, depthOf(m_Registry.get<TransformComponent>(e)), 0, e });
        for (auto e : m_Registry.view<TransformComponent, CircleRendererComponent>())
            items.push_back({ m_Registry.get<CircleRendererComponent>(e).SortingOrder, depthOf(m_Registry.get<TransformComponent>(e)), 1, e });
        for (auto e : m_Registry.view<TransformComponent, TextComponent>())
            items.push_back({ 1000, depthOf(m_Registry.get<TransformComponent>(e)), 2, e });

        if (items.empty())
            return;

        std::stable_sort(items.begin(), items.end(), [](const Item2D& a, const Item2D& b) {
            if (a.Order != b.Order) return a.Order < b.Order;
            return a.Depth < b.Depth; // more negative = farther away
        });

        Renderer2D::BeginScene(projection * view);
        for (const auto& item : items)
        {
            const auto& t = m_Registry.get<TransformComponent>(item.Handle);
            int id = (int)(uint32_t)item.Handle;
            if (item.Kind == 0)
            {
                auto& s = m_Registry.get<SpriteRendererComponent>(item.Handle);
                if (s.Texture)
                    Renderer2D::DrawQuad(t.GetTransform(), s.Texture, s.TilingFactor, s.Color, id);
                else
                    Renderer2D::DrawQuad(t.GetTransform(), s.Color, id);
            }
            else if (item.Kind == 1)
            {
                auto& c = m_Registry.get<CircleRendererComponent>(item.Handle);
                Renderer2D::DrawCircle(t.GetTransform(), c.Color, c.Thickness, c.Fade, id);
            }
            else
            {
                auto& text = m_Registry.get<TextComponent>(item.Handle);
                if (!text.FontAsset)
                    text.FontAsset = Font::GetDefault();
                Renderer2D::TextParams params{ text.Color, text.Kerning, text.LineSpacing };
                glm::mat4 transform = t.GetTransform();
                if (text.Centered)
                {
                    glm::vec2 size = Renderer2D::MeasureString(text.Text, text.FontAsset, params);
                    float firstLine = text.FontAsset->GetAscent();
                    transform = transform * glm::translate(glm::mat4(1.0f), { -size.x * 0.5f, size.y * 0.5f - firstLine, 0.0f });
                }
                Renderer2D::DrawString(text.Text, text.FontAsset, transform, params, id);
            }
        }
        Renderer2D::EndScene();
    }

}
