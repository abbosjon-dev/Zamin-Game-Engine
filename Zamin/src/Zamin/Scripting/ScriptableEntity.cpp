#include "Zamin/Scripting/ScriptableEntity.h"
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

    Entity ScriptableEntity::FindEntity(std::string_view name) const { return m_Entity.GetScene()->FindEntityByName(name); }
    std::vector<Entity> ScriptableEntity::FindEntities(std::string_view name) const { return m_Entity.GetScene()->FindEntitiesByName(name); }
    std::vector<Entity> ScriptableEntity::FindEntitiesWithTag(std::string_view tag) const { return m_Entity.GetScene()->FindEntitiesWithTag(tag); }
    Entity ScriptableEntity::FindEntityWithTag(std::string_view tag) const { return m_Entity.GetScene()->FindEntityWithTag(tag); }

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

    RaycastHit ScriptableEntity::Raycast2D(const glm::vec2& origin, const glm::vec2& direction, float maxDistance) const
    {
        return m_Entity.GetScene()->Raycast2D(origin, direction, maxDistance);
    }

    RaycastHit ScriptableEntity::Raycast3D(const glm::vec3& origin, const glm::vec3& direction, float maxDistance) const
    {
        return m_Entity.GetScene()->Raycast3D(origin, direction, maxDistance);
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

    void ScriptableEntity::LoadScene(const std::string& scenePath)
    {
        m_Entity.GetScene()->RequestLoadScene(scenePath);
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
        if (b2Body* body = BodyOf(m_Entity))
            body->ApplyForceToCenter({ force.x, force.y }, true);
    }

    void ScriptableEntity::SetPosition2D(const glm::vec2& position)
    {
        auto& t = Transform();
        t.Translation.x = position.x;
        t.Translation.y = position.y;
        if (b2Body* body = BodyOf(m_Entity))
            body->SetTransform({ position.x, position.y }, body->GetAngle());
    }

    void ScriptableEntity::PlaySound(const std::string& assetPath, float volume, float pitch)
    {
        AudioEngine::Play(Project::GetAssetFileSystemPath(assetPath), volume, pitch, false);
    }

    void ScriptableEntity::PlaySoundAt(const std::string& assetPath, const glm::vec3& position, float volume, float pitch)
    {
        AudioEngine::PlaySpatial(Project::GetAssetFileSystemPath(assetPath), position, volume, pitch, false);
    }

    bool ScriptableEntity::PlayAudio() { return m_Entity.GetScene()->PlayAudio(m_Entity); }
    void ScriptableEntity::StopAudio() { m_Entity.GetScene()->StopAudio(m_Entity); }
    bool ScriptableEntity::IsAudioPlaying() const { return m_Entity.GetScene()->IsAudioPlaying(m_Entity); }

    float ScriptableEntity::GetTime() const { return m_Entity.GetScene()->GetTime(); }

    float ApplyEase(Ease ease, float t)
    {
        t = std::clamp(t, 0.0f, 1.0f);
        switch (ease)
        {
            case Ease::Linear:     return t;
            case Ease::InQuad:     return t * t;
            case Ease::OutQuad:    return 1.0f - (1.0f - t) * (1.0f - t);
            case Ease::InOutQuad:  return t < 0.5f ? 2.0f * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) * 0.5f;
            case Ease::OutCubic:   return 1.0f - std::pow(1.0f - t, 3.0f);
            case Ease::InOutCubic: return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) * 0.5f;
            case Ease::OutBack:
            {
                const float c1 = 1.70158f, c3 = c1 + 1.0f;
                return 1.0f + c3 * std::pow(t - 1.0f, 3.0f) + c1 * std::pow(t - 1.0f, 2.0f);
            }
            case Ease::OutElastic:
            {
                if (t == 0.0f || t == 1.0f) return t;
                const float c4 = glm::two_pi<float>() / 3.0f;
                return std::pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * c4) + 1.0f;
            }
            case Ease::OutBounce:
            {
                const float n1 = 7.5625f, d1 = 2.75f;
                if (t < 1.0f / d1) return n1 * t * t;
                if (t < 2.0f / d1) { t -= 1.5f / d1; return n1 * t * t + 0.75f; }
                if (t < 2.5f / d1) { t -= 2.25f / d1; return n1 * t * t + 0.9375f; }
                t -= 2.625f / d1; return n1 * t * t + 0.984375f;
            }
        }
        return t;
    }

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
        for (Timer& t : m_Timers)
            t.Done = true;
    }

    bool ScriptableEntity::IsInvoking() const
    {
        if (!m_PendingTimers.empty()) return true;
        return std::any_of(m_Timers.begin(), m_Timers.end(), [](const Timer& t) { return !t.Done; });
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
            Timer& t = m_Timers[i];
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
        std::erase_if(m_Timers, [](const Timer& t) { return t.Done; });
        std::erase_if(m_Tweens, [](const TweenState& t) { return t.Done; });
        for (Timer& t : m_PendingTimers) m_Timers.push_back(std::move(t));
        for (TweenState& t : m_PendingTweens) m_Tweens.push_back(std::move(t));
        m_PendingTimers.clear();
        m_PendingTweens.clear();
    }

    Entity ScriptableEntity::Resolve(const EntityRef& ref) const
    {
        return ref.ID ? m_Entity.GetScene()->GetEntityByUUID(ref.ID) : Entity();
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
