#include "Arena.h"

#include <algorithm>
#include <cmath>
#include <sstream>

using namespace Zamin;

// Birinchi shaxs boshqaruvchisi. Tana (Character Controller 3D) faqat aylanadi (yaw), bosh / kamera bolasi esa
// yuqoriga-pastga qaraydi (pitch). Boshqaruv (Loyiha sozlamalari > Input): "Move" vektori, "Look" vektori (o'ng stik),
// sichqoncha harakati (kursor qulflangan), "Jump", "Sprint", "Crouch".
// Sakrash: yerdan chiqqandan keyin ham 0.12 s ichida sakrash mumkin ("coyote time"), yerga tushishdan oldinroq
// bosilgan sakrash ham hisobga olinadi (bufer). Yugurish chidamlilikni sarflaydi. Kamera: qadam bilan tebranadi,
// yonga yurganda biroz qiyshayadi, yugurganda FOV kengayadi, qattiq qo'nganda pastga "cho'kadi" va silkinadi.

static constexpr float Gravity = 9.81f;

void FirstPersonController::OnCreate()
{
    m_Head = Resolve(Head);
    m_Position = GetWorldPosition();
    m_Yaw = Transform().Rotation.y;
    m_Stamina = StaminaSeconds;
    m_Eye = EyeHeight;
    m_Fov = FieldOfView;
    ParseRoute();
}

void FirstPersonController::ParseRoute()
{
    m_Route.clear();
    std::stringstream points(BotRoute);
    std::string item;
    while (std::getline(points, item, ';'))
    {
        glm::vec3 p(0.0f);
        if (std::sscanf(item.c_str(), "%f,%f,%f", &p.x, &p.y, &p.z) == 3)
            m_Route.push_back(p);
    }
}

void FirstPersonController::ResetTo(const glm::vec3& position, float yaw, bool bot)
{
    m_Bot = bot;
    m_Position = position;
    m_Yaw = yaw;
    m_Pitch = 0.0f;
    m_Velocity = glm::vec2(0.0f);
    m_Stamina = StaminaSeconds;
    m_Exhausted = m_Sprinting = m_Crouching = m_PadLaunch = false;
    m_Distance = 0.0f;
    m_RouteIndex = 0;
    m_StuckTimer = 0.0f;
    SetPosition3D(position);
    if (auto* cc = TryGetComponent<CharacterController3DComponent>())
    {
        cc->VerticalVelocity = 0.0f;
        cc->Move(glm::vec3(0.0f));
    }
    Transform().Rotation = { 0.0f, m_Yaw, 0.0f };
}

const char* FirstPersonController::GetStateKey() const
{
    if (!m_WasGrounded)
        return "hud.air";
    if (m_Crouching)
        return "hud.crouch";
    if (glm::length(m_Velocity) < 0.3f)
        return "hud.idle";
    return m_Sprinting ? "hud.sprint" : "hud.walk";
}

FirstPersonController::Controls FirstPersonController::ReadInput(float dt)
{
    Controls c;
    if (!m_ControlsEnabled)
        return c;
    c.Move = Input::GetVector("Move");
    // the mouse (while the cursor is locked: raw movement) and the right stick
    const float sensitivity = MouseSensitivity * m_SensitivityScale;
    const glm::vec2 mouse = Cursor::IsLocked() ? Input::GetMouseDelta() : glm::vec2(0.0f);
    const glm::vec2 stick = Input::GetVector("Look") * GamepadLookSpeed * m_SensitivityScale * dt;
    c.Look.x = -mouse.x * sensitivity - stick.x;
    c.Look.y = (-mouse.y * sensitivity + stick.y) * (InvertY ? -1.0f : 1.0f);
    c.Jump = Input::IsActionJustPressed("Jump");
    c.Sprint = Input::IsActionPressed("Sprint");
    c.Crouch = Input::IsActionPressed("Crouch");
    return c;
}

