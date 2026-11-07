// Scripts of the "AI3D" scene (M20): a sliding door that carves the nav grid while it is shut (Nav
// Obstacle), a guard driven by an AI state machine and its Sight Sensor, a security camera that
// tracks what it sees, porters and a visitor walking on the grid, and a flock of sheep moved by
// steering behaviours (wander, separation, alignment, cohesion, flee, arrive).
#include <Zamin/Zamin.h>

#include <cmath>

using namespace Zamin;

namespace {

    // The yaw (radians, about +Y) that turns an entity's -Z towards `direction`.
    float YawTowards(const glm::vec3& direction) { return std::atan2(-direction.x, -direction.z); }

    // Colours a mesh (and lets it glow a little).
    void Paint(Entity entity, const glm::vec3& color, float glow)
    {
        if (!entity || !entity.HasComponent<MeshRendererComponent>())
            return;
        Material& m = entity.GetComponent<MeshRendererComponent>().Mat;
        m.Albedo = glm::vec4(color, 1.0f);
        m.Emission = color;
        m.EmissionStrength = glow;
    }

}

// A door (kinematic body + Nav Obstacle) that slides by OpenOffset and back, waiting at each end.
// While it is shut it carves the grid cells of the doorway: agents whose path went through it find
// another one at once.
class SlidingDoor : public ScriptableEntity
{
public:
    glm::vec3 OpenOffset{ -3.0f, 0.0f, 0.0f };
    float MoveTime = 1.2f;    // s to open / shut
    float OpenTime = 5.0f;    // s it stays open
    float ClosedTime = 5.0f;  // s it stays shut
    bool StartOpen = true;

    void OnStart() override
    {
        m_Closed = Transform().Translation;
        m_T = m_Target = StartOpen ? 1.0f : 0.0f;
        m_Wait = StartOpen ? OpenTime : ClosedTime;
        Apply();
    }

    void OnUpdate(Timestep ts) override
    {
        if (m_T == m_Target)
        {
            m_Wait -= ts;
            if (m_Wait <= 0.0f)
                m_Target = 1.0f - m_Target;
            return;
        }
        m_T = Math::MoveTowards(m_T, m_Target, ts / std::max(MoveTime, 0.01f));
        if (m_T == m_Target)
        {
            m_Wait = m_Target > 0.5f ? OpenTime : ClosedTime;
            ZM_INFO("Eshik {}", m_Target > 0.5f ? "ochildi" : "yopildi");
        }
        Apply();
    }

    bool IsShut() const { return m_T < 0.01f; }

private:
    void Apply() { Transform().Translation = m_Closed + OpenOffset * Math::SmoothStep(0.0f, 1.0f, m_T); }

    glm::vec3 m_Closed{ 0.0f };
    float m_T = 1.0f, m_Target = 1.0f, m_Wait = 0.0f;
};
ZM_REGISTER_SCRIPT(SlidingDoor, ZM_FIELD(SlidingDoor, OpenOffset).Label("Ochiq holatda siljish"),
                   ZM_FIELD_RANGE(SlidingDoor, MoveTime, 0.05f, 10.0f).Label("Harakat vaqti (s)"),
                   ZM_FIELD_RANGE(SlidingDoor, OpenTime, 0.0f, 60.0f).Label("Ochiq turadi (s)"),
                   ZM_FIELD_RANGE(SlidingDoor, ClosedTime, 0.0f, 60.0f).Label("Yopiq turadi (s)"),
                   ZM_FIELD(SlidingDoor, StartOpen).Label("Ochiq boshlanadi"))

// Moves between its start and start + Offset, waiting at each end. With a Nav Obstacle set to
// "Faqat turganda" it carves the grid only while parked; on the way the agents' feelers steer round it.
class PingPongMover : public ScriptableEntity
{
public:
    glm::vec3 Offset{ 8.0f, 0.0f, 0.0f };
    float Speed = 1.6f;   // units / s
    float Wait = 2.5f;    // s at each end

