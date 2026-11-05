#include <Zamin/Zamin.h>

#include <cmath>

using namespace Zamin;

// Joints2D sahnasi: joint'lar bilan ishlaydigan kichik skriptlar.

// Mashina: "Horizontal" o'qi (A / D, strelkalar, gamepad) g'ildiraklarning motorlarini aylantiradi;
// tugma bosilmaganda AutoDrive bo'lsa sekin o'ngga yuradi. G'ildiraklar - Wheel Joint 2D bilan
// kuzovga (shu obyekt) ulangan obyektlar.
class CarController : public ScriptableEntity
{
public:
    EntityRef RearWheel;
    EntityRef FrontWheel;
    float MaxSpeed = 720.0f;   // g'ildirak aylanishi, gradus / soniya
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
            // Bosilmasa motor 0 tezlikda tormoz bo'ladi (sekin to'xtaydi).
            joint.EnableMotor = true;
            joint.MotorSpeed = -input * MaxSpeed; // soat mili bo'yicha - o'ngga
            joint.MaxMotorTorque = std::abs(input) > 0.0f ? Torque : Torque * 0.1f;
        }
    }
};

ZM_REGISTER_SCRIPT(CarController,
    ZM_FIELD(CarController, RearWheel).Header("G'ildiraklar").Tooltip("Wheel Joint 2D bilan kuzovga ulangan orqa g'ildirak"),
    ZM_FIELD(CarController, FrontWheel).Tooltip("Old g'ildirak"),
    ZM_FIELD_RANGE(CarController, MaxSpeed, 0.0f, 3000.0f).Header("Motor").Tooltip("G'ildirak aylanish tezligi (gradus/s)"),
    ZM_FIELD_RANGE(CarController, Torque, 0.0f, 1000.0f).Tooltip("Motor momenti (N*m)"),
    ZM_FIELD(CarController, AutoDrive).Tooltip("Tugma bosilmaganda ham sekin o'ngga yuradi"))

// Motorli joint'ni (Prismatic / Revolute / Wheel) har Period soniyada teskari yo'nalishga aylantiradi:
// lift yuqoriga-pastga, eshik ochiladi-yopiladi.
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
    ZM_FIELD_RANGE(JointMotorPingPong, Period, 0.1f, 30.0f).Tooltip("Necha soniyada yo'nalish almashadi"))

// Joint uzilganda (Break Force / Break Torque oshib ketdi) obyekt qizarib yonib-o'chadi.
class BreakableJoint : public ScriptableEntity
{
public:
    void OnJointBreak(const JointBreak2D& joint) override
    {
        ZM_INFO("{} joint'i uzildi: {:.0f} N", GetEntity().GetName(), joint.Force);
        Flash({ 1.0f, 0.3f, 0.2f, 1.0f }, 0.4f);
    }
};

ZM_REGISTER_SCRIPT(BreakableJoint)
