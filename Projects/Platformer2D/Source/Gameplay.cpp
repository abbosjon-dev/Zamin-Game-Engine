#include <Zamin/Zamin.h>

#include "GameManager.h"

#include <cmath>

using namespace Zamin;

// A coin: collected when the player touches it. It bobs in place.
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

    // The coin's collider is a sensor: a touch arrives as OnTriggerBegin (not OnCollisionBegin).
    void OnTriggerBegin(Entity other) override
    {
        if (m_Collected || !other.HasTag("Player"))
            return;
        m_Collected = true;
        Emit("CoinCollected"); // GameManager is subscribed
        Instantiate(Effect, GetWorldPosition());
        Destroy();
    }

private:
    float m_BaseY = 0.0f, m_Phase = 0.0f;
    bool m_Collected = false;
};
ZM_REGISTER_SCRIPT(Coin,
    ZM_FIELD_RANGE(Coin, BobSpeed, 0.0f, 10.0f).Header("Tebranish"),
    ZM_FIELD_RANGE(Coin, BobHeight, 0.0f, 1.0f).Tooltip("Height of the up-and-down bobbing (units)"),
    ZM_FIELD_TOOLTIP(Coin, Effect, "The prefab spawned when collected (path inside Assets)"))

// The camera follows the player smoothly.
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
        t.Translation.x = Math::Damp(t.Translation.x, desired.x, Smoothness, ts);
        t.Translation.y = Math::Damp(t.Translation.y, desired.y, Smoothness, ts);
    }
};
ZM_REGISTER_SCRIPT(CameraFollow, ZM_FIELD(CameraFollow, Target), ZM_FIELD(CameraFollow, Smoothness),
                   ZM_FIELD(CameraFollow, Offset), ZM_FIELD(CameraFollow, MinY))

// A kinematic platform: moves back and forth (and carries the player standing on it).
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
                   ZM_FIELD_ENUM(MovingPlatform, Mode, "Smooth (sine)", "Linear").Label("Movement type")
                       .Tooltip("Smooth: slows down at the ends. Linear: goes back and forth at a constant speed"))

// Clouds: drift slowly and come back from the edge of the screen.
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

// The end of a level: when the player touches it, the game goes to the next scene.
class LevelGoal : public ScriptableEntity
{
public:
    std::string NextScene = "Scenes/Level2.zscene";
    float Delay = 0.6f;
    glm::vec4 ReachedColor = { 1.0f, 0.85f, 0.2f, 1.0f };
    float FadeSeconds = 0.0f; // > 0: the next level comes in with a fade (SceneTransition)

    void OnTriggerBegin(Entity other) override
    {
        if (other.HasTag("Player") && m_Timer < 0.0f)
        {
            m_Timer = Delay;
            ZM_INFO("Level complete! Next: {}", NextScene);
            if (auto* sprite = TryGetComponent<SpriteRendererComponent>())
                sprite->Color = ReachedColor;
        }
    }

    void OnUpdate(Timestep ts) override
    {
        if (m_Timer < 0.0f) return;
        m_Timer -= ts;
        if (m_Timer <= 0.0f)
        {
            if (FadeSeconds > 0.0f)
                LoadScene(NextScene, SceneTransition::Fade(FadeSeconds));
            else
                LoadScene(NextScene);
        }
    }

private:
    float m_Timer = -1.0f;
};
ZM_REGISTER_SCRIPT(LevelGoal, ZM_FIELD_TOOLTIP(LevelGoal, NextScene, "Next level scene (path inside Assets)"),
                   ZM_FIELD_RANGE(LevelGoal, Delay, 0.0f, 5.0f).Tooltip("Wait before the transition (seconds)"),
                   ZM_FIELD_COLOR(LevelGoal, ReachedColor).Tooltip("Flag color when the player arrives"),
                   ZM_FIELD_RANGE(LevelGoal, FadeSeconds, 0.0f, 2.0f).Tooltip("Above 0: fades to the next level (seconds)"))

// Destroys the entity after N seconds (for effects).
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

// A button on the screen: reloads the scene when pressed.
class RestartButton : public ScriptableEntity
{
public:
    std::string Scene = "Scenes/Level1.zscene";
    void OnClick() override
    {
        ZM_INFO("Restarting...");
        LoadScene(Scene);
    }
};
ZM_REGISTER_SCRIPT(RestartButton, ZM_FIELD(RestartButton, Scene))