    void OnStart() override { m_Start = Transform().Translation; }

    void OnUpdate(Timestep ts) override
    {
        if (m_Wait > 0.0f)
        {
            m_Wait -= ts;
            return;
        }
        const float length = std::max(glm::length(Offset), 0.01f);
        m_T = Math::MoveTowards(m_T, m_Forward ? 1.0f : 0.0f, Speed * ts / length);
        if (m_T == (m_Forward ? 1.0f : 0.0f))
        {
            m_Forward = !m_Forward;
            m_Wait = Wait;
        }
        Transform().Translation = m_Start + Offset * m_T;
    }

private:
    glm::vec3 m_Start{ 0.0f };
    float m_T = 0.0f, m_Wait = 0.0f;
    bool m_Forward = true;
};
ZM_REGISTER_SCRIPT(PingPongMover, ZM_FIELD(PingPongMover, Offset).Label("Siljish"),
                   ZM_FIELD_RANGE(PingPongMover, Speed, 0.1f, 20.0f).Label("Tezlik"),
                   ZM_FIELD_RANGE(PingPongMover, Wait, 0.0f, 30.0f).Label("Kutish (s)"))

// A Nav Agent walking to and fro between two points (through the door while it is open, round by
// the far passage while it is shut).
class Shuttle : public ScriptableEntity
{
public:
    glm::vec3 PointA{ 0.0f };
    glm::vec3 PointB{ 0.0f };
    float Wait = 1.0f;

    void OnStart() override { SetDestination(PointB); }

    void OnUpdate(Timestep ts) override
    {
        if (IsMoving())
            return;
        m_Timer += ts;
        if (m_Timer < Wait)
            return;
        m_Timer = 0.0f;
        m_ToB = !m_ToB;
        SetDestination(m_ToB ? PointB : PointA);
    }

private:
    float m_Timer = 0.0f;
    bool m_ToB = true;
};
ZM_REGISTER_SCRIPT(Shuttle, ZM_FIELD(Shuttle, PointA).Label("A nuqta"), ZM_FIELD(Shuttle, PointB).Label("B nuqta"),
                   ZM_FIELD_RANGE(Shuttle, Wait, 0.0f, 30.0f).Label("Kutish (s)"))

// The visitor (tagged "Player"): walks to random reachable points of the area; WASD / arrows take
// over (the Nav Agent then walks where the keys point).
class Visitor : public ScriptableEntity
{
public:
    glm::vec2 AreaMin{ -12.0f, -9.0f }; // XZ
    glm::vec2 AreaMax{ 12.0f, 9.0f };
    float PauseMin = 0.3f, PauseMax = 1.5f;
    float StartDelay = 0.0f; // s it stands still first
    int Seed = 11;

    void OnStart() override
    {
        m_Random.SetSeed((uint64_t)Seed);
        m_Pause = StartDelay;
        if (StartDelay <= 0.0f)
            PickDestination();
    }

    void OnUpdate(Timestep ts) override
    {
        glm::vec2 keys(0.0f);
        if (Input::IsKeyPressed(Key::W) || Input::IsKeyPressed(Key::Up)) keys.y -= 1.0f;
        if (Input::IsKeyPressed(Key::S) || Input::IsKeyPressed(Key::Down)) keys.y += 1.0f;
        if (Input::IsKeyPressed(Key::A) || Input::IsKeyPressed(Key::Left)) keys.x -= 1.0f;
        if (Input::IsKeyPressed(Key::D) || Input::IsKeyPressed(Key::Right)) keys.x += 1.0f;
        if (glm::dot(keys, keys) > 0.0f)
        {
            keys = glm::normalize(keys);
            SetDestination(GetWorldPosition() + glm::vec3(keys.x, 0.0f, keys.y) * 1.5f);
            m_Pause = PauseMax;
            return;
        }
        if (IsMoving())
            return;
        m_Pause -= ts;
        if (m_Pause <= 0.0f)
            PickDestination();
    }

private:
    void PickDestination()
    {
        // A few tries: points inside walls or pillars have no path.
        for (int attempt = 0; attempt < 8; attempt++)
        {
            const glm::vec3 p(m_Random.Range(AreaMin.x, AreaMax.x), 0.0f, m_Random.Range(AreaMin.y, AreaMax.y));
            if (SetDestination(p))
                break;
        }
        m_Pause = m_Random.Range(PauseMin, PauseMax);
    }

