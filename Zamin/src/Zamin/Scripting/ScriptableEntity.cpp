#include "Zamin/Scripting/ScriptableEntity.h"
#include "Zamin/Audio/Audio.h"
#include "Zamin/Audio/AudioEngine.h"
#include "Zamin/Project/Project.h"
#include "Zamin/Scene/SceneSerializer.h"
#include "Zamin/Utils/FileSystem.h"

#include "Zamin/Physics/PhysicsWorld3D.h"

#include <box2d/b2_body.h>

#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>

namespace Zamin {

    bool ScriptableEntity::IsEnabled() const
    {
        Entity e = m_Entity;
        if (!e.IsValid()) return false;
        auto* nsc = e.TryGetComponent<NativeScriptComponent>();
        return nsc && nsc->Enabled;
    }

    void ScriptableEntity::SetEnabled(bool enabled)
    {
        if (m_Entity.IsValid())
            m_Entity.GetScene()->SetScriptEnabled(m_Entity, enabled);
    }

    void ScriptableEntity::SetActive(bool active) { SetActive(m_Entity, active); }

    void ScriptableEntity::SetActive(Entity other, bool active)
    {
        // Entities of this script's scene only (a handle kept from another scene does nothing).
        if (m_Entity.IsValid() && other.IsValid() && other.GetScene() == m_Entity.GetScene())
            m_Entity.GetScene()->SetActive(other, active);
    }

    bool ScriptableEntity::IsActive() const
    {
        Entity e = m_Entity;
        return e.IsValid() && e.IsActive();
    }

    bool ScriptableEntity::IsActiveInHierarchy() const
    {
        Entity e = m_Entity;
        return e.IsValid() && e.IsActiveInHierarchy();
    }

    bool ScriptableEntity::IsActiveAndEnabled() const
    {
        return IsEnabled() && IsActiveInHierarchy();
    }

    // ---- materials
    static MeshRendererComponent* MeshRendererOf(Entity owner, Entity e)
    {
        // Entities of the script's own scene only.
        if (!e.IsValid() || !owner.IsValid() || e.GetScene() != owner.GetScene())
            return nullptr;
        return e.TryGetComponent<MeshRendererComponent>();
    }

    Ref<MaterialAsset> ScriptableEntity::GetMaterial() { return GetMaterial(m_Entity, -1); }
    Ref<MaterialAsset> ScriptableEntity::GetMaterial(size_t submesh) { return GetMaterial(m_Entity, (int)submesh); }

    Ref<MaterialAsset> ScriptableEntity::GetMaterial(Entity other, int submesh)
    {
        MeshRendererComponent* mr = MeshRendererOf(m_Entity, other);
        if (!mr)
            return nullptr;
        return submesh < 0 ? Scene::GetMaterialInstance(*mr) : Scene::GetMaterialInstance(*mr, (size_t)submesh);
    }

    Ref<MaterialAsset> ScriptableEntity::GetSharedMaterial(int submesh) const { return GetSharedMaterial(m_Entity, submesh); }

    Ref<MaterialAsset> ScriptableEntity::GetSharedMaterial(Entity other, int submesh) const
    {
        MeshRendererComponent* mr = MeshRendererOf(m_Entity, other);
        if (!mr)
            return nullptr;
        return submesh < 0 ? mr->MaterialRef : Scene::GetMeshMaterialAsset(*mr, (size_t)submesh);
    }

    bool ScriptableEntity::SetSharedMaterial(const std::string& assetPath, int submesh) { return SetSharedMaterial(m_Entity, assetPath, submesh); }

    bool ScriptableEntity::SetSharedMaterial(Entity other, const std::string& assetPath, int submesh)
    {
        MeshRendererComponent* mr = MeshRendererOf(m_Entity, other);
        if (!mr || !Scene::SetMeshMaterial(*mr, assetPath, submesh))
            return false;
        if (!assetPath.empty() && !(submesh < 0 ? mr->MaterialRef : Scene::GetMeshMaterialAsset(*mr, (size_t)submesh)))
            ZM_WARN("Material yuklanmadi: {} ({})", assetPath, other.GetName());
        return true;
    }

    void ScriptableEntity::ResetMaterial() { ResetMaterial(m_Entity); }

    void ScriptableEntity::ResetMaterial(Entity other)
    {
        if (MeshRendererComponent* mr = MeshRendererOf(m_Entity, other))
            Scene::ClearMaterialInstances(*mr);
    }

    Entity ScriptableEntity::FindEntity(std::string_view name) const
    {
        Scene* scene = m_Entity.GetScene();
        if (Entity e = scene->FindEntityByName(name))
            return e;
        SceneManager* manager = scene->GetSceneManager();
        return manager ? manager->FindEntityByName(name) : Entity();
    }

    std::vector<Entity> ScriptableEntity::FindEntities(std::string_view name) const { return m_Entity.GetScene()->FindEntitiesByName(name); }

    std::vector<Entity> ScriptableEntity::FindEntitiesWithTag(std::string_view tag) const
    {
        Scene* scene = m_Entity.GetScene();
        std::vector<Entity> found = scene->FindEntitiesWithTag(tag);
        if (SceneManager* manager = scene->GetSceneManager())
            for (Entity e : manager->FindEntitiesWithTag(tag))
                if (e.GetScene() != scene)
                    found.push_back(e);
        return found;
    }

    Entity ScriptableEntity::FindEntityWithTag(std::string_view tag) const
    {
        Scene* scene = m_Entity.GetScene();
        if (Entity e = scene->FindEntityWithTag(tag))
            return e;
        SceneManager* manager = scene->GetSceneManager();
        return manager ? manager->FindEntityWithTag(tag) : Entity();
    }

    Entity ScriptableEntity::Instantiate(Entity prefab) { return m_Entity.GetScene()->Instantiate(prefab); }

    Entity ScriptableEntity::Instantiate(Entity prefab, const glm::vec3& position)
    {
        // The position is set before physics bodies are created.
        return m_Entity.GetScene()->Instantiate(prefab, position);
    }

    Entity ScriptableEntity::Instantiate(const std::string& prefabPath, const glm::vec3& position)
    {
        Scene* scene = m_Entity.GetScene();
        // Create first, then move the root before physics bodies are created.
        std::string text = FileSystem::ReadTextFile(Project::GetAssetFileSystemPath(prefabPath));
        if (text.empty()) return {};
        std::vector<Entity> entities = SceneSerializer::DeserializeEntities(*scene, text, true);
        if (entities.empty()) return {};
        entities.front().GetComponent<TransformComponent>().Translation = position;
        scene->ResolveAssets();
        scene->OnEntitiesSpawned(entities);
        return entities.front();
    }

