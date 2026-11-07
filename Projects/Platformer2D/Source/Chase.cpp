// Scripts of the "Chase2D" scene (M20): village guards that walk their Patrol Path rounds, spot the
// hero with the Sight Sensor on their "eyes" (a child turned the way the guard looks, with a lantern
// showing the view cone), chase it over the nav grid, search where they lost it and go back to the
// round. A guard that catches the hero sends it back to where it started.
#include <Zamin/Zamin.h>

#include <cmath>

using namespace Zamin;

// Qo'riqchi: Patrul (Patrol Path bo'ylab) -> Ta'qib (ko'zi qahramonni ko'rsa) -> Qidiruv (ko'zdan
// yo'qotsa: oxirgi ko'rgan joyiga borib atrofga qaraydi) -> Qaytish (eng yaqin patrul nuqtasiga) ->
// Patrul. Ushlasa - qahramon boshiga qaytadi ("Ushlandi" xabari). Holatlar AI/StateMachine bilan,
// inspektorda (C++ Skript > AI) ko'rinadi.
class ChaseGuard2D : public ScriptableEntity
{
public:
    EntityRef Eyes;            // the child with the Sight Sensor (and the lantern)
    EntityRef Mark;            // a Text child above the head: "!" chasing, "?" searching
    EntityRef Status;          // a UI Text for messages
    float ChaseSpeed = 3.4f;
    float SearchTime = 3.5f;   // s looking round where it lost the hero
    float CatchDistance = 0.6f;
    float TurnSpeed = 7.0f;    // rad / s the eyes turn towards where it goes
    float LookAround = 1.6f;   // rad / s while it stands and looks round
    float WalkFPS = 8.0f;

    void OnStart() override
    {
        if (auto* agent = TryGetComponent<NavAgentComponent>())
            m_PatrolSpeed = agent->Speed;
        if (auto* sprite = TryGetComponent<SpriteRendererComponent>())
            m_BaseColor = sprite->Color;
        if (Entity eyes = Resolve(Eyes))
            m_Facing = eyes.GetComponent<TransformComponent>().Rotation.z;

        m_AI.Name = GetEntity().GetName();
        m_AI.LogTransitions = true;
        m_AI.AddState("Patrul", [this] { Enter(m_PatrolSpeed, "", { 1.0f, 1.0f, 1.0f }); })
            .AddState("Ta'qib",
                      [this] {
                          Enter(ChaseSpeed, "!", { 1.0f, 0.55f, 0.5f });
                          PausePatrol(false);
                          m_Repath = 0.0f;
                          Say("Qo'riqchi sizni ko'rdi - qoching!");
                      },
                      [this](float dt) { Chase(dt); })
            .AddState("Qidiruv",
                      [this] {
                          Enter(m_PatrolSpeed * 1.2f, "?", { 1.0f, 0.85f, 0.55f });
                          SetDestination(GetLastSeenPositionOfEyes());
                      })
            .AddState("Qaytish",
                      [this] {
                          Enter(m_PatrolSpeed, "", { 0.85f, 0.9f, 1.0f });
                          ResumePatrol(true); // to the nearest point of the round
                      })
            .AddState("Ushladi",
                      [this] {
                          Enter(m_PatrolSpeed, "!", { 1.0f, 0.4f, 0.35f });
                          StopMoving();
                          if (Entity hero = SeenTarget())
                              SendMessage(hero, "Ushlandi");
                          Say("Ushlandingiz! Qaytadan urinib ko'ring");
                          ZM_INFO("{} qahramonni ushladi", GetEntity().GetName());
                      })
            .AddTransition("Patrul", "Ta'qib", [this] { return Sees(); })
            .AddTransition("Qaytish", "Ta'qib", [this] { return Sees(); })
            .AddTransition("Qidiruv", "Ta'qib", [this] { return Sees(); })
            .AddTransition("Ta'qib", "Ushladi", [this] { return Caught(); })
            .AddTransition("Ta'qib", "Qidiruv", [this] { return !Sees(); })
            .AddTimedTransition("Qidiruv", "Qaytish", SearchTime)
            .AddTimedTransition("Ushladi", "Qaytish", 1.2f)
            // Back on the round once it reaches one of its points.
            .AddEventTransition("Qaytish", "Patrul", "nuqtada");
        m_AI.Start("Patrul");
        WatchStateMachine(m_AI);
    }

    void OnUpdate(Timestep ts) override
    {
        m_AI.Update(ts);
        UpdateEyes(ts);
        Animate();
        m_SayTimer -= ts;
        if (m_SayTimer <= 0.0f && m_Said)
            Say("");
    }

    void OnPatrolPointReached(int) override { m_AI.SendEvent("nuqtada"); }

    void OnDestroy() override { UnwatchStateMachine(m_AI); }

private:
    const SightSensorComponent* EyesSensor() const
    {
        Entity eyes = Resolve(Eyes);
        return eyes ? eyes.TryGetComponent<SightSensorComponent>() : nullptr;
    }
    bool Sees() const
    {
        const auto* s = EyesSensor();
        return s && s->CanSee;
    }
    Entity SeenTarget() const
    {
        const auto* s = EyesSensor();
        return s && s->SeenTarget ? GetScene().GetEntityByUUID(s->SeenTarget) : Entity();
    }
    glm::vec3 GetLastSeenPositionOfEyes() const
    {
        const auto* s = EyesSensor();
        return s ? s->LastSeenPosition : GetWorldPosition();
    }

