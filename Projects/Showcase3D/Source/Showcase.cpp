#include <Zamin/Zamin.h>

#include <cmath>
#include <deque>
#include <format>
#include <map>
#include <unordered_map>

using namespace Zamin;

// Spins the entity around its own axis.
class Rotator : public ScriptableEntity
{
public:
    glm::vec3 Speed = { 0.0f, 1.0f, 0.0f }; // radians / second
    void OnUpdate(Timestep ts) override { Transform().Rotation += Speed * (float)ts; }
};
ZM_REGISTER_SCRIPT(Rotator, ZM_FIELD(Rotator, Speed))

// Bobbing up and down.
class Bobbing : public ScriptableEntity
{
public:
    float Height = 0.3f;
    float Speed = 1.5f;
    void OnCreate() override { m_BaseY = Transform().Translation.y; }
    void OnUpdate(Timestep) override
    {
        Transform().Translation.y = m_BaseY + std::sin(GetTime() * Speed) * Height;
        Transform().Rotation.y = GetTime() * 0.3f;
    }
private:
    float m_BaseY = 0.0f;
};
ZM_REGISTER_SCRIPT(Bobbing, ZM_FIELD(Bobbing, Height), ZM_FIELD(Bobbing, Speed))

// Flies in a circle around the center (for the light spheres).
class Orbit : public ScriptableEntity
{
public:
    float Radius = 3.0f, Speed = 1.0f, Phase = 0.0f, Height = 2.0f;
    void OnUpdate(Timestep) override
    {
        float a = GetTime() * Speed + Phase;
        Transform().Translation = { std::cos(a) * Radius, Height + std::sin(a * 2.0f) * 0.4f, std::sin(a) * Radius };
    }
};
ZM_REGISTER_SCRIPT(Orbit, ZM_FIELD(Orbit, Radius), ZM_FIELD(Orbit, Speed), ZM_FIELD(Orbit, Phase), ZM_FIELD(Orbit, Height))

// The camera slowly orbits the scene; it stops while the right mouse button is held.
class OrbitCamera : public ScriptableEntity
{
public:
    float Distance = 14.0f, Height = 5.0f, Speed = 0.15f;
    void OnUpdate(Timestep ts) override
    {
        if (!Input::IsMouseButtonPressed(MouseButton::Right))
            m_Angle += Speed * (float)ts;
        auto& t = Transform();
        t.Translation = { std::sin(m_Angle) * Distance, Height, std::cos(m_Angle) * Distance };
        glm::vec3 dir = glm::normalize(glm::vec3(0.0f, 1.0f, 0.0f) - t.Translation);
        t.Rotation = { std::asin(dir.y), std::atan2(-dir.x, -dir.z), 0.0f };
    }
private:
    float m_Angle = 0.0f;
};
ZM_REGISTER_SCRIPT(OrbitCamera, ZM_FIELD(OrbitCamera, Distance), ZM_FIELD(OrbitCamera, Height), ZM_FIELD(OrbitCamera, Speed))

// Drives a Slider Joint 3D motor up and down between its limits (a lift platform): at each end it
// waits Pause seconds, then goes back.
class SliderShuttle : public ScriptableEntity
{
public:
    float Speed = 1.0f;  // units / second
    float Pause = 0.6f;  // the wait at each end (seconds)
    void OnUpdate(Timestep ts) override
    {
        auto* slider = TryGetComponent<SliderJoint3DComponent>();
        if (!slider || !slider->RuntimeJoint)
            return;
        slider->EnableMotor = true;
        if (m_Wait > 0.0f)
        {
            m_Wait -= (float)ts;
            slider->MotorSpeed = 0.0f; // holds it in place
            return;
        }
        const float target = m_Up ? slider->UpperTranslation : slider->LowerTranslation;
        slider->MotorSpeed = m_Up ? Speed : -Speed;
        if (std::abs(slider->JointTranslation - target) < 0.03f)
        {
            m_Up = !m_Up;
            m_Wait = Pause;
        }
    }
private:
    bool m_Up = true;
    float m_Wait = 0.0f;
};
ZM_REGISTER_SCRIPT(SliderShuttle, ZM_FIELD(SliderShuttle, Speed), ZM_FIELD(SliderShuttle, Pause))

// A cube that walks with WASD and jumps with Space.
class PlayerMover3D : public ScriptableEntity
{
public:
    float Speed = 6.0f;
    float JumpVelocity = 7.0f;
    float Gravity = 20.0f;

    void OnUpdate(Timestep ts) override
    {
        auto& t = Transform();
        glm::vec3 dir(0.0f);
        if (Input::IsKeyPressed(Key::W) || Input::IsKeyPressed(Key::Up))    dir.z -= 1.0f;
        if (Input::IsKeyPressed(Key::S) || Input::IsKeyPressed(Key::Down))  dir.z += 1.0f;
        if (Input::IsKeyPressed(Key::A) || Input::IsKeyPressed(Key::Left))  dir.x -= 1.0f;
        if (Input::IsKeyPressed(Key::D) || Input::IsKeyPressed(Key::Right)) dir.x += 1.0f;
        if (glm::length(dir) > 0.0f)
        {
            dir = glm::normalize(dir);
            t.Translation += dir * Speed * (float)ts;
            t.Rotation.y = std::atan2(-dir.x, -dir.z);
        }

        bool grounded = t.Translation.y <= 0.5f;
        if (grounded && Input::IsKeyPressed(Key::Space))
            m_VelocityY = JumpVelocity;
        m_VelocityY -= Gravity * (float)ts;
        t.Translation.y = std::max(0.5f, t.Translation.y + m_VelocityY * (float)ts);
        if (t.Translation.y <= 0.5f && m_VelocityY < 0.0f)
            m_VelocityY = 0.0f;
    }
private:
    float m_VelocityY = 0.0f;
};
ZM_REGISTER_SCRIPT(PlayerMover3D, ZM_FIELD(PlayerMover3D, Speed), ZM_FIELD(PlayerMover3D, JumpVelocity), ZM_FIELD(PlayerMover3D, Gravity))

// Space or the left mouse button: shoots a ball in the camera's direction (prefab + 3D physics).
class BallShooter : public ScriptableEntity
{
public:
    std::string Prefab = "Prefabs/Ball.zprefab";
    float Speed = 18.0f;
    float Cooldown = 0.25f;

    void OnUpdate(Timestep ts) override
    {
        m_Timer -= ts;
        bool fire = Input::IsKeyPressed(Key::Space) || Input::IsMouseButtonPressed(MouseButton::Left) ||
                    Input::IsGamepadButtonPressed(GamepadButton::A);
        if (fire && m_Timer <= 0.0f)
        {
            m_Timer = Cooldown;
            glm::vec3 forward = Transform().GetForward();
            Entity ball = Instantiate(Prefab, GetWorldPosition() + forward * 1.5f);
            SetVelocity3D(ball, forward * Speed);
        }
    }
private:
    float m_Timer = 0.0f;
};
ZM_REGISTER_SCRIPT(BallShooter, ZM_FIELD(BallShooter, Prefab), ZM_FIELD(BallShooter, Speed), ZM_FIELD(BallShooter, Cooldown))

// Destroys the entity after N seconds (for the balls that were shot).
class Lifetime : public ScriptableEntity
{
public:
    float Seconds = 8.0f;
    void OnUpdate(Timestep ts) override
    {
        Seconds -= ts;
        if (Seconds <= 0.0f || Transform().Translation.y < -20.0f)
            Destroy();
    }
};
ZM_REGISTER_SCRIPT(Lifetime, ZM_FIELD(Lifetime, Seconds))

// Builds a tower of cubes (a 3D physics demo). Each brick colors its own copy (GetMaterial) of the shared
// Clay.zmat material by its floor - the shared asset and the other entities do not change.
class TowerBuilder : public ScriptableEntity
{
public:
    int Levels = 5;
    float Size = 0.9f;
    glm::vec3 BottomColor{ 0.86f, 0.42f, 0.3f };
    glm::vec3 TopColor{ 0.98f, 0.8f, 0.36f };

    void OnCreate() override
    {
        Entity brick = FindEntity("Brick");
        if (!brick) return;
        glm::vec3 base = GetWorldPosition();
        for (int level = 0; level < Levels; level++)
            for (int i = 0; i < Levels - level; i++)
            {
                glm::vec3 p = base + glm::vec3((i - (Levels - level - 1) * 0.5f) * Size * 1.02f, 0.5f * Size + level * Size, 0.0f);
                Entity clone = Instantiate(brick, p);
                clone.SetName(std::format("Brick {}-{}", level + 1, i + 1));
                // Per-brick material instance: only this brick changes colour.
                if (Ref<MaterialAsset> material = GetMaterial(clone))
                {
                    float t = Levels > 1 ? (float)level / (float)(Levels - 1) : 0.0f;
                    material->SetColor(glm::mix(BottomColor, TopColor, t) * Random::Range(0.92f, 1.06f));
                }
            }
        Destroy(brick); // template is not needed anymore
    }
};
ZM_REGISTER_SCRIPT(TowerBuilder, ZM_FIELD(TowerBuilder, Levels), ZM_FIELD(TowerBuilder, Size), ZM_FIELD_COLOR(TowerBuilder, BottomColor),
                   ZM_FIELD_COLOR(TowerBuilder, TopColor))

