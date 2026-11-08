#include "Arena.h"

#include <algorithm>
#include <cmath>

using namespace Zamin;

// Nishonlar. Turg'un va sirpanuvchi nishon urilganda orqaga yiqiladi (ilmoq - "Hinge" - X o'qi atrofida -90
// gradusgacha buriladi) va RespawnTime soniyadan keyin qayta turadi; chiquvchi nishon yotgan holda kutadi, vaqti-
// vaqti bilan bir necha soniyaga turadi; dron aylana bo'ylab uchadi, urilganda yorilib yo'qoladi va keyinroq
// qaytadi. Urilgan joy markazga qanchalik yaqin bo'lsa, ochko shuncha ko'p (markaz - ikki baravar).
// Yuz ("Face") - kinematik jism: harakatlanganda fizika uni o'zi kuzatadi, nur unga tegadi.

static constexpr float DownAngle = -glm::half_pi<float>();

void Target::OnCreate()
{
    m_Hinge = Resolve(Hinge);
    m_Face = Resolve(Face);
    m_Start = Transform().Translation;
    m_Rng.SetSeed((uint64_t)GetEntity().GetUUID() ^ 0x5eed);
    m_Phase = m_Rng.Range(0.0f, glm::two_pi<float>());
    if (m_Face)
        if (auto* mr = m_Face.TryGetComponent<MeshRendererComponent>())
        {
            m_BaseEmission = mr->Mat.Emission;
            m_BaseEmissionStrength = mr->Mat.EmissionStrength;
        }
    ResetTarget(false);
}

void Target::ResetTarget(bool rangeMode)
{
    m_RespawnScale = rangeMode ? 0.45f : 1.0f;
    m_Flash = 0.0f;
    SetFaceGlow(m_BaseEmissionStrength, m_BaseEmission);
    if (m_Face && !m_Face.IsActive())
        m_Face.SetActive(true);
    if (m_Face && GetKind() == TargetKind::Drone)
        m_Face.GetComponent<TransformComponent>().Scale = glm::vec3(1.0f);
    if (GetKind() == TargetKind::PopUp)
    {
        // lying down, up at a random moment
        m_State = State::Down;
        m_Angle = DownAngle;
        m_Timer = 0.0f;
        m_DownWait = m_Rng.Range(0.5f, 3.0f) * m_RespawnScale;
    }
    else
    {
        m_State = State::Up;
        m_Angle = 0.0f;
        m_Timer = 0.0f;
    }
    if (m_Hinge)
        m_Hinge.GetComponent<TransformComponent>().Rotation.x = m_Angle;
}

bool Target::Owns(Entity entity) const
{
    return entity && entity == m_Face;
}

glm::vec3 Target::GetAimPoint() const
{
    if (!m_Face)
        return m_Start;
    // a little ahead of a moving target (where it will be when the shot... is instant - this is for the bot's turn)
    return GetScene().GetWorldPosition(m_Face) + m_Velocity * 0.08f;
}

void Target::SetFaceGlow(float strength, const glm::vec3& color)
{
    if (!m_Face)
        return;
    if (auto* mr = m_Face.TryGetComponent<MeshRendererComponent>())
    {
        mr->Mat.Emission = color;
        mr->Mat.EmissionStrength = strength;
    }
}

int Target::Hit(const glm::vec3& point, const glm::vec3& direction, bool& bullseye)
{
    bullseye = false;
    if (m_State != State::Up || !m_Face)
        return 0;
    int points = Points;
    if (GetKind() != TargetKind::Drone)
    {
        // how far from the centre, in the face's plane (its normal: the cylinder's axis, local +Y)
        const glm::mat4 world = GetScene().GetWorldTransform(m_Face);
        const glm::vec3 center(world[3]);
        const glm::vec3 axis(world[1]);
        const float radius = std::max(0.01f, glm::length(axis) > 0.0f ? glm::length(glm::vec3(world[0])) * 0.5f : 0.5f);
        const glm::vec3 normal = glm::normalize(axis);
        glm::vec3 offset = point - center;
        offset -= normal * glm::dot(offset, normal);
        const float r = glm::length(offset) / radius;
        if (r < 0.24f)
        {
            bullseye = true;
            points *= 2;
        }
        else if (r > 0.62f)
            points /= 2;
    }
    Knock(direction);
    return points;
}

