#include "Runner.h"

#include <algorithm>
#include <cmath>

using namespace Zamin;

// Yuguruvchi: oldinga (-Z) RunnerGame bergan tezlikda yuguradi, uch yo'lak orasida silliq o'tadi ("Left" / "Right"
// yoki chap stikni siltash), sakraydi ("Jump"), sirpanadi ("Slide" - havoda bosilsa tez tushadi). Qo'l-oyoqlar
// yugurish fazasi bilan tebranadi, sakrashda oyoqlar yig'iladi, sirpanishda tana orqaga yotadi; oyoq ostidan chang
// ko'tariladi. Bot: oldindagi to'siqqa qarab sakraydi, sirpanadi yoki bo'sh yo'lakka o'tadi, tangalarga intiladi.

void Runner::OnCreate()
{
    m_Rng.SetSeed(0x5111c0adull);
    ResetRunner(true);
}

void Runner::ResetRunner(bool bot)
{
    m_Bot = bot;
    m_Crashed = false;
    m_Lane = 0;
    m_X = m_Y = m_VY = 0.0f;
    m_Z = 0.0f;
    m_SlideTimer = 0.0f;
    m_CrashTime = 0.0f;
    m_Tilt = 0.0f;
    m_BotCooldown = 0.0f;
    m_ShieldTime = 0.0f;
    Transform().Translation = { 0.0f, 0.0f, 0.0f };
    Transform().Rotation = { 0.0f, 0.0f, 0.0f };
    SetShieldVisible(false, 0.0f);
}

void Runner::Crash()
{
    if (m_Crashed)
        return;
    m_Crashed = true;
    m_CrashTime = 0.0f;
    m_VY = 5.0f;
}

void Runner::SetShieldVisible(bool visible, float time)
{
    m_ShieldTime = time;
    if (Entity bubble = Resolve(ShieldBubble); bubble && bubble.IsActive() != visible)
        bubble.SetActive(visible);
}

Runner::Controls Runner::ReadInput()
{
    Controls c;
    if (Input::IsActionJustPressed("Left"))
        c.Lane = -1;
    if (Input::IsActionJustPressed("Right"))
        c.Lane = 1;
    // a flick of the left stick changes lanes once
    const float stick = Input::GetAxis("Steer");
    if (std::abs(stick) > 0.6f && std::abs(m_StickPrev) <= 0.6f)
        c.Lane = stick > 0.0f ? 1 : -1;
    m_StickPrev = stick;
    c.Jump = Input::IsActionJustPressed("Jump");
    c.Slide = Input::IsActionJustPressed("Slide");
    return c;
}

