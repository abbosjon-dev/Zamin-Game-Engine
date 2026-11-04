#include <Zamin/Zamin.h>

#include "GameManager.h"

using namespace Zamin;

// 2D platformer qahramoni: A/D yoki strelkalar bilan yurish, Space bilan sakrash.
class PlayerController : public ScriptableEntity
{
public:
    float MoveSpeed = 7.0f;
    float JumpImpulse = 9.0f;
    float FallLimit = -15.0f;

    void OnCreate() override
    {
        m_Spawn = Transform().Translation;
        ZM_INFO("O'yinchi tayyor. A/D - yurish, Space - sakrash");
    }

    void OnUpdate(Timestep ts) override
    {
        float dir = 0.0f;
        if (Input::IsKeyPressed(Key::A) || Input::IsKeyPressed(Key::Left))  dir -= 1.0f;
        if (Input::IsKeyPressed(Key::D) || Input::IsKeyPressed(Key::Right)) dir += 1.0f;

        glm::vec2 velocity = GetVelocity();
        // Smooth horizontal acceleration
        float target = dir * MoveSpeed;
        velocity.x += (target - velocity.x) * std::min(1.0f, 12.0f * (float)ts);
        SetVelocity(velocity);

        bool jump = Input::IsKeyPressed(Key::Space) || Input::IsKeyPressed(Key::W) || Input::IsKeyPressed(Key::Up);
        if (jump && !m_JumpHeld && m_GroundContacts > 0)
        {
            SetVelocity({ velocity.x, 0.0f });
            float mass = 1.0f;
            ApplyImpulse({ 0.0f, JumpImpulse * mass });
        }
        m_JumpHeld = jump;

        // Squash & stretch for a bit of juice
        auto& t = Transform();
        float stretch = std::clamp(std::abs(GetVelocity().y) * 0.02f, 0.0f, 0.15f);
        t.Scale = { 1.0f - stretch, 1.0f + stretch, 1.0f };

        if (t.Translation.y < FallLimit)
        {
            SetPosition2D({ m_Spawn.x, m_Spawn.y });
            SetVelocity({ 0.0f, 0.0f });
            if (auto* gm = GetScript<GameManager>(FindEntity("GameManager")))
                gm->OnPlayerFell();
        }
    }

    void OnCollisionBegin(Entity other) override
    {
        if (!IsCollectible(other)) m_GroundContacts++;
    }

    void OnCollisionEnd(Entity other) override
    {
        if (!IsCollectible(other)) m_GroundContacts = std::max(0, m_GroundContacts - 1);
    }

private:
    static bool IsCollectible(Entity e) { return e && e.GetName() == "Coin"; }

    glm::vec3 m_Spawn{ 0.0f };
    int m_GroundContacts = 0;
    bool m_JumpHeld = false;
};

ZM_REGISTER_SCRIPT(PlayerController,
    ZM_FIELD(PlayerController, MoveSpeed),
    ZM_FIELD(PlayerController, JumpImpulse),
    ZM_FIELD(PlayerController, FallLimit))
