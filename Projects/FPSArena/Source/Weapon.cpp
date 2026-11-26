#include "Arena.h"

#include <algorithm>
#include <cmath>

using namespace Zamin;

// The weapon (Blaster): a child of the camera. While "Fire" is held it fires FireRate times a second:
// a ray is cast from the eye along the camera's direction (Raycast3DAll - the player's own capsule is skipped),
// the spread grows when moving / in the air. Every shot: the view kicks (FirstPersonController::AddRecoil), the weapon
// jerks back, a flash and a light at the muzzle, a tracer from the eye to the hit point (TracerFx), sparks and a flash
// of light at the hit point; a dynamic body (a crate) is pushed, a target falls. When the ammo runs out or "Reload" is
// pressed it reloads. Close to a wall the weapon is pulled back (it does not go into the wall).
// The bot: picks a near, visible target (one the ray reaches), looks at it and fires when the aim is on it.

void Blaster::OnCreate()
{
    m_Rest = Transform().Translation;
    m_Ammo = MagazineSize;
    m_Rng.SetSeed(0xb1a57e5ull);
}

void Blaster::Refill()
{
    m_Ammo = MagazineSize;
    m_ReloadTimer = 0.0f;
    m_Cooldown = 0.0f;
    m_BotTarget = nullptr;
}

float Blaster::GetReload01() const
{
    return m_ReloadTimer > 0.0f ? 1.0f - m_ReloadTimer / std::max(ReloadTime, 0.01f) : 1.0f;
}

void Blaster::StartReload()
{
    if (m_ReloadTimer > 0.0f || m_Ammo >= MagazineSize)
        return;
    m_ReloadTimer = ReloadTime;
    PlaySound("Audio/reload.wav", 0.6f, 1.0f);
}

Target* Blaster::FindTarget(Entity hit) const
{
    for (Entity e = hit; e; e = GetScene().GetParent(e))
        if (Target* t = GetScript<Target>(e))
            return t;
    return nullptr;
}

