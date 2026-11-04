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

    Entity ScriptableEntity::FindEntity(std::string_view name) const { return m_Entity.GetScene()->FindEntityByName(name); }
    std::vector<Entity> ScriptableEntity::FindEntities(std::string_view name) const { return m_Entity.GetScene()->FindEntitiesByName(name); }
    std::vector<Entity> ScriptableEntity::FindEntitiesWithTag(std::string_view tag) const { return m_Entity.GetScene()->FindEntitiesWithTag(tag); }
    Entity ScriptableEntity::FindEntityWithTag(std::string_view tag) const { return m_Entity.GetScene()->FindEntityWithTag(tag); }

    Entity ScriptableEntity::Instantiate(Entity prefab) { return m_Entity.GetScene()->Instantiate(prefab); }

    Entity ScriptableEntity::Instantiate(Entity prefab, const glm::vec3& position)
    {
        if (!prefab) return {};
        // Set the position before physics bodies are created.
        glm::vec3 old = prefab.GetComponent<TransformComponent>().Translation;
        prefab.GetComponent<TransformComponent>().Translation = position;
        Entity e = m_Entity.GetScene()->Instantiate(prefab);
        prefab.GetComponent<TransformComponent>().Translation = old;
        return e;
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
    void ScriptableEntity::EmitParticles(int count) { m_Entity.GetScene()->EmitParticles(m_Entity, count); }

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
