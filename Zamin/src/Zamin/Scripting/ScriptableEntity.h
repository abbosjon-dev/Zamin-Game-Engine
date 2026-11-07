#pragma once

#include "Zamin/Math/Easing.h"
#include "Zamin/Math/Geometry.h"
#include "Zamin/Math/MathUtils.h"
#include "Zamin/Math/Quaternion.h"
#include "Zamin/Scene/TintAnimation.h"
#include "Zamin/Scene/Entity.h"
#include "Zamin/Scene/SceneManager.h"
#include "Zamin/Core/GameState.h"
#include "Zamin/Core/Input.h"
#include "Zamin/Core/Services.h"
#include "Zamin/Core/Stopwatch.h"
#include "Zamin/Core/Timer.h"
#include "Zamin/Core/Timestep.h"
#include "Zamin/Math/Random.h"
#include "Zamin/Scripting/EventBus.h"
#include "Zamin/UI/UIAnimation.h"
#include "Zamin/UI/UIEvents.h"
#include "Zamin/Utils/StringUtils.h"

#include <any>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace Zamin {

    struct SoundParams;

    // A 2D joint of the script's entity that broke (BreakForce / BreakTorque exceeded): it is gone
    // for good (its component's Broken flag is set; clearing it makes the joint again).
    struct JointBreak2D
    {
        Joint2DType Type = Joint2DType::Distance;
        Entity ConnectedBody;  // invalid: the joint held the body to a point in the world
        float Force = 0.0f;    // the reaction force / torque that broke it
        float Torque = 0.0f;
    };

    // A 3D joint of the script's entity that broke (BreakForce / BreakTorque exceeded): it is gone
    // for good (its component's Broken flag is set; clearing it makes the joint again).
    struct JointBreak3D
    {
        Joint3DType Type = Joint3DType::Fixed;
        Entity ConnectedBody;  // invalid: the joint held the body to a point in the world
        float Force = 0.0f;    // the reaction force / torque that broke it
        float Torque = 0.0f;
    };

    // A sound of the script's entity that ended by itself (OnSoundFinished): its Audio Source's
    // sound (not looping) or a one-shot this script played (PlaySound / PlaySoundAt).
    struct SoundFinished
    {
        uint32_t Handle = 0;          // the AudioEngine handle the sound had
        std::string Clip;             // its file, relative to Assets
        Entity Source;                // the entity the sound belonged to
        bool FromAudioSource = false; // the entity's Audio Source (false: PlaySound of the script)
        // Not played to its end: the voice limit stopped it to make room for another sound
        // (a looping one included).
        bool Stolen = false;
    };

    // An animation event that reached the entity's script (OnAnimationEvent): a moment of a property
    // clip (Animation Player, .zanim) or of a skeletal clip (the Animator's events).
    struct AnimationEventInfo
    {
        std::string Name;
        std::string Parameter;   // the event's optional text
        std::string Clip;        // the clip's name (.zanim file name / glTF animation name)
        float Time = 0.0f;       // seconds into a property clip; 0..1 of a skeletal clip
        Entity Source;           // the entity whose player / animator played it
        bool Skeletal = false;   // from the Animator (glTF clip)
    };
    using AnimationEventHandler = std::function<void(const AnimationEventInfo&)>;

    // Base class for C++ gameplay scripts.
    //
    //   class Player : public Zamin::ScriptableEntity
    //   {
    //   public:
    //       float Speed = 5.0f;
    //       void OnUpdate(Zamin::Timestep ts) override { ... }
    //   };
    //   ZM_REGISTER_SCRIPT(Player, ZM_FIELD(Player, Speed))
    // (Ease / ApplyEase: Zamin/Math/Easing.h; Math::Remap, SmoothDamp, MoveTowards ...:
    // Zamin/Math/MathUtils.h; Math::LookRotation, Slerp, RotateTowards ...: Zamin/Math/Quaternion.h)

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
        // A 3D joint of this entity broke (after the physics step; also reaches disabled scripts).
        virtual void OnJointBreak3D(const JointBreak3D& /*joint*/) {}
        // Called when a UIButton on this entity is clicked (by the mouse, or Enter / gamepad A while
        // it is selected).
        virtual void OnClick() {}
        // The mouse and this UI entity (one that takes the mouse: an interactable widget or a UI
        // Image that is a raycast target - the topmost one under the mouse):
        // - OnPointerEnter / OnPointerExit: the mouse came onto it / left it - also for a parent UI
        //   entity while the mouse is over one of its children (event.Target: that child);
        // - OnPointerDown: the left button went down on it; OnPointerUp: it came up again (anywhere;
        //   event.Inside: over this entity) - then OnPointerClick when it was over it.
        // Order in a frame: exits, enters, down, up, click, OnClick, value changes. Also emitted on
        // the scene's event bus: "UIPointerEnter" / "UIPointerExit" / "UIPointerDown" / "UIPointerUp" /
        // "UIPointerClick" (payload UIPointerEvent, sender: the entity).
        //   void OnPointerEnter(const UIPointerEvent&) override { TweenColor({ 1.0f, 0.9f, 0.5f, 1.0f }, 0.1f); }
        virtual void OnPointerEnter(const UIPointerEvent& /*event*/) {}
        virtual void OnPointerExit(const UIPointerEvent& /*event*/) {}
        virtual void OnPointerDown(const UIPointerEvent& /*event*/) {}
        virtual void OnPointerUp(const UIPointerEvent& /*event*/) {}
        virtual void OnPointerClick(const UIPointerEvent& /*event*/) {}
        // Keyboard / gamepad navigation (Scene::SetSelectedUI, UINavigationComponent): this widget
        // became the selected one / stopped being it; Escape / gamepad B while it is selected.
        // Bus: "UISelected", "UIDeselected", "UICancel" (sender: the widget).
        virtual void OnSelect() {}
        virtual void OnDeselect() {}
        virtual void OnCancel() {}
        // UI widgets on this entity, after the player changed them (not when code sets their fields;
        // Scene::SetSliderValue / SetToggle / SetInputText notify unless told not to). Each is also
        // emitted on the scene's event bus with the widget as the sender:
        // "UISliderChanged" (float), "UIToggleChanged" (bool), "UITextChanged" / "UITextSubmitted" /
        // "UITextEndEdit" (std::string).
        //   void OnValueChanged(float volume) override { AudioEngine::SetBusVolume("Music", volume); }
        virtual void OnValueChanged(float /*value*/) {}                 // UI Slider: dragged / clicked to a new value
        virtual void OnToggleChanged(bool /*isOn*/) {}                  // UI Toggle: switched (also off by its group)
        virtual void OnTextChanged(const std::string& /*text*/) {}      // UI Input Field: typed, deleted, pasted
        virtual void OnTextSubmit(const std::string& /*text*/) {}       // UI Input Field: Enter
        virtual void OnTextEndEdit(const std::string& /*text*/) {}      // UI Input Field: editing ended (Enter, Escape, a click elsewhere)
        // UI Scroll View: its content moved (wheel, drag, scrollbar, inertia, the spring back,
        // navigation, code); x 0 = left .. 1 = right, y 1 = top .. 0 = bottom. Bus: "UIScrollChanged" (glm::vec2).
        virtual void OnScrollChanged(const glm::vec2& /*position*/) {}
        // A sound of this entity ended by itself, on the main thread, in the frame the engine noticed
        // it (after OnLateUpdate): the Audio Source's sound or a one-shot this script played reached
        // its end (not looping), or the voice limit cut it off (sound.Stolen). Not for sounds stopped
        // on purpose (StopAudio, the entity switched off). Also emitted on the scene's event bus as
        // "SoundFinished" (payload: SoundFinished) for other scripts to listen to.
        //   void OnSoundFinished(const SoundFinished& sound) override { if (!sound.Stolen) PlayNextLine(); }
        virtual void OnSoundFinished(const SoundFinished& /*sound*/) {}
        // This entity's non-looping Particle System played its whole cycle (Duration) and its last
        // particle died (and those of its sub-emitter templates among its children: a rocket's
        // final burst) - before its stop action (switched off / destroyed). Also emitted on the
        // scene's event bus as "ParticlesFinished" (sender: the entity, no payload).
        //   void OnParticlesFinished() override { Destroy(); }
        virtual void OnParticlesFinished() {}
        // Called for every message sent to this entity (SendMessage, Broadcast ...),
        // after the handlers registered with AddMessageHandler for that name.
        virtual void OnMessage(const ScriptMessage& /*message*/) {}
        // An animation event of this entity's Animation Player or Animator was passed (play mode),
        // after the handlers bound to its name (AddAnimationEventHandler). Several events of one
        // frame come in the order they were passed; each fires once every time its moment is
        // passed (a hitch longer than a looping clip plays each of its events once). Also emitted
        // on the event bus as "AnimationEvent" (payload: AnimationEventInfo).
        //   void OnAnimationEvent(const AnimationEventInfo& e) override { if (e.Name == "Footstep") PlayStep(); }
        virtual void OnAnimationEvent(const AnimationEventInfo& /*event*/) {}
        // The game's language changed (Localization::SetLanguage, e.g. a language menu): texts the
        // script made with Localize(...) are made again here. Text components with a localization
        // key follow by themselves. Called on the next frame, before the scripts' OnUpdate.
        virtual void OnLanguageChanged(const std::string& /*language*/) {}
        // Scene events of the game's SceneManager (play mode, the player), to every script of every
        // loaded scene and of the persistent DontDestroyOnLoad one, at the end of the frame the change
        // happened in. A Single load: OnSceneUnloaded for each scene that went, OnActiveSceneChanged,
        // then OnSceneLoaded (the new scene's own scripts get it too: after their OnCreate, before
        // OnStart); an Additive load: OnSceneLoaded; UnloadScene: OnSceneUnloaded (+ OnActiveSceneChanged
        // when it was the active one); the game's first scene: OnActiveSceneChanged (from no scene) and
        // OnSceneLoaded. Also on the event bus: "SceneLoaded" / "SceneUnloaded" (payload SceneEventInfo),
        // "ActiveSceneChanged" (payload ActiveSceneChange).
        //   void OnSceneLoaded(const SceneEventInfo& scene) override { if (scene.BuildIndex == 3) PlayMusic("Audio/boss.ogg"); }
        virtual void OnSceneLoaded(const SceneEventInfo& /*scene*/) {}
        virtual void OnSceneUnloaded(const SceneEventInfo& /*scene*/) {}
        virtual void OnActiveSceneChanged(const SceneEventInfo& /*previous*/, const SceneEventInfo& /*current*/) {}

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

        // First entity with this name (cached hash lookup, cheap enough to call every frame): in this
        // script's scene, else in the other loaded scenes (the active one first, the persistent last).
        Entity FindEntity(std::string_view name) const;
        std::vector<Entity> FindEntities(std::string_view name) const;

        // ---- gameplay tags (TagsComponent)
        bool HasTag(std::string_view tag) const { return m_Entity.HasTag(tag); }
        bool AddTag(std::string_view tag) { return m_Entity.AddTag(tag); }
        bool RemoveTag(std::string_view tag) { return m_Entity.RemoveTag(tag); }
        // This scene's first, then the other loaded scenes' (indexed: O(k) for k tagged entities).
        std::vector<Entity> FindEntitiesWithTag(std::string_view tag) const;
        Entity FindEntityWithTag(std::string_view tag) const;
        // ---- by type: every component T, or every running script of class T (and its subclasses),
        // this script's scene first, then the other loaded scenes (the active one first, the
        // persistent DontDestroyOnLoad one last). O(k): the k entities with a T (scripts: every script).
        //   for (auto* enemy : FindObjectsOfType<EnemyAI>()) enemy->Alert();
        //   if (auto* hud = FindObjectOfType<Hud>()) hud->SetScore(m_Score);
        //   size_t lights = FindObjectsOfType<PointLightComponent>().size();
        template<typename T>
        std::vector<T*> FindObjectsOfType() const
        {
            Scene* scene = m_Entity.GetScene();
            std::vector<T*> found = scene->template FindObjectsOfType<T>();
            if (SceneManager* manager = scene->GetSceneManager())
                for (Scene* other : manager->GetSearchOrder())
                    if (other != scene)
                    {
                        std::vector<T*> more = other->template FindObjectsOfType<T>();
                        found.insert(found.end(), more.begin(), more.end());
                    }
            return found;
        }
        template<typename T>
        T* FindObjectOfType() const
        {
            Scene* scene = m_Entity.GetScene();
            if (T* found = scene->template FindObjectOfType<T>())
                return found;
            if (SceneManager* manager = scene->GetSceneManager())
                for (Scene* other : manager->GetSearchOrder())
                    if (other != scene)
                        if (T* found = other->template FindObjectOfType<T>())
                            return found;
            return nullptr;
        }
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
        // A force for this frame (dynamic bodies; wakes it): applied every frame, it acts the same at
        // any frame rate and sub-step count (scene settings > Physics 2D).
        void ApplyForce(const glm::vec2& force);
        void SetPosition2D(const glm::vec2& position); // teleports the physics body too (and wakes it)
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
        // Sleeping (Rigidbody 3D > Sleeping, scene settings > Physics 3D): a body that keeps still
        // for a while stops being simulated until something touches it. WakeUp3D brings it back at
        // once, PutToSleep3D stops it now (it wakes again when hit); the Entity versions are for
        // another body:
        //   if (IsSleeping3D(crate)) WakeUp3D(crate);
        bool IsSleeping3D() const;
        void WakeUp3D();
        void PutToSleep3D();
        bool IsSleeping3D(Entity other) const;
        void WakeUp3D(Entity other);
        void PutToSleep3D(Entity other);
        // Same, for another entity (e.g. a freshly instantiated projectile)
        void SetVelocity3D(Entity other, const glm::vec3& velocity);
        void ApplyImpulse3D(Entity other, const glm::vec3& impulse);
        void SetVelocity(Entity other, const glm::vec2& velocity);
        void ApplyImpulse(Entity other, const glm::vec2& impulse);

        // ---- raycasts (EntityID of the hit can be resolved with GetScene().GetEntityByUUID)
        // layerMask: the 2D collision layers that count (all by default), e.g. LayerMask({ "Ground", "Enemy" }).
        RaycastHit Raycast2D(const glm::vec2& origin, const glm::vec2& direction, float maxDistance = 100.0f, uint16_t layerMask = 0xFFFF) const;
        // The bits of the project's named collision layers (Project settings > Physics layers; 2D and
        // 3D physics share them); unknown names are left out.
        static uint16_t LayerMask(std::initializer_list<std::string_view> layerNames);
        // The index of a named collision layer, -1 when the project has none of that name.
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
        // ---- 3D queries: the same collision layers as 2D (LayerMask) - Rigidbody 3D / Character
        // Controller 3D pick theirs; triggers only with includeTriggers. A ray or swept shape that
        // starts inside a collider does not hit it, so one started inside the entity's own body
        // passes it by; mesh colliders are hit from their front side.
        //   if (RaycastHit hit = Raycast3D(GetWorldPosition(), { 0, 0, 1 }, 20.0f, LayerMask({ "Enemy" })))
        //       Hurt(GetScene().GetEntityByUUID(hit.EntityID));
        RaycastHit Raycast3D(const glm::vec3& origin, const glm::vec3& direction, float maxDistance = 100.0f, uint16_t layerMask = 0xFFFF) const;
        // Every entity along the ray (its first hit), the closest first.
        std::vector<RaycastHit> Raycast3DAll(const glm::vec3& origin, const glm::vec3& direction, float maxDistance = 100.0f,
                                             uint16_t layerMask = 0xFFFF, bool includeTriggers = false) const;
        // The entities whose 3D colliders / character capsules overlap a sphere, a box (half extents,
        // turned by `rotation`) or a capsule (the centres of its two end spheres, its radius); the one
        // nearest to the centre first. The entity itself is found too when it is inside:
        //   for (Entity e : OverlapSphere3D(GetWorldPosition(), 4.0f, LayerMask({ "Enemy" }))) Hurt(e);
        std::vector<Entity> OverlapSphere3D(const glm::vec3& center, float radius, uint16_t layerMask = 0xFFFF, bool includeTriggers = false) const;
        std::vector<Entity> OverlapBox3D(const glm::vec3& center, const glm::vec3& halfExtents, const glm::quat& rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                                         uint16_t layerMask = 0xFFFF, bool includeTriggers = false) const;
        std::vector<Entity> OverlapCapsule3D(const glm::vec3& point0, const glm::vec3& point1, float radius, uint16_t layerMask = 0xFFFF,
                                             bool includeTriggers = false) const;
        // Shape casts: a "thick ray" - the shape swept along `direction` for `maxDistance`. The hit's
        // Distance is how far the shape went (it is then at origin + direction * Distance), Point where
        // it touches, Normal the surface there. *All: every entity on the way, the closest first.
        //   RaycastHit ground = SphereCast3D(GetWorldPosition() + glm::vec3(0, 0.5f, 0), 0.3f, { 0, -1, 0 }, 1.0f);
        RaycastHit SphereCast3D(const glm::vec3& origin, float radius, const glm::vec3& direction, float maxDistance = 100.0f,
                                uint16_t layerMask = 0xFFFF, bool includeTriggers = false) const;
        std::vector<RaycastHit> SphereCast3DAll(const glm::vec3& origin, float radius, const glm::vec3& direction, float maxDistance = 100.0f,
                                                uint16_t layerMask = 0xFFFF, bool includeTriggers = false) const;
        RaycastHit BoxCast3D(const glm::vec3& center, const glm::vec3& halfExtents, const glm::vec3& direction,
                             const glm::quat& rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f), float maxDistance = 100.0f, uint16_t layerMask = 0xFFFF,
                             bool includeTriggers = false) const;
        std::vector<RaycastHit> BoxCast3DAll(const glm::vec3& center, const glm::vec3& halfExtents, const glm::vec3& direction,
                                             const glm::quat& rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f), float maxDistance = 100.0f,
                                             uint16_t layerMask = 0xFFFF, bool includeTriggers = false) const;
        RaycastHit CapsuleCast3D(const glm::vec3& point0, const glm::vec3& point1, float radius, const glm::vec3& direction,
                                 float maxDistance = 100.0f, uint16_t layerMask = 0xFFFF, bool includeTriggers = false) const;
        std::vector<RaycastHit> CapsuleCast3DAll(const glm::vec3& point0, const glm::vec3& point1, float radius, const glm::vec3& direction,
                                                 float maxDistance = 100.0f, uint16_t layerMask = 0xFFFF, bool includeTriggers = false) const;

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

        // ---- UI animations (this entity's UI Transform; Scene::TweenUI - they run on the UI's own
        // time, also while the game is paused, and its UI children move / fade with it). Each returns
        // the tween's id (0: no UI Transform).
        //   UIScaleTo({ 1.2f, 1.2f }, 0.2f);                         // grow (around its pivot)
        //   UIFadeTo(0.0f, 0.3f, Ease::OutQuad, [this] { SetActive(false); });
        //   UIMoveTo({ 0.0f, 40.0f }, 0.4f);                         // drawn 40 UI units higher
        //   TweenUI(UITweenProperty::Alpha, glm::vec4(0.3f), 0.6f, { .Easing = Ease::InOutQuad, .Loop = UITweenLoop::PingPong });
        uint64_t TweenUI(UITweenProperty property, const glm::vec4& to, float duration, UITweenOptions options = {});
        uint64_t UIScaleTo(const glm::vec2& scale, float duration, Ease ease = Ease::OutBack, std::function<void()> onComplete = {});
        uint64_t UIFadeTo(float alpha, float duration, Ease ease = Ease::OutQuad, std::function<void()> onComplete = {});
        uint64_t UIMoveTo(const glm::vec2& offset, float duration, Ease ease = Ease::OutCubic, std::function<void()> onComplete = {});
        // A quick bump: `amount` bigger, springing back (a coin counter that just changed).
        void UIPunchScale(float amount = 0.15f, float duration = 0.35f);
        void CancelUITweens(bool complete = false);
        // Another UI entity (a panel): switched on with its UIAnimation's show / its hide played,
        // then switched off (Scene::ShowUI / HideUI).
        void ShowUI(Entity panel);
        void HideUI(Entity panel, bool animate = true);

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
        // Plays one of the Sprite Animation's clips (.zsanim) by name (its file name without the
        // extension) or path: its frames, rate, loop, texture and events. Already playing it: it
        // goes on (unless restart). False when the entity has no such clip.
        bool PlaySpriteClip(const std::string& clip, bool restart = false);
        // The name of the sprite clip playing ("" = none).
        std::string GetSpriteClip() const;

        // ---- scenes (SceneManager; paths relative to Assets, or a build scene's name: "Level2").
        // The switch happens at the end of the frame. Single: every loaded scene goes and this one
        // comes (objects given to DontDestroyOnLoad stay); Additive: it is loaded next to them.
        //   LoadScene("Scenes/Level2.zscene");
        //   LoadScene("Scenes/Level2.zscene", SceneTransition::Fade(0.5f));   // fade out to black, in again
        //   LoadScene("Scenes/HUD.zscene", LoadSceneMode::Additive);
        //   LoadScene(2);                                                      // build index (Project settings)
        void LoadScene(const std::string& scenePath);
        void LoadScene(const std::string& scenePath, LoadSceneMode mode, const SceneTransition& transition = {});
        void LoadScene(const std::string& scenePath, const SceneTransition& transition);
        // By build index (Project settings > Build scenes: the enabled scenes, from 0). False (logged)
        // when the build list has no such index.
        bool LoadScene(int buildIndex, LoadSceneMode mode = LoadSceneMode::Single, const SceneTransition& transition = {});
        // The next scene of the build list after the active one (false at its end), the active one again.
        //   if (!LoadNextScene(SceneTransition::Fade(0.5f))) LoadScene(0); // the last level: back to the menu
        bool LoadNextScene(const SceneTransition& transition = {});
        bool ReloadScene(const SceneTransition& transition = {});
        // This script's scene's build index / the active scene's (-1: not in the build list), the
        // build list's length.
        int GetSceneBuildIndex() const;
        int GetActiveSceneBuildIndex() const;
        int GetBuildSceneCount() const;
        // In the background (a loading screen): the file is read, parsed and its textures decoded on a
        // worker thread, the rest a slice per frame; GetProgress() 0..1 (0.9: loaded, waiting for
        // activation). SetAllowSceneActivation(false) holds it until the screen is ready. Null without
        // a scene manager (a scene running on its own) or when the file is missing.
        //   m_Load = LoadSceneAsync("Scenes/Level1.zscene");
        //   m_Load->SetAllowSceneActivation(false);
        //   ... bar.Value = m_Load->GetProgress() / 0.9f; if (m_Load->IsReady()) m_Load->SetAllowSceneActivation(true);
        Ref<AsyncOperation> LoadSceneAsync(const std::string& scenePath, LoadSceneMode mode = LoadSceneMode::Single,
                                           const SceneTransition& transition = {});
        Ref<AsyncOperation> LoadSceneAsync(int buildIndex, LoadSceneMode mode = LoadSceneMode::Single, const SceneTransition& transition = {});
        // Unloads a loaded scene (its name or path) at the end of the frame (not the last one).
        bool UnloadScene(const std::string& nameOrPath);
        // `entity` (this one without an argument; a child leaves its parent) with its descendants moves
        // to the persistent DontDestroyOnLoad scene at the end of the frame: it survives every scene
        // switch, its script goes on with its values and subscriptions, its sounds keep playing.
        //   void OnCreate() override { DontDestroyOnLoad(); }  // a game manager with the music
        void DontDestroyOnLoad();
        void DontDestroyOnLoad(Entity entity);
        // The game's scene manager (null for a scene running on its own: tests, the editor's simulate).
        SceneManager* GetSceneManager() const { return m_Entity.GetScene() ? m_Entity.GetScene()->GetSceneManager() : nullptr; }
        // Sub-scenes (SubSceneComponent on `host`; this entity without an argument): loaded additively
        // at the end of the frame, placed by the host's transform / unloaded. A sub-scene whose
        // LoadOnStart is off waits for this (a room that opens when the player gets near).
        //   if (near && !GetSubScene()) LoadSubScene();
        bool LoadSubScene();
        bool LoadSubScene(Entity host);
        bool UnloadSubScene();
        bool UnloadSubScene(Entity host);
        // The loaded sub-scene of `host` (null when it is not loaded).
        Ref<Scene> GetSubScene() const;
        Ref<Scene> GetSubScene(Entity host) const;

        // ---- audio (music and the mixer groups: the Audio class, e.g. Audio::CrossfadeTo("Audio/boss.ogg"))
        // A one-shot sound in a mixer group (SFX, UI, ... ; an unknown name: SFX). The PlaySound
        // overloads return the AudioEngine handle (0: not played) and OnSoundFinished tells this
        // script when the sound ends.
        uint32_t PlaySound(const std::string& assetPath, float volume = 1.0f, float pitch = 1.0f, const std::string& group = "SFX");
        // 3D positional sound (heard relative to the Audio Listener / primary camera)
        uint32_t PlaySoundAt(const std::string& assetPath, const glm::vec3& position, float volume = 1.0f, float pitch = 1.0f,
                             const std::string& group = "SFX");
        // A one-shot sound with everything SoundParams has: random volume / pitch range, priority
        // and instance limit, 3D position... e.g.
        //   SoundParams step; step.PitchRandom = { 0.9f, 1.1f }; step.MaxInstances = 3;
        //   PlaySound("Audio/step.wav", step);
        // Returns the AudioEngine handle (0: not played - no device, a voice limit, a bad file).
        uint32_t PlaySound(const std::string& assetPath, const SoundParams& params);
        // This entity's AudioSourceComponent (the `other` overloads: another entity's).
        // PlayAudio (re)starts it from the beginning (with a new random variation) and returns false
        // while the entity is inactive (its sound stops when it is switched off and a PlayOnStart
        // source starts again when it is switched back on) or a voice limit refused it.
        bool PlayAudio();
        void StopAudio();
        // Pause keeps the sound where it is, Resume goes on from there; false when nothing plays.
        bool PauseAudio();
        bool ResumeAudio();
        // Heard now (false while paused) / paused.
        bool IsAudioPlaying() const;
        bool IsAudioPaused() const;
        // The source's Volume / Pitch, also of the sound playing now.
        void SetAudioVolume(float volume);
        float GetAudioVolume() const;
        void SetAudioPitch(float pitch);
        float GetAudioPitch() const;
        // Seconds into the playing sound (0 when nothing plays); SetAudioTime jumps there.
        float GetAudioTime() const;
        bool SetAudioTime(float seconds);
        // The playing sound's length in seconds (0 when nothing plays).
        float GetAudioLength() const;
        // With a fade of its own (seconds; the source's FadeIn / FadeOut are the defaults above):
        // PlayAudio(1.5f) starts silent and fades in, StopAudio(2.0f) fades out (the sound goes on
        // by itself until silent; IsAudioPlaying is false at once and PlayAudio starts anew).
        bool PlayAudio(float fadeInSeconds);
        void StopAudio(float fadeOutSeconds);
        // The playing sound's fade envelope to `volume` (0..1, x the source's Volume) over `seconds`,
        // e.g. FadeAudio(0.3f, 0.5f) to duck it under a voice line. False when nothing plays.
        bool FadeAudio(float volume, float seconds);
        float GetAudioFade() const;
        // The source's low-pass / high-pass filter, cutoff in Hz (<= 0 switches it off); a change
        // glides there (~30 ms), e.g. SetAudioLowPass(600.0f) muffles it behind a wall.
        void SetAudioLowPass(float cutoffHz);
        void SetAudioHighPass(float cutoffHz);
        bool PlayAudio(Entity other);
        void StopAudio(Entity other);
        bool PlayAudio(Entity other, float fadeInSeconds);
        void StopAudio(Entity other, float fadeOutSeconds);
        bool FadeAudio(Entity other, float volume, float seconds);
        void SetAudioLowPass(Entity other, float cutoffHz);
        bool PauseAudio(Entity other);
        bool ResumeAudio(Entity other);
        bool IsAudioPlaying(Entity other) const;
        bool IsAudioPaused(Entity other) const;
        void SetAudioVolume(Entity other, float volume);
        void SetAudioPitch(Entity other, float pitch);
        float GetAudioTime(Entity other) const;
        bool SetAudioTime(Entity other, float seconds);

        // ---- time (also from anywhere: Time::GetDeltaTime / GetUnscaledDeltaTime / GetFixedDeltaTime /
        // GetTimeScale / SetTimeScale / GetFrameCount / GetTimeSinceStartup)
        float GetTime() const;
        // The game's clock (Scene::SetTimeScale): 1 normal, 0.5 slow motion, 0 paused - OnUpdate's
        // time, timers, physics and animations follow it; the UI does not. Every loaded scene runs on
        // the same clock (a persistent pause menu pauses the level too).
        void SetTimeScale(float scale);
        float GetTimeScale() const;
        // Pauses the game (time scale 0) / goes on at the scale it had: a pause menu.
        void PauseGame();
        void ResumeGame();
        bool IsGamePaused() const;
        // This frame's real time (seconds): for what moves while the game is paused or slowed.
        float GetUnscaledDeltaTime() const;
        // This script still gets OnUpdate / OnLateUpdate (with 0) while the game is paused - the
        // one that opens and closes the pause menu. Off by default (set it in OnCreate).
        void SetUpdateWhenPaused(bool enabled) { m_UpdateWhenPaused = enabled; }
        bool IsUpdateWhenPaused() const { return m_UpdateWhenPaused; }
        // This script's timers and tweens (Invoke, Tween*) run on real time: not slowed by the time scale
        // and going on while the game is paused (a pause menu's fades, a slow-motion effect's own timer).
        // Off by default: they follow the game's clock.
        void SetTimersUnscaled(bool unscaled) { m_TimersUnscaled = unscaled; }
        bool IsTimersUnscaled() const { return m_TimersUnscaled; }
        // Ends the game (its menu's Quit): the player closes, the editor stops Play.
        void QuitGame();

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

        // ---- entity references (EntityRef fields): in this scene, else in the other loaded scenes
        // (an entity moved to DontDestroyOnLoad keeps its UUID).
        Entity Resolve(const EntityRef& ref) const;

        // ---- messages: delivered immediately to the target's script
        // (AddMessageHandler handlers for that name, then OnMessage). The payload can be
        // any copyable value; read it with message.Get<T>().
        //   SendMessage(enemy, "Damage", 25.0f);
        //   AddMessageHandler("Damage", [this](const ScriptMessage& m) { Health -= m.Get<float>(); });
        void AddMessageHandler(std::string name, MessageHandler handler);
        void RemoveMessageHandlers(std::string_view name);

        // ---- animation events: a method of this script called by an event's name
        //   AddAnimationEventHandler("Footstep", [this](const AnimationEventInfo&) { PlayFootstep(); });
        void AddAnimationEventHandler(std::string name, AnimationEventHandler handler);
        void RemoveAnimationEventHandlers(std::string_view name);

        // ---- property animation (AnimationPlayerComponent, clips made in the Animation window)
        // Plays a clip of the entity's player by name ("" = the current / first one) from its start;
        // speed < 0 plays it backwards from its end. False without a player or such a clip.
        bool PlayAnimation(std::string_view clip = {}, float speed = 1.0f);
        void StopAnimation();
        bool IsAnimationPlaying() const;
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

        // ---- the game's global state and services: outside the scenes, every scene load keeps them
        //   GameState::Increment("Coins");  GameState::GetInt("Coins");
        //   WatchGameState("Coins", [this](std::string_view) { RefreshHud(); });
        //   auto& inventory = GetOrCreateService<Inventory>();
        // Calls `handler` after the value of `key` changes ("" = any key); ends with this script.
        GameState::ListenerID WatchGameState(std::string_view key, GameState::Listener handler);
        bool UnwatchGameState(GameState::ListenerID id) { return GameState::RemoveListener(id); }
        // The game's service of type T (Services): null when none is registered.
        template<typename T>
        static T* GetService() { return Services::Get<T>(); }
        // ... made with T's default constructor when there is none.
        template<typename T>
        static T& GetOrCreateService() { return Services::GetOrCreate<T>(); }

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
        // Called by the scene for animation events (the handlers of that name, then OnAnimationEvent).
        void ReceiveAnimationEvent(const AnimationEventInfo& event);

        struct NamedHandler
        {
            std::string Name;
            MessageHandler Handler;
        };
        std::vector<NamedHandler> m_MessageHandlers;
        struct NamedAnimationHandler
        {
            std::string Name;
            AnimationEventHandler Handler;
        };
        std::vector<NamedAnimationHandler> m_AnimationHandlers;

        struct TimerState
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
        std::vector<TimerState> m_Timers, m_PendingTimers;
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
        bool m_UpdateWhenPaused = false; // OnUpdate also while the game is paused (SetUpdateWhenPaused)
        bool m_TimersUnscaled = false;   // timers / tweens on real time (SetTimersUnscaled)

        Entity m_Entity;
        std::string m_ClassName;
        uint64_t m_OwnerID = 0; // entity UUID, also known after the entity's components are gone
        friend class Scene;
    };

}
