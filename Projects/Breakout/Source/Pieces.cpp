#include "Breakout.h"

#include <algorithm>
#include <cmath>

using namespace Zamin;
using namespace BreakoutUtil;

// Raketka, to'p, g'isht, bonus kapsulasi va effekt skriptlari.
// To'p devor va g'ishtlardan Box2D bilan qaytadi (restitution 1, ishqalanishsiz); tezligini skript har kadr
// to'g'rilaydi (darajaga qarab, sekin bonusi bilan) va juda yotiq uchishiga yo'l qo'ymaydi. Raketkaga tegishni skript
// o'zi hisoblaydi: qaytish burchagi tegish nuqtasiga bog'liq (o'rtada tik, chetida 62 darajagacha) - raketka kengligi
// o'zgarsa ham aniq ishlaydi.

// ---------------------------------------------------------------------------------------- Paddle

void PaddleController::OnCreate()
{
    m_Visual = FindChild("Visual");
    m_TurretLeft = FindChild("Lazer (chap)");
    m_TurretRight = FindChild("Lazer (o'ng)");
    m_Position = glm::vec2(Transform().Translation);
    m_Width = m_TargetWidth = Width;
}

void PaddleController::ResetPaddle(bool bot)
{
    m_Bot = bot;
    m_Position.x = 0.0f;
    m_VelocityX = 0.0f;
    m_Width = m_TargetWidth = Width;
    m_BotTarget = 0.0f;
    Transform().Translation = glm::vec3(m_Position, 0.0f);
}

void PaddleController::SetLaser(bool on)
{
    m_Laser = on;
    for (Entity turret : { m_TurretLeft, m_TurretRight })
        if (turret && turret.IsActive() != on)
            turret.SetActive(on);
}

void PaddleController::Bump()
{
    m_Bump = 1.0f;
}

std::vector<glm::vec2> PaddleController::GetTurrets() const
{
    const float x = m_Width * 0.5f - 0.4f;
    return { { m_Position.x - x, m_Position.y + 0.6f }, { m_Position.x + x, m_Position.y + 0.6f } };
}

void PaddleController::OnUpdate(Timestep ts)
{
    const float dt = ts;
    BreakoutGame* game = BreakoutGame::Get();
    if (!game || dt <= 0.0f)
        return;
    m_Width = Math::Damp(m_Width, m_TargetWidth, 10.0f, dt);
    const float limit = game->FieldHalfWidth - m_Width * 0.5f;

    float desired = 0.0f;
    if (m_Bot)
        desired = std::clamp((m_BotTarget - m_Position.x) * 16.0f, -Speed, Speed);
    else
    {
        // the mouse once it moves; the keys / stick take over when they are used
        const float axis = Input::GetAxis("MoveX");
        const glm::vec2 mouse = Input::GetMousePosition();
        if (glm::distance(mouse, m_LastMouse) > 2.0f && m_LastMouse.x >= 0.0f)
            m_UsingMouse = true;
        if (std::abs(axis) > 0.1f)
            m_UsingMouse = false;
        m_LastMouse = mouse;
        if (m_UsingMouse)
            desired = std::clamp((GetMouseWorldPosition().x - m_Position.x) * 20.0f, -Speed * 1.4f, Speed * 1.4f);
        else
            desired = axis * Speed;
    }
    m_VelocityX = Math::Damp(m_VelocityX, desired, m_Bot || m_UsingMouse ? 30.0f : 18.0f, dt);
    m_Position.x += m_VelocityX * dt;
    if (std::abs(m_Position.x) > limit)
    {
        m_Position.x = std::clamp(m_Position.x, -limit, limit);
        m_VelocityX = 0.0f;
    }
    Transform().Translation = glm::vec3(m_Position, 0.0f);

    // a squash when the ball hits, and a slight lean while moving
    m_Bump = std::max(0.0f, m_Bump - dt * 6.0f);
    if (m_Visual)
    {
        auto& t = m_Visual.GetComponent<TransformComponent>();
        t.Scale = glm::vec3(m_Width * (1.0f + 0.06f * m_Bump), 0.7f * (1.0f - 0.22f * m_Bump), 1.0f);
        t.Rotation.z = std::clamp(-m_VelocityX * 0.004f, -0.06f, 0.06f);
    }
    const float tx = m_Width * 0.5f - 0.4f;
    if (m_TurretLeft)
        m_TurretLeft.GetComponent<TransformComponent>().Translation.x = -tx;
    if (m_TurretRight)
        m_TurretRight.GetComponent<TransformComponent>().Translation.x = tx;
}

// ------------------------------------------------------------------------------------------ Ball

void Ball::OnCreate()
{
    m_Visual = FindChild("Visual");
    m_Trail = FindChild("Iz");
    m_Position = glm::vec2(GetWorldPosition());
}

