// The island's guard (M20): a robot that walks its Patrol Path round in front of the gate, looking
// about at its corners; its Sight Sensor spots the hero, it chases it over the 3D nav grid, searches
// where it lost it and walks back to the round. A catch sends the hero back to the start.
#include <Zamin/Zamin.h>

#include <cmath>

using namespace Zamin;

// Orol qo'riqchisi: Patrul -> (OnTargetSpotted) Ta'qib -> (OnTargetLost) Qidiruv -> (vaqt) Qaytish ->
// (patrul nuqtasiga yetdi) Patrul; ushlasa - Ushladi (qahramonga "Ushlandi" xabari) -> Qaytish.
// Boshidagi chiroq holat rangida: yashil - patrul, qizil - ta'qib, to'q sariq - qidiruv, ko'k - qaytish.
class IslandGuard : public ScriptableEntity
{
public:
    EntityRef Model;          // the child with the robot model (its Animator: Speed / Grounded)
    EntityRef Lamp;           // a glowing sphere (with a point light) on its head
    EntityRef Status;         // a UI text for messages
    float ChaseSpeed = 4.6f;
    float SearchTime = 4.0f;  // s looking round where it lost the hero
    float CatchDistance = 1.3f;
    float LookAround = 1.1f;  // rad / s while it stands and looks round

    void OnStart() override
    {
        if (auto* agent = TryGetComponent<NavAgentComponent>())
            m_PatrolSpeed = agent->Speed;

        m_AI.Name = "Orol qo'riqchisi";
        m_AI.LogTransitions = true;
        m_AI.AddState("Patrul", [this] { Mood({ 0.3f, 0.95f, 0.4f }, m_PatrolSpeed); },
                      [this](float dt) {
                          if (IsWaitingAtPatrolPoint())
                              Look(dt);
                      })
            .AddState("Ta'qib",
                      [this] {
                          Mood({ 1.0f, 0.2f, 0.15f }, ChaseSpeed);
                          PausePatrol(false);
                          m_Repath = 0.0f;
                          Say("Qo'riqchi sizni ko'rdi - yuguring! (Shift)");
                      },
                      [this](float dt) { Chase(dt); })
            .AddState("Qidiruv",
                      [this] {
                          Mood({ 1.0f, 0.6f, 0.12f }, m_PatrolSpeed * 1.2f);
                          SetDestination(GetLastSeenPosition());
                      },
                      [this](float dt) {
                          if (!IsMoving())
                              Look(dt);
                      })
            .AddState("Qaytish",
                      [this] {
                          Mood({ 0.35f, 0.6f, 1.0f }, m_PatrolSpeed);
                          ResumePatrol(true); // the nearest point of its round
                      })
            .AddState("Ushladi",
                      [this] {
                          Mood({ 0.8f, 0.35f, 1.0f }, m_PatrolSpeed);
                          StopMoving();
                          if (Entity hero = GetSeenTarget())
                              SendMessage(hero, "Ushlandi");
                          m_Cooldown = 3.0f;
                          Say("Qo'riqchi sizni ushladi! Boshidan...");
                          ZM_INFO("Orol qo'riqchisi qahramonni ushladi");
                      })
            // The sensor's callbacks send these; after a catch it may still see the hero (no new
            // "spotted" then): the cool-down decides.
            .AddEventTransition("Patrul", "Ta'qib", "ko'rdi")
            .AddEventTransition("Qidiruv", "Ta'qib", "ko'rdi")
            .AddEventTransition("Qaytish", "Ta'qib", "ko'rdi")
            .AddEventTransition("Ta'qib", "Qidiruv", "yo'qotdi")
            .AddTransition("Patrul", "Ta'qib", [this] { return m_Cooldown <= 0.0f && CanSeeTarget(); })
            .AddTransition("Qaytish", "Ta'qib", [this] { return m_Cooldown <= 0.0f && CanSeeTarget(); })
            .AddTransition("Ta'qib", "Ushladi", [this] { return Caught(); })
            .AddTimedTransition("Qidiruv", "Qaytish", SearchTime)
            .AddTimedTransition("Ushladi", "Qaytish", 1.5f)
            .AddEventTransition("Qaytish", "Patrul", "nuqtada");
        m_AI.Start("Patrul");
        WatchStateMachine(m_AI);
    }