    void Enter(float speed, const char* mark, const glm::vec3& tint)
    {
        if (auto* agent = TryGetComponent<NavAgentComponent>())
            agent->Speed = speed;
        if (Entity m = Resolve(Mark); m && m.HasComponent<TextComponent>())
        {
            auto& text = m.GetComponent<TextComponent>();
            text.Text = mark;
            text.Color = mark[0] == '!' ? glm::vec4(1.0f, 0.3f, 0.25f, 1.0f) : glm::vec4(1.0f, 0.85f, 0.3f, 1.0f);
        }
        if (auto* sprite = TryGetComponent<SpriteRendererComponent>())
            sprite->Color = glm::vec4(glm::vec3(m_BaseColor) * tint, m_BaseColor.a);
        // The lantern: brighter and redder while it chases.
        if (Entity eyes = Resolve(Eyes); eyes && eyes.HasComponent<Light2DComponent>())
        {
            auto& light = eyes.GetComponent<Light2DComponent>();
            light.Color = mark[0] == '!' ? glm::vec3(1.0f, 0.45f, 0.35f) : mark[0] == '?' ? glm::vec3(1.0f, 0.8f, 0.45f) : glm::vec3(1.0f, 0.9f, 0.65f);
        }
    }

    void Chase(float dt)
    {
        Entity hero = SeenTarget();
        if (!hero)
            return;
        m_Repath -= dt;
        if (m_Repath <= 0.0f)
        {
            m_Repath = 0.2f;
            SetDestination(glm::vec2(GetScene().GetWorldPosition(hero)));
        }
    }

    bool Caught() const
    {
        Entity hero = SeenTarget();
        return hero && glm::length(glm::vec2(GetScene().GetWorldPosition(hero)) - glm::vec2(GetWorldPosition())) < CatchDistance;
    }

    // The eyes (sight cone and lantern) turn towards where it walks; standing still (waiting at a
    // point, searching) it looks round, back and forth.
    void UpdateEyes(float dt)
    {
        Entity eyes = Resolve(Eyes);
        if (!eyes)
            return;
        const auto* agent = TryGetComponent<NavAgentComponent>();
        const glm::vec2 velocity = agent ? glm::vec2(agent->Velocity) : glm::vec2(0.0f);
        if (glm::length(velocity) > 0.3f)
        {
            m_Facing = Math::MoveTowardsAngle(m_Facing, std::atan2(velocity.y, velocity.x), TurnSpeed * dt);
            m_ScanTime = 0.0f;
        }
        else if (!m_AI.IsInState("Ushladi"))
        {
            m_ScanTime += dt;
            m_Facing += LookAround * dt * (std::fmod(m_ScanTime, 2.4f) < 1.2f ? 1.0f : -1.0f);
        }
        eyes.GetComponent<TransformComponent>().Rotation.z = m_Facing;
    }

    // Walking: the hero sheet's clips by the way it faces (left: mirrored); standing: their first frame.
    void Animate()
    {
        const float c = std::cos(m_Facing), s = std::sin(m_Facing);
        const bool side = std::abs(c) > std::abs(s);
        const char* clip = side ? "HeroSide" : s > 0.0f ? "HeroUp" : "HeroDown";
        const char* sequence = side ? "hero_side" : s > 0.0f ? "hero_up" : "hero_down";
        if (!PlaySpriteClip(clip))
            PlaySpriteSequence(sequence, WalkFPS, true);
        const auto* agent = TryGetComponent<NavAgentComponent>();
        if (auto* anim = TryGetComponent<SpriteAnimationComponent>(); anim && (!agent || glm::length(agent->Velocity) < 0.3f))
        {
            anim->Playing = false;
            anim->Time = 0.0f;
        }
        if (auto* sprite = TryGetComponent<SpriteRendererComponent>())
            sprite->FlipX = side && c < 0.0f;
    }

    void Say(const std::string& text)
    {
        Entity status = Resolve(Status);
        if (!status || !status.HasComponent<TextComponent>())
            return;
        status.GetComponent<TextComponent>().Text = text;
        m_Said = !text.empty();
        m_SayTimer = 2.5f;
    }

    StateMachine m_AI;
    float m_PatrolSpeed = 2.0f;
    float m_Repath = 0.0f;
    float m_Facing = 0.0f;
    float m_ScanTime = 0.0f;
    float m_SayTimer = 0.0f;
    bool m_Said = false;
    glm::vec4 m_BaseColor{ 1.0f };
};
ZM_REGISTER_SCRIPT(ChaseGuard2D, ZM_FIELD(ChaseGuard2D, Eyes).Label("Ko'z").Tooltip("Sight Sensor (va fonar) turgan bola obyekt"),
                   ZM_FIELD(ChaseGuard2D, Mark).Label("Belgi").Tooltip("Bosh ustidagi matn: ! - ta'qib, ? - qidiruv"),
                   ZM_FIELD(ChaseGuard2D, Status).Label("Xabar matni").Tooltip("Ekrandagi UI matn"),
                   ZM_FIELD_RANGE(ChaseGuard2D, ChaseSpeed, 0.5f, 10.0f).Label("Ta'qib tezligi"),
                   ZM_FIELD_RANGE(ChaseGuard2D, SearchTime, 0.0f, 30.0f).Label("Qidiruv vaqti (s)"),
                   ZM_FIELD_RANGE(ChaseGuard2D, CatchDistance, 0.1f, 3.0f).Label("Ushlash masofasi"),
                   ZM_FIELD_RANGE(ChaseGuard2D, TurnSpeed, 0.5f, 30.0f).Label("Burilish tezligi"),
                   ZM_FIELD_RANGE(ChaseGuard2D, LookAround, 0.0f, 10.0f).Label("Atrofga qarash"),
                   ZM_FIELD_RANGE(ChaseGuard2D, WalkFPS, 1.0f, 30.0f).Label("Yurish FPS"))
