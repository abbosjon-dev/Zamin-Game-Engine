#include "Shooter.h"

#include "Zamin/Core/InputActions.h"
#include "Zamin/Project/Project.h"

#include <algorithm>
#include <cmath>

using namespace Zamin;
using namespace NeonUtil;

// O'yinchi kemasi. Boshqaruv (Loyiha sozlamalari > Input): "Move" vektori (WASD / chap stik), nishon - sichqoncha
// yoki "Aim" vektori (strelkalar / o'ng stik: og'dirilganda o'zi otadi), "Fire" (LKM / J / RT), "Dash" (Space / A).
// Siltanish (dash) qisqa vaqt tez uchiradi va o'q tegmaydi. Jon tugasa - ShooterGame::OnPlayerDied.
// Bot (demo / bosh menyu orqasida): yaqin dushmanlar va o'qlardan qochadi, devor va to'siqlardan uzoqlashadi,
// eng yaqin dushmanni oldindan mo'ljallab otadi, bonuslarni yig'adi.

void PlayerShip::OnCreate()
{
    m_Visual = FindChild("Visual");
    m_Thruster = FindChild("Dvigatel");
    m_Shield = FindChild("Qalqon");
    m_Reticle = FindEntity("Nishon");
    m_Health = MaxHealth;
}

void PlayerShip::ResetShip(const glm::vec2& position, bool bot)
{
    m_Bot = bot;
    m_Health = MaxHealth;
    m_Velocity = glm::vec2(0.0f);
    m_Aim = { 0.0f, 1.0f };
    m_DashLeft = m_DashCooldownLeft = m_FireCooldown = 0.0f;
    m_Invulnerable = 1.0f;
    m_RapidTime = m_SpreadTime = 0.0f;
    m_VisualAngle = 0.0f;
    SetPosition2D(position);
    SetVelocity({ 0.0f, 0.0f });
    m_Position = glm::vec3(position, 0.0f);
    if (m_Visual)
        m_Visual.GetComponent<TransformComponent>().Rotation.z = 0.0f;
}

float PlayerShip::GetDashCharge() const
{
    return DashCooldown > 0.0f ? std::clamp(1.0f - m_DashCooldownLeft / DashCooldown, 0.0f, 1.0f) : 1.0f;
}

