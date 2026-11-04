#pragma once

#include "Zamin/Scene/Entity.h"
#include "Zamin/Core/Input.h"
#include "Zamin/Core/Timestep.h"
#include "Zamin/Math/Random.h"
#include "Zamin/Scripting/EventBus.h"

#include <any>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

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
    enum class Ease { Linear = 0, InQuad, OutQuad, InOutQuad, OutCubic, InOutCubic, OutBack, OutElastic, OutBounce };
    float ApplyEase(Ease ease, float t);

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
        // Called for every message sent to this entity (SendMessage, Broadcast ...),
        // after the handlers registered with AddMessageHandler for that name.
        virtual void OnMessage(const ScriptMessage& /*message*/) {}

        // ---- helpers available to scripts
        template<typename T> T& GetComponent() { return m_Entity.GetComponent<T>(); }
        template<typename T> T* TryGetComponent() { return m_Entity.TryGetComponent<T>(); }
        template<typename T> bool HasComponent() const { return m_Entity.HasComponent<T>(); }
        template<typename T, typename... Args> T& AddComponent(Args&&... args) { return m_Entity.AddComponent<T>(std::forward<Args>(args)...); }

        Entity GetEntity() const { return m_Entity; }
        Scene& GetScene() const { return *m_Entity.GetScene(); }
        TransformComponent& Transform() { return m_Entity.GetComponent<TransformComponent>(); }
        const std::string& GetName() const { return m_Entity.GetName(); }
        void SetName(const std::string& name) { m_Entity.SetName(name); }

        // First entity with this name (cached hash lookup, cheap enough to call every frame).
        Entity FindEntity(std::string_view name) const;
        std::vector<Entity> FindEntities(std::string_view name) const;

        // ---- gameplay tags (TagsComponent)
        bool HasTag(std::string_view tag) const { return m_Entity.HasTag(tag); }
        bool AddTag(std::string_view tag) { return m_Entity.AddTag(tag); }
        bool RemoveTag(std::string_view tag) { return m_Entity.RemoveTag(tag); }
        std::vector<Entity> FindEntitiesWithTag(std::string_view tag) const;
        Entity FindEntityWithTag(std::string_view tag) const;
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
        std::vector<Entity> GetChildren() const;
        size_t GetChildCount() const;
        Entity GetChild(size_t index) const;
        // Child by name; recursive searches the whole subtree (depth-first).
        Entity FindChild(std::string_view name, bool recursive = true) const;
        // All descendants, depth-first (a parent comes before its children).
        std::vector<Entity> GetDescendants() const;
        Entity GetRoot() const;

        // Components on this entity and its children / parents. T can also be a script
        // class (derived from ScriptableEntity): then the script instance is returned.
        // includeSelf = true checks this entity first; children are searched depth-first,
        // parents nearest first.
        //   auto* light = GetComponentInChildren<PointLightComponent>();
        //   auto* game  = GetComponentInParent<GameManager>();
        template<typename T>
        T* GetComponentInChildren(bool includeSelf = true) const
        {
            T* found = nullptr;
            VisitChildren(includeSelf, [&](Entity e) { return (found = ComponentOrScript<T>(e)) != nullptr; });
            return found;
        }
        template<typename T>
        std::vector<T*> GetComponentsInChildren(bool includeSelf = true) const
        {
            std::vector<T*> all;
            VisitChildren(includeSelf, [&](Entity e) {
                if (T* c = ComponentOrScript<T>(e)) all.push_back(c);
                return false;
            });
            return all;
        }
        template<typename T>
        T* GetComponentInParent(bool includeSelf = true) const
        {
            T* found = nullptr;
            VisitParents(includeSelf, [&](Entity e) { return (found = ComponentOrScript<T>(e)) != nullptr; });
            return found;
        }
        template<typename T>
        std::vector<T*> GetComponentsInParent(bool includeSelf = true) const
        {
            std::vector<T*> all;
            VisitParents(includeSelf, [&](Entity e) {
                if (T* c = ComponentOrScript<T>(e)) all.push_back(c);
                return false;
            });
            return all;
        }
        // The entity itself (not the component) of the first match.
        template<typename T>
        Entity FindInChildren(bool includeSelf = true) const
        {
            return VisitChildren(includeSelf, [](Entity e) { return ComponentOrScript<T>(e) != nullptr; });
        }
        template<typename T>
        Entity FindInParent(bool includeSelf = true) const
        {
            return VisitParents(includeSelf, [](Entity e) { return ComponentOrScript<T>(e) != nullptr; });
        }

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

        // ---- timers (run on the main thread after OnUpdate; safe to call from inside callbacks)
        void Invoke(float delay, std::function<void()> callback);
        void InvokeRepeating(float delay, float interval, std::function<void()> callback);
        void CancelInvokes();
        bool IsInvoking() const;
        // Calls onUpdate(t) with eased t in [0,1] for `duration` seconds, then onComplete.
        void Tween(float duration, std::function<void(float)> onUpdate, Ease ease = Ease::InOutQuad,
                   std::function<void()> onComplete = {});
        void TweenPosition(const glm::vec3& target, float duration, Ease ease = Ease::InOutQuad,
                           std::function<void()> onComplete = {});
        void TweenScale(const glm::vec3& target, float duration, Ease ease = Ease::OutBack,
                        std::function<void()> onComplete = {});
        void CancelTweens();
        bool IsTweening() const;

        // ---- entity references (EntityRef fields)
        Entity Resolve(const EntityRef& ref) const;

        // ---- messages: delivered immediately to the target's script
        // (AddMessageHandler handlers for that name, then OnMessage). The payload can be
        // any copyable value; read it with message.Get<T>().
        //   SendMessage(enemy, "Damage", 25.0f);
        //   AddMessageHandler("Damage", [this](const ScriptMessage& m) { Health -= m.Get<float>(); });
        void AddMessageHandler(std::string name, MessageHandler handler);
        void RemoveMessageHandlers(std::string_view name);
        // true when `target` has a running script.
        bool SendMessage(Entity target, std::string name, std::any payload = {});
        // This entity (optional) and all its descendants. Returns the number of scripts reached.
        int SendMessageToChildren(std::string name, std::any payload = {}, bool includeSelf = true);
        // This entity (optional) and its ancestors, nearest first.
        int SendMessageUpwards(std::string name, std::any payload = {}, bool includeSelf = true);
        // Every running script in the scene, this one included.
        int Broadcast(std::string name, std::any payload = {});

        // ---- event bus (publish / subscribe). Subscriptions made here end automatically
        // when this script is destroyed.
        //   Subscribe("CoinCollected", [this](const ScriptMessage&) { m_Coins++; });
        //   Emit("CoinCollected");                 // now
        //   EmitDeferred("LevelDone", levelIndex);  // after this frame's OnLateUpdate
        SubscriptionID Subscribe(std::string eventName, MessageHandler handler);
        bool Unsubscribe(SubscriptionID id);
        int Emit(std::string eventName, std::any payload = {});
        void EmitDeferred(std::string eventName, std::any payload = {});
        EventBus& GetEventBus() const;

        // Returns the script instance of type T attached to `entity` (or nullptr).
        template<typename T>
        static T* GetScript(Entity entity)
        {
            if (!entity) return nullptr;
            auto* nsc = entity.TryGetComponent<NativeScriptComponent>();
            return nsc ? dynamic_cast<T*>(nsc->Instance) : nullptr;
        }

        // Called by the scene each frame.
        void TickTimers(float ts);

    private:
        template<typename T>
        static T* ComponentOrScript(Entity e)
        {
            if constexpr (std::is_base_of_v<ScriptableEntity, T>)
                return GetScript<T>(e);
            else
                return e.TryGetComponent<T>();
        }
        // Depth-first over this entity (optional) and its descendants / its ancestors;
        // stops at (and returns) the first entity for which visit() returns true.
        Entity VisitChildren(bool includeSelf, const std::function<bool(Entity)>& visit) const;
        Entity VisitParents(bool includeSelf, const std::function<bool(Entity)>& visit) const;
        // Called by the scene for SendMessage / Broadcast.
        void ReceiveMessage(const ScriptMessage& message);

        struct NamedHandler
        {
            std::string Name;
            MessageHandler Handler;
        };
        std::vector<NamedHandler> m_MessageHandlers;

        struct Timer
        {
            float Remaining = 0.0f, Interval = 0.0f;
            bool Repeat = false;
            bool Done = false;
            std::function<void()> Callback;
        };
        struct TweenState
        {
            float Elapsed = 0.0f, Duration = 1.0f;
            Ease Easing = Ease::Linear;
            bool Done = false;
            std::function<void(float)> OnUpdate;
            std::function<void()> OnComplete;
        };
        // Timers/tweens created while ticking go to the pending lists so the
        // vectors being iterated never reallocate under a running callback.
        std::vector<Timer> m_Timers, m_PendingTimers;
        std::vector<TweenState> m_Tweens, m_PendingTweens;
        bool m_TickingTimers = false;
        Entity m_Entity;
        friend class Scene;
    };

}
