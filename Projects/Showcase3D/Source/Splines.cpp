// "Splines" sahnasi skriptlari: AnimationCurve, shovqin (Noise), rang (Color) va spline yo'llar.
#include <Zamin/Zamin.h>

#include <cmath>

using namespace Zamin;

// Balandligi AnimationCurve egri chizig'idan (inspektorda tahrirlanadi), yon tomonga esa Perlin
// shovqini bilan sekin chayqaladi.
class CurveBob : public ScriptableEntity
{
public:
    AnimationCurve Height = []() {
        AnimationCurve c = AnimationCurve::EaseInOut(0.0f, 0.0f, 1.2f, 1.4f);
        c.PreWrap = c.PostWrap = AnimationWrap::PingPong;
        return c;
    }();
    float Phase = 0.0f;
    float Wobble = 0.35f;      // shovqin bilan siljish (birlik)
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
                   ZM_FIELD_RANGE(CurveBob, Height, 0.0f, 1.5f).Label("Balandlik egri chizig'i")
                       .Tooltip("Vaqt (s) bo'yicha balandlik: kalitlarni sudrang, o'ng tugma - tayyor shakllar"),
                   ZM_FIELD(CurveBob, Phase).Label("Faza (s)"),
                   ZM_FIELD_RANGE(CurveBob, Wobble, 0.0f, 2.0f).Label("Chayqalish").Header("Shovqin"),
                   ZM_FIELD_RANGE(CurveBob, WobbleSpeed, 0.0f, 3.0f).Label("Chayqalish tezligi"))

// Mesh rangini rang g'ildiragi bo'ylab aylantiradi (Color::FromHSV).
class RainbowTint : public ScriptableEntity
{
public:
    float Speed = 0.08f;        // aylanish / soniya
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
ZM_REGISTER_SCRIPT(RainbowTint, ZM_FIELD(RainbowTint, Speed).Label("Tezlik"), ZM_FIELD_RANGE(RainbowTint, Phase, 0.0f, 1.0f).Label("Faza"),
                   ZM_FIELD_RANGE(RainbowTint, Saturation, 0.0f, 1.0f).Label("To'yinganlik"),
                   ZM_FIELD_RANGE(RainbowTint, Brightness, 0.0f, 1.0f).Label("Yorqinlik"))

// Bolalari (ustunlar) balandligi shovqindan: fBm yoki "ridged" (tog' qirralari), sekin siljiydi;
// rang - pastdan yuqoriga HSV bo'ylab aralashadi.
class NoiseColumns : public ScriptableEntity
{
public:
    Noise::NoiseType Type = Noise::NoiseType::Perlin;
    bool Ridged = false;
    float Frequency = 0.22f;
    int Octaves = 4;
    float HeightScale = 2.6f;
    float Scroll = 0.35f;       // birlik / soniya
    int Seed = 7;
    float Contrast = 1.0f;      // 0.5 atrofida qiymatlarni yoyadi (1 - o'zgarishsiz)
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
ZM_REGISTER_SCRIPT(NoiseColumns, ZM_FIELD_ENUM(NoiseColumns, Type, "Perlin", "Simplex", "Value").Label("Shovqin turi"),
                   ZM_FIELD(NoiseColumns, Ridged).Label("Ridged (qirralar)"), ZM_FIELD_RANGE(NoiseColumns, Frequency, 0.01f, 2.0f).Label("Chastota"),
                   ZM_FIELD_RANGE(NoiseColumns, Octaves, 1, 8).Label("Oktavalar"), ZM_FIELD_RANGE(NoiseColumns, HeightScale, 0.0f, 6.0f).Label("Balandlik"),
                   ZM_FIELD(NoiseColumns, Scroll).Label("Siljish tezligi"), ZM_FIELD(NoiseColumns, Seed).Label("Urug' (seed)"),
                   ZM_FIELD_RANGE(NoiseColumns, Contrast, 0.5f, 4.0f).Label("Kontrast"),
                   ZM_FIELD_COLOR(NoiseColumns, LowColor).Label("Past rang"), ZM_FIELD_COLOR(NoiseColumns, HighColor).Label("Baland rang"))

// Yo'l (Rail) ustida Target'ga eng yaqin nuqtada turadi (Scene::GetSplineClosestDistance) va unga
// chiziq tortadi: rels kamera yoki yo'lboshlovchi uchun.
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
ZM_REGISTER_SCRIPT(SplineMagnet, ZM_FIELD(SplineMagnet, Rail).Label("Yo'l"), ZM_FIELD(SplineMagnet, Target).Label("Nishon"),
                   ZM_FIELD(SplineMagnet, Lift).Label("Balandlik"))