Runner::Controls Runner::ThinkBot(float dt)
{
    Controls c;
    RunnerGame* game = RunnerGame::Get();
    if (!game)
        return c;
    const float speed = game->GetSpeed();
    const float look = speed * 1.3f + 8.0f;
    // how good each lane is: the distance to its first blocking obstacle (jumpable / slidable ones do not block)
    auto clearance = [&](int lane) {
        const RunnerGame::Hazard h = game->FindHazard(lane, m_Z, look);
        if (!h.Found)
            return look;
        const bool passable = h.Kind == ObstacleKind::Fence || h.Kind == ObstacleKind::Gate;
        return passable ? look : h.Distance;
    };
    m_BotCooldown -= dt;
    const float here = clearance(m_Lane);
    if (m_BotCooldown <= 0.0f && std::abs(m_X - game->LaneX(m_Lane)) < 0.5f)
    {
        int best = m_Lane;
        float bestScore = here + (float)game->CountCoins(m_Lane, m_Z, 25.0f) * 1.5f;
        for (int d : { -1, 1 })
        {
            const int lane = m_Lane + d;
            if (lane < -1 || lane > 1)
                continue;
            // the lane next door must be clear right beside the runner too
            const RunnerGame::Hazard side = game->FindHazard(lane, m_Z + 3.0f, 6.0f);
            if (side.Found && side.Distance < 3.5f && side.Kind != ObstacleKind::Fence && side.Kind != ObstacleKind::Gate)
                continue;
            const float score = clearance(lane) + (float)game->CountCoins(lane, m_Z, 25.0f) * 1.5f - 0.5f;
            if (score > bestScore + 2.0f)
            {
                best = lane;
                bestScore = score;
            }
        }
        if (best != m_Lane)
        {
            c.Lane = best > m_Lane ? 1 : -1;
            m_BotCooldown = 0.18f;
        }
    }
    // the obstacle straight ahead (in the lane it is in and the one it moves to): jump a fence or a cart that cannot
    // be avoided, slide under a gate. The jump clears 1.1 m from ~0.12 s to ~0.55 s after take-off.
    const int next = std::clamp(m_Lane + c.Lane, -1, 1);
    RunnerGame::Hazard h = game->FindHazard(m_Lane, m_Z, speed * 0.6f + 3.0f);
    if (next != m_Lane)
        if (const RunnerGame::Hazard other = game->FindHazard(next, m_Z, speed * 0.6f + 3.0f); other.Found && other.Distance < h.Distance)
            h = other;
    if (h.Found && IsGrounded())
    {
        if ((h.Kind == ObstacleKind::Fence || h.Kind == ObstacleKind::Cart) && h.Distance < speed * 0.36f + 0.6f)
            c.Jump = true;
        if (h.Kind == ObstacleKind::Gate && h.Distance < speed * 0.25f + 1.2f)
            c.Slide = true;
    }
    return c;
}

void Runner::OnUpdate(Timestep ts)
{
    // the run itself is stepped by RunnerGame (Tick, in small steps with a collision check after each);
    // here: the tumble after a crash and the animation
    const float dt = ts;
    if (dt <= 0.0f)
        return;
    if (m_Crashed)
    {
        // tumbles forward and down
        auto& t = Transform();
        m_CrashTime += dt;
        m_VY -= Gravity * dt;
        m_Y = std::max(0.0f, m_Y + m_VY * dt);
        m_Z -= std::max(0.0f, 4.0f - m_CrashTime * 6.0f) * dt;
        t.Translation = { m_X, m_Y, m_Z };
        t.Rotation.x = -std::min(1.4f, m_CrashTime * 3.0f);
    }
    if (m_ShieldTime > 0.0f)
    {
        m_ShieldTime -= dt;
        if (m_ShieldTime <= 0.0f)
            SetShieldVisible(false, 0.0f);
    }
    Animate(dt);
}

void Runner::Tick(float dt, float frameDt, bool first)
{
    RunnerGame* game = RunnerGame::Get();
    if (!game || m_Crashed || dt <= 0.0f)
        return;
    if (first)
    {
        // the controls once a frame (a press is seen in one frame only)
        Controls c = m_Bot ? ThinkBot(frameDt) : ReadInput();
        if (c.Lane != 0)
        {
            const int lane = std::clamp(m_Lane + c.Lane, -1, 1);
            if (lane != m_Lane)
            {
                m_Lane = lane;
                PlaySound("Audio/whoosh.wav", 0.35f, m_Rng.Range(0.95f, 1.1f));
            }
        }
        const bool grounded = IsGrounded();
        if (c.Jump && grounded)
        {
            m_VY = JumpSpeed;
            m_SlideTimer = 0.0f;
            PlaySound("Audio/jump.wav", 0.5f, 1.0f);
        }
        if (c.Slide)
        {
            if (grounded)
            {
                m_SlideTimer = SlideTime;
                PlaySound("Audio/slide.wav", 0.5f, 1.0f);
            }
            else
                m_VY = std::min(m_VY, -18.0f); // dive down
        }
    }

    // forward, sideways towards the lane, up / down
    m_Z -= game->GetSpeed() * dt;
    const float targetX = game->LaneX(m_Lane);
    const float before = m_X;
    m_X = Math::Damp(m_X, targetX, 14.0f, dt);
    m_Tilt = Math::Damp(m_Tilt, (m_X - before) / std::max(dt, 1e-4f) * -0.05f, 10.0f, dt);
    m_VY -= Gravity * dt;
    m_Y += m_VY * dt;
    if (m_Y <= 0.0f)
    {
        m_Y = 0.0f;
        if (m_VY < -6.0f)
            PlaySound("Audio/land.wav", 0.4f, 1.0f);
        m_VY = 0.0f;
    }
    m_SlideTimer = std::max(0.0f, m_SlideTimer - dt);
    auto& t = Transform();
    t.Translation = { m_X, m_Y, m_Z };
    t.Rotation = { 0.0f, 0.0f, m_Tilt };
}