void Ball::Hold(float offset)
{
    m_Held = true;
    m_HoldOffset = offset;
}

void Ball::Launch()
{
    BreakoutGame* game = BreakoutGame::Get();
    const PaddleController* paddle = game ? game->GetPaddle() : nullptr;
    const float push = paddle ? paddle->GetVelocityX() * 0.02f : 0.0f;
    LaunchDirection({ std::clamp(m_HoldOffset * 0.5f + push, -0.7f, 0.7f), 1.0f });
}

void Ball::LaunchDirection(const glm::vec2& direction)
{
    BreakoutGame* game = BreakoutGame::Get();
    const float speed = game ? game->GetBallSpeed() : Speed;
    m_Held = false;
    m_Velocity = glm::normalize(glm::length(direction) > 1e-4f ? direction : glm::vec2(0.0f, 1.0f)) * speed;
    SetVelocity(m_Velocity);
    if (game)
        game->Sfx("launch", 0.5f, 1.0f);
}

void Ball::SetLook(bool fire)
{
    if (fire == m_FireLook)
        return;
    m_FireLook = fire;
    if (m_Visual)
        m_Visual.GetComponent<SpriteRendererComponent>().Color = fire ? glm::vec4(2.4f, 1.1f, 0.4f, 1.0f) : glm::vec4(1.3f, 1.35f, 1.45f, 1.0f);
    if (m_Trail)
        if (auto* ps = m_Trail.TryGetComponent<ParticleSystemComponent>())
        {
            ps->StartColor = fire ? glm::vec4(2.4f, 0.9f, 0.2f, 0.9f) : glm::vec4(0.5f, 1.1f, 1.8f, 0.8f);
            ps->EndColor = fire ? glm::vec4(1.0f, 0.2f, 0.05f, 0.0f) : glm::vec4(0.3f, 0.2f, 1.0f, 0.0f);
            ps->EmissionRate = fire ? 90.0f : 55.0f;
            ps->StartSize = fire ? 0.6f : 0.42f;
        }
    if (auto* light = TryGetComponent<Light2DComponent>())
        light->Color = fire ? glm::vec3(1.0f, 0.55f, 0.2f) : glm::vec3(0.6f, 0.85f, 1.0f);
}

void Ball::OnUpdate(Timestep ts)
{
    const float dt = ts;
    BreakoutGame* game = BreakoutGame::Get();
    PaddleController* paddle = game ? game->GetPaddle() : nullptr;
    if (!game || !paddle || m_Lost || dt <= 0.0f)
        return;
    SetLook(game->IsFireActive());

    if (m_Held)
    {
        m_Position = paddle->GetPosition() + glm::vec2(m_HoldOffset, 0.27f + Radius + 0.04f);
        SetPosition2D(m_Position);
        SetVelocity({ 0.0f, 0.0f });
        if (!game->IsBot() && Input::IsActionJustPressed("Action"))
            Launch();
        return;
    }

    m_Position = glm::vec2(GetWorldPosition());
    glm::vec2 v = GetVelocity();
    if (glm::length(v) < 0.1f)
        v = m_Velocity;
    // never too flat: at least ~17 degrees off the horizontal
    glm::vec2 dir = glm::normalize(v);
    if (std::abs(dir.y) < 0.3f)
    {
        dir.y = dir.y >= 0.0f ? 0.3f : -0.3f;
        dir.x = (dir.x >= 0.0f ? 1.0f : -1.0f) * std::sqrt(1.0f - 0.09f);
    }
    v = dir * game->GetBallSpeed();

    // the paddle: crossing its top edge while coming down
    if (v.y < 0.0f)
    {
        const float top = paddle->GetTop();
        const float half = paddle->GetHalfWidth();
        const float x = m_Position.x - paddle->GetPosition().x;
        const float bottom = m_Position.y - Radius;
        const float nextBottom = bottom + v.y * dt;
        const bool crossing = bottom >= top - 0.25f && nextBottom <= top + 0.02f;
        if (crossing && std::abs(x) <= half + Radius * 0.7f)
        {
            const float offset = std::clamp(x / half, -1.0f, 1.0f);
            const float angle = glm::radians(62.0f) * offset + paddle->GetVelocityX() * 0.004f;
            v = glm::vec2(std::sin(angle), std::cos(angle)) * glm::length(v);
            m_Position.y = top + Radius + 0.01f;
            SetPosition2D(m_Position);
            game->OnPaddleHit(*this, offset);
        }
    }
    m_Velocity = v;
    SetVelocity(v);

    if (m_Position.y < game->FieldBottom - 0.6f)
    {
        m_Lost = true;
        game->OnBallLost(*this);
        Destroy();
    }
}

