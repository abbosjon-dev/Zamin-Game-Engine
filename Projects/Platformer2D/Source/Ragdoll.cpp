#include <Zamin/Zamin.h>

#include <cmath>
#include <deque>

using namespace Zamin;

// Ragdoll2D sahnasi: Ragdoll prefabini (11 bo'lak, chegarali 10 ta Revolute Joint 2D) tashlaydi.
// "Fire" (sichqoncha / Ctrl) - sichqoncha turgan joyga bittasi, R - osmondan uchtasi, AutoInterval > 0
// bo'lsa o'zi ham har shuncha soniyada tashlaydi. Har ragdoll'ning ko'ylagi va shimi tasodifiy rangda.
// Eng eskilari MaxRagdolls'dan oshganda yo'qoladi.
class RagdollSpawner : public ScriptableEntity
{
public:
    std::string Prefab = "Prefabs/Ragdoll.zprefab";
    float AutoInterval = 2.0f;
    float Width = 24.0f;
    int MaxRagdolls = 10;
    float Speed = 4.0f;

    void OnUpdate(Timestep ts) override
    {
        if (Input::IsActionJustPressed("Fire"))
            Spawn(GetMouseWorldPosition());
        if (Input::IsKeyJustPressed(Key::R))
            for (int i = 0; i < 3; i++)
                Spawn(SkyPoint());
        if (AutoInterval <= 0.0f)
            return;
        m_Time += ts;
        if (m_Time < AutoInterval)
            return;
        m_Time = 0.0f;
        Spawn(SkyPoint());
    }

private:
    glm::vec2 SkyPoint()
    {
        const glm::vec3 at = Transform().Translation;
        return { at.x + Random::Range(-Width * 0.5f, Width * 0.5f), at.y + Random::Range(-0.5f, 0.5f) };
    }

    void Spawn(const glm::vec2& position)
    {
        Entity root = Instantiate(Prefab, { position.x, position.y, 0.0f });
        if (!root)
        {
            ZM_WARN("Ragdoll prefabi topilmadi: {}", Prefab);
            return;
        }
        Paint(root);
        // Hamma bo'lak bir xil tezlikda uchadi, boshi biroz boshqacha: ragdoll aylanib tushadi.
        const glm::vec2 velocity = Random::OnUnitCircle() * Speed * Random::Range(0.3f, 1.0f);
        for (Entity part : GetScene().GetChildren(root))
        {
            if (!part.HasComponent<Rigidbody2DComponent>())
                continue;
            glm::vec2 v = velocity;
            if (part.GetName() == "Head")
                v += Random::InsideUnitCircle() * Speed * 0.6f;
            SetVelocity(part, v);
        }
        m_Spawned.push_back(root.GetUUID());
        while ((int)m_Spawned.size() > std::max(1, MaxRagdolls))
        {
            if (Entity old = GetScene().GetEntityByUUID(m_Spawned.front()))
                Destroy(old);
            m_Spawned.pop_front();
        }
    }

    // Ko'ylak (ko'krak, yelkalar) va shim (bel, oyoqlar) uchun tasodifiy ranglar; orqadagi qo'l-oyoq to'qroq.
    void Paint(Entity root)
    {
        const float hue = Random::Float();
        auto fromHue = [](float h, float saturation, float value) {
            const glm::vec3 k = glm::fract(glm::vec3(h) + glm::vec3(0.0f, 2.0f / 3.0f, 1.0f / 3.0f)) * 6.0f - 3.0f;
            const glm::vec3 rgb = glm::clamp(glm::abs(k) - 1.0f, 0.0f, 1.0f);
            return glm::mix(glm::vec3(1.0f), rgb, saturation) * value;
        };
        const glm::vec3 shirt = fromHue(hue, 0.7f, 0.95f);
        const glm::vec3 pants = fromHue(hue + 0.5f, 0.45f, 0.45f);
        for (Entity part : GetScene().GetChildren(root))
        {
            auto* sprite = part.TryGetComponent<SpriteRendererComponent>();
            if (!sprite)
                continue;
            const std::string& name = part.GetName();
            const float shade = name.ends_with(" B") ? 0.72f : 1.0f;
            if (name == "Chest" || name.starts_with("Upper Arm"))
                sprite->Color = glm::vec4(shirt * shade, 1.0f);
            else if (name == "Hips" || name.starts_with("Thigh") || name.starts_with("Shin"))
                sprite->Color = glm::vec4(pants * (name.starts_with("Thigh") ? shade * 1.15f : shade), 1.0f);
        }
    }

    float m_Time = 0.0f;
    std::deque<UUID> m_Spawned;
};

ZM_REGISTER_SCRIPT(RagdollSpawner,
    ZM_FIELD(RagdollSpawner, Prefab).Tooltip("Ragdoll prefabi (Assets ichidagi yo'l)"),
    ZM_FIELD_RANGE(RagdollSpawner, AutoInterval, 0.0f, 30.0f).Header("Tashlash").Tooltip("Necha soniyada o'zi bittasini tashlaydi (0 - faqat sichqoncha / R)"),
    ZM_FIELD_RANGE(RagdollSpawner, Width, 0.0f, 60.0f).Tooltip("Osmondan tushadigan kenglik"),
    ZM_FIELD_RANGE(RagdollSpawner, MaxRagdolls, 1, 50).Tooltip("Ko'pi bilan shuncha (eskilari yo'qoladi)"),
    ZM_FIELD_RANGE(RagdollSpawner, Speed, 0.0f, 30.0f).Tooltip("Boshlang'ich tezlik (tasodifiy yo'nalishda)"))
