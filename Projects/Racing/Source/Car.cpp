#include "Race.h"

#include <algorithm>
#include <cmath>
#include <sstream>

using namespace Zamin;

// Mashina: tekislikdagi arkada fizikasi (x, z, burilish burchagi). Tezlik oldinga va yonga ajratiladi: dvigatel
// oldinga itaradi (maksimal tezlikka yaqinlashgan sari kuchsizroq), tormoz / orqaga yurish, havo qarshiligi; yon
// tezlik ilashish (Grip) bilan so'nadi - qo'l tormozida kam ilashadi va mashina sirpanadi (drift). Rul tezlikka
// qarab: past tezlikda burilmaydi, yuqori tezlikda kamroq buriladi. Trassa chetidagi to'siq (WallOffset) mashinani
// qaytaradi va sekinlashtiradi; asfaltdan chiqsa (o't) sekinlashadi va kam ilashadi.
// Bot: trassa chizig'idagi oldinroq nuqtaga rul buradi, oldindagi burilish keskinligiga qarab tezlikni tanlaydi,
// keskin burilishda qo'l tormozi bilan sirpanadi; tiqilib qolsa trassaga qaytadi.

// ------------------------------------------------------------------------------------------- track

bool Track::Parse(const std::string& text)
{
    Points.clear();
    std::stringstream in(text);
    std::string item;
    while (std::getline(in, item, ';'))
    {
        glm::vec2 p(0.0f);
        if (std::sscanf(item.c_str(), "%f,%f", &p.x, &p.y) == 2)
            Points.push_back(p);
    }
    Distance.assign(Points.size(), 0.0f);
    Length = 0.0f;
    for (size_t i = 0; i < Points.size(); i++)
    {
        Distance[i] = Length;
        Length += glm::distance(Points[i], Points[(i + 1) % Points.size()]);
    }
    return Points.size() > 3;
}

int Track::Closest(const glm::vec2& p, int hint) const
{
    const int n = (int)Points.size();
    if (n == 0)
        return 0;
    int best = 0;
    float bestD = 1e30f;
    auto test = [&](int i) {
        i = ((i % n) + n) % n;
        const float d = glm::dot(Points[(size_t)i] - p, Points[(size_t)i] - p);
        if (d < bestD)
        {
            bestD = d;
            best = i;
        }
    };
    if (hint < 0)
        for (int i = 0; i < n; i++)
            test(i);
    else
        for (int i = hint - 25; i <= hint + 25; i++)
            test(i);
    return best;
}

glm::vec2 Track::At(float s) const
{
    if (Points.empty() || Length <= 0.0f)
        return glm::vec2(0.0f);
    s = std::fmod(s, Length);
    if (s < 0.0f)
        s += Length;
    const auto it = std::upper_bound(Distance.begin(), Distance.end(), s);
    const size_t i = (size_t)std::max<ptrdiff_t>(0, (it - Distance.begin()) - 1);
    const size_t j = (i + 1) % Points.size();
    const float segment = std::max(1e-4f, glm::distance(Points[i], Points[j]));
    return glm::mix(Points[i], Points[j], std::clamp((s - Distance[i]) / segment, 0.0f, 1.0f));
}

glm::vec2 Track::Tangent(float s) const
{
    const glm::vec2 d = At(s + 1.5f) - At(s - 1.5f);
    const float len = glm::length(d);
    return len > 1e-4f ? d / len : glm::vec2(0.0f, -1.0f);
}

float Track::DistanceAt(int index, const glm::vec2& p) const
{
    const size_t i = (size_t)index;
    const size_t j = (i + 1) % Points.size();
    const glm::vec2 seg = Points[j] - Points[i];
    const float len = std::max(1e-4f, glm::length(seg));
    const float along = glm::dot(p - Points[i], seg / len);
    return std::fmod(Distance[i] + along + Length, Length);
}

// --------------------------------------------------------------------------------------------- car

void Car::OnCreate()
{
    const glm::vec3 p = Transform().Translation;
    m_Pos = { p.x, p.z };
    m_Yaw = Transform().Rotation.y;
}