void Ball::OnLateUpdate(Timestep)
{
    // a fireball goes straight through what it broke
    if (m_RestoreVelocity && !m_Held && !m_Lost)
        SetVelocity(m_Velocity);
    m_RestoreVelocity = false;
}

void Ball::OnCollisionBegin(Entity other)
{
    BreakoutGame* game = BreakoutGame::Get();
    if (!game || m_Held)
        return;
    if (Brick* brick = GetScript<Brick>(other))
    {
        const bool fire = game->IsFireActive();
        if (brick->Hit(fire) && fire)
            m_RestoreVelocity = true;
    }
    else
        game->Sfx("paddle", 0.22f, 1.7f); // a wall
}

// ----------------------------------------------------------------------------------------- Brick

void Brick::OnCreate()
{
    m_Visual = FindChild("Visual");
    if (m_Visual)
        m_BaseScale = m_Visual.GetComponent<TransformComponent>().Scale;
}

void Brick::Setup(BrickKind kind, const glm::vec3& color, float appearDelay)
{
    Kind = kind;
    Color = color;
    switch (kind)
    {
        case BrickKind::Normal: HitPoints = 1; Points = 50; break;
        case BrickKind::Tough:  HitPoints = 2; Points = 100; break;
        case BrickKind::Steel:  HitPoints = 3; Points = 200; break;
        case BrickKind::Gold:   HitPoints = 1; Points = 0; break;
        case BrickKind::Bomb:   HitPoints = 1; Points = 80; break;
    }
    m_Appear = appearDelay;
    if (m_Visual)
        m_Visual.GetComponent<TransformComponent>().Scale = glm::vec3(0.0f);
    Refresh();
}

void Brick::Refresh()
{
    if (!m_Visual)
        return;
    auto& sprite = m_Visual.GetComponent<SpriteRendererComponent>();
    switch (Kind)
    {
        case BrickKind::Normal:
            sprite.SpriteName = "brick";
            sprite.Color = glm::vec4(Color * 1.1f, 1.0f);
            break;
        case BrickKind::Tough:
            sprite.SpriteName = HitPoints >= 2 ? "brick" : "brick_cracked";
            sprite.Color = glm::vec4(Color * (HitPoints >= 2 ? 0.8f : 0.65f), 1.0f);
            break;
        case BrickKind::Steel:
            sprite.SpriteName = HitPoints >= 3 ? "steel" : "steel_cracked";
            sprite.Color = glm::vec4(glm::vec3(HitPoints >= 2 ? 1.05f : 0.85f), 1.0f);
            break;
        case BrickKind::Gold:
            sprite.SpriteName = "gold";
            sprite.Color = glm::vec4(1.15f, 1.1f, 1.0f, 1.0f);
            break;
        case BrickKind::Bomb:
            sprite.SpriteName = "bomb";
            sprite.Color = glm::vec4(1.1f, 1.05f, 1.05f, 1.0f);
            break;
    }
}

void Brick::OnUpdate(Timestep ts)
{
    if (m_Broken || !m_Visual)
        return;
    const float dt = ts;
    m_Appear -= dt;
    const float grow = std::clamp(-m_Appear * 4.0f, 0.0f, 1.0f);
    m_Punch = std::max(0.0f, m_Punch - dt * 7.0f);
    const float scale = (m_Appear > 0.0f ? 0.0f : ApplyEase(Ease::OutBack, grow)) * (1.0f + 0.16f * m_Punch);
    m_Visual.GetComponent<TransformComponent>().Scale = m_BaseScale * scale;
    // a bomb's fuse glows
    if (Kind == BrickKind::Bomb)
    {
        const float pulse = 1.0f + 0.25f * (0.5f + 0.5f * std::sin(GetTime() * 6.0f));
        m_Visual.GetComponent<SpriteRendererComponent>().Color = glm::vec4(1.1f * pulse, 1.0f, 1.0f, 1.0f);
    }
}

bool Brick::Hit(bool fire)
{
    if (m_Broken)
        return false;
    BreakoutGame* game = BreakoutGame::Get();
    m_Punch = 1.0f;
    if (m_Visual)
        m_Visual.GetComponent<SpriteRendererComponent>().Flash({ 1.0f, 1.0f, 1.0f, 0.9f }, 0.12f);
    if (Kind == BrickKind::Gold)
    {
        if (game)
            game->OnBrickHit(*this, false);
        return false;
    }
    HitPoints -= fire ? HitPoints : 1;
    if (HitPoints <= 0)
    {
        Break();
        return true;
    }
    Refresh();
    if (game)
        game->OnBrickHit(*this, false);
    return false;
}

void Brick::Break()
{
    if (m_Broken || Kind == BrickKind::Gold)
        return;
    m_Broken = true;
    if (BreakoutGame* game = BreakoutGame::Get())
        game->OnBrickHit(*this, true);
    Destroy();
}

