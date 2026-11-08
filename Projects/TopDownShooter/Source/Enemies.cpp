#include "Shooter.h"

#include <algorithm>
#include <cmath>

using namespace Zamin;
using namespace NeonUtil;

// Dushmanlar (bitta Enemy klassi, Kind maydoni bilan), o'qlar (Projectile), bonuslar (Pickup) va qisqa
// effektlar (FxAnimator). Har bir dushman portaldan "o'sib" chiqadi (SpawnTime), o'q tekkanda oq chaqnaydi va
// orqaga suriladi; o'lganda ShooterGame::OnEnemyKilled (portlash, ochko, kombo, ba'zan bonus).

// ------------------------------------------------------------------------------------------- Enemy

void Enemy::OnCreate()
{
    m_Visual = FindChild("Visual");
    if (m_Visual)
        m_VisualScale = m_Visual.GetComponent<TransformComponent>().Scale;
    m_MaxHealth = Health;
    m_Position = GetWorldPosition();
    // every enemy weaves / circles a little differently
    static thread_local RandomGenerator rng(4242);
    m_Phase = rng.Range(0.0f, glm::two_pi<float>());
    m_Orbit = rng.Sign();
    m_Timer = rng.Range(1.0f, 2.2f);
}

void Enemy::OnUpdate(Timestep ts)
{
    const float dt = ts;
    ShooterGame* game = ShooterGame::Get();
    if (m_Dead || !game || dt <= 0.0f)
        return;
    m_Age += dt;
    m_Position = GetWorldPosition();
    const glm::vec2 pos(m_Position);

    // grows out of its portal
    if (m_Visual)
    {
        const float grow = std::clamp(m_Age / SpawnTime, 0.0f, 1.0f);
        const float s = ApplyEase(Ease::OutBack, grow);
        m_Visual.GetComponent<TransformComponent>().Scale = m_VisualScale * std::max(0.05f, s);
    }

    PlayerShip* ship = game->GetPlayer();
    const bool target = ship && ship->IsAlive();
    // with no ship to chase they drift towards the middle
    const glm::vec2 goal = target ? ship->GetPosition() : glm::vec2(std::sin(m_Phase + m_Age * 0.3f) * 6.0f, std::cos(m_Phase + m_Age * 0.2f) * 4.0f);
    const glm::vec2 toGoal = goal - pos;
    const float distance = glm::length(toGoal);

    glm::vec2 desired = Steer(toGoal, distance, dt);
    if (!target && Kind != EnemyKind::Dasher)
        desired *= 0.4f;
    m_Knock = Math::Damp(m_Knock, glm::vec2(0.0f), 6.0f, dt);
    const float accel = Kind == EnemyKind::Tank ? 2.5f : Kind == EnemyKind::Swarmer ? 9.0f : 6.0f;
    m_Velocity = Math::Damp(m_Velocity, desired, accel, dt);
    SetVelocity(m_Velocity + m_Knock);

    // touching the ship hurts it (again after its invulnerability, while still touching)
    if (m_TouchingPlayer && target && !IsSpawning())
        if (ship->Hurt(1, pos) && (Kind == EnemyKind::Chaser || Kind == EnemyKind::Swarmer))
            Kill(false); // kamikaze
}