// The DebugDraw sample (Scenes/DebugDraw.zscene): a rotating raycast "radar", shapes around the spheres,
// motion arrows, a trail (points with a duration) and labels. In the editor also turn on the raycasts and
// contact points from the "Debug drawing" menu.
class DebugDrawDemo : public ScriptableEntity
{
public:
    int Rays = 9;
    float Spread = 1.1f;      // fan width (radians)
    float SweepSpeed = 0.5f;  // radians / second
    float Range = 18.0f;

    void OnUpdate(Timestep ts) override
    {
        Scene& scene = GetScene();
        const glm::vec3 origin = GetWorldPosition();
        const float heading = GetTime() * SweepSpeed;

        // Raycast fan: with "Raycastlar" on the engine draws every query (hit: green + normal, miss:
        // red); the middle ray's hit gets a label with the name and distance.
        for (int i = 0; i < Rays; i++)
        {
            float a = heading + Spread * ((float)i / (float)std::max(1, Rays - 1) - 0.5f);
            glm::vec3 dir(std::sin(a), -0.02f, std::cos(a));
            RaycastHit hit = Raycast3D(origin, dir, Range);
            if (hit && i == Rays / 2)
            {
                Entity target = scene.GetEntityByUUID(hit.EntityID);
                DebugDraw::Text(hit.Point + glm::vec3(0.0f, 0.8f, 0.0f),
                                std::format("{}\n{:.1f} m", target ? target.GetName() : std::string("?"), hit.Distance), DebugColor::Yellow);
            }
        }
        DebugDraw::Axes(scene.GetWorldTransform(GetEntity()), 1.2f);
        DebugDraw::WireSphere(origin, 0.35f, DebugColor::Cyan);
        DebugDraw::Text(origin + glm::vec3(0.0f, 1.6f, 0.0f), "Radar", DebugColor::Cyan);

        // Orbs: a ring around each, an arrow along its motion and a fading trail of points.
        m_TrailTimer -= ts;
        const bool dropTrail = m_TrailTimer <= 0.0f;
        if (dropTrail)
            m_TrailTimer = 0.08f;
        int index = 0;
        for (const char* name : { "Orb 1", "Orb 2", "Orb 3" })
        {
            Entity orb = FindEntity(name);
            if (!orb) continue;
            glm::vec3 p = scene.GetWorldPosition(orb);
            glm::vec3& last = m_LastOrb[index++];
            glm::vec3 velocity = ts > 0.0f && glm::length(last) > 0.0f ? (p - last) / (float)ts : glm::vec3(0.0f);
            last = p;
            DebugDraw::WireSphere(p, 0.45f, DebugColor::Yellow);
            if (glm::length(velocity) > 0.01f)
                DebugDraw::Arrow(p, p + velocity * 0.6f, DebugColor::Orange, 0.0f, true, 0.25f);
            if (dropTrail)
                DebugDraw::Point(p, glm::vec4(1.0f, 0.85f, 0.35f, 0.8f), 1.5f, true, 5.0f); // 1.5 s of game time
            DebugDraw::Text(p + glm::vec3(0.0f, 0.75f, 0.0f), name, DebugColor::White, 0.0f, true);
        }

        // Oriented boxes around the pillars, hidden behind what stands in front of them.
        for (Entity pillar : FindEntities("Pillar"))
            DebugDraw::Box(scene.GetWorldTransform(pillar) * glm::scale(glm::mat4(1.0f), glm::vec3(1.15f, 1.02f, 1.15f)), DebugColor::Magenta);
        if (Entity hero = FindEntity("Hero Sphere"))
        {
            glm::vec3 p = scene.GetWorldPosition(hero);
            DebugDraw::Circle(glm::vec3(p.x, 0.02f, p.z), glm::vec3(0.0f, 1.0f, 0.0f), 2.0f, DebugColor::Green);
            DebugDraw::Text(p + glm::vec3(0.0f, 1.7f, 0.0f), "Hero Sphere", DebugColor::Green, 0.0f, true);
        }
    }

private:
    float m_TrailTimer = 0.0f;
    glm::vec3 m_LastOrb[3]{};
};
ZM_REGISTER_SCRIPT(DebugDrawDemo, ZM_FIELD(DebugDrawDemo, Rays), ZM_FIELD(DebugDrawDemo, Spread), ZM_FIELD(DebugDrawDemo, SweepSpeed),
                   ZM_FIELD(DebugDrawDemo, Range))

// A character (CharacterController3D) walking round a rectangle of waypoints. The route ahead is
// drawn with DebugDraw::NavigationPath; with "Personaj tezligi" on (debug drawing) the engine
// draws its velocity, and the inspector shows it.
class PatrolWalker : public ScriptableEntity
{
public:
    float Speed = 3.0f;
    float Width = 7.0f;     // the rectangle, from the start towards -X ...
    float Depth = 2.0f;     // ... and -Z
    bool ShowRoute = true;

    void OnStart() override
    {
        const glm::vec3 s = GetWorldPosition();
        m_Route = { s, s + glm::vec3(0.0f, 0.0f, -Depth), s + glm::vec3(-Width, 0.0f, -Depth), s + glm::vec3(-Width, 0.0f, 0.0f) };
        m_Next = 1;
    }

    void OnUpdate(Timestep) override
    {
        auto* cc = TryGetComponent<CharacterController3DComponent>();
        if (!cc)
            return;
        const glm::vec3 p = GetWorldPosition();
        glm::vec3 to = m_Route[m_Next] - p;
        to.y = 0.0f;
        if (glm::length(to) < 0.25f)
        {
            m_Next = (m_Next + 1) % m_Route.size();
            to = m_Route[m_Next] - p;
            to.y = 0.0f;
        }
        if (glm::length(to) > 1e-4f)
        {
            glm::vec3 dir = glm::normalize(to);
            cc->Move(dir * Speed);
            Transform().Rotation.y = std::atan2(-dir.x, -dir.z); // -Z (the visor) faces the way
        }
        if (ShowRoute)
        {
            // One lap ahead, a little above the ground.
            std::vector<glm::vec3> path{ p };
            for (size_t k = 0; k < m_Route.size(); k++)
                path.push_back(m_Route[(m_Next + k) % m_Route.size()]);
            for (glm::vec3& point : path)
                point.y += 0.05f;
            DebugDraw::NavigationPath(path, DebugColor::Cyan);
        }
    }

private:
    std::vector<glm::vec3> m_Route;
    size_t m_Next = 1;
};
ZM_REGISTER_SCRIPT(PatrolWalker, ZM_FIELD(PatrolWalker, Speed), ZM_FIELD(PatrolWalker, Width), ZM_FIELD(PatrolWalker, Depth),
                   ZM_FIELD(PatrolWalker, ShowRoute))

// ---- The Queries3D scene: 3D physics queries. When the "Raycasts" debug drawing is on, the engine draws
// every query itself (the shape, the path, the hit points, the normals); otherwise the scripts show the
// result themselves.

// A label that is always visible above an entity.
class Label3D : public ScriptableEntity
{
public:
    std::string Text = "Label";
    glm::vec3 Offset{ 0.0f, 1.0f, 0.0f };
    glm::vec4 Color{ 1.0f, 1.0f, 1.0f, 1.0f };
    void OnUpdate(Timestep) override { DebugDraw::Text(GetWorldPosition() + Offset, Text, Color); }
};
ZM_REGISTER_SCRIPT(Label3D, ZM_FIELD(Label3D, Text), ZM_FIELD(Label3D, Offset), ZM_FIELD_COLOR(Label3D, Color))

// "A thick ray": SphereCast3DAll finds every entity on its path in order, and it sweeps from side to side.
// The entities on the Ghost layer are left out of the mask (LayerMask).
class SphereSweep3D : public ScriptableEntity
{
public:
    float Radius = 0.35f;
    float Range = 11.0f;
    float Spread = 0.55f;    // radians: how far it turns to each side
    float SweepSpeed = 0.5f;
    bool SkipGhosts = true;  // ignores the "Ghost" layer