    Entity ScriptableEntity::GetParent() const { return m_Entity.GetScene()->GetParent(m_Entity); }
    void ScriptableEntity::SetParent(Entity parent, bool keepWorld) { m_Entity.GetScene()->SetParent(m_Entity, parent, keepWorld); }
    glm::vec3 ScriptableEntity::GetWorldPosition() const { return m_Entity.GetScene()->GetWorldPosition(m_Entity); }
    std::vector<Entity> ScriptableEntity::GetChildren() const { return m_Entity.GetScene()->GetChildren(m_Entity); }
    size_t ScriptableEntity::GetChildCount() const { return m_Entity.GetScene()->GetChildCount(m_Entity); }
    Entity ScriptableEntity::GetChild(size_t index) const { return m_Entity.GetScene()->GetChild(m_Entity, index); }
    Entity ScriptableEntity::FindChild(std::string_view name, bool recursive) const { return m_Entity.GetScene()->FindChild(m_Entity, name, recursive); }
    std::vector<Entity> ScriptableEntity::GetDescendants() const { return m_Entity.GetScene()->GetDescendants(m_Entity); }
    Entity ScriptableEntity::GetRoot() const { return m_Entity.GetScene()->GetRoot(m_Entity); }

    static Entity VisitSubtree(Scene& scene, Entity e, const std::function<bool(Entity)>& visit)
    {
        if (visit(e))
            return e;
        // Copy: visit() may reparent / spawn entities.
        std::vector<UUID> children = e.GetComponent<RelationshipComponent>().Children;
        for (UUID id : children)
            if (Entity c = scene.GetEntityByUUID(id))
                if (Entity found = VisitSubtree(scene, c, visit))
                    return found;
        return {};
    }

    Entity ScriptableEntity::VisitChildren(bool includeSelf, const std::function<bool(Entity)>& visit) const
    {
        Scene& scene = *m_Entity.GetScene();
        if (!m_Entity.IsValid())
            return {};
        if (includeSelf)
            return VisitSubtree(scene, m_Entity, visit);
        for (Entity c : scene.GetChildren(m_Entity))
            if (Entity found = VisitSubtree(scene, c, visit))
                return found;
        return {};
    }

    Entity ScriptableEntity::VisitParents(bool includeSelf, const std::function<bool(Entity)>& visit) const
    {
        Scene& scene = *m_Entity.GetScene();
        if (!m_Entity.IsValid())
            return {};
        for (Entity e = includeSelf ? m_Entity : scene.GetParent(m_Entity); e; e = scene.GetParent(e))
            if (visit(e))
                return e;
        return {};
    }

    static uint32_t BodyID3D(const Entity& e)
    {
        auto* rb = const_cast<Entity&>(e).TryGetComponent<Rigidbody3DComponent>();
        return rb ? rb->RuntimeBodyID : 0xffffffff;
    }

    glm::vec3 ScriptableEntity::GetVelocity3D() const
    {
        auto* w = m_Entity.GetScene()->GetPhysicsWorld3D();
        uint32_t id = BodyID3D(m_Entity);
        return (w && id != 0xffffffff) ? w->GetLinearVelocity(id) : glm::vec3(0.0f);
    }

    void ScriptableEntity::SetVelocity3D(const glm::vec3& v)
    {
        auto* w = m_Entity.GetScene()->GetPhysicsWorld3D();
        if (uint32_t id = BodyID3D(m_Entity); w && id != 0xffffffff) w->SetLinearVelocity(id, v);
    }

    glm::vec3 ScriptableEntity::GetAngularVelocity3D() const
    {
        auto* w = m_Entity.GetScene()->GetPhysicsWorld3D();
        uint32_t id = BodyID3D(m_Entity);
        return (w && id != 0xffffffff) ? w->GetAngularVelocity(id) : glm::vec3(0.0f);
    }

    void ScriptableEntity::SetAngularVelocity3D(const glm::vec3& v)
    {
        auto* w = m_Entity.GetScene()->GetPhysicsWorld3D();
        if (uint32_t id = BodyID3D(m_Entity); w && id != 0xffffffff) w->SetAngularVelocity(id, v);
    }

    void ScriptableEntity::ApplyImpulse3D(const glm::vec3& i)
    {
        auto* w = m_Entity.GetScene()->GetPhysicsWorld3D();
        if (uint32_t id = BodyID3D(m_Entity); w && id != 0xffffffff) w->AddImpulse(id, i);
    }

    void ScriptableEntity::ApplyForce3D(const glm::vec3& f)
    {
        auto* w = m_Entity.GetScene()->GetPhysicsWorld3D();
        if (uint32_t id = BodyID3D(m_Entity); w && id != 0xffffffff) w->AddForce(id, f);
    }

    void ScriptableEntity::ApplyTorque3D(const glm::vec3& t)
    {
        auto* w = m_Entity.GetScene()->GetPhysicsWorld3D();
        if (uint32_t id = BodyID3D(m_Entity); w && id != 0xffffffff) w->AddTorque(id, t);
    }

    void ScriptableEntity::SetPosition3D(const glm::vec3& p)
    {
        Transform().Translation = p;
        auto* w = m_Entity.GetScene()->GetPhysicsWorld3D();
        if (uint32_t id = BodyID3D(m_Entity); w && id != 0xffffffff)
        {
            w->SetTransform(id, GetWorldPosition(), glm::quat(Transform().Rotation));
            w->SetLinearVelocity(id, glm::vec3(0.0f));
        }
    }

    static b2Body* BodyOf(const Entity& e);

    bool ScriptableEntity::IsSleeping3D() const { return m_Entity.GetScene()->IsSleeping3D(m_Entity); }
    void ScriptableEntity::WakeUp3D() { m_Entity.GetScene()->SetAwake3D(m_Entity, true); }
    void ScriptableEntity::PutToSleep3D() { m_Entity.GetScene()->SetAwake3D(m_Entity, false); }
    bool ScriptableEntity::IsSleeping3D(Entity other) const { return other && m_Entity.GetScene()->IsSleeping3D(other); }

    void ScriptableEntity::WakeUp3D(Entity other)
    {
        if (other)
            m_Entity.GetScene()->SetAwake3D(other, true);
    }

    void ScriptableEntity::PutToSleep3D(Entity other)
    {
        if (other)
            m_Entity.GetScene()->SetAwake3D(other, false);
    }

    void ScriptableEntity::SetVelocity3D(Entity other, const glm::vec3& v)
    {
        auto* w = m_Entity.GetScene()->GetPhysicsWorld3D();
        if (uint32_t id = BodyID3D(other); w && id != 0xffffffff) w->SetLinearVelocity(id, v);
    }

    void ScriptableEntity::ApplyImpulse3D(Entity other, const glm::vec3& i)
    {
        auto* w = m_Entity.GetScene()->GetPhysicsWorld3D();
        if (uint32_t id = BodyID3D(other); w && id != 0xffffffff) w->AddImpulse(id, i);
    }

