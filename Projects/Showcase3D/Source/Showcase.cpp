#include <Zamin/Zamin.h>

#include <cmath>

using namespace Zamin;

// Obyektni o'z o'qi atrofida aylantiradi.
class Rotator : public ScriptableEntity
{
public:
    glm::vec3 Speed = { 0.0f, 1.0f, 0.0f }; // radian / sekund
    void OnUpdate(Timestep ts) override { Transform().Rotation += Speed * (float)ts; }
};
ZM_REGISTER_SCRIPT(Rotator, ZM_FIELD(Rotator, Speed))

// Yuqoriga-pastga suzish.
class Bobbing : public ScriptableEntity
{
public:
    float Height = 0.3f;
    float Speed = 1.5f;
    void OnCreate() override { m_BaseY = Transform().Translation.y; }
    void OnUpdate(Timestep) override
    {
        Transform().Translation.y = m_BaseY + std::sin(GetTime() * Speed) * Height;
        Transform().Rotation.y = GetTime() * 0.3f;
    }
private:
    float m_BaseY = 0.0f;
};
ZM_REGISTER_SCRIPT(Bobbing, ZM_FIELD(Bobbing, Height), ZM_FIELD(Bobbing, Speed))

// Markaz atrofida aylanib uchadi (yorug'lik sharlari uchun).
class Orbit : public ScriptableEntity
{
public:
    float Radius = 3.0f, Speed = 1.0f, Phase = 0.0f, Height = 2.0f;
    void OnUpdate(Timestep) override
    {
        float a = GetTime() * Speed + Phase;
        Transform().Translation = { std::cos(a) * Radius, Height + std::sin(a * 2.0f) * 0.4f, std::sin(a) * Radius };
    }
};
ZM_REGISTER_SCRIPT(Orbit, ZM_FIELD(Orbit, Radius), ZM_FIELD(Orbit, Speed), ZM_FIELD(Orbit, Phase), ZM_FIELD(Orbit, Height))

// Kamera sahna atrofida sekin aylanadi; sichqoncha o'ng tugmasi bosilsa to'xtaydi.
class OrbitCamera : public ScriptableEntity
{
public:
    float Distance = 14.0f, Height = 5.0f, Speed = 0.15f;
    void OnUpdate(Timestep ts) override
    {
        if (!Input::IsMouseButtonPressed(MouseButton::Right))
            m_Angle += Speed * (float)ts;
        auto& t = Transform();
        t.Translation = { std::sin(m_Angle) * Distance, Height, std::cos(m_Angle) * Distance };
        glm::vec3 dir = glm::normalize(glm::vec3(0.0f, 1.0f, 0.0f) - t.Translation);
        t.Rotation = { std::asin(dir.y), std::atan2(-dir.x, -dir.z), 0.0f };
    }
private:
    float m_Angle = 0.0f;
};
ZM_REGISTER_SCRIPT(OrbitCamera, ZM_FIELD(OrbitCamera, Distance), ZM_FIELD(OrbitCamera, Height), ZM_FIELD(OrbitCamera, Speed))

// WASD bilan yuradigan kub; Space bilan sakraydi.
class PlayerMover3D : public ScriptableEntity
{
public:
    float Speed = 6.0f;
    float JumpVelocity = 7.0f;
    float Gravity = 20.0f;

    void OnUpdate(Timestep ts) override
    {
        auto& t = Transform();
        glm::vec3 dir(0.0f);
        if (Input::IsKeyPressed(Key::W) || Input::IsKeyPressed(Key::Up))    dir.z -= 1.0f;
        if (Input::IsKeyPressed(Key::S) || Input::IsKeyPressed(Key::Down))  dir.z += 1.0f;
        if (Input::IsKeyPressed(Key::A) || Input::IsKeyPressed(Key::Left))  dir.x -= 1.0f;
        if (Input::IsKeyPressed(Key::D) || Input::IsKeyPressed(Key::Right)) dir.x += 1.0f;
        if (glm::length(dir) > 0.0f)
        {
            dir = glm::normalize(dir);
            t.Translation += dir * Speed * (float)ts;
            t.Rotation.y = std::atan2(-dir.x, -dir.z);
        }

        bool grounded = t.Translation.y <= 0.5f;
        if (grounded && Input::IsKeyPressed(Key::Space))
            m_VelocityY = JumpVelocity;
        m_VelocityY -= Gravity * (float)ts;
        t.Translation.y = std::max(0.5f, t.Translation.y + m_VelocityY * (float)ts);
        if (t.Translation.y <= 0.5f && m_VelocityY < 0.0f)
            m_VelocityY = 0.0f;
    }
private:
    float m_VelocityY = 0.0f;
};
ZM_REGISTER_SCRIPT(PlayerMover3D, ZM_FIELD(PlayerMover3D, Speed), ZM_FIELD(PlayerMover3D, JumpVelocity), ZM_FIELD(PlayerMover3D, Gravity))

// Space yoki sichqonchaning chap tugmasi: kamera yo'nalishida shar otadi (prefab + 3D fizika).
class BallShooter : public ScriptableEntity
{
public:
    std::string Prefab = "Prefabs/Ball.zprefab";
    float Speed = 18.0f;
    float Cooldown = 0.25f;

    void OnUpdate(Timestep ts) override
    {
        m_Timer -= ts;
        bool fire = Input::IsKeyPressed(Key::Space) || Input::IsMouseButtonPressed(MouseButton::Left) ||
                    Input::IsGamepadButtonPressed(GamepadButton::A);
        if (fire && m_Timer <= 0.0f)
        {
            m_Timer = Cooldown;
            glm::vec3 forward = Transform().GetForward();
            Entity ball = Instantiate(Prefab, GetWorldPosition() + forward * 1.5f);
            SetVelocity3D(ball, forward * Speed);
        }
    }
private:
    float m_Timer = 0.0f;
};
ZM_REGISTER_SCRIPT(BallShooter, ZM_FIELD(BallShooter, Prefab), ZM_FIELD(BallShooter, Speed), ZM_FIELD(BallShooter, Cooldown))

// Obyektni N soniyadan keyin o'chiradi (otilgan sharlar uchun).
class Lifetime : public ScriptableEntity
{
public:
    float Seconds = 8.0f;
    void OnUpdate(Timestep ts) override
    {
        Seconds -= ts;
        if (Seconds <= 0.0f || Transform().Translation.y < -20.0f)
            Destroy();
    }
};
ZM_REGISTER_SCRIPT(Lifetime, ZM_FIELD(Lifetime, Seconds))

// Kub minorasini yaratadi (3D fizika demo).
class TowerBuilder : public ScriptableEntity
{
public:
    int Levels = 5;
    float Size = 0.9f;

    void OnCreate() override
    {
        Entity brick = FindEntity("Brick");
        if (!brick) return;
        glm::vec3 base = GetWorldPosition();
        for (int level = 0; level < Levels; level++)
            for (int i = 0; i < Levels - level; i++)
            {
                glm::vec3 p = base + glm::vec3((i - (Levels - level - 1) * 0.5f) * Size * 1.02f, 0.5f * Size + level * Size, 0.0f);
                Instantiate(brick, p);
            }
        Destroy(brick); // template is not needed anymore
    }
};
ZM_REGISTER_SCRIPT(TowerBuilder, ZM_FIELD(TowerBuilder, Levels), ZM_FIELD(TowerBuilder, Size))