glm::vec2 Enemy::Steer(const glm::vec2& toPlayer, float distance, float dt)
{
    const glm::vec2 dir = SafeNormalize(toPlayer);
    m_Phase += dt;
    switch (Kind)
    {
        case EnemyKind::Chaser:
            Face(dir, dt, 8.0f);
            return dir * Speed;
        case EnemyKind::Swarmer:
        {
            // weaving towards the ship
            const glm::vec2 side = Rotate(dir, glm::half_pi<float>()) * std::sin(m_Phase * 5.0f) * 0.75f;
            const glm::vec2 v = SafeNormalize(dir + side) * Speed;
            Face(v, dt, 14.0f);
            return v;
        }
        case EnemyKind::Shooter:
        {
            // keeps 6..9 units away, circling; fires a spread every few seconds (a white flash before)
            float radial = distance > 9.0f ? 1.0f : distance < 6.0f ? -1.0f : 0.0f;
            const glm::vec2 v = dir * radial * Speed + Rotate(dir, glm::half_pi<float>()) * m_Orbit * Speed * 0.6f;
            Face(dir, dt, 6.0f);
            m_Timer -= dt;
            if (m_Timer < 0.25f && m_Timer + dt >= 0.25f && m_Visual)
                if (auto* sprite = m_Visual.TryGetComponent<SpriteRendererComponent>())
                    sprite->Flash({ 1.0f, 1.0f, 1.0f, 0.8f }, 0.25f);
            if (m_Timer <= 0.0f && !IsSpawning() && distance < 16.0f)
            {
                const int wave = ShooterGame::Get()->GetWave();
                FireAt(glm::vec2(m_Position) + toPlayer, wave >= 6 ? 3 : 1, 14.0f);
                m_Timer = std::max(1.2f, 2.3f - 0.06f * (float)wave);
                if (std::sin(m_Phase * 0.7f) > 0.6f)
                    m_Orbit = -m_Orbit;
            }
            return v;
        }
        case EnemyKind::Dasher:
        {
            m_Timer -= dt;
            switch (m_DashPhase)
            {
                case 0: // approach until in range
                    Face(dir, dt, 8.0f);
                    if (distance < 8.0f && m_Timer <= 0.0f && !IsSpawning())
                    {
                        m_DashPhase = 1;
                        m_Timer = 0.55f;
                        if (m_Visual)
                            if (auto* sprite = m_Visual.TryGetComponent<SpriteRendererComponent>())
                                sprite->Flash({ 1.0f, 0.9f, 0.4f, 0.9f }, 0.55f);
                    }
                    return dir * Speed;
                case 1: // wind up: stops, shivers, aims
                    Face(dir, dt, 12.0f);
                    m_DashDir = dir;
                    if (m_Visual)
                        m_Visual.GetComponent<TransformComponent>().Translation.x = std::sin(m_Age * 70.0f) * 0.06f;
                    if (m_Timer <= 0.0f)
                    {
                        m_DashPhase = 2;
                        m_Timer = 0.42f;
                        ShooterGame::Get()->Sfx("dash", 0.4f, 1.3f);
                    }
                    return glm::vec2(0.0f);
                case 2: // charge
                    if (m_Visual)
                        m_Visual.GetComponent<TransformComponent>().Translation.x = 0.0f;
                    m_Velocity = m_DashDir * Speed * 4.6f; // no easing into it
                    if (m_Timer <= 0.0f)
                    {
                        m_DashPhase = 3;
                        m_Timer = 0.7f;
                    }
                    return m_DashDir * Speed * 4.6f;
                default: // recover
                    if (m_Timer <= 0.0f)
                    {
                        m_DashPhase = 0;
                        m_Timer = 0.6f;
                    }
                    return dir * Speed * 0.3f;
            }
        }
        case EnemyKind::Tank:
        {
            Face(dir, dt, 1.5f);
            m_Timer -= dt;
            if (m_Timer <= 0.0f && !IsSpawning())
            {
                FireRing(ShooterGame::Get()->GetWave() >= 10 ? 14 : 10);
                m_Timer = 3.2f;
            }
            return dir * Speed;
        }
    }
    return glm::vec2(0.0f);
}

void Enemy::Face(const glm::vec2& direction, float dt, float turnSpeed)
{
    if (glm::length(direction) < 1e-3f || !m_Visual)
        return;
    m_Angle += Math::DeltaAngle(m_Angle, AngleOf(direction)) * std::min(1.0f, turnSpeed * dt);
    m_Visual.GetComponent<TransformComponent>().Rotation.z = m_Angle;
}

void Enemy::FireAt(const glm::vec2& target, int shots, float spreadDegrees)
{
    ShooterGame* game = ShooterGame::Get();
    const glm::vec2 pos(m_Position);
    const glm::vec2 dir = SafeNormalize(target - pos);
    for (int i = 0; i < shots; i++)
    {
        const float offset = shots > 1 ? ((float)i / (float)(shots - 1) - 0.5f) * glm::radians(spreadDegrees * 2.0f) : 0.0f;
        game->SpawnBullet(pos + dir * 0.7f, Rotate(dir, offset), false);
    }
    game->Sfx("enemy_shoot", 0.45f, 1.0f);
}