    void OnUpdate(Timestep) override
    {
        const glm::vec3 origin = GetWorldPosition();
        const float a = std::sin(GetTime() * SweepSpeed) * Spread;
        const glm::vec3 dir(-std::cos(a), 0.0f, std::sin(a));
        const uint16_t mask = SkipGhosts ? (uint16_t)~LayerMask({ "Ghost" }) : (uint16_t)0xFFFF;
        const std::vector<RaycastHit> hits = SphereCast3DAll(origin, Radius, dir, Range, mask);
        if (!DebugDraw::IsShowingRaycasts())
        {
            const float stop = hits.empty() ? Range : hits.front().Distance;
            DebugDraw::Line(origin, origin + dir * stop, hits.empty() ? DebugColor::Red : DebugColor::Green, 0.0f, false);
            DebugDraw::WireSphere(origin + dir * stop, Radius, hits.empty() ? DebugColor::Red : DebugColor::Green, 0.0f, false);
            for (size_t i = 0; i < hits.size(); i++)
            {
                DebugDraw::Point(hits[i].Point, DebugColor::Yellow, 0.0f, false);
                DebugDraw::Text(hits[i].Point + glm::vec3(0.0f, 0.3f, 0.0f), std::to_string(i + 1), DebugColor::Yellow);
            }
        }
        DebugDraw::Text(origin + glm::vec3(0.0f, 0.9f, 0.0f), std::format("SphereCast3DAll: {}", hits.size()), DebugColor::Cyan);
    }
};
ZM_REGISTER_SCRIPT(SphereSweep3D, ZM_FIELD(SphereSweep3D, Radius), ZM_FIELD(SphereSweep3D, Range), ZM_FIELD(SphereSweep3D, Spread),
                   ZM_FIELD(SphereSweep3D, SweepSpeed), ZM_FIELD(SphereSweep3D, SkipGhosts))

// An OverlapSphere3D that moves in a circle: the entities inside it light up (emission) and go back to normal when they leave.
class OverlapGlow3D : public ScriptableEntity
{
public:
    float Radius = 1.4f;
    float OrbitRadius = 1.8f;
    float Speed = 0.6f;
    glm::vec4 Glow{ 1.0f, 0.7f, 0.15f, 1.0f };
    float GlowStrength = 2.5f;

    void OnCreate() override { m_Center = Transform().Translation; }

    void OnUpdate(Timestep) override
    {
        const float a = GetTime() * Speed;
        Transform().Translation = m_Center + glm::vec3(std::cos(a) * OrbitRadius, 0.0f, std::sin(a) * OrbitRadius);
        const glm::vec3 center = GetWorldPosition();
        const std::vector<Entity> found = OverlapSphere3D(center, Radius);
        std::unordered_map<uint64_t, Saved> still;
        for (Entity e : found)
        {
            auto* mr = e.TryGetComponent<MeshRendererComponent>();
            if (!mr || e == GetEntity())
                continue;
            const uint64_t id = (uint64_t)e.GetUUID();
            auto it = m_Lit.find(id);
            Saved saved = it != m_Lit.end() ? it->second : Saved{ mr->Mat.Emission, mr->Mat.EmissionStrength };
            mr->Mat.Emission = glm::vec3(Glow);
            mr->Mat.EmissionStrength = GlowStrength;
            still[id] = saved;
        }
        // The ones that left the sphere get their own light back.
        for (const auto& [id, saved] : m_Lit)
            if (!still.contains(id))
                if (Entity e = GetScene().GetEntityByUUID(id))
                    if (auto* mr = e.TryGetComponent<MeshRendererComponent>())
                    {
                        mr->Mat.Emission = saved.Emission;
                        mr->Mat.EmissionStrength = saved.Strength;
                    }
        m_Lit = std::move(still);
        if (!DebugDraw::IsShowingRaycasts())
            DebugDraw::WireSphere(center, Radius, found.empty() ? DebugColor::Red : DebugColor::Green, 0.0f, false);
        DebugDraw::Text(center + glm::vec3(0.0f, Radius + 0.3f, 0.0f), std::format("OverlapSphere3D: {}", found.size()), DebugColor::Cyan);
    }

private:
    struct Saved
    {
        glm::vec3 Emission{ 0.0f };
        float Strength = 0.0f;
    };
    glm::vec3 m_Center{ 0.0f };
    std::unordered_map<uint64_t, Saved> m_Lit;
};
ZM_REGISTER_SCRIPT(OverlapGlow3D, ZM_FIELD(OverlapGlow3D, Radius), ZM_FIELD(OverlapGlow3D, OrbitRadius), ZM_FIELD(OverlapGlow3D, Speed),
                   ZM_FIELD_COLOR(OverlapGlow3D, Glow), ZM_FIELD(OverlapGlow3D, GlowStrength))

// A laser: Raycast3DAll finds every pane on the ray's path, nearest first (each one once).
class LaserAll3D : public ScriptableEntity
{
public:
    float Range = 12.0f;
    void OnUpdate(Timestep) override
    {
        const glm::vec3 origin = GetWorldPosition();
        const glm::vec3 dir = Transform().GetForward();
        const std::vector<RaycastHit> hits = Raycast3DAll(origin, dir, Range);
        if (!DebugDraw::IsShowingRaycasts())
        {
            DebugDraw::Line(origin, origin + dir * Range, DebugColor::Red, 0.0f, false);
            for (size_t i = 0; i < hits.size(); i++)
            {
                DebugDraw::Point(hits[i].Point, DebugColor::Yellow, 0.0f, false);
                DebugDraw::Text(hits[i].Point + glm::vec3(0.0f, 0.35f, 0.0f), std::to_string(i + 1), DebugColor::Yellow);
            }
        }
        DebugDraw::Text(origin + glm::vec3(0.0f, 0.7f, 0.0f), std::format("Raycast3DAll: {}", hits.size()), DebugColor::Cyan);
    }
};
ZM_REGISTER_SCRIPT(LaserAll3D, ZM_FIELD(LaserAll3D, Range))

// ---- Physics3D scene: continuous collision, sleeping, physics materials, triggers, characters pushing.

// Shoots its body (a small ball) from where it started with Velocity every Interval seconds, and counts
// the shots that ended beyond PaneX (went through the pane) and those that stopped before it.
class Launcher3D : public ScriptableEntity
{
public:
    glm::vec3 Velocity{ 60.0f, 0.0f, 0.0f };
    float Interval = 1.2f;
    float PaneX = 4.0f;

    void OnStart() override
    {
        m_Start = Transform().Translation;
        Shoot();
    }

    void OnUpdate(Timestep ts) override
    {
        m_Time += ts;
        if (m_Time >= Interval)
        {
            (GetWorldPosition().x > PaneX ? m_Through : m_Stopped)++;
            Shoot();
        }
        const auto* rb = TryGetComponent<Rigidbody3DComponent>();
        const bool continuous = rb && rb->ContinuousCollision;
        DebugDraw::Text(m_Start + glm::vec3(0.0f, 0.9f, 0.0f),
                        std::format("{}\nPassed through: {}   Stopped: {}", continuous ? "CCD on" : "CCD off", m_Through, m_Stopped),
                        continuous ? DebugColor::Green : DebugColor::Orange);
    }

private:
    void Shoot()
    {
        m_Time = 0.0f;
        SetPosition3D(m_Start);
        SetVelocity3D(Velocity);
    }

    glm::vec3 m_Start{ 0.0f };
    float m_Time = 0.0f;
    int m_Through = 0, m_Stopped = 0;
};
ZM_REGISTER_SCRIPT(Launcher3D, ZM_FIELD(Launcher3D, Velocity), ZM_FIELD(Launcher3D, Interval), ZM_FIELD(Launcher3D, PaneX))

// Tints its mesh by whether its 3D body sleeps (IsSleeping3D): SleepColor while asleep, its own colour awake.
class SleepTint3D : public ScriptableEntity
{
public:
    glm::vec4 SleepColor{ 0.42f, 0.52f, 0.95f, 1.0f };

    void OnStart() override
    {
        if (auto* mr = TryGetComponent<MeshRendererComponent>())
            m_Awake = mr->Mat.Albedo;
    }

    void OnUpdate(Timestep) override
    {
        if (auto* mr = TryGetComponent<MeshRendererComponent>())
            mr->Mat.Albedo = IsSleeping3D() ? SleepColor : m_Awake;
    }

private:
    glm::vec4 m_Awake{ 1.0f };
};
ZM_REGISTER_SCRIPT(SleepTint3D, ZM_FIELD_COLOR(SleepTint3D, SleepColor))

// Moves its (kinematic) body Distance along Axis and back once every Period seconds, Delay seconds late.
class Sweeper3D : public ScriptableEntity
{
public:
    glm::vec3 Axis{ 1.0f, 0.0f, 0.0f };
    float Distance = 3.0f;
    float Period = 10.0f;
    float Delay = 0.0f;

    void OnStart() override { m_Start = Transform().Translation; }

    void OnUpdate(Timestep) override
    {
        const float t = std::max(0.0f, GetTime() - Delay);
        const float phase = std::fmod(t, std::max(0.1f, Period)) / std::max(0.1f, Period);
        const float s = 0.5f - 0.5f * std::cos(phase * 6.2831853f);
        const float length = glm::length(Axis);
        if (length > 1e-6f)
            Transform().Translation = m_Start + Axis / length * Distance * s;
    }

private:
    glm::vec3 m_Start{ 0.0f };
};
ZM_REGISTER_SCRIPT(Sweeper3D, ZM_FIELD(Sweeper3D, Axis), ZM_FIELD(Sweeper3D, Distance), ZM_FIELD(Sweeper3D, Period), ZM_FIELD(Sweeper3D, Delay))