void Blaster::OnLateUpdate(Timestep ts)
{
    const float dt = ts;
    FirstPersonController* fpc = GetScript<FirstPersonController>(Resolve(Player));
    if (!fpc || dt <= 0.0f)
        return;
    m_Cooldown = std::max(0.0f, m_Cooldown - dt);
    m_BotCalm = std::max(0.0f, m_BotCalm - dt);
    if (m_ReloadTimer > 0.0f)
    {
        m_ReloadTimer -= dt;
        if (m_ReloadTimer <= 0.0f)
        {
            m_ReloadTimer = 0.0f;
            m_Ammo = MagazineSize;
            PlaySound("Audio/reload_done.wav", 0.5f, 1.0f);
        }
    }

    // ---- what the shooter wants
    bool wantFire = false, wantReload = false;
    if (fpc->IsBot())
    {
        if (m_FireEnabled)
            ThinkBot(*fpc, dt, wantFire);
        else
            fpc->SetAimPoint(std::nullopt);
    }
    else if (m_FireEnabled)
    {
        fpc->SetAimPoint(std::nullopt);
        wantFire = Input::IsActionPressed("Fire");
        wantReload = Input::IsActionJustPressed("Reload");
    }
    if (wantReload)
        StartReload();
    if (wantFire && m_Cooldown <= 0.0f && m_ReloadTimer <= 0.0f)
    {
        if (m_Ammo > 0)
            Fire(*fpc);
        else
        {
            PlaySound("Audio/empty.wav", 0.5f, 1.0f);
            m_Cooldown = 0.25f;
            StartReload();
        }
    }

    // ---- the gun: kick (a spring), sway with the walk, the reload dip, pulled back near walls
    // (a stiff spring: integrated in small steps, so a slow frame cannot make it blow up)
    for (float left = std::min(dt, 0.25f); left > 0.0f; left -= 1.0f / 240.0f)
    {
        const float h = std::min(left, 1.0f / 240.0f);
        m_KickVelocity += (-m_Kick * 260.0f - m_KickVelocity * 22.0f) * h;
        m_Kick += m_KickVelocity * h;
    }
    m_Sway += dt * (1.0f + fpc->GetHorizontalSpeed() * 1.4f);
    const float walk = std::clamp(fpc->GetHorizontalSpeed() / 9.0f, 0.0f, 1.0f) * (fpc->IsGrounded() ? 1.0f : 0.3f);
    const float reload = m_ReloadTimer > 0.0f ? std::sin(glm::pi<float>() * GetReload01()) : 0.0f;
    if (Entity head = fpc->GetHead())
    {
        const glm::mat4 world = GetScene().GetWorldTransform(head);
        const glm::vec3 eye(world[3]);
        const glm::vec3 forward = -glm::normalize(glm::vec3(world[2]));
        float room = 2.0f;
        for (const RaycastHit& h : Raycast3DAll(eye, forward, 1.2f))
            if (h.EntityID != fpc->GetEntity().GetUUID())
            {
                room = h.Distance;
                break;
            }
        m_WallPull = Math::Damp(m_WallPull, std::clamp(1.0f - (room - 0.35f) / 0.7f, 0.0f, 1.0f), 14.0f, dt);
    }
    auto& t = Transform();
    t.Translation = m_Rest + glm::vec3(std::sin(m_Sway * 0.5f) * 0.012f * walk,
                                       -std::abs(std::cos(m_Sway * 0.5f)) * 0.014f * walk - reload * 0.12f - m_WallPull * 0.08f,
                                       m_Kick * 0.09f + m_WallPull * 0.3f);
    t.Rotation = { m_Kick * 0.55f - reload * 0.55f + m_WallPull * 0.5f, reload * 0.25f, reload * 0.6f };

    // ---- muzzle flash and the impact light fade
    m_FlashTimer = std::max(0.0f, m_FlashTimer - dt);
    if (Entity flash = Resolve(MuzzleFlash))
    {
        const float s = m_FlashTimer > 0.0f ? m_Rng.Range(0.07f, 0.11f) : 0.0001f;
        auto& ft = flash.GetComponent<TransformComponent>();
        ft.Scale = glm::vec3(s);
        ft.Rotation.z = m_Rng.Range(0.0f, 6.28f);
    }
    if (Entity muzzle = Resolve(Muzzle))
        if (auto* light = muzzle.TryGetComponent<PointLightComponent>())
            light->Intensity = m_FlashTimer > 0.0f ? 7.0f : 0.0f;
    m_LightTimer = std::max(0.0f, m_LightTimer - dt);
    if (Entity impact = Resolve(ImpactLight))
        if (auto* light = impact.TryGetComponent<PointLightComponent>())
            light->Intensity = 9.0f * (m_LightTimer / 0.12f);
}

