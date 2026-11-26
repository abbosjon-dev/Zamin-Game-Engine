#include <Zamin/Zamin.h>

#include <cmath>

using namespace Zamin;

// The top-down hero (the TopDown2D scene): walks in four directions with the "Horizontal" / "Vertical" axes
// (WASD, arrows, gamepad). The animation is the Sprite Animation's clips
// (Animations/HeroDown|HeroUp|HeroSide.zsanim: the "hero_*_N" frames of the topdown_objects.png atlas,
// a "Step" event on every step - the hero dips a little); without a clip, a sequence of frames.
// When walking left the picture is mirrored.
class TopDownController : public ScriptableEntity
{
public:
    float MoveSpeed = 4.0f;
    float Acceleration = 16.0f;
    float WalkFPS = 9.0f;

    void OnCreate() override
    {
        m_Spawn = glm::vec2(GetWorldPosition());
        ZM_INFO("Top-down hero ready. WASD / arrows - move");
    }

    // Chase2D: a guard caught the hero ("Caught") - back to where it started, with a red flash.
    void OnMessage(const ScriptMessage& message) override
    {
        if (message.Name != "Caught")
            return;
        SetPosition2D(m_Spawn);
        SetVelocity({ 0.0f, 0.0f });
        Flash({ 1.0f, 0.25f, 0.2f, 1.0f }, 0.4f);
    }

    void OnUpdate(Timestep ts) override
    {
        // The "Move" vector (Project settings > Input > Vectors): Horizontal + Vertical, diagonals not faster.
        glm::vec2 input = Input::GetVector("Move");

        // Smooth start and stop (the body has no gravity in this scene).
        glm::vec2 velocity = GetVelocity();
        velocity += (input * MoveSpeed - velocity) * std::min(1.0f, Acceleration * (float)ts);
        SetVelocity(velocity);

        if (glm::length(input) > 0.1f)
            m_Facing = std::abs(input.x) > std::abs(input.y) ? (input.x < 0.0f ? Facing::Left : Facing::Right)
                                                             : (input.y > 0.0f ? Facing::Up : Facing::Down);
        const char* clip = m_Facing == Facing::Up ? "HeroUp" : m_Facing == Facing::Down ? "HeroDown" : "HeroSide";
        const char* sequence = m_Facing == Facing::Up ? "hero_up" : m_Facing == Facing::Down ? "hero_down" : "hero_side";
        auto play = [&]() {
            // The direction's clip (.zsanim); without it the sheet's numbered frames.
            if (!PlaySpriteClip(clip))
                PlaySpriteSequence(sequence, WalkFPS, true);
        };
        if (glm::length(velocity) > 0.4f)
            play();
        else if (auto* anim = TryGetComponent<SpriteAnimationComponent>())
        {
            // Standing: the first frame of the direction it faces.
            play();
            anim->Playing = false;
            anim->Time = 0.0f;
        }
        if (auto* sprite = TryGetComponent<SpriteRendererComponent>())
            sprite->FlipX = m_Facing == Facing::Left;

        // A little dip on each step (the clips' "Step" events), easing back.
        m_Dip = std::max(0.0f, m_Dip - (float)ts * 6.0f);
        Transform().Scale = { 1.0f + 0.06f * m_Dip, 1.0f - 0.08f * m_Dip, 1.0f };
    }

    void OnAnimationEvent(const AnimationEventInfo& e) override
    {
        if (e.Name == "Step")
            m_Dip = 1.0f;
    }

private:
    enum class Facing { Down, Up, Left, Right };
    Facing m_Facing = Facing::Down;
    float m_Dip = 0.0f;
    glm::vec2 m_Spawn{ 0.0f };
};

ZM_REGISTER_SCRIPT(TopDownController,
    ZM_FIELD_RANGE(TopDownController, MoveSpeed, 0.0f, 20.0f).Header("Movement").Tooltip("Walk speed (units/second)"),
    ZM_FIELD_RANGE(TopDownController, Acceleration, 0.0f, 60.0f).Tooltip("How fast it reaches its speed and stops"),
    ZM_FIELD_RANGE(TopDownController, WalkFPS, 1.0f, 30.0f).Tooltip("Frame rate of the walk animation (when there are no clips; a clip uses its own FPS)"))