    RandomGenerator m_Random{ 11 };
    float m_Pause = 0.0f;
};
ZM_REGISTER_SCRIPT(Visitor, ZM_FIELD(Visitor, AreaMin).Label("Maydon (min XZ)"), ZM_FIELD(Visitor, AreaMax).Label("Maydon (max XZ)"),
                   ZM_FIELD(Visitor, PauseMin).Label("Pauza (min, s)"), ZM_FIELD(Visitor, PauseMax).Label("Pauza (max, s)"),
                   ZM_FIELD(Visitor, StartDelay).Label("Boshida kutish (s)"), ZM_FIELD(Visitor, Seed).Label("Urug' (seed)"))

// The guard: an AI state machine on top of its Nav Agent and Sight Sensor.
//   Patrul  - walks the Route's points (its children), waiting a moment at each;
//   Ta'qib  - the sensor spotted the visitor (OnTargetSpotted): runs after it;
//   Qidiruv - lost sight of it (OnTargetLost): goes where it was last seen and looks around;
//             back to Patrul after SearchTime, to Ta'qib when it sees it again;
//   Ushladi - got within CatchDistance: stands still a moment, then patrols again (and ignores the
//             visitor for a while).
// The inspector shows the state while playing (WatchStateMachine); the lamp above its head and its
// body take the state's colour.
class GuardBrain : public ScriptableEntity
{
public:
    EntityRef Route;
    EntityRef Body;
    EntityRef Lamp;
    float WaitTime = 1.0f;
    float ChaseSpeed = 4.2f;
    float SearchTime = 4.0f;
    float CatchDistance = 1.3f;

    void OnStart() override
    {
        if (Entity route = Resolve(Route))
            for (Entity point : GetScene().GetChildren(route))
                m_Points.push_back(GetScene().GetWorldPosition(point));
        if (HasComponent<NavAgentComponent>())
            m_PatrolSpeed = GetComponent<NavAgentComponent>().Speed;

        m_AI.Name = "Qorovul";
        m_AI.LogTransitions = true;
        m_AI.AddState("Patrul", [this] { SetMood({ 0.3f, 0.85f, 0.4f }, m_PatrolSpeed); GoToPoint(); }, [this](float dt) { Patrol(dt); })
            .AddState("Ta'qib", [this] { SetMood({ 1.0f, 0.22f, 0.18f }, ChaseSpeed); m_Repath = 0.0f; }, [this](float dt) { Chase(dt); })
            .AddState("Qidiruv",
                      [this] {
                          SetMood({ 1.0f, 0.62f, 0.15f }, m_PatrolSpeed);
                          SetDestination(GetLastSeenPosition());
                      },
                      [this](float dt) { Search(dt); })
            .AddState("Ushladi",
                      [this] {
                          SetMood({ 0.75f, 0.35f, 1.0f }, m_PatrolSpeed);
                          StopMoving();
                          ZM_INFO("Qorovul mehmonni ushladi!");
                      })
            // The sensor's callbacks send these (any state: a new sighting while searching too).
            .AddEventTransition("Patrul", "Ta'qib", "ko'rdi")
            .AddEventTransition("Qidiruv", "Ta'qib", "ko'rdi")
            .AddEventTransition("Ta'qib", "Qidiruv", "yo'qotdi")
            // Still in view after a catch (no new "spotted" comes then): the cool-down decides.
            .AddTransition("Patrul", "Ta'qib", [this] { return m_Cooldown <= 0.0f && CanSeeTarget(); })
            .AddTransition("Ta'qib", "Ushladi", [this] { return Caught(); })
            .AddTimedTransition("Qidiruv", "Patrul", SearchTime)
            .AddTimedTransition("Ushladi", "Patrul", 2.0f);
        m_AI.Start("Patrul");
        WatchStateMachine(m_AI);
    }

