#include "Zamin/Scripting/ScriptableEntity.h"
#include "Zamin/Audio/AudioEngine.h"
#include "Zamin/Project/Project.h"

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

    float ScriptableEntity::GetTime() const { return m_Entity.GetScene()->GetTime(); }

}
