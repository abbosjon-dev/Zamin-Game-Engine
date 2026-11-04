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
        // Called when a UIButton on this entity is clicked.
        virtual void OnClick() {}

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
        // Spawn a prefab asset (.zprefab, path relative to Assets).
        Entity Instantiate(const std::string& prefabPath, const glm::vec3& position);
        // Deferred: entity is removed at the end of the frame.
        void Destroy(Entity entity);
        void Destroy() { Destroy(m_Entity); }

        // ---- 2D physics (requires Rigidbody2DComponent)
        glm::vec2 GetVelocity() const;
        void SetVelocity(const glm::vec2& velocity);
        void ApplyImpulse(const glm::vec2& impulse);
        void ApplyForce(const glm::vec2& force);
        void SetPosition2D(const glm::vec2& position); // teleports the physics body too

        // ---- hierarchy
        Entity GetParent() const;
        void SetParent(Entity parent, bool keepWorldTransform = true);
        glm::vec3 GetWorldPosition() const;

        // ---- 3D physics (requires Rigidbody3DComponent + a 3D collider)
        glm::vec3 GetVelocity3D() const;
        void SetVelocity3D(const glm::vec3& velocity);
        glm::vec3 GetAngularVelocity3D() const;
        void SetAngularVelocity3D(const glm::vec3& velocity);
        void ApplyImpulse3D(const glm::vec3& impulse);
        void ApplyForce3D(const glm::vec3& force);
        void ApplyTorque3D(const glm::vec3& torque);
        void SetPosition3D(const glm::vec3& position); // teleports the physics body too
        // Same, for another entity (e.g. a freshly instantiated projectile)
        void SetVelocity3D(Entity other, const glm::vec3& velocity);
        void ApplyImpulse3D(Entity other, const glm::vec3& impulse);
        void SetVelocity(Entity other, const glm::vec2& velocity);

        // ---- raycasts (EntityID of the hit can be resolved with GetScene().GetEntityByUUID)
        RaycastHit Raycast2D(const glm::vec2& origin, const glm::vec2& direction, float maxDistance = 100.0f) const;
        RaycastHit Raycast3D(const glm::vec3& origin, const glm::vec3& direction, float maxDistance = 100.0f) const;

        // ---- viewport / mouse (pixels, origin top-left of the game view)
        glm::vec2 GetMousePosition() const;
        glm::vec2 GetViewportSize() const;
        // Mouse position projected onto the z = 0 plane using the primary camera (2D games).
        glm::vec2 GetMouseWorldPosition() const;
        glm::vec3 ScreenToWorld(const glm::vec2& screen, float depthZ = 0.0f) const;

        // ---- particles
        void EmitParticles(int count); // requires ParticleSystemComponent

        // ---- scenes: switch to another scene at the end of the frame (path relative to Assets)
        void LoadScene(const std::string& scenePath);

        // ---- audio
        void PlaySound(const std::string& assetPath, float volume = 1.0f, float pitch = 1.0f);
        // 3D positional sound (heard relative to the primary camera)
        void PlaySoundAt(const std::string& assetPath, const glm::vec3& position, float volume = 1.0f, float pitch = 1.0f);

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
