// "Geometry" sahnasi skriptlari: Math/Geometry.h (AABB, Sphere, Ray, Plane) - fizikasiz, sof matematik
// nur va kesishuv testlari DebugDraw bilan ko'rsatiladi.
#include <Zamin/Zamin.h>

#include <cmath>
#include <format>
#include <optional>

using namespace Zamin;

namespace {

    // A "Nishon" target as a volume: a sphere mesh as a Sphere, anything else as its local bounds
    // moved by its world matrix (an oriented box).
    struct TargetShape
    {
        Entity Target;
        glm::mat4 World{ 1.0f };
        AABB Local;
        bool IsSphere = false;
        Sphere Ball;

        AABB GetWorldBounds() const { return IsSphere ? Ball.GetBounds() : Local.Transformed(World); }

        std::optional<RayHit> Raycast(const Ray& ray, float range) const
        {
            return IsSphere ? Geometry::Raycast(ray, Ball, range) : Geometry::Raycast(ray, Local, World, range);
        }

        // The nearest point of the (solid) volume.
        glm::vec3 ClosestPoint(const glm::vec3& point) const
        {
            if (IsSphere)
                return Ball.ClosestPoint(point);
            const glm::vec3 local = glm::vec3(glm::inverse(World) * glm::vec4(point, 1.0f));
            return glm::vec3(World * glm::vec4(Local.ClosestPoint(local), 1.0f));
        }
    };

    std::vector<TargetShape> GatherTargets(ScriptableEntity& script)
    {
        std::vector<TargetShape> shapes;
        Scene& scene = script.GetScene();
        for (Entity e : script.FindEntitiesWithTag("Nishon"))
        {
            TargetShape shape;
            shape.Target = e;
            shape.World = scene.GetWorldTransform(e);
            if (!scene.GetEntityLocalBounds(e, shape.Local))
                continue;
            const auto* mesh = e.TryGetComponent<MeshRendererComponent>();
            shape.IsSphere = mesh && mesh->Type == PrimitiveMesh::Sphere;
            if (shape.IsSphere)
                shape.Ball = Sphere::FromAABB(shape.Local).Transformed(shape.World);
            shapes.push_back(shape);
        }
        return shapes;
    }

}

// Aylanib turuvchi nurlar yelpig'ichi: har bir nur Geometry::Raycast bilan hamma nishonlarga (aylanuvchi
// qutilar - yo'naltirilgan AABB, sharlar) tekshiriladi; eng yaqin tegish nuqtasi va normal chiziladi.
class GeometryRayFan : public ScriptableEntity
{
public:
    int Rays = 21;
    float Spread = 1.9f;      // radian
    float SweepSpeed = 0.35f; // radian / sekund
    float Range = 16.0f;

    void OnUpdate(Timestep) override
    {
        const std::vector<TargetShape> targets = GatherTargets(*this);
        const glm::vec3 origin = GetWorldPosition();
        const float heading = std::sin(GetTime() * SweepSpeed) * 0.6f;
        int hits = 0;
        for (int i = 0; i < Rays; i++)
        {
            const float a = heading + Spread * ((float)i / (float)std::max(1, Rays - 1) - 0.5f);
            const Ray ray(origin, glm::vec3(std::sin(a), -0.03f, -std::cos(a)));
            std::optional<RayHit> nearest;
            for (const TargetShape& t : targets)
                if (auto hit = t.Raycast(ray, nearest ? nearest->Distance : Range))
                    nearest = hit;
            if (nearest)
            {
                hits++;
                DebugDraw::Line(origin, nearest->Point, DebugColor::Green);
                DebugDraw::Point(nearest->Point, DebugColor::Yellow, 0.0f, true, 7.0f);
                DebugDraw::Arrow(nearest->Point, nearest->Point + nearest->Normal * 0.7f, DebugColor::Cyan, 0.0f, true, 0.15f);
            }
            else
                DebugDraw::Line(origin, ray.GetPoint(Range), glm::vec4(1.0f, 0.3f, 0.3f, 0.55f));
        }
        DebugDraw::WireSphere(origin, 0.3f, DebugColor::Cyan);
        DebugDraw::Text(origin + glm::vec3(0.0f, 1.1f, 0.0f), std::format("Geometry::Raycast\n{} / {} nur tegdi", hits, Rays), DebugColor::Cyan);
    }
};
ZM_REGISTER_SCRIPT(GeometryRayFan, ZM_FIELD_RANGE(GeometryRayFan, Rays, 1, 64).Label("Nurlar soni"),
                   ZM_FIELD_RANGE(GeometryRayFan, Spread, 0.1f, 3.1f).Label("Yoyilish (rad)"),
                   ZM_FIELD(GeometryRayFan, SweepSpeed).Label("Burilish tezligi"), ZM_FIELD(GeometryRayFan, Range).Label("Uzoqlik"))

