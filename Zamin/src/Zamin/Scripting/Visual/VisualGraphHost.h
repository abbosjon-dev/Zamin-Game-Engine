#pragma once
#include "VisualGraph.h"
#include "Zamin/Gameplay/GameplayRuntime.h"
#include "Zamin/Scripting/ScriptableEntity.h"
#include "Zamin/Scripting/ScriptEngine.h"
namespace Zamin {
// Built into the runtime: graph projects do not require a C++ compiler or game module.
    class VisualGraphHost final : public ScriptableEntity
    {
    public:
        std::string Graph, Overrides;
        Visual::Machine VM;
        void OnCreate() override;
        void OnUpdate(Timestep dt) override;
        void OnLateUpdate(Timestep dt) override
        {
            m_Gameplay.AfterPhysics();
            Emit("LateUpdate", { { "Delta", (float)dt } });
        }
        void Emit(const std::string& event, Visual::Value payload = Visual::Value::object());
#define ZM_GRAPH_EVENT(name) \
    void On##name() override { Emit(#name); }
        ZM_GRAPH_EVENT(Enable)
        ZM_GRAPH_EVENT(Start)
        void OnDisable() override;
        void OnDestroy() override;
        ZM_GRAPH_EVENT(Click)
        ZM_GRAPH_EVENT(Select)
        ZM_GRAPH_EVENT(Deselect)
        ZM_GRAPH_EVENT(Cancel) ZM_GRAPH_EVENT(ParticlesFinished)
#undef ZM_GRAPH_EVENT
#define ZM_GRAPH_ENTITY_EVENT(name) \
    void On##name(Entity e) override { Emit(#name, { { "Other", e ? (uint64_t)e.GetUUID() : 0ull } }); }
            ZM_GRAPH_ENTITY_EVENT(CollisionBegin) ZM_GRAPH_ENTITY_EVENT(CollisionEnd) ZM_GRAPH_ENTITY_EVENT(TriggerBegin) ZM_GRAPH_ENTITY_EVENT(TriggerEnd) ZM_GRAPH_ENTITY_EVENT(TargetSpotted) ZM_GRAPH_ENTITY_EVENT(TargetLost)
#undef ZM_GRAPH_ENTITY_EVENT
#define ZM_GRAPH_POINTER_EVENT(name) \
    void On##name(const UIPointerEvent& e) override { Emit(#name, { { "Other", e.Target ? (uint64_t)e.Target.GetUUID() : 0ull }, { "Vector", { e.Position.x, e.Position.y } }, { "Bool", e.Inside } }); }
                ZM_GRAPH_POINTER_EVENT(PointerEnter) ZM_GRAPH_POINTER_EVENT(PointerExit) ZM_GRAPH_POINTER_EVENT(PointerDown) ZM_GRAPH_POINTER_EVENT(PointerUp) ZM_GRAPH_POINTER_EVENT(PointerClick)
#undef ZM_GRAPH_POINTER_EVENT
                    void OnValueChanged(float v) override
        {
            Emit("ValueChanged", { { "Number", v } });
        }
        void OnToggleChanged(bool v) override { Emit("ToggleChanged", { { "Bool", v } }); }
        void OnTextChanged(const std::string& v) override { Emit("TextChanged", { { "Text", v } }); }
        void OnTextSubmit(const std::string& v) override { Emit("TextSubmit", { { "Text", v } }); }
        void OnTextEndEdit(const std::string& v) override { Emit("TextEndEdit", { { "Text", v } }); }
        void OnScrollChanged(const glm::vec2& v) override { Emit("ScrollChanged", { { "Vector", { v.x, v.y } } }); }
        void OnPatrolPointReached(int v) override { Emit("PatrolPointReached", { { "Number", v } }); }
        void OnLanguageChanged(const std::string& v) override { Emit("LanguageChanged", { { "Text", v } }); }
        void OnMessage(const ScriptMessage& m) override { Emit("Message", { { "Text", m.Name }, { "Payload", m.Get<std::string>() }, { "Other", m.Sender ? (uint64_t)m.Sender.GetUUID() : 0ull }, { "Number", m.Get<double>() } }); }
        void OnAnimationEvent(const AnimationEventInfo& e) override { Emit("AnimationEvent", { { "Text", e.Name }, { "Number", e.Time } }); }
        void OnSceneLoaded(const SceneEventInfo& e) override { Emit("SceneLoaded", { { "Text", e.Path }, { "Number", e.BuildIndex } }); }
        void OnSceneUnloaded(const SceneEventInfo& e) override { Emit("SceneUnloaded", { { "Text", e.Path }, { "Number", e.BuildIndex } }); }
        void OnActiveSceneChanged(const SceneEventInfo&, const SceneEventInfo& e) override { Emit("ActiveSceneChanged", { { "Text", e.Path } }); }
        void OnSoundFinished(const SoundFinished& e) override { Emit("SoundFinished", { { "Text", e.Clip }, { "Bool", e.Stolen } }); }
        void OnJointBreak(const JointBreak2D& e) override { Emit("JointBreak", { { "Number", e.Force } }); }
        void OnJointBreak3D(const JointBreak3D& e) override { Emit("JointBreak3D", { { "Number", e.Force } }); }
    private:
        Visual::Value Bridge(const std::string&, const std::map<std::string, Visual::Value>&);
        Gameplay::Runtime m_Gameplay;
        bool m_Reported = false;
        uint64_t m_TraversalLink = 0, m_TraversalRevision = 0;
    };
    ScriptClassInfo MakeVisualGraphClassInfo();
}