void Blaster::Fire(FirstPersonController& fpc)
{
    Entity head = fpc.GetHead();
    if (!head)
        return;
    m_Ammo--;
    m_Cooldown = 1.0f / std::max(FireRate, 0.1f);

    // the ray from the eye, spread by movement
    const glm::mat4 world = GetScene().GetWorldTransform(head);
    const glm::vec3 eye(world[3]);
    const glm::vec3 forward = -glm::normalize(glm::vec3(world[2]));
    const glm::vec3 right = glm::normalize(glm::vec3(world[0]));
    const glm::vec3 up = glm::normalize(glm::vec3(world[1]));
    const float spread = Spread + std::clamp(fpc.GetHorizontalSpeed() / 9.0f, 0.0f, 1.0f) * 0.018f + (fpc.IsGrounded() ? 0.0f : 0.03f);
    const glm::vec3 dir = glm::normalize(forward + right * m_Rng.Gaussian(0.0f, spread) + up * m_Rng.Gaussian(0.0f, spread));

    RaycastHit hit;
    for (const RaycastHit& h : Raycast3DAll(eye, dir, Range))
        if (h.EntityID != fpc.GetEntity().GetUUID())
        {
            hit = h;
            break;
        }
    const glm::vec3 end = hit ? hit.Point : eye + dir * Range;

    // the beam from the barrel's tip
    glm::vec3 muzzle = eye + forward * 0.6f;
    if (Entity m = Resolve(Muzzle))
        muzzle = GetScene().GetWorldPosition(m);
    if (Entity tpl = Resolve(TracerTemplate))
    {
        Entity beam = Instantiate(tpl, muzzle);
        if (TracerFx* fx = GetScript<TracerFx>(beam))
            fx->Setup(muzzle, end, { 0.5f, 2.6f, 4.0f });
    }

    bool scored = false;
    if (hit)
    {
        Entity other = GetScene().GetEntityByUUID(hit.EntityID);
        Target* target = other ? FindTarget(other) : nullptr;
        if (target && target->IsUp())
        {
            bool bullseye = false;
            const int points = target->Hit(hit.Point, dir, bullseye);
            if (points > 0)
            {
                scored = true;
                if (ArenaGame* game = ArenaGame::Get())
                    game->OnTargetHit(*target, points, bullseye);
            }
        }
        Impact(hit.Point, hit.Normal, scored);
        if (scored && target && target->GetKind() == TargetKind::Drone)
        {
            // a bursting drone: a big shower of sparks
            if (Entity sparks = Resolve(ImpactSparks))
                GetScene().EmitParticles(sparks, 40);
        }
        // a crate (a dynamic body) takes the shot's push
        if (other)
            if (auto* rb = other.TryGetComponent<Rigidbody3DComponent>(); rb && rb->Type == Rigidbody3DComponent::BodyType::Dynamic)
            {
                WakeUp3D(other);
                ApplyImpulse3D(other, dir * PushImpulse);
            }
    }
    if (ArenaGame* game = ArenaGame::Get())
        game->OnShot(scored);

    // the shot's feel: the view kicks, the gun jumps back, the muzzle flashes
    const float kick = fpc.IsBot() ? 0.45f : 1.0f;
    fpc.AddRecoil(Recoil * kick, m_Rng.Range(-0.35f, 0.35f) * Recoil * kick);
    m_KickVelocity += 7.5f;
    m_FlashTimer = 0.045f;
    PlaySound("Audio/shot.wav", 0.5f, m_Rng.Range(0.94f, 1.06f));
    if (!fpc.IsBot())
        ShakeCamera(0.12f, 0.08f, 30.0f);
    m_BotJitter = glm::vec3(m_Rng.Gaussian(0.0f, 0.09f), m_Rng.Gaussian(0.0f, 0.09f), m_Rng.Gaussian(0.0f, 0.09f));
    if (m_Ammo == 0)
        StartReload();
}

void Blaster::Impact(const glm::vec3& point, const glm::vec3& normal, bool target)
{
    const glm::vec3 n = glm::length(normal) > 0.1f ? glm::normalize(normal) : glm::vec3(0.0f, 1.0f, 0.0f);
    if (Entity sparks = Resolve(ImpactSparks))
    {
        sparks.GetComponent<TransformComponent>().Translation = point + n * 0.04f;
        if (auto* ps = sparks.TryGetComponent<ParticleSystemComponent>())
        {
            ps->Direction = n;
            ps->StartColor = target ? glm::vec4(3.0f, 2.2f, 0.8f, 1.0f) : glm::vec4(1.2f, 2.4f, 3.2f, 1.0f);
            ps->EndColor = target ? glm::vec4(1.0f, 0.3f, 0.05f, 0.0f) : glm::vec4(0.2f, 0.5f, 1.0f, 0.0f);
        }
        GetScene().EmitParticles(sparks, target ? 26 : 12);
    }
    if (Entity light = Resolve(ImpactLight))
    {
        light.GetComponent<TransformComponent>().Translation = point + n * 0.35f;
        if (auto* l = light.TryGetComponent<PointLightComponent>())
            l->Color = target ? glm::vec3(1.0f, 0.7f, 0.3f) : glm::vec3(0.4f, 0.75f, 1.0f);
        m_LightTimer = 0.12f;
    }
    PlaySoundAt(target ? "Audio/hit_target.wav" : "Audio/hit_wall.wav", point, target ? 0.8f : 0.45f, m_Rng.Range(0.9f, 1.1f));
}

// ------------------------------------------------------------------------------------------- bot