    void ScriptableEntity::SetVelocity(Entity other, const glm::vec2& v)
    {
        if (b2Body* body = BodyOf(other)) body->SetLinearVelocity({ v.x, v.y });
    }

    void ScriptableEntity::ApplyImpulse(Entity other, const glm::vec2& i)
    {
        if (b2Body* body = BodyOf(other)) body->ApplyLinearImpulseToCenter({ i.x, i.y }, true);
    }

    RaycastHit ScriptableEntity::Raycast2D(const glm::vec2& origin, const glm::vec2& direction, float maxDistance, uint16_t layerMask) const
    {
        return m_Entity.GetScene()->Raycast2D(origin, direction, maxDistance, layerMask);
    }

    std::vector<RaycastHit> ScriptableEntity::Raycast2DAll(const glm::vec2& origin, const glm::vec2& direction, float maxDistance, uint16_t layerMask,
                                                           bool includeTriggers) const
    {
        return m_Entity.GetScene()->Raycast2DAll(origin, direction, maxDistance, layerMask, includeTriggers);
    }

    std::vector<Entity> ScriptableEntity::OverlapCircle2D(const glm::vec2& center, float radius, uint16_t layerMask, bool includeTriggers) const
    {
        return m_Entity.GetScene()->OverlapCircle2D(center, radius, layerMask, includeTriggers);
    }

    std::vector<Entity> ScriptableEntity::OverlapBox2D(const glm::vec2& center, const glm::vec2& halfExtents, float angleDegrees, uint16_t layerMask,
                                                       bool includeTriggers) const
    {
        return m_Entity.GetScene()->OverlapBox2D(center, halfExtents, angleDegrees, layerMask, includeTriggers);
    }

    std::vector<Entity> ScriptableEntity::OverlapPoint2D(const glm::vec2& point, uint16_t layerMask, bool includeTriggers) const
    {
        return m_Entity.GetScene()->OverlapPoint2D(point, layerMask, includeTriggers);
    }

    uint16_t ScriptableEntity::LayerMask(std::initializer_list<std::string_view> layerNames)
    {
        return Project::GetCollisionLayers2D().MakeMask(layerNames);
    }

    int ScriptableEntity::LayerIndex(std::string_view layerName)
    {
        return Project::GetCollisionLayers2D().Find(layerName);
    }

    RaycastHit ScriptableEntity::Raycast3D(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, uint16_t layerMask) const
    {
        return m_Entity.GetScene()->Raycast3D(origin, direction, maxDistance, layerMask);
    }

    std::vector<RaycastHit> ScriptableEntity::Raycast3DAll(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, uint16_t layerMask,
                                                           bool includeTriggers) const
    {
        return m_Entity.GetScene()->Raycast3DAll(origin, direction, maxDistance, layerMask, includeTriggers);
    }

    std::vector<Entity> ScriptableEntity::OverlapSphere3D(const glm::vec3& center, float radius, uint16_t layerMask, bool includeTriggers) const
    {
        return m_Entity.GetScene()->OverlapSphere3D(center, radius, layerMask, includeTriggers);
    }

    std::vector<Entity> ScriptableEntity::OverlapBox3D(const glm::vec3& center, const glm::vec3& halfExtents, const glm::quat& rotation,
                                                       uint16_t layerMask, bool includeTriggers) const
    {
        return m_Entity.GetScene()->OverlapBox3D(center, halfExtents, rotation, layerMask, includeTriggers);
    }

    std::vector<Entity> ScriptableEntity::OverlapCapsule3D(const glm::vec3& point0, const glm::vec3& point1, float radius, uint16_t layerMask,
                                                           bool includeTriggers) const
    {
        return m_Entity.GetScene()->OverlapCapsule3D(point0, point1, radius, layerMask, includeTriggers);
    }

    RaycastHit ScriptableEntity::SphereCast3D(const glm::vec3& origin, float radius, const glm::vec3& direction, float maxDistance,
                                              uint16_t layerMask, bool includeTriggers) const
    {
        return m_Entity.GetScene()->SphereCast3D(origin, radius, direction, maxDistance, layerMask, includeTriggers);
    }

    std::vector<RaycastHit> ScriptableEntity::SphereCast3DAll(const glm::vec3& origin, float radius, const glm::vec3& direction, float maxDistance,
                                                              uint16_t layerMask, bool includeTriggers) const
    {
        return m_Entity.GetScene()->SphereCast3DAll(origin, radius, direction, maxDistance, layerMask, includeTriggers);
    }

    RaycastHit ScriptableEntity::BoxCast3D(const glm::vec3& center, const glm::vec3& halfExtents, const glm::vec3& direction,
                                           const glm::quat& rotation, float maxDistance, uint16_t layerMask, bool includeTriggers) const
    {
        return m_Entity.GetScene()->BoxCast3D(center, halfExtents, direction, rotation, maxDistance, layerMask, includeTriggers);
    }

    std::vector<RaycastHit> ScriptableEntity::BoxCast3DAll(const glm::vec3& center, const glm::vec3& halfExtents, const glm::vec3& direction,
                                                           const glm::quat& rotation, float maxDistance, uint16_t layerMask,
                                                           bool includeTriggers) const
    {
        return m_Entity.GetScene()->BoxCast3DAll(center, halfExtents, direction, rotation, maxDistance, layerMask, includeTriggers);
    }

    RaycastHit ScriptableEntity::CapsuleCast3D(const glm::vec3& point0, const glm::vec3& point1, float radius, const glm::vec3& direction,
                                               float maxDistance, uint16_t layerMask, bool includeTriggers) const
    {
        return m_Entity.GetScene()->CapsuleCast3D(point0, point1, radius, direction, maxDistance, layerMask, includeTriggers);
    }

    std::vector<RaycastHit> ScriptableEntity::CapsuleCast3DAll(const glm::vec3& point0, const glm::vec3& point1, float radius,
                                                               const glm::vec3& direction, float maxDistance, uint16_t layerMask,
                                                               bool includeTriggers) const
    {
        return m_Entity.GetScene()->CapsuleCast3DAll(point0, point1, radius, direction, maxDistance, layerMask, includeTriggers);
    }

    glm::vec2 ScriptableEntity::GetMousePosition() const { return m_Entity.GetScene()->GetViewportMouse(); }
    glm::vec2 ScriptableEntity::GetViewportSize() const { return m_Entity.GetScene()->GetViewportSize(); }
    glm::vec2 ScriptableEntity::GetMouseWorldPosition() const { return glm::vec2(ScreenToWorld(GetMousePosition(), 0.0f)); }
    glm::vec3 ScriptableEntity::ScreenToWorld(const glm::vec2& screen, float depthZ) const { return m_Entity.GetScene()->ScreenToWorld(screen, depthZ); }

    bool ScriptableEntity::ShakeCamera(float intensity, float duration, float frequency)
    {
        return m_Entity.GetScene()->ShakeCamera(intensity, duration, frequency);
    }