// Aylanib yuruvchi zond: har nishonning eng yaqin nuqtasigacha chiziq (ClosestPoint), zond sferasi
// tekkan nishonlarning dunyo AABB'si to'q sariq (Geometry::Intersects(Sphere, AABB)).
class GeometryProbe : public ScriptableEntity
{
public:
    float Radius = 1.6f;
    float OrbitRadius = 4.5f;
    float Speed = 0.6f;

    void OnCreate() override { m_Center = Transform().Translation; }
    void OnUpdate(Timestep) override
    {
        const float a = GetTime() * Speed;
        Transform().Translation = m_Center + glm::vec3(std::cos(a) * OrbitRadius, 0.6f * std::sin(a * 2.0f), std::sin(a) * OrbitRadius);
        const glm::vec3 probe = GetWorldPosition();
        const Sphere reach{ probe, Radius };
        int touching = 0;
        for (const TargetShape& t : GatherTargets(*this))
        {
            const glm::vec3 closest = t.ClosestPoint(probe);
            const float distance = glm::length(closest - probe);
            const glm::vec4 color = Color::LerpHSV(glm::vec4(0.3f, 1.0f, 0.4f, 1.0f), glm::vec4(0.6f, 0.4f, 1.0f, 0.6f),
                                                   Math::InverseLerp(0.0f, 9.0f, distance));
            DebugDraw::Line(probe, closest, color);
            DebugDraw::Point(closest, color, 0.0f, true, 5.0f);
            const AABB bounds = t.GetWorldBounds();
            if (Geometry::Intersects(reach, bounds))
            {
                touching++;
                DebugDraw::Bounds(bounds, DebugColor::Orange);
            }
        }
        DebugDraw::WireSphere(probe, Radius, touching > 0 ? DebugColor::Orange : DebugColor::White);
        DebugDraw::Text(probe + glm::vec3(0.0f, Radius + 0.4f, 0.0f), std::format("ClosestPoint / Intersects: {}", touching), DebugColor::White);
    }

private:
    glm::vec3 m_Center{ 0.0f };
};
ZM_REGISTER_SCRIPT(GeometryProbe, ZM_FIELD(GeometryProbe, Radius).Label("Zond radiusi"), ZM_FIELD(GeometryProbe, OrbitRadius).Label("Orbita radiusi"),
                   ZM_FIELD(GeometryProbe, Speed).Label("Tezlik"))

// Hamma nishonlar atrofidagi sfera (Sphere::FromPoints) va qiya aylanuvchi tekislik: har nishonning
// dunyo AABB'si tekislikka nisbatan rangda (Classify: oldida yashil, orqasida ko'k, kesib o'tsa pushti).
class GeometryBoundsView : public ScriptableEntity
{
public:
    float PlaneSpeed = 0.25f;
    float PlaneTilt = 0.45f; // radian

    void OnUpdate(Timestep) override
    {
        const std::vector<TargetShape> targets = GatherTargets(*this);
        std::vector<glm::vec3> corners;
        for (const TargetShape& t : targets)
        {
            const AABB b = t.GetWorldBounds();
            for (int k = 0; k < 8; k++)
                corners.push_back(b.GetCorner(k));
        }
        if (!corners.empty())
        {
            const Sphere all = Sphere::FromPoints(corners);
            DebugDraw::WireSphere(all.Center, all.Radius, glm::vec4(1.0f, 1.0f, 1.0f, 0.35f));
            DebugDraw::Text(all.Center + glm::vec3(0.0f, all.Radius + 0.3f, 0.0f), std::format("Sphere::FromPoints r = {:.2f}", all.Radius),
                            DebugColor::White);
        }

        const glm::vec3 center = GetWorldPosition();
        const float spin = GetTime() * PlaneSpeed;
        const glm::vec3 normal = glm::normalize(glm::vec3(std::sin(PlaneTilt) * std::cos(spin), std::cos(PlaneTilt), std::sin(PlaneTilt) * std::sin(spin)));
        const Plane plane = Plane::FromPointNormal(center, normal);
        for (float r : { 3.0f, 6.0f, 9.0f })
            DebugDraw::Circle(center, normal, r, glm::vec4(0.9f, 0.9f, 0.3f, 0.5f));
        DebugDraw::Arrow(center, center + normal * 2.0f, DebugColor::Yellow, 0.0f, true, 0.3f);
        for (const TargetShape& t : targets)
        {
            const AABB b = t.GetWorldBounds();
            const PlaneSide side = Geometry::Classify(b, plane);
            DebugDraw::Bounds(b.Expanded(0.05f), side == PlaneSide::Front  ? DebugColor::Green
                                                 : side == PlaneSide::Back ? glm::vec4(0.3f, 0.5f, 1.0f, 1.0f)
                                                                           : DebugColor::Magenta);
        }
    }
};
ZM_REGISTER_SCRIPT(GeometryBoundsView, ZM_FIELD(GeometryBoundsView, PlaneSpeed).Label("Tekislik tezligi"),
                   ZM_FIELD_RANGE(GeometryBoundsView, PlaneTilt, 0.0f, 1.5f).Label("Tekislik qiyaligi"))
