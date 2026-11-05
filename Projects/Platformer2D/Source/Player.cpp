#include <Zamin/Zamin.h>

#include "GameManager.h"

using namespace Zamin;

// 2D platformer qahramoni: "Horizontal" o'qi (A/D, strelkalar, gamepad) bilan yurish,
// "Jump" harakati (Space, W, Up, gamepad A) bilan sakrash. Loyiha sozlamalari > Input.
class PlayerController : public ScriptableEntity
{
public:
    float MoveSpeed = 7.0f;
    float JumpImpulse = 9.0f;
    float FallLimit = -15.0f;
    float LandShake = 0.0f; // 0 = o'chiq; shundan tez qo'nsa kamera silkinadi (birlik/s)

    void OnCreate() override
    {
        m_Spawn = Transform().Translation;
        ZM_INFO("O'yinchi tayyor. A/D - yurish, Space - sakrash");
    }

    void OnUpdate(Timestep ts) override
    {
        float dir = Input::GetAxis("Horizontal");

        glm::vec2 velocity = GetVelocity();
        m_FallSpeed = std::max(0.0f, -velocity.y); // before this frame's physics step: the speed we land with
        // Smooth horizontal acceleration
        float target = dir * MoveSpeed;
        velocity.x += (target - velocity.x) * std::min(1.0f, 12.0f * (float)ts);
        SetVelocity(velocity);

        if (Input::IsActionJustPressed("Jump") && m_GroundContacts > 0)
        {
            SetVelocity({ velocity.x, 0.0f });
            float mass = 1.0f;
            ApplyImpulse({ 0.0f, JumpImpulse * mass });
        }

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
            SendMessage(FindEntity("GameManager"), "PlayerFell");
            if (LandShake > 0.0f)
                ShakeCamera(0.6f, 0.5f, 18.0f);
        }
    }

    // Faqat qattiq to'qnashuvlar (yer, platformalar). Tangalar va bayroq sensor - ular
    // OnTriggerBegin/End bo'lib keladi, shuning uchun bu yerda alohida tekshirish shart emas.
    void OnCollisionBegin(Entity) override
    {
        m_GroundContacts++;
        // Hard landing: the camera shakes (the faster, the stronger).
        if (LandShake > 0.0f && m_FallSpeed > LandShake)
            ShakeCamera(std::clamp(0.25f + (m_FallSpeed - LandShake) * 0.06f, 0.0f, 0.8f), 0.35f, 22.0f);
        m_FallSpeed = 0.0f;
    }
    void OnCollisionEnd(Entity) override { m_GroundContacts = std::max(0, m_GroundContacts - 1); }

private:
    glm::vec3 m_Spawn{ 0.0f };
    int m_GroundContacts = 0;
    float m_FallSpeed = 0.0f;
};

ZM_REGISTER_SCRIPT(PlayerController,
    ZM_FIELD_RANGE(PlayerController, MoveSpeed, 0.0f, 20.0f).Header("Harakat").Tooltip("Yurish tezligi (birlik/sekund)"),
    ZM_FIELD_RANGE(PlayerController, JumpImpulse, 0.0f, 30.0f).Tooltip("Sakrash kuchi (impuls)"),
    ZM_FIELD(PlayerController, FallLimit).Tooltip("Shu balandlikdan pastga tushsa, o'yinchi boshlang'ich joyga qaytadi"),
    ZM_FIELD_RANGE(PlayerController, LandShake, 0.0f, 40.0f).Header("Kamera")
        .Tooltip("Shundan tezroq tushib qo'nsa (birlik/s) kamera silkinadi; yiqilganda ham. 0 - o'chiq"))