    void ScriptableEntity::StopCameraShake() { m_Entity.GetScene()->StopCameraShake(); }

    bool ScriptableEntity::DropThroughPlatforms(float seconds) { return m_Entity.GetScene()->DropThroughPlatforms(m_Entity, seconds); }

    bool ScriptableEntity::SetCameraTarget(Entity target, bool snap)
    {
        Entity camera = m_Entity.GetScene()->GetPrimaryCameraEntity();
        if (!camera)
            return false;
        auto& follow = camera.HasComponent<Camera2DFollowComponent>() ? camera.GetComponent<Camera2DFollowComponent>()
                                                                        : camera.AddComponent<Camera2DFollowComponent>();
        follow.Target.ID = target && target.IsValid() ? (uint64_t)target.GetUUID() : 0;
        if (snap)
            follow.Snap();
        return true;
    }
    void ScriptableEntity::EmitParticles(int count) { m_Entity.GetScene()->EmitParticles(m_Entity, count); }

    bool ScriptableEntity::Flash(const glm::vec4& color, float duration)
    {
        Entity entity = m_Entity;
        auto* sprite = entity.TryGetComponent<SpriteRendererComponent>();
        if (!sprite)
            return false;
        sprite->Flash(color, duration);
        return true;
    }

    bool ScriptableEntity::SetOutline(const glm::vec4& color, float thickness)
    {
        Entity entity = m_Entity;
        auto* sprite = entity.TryGetComponent<SpriteRendererComponent>();
        if (!sprite)
            return false;
        sprite->OutlineColor = color;
        sprite->OutlineThickness = std::clamp(thickness, 0.0f, 64.0f);
        return true;
    }

    // The colour of the entity's 2D renderer (the first it has of these).
    static glm::vec4* RendererColor(Entity entity)
    {
        if (!entity.IsValid()) return nullptr;
        if (auto* c = entity.TryGetComponent<SpriteRendererComponent>()) return &c->Color;
        if (auto* c = entity.TryGetComponent<CircleRendererComponent>()) return &c->Color;
        if (auto* c = entity.TryGetComponent<TextComponent>()) return &c->Color;
        if (auto* c = entity.TryGetComponent<TilemapComponent>()) return &c->Color;
        return nullptr;
    }

    glm::vec4 ScriptableEntity::GetColor() const
    {
        const glm::vec4* color = RendererColor(m_Entity);
        return color ? *color : glm::vec4(1.0f);
    }

    bool ScriptableEntity::SetColor(const glm::vec4& color)
    {
        glm::vec4* c = RendererColor(m_Entity);
        if (!c) return false;
        *c = color;
        return true;
    }

    bool ScriptableEntity::TweenColor(const glm::vec4& target, float duration, Ease ease, std::function<void()> onComplete)
    {
        const glm::vec4* color = RendererColor(m_Entity);
        if (!color)
            return false;
        const glm::vec4 start = *color;
        Entity e = m_Entity;
        Tween(duration, [e, start, target](float t) mutable {
            if (glm::vec4* c = RendererColor(e)) *c = glm::mix(start, target, t);
        }, ease, std::move(onComplete));
        return true;
    }

    bool ScriptableEntity::FadeTo(float alpha, float duration, Ease ease, std::function<void()> onComplete)
    {
        const glm::vec4* color = RendererColor(m_Entity);
        if (!color)
            return false;
        const float start = color->a;
        Entity e = m_Entity;
        Tween(duration, [e, start, alpha](float t) mutable {
            if (glm::vec4* c = RendererColor(e)) c->a = start + (alpha - start) * t;
        }, ease, std::move(onComplete));
        return true;
    }

    uint64_t ScriptableEntity::TweenUI(UITweenProperty property, const glm::vec4& to, float duration, UITweenOptions options)
    {
        Scene* scene = m_Entity.GetScene();
        return scene && m_Entity.IsValid() ? scene->TweenUI(m_Entity, property, to, duration, std::move(options)) : 0;
    }

    uint64_t ScriptableEntity::UIScaleTo(const glm::vec2& scale, float duration, Ease ease, std::function<void()> onComplete)
    {
        UITweenOptions options;
        options.Easing = ease;
        options.OnComplete = std::move(onComplete);
        return TweenUI(UITweenProperty::Scale, glm::vec4(scale, 0.0f, 0.0f), duration, std::move(options));
    }

    uint64_t ScriptableEntity::UIFadeTo(float alpha, float duration, Ease ease, std::function<void()> onComplete)
    {
        UITweenOptions options;
        options.Easing = ease;
        options.OnComplete = std::move(onComplete);
        return TweenUI(UITweenProperty::Alpha, glm::vec4(alpha, 0.0f, 0.0f, 0.0f), duration, std::move(options));
    }

    uint64_t ScriptableEntity::UIMoveTo(const glm::vec2& offset, float duration, Ease ease, std::function<void()> onComplete)
    {
        UITweenOptions options;
        options.Easing = ease;
        options.OnComplete = std::move(onComplete);
        return TweenUI(UITweenProperty::Offset, glm::vec4(offset, 0.0f, 0.0f), duration, std::move(options));
    }

    void ScriptableEntity::UIPunchScale(float amount, float duration)
    {
        if (Scene* scene = m_Entity.GetScene(); scene && m_Entity.IsValid())
            scene->PunchUIScale(m_Entity, amount, duration);
    }

    void ScriptableEntity::CancelUITweens(bool complete)
    {
        if (Scene* scene = m_Entity.GetScene(); scene && m_Entity.IsValid())
            scene->CancelUITweens(m_Entity, complete);
    }

    void ScriptableEntity::ShowUI(Entity panel)
    {
        if (Scene* scene = m_Entity.GetScene(); scene && panel.IsValid() && panel.GetScene() == scene)
            scene->ShowUI(panel);
    }

    void ScriptableEntity::HideUI(Entity panel, bool animate)
    {
        if (Scene* scene = m_Entity.GetScene(); scene && panel.IsValid() && panel.GetScene() == scene)
            scene->HideUI(panel, animate);
    }

    bool ScriptableEntity::PlayTint(bool restart)
    {
        Entity entity = m_Entity;
        auto* tint = entity.TryGetComponent<TintAnimationComponent>();
        if (!tint)
            return false;
        tint->Play(restart);
        return true;
    }

    void ScriptableEntity::PlayTint(const glm::vec4& color, float duration, TintPlayMode mode, int cycles, Ease ease)
    {
        Entity entity = m_Entity;
        auto& tint = entity.HasComponent<TintAnimationComponent>() ? entity.GetComponent<TintAnimationComponent>()
                                                                   : entity.AddComponent<TintAnimationComponent>();
        tint.Keys = { TintKey{ 0.0f, glm::vec4(1.0f) }, TintKey{ 1.0f, color } };
        tint.Duration = std::max(0.0f, duration);
        tint.Mode = mode;
        tint.Cycles = std::max(0, cycles);
        tint.Easing = ease;
        tint.StartOffset = 0.0f;
        tint.Play(true);
    }