void FirstPersonController::OnUpdate(Timestep ts)
{
    const float dt = ts;
    auto* cc = TryGetComponent<CharacterController3DComponent>();
    if (!cc || dt <= 0.0f)
        return;
    const glm::vec3 previous = m_Position;
    m_Position = GetWorldPosition();
    m_Distance += glm::length(glm::vec2(m_Position.x - previous.x, m_Position.z - previous.z));

    Controls c = m_Bot ? ThinkBot(dt) : ReadInput(dt);

    // ---- look: the body turns (yaw), the head nods (pitch)
    m_Yaw += c.Look.x;
    m_Pitch = std::clamp(m_Pitch + c.Look.y, glm::radians(-85.0f), glm::radians(85.0f));
    Transform().Rotation = { 0.0f, m_Yaw, 0.0f };

    // ---- stance and stamina
    const bool grounded = cc->Grounded;
    m_Crouching = c.Crouch && grounded;
    const bool moving = glm::length(c.Move) > 0.2f;
    if (m_Stamina <= 0.05f)
        m_Exhausted = true;
    if (m_Exhausted && m_Stamina > StaminaSeconds * 0.3f)
        m_Exhausted = false;
    m_Sprinting = c.Sprint && moving && c.Move.y > 0.1f && !m_Crouching && !m_Exhausted;
    if (m_Sprinting && grounded)
        m_Stamina = std::max(0.0f, m_Stamina - dt);
    else if (!m_Sprinting)
        m_Stamina = std::min(StaminaSeconds, m_Stamina + dt * 0.6f);

    // ---- horizontal movement relative to where the body faces
    const glm::vec3 forward3 = Transform().GetForward();
    const glm::vec2 forward = glm::normalize(glm::vec2(forward3.x, forward3.z));
    const glm::vec2 right(-forward.y, forward.x);
    glm::vec2 move = c.Move;
    if (glm::length(move) > 1.0f)
        move = glm::normalize(move);
    const float speed = m_Crouching ? CrouchSpeed : m_Sprinting ? SprintSpeed : WalkSpeed;
    const glm::vec2 target = (forward * move.y + right * move.x) * speed;
    m_Velocity = Math::Damp(m_Velocity, target, grounded ? Acceleration : AirAcceleration, dt);
    cc->Move({ m_Velocity.x, 0.0f, m_Velocity.y });

    // ---- jumping: coyote time and a buffered press
    m_CoyoteTimer = grounded ? 0.12f : std::max(0.0f, m_CoyoteTimer - dt);
    m_JumpBuffer = c.Jump ? 0.12f : std::max(0.0f, m_JumpBuffer - dt);
    const float jumpSpeed = std::sqrt(2.0f * Gravity * cc->GravityScale * JumpHeight);
    if (m_JumpBuffer > 0.0f && m_CoyoteTimer > 0.0f && !m_Crouching)
    {
        cc->VerticalVelocity = jumpSpeed;
        cc->Grounded = false;
        m_CoyoteTimer = m_JumpBuffer = 0.0f;
        PlaySound("Audio/jump.wav", 0.5f, 1.0f);
    }
    // jump pads: standing on one throws the player up
    if (grounded)
        for (Entity pad : FindEntitiesWithTag("JumpPad"))
        {
            const glm::vec3 p = GetScene().GetWorldPosition(pad);
            const float radius = pad.GetComponent<TransformComponent>().Scale.x * 0.5f;
            if (glm::distance(glm::vec2(p.x, p.z), glm::vec2(m_Position.x, m_Position.z)) < radius && std::abs(m_Position.y - p.y) < 0.6f)
            {
                cc->VerticalVelocity = JumpPadSpeed;
                cc->Grounded = false;
                m_PadLaunch = true;
                PlaySound("Audio/jumppad.wav", 0.7f, 1.0f);
                ShakeCamera(0.25f, 0.3f);
            }
        }

    // ---- landing
    if (!grounded)
        m_FallSpeed = std::min(m_FallSpeed, cc->VerticalVelocity);
    if (grounded && !m_WasGrounded)
    {
        const float impact = -m_FallSpeed;
        if (impact > 3.0f)
        {
            m_LandDipVelocity -= std::min(impact, 16.0f) * 0.06f;
            PlaySound("Audio/land.wav", std::clamp(impact / 14.0f, 0.25f, 0.9f), 1.0f);
            if (impact > 9.0f)
                ShakeCamera(std::min(0.6f, impact * 0.03f), 0.3f);
        }
        m_FallSpeed = 0.0f;
        m_PadLaunch = false;
    }
    m_WasGrounded = grounded;

    UpdateCamera(dt, grounded, glm::length(m_Velocity) / std::max(SprintSpeed, 0.1f));
}

