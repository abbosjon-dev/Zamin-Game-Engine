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

        // Animation: idle (0-1), run (4-7), jump (8) on the sprite sheet
        auto& t = Transform();
        if (auto* anim = TryGetComponent<SpriteAnimationComponent>())
        {
            if (m_GroundContacts == 0)              anim->Play(8, 1, 1.0f, true);
            else if (std::abs(velocity.x) > 0.5f)  anim->Play(4, 4, 12.0f, true);
            else                                    anim->Play(0, 2, 3.0f, true);
        }
        if (auto* sprite = TryGetComponent<SpriteRendererComponent>(); sprite && dir != 0.0f)
            sprite->FlipX = dir < 0.0f;

        // Squash & stretch for a bit of juice
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
    static bool IsCollectible(Entity e)
    {
        if (!e) return true;
        if (e.GetName() == "Coin" || e.GetName() == "Goal") return true;
        auto* box = e.TryGetComponent<BoxCollider2DComponent>();
        auto* circle = e.TryGetComponent<CircleCollider2DComponent>();
        return (box && box->IsSensor) || (circle && circle->IsSensor);
    }

    glm::vec3 m_Spawn{ 0.0f };
    int m_GroundContacts = 0;
    bool m_JumpHeld = false;
};

ZM_REGISTER_SCRIPT(PlayerController,
    ZM_FIELD(PlayerController, MoveSpeed),
    ZM_FIELD(PlayerController, JumpImpulse),
    ZM_FIELD(PlayerController, FallLimit))