void Car::ResetCar(const glm::vec2& position, float yaw)
{
    m_Pos = position;
    m_Vel = glm::vec2(0.0f);
    m_Yaw = yaw;
    m_YawRate = m_SideSpeed = m_Accel = m_LatAccel = 0.0f;
    m_StuckTimer = 0.0f;
    m_Controls = {};
    Lap = 0;
    NextCheckpoint = 0;
    LapTime = BestLap = RaceTime = 0.0f;
    Finished = false;
    FinishPlace = 0;
    if (const RaceGame* game = RaceGame::Get())
    {
        m_TrackIndex = game->GetTrack().Closest(m_Pos, -1);
        m_TrackS = game->GetTrack().DistanceAt(m_TrackIndex, m_Pos);
    }
    Transform().Translation = { m_Pos.x, 0.0f, m_Pos.y };
    Transform().Rotation = { 0.0f, m_Yaw, 0.0f };
}

void Car::Respawn()
{
    const RaceGame* game = RaceGame::Get();
    if (!game)
        return;
    const Track& track = game->GetTrack();
    const float s = track.Distance[(size_t)m_TrackIndex];
    const glm::vec2 t = track.Tangent(s);
    m_Pos = track.At(s);
    m_Vel = glm::vec2(0.0f);
    m_Yaw = std::atan2(-t.x, -t.y);
    m_YawRate = m_SideSpeed = 0.0f;
    m_StuckTimer = 0.0f;
}

Car::Controls Car::ReadInput() const
{
    Controls c;
    c.Throttle = std::clamp(Input::GetAxis("Throttle"), 0.0f, 1.0f);
    c.Brake = std::clamp(Input::GetAxis("Brake"), 0.0f, 1.0f);
    c.Steer = std::clamp(Input::GetAxis("Steer"), -1.0f, 1.0f);
    c.Handbrake = Input::IsActionPressed("Handbrake");
    return c;
}

Car::Controls Car::ThinkBot(float dt)
{
    Controls c;
    const RaceGame* game = RaceGame::Get();
    if (!game)
        return c;
    const Track& track = game->GetTrack();
    const float speed = GetSpeed();
    // aim at a point ahead on its line
    const float ahead = 7.0f + std::max(0.0f, speed) * 0.5f;
    const glm::vec2 tangent = track.Tangent(m_TrackS + ahead);
    const glm::vec2 normal(-tangent.y, tangent.x);
    const glm::vec2 target = track.At(m_TrackS + ahead) - normal * LineOffset;
    const glm::vec2 to = target - m_Pos;
    const float goalYaw = std::atan2(-to.x, -to.y);
    const float error = Math::DeltaAngle(m_Yaw, goalYaw);
    c.Steer = std::clamp(-error * 2.4f, -1.0f, 1.0f); // positive steer turns right (yaw decreases)

    // how sharp the road ahead turns: the angle between the tangent now and further on
    const float near = 8.0f, far = 30.0f + std::max(0.0f, speed) * 0.9f;
    const glm::vec2 t0 = track.Tangent(m_TrackS + near), t1 = track.Tangent(m_TrackS + far);
    const float bend = std::acos(std::clamp(glm::dot(t0, t1), -1.0f, 1.0f));
    const float targetSpeed = MaxSpeed * Skill * std::clamp(1.0f - bend * 0.62f, 0.38f, 1.0f);
    c.Throttle = speed < targetSpeed ? 1.0f : 0.0f;
    c.Brake = speed > targetSpeed + 2.5f ? std::clamp((speed - targetSpeed) / 8.0f, 0.3f, 1.0f) : 0.0f;
    c.Handbrake = bend > 1.15f && speed > 22.0f && std::abs(error) > 0.25f;

    // stuck against a wall: back to the track
    if (std::abs(speed) < 1.5f && !m_Frozen)
        m_StuckTimer += dt;
    else
        m_StuckTimer = 0.0f;
    if (m_StuckTimer > 2.5f)
        Respawn();
    return c;
}