// Puts its body back where it started (still, as it was turned) every Period seconds.
class Respawn3D : public ScriptableEntity
{
public:
    float Period = 6.0f;

    void OnStart() override
    {
        m_Position = Transform().Translation;
        m_Rotation = Transform().Rotation;
    }

    void OnUpdate(Timestep ts) override
    {
        m_Time += ts;
        if (m_Time < Period)
            return;
        m_Time = 0.0f;
        Transform().Rotation = m_Rotation;
        SetPosition3D(m_Position); // also stops it and wakes it
        SetAngularVelocity3D(glm::vec3(0.0f));
    }

private:
    glm::vec3 m_Position{ 0.0f }, m_Rotation{ 0.0f };
    float m_Time = 0.0f;
};
ZM_REGISTER_SCRIPT(Respawn3D, ZM_FIELD(Respawn3D, Period))

// A trigger collider of its entity (a pressure plate, or a sphere around a crate) lights it up while
// dynamic bodies or characters are inside it (OnTriggerBegin / OnTriggerEnd); Radius > 0 also draws
// that sphere.
class TriggerGlow3D : public ScriptableEntity
{
public:
    std::string Title = "Trigger";
    glm::vec4 Glow{ 0.3f, 1.0f, 0.5f, 1.0f };
    float GlowStrength = 2.5f;
    float Radius = 0.0f;
    glm::vec3 LabelOffset{ 0.0f, 0.8f, 0.0f };

    void OnStart() override
    {
        if (auto* mr = TryGetComponent<MeshRendererComponent>())
        {
            m_Emission = mr->Mat.Emission;
            m_Strength = mr->Mat.EmissionStrength;
        }
    }

    void OnTriggerBegin(Entity other) override
    {
        if (Counts(other))
            m_Inside[(uint64_t)other.GetUUID()] = other.GetName();
    }

    void OnTriggerEnd(Entity other) override { m_Inside.erase((uint64_t)other.GetUUID()); }

    void OnUpdate(Timestep) override
    {
        const bool lit = !m_Inside.empty();
        if (auto* mr = TryGetComponent<MeshRendererComponent>())
        {
            mr->Mat.Emission = lit ? glm::vec3(Glow) : m_Emission;
            mr->Mat.EmissionStrength = lit ? GlowStrength : m_Strength;
        }
        const glm::vec3 at = GetWorldPosition();
        if (Radius > 0.0f)
            DebugDraw::WireSphere(at, Radius, lit ? Glow : DebugColor::Gray, 0.0f, false);
        std::string text = std::format("{}\nInside: {}", Title, m_Inside.size());
        for (const auto& [id, name] : m_Inside)
            text += "\n" + name;
        DebugDraw::Text(at + LabelOffset, text, lit ? Glow : DebugColor::White);
    }

private:
    // Moving things only: a trigger also touches the ground it lies on.
    static bool Counts(Entity e)
    {
        if (e.HasComponent<CharacterController3DComponent>())
            return true;
        const auto* rb = e.TryGetComponent<Rigidbody3DComponent>();
        return rb && rb->Type == Rigidbody3DComponent::BodyType::Dynamic;
    }

    std::map<uint64_t, std::string> m_Inside;
    glm::vec3 m_Emission{ 0.0f };
    float m_Strength = 0.0f;
};
ZM_REGISTER_SCRIPT(TriggerGlow3D, ZM_FIELD(TriggerGlow3D, Title), ZM_FIELD_COLOR(TriggerGlow3D, Glow), ZM_FIELD(TriggerGlow3D, GlowStrength),
                   ZM_FIELD(TriggerGlow3D, Radius), ZM_FIELD(TriggerGlow3D, LabelOffset))

// ---- The Ragdoll3D scene: 11-part ragdolls joined with Cone Joint 3D (shoulders, hips, waist, neck) and
// Hinge Joint 3D (elbows, knees) tumble down the stairs.

// R - 3 more ragdolls from above the stairs, left mouse button / F - throws one from the camera toward the
// mouse, and every AutoInterval seconds it drops one by itself. At most MaxRagdolls (the old ones disappear).
class RagdollSpawner3D : public ScriptableEntity
{
public:
    std::string Prefab = "Prefabs/Ragdoll3D.zprefab";
    float AutoInterval = 3.0f;
    float Width = 4.0f;       // the width of the drop area (along Z)
    int MaxRagdolls = 8;
    float Push = 4.5f;        // the initial velocity toward the stairs (+X)
    float ThrowSpeed = 14.0f; // when thrown from the camera

    void OnUpdate(Timestep ts) override
    {
        if (Input::IsMouseButtonJustPressed(MouseButton::Left) || Input::IsKeyJustPressed(Key::F))
            Throw();
        if (Input::IsKeyJustPressed(Key::R))
            for (int i = 0; i < 3; i++)
                Spawn(SkyPoint(), { Push * Random::Range(0.6f, 1.4f), 0.0f, 0.0f });
        // The ones that fell off the scene disappear.
        for (auto it = m_Spawned.begin(); it != m_Spawned.end();)
        {
            Entity root = GetScene().GetEntityByUUID(*it);
            Entity chest = root ? GetScene().FindChild(root, "Chest") : Entity();
            if (!root || (chest && GetScene().GetWorldPosition(chest).y < -20.0f))
            {
                if (root)
                    Destroy(root);
                it = m_Spawned.erase(it);
            }
            else
                ++it;
        }
        if (AutoInterval <= 0.0f)
            return;
        m_Time += ts;
        if (m_Time < AutoInterval)
            return;
        m_Time = 0.0f;
        Spawn(SkyPoint(), { Push * Random::Range(0.6f, 1.4f), 0.0f, Random::Range(-0.5f, 0.5f) });
    }

private:
    glm::vec3 SkyPoint() const
    {
        const glm::vec3 at = GetWorldPosition();
        return { at.x + Random::Range(-1.0f, 1.0f), at.y + Random::Range(-0.3f, 0.8f), at.z + Random::Range(-Width * 0.5f, Width * 0.5f) };
    }

    // From the camera toward the point the mouse points at (on the z = 0 plane).
    void Throw()
    {
        Entity camera = GetScene().GetPrimaryCameraEntity();
        if (!camera)
            return;
        const glm::vec3 from = GetScene().GetWorldPosition(camera);
        glm::vec3 dir = ScreenToWorld(GetMousePosition(), 0.0f) - from;
        if (glm::length(dir) < 1e-3f)
            return;
        dir = glm::normalize(dir);
        // The root is at the feet: the centers of the parts are ~0.8 higher.
        Spawn(from + dir * 3.0f - glm::vec3(0.0f, 0.8f, 0.0f), dir * ThrowSpeed);
    }

    void Spawn(const glm::vec3& position, const glm::vec3& velocity)
    {
        Entity root = Instantiate(Prefab, position);
        if (!root)
        {
            ZM_WARN("Ragdoll prefab not found: {}", Prefab);
            return;
        }
        Paint(root);
        // Every part at the same velocity, the head a little different: the ragdoll flies spinning.
        for (Entity part : GetScene().GetChildren(root))
        {
            if (!part.HasComponent<Rigidbody3DComponent>())
                continue;
            glm::vec3 v = velocity;
            if (part.GetName() == "Head")
                v += Random::InsideUnitSphere() * 2.5f;
            SetVelocity3D(part, v);
        }
        m_Spawned.push_back(root.GetUUID());
        while ((int)m_Spawned.size() > std::max(1, MaxRagdolls))
        {
            if (Entity old = GetScene().GetEntityByUUID(m_Spawned.front()))
                Destroy(old);
            m_Spawned.pop_front();
        }
    }

    // Random colors for the shirt (chest, shoulders), the trousers (waist, thighs, shins) and the skin.
    void Paint(Entity root)
    {
        const float hue = Random::Float();
        auto fromHue = [](float h, float saturation, float value) {
            const glm::vec3 k = glm::fract(glm::vec3(h) + glm::vec3(0.0f, 2.0f / 3.0f, 1.0f / 3.0f)) * 6.0f - 3.0f;
            const glm::vec3 rgb = glm::clamp(glm::abs(k) - 1.0f, 0.0f, 1.0f);
            return glm::mix(glm::vec3(1.0f), rgb, saturation) * value;
        };
        const glm::vec3 shirt = fromHue(hue, 0.7f, 0.9f);
        const glm::vec3 pants = fromHue(hue + 0.5f, 0.45f, 0.45f);
        const glm::vec3 skin = glm::mix(glm::vec3(0.95f, 0.78f, 0.64f), glm::vec3(0.55f, 0.37f, 0.25f), Random::Float());
        for (Entity part : GetScene().GetChildren(root))
        {
            auto* mesh = part.TryGetComponent<MeshRendererComponent>();
            if (!mesh)
                continue;
            const std::string& name = part.GetName();
            if (name == "Chest" || name.starts_with("Upper arm"))
                mesh->Mat.Albedo = glm::vec4(shirt, 1.0f);
            else if (name == "Waist" || name.starts_with("Thigh") || name.starts_with("Shin"))
                mesh->Mat.Albedo = glm::vec4(name.starts_with("Shin") ? pants * 0.8f : pants, 1.0f);
            else if (name == "Head" || name.starts_with("Forearm"))
                mesh->Mat.Albedo = glm::vec4(skin, 1.0f);
        }
    }

