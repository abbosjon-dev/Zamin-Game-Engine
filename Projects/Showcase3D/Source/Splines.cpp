// Scripts of the "Splines" scene: AnimationCurve, noise (Noise), color (Color) and spline paths.
#include <Zamin/Zamin.h>

#include <cmath>

using namespace Zamin;

// Its height comes from an AnimationCurve (edited in the inspector), and it sways slowly sideways with
// Perlin noise.
class CurveBob : public ScriptableEntity
{
public:
    AnimationCurve Height = []() {
        AnimationCurve c = AnimationCurve::EaseInOut(0.0f, 0.0f, 1.2f, 1.4f);
        c.PreWrap = c.PostWrap = AnimationWrap::PingPong;
        return c;
    }();
    float Phase = 0.0f;
    float Wobble = 0.35f;      // the offset from the noise (units)
    float WobbleSpeed = 0.5f;  // shovqin tezligi

    void OnCreate() override { m_Base = Transform().Translation; }
    void OnUpdate(Timestep) override
    {
        const float t = GetTime() + Phase;
        Noise::FractalSettings drift;
        drift.Octaves = 3;
        drift.Seed = (uint32_t)(Phase * 1000.0f);
        glm::vec3 p = m_Base;
        p.y += Height.Evaluate(t);
        p.x += Noise::Fractal(t * WobbleSpeed, drift) * Wobble;
        p.z += Noise::Fractal(t * WobbleSpeed + 37.0f, drift) * Wobble;
        Transform().Translation = p;
    }

private:
    glm::vec3 m_Base{ 0.0f };
};
ZM_REGISTER_SCRIPT(CurveBob,
                   ZM_FIELD_RANGE(CurveBob, Height, 0.0f, 1.5f).Label("Height curve")
                       .Tooltip("Height over time (s): drag the keys, right click - ready-made shapes"),
                   ZM_FIELD(CurveBob, Phase).Label("Faza (s)"),
                   ZM_FIELD_RANGE(CurveBob, Wobble, 0.0f, 2.0f).Label("Chayqalish").Header("Noise"),
                   ZM_FIELD_RANGE(CurveBob, WobbleSpeed, 0.0f, 3.0f).Label("Sway speed"))

// Cycles the mesh color around the color wheel (Color::FromHSV).
class RainbowTint : public ScriptableEntity
{
public:
    float Speed = 0.08f;        // turns / second
    float Phase = 0.0f;         // 0..1
    float Saturation = 0.75f;
    float Brightness = 1.0f;

    void OnUpdate(Timestep) override
    {
        if (!HasComponent<MeshRendererComponent>())
            return;
        const float hue = GetTime() * Speed + Phase;
        GetComponent<MeshRendererComponent>().Mat.Albedo = Color::FromHSV(hue - std::floor(hue), Saturation, Brightness);
    }
};
ZM_REGISTER_SCRIPT(RainbowTint, ZM_FIELD(RainbowTint, Speed).Label("Speed"), ZM_FIELD_RANGE(RainbowTint, Phase, 0.0f, 1.0f).Label("Faza"),
                   ZM_FIELD_RANGE(RainbowTint, Saturation, 0.0f, 1.0f).Label("Saturation"),
                   ZM_FIELD_RANGE(RainbowTint, Brightness, 0.0f, 1.0f).Label("Brightness"))

// The height of its children (the columns) comes from noise: fBm or "ridged" (mountain ridges), drifting slowly;
// the color blends along HSV from bottom to top.
class NoiseColumns : public ScriptableEntity
{
public:
    Noise::NoiseType Type = Noise::NoiseType::Perlin;
    bool Ridged = false;
    float Frequency = 0.22f;
    int Octaves = 4;
    float HeightScale = 2.6f;
    float Scroll = 0.35f;       // units / second
    int Seed = 7;
    float Contrast = 1.0f;      // spreads the values around 0.5 (1 - unchanged)
    glm::vec4 LowColor{ 0.12f, 0.35f, 0.85f, 1.0f };
    glm::vec4 HighColor{ 1.0f, 0.85f, 0.35f, 1.0f };

    void OnUpdate(Timestep) override
    {
        Noise::FractalSettings settings;
        settings.Type = Type;
        settings.Octaves = Octaves;
        settings.Frequency = Frequency;
        settings.Seed = (uint32_t)Seed;
        const glm::vec2 offset(GetTime() * Scroll, GetTime() * Scroll * 0.6f);
        for (Entity child : GetChildren())
        {
            auto& t = child.GetComponent<TransformComponent>();
            const glm::vec2 at = glm::vec2(t.Translation.x, t.Translation.z) + offset;
            const float raw = Ridged ? Noise::Ridged(at, settings) : Noise::To01(Noise::Fractal(at, settings));
            const float n = std::clamp((raw - 0.5f) * Contrast + 0.5f, 0.0f, 1.0f);
            const float h = 0.15f + n * HeightScale;
            t.Scale.y = h;
            t.Translation.y = h * 0.5f;
            if (child.HasComponent<MeshRendererComponent>())
                child.GetComponent<MeshRendererComponent>().Mat.Albedo = Color::LerpHSV(LowColor, HighColor, n);
        }
    }
};
ZM_REGISTER_SCRIPT(NoiseColumns, ZM_FIELD_ENUM(NoiseColumns, Type, "Perlin", "Simplex", "Value").Label("Noise type"),
                   ZM_FIELD(NoiseColumns, Ridged).Label("Ridged"), ZM_FIELD_RANGE(NoiseColumns, Frequency, 0.01f, 2.0f).Label("Frequency"),
                   ZM_FIELD_RANGE(NoiseColumns, Octaves, 1, 8).Label("Octaves"), ZM_FIELD_RANGE(NoiseColumns, HeightScale, 0.0f, 6.0f).Label("Height"),
                   ZM_FIELD(NoiseColumns, Scroll).Label("Scroll speed"), ZM_FIELD(NoiseColumns, Seed).Label("Seed"),
                   ZM_FIELD_RANGE(NoiseColumns, Contrast, 0.5f, 4.0f).Label("Contrast"),
                   ZM_FIELD_COLOR(NoiseColumns, LowColor).Label("Low color"), ZM_FIELD_COLOR(NoiseColumns, HighColor).Label("High color"))

// Stays at the point of the path (Rail) nearest to Target (Scene::GetSplineClosestDistance) and draws a
// line to it: a rail for a camera or a guide.
class SplineMagnet : public ScriptableEntity
{
public:
    EntityRef Rail;
    EntityRef Target;
    float Lift = 0.6f;

    void OnUpdate(Timestep) override
    {
        Scene& scene = GetScene();
        Entity rail = Rail ? scene.GetEntityByUUID(Rail.ID) : Entity();
        Entity target = Target ? scene.GetEntityByUUID(Target.ID) : Entity();
        if (!rail || !target)
            return;
        const glm::vec3 goal = scene.GetWorldPosition(target);
        const float distance = scene.GetSplineClosestDistance(rail, goal);
        const glm::vec3 p = scene.GetSplinePosition(rail, distance) + glm::vec3(0.0f, Lift, 0.0f);
        Transform().Translation = p;
        DebugDraw::Line(p, goal, Color::FromHex("#FFB347").value_or(Colors::Orange));
    }
};
ZM_REGISTER_SCRIPT(SplineMagnet, ZM_FIELD(SplineMagnet, Rail).Label("Path"), ZM_FIELD(SplineMagnet, Target).Label("Target"),
                   ZM_FIELD(SplineMagnet, Lift).Label("Height"))