void Enemy::FireRing(int shots)
{
    ShooterGame* game = ShooterGame::Get();
    const glm::vec2 pos(m_Position);
    for (int i = 0; i < shots; i++)
    {
        const glm::vec2 dir = Rotate({ 1.0f, 0.0f }, glm::two_pi<float>() * ((float)i + 0.5f * std::sin(m_Phase)) / (float)shots);
        game->SpawnBullet(pos + dir * 1.1f, dir, false, 0.8f);
    }
    game->Sfx("enemy_shoot", 0.6f, 0.7f);
    if (m_Visual)
        if (auto* sprite = m_Visual.TryGetComponent<SpriteRendererComponent>())
            sprite->Flash({ 1.0f, 0.8f, 0.5f, 0.8f }, 0.3f);
}

void Enemy::Damage(float amount, const glm::vec2& direction)
{
    if (m_Dead)
        return;
    Health -= amount;
    const float weight = Kind == EnemyKind::Tank ? 0.25f : Kind == EnemyKind::Swarmer ? 1.6f : 1.0f;
    m_Knock += SafeNormalize(direction) * 4.0f * weight;
    if (m_Visual)
        if (auto* sprite = m_Visual.TryGetComponent<SpriteRendererComponent>())
            sprite->Flash({ 1.0f, 1.0f, 1.0f, 1.0f }, 0.09f);
    if (ShooterGame* game = ShooterGame::Get())
    {
        game->SparksAt(glm::vec2(m_Position) - SafeNormalize(direction) * 0.4f, Color, 6);
        game->Sfx("hit", 0.35f, 0.9f + 0.3f * std::clamp(Health / std::max(m_MaxHealth, 0.01f), 0.0f, 1.0f));
    }
    if (Health <= 0.0f)
        Kill(true);
}

void Enemy::Kill(bool scored)
{
    if (m_Dead)
        return;
    m_Dead = true;
    if (ShooterGame* game = ShooterGame::Get())
        game->OnEnemyKilled(*this, scored);
    Destroy();
}

void Enemy::OnCollisionBegin(Entity other)
{
    if (GetScript<PlayerShip>(other))
        m_TouchingPlayer = true;
}

void Enemy::OnCollisionEnd(Entity other)
{
    if (GetScript<PlayerShip>(other))
        m_TouchingPlayer = false;
}

// -------------------------------------------------------------------------------------- Projectile

void Projectile::Launch(const glm::vec2& direction, float speedScale)
{
    m_Direction = SafeNormalize(direction);
    m_Speed = Speed * speedScale;
    SetVelocity(m_Direction * m_Speed);
    if (Entity visual = FindChild("Visual"))
        visual.GetComponent<TransformComponent>().Rotation.z = AngleOf(m_Direction);
}

void Projectile::OnUpdate(Timestep ts)
{
    if (m_Spent)
        return;
    m_Age += ts;
    if (m_Age >= Lifetime)
    {
        m_Spent = true;
        Destroy();
    }
}

void Projectile::OnTriggerBegin(Entity other)
{
    if (m_Spent)
        return;
    const glm::vec2 pos(GetWorldPosition());
    if (FromPlayer)
    {
        if (Enemy* enemy = GetScript<Enemy>(other))
        {
            if (enemy->IsDead())
                return;
            enemy->Damage(Damage, m_Direction);
            Spend(pos, false);
            return;
        }
    }
    else if (PlayerShip* ship = GetScript<PlayerShip>(other))
    {
        if (ship->Hurt((int)Damage, pos))
            Spend(pos, false);
        return; // a dash passes through
    }
    // anything else on its layers is a wall
    if (!GetScript<Enemy>(other) && !GetScript<PlayerShip>(other))
        Spend(pos, true);
}

void Projectile::Spend(const glm::vec2& where, bool sparks)
{
    m_Spent = true;
    if (sparks)
        if (ShooterGame* game = ShooterGame::Get())
            game->SparksAt(where, FromPlayer ? glm::vec3(0.3f, 0.9f, 1.0f) : glm::vec3(1.0f, 0.5f, 0.2f), 5);
    Destroy();
}

// ---------------------------------------------------------------------------------------- Pickup

void Pickup::OnCreate()
{
    m_Base = Transform().Translation;
    m_Ring = FindChild("Halqa");
    m_Visual = FindChild("Visual");
}