    float m_Time = 0.0f;
    std::deque<UUID> m_Spawned;
};
ZM_REGISTER_SCRIPT(RagdollSpawner3D,
    ZM_FIELD(RagdollSpawner3D, Prefab).Tooltip("Ragdoll prefab (path inside Assets)"),
    ZM_FIELD_RANGE(RagdollSpawner3D, AutoInterval, 0.0f, 30.0f).Header("Dropping").Tooltip("Drops one by itself every this many seconds (0 - only R / the mouse)"),
    ZM_FIELD_RANGE(RagdollSpawner3D, Width, 0.0f, 20.0f).Tooltip("Width of the drop area (along Z)"),
    ZM_FIELD_RANGE(RagdollSpawner3D, MaxRagdolls, 1, 40).Tooltip("At most this many (the oldest disappear)"),
    ZM_FIELD_RANGE(RagdollSpawner3D, Push, 0.0f, 20.0f).Tooltip("Initial speed towards the stairs (+X)"),
    ZM_FIELD_RANGE(RagdollSpawner3D, ThrowSpeed, 0.0f, 60.0f).Tooltip("Throw speed from the camera towards the mouse"))

// ---- The PhysicsDebug3D scene: the physics debug views - contact points (normal, depth, impact ring,
// sleeping ones dimmed), value labels (depth, impact speed and impulse) and joints (anchors, axis,
// closeness to breaking as a color, a broken one as a red X).

// When the game starts it turns on the physics views of debug drawing; C - contacts, J - joints,
// V - value labels. In the editor the viewport settings come back when the game stops.
class PhysicsDebugView3D : public ScriptableEntity
{
public:
    bool Contacts = true;
    bool Joints = true;
    bool Values = true;

    void OnStart() override { Apply(); }

    void OnUpdate(Timestep) override
    {
        bool changed = false;
        if (Input::IsKeyJustPressed(Key::C)) { Contacts = !Contacts; changed = true; }
        if (Input::IsKeyJustPressed(Key::J)) { Joints = !Joints; changed = true; }
        if (Input::IsKeyJustPressed(Key::V)) { Values = !Values; changed = true; }
        if (changed)
            Apply();
        auto mark = [](bool on) { return on ? "[x]" : "[ ]"; };
        DebugDraw::Text(GetWorldPosition(),
                        std::format("{} C - contacts   {} J - joints   {} V - values", mark(Contacts), mark(Joints), mark(Values)),
                        glm::vec4(0.9f, 0.95f, 1.0f, 1.0f));
    }

private:
    void Apply()
    {
        DebugDrawSettings& s = DebugDraw::GetSettings();
        s.Enabled = true;
        s.ShowContacts = Contacts;
        s.ShowJoints = Joints;
        s.ShowContactDetails = Values;
    }
};
ZM_REGISTER_SCRIPT(PhysicsDebugView3D,
    ZM_FIELD(PhysicsDebugView3D, Contacts).Tooltip("Contact points and normals (C)"),
    ZM_FIELD(PhysicsDebugView3D, Joints).Tooltip("Joints: anchors, axis, load color (J)"),
    ZM_FIELD(PhysicsDebugView3D, Values).Tooltip("Depth, hit, impulse and force labels (V)"))

// ---- The Audio scene: mixer groups, music crossfade, Audio Listener.
// The music of whichever zone the listener (Audio Listener) is in plays with a crossfade
// (Audio::CrossfadeTo, the Music group, streamed from disk). A signal plays now and then in the UI group.
// 1 / 2 - pick the music by hand, M - mute the Music group, P - pause, S - fade the music out.
class MusicDirector : public ScriptableEntity
{
public:
    std::string CalmTrack = "Audio/MusicCalm.wav";
    std::string TenseTrack = "Audio/MusicTense.wav";
    float Crossfade = 2.0f;
    float Border = 0.0f;       // the border between the zones (X)
    std::string Ping = "Audio/UiPing.wav";
    float PingInterval = 2.5f; // the signal in the UI group (0: none)

    void OnStart() override
    {
        Audio::PlayMusic(CalmTrack, 1.0f);
        m_Zone = 0;
    }

    void OnUpdate(Timestep ts) override
    {
        Scene* scene = GetEntity().GetScene();
        Entity listener = scene->GetAudioListenerEntity();
        if (listener && !m_Manual)
        {
            float x = scene->GetWorldPosition(listener).x;
            int zone = x < Border - 0.5f ? 0 : x > Border + 0.5f ? 1 : m_Zone; // a little dead band at the border
            if (zone != m_Zone)
            {
                m_Zone = zone;
                Audio::CrossfadeTo(zone == 0 ? CalmTrack : TenseTrack, Crossfade);
            }
        }
        if (Input::IsKeyJustPressed(Key::D1) || Input::IsKeyJustPressed(Key::D2))
        {
            m_Manual = true;
            m_Zone = Input::IsKeyJustPressed(Key::D1) ? 0 : 1;
            Audio::CrossfadeTo(m_Zone == 0 ? CalmTrack : TenseTrack, Crossfade);
        }
        if (Input::IsKeyJustPressed(Key::M))
            Audio::SetGroupMuted("Music", !Audio::IsGroupMuted("Music"));
        if (Input::IsKeyJustPressed(Key::P))
        {
            if (Audio::IsMusicPaused())
                Audio::ResumeMusic();
            else
                Audio::PauseMusic();
        }
        if (Input::IsKeyJustPressed(Key::S))
        {
            if (Audio::IsMusicPlaying())
                Audio::StopMusic(1.5f);
            else
                Audio::PlayMusic(m_Zone == 0 ? CalmTrack : TenseTrack, 1.5f);
        }
        if (PingInterval > 0.0f && (m_PingTime += ts) >= PingInterval)
        {
            m_PingTime = 0.0f;
            PlaySound(Ping, 0.6f, m_Zone == 0 ? 1.0f : 1.5f, "UI");
        }

        std::string music = Audio::GetMusic();
        std::string state = music.empty() ? std::string("none") : music.substr(music.find_last_of('/') + 1);
        if (Audio::IsMusicPaused())
            state += " (paused)";
        if (Audio::IsGroupMuted("Music"))
            state += " [Music: mute]";
        DebugDraw::Text(GetWorldPosition(), std::format("Music: {}   |   1 / 2 - track, M - mute, P - pause, S - stop", state),
                        glm::vec4(0.9f, 0.95f, 1.0f, 1.0f));
    }

private:
    int m_Zone = 0;
    bool m_Manual = false;
    float m_PingTime = 0.0f;
};
ZM_REGISTER_SCRIPT(MusicDirector,
    ZM_FIELD(MusicDirector, CalmTrack).Tooltip("Music of the calm zone (X < the border)"),
    ZM_FIELD(MusicDirector, TenseTrack).Tooltip("Music of the danger zone (X > the border)"),
    ZM_FIELD(MusicDirector, Crossfade).Tooltip("Crossfade time of the tracks, seconds"),
    ZM_FIELD(MusicDirector, Border),
    ZM_FIELD(MusicDirector, Ping).Tooltip("The signal played in the UI group"),
    ZM_FIELD(MusicDirector, PingInterval))

// A sound source flies fast along one axis (back and forth on a cosine) and its live state is written
// above it: the pitch rises as it approaches the listener and falls as it moves away (the Doppler effect;
// the velocity is measured from the entity's motion, and the Audio Source's Doppler factor amplifies it).
class DopplerFlyby : public ScriptableEntity
{
public:
    glm::vec3 Axis{ 0.0f, 0.0f, 1.0f };
    float Distance = 12.0f;
    float Period = 4.0f;

    void OnStart() override { m_Start = Transform().Translation; }

