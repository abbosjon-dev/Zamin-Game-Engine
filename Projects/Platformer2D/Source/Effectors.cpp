#include <Zamin/Zamin.h>

#include <cmath>
#include <format>

using namespace Zamin;

// Effectors2D sahnasi: Area / Buoyancy Effector 2D, OverlapCircle2D va Raycast2DAll so'rovlarini
// ko'rsatadigan skriptlar.

// Portlash: sichqoncha bosilgan joyda ("Fire") Radius ichidagi jismlarni OverlapCircle2D bilan topadi
// va markazdan uzoqqa itaradi (markazga yaqinroq - kuchliroq). AutoInterval > 0 bo'lsa AutoCenter
// atrofida o'zi ham portlaydi.
class BlastOnClick : public ScriptableEntity
{
public:
    float Radius = 3.0f;
    float Strength = 9.0f;
    float AutoInterval = 0.0f;
    glm::vec2 AutoCenter = { 0.0f, 0.0f };
    glm::vec2 AutoSpread = { 2.0f, 0.5f };

    void OnUpdate(Timestep ts) override
    {
        if (Input::IsActionJustPressed("Fire"))
            Blast(GetMouseWorldPosition());
        if (AutoInterval <= 0.0f)
            return;
        m_Time += ts;
        if (m_Time < AutoInterval)
            return;
        m_Time = 0.0f;
        Blast(AutoCenter + glm::vec2(Random::Range(-AutoSpread.x, AutoSpread.x), Random::Range(-AutoSpread.y, AutoSpread.y)));
    }

private:
    void Blast(const glm::vec2& center)
    {
        const std::vector<Entity> found = OverlapCircle2D(center, Radius);
        for (Entity e : found)
        {
            glm::vec2 d = glm::vec2(e.GetComponent<TransformComponent>().Translation) - center;
            const float distance = glm::length(d);
            d = distance > 1e-3f ? d / distance : glm::vec2(0.0f, 1.0f);
            const float falloff = std::clamp(1.0f - distance / std::max(Radius, 0.01f), 0.2f, 1.0f);
            ApplyImpulse(e, (d + glm::vec2(0.0f, 0.35f)) * Strength * falloff);
        }
        // What it reached: the circle for a moment.
        DebugDraw::Circle({ center, 0.5f }, Radius, glm::vec4(1.0f, 0.6f, 0.25f, 0.9f), 0.25f, false);
        DebugDraw::Text({ center + glm::vec2(0.0f, Radius + 0.3f), 0.5f }, std::format("{} ta jism", found.size()),
                        glm::vec4(1.0f, 0.8f, 0.4f, 1.0f), 0.6f);
    }

    float m_Time = 0.0f;
};

ZM_REGISTER_SCRIPT(BlastOnClick,
    ZM_FIELD_RANGE(BlastOnClick, Radius, 0.1f, 20.0f).Header("Portlash").Tooltip("OverlapCircle2D radiusi"),
    ZM_FIELD_RANGE(BlastOnClick, Strength, 0.0f, 200.0f).Tooltip("Markazdagi turtki (impuls)"),
    ZM_FIELD_RANGE(BlastOnClick, AutoInterval, 0.0f, 30.0f).Header("Avtomatik").Tooltip("Necha soniyada o'zi portlaydi (0 - faqat sichqoncha)"),
    ZM_FIELD(BlastOnClick, AutoCenter).Tooltip("Avtomatik portlash markazi (dunyoda)"),
    ZM_FIELD(BlastOnClick, AutoSpread).Tooltip("Markaz atrofida tasodifiy siljish"))

// Skaner nuri: Raycast2DAll bilan nur yo'lidagi hamma obyektlarni topadi, ularni tartib raqami bilan
// belgilaydi va Label matniga sonini yozadi. Nur MinAngle..MaxAngle oralig'ida tebranadi.
class LaserScanner : public ScriptableEntity
{
public:
    float Length = 24.0f;
    float MinAngle = 165.0f;
    float MaxAngle = 195.0f;
    float SweepSpeed = 0.35f; // tebranishlar soniyada
    bool IncludeTriggers = false;
    EntityRef Label;

    void OnUpdate(Timestep ts) override
    {
        m_Time += ts;
        const float mid = (MinAngle + MaxAngle) * 0.5f, half = (MaxAngle - MinAngle) * 0.5f;
        const float angle = glm::radians(mid + half * std::sin(m_Time * SweepSpeed * 6.2831853f));
        const glm::vec2 origin(GetWorldPosition());
        const glm::vec2 dir(std::cos(angle), std::sin(angle));
        const std::vector<RaycastHit> hits = Raycast2DAll(origin, dir, Length, 0xFFFF, IncludeTriggers);

        // The beam: bright up to the last hit, faint after it; every hit numbered in order.
        const float z = 0.6f;
        const glm::vec3 from(origin, z), to(origin + dir * Length, z);
        const glm::vec3 last = hits.empty() ? to : glm::vec3(glm::vec2(hits.back().Point), z);
        DebugDraw::Line(from, last, glm::vec4(1.0f, 0.25f, 0.2f, 0.95f), 0.0f, false);
        DebugDraw::Line(last, to, glm::vec4(1.0f, 0.3f, 0.25f, 0.25f), 0.0f, false);
        for (size_t i = 0; i < hits.size(); i++)
        {
            const glm::vec3 p(glm::vec2(hits[i].Point), z);
            DebugDraw::Point(p, glm::vec4(1.0f, 0.9f, 0.3f, 1.0f), 0.0f, false, 10.0f);
            DebugDraw::Text(p + glm::vec3(0.0f, 0.45f, 0.0f), std::to_string(i + 1), glm::vec4(1.0f, 0.92f, 0.45f, 1.0f), 0.0f, false);
        }
        if (Entity label = Resolve(Label); label && label.HasComponent<TextComponent>())
            label.GetComponent<TextComponent>().Text = std::format("Raycast2DAll: {} ta obyekt", hits.size());
    }

private:
    float m_Time = 0.0f;
};

ZM_REGISTER_SCRIPT(LaserScanner,
    ZM_FIELD_RANGE(LaserScanner, Length, 0.5f, 200.0f).Header("Nur").Tooltip("Nur uzunligi"),
    ZM_FIELD_RANGE(LaserScanner, MinAngle, -360.0f, 360.0f).Tooltip("Tebranish chegarasi (gradus, 0 - o'ngga)"),
    ZM_FIELD_RANGE(LaserScanner, MaxAngle, -360.0f, 360.0f),
    ZM_FIELD_RANGE(LaserScanner, SweepSpeed, 0.0f, 5.0f).Tooltip("Tebranishlar soniyada"),
    ZM_FIELD(LaserScanner, IncludeTriggers).Tooltip("Trigger'lar (suv, shamol) ham sanalsin"),
    ZM_FIELD(LaserScanner, Label).Header("Ko'rsatish").Tooltip("Natija yoziladigan Text obyekti"))
