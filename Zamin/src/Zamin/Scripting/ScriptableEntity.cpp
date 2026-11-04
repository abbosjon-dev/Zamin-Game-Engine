#include "Zamin/Scripting/ScriptableEntity.h"
#include "Zamin/Audio/AudioEngine.h"
#include "Zamin/Project/Project.h"
#include "Zamin/Scene/SceneSerializer.h"
#include "Zamin/Utils/FileSystem.h"

#include "Zamin/Physics/PhysicsWorld3D.h"

#include <box2d/b2_body.h>

namespace Zamin {

    Entity ScriptableEntity::FindEntity(std::string_view name) const { return m_Entity.GetScene()->FindEntityByName(name); }

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

}