    bool ScriptableEntity::StopTint(bool reset)
    {
        Entity entity = m_Entity;
        auto* tint = entity.TryGetComponent<TintAnimationComponent>();
        if (!tint)
            return false;
        if (reset) tint->Reset();
        else tint->Stop();
        return true;
    }

    bool ScriptableEntity::IsTintPlaying() const
    {
        Entity entity = m_Entity;
        auto* tint = entity.TryGetComponent<TintAnimationComponent>();
        return tint && tint->Playing;
    }

    const SpriteSheet* ScriptableEntity::GetSpriteSheet() const
    {
        Entity entity = m_Entity;
        const auto* sprite = entity.TryGetComponent<SpriteRendererComponent>();
        return sprite ? SpriteSheet::Get(sprite->TexturePath) : nullptr;
    }

    bool ScriptableEntity::SetSprite(const std::string& frameName)
    {
        auto* sprite = m_Entity.TryGetComponent<SpriteRendererComponent>();
        if (!sprite)
            return false;
        sprite->SpriteName = frameName;
        if (frameName.empty())
            return true;
        const SpriteSheet* sheet = SpriteSheet::Get(sprite->TexturePath);
        return sheet && sheet->Find(frameName);
    }

    int ScriptableEntity::PlaySpriteSequence(const std::string& prefix, float fps, bool loop)
    {
        const SpriteSheet* sheet = GetSpriteSheet();
        std::vector<std::string> frames = sheet ? sheet->FindSequence(prefix) : std::vector<std::string>();
        if (frames.empty())
            return 0;
        auto& anim = m_Entity.HasComponent<SpriteAnimationComponent>() ? m_Entity.GetComponent<SpriteAnimationComponent>()
                                                                       : m_Entity.AddComponent<SpriteAnimationComponent>();
        anim.PlayFrames(frames, fps, loop);
        return (int)frames.size();
    }

    bool ScriptableEntity::PlaySpriteClip(const std::string& clip, bool restart)
    {
        auto* anim = m_Entity.TryGetComponent<SpriteAnimationComponent>();
        return anim && anim->PlayClip(clip, restart);
    }

    std::string ScriptableEntity::GetSpriteClip() const
    {
        const auto* anim = const_cast<Entity&>(m_Entity).TryGetComponent<SpriteAnimationComponent>();
        return anim ? anim->GetCurrentClipName() : std::string();
    }

    void ScriptableEntity::LoadScene(const std::string& scenePath)
    {
        LoadScene(scenePath, LoadSceneMode::Single);
    }

    void ScriptableEntity::LoadScene(const std::string& scenePath, const SceneTransition& transition)
    {
        LoadScene(scenePath, LoadSceneMode::Single, transition);
    }

    void ScriptableEntity::LoadScene(const std::string& scenePath, LoadSceneMode mode, const SceneTransition& transition)
    {
        if (SceneManager* manager = GetSceneManager())
            manager->LoadScene(scenePath, mode, transition);
        else if (mode == LoadSceneMode::Single)
            m_Entity.GetScene()->RequestLoadScene(scenePath); // a scene on its own: the host switches
        else
            ZM_CORE_WARN("'{}': qo'shimcha sahna yuklash uchun SceneManager kerak (o'yin rejimi / player)", scenePath);
    }

    bool ScriptableEntity::LoadScene(int buildIndex, LoadSceneMode mode, const SceneTransition& transition)
    {
        if (SceneManager* manager = GetSceneManager())
            return manager->LoadScene(buildIndex, mode, transition);
        std::filesystem::path path = Project::GetBuildScenePath(buildIndex);
        if (path.empty())
        {
            ZM_CORE_ERROR("Build ro'yxatida {} indeksli sahna yo'q", buildIndex);
            return false;
        }
        LoadScene(path.generic_string(), mode, transition);
        return true;
    }

    Ref<AsyncOperation> ScriptableEntity::LoadSceneAsync(const std::string& scenePath, LoadSceneMode mode, const SceneTransition& transition)
    {
        if (SceneManager* manager = GetSceneManager())
            return manager->LoadSceneAsync(scenePath, mode, transition);
        ZM_CORE_WARN("'{}': fonda yuklash uchun SceneManager kerak (o'yin rejimi / player)", scenePath);
        return nullptr;
    }

    Ref<AsyncOperation> ScriptableEntity::LoadSceneAsync(int buildIndex, LoadSceneMode mode, const SceneTransition& transition)
    {
        if (SceneManager* manager = GetSceneManager())
            return manager->LoadSceneAsync(buildIndex, mode, transition);
        ZM_CORE_WARN("{}-build sahna: fonda yuklash uchun SceneManager kerak (o'yin rejimi / player)", buildIndex);
        return nullptr;
    }

    bool ScriptableEntity::LoadNextScene(const SceneTransition& transition)
    {
        if (SceneManager* manager = GetSceneManager())
            return manager->LoadNextScene(transition);
        const int current = GetSceneBuildIndex();
        if (current < 0 || current + 1 >= GetBuildSceneCount())
            return false;
        return LoadScene(current + 1, LoadSceneMode::Single, transition);
    }

    bool ScriptableEntity::ReloadScene(const SceneTransition& transition)
    {
        if (SceneManager* manager = GetSceneManager())
            return manager->ReloadActiveScene(transition);
        ZM_CORE_WARN("ReloadScene: sahna SceneManager'siz ishlayapti (o'yin rejimi / player kerak)");
        return false;
    }

    int ScriptableEntity::GetSceneBuildIndex() const
    {
        if (SceneManager* manager = GetSceneManager())
            return manager->GetBuildIndex(m_Entity.GetScene());
        return -1;
    }

    int ScriptableEntity::GetActiveSceneBuildIndex() const
    {
        SceneManager* manager = GetSceneManager();
        return manager ? manager->GetActiveSceneBuildIndex() : -1;
    }

    int ScriptableEntity::GetBuildSceneCount() const { return SceneManager::GetBuildSceneCount(); }

    bool ScriptableEntity::UnloadScene(const std::string& nameOrPath)
    {
        SceneManager* manager = GetSceneManager();
        return manager && manager->UnloadScene(nameOrPath);
    }

    bool ScriptableEntity::LoadSubScene() { return LoadSubScene(m_Entity); }
    bool ScriptableEntity::UnloadSubScene() { return UnloadSubScene(m_Entity); }
    Ref<Scene> ScriptableEntity::GetSubScene() const { return GetSubScene(m_Entity); }

