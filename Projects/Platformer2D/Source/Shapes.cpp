#include <Zamin/Zamin.h>

#include <deque>

using namespace Zamin;

// Shapes2D sahnasi: ko'pburchak / chiziq kolliderlari, fizik materiallar, to'qnashuv qatlamlari va
// bir tomonlama platformalarni ko'rsatadigan kichik skriptlar.

// Yuqoridan tasodifiy shakllarni tashlaydi (shablonlar o'chiq "Shablonlar" guruhida turadi).
// "Fire" (sichqoncha / Ctrl) - bir yo'la bir nechtasi. Eng eskilari MaxShapes'dan oshganda yo'qoladi.
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
    ZM_FIELD(ShapeSpawner, Boulder).Header("Shablonlar").Tooltip("Tosh (Polygon Collider 2D)"),
    ZM_FIELD(ShapeSpawner, Crystal).Tooltip("Kristall (botiq Polygon Collider 2D)"),
    ZM_FIELD(ShapeSpawner, Crate).Tooltip("Quti (Box Collider 2D)"),
    ZM_FIELD(ShapeSpawner, Ball).Tooltip("Rezina to'p (Bouncy fizik materiali)"),
    ZM_FIELD(ShapeSpawner, Ghost).Tooltip("Arvoh shar: Ghost qatlamida - faqat Ground bilan to'qnashadi"),
    ZM_FIELD_RANGE(ShapeSpawner, Interval, 0.05f, 10.0f).Header("Tashlash").Tooltip("Necha soniyada bitta shakl"),
    ZM_FIELD_RANGE(ShapeSpawner, Width, 0.0f, 60.0f).Tooltip("Shakllar tushadigan kenglik"),
    ZM_FIELD_RANGE(ShapeSpawner, MaxShapes, 1, 300).Tooltip("Sahnada ko'pi bilan shuncha shakl"))

// Shablonni pastdan yuqoriga otadi: to'p bir tomonlama platformadan o'tib, ustiga tushadi.
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
    ZM_FIELD(ShapeLauncher, Template).Tooltip("Otiladigan shablon"),
    ZM_FIELD_RANGE(ShapeLauncher, Interval, 0.1f, 30.0f).Tooltip("Necha soniyada bitta"),
    ZM_FIELD(ShapeLauncher, Velocity).Tooltip("Boshlang'ich tezlik (birlik/s)"),
    ZM_FIELD_RANGE(ShapeLauncher, MaxShapes, 1, 100).Tooltip("Ko'pi bilan shuncha (eskilari yo'qoladi)"))
