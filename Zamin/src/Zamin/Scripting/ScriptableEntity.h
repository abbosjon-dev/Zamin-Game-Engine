#pragma once

#include "Zamin/Scene/Entity.h"
#include "Zamin/Core/Input.h"
#include "Zamin/Core/Timestep.h"

#include <string_view>

namespace Zamin {

    // Base class for C++ gameplay scripts.
    //
    //   class Player : public Zamin::ScriptableEntity
    //   {
    //   public:
    //       float Speed = 5.0f;
    //       void OnUpdate(Zamin::Timestep ts) override { ... }
    //   };
    //   ZM_REGISTER_SCRIPT(Player, ZM_FIELD(Player, Speed))
    class ScriptableEntity
    {
    public:
        virtual ~ScriptableEntity() = default;

        virtual void OnCreate() {}
        virtual void OnDestroy() {}
        virtual void OnUpdate(Timestep) {}
        virtual void OnLateUpdate(Timestep) {}
        virtual void OnCollisionBegin(Entity /*other*/) {}
        virtual void OnCollisionEnd(Entity /*other*/) {}

        // ---- helpers available to scripts
        template<typename T> T& GetComponent() { return m_Entity.GetComponent<T>(); }
        template<typename T> T* TryGetComponent() { return m_Entity.TryGetComponent<T>(); }
        template<typename T> bool HasComponent() const { return m_Entity.HasComponent<T>(); }
        template<typename T, typename... Args> T& AddComponent(Args&&... args) { return m_Entity.AddComponent<T>(std::forward<Args>(args)...); }

        Entity GetEntity() const { return m_Entity; }
        Scene& GetScene() const { return *m_Entity.GetScene(); }
        TransformComponent& Transform() { return m_Entity.GetComponent<TransformComponent>(); }
        const std::string& GetName() const { return m_Entity.GetName(); }

        Entity FindEntity(std::string_view name) const;
        // Copy an existing entity (a "prefab") into the running scene.
        Entity Instantiate(Entity prefab, const glm::vec3& position);
        Entity Instantiate(Entity prefab);
        // Deferred: entity is removed at the end of the frame.
        void Destroy(Entity entity);
        void Destroy() { Destroy(m_Entity); }

        // ---- 2D physics (requires Rigidbody2DComponent)
        glm::vec2 GetVelocity() const;
        void SetVelocity(const glm::vec2& velocity);
        void ApplyImpulse(const glm::vec2& impulse);
        void ApplyForce(const glm::vec2& force);
        void SetPosition2D(const glm::vec2& position); // teleports the physics body too

        // ---- audio
        void PlaySound(const std::string& assetPath, float volume = 1.0f, float pitch = 1.0f);

        // ---- time
        float GetTime() const;

        // Returns the script instance of type T attached to `entity` (or nullptr).
        template<typename T>
        static T* GetScript(Entity entity)
        {
            if (!entity) return nullptr;
            auto* nsc = entity.TryGetComponent<NativeScriptComponent>();
            return nsc ? dynamic_cast<T*>(nsc->Instance) : nullptr;
        }

    private:
        Entity m_Entity;
        friend class Scene;
    };

}