    bool ScriptableEntity::LoadSubScene(Entity host)
    {
        SceneManager* manager = host.GetScene() ? host.GetScene()->GetSceneManager() : nullptr;
        if (!manager)
        {
            ZM_CORE_WARN("LoadSubScene('{}'): sahna SceneManager'siz ishlayapti - sub-sahna yuklanmadi",
                         host.IsValid() ? host.GetName() : std::string("?"));
            return false;
        }
        return manager->LoadSubScene(host);
    }

    bool ScriptableEntity::UnloadSubScene(Entity host)
    {
        SceneManager* manager = host.GetScene() ? host.GetScene()->GetSceneManager() : nullptr;
        return manager && manager->UnloadSubScene(host);
    }

    Ref<Scene> ScriptableEntity::GetSubScene(Entity host) const
    {
        SceneManager* manager = host.GetScene() ? host.GetScene()->GetSceneManager() : nullptr;
        return manager ? manager->GetSubScene(host) : nullptr;
    }

    void ScriptableEntity::DontDestroyOnLoad() { DontDestroyOnLoad(m_Entity); }

    void ScriptableEntity::DontDestroyOnLoad(Entity entity)
    {
        if (SceneManager* manager = entity.GetScene() ? entity.GetScene()->GetSceneManager() : nullptr)
            manager->DontDestroyOnLoad(entity);
        else
            ZM_CORE_WARN("DontDestroyOnLoad('{}'): sahna SceneManager'siz ishlayapti - hech narsa qilinmadi",
                         entity.IsValid() ? entity.GetName() : std::string("?"));
    }

    void ScriptableEntity::Destroy(Entity entity) { m_Entity.GetScene()->DestroyEntityDeferred(entity); }

    static b2Body* BodyOf(const Entity& e)
    {
        auto* rb = const_cast<Entity&>(e).TryGetComponent<Rigidbody2DComponent>();
        return rb ? (b2Body*)rb->RuntimeBody : nullptr;
    }

    glm::vec2 ScriptableEntity::GetVelocity() const
    {
        if (b2Body* body = BodyOf(m_Entity))
        {
            const b2Vec2& v = body->GetLinearVelocity();
            return { v.x, v.y };
        }
        return { 0.0f, 0.0f };
    }

    void ScriptableEntity::SetVelocity(const glm::vec2& velocity)
    {
        if (b2Body* body = BodyOf(m_Entity))
            body->SetLinearVelocity({ velocity.x, velocity.y });
    }

    void ScriptableEntity::ApplyImpulse(const glm::vec2& impulse)
    {
        if (b2Body* body = BodyOf(m_Entity))
            body->ApplyLinearImpulseToCenter({ impulse.x, impulse.y }, true);
    }

    void ScriptableEntity::ApplyForce(const glm::vec2& force)
    {
        // Kept by the scene for every step / sub-step of the frame (Box2D clears forces after each).
        m_Entity.GetScene()->ApplyForce2D(m_Entity, force);
    }

    bool ScriptableEntity::IsSleeping() const { return m_Entity.GetScene()->IsSleeping2D(m_Entity); }
    void ScriptableEntity::WakeUp() { m_Entity.GetScene()->SetAwake2D(m_Entity, true); }
    void ScriptableEntity::PutToSleep() { m_Entity.GetScene()->SetAwake2D(m_Entity, false); }
    bool ScriptableEntity::IsSleeping(Entity other) const { return other && m_Entity.GetScene()->IsSleeping2D(other); }
    void ScriptableEntity::WakeUp(Entity other)
    {
        if (other)
            m_Entity.GetScene()->SetAwake2D(other, true);
    }

    void ScriptableEntity::SetPosition2D(const glm::vec2& position)
    {
        auto& t = Transform();
        t.Translation.x = position.x;
        t.Translation.y = position.y;
        if (b2Body* body = BodyOf(m_Entity))
        {
            body->SetTransform({ position.x, position.y }, body->GetAngle());
            // Box2D does not wake a body it teleports: a sleeping one would hang where it was put.
            m_Entity.GetScene()->SetAwake2D(m_Entity, true);
        }
    }

    uint32_t ScriptableEntity::PlaySound(const std::string& assetPath, float volume, float pitch, const std::string& group)
    {
        uint32_t handle = Audio::PlaySound(assetPath, volume, pitch, group);
        m_Entity.GetScene()->TrackSound(handle, m_Entity);
        return handle;
    }

    uint32_t ScriptableEntity::PlaySoundAt(const std::string& assetPath, const glm::vec3& position, float volume, float pitch,
                                           const std::string& group)
    {
        uint32_t handle = Audio::PlaySoundAt(assetPath, position, volume, pitch, group);
        m_Entity.GetScene()->TrackSound(handle, m_Entity);
        return handle;
    }

    uint32_t ScriptableEntity::PlaySound(const std::string& assetPath, const SoundParams& params)
    {
        uint32_t handle = Audio::PlaySound(assetPath, params);
        m_Entity.GetScene()->TrackSound(handle, m_Entity);
        return handle;
    }

    bool ScriptableEntity::PlayAudio() { return m_Entity.GetScene()->PlayAudio(m_Entity); }
    void ScriptableEntity::StopAudio() { m_Entity.GetScene()->StopAudio(m_Entity); }
    bool ScriptableEntity::PauseAudio() { return m_Entity.GetScene()->PauseAudio(m_Entity); }
    bool ScriptableEntity::ResumeAudio() { return m_Entity.GetScene()->ResumeAudio(m_Entity); }
    bool ScriptableEntity::IsAudioPlaying() const { return m_Entity.GetScene()->IsAudioPlaying(m_Entity); }
    bool ScriptableEntity::IsAudioPaused() const { return m_Entity.GetScene()->IsAudioPaused(m_Entity); }
    void ScriptableEntity::SetAudioVolume(float volume) { m_Entity.GetScene()->SetAudioVolume(m_Entity, volume); }
    void ScriptableEntity::SetAudioPitch(float pitch) { m_Entity.GetScene()->SetAudioPitch(m_Entity, pitch); }
    float ScriptableEntity::GetAudioTime() const { return m_Entity.GetScene()->GetAudioTime(m_Entity); }
    bool ScriptableEntity::SetAudioTime(float seconds) { return m_Entity.GetScene()->SetAudioTime(m_Entity, seconds); }
    float ScriptableEntity::GetAudioLength() const { return m_Entity.GetScene()->GetAudioLength(m_Entity); }

    float ScriptableEntity::GetAudioVolume() const
    {
        Entity self = m_Entity;
        const auto* audio = self.TryGetComponent<AudioSourceComponent>();
        return audio ? audio->Volume : 0.0f;
    }

    float ScriptableEntity::GetAudioPitch() const
    {
        Entity self = m_Entity;
        const auto* audio = self.TryGetComponent<AudioSourceComponent>();
        return audio ? audio->Pitch : 0.0f;
    }

