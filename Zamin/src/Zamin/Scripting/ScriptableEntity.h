#pragma once

#include "Zamin/Math/Easing.h"
#include "Zamin/Scene/TintAnimation.h"
#include "Zamin/Scene/Entity.h"
#include "Zamin/Core/Input.h"
#include "Zamin/Core/Timestep.h"
#include "Zamin/Math/Random.h"
#include "Zamin/Scripting/EventBus.h"

#include <any>
#include <functional>
#include <initializer_list>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace Zamin {

    // A 2D joint of the script's entity that broke (BreakForce / BreakTorque exceeded): it is gone
    // for good (its component's Broken flag is set; clearing it makes the joint again).
    struct JointBreak2D
    {
        Joint2DType Type = Joint2DType::Distance;
        Entity ConnectedBody;  // invalid: the joint held the body to a point in the world
        float Force = 0.0f;    // the reaction force / torque that broke it
        float Torque = 0.0f;
    };

    // Base class for C++ gameplay scripts.
    //
    //   class Player : public Zamin::ScriptableEntity
    //   {
    //   public:
    //       float Speed = 5.0f;
    //       void OnUpdate(Zamin::Timestep ts) override { ... }
    //   };
    //   ZM_REGISTER_SCRIPT(Player, ZM_FIELD(Player, Speed))
    // (Ease / ApplyEase: Zamin/Math/Easing.h)

    //
    // Lifecycle (in this order):
    //   OnCreate   - once, when the instance is created (play starts / the entity is spawned).
    //                All scripts of the scene are created before any OnEnable runs.
    //   OnEnable   - after OnCreate when the script is enabled, and every time it is re-enabled.
    //   OnStart    - once, right before the script's first OnUpdate (only while enabled), after
    //                every script that exists at that moment has run OnCreate/OnEnable.
    //   OnUpdate / OnLateUpdate - every frame while enabled.
    //   OnDisable  - when it is disabled, and before OnDestroy if it is enabled at that moment.
    //   OnDestroy  - once, when the entity is destroyed or play stops.
    // Event callbacks (collisions, triggers, messages, OnClick) also reach disabled scripts,
    // so a script can enable itself in response to them.
    // "Enabled" above means the script's own flag is on AND its entity is active in the
    // hierarchy (SetActive below / Scene::SetActive): switching the entity or an ancestor off
    // delivers OnDisable and stops OnUpdate / OnLateUpdate / timers; switching it back on
    // delivers OnEnable (OnStart is not repeated). Both come before SetActive returns, for the
    // whole subtree in hierarchy order (a parent before its children); OnDisable still sees the
    // subtree's physics bodies, OnEnable already sees the new ones. A script on an entity that
    // is inactive when play starts - or spawned inactive (Instantiate of an inactive template) -
    // gets OnCreate, and OnEnable / OnStart only once the entity becomes active. Messages and
    // event bus deliveries still reach scripts of inactive entities (a manager can wake them);
    // OnClick does not (an inactive button cannot be clicked). While the entity is inactive its
    // sound stops, its particles are cleared, its animators pause and its camera does not count
    // (see Scene::SetActive).
    class ScriptableEntity
    {
    public:
        virtual ~ScriptableEntity() = default;

        virtual void OnCreate() {}
        virtual void OnEnable() {}
        virtual void OnStart() {}
        virtual void OnDisable() {}
        virtual void OnDestroy() {}
        virtual void OnUpdate(Timestep) {}
        virtual void OnLateUpdate(Timestep) {}
        // Solid contacts (neither collider is a sensor), once per entity pair.
        virtual void OnCollisionBegin(Entity /*other*/) {}
        virtual void OnCollisionEnd(Entity /*other*/) {}
        // Contacts where at least one collider is a sensor (IsSensor), once per entity pair.
        // OnTriggerEnd also comes when the other entity is destroyed while overlapping.
        virtual void OnTriggerBegin(Entity /*other*/) {}
        virtual void OnTriggerEnd(Entity /*other*/) {}
        // A 2D joint of this entity broke (after the physics step; also reaches disabled scripts).
        virtual void OnJointBreak(const JointBreak2D& /*joint*/) {}
        // Called when a UIButton on this entity is clicked.
        virtual void OnClick() {}
        // Called for every message sent to this entity (SendMessage, Broadcast ...),
        // after the handlers registered with AddMessageHandler for that name.
        virtual void OnMessage(const ScriptMessage& /*message*/) {}

        // ---- enabled state (NativeScriptComponent::Enabled). SetEnabled runs
        // OnEnable / OnDisable immediately; a disabled script keeps its state and timers
        // (they are paused) and continues where it stopped when enabled again.
        bool IsEnabled() const;
        void SetEnabled(bool enabled);
        // true once OnStart has run.
        bool HasStarted() const { return m_Started; }

        // ---- active state of entities (TagComponent::Active, see Scene::SetActive)
        // SetActive switches an entity - and with it everything below it - on or off at once:
        // the scripts there get OnDisable / OnEnable before it returns, their bodies leave /
        // rejoin the physics worlds (at the current transform) and nothing of it is drawn while
        // it is off. Safe to call from any callback, also for this script's own entity (after
        // SetActive(false) inside OnUpdate this script gets no OnLateUpdate in that frame).
        //   SetActive(false);                    // this entity
        //   SetActive(FindEntity("Door"), true); // another one (or entity.SetActive(true))
        void SetActive(bool active);
        void SetActive(Entity other, bool active);
        // This entity's own flag: an inactive ancestor can still keep it out of the game.
        bool IsActive() const;
        // This entity and every ancestor are active.
        bool IsActiveInHierarchy() const;
        // The script's own flag is on (IsEnabled) and its entity is active in the hierarchy:
        // the state in which it gets OnUpdate / OnLateUpdate / timers.
        bool IsActiveAndEnabled() const;
        // Registered class this instance was created from (NativeScriptComponent::ClassName
        // at creation time; the component's value can change later, the instance cannot).
        const std::string& GetScriptClassName() const { return m_ClassName; }

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
        // Copy an existing entity (a "prefab") into the running scene. The copies keep their
        // own active flags: a switched-off template gives a switched-off clone (SetActive(clone,
        // true) brings it in). A template parked in a switched-off group gives a root clone.
        // position: local to the template's parent, or the world position for a root clone.
        Entity Instantiate(Entity prefab, const glm::vec3& position);
        Entity Instantiate(Entity prefab);
        // Spawn a prefab asset (.zprefab, path relative to Assets). Every entity of the prefab
        // starts as active as it was saved.
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
        // Falls through one-way platforms (OneWay tiles, one-way box colliders) for `seconds`:
        //   if (Input::GetAxis("Vertical") < -0.5f && Input::IsActionJustPressed("Jump")) DropThroughPlatforms();
        bool DropThroughPlatforms(float seconds = 0.35f);
        // Sleeping (Rigidbody 2D > Sleeping, scene settings > Physics 2D): a body that keeps still
        // for a while stops being simulated until something touches it. WakeUp brings it back at
        // once, PutToSleep stops it now (it wakes again when hit); the Entity versions are for another
        // body:
        //   if (IsSleeping(crate)) WakeUp(crate);
        bool IsSleeping() const;
        void WakeUp();
        void PutToSleep();
        bool IsSleeping(Entity other) const;
        void WakeUp(Entity other);

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
        void ApplyImpulse(Entity other, const glm::vec2& impulse);

        // ---- raycasts (EntityID of the hit can be resolved with GetScene().GetEntityByUUID)
        // layerMask: the 2D collision layers that count (all by default), e.g. LayerMask({ "Ground", "Enemy" }).
        RaycastHit Raycast2D(const glm::vec2& origin, const glm::vec2& direction, float maxDistance = 100.0f, uint16_t layerMask = 0xFFFF) const;
        // The bits of the project's named 2D collision layers (Project settings > Physics layers);
        // unknown names are left out.
        static uint16_t LayerMask(std::initializer_list<std::string_view> layerNames);
        // The index of a named 2D collision layer, -1 when the project has none of that name.
        static int LayerIndex(std::string_view layerName);
        // Every entity along the ray (its first hit), the closest first; triggers with includeTriggers:
        //   for (const RaycastHit& hit : Raycast2DAll(origin, { 1, 0 }, 20.0f, LayerMask({ "Enemy" }))) ...
        std::vector<RaycastHit> Raycast2DAll(const glm::vec2& origin, const glm::vec2& direction, float maxDistance = 100.0f,
                                             uint16_t layerMask = 0xFFFF, bool includeTriggers = false) const;
        // The entities whose 2D colliders overlap the circle / the box (half extents, turned by
        // `angleDegrees`) / contain the point; the one nearest to the centre first. The entity itself
        // is found too when it is inside - skip it if you do not want it:
        //   for (Entity e : OverlapCircle2D(GetWorldPosition(), 3.0f, LayerMask({ "Enemy" }))) Hurt(e);
        std::vector<Entity> OverlapCircle2D(const glm::vec2& center, float radius, uint16_t layerMask = 0xFFFF, bool includeTriggers = false) const;
        std::vector<Entity> OverlapBox2D(const glm::vec2& center, const glm::vec2& halfExtents, float angleDegrees = 0.0f,
                                         uint16_t layerMask = 0xFFFF, bool includeTriggers = false) const;
        std::vector<Entity> OverlapPoint2D(const glm::vec2& point, uint16_t layerMask = 0xFFFF, bool includeTriggers = false) const;
        RaycastHit Raycast3D(const glm::vec3& origin, const glm::vec3& direction, float maxDistance = 100.0f) const;

        // ---- viewport / mouse (pixels, origin top-left of the game view)
        glm::vec2 GetMousePosition() const;
        glm::vec2 GetViewportSize() const;
        // Mouse position projected onto the z = 0 plane using the primary camera (2D games).
        glm::vec2 GetMouseWorldPosition() const;
        glm::vec3 ScreenToWorld(const glm::vec2& screen, float depthZ = 0.0f) const;

        // ---- camera
        // Shakes the game camera's view (Scene::ShakeCamera): `intensity` 0..1 of trauma that fades
        // out over `duration` seconds; `frequency`: how jittery. Several shakes add up (at most 1).
        //   ShakeCamera(0.3f);               // a small hit
        //   ShakeCamera(1.0f, 1.2f, 25.0f);  // an explosion
        bool ShakeCamera(float intensity, float duration = 0.4f, float frequency = 20.0f);
        void StopCameraShake();
        // The primary camera's Camera 2D Follow target (added to the camera when missing). snap:
        // the camera jumps there on the next frame instead of gliding. False without a primary camera.
        bool SetCameraTarget(Entity target, bool snap = false);

        // ---- materials (requires MeshRendererComponent)
        // This entity's own copy of its material, made on the first call - of the material asset,
        // or of the inline material without one (models: the inline material is the tint of every
        // part) - and the same object after that. Its edits change only this entity: the shared
        // .zmat and the other entities using it stay as they are. Runtime only: gone when play
        // stops (a copy made with Instantiate gets a copy of it). nullptr without a MeshRenderer.
        //   GetMaterial()->SetColor({ 1.0f, 0.2f, 0.2f, 1.0f });
        //   GetMaterial()->SetEmission({ 1.0f, 0.5f, 0.1f }, 4.0f);
        Ref<MaterialAsset> GetMaterial();
        // Part `submesh` of a model: a copy of that part's whole material (primitives: GetMaterial()).
        Ref<MaterialAsset> GetMaterial(size_t submesh);
        Ref<MaterialAsset> GetMaterial(Entity other, int submesh = -1);
        // The shared material asset this entity uses (submesh: a model part's slot, else the
        // renderer's), nullptr with an inline material. Its edits change EVERY entity using it;
        // in the editor they are taken back when play stops.
        Ref<MaterialAsset> GetSharedMaterial(int submesh = -1) const;
        Ref<MaterialAsset> GetSharedMaterial(Entity other, int submesh = -1) const;
        // Another material asset (path relative to Assets, "" = back to the inline material) for
        // the whole renderer or one model part; the entity's instances are dropped. False without
        // a MeshRenderer or when the path is not a .zmat.
        bool SetSharedMaterial(const std::string& assetPath, int submesh = -1);
        bool SetSharedMaterial(Entity other, const std::string& assetPath, int submesh = -1);
        // Drops this entity's instances: it shows the shared material again.
        void ResetMaterial();
        void ResetMaterial(Entity other);

        // ---- particles
        void EmitParticles(int count); // requires ParticleSystemComponent; nothing while inactive

        // ---- sprites (2D)
        // The named frames of the Sprite Renderer's texture (Sprite Editor); nullptr when it has none.
        // ---- sprite effects (requires SpriteRendererComponent)
        // A hit flash: the sprite turns `color` at once (its alpha: how strongly) and fades back over
        // `duration` seconds.
        //   Flash();                                  // white, 0.15 s
        //   Flash({ 1.0f, 0.25f, 0.2f, 1.0f }, 0.3f); // red
        bool Flash(const glm::vec4& color = glm::vec4(1.0f), float duration = 0.15f);
        // A border `thickness` texture pixels wide around the sprite's picture (0: off).
        bool SetOutline(const glm::vec4& color, float thickness = 1.0f);

        // ---- colour of the 2D renderer (Sprite Renderer, else Circle Renderer, Text, Tilemap)
        // False / white without one.
        glm::vec4 GetColor() const;
        bool SetColor(const glm::vec4& color);
        // Tweens the renderer's colour to `target` over `duration` seconds (a Tween: CancelTweens
        // stops it; the colour then stays where it got to).
        //   TweenColor({ 1.0f, 0.3f, 0.3f, 1.0f }, 0.25f);       // to red
        //   FadeTo(0.0f, 0.5f, Ease::OutQuad, [this] { Destroy(); });
        bool TweenColor(const glm::vec4& target, float duration, Ease ease = Ease::InOutQuad, std::function<void()> onComplete = {});
        // Only the alpha.
        bool FadeTo(float alpha, float duration, Ease ease = Ease::InOutQuad, std::function<void()> onComplete = {});

        // ---- tint animation (TintAnimationComponent: the renderer's colour times a gradient)
        // (Re)starts the entity's tint animation; false without one.
        bool PlayTint(bool restart = true);
        // Plays white -> `color` (added when missing; its gradient is replaced), e.g. a blink:
        //   PlayTint({ 1.0f, 1.0f, 1.0f, 0.2f }, 0.08f, TintPlayMode::PingPong, 6);
        void PlayTint(const glm::vec4& color, float duration, TintPlayMode mode = TintPlayMode::PingPong, int cycles = 0,
                      Ease ease = Ease::InOutQuad);
        // Stops it where it is (reset: back to its first colour).
        bool StopTint(bool reset = false);
        bool IsTintPlaying() const;

        const SpriteSheet* GetSpriteSheet() const;
        // Shows one named frame of the texture ("" = the whole texture). False without a Sprite
        // Renderer or when the sheet has no such frame (the frame is set anyway).
        bool SetSprite(const std::string& frameName);
        // Plays the sheet's numbered frames "<prefix>_0", "<prefix>_1", ... on the Sprite Animation
        // (added when missing). Returns how many frames were found (0: nothing changes).
        int PlaySpriteSequence(const std::string& prefix, float fps = 10.0f, bool loop = true);

        // ---- scenes: switch to another scene at the end of the frame (path relative to Assets)
        void LoadScene(const std::string& scenePath);

        // ---- audio
        void PlaySound(const std::string& assetPath, float volume = 1.0f, float pitch = 1.0f);
        // 3D positional sound (heard relative to the primary camera)
        void PlaySoundAt(const std::string& assetPath, const glm::vec3& position, float volume = 1.0f, float pitch = 1.0f);
        // This entity's AudioSourceComponent: (re)start it from the beginning / stop it. PlayAudio
        // returns false while the entity is inactive (its sound stops when it is switched off and
        // a PlayOnStart source starts again when it is switched back on).
        bool PlayAudio();
        void StopAudio();
        bool IsAudioPlaying() const;

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

        // Lifecycle state, managed by the scene.
        bool m_Created = false;       // OnCreate has returned
        bool m_EnabledState = false;  // the last OnEnable / OnDisable that was delivered
        bool m_Started = false;       // OnStart has been called
        bool m_Destroying = false;    // OnDisable / OnDestroy in progress
        int m_EnableDepth = 0;        // nested SetEnabled calls from inside OnEnable / OnDisable
        int m_EnableTransitions = 0;  // OnEnable/OnDisable delivered by the current (nested) sync
        bool IsActiveForUpdate() const { return m_EnabledState && m_Started && !m_Destroying; }

        Entity m_Entity;
        std::string m_ClassName;
        uint64_t m_OwnerID = 0; // entity UUID, also known after the entity's components are gone
        friend class Scene;
    };

}