void Pickup::OnUpdate(Timestep ts)
{
    if (m_Taken)
        return;
    m_Age += ts;
    // bobs, its ring turns; blinks in its last seconds
    if (m_Visual)
    {
        auto& t = m_Visual.GetComponent<TransformComponent>();
        t.Translation.y = std::sin(m_Age * 3.0f) * 0.12f;
        const float pop = std::min(1.0f, m_Age * 4.0f);
        t.Scale = glm::vec3(0.95f * ApplyEase(Ease::OutBack, pop));
    }
    if (m_Ring)
        m_Ring.GetComponent<TransformComponent>().Rotation.z += ts * 2.0f;
    const float left = Lifetime - m_Age;
    const bool visible = left > 2.5f || std::fmod(m_Age * 8.0f, 1.0f) < 0.6f;
    if (m_Visual)
        m_Visual.GetComponent<SpriteRendererComponent>().Color.a = visible ? 1.0f : 0.15f;
    if (left <= 0.0f)
    {
        m_Taken = true;
        Destroy();
    }
}

void Pickup::OnTriggerBegin(Entity other)
{
    if (m_Taken)
        return;
    PlayerShip* ship = GetScript<PlayerShip>(other);
    if (!ship || !ship->IsAlive())
        return;
    m_Taken = true;
    if (ShooterGame* game = ShooterGame::Get())
        game->OnPickupCollected(Kind, glm::vec2(GetWorldPosition()));
    Destroy();
}

// ------------------------------------------------------------------------------------- FxAnimator

void FxAnimator::OnCreate()
{
    m_Age = 0.0f;
}

void FxAnimator::OnUpdate(Timestep ts)
{
    if (m_Age == 0.0f)
        m_Color = GetColor(); // the colour the spawner gave it
    m_Age += ts;
    const float t = std::clamp(m_Age / std::max(Lifetime, 0.01f), 0.0f, 1.0f);
    const float eased = Pop ? ApplyEase(Ease::OutBack, std::min(1.0f, t * 3.0f)) : ApplyEase(Ease::OutCubic, t);
    const float scale = (StartScale + (EndScale - StartScale) * eased) * m_Multiplier;
    auto& tr = Transform();
    tr.Scale = glm::vec3(scale, scale, 1.0f);
    tr.Translation.y += Rise * (1.0f - t) * ts;
    tr.Rotation.z += Spin * ts;
    float alpha = 1.0f;
    if (FadeIn > 0.0f)
        alpha = std::min(alpha, m_Age / FadeIn);
    if (FadeOut > 0.0f)
        alpha = std::min(alpha, (Lifetime - m_Age) / FadeOut);
    SetColor(glm::vec4(glm::vec3(m_Color), m_Color.a * std::clamp(alpha, 0.0f, 1.0f)));
    if (m_Age >= Lifetime)
        Destroy();
}

ZM_REGISTER_SCRIPT(Enemy,
    ZM_FIELD_ENUM(Enemy, Kind, "Quvg'in", "To'da", "O'qchi", "Hujumchi", "Tank").Label("Turi"),
    ZM_FIELD_RANGE(Enemy, Health, 0.5f, 100.0f).Label("Jon"),
    ZM_FIELD_RANGE(Enemy, Speed, 0.5f, 15.0f).Label("Tezlik"),
    ZM_FIELD_RANGE(Enemy, Score, 0, 5000).Label("Ochko"),
    ZM_FIELD_COLOR(Enemy, Color).Label("Effekt rangi"))

ZM_REGISTER_SCRIPT(Projectile,
    ZM_FIELD_RANGE(Projectile, Damage, 0.1f, 10.0f),
    ZM_FIELD_RANGE(Projectile, Lifetime, 0.1f, 10.0f),
    ZM_FIELD_RANGE(Projectile, Speed, 1.0f, 60.0f),
    ZM_FIELD(Projectile, FromPlayer).Tooltip("O'yinchining o'qi (dushmanlarga tegadi); aks holda dushmanniki"))

ZM_REGISTER_SCRIPT(Pickup,
    ZM_FIELD_ENUM(Pickup, Kind, "Jon", "Tezotar", "Uch o'q").Label("Bonus"),
    ZM_FIELD_RANGE(Pickup, Lifetime, 1.0f, 30.0f))

ZM_REGISTER_SCRIPT(FxAnimator,
    ZM_FIELD_RANGE(FxAnimator, Lifetime, 0.05f, 5.0f),
    ZM_FIELD(FxAnimator, StartScale),
    ZM_FIELD(FxAnimator, EndScale),
    ZM_FIELD(FxAnimator, Rise),
    ZM_FIELD(FxAnimator, Spin),
    ZM_FIELD(FxAnimator, FadeIn),
    ZM_FIELD(FxAnimator, FadeOut),
    ZM_FIELD(FxAnimator, Pop))
