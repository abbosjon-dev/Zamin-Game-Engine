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

    // Tanganing collider'i sensor: tegish OnTriggerBegin bo'lib keladi (OnCollisionBegin emas).
    void OnTriggerBegin(Entity other) override
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
ZM_REGISTER_SCRIPT(Coin,
    ZM_FIELD_RANGE(Coin, BobSpeed, 0.0f, 10.0f).Header("Tebranish"),
    ZM_FIELD_RANGE(Coin, BobHeight, 0.0f, 1.0f).Tooltip("Yuqoriga-pastga tebranish balandligi (birlik)"),
    ZM_FIELD_TOOLTIP(Coin, Effect, "Yig'ilganda paydo bo'ladigan prefab (Assets ichidagi yo'l)"))

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
    enum class Motion { Smooth, Linear };

    glm::vec2 Direction = { 1.0f, 0.0f };
    float Distance = 3.0f;
    float Speed = 1.0f;
    Motion Mode = Motion::Smooth;

    void OnUpdate(Timestep) override
    {
        // Same amplitude and period for both: a sine wave, or a triangle wave (constant speed).
        float phase = GetTime() * Speed;
        float v = Mode == Motion::Smooth ? Distance * Speed * std::cos(phase)
                                         : Distance * Speed * (2.0f / glm::pi<float>()) * (std::cos(phase) >= 0.0f ? 1.0f : -1.0f);
        SetVelocity(glm::normalize(Direction) * v);
    }
};
ZM_REGISTER_SCRIPT(MovingPlatform, ZM_FIELD(MovingPlatform, Direction), ZM_FIELD_RANGE(MovingPlatform, Distance, 0.0f, 20.0f),
                   ZM_FIELD_RANGE(MovingPlatform, Speed, 0.0f, 5.0f),
                   ZM_FIELD_ENUM(MovingPlatform, Mode, "Silliq (sinus)", "Bir tekis").Label("Harakat turi")
                       .Tooltip("Silliq: chetlarda sekinlashadi. Bir tekis: doimiy tezlik bilan boradi-qaytadi"))

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
    glm::vec4 ReachedColor = { 1.0f, 0.85f, 0.2f, 1.0f };

    void OnTriggerBegin(Entity other) override
    {
        if (other.HasTag("Player") && m_Timer < 0.0f)
        {
            m_Timer = Delay;
            ZM_INFO("Daraja tugadi! Keyingisi: {}", NextScene);
            if (auto* sprite = TryGetComponent<SpriteRendererComponent>())
                sprite->Color = ReachedColor;
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
ZM_REGISTER_SCRIPT(LevelGoal, ZM_FIELD_TOOLTIP(LevelGoal, NextScene, "Keyingi daraja sahnasi (Assets ichidagi yo'l)"),
                   ZM_FIELD_RANGE(LevelGoal, Delay, 0.0f, 5.0f).Tooltip("O'tishdan oldingi kutish (sekund)"),
                   ZM_FIELD_COLOR(LevelGoal, ReachedColor).Tooltip("O'yinchi yetib kelganda bayroq rangi"))

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