void Target::Knock(const glm::vec3&)
{
    m_Flash = 1.0f;
    m_State = State::Falling;
    m_Timer = 0.0f;
    if (GetKind() == TargetKind::Drone && m_Face)
    {
        // bursts: gone at once (the weapon throws the sparks), back after a while
        m_Face.SetActive(false);
        m_State = State::Down;
        m_Timer = 0.0f;
        m_DownWait = RespawnTime * m_RespawnScale;
        PlaySoundAt("Audio/drone_burst.wav", GetScene().GetWorldPosition(GetEntity()), 0.9f, m_Rng.Range(0.92f, 1.08f));
        return;
    }
    PlaySoundAt("Audio/target_fall.wav", GetScene().GetWorldPosition(GetEntity()), 0.7f, m_Rng.Range(0.9f, 1.1f));
}

void Target::Raise()
{
    m_State = State::Rising;
    m_Timer = 0.0f;
    if (GetKind() == TargetKind::Drone && m_Face)
    {
        m_Face.SetActive(true);
        m_Face.GetComponent<TransformComponent>().Scale = glm::vec3(0.01f);
    }
    PlaySoundAt("Audio/target_up.wav", GetScene().GetWorldPosition(GetEntity()), 0.45f, m_Rng.Range(0.95f, 1.1f));
}

void Target::OnUpdate(Timestep ts)
{
    const float dt = ts;
    if (dt <= 0.0f)
        return;
    const glm::vec3 facePos = m_Face ? GetScene().GetWorldPosition(m_Face) : m_Start;

    // ---- motion
    auto& t = Transform();
    switch (GetKind())
    {
        case TargetKind::Slider:
        {
            // back and forth along the rail, easing at the ends
            const float length = std::max(0.1f, glm::distance(m_Start, PointB));
            m_Phase += dt * Speed / length * glm::pi<float>() * 0.5f;
            const float s = 0.5f - 0.5f * std::cos(m_Phase);
            t.Translation = glm::mix(m_Start, PointB, s);
            break;
        }
        case TargetKind::Drone:
        {
            // a circle (PointB.x: its radius) with a bob, turning as it goes
            m_Phase += dt * Speed;
            const float r = std::max(0.5f, PointB.x);
            t.Translation = m_Start + glm::vec3(std::cos(m_Phase) * r, std::sin(m_Phase * 2.0f) * 0.35f, std::sin(m_Phase) * r);
            t.Rotation.y += dt * 1.6f;
            break;
        }
        default:
            break;
    }

    // ---- knocked down / back up
    m_Timer += dt;
    switch (m_State)
    {
        case State::Up:
            if (GetKind() == TargetKind::PopUp && m_Timer > m_PopTimer)
            {
                // a pop-up does not wait for a shot forever
                m_State = State::Falling;
                m_Timer = 0.0f;
            }
            break;
        case State::Falling:
        {
            const float k = std::min(1.0f, m_Timer / 0.25f);
            m_Angle = DownAngle * ApplyEase(Ease::OutBounce, k);
            if (k >= 1.0f)
            {
                // how long it lies: a pop-up a random while, the others their respawn time
                m_State = State::Down;
                m_Timer = 0.0f;
                m_DownWait = (GetKind() == TargetKind::PopUp ? m_Rng.Range(1.5f, 3.5f) : RespawnTime) * m_RespawnScale;
            }
            break;
        }
        case State::Down:
            if (m_Timer > m_DownWait)
                Raise();
            break;
        case State::Rising:
        {
            const float k = std::min(1.0f, m_Timer / 0.35f);
            if (GetKind() == TargetKind::Drone)
            {
                if (m_Face)
                    m_Face.GetComponent<TransformComponent>().Scale = glm::vec3(std::max(0.01f, ApplyEase(Ease::OutBack, k)));
            }
            else
                m_Angle = DownAngle * (1.0f - ApplyEase(Ease::OutBack, k));
            if (k >= 1.0f)
            {
                m_State = State::Up;
                m_Timer = 0.0f;
                m_Angle = 0.0f;
                m_PopTimer = m_Rng.Range(2.5f, 4.5f) * (m_RespawnScale < 1.0f ? 0.8f : 1.0f);
            }
            break;
        }
    }
    if (m_Hinge)
        m_Hinge.GetComponent<TransformComponent>().Rotation.x = m_Angle;

    // ---- the hit flash fades back to the face's own glow
    if (m_Flash > 0.0f)
    {
        m_Flash = std::max(0.0f, m_Flash - dt * 4.0f);
        SetFaceGlow(m_BaseEmissionStrength + m_Flash * 12.0f, glm::mix(m_BaseEmission, glm::vec3(1.0f, 0.95f, 0.8f), m_Flash));
    }

    const glm::vec3 now = m_Face ? GetScene().GetWorldPosition(m_Face) : m_Start;
    m_Velocity = Math::Damp(m_Velocity, (now - facePos) / dt, 10.0f, dt);
    m_LastFace = now;
}