    bool ScriptableEntity::PlayAudio(float fadeInSeconds) { return m_Entity.GetScene()->PlayAudio(m_Entity, std::max(0.0f, fadeInSeconds)); }
    void ScriptableEntity::StopAudio(float fadeOutSeconds) { m_Entity.GetScene()->StopAudio(m_Entity, std::max(0.0f, fadeOutSeconds)); }
    bool ScriptableEntity::FadeAudio(float volume, float seconds) { return m_Entity.GetScene()->FadeAudio(m_Entity, volume, seconds); }
    float ScriptableEntity::GetAudioFade() const { return m_Entity.GetScene()->GetAudioFade(m_Entity); }
    void ScriptableEntity::SetAudioLowPass(float cutoffHz) { m_Entity.GetScene()->SetAudioLowPass(m_Entity, cutoffHz); }
    void ScriptableEntity::SetAudioHighPass(float cutoffHz) { m_Entity.GetScene()->SetAudioHighPass(m_Entity, cutoffHz); }
    bool ScriptableEntity::PlayAudio(Entity other) { return m_Entity.GetScene()->PlayAudio(other); }
    void ScriptableEntity::StopAudio(Entity other) { m_Entity.GetScene()->StopAudio(other); }
    bool ScriptableEntity::PlayAudio(Entity other, float fadeInSeconds) { return m_Entity.GetScene()->PlayAudio(other, std::max(0.0f, fadeInSeconds)); }
    void ScriptableEntity::StopAudio(Entity other, float fadeOutSeconds) { m_Entity.GetScene()->StopAudio(other, std::max(0.0f, fadeOutSeconds)); }
    bool ScriptableEntity::FadeAudio(Entity other, float volume, float seconds) { return m_Entity.GetScene()->FadeAudio(other, volume, seconds); }
    void ScriptableEntity::SetAudioLowPass(Entity other, float cutoffHz) { m_Entity.GetScene()->SetAudioLowPass(other, cutoffHz); }
    bool ScriptableEntity::PauseAudio(Entity other) { return m_Entity.GetScene()->PauseAudio(other); }
    bool ScriptableEntity::ResumeAudio(Entity other) { return m_Entity.GetScene()->ResumeAudio(other); }
    bool ScriptableEntity::IsAudioPlaying(Entity other) const { return m_Entity.GetScene()->IsAudioPlaying(other); }
    bool ScriptableEntity::IsAudioPaused(Entity other) const { return m_Entity.GetScene()->IsAudioPaused(other); }
    void ScriptableEntity::SetAudioVolume(Entity other, float volume) { m_Entity.GetScene()->SetAudioVolume(other, volume); }
    void ScriptableEntity::SetAudioPitch(Entity other, float pitch) { m_Entity.GetScene()->SetAudioPitch(other, pitch); }
    float ScriptableEntity::GetAudioTime(Entity other) const { return m_Entity.GetScene()->GetAudioTime(other); }
    bool ScriptableEntity::SetAudioTime(Entity other, float seconds) { return m_Entity.GetScene()->SetAudioTime(other, seconds); }

    float ScriptableEntity::GetTime() const { return m_Entity.GetScene()->GetTime(); }
    // Every scene of a SceneManager runs on one clock.
    void ScriptableEntity::SetTimeScale(float scale)
    {
        if (SceneManager* manager = GetSceneManager())
            manager->SetTimeScale(scale);
        else
            m_Entity.GetScene()->SetTimeScale(scale);
    }
    float ScriptableEntity::GetTimeScale() const { return m_Entity.GetScene()->GetTimeScale(); }
    void ScriptableEntity::PauseGame()
    {
        if (SceneManager* manager = GetSceneManager())
            manager->SetGamePaused(true);
        else
            m_Entity.GetScene()->SetGamePaused(true);
    }
    void ScriptableEntity::ResumeGame()
    {
        if (SceneManager* manager = GetSceneManager())
            manager->SetGamePaused(false);
        else
            m_Entity.GetScene()->SetGamePaused(false);
    }
    bool ScriptableEntity::IsGamePaused() const { return m_Entity.GetScene()->IsGamePaused(); }
    float ScriptableEntity::GetUnscaledDeltaTime() const { return m_Entity.GetScene()->GetUnscaledDeltaTime(); }
    void ScriptableEntity::QuitGame() { m_Entity.GetScene()->RequestQuit(); }

    void ScriptableEntity::Invoke(float delay, std::function<void()> callback)
    {
        if (!callback) return;
        (m_TickingTimers ? m_PendingTimers : m_Timers).push_back({ delay, 0.0f, false, false, std::move(callback) });
    }

    void ScriptableEntity::InvokeRepeating(float delay, float interval, std::function<void()> callback)
    {
        if (!callback) return;
        (m_TickingTimers ? m_PendingTimers : m_Timers).push_back({ delay, std::max(0.001f, interval), true, false, std::move(callback) });
    }

    void ScriptableEntity::CancelInvokes()
    {
        m_PendingTimers.clear();
        if (!m_TickingTimers)
        {
            m_Timers.clear();
            return;
        }
        // A callback may be running right now: only flag, erase after the loop.
        for (TimerState& t : m_Timers)
            t.Done = true;
    }

    bool ScriptableEntity::IsInvoking() const
    {
        if (!m_PendingTimers.empty()) return true;
        return std::any_of(m_Timers.begin(), m_Timers.end(), [](const TimerState& t) { return !t.Done; });
    }

    void ScriptableEntity::Tween(float duration, std::function<void(float)> onUpdate, Ease ease, std::function<void()> onComplete)
    {
        if (!onUpdate && !onComplete) return;
        TweenState tw{ 0.0f, std::max(0.0001f, duration), ease, false, std::move(onUpdate), std::move(onComplete) };
        (m_TickingTimers ? m_PendingTweens : m_Tweens).push_back(std::move(tw));
    }

    void ScriptableEntity::TweenPosition(const glm::vec3& target, float duration, Ease ease, std::function<void()> onComplete)
    {
        glm::vec3 start = Transform().Translation;
        Entity e = m_Entity;
        Tween(duration, [e, start, target](float t) mutable {
            if (e.IsValid()) e.GetComponent<TransformComponent>().Translation = glm::mix(start, target, t);
        }, ease, std::move(onComplete));
    }

    void ScriptableEntity::TweenScale(const glm::vec3& target, float duration, Ease ease, std::function<void()> onComplete)
    {
        glm::vec3 start = Transform().Scale;
        Entity e = m_Entity;
        Tween(duration, [e, start, target](float t) mutable {
            if (e.IsValid()) e.GetComponent<TransformComponent>().Scale = glm::mix(start, target, t);
        }, ease, std::move(onComplete));
    }

    void ScriptableEntity::CancelTweens()
    {
        m_PendingTweens.clear();
        if (!m_TickingTimers)
        {
            m_Tweens.clear();
            return;
        }
        for (TweenState& tw : m_Tweens)
            tw.Done = true;
    }