    void OnUpdate(Timestep) override
    {
        const float period = std::max(0.1f, Period);
        const float s = 0.5f - 0.5f * std::cos(std::fmod(GetTime(), period) / period * 6.2831853f);
        if (glm::length(Axis) > 1e-6f)
            Transform().Translation = m_Start + glm::normalize(Axis) * Distance * s;

        SoundInfo info;
        if (!GetEntity().GetScene()->GetAudioInfo(GetEntity(), info))
            return;
        const char* trend = info.DopplerPitch > 1.005f ? "yaqinlashmoqda" : info.DopplerPitch < 0.995f ? "uzoqlashmoqda" : "";
        DebugDraw::Text(GetWorldPosition() + glm::vec3(0.0f, 0.7f, 0.0f),
                        std::format("Doppler x{:.3f}  {:.1f} m/s  {}", info.DopplerPitch, glm::length(info.Velocity), trend),
                        glm::vec4(1.0f, 0.75f, 0.4f, 1.0f));
    }

private:
    glm::vec3 m_Start{ 0.0f };
};

ZM_REGISTER_SCRIPT(DopplerFlyby,
    ZM_FIELD(DopplerFlyby, Axis).Tooltip("Flight direction"),
    ZM_FIELD(DopplerFlyby, Distance).Tooltip("Back-and-forth distance, m"),
    ZM_FIELD(DopplerFlyby, Period).Tooltip("Time of one trip there and back, seconds (top speed: pi x distance / time)"))

// Wind chimes: one short chime sound plays at random intervals - each time with a different pitch and
// volume (SoundParams::PitchRandom / VolumeRandom). At most MaxAtOnce of them are heard at the same time:
// a new one takes the place of the quietest (MaxInstances + Quietest). H - pause / resume the shrine's
// hum (the Audio Source script API: PauseAudio / ResumeAudio / GetAudioTime).
class ChimeRinger : public ScriptableEntity
{
public:
    std::string Clip = "Audio/Chime.wav";
    float MinInterval = 0.15f;
    float MaxInterval = 0.55f;
    glm::vec2 PitchRange{ 0.75f, 1.35f };
    glm::vec2 VolumeRange{ 0.55f, 1.0f };
    int MaxAtOnce = 3;
    EntityRef Hum;

    void OnUpdate(Timestep ts) override
    {
        if ((m_Wait -= ts) <= 0.0f)
        {
            m_Wait = Random::Range(MinInterval, MaxInterval);
            SoundParams chime;
            chime.Spatial = true;
            chime.Position = GetWorldPosition();
            chime.MinDistance = 1.5f;
            chime.MaxDistance = 12.0f;
            chime.PitchRandom = PitchRange;
            chime.VolumeRandom = VolumeRange;
            chime.MaxInstances = MaxAtOnce;
            chime.Steal = AudioStealPolicy::Quietest;
            chime.Priority = 100;
            if (uint32_t handle = PlaySound(Clip, chime))
            {
                m_Handles.push_back(handle);
                m_Last = handle;
            }
        }
        std::erase_if(m_Handles, [](uint32_t h) { return !AudioEngine::IsPlaying(h); });

        Entity hum = Resolve(Hum);
        if (hum && Input::IsKeyJustPressed(Key::H))
        {
            if (IsAudioPaused(hum))
                ResumeAudio(hum);
            else
                PauseAudio(hum);
        }

        SoundInfo info;
        std::string last = AudioEngine::GetSoundInfo(m_Last, info)
                               ? std::format("pitch x{:.2f}, volume x{:.2f}", info.PitchVariation, info.VolumeVariation)
                               : std::string("-");
        std::string humState = !hum                  ? std::string("-")
                               : IsAudioPaused(hum)  ? std::format("paused ({:.1f} s)", GetAudioTime(hum))
                               : IsAudioPlaying(hum) ? std::format("{:.1f} s", GetAudioTime(hum))
                                                     : std::string("silent");
        DebugDraw::Text(GetWorldPosition() + glm::vec3(0.0f, 1.45f, 0.0f), std::format("Chime: {}", last),
                        glm::vec4(1.0f, 0.9f, 0.55f, 1.0f));
        DebugDraw::Text(GetWorldPosition() + glm::vec3(0.0f, 0.9f, 0.0f),
                        std::format("at once {}/{}   H - hum: {}", m_Handles.size(), MaxAtOnce, humState),
                        glm::vec4(0.75f, 0.9f, 1.0f, 1.0f));
    }

private:
    float m_Wait = 0.0f;
    uint32_t m_Last = 0;
    std::vector<uint32_t> m_Handles;
};

ZM_REGISTER_SCRIPT(ChimeRinger,
    ZM_FIELD(ChimeRinger, Clip).Tooltip("Bell sound"),
    ZM_FIELD_RANGE(ChimeRinger, MinInterval, 0.05f, 5.0f).Header("Chalinish").Tooltip("Shortest time between two rings, s"),
    ZM_FIELD_RANGE(ChimeRinger, MaxInterval, 0.05f, 5.0f).Tooltip("Longest time, s"),
    ZM_FIELD(ChimeRinger, PitchRange).Header("Randomness").Tooltip("Pitch multiplier: min, max"),
    ZM_FIELD(ChimeRinger, VolumeRange).Tooltip("Volume multiplier: min, max"),
    ZM_FIELD_RANGE(ChimeRinger, MaxAtOnce, 1, 16).Tooltip("How many can be heard at once"),
    ZM_FIELD(ChimeRinger, Hum).Header("Pause sample").Tooltip("The Audio Source paused with the H key"))

// A radio: an Audio Source's fades and filters. The source has FadeIn 1.5 s / FadeOut 1.2 s and a high-pass 700 Hz +
// low-pass 3.5 kHz (as if from a small speaker). R - turn the radio on / off (it fades in and out),
// T - turn the "speaker" filter on / off (the cutoff frequency glides smoothly), G - lower it with FadeAudio.
class RadioToggle : public ScriptableEntity
{
public:
    float HighPass = 700.0f;
    float LowPass = 3500.0f;
    float DuckVolume = 0.3f;

    void OnStart() override
    {
        if (auto* audio = GetEntity().TryGetComponent<AudioSourceComponent>())
            m_Filtered = audio->Filter.IsActive();
    }

    void OnUpdate(Timestep) override
    {
        if (Input::IsKeyJustPressed(Key::R))
        {
            if (IsAudioPlaying())
                StopAudio(); // the source's FadeOut: it fades out by itself
            else
                PlayAudio(); // the source's FadeIn
            m_Ducked = false;
        }
        if (Input::IsKeyJustPressed(Key::T))
        {
            m_Filtered = !m_Filtered;
            SetAudioHighPass(m_Filtered ? HighPass : 0.0f);
            SetAudioLowPass(m_Filtered ? LowPass : 0.0f);
        }
        if (Input::IsKeyJustPressed(Key::G) && IsAudioPlaying())
        {
            m_Ducked = !m_Ducked;
            FadeAudio(m_Ducked ? DuckVolume : 1.0f, 0.6f);
        }

        const auto* audio = GetEntity().TryGetComponent<AudioSourceComponent>();
        std::string state = IsAudioPlaying() ? std::format("playing, fade {:.0f}%", GetAudioFade() * 100.0f) : std::string("off");
        std::string filter = !audio || !audio->Filter.IsActive() ? std::string("none")
                           : std::format("high-pass {:.0f} Hz + low-pass {:.1f} kHz", audio->Filter.GetHighPass(), audio->Filter.GetLowPass() / 1000.0f);
        // High enough above the radio that the drone's label flying past below stays readable, and a
        // little to the right, clear of the wind chimes' labels.
        const glm::vec3 label = GetWorldPosition() + glm::vec3(0.9f, 0.0f, 0.0f);
        DebugDraw::Text(label + glm::vec3(0.0f, 1.95f, 0.0f), std::format("Radio: {}   (R - on / off)", state),
                        glm::vec4(1.0f, 0.85f, 0.5f, 1.0f));
        DebugDraw::Text(label + glm::vec3(0.0f, 1.6f, 0.0f), std::format("Filter: {}   (T - filter, G - lower)", filter),
                        glm::vec4(0.6f, 0.85f, 1.0f, 1.0f));
    }

private:
    bool m_Filtered = false;
    bool m_Ducked = false;
};

ZM_REGISTER_SCRIPT(RadioToggle,
    ZM_FIELD_RANGE(RadioToggle, HighPass, 20.0f, 8000.0f).Tooltip("High-pass turned on with T, Hz"),
    ZM_FIELD_RANGE(RadioToggle, LowPass, 200.0f, 20000.0f).Tooltip("Low-pass turned on with T, Hz"),
    ZM_FIELD_RANGE(RadioToggle, DuckVolume, 0.0f, 1.0f).Tooltip("Fade level while G is pressed"))

// A pool: when the listener (Audio Listener) enters the pool, the SFX group (with its child Ambience) and the Music
// group are muffled with a low-pass - as if under water; on leaving the filter opens smoothly (Audio::SetGroupLowPass).
class UnderwaterZone : public ScriptableEntity
{
public:
    float Cutoff = 450.0f;      // SFX (and Ambience)
    float MusicCutoff = 800.0f; // Music