    void OnUpdate(Timestep ts) override
    {
        m_Cooldown -= ts;
        m_AI.Update(ts);
    }

    void OnTargetSpotted(Entity) override
    {
        if (m_Cooldown <= 0.0f)
            m_AI.SendEvent("ko'rdi");
    }
    void OnTargetLost(Entity) override { m_AI.SendEvent("yo'qotdi"); }

    void OnDestroy() override { UnwatchStateMachine(m_AI); }

private:
    void SetMood(const glm::vec3& color, float speed)
    {
        Paint(Resolve(Body), color, 0.15f);
        Paint(Resolve(Lamp), color, 4.0f);
        if (HasComponent<NavAgentComponent>())
            GetComponent<NavAgentComponent>().Speed = speed;
        m_Wait = 0.0f;
        if (m_AI.GetPreviousStateName() == "Ushladi")
            m_Cooldown = 4.0f;
    }

    void GoToPoint()
    {
        if (!m_Points.empty())
            SetDestination(m_Points[m_Next % m_Points.size()]);
    }

    void Patrol(float dt)
    {
        if (m_Points.empty() || IsMoving())
            return;
        // A look round at each point, then on to the next one.
        m_Wait += dt;
        Transform().Rotation.y += dt * 1.5f;
        if (m_Wait < WaitTime)
            return;
        m_Wait = 0.0f;
        m_Next = (m_Next + 1) % m_Points.size();
        GoToPoint();
    }

    void Chase(float dt)
    {
        Entity target = GetSeenTarget();
        if (!target)
            return;
        m_Repath -= dt;
        if (m_Repath <= 0.0f)
        {
            m_Repath = 0.25f;
            SetDestination(GetScene().GetWorldPosition(target));
        }
    }

    void Search(float dt)
    {
        // At the spot: turns round slowly.
        if (!IsMoving())
            Transform().Rotation.y += dt * 2.0f;
    }

    bool Caught()
    {
        Entity target = GetSeenTarget();
        if (!target)
            return false;
        glm::vec3 d = GetScene().GetWorldPosition(target) - GetWorldPosition();
        d.y = 0.0f;
        return glm::length(d) < CatchDistance;
    }

    StateMachine m_AI;
    std::vector<glm::vec3> m_Points;
    size_t m_Next = 0;
    float m_PatrolSpeed = 2.5f;
    float m_Wait = 0.0f;
    float m_Repath = 0.0f;
    float m_Cooldown = 0.0f;
};
ZM_REGISTER_SCRIPT(GuardBrain, ZM_FIELD(GuardBrain, Route).Label("Patrul yo'li").Tooltip("Bolalari - patrul nuqtalari"),
                   ZM_FIELD(GuardBrain, Body).Label("Tana"), ZM_FIELD(GuardBrain, Lamp).Label("Chiroq"),
                   ZM_FIELD_RANGE(GuardBrain, WaitTime, 0.0f, 10.0f).Label("Nuqtada kutish (s)"),
                   ZM_FIELD_RANGE(GuardBrain, ChaseSpeed, 0.5f, 10.0f).Label("Ta'qib tezligi"),
                   ZM_FIELD_RANGE(GuardBrain, SearchTime, 0.0f, 30.0f).Label("Qidiruv vaqti (s)"),
                   ZM_FIELD_RANGE(GuardBrain, CatchDistance, 0.2f, 5.0f).Label("Ushlash masofasi"))