void Car::Simulate(float dt, float frameDt, bool first)
{
    RaceGame* game = RaceGame::Get();
    if (!game || dt <= 0.0f)
        return;
    const Track& track = game->GetTrack();
    if (first)
        m_Controls = m_Frozen || m_Driver == Driver::None ? Controls{} : m_Driver == Driver::Player ? ReadInput() : ThinkBot(frameDt);
    if (m_Frozen)
    {
        m_Controls.Brake = 1.0f; // held at the start line
        m_Controls.Throttle = 0.0f;
    }
    const Controls& c = m_Controls;

    // the velocity along the car and across it (`side`: forward turned a quarter - the same axis both ways)
    const glm::vec2 f = GetForward();
    float vF = glm::dot(m_Vel, f);
    float vR = glm::dot(m_Vel, glm::vec2(f.y, -f.x));

    // where it is on the track: asphalt, grass, the barrier
    m_TrackIndex = track.Closest(m_Pos, m_TrackIndex);
    m_TrackS = track.DistanceAt(m_TrackIndex, m_Pos);
    const glm::vec2 centre = track.At(m_TrackS);
    const glm::vec2 tangent = track.Tangent(m_TrackS);
    const glm::vec2 normal(-tangent.y, tangent.x);
    const float offset = glm::dot(m_Pos - centre, normal);
    m_OffRoad = std::abs(offset) > track.HalfWidth + 1.2f;

    // ---- along: engine, brakes, reverse, drag
    const float before = vF;
    if (c.Throttle > 0.0f)
        vF += EngineAccel * c.Throttle * std::max(0.0f, 1.0f - vF / MaxSpeed) * dt;
    if (c.Brake > 0.0f)
    {
        if (vF > 0.5f)
            vF = std::max(0.0f, vF - BrakeDecel * c.Brake * dt);
        else
            vF = std::max(-ReverseSpeed, vF - 6.0f * c.Brake * dt); // reverse
    }
    if (c.Handbrake)
        vF -= std::copysign(std::min(std::abs(vF), 6.0f * dt), vF);
    const float drag = 0.06f + 0.0011f * std::abs(vF) + (m_OffRoad ? 0.9f : 0.0f);
    vF -= vF * drag * dt;
    if (c.Throttle <= 0.0f && c.Brake <= 0.0f)
        vF -= std::copysign(std::min(std::abs(vF), 1.2f * dt), vF); // rolling resistance

    // ---- sideways: the tyres grip (less on grass, much less with the handbrake: a drift)
    const float grip = (c.Handbrake ? DriftGrip : Grip) * (m_OffRoad ? 0.55f : 1.0f);
    vR *= std::exp(-grip * dt);

    // ---- steering: needs speed; less lock at high speed; backwards it turns the other way
    const float speed01 = std::clamp(std::abs(vF) / MaxSpeed, 0.0f, 1.0f);
    const float authority = std::clamp(std::abs(vF) / 7.0f, 0.0f, 1.0f) * (1.0f - 0.45f * speed01) * (c.Handbrake ? 1.35f : 1.0f);
    const float targetRate = -c.Steer * SteerRate * authority * (vF >= 0.0f ? 1.0f : -1.0f);
    m_YawRate = Math::Damp(m_YawRate, targetRate, 10.0f, dt);
    m_Yaw += m_YawRate * dt;

    // back to world space (the body turned under the velocity: what was forward is now partly sideways)
    const glm::vec2 nf = GetForward();
    const glm::vec2 nr(nf.y, -nf.x);
    m_Vel = nf * vF + nr * vR;
    m_SideSpeed = vR;
    m_Accel = Math::Damp(m_Accel, (vF - before) / dt, 6.0f, dt);
    m_LatAccel = Math::Damp(m_LatAccel, m_YawRate * vF, 6.0f, dt);
    m_Pos += m_Vel * dt;

    // ---- the barriers: pushed back inside, the outward speed lost, a scrape
    const float newOffset = glm::dot(m_Pos - centre, normal);
    const float limit = track.WallOffset - 1.0f;
    m_HitCooldown = std::max(0.0f, m_HitCooldown - dt);
    if (std::abs(newOffset) > limit)
    {
        const float side = newOffset > 0.0f ? 1.0f : -1.0f;
        m_Pos -= normal * (std::abs(newOffset) - limit) * side;
        const float outward = glm::dot(m_Vel, normal) * side;
        if (outward > 0.0f)
        {
            m_Vel -= normal * side * outward * 1.3f; // a small bounce
            m_Vel *= 0.92f;
            if (outward > 3.0f && m_HitCooldown <= 0.0f)
            {
                m_HitCooldown = 0.4f;
                game->OnCarHitWall(*this, m_Pos + normal * side * 1.0f, outward);
            }
        }
    }
}

// ----------------------------------------------------------------------------------------- visuals