// --------------------------------------------------------------------------------------- PowerUp

void PowerUp::OnCreate()
{
    m_Position = Transform().Translation;
}

void PowerUp::OnUpdate(Timestep ts)
{
    BreakoutGame* game = BreakoutGame::Get();
    if (m_Done || !game)
        return;
    const float dt = ts;
    m_Age += dt;
    m_Position.y -= FallSpeed * dt;
    Transform().Translation = m_Position;
    if (Entity visual = FindChild("Visual"))
    {
        auto& t = visual.GetComponent<TransformComponent>();
        t.Rotation.z = std::sin(m_Age * 4.0f) * 0.12f;
        const float pop = ApplyEase(Ease::OutBack, std::min(1.0f, m_Age * 4.0f));
        t.Scale = glm::vec3(1.5f * pop, 0.64f * pop, 1.0f);
    }
    if (const PaddleController* paddle = game->GetPaddle())
    {
        const glm::vec2 p = paddle->GetPosition();
        if (std::abs(m_Position.x - p.x) < paddle->GetHalfWidth() + 0.7f && std::abs(m_Position.y - p.y) < 0.55f)
        {
            m_Done = true;
            game->OnPowerUpCaught(Kind, glm::vec2(m_Position));
            Destroy();
            return;
        }
    }
    if (m_Position.y < game->FieldBottom - 1.0f)
    {
        m_Done = true;
        Destroy();
    }
}

// ------------------------------------------------------------------------------------ FxAnimator

void FxAnimator::OnUpdate(Timestep ts)
{
    if (m_Age == 0.0f)
        m_Color = GetColor(); // the colour the spawner gave it
    m_Age += ts;
    const float t = std::clamp(m_Age / std::max(Lifetime, 0.01f), 0.0f, 1.0f);
    auto& tr = Transform();
    if (!KeepScale)
    {
        const float eased = Pop ? ApplyEase(Ease::OutBack, std::min(1.0f, t * 3.0f)) : ApplyEase(Ease::OutCubic, t);
        const float scale = (StartScale + (EndScale - StartScale) * eased) * m_Multiplier;
        tr.Scale = glm::vec3(scale, scale, 1.0f);
    }
    tr.Translation.y += Rise * (1.0f - t) * ts;
    const float alpha = FadeOut > 0.0f ? std::clamp((Lifetime - m_Age) / FadeOut, 0.0f, 1.0f) : 1.0f;
    SetColor(glm::vec4(glm::vec3(m_Color), m_Color.a * alpha));
    if (m_Age >= Lifetime)
        Destroy();
}

ZM_REGISTER_SCRIPT(PaddleController,
    ZM_FIELD_RANGE(PaddleController, Speed, 5.0f, 60.0f).Tooltip("Eng katta tezlik (birlik/s)"),
    ZM_FIELD_RANGE(PaddleController, Width, 1.0f, 8.0f).Tooltip("Oddiy kenglik (keng bonusi 1.6 barobar)"))

ZM_REGISTER_SCRIPT(Ball,
    ZM_FIELD_RANGE(Ball, Speed, 4.0f, 30.0f).Tooltip("1-darajadagi tezlik; har darajada va har g'ishtda biroz oshadi"),
    ZM_FIELD_RANGE(Ball, MaxSpeed, 4.0f, 40.0f))

ZM_REGISTER_SCRIPT(Brick,
    ZM_FIELD_ENUM(Brick, Kind, "Oddiy", "Qattiq", "Po'lat", "Oltin", "Bomba").Label("Turi"),
    ZM_FIELD_RANGE(Brick, HitPoints, 1, 10).Label("Zarbalar"),
    ZM_FIELD_COLOR(Brick, Color).Label("Rang"),
    ZM_FIELD_RANGE(Brick, Points, 0, 1000).Label("Ochko"))

ZM_REGISTER_SCRIPT(PowerUp,
    ZM_FIELD_ENUM(PowerUp, Kind, "Keng", "Ko'p to'p", "Sekin", "Olov", "Lazer", "Jon").Label("Bonus"),
    ZM_FIELD_RANGE(PowerUp, FallSpeed, 0.5f, 15.0f).Label("Tushish tezligi"))

ZM_REGISTER_SCRIPT(FxAnimator,
    ZM_FIELD_RANGE(FxAnimator, Lifetime, 0.05f, 5.0f),
    ZM_FIELD(FxAnimator, StartScale),
    ZM_FIELD(FxAnimator, EndScale),
    ZM_FIELD(FxAnimator, Rise),
    ZM_FIELD(FxAnimator, FadeOut),
    ZM_FIELD(FxAnimator, Pop),
    ZM_FIELD(FxAnimator, KeepScale))