void FirstPersonController::UpdateCamera(float dt, bool grounded, float speed01)
{
    if (!m_Head)
        return;
    const float horizontal = glm::length(m_Velocity);
    // head bob and steps: the phase follows the distance walked
    if (grounded && horizontal > 0.5f)
        m_BobPhase += horizontal * dt * (m_Sprinting ? 1.25f : 1.55f);
    const int step = (int)std::floor(m_BobPhase / glm::pi<float>());
    if (step != m_LastStep && grounded && horizontal > 0.8f)
    {
        static const char* steps[] = { "Audio/step1.wav", "Audio/step2.wav", "Audio/step3.wav", "Audio/step4.wav" };
        PlaySound(steps[(size_t)std::abs(step) % 4], m_Crouching ? 0.25f : m_Sprinting ? 0.7f : 0.5f, m_Sprinting ? 1.08f : 1.0f);
    }
    m_LastStep = step;
    const float bobAmount = grounded ? HeadBob * std::clamp(horizontal / WalkSpeed, 0.0f, 1.6f) : 0.0f;
    const float bobY = std::abs(std::sin(m_BobPhase)) * bobAmount;
    const float bobX = std::cos(m_BobPhase) * bobAmount * 0.6f;

    // crouch eye height, landing dip (a spring)
    m_Eye = Math::Damp(m_Eye, m_Crouching ? CrouchEyeHeight : EyeHeight, 12.0f, dt);
    m_LandDipVelocity += (-m_LandDip * 90.0f - m_LandDipVelocity * 12.0f) * dt;
    m_LandDip += m_LandDipVelocity * dt;

    // strafing leans the view a little
    const glm::vec3 forward3 = Transform().GetForward();
    const glm::vec2 right(-forward3.z, forward3.x);
    const float lateral = glm::dot(m_Velocity, glm::normalize(right));
    m_Roll = Math::Damp(m_Roll, -lateral * 0.0045f, 8.0f, dt);

    auto& t = m_Head.GetComponent<TransformComponent>();
    t.Translation = { bobX, m_Eye + bobY - bobAmount * 0.5f + m_LandDip, 0.0f };
    t.Rotation = { m_Pitch, 0.0f, m_Roll };

    // field of view: wider when sprinting and flying off a jump pad
    const float targetFov = FieldOfView + (m_Sprinting ? SprintFovBoost * std::clamp(speed01 * 1.3f, 0.0f, 1.0f) : 0.0f) + (m_PadLaunch ? 6.0f : 0.0f);
    m_Fov = Math::Damp(m_Fov, targetFov, 8.0f, dt);
    if (auto* cam = m_Head.TryGetComponent<CameraComponent>())
        if (std::abs(cam->Camera.GetPerspectiveVerticalFOV() - glm::radians(m_Fov)) > 1e-4f)
            cam->Camera.SetPerspectiveVerticalFOV(glm::radians(m_Fov));
}

// ------------------------------------------------------------------------------------------- bot