void Car::OnUpdate(Timestep ts)
{
    const float dt = ts;
    if (dt <= 0.0f)
        return;
    auto& t = Transform();
    t.Translation = { m_Pos.x, 0.0f, m_Pos.y };
    t.Rotation = { 0.0f, m_Yaw, 0.0f };
    const float speed = GetSpeed();
    // the body leans out of a turn and dips under braking
    m_Roll = Math::Damp(m_Roll, std::clamp(-m_LatAccel * 0.006f, -0.09f, 0.09f), 8.0f, dt);
    m_Pitch = Math::Damp(m_Pitch, std::clamp(m_Accel * 0.004f, -0.06f, 0.05f), 8.0f, dt);
    if (Entity body = Resolve(Body))
        body.GetComponent<TransformComponent>().Rotation = { m_Pitch, 0.0f, m_Roll };
    // wheels roll; the front ones steer
    m_WheelSpin += speed * dt / 0.38f;
    m_SteerAngle = Math::Damp(m_SteerAngle, -m_Controls.Steer * 0.45f, 12.0f, dt);
    for (const EntityRef* ref : { &WheelFL, &WheelFR, &WheelRL, &WheelRR })
        if (Entity wheel = Resolve(*ref))
            wheel.GetComponent<TransformComponent>().Rotation = { 0.0f, -m_WheelSpin, glm::half_pi<float>() };
    for (const EntityRef* ref : { &SteerFL, &SteerFR })
        if (Entity pivot = Resolve(*ref))
            pivot.GetComponent<TransformComponent>().Rotation.y = m_SteerAngle;
    // brake lights; tyre smoke in a drift; dust off the road
    if (Entity lights = Resolve(BrakeLights))
        if (auto* mr = lights.TryGetComponent<MeshRendererComponent>())
            mr->Mat.EmissionStrength = m_Controls.Brake > 0.1f || m_Controls.Handbrake ? 9.0f : 1.6f;
    const float drift = GetDrift();
    if (Entity smoke = Resolve(Smoke))
        if (auto* ps = smoke.TryGetComponent<ParticleSystemComponent>())
            ps->EmissionRate = drift > 3.0f && !m_OffRoad ? std::min(90.0f, drift * 10.0f) : 0.0f;
    if (Entity dust = Resolve(Dust))
        if (auto* ps = dust.TryGetComponent<ParticleSystemComponent>())
            ps->EmissionRate = m_OffRoad && std::abs(speed) > 4.0f ? 40.0f : 0.0f;
    // the engine: pitch with the revs (four gears), the skid
    if (HasComponent<AudioSourceComponent>())
    {
        const float s01 = std::clamp(std::abs(speed) / MaxSpeed, 0.0f, 1.0f);
        const float gear = std::min(3.0f, std::floor(s01 * 4.0f));
        const float rev = s01 * 4.0f - gear;
        SetAudioPitch(0.7f + rev * 0.65f + gear * 0.12f);
        SetAudioVolume(0.35f + 0.35f * m_Controls.Throttle);
    }
    for (Entity child : GetChildren())
        if (child.GetName() == "Sirpanish ovozi" && child.HasComponent<AudioSourceComponent>())
            SetAudioVolume(child, std::clamp((drift - 2.5f) / 6.0f, 0.0f, 0.9f));
}

ZM_REGISTER_SCRIPT(Car,
    ZM_FIELD(Car, Body).Header("Qismlar"),
    ZM_FIELD(Car, WheelFL),
    ZM_FIELD(Car, WheelFR),
    ZM_FIELD(Car, WheelRL),
    ZM_FIELD(Car, WheelRR),
    ZM_FIELD(Car, SteerFL),
    ZM_FIELD(Car, SteerFR),
    ZM_FIELD(Car, BrakeLights),
    ZM_FIELD(Car, Smoke),
    ZM_FIELD(Car, Dust),
    ZM_FIELD_RANGE(Car, EngineAccel, 1.0f, 60.0f).Header("Fizika").Tooltip("To'liq gazda tezlanish (m/s²)"),
    ZM_FIELD_RANGE(Car, MaxSpeed, 5.0f, 120.0f).Tooltip("Eng yuqori tezlik (m/s)"),
    ZM_FIELD_RANGE(Car, BrakeDecel, 1.0f, 80.0f),
    ZM_FIELD_RANGE(Car, ReverseSpeed, 1.0f, 30.0f),
    ZM_FIELD_RANGE(Car, SteerRate, 0.2f, 6.0f).Tooltip("To'liq rulda burilish tezligi (rad/s)"),
    ZM_FIELD_RANGE(Car, Grip, 0.5f, 30.0f).Tooltip("Yonga sirpanish qanchalik tez so'nadi"),
    ZM_FIELD_RANGE(Car, DriftGrip, 0.1f, 10.0f).Tooltip("Qo'l tormozida ilashish (drift)"),
    ZM_FIELD_RANGE(Car, Skill, 0.5f, 1.1f).Header("Bot").Tooltip("Botning tezligi va burilishdagi jasorati"),
    ZM_FIELD_RANGE(Car, LineOffset, -5.0f, 5.0f).Tooltip("Botning yo'li - markazdan (m, + o'ngga)"))