// A security camera: sweeps left and right; when its Sight Sensor spots someone it turns its lamp
// red and follows them (OnTargetSpotted / OnTargetLost), then goes back to sweeping.
class SecurityCamera : public ScriptableEntity
{
public:
    EntityRef Lamp;
    float SweepAngle = 60.0f;  // degrees to each side
    float SweepSpeed = 0.5f;   // sweeps / s (radians of the phase)
    float TurnSpeed = 120.0f;  // degrees / s while it follows a target

    void OnStart() override
    {
        m_BaseYaw = Transform().Rotation.y;
        Paint(Resolve(Lamp), { 0.3f, 1.0f, 0.45f }, 4.0f);
    }

    void OnUpdate(Timestep ts) override
    {
        float& yaw = Transform().Rotation.y;
        if (Entity target = m_Following ? GetSeenTarget() : Entity())
        {
            const glm::vec3 to = GetScene().GetWorldPosition(target) - GetWorldPosition();
            yaw = Math::MoveTowardsAngle(yaw, YawTowards(to), glm::radians(TurnSpeed) * ts);
            return;
        }
        // Back onto the sweep where it is now (no jump).
        m_Phase += SweepSpeed * ts;
        const float goal = m_BaseYaw + glm::radians(SweepAngle) * std::sin(m_Phase);
        yaw = Math::MoveTowardsAngle(yaw, goal, glm::radians(TurnSpeed) * ts);
    }

    void OnTargetSpotted(Entity target) override
    {
        m_Following = true;
        Paint(Resolve(Lamp), { 1.0f, 0.18f, 0.12f }, 6.0f);
        ZM_INFO("Kamera {} ni ko'rdi", target ? target.GetName() : std::string("?"));
    }

    void OnTargetLost(Entity) override
    {
        m_Following = false;
        Paint(Resolve(Lamp), { 0.3f, 1.0f, 0.45f }, 4.0f);
    }

private:
    float m_BaseYaw = 0.0f;
    float m_Phase = 0.0f;
    bool m_Following = false;
};
ZM_REGISTER_SCRIPT(SecurityCamera, ZM_FIELD(SecurityCamera, Lamp).Label("Chiroq"),
                   ZM_FIELD_RANGE(SecurityCamera, SweepAngle, 0.0f, 180.0f).Label("Burilish burchagi"),
                   ZM_FIELD_RANGE(SecurityCamera, SweepSpeed, 0.0f, 5.0f).Label("Burilish tezligi"),
                   ZM_FIELD_RANGE(SecurityCamera, TurnSpeed, 1.0f, 720.0f).Label("Kuzatish tezligi"))

// A flock of sheep: every child is one. Each frame they steer (AI/Steering.h) with prioritised
// blending: flee from the visitor when it comes near, keep apart, stay in the pen (arrive at its
// centre when they stray), go the way their neighbours go, keep together and wander.
class SheepFlock : public ScriptableEntity
{
public:
    float MaxSpeed = 2.0f;
    float MaxForce = 5.0f;
    float NeighbourRadius = 2.5f;
    float SeparationRadius = 0.9f;
    float PenRadius = 3.2f;
    float PanicDistance = 3.0f;
    std::string ThreatTag = "Player";

    void OnStart() override
    {
        uint64_t seed = 1;
        for (Entity sheep : GetChildren())
        {
            Sheep s;
            s.Body = sheep;
            s.Position = glm::vec2(sheep.GetComponent<TransformComponent>().Translation.x, sheep.GetComponent<TransformComponent>().Translation.z);
            s.Wander.Reset(seed++ * 7919);
            m_Sheep.push_back(s);
        }
    }