FirstPersonController::Controls FirstPersonController::ThinkBot(float dt)
{
    Controls c;
    if (m_Route.empty())
        return c;
    m_BotTime += dt;
    const glm::vec3 target = m_Route[m_RouteIndex];
    const glm::vec2 to(target.x - m_Position.x, target.z - m_Position.z);
    const float distance = glm::length(to);
    if (distance < 1.1f && std::abs(target.y - m_Position.y) < 1.8f)
    {
        m_RouteIndex = (m_RouteIndex + 1) % m_Route.size();
        m_StuckTimer = 0.0f;
    }
    // flying off a jump pad: straight up until the top
    auto* cc = TryGetComponent<CharacterController3DComponent>();
    const bool rising = m_PadLaunch && cc && cc->VerticalVelocity > 0.0f;

    // turn towards the waypoint (the move is mostly "forward"), glancing around a little
    const glm::vec3 next = m_Route[m_RouteIndex];
    const glm::vec2 dir(next.x - m_Position.x, next.z - m_Position.z);
    if (glm::length(dir) > 0.05f)
    {
        // the body's forward is -Z turned by yaw: yaw = atan2(-x, -z)
        const float goalYaw = std::atan2(-dir.x, -dir.y) + std::sin(m_BotTime * 0.7f) * 0.18f;
        c.Look.x = Math::DeltaAngle(m_Yaw, goalYaw) * std::min(1.0f, 5.0f * dt);
    }
    const float goalPitch = rising ? glm::radians(-25.0f) : std::sin(m_BotTime * 0.45f) * glm::radians(6.0f) - glm::radians(3.0f);
    c.Look.y = (goalPitch - m_Pitch) * std::min(1.0f, 3.0f * dt);

    // walk forward (sideways a bit while it turns); sprint on the straights
    const glm::vec3 fwd3 = Transform().GetForward();
    const glm::vec2 fwd = glm::normalize(glm::vec2(fwd3.x, fwd3.z));
    const glm::vec2 right(-fwd.y, fwd.x);
    const glm::vec2 want = glm::length(dir) > 0.01f ? glm::normalize(dir) : glm::vec2(0.0f);
    c.Move = rising ? glm::vec2(0.0f) : glm::vec2(glm::dot(want, right), glm::dot(want, fwd));
    c.Sprint = c.Move.y > 0.85f && distance > 4.0f && GetStamina01() > 0.35f;

    // stuck against something: hop
    if (!rising && glm::length(m_Velocity) < 0.6f && glm::length(c.Move) > 0.5f)
        m_StuckTimer += dt;
    else
        m_StuckTimer = 0.0f;
    if (m_StuckTimer > 0.5f)
    {
        c.Jump = true;
        c.Move.x += std::sin(m_BotTime * 3.0f) > 0.0f ? 0.6f : -0.6f;
        m_StuckTimer = 0.0f;
    }
    return c;
}

ZM_REGISTER_SCRIPT(FirstPersonController,
    ZM_FIELD(FirstPersonController, Head).Tooltip("Kamera (bosh) - ko'z balandligidagi bola obyekt"),
    ZM_FIELD_RANGE(FirstPersonController, WalkSpeed, 1.0f, 15.0f).Header("Harakat"),
    ZM_FIELD_RANGE(FirstPersonController, SprintSpeed, 1.0f, 20.0f),
    ZM_FIELD_RANGE(FirstPersonController, CrouchSpeed, 0.5f, 10.0f),
    ZM_FIELD_RANGE(FirstPersonController, Acceleration, 1.0f, 40.0f),
    ZM_FIELD_RANGE(FirstPersonController, AirAcceleration, 0.0f, 20.0f).Tooltip("Havoda yo'nalishni qanchalik o'zgartira oladi"),
    ZM_FIELD_RANGE(FirstPersonController, JumpHeight, 0.2f, 4.0f).Tooltip("Sakrash balandligi (metr)"),
    ZM_FIELD_RANGE(FirstPersonController, StaminaSeconds, 0.5f, 20.0f).Tooltip("To'liq chidamlilik bilan necha sekund yugurish mumkin"),
    ZM_FIELD_RANGE(FirstPersonController, JumpPadSpeed, 1.0f, 40.0f),
    ZM_FIELD_RANGE(FirstPersonController, MouseSensitivity, 0.0002f, 0.01f).Header("Qarash"),
    ZM_FIELD_RANGE(FirstPersonController, GamepadLookSpeed, 0.5f, 8.0f),
    ZM_FIELD(FirstPersonController, InvertY),
    ZM_FIELD_RANGE(FirstPersonController, FieldOfView, 40.0f, 110.0f).Header("Kamera"),
    ZM_FIELD_RANGE(FirstPersonController, SprintFovBoost, 0.0f, 25.0f),
    ZM_FIELD_RANGE(FirstPersonController, EyeHeight, 0.5f, 2.2f),
    ZM_FIELD_RANGE(FirstPersonController, CrouchEyeHeight, 0.3f, 2.0f),
    ZM_FIELD_RANGE(FirstPersonController, HeadBob, 0.0f, 0.2f),
    ZM_FIELD(FirstPersonController, BotRoute).Header("Bot").Tooltip("Botning yo'l nuqtalari: \"x,y,z;x,y,z;...\""))