    void OnUpdate(Timestep ts) override
    {
        m_Cooldown -= ts;
        m_AI.Update(ts);
        // The model's walk / run blend follows how fast it goes.
        if (Entity model = Resolve(Model))
            if (auto* anim = model.TryGetComponent<AnimatorComponent>())
            {
                const auto* agent = TryGetComponent<NavAgentComponent>();
                anim->SetFloat("Speed", agent ? glm::length(glm::vec2(agent->Velocity.x, agent->Velocity.z)) : 0.0f);
                anim->SetBool("Grounded", true);
            }
        if ((m_SayTimer -= ts) <= 0.0f && m_Said)
            Say("");
    }

    void OnTargetSpotted(Entity) override
    {
        if (m_Cooldown <= 0.0f)
            m_AI.SendEvent("ko'rdi");
    }
    void OnTargetLost(Entity) override { m_AI.SendEvent("yo'qotdi"); }
    void OnPatrolPointReached(int) override { m_AI.SendEvent("nuqtada"); }

    void OnDestroy() override { UnwatchStateMachine(m_AI); }

private:
    void Mood(const glm::vec3& color, float speed)
    {
        if (auto* agent = TryGetComponent<NavAgentComponent>())
            agent->Speed = speed;
        if (Entity lamp = Resolve(Lamp))
        {
            if (auto* mr = lamp.TryGetComponent<MeshRendererComponent>())
            {
                mr->Mat.Albedo = glm::vec4(color, 1.0f);
                mr->Mat.Emission = color;
                mr->Mat.EmissionStrength = 4.0f;
            }
            if (auto* light = lamp.TryGetComponent<PointLightComponent>())
                light->Color = color;
        }
        // The robot glows a little in the state's colour.
        if (Entity model = Resolve(Model))
            if (auto* mr = model.TryGetComponent<MeshRendererComponent>())
            {
                mr->Mat.Emission = color;
                mr->Mat.EmissionStrength = 0.35f;
            }
    }

    // Standing: turns round slowly, a while one way, then the other.
    void Look(float dt)
    {
        m_LookTime += dt;
        Transform().Rotation.y += LookAround * dt * (std::fmod(m_LookTime, 4.0f) < 2.0f ? 1.0f : -1.0f);
    }

    void Chase(float dt)
    {
        Entity hero = GetSeenTarget();
        if (!hero)
            return;
        m_Repath -= dt;
        if (m_Repath <= 0.0f)
        {
            m_Repath = 0.25f;
            SetDestination(GetScene().GetWorldPosition(hero));
        }
    }

    bool Caught() const
    {
        Entity hero = GetSeenTarget();
        if (!hero)
            return false;
        glm::vec3 d = GetScene().GetWorldPosition(hero) - GetWorldPosition();
        d.y = 0.0f;
        return glm::length(d) < CatchDistance;
    }

    void Say(const std::string& text)
    {
        Entity status = Resolve(Status);
        if (!status || !status.HasComponent<TextComponent>())
            return;
        status.GetComponent<TextComponent>().Text = text;
        m_Said = !text.empty();
        m_SayTimer = 3.0f;
    }

    StateMachine m_AI;
    float m_PatrolSpeed = 2.2f;
    float m_Repath = 0.0f;
    float m_Cooldown = 0.0f;
    float m_LookTime = 0.0f;
    float m_SayTimer = 0.0f;
    bool m_Said = false;
};
ZM_REGISTER_SCRIPT(IslandGuard, ZM_FIELD(IslandGuard, Model).Label("Model").Tooltip("Robot modeli turgan bola obyekt (Animator)"),
                   ZM_FIELD(IslandGuard, Lamp).Label("Chiroq").Tooltip("Boshidagi chiroq: holat rangida yonadi"),
                   ZM_FIELD(IslandGuard, Status).Label("Xabar matni").Tooltip("Ekrandagi UI matn"),
                   ZM_FIELD_RANGE(IslandGuard, ChaseSpeed, 0.5f, 12.0f).Label("Ta'qib tezligi"),
                   ZM_FIELD_RANGE(IslandGuard, SearchTime, 0.0f, 30.0f).Label("Qidiruv vaqti (s)"),
                   ZM_FIELD_RANGE(IslandGuard, CatchDistance, 0.2f, 5.0f).Label("Ushlash masofasi"),
                   ZM_FIELD_RANGE(IslandGuard, LookAround, 0.0f, 10.0f).Label("Atrofga qarash"))