void Runner::Animate(float dt)
{
    RunnerGame* game = RunnerGame::Get();
    const float speed = game ? game->GetSpeed() : 10.0f;
    const bool grounded = IsGrounded();
    const bool sliding = m_SlideTimer > 0.0f;
    if (!m_Crashed && grounded && !sliding)
        m_Phase += dt * (6.0f + speed * 0.28f);
    const float s = std::sin(m_Phase);
    float legSwing = grounded ? s * 0.9f : 0.0f;
    float armSwing = grounded ? -s * 0.8f : 0.0f;
    float tuck = 0.0f;
    if (!grounded)
        tuck = 0.9f; // knees up in the air
    auto set = [&](const EntityRef& ref, float rx) {
        if (Entity e = Resolve(ref))
            e.GetComponent<TransformComponent>().Rotation.x = rx;
    };
    set(LegL, legSwing - tuck);
    set(LegR, -legSwing - tuck * 0.6f);
    set(ArmL, armSwing + (grounded ? 0.0f : -2.4f));
    set(ArmR, -armSwing + (grounded ? 0.0f : -2.4f));
    if (Entity body = Resolve(Body))
    {
        auto& bt = body.GetComponent<TransformComponent>();
        // a bob in the stride; sliding: lies back, low
        const float bob = grounded && !sliding ? std::abs(std::cos(m_Phase)) * 0.08f : 0.0f;
        const float slide = sliding ? 1.0f : 0.0f;
        bt.Translation.y = Math::Damp(bt.Translation.y, bob - slide * 0.55f, 18.0f, dt);
        bt.Rotation.x = Math::Damp(bt.Rotation.x, sliding ? 1.25f : (grounded ? -0.12f : -0.05f), 16.0f, dt);
    }
    // dust under the feet while running on the ground
    if (Entity dust = Resolve(Dust))
        if (auto* ps = dust.TryGetComponent<ParticleSystemComponent>())
            ps->EmissionRate = grounded && !m_Crashed ? (sliding ? 60.0f : 18.0f + speed) : 0.0f;
    if (Entity bubble = Resolve(ShieldBubble); bubble && m_ShieldTime > 0.0f)
    {
        auto& bt = bubble.GetComponent<TransformComponent>();
        bt.Rotation.y += dt * 1.5f;
        const float pulse = 1.0f + 0.05f * std::sin(m_Phase * 0.5f);
        bt.Scale = glm::vec3(2.2f * pulse);
    }
}

ZM_REGISTER_SCRIPT(Runner,
    ZM_FIELD(Runner, Body).Header("Tana qismlari"),
    ZM_FIELD(Runner, Head),
    ZM_FIELD(Runner, ArmL),
    ZM_FIELD(Runner, ArmR),
    ZM_FIELD(Runner, LegL),
    ZM_FIELD(Runner, LegR),
    ZM_FIELD(Runner, Dust),
    ZM_FIELD(Runner, ShieldBubble),
    ZM_FIELD_RANGE(Runner, LaneWidth, 1.0f, 5.0f).Header("Harakat"),
    ZM_FIELD_RANGE(Runner, JumpSpeed, 2.0f, 30.0f),
    ZM_FIELD_RANGE(Runner, Gravity, 5.0f, 80.0f),
    ZM_FIELD_RANGE(Runner, SlideTime, 0.2f, 2.0f))