    bool ScriptableEntity::IsTweening() const
    {
        if (!m_PendingTweens.empty()) return true;
        return std::any_of(m_Tweens.begin(), m_Tweens.end(), [](const TweenState& t) { return !t.Done; });
    }

    void ScriptableEntity::TickTimers(float ts)
    {
        if (m_TickingTimers) return; // no recursive ticking
        m_TickingTimers = true;

        // The vectors do not grow while ticking (new entries go to the pending lists),
        // so references stay valid even when a callback schedules or cancels timers.
        for (size_t i = 0; i < m_Timers.size(); i++)
        {
            TimerState& t = m_Timers[i];
            if (t.Done) continue;
            t.Remaining -= ts;
            if (t.Remaining > 0.0f) continue;
            if (t.Repeat) t.Remaining += t.Interval;
            else t.Done = true;
            t.Callback();
        }

        for (size_t i = 0; i < m_Tweens.size(); i++)
        {
            TweenState& tw = m_Tweens[i];
            if (tw.Done) continue;
            tw.Elapsed += ts;
            float k = std::min(1.0f, tw.Elapsed / tw.Duration);
            if (tw.OnUpdate) tw.OnUpdate(ApplyEase(tw.Easing, k));
            if (tw.Done || k < 1.0f) continue; // cancelled inside the callback, or still running
            tw.Done = true;
            if (tw.OnComplete) tw.OnComplete();
        }

        m_TickingTimers = false;
        std::erase_if(m_Timers, [](const TimerState& t) { return t.Done; });
        std::erase_if(m_Tweens, [](const TweenState& t) { return t.Done; });
        for (TimerState& t : m_PendingTimers) m_Timers.push_back(std::move(t));
        for (TweenState& t : m_PendingTweens) m_Tweens.push_back(std::move(t));
        m_PendingTimers.clear();
        m_PendingTweens.clear();
    }

    Entity ScriptableEntity::Resolve(const EntityRef& ref) const
    {
        if (!ref.ID)
            return {};
        Scene* scene = m_Entity.GetScene();
        if (Entity e = scene->GetEntityByUUID(ref.ID))
            return e;
        SceneManager* manager = scene->GetSceneManager();
        return manager ? manager->FindEntityByUUID(ref.ID) : Entity();
    }

    // ------------------------------------------------------------------ messages
    void ScriptableEntity::AddMessageHandler(std::string name, MessageHandler handler)
    {
        if (name.empty() || !handler) return;
        m_MessageHandlers.push_back({ std::move(name), std::move(handler) });
    }

    void ScriptableEntity::RemoveMessageHandlers(std::string_view name)
    {
        std::erase_if(m_MessageHandlers, [&](const NamedHandler& h) { return h.Name == name; });
    }

    void ScriptableEntity::ReceiveMessage(const ScriptMessage& message)
    {
        // Copy the matching handlers: a handler may add / remove handlers while running.
        std::vector<MessageHandler> handlers;
        for (const NamedHandler& h : m_MessageHandlers)
            if (h.Name == message.Name)
                handlers.push_back(h.Handler);
        for (const MessageHandler& handler : handlers)
            handler(message);
        OnMessage(message);
    }

    void ScriptableEntity::AddAnimationEventHandler(std::string name, AnimationEventHandler handler)
    {
        if (name.empty() || !handler) return;
        m_AnimationHandlers.push_back({ std::move(name), std::move(handler) });
    }

    void ScriptableEntity::RemoveAnimationEventHandlers(std::string_view name)
    {
        std::erase_if(m_AnimationHandlers, [&](const NamedAnimationHandler& h) { return h.Name == name; });
    }

    void ScriptableEntity::ReceiveAnimationEvent(const AnimationEventInfo& event)
    {
        std::vector<AnimationEventHandler> handlers;
        for (const NamedAnimationHandler& h : m_AnimationHandlers)
            if (h.Name == event.Name)
                handlers.push_back(h.Handler);
        for (const AnimationEventHandler& handler : handlers)
            handler(event);
        OnAnimationEvent(event);
    }

    bool ScriptableEntity::PlayAnimation(std::string_view clip, float speed)
    {
        auto* player = m_Entity ? m_Entity.TryGetComponent<AnimationPlayerComponent>() : nullptr;
        return player && player->Play(clip, speed);
    }

    void ScriptableEntity::StopAnimation()
    {
        if (auto* player = m_Entity ? m_Entity.TryGetComponent<AnimationPlayerComponent>() : nullptr)
            player->Stop();
    }

    bool ScriptableEntity::IsAnimationPlaying() const
    {
        Entity e = m_Entity;
        const auto* player = e ? e.TryGetComponent<AnimationPlayerComponent>() : nullptr;
        return player && player->IsPlaying();
    }

    bool ScriptableEntity::SendMessage(Entity target, std::string name, std::any payload)
    {
        return m_Entity.GetScene()->SendMessage(target, ScriptMessage{ std::move(name), m_Entity, std::move(payload) });
    }

    int ScriptableEntity::SendMessageToChildren(std::string name, std::any payload, bool includeSelf)
    {
        return m_Entity.GetScene()->SendMessageToChildren(m_Entity, ScriptMessage{ std::move(name), m_Entity, std::move(payload) }, includeSelf);
    }

    int ScriptableEntity::SendMessageUpwards(std::string name, std::any payload, bool includeSelf)
    {
        return m_Entity.GetScene()->SendMessageUpwards(m_Entity, ScriptMessage{ std::move(name), m_Entity, std::move(payload) }, includeSelf);
    }

    int ScriptableEntity::Broadcast(std::string name, std::any payload)
    {
        return m_Entity.GetScene()->Broadcast(ScriptMessage{ std::move(name), m_Entity, std::move(payload) });
    }

    EventBus& ScriptableEntity::GetEventBus() const { return m_Entity.GetScene()->GetEventBus(); }

    GameState::ListenerID ScriptableEntity::WatchGameState(std::string_view key, GameState::Listener handler)
    {
        // Owned by this instance: the scene removes its listeners when the script is destroyed.
        return GameState::AddListener(key, std::move(handler), (uint64_t)(uintptr_t)this);
    }

    SubscriptionID ScriptableEntity::Subscribe(std::string eventName, MessageHandler handler)
    {
        return GetEventBus().Subscribe(std::move(eventName), std::move(handler), (uint64_t)m_Entity.GetUUID());
    }

    bool ScriptableEntity::Unsubscribe(SubscriptionID id) { return GetEventBus().Unsubscribe(id); }

    int ScriptableEntity::Emit(std::string eventName, std::any payload)
    {
        return GetEventBus().Emit(std::move(eventName), std::move(payload), m_Entity);
    }

    void ScriptableEntity::EmitDeferred(std::string eventName, std::any payload)
    {
        GetEventBus().EmitDeferred(std::move(eventName), std::move(payload), m_Entity);
    }

}
