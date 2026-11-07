#include "Adventure.h"

#include <cmath>

using namespace Zamin;

// Uchinchi shaxs qahramoni: WASD / strelkalar / chap stick - yurish, Shift - yugurish, Space - sakrash.
class ThirdPersonController : public ScriptableEntity
{
public:
    float WalkSpeed = 3.0f;
    float RunSpeed = 6.5f;
    float JumpSpeed = 6.5f;
    float TurnSpeed = 12.0f;
    float Acceleration = 14.0f;
    std::string Camera = "Main Camera";

    void OnCreate() override
    {
        m_Spawn = Transform().Translation;
        ZM_INFO("Qahramon tayyor: WASD - yurish, Shift - yugurish, Space - sakrash, RMB - kamera");
    }

    void OnUpdate(Timestep ts) override
    {
        auto* cc = TryGetComponent<CharacterController3DComponent>();
        if (!cc) return;

        // ---- input (named actions / axes: Loyiha sozlamalari > Input)
        glm::vec2 input = Input::GetVector("Move"); // Horizontal + Vertical as one 2D value
        bool run = Input::IsActionPressed("Sprint");

        // ---- camera-relative direction
        glm::vec3 forward(0, 0, -1), right(1, 0, 0);
        if (Entity cam = FindEntity(Camera))
        {
            glm::vec3 f = cam.GetComponent<TransformComponent>().GetForward();
            f.y = 0.0f;
            if (glm::length(f) > 0.01f) forward = glm::normalize(f);
            right = glm::normalize(glm::cross(forward, glm::vec3(0, 1, 0)));
        }
        glm::vec3 wish = forward * input.y + right * input.x;
        float targetSpeed = glm::length(input) * (run ? RunSpeed : WalkSpeed);
        glm::vec3 targetVelocity = glm::length(wish) > 0.001f ? glm::normalize(wish) * targetSpeed : glm::vec3(0.0f);
        m_Velocity = Math::Damp(m_Velocity, targetVelocity, Acceleration, ts); // frame-rate independent
        cc->Move(m_Velocity);

        // ---- face the movement direction (model faces +Z)
        float speed = glm::length(m_Velocity);
        if (speed > 0.2f)
        {
            float targetYaw = std::atan2(m_Velocity.x, m_Velocity.z);
            float& yaw = Transform().Rotation.y;
            float diff = std::remainder(targetYaw - yaw, glm::two_pi<float>());
            yaw += diff * std::min(1.0f, TurnSpeed * (float)ts);
        }

        // ---- jump
        if (Input::IsActionJustPressed("Jump") && cc->Jump(JumpSpeed))
            m_AirTime = 0.0f;

        // ---- animation state
        if (!cc->Grounded) m_AirTime += ts; else m_AirTime = 0.0f;
        // The Animator state machine (defined in the scene) picks the clip from these parameters:
        // on the ground a 1D blend tree mixes Idle / Walk / Run by Speed (thresholds 0, 3, 6.5 - the
        // walk and run speeds, so the feet match the ground), in the air the Jump clip.
        if (auto* anim = TryGetComponent<AnimatorComponent>())
        {
            anim->SetFloat("Speed", speed);
            anim->SetBool("Grounded", m_AirTime <= 0.12f);
        }

        // ---- fell off the world
        if (Transform().Translation.y < -25.0f)
        {
            SetPosition3D(m_Spawn);
            m_Velocity = glm::vec3(0.0f);
            cc->VerticalVelocity = 0.0f;
        }
    }

private:
    glm::vec3 m_Spawn{ 0.0f };
    glm::vec3 m_Velocity{ 0.0f };
    float m_AirTime = 0.0f;
};
ZM_REGISTER_SCRIPT(ThirdPersonController, ZM_FIELD(ThirdPersonController, WalkSpeed), ZM_FIELD(ThirdPersonController, RunSpeed),
                   ZM_FIELD(ThirdPersonController, JumpSpeed), ZM_FIELD(ThirdPersonController, TurnSpeed), ZM_FIELD(ThirdPersonController, Camera))
