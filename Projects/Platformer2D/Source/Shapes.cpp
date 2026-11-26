#include <Zamin/Zamin.h>

#include <deque>

using namespace Zamin;

// The Shapes2D scene: small scripts that show polygon / edge colliders, physics materials, collision layers
// and one-way platforms.

// Drops random shapes from above (the templates sit in the inactive "Templates" group).
// "Fire" (mouse / Ctrl) - several at once. The oldest disappear once there are more than MaxShapes.
class ShapeSpawner : public ScriptableEntity
{
public:
    EntityRef Boulder;
    EntityRef Crystal;
    EntityRef Crate;
    EntityRef Ball;
    EntityRef Ghost;
    float Interval = 0.5f;
    float Width = 22.0f;
    int MaxShapes = 70;

    void OnUpdate(Timestep ts) override
    {
        m_Time += ts;
        const bool burst = Input::IsActionJustPressed("Fire");
        if (m_Time < Interval && !burst)
            return;
        m_Time = 0.0f;
        for (int i = 0; i < (burst ? 6 : 1); i++)
            Spawn();
    }

private:
    void Spawn()
    {
        const EntityRef choices[] = { Boulder, Boulder, Crystal, Crate, Ball, Ghost, Ghost };
        Entity templ = Resolve(choices[Random::Index(std::size(choices))]);
        if (!templ)
            return;
        const glm::vec3 at = Transform().Translation;
        Entity copy = Instantiate(templ, { at.x + Random::Range(-Width * 0.5f, Width * 0.5f), at.y + Random::Range(-0.5f, 0.5f), 0.0f });
        if (!copy)
            return;
        SetVelocity(copy, { Random::Range(-1.5f, 1.5f), 0.0f });
        m_Spawned.push_back(copy.GetUUID());
        while ((int)m_Spawned.size() > std::max(1, MaxShapes))
        {
            if (Entity old = GetScene().GetEntityByUUID(m_Spawned.front()))
                Destroy(old);
            m_Spawned.pop_front();
        }
    }

    float m_Time = 0.0f;
    std::deque<UUID> m_Spawned;
};

ZM_REGISTER_SCRIPT(ShapeSpawner,
    ZM_FIELD(ShapeSpawner, Boulder).Header("Templates").Tooltip("Rock (Polygon Collider 2D)"),
    ZM_FIELD(ShapeSpawner, Crystal).Tooltip("Crystal (concave Polygon Collider 2D)"),
    ZM_FIELD(ShapeSpawner, Crate).Tooltip("Crate (Box Collider 2D)"),
    ZM_FIELD(ShapeSpawner, Ball).Tooltip("Rubber ball (the Bouncy physics material)"),
    ZM_FIELD(ShapeSpawner, Ghost).Tooltip("Ghost ball: on the Ghost layer - collides only with Ground"),
    ZM_FIELD_RANGE(ShapeSpawner, Interval, 0.05f, 10.0f).Header("Dropping").Tooltip("Seconds per shape"),
    ZM_FIELD_RANGE(ShapeSpawner, Width, 0.0f, 60.0f).Tooltip("Width of the area the shapes fall from"),
    ZM_FIELD_RANGE(ShapeSpawner, MaxShapes, 1, 300).Tooltip("At most this many shapes in the scene"))

// Shoots a template upward from below: the ball passes through a one-way platform and lands on top of it.
class ShapeLauncher : public ScriptableEntity
{
public:
    EntityRef Template;
    float Interval = 1.8f;
    glm::vec2 Velocity = { -1.2f, 13.0f };
    int MaxShapes = 10;

    void OnUpdate(Timestep ts) override
    {
        m_Time += ts;
        if (m_Time < Interval)
            return;
        m_Time = 0.0f;
        Entity templ = Resolve(Template);
        if (!templ)
            return;
        Entity copy = Instantiate(templ, Transform().Translation);
        if (!copy)
            return;
        SetVelocity(copy, Velocity + glm::vec2(Random::Range(-0.4f, 0.4f), 0.0f));
        m_Spawned.push_back(copy.GetUUID());
        while ((int)m_Spawned.size() > std::max(1, MaxShapes))
        {
            if (Entity old = GetScene().GetEntityByUUID(m_Spawned.front()))
                Destroy(old);
            m_Spawned.pop_front();
        }
    }

private:
    float m_Time = 0.0f;
    std::deque<UUID> m_Spawned;
};

ZM_REGISTER_SCRIPT(ShapeLauncher,
    ZM_FIELD(ShapeLauncher, Template).Tooltip("The template that is fired"),
    ZM_FIELD_RANGE(ShapeLauncher, Interval, 0.1f, 30.0f).Tooltip("Seconds per shot"),
    ZM_FIELD(ShapeLauncher, Velocity).Tooltip("Initial speed (units/s)"),
    ZM_FIELD_RANGE(ShapeLauncher, MaxShapes, 1, 100).Tooltip("At most this many (the oldest disappear)"))