void PlayerShip::OnUpdate(Timestep ts)
{
    const float dt = ts;
    ShooterGame* game = ShooterGame::Get();
    if (!game || !IsAlive() || dt <= 0.0f)
        return;
    m_Position = GetWorldPosition();
    const glm::vec2 pos(m_Position);

    const Controls c = m_Bot ? ThinkBot(dt) : ReadInput();

    // ---- dash
    m_DashCooldownLeft = std::max(0.0f, m_DashCooldownLeft - dt);
    if (c.Dash && m_DashCooldownLeft <= 0.0f && m_DashLeft <= 0.0f)
    {
        m_DashDir = SafeNormalize(glm::length(c.Move) > 0.2f ? c.Move : m_Aim);
        m_DashLeft = DashTime;
        m_DashCooldownLeft = DashCooldown;
        m_Invulnerable = std::max(m_Invulnerable, DashTime + 0.08f);
        game->Sfx("dash", 0.6f, 1.0f);
        game->SparksAt(pos, { 0.3f, 0.9f, 1.0f }, 14);
    }

    // ---- movement: eased towards the stick's velocity; a dash overrides it
    if (m_DashLeft > 0.0f)
    {
        m_DashLeft -= dt;
        m_Velocity = m_DashDir * DashSpeed;
    }
    else
        m_Velocity = Math::Damp(m_Velocity, c.Move * MoveSpeed, Acceleration, dt);
    SetVelocity(m_Velocity);

    // ---- aim: the mouse, the right stick / arrows, or keep the last direction
    if (c.MouseAim)
        m_Aim = SafeNormalize(GetMouseWorldPosition() - pos, m_Aim);
    else if (glm::length(c.Aim) > 0.2f)
        m_Aim = SafeNormalize(c.Aim, m_Aim);
    const float target = AngleOf(m_Aim);
    m_VisualAngle += Math::DeltaAngle(m_VisualAngle, target) * std::min(1.0f, 22.0f * dt);
    if (m_Visual)
    {
        auto& t = m_Visual.GetComponent<TransformComponent>();
        t.Rotation.z = m_VisualAngle;
        // a little squash in the direction of a dash
        const float stretch = m_DashLeft > 0.0f ? 1.18f : 1.0f;
        t.Scale = glm::vec3(1.35f / std::sqrt(stretch), 1.35f * stretch, 1.0f);
    }
    if (m_Reticle && m_Reticle.IsActive())
    {
        const glm::vec2 at = c.MouseAim ? GetMouseWorldPosition() : pos + m_Aim * 4.5f;
        auto& t = m_Reticle.GetComponent<TransformComponent>();
        t.Translation = glm::vec3(at, 0.0f);
        t.Rotation.z += dt * 1.5f;
    }

    // ---- fire
    m_FireCooldown -= dt;
    if (m_RapidTime > 0.0f)
        m_RapidTime = std::max(0.0f, m_RapidTime - dt);
    if (m_SpreadTime > 0.0f)
        m_SpreadTime = std::max(0.0f, m_SpreadTime - dt);
    const bool stickFire = !c.MouseAim && glm::length(c.Aim) > 0.5f;
    if ((c.Fire || stickFire) && m_FireCooldown <= 0.0f && m_DashLeft <= 0.0f)
    {
        Fire();
        m_FireCooldown = 1.0f / (FireRate * (m_RapidTime > 0.0f ? 1.9f : 1.0f));
    }

    // ---- effects: the thruster follows the speed, the shield shows invulnerability
    const float speed01 = std::clamp(glm::length(m_Velocity) / MoveSpeed, 0.0f, 1.5f);
    if (m_Thruster)
        if (auto* ps = m_Thruster.TryGetComponent<ParticleSystemComponent>())
            ps->EmissionRate = 15.0f + 70.0f * speed01;
    if (m_Invulnerable > 0.0f)
        m_Invulnerable = std::max(0.0f, m_Invulnerable - dt);
    if (m_Shield)
        if (auto* sprite = m_Shield.TryGetComponent<SpriteRendererComponent>())
        {
            const float pulse = 0.55f + 0.45f * std::sin(GetTime() * 18.0f);
            sprite->Color.a = m_Invulnerable > 0.0f ? std::min(1.0f, m_Invulnerable * 3.0f) * pulse : 0.0f;
        }
}

// A mouse button bound to "Fire" in the input map is held (whichever one it is: rebinding Fire in Project settings >
// Input moves the switch to mouse aiming with it; a key or a gamepad button of Fire does not switch).
static bool FirePressedByMouse()
{
    if (const InputAction* fire = Project::GetInputActionMap().FindAction("Fire"))
        for (const InputBinding& binding : fire->Bindings)
            if (binding.Source == InputSource::Mouse && Input::IsMouseButtonPressed((MouseButton)binding.Code))
                return true;
    return false;
}

PlayerShip::Controls PlayerShip::ReadInput()
{
    Controls c;
    c.Move = Input::GetVector("Move");
    c.Aim = Input::GetVector("Aim");
    c.Fire = Input::IsActionPressed("Fire");
    c.Dash = Input::IsActionJustPressed("Dash");
    // The mouse aims once it moves or fires with a mouse button; the stick / arrows take over when they are used.
    const glm::vec2 mouse = Input::GetMousePosition();
    if (glm::distance(mouse, m_LastMouse) > 2.0f || (c.Fire && FirePressedByMouse()))
        m_UsingMouse = true;
    if (glm::length(c.Aim) > 0.3f)
        m_UsingMouse = false;
    m_LastMouse = mouse;
    c.MouseAim = m_UsingMouse;
    return c;
}

