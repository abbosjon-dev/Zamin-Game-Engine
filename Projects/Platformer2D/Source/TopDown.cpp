#include <Zamin/Zamin.h>

#include <cmath>

using namespace Zamin;

// Top-down qahramon (TopDown2D sahnasi): "Horizontal" / "Vertical" o'qlari (WASD, strelkalar,
// gamepad) bilan to'rt tomonga yuradi. Animatsiya - topdown_objects.png atlasidagi
// "hero_down_N", "hero_up_N", "hero_side_N" kadrlari; chapga yurganda rasm aks ettiriladi.
class TopDownController : public ScriptableEntity
{
public:
    float MoveSpeed = 4.0f;
    float Acceleration = 16.0f;
    float WalkFPS = 9.0f;

    void OnCreate() override
    {
        ZM_INFO("Top-down qahramon tayyor. WASD / strelkalar - yurish");
    }

    void OnUpdate(Timestep ts) override
    {
        glm::vec2 input = { Input::GetAxis("Horizontal"), Input::GetAxis("Vertical") };
        if (glm::length(input) > 1.0f)
            input = glm::normalize(input);

        // Smooth start and stop (the body has no gravity in this scene).
        glm::vec2 velocity = GetVelocity();
        velocity += (input * MoveSpeed - velocity) * std::min(1.0f, Acceleration * (float)ts);
        SetVelocity(velocity);

        if (glm::length(input) > 0.1f)
            m_Facing = std::abs(input.x) > std::abs(input.y) ? (input.x < 0.0f ? Facing::Left : Facing::Right)
                                                             : (input.y > 0.0f ? Facing::Up : Facing::Down);
        const char* sequence = m_Facing == Facing::Up ? "hero_up" : m_Facing == Facing::Down ? "hero_down" : "hero_side";
        if (glm::length(velocity) > 0.4f)
            PlaySpriteSequence(sequence, WalkFPS, true);
        else if (auto* anim = TryGetComponent<SpriteAnimationComponent>())
        {
            // Standing: the first frame of the direction it faces.
            PlaySpriteSequence(sequence, WalkFPS, true);
            anim->Playing = false;
            anim->Time = 0.0f;
        }
        if (auto* sprite = TryGetComponent<SpriteRendererComponent>())
            sprite->FlipX = m_Facing == Facing::Left;
    }

private:
    enum class Facing { Down, Up, Left, Right };
    Facing m_Facing = Facing::Down;
};

ZM_REGISTER_SCRIPT(TopDownController,
    ZM_FIELD_RANGE(TopDownController, MoveSpeed, 0.0f, 20.0f).Header("Harakat").Tooltip("Yurish tezligi (birlik/sekund)"),
    ZM_FIELD_RANGE(TopDownController, Acceleration, 0.0f, 60.0f).Tooltip("Tezlikka qanchalik tez yetadi va to'xtaydi"),
    ZM_FIELD_RANGE(TopDownController, WalkFPS, 1.0f, 30.0f).Tooltip("Yurish animatsiyasining kadr tezligi"))