    void OnUpdate(Timestep) override
    {
        Scene* scene = GetEntity().GetScene();
        Entity listener = scene->GetAudioListenerEntity();
        const glm::vec3 center = GetWorldPosition(), half = Transform().Scale * 0.5f;
        bool inside = false;
        if (listener)
        {
            glm::vec3 p = scene->GetWorldPosition(listener);
            inside = std::abs(p.x - center.x) <= half.x && std::abs(p.z - center.z) <= half.z && std::abs(p.y - center.y) <= half.y + 0.5f;
        }
        if (inside != m_Inside)
        {
            m_Inside = inside;
            Apply(inside);
        }
        AudioFilterSettings sfx = Audio::GetGroupFilter("SFX");
        std::string text = m_Inside ? std::format("Under water: SFX low-pass {:.0f} Hz, Music {:.0f} Hz", sfx.GetLowPass(), MusicCutoff)
                                    : std::string("Pool: sounds are muffled when the listener goes in");
        DebugDraw::Text(center + glm::vec3(0.0f, half.y + 0.35f, 0.0f), text,
                        m_Inside ? glm::vec4(0.45f, 0.8f, 1.0f, 1.0f) : glm::vec4(0.75f, 0.85f, 0.95f, 1.0f));
    }

    // The mixer is the game's: a scene that ends with the listener in the water leaves no filter behind.
    void OnDestroy() override
    {
        if (m_Inside)
            Apply(false);
    }

private:
    void Apply(bool on)
    {
        Audio::SetGroupLowPass("SFX", on ? Cutoff : 0.0f);
        Audio::SetGroupLowPass("Music", on ? MusicCutoff : 0.0f);
    }

    bool m_Inside = false;
};

ZM_REGISTER_SCRIPT(UnderwaterZone,
    ZM_FIELD_RANGE(UnderwaterZone, Cutoff, 50.0f, 20000.0f).Tooltip("Low-pass frequency of the SFX group under water, Hz"),
    ZM_FIELD_RANGE(UnderwaterZone, MusicCutoff, 50.0f, 20000.0f).Tooltip("Low-pass frequency of the Music group under water, Hz"))

// Loudspeaker announcements: when an Audio Source (a non-looping chime, with a direction cone) finishes,
// OnSoundFinished arrives - the script plays it again after Pause seconds and shows how many announcements
// there have been. A message also arrives when the sound limit takes its place (sound.Stolen).
class PaAnnouncer : public ScriptableEntity
{
public:
    float Pause = 2.0f;

    void OnSoundFinished(const SoundFinished& sound) override
    {
        if (sound.Stolen)
        {
            m_Status = "stopped by the limit";
            Invoke(Pause, [this]() { PlayAudio(); });
            return;
        }
        m_Count++;
        m_Status = std::format("announcement #{} finished", m_Count);
        m_NextAt = m_Clock + Pause;
        Invoke(Pause, [this]() { PlayAudio(); });
    }

    void OnUpdate(Timestep ts) override
    {
        m_Clock += ts;
        std::string state = IsAudioPlaying() ? std::format("announcement #{} ({:.1f} s)", m_Count + 1, GetAudioTime())
                          : m_Count > 0       ? std::format("{}, next in {:.1f} s", m_Status, std::max(0.0f, m_NextAt - m_Clock))
                                              : m_Status;
        DebugDraw::Text(GetWorldPosition() + glm::vec3(0.0f, 0.75f, 0.0f), std::format("Loudspeaker: {}   (OnSoundFinished)", state),
                        glm::vec4(1.0f, 0.78f, 0.45f, 1.0f));
        const auto* audio = GetEntity().TryGetComponent<AudioSourceComponent>();
        SoundInfo info;
        if (audio && GetEntity().GetScene()->GetAudioInfo(GetEntity(), info) && info.Cone.IsActive())
            DebugDraw::Text(GetWorldPosition() + glm::vec3(0.0f, 0.45f, 0.0f),
                            std::format("Direction cone: x{:.2f} ({})", info.ConeGain,
                                        info.ConeGain >= 0.999f ? "in front" : info.ConeGain <= audio->Cone.OuterGain + 1e-3f ? "behind" : "beside"),
                            glm::vec4(1.0f, 0.65f, 0.35f, 1.0f));
    }

private:
    int m_Count = 0;
    float m_Clock = 0.0f, m_NextAt = 0.0f;
    std::string m_Status = "waiting";
};
ZM_REGISTER_SCRIPT(PaAnnouncer, ZM_FIELD_RANGE(PaAnnouncer, Pause, 0.0f, 30.0f).Tooltip("Break between announcements, seconds"))

// An ambient zone label: above the zone (AmbientZoneComponent), its name and the level heard right now - full
// while the listener is inside the volume, fading slowly over the blend distance outside its edge.
class AmbientZoneLabel : public ScriptableEntity
{
public:
    std::string Title = "Forest (Ambient Zone)";
    float LabelHeight = 1.3f;

    void OnUpdate(Timestep) override
    {
        const auto* zone = GetEntity().TryGetComponent<AmbientZoneComponent>();
        if (!zone)
            return;
        std::string state = zone->Target >= 1.0f ? std::string("inside")
                          : zone->Target > 0.0f  ? std::format("{:.1f} m from the edge", zone->Distance)
                                                 : std::string("far away");
        float top = zone->Shape == AmbientZoneComponent::ZoneShape::Box ? zone->Size.y * 0.5f : zone->Radius;
        DebugDraw::Text(GetWorldPosition() + glm::vec3(0.0f, top + LabelHeight, 0.0f),
                        std::format("{}: {:.0f}%  ({})", Title, GetEntity().GetScene()->GetAmbientZoneLevel(GetEntity()) * 100.0f, state),
                        glm::mix(glm::vec4(0.6f, 0.75f, 0.6f, 1.0f), glm::vec4(0.55f, 1.0f, 0.6f, 1.0f), zone->Level));
    }
};
ZM_REGISTER_SCRIPT(AmbientZoneLabel, ZM_FIELD(AmbientZoneLabel, Title).Tooltip("The label above the zone"),
    ZM_FIELD(AmbientZoneLabel, LabelHeight).Tooltip("How high the label is above the zone, m (so it does not overlap other labels)"))

// The player's sound settings: - / = lower / raise the Music group by 3 dB, N - mute the SFX,
// 9 - save (Audio::SaveSettings: to SaveData, separate from the project default), 0 - delete the saved ones.
// The saved settings are loaded automatically when the game starts (in the player and with Play in the editor).
class AudioOptions : public ScriptableEntity
{
public:
    void OnUpdate(Timestep) override
    {
        auto step = [](float decibels) {
            float db = std::clamp(AudioGain::ToDecibels(Audio::GetGroupVolume("Music")) + decibels, AudioGain::MinDecibels, 6.0f);
            Audio::SetGroupVolume("Music", db <= AudioGain::MinDecibels ? 0.0f : AudioGain::FromDecibels(db));
        };
        if (Input::IsKeyJustPressed(Key::Minus))
            step(-3.0f);
        if (Input::IsKeyJustPressed(Key::Equal))
            step(3.0f);
        if (Input::IsKeyJustPressed(Key::N))
            Audio::SetGroupMuted("SFX", !Audio::IsGroupMuted("SFX"));
        if (Input::IsKeyJustPressed(Key::D9))
            m_Status = Audio::SaveSettings() ? "saved" : "not saved";
        if (Input::IsKeyJustPressed(Key::D0))
        {
            Audio::ClearSavedSettings();
            m_Status = "cleared (project default)";
        }
        float music = Audio::GetGroupVolume("Music");
        std::string musicText = music <= 0.0f ? std::string("silent") : std::format("{:+.1f} dB", AudioGain::ToDecibels(music));
        std::string saved = !m_Status.empty() ? m_Status : Audio::HasSavedSettings() ? std::string("yes") : std::string("none");
        DebugDraw::Text(GetWorldPosition(),
                        std::format("Settings: Music {}{}   |   - / = volume, N - SFX mute, 9 - save, 0 - clear   |   saved: {}",
                                    musicText, Audio::IsGroupMuted("SFX") ? ", SFX: mute" : "", saved),
                        glm::vec4(0.8f, 1.0f, 0.75f, 1.0f));
    }

private:
    std::string m_Status;
};
ZM_REGISTER_SCRIPT(AudioOptions)

// The animation scene (M13): shows the events the entity's Animation Player clip emits.
// OnAnimationEvent gets every event; AddAnimationEventHandler binds a method to one name.
class AnimationEventDemo : public ScriptableEntity
{
public:
    std::string Title = "Animation";
    float Height = 3.4f; // the labels above the entity