void PlayerShip::Fire()
{
    ShooterGame* game = ShooterGame::Get();
    const glm::vec2 pos(m_Position);
    const glm::vec2 muzzle = pos + m_Aim * 0.75f;
    const float jitter = glm::radians(2.0f);
    static thread_local RandomGenerator rng(77);
    if (m_SpreadTime > 0.0f)
        for (int i = -1; i <= 1; i++)
            game->SpawnBullet(muzzle, Rotate(m_Aim, glm::radians(11.0f) * (float)i + rng.Range(-jitter, jitter)), true);
    else
        game->SpawnBullet(muzzle, Rotate(m_Aim, rng.Range(-jitter, jitter)), true);
    m_Velocity -= m_Aim * 0.5f; // recoil
    game->OnPlayerFired();
    game->SparksAt(muzzle, { 0.3f, 0.9f, 1.0f }, 2);
    game->Sfx("shoot", 0.28f, rng.Range(0.94f, 1.08f));
}

bool PlayerShip::Hurt(int damage, const glm::vec2& from)
{
    if (!IsAlive() || m_Invulnerable > 0.0f)
        return false;
    ShooterGame* game = ShooterGame::Get();
    m_Health = std::max(0, m_Health - damage);
    m_Invulnerable = 1.4f;
    const glm::vec2 pos(m_Position);
    m_Velocity += SafeNormalize(pos - from) * 12.0f; // knocked away
    if (m_Visual)
        if (auto* sprite = m_Visual.TryGetComponent<SpriteRendererComponent>())
            sprite->Flash({ 1.0f, 0.3f, 0.3f, 1.0f }, 0.35f);
    if (game)
        game->OnPlayerHurt(pos);
    if (m_Health <= 0)
    {
        SetVelocity({ 0.0f, 0.0f });
        if (game)
            game->OnPlayerDied(pos);
        SetActive(false); // gone until the next game
    }
    return true;
}

void PlayerShip::Heal(int amount)
{
    m_Health = std::min(MaxHealth, m_Health + amount);
}

void PlayerShip::GivePowerUp(PickupKind kind, float seconds)
{
    if (kind == PickupKind::Rapid)
        m_RapidTime = std::max(m_RapidTime, seconds);
    else if (kind == PickupKind::Spread)
        m_SpreadTime = std::max(m_SpreadTime, seconds);
}

// ------------------------------------------------------------------------------------------- bot

