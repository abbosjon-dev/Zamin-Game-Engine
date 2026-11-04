#include <Zamin/Zamin.h>

#include "GameManager.h"

#include <cmath>

using namespace Zamin;

// Tanga: o'yinchi tegsa yig'iladi. Joyida tebranib turadi.
class Coin : public ScriptableEntity
{
public:
    float BobSpeed = 3.0f;
    float BobHeight = 0.15f;
    std::string Effect = "Prefabs/CoinSparkle.zprefab";

    void OnCreate() override { m_BaseY = Transform().Translation.y; m_Phase = Random::Angle(); }

    void OnUpdate(Timestep) override
    {
        auto& t = Transform();
        t.Translation.y = m_BaseY + std::sin(GetTime() * BobSpeed + m_Phase) * BobHeight;
    }

    void OnCollisionBegin(Entity other) override
    {
        if (m_Collected || !other.HasTag("Player"))
            return;
        m_Collected = true;
        Emit("CoinCollected"); // GameManager obuna bo'lgan
        Instantiate(Effect, GetWorldPosition());
        Destroy();
    }

private:
    float m_BaseY = 0.0f, m_Phase = 0.0f;
    bool m_Collected = false;
};
ZM_REGISTER_SCRIPT(Coin, ZM_FIELD(Coin, BobSpeed), ZM_FIELD(Coin, BobHeight), ZM_FIELD(Coin, Effect))

// Kamera o'yinchini silliq kuzatadi.
class CameraFollow : public ScriptableEntity
{
public:
    std::string Target = "Player";
    float Smoothness = 5.0f;
    glm::vec2 Offset = { 0.0f, 1.5f };
    float MinY = -2.0f;

    void OnLateUpdate(Timestep ts) override
    {
        Entity target = FindEntity(Target);
        if (!target) return;
        auto& t = Transform();
        glm::vec3 goal = target.GetComponent<TransformComponent>().Translation;
        glm::vec2 desired = { goal.x + Offset.x, std::max(MinY, goal.y + Offset.y) };
        float k = 1.0f - std::exp(-Smoothness * (float)ts);
        t.Translation.x += (desired.x - t.Translation.x) * k;
        t.Translation.y += (desired.y - t.Translation.y) * k;
    }
};
ZM_REGISTER_SCRIPT(CameraFollow, ZM_FIELD(CameraFollow, Target), ZM_FIELD(CameraFollow, Smoothness),
                   ZM_FIELD(CameraFollow, Offset), ZM_FIELD(CameraFollow, MinY))

// Kinematik platforma: oldinga-orqaga harakatlanadi (ustidagi o'yinchini ham olib yuradi).
class MovingPlatform : public ScriptableEntity
{
public:
    glm::vec2 Direction = { 1.0f, 0.0f };
    float Distance = 3.0f;
    float Speed = 1.0f;

    void OnUpdate(Timestep) override
    {
        float v = Distance * Speed * std::cos(GetTime() * Speed);
        SetVelocity(glm::normalize(Direction) * v);
    }
};
ZM_REGISTER_SCRIPT(MovingPlatform, ZM_FIELD(MovingPlatform, Direction), ZM_FIELD(MovingPlatform, Distance),
                   ZM_FIELD(MovingPlatform, Speed))

// Bulutlar: sekin suzib yuradi va ekran chetidan qaytadi.
class Cloud : public ScriptableEntity
{
public:
    float Speed = 0.4f;
    float WrapLeft = -20.0f, WrapRight = 60.0f;

    void OnUpdate(Timestep ts) override
    {
        auto& t = Transform();
        t.Translation.x += Speed * (float)ts;
        if (t.Translation.x > WrapRight) t.Translation.x = WrapLeft;
    }
};
ZM_REGISTER_SCRIPT(Cloud, ZM_FIELD(Cloud, Speed))

// Daraja oxiri: o'yinchi tegsa keyingi sahnaga o'tadi.
class LevelGoal : public ScriptableEntity
{
public:
    std::string NextScene = "Scenes/Level2.zscene";
    float Delay = 0.6f;

    void OnCollisionBegin(Entity other) override
    {
        if (other.GetName() == "Player" && m_Timer < 0.0f)
        {
            m_Timer = Delay;
            ZM_INFO("Daraja tugadi! Keyingisi: {}", NextScene);
            if (auto* sprite = TryGetComponent<SpriteRendererComponent>())
                sprite->Color = { 1.0f, 0.85f, 0.2f, 1.0f };
        }
    }

    void OnUpdate(Timestep ts) override
    {
        if (m_Timer < 0.0f) return;
        m_Timer -= ts;
        if (m_Timer <= 0.0f)
            LoadScene(NextScene);
    }

private:
    float m_Timer = -1.0f;
};
ZM_REGISTER_SCRIPT(LevelGoal, ZM_FIELD(LevelGoal, NextScene), ZM_FIELD(LevelGoal, Delay))

// N soniyadan keyin obyektni o'chiradi (effektlar uchun).
class AutoDestroy : public ScriptableEntity
{
public:
    float Seconds = 1.5f;
    void OnUpdate(Timestep ts) override
    {
        Seconds -= ts;
        if (Seconds <= 0.0f) Destroy();
    }
};
ZM_REGISTER_SCRIPT(AutoDestroy, ZM_FIELD(AutoDestroy, Seconds))

// Ekrandagi tugma: bosilganda sahnani qayta yuklaydi.
class RestartButton : public ScriptableEntity
{
public:
    std::string Scene = "Scenes/Level1.zscene";
    void OnClick() override
    {
        ZM_INFO("Qayta boshlanmoqda...");
        LoadScene(Scene);
    }
};
ZM_REGISTER_SCRIPT(RestartButton, ZM_FIELD(RestartButton, Scene))
