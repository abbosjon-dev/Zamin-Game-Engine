#include <Zamin/Zamin.h>

#include <cmath>
#include <deque>
#include <format>
#include <map>
#include <unordered_map>

using namespace Zamin;

// Obyektni o'z o'qi atrofida aylantiradi.
class Rotator : public ScriptableEntity
{
public:
    glm::vec3 Speed = { 0.0f, 1.0f, 0.0f }; // radian / sekund
    void OnUpdate(Timestep ts) override { Transform().Rotation += Speed * (float)ts; }
};
ZM_REGISTER_SCRIPT(Rotator, ZM_FIELD(Rotator, Speed))

// Yuqoriga-pastga suzish.
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

// Markaz atrofida aylanib uchadi (yorug'lik sharlari uchun).
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

// Kamera sahna atrofida sekin aylanadi; sichqoncha o'ng tugmasi bosilsa to'xtaydi.
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

// Slider Joint 3D motorini chegaralar orasida yuqoriga-pastga yuritadi (lift platformasi): har
// chetda Pause sekund kutadi, keyin orqaga qaytadi.
class SliderShuttle : public ScriptableEntity
{
public:
    float Speed = 1.0f;  // birlik / sekund
    float Pause = 0.6f;  // har chetda kutish (sekund)
    void OnUpdate(Timestep ts) override
    {
        auto* slider = TryGetComponent<SliderJoint3DComponent>();
        if (!slider || !slider->RuntimeJoint)
            return;
        slider->EnableMotor = true;
        if (m_Wait > 0.0f)
        {
            m_Wait -= (float)ts;
            slider->MotorSpeed = 0.0f; // joyida ushlab turadi
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

// WASD bilan yuradigan kub; Space bilan sakraydi.
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

// Space yoki sichqonchaning chap tugmasi: kamera yo'nalishida shar otadi (prefab + 3D fizika).
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

// Obyektni N soniyadan keyin o'chiradi (otilgan sharlar uchun).
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

// Kub minorasini yaratadi (3D fizika demo). Har bir g'isht umumiy Clay.zmat materialining o'z
// nusxasini (GetMaterial) qavatiga qarab bo'yaydi - umumiy asset va boshqa obyektlar o'zgarmaydi.
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

// DebugDraw namunasi (Scenes/DebugDraw.zscene): aylanib turuvchi raycast "radari", sharlar atrofida
// shakllar, harakat o'qlari, iz (davomiyligi bor nuqtalar) va yozuvlar. Editor'da "Debug chizish"
// menyusidan raycastlar va kontakt nuqtalarini ham yoqing.
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

// ---- Queries3D sahnasi: 3D fizika so'rovlari. "Raycastlar" debug chizish yoqiq bo'lsa dvigatel har
// so'rovni o'zi chizadi (shakl, yo'l, tegish nuqtalari, normallar); aks holda skriptlar natijani o'zlari
// ko'rsatadi.

// Obyekt ustida doim ko'rinadigan yozuv.
class Label3D : public ScriptableEntity
{
public:
    std::string Text = "Yozuv";
    glm::vec3 Offset{ 0.0f, 1.0f, 0.0f };
    glm::vec4 Color{ 1.0f, 1.0f, 1.0f, 1.0f };
    void OnUpdate(Timestep) override { DebugDraw::Text(GetWorldPosition() + Offset, Text, Color); }
};
ZM_REGISTER_SCRIPT(Label3D, ZM_FIELD(Label3D, Text), ZM_FIELD(Label3D, Offset), ZM_FIELD_COLOR(Label3D, Color))

// "Qalin nur": SphereCast3DAll yo'lidagi hamma obyektlarni tartib bilan topadi va u yoq-bu yoqqa buriladi.
// Arvoh qatlamidagi obyektlar niqobdan chiqarilgan (LayerMask).
class SphereSweep3D : public ScriptableEntity
{
public:
    float Radius = 0.35f;
    float Range = 11.0f;
    float Spread = 0.55f;    // radian: har tomonga qancha buriladi
    float SweepSpeed = 0.5f;
    bool SkipGhosts = true;  // "Arvoh" qatlamini hisobga olmaydi

    void OnUpdate(Timestep) override
    {
        const glm::vec3 origin = GetWorldPosition();
        const float a = std::sin(GetTime() * SweepSpeed) * Spread;
        const glm::vec3 dir(-std::cos(a), 0.0f, std::sin(a));
        const uint16_t mask = SkipGhosts ? (uint16_t)~LayerMask({ "Arvoh" }) : (uint16_t)0xFFFF;
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
        DebugDraw::Text(origin + glm::vec3(0.0f, 0.9f, 0.0f), std::format("SphereCast3DAll: {} ta", hits.size()), DebugColor::Cyan);
    }
};
ZM_REGISTER_SCRIPT(SphereSweep3D, ZM_FIELD(SphereSweep3D, Radius), ZM_FIELD(SphereSweep3D, Range), ZM_FIELD(SphereSweep3D, Spread),
                   ZM_FIELD(SphereSweep3D, SweepSpeed), ZM_FIELD(SphereSweep3D, SkipGhosts))

// Aylanib yuradigan OverlapSphere3D: ichiga tushgan obyektlar yorishadi (emissiya), chiqqanda asl holiga qaytadi.
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
        DebugDraw::Text(center + glm::vec3(0.0f, Radius + 0.3f, 0.0f), std::format("OverlapSphere3D: {} ta", found.size()), DebugColor::Cyan);
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

// Lazer: Raycast3DAll nur yo'lidagi hamma oynalarni yaqinidan boshlab topadi (har biri bir marta).
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
        DebugDraw::Text(origin + glm::vec3(0.0f, 0.7f, 0.0f), std::format("Raycast3DAll: {} ta", hits.size()), DebugColor::Cyan);
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
                        std::format("{}\nO'tib ketdi: {}   To'xtadi: {}", continuous ? "CCD yoqiq" : "CCD o'chiq", m_Through, m_Stopped),
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
        std::string text = std::format("{}\nIchida: {}", Title, m_Inside.size());
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

// ---- Ragdoll3D sahnasi: Cone Joint 3D (yelka, son, bel, bo'yin) va Hinge Joint 3D (tirsak, tizza) bilan
// bog'langan 11 bo'lakli ragdoll'lar zinadan yumalab tushadi.

// R - zina ustidan yana 3 ta ragdoll, sichqoncha chap tugmasi / F - kameradan sichqoncha tomonga otadi,
// har AutoInterval soniyada o'zi bittasini tashlaydi. Ko'pi bilan MaxRagdolls ta (eskilari yo'qoladi).
class RagdollSpawner3D : public ScriptableEntity
{
public:
    std::string Prefab = "Prefabs/Ragdoll3D.zprefab";
    float AutoInterval = 3.0f;
    float Width = 4.0f;       // tashlash joyining kengligi (Z bo'ylab)
    int MaxRagdolls = 8;
    float Push = 4.5f;        // zina tomonga (+X) boshlang'ich tezlik
    float ThrowSpeed = 14.0f; // kameradan otilganda

    void OnUpdate(Timestep ts) override
    {
        if (Input::IsMouseButtonJustPressed(MouseButton::Left) || Input::IsKeyJustPressed(Key::F))
            Throw();
        if (Input::IsKeyJustPressed(Key::R))
            for (int i = 0; i < 3; i++)
                Spawn(SkyPoint(), { Push * Random::Range(0.6f, 1.4f), 0.0f, 0.0f });
        // Sahnadan tushib ketganlari yo'qoladi.
        for (auto it = m_Spawned.begin(); it != m_Spawned.end();)
        {
            Entity root = GetScene().GetEntityByUUID(*it);
            Entity chest = root ? GetScene().FindChild(root, "Ko'krak") : Entity();
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

    // Kameradan sichqoncha ko'rsatgan joyga (z = 0 tekisligida) qarab.
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
        // Ildiz oyoq ostida: bo'laklar markazi ~0.8 yuqorida.
        Spawn(from + dir * 3.0f - glm::vec3(0.0f, 0.8f, 0.0f), dir * ThrowSpeed);
    }

    void Spawn(const glm::vec3& position, const glm::vec3& velocity)
    {
        Entity root = Instantiate(Prefab, position);
        if (!root)
        {
            ZM_WARN("Ragdoll prefabi topilmadi: {}", Prefab);
            return;
        }
        Paint(root);
        // Hamma bo'lak bir xil tezlikda, boshi biroz boshqacha: ragdoll aylanib uchadi.
        for (Entity part : GetScene().GetChildren(root))
        {
            if (!part.HasComponent<Rigidbody3DComponent>())
                continue;
            glm::vec3 v = velocity;
            if (part.GetName() == "Bosh")
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

    // Ko'ylak (ko'krak, yelkalar), shim (bel, son, boldir) va teri uchun tasodifiy ranglar.
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
            if (name == "Ko'krak" || name.starts_with("Yelka"))
                mesh->Mat.Albedo = glm::vec4(shirt, 1.0f);
            else if (name == "Bel" || name.starts_with("Son") || name.starts_with("Boldir"))
                mesh->Mat.Albedo = glm::vec4(name.starts_with("Boldir") ? pants * 0.8f : pants, 1.0f);
            else if (name == "Bosh" || name.starts_with("Bilak"))
                mesh->Mat.Albedo = glm::vec4(skin, 1.0f);
        }
    }

    float m_Time = 0.0f;
    std::deque<UUID> m_Spawned;
};
ZM_REGISTER_SCRIPT(RagdollSpawner3D,
    ZM_FIELD(RagdollSpawner3D, Prefab).Tooltip("Ragdoll prefabi (Assets ichidagi yo'l)"),
    ZM_FIELD_RANGE(RagdollSpawner3D, AutoInterval, 0.0f, 30.0f).Header("Tashlash").Tooltip("Necha soniyada o'zi bittasini tashlaydi (0 - faqat R / sichqoncha)"),
    ZM_FIELD_RANGE(RagdollSpawner3D, Width, 0.0f, 20.0f).Tooltip("Tashlash joyining kengligi (Z bo'ylab)"),
    ZM_FIELD_RANGE(RagdollSpawner3D, MaxRagdolls, 1, 40).Tooltip("Ko'pi bilan shuncha (eskilari yo'qoladi)"),
    ZM_FIELD_RANGE(RagdollSpawner3D, Push, 0.0f, 20.0f).Tooltip("Zina tomonga (+X) boshlang'ich tezlik"),
    ZM_FIELD_RANGE(RagdollSpawner3D, ThrowSpeed, 0.0f, 60.0f).Tooltip("Kameradan sichqoncha tomonga otish tezligi"))

// ---- PhysicsDebug3D sahnasi: fizika debug ko'rinishlari - kontakt nuqtalari (normal, chuqurlik, zarba
// halqasi, uxlayotganlari xira), qiymat yozuvlari (chuqurlik, zarba tezligi va impulsi) va joint'lar
// (langarlar, o'q, uzilishga yaqinligi rangda, uzilgani qizil X).

// O'yin boshlanganda debug chizishning fizika ko'rinishlarini yoqadi; C - kontaktlar, J - joint'lar,
// V - qiymat yozuvlari. Editor'da o'yin to'xtaganda viewport sozlamalari qaytadi.
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
                        std::format("{} C - kontaktlar   {} J - joint'lar   {} V - qiymatlar", mark(Contacts), mark(Joints), mark(Values)),
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
    ZM_FIELD(PhysicsDebugView3D, Contacts).Tooltip("Kontakt nuqtalari va normallari (C)"),
    ZM_FIELD(PhysicsDebugView3D, Joints).Tooltip("Joint'lar: langarlar, o'q, yuk rangi (J)"),
    ZM_FIELD(PhysicsDebugView3D, Values).Tooltip("Chuqurlik, zarba, impuls, kuch yozuvlari (V)"))

// ---- Audio sahnasi: mixer guruhlari, musiqa crossfade, Audio Listener.
// Tinglovchi (Audio Listener) qaysi zonada bo'lsa o'sha musiqa crossfade bilan yangraydi
// (Audio::CrossfadeTo, Music guruhi, diskdan oqim bilan). UI guruhida vaqti-vaqti bilan signal chalinadi.
// 1 / 2 - musiqani qo'lda tanlash, M - Music guruhini o'chirish (mute), P - pauza, S - musiqani so'ndirish.
class MusicDirector : public ScriptableEntity
{
public:
    std::string CalmTrack = "Audio/MusicCalm.wav";
    std::string TenseTrack = "Audio/MusicTense.wav";
    float Crossfade = 2.0f;
    float Border = 0.0f;       // zonalar chegarasi (X)
    std::string Ping = "Audio/UiPing.wav";
    float PingInterval = 2.5f; // UI guruhidagi signal (0: yo'q)

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
        std::string state = music.empty() ? std::string("yo'q") : music.substr(music.find_last_of('/') + 1);
        if (Audio::IsMusicPaused())
            state += " (pauza)";
        if (Audio::IsGroupMuted("Music"))
            state += " [Music: mute]";
        DebugDraw::Text(GetWorldPosition(), std::format("Musiqa: {}   |   1 / 2 - trek, M - mute, P - pauza, S - to'xtatish", state),
                        glm::vec4(0.9f, 0.95f, 1.0f, 1.0f));
    }

private:
    int m_Zone = 0;
    bool m_Manual = false;
    float m_PingTime = 0.0f;
};
ZM_REGISTER_SCRIPT(MusicDirector,
    ZM_FIELD(MusicDirector, CalmTrack).Tooltip("Tinch zona musiqasi (X < chegara)"),
    ZM_FIELD(MusicDirector, TenseTrack).Tooltip("Xavfli zona musiqasi (X > chegara)"),
    ZM_FIELD(MusicDirector, Crossfade).Tooltip("Treklar almashinish vaqti, sekund"),
    ZM_FIELD(MusicDirector, Border),
    ZM_FIELD(MusicDirector, Ping).Tooltip("UI guruhida chalinadigan signal"),
    ZM_FIELD(MusicDirector, PingInterval))