// ------------------------------------------------------------------------------------------ beam

void TracerFx::Setup(const glm::vec3& from, const glm::vec3& to, const glm::vec3& color)
{
    const glm::vec3 delta = to - from;
    const float length = glm::length(delta);
    if (length < 1e-3f)
        return;
    const glm::vec3 d = delta / length;
    // the bar's length is along its local +Z: pitch first (about X), then yaw (about Y)
    auto& t = Transform();
    t.Translation = from + delta * 0.5f;
    t.Rotation = { -std::asin(std::clamp(d.y, -1.0f, 1.0f)), std::atan2(d.x, d.z), 0.0f };
    m_Width = t.Scale.x;
    t.Scale = { m_Width, m_Width, length };
    if (auto* mr = TryGetComponent<MeshRendererComponent>())
    {
        mr->Mat.Albedo = glm::vec4(color, 1.0f);
        mr->Mat.Emission = color;
    }
}

void TracerFx::OnUpdate(Timestep ts)
{
    m_Age += ts;
    const float k = 1.0f - std::clamp(m_Age / Lifetime, 0.0f, 1.0f);
    auto& t = Transform();
    t.Scale.x = t.Scale.y = std::max(0.001f, m_Width * k);
    if (m_Age >= Lifetime)
        Destroy();
}

ZM_REGISTER_SCRIPT(Target,
    ZM_FIELD_ENUM(Target, Kind, "Turg'un", "Sirpanuvchi", "Chiquvchi", "Dron").Tooltip("Nishon turi"),
    ZM_FIELD(Target, Hinge).Tooltip("Yiqilish ilmog'i: urilganda X o'qi atrofida buriladi"),
    ZM_FIELD(Target, Face).Tooltip("Nur tegadigan qism (kinematik jism)"),
    ZM_FIELD(Target, PointB).Tooltip("Sirpanuvchi: relsning ikkinchi uchi; dron: x - aylana radiusi"),
    ZM_FIELD_RANGE(Target, Speed, 0.1f, 10.0f),
    ZM_FIELD_RANGE(Target, RespawnTime, 0.5f, 20.0f).Tooltip("Yiqilgandan keyin necha soniyada qaytadi"),
    ZM_FIELD_RANGE(Target, Points, 0, 1000))

ZM_REGISTER_SCRIPT(TracerFx,
    ZM_FIELD_RANGE(TracerFx, Lifetime, 0.01f, 1.0f))