void Blaster::ThinkBot(FirstPersonController& fpc, float dt, bool& wantFire)
{
    Entity head = fpc.GetHead();
    if (!head)
        return;
    const glm::mat4 world = GetScene().GetWorldTransform(head);
    const glm::vec3 eye(world[3]);
    const glm::vec3 forward = -glm::normalize(glm::vec3(world[2]));
    const UUID self = fpc.GetEntity().GetUUID();

    auto visible = [&](const Target& t, const glm::vec3& aim) {
        const glm::vec3 to = aim - eye;
        const float distance = glm::length(to);
        if (distance < 0.5f)
            return false;
        for (const RaycastHit& h : Raycast3DAll(eye, to / distance, distance + 0.6f))
            if (h.EntityID != self)
                return t.Owns(GetScene().GetEntityByUUID(h.EntityID));
        return false;
    };

    // the locked target went down or out of sight: a short pause, then the next
    m_BotLock += dt;
    if (m_BotTarget && (!m_BotTarget->IsUp() || m_BotLock > 3.0f))
    {
        m_BotTarget = nullptr;
        m_BotCalm = 0.3f;
    }
    m_BotSearch -= dt;
    if (m_BotSearch <= 0.0f)
    {
        m_BotSearch = 0.2f;
        if (m_BotTarget && !visible(*m_BotTarget, m_BotTarget->GetAimPoint()))
            m_BotTarget = nullptr;
        if (!m_BotTarget && m_BotCalm <= 0.0f)
        {
            // the nearest target in view, preferring what is already in front
            float best = 1e9f;
            for (Target* t : FindObjectsOfType<Target>())
            {
                if (!t->IsActiveInHierarchy() || !t->IsUp())
                    continue;
                const glm::vec3 aim = t->GetAimPoint();
                const glm::vec3 to = aim - eye;
                const float distance = glm::length(to);
                if (distance > 32.0f || distance < 1.5f)
                    continue;
                const float angle = std::acos(std::clamp(glm::dot(forward, to / distance), -1.0f, 1.0f));
                const float score = distance + angle * 9.0f;
                if (score < best && visible(*t, aim))
                {
                    best = score;
                    m_BotTarget = t;
                }
            }
            if (m_BotTarget)
                m_BotLock = 0.0f;
        }
    }
    if (!m_BotTarget)
    {
        fpc.SetAimPoint(std::nullopt);
        return;
    }
    // aim (with a little wobble, re-rolled every shot: it misses now and then) and fire when on it
    const glm::vec3 aim = m_BotTarget->GetAimPoint() + m_BotJitter;
    fpc.SetAimPoint(aim);
    const glm::vec3 to = glm::normalize(aim - eye);
    const float error = std::acos(std::clamp(glm::dot(forward, to), -1.0f, 1.0f));
    if (error < glm::radians(1.6f) && m_BotCalm <= 0.0f)
        wantFire = true;
}

ZM_REGISTER_SCRIPT(Blaster,
    ZM_FIELD(Blaster, Player).Header("Objects").Tooltip("FirstPersonController (the player)"),
    ZM_FIELD(Blaster, Muzzle).Tooltip("Muzzle: where the flash light and the tracer start"),
    ZM_FIELD(Blaster, MuzzleFlash),
    ZM_FIELD(Blaster, TracerTemplate).Tooltip("Tracer template (TracerFx)"),
    ZM_FIELD(Blaster, ImpactSparks).Tooltip("Sparks at the hit point (a particle system)"),
    ZM_FIELD(Blaster, ImpactLight),
    ZM_FIELD_RANGE(Blaster, FireRate, 0.5f, 20.0f).Header("Weapon").Tooltip("Shots / second"),
    ZM_FIELD_RANGE(Blaster, MagazineSize, 1, 100),
    ZM_FIELD_RANGE(Blaster, ReloadTime, 0.1f, 5.0f),
    ZM_FIELD_RANGE(Blaster, Range, 5.0f, 500.0f),
    ZM_FIELD_RANGE(Blaster, Spread, 0.0f, 0.1f).Tooltip("Spread (radians) - standing; more when walking / in the air"),
    ZM_FIELD_RANGE(Blaster, Recoil, 0.0f, 0.2f).Tooltip("How much the view kicks up per shot (radians)"),
    ZM_FIELD_RANGE(Blaster, PushImpulse, 0.0f, 100.0f).Tooltip("Impulse given to a dynamic body (a crate)"))