PlayerShip::Controls PlayerShip::ThinkBot(float dt)
{
    Controls c;
    ShooterGame* game = ShooterGame::Get();
    const glm::vec2 pos(m_Position);
    const glm::vec2 half = game->ArenaHalfSize;

    glm::vec2 push(0.0f);
    const Enemy* nearest = nullptr;
    float nearestDist = 1e9f;
    for (const Enemy* e : Live<Enemy>(*this))
    {
        if (e->IsDead())
            continue;
        const glm::vec2 d = e->GetPosition() - pos;
        const float dist = glm::length(d);
        if (dist < nearestDist)
        {
            nearestDist = dist;
            nearest = e;
        }
        const float reach = e->Kind == EnemyKind::Tank ? 8.0f : e->Kind == EnemyKind::Dasher ? 7.5f : 6.0f;
        if (dist < reach)
            push -= SafeNormalize(d) * (reach - dist) * (reach - dist) / reach;
    }
    // enemy bullets that would pass close: step out of their line
    bool danger = false;
    for (const Projectile* p : Live<Projectile>(*this))
    {
        if (p->FromPlayer)
            continue;
        const glm::vec2 rel = pos - glm::vec2(p->GetEntity().GetComponent<TransformComponent>().Translation);
        const glm::vec2 vel = p->GetDirection() * p->GetSpeed();
        const float speed2 = glm::dot(vel, vel);
        if (speed2 < 1e-3f || glm::length(rel) > 7.0f)
            continue;
        const float t = std::clamp(glm::dot(rel, vel) / speed2, 0.0f, 1.2f);
        const glm::vec2 miss = rel - vel * t; // from the bullet's closest point to the ship
        const float d = glm::length(miss);
        if (d < 1.8f)
        {
            push += SafeNormalize(miss, Rotate(SafeNormalize(vel), glm::half_pi<float>())) * (1.8f - d) * 4.0f;
            danger = danger || (t < 0.25f && d < 0.8f);
        }
    }
    // the arena's walls
    const float margin = 3.0f;
    if (pos.x > half.x - margin) push.x -= (pos.x - (half.x - margin)) * 1.5f;
    if (pos.x < -half.x + margin) push.x += (-half.x + margin - pos.x) * 1.5f;
    if (pos.y > half.y - margin) push.y -= (pos.y - (half.y - margin)) * 1.5f;
    if (pos.y < -half.y + margin) push.y += (-half.y + margin - pos.y) * 1.5f;
    // keep at a fighting distance from the nearest enemy, circling it
    if (nearest)
    {
        const glm::vec2 to = SafeNormalize(nearest->GetPosition() - pos);
        if (nearestDist > 9.0f)
            push += to * 0.8f;
        m_BotWander += dt * 0.4f;
        push += Rotate(to, glm::half_pi<float>()) * (std::sin(m_BotWander) > 0.0f ? 0.7f : -0.7f);
    }
    // bonuses when it is safe
    if (nearestDist > 4.0f)
    {
        const Pickup* best = nullptr;
        float bestDist = 12.0f;
        for (const Pickup* p : Live<Pickup>(*this))
        {
            const float d = glm::distance(glm::vec2(p->GetEntity().GetComponent<TransformComponent>().Translation), pos);
            if (d < bestDist)
            {
                bestDist = d;
                best = p;
            }
        }
        if (best)
            push += SafeNormalize(glm::vec2(best->GetEntity().GetComponent<TransformComponent>().Translation) - pos) * 1.6f;
    }
    push -= pos * 0.03f; // drift back towards the middle
    // obstacles ahead: slide along them
    if (glm::length(push) > 0.05f)
    {
        const glm::vec2 dir = SafeNormalize(push);
        RaycastHit hit = Raycast2D(pos, dir, 2.2f, LayerMask({ "Wall" }));
        if (hit)
        {
            const glm::vec2 n(hit.Normal);
            push = SafeNormalize(dir - n * glm::dot(dir, n) * 1.2f, Rotate(dir, glm::half_pi<float>())) * glm::length(push);
        }
    }
    c.Move = glm::length(push) > 0.15f ? SafeNormalize(push) * std::min(1.0f, glm::length(push)) : glm::vec2(0.0f);

    // aim ahead of the nearest enemy (its speed x the bullet's flight time)
    if (nearest)
    {
        const float flight = nearestDist / 26.0f;
        c.Aim = nearest->GetPosition() + nearest->GetMoveVelocity() * flight - pos;
        c.Fire = nearestDist < 22.0f;
    }
    c.Dash = (nearest && nearestDist < 1.8f && !nearest->IsSpawning()) || danger;
    if (c.Dash && glm::length(c.Move) < 0.2f)
        c.Move = SafeNormalize(push, { 1.0f, 0.0f });
    return c;
}

ZM_REGISTER_SCRIPT(PlayerShip,
    ZM_FIELD_RANGE(PlayerShip, MoveSpeed, 1.0f, 20.0f).Header("Harakat").Tooltip("Uchish tezligi (birlik/s)"),
    ZM_FIELD_RANGE(PlayerShip, Acceleration, 1.0f, 40.0f).Tooltip("Tezlikka qanchalik tez yetadi"),
    ZM_FIELD_RANGE(PlayerShip, DashSpeed, 5.0f, 60.0f).Header("Siltanish"),
    ZM_FIELD_RANGE(PlayerShip, DashTime, 0.05f, 0.5f),
    ZM_FIELD_RANGE(PlayerShip, DashCooldown, 0.1f, 3.0f),
    ZM_FIELD_RANGE(PlayerShip, FireRate, 1.0f, 30.0f).Header("Qurol").Tooltip("Sekundiga o'qlar"),
    ZM_FIELD_RANGE(PlayerShip, MaxHealth, 1, 10).Header("Jon"))