    void OnUpdate(Timestep ts) override
    {
        const float dt = std::min((float)ts, 0.05f);
        if (dt <= 0.0f || m_Sheep.empty())
            return;
        const glm::vec3 home3 = GetWorldPosition();
        // The threat in the pen's frame (the pen is not rotated or scaled).
        bool threatened = false;
        glm::vec2 threat(0.0f), threatVelocity(0.0f);
        if (Entity t = FindEntityWithTag(ThreatTag))
        {
            const glm::vec3 p = GetScene().GetWorldPosition(t) - home3;
            threat = { p.x, p.z };
            threatened = true;
            if (const auto* agent = t.TryGetComponent<NavAgentComponent>())
                threatVelocity = { agent->Velocity.x, agent->Velocity.z };
        }

        Steering::WanderSettings wander;
        wander.Distance = 1.2f;
        wander.Radius = 0.6f;
        wander.Jitter = 4.0f;
        for (Sheep& s : m_Sheep)
        {
            m_Positions.clear();
            m_Velocities.clear();
            for (const Sheep& other : m_Sheep)
                if (&other != &s && glm::length(other.Position - s.Position) < NeighbourRadius)
                {
                    m_Positions.push_back(other.Position);
                    m_Velocities.push_back(other.Velocity);
                }
            glm::vec2 force(0.0f);
            if (threatened)
                Steering::Accumulate(force, Steering::Evade<2>(s.Position, s.Velocity, threat, threatVelocity, MaxSpeed, 0.5f, PanicDistance) * 3.0f, MaxForce);
            Steering::Accumulate(force, Steering::Separation<2>(s.Position, m_Positions, SeparationRadius, MaxSpeed) * 2.0f, MaxForce);
            if (glm::length(s.Position) > PenRadius)
                Steering::Accumulate(force, Steering::Arrive<2>(s.Position, s.Velocity, glm::vec2(0.0f), MaxSpeed * 0.6f, 1.5f), MaxForce);
            Steering::Accumulate(force, Steering::Alignment<2>(s.Velocity, m_Velocities) * 0.5f, MaxForce);
            Steering::Accumulate(force, Steering::Cohesion<2>(s.Position, s.Velocity, m_Positions, MaxSpeed * 0.5f) * 0.4f, MaxForce);
            Steering::Accumulate(force, Steering::Wander<2>(s.Wander, s.Position, s.Velocity, MaxSpeed * 0.4f, dt, wander) * 0.5f, MaxForce);
            Steering::Integrate<2>(s.Position, s.Velocity, force, MaxForce, MaxSpeed, dt);
        }

        for (const Sheep& s : m_Sheep)
        {
            Entity body = s.Body;
            if (!body.IsValid())
                continue;
            auto& t = body.GetComponent<TransformComponent>();
            t.Translation.x = s.Position.x;
            t.Translation.z = s.Position.y;
            if (glm::length(s.Velocity) > 0.15f)
                t.Rotation.y = Math::MoveTowardsAngle(t.Rotation.y, YawTowards({ s.Velocity.x, 0.0f, s.Velocity.y }), 6.0f * dt);
        }
    }

private:
    struct Sheep
    {
        Entity Body;
        glm::vec2 Position{ 0.0f };
        glm::vec2 Velocity{ 0.0f };
        Steering::WanderState2D Wander;
    };
    std::vector<Sheep> m_Sheep;
    std::vector<glm::vec2> m_Positions, m_Velocities;
};
ZM_REGISTER_SCRIPT(SheepFlock, ZM_FIELD_RANGE(SheepFlock, MaxSpeed, 0.1f, 10.0f).Label("Eng katta tezlik"),
                   ZM_FIELD_RANGE(SheepFlock, MaxForce, 0.1f, 50.0f).Label("Eng katta kuch"),
                   ZM_FIELD_RANGE(SheepFlock, NeighbourRadius, 0.1f, 10.0f).Label("Qo'shnilar radiusi").Header("Suruv"),
                   ZM_FIELD_RANGE(SheepFlock, SeparationRadius, 0.1f, 5.0f).Label("Ajralish radiusi"),
                   ZM_FIELD_RANGE(SheepFlock, PenRadius, 0.5f, 20.0f).Label("Qo'ra radiusi"),
                   ZM_FIELD_RANGE(SheepFlock, PanicDistance, 0.0f, 20.0f).Label("Qo'rqish masofasi").Header("Xavf"),
                   ZM_FIELD(SheepFlock, ThreatTag).Label("Xavf tegi"))
