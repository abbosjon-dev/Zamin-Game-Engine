// Scripts of the "Squad3D" scene (M20): a squad that keeps a formation round its commander, the
// commander's mind a behavior tree (AI/BehaviorTree.h) - patrol, chase on sight, encircle where
// the intruder was last seen - and a caravan escorted by a ring of guards.
#include <Zamin/Zamin.h>

#include <cmath>

using namespace Zamin;

namespace {

    void Paint(Entity entity, const glm::vec3& color, float glow)
    {
        if (!entity || !entity.HasComponent<MeshRendererComponent>())
            return;
        Material& m = entity.GetComponent<MeshRendererComponent>().Mat;
        m.Albedo = glm::vec4(color, 1.0f);
        m.Emission = color;
        m.EmissionStrength = glow;
    }

    const char* ShapeName(FormationShape shape)
    {
        switch (shape)
        {
            case FormationShape::Line: return "qator";
            case FormationShape::Column: return "kolonna";
            case FormationShape::Wedge: return "pona";
            case FormationShape::Circle: return "doira";
        }
        return "?";
    }

}

// The squad's commander: a Nav Agent walking a Patrol Path, a Formation (its soldiers: the entities
// tagged "Askar") and a Sight Sensor. Its behavior tree, ticked every frame:
//
//   ReactiveSelector "Komandir"
//     ReactiveSequence "Hujum"     - sees the intruder: the patrol pauses, the squad runs after it in
//       Condition / Action           a wedge;
//     ReactiveSequence "Qidiruv"   - lost it but remembers where (the sensor's MemoryTime): the squad
//       Condition / Action           goes there and encircles the spot;
//     Sequence "Patrul"            - back to the round (the nearest point) in a column; at each point
//       Action / Action              reached the shape changes: column, wedge, line ...
//
// The reactive nodes ask their conditions every tick: a sighting interrupts the patrol at once. The
// inspector shows the tree while playing (WatchBehaviorTree); the lamp takes the branch's colour.
class SquadCommander : public ScriptableEntity
{
public:
    EntityRef Lamp;
    float ChaseSpeed = 2.6f;
    float CatchDistance = 1.5f;

    void OnStart() override
    {
        if (HasComponent<NavAgentComponent>())
            m_PatrolSpeed = GetComponent<NavAgentComponent>().Speed;
        m_Tree.Name = "Komandir";
        m_Tree.LogChanges = true;
        BehaviorTreeBuilder()
            .ReactiveSelector("Komandir")
                .ReactiveSequence("Hujum")
                    .Condition("Ko'ryaptimi", [this] { return CanSeeTarget(); })
                    .Action("Ta'qib", [this](BTContext& c) { return Chase(c); })
                .End()
                .ReactiveSequence("Qidiruv")
                    .Condition("Esidami", [this] { return RemembersTarget(); })
                    .Action("O'rab olish", [this](BTContext& c) { return Encircle(c); })
                .End()
                .Sequence("Patrul")
                    .Action("Saf tortish", [this](BTContext& c) { return Regroup(c); })
                    .Action("Yurish", [this] { return IsPatrolling() ? BTStatus::Running : BTStatus::Failure; })
                .End()
            .End()
            .Build(m_Tree);
        WatchBehaviorTree(m_Tree);
    }

    void OnUpdate(Timestep ts) override { m_Tree.Tick(ts); }

    // On the round: a new shape at every point.
    void OnPatrolPointReached(int index) override
    {
        if (m_Mode != Mode::Patrol)
            return;
        static constexpr FormationShape s_Order[] = { FormationShape::Column, FormationShape::Wedge, FormationShape::Line };
        m_Shape = (m_Shape + 1) % 3;
        SetFormationShape(s_Order[m_Shape]);
        m_Tree.GetBlackboard().Set("Nuqta", index);
        m_Tree.GetBlackboard().Set("Shakl", ShapeName(s_Order[m_Shape]));
    }

    void OnDestroy() override { UnwatchBehaviorTree(m_Tree); }

private:
    enum class Mode { None, Patrol, Chase, Search };

    void Enter(Mode mode, BTContext& c, FormationShape shape, const glm::vec3& color, float speed)
    {
        m_Mode = mode;
        SetFormationShape(shape);
        Paint(Resolve(Lamp), color, 4.0f);
        if (HasComponent<NavAgentComponent>())
            GetComponent<NavAgentComponent>().Speed = speed;
        c.Board.Set("Shakl", ShapeName(shape));
        m_Repath = 0.0f;
    }

    BTStatus Chase(BTContext& c)
    {
        if (m_Mode != Mode::Chase)
        {
            PausePatrol(false);
            Enter(Mode::Chase, c, FormationShape::Wedge, { 1.0f, 0.22f, 0.18f }, ChaseSpeed);
            c.Board.Set("Ko'rdi", c.Board.Get<int>("Ko'rdi") + 1);
        }
        Entity target = GetSeenTarget();
        if (!target)
            return BTStatus::Running;
        const glm::vec3 at = GetScene().GetWorldPosition(target);
        c.Board.Set("Nishon", at);
        m_Repath -= c.DeltaTime;
        if (m_Repath <= 0.0f)
        {
            m_Repath = 0.3f;
            SetDestination(at);
        }
        glm::vec3 d = at - GetWorldPosition();
        d.y = 0.0f;
        if (glm::length(d) < CatchDistance && !c.Board.Get<bool>("Yetdi"))
        {
            c.Board.Set("Yetdi", true);
            ZM_INFO("Otryad mehmonga yetib oldi!");
        }
        return BTStatus::Running;
    }

    BTStatus Encircle(BTContext& c)
    {
        if (m_Mode != Mode::Search)
        {
            Enter(Mode::Search, c, FormationShape::Circle, { 1.0f, 0.62f, 0.15f }, m_PatrolSpeed);
            SetDestination(GetLastSeenPosition());
            c.Board.Set("Yetdi", false);
        }
        return BTStatus::Running; // until it forgets (the condition above then fails)
    }

    BTStatus Regroup(BTContext& c)
    {
        if (m_Mode != Mode::Patrol)
        {
            Enter(Mode::Patrol, c, FormationShape::Column, { 0.3f, 0.85f, 0.4f }, m_PatrolSpeed);
            m_Shape = 0;
            if (m_Started)
                ResumePatrol(true);
            m_Started = true;
        }
        return BTStatus::Success;
    }

    BehaviorTree m_Tree;
    Mode m_Mode = Mode::None;
    int m_Shape = 0;
    bool m_Started = false;
    float m_PatrolSpeed = 1.8f;
    float m_Repath = 0.0f;
};
ZM_REGISTER_SCRIPT(SquadCommander, ZM_FIELD(SquadCommander, Lamp).Label("Chiroq"),
                   ZM_FIELD_RANGE(SquadCommander, ChaseSpeed, 0.5f, 10.0f).Label("Ta'qib tezligi"),
                   ZM_FIELD_RANGE(SquadCommander, CatchDistance, 0.2f, 5.0f).Label("Yetish masofasi"))