    void OnCreate() override
    {
        AddAnimationEventHandler("Pulse", [this](const AnimationEventInfo&) { m_Pulses++; });
    }
    void OnAnimationEvent(const AnimationEventInfo& e) override
    {
        m_Last = e.Name;
        m_Count++;
        m_Flash = 1.0f;
    }
    void OnUpdate(Timestep ts) override
    {
        m_Flash = std::max(0.0f, m_Flash - ts * 1.5f);
        const glm::vec3 p = GetWorldPosition();
        DebugDraw::Text(p + glm::vec3(0.0f, Height + 0.35f, 0.0f), Title, glm::vec4(0.85f, 0.88f, 0.95f, 1.0f));
        if (m_Last.empty())
            return;
        const glm::vec4 color = glm::mix(glm::vec4(0.7f, 0.75f, 0.82f, 1.0f), glm::vec4(1.0f, 0.82f, 0.3f, 1.0f), m_Flash);
        std::string text = std::format("event: {}  ({} total)", m_Last, m_Count);
        if (m_Pulses > 0)
            text += std::format(", Pulse: {}", m_Pulses);
        DebugDraw::Text(p + glm::vec3(0.0f, Height, 0.0f), text, color);
    }

private:
    std::string m_Last;
    int m_Count = 0;
    int m_Pulses = 0;
    float m_Flash = 0.0f;
};
ZM_REGISTER_SCRIPT(AnimationEventDemo, ZM_FIELD(AnimationEventDemo, Title), ZM_FIELD(AnimationEventDemo, Height))

// The Animator demo (Scenes/Animator.zscene): changes a float parameter with a slow wave
// (the blend tree's mix or a layer's weight), sets a trigger every TriggerEvery seconds and
// keeps writing the animator's live state above the entity.
class AnimatorDemo : public ScriptableEntity
{
public:
    std::string Title = "Animator";
    std::string Parameter = "Blend";   // the float parameter it drives
    float Min = 0.0f;
    float Max = 2.0f;
    float Period = 9.0f;               // seconds for a sweep there and back
    std::string Trigger;               // fired every TriggerEvery seconds ("" = none)
    float TriggerEvery = 7.0f;
    float Height = 3.6f;               // the labels above the entity

    void OnCreate() override { m_Next = TriggerEvery; }

    void OnUpdate(Timestep ts) override
    {
        m_Time += ts;
        auto* anim = TryGetComponent<AnimatorComponent>();
        if (!anim)
            return;
        const float k = 0.5f - 0.5f * std::cos(m_Time * glm::two_pi<float>() / std::max(0.1f, Period));
        const float value = Min + (Max - Min) * k;
        anim->SetFloat(Parameter, value);
        if (!Trigger.empty() && TriggerEvery > 0.0f && (m_Next -= ts) <= 0.0f)
        {
            anim->SetTrigger(Trigger);
            m_Next = TriggerEvery;
        }

        const glm::vec3 p = GetWorldPosition();
        DebugDraw::Text(p + glm::vec3(0.0f, Height + 0.35f, 0.0f), Title, glm::vec4(0.85f, 0.88f, 0.95f, 1.0f));
        std::string line = std::format("{}: {:.2f}", Parameter, value);
        if (anim->UsesStateMachine())
            line += std::format("   state: {}", anim->GetCurrentStateName());
        if (anim->IsFading())
            line += std::format("  (transition {:.0f}%)", anim->GetFadeProgress() * 100.0f);
        DebugDraw::Text(p + glm::vec3(0.0f, Height, 0.0f), line, glm::vec4(1.0f, 0.82f, 0.3f, 1.0f));
        // A blend tree's weights / the layers' weights.
        std::string weights;
        if (const AnimatorState* tree = anim->GetBlendTreeState(); tree && anim->BlendWeights.size() == tree->Motions.size())
            for (size_t i = 0; i < tree->Motions.size(); i++)
                weights += std::format("{}{} {:.2f}", i ? "   " : "", tree->Motions[i].Clip, anim->BlendWeights[i]);
        for (const AnimatorLayer& l : anim->Layers)
            weights += std::format("{}layer {}: {:.2f}", weights.empty() ? "" : "   ", l.Name, l.EffectiveWeight);
        if (!weights.empty())
            DebugDraw::Text(p + glm::vec3(0.0f, Height - 0.35f, 0.0f), weights, glm::vec4(0.6f, 0.78f, 1.0f, 1.0f));
    }

private:
    float m_Time = 0.0f;
    float m_Next = 0.0f;
};
ZM_REGISTER_SCRIPT(AnimatorDemo, ZM_FIELD(AnimatorDemo, Title), ZM_FIELD(AnimatorDemo, Parameter), ZM_FIELD(AnimatorDemo, Min),
                   ZM_FIELD(AnimatorDemo, Max), ZM_FIELD(AnimatorDemo, Period), ZM_FIELD(AnimatorDemo, Trigger),
                   ZM_FIELD(AnimatorDemo, TriggerEvery), ZM_FIELD(AnimatorDemo, Height))

// Blend shapes from a script (Scenes/Animator.zscene): the Mesh Renderer's morph target weights
// (SetMorphWeight) over time - a smile, a squash, spikes; the values are written above the entity.
class BlendShapeDemo : public ScriptableEntity
{
public:
    float Speed = 1.0f;
    float Height = 1.6f;   // the label above the entity

    void OnUpdate(Timestep ts) override
    {
        m_Time += ts * Speed;
        auto* mr = TryGetComponent<MeshRendererComponent>();
        if (!mr || mr->GetMorphTargetCount() == 0)
            return;
        mr->SetMorphWeight("Smile", 0.5f + 0.5f * std::sin(m_Time * 1.7f));
        mr->SetMorphWeight("Squash", 0.6f * std::max(0.0f, std::sin(m_Time * 2.6f)));
        mr->SetMorphWeight("Spikes", std::pow(std::max(0.0f, std::sin(m_Time * 0.8f)), 3.0f));

        std::string line;
        for (uint32_t i = 0; i < mr->GetMorphTargetCount(); i++)
            line += std::format("{}{} {:.2f}", i ? "   " : "", mr->ModelAsset->GetMorphTargetName(i), mr->GetMorphWeight(i));
        const glm::vec3 p = GetWorldPosition();
        DebugDraw::Text(p + glm::vec3(0.0f, Height + 0.35f, 0.0f), "Blend shapes: script (SetMorphWeight)", glm::vec4(0.85f, 0.88f, 0.95f, 1.0f));
        DebugDraw::Text(p + glm::vec3(0.0f, Height, 0.0f), line, glm::vec4(1.0f, 0.6f, 0.8f, 1.0f));
    }

private:
    float m_Time = 0.0f;
};
ZM_REGISTER_SCRIPT(BlendShapeDemo, ZM_FIELD(BlendShapeDemo, Speed), ZM_FIELD(BlendShapeDemo, Height))

// The camera stands in place and turns slowly left and right (Scenes/Crowd.zscene): as the view changes,
// the animation of the characters that leave the screen is culled and the ones that come in are posed again.
class SweepCamera : public ScriptableEntity
{
public:
    float Amplitude = 0.7f; // radians to each side
    float Period = 16.0f;   // seconds for a sweep there and back
    void OnCreate() override { m_Base = Transform().Rotation.y; }
    void OnUpdate(Timestep ts) override
    {
        m_Time += ts;
        Transform().Rotation.y = m_Base + Amplitude * std::sin(m_Time * glm::two_pi<float>() / std::max(0.1f, Period));
    }
private:
    float m_Base = 0.0f;
    float m_Time = 0.0f;
};
ZM_REGISTER_SCRIPT(SweepCamera, ZM_FIELD(SweepCamera, Amplitude), ZM_FIELD(SweepCamera, Period))

// Keeps writing the scene's animation work above the entity (Scene::GetAnimationStats): how many animators
// were posed, how many were skipped because nothing changed, how many were culled off screen.
class AnimationStatsDemo : public ScriptableEntity
{
public:
    float Height = 4.4f; // the labels above the entity
    void OnUpdate(Timestep) override
    {
        const Scene::AnimationStats& a = GetScene().GetAnimationStats();
        const glm::vec3 p = GetWorldPosition();
        DebugDraw::Text(p + glm::vec3(0.0f, Height + 0.35f, 0.0f), "Animation caching and culling", glm::vec4(0.85f, 0.88f, 0.95f, 1.0f));
        DebugDraw::Text(p + glm::vec3(0.0f, Height, 0.0f),
                        std::format("{} animators: {} posed, {} unchanged, {} culled  ({:.2f} ms)", a.Animators, a.Posed,
                                    a.Unchanged, a.Culled, a.Milliseconds),
                        glm::vec4(1.0f, 0.82f, 0.3f, 1.0f));
        DebugDraw::Text(p + glm::vec3(0.0f, Height - 0.35f, 0.0f),
                        std::format("{} samples from cache / {} from clips, cache {:.0f} KB", a.CachedSamples, a.ClipSamples,
                                    (double)a.CacheBytes / 1024.0),
                        glm::vec4(0.6f, 0.78f, 1.0f, 1.0f));
    }
};
ZM_REGISTER_SCRIPT(AnimationStatsDemo, ZM_FIELD(AnimationStatsDemo, Height))
