#include <Zamin/Zamin.h>

#include <cmath>

using namespace Zamin;

// The Joints2D scene: small scripts that work with joints.

// A car: the "Horizontal" axis (A / D, arrows, gamepad) turns the wheels' motors; when no key is pressed and
// AutoDrive is on, it drives slowly to the right. The wheels are entities joined to the body (this entity)
// with Wheel Joint 2D.
class CarController : public ScriptableEntity
{
public:
    EntityRef RearWheel;
    EntityRef FrontWheel;
    float MaxSpeed = 720.0f;   // wheel rotation, degrees / second
    float Torque = 60.0f;
    bool AutoDrive = true;

    void OnUpdate(Timestep) override
    {
        float input = Input::GetAxis("Horizontal");
        if (std::abs(input) < 0.1f)
            input = AutoDrive ? 0.35f : 0.0f;
        for (EntityRef ref : { RearWheel, FrontWheel })
        {
            Entity wheel = Resolve(ref);
            if (!wheel || !wheel.HasComponent<WheelJoint2DComponent>())
                continue;
            auto& joint = wheel.GetComponent<WheelJoint2DComponent>();
            // With nothing pressed the motor brakes at speed 0 (stops slowly).
            joint.EnableMotor = true;
            joint.MotorSpeed = -input * MaxSpeed; // clockwise - to the right
            joint.MaxMotorTorque = std::abs(input) > 0.0f ? Torque : Torque * 0.1f;
        }
    }
};

ZM_REGISTER_SCRIPT(CarController,
    ZM_FIELD(CarController, RearWheel).Header("Wheels").Tooltip("The rear wheel joined to the body with a Wheel Joint 2D"),
    ZM_FIELD(CarController, FrontWheel).Tooltip("Front wheel"),
    ZM_FIELD_RANGE(CarController, MaxSpeed, 0.0f, 3000.0f).Header("Motor").Tooltip("Wheel spin speed (degrees/s)"),
    ZM_FIELD_RANGE(CarController, Torque, 0.0f, 1000.0f).Tooltip("Motor torque (N*m)"),
    ZM_FIELD(CarController, AutoDrive).Tooltip("Rolls slowly to the right even when no key is pressed"))

// Reverses a motorized joint (Prismatic / Revolute / Wheel) every Period seconds:
// a lift goes up and down, a door opens and closes.
class JointMotorPingPong : public ScriptableEntity
{
public:
    float Period = 2.5f;

    void OnUpdate(Timestep ts) override
    {
        m_Time += ts;
        if (m_Time < Period)
            return;
        m_Time = 0.0f;
        if (auto* j = TryGetComponent<PrismaticJoint2DComponent>()) j->MotorSpeed = -j->MotorSpeed;
        if (auto* j = TryGetComponent<RevoluteJoint2DComponent>()) j->MotorSpeed = -j->MotorSpeed;
        if (auto* j = TryGetComponent<WheelJoint2DComponent>()) j->MotorSpeed = -j->MotorSpeed;
    }

private:
    float m_Time = 0.0f;
};

ZM_REGISTER_SCRIPT(JointMotorPingPong,
    ZM_FIELD_RANGE(JointMotorPingPong, Period, 0.1f, 30.0f).Tooltip("Seconds until the direction changes"))

// When the joint breaks (Break Force / Break Torque exceeded) the entity flashes red.
class BreakableJoint : public ScriptableEntity
{
public:
    void OnJointBreak(const JointBreak2D& joint) override
    {
        ZM_INFO("The {} joint broke: {:.0f} N", GetEntity().GetName(), joint.Force);
        Flash({ 1.0f, 0.3f, 0.2f, 1.0f }, 0.4f);
    }
};

ZM_REGISTER_SCRIPT(BreakableJoint)
